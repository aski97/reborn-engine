/*
 * Vectors and matrices (ADR 0016), angles and bounces (ADR 0026).
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
 * Lengths and directions
 * ========================================================================== */

/* The sign says where `a` goes relative to `b`: along, against, or across it. */
RB_TEST(math, dot_sign_tells_toward_away_or_across) {
    rb_vec2 n = {0.0f, 1.0f};
    EXPECT_F32_NEAR(rb_vec2_dot((rb_vec2){2.0f, 3.0f}, n), 3.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_dot((rb_vec2){2.0f, -3.0f}, n), -3.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_dot((rb_vec2){5.0f, 0.0f}, n), 0.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_dot((rb_vec2){2.0f, 1.0f}, (rb_vec2){0.6f, 0.8f}), 2.0f, 1e-6f);
}

RB_TEST(math, length_is_pythagoras) {
    EXPECT_F32_NEAR(rb_vec2_length((rb_vec2){3.0f, 4.0f}), 5.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_length((rb_vec2){-3.0f, -4.0f}), 5.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_length((rb_vec2){0.0f, 0.0f}), 0.0f, 1e-7f);
}

RB_TEST(math, normalize_keeps_the_direction_at_length_one) {
    EXPECT_VEC2_NEAR(rb_vec2_normalize((rb_vec2){3.0f, 4.0f}), 0.6f, 0.8f);
    EXPECT_VEC2_NEAR(rb_vec2_normalize((rb_vec2){0.0f, -7.0f}), 0.0f, -1.0f);
    EXPECT_F32_NEAR(rb_vec2_length(rb_vec2_normalize((rb_vec2){-2.0f, 7.0f})), 1.0f, 1e-6f);
}

/* No direction: zero back, never NaN. */
RB_TEST(math, normalize_leaves_the_zero_vector_zero) {
    rb_vec2 n = rb_vec2_normalize((rb_vec2){0.0f, 0.0f});
    EXPECT_VEC2_NEAR(n, 0.0f, 0.0f);
    /* NaN is the only value that differs from itself. */
    RB_EXPECT(n.x == n.x && n.y == n.y);
}

/* ============================================================================
 * Bounces
 * ========================================================================== */

/*
 * A ball at (11, 8.5) on a ring centered at (8, 4.5) with radius 5: the wall
 * faces (0.6, 0.8) there. Moving at (2, 1), it goes toward the wall (dot 2),
 * comes back at (-0.4, -2.2), as fast as before, going away from it.
 */
RB_TEST(math, reflect_bounces_off_the_ring) {
    rb_vec2 offset = rb_vec2_sub((rb_vec2){11.0f, 8.5f}, (rb_vec2){8.0f, 4.5f});
    EXPECT_F32_NEAR(rb_vec2_length(offset), 5.0f, 1e-6f);
    rb_vec2 n = rb_vec2_normalize(offset);
    rb_vec2 v = {2.0f, 1.0f};
    EXPECT_F32_NEAR(rb_vec2_dot(v, n), 2.0f, 1e-6f);

    rb_vec2 r = rb_vec2_reflect(v, n);
    EXPECT_VEC2_NEAR(r, -0.4f, -2.2f);
    EXPECT_F32_NEAR(rb_vec2_length(r), rb_vec2_length(v), 1e-6f);
    EXPECT_F32_NEAR(rb_vec2_dot(r, n), -2.0f, 1e-6f);
}

/* A floor facing up (y down: -y is up): the vertical part flips, the horizontal stays. */
RB_TEST(math, reflect_flips_only_the_part_along_the_normal) {
    rb_vec2 up = {0.0f, -1.0f};
    EXPECT_VEC2_NEAR(rb_vec2_reflect((rb_vec2){3.0f, 2.0f}, up), 3.0f, -2.0f);
    /* Sliding along the surface: nothing to flip. */
    EXPECT_VEC2_NEAR(rb_vec2_reflect((rb_vec2){3.0f, 0.0f}, up), 3.0f, 0.0f);
}

/* (v . -n)(-n) is (v . n) n: which side the normal faces does not matter. */
RB_TEST(math, reflect_gives_the_same_result_for_either_side) {
    rb_vec2 v = {2.0f, 1.0f};
    rb_vec2 a = rb_vec2_reflect(v, (rb_vec2){0.6f, 0.8f});
    rb_vec2 b = rb_vec2_reflect(v, (rb_vec2){-0.6f, -0.8f});
    EXPECT_VEC2_NEAR(a, b.x, b.y);
}

/* ============================================================================
 * Angles
 * ========================================================================== */

/* Radians from +x; with y down, a quarter turn points below: clockwise on screen. */
RB_TEST(math, from_angle_turns_clockwise_on_screen) {
    EXPECT_VEC2_NEAR(rb_vec2_from_angle(0.0f), 1.0f, 0.0f);
    EXPECT_VEC2_NEAR(rb_vec2_from_angle(RB_PI * 0.5f), 0.0f, 1.0f);
    EXPECT_VEC2_NEAR(rb_vec2_from_angle(RB_PI), -1.0f, 0.0f);
    EXPECT_VEC2_NEAR(rb_vec2_from_angle(RB_PI * 1.5f), 0.0f, -1.0f);
    EXPECT_F32_NEAR(rb_vec2_length(rb_vec2_from_angle(0.7f)), 1.0f, 1e-6f);
}

RB_TEST(math, rotate_turns_about_the_origin_keeping_the_length) {
    EXPECT_VEC2_NEAR(rb_vec2_rotate((rb_vec2){2.0f, 0.0f}, RB_PI * 0.5f), 0.0f, 2.0f);
    EXPECT_VEC2_NEAR(rb_vec2_rotate((rb_vec2){0.0f, 3.0f}, RB_PI), 0.0f, -3.0f);
    rb_vec2 v = {3.0f, 4.0f};
    EXPECT_F32_NEAR(rb_vec2_length(rb_vec2_rotate(v, 1.234f)), 5.0f, 1e-5f);
    /* There and back. */
    rb_vec2 back = rb_vec2_rotate(rb_vec2_rotate(v, 0.8f), -0.8f);
    EXPECT_F32_NEAR(back.x, 3.0f, 1e-5f);
    EXPECT_F32_NEAR(back.y, 4.0f, 1e-5f);
}

/* The two angle functions agree: a direction at a, turned by b, is the one at a + b. */
RB_TEST(math, rotate_agrees_with_from_angle) {
    rb_vec2 turned = rb_vec2_rotate(rb_vec2_from_angle(0.5f), 1.2f);
    rb_vec2 direct = rb_vec2_from_angle(1.7f);
    EXPECT_VEC2_NEAR(turned, direct.x, direct.y);
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
