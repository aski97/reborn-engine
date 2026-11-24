/* Blink's presentation layer (ADR 0014). */
#include "blink_view.h"

#include <stdio.h>

#include <reborn/assert.h>
#include <reborn/color.h>
#include <reborn/log.h>
#include <reborn/text.h>

/* Saturated and bright, so each stands out on the dark field; BLINK_COLOR_COUNT of them. */
static rb_color blink_square_color(u32 index) {
    static const f32 srgb[BLINK_COLOR_COUNT][3] = {
        {0.95f, 0.30f, 0.30f}, /* red */
        {0.98f, 0.70f, 0.20f}, /* orange */
        {0.95f, 0.90f, 0.30f}, /* yellow */
        {0.35f, 0.85f, 0.45f}, /* green */
        {0.30f, 0.70f, 1.00f}, /* blue */
        {0.80f, 0.45f, 0.95f}, /* violet */
    };
    RB_ASSERT(index < BLINK_COLOR_COUNT);
    return rb_color_srgb(srgb[index][0], srgb[index][1], srgb[index][2], 1.0f);
}

/* ============================================================================
 * Field
 * ========================================================================== */

/* The world, through the camera: the field and, while playing, the square. */
static void blink_draw_field(const blink_state *state, const rb_camera2d *camera,
                             rb_renderer *renderer) {
    bool over = state->phase == BLINK_PHASE_GAME_OVER;
    rb_renderer_set_camera(renderer, camera);
    rb_renderer_draw_quad(renderer, 0.0f, 0.0f, state->tuning.field.x, state->tuning.field.y,
                          over ? rb_color_srgb(0.25f, 0.05f, 0.06f, 1.0f)
                               : rb_color_srgb(0.07f, 0.08f, 0.11f, 1.0f));
    /*
     * Drawn as the last step left it: alpha would let it shrink between steps,
     * but at 60 steps per second the difference is below a pixel.
     */
    if (state->phase == BLINK_PHASE_PLAYING) {
        rb_rect square = blink_square_rect(state);
        rb_renderer_draw_quad(renderer, square.x, square.y, square.width, square.height,
                              blink_square_color(state->square.color));
    }
    rb_renderer_set_camera(renderer, NULL);
}

/* ============================================================================
 * HUD (ADR 0021, G1)
 *
 * Window pixels, anchored to the window's edges and center. Every size below
 * is in pixels of the builtin font, times the HUD's scale: whole numbers
 * times a whole number, from whole-pixel anchors, so the text stays sharp.
 * ========================================================================== */

#define BLINK_HUD_DESIGN_HEIGHT 180.0f /* window pixels per unit of scale */
#define BLINK_HUD_MARGIN        6.0f   /* from the window's edges */
#define BLINK_TEXT              8.0f   /* a line: the builtin font's own height */
#define BLINK_TITLE_TEXT        16.0f
#define BLINK_PIP_SIDE          7.0f /* as tall as the capitals beside it */
#define BLINK_PIP_STEP          10.0f
#define BLINK_LOGO_CELL         4.0f /* a square of 3 and a gap of 1 */
#define BLINK_PROMPT_PERIOD     1.0f /* seconds: "PRESS SPACE" shows for the first half */

f32 blink_view_hud_scale(f32 pixel_height) {
    if (pixel_height < 2.0f * BLINK_HUD_DESIGN_HEIGHT) {
        return 1.0f;
    }
    /* The cast drops the fraction: a scale of 3.9 would make the font's pixels uneven. */
    return (f32)(u32)(pixel_height / BLINK_HUD_DESIGN_HEIGHT);
}

typedef struct blink_hud {
    rb_renderer   *renderer;
    const rb_font *font;
    f32            unit;   /* window pixels per font pixel */
    rb_vec2        size;   /* the window, in pixels */
    rb_vec2        center; /* rounded down to a whole pixel */
} blink_hud;

/* White text at `size` font pixels, faded by `alpha`, anchored at `at` in window pixels. */
static void blink_hud_text(const blink_hud *hud, rb_vec2 at, f32 size, rb_text_align_x align_x,
                           rb_text_align_y align_y, f32 alpha, const char *text) {
    rb_text_style style = {.font = hud->font,
                           .size = size * hud->unit,
                           .color = rb_color_srgb(1.0f, 1.0f, 1.0f, alpha),
                           .align_x = align_x,
                           .align_y = align_y};
    rb_text_draw(hud->renderer, at.x, at.y, &style, text);
}

/* The logo's letters, 5x5 cells each: a square per '#'. */
#define BLINK_LOGO_LETTERS 5u
static const char blink_logo[BLINK_LOGO_LETTERS][5][6] = {
    {"####.", "#...#", "####.", "#...#", "####."}, /* B */
    {"#....", "#....", "#....", "#....", "#####"}, /* L */
    {"#####", "..#..", "..#..", "..#..", "#####"}, /* I */
    {"#...#", "##..#", "#.#.#", "#..##", "#...#"}, /* N */
    {"#...#", "#..#.", "###..", "#..#.", "#...#"}, /* K */
};

/* "BLINK" made of squares, centered on `center`, each letter in one of the squares' colors. */
static void blink_draw_logo(const blink_hud *hud, rb_vec2 center) {
    const f32 cell = BLINK_LOGO_CELL * hud->unit;
    /* 5 cells per letter and one between letters: 29 cells, 116 font pixels. */
    const f32 width = (f32)(BLINK_LOGO_LETTERS * 6 - 1) * cell;
    const f32 left = center.x - width * 0.5f;
    const f32 top = center.y - 2.5f * cell;
    for (u32 letter = 0; letter < BLINK_LOGO_LETTERS; ++letter) {
        rb_color color = blink_square_color(letter);
        for (u32 row = 0; row < 5; ++row) {
            for (u32 col = 0; col < 5; ++col) {
                if (blink_logo[letter][row][col] == '#') {
                    rb_renderer_draw_quad(hud->renderer, left + (f32)(letter * 6 + col) * cell,
                                          top + (f32)row * cell, cell - hud->unit, cell - hud->unit,
                                          color);
                }
            }
        }
    }
}

static void blink_draw_title(const blink_state *state, const blink_hud *hud) {
    f32 u = hud->unit;
    f32 cx = hud->center.x;
    f32 cy = hud->center.y;
    blink_draw_logo(hud, (rb_vec2){cx, cy - 16.0f * u});
    blink_hud_text(hud, (rb_vec2){cx, cy + 14.0f * u}, BLINK_TEXT, RB_TEXT_CENTER, RB_TEXT_MIDDLE,
                   0.6f, "CLICK THE SQUARES");
    /* The rules' clock, not the wall clock: a replay blinks the same way. */
    f32 cycle = state->phase_time / BLINK_PROMPT_PERIOD;
    if (cycle - (f32)(u32)cycle < 0.5f) {
        blink_hud_text(hud, (rb_vec2){cx, cy + 30.0f * u}, BLINK_TEXT, RB_TEXT_CENTER,
                       RB_TEXT_MIDDLE, 1.0f, "PRESS SPACE");
    }
}

/* The score in the top left corner, a pip per life in the top right one (faded once lost). */
static void blink_draw_status(const blink_state *state, const blink_hud *hud) {
    f32  u = hud->unit;
    f32  margin = BLINK_HUD_MARGIN * u;
    char score[32];
    snprintf(score, sizeof(score), "SCORE %u", state->score);
    blink_hud_text(hud, (rb_vec2){margin, margin}, BLINK_TEXT, RB_TEXT_LEFT, RB_TEXT_TOP, 1.0f,
                   score);

    /* n pips and the n - 1 gaps between them, ending at the right margin. */
    u32 lives = state->tuning.lives;
    f32 row = (f32)lives * BLINK_PIP_STEP - (BLINK_PIP_STEP - BLINK_PIP_SIDE);
    f32 left = hud->size.x - margin - row * u;
    for (u32 i = 0; i < lives; ++i) {
        rb_color color = i < state->lives ? rb_color_srgb(0.95f, 0.95f, 0.95f, 1.0f)
                                          : rb_color_srgb(1.0f, 1.0f, 1.0f, 0.15f);
        rb_renderer_draw_quad(hud->renderer, left + (f32)i * BLINK_PIP_STEP * u, margin,
                              BLINK_PIP_SIDE * u, BLINK_PIP_SIDE * u, color);
    }
}

/* Over the dark red field: the final score and, once a click may restart, how to. */
static void blink_draw_game_over(const blink_state *state, const blink_hud *hud) {
    f32  u = hud->unit;
    f32  cx = hud->center.x;
    f32  cy = hud->center.y;
    char summary[64];
    snprintf(summary, sizeof(summary), "SCORE %u  BEST %u", state->score, state->best);
    blink_hud_text(hud, (rb_vec2){cx, cy - 14.0f * u}, BLINK_TITLE_TEXT, RB_TEXT_CENTER,
                   RB_TEXT_MIDDLE, 1.0f, "GAME OVER");
    blink_hud_text(hud, (rb_vec2){cx, cy + 6.0f * u}, BLINK_TEXT, RB_TEXT_CENTER, RB_TEXT_MIDDLE,
                   1.0f, summary);
    if (state->phase_time >= state->tuning.restart_delay) {
        blink_hud_text(hud, (rb_vec2){cx, cy + 22.0f * u}, BLINK_TEXT, RB_TEXT_CENTER,
                       RB_TEXT_MIDDLE, 0.6f, "CLICK OR SPACE");
    }
}

void blink_view_draw(const blink_state *state, const rb_camera2d *camera, const rb_font *font,
                     rb_vec2 target, rb_renderer *renderer) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(camera != NULL);
    RB_ASSERT(font != NULL);
    blink_draw_field(state, camera, renderer);

    blink_hud hud = {
        .renderer = renderer,
        .font = font,
        .unit = blink_view_hud_scale(target.y),
        .size = target,
        .center = {(f32)(u32)(target.x * 0.5f), (f32)(u32)(target.y * 0.5f)},
    };
    switch (state->phase) {
    case BLINK_PHASE_TITLE:
        blink_draw_title(state, &hud);
        break;
    case BLINK_PHASE_PLAYING:
        blink_draw_status(state, &hud);
        break;
    case BLINK_PHASE_GAME_OVER:
        blink_draw_game_over(state, &hud);
        break;
    }
}

void blink_view_report(const blink_state *state, blink_events events) {
    RB_ASSERT(state != NULL);
    /* In the order they happen within a step: the actions first, then the time. */
    if ((events & BLINK_EVENT_STARTED) != 0) {
        RB_LOG_INFO("New game: %u lives", state->lives);
    }
    if ((events & BLINK_EVENT_HIT) != 0) {
        RB_LOG_INFO("Hit: score %u, next square lasts %.2f s", state->score,
                    (double)state->lifetime);
    }
    if ((events & BLINK_EVENT_EXPIRED) != 0) {
        RB_LOG_INFO("Missed: %u %s left", state->lives, state->lives == 1 ? "life" : "lives");
    }
    if ((events & BLINK_EVENT_GAME_OVER) != 0) {
        RB_LOG_INFO("Game over: score %u, best %u. Click or Space to play again", state->score,
                    state->best);
    }
}

/* ============================================================================
 * Icon
 * ========================================================================== */

/* Inside the square [lo, hi] x [lo, hi] with corners rounded by `radius`. */
static bool in_rounded_square(f32 x, f32 y, f32 lo, f32 hi, f32 radius) {
    /* The nearest point of the square shrunk by the radius: within it, the distance is 0. */
    f32 cx = x < lo + radius ? lo + radius : x > hi - radius ? hi - radius : x;
    f32 cy = y < lo + radius ? lo + radius : y > hi - radius ? hi - radius : y;
    f32 dx = x - cx;
    f32 dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

/*
 * Each pixel is sampled 4x4 times: the share of samples inside the tile is
 * its alpha, so the rounded corners fade instead of stepping, even at 16
 * pixels. Colors are sRGB bytes, as rb_image wants them.
 */
#define ICON_SAMPLES 4u

void blink_view_draw_icon(u32 side, u8 *pixels) {
    RB_ASSERT(side > 0 && pixels != NULL);
    static const u8 tile[3] = {24, 27, 38};
    static const u8 border[3] = {96, 104, 132};
    static const u8 square[3] = {250, 179, 51}; /* the palette's orange */
    const f32       s = (f32)side;
    const f32       radius = s * 0.2f;
    const f32       edge = s / 16.0f; /* the border: 1 pixel at 16, 4 at 64 */
    const f32       half_square = s * 0.25f;

    for (u32 y = 0; y < side; ++y) {
        for (u32 x = 0; x < side; ++x) {
            u32 sum[3] = {0};
            u32 covered = 0;
            for (u32 sy = 0; sy < ICON_SAMPLES; ++sy) {
                for (u32 sx = 0; sx < ICON_SAMPLES; ++sx) {
                    /* The sample's center inside the pixel. */
                    f32 px = (f32)x + ((f32)sx + 0.5f) / (f32)ICON_SAMPLES;
                    f32 py = (f32)y + ((f32)sy + 0.5f) / (f32)ICON_SAMPLES;
                    if (!in_rounded_square(px, py, 0.0f, s, radius)) {
                        continue;
                    }
                    const u8 *color = border;
                    if (px >= s * 0.5f - half_square && px <= s * 0.5f + half_square &&
                        py >= s * 0.5f - half_square && py <= s * 0.5f + half_square) {
                        color = square;
                    } else if (in_rounded_square(px, py, edge, s - edge, radius - edge)) {
                        color = tile;
                    }
                    sum[0] += color[0];
                    sum[1] += color[1];
                    sum[2] += color[2];
                    ++covered;
                }
            }
            u8 *p = &pixels[(y * side + x) * 4];
            /* Straight alpha: the color of the covered samples, their share as alpha. */
            for (u32 c = 0; c < 3; ++c) {
                p[c] = covered > 0 ? (u8)(sum[c] / covered) : 0;
            }
            p[3] = (u8)(covered * 255u / (ICON_SAMPLES * ICON_SAMPLES));
        }
    }
}
