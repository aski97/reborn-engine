#include <stdio.h>
#include <stdlib.h>

#include <reborn/log.h>
#include <reborn/platform.h>
#include <reborn/version.h>

/*
 * Modifiers as " [Ctrl+Shift]" (empty without any), so a combination reads as
 * one line in the log: "key S pressed [Ctrl]".
 */
static const char *format_mods(u32 mods, char *buf, usize size) {
    static const struct {
        u32         bit;
        const char *name;
    } names[] = {{RB_KEY_MOD_CTRL, "Ctrl"},
                 {RB_KEY_MOD_SHIFT, "Shift"},
                 {RB_KEY_MOD_ALT, "Alt"},
                 {RB_KEY_MOD_SUPER, "Super"}};
    usize used = 0;
    buf[0] = '\0';
    for (usize i = 0; i < RB_ARRAY_COUNT(names); ++i) {
        if ((mods & names[i].bit) != 0 && used < size) {
            int n =
                snprintf(buf + used, size - used, "%s%s", used == 0 ? " [" : "+", names[i].name);
            used += n > 0 ? (usize)n : 0;
        }
    }
    if (used > 0 && used < size) {
        snprintf(buf + used, size - used, "]");
    }
    return buf;
}

/* Logs every event, so window lifecycle and input can be checked by hand. False to quit. */
static bool handle_event(const rb_event *ev) {
    u32  id = ev->window.id;
    char mods[32];
    switch (ev->type) {
    case RB_EVENT_WINDOW_CLOSE_REQUESTED:
        RB_LOG_INFO("Window 0x%08x: close requested, shutting down", id);
        return false;
    case RB_EVENT_WINDOW_RESIZED:
        RB_LOG_DEBUG("Window 0x%08x: resized to %ux%u logical, %ux%u pixels", id, ev->resize.width,
                     ev->resize.height, ev->resize.pixel_width, ev->resize.pixel_height);
        return true;
    case RB_EVENT_WINDOW_MOVED:
        RB_LOG_DEBUG("Window 0x%08x: moved to %d,%d", id, ev->position.x, ev->position.y);
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
    case RB_EVENT_KEY_PRESSED:
    case RB_EVENT_KEY_RELEASED:
        RB_LOG_DEBUG("Window 0x%08x: key %s %s%s%s", id, rb_key_name(ev->key.key),
                     ev->type == RB_EVENT_KEY_PRESSED ? "pressed" : "released",
                     format_mods(ev->key.mods, mods, sizeof(mods)),
                     ev->key.repeat ? " (repeat)" : "");
        return true;
    case RB_EVENT_MOUSE_BUTTON_PRESSED:
    case RB_EVENT_MOUSE_BUTTON_RELEASED:
        RB_LOG_DEBUG("Window 0x%08x: mouse %s %s at %d,%d%s", id,
                     rb_mouse_button_name(ev->button.button),
                     ev->type == RB_EVENT_MOUSE_BUTTON_PRESSED ? "pressed" : "released",
                     ev->button.x, ev->button.y, format_mods(ev->button.mods, mods, sizeof(mods)));
        return true;
    case RB_EVENT_MOUSE_MOVED:
        /* TRACE: one per pump while the mouse moves. */
        RB_LOG_TRACE("Window 0x%08x: mouse moved to %d,%d", id, ev->move.x, ev->move.y);
        return true;
    case RB_EVENT_MOUSE_WHEEL:
        RB_LOG_DEBUG("Window 0x%08x: wheel %.2f,%.2f", id, (double)ev->wheel.x,
                     (double)ev->wheel.y);
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
        /* The queries, next to the events: a tap within one pump shows both edges. */
        if (rb_key_was_pressed(platform, RB_KEY_SPACE)) {
            RB_LOG_DEBUG("Query: Space was pressed this pump (down: %d)",
                         rb_key_is_down(platform, RB_KEY_SPACE));
        }
        if (rb_key_was_released(platform, RB_KEY_SPACE)) {
            RB_LOG_DEBUG("Query: Space was released this pump (down: %d)",
                         rb_key_is_down(platform, RB_KEY_SPACE));
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
