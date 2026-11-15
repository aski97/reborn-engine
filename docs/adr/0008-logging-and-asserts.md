# 0008 — Logging and asserts

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
The engine needs diagnostics before window lifecycle and input: every new event should be visible
while it is developed. Logs are read by people (console) and by LLMs and scripts (a file). The
logger must work from the first line of `main()`, inside OS callbacks (`WndProc`) and in tests.
There are no threads and no allocator yet (roadmap §6).

## Options
**A. Where the state lives**
1. One global, file-local state, zero-initialized before `main()` — callable from anywhere,
   works before startup; an exception to ADR 0006's explicit state, and tests must clean up.
2. Logger passed to every function — consistent with ADR 0006; every signature grows, and
   containers or OS callbacks cannot log.
3. Global pointer to a registered instance — allows several instances; still a global, plus an
   indirection.

**B. API shape**
1. printf-style macros over a variadic function — familiar; macros capture `__FILE__`,
   `__LINE__`, `__func__`; the compiler checks formats through `format(printf)`.
2. Key-value API — ideal for JSON; C varargs carry no types, so every field needs a type tag.

**C. Outputs**
1. List of sinks (function pointer + `user` pointer, fixed array) — console, file, test
   capture, a future in-game console are all the same thing.
2. Fixed console and file outputs — less code; every new output edits the logger, tests cannot
   capture.

**D. File format**
1. JSON lines only; people read the console.
2. Text file and JSON lines — one more output.

**E. Console colors**
1. ANSI sequences everywhere, enabled on Windows with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` —
   one code path.
2. `SetConsoleTextAttribute` on Windows, ANSI on Linux — two implementations.

**F. Level filtering**
1. Compile-time minimum plus a runtime level per sink — TRACE/DEBUG cost nothing in release;
   arguments of removed calls are not evaluated.
2. Runtime only — simpler; a comparison per call in release.

**G. Asserts**
1. Debug only, like `assert` — free in release; release bugs go unchecked.
2. Always on — safer; costs in hot loops.
3. Two macros (debug only, always on) — added when a real need appears.

**H. Timestamp**
1. `timespec_get` (C11) — no platform code; wall clock, may jump.
2. Platform monotonic timer — the right clock; pulls part of roadmap §4 forward.

## Decision
**A1, B1, C1, D1, E1, F1, G1, H1.**
- **A1:** logging is cross-cutting and must work before any system exists, so it is the one
  global. The zero state is a valid configuration: with no sink registered (before startup, after
  shutdown) records go to the console, so nothing is lost.
- **B1:** `RB_LOG_INFO("Window %ux%u", w, h)`. The format string is part of `__VA_ARGS__`:
  with `(fmt, ...)`, a call with only a format leaves `...` empty, which C17 forbids.
  `RB_PRINTF_FORMAT` wraps `__attribute__((format(printf)))`.
- **C1:** at most `RB_LOG_MAX_SINKS` (8), identified by the `(fn, user)` pair. Startup adds the
  console and file sinks; others (tests) can be added at any time.
- **D1:** `{"t":…,"ts":…,"level":…,"file":…,"line":…,"func":…,"msg":…}` per line. A line that does not
  fit the buffer replaces the message with a notice rather than emitting broken JSON.
- **E1:** colors only when stderr is a terminal (`GetConsoleMode` / `isatty`), not under `NO_COLOR`
  nor `TERM=dumb`, so captured output (ctest, files) stays plain.
- **F1:** `RB_LOG_MIN_LEVEL` is TRACE in debug and INFO in release, overridable. Removed calls are
  `if (constant) …`, folded away but still type-checked, so variables used only in logs do not
  become unused.
- **G1:** `RB_ASSERT(cond)` and `RB_ASSERT_MSG(cond, fmt, ...)`. On failure: FATAL log with the
  condition's text, a breakpoint if a debugger is attached, otherwise `abort()` (no `atexit`
  handlers on broken state; the OS records a crash). `RB_ASSERT_ENABLED` is overridable; a
  disabled condition is not evaluated but still compiles. FATAL itself is only a level: it never
  stops the program. Asserts are for bugs in the calling code only: required pointers at public
  entry points (the platform API asserts its `state`, `desc` and `out` parameters) and invariants
  whose violation would silently corrupt state. Failures caused by input or the environment are
  logged and returned, never asserted.
- **H1:** `t` is seconds since the logger's first use: durations read at a glance. The clock can
  move to the §4 timer later without changing the record. Absolute time, needed to match other
  logs (crash dumps, OS event logs, bug reports), comes from two additions:
  - `rb_log_startup` logs one anchor line, `Log clock: t = 0 at 2026-11-15T19:32:10.123+01:00`
    (ISO 8601 local time with its UTC offset): any line's absolute time is the anchor plus its
    `t`. Converting to local time once, at startup and single-threaded, allows the standard
    `localtime`; per-line local time on the console would need the per-OS thread-safe variants
    (`localtime_r` / `localtime_s`) and would hide durations.
  - Each JSON line carries `"ts"`, milliseconds since 1970 UTC: no conversion at all, and scripts
    or LLMs need no arithmetic.

Further choices:
- **Formatting:** `vsnprintf` into a static 2 KiB buffer, no allocation. Longer messages are cut at
  a UTF-8 character boundary and end with `...`.
- **Console stream:** every level goes to stderr: unbuffered, so order is kept and a crash loses
  nothing; stdout stays free for program output.
- **UTF-8 on the Windows console:** when stderr is a console, lines are converted to UTF-16 and
  written with `WriteConsoleW`. Rejected: `SetConsoleOutputCP(CP_UTF8)`, which changes the code
  page of the console the process was started from, also after it exits. Redirected output is
  written as UTF-8 bytes.
- **File:** opened in binary mode (same bytes on every OS), overwritten at startup, flushed after
  every line so a crash cannot lose the line that explains it.
- **Paths:** `-fmacro-prefix-map=<repo>/=` makes `__FILE__` relative to the repository (shorter, no
  home directory in logs); debug info keeps full paths. Output always uses `/`.
- **Re-entrancy:** a sink that logs is ignored (it would overwrite the shared buffers and recurse).
- **Threads:** not thread-safe; a mutex comes with threading (backlog).
- **No categories** (`platform`, `renderer`): `file:line` is enough until a subsystem gets noisy.
- **Standard C functions on Windows:** `_CRT_SECURE_NO_WARNINGS` silences the CRT's deprecation of
  `fopen`/`getenv` in favour of Annex K `_s` functions, which glibc does not provide.
- **Window handles in logs:** hexadecimal, so generation (top 3 digits) and slot (bottom 5) can be
  read off.

### File layout
```
engine/include/reborn/log.h        public: levels, sinks, lifecycle, RB_LOG_* macros
engine/include/reborn/assert.h     public: RB_ASSERT, RB_ASSERT_MSG
engine/src/core/log.c              global state, formatting, built-in sinks
engine/src/core/log_internal.h     console and JSON formatters, exposed for tests
engine/src/core/assert.c           failure path
engine/src/platform/platform_os.h  stateless OS services: console, debugger detection
engine/src/platform/win32/win32_os.c
engine/src/platform/linux/linux_os.c
```
The OS services take no platform state: the logger needs them before `rb_platform_startup`. The
Linux file is `linux_`, not `xlib_`: it uses POSIX and `/proc`, nothing from X11.

## Consequences
- Any engine or game code can log with one include; systems report failures instead of returning
  a bare `false` (window creation, X display, Win32 errors with `FormatMessageW`).
- Event queue overflow is logged: one warning per burst, not one per dropped event.
- Tests can capture records with a sink, but must remove it: the state is shared by the process.
- A failing assert cannot be tested yet: it needs a replaceable handler (backlog). In Windows
  debug builds `abort()` opens the debug CRT's dialog; under ctest that test would hang until the
  timeout.

### When to revisit
| Trigger | Change | Impact |
|---|---|---|
| Threads (backlog) | Mutex around delivery, or a queue drained by a logging thread | Internal |
| Game loop timer (§4) | `t` from the monotonic timer | None for readers |
| A subsystem floods the log | Categories with their own level | New macro argument or per-file define |
| Logging shows up in profiles | Batch file writes, flush only from WARN | File may lose last lines on a crash |
| Testing assert failures | Replaceable assert handler | New function in `assert.h` |
