# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Project layout: `engine/` (`reborn` library), `sandbox/`, `cmake/`, `docs/`.
- CMake build with `debug`, `release`, `debug-asan` presets (Clang + Ninja, C17).
- Engine built as a static library; `REBORN_SHARED` option and `RB_API` macro ready for a DLL build.
- `reborn/defines.h`: fixed-width types, platform/compiler detection, utilities.
- Single-source version, exposed to C through a generated `reborn/version.h`.
- Docs: feature pages, ADRs, roadmap, `CLAUDE.md` for LLM agents.
- Linux build container (`docker/linux.Dockerfile`: Ubuntu 26.04, Clang 23 from apt.llvm.org) and
  `scripts/linux-build.ps1`/`.sh` to build and run any preset on Linux from Windows.
- MIT license.
- Unit test framework `tests/framework/rb_test.h`: auto-registered `RB_TEST`, `RB_EXPECT_*` /
  `RB_REQUIRE_*` checks, runner with `--list` / `--run`.
- CTest integration: one CTest entry per test (discovered after each build), test presets with
  JUnit output, `REBORN_BUILD_TESTS` option. First tests for `version`.
- Linux build scripts also run `ctest`.
- Internal containers (ADR 0007): generational handle pool and index-only ring buffer, with tests.
- Platform layer (ADR 0006): `reborn/platform.h` with generational window handles and an event
  queue; Win32 backend (UTF-8 titles, Per-Monitor V2 DPI, close as a request).
- Xlib platform backend for Linux (UTF-8 titles via `_NET_WM_NAME`, close as a request through
  `WM_DELETE_WINDOW`); links `libX11`, added to the Linux build container (`libx11-dev`).
  The sandbox opens a window on both platforms.
- Linux build scripts no longer run the sandbox (the container has no X server): `ctest` is the
  gate. The sandbox runs on a Linux desktop (e.g. a WSL distro with WSLg).
- Logging (ADR 0008): `reborn/log.h` with TRACE…FATAL levels, `RB_LOG_*` macros (compile-time
  minimum level), up to 8 sinks, colored stderr console (UTF-8 on Windows via `WriteConsoleW`) and
  a JSON lines file. Startup logs the local date and time of `t = 0`; JSON lines carry `ts`
  (Unix milliseconds). `reborn/assert.h`: `RB_ASSERT` / `RB_ASSERT_MSG` (debug builds).
- Platform backends log failures (Win32 `GetLastError` text), window creation and destruction,
  and event queue overflow (one warning per burst). The platform API asserts its pointer
  parameters. The sandbox logs to the console and to
  `reborn.log.jsonl`.
- Window lifecycle (ADR 0006): `RB_EVENT_WINDOW_RESIZED` (logical and pixel size),
  `FOCUS_GAINED` / `FOCUS_LOST`, `MINIMIZED` / `RESTORED`, sent only on a real change; consecutive
  resizes merge in the queue. Queries `rb_window_get_size`, `rb_window_is_focused`,
  `rb_window_is_minimized`. Win32 and Xlib backends; the sandbox logs every event.
  `rb_ring_newest_index` in the ring buffer.
- Keyboard and mouse input (ADR 0006, F1 and O–U): `reborn/input.h` with physical keys (`rb_key`,
  USB HID values, US QWERTY names), mouse buttons and modifier flags; events `KEY_PRESSED` /
  `KEY_RELEASED` (with repeat flag and modifiers), `MOUSE_BUTTON_PRESSED` / `RELEASED`,
  `MOUSE_MOVED` and `MOUSE_WHEEL` (merged in the queue); per-pump state queries (`rb_key_is_down`,
  `rb_key_was_pressed`, `rb_mouse_get_position`...). Held keys and buttons are released on focus
  loss. Win32 (scancodes, mouse capture, AltGr / Shift / Print Screen quirks, no window menu mode
  on Alt) and Xlib (evdev keycodes, detectable auto-repeat) backends; the sandbox logs input.
- `RB_PRINTF_FORMAT` in `defines.h`; `-fmacro-prefix-map` makes `__FILE__` relative to the
  repository; `_CRT_SECURE_NO_WARNINGS` on Windows.
