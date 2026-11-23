# 0016 — Math library and 2D camera

- **Status:** Accepted
- **Date:** 2026-11-23

## Context
`rb_renderer_draw_quad` takes window pixels (ADR 0015, B1): a game's layout depends on the window's
resolution, and a smaller window cuts the scene. Blink needs a field in its own units, the same on
every window, and the mouse converted into those units to test clicks; its score and lives stay in
window pixels. A camera maps world units to the GPU's normalized coordinates (-1..1); that mapping
is an orthographic projection, usually written as a 4x4 matrix (homogeneous coordinates: the fourth
component, 1, turns a translation into a multiplication, so transforms combine by multiplying).
The quad shader already does a fixed version of it: `pixel / target * 2 - 1`. There is no math
code in the engine yet. Viewport and scissor are dynamic state, set at each frame's begin
(ADR 0015, G1).

## Options
**A. What the camera hands the shader**
1. `rb_vec2` + `rb_mat4` — the standard form, used by the shader as is; rotated quads (Orbit) and 3D
   reuse it; 64 bytes of push constants, within the 128 guaranteed.
2. Position and scale only (4 floats) — enough for Blink, which rotates nothing; replaced by the
   next game.
3. `rb_vec2` + a 3x3 matrix — enough for 2D, smaller; 3D adds the 4x4 anyway (two types), and std430
   pads each column to 16 bytes.

**B. Function style**
1. By value: `c = rb_vec2_add(a, b)` — reads like the math; small structs are cheap to copy.
2. Output pointers: `rb_vec2_add(&c, &a, &b)` — no copies; verbose, no nested expressions.

**C. Where the math code lives**
1. Header-only, `static inline` functions in `reborn/math.h` — no call overhead in release builds;
   what most game math libraries do.
2. Functions in a `.c` with `RB_API`, like the rest of the engine — one call per operation.

**D. Matrix memory layout** (A1)
1. Column-major, element (row r, column c) at `m[c * 4 + r]` — how GLSL reads a `mat4`: passed to
   the GPU as is.
2. Row-major — reads like the matrix on paper; transposed before the GPU, or `row_major` in GLSL.

**E. What the camera shows when the window's aspect differs from the field's**
1. Fixed height, width following the window — more or less world at the sides.
2. The whole area always, with bars where space is left (letterbox) — the field is the same
   everywhere; the renderer restricts viewport and scissor to it.
3. Fixed pixels per unit — a larger window shows more world.

**F. World y axis**
1. Down, as window pixels, the mouse and Vulkan — one direction in the whole engine.
2. Up, as in math and Unity — the projection and the mouse conversion flip it.

**G. Choosing world or pixel space for each draw**
1. A current camera: `rb_renderer_set_camera(renderer, &camera)` for the world, `NULL` for window
   pixels (as raylib's `BeginMode2D` / `EndMode2D`) — `rb_renderer_draw_quad` keeps its signature;
   hidden state.
2. The camera passed to every draw — no hidden state; one more argument, a changed signature.

**H. Where the camera lives**
1. `reborn/camera2d.h`, code in `engine/src/renderer/camera2d.c`, outside `vulkan/` — any backend
   uses it; the Vulkan code sees only a matrix and a rectangle.
2. Inside `reborn/renderer.h` — one header less; game logic converting coordinates includes the
   whole renderer.
3. Header-only like the math — longer functions compiled into every file that includes them.

**I. Camera fields**
1. `center` + `size` (the area to show) — zooming is a smaller `size`; adding a field later leaves
   it zero in every game that initializes the struct by hand.
2. `center` + `size` + `zoom`, built by `rb_camera2d_make` — `size` is the design area and sets the
   bars, `zoom` an effect, one number to animate; the constructor gives new fields their default
   in one place.

**J. Bars and the field's background** (E2)
1. The renderer only clips: outside the field the frame keeps its clear color (the game picks it);
   a different field background is a quad the game draws first — no new API.
2. A bars color in the renderer, the field cleared with the clear color when the camera is set —
   anything drawn there before is erased: draw order becomes a rule to remember.

## Decision
**A1, B1, C1, D1, E2, F1, G1, H1, I2, J1.**
- **Math** (`reborn/math.h`): `rb_vec2 {f32 x, y}`, `rb_mat4 {f32 m[16]}` column-major. Plain
  `static inline` (not `RB_INLINE`): debug builds keep the calls, so the debugger steps into them.
  Only what the camera uses: vector add, subtract, scale, component-wise multiply;
  `rb_mat4_ortho(left, right, top, bottom)`, mapping that rectangle to -1..1 with y down and z 0.
  More (multiplication, rotation...) arrives with its first user.
- **Camera** (`reborn/camera2d.h`):
  ```c
  typedef struct rb_camera2d {
      rb_vec2 center; /* world point at the middle of the field */
      rb_vec2 size;   /* design area in world units, always shown whole */
      f32     zoom;   /* 1: `size` as is; 2: half the area, twice as big */
  } rb_camera2d;
  RB_API rb_camera2d rb_camera2d_make(rb_vec2 center, rb_vec2 size); /* zoom 1 */
  RB_API rb_vec2 rb_camera2d_screen_to_world(const rb_camera2d *camera, rb_vec2 target,
                                             rb_vec2 point);
  ```
  The visible area is `size / zoom` around `center`. A camera is made with `rb_camera2d_make`, so
  fields added later get their default there. `size` or `zoom` not positive is a bug (`RB_ASSERT`).
  `target` is the window's size in pixels (`rb_window_get_size`): the functions are pure, testable
  without a renderer, and use the window's current size rather than the swapchain's, which can lag
  a frame. With a zero-pixel target, `screen_to_world` returns `center`. World-to-screen arrives
  with its first user.
- **Field rectangle** (internal, shared by the renderer and `screen_to_world`): the largest
  rectangle with the aspect of `size` that fits the target, centered, rounded to whole pixels so
  viewport and scissor cover the same pixels. 1280x1024 with a 16x9 field: 1280x720 at y 152.
- **Renderer:** `rb_renderer_set_camera(renderer, camera)`, only inside a frame (`RB_ASSERT`, as
  `draw_quad`). The camera is copied, not kept. Non-NULL: viewport and scissor become the field
  rectangle, the view matrix the camera's projection. `NULL`: the whole window, the view
  `rb_mat4_ortho(0, width, 0, height)` in pixels. Each frame begins with `NULL`, so a game that
  never sets a camera draws in pixels as before.
- **Shader:** the view matrix replaces the quad's `target`:
  `gl_Position = view * vec4(position + corner * size, 0.0, 1.0)`. Push constants: `view` (offset
  0, 64 bytes), then position, size, color (32 bytes): 96. The view is pushed when the camera
  changes, the quad's 32 bytes with each draw (push constants persist across draws).
- **Tests:** `tests/math/` for the math header, `tests/renderer/` for the camera.
- **Sandbox:** draws a world field through a camera, with the bars visible on a resized window,
  and converts clicks to world units; the frame-time graph stays in pixels.

## Consequences
- Games lay out in their own units; the field looks the same on every window, with bars when the
  aspect differs.
- Two spaces in one frame: forgetting `rb_renderer_set_camera(renderer, NULL)` draws the HUD in
  world units. Each frame starting in pixels limits the damage to the rest of that frame.
- Push constants grow from 40 to 96 bytes, still within the 128 every device guarantees; per quad
  only 32 are recorded.
- `reborn/math.h` shares its name with the C library's `<math.h>`: harmless with `<reborn/math.h>`,
  but a quoted `#include "math.h"` next to it would pick ours.
- Rotation, matrix multiplication and world-to-screen are left for their first user; rotated
  quads (Orbit) will extend the quad's push constants.
