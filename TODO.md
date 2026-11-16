# Roadmap

## v0.1.0 — Foundations

Order of the remaining work: §4 timer → §6 core (OS allocation, arena, allocator interface, ASan,
failing allocator) → rest of §4, designed on top of the allocators → rest of §6 (tracking) → §7.
Section numbers stay fixed: ADRs and code refer to them.

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
- [x] Basic keyboard/mouse input by physical key position (ADR 0006)

### 4. Game lifecycle
- [x] High-resolution monotonic timer and sleep (done before §6, ADR 0009); the logger's `t` moves
      to it (ADR 0008, H); the sandbox loop stops spinning a CPU core
- [ ] Application interface (init / update / render / shutdown); the engine allocates the game's
      state through the §6 allocators
- [ ] Game loop: frame timing, per-frame arena reset
- [ ] Timestep strategy (fixed vs variable: to decide)

### 5. Logging (done before finishing §3, to see window and input events)
- [x] Levels (TRACE → FATAL), console with colors, file output (ADR 0008)
- [x] `RB_ASSERT`
- [x] Structured output (JSON lines) for LLM consumption

### 6. Memory
- [x] OS allocation: reserve / commit / decommit / release (VirtualAlloc / mmap, ADR 0010)
- [x] Arena allocator: virtual memory or caller buffer, temp scopes (ADR 0010)
- [x] Allocator interface, arena adapter, heap allocator; system startup takes an allocator
      instead of a size query (ADR 0006, B3; ADR 0010)
- [x] ASan integration for custom allocators (`ASAN_POISON_MEMORY_REGION` / `ASAN_UNPOISON_MEMORY_REGION`),
      otherwise overflows between objects inside an arena go undetected
- [x] Failing test allocator (fails at the N-th allocation) to cover error paths
- [ ] Allocation tracking by tag, leak report (after the rest of §4)

### 7. Rendering (Vulkan)
- [ ] Vulkan loading (link vs dynamic: to decide)
- [ ] Instance, validation layers, debug messenger
- [ ] Surface, devices, swapchain
- [ ] Pipeline, command buffers, synchronization
- [ ] First triangle → 2D quad

## Backlog

### Core
- [ ] Threading: threads, mutexes, condition variables, atomics, then a job system
      (C11 `<threads.h>` vs own Win32/pthreads wrappers: to decide)
- [ ] Thread safety: decide which public functions must be callable from any thread (logger first,
      then event queue, allocators...), state the guarantee in each function's doc comment,
      protection per case (mutex, lock-free, per-thread data: to decide)
- [ ] Configuration separate from code: values hard-coded today (window size and title, log level
      and file, capacities...) read from a config file and/or command line (format and parser to decide)
- [ ] Logging: categories with their own level, in-memory history for an in-game console / LLM
      debug API (ADR 0008)
- [ ] Containers that own memory: dynamic array, hash map, string/string builder
      (allocator-aware, see Memory; macros vs `void*` + size to decide in their ADR — ADR 0007)
- [ ] Chained arena blocks (ADR 0010, B3), inside `rb_arena` without an API change. Only when one
      of these shows up: an arena whose size depends on content with no sensible upper bound
      (editor, mods, long sessions); reservations failing in a real environment (`ulimit -v`,
      Windows job objects); many small short-lived arenas (per asset, per entity), better carved
      from a parent allocator than each reserving address space; a target without virtual memory
      control (WebAssembly, some consoles)

### Profiling and performance
- [ ] Profiler: timed scopes per subsystem and per frame, to see where time goes and find bottlenecks
      (own implementation vs external tool: to decide)
- [ ] Resource usage: memory by tag (builds on §6 allocation tracking: current, peak, allocation
      count per subsystem), process CPU and memory from the OS, later GPU
- [ ] Viewer: in-game overlay and/or export (JSON lines for LLMs, trace format for external viewers)
- [ ] Performance tests for data structures (handle pool, ring, future containers): `bench/`
      executable (ADR 0004), release build, timings not pass/fail (timer and approach to decide)

### Platform
- [ ] More platforms
- [ ] Wayland backend, selected at runtime next to Xlib (only if XWayland falls short, ADR 0006)
- [ ] Text input events (Unicode), separate from key events
- [ ] Relative mouse motion with a locked, hidden cursor (raw input / XInput2), for first-person cameras
- [ ] Key labels of the current layout, for prompts ("press Z" on AZERTY)
- [ ] Mouse enter/leave events, cursor shape
- [ ] Gamepads
- [ ] Input action mapping (bindings to actions), after the game lifecycle (§4)
- [ ] Window commands: set size, minimize, fullscreen; maximized as a state (ADR 0006, G3)
- [ ] Win32 modal loop: the game freezes while a window is dragged or resized (with §4)
- [ ] Xlib error handlers (default ones exit the process): log X errors instead of exiting
- [ ] Linux title parity with Windows: UTF-8 validation and length limit (Xlib passes bytes as-is)
- [ ] Linux HiDPI: read the desktop scale (`Xft.dpi`) if logical units must differ from pixels

### Rendering
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 3D

### Testing
- [ ] Testing: `longjmp`-based `RB_REQUIRE` + `RB_ASSERT` handler hook to test that asserts fire;
      fixtures when tests start repeating setup; ULP-based float comparison (ADR 0004)
- [ ] Integration tests for platform/renderer (offscreen render + golden images)
- [ ] Fuzzing with libFuzzer (`-fsanitize=fuzzer`) for parsers and containers
- [ ] Code coverage with Clang source-based coverage (`-fprofile-instr-generate`, `llvm-cov`)

### Build, CI and tooling
- [ ] CI: GitHub Actions (Windows + Linux), tests on every push; reuse `docker/linux.Dockerfile`,
      decide whether to publish the image (GHCR) or build it per run
- [ ] Run `scripts/linux-build.sh` end-to-end on a Linux host (only syntax-checked so far)
- [ ] Per-platform dependency setup scripts (Clang, CMake, Ninja, Vulkan SDK): `winget` on Windows,
      package manager on Linux (distros to support: to decide)
- [ ] Game code hot-reload as a DLL
- [ ] LLM debug/control API (state inspection, scene commands)

### Documentation
- [ ] Doc comments on every public header and function: purpose, parameters, ownership, errors,
      thread safety (format, e.g. Doxygen-style `/** */` vs plain comments: to decide with the website)
- [ ] Documentation website: feature pages, ADRs and an API reference generated from doc comments
      (generator and hosting, e.g. GitHub Pages published by CI: to decide)
