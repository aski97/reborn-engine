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
- [ ] `rb_test.h`: test macros, expectations, auto-registration (constructor / `.CRT$XCU`)
- [ ] CTest integration (`ctest --preset debug`, JUnit output)
- [ ] `tests/` layout mirroring `engine/src/`, first test for `version`
- [ ] Feature page + ADR

### 3. Application layer and windows
- [ ] Platform abstraction + Win32 backend
- [ ] Linux backend (X11/XCB vs Wayland: to decide)
- [ ] Window lifecycle (create, resize, close, focus, minimize)
- [ ] Event queue, basic keyboard/mouse input

### 4. Game lifecycle
- [ ] Application interface (init / update / render / shutdown)
- [ ] Game loop with high-resolution timer
- [ ] Timestep strategy (fixed vs variable: to decide)

### 5. Logging
- [ ] Levels (TRACE → FATAL), console with colors, file output
- [ ] `RB_ASSERT`
- [ ] Structured output (JSON lines) for LLM consumption

### 6. Memory
- [ ] OS allocation (VirtualAlloc / mmap)
- [ ] Arena allocator
- [ ] Allocation tracking by tag, leak report

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
- [ ] Reusable containers: dynamic array, hash map, ring buffer, string/string builder
      (allocator-aware, see Memory; generic approach — macros vs `void*` + size — to decide)
- [ ] Threading: threads, mutexes, condition variables, atomics, then a job system
      (C11 `<threads.h>` vs own Win32/pthreads wrappers: to decide)
- [ ] Per-platform dependency setup scripts (Clang, CMake, Ninja, Vulkan SDK): `winget` on Windows,
      package manager on Linux (distros to support: to decide)
- [ ] Integration tests for platform/renderer (offscreen render + golden images)
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 3D
- [ ] More platforms
