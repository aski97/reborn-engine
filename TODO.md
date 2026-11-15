# Roadmap

## v0.1.0 — Foundations

### 1. Project setup
- [x] Folder layout, git, `.gitignore` / `.gitattributes`
- [x] CMake + presets (Clang, Ninja, C17)
- [x] Single-source version + generated `version.h`
- [x] Docs: changelog, roadmap, ADRs, feature pages, `CLAUDE.md`
- [x] Verify CMake build on Windows (all presets)
- [ ] Verify CMake build on Linux

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
- [ ] CI: GitHub Actions (Windows + Linux), tests on every push
- [ ] Integration tests for platform/renderer (offscreen render + golden images)
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 3D
- [ ] More platforms
