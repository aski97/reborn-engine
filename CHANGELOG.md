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
