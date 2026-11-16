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

**G. Window lifecycle: what the API offers**
1. Events only (resize, focus, minimize) — smallest; the renderer still needs the size when it
   creates the swapchain.
2. Events + queries (`rb_window_get_size`, `rb_window_is_focused`, `rb_window_is_minimized`),
   answering as of the last pump.
3. 2 + commands (`set_size`, `minimize`, `focus`) — nothing needs them before fullscreen and video
   options.

**H. Shape of lifecycle events**
1. One type per transition (`RESIZED`, `FOCUS_GAINED`/`LOST`, `MINIMIZED`/`RESTORED`) — a `switch`
   handles them; only resize has a payload.
2. One `WINDOW_STATE_CHANGED` with a snapshot — fewer types; every handler diffs it against the
   previous state.

**I. Units of a resize**
1. One event with logical size and pixel size — a DPI change alters pixels only and still fires;
   on Linux both are equal.
2. Two events (logical, pixel), as SDL3 and GLFW — precise; two events per resize.
3. Pixels only, plus a scale query.

**J. Size of a minimized window** (Win32 reports 0×0, X11 keeps the size, Vulkan rejects a 0×0
swapchain)
1. Size kept while minimized; only `MINIMIZED` is sent — same on both OSes, the renderer skips
   frames.
2. Report 0×0 as Win32 does — Linux differs, every user handles zero.

**K. Filtering redundant OS messages** (Win32 sends `WM_SIZE` with an unchanged size, X11
`ConfigureNotify` for moves)
1. Per-window state in `rb_platform_common`; backends report what the OS says, the common code
   compares and queues an event only on a change — written once, unit-testable, shared with the
   queries.
2. Each backend filters by itself — duplicated, not testable without windows.

**L. Bursts of resizes** (one per mouse move while dragging a border: hundreds, the queue holds 256)
1. Merge a resize into the newest queued event when that is a resize of the same window — keeps
   ordering, the mechanism planned for mouse moves; the ring needs newest-element access.
2. Send the resize only at the end of the drag (`WM_EXITSIZEMOVE`) — Win32 only.

**M. Win32 modal loop** (while dragging or resizing, `rb_platform_pump` does not return)
1. Keep it as a known limit until the game loop (§4).
2. A `WM_TIMER` that runs game code inside the modal loop — engine code inside an OS callback.
3. A window thread — needs threading (backlog).

**N. Minimized on X11** (no such event: the window manager reports it)
1. ICCCM `WM_STATE` property (`IconicState`), read on `PropertyNotify` — oldest, most widely set.
2. EWMH `_NET_WM_STATE_HIDDEN` — newer; a list of atoms to parse.
3. `UnmapNotify`/`MapNotify` — compositing window managers may keep minimized windows mapped.

**O. Values of the key enum** (F1: keys are physical positions; which numbers name them)
1. USB HID usage IDs (keyboard page, as SDL) — a published standard gives the key list and names;
   one `bool[256]` covers every key; values are sparse.
2. Own dense enum (0, 1, 2…) — smaller arrays; an arbitrary order to maintain by hand.

**P. Linux: X11 keycode to key**
1. Fixed table: on Xorg, XWayland, Xvfb and WSLg a keycode is the kernel's evdev code + 8 —
   one array; wrong on servers with another numbering (some VNC servers, XQuartz).
2. XKB key names read at startup (`AC01` is the QWERTY "A" position; XKB ships in `libX11`) —
   works with any numbering; more code and another API.

**Q. Auto-repeat** (held keys are re-sent by the OS after a delay)
1. `KEY_PRESSED` with a `repeat` flag — games skip repeats, menus use them. X11 needs
   `XkbSetDetectableAutoRepeat`, otherwise every repeat is a fake release + press pair.
2. Repeats dropped — simpler; menus would reimplement them.

**R. Modifiers in events**
1. Bit mask (Shift, Ctrl, Alt, Super) in key and button events, as at that event — shortcuts
   (Ctrl+S) read it directly.
2. None: query the key state — the state is as of the last pump, possibly newer than the event.

**S. Units of the mouse position**
1. Pixels of the client area, as the renderer draws — picking compares against what is drawn.
2. Logical units, as `rb_window_desc`.
3. Both, as `rb_window_size`.

**T. Focus lost while keys are held** (Alt+Tab: Alt's release goes to the other window)
1. Synthesized `KEY_RELEASED` / `MOUSE_BUTTON_RELEASED` for everything held, then `FOCUS_LOST` —
   event consumers see the release too.
2. State cleared silently — event consumers keep a stuck key.

**U. Input state queries**
1. `is_down` (now), `was_pressed` / `was_released` (during the last pump), counting transitions:
   a press and release within one pump gives both — the flaw that rejected D3.
2. `is_down` only.

## Decision
**A1, B2 (then B3 with §6), C3, D2, E1, F1; window lifecycle: G2, H1, I1, J1, K1, L1, M1, N1;
input: O1, P1, Q1, R1, S1, T1, U1.**
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
  revisited if overflow is ever observed. Each burst of drops logs one warning (ADR 0008).
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

Window lifecycle:
- **G2, H1, I1:** queries answer "how is the window now", events "what changed"; the renderer
  needs the former at swapchain creation and the latter to recreate it. `rb_window_size` holds
  logical and pixel size and is both the event payload (`ev.resize`) and the query result.
- **J1:** a minimized window keeps its size, so no consumer ever sees zero from minimizing.
  Maximizing is not a state: for the game it is a resize. Win32 can still report 0 pixels for a
  window shrunk to its title bar; the renderer skips such frames like minimized ones.
- **K1:** the state is not a cache with expiry: the OS reports every change, so it is current as of
  the last pump. It is updated before the event is queued, so queries stay right when the queue
  drops an event. A new window starts with its measured size, not focused, not minimized; the
  OS's messages during creation therefore send nothing, and the first focus arrives as an event.
- **L1:** only the newest queued event is checked, so events of different kinds never change order.
  Within one pump, a drag yields one resize per window.
- **M1:** with L1 a frozen drag still ends in one resize; the freeze is solved with the game loop.
- **N1:** focus on X11 comes from `FocusIn`/`FocusOut`, ignoring the ones caused by keyboard grabs
  (`NotifyGrab`/`NotifyUngrab`: the window manager during Alt+Tab or a drag). On Win32 focus is
  `WM_SETFOCUS`/`WM_KILLFOCUS` (keyboard focus, as on X11; `WM_ACTIVATE` would report a minimized
  window as active). Messages sent while a window is being created or destroyed produce no events:
  the application already knows.

Input:
- **O1:** `rb_key` values are HID usages below 256. Names follow the US QWERTY label at that
  position (`RB_KEY_W` is where QWERTY prints W), as SDL, GLFW and most engines do. Keys outside
  the enum (media keys, Japanese and Korean keys) produce no events. On ISO keyboards the key left
  of Enter shares the US backslash scancode, so it is `RB_KEY_BACKSLASH` (HID's `NON_US_HASH` is
  never reported by the OS); the key between Left Shift and Z is `RB_KEY_NON_US_BACKSLASH`.
- **P1:** the evdev numbering is what every current Linux X server uses; P2 waits for a server
  that differs. Win32 needs no choice: the scancode, with its extended bit, is in the `lParam` of
  `WM_KEYDOWN`.
- **Q1:** repeat is decided by the common code: a press of a key already down. Both OSes then
  agree, and a key held before the window got focus starts as a press.
- **R1:** modifiers come from the engine's own key state after the event (a Shift press carries
  `RB_KEY_MOD_SHIFT`), not from the OS: the same on both backends and unit-testable.
- **S1:** on Linux pixels and logical units are equal anyway.
- **T1:** the releases go to the window losing focus, before its `FOCUS_LOST`. On X11 a keyboard
  grab (`FocusOut` with `NotifyGrab`, e.g. the window manager's Alt+Tab) also releases everything
  without a focus event: the releases go to the grabbing client.
- **U1:** "the last pump" is the frame: `rb_platform_pump` clears the edges and the wheel first.
  The state is global (one keyboard, one mouse); events name their window. A destroyed window
  that had focus clears the input state without events.
- **Event types** (one per transition, H1): `KEY_PRESSED` / `KEY_RELEASED` (`ev.key`),
  `MOUSE_BUTTON_PRESSED` / `RELEASED` (`ev.button`: button, position, modifiers), `MOUSE_MOVED`
  (`ev.move`), `MOUSE_WHEEL` (`ev.wheel`). Buttons: left, right, middle, X1, X2 (back, forward).
- **Merging** (L1 extended): a move replaces a move of the same window that is the newest queued
  event, a wheel event adds to such a wheel event. A move to the same position sends nothing.
- **Capture:** while any button is held the window keeps receiving the mouse outside its client
  area, or a release outside would be lost and the button stuck. Win32: `SetCapture` /
  `ReleaseCapture`; X11 grabs the pointer implicitly on a button press. Positions can then be
  negative or beyond the size.
- **Wheel** in notches as `f32`, positive up (away from the user) and right. Win32 divides by
  `WHEEL_DELTA` (120), so high-resolution wheels give fractions; X11 core buttons 4–7 are one
  notch each.
- **Win32 quirks**, handled in the backend: AltGr arrives as a fake Left Ctrl plus Right Alt with
  the same timestamp (the Ctrl is dropped); with both Shifts held, releasing one sends no key-up
  (the pump checks `GetKeyState`); Print Screen sends only a key-up (a press is synthesized
  first); synthetic messages may carry scancode 0 (mapped back from the virtual key); Alt and
  F10 alone enter the window menu mode, which swallows the next key press (`SC_KEYMENU` is
  swallowed instead, so Alt+Space does not open the system menu).
- Rejected for now: relative mouse motion with a locked cursor (raw input / XInput2), key labels
  of the current layout, mouse enter/leave events, cursor shape, gamepads (backlog).

### File layout
```
engine/include/reborn/platform.h          public API
engine/include/reborn/input.h             key, mouse button and modifier enums (included by platform.h)
engine/src/platform/platform_internal.h   rb_platform_common + helpers shared by backends
engine/src/platform/platform.c            OS-independent: event queue, window pool and state
engine/src/platform/input.c               OS-independent: input state, queries, key names
engine/src/platform/win32/win32_platform.c
engine/src/platform/win32/win32_input.c   scancode -> rb_key (no OS header: unit-tested)
engine/src/platform/linux/xlib_platform.c
engine/src/platform/linux/xlib_input.c    keycode -> rb_key (no OS header: unit-tested)
```
Stateless OS services used by the logger (console, debugger detection) live next to the backends
in `platform_os.h`, `win32/win32_os.c` and `linux/linux_os.c` (ADR 0008).
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
- Pure parts (ring buffer, handle pool, key mapping, window and input state, event merging) are
  unit-tested; windows and events are
  tested once logging and the game lifecycle exist (§4, §5).
- Linux needs `libx11-dev` in `docker/linux.Dockerfile`. The container has no X server, so the
  Linux scripts gate on `ctest` and no longer run the sandbox, which runs on a Linux desktop
  (e.g. a WSL distro with WSLg). Rejected: Xvfb in the image (new package, and the sandbox
  cannot close by itself yet: revisit with integration tests) and a sandbox exiting 0 without a
  display (turns a failure into success).

### When to revisit
| Trigger | Change | Impact |
|---|---|---|
| Allocator interface available (§6) | System startup takes an allocator (B3) | Every system's startup signature; a new ADR |
| XWayland not good enough (fractional scaling blur, HDR, a distro dropping it) | Add a Wayland backend next to Xlib, chosen at runtime inside `platform/linux/` | Public API unchanged; adds `wayland-client` + `libxkbcommon` (new approval) |
| Xlib's synchronous round-trips become measurable | Move window/event code to XCB (E2) | Linux backend only |
| More windows than the pool allows | Raise the constant | None |
| Fullscreen, video options | Window commands (G3), maximized as a state | Additions only |
| A window manager without `WM_STATE` | Also read `_NET_WM_STATE_HIDDEN` (N2) | Linux backend only |
| Events lost in bursts | Grow the ring buffer or change the overflow policy | None for users |
| An X server without evdev keycodes (VNC, XQuartz) | Map keycodes through XKB key names (P2) | Linux backend only |
| First-person camera, cursor lock | Relative mouse motion (raw input / XInput2) | New event type; XInput2 is `libXi` (new approval) |
| Prompts must show the layout's label ("press Z" on AZERTY) | Key label query (`ToUnicode` / `XLookupKeysym`) | Addition only |
| Another OS | New `platform/<os>/` directory | None for users |
