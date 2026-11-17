# 0013 — Vulkan renderer

- **Status:** Proposed
- **Date:** 2026-11-17

## Context
The renderer draws through Vulkan on Windows and Linux. Every Vulkan call goes through the
*loader* (`vulkan-1.dll`, `libvulkan.so.1`), which comes with the GPU driver, finds the drivers
and layers and dispatches calls to them. The Vulkan SDK (headers, validation layers, shader
compilers) is installed on the Windows development machine; the Linux container only has the
loader. The engine must not need external libraries without approval, and a machine without
Vulkan should get a logged error, not a crash. This ADR grows with the renderer, one group per step.

## Options
**A. Where the headers come from**
1. Vulkan SDK on Windows, distro packages on Linux (`libvulkan-dev`), found by
   `find_package(Vulkan)` — nothing added to the repository; both are needed anyway for validation
   layers and shader compilers; the header version depends on the machine.
2. `Vulkan-Headers` copied into the repository at a fixed version — reproducible, builds without
   the SDK; external code to update by hand, the SDK is still needed for layers and shaders.

**B. How the engine reaches the loader**
1. Link to it (`vulkan-1.lib`, `-lvulkan`), like `user32` or `X11` — no code; without a loader the
   OS refuses to start the executable (a system dialog on Windows, nothing in our log); every call
   goes through the loader's dispatch.
2. Open it at run time (`LoadLibrary` / `dlopen`), take `vkGetInstanceProcAddr` and get every other
   function from it, with `VK_NO_PROTOTYPES` — a missing Vulkan is a logged error; device functions
   skip the loader's dispatch; a function list to maintain and a dynamic library API in the platform
   layer (which game code hot-reload needs later too).
3. `volk` (single-file library doing B2) — tested code; an external dependency, less to learn.

**C. Where the function pointers live** (B2, B3; device-level pointers belong to one device: two
GPUs have two drivers)
1. Globals named after the functions, inside the Vulkan backend — code reads like the spec and
   tutorials; one device at a time.
2. Tables in the structs of the object they were loaded for (loader, instance, device) — several
   devices at once, explicit state as in the rest of the engine; longer calls, the table must reach
   every function that calls Vulkan.

**D. Minimum Vulkan version**
1. 1.0 — runs on the oldest drivers; render passes, framebuffers and the original barriers, many
   extensions to query.
2. 1.2 — adds timeline semaphores, descriptor indexing, buffer device address; still render passes.
3. 1.3 — adds dynamic rendering (no `VkRenderPass` / `VkFramebuffer`) and synchronization2 as core;
   drivers since 2022 (lavapipe included), drops some older GPUs.
4. 1.4 — newest features; drivers since 2024–2025, fewer machines.

**E. When validation layers are on** (they check every call and are slow)
1. Debug builds only; a missing layer is a warning — simple; release can still force them from
   outside (Vulkan Configurator, loader environment variables).
2. Default from the build type, plus a configuration field the game can flip — validation in a
   release build, or off in a debug one, without external tools.

**F. What happens to their messages** (delivered by our callback, `VK_EXT_debug_utils`, while the
offending call has not returned yet)
1. Logged at the matching level — the log names the Vulkan function, not our line; nobody notices
   an error nobody reads.
2. Logged and counted (errors, warnings) — tests can require zero; the run's totals are logged.
3. As 2, plus a stop in the debugger, when one is attached, on the first occurrence of each error
   — the call stack shows our offending line; an error repeated every frame stops only once.

**G. Who sees the renderer**
1. Internal module until drawing exists — no API to freeze early.
2. Public `reborn/renderer.h` now — the shape is settled step by step with the game in view; the
   signature will change when the surface arrives (step 3), which is allowed before v0.1.

**H. Who starts it**
1. The application layer, like the platform: started after the main window, stopped before it;
   reached with `rb_app_renderer`; configured in `rb_app_config.renderer` — no boilerplate per game.
2. The game, in its init and shutdown — more control, the same code in every game.

**I. Validation field in the configuration**
1. `bool validation`, prefilled from the engine's build type — one field; a zeroed config means off.
2. Enum `DEFAULT` / `ON` / `OFF` — a zeroed config follows the build type; one more type.

**J. Who creates the surface** (OS-specific: `VK_KHR_win32_surface` takes an `HWND`,
`VK_KHR_xlib_surface` a `Display *` and a `Window`)
1. The platform, through an internal `platform/platform_vulkan.h` (the instance extension it needs,
   and surface creation), implemented by each backend — OS code stays in `platform/<os>/`, native
   handles never leave the platform, no `#ifdef` in the renderer; the platform knows Vulkan.
2. The renderer, from native handles the platform exposes as `void *` — the platform ignores
   Vulkan; the renderer includes `<windows.h>` and Xlib behind `#ifdef`, against the layout rules.
3. The renderer, in per-OS files under `renderer/vulkan/` — clean, but widens the rule on where OS
   code may live.

**K. A renderer without a window** (tools, offscreen rendering, servers and CI without a display)
1. Window optional: `platform` NULL creates a device without surface or swapchain — tests run
   anywhere; two paths through device selection, for a use no game has yet.
2. Window required — one path; tests that start the renderer need a display. Headless stays
   possible later as an addition (NULL accepted) without changing the signature.

**L. Which GPU** (fixed requirements: device Vulkan 1.3, VK_KHR_swapchain, a queue family as N)
1. The first suitable, in the loader's order — least code; the order is the loader's and the
   OS's, a laptop may get its integrated GPU.
2. Ranked by type: discrete > integrated > virtual > CPU, ties in the loader's order — predictable,
   picks the strongest.
3. 2 plus an override (config or environment, by name or index) — nobody needs it yet, and the
   configuration file does not exist yet.

**M. CPU implementations** (lavapipe, SwiftShader)
1. Last resort, with a warning — runs everywhere; a Linux player whose GPU driver is too old gets
   a slideshow instead of an error (Mesa installs lavapipe next to the real driver).
2. Only when the config allows it — a clear error for players; tests and sandboxes set a field.
3. Never — no device at all where only lavapipe exists (containers, CI).

**N. Queues**
1. One family that does both graphics and presentation, required — one queue, swapchain images
   never change family; rejects GPUs with split families (none among desktop GPUs in practice).
2. Split families supported — more general; shared images or ownership transfers, code our
   hardware would never run.

**O. Vsync** (present mode: `FIFO` waits for the vertical blank and is always available; `MAILBOX`
replaces the queued image, no tearing but the GPU renders unbounded; `IMMEDIATE` tears)
1. Always FIFO.
2. `bool vsync` in the config: FIFO when on; otherwise MAILBOX, then IMMEDIATE, then FIFO — uncapped
   runs for profiling; one field.
3. An enum of present modes in the config — a Vulkan concept in the game's hands.

**P. Swapchain color format**
1. sRGB (`B8G8R8A8_SRGB` / `R8G8B8A8_SRGB`, sRGB nonlinear): shaders write linear values, the
   GPU encodes them — correct blending and lighting; colors picked as sRGB must be linearized.
2. UNORM: values reach the screen as written — what you write is what you see; blending happens
   in gamma space and gradients come out too dark.

**Q. When the swapchain is recreated** (its extent is fixed at creation; a window with zero pixels
cannot have one)
1. The renderer compares the window's pixel size with the swapchain's at the start of each frame
   (plus `VK_ERROR_OUT_OF_DATE_KHR` once frames are presented) — no wiring to events, nothing lost
   when the queue drops one; needs a per-frame entry point now.
2. The application forwards `RB_EVENT_WINDOW_RESIZED` — one more link, depends on events.
3. Only on Vulkan's out-of-date errors — least code; drivers are not required to report them (some
   X11 drivers do not): a stretched image.

**R. What step 4 covers** (roadmap: pipeline, command buffers, synchronization)
1. All three: the first visible result is a triangle.
2. Command buffers, synchronization and a cleared screen presented every frame; shaders and the
   pipeline move to step 5 with the triangle — synchronization is checked on its own, and the
   shader choices (language, compiler, files or embedded) come with the step that uses them.

**S. Frames in flight** (frames the CPU records while the GPU still works on earlier ones; each
needs its own command buffer, semaphores and fence)
1. One — the CPU waits for the GPU every frame: they never work in parallel.
2. Two, an internal constant — the usual choice.
3. Three — more slack for spikes; more input latency and memory.

**T. Presentation semaphores** (acquire signals A when the image is ready; the submit waits on A
and signals B; present waits on B. Present signals nothing back, so nothing says when B is free)
1. A and B per frame in flight — B may be reused while a present still waits on it; recent
   validation layers report it.
2. A per frame in flight, B per swapchain image — B is reused only when its image comes back from
   acquire, which means its present is over; core 1.3, no extension.
3. `VK_EXT_swapchain_maintenance1` present fences — exact; an extension not every driver has,
   so two paths.

**U. Command buffers**
1. One pool per frame in flight, reset whole at the start of the frame (`vkResetCommandPool`) —
   the cheapest reset, like the frame arena.
2. One pool, buffers reset one by one — one object fewer; more work for the driver.

**V. How the app drives a frame**
1. Internal `rb_renderer_begin_frame` / `rb_renderer_end_frame` around the game's `render`; later
   public draw calls record into the current command buffer — extends Q1.
2. One `rb_renderer_draw_frame` at the end, recording what the game queued during `render` — the
   renderer owns the whole frame; that is the batched 2D renderer's design (backlog).

**W. Clear color** (until the game draws, the only thing on screen)
1. An internal constant — no API.
2. A field of `rb_renderer_config` — set once in configure.
3. Public `rb_renderer_set_clear_color`, callable any time — the sandbox can animate it, which
   shows frames are presented.

**X. Frame cap with vsync** (ADR 0011, G: the cap stood in for vsync)
1. Keep `max_fps` 60 by default — two pacers: 60 fps on a 144 Hz monitor, and the sleep and the
   wait for the vertical blank can drift apart into stutter.
2. `max_fps` uncapped by default — the present paces frames; the cap stays for vsync off or to
   save power.

**Y. Color space of the game's colors** (the swapchain is sRGB, P1: shaders and clears write
linear values, the GPU encodes them; color pickers and CSS give sRGB values)
1. Linear — what Vulkan takes; a picked color must be converted by hand, and forgetting gives a
   washed-out color with no warning.
2. sRGB, converted by the renderer — what you write is what the picker shows; every color entering
   the renderer is converted.
3. Two constructors, `rb_color_srgb` and `rb_color_linear`; the value inside is always linear —
   the caller states where the color comes from; one more concept.

**Z. Where the color type lives**
1. `reborn/renderer.h` — its only user today.
2. Its own `reborn/color.h` — sprites, UI and text will use it without depending on the renderer.

## Decision
**A1, B2, C2, D3, E2, F3, G2, H1, I1, J1, K2, L2, M1, N1, O2, P1, Q1, R2, S2, T2, U1, V1, W3, X2,
Y3, Z2.**
- **Version:** Vulkan 1.3, for instance and device: less code (no render passes) and the modern
  synchronization API, on every GPU we target. A loader offering less fails the startup (logged).
- **Public API** (`reborn/renderer.h`): opaque `rb_renderer`, `rb_renderer_startup(allocator,
  config)` (NULL on failure, nothing left allocated; state tagged `RB_MEMORY_TAG_RENDERER`),
  `rb_renderer_shutdown`, `rb_renderer_get_validation_stats`. The app starts it after the main
  window and stops it before; a failure fails `rb_app_run`.
- **Validation:** `rb_renderer_config.validation`, prefilled `true` in debug builds of the engine.
  It needs the `VK_LAYER_KHRONOS_validation` layer and the `VK_EXT_debug_utils` extension; if
  either is missing, a warning and the run goes on without validation.
- **Messages:** the messenger is also chained to `VkInstanceCreateInfo`, so instance creation and
  destruction are checked too. Severity maps to log levels (verbose → TRACE, info → DEBUG,
  warning → WARN, error → ERROR); the loader's general info is TRACE, as it narrates its search
  for layers and drivers in about a hundred lines per instance. Only messages of the *validation* type are counted. The first
  error of each message ID (up to 64 IDs) stops in an attached debugger, through the same break
  `RB_ASSERT` uses (`core/debug_break.h`). The renderer logs the totals at shutdown.
- **Vulkan's own host memory:** `pAllocator` is NULL (the driver's allocator) for now; routing it
  through `rb_allocator` and the tags is possible later if tracking needs it.
- **Headers:** CMake's `Vulkan::Headers` target only (no `Vulkan::Vulkan`, which would link the
  loader). Engine code includes `"renderer/vulkan/vulkan_api.h"`, never `<vulkan/vulkan.h>`
  directly: it defines `VK_NO_PROTOTYPES` first, so no file can call a function that would need
  the loader at link time.
- **Dynamic libraries** (`platform/platform_os.h`, internal, stateless): `rb_platform_library_open`
  / `_symbol` / `_close`, with `LoadLibraryExW` restricted to the application folder and System32
  (`LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`: no DLL planted in the current folder or on `PATH`) and
  `dlopen(RTLD_NOW | RTLD_LOCAL)`. A symbol is a generic function pointer `rb_platform_proc`,
  since ISO C forbids converting `dlsym`'s `void *` to a function pointer by a cast.
- **Loader** (`renderer/vulkan/vulkan_loader.c`): opens `vulkan-1.dll` / `libvulkan.so.1` (the
  versioned name: `libvulkan.so` exists only with the development package) and fills the global
  table (functions callable before an instance exists). `vkEnumerateInstanceVersion` is optional:
  a Vulkan 1.0 loader lacks it, which means version 1.0.
- **Function lists:** one X-macro per level (global, instance, device) names each function once;
  it generates the table fields and the loading code. Fields keep the full Vulkan name
  (`loader->global.vkCreateInstance`), so the spec's names are searchable in the code.
- **Startup signature:** `rb_renderer_startup(allocator, config, platform, window)`. The renderer
  keeps `platform` to read the window's size, so the platform must outlive it.
- **Surface:** `rb_platform_vulkan_surface_extension()` names the OS extension, enabled with
  `VK_KHR_surface` (missing: startup fails); `rb_platform_vulkan_create_surface` loads the OS
  function from the instance and creates the surface. The renderer destroys it (`VK_KHR_surface`).
- **GPU:** a candidate offers device API 1.3, `VK_KHR_swapchain` and a queue family with graphics and
  presentation to the surface (dynamic rendering and synchronization2 are mandatory in 1.3, so the
  version is enough; both are enabled at device creation). Every GPU is logged at INFO with its
  verdict (suitable, or why not), so a failure explains itself; a CPU implementation chosen adds a
  warning. One queue.
- **Swapchain:** `rb_renderer_config.vsync`, prefilled `true` by the app. Images: the driver's
  minimum + 1, within its maximum and the engine's array (8). Format: `B8G8R8A8_SRGB`, then
  `R8G8B8A8_SRGB` (sRGB nonlinear), else the first offered, with a warning. One image view per
  image. The first creation is logged at INFO, recreations at DEBUG (a drag makes many).
- **Recreation:** internal `rb_renderer_prepare_frame` (`renderer/renderer_internal.h`), called by
  the app each frame instead of the minimized check, says whether the frame can be shown: false
  while minimized (Windows reports a 0x0 surface then) or with zero pixels.
  When the window's pixel size differs from the one the swapchain was built for, it waits for the
  device to go idle and recreates it, passing the old one as `oldSwapchain`. A failure is logged
  once: no swapchain until the size changes again.
- **Frame:** `rb_renderer_begin_frame` replaces `rb_renderer_prepare_frame`; when it returns true
  the app calls the game's `render`, then `rb_renderer_end_frame`. Begin: the checks above, wait
  for the frame's fence, acquire an image, reset the fence and the frame's command pool, record a
  barrier to `COLOR_ATTACHMENT_OPTIMAL` and begin dynamic rendering, which clears to the clear
  color. End: end rendering, barrier to `PRESENT_SRC_KHR`, `vkQueueSubmit2` (waits on the
  acquire semaphore at the color output stage, signals the image's semaphore and the fence),
  present. Barriers and the submit use synchronization2. Begin checks everything that can fail
  before acquiring: once an image is acquired, the frame is always submitted and presented.
- **Frames in flight:** `RB_VK_FRAMES_IN_FLIGHT` 2, each with a command pool, its command buffer,
  the acquire semaphore and a fence created signaled (the first wait returns at once). The
  presentation semaphore is one per swapchain image, created and destroyed with the swapchain.
- **Out of date:** `VK_ERROR_OUT_OF_DATE_KHR` from acquire skips the frame, `VK_SUBOPTIMAL_KHR`
  goes on. Either, from acquire or from present, marks the swapchain stale: the next begin
  rebuilds it, as for a size change. Any other error (device lost,
  out of memory) is logged and stops drawing for good: nothing more is presented.
- **Clear color:** `rb_renderer_set_clear_color(renderer, color)`, used from the next frame begun
  (rendering begins before the game's `render`); a dark gray until set.
- **Frame cap:** the app's `max_fps` defaults to `RB_APP_FPS_UNCAPPED` (ADR 0011, G1 stays: the
  cap is still applied when set).
- **Color** (`reborn/color.h`): `rb_color {f32 r, g, b, a}`, always linear with straight alpha.
  `rb_color_srgb` converts each channel with the exact sRGB curve (clamped to [0, 1]); alpha is
  never converted (it is coverage, not light). `rb_color_linear` takes the values as they are.
  The engine links the C math library (`-lm` on Linux) for `powf`.

## Consequences
- The executable starts without Vulkan; the renderer reports it and the engine decides what to do.
- Using a new Vulkan function means adding it to the right list first; forgetting it is a compile
  error, not a crash.
- Every function that calls Vulkan receives the struct that holds its table.
- The Linux build container needs `libvulkan-dev`, a driver (`mesa-vulkan-drivers`: lavapipe
  runs Vulkan on the CPU, so tests need no GPU) and `vulkan-validationlayers`.
- A game cannot run on a machine without Vulkan 1.3, even one that never draws (tools).
- The platform layer depends on the Vulkan headers (internal header and backends only).
- Tests that start the renderer need a window: they are built on Windows only, like the
  application runs; Linux checks them through the sandbox under Xvfb.
- A clear color set during `render` shows one frame later; with draws (step 5) the game will
  record into the frame begun before `render`.
- A swapchain rebuild waits for the device to go idle, so a drag stalls the GPU at every size;
  acceptable for resizes.
- Where FIFO does not wait for the refresh, frames are unbounded unless `max_fps` is set: Mesa's
  lavapipe on X11 (Xvfb, WSLg) runs the sandbox at hundreds of frames per second.
