# 0027 — Sprites and rotation

- **Status:** Accepted
- **Date:** 2026-11-28

## Context
Every quad today has its edges along the window's axes: `rb_renderer_draw_quad(x, y, w, h)` and
the textured variants describe upright rectangles only. Orbit's arc lies along a ring, tilted
differently at every point, and the ring itself is best drawn as segments tangent to it. The
vertex shader builds each corner as `position + corner * size` from push constants (ADR 0015, F1;
ADR 0016): 112 of the 128 bytes every device guarantees. ADR 0026 fixed the angle convention:
radians, clockwise on screen for a positive angle (y down).

The owner asked for choices that make later improvements easy: the shapes that would be costly to
change later (public interfaces, data paths) are generalized now; features without a user
(a pivot, flipping) still wait, and the general shape makes adding them non-breaking.

## Options
**A. How a game asks for a rotated quad**
1. One more function per need, `rb_renderer_draw_quad_rotated(..., radians, color)` — explicit, no
   call changes; every later property (pivot, flip) multiplies the variants.
2. One general draw taking a struct, `rb_renderer_draw_sprite(renderer, &(rb_sprite){...})`; the
   three draws of today stay as shorthands — a later property is a field whose zero keeps today's
   behavior; the same struct can feed a batched renderer as an array; rotated textures come with
   it. Fields left out are zero, so a forgotten `color` is transparent (as `rb_text_style`).
3. An angle parameter on the three existing draws — about sixty calls gain a `0.0f`.

**B. What it turns about**
1. The quad's center — `rect` keeps its meaning (where it is upright) and the quad turns in place;
   turning about another point is the caller placing the center (`rb_vec2_from_angle`).
2. A pivot chosen by the caller (raylib's `origin`) — general; no game asks for it.
3. The top-left corner — the quad's center moves as it turns, rarely wanted.

**C. What reaches the shader**
1. The angle (4 bytes; 116 in all), sine and cosine on the GPU — Vulkan only guarantees an
   absolute error of 2^-11 for `sin`/`cos` within [-π, π], nothing outside, and GPUs differ.
2. Its cosine and sine from the CPU (8 bytes; 120) — full float precision at any angle; the
   shader still knows about position, size and rotation.
3. The quad as a corner and two edge vectors, built by the CPU (an affine map of the unit
   square: 6 floats replacing position and size; 120) — any parallelogram: a pivot, flipping,
   skew or scale later change only the CPU code; pure, tested without a GPU; the same corners a
   batched renderer computes. The largest change now.

**D. The texture region of a sprite left out** (A2: a zero `source`)
1. The whole texture — the zero default is the useful one: `.texture = t` alone draws all of it.
2. Nothing special — a zero region stretches one texel's corner over the quad.

## Decision
**A2, B1, C3, D1.**
```c
typedef struct rb_sprite {
    rb_rect    rect;     /* where it is upright, in the current space */
    f32        rotation; /* radians about the center, clockwise on screen (ADR 0026) */
    rb_texture texture;  /* zero or stale: none, a plain quad of `color` */
    rb_rect    source;   /* the texture's region, in its pixels; no size: the whole texture */
    rb_color   color;    /* the quad's color or the texture's tint: always given */
} rb_sprite;

RB_API void rb_renderer_draw_sprite(rb_renderer *renderer, const rb_sprite *sprite);
```
- Every field's zero is its default, except `color` (transparent), documented like
  `rb_text_style`'s. Fields added later keep the rule: zero leaves a sprite as it was. A pivot,
  when it comes, therefore measures from the center.
- `source` with no size (width and height 0) is the whole texture. `rb_renderer_draw_texture_region`
  keeps its source as given.
- `rb_renderer_draw_quad`, `_draw_texture` and `_draw_texture_region` stay, as shorthands for
  upright sprites: every draw goes through one path.
- **Geometry** (`engine/src/renderer/sprite.c`, internal, backend-independent like `camera2d.c`):
  `cos` and `sin` of the rotation give the edges `axis_x = (w cos, w sin)`,
  `axis_y = (-h sin, h cos)`; the corner is `(x, y) + (half - (axis_x + axis_y) / 2)`, the center
  fixed. Written so that 0 gives exactly `(x, y)`, `(w, 0)`, `(0, h)`: upright quads land on the
  same pixels as before.
- **Shader:** `point = origin + corner.x * axis_x + corner.y * axis_y`. Push constants: `view`
  (offset 0), `color` (64, a `vec4` on a 16-byte boundary), `origin` (80), `axis_x` (88), `axis_y`
  (96), `uv_offset` (104), `uv_size` (112): 120 bytes; 56 pushed with each draw.
- **Tests:** `tests/renderer/test_sprite.c` for the geometry and the default region (pure, run in
  the Linux container too); in `tests/renderer/vulkan/`, sprites drawn without validation messages
  and a captured frame showing a quad turned about its center.
- **Sandbox:** the sliding square rolls like a wheel; the checkerboard turns on its center; the
  bounce demo's ring is drawn with segments tangent to it, and its orbiting square becomes a bar
  pointing at the center.

## Consequences
- Any quad, plain or textured, can turn; properties added later (pivot, flip) are new fields and
  CPU code, with no change to callers, the shader or the push constants.
- 120 of the 128 guaranteed bytes are used: the next per-quad datum needs the batched renderer
  (vertex or storage buffer), which can take the same `rb_sprite` array.
- Two ways to draw an upright quad: the shorthands and a sprite. The shorthands stay the short
  form for the common case.
- A sprite whose `color` is left out draws nothing, without any error.
- A rotation is any finite float; one growing without bound loses precision in the float itself
  (the C library's sine and cosine stay accurate for the value it holds): a game wraps it into a
  turn.
