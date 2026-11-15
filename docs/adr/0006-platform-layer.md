# 0006 — Platform layer

- **Status:** Proposed
- **Date:** 2026-11-15

## Context
The engine needs windows, events and input on Windows and Linux, without OS headers leaking into
engine or game code. The renderer will later need a Vulkan surface per window. Multiple windows
are not needed for v0.1 but must not require an API break later.

## Options
**A. Selecting the backend**
1. One source file per OS, chosen by CMake — zero runtime cost, keeps OS code in
   `platform/<os>/`; one backend per OS in a binary.
2. `#ifdef` inside shared files — fewer files; unreadable as backends grow.
3. Runtime table of function pointers (vtable) — several backends in one binary (X11 + Wayland,
   a null backend); indirect calls, more code.

**B. Who provides system state memory**
1. Hidden `static` state allocated by the system itself — simplest; the engine cannot control
   where the memory comes from.
2. Size query + startup: the system reports the size of its state, the engine allocates it and
   passes it to `startup` — the engine owns all memory, fits arenas (roadmap §6); one more call
   per system.
3. Allocator passed in: `startup(allocator, cfg)` and the system takes its memory from it — one
   call, the engine still chooses the source; needs an allocator interface (§6 does not exist
   yet), and the total size is no longer known before allocating.

**C. Referring to a window**
1. Opaque pointer `rb_window *` (SDL, GLFW) — familiar, simple; a pointer to a destroyed window
   is undefined behaviour and may silently hit a new window reusing the memory.
2. Public struct with one opaque pointer per subsystem — everything about a
   window in one place; couples the window header to every subsystem, same dangling problem.
3. Generational handle `typedef struct rb_window { u32 id; } rb_window;` (index + generation into
   a fixed pool) — stale handles are detected, no allocation per window, serializable (LLM debug
   API), each subsystem keeps per-window data in its own array indexed by the same slot; a pool to
   write, a lookup per call, a fixed maximum count.

**D. Delivering events**
1. Registered callbacks — mirrors Win32's `WndProc`; user code runs inside the OS pump
   (re-entrancy, ordering).
2. Polled queue (`rb_platform_pump()` then `while (rb_event_poll(&ev))`, as SDL) — explicit flow,
   deterministic, easy to log and test; needs a buffer.
3. State only (`rb_key_down()`) — convenient; loses ordering and press+release within a frame.

**E. Linux backend**
1. Xlib — one library (`libX11`), well documented, keyboard handling built in, used by GLFW;
   synchronous and old, X11-only.
2. Xlib + XCB — asynchronous XCB for windows and events, Xlib for the connection and
   keyboard; three libraries and two APIs mixed.
3. XCB + `libxkbcommon` — modern X11 stack; verbose, extra dependency for the keyboard.
4. Wayland — default session on current distros; needs `wayland-scanner` generated protocol code,
   `libxkbcommon`, and client-side decorations (GNOME draws none). Much more work.

**F. Key codes**
1. Own enum by physical position (scancode) — WASD stays in place on every layout; names do not
   match printed letters on non-QWERTY layouts.
2. Own enum by character (virtual key) — names match the letter; game bindings scatter on
   AZERTY/QWERTZ.

## Decision
**A1, B2 (then B3 with §6), C3, D2, E1, F1.**
- **A1:** the public API is one header; CMake compiles `platform/win32/` or `platform/linux/`.
  If Linux ever needs two backends, the vtable lives inside the Linux backend only (A3 locally).
- **B2:** systems never allocate their own state; `rb_engine` owns every system's memory.
  The size query is a separate function rather than a `NULL` argument, so each function does one
  thing: `rb_platform_state_size()`, then `rb_platform_startup(state)`. Until §6 exists, the
  caller allocates with `malloc`. Once the allocator interface exists, systems move to **B3**:
  one call, and systems that allocate at runtime (renderer resources) need an allocator anyway.
- **C3:** the API stays object-like (`rb_window_set_title(win, ...)`), the handle plays the role
  of `this`. The zero handle is invalid, so a zero-initialized `rb_window` is never a live window.
  The pool is a generic container (ADR 0007), reused for renderer resources.
- **D2:** one queue per platform, not per window: Win32 queues messages per thread and X11 per
  connection, so every event carries the window it refers to. Fixed-size ring buffer, no
  allocation. A per-frame input state (`rb_key_down`) is derived from the events. Text input will
  be a separate event, later.
- **E1:** least dependencies and one API to learn. XCB's asynchrony matters mostly at window
  creation, not per frame. Wayland desktops run it through XWayland. `libX11` (`libx11-dev`) is
  approved as a system dependency.
- **F1:** game input is about position; text input is handled separately.

Further choices:
- **Public header** `reborn/platform.h`: the sandbox drives the platform until the application
  layer (§4) owns the window; whether to make it internal is decided there.
- **Queue overflow:** the queue is sized so it never fills in practice (a few hundred events) and
  consecutive mouse moves are merged. If it fills anyway, new events are dropped and counted:
  a full queue means a stalled frame. Dropping any event can leave a key stuck, so the policy is
  revisited if overflow is ever observed.
- **DPI:** the process declares Per-Monitor V2 awareness at startup
  (`SetProcessDpiAwarenessContext`), otherwise Windows bitmap-scales the window and it looks blurry.
- **State passed explicitly** to every call (`rb_window_create(platform, &desc)`), never kept in
  a hidden global: consistent with B2, allows several instances in tests. The application layer
  (§4) will hold it, so game code never sees it.
- **Close is a request:** the close button produces `RB_EVENT_WINDOW_CLOSE_REQUESTED` and the
  window stays alive until the application destroys it (as Win32's `WM_CLOSE`); a handle never
  becomes invalid behind its owner's back. On X11 each window registers `WM_DELETE_WINDOW`, so
  the window manager sends a `ClientMessage` instead of killing the connection.
- **Events from the first step:** the queue exists from the first backend with a single event
  type, instead of a temporary `should_close` flag to be thrown away; resize, keys and mouse add
  event types with their payload in a union selected by `type`.
- **Sizes in logical units:** `rb_window_desc` sizes are scaled by the monitor's DPI (1280×720 at
  150% is 1920×1080 physical pixels), so a window looks the same size on every monitor. The
  renderer will query the size in physical pixels. Windows moved to a monitor with another scale
  are resized by `WM_DPICHANGED`. The X11 core protocol has no per-monitor scale: on Linux
  logical units are pixels. Sizes above 32767 are rejected there (16-bit protocol fields).
- **Strings:** every engine string is UTF-8 `const char *`, as on Linux. The Win32 backend converts
  to UTF-16 (`MultiByteToWideChar`) and calls the `-W` functions. Rejected: UTF-16 in the API
  (`wchar_t` is 2 bytes on Windows, 4 on Linux) and `-A` functions with a UTF-8 manifest (a hidden
  setting that silently breaks if missing, not honoured by every API). The Xlib backend writes
  titles to `_NET_WM_NAME` and `WM_NAME`, both typed `UTF8_STRING`. Rejected: `XStoreName`
  (types them Latin-1) and `Xutf8SetWMProperties` (converts through the process locale).

### File layout
```
engine/include/reborn/platform.h          public API
engine/src/platform/platform_internal.h   rb_platform_common + helpers shared by backends
engine/src/platform/platform.c            OS-independent: event queue, key state, window pool
engine/src/platform/win32/win32_platform.c
engine/src/platform/linux/xlib_platform.c
```
- Each backend's `struct rb_platform_state` embeds `rb_platform_common` as a member, so the event
  queue and input state are written once. `platform.c` includes no OS header.
- Backend files carry the backend prefix: three `platform.c` open in an editor or a debugger
  cannot be told apart. One file per backend; split it when it grows.
- CMake picks the backend source per OS. Windows links `user32` and `gdi32` explicitly: CMake's
  default library list already contains them, but that list is a toolchain convenience that can
  change, while an explicit link documents the dependency. Linux uses `find_package(X11 REQUIRED)`
  and links `X11::X11`.

## Consequences
- No OS header ever appears in `engine/include/`; the renderer asks the platform for a Vulkan
  surface (`VK_KHR_win32_surface` / `VK_KHR_xlib_surface`).
- Using a destroyed window is a detectable error instead of undefined behaviour.
- The maximum number of windows is a compile-time constant.
- Pure parts (ring buffer, handle pool, key mapping) are unit-tested; windows and events are
  tested once logging and the game lifecycle exist (§4, §5).
- Linux needs `libx11-dev` in `docker/linux.Dockerfile`. The container has no X server, so the
  Linux scripts gate on `ctest` and no longer run the sandbox; `-Window` runs it through WSLg's
  X socket. Rejected: Xvfb in the image (new package, and the sandbox cannot close by itself
  yet: revisit with integration tests) and a sandbox exiting 0 without a display (turns a failure
  into success).

### When to revisit
| Trigger | Change | Impact |
|---|---|---|
| Allocator interface available (§6) | System startup takes an allocator (B3) | Every system's startup signature; a new ADR |
| XWayland not good enough (fractional scaling blur, HDR, a distro dropping it) | Add a Wayland backend next to Xlib, chosen at runtime inside `platform/linux/` | Public API unchanged; adds `wayland-client` + `libxkbcommon` (new approval) |
| Xlib's synchronous round-trips become measurable | Move window/event code to XCB (E2) | Linux backend only |
| More windows than the pool allows | Raise the constant | None |
| Events lost in bursts | Grow the ring buffer or change the overflow policy | None for users |
| Another OS | New `platform/<os>/` directory | None for users |
