# 0022 — Command system

- **Status:** Accepted
- **Date:** 2026-11-24

## Context
An LLM agent should play and check Blink on its own, on every platform, without clicking by hand.
Today it can only start the game (under Xvfb on Linux), click at blind coordinates with `xdotool`
and read the log: it cannot ask where the square is, stop the game or advance it by an exact
number of steps, and none of it works on Windows.

The classic answer is a console: every action is a named command with arguments (`map e1m1`),
reachable from a startup file (`autoexec.cfg`), from a remote connection (`rcon`) and from an
in-game console. This ADR covers the commands, the startup script and the remote channel; the
in-game console comes with the debug UI, input injection and screenshots are the next steps.

Constraints:
- The agent runs each shell command as a separate process: it cannot keep a game's stdin open
  between two calls.
- There are no threads yet: whatever reads the channel must not block the game loop.
- The same commands work on Windows and Linux, with or without a display.

## Options
**A. Live channel**
1. TCP on `127.0.0.1`, non-blocking, polled once per frame, plus a small client executable — the
   same Berkeley sockets API on both systems (Winsock on Windows), no thread, any later process
   can connect; one more executable, and any local process can connect (only when enabled).
2. stdin — the agent cannot hold it open across calls; non-blocking reads on Windows need a thread.
3. Named pipes (Windows) and Unix sockets (Linux) — two APIs, two implementations.
4. A mailbox file the game polls — races on half-written files.

**B. How the script and the channel are enabled**
1. Command-line options (`--script <file>`, `--listen <port>`), `argc`/`argv` passed to
   `rb_app_run` — visible in the command that starts the game; changes `rb_app_run`'s signature.
2. Environment variables — no API change; invisible, easy to leave set by mistake.
3. Fields of `rb_app_config` only — the agent would have to rebuild the game.

**C. Format**
1. Text lines in, shell-like (`step 10`, double quotes for spaces); JSON lines out — easy to type
   in a script or a terminal, no JSON parser needed.
2. JSON in and out — rigid to type, needs a parser.

**D. Arguments**
1. `count`/`values` like `main`, helpers that parse a number and report the error, a usage string
   registered with the command; the engine checks the argument count.
2. A typed signature declared at registration (`"step <count:u32>"`) — a mini-language to parse.

**E. Result**
1. A reply object with typed functions (`rb_command_reply_u64(reply, "step", 1244)`), nested
   objects, an error with a message — always valid JSON, escaping done once by the engine.
2. The handler formats JSON itself with `snprintf` — quoting and escaping easy to get wrong.

**F. Registry**
1. A fixed-capacity table in the engine, linear search by name — no container exists, and a few
   dozen names cost nothing to scan.
2. A hash map — not needed at this size; considered again when the hash map exists.

**G. When commands run, pause**
1. Once per frame, before the steps, on the main thread; pause and step owned by the engine.
2. Inside every step — no steps run while paused, so the command that resumes would never be read.

## Decision
**A1, B1, C1, D1, E1, F1, G1.**

- **API** (`reborn/command.h`, registration in `reborn/app.h`):
  - `rb_command_desc`: `name`, `usage` (the arguments, for `help`), `summary`, `min_args`,
    `max_args`, `fn`. The strings must outlive the run (literals).
  - `void fn(void *state, rb_app *app, const rb_command_args *args, rb_command_reply *reply)`:
    `state` is the game's, as in the other callbacks; `args` holds the words after the name.
  - `rb_app_register_command(app, &desc)`, from `init` on. Names are lowercase letters, digits,
    `_` and `.`, at most 31 characters; game commands carry a prefix with a dot (`blink.state`),
    so a later engine command never clashes with them. A bad or duplicate name, or a full table
    (64 commands), is logged and returns false.
  - `rb_command_arg_u32` / `_u64` / `_f32`: parse one argument; on failure they write the error
    into the reply and return false.
  - `rb_command_reply_bool` / `_i64` / `_u64` / `_f32` / `_f64` / `_str`, `_begin_object` /
    `_end_object` (8 levels), `_error(fmt, ...)`. An error drops the fields. Floats are written
    with the fewest digits that read back as the same value (`0.1f` is `0.1`, not the
    `0.10000000149011612` of the float widened to a double); NaN and infinities as `null`.
- **Line syntax:** words split by spaces and tabs; double quotes group words, with `\"` and `\\`
  inside them; a word starting with `#` starts a comment; empty lines are skipped. At most 1023
  bytes and 16 words.
- **Reply** (one JSON line):
  `{"id":7,"ok":true,"from":"socket","line":"step 10","result":{"step":1244}}`, or `"ok":false`
  and `"error":"..."` instead of `result`. `id` counts commands since startup, `from` is `socket`
  or `script:<line number>`. `id` and `ok` always come first, so a client can check the outcome
  without a JSON parser (inside a JSON string every `"` is escaped: `,"ok":true` cannot appear in
  `line`). At most 16 KiB; a longer result becomes an error.
- **Command line:** `rb_app_run(&callbacks, argc, argv)`. The engine knows `--script <file>`,
  `--listen <port>` and `--help`; anything else fails the run, so a typo does not go unnoticed.
  `--listen 0` lets the system pick the port (logged).
- **Script:** read whole at startup (a missing file fails the run). Each frame runs its lines
  until a `wait <steps>` or its end; replies go to stdout (the log is on stderr and in its file),
  so `blink --script s.txt > replies.jsonl` captures them. A failed command does not stop the
  script. At its end the game keeps running: a script that should end the run says `quit`.
- **Socket:** listens on `127.0.0.1` only, one client at a time (others wait in the backlog), lines
  ended by `\n` (a `\r` before it is dropped). Lines received in a frame all run in that frame.
  Pitfalls handled: on Linux a send to a closed connection raises `SIGPIPE`, which kills the
  process (`MSG_NOSIGNAL`); a restarted game must reuse its port while the old connection is in
  `TIME_WAIT` (`SO_REUSEADDR` on Linux; on Windows that option lets another process steal the port,
  so `SO_EXCLUSIVEADDRUSE` instead); accepted sockets inherit non-blocking mode on Windows, not on
  Linux; Nagle's algorithm would hold short replies back (`TCP_NODELAY`). A reply to a client that
  does not read for a second drops the client, so it cannot freeze the game.
- **Client:** `reborn-cmd <port> <line>...` (`tools/reborn-cmd/`, option `REBORN_BUILD_TOOLS`):
  sends each line, prints each reply, exits 0 when all succeeded, 1 when one failed, 2 when the
  game cannot be reached or does not answer within 60 s. It uses the engine's platform sockets.
- **Pause:** owned by the engine. While paused, every frame pumps the window and delivers events
  to `on_event` (the window stays responsive and can be closed) but runs no `update`; the step
  clock drops the time that passes (no burst on resume) and `render` keeps drawing the same
  state. Accessors `rb_app_is_paused` and `rb_app_step_index` let a game show it.
- **Engine commands:** `help [name]`, `status` (frame, step, paused, game time, update rate,
  window size), `pause`, `resume`, `step [count]` (pauses, runs `count` steps right away, 1 by
  default, at most 100000, then replies with the step reached; the frame arena is reset before
  each step), `quit`, `wait <steps>` (scripts only; refused while paused, where it would never end).
- **Blink:** `blink.state` (phase, score, lives, best, the square in world units and in window
  pixels) and `blink.restart <seed>` (a new session on the title with that seed). The pixel
  rectangle needs `rb_camera2d_world_to_screen`, the inverse of `rb_camera2d_screen_to_world`.
- **Sandbox:** `sandbox.clear [<r> <g> <b>]` fixes the clear color (no arguments: back to the
  turning hue); a panel at the bottom shows the step, running or paused, a ticker that moves one
  cell per step and the last `sandbox.clear`. The sandbox's animations move on game time (steps
  plus alpha), so the pause freezes them, except the clear color, on the wall clock, which shows
  that frames keep coming.
- **Code:** `engine/src/command/` (line splitting, registry and reply, script reader, socket
  server), the engine commands in `engine/src/app/`, sockets in `platform/platform_socket.h`
  with `win32_socket.c` and `linux_socket.c`. The logger's line writer moves to
  `core/text_writer.h`, shared with the replies.

## Consequences
- An agent can stop Blink, read where the square is and advance it step by step, on Windows and
  Linux alike; with the seed of `blink.restart`, a session is reproducible.
- Every later feature adds its own commands (input injection, screenshots, scene save and load).
- `rb_app_run` takes `argc` and `argv`: every game's `main` changes. Command-line arguments on
  Windows arrive in the ANSI code page, not UTF-8: non-ASCII script paths may fail (a wide-char
  entry point is part of the engine-provided entry point in the backlog).
- Keys pressed while paused reach the game as events, but not through `rb_key_was_pressed`: the
  next step's pump clears the edges. Input injection must land inside the pump of the step that
  should see it.
- Anyone on the machine can send commands while `--listen` is on; it is off unless asked for.
- The registry is a linear scan of at most 64 names; a hash map can replace it once one exists
  (planned for v0.3.0).
- Script replies on Windows end in `\r\n`: the C runtime's stdout is in text mode. JSON parsers
  take the `\r` as whitespace.
