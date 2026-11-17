/*
 * Sandbox game: logs every event and the loop's rates, so window lifecycle,
 * input and timing can be checked by hand, and draws a few quads.
 */
#include "game.h"

#include <stdio.h>

#include <reborn/clock.h>
#include <reborn/log.h>
#include <reborn/renderer.h>

/* Frames shown by the frame-time graph: two seconds at 60 fps. */
#define SANDBOX_GRAPH_FRAMES 120u

typedef struct sandbox_state {
    u64 start; /* the clear color's animation runs from here */
    /* Rates over the last second, logged once per second. */
    u64 window_start;
    u32 steps;
    u32 frames;
    /*
     * Frame-time graph, toggled with L: the time between the starts of the
     * last frames, in milliseconds, in a ring (the oldest is overwritten; 0:
     * not measured yet).
     */
    bool graph_visible;
    u64  last_render; /* 0 before the first frame */
    f32  frame_ms[SANDBOX_GRAPH_FRAMES];
    u32  frame_next; /* where the next interval goes */
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
    game->graph_visible = true;
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
    if (rb_key_was_pressed(platform, RB_KEY_L)) {
        game->graph_visible = !game->graph_visible;
        RB_LOG_INFO("Frame-time graph %s (L)", game->graph_visible ? "shown" : "hidden");
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

/* One trip across the window and back for the sliding square. */
#define SANDBOX_SLIDE_PERIOD_NS (4 * RB_NS_PER_S)

#define SANDBOX_QUAD_SIDE 64.0f

/* Where `period` has got to since `start`, in [0, 1). */
static f32 sandbox_phase(u64 start, u64 period) {
    /* Integer modulo first: the elapsed nanoseconds would lose precision as a float. */
    u64 phase = (rb_time_now() - start) % period;
    return (f32)((f64)phase / (f64)period);
}

/*
 * Quads (ADR 0015): a row of opaque squares around the hue wheel; one sliding
 * across the window by fractions of a pixel, so its motion stays smooth; a
 * translucent one under the mouse, blending with whatever it covers.
 */
static void sandbox_draw_quads(rb_app *app, f32 hue, f32 slide) {
    rb_renderer       *renderer = rb_app_renderer(app);
    rb_platform_state *platform = rb_app_platform(app);
    rb_window_size     size = rb_window_get_size(platform, rb_app_window(app));
    const f32          side = SANDBOX_QUAD_SIDE;

    for (u32 i = 0; i < 6; ++i) {
        f32 h = hue + (f32)i / 6.0f;
        h = h >= 1.0f ? h - 1.0f : h;
        rb_renderer_draw_quad(renderer, 32.0f + (f32)i * (side + 16.0f), 32.0f, side, side,
                              hsv_to_srgb(h, 0.7f, 0.9f));
    }

    /* There and back: 0 to 1 in the first half of the period, 1 to 0 in the second. */
    f32 there_and_back = slide < 0.5f ? slide * 2.0f : 2.0f - slide * 2.0f;
    f32 travel = (f32)size.pixel_width - side;
    rb_renderer_draw_quad(renderer, there_and_back * travel, ((f32)size.pixel_height - side) * 0.5f,
                          side, side, rb_color_srgb(0.95f, 0.95f, 0.95f, 1.0f));

    /* Drawn last: on top of the others, which show through at half opacity. */
    rb_mouse_position mouse = rb_mouse_get_position(platform);
    f32               big = side * 1.5f;
    rb_renderer_draw_quad(renderer, (f32)mouse.x - big * 0.5f, (f32)mouse.y - big * 0.5f, big, big,
                          rb_color_srgb(1.0f, 0.85f, 0.2f, 0.5f));
}

/* ============================================================================
 * Frame-time graph
 * ========================================================================== */

#define SANDBOX_GRAPH_BAR_WIDTH 3.0f /* plus a 1-pixel gap */
#define SANDBOX_GRAPH_MAX_MS    40.0f
#define SANDBOX_GRAPH_PX_PER_MS 4.0f /* 40 ms fill 160 pixels */
#define SANDBOX_GRAPH_MARGIN    16.0f
#define SANDBOX_GRAPH_PADDING   8.0f

/*
 * The time since the previous frame started. It includes the wait for vsync,
 * so it is the interval the player sees: a stutter is one tall bar, which an
 * average over a second would hide.
 */
static void sandbox_record_frame_time(sandbox_state *game) {
    u64 now = rb_time_now();
    if (game->last_render != 0) {
        game->frame_ms[game->frame_next] =
            (f32)(rb_time_to_seconds(now - game->last_render) * 1000.0);
        game->frame_next = (game->frame_next + 1) % SANDBOX_GRAPH_FRAMES;
    }
    game->last_render = now;
}

/* A frame within the budget of a 60 Hz monitor is green; up to two frames' time, yellow; red. */
static rb_color sandbox_frame_color(f32 ms) {
    if (ms <= 1000.0f / 60.0f + 1.0f) {
        return rb_color_srgb(0.3f, 0.85f, 0.4f, 1.0f);
    }
    if (ms <= 2000.0f / 60.0f + 1.0f) {
        return rb_color_srgb(0.95f, 0.8f, 0.2f, 1.0f);
    }
    return rb_color_srgb(0.95f, 0.3f, 0.25f, 1.0f);
}

/*
 * Bottom left, on a translucent panel: one bar per frame, oldest on the left,
 * clipped at SANDBOX_GRAPH_MAX_MS, with lines at 60 and 30 fps. Drawn last, on
 * top of everything.
 */
static void sandbox_draw_frame_graph(const sandbox_state *game, rb_app *app) {
    rb_renderer   *renderer = rb_app_renderer(app);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    const f32      width = (f32)SANDBOX_GRAPH_FRAMES * (SANDBOX_GRAPH_BAR_WIDTH + 1.0f);
    const f32      height = SANDBOX_GRAPH_MAX_MS * SANDBOX_GRAPH_PX_PER_MS;
    const f32      left = SANDBOX_GRAPH_MARGIN + SANDBOX_GRAPH_PADDING;
    const f32      bottom = (f32)size.pixel_height - SANDBOX_GRAPH_MARGIN - SANDBOX_GRAPH_PADDING;

    rb_renderer_draw_quad(renderer, SANDBOX_GRAPH_MARGIN, bottom - height - SANDBOX_GRAPH_PADDING,
                          width + 2.0f * SANDBOX_GRAPH_PADDING,
                          height + 2.0f * SANDBOX_GRAPH_PADDING,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));

    for (u32 i = 0; i < SANDBOX_GRAPH_FRAMES; ++i) {
        /* frame_next is the oldest slot: start there and wrap around. */
        f32 ms = game->frame_ms[(game->frame_next + i) % SANDBOX_GRAPH_FRAMES];
        if (ms <= 0.0f) {
            continue;
        }
        f32 bar = (ms < SANDBOX_GRAPH_MAX_MS ? ms : SANDBOX_GRAPH_MAX_MS) * SANDBOX_GRAPH_PX_PER_MS;
        rb_renderer_draw_quad(renderer, left + (f32)i * (SANDBOX_GRAPH_BAR_WIDTH + 1.0f),
                              bottom - bar, SANDBOX_GRAPH_BAR_WIDTH, bar, sandbox_frame_color(ms));
    }

    /* Over the bars, so a bar crossing a line still shows where the budget ends. */
    rb_color line = rb_color_srgb(1.0f, 1.0f, 1.0f, 0.4f);
    rb_renderer_draw_quad(renderer, left, bottom - (1000.0f / 60.0f) * SANDBOX_GRAPH_PX_PER_MS,
                          width, 1.0f, line);
    rb_renderer_draw_quad(renderer, left, bottom - (1000.0f / 30.0f) * SANDBOX_GRAPH_PX_PER_MS,
                          width, 1.0f, line);
}

/* ============================================================================
 * Render
 * ========================================================================== */

/*
 * The clear color turns around the hue wheel, which shows that frames keep
 * reaching the screen. Muted (low saturation and value) so it stays easy on
 * the eyes and the quads stand out. Shows from the next frame.
 */
static void game_render(void *state, rb_app *app, f64 alpha) {
    (void)alpha;
    sandbox_state *game = state;
    ++game->frames;
    sandbox_record_frame_time(game);
    f32 hue = sandbox_phase(game->start, SANDBOX_HUE_PERIOD_NS);
    rb_renderer_set_clear_color(rb_app_renderer(app), hsv_to_srgb(hue, 0.5f, 0.45f));
    sandbox_draw_quads(app, hue, sandbox_phase(game->start, SANDBOX_SLIDE_PERIOD_NS));
    if (game->graph_visible) {
        sandbox_draw_frame_graph(game, app);
    }
}

const rb_app_callbacks sandbox_game = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
};
