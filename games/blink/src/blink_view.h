/*
 * Blink's presentation layer (ADR 0014): reads the rules' state, never changes
 * it (`const`), and draws it or reports it.
 */
#ifndef BLINK_VIEW_H
#define BLINK_VIEW_H

#include <reborn/camera2d.h>
#include <reborn/renderer.h>

#include "blink_rules.h"

/*
 * The field through `camera`, the square shrinking with its time, a pip per
 * life in the top left corner; the field turns dark red once the game is over.
 * Only inside the game's `render`.
 */
void blink_view_draw(const blink_state *state, const rb_camera2d *camera, rb_renderer *renderer);

/* A log line per event of the step, with the score and lives after it: the HUD until text. */
void blink_view_report(const blink_state *state, blink_events events);

/*
 * The window icon at `side` pixels (ADR 0020): a dark rounded tile with a light
 * border and an orange square in the middle, the game in miniature. Writes
 * side * side RGBA pixels into `pixels`.
 */
void blink_view_draw_icon(u32 side, u8 *pixels);

#endif /* BLINK_VIEW_H */
