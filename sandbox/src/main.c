#include <stdio.h>
#include <stdlib.h>

#include <reborn/platform.h>
#include <reborn/version.h>

/*
 * Opens a window and runs until it is closed. The loop never sleeps, so it
 * keeps one CPU core busy: the game loop (roadmap §4) will add timing.
 */
static int run_window(void) {
    rb_platform_state *platform = malloc(rb_platform_state_size());
    if (platform == NULL || !rb_platform_startup(platform)) {
        fprintf(stderr, "Platform startup failed\n");
        free(platform);
        return 1;
    }

    rb_window window = rb_window_create(
        platform,
        &(rb_window_desc){.title = "Reborn Sandbox — città", .width = 1280, .height = 720});
    if (!rb_window_is_valid(platform, window)) {
        fprintf(stderr, "Window creation failed\n");
        rb_platform_shutdown(platform);
        free(platform);
        return 1;
    }

    for (bool running = true; running;) {
        rb_platform_pump(platform);
        rb_event ev;
        while (rb_platform_poll_event(platform, &ev)) {
            if (ev.type == RB_EVENT_WINDOW_CLOSE_REQUESTED) {
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
    printf("Reborn Engine v%s\n", rb_version_string());

    if (rb_version_number() != RB_VERSION_NUMBER) {
        fprintf(stderr, "Version mismatch: header %s, library %s\n", RB_VERSION_STRING,
                rb_version_string());
        return 1;
    }

    return run_window();
}
