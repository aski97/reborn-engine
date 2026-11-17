# 0015 — Shaders and graphics pipeline

- **Status:** Accepted
- **Date:** 2026-11-17

## Context
The renderer (ADR 0013) clears and presents frames; drawing anything else needs a graphics
pipeline. Vulkan has no fixed-function drawing: a pipeline needs at least a vertex and a fragment
shader, programs the GPU runs once per vertex and once per covered pixel. Vulkan takes shaders as
SPIR-V, a portable bytecode the driver compiles for its GPU when the pipeline is created, so the
source language needs a compiler and the result a way into the program. The whole pipeline
(shaders, blending, target format...) is one immutable object, costly to create and cheap to use.
The first game (Blink) needs colored squares at any position and a click test against the mouse.
No external libraries without approval; there is no file system layer yet.

## Options
**A. What this step delivers**
1. A triangle fixed in the renderer; the quad comes with Blink — least code; every game would see
   it, and it gets thrown away.
2. Triangle, then quad, both fixed in the renderer — the same, thrown away later.
3. The triangle as an intermediate check, then a public `rb_renderer_draw_quad` called from the
   game's `render`; the sandbox draws a few — what Blink needs; one more public function.

**B. Quad coordinates** (A3)
1. Window pixels, origin top left, y down: those of `rb_mouse_position` — a click test is a
   comparison; a resized window shows more or less, the game scales by itself.
2. Vulkan's normalized coordinates (-1..1) — no conversion; squares stretch with the window's
   aspect, the mouse must be converted.
3. World units through a 2D camera (orthographic projection) — independent of the resolution;
   needs the math library.

**C. Shader language**
1. GLSL — Vulkan's usual language, used by most tutorials; compilers in the SDK and Ubuntu packages.
2. HLSL through DXC — DirectX's language, common in commercial engines; fewer Vulkan examples, DXC
   rarely packaged on Linux.
3. Slang — modules, generics, now under Khronos, in the SDK; few examples, not packaged by Ubuntu.

**D. Who compiles, and when**
1. `glslc` at build time, one CMake rule per shader — a broken shader fails the build; it reports
   the files a shader includes (depfile), so CMake recompiles when they change. In the SDK on
   Windows, package `glslc` on Ubuntu.
2. `glslangValidator` at build time — the Khronos reference compiler; no depfile.
3. At run time, linking shaderc — hot reload is easy; a large external library, errors found only
   when running.
4. Compiled `.spv` committed — no tool needed to build; binaries that drift from their source.

**E. How SPIR-V reaches the program**
1. Embedded in the executable: `glslc -mfmt=c` writes the words as a C initializer list, included
   by the renderer — no files to find or lose; no hot reload.
2. `.spv` files next to the executable, read at startup — assets like any other, open to hot
   reload; needs the file system layer first.
3. E1 now for the engine's own shaders; files when a game brings its own shaders or hot reload
   arrives.

**F. Quad data**
1. Corners computed in the vertex shader from `gl_VertexIndex`; rectangle, color and target size
   as push constants (bytes recorded into the command buffer with the draw) — no GPU memory; one
   draw per quad: fine for tens, not thousands.
2. A vertex buffer — the batched renderer's way; brings GPU memory allocation now, which only many
   sprites need.

**G. Viewport and scissor** (the area of the image drawn into; it changes with the window)
1. Dynamic state, set every frame — the pipeline survives resizes.
2. Baked into the pipeline — the pipeline is rebuilt at every size, slowly, during a drag.

**H. Blending** (`rb_color` has straight alpha)
1. Off: alpha ignored — simplest; a translucent color comes out opaque with no warning.
2. Straight alpha, `src * a + dst * (1 - a)` — `rb_color`'s alpha works (a square fading out);
   draw order matters.
3. Premultiplied alpha — also right for filtered textures; `rb_color` would change convention.

## Decision
**A3, B1, C1, D1, E3, F1, G1, H2.** B3 is in the backlog with the 2D camera.
- **API:** `rb_renderer_draw_quad(renderer, x, y, width, height, color)` in `reborn/renderer.h`,
  `f32` pixels of the client area (fractions allowed). It records into the frame begun before the
  game's `render` (ADR 0013, V1); outside a frame it is a bug (`RB_ASSERT`). Drawn in call order,
  each on top of the previous ones.
- **Shaders:** `engine/src/renderer/vulkan/shaders/` (`quad.vert`, `quad.frag`; the push constant
  block in `quad.glsl`, included by both). GLSL in Vulkan's dialect, so they belong to the backend.
- **Build:** `find_package(Vulkan REQUIRED COMPONENTS glslc)`; each shader compiles with
  `--target-env=vulkan1.3 -Werror -mfmt=c` and a depfile into `<build>/engine/generated/shaders/
  <name>.inc`, listed as a source of the engine so it is built first, included by
  `vulkan_pipeline.c` as `static const u32 ...[] = { ... };`.
- **Push constants:** `rb_vk_quad_push` (40 bytes: position, size, color, target size) mirrors
  the GLSL block (std430: the `vec4` at offset 16); `_Static_assert`s on its offsets and size catch
  a mismatch at compile time. Both stages read it.
- **Pipeline:** one, created at renderer startup with the swapchain's format (dynamic rendering
  bakes the format in); startup fails if it cannot be created. Rebuilt only when a new swapchain
  comes with another format; a failure then stops drawing (logged). No vertex input, triangle
  list, no culling (2D has no back side), one sample, straight alpha blending, viewport and scissor
  dynamic. Shader modules are destroyed right after the pipeline is created. No pipeline cache
  (needs files; drivers keep their own).
- **Frame:** begin binds the pipeline and sets viewport and scissor to the swapchain's extent
  after the clear; each quad is a push of its constants and a 6-vertex draw.

## Consequences
- Blink can draw its squares and test clicks with the mouse's own coordinates.
- Building needs `glslc`: the Vulkan SDK on Windows, `glslc` in the Linux container and on any
  Linux machine that builds the engine.
- A shader change recompiles the engine; no hot reload until shaders become files (E2).
- One draw call per quad: thousands of sprites need the batched renderer and vertex buffers.
- Changing the push constant block means changing `quad.glsl` and `rb_vk_quad_push` together;
  the static asserts catch offsets and size, not the meaning of the fields.
