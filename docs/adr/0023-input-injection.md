# 0023 — Input injection

- **Status:** Accepted
- **Date:** 2026-11-26

## Context
With the command system (ADR 0022) an agent can read Blink's state and step it, but not play it:
the only way to click is `xdotool`, Linux only, which needs a display and a focused window, moves
the real cursor, and lands at no particular step. Agents on Windows run on the owner's desktop,
where the owner keeps using mouse and keyboard.

Input injection is synthetic input: key and mouse events the engine treats as if a device had sent
them, the same on every platform and without a display.

Constraints:
- Commands run once per frame, before the steps; each step's pump first clears the edges
  (`rb_key_was_pressed`), so input applied by a command is gone before the update reads it (ADR
  0022, *Consequences*).
- On Win32 the pump releases a Shift whose real key is up (both Shifts held, one released, send one
  key-up): it would release an injected Shift at every step.
- Focus loss releases every held key and button (ADR 0006).

## Options
**A. Where injected input enters**
1. The common input functions the backends call (`platform/input.c`) — same state, same events,
   same merges as real input; no per-OS code, no display; skips only the OS-to-engine key mapping,
   which has its own tests.
2. The OS (`SendInput`, XTest) — tests the backends too; two implementations, needs a display and
   the focus, XTest is another library, moves the real cursor.
3. Events only, without the state — `rb_key_is_down` would contradict the events.

**B. When the game sees it**
1. Queued, applied by the pump of the next step, after the OS's messages; held while paused until a
   step runs. The reply names that step.
2. Applied by the command — the next pump clears its edges.

**C. Level of the commands**
1. Low-level (`key`, `button`, `mouse`, `wheel`) plus `tap` and `click` (press in the next step,
   release in the one after: held for one step), and a query of the input state.
2. Low-level only — the agent composes taps with `step`.

**D. Several injections in one frame**
1. All go to the next step (a tap's release to the one after); `step` and `wait` space them out.
2. A timeline that puts each after the previous one — less obvious rules.

**E. Names**
1. Unprefixed, like the engine's commands so far (`tap`, `click`).
2. Grouped by subsystem (`input.tap`) — room for the commands to come (screenshots, scenes,
   console variables, the editor) without clashes on short words (`move`, `set`, `select`), one
   group per subsystem for `help` and a console's completion; decided now, while renaming breaks
   nothing. Changes the rule of ADR 0022 that engine names have no dot; a game whose prefix is a
   group's name is possible, a clash on a full name is refused at registration (logged).

**F. Real and injected input together**
1. Always mixed.
2. Real input can be turned off — an agent on the owner's desktop is not disturbed by the owner's
   mouse and clicks; the first piece of "where input comes from", which record and replay will
   need. Window events still pass (the window can be closed).

## Decision
**A1, B1, C1, D1, E2, F2** (as a command).

- **Group rule** (replaces ADR 0022's): the commands that drive the run stay unprefixed (`help`,
  `status`, `pause`, `resume`, `step`, `quit`, `wait`); every other engine command lives in its
  subsystem's group (`input.*` here; later `screen.*`, `scene.*`...). Games keep their own prefix.
- **Commands:**
  - `input.key <key> <down|up>`, `input.tap <key>`;
  - `input.button <button> <down|up> [<x> <y>]`, `input.click <x> <y> [button]` (a move to the
    point, then the press, the release one step later);
  - `input.mouse <x> <y>` (a move), `input.wheel <dy> [dx]` (notches, positive up and right);
  - `input.real [on|off]` (no argument: the current value);
  - `input.state`: keys and buttons down, each `real` or `injected`; the mouse position and
    whether it is in the main window; real input on or off; injections waiting; the next step.
- **Names:** keys as `rb_key_name` spells them, any case, a space or `_` alike (`space`, `a`,
  `f1`, `left_ctrl`, `keypad_1`); buttons `left`, `right`, `middle`, `x1`, `x2`. Positions in the
  main window's pixels, as `blink.state` gives them; any decimal rounds to the nearest pixel.
- **Timing:** an injection waits in a queue (`app/input_queue.c`, 64 entries) tagged with its step.
  `rb_app_step` pumps, applies the entries due, then delivers the events: the update sees them
  through events and queries. The pump of a paused frame applies nothing. Replies give the step
  (the count `status` reports once it has run): `{"step":1245}`, plus `"release_step"` for `tap`
  and `click`. A full queue is a command error.
- **Injected keys and buttons are marked** (a flag per key and button in the input state, cleared
  when they go up): focus loss, an X keyboard grab and Win32's stuck-Shift fix release only real
  ones. A real press or release of a marked key takes it over.
- **Real input off:** the backends' reports of keys, buttons, moves and wheel are dropped in the
  common code; window events (focus, resize, close) pass, and so do the OS's own shortcuts (Alt+F4).
  Turning it off releases at once whatever real input is held (with events). Logged as a warning;
  shown by `input.state` and in the sandbox.
- **Code:** `rb_platform_common_inject_*` and `rb_platform_common_set_real_input` next to the
  backends' entry points (`platform/input.c`), `rb_key_from_name` and `rb_mouse_button_from_name`
  there too; the queue in `app/input_queue.c`, the commands in `app/app_input.c`. One public
  accessor, `rb_app_real_input`, for a game to show that real input is off (the sandbox does);
  otherwise a game sees injected input exactly as real input.

## Consequences
- An agent plays Blink on Windows and Linux alike, with no display tools, at known steps; with a
  seed (`blink.restart`), a session plays the same every time.
- Injected input skips the OS key mapping and the window system: a bug there needs a real keyboard
  (or `xdotool`) to show up.
- Record and replay can feed the same queue, with real input off.
- With real input on, a real press or release of a key the agent holds takes it over.
- Turning real input off releases held real input immediately, between two pumps: the game gets the
  release events, but `rb_key_was_released` does not report them.
