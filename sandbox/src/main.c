#include <stdlib.h>

#include <reborn/log.h>
#include <reborn/platform.h>
#include <reborn/version.h>

/*
 * Opens a window and runs until it is closed. The loop never sleeps, so it
 * keeps one CPU core busy: the game loop (roadmap §4) will add timing.
 */
static int run_window(void) {
    rb_platform_state *platform = malloc(rb_platform_state_size());
    if (platform == NULL || !rb_platform_startup(platform)) {
        RB_LOG_FATAL("Platform startup failed");
        free(platform);
        return 1;
    }

    rb_window window = rb_window_create(
        platform,
        &(rb_window_desc){.title = "Reborn Sandbox — città", .width = 1280, .height = 720});
    if (!rb_window_is_valid(platform, window)) {
        RB_LOG_FATAL("Window creation failed");
        rb_platform_shutdown(platform);
        free(platform);
        return 1;
    }

    for (bool running = true; running;) {
        rb_platform_pump(platform);
        rb_event ev;
        while (rb_platform_poll_event(platform, &ev)) {
            if (ev.type == RB_EVENT_WINDOW_CLOSE_REQUESTED) {
                RB_LOG_INFO("Window 0x%08x: close requested, shutting down", ev.window.id);
                running = false;
            }
        }
    }

    rb_window_destroy(platform, window);
    rb_platform_shutdown(platform);
    free(platform);
    return 0;
}

int main(void) {
    /* The file lands in the working directory: the repository root when run as in CLAUDE.md. */
    rb_log_startup(&(rb_log_config){
        .console_level = RB_LOG_LEVEL_TRACE,
        .file_path = "reborn.log.jsonl",
        .file_level = RB_LOG_LEVEL_TRACE,
    });
    RB_LOG_INFO("Reborn Engine v%s", rb_version_string());

    int result = 1;
    if (rb_version_number() != RB_VERSION_NUMBER) {
        RB_LOG_FATAL("Version mismatch: header %s, library %s", RB_VERSION_STRING,
                     rb_version_string());
    } else {
        result = run_window();
    }

    RB_LOG_INFO("Exiting with code %d", result);
    rb_log_shutdown();
    return result;
}
