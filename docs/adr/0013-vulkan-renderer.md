# 0013 — Vulkan renderer

- **Status:** Proposed
- **Date:** 2026-11-17

## Context
Roadmap §7 renders through Vulkan on Windows and Linux. Every Vulkan call goes through the
*loader* (`vulkan-1.dll`, `libvulkan.so.1`), which comes with the GPU driver, finds the drivers
and layers and dispatches calls to them. The Vulkan SDK (headers, validation layers, shader
compilers) is installed on the Windows development machine; the Linux container only has the
loader. The engine must not need external libraries without approval, and a machine without
Vulkan should get a logged error, not a crash. This ADR grows with §7, one group per step.

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

## Decision
**A1, B2, C2, D3, E2, F3, G2, H1, I1.**
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

## Consequences
- The executable starts without Vulkan; the renderer reports it and the engine decides what to do.
- Using a new Vulkan function means adding it to the right list first; forgetting it is a compile
  error, not a crash.
- Every function that calls Vulkan receives the struct that holds its table.
- The Linux build container needs `libvulkan-dev`, a driver (`mesa-vulkan-drivers`: lavapipe
  runs Vulkan on the CPU, so tests need no GPU) and `vulkan-validationlayers`.
- A game cannot run on a machine without Vulkan 1.3, even one that never draws (tools).
