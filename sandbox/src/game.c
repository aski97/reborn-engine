/*
 * Sandbox game: logs every event and the loop's rates, so window lifecycle,
 * input and timing can be checked by hand, and draws a few quads: a world
 * field through a 2D camera (with a ball bouncing inside a ring), a HUD in
 * window pixels on top, textures made in code, the builtin font's atlas and
 * text drawn with it, numbers drawn from a seed, the commands' panel (pause,
 * steps, its own sandbox.clear), the
 * input panel (keys, buttons and mouse as the engine sees them, injected or not),
 * the capture panel (P: the frame saved as PNG and shown as a thumbnail) and the
 * stats overlay (L: the engine's frame, renderer and memory numbers over a
 * frame-time graph).
 */
#include "game.h"

#include <stdio.h>
#include <string.h>

#include <reborn/camera2d.h>
#include <reborn/clock.h>
#include <reborn/font.h>
#include <reborn/image.h>
#include <reborn/log.h>
#include <reborn/math.h>
#include <reborn/random.h>
#include <reborn/renderer.h>
#include <reborn/text.h>

/* Steps per second, set in configure: the animations on game time count in steps. */
#define SANDBOX_UPDATE_RATE 60u

/* Frames shown by the frame-time graph: two seconds at 60 fps. */
#define SANDBOX_GRAPH_FRAMES 120u

/* The bounce demo's ring, in world units: right of the stats panel in a 1600x900 window. */
#define SANDBOX_RING_X      10.67f
#define SANDBOX_RING_Y      6.4f
#define SANDBOX_RING_RADIUS 1.2f
#define SANDBOX_BALL_SIDE   0.14f
#define SANDBOX_BALL_SPEED  1.5f /* units per second */

/* The ball's last positions, one every few steps: about five seconds of its path. */
#define SANDBOX_TRAIL_DOTS  96u
#define SANDBOX_TRAIL_EVERY 3u /* steps between two dots */

typedef struct sandbox_state {
    u64 start;          /* the clear color's animation runs from here */
    u64 last_stats_log; /* the engine's frame statistics are logged once per second */
    /*
     * Stats overlay, toggled with L (ADR 0025): the engine's numbers on a
     * panel, and a frame-time graph under it: the time between the starts of
     * the last frames, in milliseconds, in a ring (the oldest is overwritten;
     * 0: not measured yet).
     */
    bool graph_visible;
    u64  last_render; /* 0 before the first frame */
    f32  frame_ms[SANDBOX_GRAPH_FRAMES];
    u32  frame_next; /* where the next interval goes */
    /*
     * World field (ADR 0016): the wheel zooms, Z resets, a left click drops a
     * marker where it lands in world units.
     */
    rb_camera2d camera;
    rb_vec2     marker;
    bool        has_marker;
    /*
     * Bounces (ADR 0026), in the world field: a ball moved by each step and
     * reflected off a ring of dots. Its trail draws the path: every bounce
     * leaves at the angle it came in, so the chords stay tangent to one inner
     * circle, and the speed shown stays the same.
     */
    rb_vec2 ball;
    rb_vec2 ball_previous; /* before the last step: render draws in between with alpha */
    rb_vec2 ball_velocity; /* units per second */
    u32     bounces;
    rb_vec2 trail[SANDBOX_TRAIL_DOTS]; /* a ring: the oldest dot is overwritten */
    u32     trail_next;
    u32     trail_count;
    /*
     * Textures (ADR 0017): a checkerboard, nearest or linear (T swaps it: a
     * destroy and a create), and a soft disc whose alpha fades to its edge.
     */
    rb_texture        checker;
    rb_texture_filter checker_filter;
    rb_texture        glow;
    /* The builtin font (ADR 0018): its atlas, and text (ADR 0021) on screen and in the world. */
    rb_font *font;
    /*
     * Random numbers (ADR 0019): every frame redraws the same picture from
     * `seed`, which shows that a seed fixes the sequence. R takes a new seed
     * from the clock, B goes back to the previous one.
     */
    u64 seed;
    u64 previous_seed;
    /*
     * Commands (ADR 0022): sandbox.clear fixes the clear color (no arguments:
     * back to the turning hue); the panel shows its last call.
     */
    bool     clear_fixed;
    rb_color clear_color;
    char     last_command[64];
    /*
     * Frame capture (ADR 0024): P books a capture of the next frame; the step
     * after it saves the PNG and shows the pixels as a texture, bottom right.
     */
    bool       capture_wanted; /* P pressed: the next render books it */
    bool       capture_booked; /* booked: the next update collects it */
    rb_texture capture;
    u32        capture_count;
    char       capture_info[32];
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

/* The world point under a window pixel, for the main window's current size. */
static rb_vec2 sandbox_to_world(const sandbox_state *game, rb_app *app, i32 x, i32 y) {
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    rb_vec2        target = {(f32)size.pixel_width, (f32)size.pixel_height};
    return rb_camera2d_screen_to_world(&game->camera, target, (rb_vec2){(f32)x, (f32)y});
}

static void game_on_event(void *state, rb_app *app, const rb_event *ev) {
    sandbox_state *game = state;
    u32            id = ev->window.id;
    char           mods[32];
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
    case RB_EVENT_MOUSE_BUTTON_RELEASED: {
        rb_vec2 world = sandbox_to_world(game, app, ev->button.x, ev->button.y);
        RB_LOG_DEBUG("Window 0x%08x: mouse %s %s at %d,%d (world %.2f,%.2f)%s", id,
                     rb_mouse_button_name(ev->button.button),
                     ev->type == RB_EVENT_MOUSE_BUTTON_PRESSED ? "pressed" : "released",
                     ev->button.x, ev->button.y, (double)world.x, (double)world.y,
                     format_mods(ev->button.mods, mods, sizeof(mods)));
        if (ev->type == RB_EVENT_MOUSE_BUTTON_PRESSED &&
            ev->button.button == RB_MOUSE_BUTTON_LEFT) {
            game->marker = world;
            game->has_marker = true;
        }
        break;
    }
    case RB_EVENT_MOUSE_MOVED:
        /* TRACE: one per step while the mouse moves. */
        RB_LOG_TRACE("Window 0x%08x: mouse moved to %d,%d", id, ev->move.x, ev->move.y);
        break;
    case RB_EVENT_MOUSE_WHEEL: {
        /* A tenth per notch, kept within a range where the field stays recognizable. */
        f32 zoom = game->camera.zoom * (1.0f + 0.1f * ev->wheel.y);
        game->camera.zoom = zoom < 0.25f ? 0.25f : zoom > 8.0f ? 8.0f : zoom;
        RB_LOG_DEBUG("Window 0x%08x: wheel %.2f,%.2f (zoom %.2f)", id, (double)ev->wheel.x,
                     (double)ev->wheel.y, (double)game->camera.zoom);
        break;
    }
    default:
        break;
    }
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

/*
 * The window icon (ADR 0020): four squares in the hues of the row on screen,
 * with transparent gaps. Static: the pixels must outlive configure, until the
 * engine creates the window.
 */
static u8             icon_16[16 * 16 * 4];
static u8             icon_32[32 * 32 * 4];
static u8             icon_64[64 * 64 * 4];
static const rb_image icons[] = {{16, 16, icon_16}, {32, 32, icon_32}, {64, 64, icon_64}};

static void sandbox_draw_icon(u32 side, u8 *pixels) {
    static const u8 colors[4][3] = {{235, 80, 80}, {240, 210, 70}, {80, 200, 110}, {80, 140, 240}};
    const u32       gap = side / 16; /* 1 pixel at 16 */
    const u32       half = side / 2;
    for (u32 y = 0; y < side; ++y) {
        for (u32 x = 0; x < side; ++x) {
            u8 *p = &pixels[(y * side + x) * 4];
            /* Quadrant 0 top left, 1 top right, 2 bottom left, 3 bottom right. */
            const u8 *color = colors[(y >= half ? 2u : 0u) + (x >= half ? 1u : 0u)];
            u32       qx = x % half;
            u32       qy = y % half;
            bool      in_gap = qx < gap || qx >= half - gap || qy < gap || qy >= half - gap;
            p[0] = color[0];
            p[1] = color[1];
            p[2] = color[2];
            p[3] = in_gap ? 0 : 255;
        }
    }
}

static bool game_configure(rb_app_config *config) {
    /*
     * Everything, to check events by hand. The file lands in the working
     * directory: the repository root when run as in CLAUDE.md.
     */
    config->log.console_level = RB_LOG_LEVEL_TRACE;
    config->log.file_path = "reborn.log.jsonl";
    config->log.file_level = RB_LOG_LEVEL_TRACE;
    config->window.title = "Reborn Sandbox — città";
    sandbox_draw_icon(16, icon_16);
    sandbox_draw_icon(32, icon_32);
    sandbox_draw_icon(64, icon_64);
    config->window.icons = icons;
    config->window.icon_count = RB_ARRAY_COUNT(icons);
    config->update_rate = SANDBOX_UPDATE_RATE;
    RB_APP_STATE(config, sandbox_state);
    return true;
}

/* Textures made in code (ADR 0017): created in init, destroyed in shutdown. */

#define SANDBOX_CHECKER_SIDE 8u
#define SANDBOX_GLOW_SIDE    32u

/* 8x8 squares in two tones, a red one in the top-left corner to show which way is up. */
static rb_texture sandbox_create_checker(rb_renderer *renderer, rb_texture_filter filter) {
    u8 pixels[SANDBOX_CHECKER_SIDE * SANDBOX_CHECKER_SIDE * 4];
    for (u32 y = 0; y < SANDBOX_CHECKER_SIDE; ++y) {
        for (u32 x = 0; x < SANDBOX_CHECKER_SIDE; ++x) {
            u8  *p = &pixels[(y * SANDBOX_CHECKER_SIDE + x) * 4];
            u8   tone = (x + y) % 2 == 0 ? 230 : 60;
            bool corner = x == 0 && y == 0;
            p[0] = corner ? 230 : tone;
            p[1] = corner ? 40 : tone;
            p[2] = corner ? 40 : tone;
            p[3] = 255;
        }
    }
    return rb_renderer_create_texture(renderer, &(rb_texture_desc){.width = SANDBOX_CHECKER_SIDE,
                                                                   .height = SANDBOX_CHECKER_SIDE,
                                                                   .pixels = pixels,
                                                                   .filter = filter});
}

/*
 * White, opaque at the center and transparent at the edge: alpha (1 - d^2)^2,
 * d the distance from the center as a fraction of the radius. No square root.
 */
static rb_texture sandbox_create_glow(rb_renderer *renderer) {
    u8        pixels[SANDBOX_GLOW_SIDE * SANDBOX_GLOW_SIDE * 4];
    const f32 radius = (f32)SANDBOX_GLOW_SIDE * 0.5f;
    for (u32 y = 0; y < SANDBOX_GLOW_SIDE; ++y) {
        for (u32 x = 0; x < SANDBOX_GLOW_SIDE; ++x) {
            /* From the pixel's center: +0.5. */
            f32 dx = ((f32)x + 0.5f - radius) / radius;
            f32 dy = ((f32)y + 0.5f - radius) / radius;
            f32 d2 = dx * dx + dy * dy;
            f32 a = d2 < 1.0f ? (1.0f - d2) * (1.0f - d2) : 0.0f;
            u8 *p = &pixels[(y * SANDBOX_GLOW_SIDE + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (u8)(a * 255.0f + 0.5f);
        }
    }
    return rb_renderer_create_texture(renderer,
                                      &(rb_texture_desc){.width = SANDBOX_GLOW_SIDE,
                                                         .height = SANDBOX_GLOW_SIDE,
                                                         .pixels = pixels,
                                                         .filter = RB_TEXTURE_FILTER_LINEAR});
}

/* ============================================================================
 * Commands
 * ========================================================================== */

static void cmd_clear(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)app;
    sandbox_state *game = state;
    if (args->count == 0) {
        game->clear_fixed = false;
        snprintf(game->last_command, sizeof(game->last_command), "clear (turning)");
        rb_command_reply_bool(reply, "fixed", false);
        return;
    }
    if (args->count != 3) {
        rb_command_reply_error(reply, "give r g b from 0 to 1, or nothing for the turning hue");
        return;
    }
    f32 rgb[3];
    for (u32 i = 0; i < 3; ++i) {
        if (!rb_command_arg_f32(args, i, &rgb[i], reply)) {
            return;
        }
        if (rgb[i] < 0.0f || rgb[i] > 1.0f) {
            rb_command_reply_error(reply, "argument %u: %g is outside 0..1", i + 1, (f64)rgb[i]);
            return;
        }
    }
    game->clear_fixed = true;
    game->clear_color = rb_color_srgb(rgb[0], rgb[1], rgb[2], 1.0f);
    snprintf(game->last_command, sizeof(game->last_command), "clear %.2f %.2f %.2f", (f64)rgb[0],
             (f64)rgb[1], (f64)rgb[2]);
    rb_command_reply_bool(reply, "fixed", true);
}

static const rb_command_desc sandbox_commands[] = {
    {"sandbox.clear", "[<r> <g> <b>]", "fix the clear color (sRGB, 0..1); nothing: turning hue", 0,
     3, cmd_clear},
};

static bool game_init(void *state, rb_app *app) {
    sandbox_state *game = state;
    game->start = rb_time_now();
    game->last_stats_log = game->start;
    game->graph_visible = true;
    game->camera = rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
    /*
     * Above the ring's center, launched 20 degrees below the horizontal (a
     * positive angle turns clockwise: y points down). Off the center line, so
     * the bounces draw a star instead of going back and forth on a diameter.
     */
    game->ball = (rb_vec2){SANDBOX_RING_X, SANDBOX_RING_Y - 0.6f};
    game->ball_previous = game->ball;
    game->ball_velocity = rb_vec2_scale(rb_vec2_from_angle(RB_PI / 9.0f), SANDBOX_BALL_SPEED);
    /* A failure is logged; a zero handle draws a plain quad, so the sandbox goes on. */
    game->checker_filter = RB_TEXTURE_FILTER_NEAREST;
    game->checker = sandbox_create_checker(rb_app_renderer(app), game->checker_filter);
    game->glow = sandbox_create_glow(rb_app_renderer(app));
    /* NULL on failure (logged): the font's drawing is skipped. */
    game->font = rb_font_create_builtin(rb_app_allocator(app), rb_app_renderer(app));
    /* Different on every run, and logged: that number repeats the run. */
    game->seed = rb_time_now();
    game->previous_seed = game->seed;
    RB_LOG_INFO("Random seed: %llu", (unsigned long long)game->seed);
    snprintf(game->last_command, sizeof(game->last_command), "none yet");
    for (u32 i = 0; i < RB_ARRAY_COUNT(sandbox_commands); ++i) {
        /* A failure is logged; the sandbox goes on without that command. */
        rb_app_register_command(app, &sandbox_commands[i]);
    }
    return true;
}

/* The renderer's shutdown would free them anyway: done here as a game unloading its assets. */
static void game_shutdown(void *state, rb_app *app) {
    sandbox_state *game = state;
    rb_renderer_destroy_texture(rb_app_renderer(app), game->checker);
    rb_renderer_destroy_texture(rb_app_renderer(app), game->glow);
    rb_renderer_destroy_texture(rb_app_renderer(app), game->capture);
    rb_font_destroy(game->font);
}

/* In the working directory (the repository root when run as in CLAUDE.md), ignored by git. */
#define SANDBOX_CAPTURE_PATH "sandbox-capture.png"

/*
 * The frame booked by the last render has ended: its pixels go to a PNG file
 * and into a texture, which the capture panel shows. In update, not render: the
 * old texture cannot be destroyed while a frame is being recorded.
 */
static void sandbox_collect_capture(sandbox_state *game, rb_app *app) {
    rb_renderer    *renderer = rb_app_renderer(app);
    const rb_image *image = rb_renderer_captured_image(renderer);
    if (image == NULL) {
        snprintf(game->capture_info, sizeof(game->capture_info), "failed");
        return;
    }
    usize bytes = 0;
    bool  saved = rb_image_write_png(image, SANDBOX_CAPTURE_PATH, rb_app_allocator(app), &bytes);
    rb_renderer_destroy_texture(renderer, game->capture);
    /* Linear: the panel shows it at a fraction of its size. */
    game->capture = rb_renderer_create_texture(
        renderer, &(rb_texture_desc){.width = image->width,
                                     .height = image->height,
                                     .pixels = image->pixels,
                                     .filter = RB_TEXTURE_FILTER_LINEAR});
    ++game->capture_count;
    if (saved) {
        /* The panel holds 11 glyphs per line: the size in pixels goes to the log only. */
        snprintf(game->capture_info, sizeof(game->capture_info), "#%u  %zu KB", game->capture_count,
                 (bytes + 512) / 1024);
        RB_LOG_INFO("Frame captured to %s: %ux%u, %zu bytes (P)", SANDBOX_CAPTURE_PATH,
                    image->width, image->height, bytes);
    } else {
        snprintf(game->capture_info, sizeof(game->capture_info), "#%u unsaved",
                 game->capture_count);
    }
}

/*
 * One step of the ball (ADR 0026): it moves, then bounces if it has reached
 * the ring while going outward. The ring's wall at the ball faces along the
 * line from the center, so that direction, normalized, is the normal.
 */
static void sandbox_update_ball(sandbox_state *game, rb_app *app, f32 dt) {
    rb_vec2 center = {SANDBOX_RING_X, SANDBOX_RING_Y};
    game->ball_previous = game->ball;
    game->ball = rb_vec2_add(game->ball, rb_vec2_scale(game->ball_velocity, dt));

    rb_vec2 offset = rb_vec2_sub(game->ball, center);
    rb_vec2 normal = rb_vec2_normalize(offset);
    /*
     * Only while going outward: a step can leave the ball a little past the
     * wall, and bouncing there again would send it back out, stuck on the ring.
     */
    if (rb_vec2_length(offset) >= SANDBOX_RING_RADIUS - SANDBOX_BALL_SIDE * 0.5f &&
        rb_vec2_dot(game->ball_velocity, normal) > 0.0f) {
        game->ball_velocity = rb_vec2_reflect(game->ball_velocity, normal);
        ++game->bounces;
    }

    if (rb_app_step_index(app) % SANDBOX_TRAIL_EVERY == 0) {
        game->trail[game->trail_next] = game->ball;
        game->trail_next = (game->trail_next + 1) % SANDBOX_TRAIL_DOTS;
        if (game->trail_count < SANDBOX_TRAIL_DOTS) {
            ++game->trail_count;
        }
    }
}

static void game_update(void *state, rb_app *app, f64 dt) {
    sandbox_state     *game = state;
    rb_platform_state *platform = rb_app_platform(app);

    sandbox_update_ball(game, app, (f32)dt);

    if (game->capture_booked) {
        game->capture_booked = false;
        sandbox_collect_capture(game, app);
    }
    if (rb_key_was_pressed(platform, RB_KEY_P)) {
        game->capture_wanted = true;
    }

    /* Edges cover the time since the previous step: a tap shows both, exactly once. */
    if (rb_key_was_pressed(platform, RB_KEY_SPACE)) {
        RB_LOG_DEBUG("Query: Space was pressed this step (down: %d)",
                     rb_key_is_down(platform, RB_KEY_SPACE));
    }
    if (rb_key_was_released(platform, RB_KEY_SPACE)) {
        RB_LOG_DEBUG("Query: Space was released this step (down: %d)",
                     rb_key_is_down(platform, RB_KEY_SPACE));
    }
    if (rb_key_was_pressed(platform, RB_KEY_Z)) {
        game->camera.zoom = 1.0f;
        RB_LOG_INFO("Zoom reset (Z)");
    }
    if (rb_key_was_pressed(platform, RB_KEY_L)) {
        game->graph_visible = !game->graph_visible;
        RB_LOG_INFO("Frame-time graph %s (L)", game->graph_visible ? "shown" : "hidden");
    }
    /* In update, not render: a texture cannot be destroyed while a frame is being recorded. */
    if (rb_key_was_pressed(platform, RB_KEY_T)) {
        rb_renderer *renderer = rb_app_renderer(app);
        game->checker_filter = game->checker_filter == RB_TEXTURE_FILTER_NEAREST
                                   ? RB_TEXTURE_FILTER_LINEAR
                                   : RB_TEXTURE_FILTER_NEAREST;
        rb_renderer_destroy_texture(renderer, game->checker);
        game->checker = sandbox_create_checker(renderer, game->checker_filter);
        RB_LOG_INFO("Checkerboard filter: %s (T)",
                    game->checker_filter == RB_TEXTURE_FILTER_NEAREST ? "nearest" : "linear");
    }
    if (rb_key_was_pressed(platform, RB_KEY_R)) {
        game->previous_seed = game->seed;
        game->seed = rb_time_now();
        RB_LOG_INFO("Random seed: %llu (R)", (unsigned long long)game->seed);
    }
    if (rb_key_was_pressed(platform, RB_KEY_B)) {
        u64 seed = game->seed;
        game->seed = game->previous_seed;
        game->previous_seed = seed;
        RB_LOG_INFO("Random seed: %llu, back to the previous one (B)",
                    (unsigned long long)game->seed);
    }

    /* The engine's last window, which closes about once a second too (ADR 0025). */
    u64 now = rb_time_now();
    if (now - game->last_stats_log >= RB_NS_PER_S) {
        game->last_stats_log = now;
        rb_app_frame_stats stats = rb_app_get_frame_stats(app);
        RB_LOG_DEBUG("Last %.2f s: %u frames (%.1f ms avg, %.1f 1%%, %.1f max; work %.2f ms), "
                     "%u steps",
                     stats.seconds, stats.frames, stats.frame_avg_ms, stats.frame_p99_ms,
                     stats.frame_max_ms, stats.work_avg_ms, stats.steps);
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

/* One trip across the window and back for the sliding square, in seconds of game time. */
#define SANDBOX_SLIDE_PERIOD_S 4u
/* One turn of the row of squares around the hue wheel, in seconds of game time. */
#define SANDBOX_ROW_PERIOD_S 12u

#define SANDBOX_QUAD_SIDE 64.0f

/* Where `period` has got to since `start` on the wall clock, in [0, 1). */
static f32 sandbox_phase(u64 start, u64 period) {
    /* Integer modulo first: the elapsed nanoseconds would lose precision as a float. */
    u64 phase = (rb_time_now() - start) % period;
    return (f32)((f64)phase / (f64)period);
}

/*
 * Where a period of `seconds` has got to in game time, in [0, 1): the steps
 * run so far, plus `alpha`, render's fraction of the next one (ADR 0011, C2),
 * so the motion stays smooth between steps on any monitor. Paused, neither
 * the steps nor alpha change (ADR 0022): what moves on game time stands still.
 */
static f32 sandbox_game_phase(rb_app *app, f64 alpha, u32 seconds) {
    u64 period = (u64)seconds * SANDBOX_UPDATE_RATE;
    u64 step = rb_app_step_index(app) % period;
    return (f32)(((f64)step + alpha) / (f64)period);
}

/*
 * Quads (ADR 0015): a row of opaque squares around the hue wheel; one sliding
 * across the window by fractions of a pixel, so its motion stays smooth; the
 * checkerboard enlarged 16 times, sharp or blurred by its filter; a yellow
 * glow under the mouse, blending with whatever it covers (ADR 0017).
 */
static void sandbox_draw_quads(const sandbox_state *game, rb_app *app, f32 row_hue, f32 slide) {
    rb_renderer       *renderer = rb_app_renderer(app);
    rb_platform_state *platform = rb_app_platform(app);
    rb_window_size     size = rb_window_get_size(platform, rb_app_window(app));
    const f32          side = SANDBOX_QUAD_SIDE;

    for (u32 i = 0; i < 6; ++i) {
        f32 h = row_hue + (f32)i / 6.0f;
        h = h >= 1.0f ? h - 1.0f : h;
        rb_renderer_draw_quad(renderer, 32.0f + (f32)i * (side + 16.0f), 32.0f, side, side,
                              hsv_to_srgb(h, 0.7f, 0.9f));
    }

    /* There and back: 0 to 1 in the first half of the period, 1 to 0 in the second. */
    f32 there_and_back = slide < 0.5f ? slide * 2.0f : 2.0f - slide * 2.0f;
    f32 travel = (f32)size.pixel_width - side;
    rb_renderer_draw_quad(renderer, there_and_back * travel, ((f32)size.pixel_height - side) * 0.5f,
                          side, side, rb_color_srgb(0.95f, 0.95f, 0.95f, 1.0f));

    /* Top right: 8 texels across 128 pixels. */
    const f32 checker = (f32)SANDBOX_CHECKER_SIDE * 16.0f;
    rb_renderer_draw_texture(renderer, (f32)size.pixel_width - checker - 32.0f, 32.0f, checker,
                             checker, game->checker, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));

    /* Drawn last: on top of the others, which show through where the glow fades. */
    rb_mouse_position mouse = rb_mouse_get_position(platform);
    f32               big = side * 2.0f;
    rb_renderer_draw_texture(renderer, (f32)mouse.x - big * 0.5f, (f32)mouse.y - big * 0.5f, big,
                             big, game->glow, rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f));
}

/* ============================================================================
 * Font and text
 * ========================================================================== */

#define SANDBOX_TEXT_SIZE   24.0f /* the 8x8 font 3 times larger: its pixels stay square */
#define SANDBOX_ATLAS_SCALE 2.0f

/* A style of the builtin font; the fields left out are left and top aligned. */
static rb_text_style sandbox_style(const rb_font *font, f32 size, rb_color color) {
    return (rb_text_style){.font = font, .size = size, .color = color};
}

/*
 * Under the row of squares: two lines of text (ADR 0021), the second with
 * characters the font lacks (one box each: UTF-8 is decoded), then the whole
 * atlas on a dark panel, glyphs white on transparent, the padding between
 * them visible (ADR 0018).
 */
static void sandbox_draw_font(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer  *renderer = rb_app_renderer(app);
    const f32     left = 32.0f;
    rb_text_style yellow =
        sandbox_style(game->font, SANDBOX_TEXT_SIZE, rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f));
    rb_text_style white =
        sandbox_style(game->font, SANDBOX_TEXT_SIZE, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    rb_text_draw(renderer, left, 128.0f, &yellow, "SCORE 120  LIVES 3  Blink!");
    rb_text_draw(renderer, left, 158.0f, &white, "UTF-8: città, naïve (a box each)");

    /* The atlas's size from its last glyph: 14 per row, '~' ends the last one. */
    rb_glyph last = rb_font_get_glyph(game->font, '~');
    f32      width = 128.0f * SANDBOX_ATLAS_SCALE;
    f32      height = (last.source.y + last.source.height) * SANDBOX_ATLAS_SCALE;
    f32      top = 206.0f;
    rb_renderer_draw_quad(renderer, left - 8.0f, top - 8.0f, width + 16.0f, height + 16.0f,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));
    rb_renderer_draw_texture(renderer, left, top, width, height, rb_font_get_atlas(game->font),
                             rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
}

#define SANDBOX_DEMO_LEFT  320.0f /* right of the atlas */
#define SANDBOX_DEMO_TOP   206.0f
#define SANDBOX_DEMO_RIGHT 900.0f /* left of the random numbers' panel */

/* A thin line and a dot: where an anchor is, so the alignment around it shows. */
static void sandbox_draw_anchor(rb_renderer *renderer, f32 x, f32 y) {
    rb_renderer_draw_quad(renderer, x - 2.0f, y - 2.0f, 4.0f, 4.0f,
                          rb_color_srgb(1.0f, 0.35f, 0.3f, 1.0f));
}

/*
 * Right of the atlas, on a dark panel (ADR 0021): the three horizontal
 * alignments on one vertical guide; the same size drawn at a whole multiple
 * of the font (sharp) and at a fraction (uneven strokes); the four vertical
 * alignments on one horizontal guide. Red dots are the anchors.
 */
static void sandbox_draw_text_demo(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer *renderer = rb_app_renderer(app);
    const f32    left = SANDBOX_DEMO_LEFT;
    const f32    top = SANDBOX_DEMO_TOP;
    rb_color     guide = rb_color_srgb(1.0f, 1.0f, 1.0f, 0.3f);
    rb_renderer_draw_quad(renderer, left - 8.0f, top - 8.0f, SANDBOX_DEMO_RIGHT - left + 16.0f,
                          140.0f, rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));

    /* Horizontal: each line anchored on the same x. */
    rb_text_style style = sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    const f32     guide_x = left + 128.0f;
    rb_renderer_draw_quad(renderer, guide_x, top, 1.0f, 64.0f, guide);
    static const struct {
        rb_text_align_x align;
        const char     *name;
    } horizontal[] = {{RB_TEXT_LEFT, "LEFT"}, {RB_TEXT_CENTER, "CENTER"}, {RB_TEXT_RIGHT, "RIGHT"}};
    for (u32 i = 0; i < RB_ARRAY_COUNT(horizontal); ++i) {
        f32 y = top + (f32)i * 22.0f;
        style.align_x = horizontal[i].align;
        rb_text_draw(renderer, guide_x, y, &style, horizontal[i].name);
        sandbox_draw_anchor(renderer, guide_x, y);
    }

    /* Sharpness: x2 (16) covers 2 screen pixels per font pixel; x2.5 (20) alternates 2 and 3. */
    style.align_x = RB_TEXT_LEFT;
    rb_text_draw(renderer, left + 300.0f, top, &style, "SHARP x2");
    rb_text_style uneven = style;
    uneven.size = 20.0f;
    rb_text_draw(renderer, left + 300.0f, top + 26.0f, &uneven, "UNEVEN x2.5");

    /* Vertical: each word anchored on the same y, at its own x. */
    const f32 guide_y = top + 100.0f;
    rb_renderer_draw_quad(renderer, left, guide_y, SANDBOX_DEMO_RIGHT - left, 1.0f, guide);
    static const struct {
        rb_text_align_y align;
        const char     *name;
        f32             x;
    } vertical[] = {{RB_TEXT_TOP, "TOP", 0.0f},
                    {RB_TEXT_MIDDLE, "MIDDLE", 80.0f},
                    {RB_TEXT_BASELINE, "BASELINE", 210.0f},
                    {RB_TEXT_BOTTOM, "BOTTOM", 370.0f}};
    for (u32 i = 0; i < RB_ARRAY_COUNT(vertical); ++i) {
        style.align_y = vertical[i].align;
        rb_text_draw(renderer, left + vertical[i].x, guide_y, &style, vertical[i].name);
        sandbox_draw_anchor(renderer, left + vertical[i].x, guide_y);
    }
}

/* ============================================================================
 * Random numbers
 * ========================================================================== */

#define SANDBOX_RANDOM_SIDE    320.0f /* the scatter's square: 20 glyphs twice as large */
#define SANDBOX_RANDOM_TOP     192.0f /* under the checkerboard */
#define SANDBOX_RANDOM_POINTS  1024u
#define SANDBOX_RANDOM_BINS    16u
#define SANDBOX_RANDOM_DRAWS   16384u /* 1024 per bin on average */
#define SANDBOX_RANDOM_BAR_MAX 64.0f  /* pixels for 1.5 times the average */

/*
 * Right, under the checkerboard, on a dark panel: the seed (up to 20 digits),
 * then points at random spots in random hues, then a histogram of integers in
 * [0, 16), bars about level since every value is equally likely. Drawn again
 * each frame from the seed alone: if the sequence changed between frames, the
 * points would flicker.
 */
static void sandbox_draw_random(const sandbox_state *game, rb_app *app) {
    rb_renderer   *renderer = rb_app_renderer(app);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    const f32      left = (f32)size.pixel_width - SANDBOX_RANDOM_SIDE - 32.0f;
    f32            line = 0.0f;
    if (game->font != NULL) {
        /* From one line to the next: what a second line adds to the block's height. */
        rb_text_style style = sandbox_style(game->font, 16.0f, rb_color_srgb(1, 1, 1, 1));
        line = rb_text_measure(&style, "\n").y - rb_text_measure(&style, "").y;
    }
    const f32 top = SANDBOX_RANDOM_TOP + 2.0f * line + 8.0f;
    const f32 bars_top = top + SANDBOX_RANDOM_SIDE + 8.0f;
    const f32 bottom = bars_top + SANDBOX_RANDOM_BAR_MAX;
    rb_rng    rng = rb_rng_create(game->seed);

    rb_renderer_draw_quad(renderer, left - 8.0f, SANDBOX_RANDOM_TOP - 8.0f,
                          SANDBOX_RANDOM_SIDE + 16.0f, bottom - SANDBOX_RANDOM_TOP + 16.0f,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));
    if (game->font != NULL) {
        char seed[24];
        snprintf(seed, sizeof(seed), "%llu", (unsigned long long)game->seed);
        rb_text_style label =
            sandbox_style(game->font, 16.0f, rb_color_srgb(0.7f, 0.7f, 0.7f, 1.0f));
        rb_text_style value =
            sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
        rb_text_draw(renderer, left, SANDBOX_RANDOM_TOP, &label, "SEED (R new, B back)");
        rb_text_draw(renderer, left, SANDBOX_RANDOM_TOP + line, &value, seed);
    }

    for (u32 i = 0; i < SANDBOX_RANDOM_POINTS; ++i) {
        f32 x = rb_rng_range_f32(&rng, 0.0f, SANDBOX_RANDOM_SIDE - 3.0f);
        f32 y = rb_rng_range_f32(&rng, 0.0f, SANDBOX_RANDOM_SIDE - 3.0f);
        rb_renderer_draw_quad(renderer, left + x, top + y, 3.0f, 3.0f,
                              hsv_to_srgb(rb_rng_f32(&rng), 0.6f, 1.0f));
    }

    u32 counts[SANDBOX_RANDOM_BINS] = {0};
    for (u32 i = 0; i < SANDBOX_RANDOM_DRAWS; ++i) {
        ++counts[rb_rng_range_u32(&rng, 0, SANDBOX_RANDOM_BINS)];
    }
    const f32 average = (f32)SANDBOX_RANDOM_DRAWS / (f32)SANDBOX_RANDOM_BINS;
    const f32 bar_width = SANDBOX_RANDOM_SIDE / (f32)SANDBOX_RANDOM_BINS;
    for (u32 i = 0; i < SANDBOX_RANDOM_BINS; ++i) {
        f32 bar = (f32)counts[i] / (average * 1.5f) * SANDBOX_RANDOM_BAR_MAX;
        bar = bar < SANDBOX_RANDOM_BAR_MAX ? bar : SANDBOX_RANDOM_BAR_MAX;
        rb_renderer_draw_quad(renderer, left + (f32)i * bar_width, bottom - bar, bar_width - 2.0f,
                              bar, rb_color_srgb(0.4f, 0.75f, 1.0f, 1.0f));
    }
    /* The average's level: the bars should hover around it. */
    rb_renderer_draw_quad(renderer, left, bottom - SANDBOX_RANDOM_BAR_MAX / 1.5f,
                          SANDBOX_RANDOM_SIDE, 1.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 0.5f));
}

/* ============================================================================
 * World field
 * ========================================================================== */

#define SANDBOX_FIELD_WIDTH  16.0f
#define SANDBOX_FIELD_HEIGHT 9.0f
#define SANDBOX_GRID_LINE    0.04f /* world units */

#define SANDBOX_RING_DOTS 32u

/*
 * The bounce demo (ADR 0026), in world units. The ring's dots come from one
 * spoke turned a little further for each; the square orbiting the ring is
 * placed by its angle, `turn` of a whole turn. The ball is drawn between its
 * last two steps, `alpha` of the way (ADR 0011), so it moves smoothly at any
 * frame rate. Text under the ring: the speed (the velocity's length, kept by
 * every reflection) and the bounces so far.
 */
static void sandbox_draw_bounces(const sandbox_state *game, rb_renderer *renderer, f32 turn,
                                 f32 alpha) {
    rb_vec2 center = {SANDBOX_RING_X, SANDBOX_RING_Y};

    const f32 dot = 0.06f;
    rb_vec2   spoke = {SANDBOX_RING_RADIUS, 0.0f};
    for (u32 i = 0; i < SANDBOX_RING_DOTS; ++i) {
        rb_vec2 at = rb_vec2_add(center, spoke);
        rb_renderer_draw_quad(renderer, at.x - dot * 0.5f, at.y - dot * 0.5f, dot, dot,
                              rb_color_srgb(1.0f, 1.0f, 1.0f, 0.6f));
        spoke = rb_vec2_rotate(spoke, 2.0f * RB_PI / (f32)SANDBOX_RING_DOTS);
    }

    /* Oldest first, fading in toward the ball. */
    const f32 trail = 0.05f;
    u32 oldest = (game->trail_next + SANDBOX_TRAIL_DOTS - game->trail_count) % SANDBOX_TRAIL_DOTS;
    for (u32 i = 0; i < game->trail_count; ++i) {
        rb_vec2 at = game->trail[(oldest + i) % SANDBOX_TRAIL_DOTS];
        f32     fade = (f32)(i + 1) / (f32)game->trail_count;
        rb_renderer_draw_quad(renderer, at.x - trail * 0.5f, at.y - trail * 0.5f, trail, trail,
                              rb_color_srgb(1.0f, 0.85f, 0.3f, 0.6f * fade));
    }

    rb_vec2 ball = rb_vec2_add(game->ball_previous,
                               rb_vec2_scale(rb_vec2_sub(game->ball, game->ball_previous), alpha));
    rb_renderer_draw_quad(renderer, ball.x - SANDBOX_BALL_SIDE * 0.5f,
                          ball.y - SANDBOX_BALL_SIDE * 0.5f, SANDBOX_BALL_SIDE, SANDBOX_BALL_SIDE,
                          rb_color_srgb(1.0f, 0.85f, 0.3f, 1.0f));

    const f32 square = 0.2f;
    rb_vec2   orbit = rb_vec2_add(
        center, rb_vec2_scale(rb_vec2_from_angle(turn * 2.0f * RB_PI), SANDBOX_RING_RADIUS + 0.3f));
    rb_renderer_draw_quad(renderer, orbit.x - square * 0.5f, orbit.y - square * 0.5f, square,
                          square, rb_color_srgb(0.3f, 0.7f, 1.0f, 1.0f));

    if (game->font != NULL) {
        /* 24 pixels in a 1600x900 window: 3 per font pixel, sharp. */
        rb_text_style style = {.font = game->font,
                               .size = 0.24f,
                               .color = rb_color_srgb(0.9f, 0.9f, 0.9f, 1.0f),
                               .align_x = RB_TEXT_CENTER,
                               .align_y = RB_TEXT_TOP};
        char          text[48];
        snprintf(text, sizeof(text), "SPEED %.2f\nBOUNCES %u",
                 (double)rb_vec2_length(game->ball_velocity), game->bounces);
        rb_text_draw(renderer, center.x, center.y + SANDBOX_RING_RADIUS + 0.5f, &style, text);
    }
}

/*
 * Through the camera (ADR 0016): a dark field with a grid at every unit, the
 * bounce demo and the last click's marker. Nothing reaches the bars, which
 * keep the clear color: resizing the window shows them.
 */
static void sandbox_draw_world(const sandbox_state *game, rb_app *app, f32 turn, f32 alpha) {
    rb_renderer *renderer = rb_app_renderer(app);
    rb_renderer_set_camera(renderer, &game->camera);

    rb_renderer_draw_quad(renderer, 0.0f, 0.0f, SANDBOX_FIELD_WIDTH, SANDBOX_FIELD_HEIGHT,
                          rb_color_srgb(0.08f, 0.09f, 0.12f, 1.0f));
    rb_color grid = rb_color_srgb(1.0f, 1.0f, 1.0f, 0.12f);
    for (u32 x = 0; x <= (u32)SANDBOX_FIELD_WIDTH; ++x) {
        rb_renderer_draw_quad(renderer, (f32)x - SANDBOX_GRID_LINE * 0.5f, 0.0f, SANDBOX_GRID_LINE,
                              SANDBOX_FIELD_HEIGHT, grid);
    }
    for (u32 y = 0; y <= (u32)SANDBOX_FIELD_HEIGHT; ++y) {
        rb_renderer_draw_quad(renderer, 0.0f, (f32)y - SANDBOX_GRID_LINE * 0.5f,
                              SANDBOX_FIELD_WIDTH, SANDBOX_GRID_LINE, grid);
    }

    sandbox_draw_bounces(game, renderer, turn, alpha);

    if (game->has_marker) {
        rb_renderer_draw_quad(renderer, game->marker.x - 0.25f, game->marker.y - 0.25f, 0.5f, 0.5f,
                              rb_color_srgb(1.0f, 0.35f, 0.3f, 1.0f));
    }

    /*
     * Text in world units (ADR 0021): lines 0.4 units high, centered on a
     * point of the field, on a panel as large as the measured block. It grows
     * and shrinks with the zoom, and its pixels turn uneven when a font pixel
     * no longer covers a whole number of screen pixels.
     */
    if (game->font != NULL) {
        rb_text_style style = {.font = game->font,
                               .size = 0.4f,
                               .color = rb_color_srgb(0.9f, 0.9f, 0.9f, 1.0f),
                               .align_x = RB_TEXT_CENTER,
                               .align_y = RB_TEXT_MIDDLE};
        const char   *text = "WORLD UNITS\nthe wheel zooms";
        rb_vec2       size = rb_text_measure(&style, text);
        /* Between the text demo and the stats overlay, in a 1600x900 window. */
        rb_vec2 anchor = {8.0f, 4.0f};
        rb_renderer_draw_quad(renderer, anchor.x - size.x * 0.5f - 0.2f,
                              anchor.y - size.y * 0.5f - 0.2f, size.x + 0.4f, size.y + 0.4f,
                              rb_color_srgb(0.0f, 0.0f, 0.0f, 0.5f));
        rb_text_draw(renderer, anchor.x, anchor.y, &style, text);
    }

    /* Back to pixels for the HUD drawn after. */
    rb_renderer_set_camera(renderer, NULL);
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
 * Commands panel
 * ========================================================================== */

#define SANDBOX_COMMANDS_LEFT   528.0f /* right of the frame-time graph */
#define SANDBOX_COMMANDS_WIDTH  368.0f /* up to the random numbers' panel */
#define SANDBOX_COMMANDS_HEIGHT 112.0f
#define SANDBOX_COMMANDS_TICKS  60u /* steps per ticker round: a second at 60 steps/s */

/*
 * Bottom, between the graph and the random numbers (ADR 0022): the step count
 * and the pause, a ticker that advances one cell per step, the last
 * sandbox.clear. Paused, the ticker stops with everything on game time (the
 * squares, the world's ball and square), while the clear color, on the wall
 * clock, keeps turning: rendering goes on, updates do not. `step 1` moves it
 * one cell.
 */
static void sandbox_draw_commands(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer   *renderer = rb_app_renderer(app);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    const f32      left = SANDBOX_COMMANDS_LEFT;
    const f32      top = (f32)size.pixel_height - SANDBOX_GRAPH_MARGIN - SANDBOX_COMMANDS_HEIGHT;
    rb_renderer_draw_quad(renderer, left, top, SANDBOX_COMMANDS_WIDTH, SANDBOX_COMMANDS_HEIGHT,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));

    bool     paused = rb_app_is_paused(app);
    u64      step = rb_app_step_index(app);
    rb_color state_color =
        paused ? rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f) : rb_color_srgb(0.3f, 0.85f, 0.4f, 1.0f);
    rb_text_style style = sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    char          line[48];
    snprintf(line, sizeof(line), "STEP %llu", (unsigned long long)step);
    rb_text_draw(renderer, left + 8.0f, top + 8.0f, &style, line);
    rb_text_style state_style = style;
    state_style.color = state_color;
    state_style.align_x = RB_TEXT_RIGHT;
    rb_text_draw(renderer, left + SANDBOX_COMMANDS_WIDTH - 8.0f, top + 8.0f, &state_style,
                 paused ? "PAUSED" : "RUNNING");

    /* The ticker: one cell lit per step, the current one brighter. */
    const f32 cell = (SANDBOX_COMMANDS_WIDTH - 16.0f) / (f32)SANDBOX_COMMANDS_TICKS;
    u32       current = (u32)(step % SANDBOX_COMMANDS_TICKS);
    for (u32 i = 0; i <= current; ++i) {
        rb_color color = i == current ? state_color : rb_color_srgb(1.0f, 1.0f, 1.0f, 0.35f);
        rb_renderer_draw_quad(renderer, left + 8.0f + (f32)i * cell, top + 36.0f, cell - 1.0f,
                              16.0f, color);
    }

    rb_text_draw(renderer, left + 8.0f, top + 64.0f, &style, "sandbox.clear:");
    style.color = rb_color_srgb(0.7f, 0.85f, 1.0f, 1.0f);
    rb_text_draw(renderer, left + 8.0f, top + 86.0f, &style, game->last_command);
}

/* ============================================================================
 * Input panel
 * ========================================================================== */

#define SANDBOX_INPUT_HEIGHT 88.0f
#define SANDBOX_INPUT_CHARS  21u /* 16-pixel glyphs across the panel, after the margins */

/*
 * Above the commands panel (ADR 0023): the keys and buttons down and the mouse
 * as the engine sees them, and whether real input is on. Injected input shows
 * here like real input (input.key, input.click...), and the sandbox's keys
 * (L, T, R, B, Z) answer it; the glow follows an injected mouse too.
 */
static void sandbox_draw_input(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer       *renderer = rb_app_renderer(app);
    rb_platform_state *platform = rb_app_platform(app);
    rb_window_size     size = rb_window_get_size(platform, rb_app_window(app));
    const f32          left = SANDBOX_COMMANDS_LEFT;
    const f32 top = (f32)size.pixel_height - SANDBOX_GRAPH_MARGIN - SANDBOX_COMMANDS_HEIGHT - 8.0f -
                    SANDBOX_INPUT_HEIGHT;
    rb_renderer_draw_quad(renderer, left, top, SANDBOX_COMMANDS_WIDTH, SANDBOX_INPUT_HEIGHT,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));

    rb_text_style style = sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    rb_text_draw(renderer, left + 8.0f, top + 8.0f, &style, "INPUT");
    bool          real = rb_app_real_input(app);
    rb_text_style real_style = style;
    real_style.align_x = RB_TEXT_RIGHT;
    real_style.color =
        real ? rb_color_srgb(0.3f, 0.85f, 0.4f, 1.0f) : rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f);
    rb_text_draw(renderer, left + SANDBOX_COMMANDS_WIDTH - 8.0f, top + 8.0f, &real_style,
                 real ? "REAL ON" : "REAL OFF");

    /* Every key down, by name, as many as fit; "..." when more are held. */
    char  keys[SANDBOX_INPUT_CHARS + 1];
    usize used = 0;
    bool  cut = false;
    for (u32 key = 1; key < RB_KEY_COUNT && !cut; ++key) {
        if (!rb_key_is_down(platform, (rb_key)key)) {
            continue;
        }
        const char *name = rb_key_name((rb_key)key);
        usize       need = strlen(name) + (used > 0 ? 1 : 0);
        if (used + need > SANDBOX_INPUT_CHARS - 3) {
            cut = true;
            break;
        }
        snprintf(keys + used, sizeof(keys) - used, "%s%s", used > 0 ? " " : "", name);
        used += need;
    }
    if (cut) {
        snprintf(keys + used, sizeof(keys) - used, "...");
    } else if (used == 0) {
        snprintf(keys, sizeof(keys), "-");
    }
    rb_text_style value = style;
    value.color = rb_color_srgb(0.7f, 0.85f, 1.0f, 1.0f);
    rb_text_draw(renderer, left + 8.0f, top + 34.0f, &value, keys);

    /* The mouse: where it is, then one letter per button, lit while down. */
    rb_mouse_position mouse = rb_mouse_get_position(platform);
    char              where[24];
    snprintf(where, sizeof(where), "%d,%d", mouse.x, mouse.y);
    rb_text_draw(renderer, left + 8.0f, top + 60.0f, &value, where);
    static const char *const letters[RB_MOUSE_BUTTON_COUNT] = {"L", "R", "M", "4", "5"};
    for (u32 button = 0; button < RB_MOUSE_BUTTON_COUNT; ++button) {
        rb_text_style letter = style;
        letter.color = rb_mouse_button_is_down(platform, (rb_mouse_button)button)
                           ? rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f)
                           : rb_color_srgb(1.0f, 1.0f, 1.0f, 0.25f);
        rb_text_draw(renderer, left + SANDBOX_COMMANDS_WIDTH - 8.0f - (f32)(5 - button) * 24.0f,
                     top + 60.0f, &letter, letters[button]);
    }
}

/* ============================================================================
 * Capture panel
 * ========================================================================== */

/* At most 128x72, the shape of a 16:9 window: the free strip at the top is 88 pixels high. */
#define SANDBOX_CAPTURE_THUMB_WIDTH  128.0f
#define SANDBOX_CAPTURE_THUMB_HEIGHT 72.0f
#define SANDBOX_CAPTURE_TEXT_WIDTH   176.0f /* 11 glyphs: "CAPTURE (P)" */

/*
 * Top, right of the row of squares (ADR 0024): the last frame P captured,
 * shrunk, with its number and file size. The panel is in the next capture too:
 * a picture of a picture, one level deeper each time.
 */
static void sandbox_draw_capture(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer   *renderer = rb_app_renderer(app);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    if (size.pixel_width == 0 || size.pixel_height == 0) {
        return;
    }
    /* The window's shape, fitted inside the thumbnail's box. */
    f32 aspect = (f32)size.pixel_width / (f32)size.pixel_height;
    f32 thumb_width = SANDBOX_CAPTURE_THUMB_HEIGHT * aspect;
    f32 thumb_height = SANDBOX_CAPTURE_THUMB_HEIGHT;
    if (thumb_width > SANDBOX_CAPTURE_THUMB_WIDTH) {
        thumb_width = SANDBOX_CAPTURE_THUMB_WIDTH;
        thumb_height = SANDBOX_CAPTURE_THUMB_WIDTH / aspect;
    }
    const f32 left = SANDBOX_COMMANDS_LEFT;
    const f32 top = SANDBOX_GRAPH_MARGIN;
    rb_renderer_draw_quad(
        renderer, left, top,
        8.0f + SANDBOX_CAPTURE_THUMB_WIDTH + 8.0f + SANDBOX_CAPTURE_TEXT_WIDTH + 8.0f,
        8.0f + SANDBOX_CAPTURE_THUMB_HEIGHT + 8.0f, rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));
    if (rb_renderer_texture_is_valid(renderer, game->capture)) {
        rb_renderer_draw_texture(renderer, left + 8.0f, top + 8.0f, thumb_width, thumb_height,
                                 game->capture, rb_color_srgb(1, 1, 1, 1));
    } else {
        rb_renderer_draw_quad(renderer, left + 8.0f, top + 8.0f, thumb_width, thumb_height,
                              rb_color_srgb(1.0f, 1.0f, 1.0f, 0.08f));
    }

    const f32     text_left = left + 8.0f + SANDBOX_CAPTURE_THUMB_WIDTH + 8.0f;
    rb_text_style style = sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    rb_text_draw(renderer, text_left, top + 8.0f, &style, "CAPTURE (P)");
    rb_text_style info = style;
    info.color = rb_color_srgb(0.7f, 0.85f, 1.0f, 1.0f);
    rb_text_draw(renderer, text_left, top + 34.0f, &info,
                 game->capture_info[0] != '\0' ? game->capture_info : "none yet");
}

/* ============================================================================
 * Stats overlay
 * ========================================================================== */

#define SANDBOX_STATS_LINE   18.0f /* 16-pixel glyphs, 2 pixels apart */
#define SANDBOX_STATS_ROWS   11u
#define SANDBOX_STATS_COLUMN 416.0f /* 26 glyphs */
#define SANDBOX_STATS_GAP    32.0f
#define SANDBOX_STATS_VALUE  80.0f /* a value column: 4 glyphs and a space */

/*
 * At most 4 characters, so the columns line up: "0", "720B", "7.6K", "52K",
 * "15M". Up a unit from 1000, so "1000K" never shows: it reads "1.0M".
 */
static void sandbox_format_bytes(char *buf, usize size, usize bytes) {
    static const char units[] = {'K', 'M', 'G'};
    if (bytes < 1000) {
        snprintf(buf, size, bytes == 0 ? "0" : "%zuB", bytes);
        return;
    }
    f64 value = (f64)bytes / 1024.0;
    u32 unit = 0;
    while (value >= 1000.0 && unit + 1 < RB_ARRAY_COUNT(units)) {
        value /= 1024.0;
        ++unit;
    }
    snprintf(buf, size, value < 10.0 ? "%.1f%c" : "%.0f%c", value, units[unit]);
}

/* Milliseconds in 4 characters too: "0.04", "16.7", "123"; "-" before the first window. */
static void sandbox_format_ms(char *buf, usize size, f64 ms, bool known) {
    if (!known) {
        snprintf(buf, size, "-");
    } else {
        snprintf(buf, size, ms < 1.0 ? "%.2f" : ms < 100.0 ? "%.1f" : "%.0f", ms);
    }
}

/*
 * What the panel needs to draw its sections: a title, then rows of a label and
 * up to three right-aligned values.
 */
typedef struct sandbox_table {
    rb_renderer  *renderer;
    rb_text_style title;
    rb_text_style label;
    rb_text_style value;
    f32           left;
    f32           top;
} sandbox_table;

static void sandbox_table_row(const sandbox_table *table, u32 row, const char *label, const char *a,
                              const char *b, const char *c) {
    f32         y = table->top + (f32)row * SANDBOX_STATS_LINE;
    const char *values[3] = {a, b, c};
    rb_text_draw(table->renderer, table->left, y, &table->label, label);
    for (u32 i = 0; i < 3; ++i) {
        if (values[i] != NULL) {
            f32 right = table->left + SANDBOX_STATS_COLUMN - (f32)(2 - i) * SANDBOX_STATS_VALUE;
            rb_text_draw(table->renderer, right, y, &table->value, values[i]);
        }
    }
}

/* A row of ms: average, 99th percentile (frames only), maximum. */
static void sandbox_ms_row(const sandbox_table *table, u32 row, const char *label, f64 avg,
                           const f64 *p99, f64 max, bool known) {
    char a[16];
    char b[16];
    char c[16];
    sandbox_format_ms(a, sizeof(a), avg, known);
    sandbox_format_ms(b, sizeof(b), p99 != NULL ? *p99 : 0.0, known);
    sandbox_format_ms(c, sizeof(c), max, known);
    sandbox_table_row(table, row, label, a, p99 != NULL ? b : NULL, c);
}

/* A row of bytes: used, peak and, for arenas, committed. */
static void sandbox_bytes_row(const sandbox_table *table, u32 row, const char *label, usize used,
                              usize peak, const usize *committed) {
    char a[16];
    char b[16];
    char c[16];
    sandbox_format_bytes(a, sizeof(a), used);
    sandbox_format_bytes(b, sizeof(b), peak);
    if (committed != NULL) {
        sandbox_format_bytes(c, sizeof(c), *committed);
    }
    sandbox_table_row(table, row, label, a, b, committed != NULL ? c : NULL);
}

/*
 * Left half: the frame (ADR 0025). Times run from a frame's start to the next
 * one's, waits included: with vsync they sit at the refresh period. Work is
 * what the frame costs without its waits; update and render are parts of it.
 * Then the GPU, the swapchain and the validation counts.
 */
static void sandbox_draw_stats_frame(rb_app *app, sandbox_table table) {
    rb_app_frame_stats stats = rb_app_get_frame_stats(app);
    bool               known = stats.frames > 0;
    /* Values flush with the column's right edge, or with its left one for free text. */
    rb_text_style right = table.value;
    rb_text_style left = table.value;
    left.align_x = RB_TEXT_LEFT;
    char line[64];

    rb_text_draw(table.renderer, table.left, table.top, &table.title, "FRAME");
    snprintf(line, sizeof(line), "%.0f fps  %.0f steps/s", stats.fps, stats.steps_per_second);
    rb_text_draw(table.renderer, table.left + SANDBOX_STATS_COLUMN, table.top, &right,
                 known ? line : "-");
    sandbox_table_row(&table, 1, "ms", "avg", "1%", "max");
    sandbox_ms_row(&table, 2, "frame", stats.frame_avg_ms, &stats.frame_p99_ms, stats.frame_max_ms,
                   known);
    sandbox_ms_row(&table, 3, "work", stats.work_avg_ms, NULL, stats.work_max_ms, known);
    sandbox_ms_row(&table, 4, "update", stats.update_avg_ms, NULL, stats.update_max_ms, known);
    sandbox_ms_row(&table, 5, "render", stats.render_avg_ms, NULL, stats.render_max_ms, known);

    rb_renderer     *renderer = rb_app_renderer(app);
    rb_renderer_info info = rb_renderer_get_info(renderer);
    f32              y = table.top + 6.0f * SANDBOX_STATS_LINE;
    rb_text_draw(table.renderer, table.left, y, &table.title, "GPU");
    rb_text_draw(table.renderer, table.left + SANDBOX_STATS_COLUMN, y, &right, info.gpu_type);
    /* Cut to the column: driver names run to 40 characters and more. */
    snprintf(line, sizeof(line), "%.26s", info.gpu_name);
    rb_text_draw(table.renderer, table.left, y + SANDBOX_STATS_LINE, &left, line);
    rb_text_draw(table.renderer, table.left, y + 2.0f * SANDBOX_STATS_LINE, &left,
                 info.present_mode);
    rb_text_draw(table.renderer, table.left + SANDBOX_STATS_COLUMN, y + 2.0f * SANDBOX_STATS_LINE,
                 &right, info.vsync ? "vsync on" : "vsync off");
    snprintf(line, sizeof(line), "%ux%u, %u images", info.width, info.height, info.image_count);
    rb_text_draw(table.renderer, table.left, y + 3.0f * SANDBOX_STATS_LINE, &left, line);

    rb_renderer_validation_stats validation = rb_renderer_get_validation_stats(renderer);
    rb_text_draw(table.renderer, table.left, y + 4.0f * SANDBOX_STATS_LINE, &table.label,
                 "validation");
    rb_text_style counts = right;
    if (validation.enabled) {
        snprintf(line, sizeof(line), "%u err  %u warn", validation.errors, validation.warnings);
        /* Red at the first error: it is a bug, in the engine or the game. */
        if (validation.errors > 0) {
            counts.color = rb_color_srgb(0.95f, 0.3f, 0.25f, 1.0f);
        }
    } else {
        snprintf(line, sizeof(line), "off");
    }
    rb_text_draw(table.renderer, table.left + SANDBOX_STATS_COLUMN, y + 4.0f * SANDBOX_STATS_LINE,
                 &counts, line);
}

/*
 * Right half: memory (ADR 0012, 0025). Tags count the bytes asked for, by
 * subsystem; arenas also show the pages committed for them; the process is
 * the RAM the OS gives the whole program, the GPU driver's share included,
 * which no tag sees.
 */
static void sandbox_draw_stats_memory(rb_app *app, sandbox_table table) {
    rb_text_draw(table.renderer, table.left, table.top, &table.title, "MEMORY");
    sandbox_table copy = table;
    copy.value = table.label;
    copy.value.align_x = RB_TEXT_RIGHT;
    sandbox_table_row(&copy, 0, "", "used", "peak", "comm");

    u32 row = 1;
    for (u32 tag = 0; tag < RB_MEMORY_TAG_COUNT; ++tag, ++row) {
        rb_memory_stats stats = rb_memory_get_stats((rb_memory_tag)tag);
        sandbox_bytes_row(&table, row, rb_memory_tag_name((rb_memory_tag)tag), stats.live_bytes,
                          stats.peak_bytes, NULL);
    }
    const rb_arena *persistent = rb_app_persistent_arena(app);
    const rb_arena *frame = rb_app_frame_arena(app);
    sandbox_bytes_row(&table, row++, "persistent", persistent->pos, persistent->peak,
                      &persistent->committed);
    sandbox_bytes_row(&table, row++, "frame arena", frame->pos, frame->peak, &frame->committed);
    rb_process_memory process;
    if (rb_process_get_memory(&process)) {
        sandbox_bytes_row(&table, row, "process", process.resident_bytes,
                          process.peak_resident_bytes, NULL);
    } else {
        sandbox_table_row(&table, row, "process", "-", "-", NULL);
    }
}

/*
 * Above the frame-time graph and the input panel, toggled with them (L): the
 * engine's numbers, refreshed once a second. Titles white, labels gray, values
 * light blue.
 */
static void sandbox_draw_stats(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer   *renderer = rb_app_renderer(app);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    const f32      width = 8.0f + 2.0f * SANDBOX_STATS_COLUMN + SANDBOX_STATS_GAP + 8.0f;
    const f32      height = 8.0f + (f32)SANDBOX_STATS_ROWS * SANDBOX_STATS_LINE + 6.0f;
    /* 8 pixels above the input panel. */
    const f32 bottom = (f32)size.pixel_height - SANDBOX_GRAPH_MARGIN - SANDBOX_COMMANDS_HEIGHT -
                       8.0f - SANDBOX_INPUT_HEIGHT - 8.0f;
    const f32 top = bottom - height;
    /*
     * Opaque, unlike the other panels: in a small window it covers text, and
     * even a tenth of white glyphs showing through reads as clutter.
     */
    rb_renderer_draw_quad(renderer, SANDBOX_GRAPH_MARGIN, top, width, height,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 1.0f));

    rb_text_style title = sandbox_style(game->font, 16.0f, rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
    rb_text_style label = sandbox_style(game->font, 16.0f, rb_color_srgb(0.7f, 0.7f, 0.7f, 1.0f));
    rb_text_style value = sandbox_style(game->font, 16.0f, rb_color_srgb(0.7f, 0.85f, 1.0f, 1.0f));
    value.align_x = RB_TEXT_RIGHT;
    sandbox_table table = {.renderer = renderer,
                           .title = title,
                           .label = label,
                           .value = value,
                           .left = SANDBOX_GRAPH_MARGIN + 8.0f,
                           .top = top + 8.0f};
    sandbox_draw_stats_frame(app, table);
    table.left += SANDBOX_STATS_COLUMN + SANDBOX_STATS_GAP;
    sandbox_draw_stats_memory(app, table);
}

/* ============================================================================
 * Render
 * ========================================================================== */

/*
 * Two clocks on screen (ADR 0022). The clear color turns around the hue wheel
 * on the wall clock: it shows that frames keep reaching the screen, paused or
 * not. Muted (low saturation and value) so it stays easy on the eyes and the
 * quads stand out; shows from the next frame. The row of squares, the sliding
 * square, the world's ball and square move on game time, as a game's
 * animations would: the pause freezes them.
 */
static void game_render(void *state, rb_app *app, f64 alpha) {
    sandbox_state *game = state;
    sandbox_record_frame_time(game);
    f32 hue = sandbox_phase(game->start, SANDBOX_HUE_PERIOD_NS);
    rb_renderer_set_clear_color(rb_app_renderer(app), game->clear_fixed
                                                          ? game->clear_color
                                                          : hsv_to_srgb(hue, 0.5f, 0.45f));
    f32 slide = sandbox_game_phase(app, alpha, SANDBOX_SLIDE_PERIOD_S);
    sandbox_draw_world(game, app, slide, (f32)alpha);
    sandbox_draw_quads(game, app, sandbox_game_phase(app, alpha, SANDBOX_ROW_PERIOD_S), slide);
    sandbox_draw_font(game, app);
    sandbox_draw_text_demo(game, app);
    sandbox_draw_random(game, app);
    sandbox_draw_commands(game, app);
    sandbox_draw_input(game, app);
    sandbox_draw_capture(game, app);
    if (game->graph_visible) {
        sandbox_draw_frame_graph(game, app);
        sandbox_draw_stats(game, app);
    }
    /* Anywhere in the frame: the capture is taken when the frame ends, with every draw in it. */
    if (game->capture_wanted) {
        game->capture_wanted = false;
        game->capture_booked = true;
        rb_renderer_capture_frame(rb_app_renderer(app));
    }
}

const rb_app_callbacks sandbox_game = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
    .shutdown = game_shutdown,
};
