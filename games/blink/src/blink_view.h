/*
 * Blink's presentation layer (ADR 0014): reads the rules' state, never changes
 * it (`const`), and draws it or reports it.
 */
#ifndef BLINK_VIEW_H
#define BLINK_VIEW_H

#include <reborn/camera2d.h>
#include <reborn/font.h>
#include <reborn/renderer.h>

#include "blink_rules.h"

/*
 * The HUD's scale (ADR 0021, G1): how many window pixels one pixel of the
 * builtin font covers, a whole number so the text stays sharp. One per 180
 * pixels of window height, at least 1: 4 at 720p, 6 at 1080p, 8 at 1440p, 12
 * at 2160p, so on common screens the HUD keeps its proportion exactly.
 */
f32 blink_view_hud_scale(f32 pixel_height);

/*
 * The field and the square through `camera`, the field dark red once the
 * game is over; then the HUD in window pixels for a window of `target`
 * pixels: the title (a logo of squares, "PRESS SPACE" blinking), the score
 * and a pip per life while playing, the final score over the field after it.
 * Only inside the game's `render`.
 */
void blink_view_draw(const blink_state *state, const rb_camera2d *camera, const rb_font *font,
                     rb_vec2 target, rb_renderer *renderer);

/* A log line per event of the step, with the score and lives after it: what an agent reads. */
void blink_view_report(const blink_state *state, blink_events events);

/*
 * The window icon at `side` pixels (ADR 0020): a dark rounded tile with a light
 * border and an orange square in the middle, the game in miniature. Writes
 * side * side RGBA pixels into `pixels`.
 */
void blink_view_draw_icon(u32 side, u8 *pixels);

#endif /* BLINK_VIEW_H */
