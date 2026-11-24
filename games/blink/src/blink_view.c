/* Blink's presentation layer (ADR 0014). */
#include "blink_view.h"

#include <reborn/assert.h>
#include <reborn/color.h>
#include <reborn/log.h>

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

#define BLINK_PIP_SIDE 0.3f /* world units */
#define BLINK_PIP_STEP 0.45f
#define BLINK_PIP_EDGE 0.3f /* from the field's top left corner */

void blink_view_draw(const blink_state *state, const rb_camera2d *camera, rb_renderer *renderer) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(camera != NULL);
    bool over = state->phase == BLINK_PHASE_GAME_OVER;
    rb_renderer_set_camera(renderer, camera);

    rb_renderer_draw_quad(renderer, 0.0f, 0.0f, state->tuning.field.x, state->tuning.field.y,
                          over ? rb_color_srgb(0.25f, 0.05f, 0.06f, 1.0f)
                               : rb_color_srgb(0.07f, 0.08f, 0.11f, 1.0f));

    /*
     * Drawn as the last step left it: alpha would let it shrink between steps,
     * but at 60 steps per second the difference is below a pixel.
     */
    if (!over) {
        rb_rect square = blink_square_rect(state);
        rb_renderer_draw_quad(renderer, square.x, square.y, square.width, square.height,
                              blink_square_color(state->square.color));
    }

    /* On top of the square: the lives stay readable wherever it appears. */
    for (u32 i = 0; i < state->tuning.lives; ++i) {
        rb_color color = i < state->lives ? rb_color_srgb(0.95f, 0.95f, 0.95f, 1.0f)
                                          : rb_color_srgb(1.0f, 1.0f, 1.0f, 0.15f);
        rb_renderer_draw_quad(renderer, BLINK_PIP_EDGE + (f32)i * BLINK_PIP_STEP, BLINK_PIP_EDGE,
                              BLINK_PIP_SIDE, BLINK_PIP_SIDE, color);
    }

    rb_renderer_set_camera(renderer, NULL);
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
