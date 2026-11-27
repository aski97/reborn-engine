# Math

| | |
|---|---|
| **Status** | In progress |
| **Since** | v0.2.0 (2026-11-23) |
| **Updated** | v0.3.0 (2026-11-27) |
| **Code** | `engine/include/reborn/math.h` |
| **ADRs** | [0016](../adr/0016-math-and-2d-camera.md), [0026](../adr/0026-vectors-for-angles-and-bounces.md) |

## Summary
Vectors and matrices for games and the engine: positions and sizes, the camera's projection, and
the geometry of moving things (distances, directions, bounces, angles).

## Spec
- Header-only, `static inline`, structs by value (ADR 0016); stateless, callable from any thread.
- `rb_vec2 {x, y}`: add, sub, scale, component-wise mul; dot, length, normalize, reflect;
  from an angle, rotated by an angle.
- `rb_rect {x, y, width, height}`: top-left corner and size.
- `rb_mat4` column-major (row r, column c at `m[c * 4 + r]`, as GLSL reads it);
  `rb_mat4_ortho(left, right, top, bottom)` maps that rectangle to Vulkan's -1..1, y down, z 0.
- Angles in radians (`RB_PI`); with y down, a positive angle turns clockwise on screen.
- `rb_vec2_normalize` of the zero vector is the zero vector, never NaN.
- `rb_vec2_reflect` needs a unit normal: `RB_ASSERT` in debug builds, within 1e-3 on the squared
  length.
- Not yet: squared length, lerp, angle of a vector, matrix multiplication, 3D types.

## Design
- Inline functions are compiled into the code that includes the header, with that code's flags:
  the reflect assert follows the includer's Debug or Release build.
- They call `sqrtf`, `sinf`, `cosf` from the C library's `<math.h>`; on Linux those live in libm,
  which the engine links `PUBLIC` so every executable using the engine gets it.
- Names follow GLSL (`dot`, `length`, `normalize`, `reflect`): C and shader code read alike.

## API
```c
/* A ball inside a ring: bounce when it reaches the wall going outward. */
rb_vec2 offset = rb_vec2_sub(ball, center);
rb_vec2 normal = rb_vec2_normalize(offset);
if (rb_vec2_length(offset) >= radius && rb_vec2_dot(velocity, normal) > 0.0f) {
    velocity = rb_vec2_reflect(velocity, normal); /* same speed, new direction */
}

/* A point on the ring at an angle; a direction turned by a quarter turn. */
rb_vec2 at = rb_vec2_add(center, rb_vec2_scale(rb_vec2_from_angle(angle), radius));
rb_vec2 turned = rb_vec2_rotate(direction, RB_PI * 0.5f);
```

## Decisions
- `rb_vec2` + `rb_mat4`, by value, header-only, column-major (ADR 0016: A1, B1, C1, D1).
- Radians; standard formulas, clockwise on screen; zero normalizes to zero; reflect asserts a unit
  normal; libm `PUBLIC` (ADR 0026).

## Future work
- Matrix multiplication and rotation matrices, if rotated quads or 3D need them.
- Lerp, angle of a vector, squared length: with their first user.

## History
| Version | Date | Change |
|---|---|---|
| v0.2.0 | 2026-11-23 | First version (on the 2D camera's page): `rb_vec2`, `rb_mat4`, ortho |
| v0.2.0 | 2026-11-24 | `rb_rect`, for texture regions (ADR 0018) |
| v0.3.0 | 2026-11-27 | Page of its own; dot, length, normalize, reflect, from-angle, rotate, `RB_PI` (ADR 0026) |
