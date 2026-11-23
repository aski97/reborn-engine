# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-23

### Added
- Project layout: `engine/` (`reborn` library), `sandbox/`, `cmake/`, `docs/`.
- CMake build with `debug`, `release`, `debug-asan` presets (Clang + Ninja, C17).
- Engine built as a static library; `REBORN_SHARED` option and `RB_API` macro ready for a DLL build.
- `reborn/defines.h`: fixed-width types, platform/compiler detection, utilities.
- Single-source version, exposed to C through a generated `reborn/version.h`.
- Docs: feature pages, ADRs, roadmap, `CLAUDE.md` for LLM agents.
- Linux build container (`docker/linux.Dockerfile`: Ubuntu 26.04, Clang 23 from apt.llvm.org) and
  `scripts/linux-build.ps1`/`.sh` to build any preset on Linux from Windows and run `ctest` on it
  (the container has no X server: the sandbox runs on a Linux desktop, e.g. WSL with WSLg).
- MIT license.
- Unit test framework `tests/framework/rb_test.h`: auto-registered `RB_TEST`, `RB_EXPECT_*` /
  `RB_REQUIRE_*` checks, runner with `--list` / `--run`.
- CTest integration: one CTest entry per test (discovered after each build), test presets with
  JUnit output, `REBORN_BUILD_TESTS` option. First tests for `version`.
- Internal containers (ADR 0007): generational handle pool and index-only ring buffer, with tests.
- Platform layer (ADR 0006): `reborn/platform.h` with generational window handles and an event
  queue; Win32 backend (UTF-8 titles, Per-Monitor V2 DPI, close as a request).
  `rb_platform_startup(allocator)` takes its state from an `rb_allocator` (`NULL` on failure,
  nothing left allocated) and shutdown frees it through the same allocator (ADR 0010, K1).
- Xlib platform backend for Linux (UTF-8 titles via `_NET_WM_NAME`, close as a request through
  `WM_DELETE_WINDOW`); links `libX11`, added to the Linux build container (`libx11-dev`).
- Logging (ADR 0008): `reborn/log.h` with TRACE…FATAL levels, `RB_LOG_*` macros (compile-time
  minimum level), up to 8 sinks, colored stderr console (UTF-8 on Windows via `WriteConsoleW`) and
  a JSON lines file. Startup logs the local date and time of `t = 0`; JSON lines carry `ts`
  (Unix milliseconds). `reborn/assert.h`: `RB_ASSERT` / `RB_ASSERT_MSG` (debug builds).
- Platform backends log failures (Win32 `GetLastError` text), window creation and destruction,
  and event queue overflow (one warning per burst). The platform API asserts its pointer
  parameters.
- Window lifecycle (ADR 0006): `RB_EVENT_WINDOW_RESIZED` (logical and pixel size),
  `FOCUS_GAINED` / `FOCUS_LOST`, `MINIMIZED` / `RESTORED`, sent only on a real change; consecutive
  resizes merge in the queue. Queries `rb_window_get_size`, `rb_window_is_focused`,
  `rb_window_is_minimized`. Win32 and Xlib backends. `rb_ring_newest_index` in the ring buffer.
- Keyboard and mouse input (ADR 0006, F1 and O–U): `reborn/input.h` with physical keys (`rb_key`,
  USB HID values, US QWERTY names), mouse buttons and modifier flags; events `KEY_PRESSED` /
  `KEY_RELEASED` (with repeat flag and modifiers), `MOUSE_BUTTON_PRESSED` / `RELEASED`,
  `MOUSE_MOVED` and `MOUSE_WHEEL` (merged in the queue); per-pump state queries (`rb_key_is_down`,
  `rb_key_was_pressed`, `rb_mouse_get_position`...). Held keys and buttons are released on focus
  loss. Win32 (scancodes, mouse capture, AltGr / Shift / Print Screen quirks, no window menu mode
  on Alt) and Xlib (evdev keycodes, detectable auto-repeat) backends.
- Window position (ADR 0006, V–X): `RB_EVENT_WINDOW_MOVED` and `rb_window_get_position`, the
  client area's top left in virtual screen pixels; consecutive moves merge, kept while minimized.
  The Xlib backend translates it through the server (the window manager's frame offsets the
  coordinates of `ConfigureNotify`).
- Monotonic clock and sleep (ADR 0009): `reborn/clock.h` with `rb_time_now` (nanoseconds,
  `QueryPerformanceCounter` / `CLOCK_MONOTONIC`), `rb_time_sleep` (at least the time asked; a
  high-resolution waitable timer on Windows, so 1 ms lasts ~1.6 ms instead of ~15.6 ms) and
  `rb_time_to_seconds`. The logger's `t` uses it, so it does not jump with the system time.
- Virtual memory (ADR 0010, A1), internal to the engine: reserve an address range, commit and
  decommit pages of it, release it (`VirtualAlloc` / `mmap`). Reserving costs no RAM nor commit
  charge; out-of-memory is reported at commit on both platforms. Win32 `GetLastError` logging is
  shared by the backend files (`win32_error.c`).
- Arena (ADR 0010): `reborn/memory.h` with `rb_arena` over a virtual memory reservation (64 KiB
  commit steps, never moves) or a caller's buffer; zeroed `rb_arena_push`, `_push_uninit`,
  overflow-checked `_push_array`, `RB_ARENA_PUSH_TYPE` / `_ARRAY`; `rb_arena_reset` and nested temp
  scopes. ASan builds poison free memory and a 16-byte red zone before each allocation, so
  overflows between allocations and use after reset are reported.
- Allocator interface (ADR 0010): `rb_allocator` (context + vtable, by value) with sized
  `rb_alloc` (zeroed) / `_uninit` / `_array`, `rb_resize` / `_uninit`, `rb_free` and
  `RB_ALLOC_TYPE` / `_ARRAY`, `RB_FREE_TYPE` / `_ARRAY`; `rb_arena_allocator` (frees and grows the
  last block in place, never below an open temp scope) and `rb_heap_allocator` (`_aligned_malloc` /
  `posix_memalign`). Failing allocator for tests in `tests/support/`.
- Allocation tracking (ADR 0012): every allocator call names an `rb_memory_tag` (`GENERAL`,
  `PLATFORM`, `GAME`); the helpers count live bytes and blocks, peak, allocations and frees per tag,
  whichever allocator serves the call (`rb_memory_get_stats`). Arenas keep a high-water mark
  (`rb_arena.peak`). At the end of the run the app logs each tag's peak and totals, a
  `Memory leak` error per tag still holding blocks, and each arena's peak of reserved.
- `RB_PRINTF_FORMAT` in `defines.h`; `-fmacro-prefix-map` makes `__FILE__` relative to the
  repository; `_CRT_SECURE_NO_WARNINGS` on Windows.
- Application layer (ADR 0011): `reborn/app.h` with `rb_app_run(&callbacks)`. The engine owns the
  loop and calls the game's `configure` (before any system), `init`, `on_event`, `update`,
  `render` and `shutdown`. Fixed timestep (60 updates/s by default) with an integer nanosecond
  accumulator, deltas clamped to 250 ms, `render(alpha)` for interpolation; one pump per step, so
  input edges are exact per update. Optional frame cap (none by default: vsync paces frames) paced
  on absolute deadlines;
  render skipped while minimized. The engine allocates the game's state (`RB_APP_STATE`) from a
  persistent arena and resets a frame arena every frame. `rb_app_run` also checks the game's header
  version against the library and starts the logger with `config.log`: a game's `main` is one line.
  `rb_app_allocator` gives the game a general-purpose allocator for objects freed one at a time.
  Each phase is logged once, nothing per frame: the configuration as received and the quit reason
  at INFO, game init, loop and game shutdown at DEBUG.
- Sandbox: a game under the application layer (`main.c` is the one-line entry point, `game.c`
  holds the callbacks). It logs every window and input event (modifiers by name:
  `key S pressed [Ctrl]`) and the steps and frames of each second, to the console and to
  `reborn.log.jsonl`. The clear color turns slowly around the hue wheel, showing that frames reach the
  screen; on top, a row of colored squares, one sliding across the window and a translucent one
  under the mouse. A frame-time graph (bottom left, toggled with L) draws the interval between
  the last 120 frames as bars, green within a 60 Hz frame, yellow up to two, red beyond, with
  lines at 60 and 30 fps.
- Vulkan renderer (ADR 0013), `reborn/renderer.h`: `rb_renderer_startup(allocator, config,
  platform, window)` / `rb_renderer_shutdown` create and destroy a Vulkan 1.3 instance, the
  window's surface, a device and a swapchain, and present a frame each loop iteration; state
  tagged `RB_MEMORY_TAG_RENDERER`. The engine opens `vulkan-1.dll` / `libvulkan.so.1` at run time instead
  of linking it, so a machine without Vulkan (or below 1.3) gets a logged error; functions are
  loaded into per-object tables (X-macro lists, `VK_NO_PROTOTYPES`). Headers come from the Vulkan
  SDK on Windows and `libvulkan-dev` on Linux.
- Vulkan validation: `rb_renderer_config.validation` (on by default in debug builds of the engine)
  enables `VK_LAYER_KHRONOS_validation` with a `VK_EXT_debug_utils` messenger, also covering
  instance creation and destruction; without the layer, a warning and the run goes on. Messages go
  to the log at the matching level; validation errors and warnings are counted
  (`rb_renderer_get_validation_stats`) and their totals logged at shutdown; the first error of each
  kind stops in an attached debugger (`RB_DEBUG_BREAK`, shared with `RB_ASSERT`).
- GPU choice: every GPU is logged with its verdict; suitable ones (Vulkan 1.3, `VK_KHR_swapchain`,
  one queue family that draws and presents) are ranked discrete > integrated > virtual > CPU, with
  a warning when only a CPU implementation such as lavapipe fits. The device enables dynamic
  rendering and synchronization2.
- Swapchain: sRGB format (`B8G8R8A8_SRGB` / `R8G8B8A8_SRGB`), minimum images + 1, one image view
  each; `rb_renderer_config.vsync` picks FIFO, or MAILBOX / IMMEDIATE when off. Rebuilt when the
  window's pixel size changes, checked at the start of each frame; none while the window has no
  pixels.
- Frames (ADR 0013, R–X): two frames in flight, each with a command pool reset whole every frame,
  its command buffer, an acquire semaphore and a fence; one presentation semaphore per swapchain
  image. Each frame waits for its fence, acquires an image, clears it with dynamic rendering,
  submits with synchronization2 barriers and presents. Out-of-date or suboptimal swapchains are
  rebuilt at the next frame; an unrecoverable error (device lost) is logged once and stops drawing.
- `rb_renderer_set_clear_color`: the color every frame starts from (dark gray by default).
- Quads (ADR 0015): `rb_renderer_draw_quad(renderer, x, y, width, height, color)` draws a filled
  rectangle from the game's `render`, in window pixels (origin top left, like the mouse), in call
  order, with straight alpha blending. One graphics pipeline, built at startup for the swapchain's
  format; the vertex shader makes the corners from the vertex index and the quad's push constants,
  so no GPU memory is allocated.
- Shaders: GLSL in `engine/src/renderer/vulkan/shaders/`, compiled to SPIR-V by `glslc` at build
  time (`find_package(Vulkan COMPONENTS glslc)`, a depfile tracks `#include`s) and embedded in the
  engine as C arrays: a shader error fails the build, nothing is loaded at run time.
- Colors (ADR 0013, Y3 and Z2), `reborn/color.h`: `rb_color` holds linear RGB with straight alpha;
  `rb_color_srgb` converts from sRGB (the exact curve, clamped, alpha untouched), `rb_color_linear`
  takes values as they are, `rb_color_srgb_to_linear` converts one channel. The engine links libm
  on Linux.
- Vulkan surfaces created by the platform backends (internal `platform/platform_vulkan.h`:
  `VK_KHR_win32_surface`, `VK_KHR_xlib_surface`), so native window handles stay in the platform.
- Application layer starts the renderer on the main window (`rb_app_config.renderer`, vsync on by
  default, `rb_app_renderer`); each frame begins a renderer frame, calls `render` inside it and
  ends it; render is skipped when no frame can be shown (minimized, no pixels).
- Linux build container: `libvulkan-dev`, `mesa-vulkan-drivers` (lavapipe, Vulkan on the CPU) and
  `vulkan-validationlayers`, so renderer tests run without a GPU; `glslc` for the shaders.
- Internal dynamic library loading in the platform layer (`LoadLibraryExW` limited to the
  application folder and System32, `dlopen`).

[Unreleased]: https://github.com/aski97/reborn-engine/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/aski97/reborn-engine/releases/tag/v0.1.0
