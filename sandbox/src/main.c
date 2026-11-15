#include <stdlib.h>

#include <reborn/log.h>
#include <reborn/platform.h>
#include <reborn/version.h>

/* Logs every event, so window lifecycle can be checked by hand. False to quit. */
static bool handle_event(const rb_event *ev) {
    u32 id = ev->window.id;
    switch (ev->type) {
    case RB_EVENT_WINDOW_CLOSE_REQUESTED:
        RB_LOG_INFO("Window 0x%08x: close requested, shutting down", id);
        return false;
    case RB_EVENT_WINDOW_RESIZED:
        RB_LOG_DEBUG("Window 0x%08x: resized to %ux%u logical, %ux%u pixels", id, ev->resize.width,
                     ev->resize.height, ev->resize.pixel_width, ev->resize.pixel_height);
        return true;
    case RB_EVENT_WINDOW_FOCUS_GAINED:
        RB_LOG_DEBUG("Window 0x%08x: focus gained", id);
        return true;
    case RB_EVENT_WINDOW_FOCUS_LOST:
        RB_LOG_DEBUG("Window 0x%08x: focus lost", id);
        return true;
    case RB_EVENT_WINDOW_MINIMIZED:
        RB_LOG_DEBUG("Window 0x%08x: minimized", id);
        return true;
    case RB_EVENT_WINDOW_RESTORED:
        RB_LOG_DEBUG("Window 0x%08x: restored", id);
        return true;
    default:
        return true;
    }
}

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
            /* Only ever cleared: a later event in the same batch must not undo a quit. */
            if (!handle_event(&ev)) {
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
