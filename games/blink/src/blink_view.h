/*
 * Blink's presentation layer (ADR 0014): reads the rules' state, never changes
 * it (`const`), and draws it or reports it. What it keeps of its own are the
 * effects still playing, started by the rules' events and timed by the steps,
 * so a replay shows them the same.
 */
#ifndef BLINK_VIEW_H
#define BLINK_VIEW_H

#include <reborn/camera2d.h>
#include <reborn/font.h>
#include <reborn/renderer.h>

#include "blink_rules.h"

/* More than a step can start: when full, a new effect takes the oldest one's place. */
#define BLINK_MAX_EFFECTS 32u

typedef enum blink_effect_type {
    BLINK_EFFECT_NONE, /* a free slot: a zeroed view has no effects */
    BLINK_EFFECT_POP,  /* a hit: the square flashes and its outline grows and fades */
    BLINK_EFFECT_MARK, /* a click outside: a small outline where it landed */
    BLINK_EFFECT_LOST, /* a square ran out: a red flash where it vanished */
} blink_effect_type;

typedef struct blink_effect {
    blink_effect_type type;
    rb_rect           area;  /* world units: where it plays */
    u32               color; /* POP: the square's */
    f32               age;   /* seconds since it started; it ends at its type's duration */
} blink_effect;

/* Zeroed, it is ready: no effects. */
typedef struct blink_view {
    blink_effect effects[BLINK_MAX_EFFECTS];
} blink_view;

/*
 * Once per step, after blink_step: the effects age by `dt` and end, the
 * step's events start new ones and log a line each, with the score and lives
 * after them: what an agent reads.
 */
void blink_view_update(blink_view *view, const blink_state *state, const blink_events *events,
                       f32 dt);

/*
 * The HUD's scale (ADR 0021, G1): how many window pixels one pixel of the
 * builtin font covers, a whole number so the text stays sharp. One per 180
 * pixels of window height, at least 1: 4 at 720p, 6 at 1080p, 8 at 1440p, 12
 * at 2160p, so on common screens the HUD keeps its proportion exactly.
 */
f32 blink_view_hud_scale(f32 pixel_height);

/*
 * The field, the effects and the square through `camera`, the field dark red
 * once the game is over; then the HUD in window pixels for a window of
 * `target` pixels: the title (a logo of squares, "PRESS SPACE" blinking), the
 * score and a pip per life while playing, READY before a game's first square,
 * the final score over the field after it. Only inside the game's `render`.
 */
void blink_view_draw(const blink_view *view, const blink_state *state, const rb_camera2d *camera,
                     const rb_font *font, rb_vec2 target, rb_renderer *renderer);

/*
 * The window icon at `side` pixels (ADR 0020): a dark rounded tile with a light
 * border and an orange square in the middle, the game in miniature. Writes
 * side * side RGBA pixels into `pixels`.
 */
void blink_view_draw_icon(u32 side, u8 *pixels);

#endif /* BLINK_VIEW_H */
