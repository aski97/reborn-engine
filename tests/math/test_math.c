/*
 * Vectors and matrices (ADR 0016).
 */
#include "rb_test.h"

#include <reborn/math.h>

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

#define EXPECT_VEC2_NEAR(v, ex, ey)                                                                \
    do {                                                                                           \
        EXPECT_F32_NEAR((v).x, (ex), 1e-6f);                                                       \
        EXPECT_F32_NEAR((v).y, (ey), 1e-6f);                                                       \
    } while (0)

/* What the vertex shader computes: matrix times (x, y, 0, 1), column-major. */
static rb_vec2 transform(rb_mat4 m, rb_vec2 p) {
    return (rb_vec2){
        m.m[0] * p.x + m.m[4] * p.y + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[13],
    };
}

/* ============================================================================
 * Vectors
 * ========================================================================== */

RB_TEST(math, vec2_add_and_sub_work_per_component) {
    rb_vec2 a = {1.5f, -2.0f};
    rb_vec2 b = {0.5f, 4.0f};
    EXPECT_VEC2_NEAR(rb_vec2_add(a, b), 2.0f, 2.0f);
    EXPECT_VEC2_NEAR(rb_vec2_sub(a, b), 1.0f, -6.0f);
}

RB_TEST(math, vec2_scale_and_mul) {
    EXPECT_VEC2_NEAR(rb_vec2_scale((rb_vec2){3.0f, -1.0f}, 0.5f), 1.5f, -0.5f);
    EXPECT_VEC2_NEAR(rb_vec2_mul((rb_vec2){3.0f, -1.0f}, (rb_vec2){2.0f, 4.0f}), 6.0f, -4.0f);
}

/* By value: nested calls read like the formula, p + v * dt. */
RB_TEST(math, vec2_calls_nest) {
    rb_vec2 p = rb_vec2_add((rb_vec2){1.0f, 1.0f}, rb_vec2_scale((rb_vec2){2.0f, -4.0f}, 0.25f));
    EXPECT_VEC2_NEAR(p, 1.5f, 0.0f);
}

/* ============================================================================
 * Matrices
 * ========================================================================== */

/* Blink's field: 16 x 9 units, (0, 0) at the top left. */
RB_TEST(math, ortho_maps_corners_and_center_to_normalized_coordinates) {
    rb_mat4 m = rb_mat4_ortho(0.0f, 16.0f, 0.0f, 9.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){0.0f, 0.0f}), -1.0f, -1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){16.0f, 9.0f}), 1.0f, 1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){8.0f, 4.5f}), 0.0f, 0.0f);
}

/* y down: a larger y goes toward +1, the bottom of the image in Vulkan. */
RB_TEST(math, ortho_keeps_y_pointing_down) {
    rb_mat4 m = rb_mat4_ortho(-2.0f, 2.0f, 10.0f, 20.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){-2.0f, 10.0f}), -1.0f, -1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){2.0f, 20.0f}), 1.0f, 1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){0.0f, 17.5f}), 0.0f, 0.5f);
}

/* The layout GLSL expects: scales on the diagonal, the translation in the last column. */
RB_TEST(math, ortho_is_column_major) {
    rb_mat4 m = rb_mat4_ortho(0.0f, 4.0f, 0.0f, 2.0f);
    EXPECT_F32_NEAR(m.m[0], 0.5f, 1e-7f);
    EXPECT_F32_NEAR(m.m[5], 1.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[10], 1.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[12], -1.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[13], -1.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[15], 1.0f, 1e-7f);
    /* Row 3 is (0, 0, 0, 1): w stays 1, nothing is divided by perspective. */
    EXPECT_F32_NEAR(m.m[3], 0.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[7], 0.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[11], 0.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[4], 0.0f, 1e-7f);
    EXPECT_F32_NEAR(m.m[1], 0.0f, 1e-7f);
}
