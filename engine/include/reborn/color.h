/*
 * Colors (ADR 0013, Y3 and Z2).
 *
 *   rb_color orange = rb_color_srgb(1.0f, 0.5f, 0.0f, 1.0f); // as a color picker shows it
 *   rb_color light = rb_color_linear(2.0f, 2.0f, 2.0f, 1.0f); // light intensities, computed
 *
 * The values inside are always linear: proportional to the light emitted, which
 * is what blending and lighting must add up. Color pickers, CSS and image files
 * give sRGB values instead, encoded for the eye: sRGB 0.5 is about 21% of the
 * light of 1.0. Passing one where the other is expected gives a color too light
 * or too dark with no error, so each constructor names its source.
 *
 * Alpha is straight (not premultiplied) and never converted: it is coverage,
 * not light. Stateless: callable from any thread.
 */
#ifndef RB_COLOR_H
#define RB_COLOR_H

#include <reborn/defines.h>

/* Linear RGB, straight alpha. Build it with the constructors below, not by hand. */
typedef struct rb_color {
    f32 r, g, b, a;
} rb_color;

/* From sRGB values in [0, 1] (clamped): what color pickers show. */
RB_API rb_color rb_color_srgb(f32 r, f32 g, f32 b, f32 a);

/* From linear values, taken as they are (above 1 is allowed: brighter than white). */
RB_API rb_color rb_color_linear(f32 r, f32 g, f32 b, f32 a);

/* One sRGB channel in [0, 1] (clamped) to linear, with the exact sRGB curve. */
RB_API f32 rb_color_srgb_to_linear(f32 value);

#endif /* RB_COLOR_H */
