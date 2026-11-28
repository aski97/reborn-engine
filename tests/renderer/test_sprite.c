/*
 * Sprite geometry (ADR 0027): the quad turned about its center as a corner
 * and two edges, and the region a sprite shows. Pure math, no window: runs on
 * every platform.
 */
#include "rb_test.h"

#include <reborn/math.h>

#include "renderer/sprite_internal.h"

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

#define EXPECT_VEC2_NEAR(v, ex, ey)                                                                \
    do {                                                                                           \
        EXPECT_F32_NEAR((v).x, (ex), 1e-5f);                                                       \
        EXPECT_F32_NEAR((v).y, (ey), 1e-5f);                                                       \
    } while (0)

/* Bit for bit: what the shader drew before rotation existed. */
#define EXPECT_VEC2_EXACT(v, ex, ey)                                                               \
    do {                                                                                           \
        RB_EXPECT((v).x == (ex));                                                                  \
        RB_EXPECT((v).y == (ey));                                                                  \
    } while (0)

static rb_vec2 quad_center(rb_sprite_quad quad) {
    return rb_vec2_add(quad.origin, rb_vec2_scale(rb_vec2_add(quad.axis_x, quad.axis_y), 0.5f));
}

/* ============================================================================
 * Geometry
 * ========================================================================== */

/* Fractions that do not survive (x + w/2) - w/2: the corner must still come back exact. */
RB_TEST(sprite, upright_keeps_the_rectangle_exactly) {
    rb_rect        rect = {0.1f, 0.7f, 3.3f, 0.3f};
    rb_sprite_quad quad = rb_sprite_quad_make(rect, 0.0f);
    EXPECT_VEC2_EXACT(quad.origin, 0.1f, 0.7f);
    EXPECT_VEC2_EXACT(quad.axis_x, 3.3f, 0.0f);
    EXPECT_VEC2_EXACT(quad.axis_y, 0.0f, 0.3f);
}

/*
 * A 2 x 0.5 segment with its center at (5, 3), turned a quarter: it stands
 * up, x 4.75..5.25, y 2..4. Its top-left corner goes to the top right: with y
 * down, a positive angle turns clockwise.
 */
RB_TEST(sprite, a_quarter_turn_stands_the_quad_up_clockwise) {
    rb_sprite_quad quad = rb_sprite_quad_make((rb_rect){4.0f, 2.75f, 2.0f, 0.5f}, RB_PI * 0.5f);
    EXPECT_VEC2_NEAR(quad.origin, 5.25f, 2.0f);
    EXPECT_VEC2_NEAR(quad.axis_x, 0.0f, 2.0f);
    EXPECT_VEC2_NEAR(quad.axis_y, -0.5f, 0.0f);
    /* The other three corners. */
    EXPECT_VEC2_NEAR(rb_vec2_add(quad.origin, quad.axis_x), 5.25f, 4.0f);
    EXPECT_VEC2_NEAR(rb_vec2_add(quad.origin, quad.axis_y), 4.75f, 2.0f);
    EXPECT_VEC2_NEAR(rb_vec2_add(rb_vec2_add(quad.origin, quad.axis_x), quad.axis_y), 4.75f, 4.0f);
}

/* A small positive angle tips the top edge's right end down the screen (y grows). */
RB_TEST(sprite, a_positive_angle_turns_clockwise_on_screen) {
    rb_sprite_quad quad = rb_sprite_quad_make((rb_rect){0.0f, 0.0f, 10.0f, 2.0f}, 0.1f);
    RB_EXPECT(quad.axis_x.y > 0.0f);
    RB_EXPECT(quad.axis_y.x < 0.0f);
}

/* Turning moves no quad: its center, edge lengths and right angle stay at any angle. */
RB_TEST(sprite, the_quad_turns_in_place_and_keeps_its_shape) {
    rb_rect rect = {-3.0f, 8.0f, 6.0f, 1.5f};
    f32     angles[] = {0.3f, -0.3f, 1.0f, RB_PI, -2.5f, 100.0f};
    for (usize i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        rb_sprite_quad quad = rb_sprite_quad_make(rect, angles[i]);
        EXPECT_VEC2_NEAR(quad_center(quad), 0.0f, 8.75f);
        EXPECT_F32_NEAR(rb_vec2_length(quad.axis_x), 6.0f, 1e-5f);
        EXPECT_F32_NEAR(rb_vec2_length(quad.axis_y), 1.5f, 1e-5f);
        EXPECT_F32_NEAR(rb_vec2_dot(quad.axis_x, quad.axis_y), 0.0f, 1e-5f);
    }
}

/* The CPU's sine and cosine: a whole turn, even several, comes back upright. */
RB_TEST(sprite, whole_turns_come_back_upright) {
    rb_rect rect = {1.0f, 2.0f, 4.0f, 3.0f};
    f32     turns[] = {2.0f * RB_PI, -2.0f * RB_PI, 20.0f * RB_PI};
    for (usize i = 0; i < sizeof(turns) / sizeof(turns[0]); ++i) {
        rb_sprite_quad quad = rb_sprite_quad_make(rect, turns[i]);
        /*
         * 20 pi as a float is off by a few millionths of a radian (its spacing
         * near 63 is 4e-6): upright within that, times the quad's size.
         */
        EXPECT_F32_NEAR(quad.origin.x, 1.0f, 1e-4f);
        EXPECT_F32_NEAR(quad.origin.y, 2.0f, 1e-4f);
        EXPECT_F32_NEAR(quad.axis_x.x, 4.0f, 1e-4f);
        EXPECT_F32_NEAR(quad.axis_x.y, 0.0f, 1e-4f);
        EXPECT_F32_NEAR(quad.axis_y.x, 0.0f, 1e-4f);
        EXPECT_F32_NEAR(quad.axis_y.y, 3.0f, 1e-4f);
    }
}

/* Same center and edges as the turned rectangle; matches rb_vec2_rotate on each edge. */
RB_TEST(sprite, the_edges_turn_like_vectors) {
    f32            angle = 0.8f;
    rb_sprite_quad quad = rb_sprite_quad_make((rb_rect){0.0f, 0.0f, 3.0f, 2.0f}, angle);
    rb_vec2        x = rb_vec2_rotate((rb_vec2){3.0f, 0.0f}, angle);
    rb_vec2        y = rb_vec2_rotate((rb_vec2){0.0f, 2.0f}, angle);
    EXPECT_VEC2_NEAR(quad.axis_x, x.x, x.y);
    EXPECT_VEC2_NEAR(quad.axis_y, y.x, y.y);
}

/* A negative size (mirrored, as a texture region can be) turns about the same center. */
RB_TEST(sprite, a_negative_size_turns_about_its_center) {
    rb_rect        rect = {10.0f, 10.0f, -4.0f, 2.0f}; /* spans x 6..10 */
    rb_sprite_quad upright = rb_sprite_quad_make(rect, 0.0f);
    EXPECT_VEC2_EXACT(upright.origin, 10.0f, 10.0f);
    EXPECT_VEC2_EXACT(upright.axis_x, -4.0f, 0.0f);
    rb_sprite_quad turned = rb_sprite_quad_make(rect, 1.2f);
    EXPECT_VEC2_NEAR(quad_center(turned), 8.0f, 11.0f);
}

/* ============================================================================
 * Region
 * ========================================================================== */

RB_TEST(sprite, a_source_left_out_is_the_whole_texture) {
    rb_rect whole = rb_sprite_source((rb_rect){0}, 64, 32);
    EXPECT_VEC2_EXACT(((rb_vec2){whole.x, whole.y}), 0.0f, 0.0f);
    EXPECT_VEC2_EXACT(((rb_vec2){whole.width, whole.height}), 64.0f, 32.0f);
    /* Only the size matters: a position without a size is still "left out". */
    rb_rect moved = rb_sprite_source((rb_rect){5.0f, 6.0f, 0.0f, 0.0f}, 64, 32);
    EXPECT_VEC2_EXACT(((rb_vec2){moved.width, moved.height}), 64.0f, 32.0f);
}

RB_TEST(sprite, a_source_with_a_size_is_kept) {
    rb_rect regions[] = {
        {16.0f, 8.0f, 8.0f, 8.0f},  /* a glyph */
        {2.0f, 0.0f, -2.0f, 2.0f},  /* mirrored */
        {0.0f, 0.0f, 0.0f, 4.0f},   /* no width, but a height: as given */
        {1.5f, 1.5f, 0.25f, 0.25f}, /* fractions */
    };
    for (usize i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        rb_rect kept = rb_sprite_source(regions[i], 64, 32);
        EXPECT_VEC2_EXACT(((rb_vec2){kept.x, kept.y}), regions[i].x, regions[i].y);
        EXPECT_VEC2_EXACT(((rb_vec2){kept.width, kept.height}), regions[i].width,
                          regions[i].height);
    }
}
