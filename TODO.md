# Roadmap

From v0.2.0 the roadmap follows a ladder of small games of our own, simplest first (ADR 0014).
Engine tasks come out of building them: a piece (container, thread, file format...) is added when
a game needs it, and moves from the backlog into that game's version. The sandbox keeps exercising
every engine feature. "Likely needs" are guesses, not tasks. A piece that replaces code a game
already has comes with that game's migration; whether an older game also adopts a new feature is
decided case by case.

A task with parts lists them as sub-tasks; the parts already done are checked, so what is left
shows at a glance. A trigger ("when...", "with...") says which feature will call for the task.
Details live in the ADRs, the feature pages and the changelog: here, the concepts.

## v0.2.0 (current)

**Blink** (2D): a colored square appears at a random spot; click it before it vanishes. It speeds
up, a miss costs a life; score and lives are shown as text.
In order: each step needs the ones before, and each new engine piece is shown in the sandbox too.
The game starts as soon as it can run, so later steps answer its real needs instead of guesses.
No batching yet: a few hundred quads and glyphs fit one draw each.
- [x] 2D math and camera (ADR 0016)
  - [x] Vectors, rectangles, matrices, orthographic projection
  - [x] World units independent of the window's resolution, the design area always whole
        (letterbox), zoom
  - [x] Screen-to-world and world-to-screen conversion
  - [x] World and window pixels in the same frame (UI and debug drawing in pixels)
- [x] GPU memory and textures (ADR 0017)
  - [x] One device memory allocation per resource
  - [x] Uploads through a staging buffer
  - [x] Nearest and linear samplers, one descriptor set per texture
  - [x] Texture handles: create from RGBA pixels, destroy, validity, a cap on live textures
  - [x] Tinted textured quads; plain quads through a 1x1 white texture
- [x] Fonts (ADR 0018)
  - [x] The engine's own 8x8 bitmap font: ASCII, a box for the rest
  - [x] Glyph sources feeding a shelf packer into one atlas
  - [x] Glyph and font metrics
  - [x] Drawing a region of a texture
- [x] Random number generator (ADR 0019)
  - [x] PCG32 with an explicit seed, the same sequence everywhere, owned by the caller
  - [x] Integers in a range without modulo bias, floats
  - [x] The engine-wide rule for header-only code
- [x] Blink, the first game (ADR 0014). Assets deferred: Blink has none. From here on Blink drives
      the engine tasks
  - [x] Layers (rules, input, view, glue), rules and input tested
  - [x] Playable without text: the shrinking square, speed-up, lives, game over, restart
  - [x] Seed, score and lives logged
  - [x] Games in the build, one target each; one test runner for the engine and the games
  - [x] A README per game, a list of the games
- [x] Window icon (ADR 0020)
  - [x] One icon at several sizes, set before the window shows
  - [x] Sizes picked for the window's DPI (Win32), every size given (X11)
  - [x] An image type for RGBA pixels, later shared by the PNG writer and captures
  - [x] Icons drawn in code by Blink and the sandbox
- [x] Text rendering (ADR 0021)
  - [x] Drawing and measuring with a reusable style: size, color, alignment
  - [x] Anchors with horizontal and vertical alignment, several lines
  - [x] UTF-8 decoding, the missing glyph for unknown characters
  - [x] In window pixels or world units
  - [x] Layout apart from the drawing, tested without a GPU
  - [x] Blink's title screen, HUD and game over at a whole pixel scale

**Driving a game from an agent** (an LLM agent plays and checks Blink on its own, on every
platform and without clicking by hand; the sandbox exposes the same commands). In order.
- [x] Command system (ADR 0022)
  - [x] Named commands with arguments, one JSON line per reply
  - [x] Registered by the engine and by each game
  - [x] Scripts run at startup
  - [x] A local TCP server, polled without a thread; sockets in the platform layer
  - [x] The engine's command-line options
  - [x] Run control: pause, resume, step, wait, quit; status and help
  - [x] A command-line client
  - [x] Blink's state and a restart with a seed
- [x] Input injection (ADR 0023)
  - [x] Keys, buttons, mouse and wheel queued for the next step, seen as real input
  - [x] The same on every platform, without a display
  - [x] Injected input kept through focus loss
  - [x] Real input turned off, input state shown
  - [x] Keys and buttons by name
  - [x] Engine commands grouped by subsystem
  - [x] Blink played by an agent on Windows and under Xvfb
- [x] Screen capture (ADR 0024)
  - [x] A frame captured on command and saved as PNG
  - [x] Swapchain readback
  - [x] Own PNG writer
  - [x] Reusable layers: checksums (CRC-32, Adler-32), DEFLATE, zlib
  - [x] A test-only inflate; captured pixels checked by tests
  - [x] Blink seen by an agent on Windows and under Xvfb

**Tests**
- [x] Tests in parallel by default, none sharing a file or a port with another
- [ ] `app_run.frame_arena_is_reset_every_frame` fails about once in six runs of the window tests
      under load (`ctest -R "app_run|renderer" --repeat until-fail:6`): `rec.renders > 1` is false
      after a 12 s run, so frames went unrendered (window not yet shown or swapchain not ready?).
      Find the cause, then make the test wait for rendered frames instead of a step count

**Sandbox stats overlay** (the sandbox shows a frame-time graph built from quads, toggled with L;
these steps put numbers next to it, now that text can be drawn).
- [x] Numbers in the overlay (ADR 0025)
  - [x] Frames per second; average, 99th percentile and worst frame time
  - [x] Work, update and render time: the frame without its waits, measured by the engine
  - [x] Steps per second
  - [x] Memory per tag, the arenas with their committed pages, the process from the OS
  - [x] Validation counts
  - [x] GPU, present mode, vsync and swapchain size
  - [x] The same numbers as commands: `stats`, `memory.stats`, `renderer.info`
- [x] Decide whether the overlay stays in the sandbox or becomes an engine feature any game can
      toggle: the engine measures and publishes, the sandbox draws (ADR 0025, A3); an engine
      overlay comes with the debug UI or the first game that asks

**Continuous integration** (needed before the refactoring)
- [ ] GitHub Actions, tests on every push
  - [ ] Windows job
  - [ ] Linux job on the build container: publish the image (GHCR) or build it per run, to decide

## v0.3.0

**Orbit** (2D): a ball bounces inside a ring; rotate an arc along the ring to keep it in, while the
ring shrinks.
Likely needs: circle vs arc collisions (in its rules: the arc is a shape of its own), rotated
quads, sound effects (audio backend, mixer, WAV), pause and game states.

**Engine**
- [ ] Command registry on a hash map (ADR 0022, F2) instead of the linear scan of a fixed table:
      with the hash map of the backlog's containers, where optimizations start
- [ ] Vsync at run time: with vsync on, frame times sit at the refresh period; off, fps, 1% and
      worst show what the engine can do (ADR 0025)
  - [ ] `rb_renderer_set_vsync`: the swapchain rebuilt with the new present mode at the next frame
  - [ ] `renderer.vsync [on|off]` command
  - [ ] V in the sandbox; the stats panel shows the present mode change
  - [ ] A console variable once they exist

## v0.4.0

**Refactoring** (after Blink and Orbit, and threading if one of them brought it)
- [ ] File by file
  - [ ] Drop comments that restate the code
  - [ ] Review function, variable and file names
- [ ] Interfaces and duplication: logic repeated across backends moves to common code (platform
      startup/shutdown and window slots are identical in Win32 and Xlib)
- [ ] Components behind interfaces
  - [ ] Swappable backends (renderer, audio...): compile time vs function table vs dynamic
        plugins, to decide
  - [ ] A null backend next to each real one
  - [ ] The platform hands out native window handles instead of creating Vulkan surfaces
        (`platform_vulkan.h` makes the platform depend on the renderer)
  - [ ] Subsystem startup in the app
- [ ] Directory layout and file names
  - [ ] Public headers grouped by subsystem like `engine/src/` (e.g. `reborn/core/log.h`,
        `reborn/renderer/camera2d.h`): `engine/include/reborn/` is one flat folder mixing
        foundations (`defines.h`, `assert.h`, `log.h`), memory, platform, renderer, camera and math
  - [ ] An umbrella header, if games want one (to decide)
  - [ ] Where each file lives on both sides (clock and input under `platform/`, color under
        `core/`, camera under `renderer/`)
  - [ ] The top-level folders (`docker/`, `scripts/`, `cmake/`, `games/`, `tools/`)
  - [ ] Stateless OS services (`linux_*`) apart from the window system (`xlib_*`), so a Wayland
        backend replaces only the latter

## v0.5.0

**Seedling** (2D, top-down): walk a character around a tiled garden, collect seeds, plant and water
them before they wither.
Likely needs: sprite sheets (PNG decoding, file system), batched renderer, sprite
animation, tile map loaded from a file, a camera following the player, asset manager.

## Next games

In order; each gets a version when it starts.

- **Hollow** (2D platformer): levels, simple enemies, menus, saves. Likely needs: AABB physics,
  raycasts, entity model, level files (serialization), streamed music, gamepads, input mapping,
  menu UI.
- **Swarm** (2D): thousands of creatures moving as a flock and avoiding the player. Likely needs:
  job system, profiler, spatial structure for neighbour queries, performance tests.
- **Gallery** (3D): walk in first person through a room with a few objects and a light. Likely
  needs: 3D math, depth buffer, meshes from files, first-person camera with a locked mouse, basic
  lighting, raycasts (what the player looks at).
- **Archipelago** (3D): a small island to explore, with terrain, shadows and objects to interact
  with. Likely needs: terrain, shadows, 3D physics, transform hierarchies, larger scenes.

## Backlog

### Core
- [ ] Engine-provided entry point (ADR 0011, H3): `reborn/entry.h`; the game only defines a
      function returning its callbacks. When a GUI-subsystem build is needed, or command-line
      arguments in UTF-8 on Windows
  - [ ] `main` defined by the engine
  - [ ] `WinMain` for a Windows build without a console
  - [ ] `wmain`: `argv` in UTF-8 (it arrives in the ANSI code page today, ADR 0022)
- [ ] Application (ADR 0011)
  - [ ] Veto on the main window's close ("save before quitting?")
  - [ ] A variable `update` next to the fixed one, if camera or UI need it (C4)
  - [ ] Window events between steps, if a low update rate makes them lag
- [ ] Containers that own memory (allocator-aware, ADR 0010; macros vs `void*` + size to decide
      in their ADR, ADR 0007). With the first feature that needs one: likely the batched 2D
      renderer (sprites per frame) or the asset manager (name → handle)
  - [x] Generational handle pool and index-only ring buffer (v0.1.0, ADR 0007: they own no memory)
  - [ ] Dynamic array
  - [ ] Hash map (its first user may be the command registry, v0.3.0)
  - [ ] String / string builder
- [ ] Hash functions for the hash map, asset IDs and string interning (algorithm to decide), with
      the hash map. CRC-32 and Adler-32 exist (`core/checksum`), but are checksums for file
      formats, not hash functions for tables
- [ ] Threading (C11 `<threads.h>` vs own Win32/pthreads wrappers: to decide). With the first
      feature that needs it: likely the audio backend (a thread feeding the device) or
      asynchronous asset loading
  - [ ] Threads
  - [ ] Mutexes and condition variables
  - [ ] Atomics
  - [ ] Job system
- [ ] Thread safety, with threading
  - [ ] Decide which public functions must be callable from any thread (logger first, then event
        queue, allocators...)
  - [ ] State the guarantee in each function's doc comment
  - [ ] Protection per case (mutex, lock-free, per-thread data: to decide)
- [ ] Configuration separate from code: values hard-coded today (window size and title, log level
      and file, capacities...)
  - [x] A command-line parser for the engine's options (`--script`, `--listen`, `--help`,
        ADR 0022)
  - [ ] Command-line options of the game's own (e.g. `--seed`): an option the engine does not know
        fails the run today. When a game needs one
  - [ ] A config file (format and parser to decide)
- [ ] Logging (ADR 0008)
  - [ ] Categories with their own level
  - [ ] In-memory history, for the in-game console and the agents (see State inspection)
- [ ] Log file size cap and rotation: today the file is overwritten at startup and grows without
      limit during a run (a long session at TRACE, or a message logged every frame, fills the disk)
  - [ ] A maximum size per file; past it the file is closed and the next one opened
        (`reborn.log.jsonl`, `reborn.log.1.jsonl`...)
  - [ ] At most N files kept, the oldest deleted
  - [ ] Each file starts with the clock anchor line, so it can be read alone
  - [ ] Earlier runs kept the same way instead of overwritten (to decide)
  - [ ] A repeated message collapsed into one line ("repeated N times")
  - [ ] Sizes and count from the config
- [ ] Chained arena blocks (ADR 0010, B3), inside `rb_arena` without an API change. Only when one
      of these shows up: an arena whose size depends on content with no sensible upper bound
      (editor, mods, long sessions); reservations failing in a real environment (`ulimit -v`,
      Windows job objects); many small short-lived arenas (per asset, per entity), better carved
      from a parent allocator than each reserving address space; a target without virtual memory
      control (WebAssembly, some consoles)
- [ ] Error handling: public functions return `bool` and log today; decide whether callers need
      error codes to react (missing vs corrupt file, out of memory), and how they are named
- [ ] Fatal errors shown to the player: a message box when startup fails (no Vulkan driver, no
      window), not only a log line nobody reads
- [ ] Crash handling
  - [ ] Catch crashes: unhandled exceptions on Windows, signals on Linux
  - [ ] Write the stack trace to the log and flush it
  - [ ] Minidumps on Windows

### Profiling and performance
- [ ] Profiler: timed scopes per subsystem and per frame, to see where time goes and find
      bottlenecks (own implementation vs external tool: to decide)
- [ ] Resource usage
  - [x] Memory by tag: current, peak, allocation count per subsystem (ADR 0012, v0.1.0), logged at
        the end of the run
  - [x] Frame statistics: fps, frame time (average, 99th percentile, worst), work, update and
        render time (ADR 0025, v0.2.0)
  - [ ] Process CPU and memory from the OS
    - [x] Resident memory and its peak (ADR 0025, v0.2.0)
    - [ ] CPU time, if a game's performance work needs it
  - [ ] GPU, when a game loads real textures (Seedling's sprite sheets)
    - [ ] GPU memory: our device allocations (count, bytes) next to the tags
    - [ ] The heap's usage and budget (`VK_EXT_memory_budget`, where the driver has it)
    - [ ] GPU time per frame (timestamp queries)
- [ ] Viewer
  - [ ] In-game overlay any game can turn on: with the debug UI, or when a game asks; built on the
        engine's statistics (ADR 0025), drawn after the game's `render`
    - [ ] Per-frame times from the engine (the last frame, or a history of the last N): the
          sandbox's frame-time graph still times its own `render` calls
          (`sandbox_record_frame_time`), the same interval the loop already measures
  - [ ] Export: JSON lines for LLMs, trace format for external viewers
- [ ] Performance tests for data structures (handle pool, ring, future containers): `bench/`
      executable (ADR 0004), release build, timings not pass/fail (timer and approach to decide)

### Platform
- [ ] More platforms
- [ ] Wayland backend, selected at runtime next to Xlib (only if XWayland falls short, ADR 0006)
- [ ] Text input events (Unicode), separate from key events
- [ ] Relative mouse motion with a locked, hidden cursor (raw input / XInput2), for first-person
      cameras
- [ ] Key labels of the current layout, for prompts ("press Z" on AZERTY); key names exist for the
      US QWERTY positions (`rb_key_name`, `rb_key_from_name`)
- [ ] Mouse
  - [ ] Enter/leave events
  - [ ] Cursor shape
- [ ] Gamepads
- [ ] Input action mapping (bindings to actions)
- [ ] Window commands (ADR 0006, G3)
  - [ ] Set size
  - [ ] Minimize
  - [ ] Fullscreen
  - [ ] Maximized as a state
- [ ] Window icon beyond creation (ADR 0020). When a game needs it
  - [x] Icon at creation, at several sizes (v0.2.0)
  - [ ] Changing it after creation (`rb_window_set_icon`, the same code as at creation)
  - [ ] New sizes when a window moves to a monitor with another scale
- [ ] Application icon beyond the window's, with release packaging
  - [ ] Windows: embedded in the executable (`.rc` resource, shown by Explorer and pinned
        shortcuts)
  - [ ] Linux: a `.desktop` file
  - [ ] Icons loaded from image files (needs image decoding)
- [ ] Win32 modal loop: the game freezes while a window is dragged or resized (the loop clamps
      the frozen time, ADR 0011; running during the drag needs a timer or a window thread)
- [ ] Linux parity
  - [ ] Xlib error handlers (default ones exit the process): log X errors instead of exiting
  - [ ] Titles like on Windows: UTF-8 validation and length limit (Xlib passes bytes as-is)
  - [ ] HiDPI: read the desktop scale (`Xft.dpi`) if logical units must differ from pixels

### Rendering
- [ ] Rendering without a window (ADR 0013, K): for tools (asset baking, thumbnails), golden-image
      tests and CI machines without a display
  - [ ] `rb_renderer_startup` accepts a NULL platform: a device without surface or swapchain
  - [ ] Drawing to offscreen images
  - [ ] Captures read the offscreen image, which also covers GPUs whose window images cannot be
        copied (ADR 0024, B2)
- [ ] GPU override: choose the device by name or index (config or environment), e.g. to test the
      integrated GPU of a laptop or force lavapipe (ADR 0013, L3)
- [ ] Batched 2D renderer (sprites, textures): few draw calls instead of one per quad (ADR 0015,
      F1). `rb_renderer_draw_quad` and `_draw_texture` keep their signatures
  - [ ] The frame's quads collected into a vertex buffer
  - [ ] `quad.vert` reads vertices instead of push constants
  - [ ] A texture change breaks a batch: atlases vs bindless textures (one array of every texture
        bound once per frame, its index per vertex; ADR 0017, D2), to decide
- [ ] GPU memory block allocator (ADR 0017, B2). Why it will be needed: today every texture and
      buffer has its own allocation; Vulkan guarantees only 4096 alive at once (many Windows
      drivers allow exactly that), each allocation is a slow call into the OS kernel (a stutter if
      done mid-game), and drivers may round each one up to a large granularity, wasting memory on
      small resources. Blink's handful of textures is far from that; hundreds of sprites, glyph
      pages, per-frame vertex buffers and later meshes are not. With the batched renderer or the
      asset manager (Seedling), or earlier if the live allocation count nears the cap
  - [ ] A few large `vkAllocateMemory` blocks (e.g. 64 MiB) per memory type
  - [ ] Carved into the resources' allocations by our own code: offsets, alignment,
        `bufferImageGranularity` between buffers and images sharing a block
  - [ ] Allocations returned to the block when freed
- [ ] Texture uploads without a stall (ADR 0017, C2): a persistent staging buffer, copies recorded
      into the frame instead of a submit and wait each. With asynchronous asset loading /
      streaming
- [ ] Deferred texture destruction (ADR 0017, G2): queued until the frames in flight that may use
      it are done, instead of waiting for the whole GPU. With streaming
- [ ] Premultiplied alpha for textures: with straight alpha, linear filtering blends the color of
      transparent pixels into a sprite's edge (dark fringes). Decide with image decoding (PNG)
- [ ] Texture formats other than RGBA8 sRGB (single-channel font atlases, linear data): when
      memory or a feature needs them (ADR 0017, F3)
- [ ] Layers: today the call order is the layer order. With the batched renderer, which collects
      the frame's quads anyway; likely Seedling (map, characters, interface)
  - [ ] Each draw tagged with a layer (background, world, HUD, menus)
  - [ ] Each layer with its camera
  - [ ] Drawn in layer order whatever the call order
- [ ] Low-resolution pixel-art rendering: every game pixel a perfect square (ADR 0021). When a
      game wants that look (perhaps Seedling)
  - [ ] The whole frame (world, HUD, text) drawn into an offscreen image (e.g. 320x180), shared
        with rendering without a window
  - [ ] Enlarged by a whole factor onto the window
- [ ] Several cameras on the world at once (split-screen, minimap): a viewport rectangle in
      `rb_camera2d`, the part of the window its field goes into (today: the largest centered one);
      `rb_camera2d_make` gives it the whole window. When a game shows the world twice
- [ ] Rotation, when a game turns objects (Orbit's arc may be the first; ADR 0016)
  - [ ] Rotated quads: an angle (or a 2D transform) in the quad's data
  - [ ] Matrix multiplication
  - [ ] Camera rotation
- [ ] A color of their own for the camera's bars, where the window's aspect differs from the
      design area (today the clear color shows there). When a game wants it
- [ ] The game's name in `VkApplicationInfo` (drivers use it for per-game profiles), from its
      config
- [ ] Vulkan host allocations through `rb_allocator` (`pAllocator`), if tracking needs them
      (ADR 0013)
- [ ] Device lost: today the renderer logs it and stops drawing (ADR 0013); recreate the device
      and every GPU resource if a real driver reset shows up
- [ ] 3D

### Assets
- [ ] Where a game's assets live and how it finds them (ADR 0014, D1: deferred by Blink, which has
      none), e.g. `games/<name>/assets/` copied next to the executable. With the first game that
      loads a file: likely Orbit's sounds
- [ ] File system layer
  - [ ] Read and write whole files
  - [ ] Paths resolved from the executable's folder (not the current directory, which depends on
        how the game is launched)
  - [ ] Directory listing
- [ ] Asset manager
  - [ ] Assets referenced by handle (generational pool, ADR 0007)
  - [ ] Loaded once and shared
  - [ ] Unloaded when no longer used (reference counting vs explicit lifetimes: to decide)
- [ ] Image decoding (PNG at least) for textures and the icon (ADR 0024)
  - [x] CRC-32 and Adler-32 (`core/checksum`), zlib framing (`compression/`), with the PNG writer
  - [x] An inflate for stored and fixed-Huffman blocks, in `tests/support/`
  - [ ] The inflate moves into the engine, with dynamic Huffman codes (what other encoders write)
  - [ ] PNG chunks read and the writer's filters run backwards
- [ ] Asynchronous loading on the job system (see Threading)
- [ ] Hot reload: watch asset files and reload them while the game runs
  - [ ] Shaders first: embedded in the engine today, they become `.spv` files next to the
        executable (ADR 0015, E2)
  - [ ] The other assets
- [ ] Asset baking
  - [ ] Source files converted into engine-ready formats at build time
  - [ ] Packed into one archive for release builds (format to decide)

### Audio
- [ ] Backend per platform, on its own thread feeding the device
  - [ ] WASAPI on Windows
  - [ ] PulseAudio or ALSA on Linux (to decide)
- [ ] Mixer
  - [ ] Several sounds at once
  - [ ] Volume per sound and per group (music, effects)
- [ ] Sounds (own decoder vs external library: to decide)
  - [ ] WAV decoding
  - [ ] Sound effects, loaded whole
  - [ ] Music, streamed from disk
  - [ ] Compressed formats, later

### Text and UI
- [ ] Text layout beyond lines of glyphs (ADR 0021). When a game shows paragraphs (dialogue,
      menus' descriptions)
  - [x] `\n` lines, each aligned on its own; measuring (v0.2.0)
  - [ ] Word wrapping to a width
  - [ ] Kerning (with TrueType)
  - [ ] Colors per character
  - [ ] `rb_text_drawf`, if formatting numbers repeats everywhere
- [ ] HUD scale helper in the engine: the whole scale per N pixels of window height that keeps
      bitmap text sharp (ADR 0021, G1; in Blink today). When a second game computes it
- [ ] UI scaled from a reference resolution, with elements anchored to the window's edges and a
      continuous scale (ADR 0021): with smooth TrueType text, whose pixels stay even at any scale
      (likely Hollow's menus)
- [ ] TrueType fonts, own parser and rasterizer (ADR 0018): a new glyph source feeding the
      existing packer (`rb_font_create_ttf`, a size in pixels). With the first game showing text
      at several sizes or smooth text (menus: likely Hollow)
  - [x] The glyph source interface and the shelf packer (v0.2.0)
  - [ ] The `.ttf` file embedded at build time, or read through the file system layer
  - [ ] Tables `cmap`, `loca`, `glyf`, `hmtx`
  - [ ] Quadratic curves flattened to lines, coverage per pixel
  - [ ] The packer sorted by height, for TrueType's varied glyphs
  - [ ] Hinting and kerning, later
- [ ] Glyphs beyond ASCII
  - [x] Strings decoded as UTF-8, a missing or broken character shown as the missing glyph
        (ADR 0021)
  - [ ] A codepoint to glyph map behind `rb_font_get_glyph` (needs the hash map or a sorted table)
  - [ ] Glyphs beyond ASCII in the builtin font or from TrueType
- [ ] Debug UI, immediate mode (rebuilt every frame): base for the in-game console and the
      profiler overlay
  - [ ] Panels
  - [ ] Buttons and sliders
  - [ ] Value inspection

### Collision
Detection: do two shapes touch, where, in which direction. What happens then (a bounce, a point, a
lost life) is the game's rule. A test is born in the game that needs it and moves here when a
second game needs it too.
- [ ] Shapes and the tests between them (one function per pair vs a shape type: to decide in its
      ADR)
  - [ ] Point in rectangle: in Blink's rules today (`rect_contains`, edges included); with the
        second game that needs it, Blink then drops its own
  - [ ] Circles and axis-aligned boxes (AABB), and the tests between them: Orbit's ball is a
        circle, Hollow's world is boxes
  - [ ] Contact data for the response: normal and penetration depth
- [ ] Raycasts: the first shape hit by a ray (origin, direction, maximum distance), with distance,
      point and normal. With Hollow (ground under the feet, enemies' line of sight) or Gallery
      (what the player looks at or clicks in 3D)
- [ ] Moving shapes without tunneling: a fast shape tested along its whole path in a step, not
      only where it ends (swept tests). When something passes through a thin obstacle (likely
      Hollow's platforms)
- [ ] Broad phase: a grid or a tree that skips pairs far apart, instead of testing every pair.
      With Swarm (the spatial structure for neighbour queries)
- [ ] AABB physics: velocity, gravity, boxes pushed out of what they enter. With Hollow
- [ ] 3D shapes and queries (spheres, boxes, capsules, rays), with Gallery or Archipelago

### Scene
- [ ] Game object model: ECS vs object hierarchy vs plain arrays per type (to decide in its ADR)
- [ ] Transforms with parent-child relations, if the model needs them
- [ ] Scenes saved and loaded from files (needs serialization)
- [ ] Serialization: one mechanism for scenes, save games and config (text vs binary, format to
      decide)

### Editor
Needs the debug UI, the game object model and scene serialization; likely with Hollow, whose
levels are painful to write by hand.
- [ ] Editor mode and play mode
  - [ ] Built into the game's executable (debug builds only) vs a separate editor executable
        loading the game as a DLL (see game code hot-reload): to decide
  - [ ] The game starts in one or the other and switches at run time (e.g. a key)
  - [ ] Editor mode: the simulation paused (the commands' pause exists, ADR 0022), the world
        explored and changed
  - [ ] Play starts the game from the edited scene; going back restores the scene as it was
        before play (a snapshot through serialization)
- [ ] Free editor camera, independent of the game's
  - [ ] 2D: pan by dragging, zoom with the wheel
  - [ ] 3D: fly with the mouse (needs relative mouse motion)
- [ ] Selection and editing
  - [x] Screen-to-world and world-to-screen conversion (ADR 0016)
  - [ ] Pick objects with the mouse (a point query in 2D, a raycast in 3D; see Collision)
  - [ ] Move them with handles (gizmos)
  - [ ] Edit their fields in a panel (needs type information for the fields: reflection, to
        decide)
  - [ ] Undo/redo
- [ ] Scenes and objects from the editor
  - [ ] Scenes created, saved and loaded
  - [ ] Objects created, duplicated and deleted
- [ ] Commands from the editor (ADR 0022); shares the in-game console's input and history (debug
      UI)
  - [x] The registry lists every command, engine and game, with usage and summary (`help`)
  - [ ] A menu of the commands, run with a click
  - [ ] A field to type a command line, with the replies shown next to it

### LLM and automation tooling
Agents read the JSON lines log, `ctest`'s JUnit report and `compile_commands.json`; with the
commands they pause and step a running game, ask it for its state and play it with injected input.
With `screen.capture` they see what it draws. The command system, input injection and screen
capture are in v0.2.0 (driving Blink from an agent); these build on them.
- [ ] A page for agents listing commands, variables and output formats, linked from `CLAUDE.md`
  - [x] Commands and their replies: README's *Commands*, `docs/features/commands.md`, linked from
        `CLAUDE.md`
  - [ ] Console variables, when they exist
- [ ] Commands from the in-game console, with the debug UI
- [ ] Command server (ADR 0022)
  - [ ] Several clients at once, if agents need them (one at a time today)
  - [ ] Replies on stdout without the `\r` the Windows C runtime adds in text mode
- [ ] Console variables: named, typed values (speeds, toggles, log levels)
  - [ ] Read and set by command
  - [ ] Listed with their current value
  - [ ] Loaded from the config file, later
- [ ] State inspection: engine and game state as JSON
  - [x] `status`: frame, step, pause, game time, window (ADR 0022)
  - [x] `input.state`: keys, buttons and mouse as the engine sees them (ADR 0023)
  - [x] A game's own state (`blink.state`)
  - [x] Frame timing: `stats` (ADR 0025)
  - [x] Memory per tag, the arenas, the process: `memory.stats` (ADR 0025)
  - [ ] Renderer resources: the GPU and the swapchain in `renderer.info` (ADR 0025); textures and
        buffers not yet
  - [ ] Recent log lines (needs the log's in-memory history)
  - [ ] Objects and their fields, later
- [ ] Unattended runs: a scenario run in CI or Docker
  - [x] A script ends the game with `quit`, replies on stdout (ADR 0022)
  - [ ] `--frames N`, without a script
  - [ ] An exit code that reflects warnings, errors and leaks
  - [ ] A summary (frames, warnings, errors, leaks) as the last JSON line
  - [ ] No display needed (needs rendering without a window, see Rendering)
- [ ] Record and replay: a run reproduced exactly, e.g. a bug
  - [x] A deterministic simulation: seeded random number generator (ADR 0019), fixed step
        (ADR 0011)
  - [x] The injection queue, which replays through with real input off (ADR 0023)
  - [ ] Each fixed step's input and the seed written to a file
  - [ ] Played back from the file
  - [ ] A generator's state saved and restored, if a replay starts mid-run
- [ ] Real input off from the first frame (`--no-real-input`), if the frames before an agent's
      first command ever matter (ADR 0023, F2: a command today)
- [ ] `help <group>` (`help input`): the commands of a group, when the list grows past a screen
      (ADR 0023, E2)
- [ ] Golden-image tests: captures compared with reference images
  - [x] Captures (`screen.capture`, `rb_renderer_captured_image`, ADR 0024)
  - [ ] PNG decoding (see Assets)
  - [ ] Comparison with a tolerance, since drivers may round differently
- [ ] Faster captures, if agents take many: the PNG filter choice and DEFLATE are about 0.1 s for a
      1600x900 frame in release, up to 1 s in debug, on the main thread (ADR 0024)
  - [ ] Encoding on a thread
  - [ ] Dynamic Huffman codes, if file sizes matter
- [ ] Frame number on every log line, to match log lines with commands and screenshots (their
      replies already carry frame and step)

### Testing
- [ ] Framework (ADR 0004)
  - [ ] `longjmp`-based `RB_REQUIRE` and an `RB_ASSERT` handler hook, to test that asserts fire
  - [ ] Fixtures, when tests start repeating setup
  - [ ] ULP-based float comparison
- [ ] Integration tests for platform and renderer
  - [x] Tests that open a window, start the renderer and run the app (`tests/app/`, renderer
        tests)
  - [x] Captured pixels checked by GPU tests (ADR 0024)
  - [ ] Offscreen render (see Rendering)
  - [ ] Golden images (see LLM and automation tooling)
- [ ] Fuzzing with libFuzzer (`-fsanitize=fuzzer`) for parsers and containers
- [ ] Code coverage with Clang source-based coverage (`-fprofile-instr-generate`, `llvm-cov`)
- [ ] Tests that open a window show it on screen (`ShowWindow` is unconditional): a hidden-window
      option in the window config (alternatives and decision in an ADR; also lets a game show its
      window after the first frame), or render offscreen (see Rendering)
- [ ] CTest labels
  - [ ] `unit` / `window` on discovered tests (the runner reports them in `--list`):
        `ctest -LE window` is the quick loop
  - [ ] Reconcile with ADR 0004, which placed the window tests in `tests/integration/` with their
        own executable: today they live in `reborn_tests`
- [ ] Find why `vulkan_renderer.textures_fill_every_slot` is slow: 5 s on 2026-11-24 (each other
      Vulkan test ~1.2 s); on 2026-11-26, on Windows, 0.9 s serial but 7.4 s with `-j8`. Measure
      on Linux (lavapipe) too before digging
- [ ] One Vulkan device per suite (one process per suite) if the window tests stay slow after the
      above: about 50 tests start instance, device and swapchain from scratch (21 s of the 32 s
      serial run on Windows, 2026-11-26); trades away per-test isolation
- [ ] Runner
  - [ ] Filter (`--run 'arena.*'`) to run a suite in one process, quickest to debug
  - [ ] Time per test in its output (`[ OK ] arena.x (12 ms)`)
  - [ ] The slowest tests at the end
  - [ ] `--shuffle <seed>` (PCG32) to expose tests that depend on global state left by others
        (e.g. the log); when tests share a process (suite-level device, filter)
- [ ] `RB_SKIP(reason)` mapped to CTest's `SKIP_RETURN_CODE`: a test without a GPU or display is
      reported skipped, not passed or failed. When CI arrives
- [ ] Fail a test that leaks, through the tracking allocator (ADR 0012); when more tests start
      checking leaks by hand

### Build, CI and tooling
- [ ] Run `scripts/linux-build.sh` end-to-end on a Linux host (only syntax-checked so far)
- [ ] Per-platform dependency setup scripts (Clang, CMake, Ninja, Vulkan SDK; distros to support:
      to decide)
  - [ ] `winget` on Windows
  - [ ] The package manager on Linux
- [ ] Release packaging, built by a script or CI
  - [ ] One folder or archive per platform: the executable, packed assets, icon and (Linux)
        `.desktop` file
  - [ ] Installers, later
- [ ] Game code hot-reload as a DLL

### Documentation
- [ ] Doc comments on every public header and function: purpose, parameters, ownership, errors,
      thread safety (format, e.g. Doxygen-style `/** */` vs plain comments: to decide with the
      website)
- [ ] Documentation website: feature pages, ADRs and an API reference generated from doc comments
      (generator and hosting, e.g. GitHub Pages published by CI: to decide)

## v0.1.0 — Foundations (released 2026-11-23)

- [x] Project setup (ADR 0001, 0002, 0003, 0005)
  - [x] Folder layout, git
  - [x] CMake with presets: Clang, Ninja, C17
  - [x] Static engine library
  - [x] Single-source version
  - [x] Docs: changelog, roadmap, ADRs, feature pages, notes for agents
  - [x] Every preset built on Windows and on Linux (Docker)
- [x] Unit testing (ADR 0004)
  - [x] Own framework: test macros, expectations, auto-registration
  - [x] CTest integration: one entry per test, JUnit output
  - [x] Tests mirroring the engine's folders
- [x] Platform layer and windows (ADR 0006, 0007)
  - [x] Platform abstraction; Win32 and Xlib backends
  - [x] Generational handle pool and index-only ring buffer
  - [x] Event queue
  - [x] Window lifecycle: resize, focus, minimize, move
  - [x] Keyboard and mouse by physical key position
  - [x] Linux scripts gate on the tests; the sandbox runs on a Linux desktop
- [x] Logging (ADR 0008), before the windows were finished, to see their events
  - [x] Levels, colored console, file
  - [x] Asserts
  - [x] Structured output (JSON lines) for LLMs
- [x] Game lifecycle (ADR 0009, 0011)
  - [x] Monotonic clock and sleep, also behind the logger's timestamps
  - [x] Application interface: configure, init, events, update, render, shutdown
  - [x] The game's state allocated by the engine
  - [x] Game loop: frame timing, per-frame arena, optional frame cap
  - [x] Fixed timestep with an accumulator, one pump per step
- [x] Memory (ADR 0010, 0012)
  - [x] Virtual memory: reserve, commit, decommit, release
  - [x] Arena over virtual memory or a caller's buffer, temp scopes
  - [x] Allocator interface, arena adapter, heap allocator; systems start from an allocator
  - [x] ASan poisoning inside arenas
  - [x] Failing allocator for tests
  - [x] Allocation tracking by tag, leak report, arena high-water mark
- [x] Vulkan renderer (ADR 0013, 0015)
  - [x] Vulkan loaded at run time
  - [x] Vulkan 1.3 instance, validation layers, debug messenger
  - [x] Surface, GPU choice (a CPU implementation as last resort), device, swapchain rebuilt on
        resize
  - [x] Command buffers, synchronization, two frames in flight
  - [x] Linear colors, the clear color set by the game
  - [x] Shaders compiled at build time and embedded, a quad pipeline
