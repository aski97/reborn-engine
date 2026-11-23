/*
 * 2D camera (ADR 0016): the field, the projection, the mouse conversion. Pure
 * math, no window: runs on every platform.
 */
#include "rb_test.h"

#include <reborn/camera2d.h>

#include "renderer/camera2d_internal.h"

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

#define EXPECT_VEC2_NEAR(v, ex, ey)                                                                \
    do {                                                                                           \
        EXPECT_F32_NEAR((v).x, (ex), 1e-4f);                                                       \
        EXPECT_F32_NEAR((v).y, (ey), 1e-4f);                                                       \
    } while (0)

#define EXPECT_FIELD(f, ex, ey, ew, eh)                                                            \
    do {                                                                                           \
        RB_EXPECT_EQ_UINT((f).x, (ex));                                                            \
        RB_EXPECT_EQ_UINT((f).y, (ey));                                                            \
        RB_EXPECT_EQ_UINT((f).width, (ew));                                                        \
        RB_EXPECT_EQ_UINT((f).height, (eh));                                                       \
    } while (0)

/* Blink's field: 16 x 9 units centered on (8, 4.5), so (0, 0) is its top-left corner. */
static rb_camera2d blink_camera(void) {
    return rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
}

static rb_vec2 transform(rb_mat4 m, rb_vec2 p) {
    return (rb_vec2){
        m.m[0] * p.x + m.m[4] * p.y + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[13],
    };
}

RB_TEST(camera2d, make_sets_zoom_to_one) {
    rb_camera2d camera = blink_camera();
    EXPECT_VEC2_NEAR(camera.center, 8.0f, 4.5f);
    EXPECT_VEC2_NEAR(camera.size, 16.0f, 9.0f);
    EXPECT_F32_NEAR(camera.zoom, 1.0f, 1e-7f);
}

/* ============================================================================
 * Field
 * ========================================================================== */

RB_TEST(camera2d, field_fills_a_window_of_the_same_aspect) {
    rb_camera2d camera = blink_camera();
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1280, 720), 0, 0, 1280, 720);
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1920, 1080), 0, 0, 1920, 1080);
}

/* 5:4: the width limits, 304 pixels left in height, split above and below. */
RB_TEST(camera2d, field_gets_bars_above_and_below_on_a_taller_window) {
    rb_camera2d camera = blink_camera();
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1280, 1024), 0, 152, 1280, 720);
}

RB_TEST(camera2d, field_gets_bars_at_the_sides_on_a_wider_window) {
    rb_camera2d camera = blink_camera();
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1600, 720), 160, 0, 1280, 720);
}

/* 1000 * 9/16 = 562.5: rounded to 563, the odd pixel left goes to the bottom bar. */
RB_TEST(camera2d, field_is_rounded_to_whole_pixels) {
    rb_camera2d camera = blink_camera();
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1000, 720), 0, 78, 1000, 563);
}

/* Zoom changes what is shown inside the field, not where the field is. */
RB_TEST(camera2d, field_ignores_zoom) {
    rb_camera2d camera = blink_camera();
    camera.zoom = 3.0f;
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 1280, 1024), 0, 152, 1280, 720);
}

RB_TEST(camera2d, field_of_an_empty_target_is_empty) {
    rb_camera2d camera = blink_camera();
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 0, 0), 0, 0, 0, 0);
    EXPECT_FIELD(rb_camera2d_field_rect(&camera, 0, 720), 0, 360, 0, 0);
}

/* ============================================================================
 * Projection
 * ========================================================================== */

RB_TEST(camera2d, projection_maps_the_area_shown_onto_the_field) {
    rb_camera2d camera = blink_camera();
    rb_mat4     m = rb_camera2d_projection(&camera);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){0.0f, 0.0f}), -1.0f, -1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){16.0f, 9.0f}), 1.0f, 1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){8.0f, 4.5f}), 0.0f, 0.0f);
}

/* Zoom 2 shows 8 x 4.5 units around the center: (4, 2.25) is the new top-left corner. */
RB_TEST(camera2d, projection_zooms_around_the_center) {
    rb_camera2d camera = blink_camera();
    camera.zoom = 2.0f;
    rb_mat4 m = rb_camera2d_projection(&camera);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){4.0f, 2.25f}), -1.0f, -1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){12.0f, 6.75f}), 1.0f, 1.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){8.0f, 4.5f}), 0.0f, 0.0f);
}

RB_TEST(camera2d, projection_follows_the_center) {
    rb_camera2d camera = blink_camera();
    camera.center = (rb_vec2){100.0f, -50.0f};
    rb_mat4 m = rb_camera2d_projection(&camera);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){100.0f, -50.0f}), 0.0f, 0.0f);
    EXPECT_VEC2_NEAR(transform(m, (rb_vec2){92.0f, -54.5f}), -1.0f, -1.0f);
}

/* ============================================================================
 * Screen to world
 * ========================================================================== */

/* 1280 x 1024: the field is 1280 x 720 at y 152, 80 pixels per unit. */
RB_TEST(camera2d, screen_to_world_inverts_the_field) {
    rb_camera2d camera = blink_camera();
    rb_vec2     target = {1280.0f, 1024.0f};
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){0.0f, 152.0f}), 0.0f,
                     0.0f);
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){640.0f, 512.0f}), 8.0f,
                     4.5f);
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){1280.0f, 872.0f}),
                     16.0f, 9.0f);
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){120.0f, 192.0f}), 1.5f,
                     0.5f);
}

/* The top bar is above the field: outside the area shown, y negative. */
RB_TEST(camera2d, screen_to_world_on_a_bar_is_outside_the_area) {
    rb_camera2d camera = blink_camera();
    rb_vec2     world =
        rb_camera2d_screen_to_world(&camera, (rb_vec2){1280.0f, 1024.0f}, (rb_vec2){640.0f, 72.0f});
    EXPECT_VEC2_NEAR(world, 8.0f, -1.0f);
}

RB_TEST(camera2d, screen_to_world_follows_zoom) {
    rb_camera2d camera = blink_camera();
    camera.zoom = 2.0f;
    rb_vec2 target = {1280.0f, 720.0f};
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){0.0f, 0.0f}), 4.0f,
                     2.25f);
    EXPECT_VEC2_NEAR(rb_camera2d_screen_to_world(&camera, target, (rb_vec2){640.0f, 360.0f}), 8.0f,
                     4.5f);
}

RB_TEST(camera2d, screen_to_world_without_pixels_returns_the_center) {
    rb_camera2d camera = blink_camera();
    camera.center = (rb_vec2){3.0f, 7.0f};
    EXPECT_VEC2_NEAR(
        rb_camera2d_screen_to_world(&camera, (rb_vec2){0.0f, 0.0f}, (rb_vec2){10.0f, 10.0f}), 3.0f,
        7.0f);
    EXPECT_VEC2_NEAR(
        rb_camera2d_screen_to_world(&camera, (rb_vec2){-5.0f, 720.0f}, (rb_vec2){10.0f, 10.0f}),
        3.0f, 7.0f);
}
