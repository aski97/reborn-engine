/*
 * The engine's screen commands (ADR 0024): what the window shows, as a PNG
 * file an agent can open.
 */
#include <reborn/assert.h>
#include <reborn/image.h>
#include <reborn/log.h>

#include <inttypes.h>
#include <stdio.h>

#include "app/app_state.h"
#include "renderer/renderer_internal.h"

/*
 * Renders a frame of its own, as `step` runs steps of its own: the image shows
 * the game exactly as the command finds it, paused or not, and the reply can
 * say which step that is. The extra frame reaches the window like any other.
 */
static void cmd_screen_capture(void *state, rb_app *app, const rb_command_args *args,
                               rb_command_reply *reply) {
    char        default_path[48];
    const char *path = default_path;
    if (args->count == 1) {
        path = args->values[0];
    } else {
        snprintf(default_path, sizeof(default_path), "screenshot-%" PRIu64 ".png",
                 app->frame_count);
    }
    if (!rb_renderer_begin_frame(app->renderer)) {
        rb_command_reply_error(reply, "nothing to capture: the window is minimized or shows "
                                      "nothing (see the log)");
        return;
    }
    if (app->callbacks->render != NULL) {
        /* Alpha 0: the state exactly as the last step left it, the step the reply names. */
        app->callbacks->render(state, app, 0.0);
    }
    rb_renderer_capture_frame(app->renderer);
    rb_renderer_end_frame(app->renderer);

    const rb_image *image = rb_renderer_captured_image(app->renderer);
    if (image == NULL) {
        rb_command_reply_error(reply, "the capture failed (see the log)");
        return;
    }
    usize bytes = 0;
    if (!rb_image_write_png(image, path, rb_heap_allocator(), &bytes)) {
        rb_command_reply_error(reply, "cannot write \"%s\" (see the log)", path);
        return;
    }
    RB_LOG_INFO("Screen captured to %s (%ux%u, %zu bytes) at step %" PRIu64, path, image->width,
                image->height, bytes, app->step_count);
    rb_command_reply_str(reply, "path", path);
    rb_command_reply_u64(reply, "width", image->width);
    rb_command_reply_u64(reply, "height", image->height);
    rb_command_reply_u64(reply, "bytes", bytes);
    rb_command_reply_u64(reply, "frame", app->frame_count);
    rb_command_reply_u64(reply, "step", app->step_count);
}

static const rb_command_desc APP_SCREEN_COMMANDS[] = {
    {"screen.capture", "[path]", "render a frame now and save it as PNG (screenshot-<frame>.png)",
     0, 1, cmd_screen_capture},
};

void rb_app_screen_commands_add(rb_app *app) {
    for (u32 i = 0; i < RB_ARRAY_COUNT(APP_SCREEN_COMMANDS); ++i) {
        bool added = rb_commands_add(app->commands, &APP_SCREEN_COMMANDS[i], false);
        RB_ASSERT(added);
        (void)added;
    }
}
