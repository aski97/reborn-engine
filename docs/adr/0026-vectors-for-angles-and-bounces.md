# 0026 — Vectors for angles and bounces

- **Status:** Accepted
- **Date:** 2026-11-27

## Context
Orbit's ball bounces inside a ring and off an arc the player turns around it. Each step its rules
ask: how far is the ball from the ring's center (a length), which way does the wall face there (a
unit vector, the normal), is the ball moving toward it (the sign of a dot product), what is its new
velocity (a reflection); the arc and the ball's launch are placed by angles (a vector from an
angle, a vector rotated). `reborn/math.h` (ADR 0016: header-only, `static inline`, by value) has
only what the camera needed: add, subtract, scale, component-wise multiply, an orthographic
matrix. ADR 0016 left the rest to its first user, and rotated quads, next, take an angle in the
same convention. World y points down (ADR 0016, F1).

## Options
**A. Angle unit**
1. Radians, with an `RB_PI` constant — what `sinf` and `cosf` take, no conversion; a whole turn
   is `2 * RB_PI`.
2. Degrees — readable in game code (90, not 1.5708); converted at every call.

**B. Which way a positive angle turns** (y down)
1. The standard formulas, `(cos a, sin a)` — a growing angle turns clockwise on screen; the same
   formulas as every reference and as `atan2`.
2. Sine negated — counterclockwise as on paper; our functions would disagree with the formulas
   everyone writes.

**C. Normalizing a zero vector** (a division by zero would give NaN, which spreads unseen)
1. The zero vector back — as Unity and raylib; a ball exactly at the ring's center is a state, not
   a bug; the caller checks when it matters.
2. `RB_ASSERT` — treats it as a bug in the caller.

**D. Reflecting off a normal that is not unit length** (the speed would change unseen)
1. `RB_ASSERT` within a tolerance — caught in debug builds; the code that includes the header
   decides, since an inline function is compiled with its includer's flags.
2. Documented only — as GLSL and Unity.

**E. Linking the C math library (`libm` on Linux)**
1. `PUBLIC` on the engine — the inline functions call `sqrtf`, `sinf`, `cosf` from the code that
   includes the header, so every user of the engine needs it; the dependency is stated.
2. `PRIVATE`, as today — works only because CMake passes a static library's private link
   dependencies on to whoever links it.

## Decision
**A1, B1, C1, D1, E1.** The names are GLSL's (`dot`, `length`, `normalize`, `reflect`), so C and
shader code read alike:
```c
#define RB_PI 3.14159265358979323846f

f32     rb_vec2_dot(rb_vec2 a, rb_vec2 b);
f32     rb_vec2_length(rb_vec2 v);
rb_vec2 rb_vec2_normalize(rb_vec2 v);               /* zero stays zero */
rb_vec2 rb_vec2_reflect(rb_vec2 v, rb_vec2 normal); /* v - 2 (v . n) n; |n| = 1 */
rb_vec2 rb_vec2_from_angle(f32 radians);            /* (cos, sin): clockwise on screen */
rb_vec2 rb_vec2_rotate(rb_vec2 v, f32 radians);
```
- `reflect` asserts `|dot(n, n) - 1| < 1e-3`: a normal from `rb_vec2_normalize` is within about
  1e-7, a hand-written one such as (0.6, 0.8) passes, a raw difference of positions does not.
- The rotated quads' angle follows the same convention (radians, clockwise on screen for positive
  angles).
- Squared length, lerp, angle-from-vector (`atan2`) and the rest arrive with their first user.
- **Tests:** `tests/math/test_math.c`.
- **Sandbox:** a ball bouncing inside a ring of dots, its trail and its speed; a square orbiting the
  ring.

## Consequences
- Orbit's collisions are written as their geometry: lengths, normals, dot products, reflections.
- Angles read as numbers of `RB_PI`; a game that thinks in degrees converts them itself.
- A game coming from y-up references must remember that its positive angles turn clockwise.
- Every file that includes `reborn/math.h` also gets the C library's `<math.h>` and
  `<reborn/assert.h>` (and through it the logger's header).
- `normalize` returns zero for the zero vector and also where the squared length does not fit a
  float (components below about 1e-22 or above about 1e19): far outside any game's coordinates.
