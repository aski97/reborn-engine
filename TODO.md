# Roadmap

## v0.1.0 — Foundations

### 1. Project setup
- [x] Folder layout, git, `.gitignore` / `.gitattributes`
- [x] CMake + presets (Clang, Ninja, C17)
- [x] Single-source version + generated `version.h`
- [x] Docs: changelog, roadmap, ADRs, feature pages, `CLAUDE.md`
- [x] Verify CMake build on Windows (all presets)
- [x] Verify CMake build on Linux (Docker, all presets — ADR 0005)

### 2. Unit testing
- [x] `rb_test.h`: test macros, expectations, auto-registration (`__attribute__((constructor))`)
- [x] CTest integration (`ctest --preset debug`, one entry per test, JUnit output — ADR 0004)
- [x] `tests/` layout mirroring `engine/src/`, first test for `version`
- [x] Feature page + ADR

### 3. Application layer and windows
- [x] Platform abstraction + Win32 backend (ADR 0006)
- [x] Generational handle pool and index-only ring buffer (ADR 0007)
- [x] Linux backend: Xlib (ADR 0006)
- [x] Linux build scripts gate on `ctest`; the sandbox runs on a Linux desktop
- [x] Window lifecycle: resize, focus, minimize (events + queries, ADR 0006)
- [x] Event queue (fixed-size ring buffer, ADR 0006)
- [ ] Basic keyboard/mouse input by physical key position

### 4. Game lifecycle
- [ ] Application interface (init / update / render / shutdown)
- [ ] Game loop with high-resolution timer
- [ ] Timestep strategy (fixed vs variable: to decide)

### 5. Logging (done before finishing §3, to see window and input events)
- [x] Levels (TRACE → FATAL), console with colors, file output (ADR 0008)
- [x] `RB_ASSERT`
- [x] Structured output (JSON lines) for LLM consumption

### 6. Memory
- [ ] OS allocation (VirtualAlloc / mmap)
- [ ] Arena allocator
- [ ] Allocator interface; system startup takes an allocator instead of a size query (ADR 0006, B3)
- [ ] ASan integration for custom allocators (`ASAN_POISON_MEMORY_REGION` / `ASAN_UNPOISON_MEMORY_REGION`),
      otherwise overflows between objects inside an arena go undetected
- [ ] Allocation tracking by tag, leak report
- [ ] Failing test allocator (fails at the N-th allocation) to cover error paths

### 7. Rendering (Vulkan)
- [ ] Vulkan loading (link vs dynamic: to decide)
- [ ] Instance, validation layers, debug messenger
- [ ] Surface, devices, swapchain
- [ ] Pipeline, command buffers, synchronization
- [ ] First triangle → 2D quad

## Backlog
- [ ] LLM debug/control API (state inspection, scene commands)
- [ ] Game code hot-reload as a DLL
- [ ] CI: GitHub Actions (Windows + Linux), tests on every push; reuse `docker/linux.Dockerfile`,
      decide whether to publish the image (GHCR) or build it per run
- [ ] Run `scripts/linux-build.sh` end-to-end on a Linux host (only syntax-checked so far)
- [ ] Containers that own memory: dynamic array, hash map, string/string builder
      (allocator-aware, see Memory; macros vs `void*` + size to decide in their ADR — ADR 0007)
- [ ] Threading: threads, mutexes, condition variables, atomics, then a job system
      (C11 `<threads.h>` vs own Win32/pthreads wrappers: to decide); make the logger thread-safe
- [ ] Logging: categories with their own level, in-memory history for an in-game console / LLM
      debug API, monotonic clock once the game loop timer exists (ADR 0008)
- [ ] Per-platform dependency setup scripts (Clang, CMake, Ninja, Vulkan SDK): `winget` on Windows,
      package manager on Linux (distros to support: to decide)
- [ ] Integration tests for platform/renderer (offscreen render + golden images)
- [ ] Testing: `longjmp`-based `RB_REQUIRE` + `RB_ASSERT` handler hook to test that asserts fire;
      fixtures when tests start repeating setup; ULP-based float comparison (ADR 0004)
- [ ] Performance tests for data structures (handle pool, ring, future containers): `bench/`
      executable (ADR 0004), release build, timings not pass/fail (timer and approach to decide)
- [ ] Fuzzing with libFuzzer (`-fsanitize=fuzzer`) for parsers and containers
- [ ] Code coverage with Clang source-based coverage (`-fprofile-instr-generate`, `llvm-cov`)
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 3D
- [ ] More platforms
- [ ] Wayland backend, selected at runtime next to Xlib (only if XWayland falls short, ADR 0006)
- [ ] Text input events (Unicode), separate from key events
- [ ] Xlib error handlers (default ones exit the process): log X errors instead of exiting
- [ ] Linux title parity with Windows: UTF-8 validation and length limit (Xlib passes bytes as-is)
- [ ] Linux HiDPI: read the desktop scale (`Xft.dpi`) if logical units must differ from pixels
- [ ] Window commands: set size, minimize, fullscreen; maximized as a state (ADR 0006, G3)
- [ ] Win32 modal loop: the game freezes while a window is dragged or resized (with §4)
