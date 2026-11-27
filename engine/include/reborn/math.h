/*
 * Vectors and matrices (ADR 0016), angles and bounces (ADR 0026).
 *
 *   rb_vec2 p = rb_vec2_add(position, rb_vec2_scale(velocity, dt));
 *
 * Header-only: every function is `static inline`, so each file that includes
 * this header gets its own copy, which the compiler can paste where it is
 * called. Plain `inline`, not RB_INLINE: debug builds keep the calls, so a
 * debugger can step into them. Structs are passed and returned by value: they
 * are small, and expressions nest like the math they write.
 *
 * Angles are in radians; with world y pointing down, a positive angle turns
 * clockwise on screen. Names follow GLSL (dot, length, normalize, reflect).
 *
 * Only what the engine uses so far; more arrives with its first user.
 * Stateless: callable from any thread.
 */
#ifndef RB_MATH_H
#define RB_MATH_H

/*
 * The C library's: angle brackets never search this header's own folder, so
 * this cannot pick reborn/math.h itself. On Linux its functions live in libm,
 * which the engine links PUBLIC: these inline functions call them from the
 * code that includes this header.
 */
#include <math.h>

#include <reborn/assert.h>
#include <reborn/defines.h>

/* Half a turn, in radians: a whole turn is 2 * RB_PI. */
#define RB_PI 3.14159265358979323846f

/* A point, a displacement or a size: what it means depends on where it is used. */
typedef struct rb_vec2 {
    f32 x, y;
} rb_vec2;

/* An axis-aligned rectangle: its top-left corner and its size, in the space it is used in. */
typedef struct rb_rect {
    f32 x, y;
    f32 width, height;
} rb_rect;

/*
 * 4x4 matrix, column-major: element (row r, column c) is m[c * 4 + r], the
 * order GLSL reads a mat4 in, so it reaches the GPU as is. A translation is in
 * the last column, m[12] and m[13].
 */
typedef struct rb_mat4 {
    f32 m[16];
} rb_mat4;

/* ============================================================================
 * Vectors
 * ========================================================================== */

static inline rb_vec2 rb_vec2_add(rb_vec2 a, rb_vec2 b) {
    return (rb_vec2){a.x + b.x, a.y + b.y};
}

static inline rb_vec2 rb_vec2_sub(rb_vec2 a, rb_vec2 b) {
    return (rb_vec2){a.x - b.x, a.y - b.y};
}

static inline rb_vec2 rb_vec2_scale(rb_vec2 v, f32 s) {
    return (rb_vec2){v.x * s, v.y * s};
}

/* Component by component: a size in units times pixels per unit on each axis. */
static inline rb_vec2 rb_vec2_mul(rb_vec2 a, rb_vec2 b) {
    return (rb_vec2){a.x * b.x, a.y * b.y};
}

/*
 * How far `a` goes along `b`, times the length of `b`. With `b` a unit
 * vector, the sign says whether `a` moves toward it (> 0), away from it (< 0)
 * or across it (0).
 */
static inline f32 rb_vec2_dot(rb_vec2 a, rb_vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static inline f32 rb_vec2_length(rb_vec2 v) {
    return sqrtf(rb_vec2_dot(v, v));
}

/*
 * The same direction, length 1. The zero vector has no direction and stays
 * zero (no division by zero, so no NaN): a ball exactly at a ring's center is
 * a state, not a bug, and the caller decides what it means.
 */
static inline rb_vec2 rb_vec2_normalize(rb_vec2 v) {
    f32 length = rb_vec2_length(v);
    if (length <= 0.0f) {
        return (rb_vec2){0.0f, 0.0f};
    }
    return (rb_vec2){v.x / length, v.y / length};
}

/*
 * `v` bounced off a surface facing `normal`, which must be unit length: the
 * part of `v` along the normal is flipped, the part along the surface kept,
 * so the length stays the same. Either side's normal gives the same result.
 * A longer normal would change the speed unseen, hence the assert; the
 * tolerance lets through hand-written ones such as (0.6, 0.8).
 */
static inline rb_vec2 rb_vec2_reflect(rb_vec2 v, rb_vec2 normal) {
    RB_ASSERT_MSG(fabsf(rb_vec2_dot(normal, normal) - 1.0f) < 1e-3f,
                  "normal (%f, %f) is not unit length", (double)normal.x, (double)normal.y);
    return rb_vec2_sub(v, rb_vec2_scale(normal, 2.0f * rb_vec2_dot(v, normal)));
}

/* The unit vector at `radians` from +x: 0 is (1, 0), RB_PI / 2 is (0, 1), below on screen. */
static inline rb_vec2 rb_vec2_from_angle(f32 radians) {
    return (rb_vec2){cosf(radians), sinf(radians)};
}

/* `v` turned by `radians` about the origin, clockwise on screen for a positive angle. */
static inline rb_vec2 rb_vec2_rotate(rb_vec2 v, f32 radians) {
    f32 c = cosf(radians);
    f32 s = sinf(radians);
    return (rb_vec2){v.x * c - v.y * s, v.x * s + v.y * c};
}

/* ============================================================================
 * Matrices
 * ========================================================================== */

/*
 * Orthographic projection: maps the rectangle from (left, top) to (right,
 * bottom) onto Vulkan's normalized coordinates, -1 at its top-left corner and
 * +1 at its bottom-right, y pointing down like window pixels. z is kept at 0.
 * `left` must differ from `right`, `top` from `bottom`.
 *
 *   x' = (x - left) / (right - left) * 2 - 1,  written as  x * sx + tx
 */
static inline rb_mat4 rb_mat4_ortho(f32 left, f32 right, f32 top, f32 bottom) {
    f32     sx = 2.0f / (right - left);
    f32     sy = 2.0f / (bottom - top);
    rb_mat4 result = {{0}};
    result.m[0] = sx;                                /* row 0, column 0 */
    result.m[5] = sy;                                /* row 1, column 1 */
    result.m[10] = 1.0f;                             /* row 2, column 2 */
    result.m[12] = -(right + left) / (right - left); /* row 0, column 3: tx */
    result.m[13] = -(bottom + top) / (bottom - top); /* row 1, column 3: ty */
    result.m[15] = 1.0f;                             /* row 3, column 3: keeps w at 1 */
    return result;
}

#endif /* RB_MATH_H */
