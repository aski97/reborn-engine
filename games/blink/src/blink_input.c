/* Blink's input layer (ADR 0014). */
#include "blink_input.h"

#include <reborn/assert.h>

bool blink_input_translate(const rb_event *event, const rb_camera2d *camera, rb_vec2 target,
                           blink_action *out) {
    RB_ASSERT(event != NULL);
    RB_ASSERT(camera != NULL);
    RB_ASSERT(out != NULL);
    switch (event->type) {
    case RB_EVENT_MOUSE_BUTTON_PRESSED:
        if (event->button.button != RB_MOUSE_BUTTON_LEFT) {
            return false;
        }
        /* Where the press happened, not where the mouse is now: it may have moved since. */
        *out = (blink_action){
            .type = BLINK_ACTION_CLICK,
            .point = rb_camera2d_screen_to_world(
                camera, target, (rb_vec2){(f32)event->button.x, (f32)event->button.y}),
        };
        return true;
    case RB_EVENT_KEY_PRESSED:
        /* A held key repeats: only the first press counts. */
        if (event->key.key != RB_KEY_SPACE || event->key.repeat) {
            return false;
        }
        *out = (blink_action){.type = BLINK_ACTION_START};
        return true;
    default:
        return false;
    }
}
