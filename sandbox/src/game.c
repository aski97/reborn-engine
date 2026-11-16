/*
 * Sandbox game: logs every event and the loop's rates, so window lifecycle,
 * input and timing can be checked by hand.
 */
#include "game.h"

#include <stdio.h>

#include <reborn/clock.h>
#include <reborn/log.h>

typedef struct sandbox_state {
    /* Rates over the last second, logged once per second. */
    u64 window_start;
    u32 steps;
    u32 frames;
} sandbox_state;

/* ============================================================================
 * Event logging
 * ========================================================================== */

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

static void game_on_event(void *state, rb_app *app, const rb_event *ev) {
    (void)state;
    (void)app;
    u32  id = ev->window.id;
    char mods[32];
    switch (ev->type) {
    case RB_EVENT_WINDOW_CLOSE_REQUESTED:
        RB_LOG_INFO("Window 0x%08x: close requested", id);
        break;
    case RB_EVENT_WINDOW_RESIZED:
        RB_LOG_DEBUG("Window 0x%08x: resized to %ux%u logical, %ux%u pixels", id, ev->resize.width,
                     ev->resize.height, ev->resize.pixel_width, ev->resize.pixel_height);
        break;
    case RB_EVENT_WINDOW_MOVED:
        RB_LOG_DEBUG("Window 0x%08x: moved to %d,%d", id, ev->position.x, ev->position.y);
        break;
    case RB_EVENT_WINDOW_FOCUS_GAINED:
        RB_LOG_DEBUG("Window 0x%08x: focus gained", id);
        break;
    case RB_EVENT_WINDOW_FOCUS_LOST:
        RB_LOG_DEBUG("Window 0x%08x: focus lost", id);
        break;
    case RB_EVENT_WINDOW_MINIMIZED:
        RB_LOG_DEBUG("Window 0x%08x: minimized", id);
        break;
    case RB_EVENT_WINDOW_RESTORED:
        RB_LOG_DEBUG("Window 0x%08x: restored", id);
        break;
    case RB_EVENT_KEY_PRESSED:
    case RB_EVENT_KEY_RELEASED:
        RB_LOG_DEBUG("Window 0x%08x: key %s %s%s%s", id, rb_key_name(ev->key.key),
                     ev->type == RB_EVENT_KEY_PRESSED ? "pressed" : "released",
                     format_mods(ev->key.mods, mods, sizeof(mods)),
                     ev->key.repeat ? " (repeat)" : "");
        break;
    case RB_EVENT_MOUSE_BUTTON_PRESSED:
    case RB_EVENT_MOUSE_BUTTON_RELEASED:
        RB_LOG_DEBUG("Window 0x%08x: mouse %s %s at %d,%d%s", id,
                     rb_mouse_button_name(ev->button.button),
                     ev->type == RB_EVENT_MOUSE_BUTTON_PRESSED ? "pressed" : "released",
                     ev->button.x, ev->button.y, format_mods(ev->button.mods, mods, sizeof(mods)));
        break;
    case RB_EVENT_MOUSE_MOVED:
        /* TRACE: one per step while the mouse moves. */
        RB_LOG_TRACE("Window 0x%08x: mouse moved to %d,%d", id, ev->move.x, ev->move.y);
        break;
    case RB_EVENT_MOUSE_WHEEL:
        RB_LOG_DEBUG("Window 0x%08x: wheel %.2f,%.2f", id, (double)ev->wheel.x,
                     (double)ev->wheel.y);
        break;
    default:
        break;
    }
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

static bool game_configure(rb_app_config *config) {
    /*
     * Everything, to check events by hand. The file lands in the working
     * directory: the repository root when run as in CLAUDE.md.
     */
    config->log.console_level = RB_LOG_LEVEL_TRACE;
    config->log.file_path = "reborn.log.jsonl";
    config->log.file_level = RB_LOG_LEVEL_TRACE;
    config->window.title = "Reborn Sandbox — città";
    RB_APP_STATE(config, sandbox_state);
    return true;
}

static bool game_init(void *state, rb_app *app) {
    (void)app;
    sandbox_state *game = state;
    game->window_start = rb_time_now();
    return true;
}

static void game_update(void *state, rb_app *app, f64 dt) {
    (void)dt;
    sandbox_state     *game = state;
    rb_platform_state *platform = rb_app_platform(app);

    /* Edges cover the time since the previous step: a tap shows both, exactly once. */
    if (rb_key_was_pressed(platform, RB_KEY_SPACE)) {
        RB_LOG_DEBUG("Query: Space was pressed this step (down: %d)",
                     rb_key_is_down(platform, RB_KEY_SPACE));
    }
    if (rb_key_was_released(platform, RB_KEY_SPACE)) {
        RB_LOG_DEBUG("Query: Space was released this step (down: %d)",
                     rb_key_is_down(platform, RB_KEY_SPACE));
    }

    ++game->steps;
    u64 now = rb_time_now();
    if (now - game->window_start >= RB_NS_PER_S) {
        RB_LOG_DEBUG("Last %.2f s: %u steps, %u frames",
                     rb_time_to_seconds(now - game->window_start), game->steps, game->frames);
        game->window_start = now;
        game->steps = 0;
        game->frames = 0;
    }
}

/* Nothing to draw before the renderer (roadmap §7): only counts frames. */
static void game_render(void *state, rb_app *app, f64 alpha) {
    (void)app;
    (void)alpha;
    sandbox_state *game = state;
    ++game->frames;
}

const rb_app_callbacks sandbox_game = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
};
