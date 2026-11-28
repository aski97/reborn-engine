/* Blink's presentation layer (ADR 0014). */
#include "blink_view.h"

#include <stdio.h>

#include <reborn/assert.h>
#include <reborn/color.h>
#include <reborn/log.h>
#include <reborn/text.h>

/* Saturated and bright, so each stands out on the dark field; BLINK_COLOR_COUNT of them. */
static rb_color blink_square_color(u32 index, f32 alpha) {
    static const f32 srgb[BLINK_COLOR_COUNT][3] = {
        {0.95f, 0.30f, 0.30f}, /* red */
        {0.98f, 0.70f, 0.20f}, /* orange */
        {0.95f, 0.90f, 0.30f}, /* yellow */
        {0.35f, 0.85f, 0.45f}, /* green */
        {0.30f, 0.70f, 1.00f}, /* blue */
        {0.80f, 0.45f, 0.95f}, /* violet */
    };
    RB_ASSERT(index < BLINK_COLOR_COUNT);
    return rb_color_srgb(srgb[index][0], srgb[index][1], srgb[index][2], alpha);
}

/* ============================================================================
 * Effects (world units): what answers a click or a lost square
 * ========================================================================== */

#define BLINK_POP_TIME  0.30f /* seconds */
#define BLINK_MARK_TIME 0.25f
#define BLINK_LOST_TIME 0.50f
#define BLINK_MARK_SIDE 0.30f /* world units, as it starts */
#define BLINK_OUTLINE   0.07f /* world units: 6 to 8 pixels from 720p to 1080p */

static f32 blink_effect_duration(blink_effect_type type) {
    switch (type) {
    case BLINK_EFFECT_POP:
        return BLINK_POP_TIME;
    case BLINK_EFFECT_MARK:
        return BLINK_MARK_TIME;
    case BLINK_EFFECT_LOST:
        return BLINK_LOST_TIME;
    case BLINK_EFFECT_NONE:
        break;
    }
    return 0.0f;
}

/* A free slot, or else the effect nearest its end: the newest is what the player just did. */
static void blink_effect_start(blink_view *view, blink_effect effect) {
    blink_effect *slot = &view->effects[0];
    f32           slot_done = 0.0f;
    for (u32 i = 0; i < BLINK_MAX_EFFECTS; ++i) {
        blink_effect *candidate = &view->effects[i];
        if (candidate->type == BLINK_EFFECT_NONE) {
            slot = candidate;
            break;
        }
        f32 done = candidate->age / blink_effect_duration(candidate->type);
        if (done > slot_done) {
            slot = candidate;
            slot_done = done;
        }
    }
    *slot = effect;
}

static rb_rect blink_rect_around(rb_vec2 center, f32 side) {
    return (rb_rect){center.x - side * 0.5f, center.y - side * 0.5f, side, side};
}

static rb_rect blink_rect_scaled(rb_rect rect, f32 scale) {
    rb_vec2 center = {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    return (rb_rect){center.x - rect.width * scale * 0.5f, center.y - rect.height * scale * 0.5f,
                     rect.width * scale, rect.height * scale};
}

/* Four strips that do not overlap, so a translucent outline has even corners. */
static void blink_draw_outline(rb_renderer *renderer, rb_rect rect, f32 thickness, rb_color color) {
    if (rect.width <= 2.0f * thickness || rect.height <= 2.0f * thickness) {
        rb_renderer_draw_quad(renderer, rect.x, rect.y, rect.width, rect.height, color);
        return;
    }
    f32 inner = rect.height - 2.0f * thickness;
    f32 right = rect.x + rect.width - thickness;
    rb_renderer_draw_quad(renderer, rect.x, rect.y, rect.width, thickness, color);
    rb_renderer_draw_quad(renderer, rect.x, rect.y + rect.height - thickness, rect.width, thickness,
                          color);
    rb_renderer_draw_quad(renderer, rect.x, rect.y + thickness, thickness, inner, color);
    rb_renderer_draw_quad(renderer, right, rect.y + thickness, thickness, inner, color);
}

static void blink_draw_effect(rb_renderer *renderer, const blink_effect *effect) {
    /* 0 as it starts, toward 1 as it ends. */
    f32 t = effect->age / blink_effect_duration(effect->type);
    f32 fade = 1.0f - t;
    switch (effect->type) {
    case BLINK_EFFECT_POP: {
        /* A white flash over its first 40%, the outline growing to 1.8 times. */
        f32 flash = 1.0f - t * 2.5f;
        if (flash > 0.0f) {
            rb_rect area = effect->area;
            rb_renderer_draw_quad(renderer, area.x, area.y, area.width, area.height,
                                  rb_color_srgb(1.0f, 1.0f, 1.0f, 0.7f * flash));
        }
        blink_draw_outline(renderer, blink_rect_scaled(effect->area, 1.0f + 0.8f * t),
                           BLINK_OUTLINE, blink_square_color(effect->color, fade));
        break;
    }
    case BLINK_EFFECT_MARK:
        blink_draw_outline(renderer, blink_rect_scaled(effect->area, 1.0f + 0.5f * t),
                           BLINK_OUTLINE * 0.6f, rb_color_srgb(1.0f, 1.0f, 1.0f, 0.8f * fade));
        break;
    case BLINK_EFFECT_LOST: {
        rb_rect area = effect->area;
        rb_renderer_draw_quad(renderer, area.x, area.y, area.width, area.height,
                              rb_color_srgb(0.95f, 0.20f, 0.20f, 0.45f * fade));
        blink_draw_outline(renderer, blink_rect_scaled(area, 1.0f + 0.4f * t), BLINK_OUTLINE,
                           rb_color_srgb(0.95f, 0.20f, 0.20f, fade));
        break;
    }
    case BLINK_EFFECT_NONE:
        break;
    }
}

void blink_view_update(blink_view *view, const blink_state *state, const blink_events *events,
                       f32 dt) {
    RB_ASSERT(view != NULL);
    RB_ASSERT(state != NULL);
    RB_ASSERT(events != NULL);
    for (u32 i = 0; i < BLINK_MAX_EFFECTS; ++i) {
        blink_effect *effect = &view->effects[i];
        if (effect->type == BLINK_EFFECT_NONE) {
            continue;
        }
        effect->age += dt;
        if (effect->age >= blink_effect_duration(effect->type)) {
            *effect = (blink_effect){0};
        }
    }
    /* In the order they happened within the step: the actions first, then the time. */
    for (u32 i = 0; i < events->count; ++i) {
        const blink_event *event = &events->items[i];
        switch (event->type) {
        case BLINK_EVENT_STARTED:
            RB_LOG_INFO("New game: %u lives", state->lives);
            break;
        case BLINK_EVENT_HIT:
            RB_LOG_INFO("Hit: score %u, next square lasts %.2f s", state->score,
                        (double)state->lifetime);
            if (event->graced) {
                RB_LOG_DEBUG("Hit within the grace: outside the square as it is now");
            }
            blink_effect_start(view, (blink_effect){.type = BLINK_EFFECT_POP,
                                                    .area = event->square,
                                                    .color = event->color});
            break;
        case BLINK_EVENT_CLICK_OUTSIDE:
            /* Debug only: a player clicks outside often, and it costs nothing. */
            RB_LOG_DEBUG("Click outside the square: %.2f away, its side %.2f",
                         (double)event->distance, (double)event->square.width);
            blink_effect_start(
                view, (blink_effect){.type = BLINK_EFFECT_MARK,
                                     .area = blink_rect_around(event->point, BLINK_MARK_SIDE)});
            break;
        case BLINK_EVENT_EXPIRED:
            RB_LOG_INFO("Missed: %u %s left", state->lives, state->lives == 1 ? "life" : "lives");
            blink_effect_start(view,
                               (blink_effect){.type = BLINK_EFFECT_LOST, .area = event->square});
            break;
        case BLINK_EVENT_GAME_OVER:
            RB_LOG_INFO("Game over: score %u, best %u. Click or Space to play again", state->score,
                        state->best);
            break;
        }
    }
}

/* ============================================================================
 * Field
 * ========================================================================== */

/* The world, through the camera: the field, the effects and, while playing, the square. */
static void blink_draw_field(const blink_view *view, const blink_state *state,
                             const rb_camera2d *camera, rb_renderer *renderer) {
    bool over = state->phase == BLINK_PHASE_GAME_OVER;
    rb_renderer_set_camera(renderer, camera);
    rb_renderer_draw_quad(renderer, 0.0f, 0.0f, state->tuning.field.x, state->tuning.field.y,
                          over ? rb_color_srgb(0.25f, 0.05f, 0.06f, 1.0f)
                               : rb_color_srgb(0.07f, 0.08f, 0.11f, 1.0f));
    /* Under the square: the new one, elsewhere, is what to click next. */
    for (u32 i = 0; i < BLINK_MAX_EFFECTS; ++i) {
        blink_draw_effect(renderer, &view->effects[i]);
    }
    /*
     * Drawn as the last step left it: alpha would let it shrink between steps,
     * but at 60 steps per second the difference is below a pixel.
     */
    if (state->phase == BLINK_PHASE_PLAYING) {
        rb_rect square = blink_square_rect(state);
        rb_renderer_draw_quad(renderer, square.x, square.y, square.width, square.height,
                              blink_square_color(state->square.color, 1.0f));
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
        rb_color color = blink_square_color(letter, 1.0f);
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

/* Before a game's first square: where to look, and that it is about to start. */
static void blink_draw_ready(const blink_hud *hud) {
    blink_hud_text(hud, hud->center, BLINK_TITLE_TEXT, RB_TEXT_CENTER, RB_TEXT_MIDDLE, 1.0f,
                   "READY");
}

void blink_view_draw(const blink_view *view, const blink_state *state, const rb_camera2d *camera,
                     const rb_font *font, rb_vec2 target, rb_renderer *renderer) {
    RB_ASSERT(view != NULL);
    RB_ASSERT(state != NULL);
    RB_ASSERT(camera != NULL);
    RB_ASSERT(font != NULL);
    blink_draw_field(view, state, camera, renderer);

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
    case BLINK_PHASE_READY:
        blink_draw_status(state, &hud);
        blink_draw_ready(&hud);
        break;
    case BLINK_PHASE_PLAYING:
        blink_draw_status(state, &hud);
        break;
    case BLINK_PHASE_GAME_OVER:
        blink_draw_game_over(state, &hud);
        break;
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
