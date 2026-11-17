/*
 * Sandbox game: logs every event and the loop's rates, so window lifecycle,
 * input and timing can be checked by hand.
 */
#include "game.h"

#include <stdio.h>

#include <reborn/clock.h>
#include <reborn/log.h>
#include <reborn/renderer.h>

typedef struct sandbox_state {
    u64 start; /* the clear color's animation runs from here */
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
    game->start = rb_time_now();
    game->window_start = game->start;
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

/* ============================================================================
 * Rendering
 * ========================================================================== */

/* One turn of the hue wheel: slow enough to be calm, fast enough to see it move. */
#define SANDBOX_HUE_PERIOD_NS (12 * RB_NS_PER_S)

/*
 * Hue, saturation, value in [0, 1] to sRGB: the hue wheel is six sectors,
 * each fading one channel between the other two. In sRGB, like a color picker.
 */
static rb_color hsv_to_srgb(f32 h, f32 s, f32 v) {
    f32 h6 = h * 6.0f;
    u32 sector = (u32)h6;
    f32 f = h6 - (f32)sector; /* how far into the sector */
    f32 p = v * (1.0f - s);
    f32 q = v * (1.0f - s * f);
    f32 t = v * (1.0f - s * (1.0f - f));
    switch (sector % 6) {
    case 0:
        return rb_color_srgb(v, t, p, 1.0f);
    case 1:
        return rb_color_srgb(q, v, p, 1.0f);
    case 2:
        return rb_color_srgb(p, v, t, 1.0f);
    case 3:
        return rb_color_srgb(p, q, v, 1.0f);
    case 4:
        return rb_color_srgb(t, p, v, 1.0f);
    default:
        return rb_color_srgb(v, p, q, 1.0f);
    }
}

/*
 * Nothing to draw yet (roadmap §7): the clear color turns around the hue
 * wheel, which shows that frames keep reaching the screen. Muted (low
 * saturation and value) so it stays easy on the eyes. Shows from the next frame.
 */
static void game_render(void *state, rb_app *app, f64 alpha) {
    (void)alpha;
    sandbox_state *game = state;
    ++game->frames;
    /* Integer modulo first: the elapsed nanoseconds would lose precision as a float. */
    u64 phase = (rb_time_now() - game->start) % SANDBOX_HUE_PERIOD_NS;
    f32 hue = (f32)((f64)phase / (f64)SANDBOX_HUE_PERIOD_NS);
    rb_renderer_set_clear_color(rb_app_renderer(app), hsv_to_srgb(hue, 0.5f, 0.45f));
}

const rb_app_callbacks sandbox_game = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
};
