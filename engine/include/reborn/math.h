/*
 * Vectors and matrices (ADR 0016).
 *
 *   rb_vec2 p = rb_vec2_add(position, rb_vec2_scale(velocity, dt));
 *
 * Header-only: every function is `static inline`, so each file that includes
 * this header gets its own copy, which the compiler can paste where it is
 * called. Plain `inline`, not RB_INLINE: debug builds keep the calls, so a
 * debugger can step into them. Structs are passed and returned by value: they
 * are small, and expressions nest like the math they write.
 *
 * Only what the engine uses so far; more arrives with its first user.
 * Stateless: callable from any thread.
 */
#ifndef RB_MATH_H
#define RB_MATH_H

#include <reborn/defines.h>

/* A point, a displacement or a size: what it means depends on where it is used. */
typedef struct rb_vec2 {
    f32 x, y;
} rb_vec2;

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
