/*
 * Colors (ADR 0013, Y3).
 */
#include <reborn/color.h>

#include <math.h>

f32 rb_color_srgb_to_linear(f32 value) {
    /* Written so NaN fails both tests and becomes 0, not a NaN that reaches the GPU. */
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    if (value >= 1.0f) {
        return 1.0f;
    }
    /*
     * The sRGB standard (IEC 61966-2-1): a straight segment near black, where a
     * pure power curve would be too steep to encode dark values precisely, then
     * a power of 2.4. Overall it is close to value^2.2, but not equal: the
     * approximation is off by several units of an 8-bit channel in the darks.
     */
    if (value <= 0.04045f) {
        return value / 12.92f;
    }
    return powf((value + 0.055f) / 1.055f, 2.4f);
}

rb_color rb_color_srgb(f32 r, f32 g, f32 b, f32 a) {
    /* Alpha is coverage: it stays as it is, only clamped like the channels. */
    f32 alpha = !(a > 0.0f) ? 0.0f : a > 1.0f ? 1.0f : a;
    return (rb_color){rb_color_srgb_to_linear(r), rb_color_srgb_to_linear(g),
                      rb_color_srgb_to_linear(b), alpha};
}

rb_color rb_color_linear(f32 r, f32 g, f32 b, f32 a) {
    return (rb_color){r, g, b, a};
}
