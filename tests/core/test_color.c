#include "rb_test.h"

#include <math.h>

#include <reborn/color.h>

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

/* Half an 8-bit step: a conversion error below it is invisible on screen. */
#define COLOR_EPS (0.5f / 255.0f)

RB_TEST(color, srgb_ends_stay_put) {
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(0.0f), 0.0f, 1e-7f);
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(1.0f), 1.0f, 1e-6f);
}

/* Reference values of the sRGB standard. */
RB_TEST(color, srgb_mid_gray_is_about_a_fifth_of_the_light) {
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(0.5f), 0.214041f, 1e-5f);
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(0.2f), 0.033105f, 1e-5f);
}

/* Both sides of the junction between the straight segment and the curve agree. */
RB_TEST(color, srgb_curve_is_continuous_at_the_linear_segment) {
    f32 below = rb_color_srgb_to_linear(0.04045f);
    f32 above = rb_color_srgb_to_linear(0.04046f);
    EXPECT_F32_NEAR(below, 0.0031308f, 1e-6f);
    EXPECT_F32_NEAR(above, below, 1e-5f);
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(0.02f), 0.02f / 12.92f, 1e-7f);
}

RB_TEST(color, srgb_input_is_clamped) {
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(-0.5f), 0.0f, 1e-7f);
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(3.0f), 1.0f, 1e-6f);
    EXPECT_F32_NEAR(rb_color_srgb_to_linear(NAN), 0.0f, 1e-7f);
}

RB_TEST(color, srgb_constructor_converts_rgb_but_not_alpha) {
    rb_color c = rb_color_srgb(0.5f, 1.0f, 0.0f, 0.5f);
    EXPECT_F32_NEAR(c.r, 0.214041f, COLOR_EPS);
    EXPECT_F32_NEAR(c.g, 1.0f, COLOR_EPS);
    EXPECT_F32_NEAR(c.b, 0.0f, COLOR_EPS);
    EXPECT_F32_NEAR(c.a, 0.5f, 1e-7f);
    EXPECT_F32_NEAR(rb_color_srgb(0, 0, 0, 2.0f).a, 1.0f, 1e-7f);
}

RB_TEST(color, linear_constructor_keeps_values) {
    rb_color c = rb_color_linear(2.0f, 0.25f, 0.0f, 0.75f);
    EXPECT_F32_NEAR(c.r, 2.0f, 1e-7f);
    EXPECT_F32_NEAR(c.g, 0.25f, 1e-7f);
    EXPECT_F32_NEAR(c.b, 0.0f, 1e-7f);
    EXPECT_F32_NEAR(c.a, 0.75f, 1e-7f);
}
