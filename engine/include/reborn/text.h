/*
 * Text (ADR 0021): strings drawn with a font, measured and aligned to a point.
 *
 *   rb_text_style title = {.font = font, .size = 32, .color = white,
 *                          .align_x = RB_TEXT_CENTER, .align_y = RB_TEXT_MIDDLE};
 *   rb_text_draw(renderer, 640, 360, &title, "GAME OVER");   // centered on (640, 360)
 *
 *   char score[32];
 *   snprintf(score, sizeof(score), "SCORE %u", points);       // numbers: formatted by the caller
 *   rb_text_draw(renderer, 24, 24, &hud, score);
 *
 * Strings are UTF-8; a character the font lacks, or a broken sequence, shows
 * the font's missing glyph. '\n' starts a new line; every other character is
 * a glyph. One glyph per character, left to right: no kerning, no wrapping.
 *
 * Sizes and positions are in the current space (rb_renderer_set_camera):
 * window pixels by default, world units through a camera. A bitmap font stays
 * sharp in pixels at a whole multiple of its own height, from a whole-pixel
 * anchor: the builtin font at 8, 16, 24, 32...
 *
 * Nothing is allocated or kept. Pointer parameters must not be NULL
 * (RB_ASSERT in debug builds).
 */
#ifndef RB_TEXT_H
#define RB_TEXT_H

#include <reborn/color.h>
#include <reborn/defines.h>
#include <reborn/font.h>
#include <reborn/math.h>
#include <reborn/renderer.h>

/* Where each line sits horizontally relative to the anchor's x. */
typedef enum rb_text_align_x {
    RB_TEXT_LEFT,   /* starts at x */
    RB_TEXT_CENTER, /* its middle at x */
    RB_TEXT_RIGHT,  /* ends at x */
} rb_text_align_x;

/*
 * Where the block of lines sits vertically relative to the anchor's y.
 * Measured on the font's metrics, not on the characters present: "SCORE 9"
 * and "score 10" with the same anchor share their lines.
 */
typedef enum rb_text_align_y {
    RB_TEXT_TOP,      /* the block's top (the first line's ascent) at y */
    RB_TEXT_MIDDLE,   /* the block's middle at y */
    RB_TEXT_BASELINE, /* the first line's baseline at y */
    RB_TEXT_BOTTOM,   /* the block's bottom (the last line's descent) at y */
} rb_text_align_y;

/*
 * How text looks: define one per kind of text and reuse it. With designated
 * initializers the fields left out are zero: left and top aligned, but also a
 * transparent color, so `color` is always given.
 */
typedef struct rb_text_style {
    const rb_font *font;
    /*
     * The height of a line (the font's ascent + descent) in the current space:
     * 32 in pixels draws the 8x8 builtin font 4 times larger. Lines are
     * size * (ascent + descent + line_gap) / (ascent + descent) apart. 0 or
     * less draws nothing.
     */
    f32             size;
    rb_color        color;
    rb_text_align_x align_x;
    rb_text_align_y align_y;
} rb_text_style;

/*
 * Draws `text` anchored at (x, y) in the current space, one textured quad per
 * visible glyph (spaces have none), on top of what was drawn before. Only
 * inside the game's `render`, like the renderer's other draws.
 */
RB_API void rb_text_draw(rb_renderer *renderer, f32 x, f32 y, const rb_text_style *style,
                         const char *text);

/*
 * The block `text` covers when drawn with `style`: the widest line's width
 * (the sum of its advances, trailing spaces included) and the height from the
 * first line's top to the last one's bottom. An empty string is one empty
 * line: zero wide, one line high. Callable at any time.
 */
RB_API rb_vec2 rb_text_measure(const rb_text_style *style, const char *text);

#endif /* RB_TEXT_H */
