/*
 * Blink's input layer (ADR 0014): platform events become the game's actions,
 * so the rules never see a mouse or a key, and a test or a replay can feed
 * them the same actions. Pure: the camera and the window's size come in.
 */
#ifndef BLINK_INPUT_H
#define BLINK_INPUT_H

#include <reborn/camera2d.h>
#include <reborn/platform.h>

#include "blink_rules.h"

/*
 * The action `event` stands for, if any: a left button press is a click at its
 * world point (through `camera`, for a window of `target` pixels), a Space
 * press a restart. False for every other event.
 */
bool blink_input_translate(const rb_event *event, const rb_camera2d *camera, rb_vec2 target,
                           blink_action *out);

#endif /* BLINK_INPUT_H */
