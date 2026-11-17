# Roadmap

## v0.1.0 — Foundations

§1–§6 are done; what remains is §7, in its listed order. Section numbers stay fixed: ADRs and
code refer to them.

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
- [x] Application interface (configure / init / on_event / update / render / shutdown); the
      engine allocates the game's state through the §6 allocators (ADR 0011)
- [x] Game loop: frame timing, per-frame arena reset, frame cap until vsync (ADR 0011)
- [x] Timestep strategy: fixed step with accumulator, one pump per step (ADR 0011)

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
- [x] Allocation tracking by tag, leak report, arena high-water mark (ADR 0012)

### 7. Rendering (Vulkan)
- [x] Vulkan loading: loader opened at run time, function tables (ADR 0013)
- [x] Instance (Vulkan 1.3), validation layers, debug messenger; public `rb_renderer` started
      by the app (ADR 0013)
- [x] Surface (created by the platform), GPU ranked by type with lavapipe as last resort, device
      with one graphics + present queue, swapchain (sRGB, vsync option) recreated when the window's
      pixel size changes; `rb_renderer_startup` takes the platform and window (ADR 0013, J–Q)
- [ ] Pipeline, command buffers, synchronization
- [ ] First triangle → 2D quad

## Backlog

### Core
- [ ] Engine-provided entry point (ADR 0011, H3): `reborn/entry.h` defines `main` (and `WinMain` for
      a Windows build without a console); the game only defines a function returning its
      callbacks. When a GUI-subsystem build or command-line arguments are needed
- [ ] Application: veto on the main window's close ("save before quitting?"); a variable
      `update` next to the fixed one if camera or UI need it (ADR 0011, C4); window events
      between steps if a low update rate makes them lag
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
- [ ] Random number generator with an explicit seed (reproducible runs, replays, tests)
- [ ] Hash functions for the hash map, asset IDs and string interning (algorithm to decide)
- [ ] Error handling: public functions return `bool` and log today; decide whether callers need
      error codes to react (missing vs corrupt file, out of memory), and how they are named
- [ ] Fatal errors shown to the player: a message box when startup fails (no Vulkan driver, no
      window), not only a log line nobody reads
- [ ] Crash handling: catch crashes (unhandled exceptions on Windows, signals on Linux), write the
      stack trace to the log and flush it; minidumps on Windows

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
- [ ] Application icon: on Windows embedded in the executable (`.rc` resource, shown by Explorer
      and the taskbar) and set per window (`WM_SETICON`); on Linux the `_NET_WM_ICON` property
      (ARGB pixels, so it needs decoded images, see Assets) and a `.desktop` file when packaging
- [ ] Win32 modal loop: the game freezes while a window is dragged or resized (the loop clamps
      the frozen time, ADR 0011; running during the drag needs a timer or a window thread)
- [ ] Xlib error handlers (default ones exit the process): log X errors instead of exiting
- [ ] Linux title parity with Windows: UTF-8 validation and length limit (Xlib passes bytes as-is)
- [ ] Linux HiDPI: read the desktop scale (`Xft.dpi`) if logical units must differ from pixels

### Rendering
- [ ] Rendering without a window (ADR 0013, K): `rb_renderer_startup` accepts a NULL platform and
      creates a device without surface or swapchain, drawing to offscreen images. For tools (asset
      baking, thumbnails), golden-image tests and CI machines without a display
- [ ] GPU override: choose the device by name or index (config or environment), e.g. to test the
      integrated GPU of a laptop or force lavapipe (ADR 0013, L3)
- [ ] Batched 2D renderer (sprites, textures)
- [ ] 2D camera: position, zoom, world-to-screen conversion (needs the math library)
- [ ] Screenshots: copy the presented image to a file (also for golden-image tests)
- [ ] The game's name in `VkApplicationInfo` (drivers use it for per-game profiles), from its config
- [ ] Vulkan host allocations through `rb_allocator` (`pAllocator`), if tracking needs them (ADR 0013)
- [ ] 3D

### Assets
- [ ] File system layer: read/write whole files, paths resolved from the executable's folder
      (not the current directory, which depends on how the game is launched), directory listing
- [ ] Asset manager: assets referenced by handle (generational pool, ADR 0007), loaded once and
      shared, unloaded when no longer used (reference counting vs explicit lifetimes: to decide)
- [ ] Image decoding (PNG at least) for textures and the icon (own decoder vs external library:
      to decide)
- [ ] Asynchronous loading on the job system (see Threading)
- [ ] Hot reload: watch asset files and reload them while the game runs (shaders first)
- [ ] Asset baking: convert source files into engine-ready formats at build time, packed into
      one archive for release builds (format to decide)

### Audio
- [ ] Backend per platform: WASAPI on Windows, PulseAudio or ALSA on Linux (to decide), on its own
      thread feeding the device
- [ ] Mixer: several sounds at once, volume per sound and per group (music, effects)
- [ ] Sound effects (loaded whole) and music (streamed from disk); decoding WAV first, compressed
      formats later (own decoder vs external library: to decide)

### Text and UI
- [ ] Fonts: rasterize glyphs into an atlas texture (own rasterizer vs external library vs
      pre-baked bitmap font: to decide)
- [ ] Text rendering on the 2D renderer: UTF-8 strings, measuring, alignment
- [ ] Debug UI, immediate mode (rebuilt every frame): panels, buttons, sliders, value inspection.
      Base for the in-game console and the profiler overlay

### Scene
- [ ] Game object model: ECS vs object hierarchy vs plain arrays per type (to decide in its ADR)
- [ ] Transforms with parent-child relations, if the model needs them
- [ ] Scenes saved and loaded from files (needs serialization)
- [ ] Serialization: one mechanism for scenes, save games and config (text vs binary, format to
      decide)

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
- [ ] Release packaging: one folder or archive per platform with the executable, packed assets,
      icon and (Linux) `.desktop` file, built by a script or CI; installers later
- [ ] Game code hot-reload as a DLL
- [ ] LLM debug/control API (state inspection, scene commands)

### Documentation
- [ ] Doc comments on every public header and function: purpose, parameters, ownership, errors,
      thread safety (format, e.g. Doxygen-style `/** */` vs plain comments: to decide with the website)
- [ ] Documentation website: feature pages, ADRs and an API reference generated from doc comments
      (generator and hosting, e.g. GitHub Pages published by CI: to decide)
