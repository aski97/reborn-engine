# Roadmap

## v0.1.0 — Foundations

### 1. Project setup
- [x] Folder layout, git, `.gitignore` / `.gitattributes`
- [x] CMake + presets (Clang, Ninja, C17)
- [x] Single-source version + generated `version.h`
- [x] Docs: changelog, roadmap, ADRs, feature pages, `CLAUDE.md`
- [x] Verify CMake build on Windows (all presets)
- [ ] Verify CMake build on Linux

### 2. Application layer and windows
- [ ] Platform abstraction + Win32 backend
- [ ] Linux backend (X11/XCB vs Wayland: to decide)
- [ ] Window lifecycle (create, resize, close, focus, minimize)
- [ ] Event queue, basic keyboard/mouse input

### 3. Game lifecycle
- [ ] Application interface (init / update / render / shutdown)
- [ ] Game loop with high-resolution timer
- [ ] Timestep strategy (fixed vs variable: to decide)

### 4. Logging
- [ ] Levels (TRACE → FATAL), console with colors, file output
- [ ] `RB_ASSERT`
- [ ] Structured output (JSON lines) for LLM consumption

### 5. Memory
- [ ] OS allocation (VirtualAlloc / mmap)
- [ ] Arena allocator
- [ ] Allocation tracking by tag, leak report

### 6. Rendering (Vulkan)
- [ ] Vulkan loading (link vs dynamic: to decide)
- [ ] Instance, validation layers, debug messenger
- [ ] Surface, devices, swapchain
- [ ] Pipeline, command buffers, synchronization
- [ ] First triangle → 2D quad

## Backlog
- [ ] LLM debug/control API (state inspection, scene commands)
- [ ] Game code hot-reload as a DLL
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 3D
- [ ] More platforms
