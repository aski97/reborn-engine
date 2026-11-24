/*
 * Sandbox game: logs every event and the loop's rates, so window lifecycle,
 * input and timing can be checked by hand, and draws a few quads: a world
 * field through a 2D camera, a HUD in window pixels on top, textures made in
 * code, the builtin font's glyphs.
 */
#include "game.h"

#include <stdio.h>

#include <reborn/camera2d.h>
#include <reborn/clock.h>
#include <reborn/font.h>
#include <reborn/log.h>
#include <reborn/math.h>
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
    /*
     * World field (ADR 0016): the wheel zooms, Z resets, a left click drops a
     * marker where it lands in world units.
     */
    rb_camera2d camera;
    rb_vec2     marker;
    bool        has_marker;
    /*
     * Textures (ADR 0017): a checkerboard, nearest or linear (T swaps it: a
     * destroy and a create), and a soft disc whose alpha fades to its edge.
     */
    rb_texture        checker;
    rb_texture_filter checker_filter;
    rb_texture        glow;
    /* The builtin font (ADR 0018): its atlas and a line of glyphs placed by hand. */
    rb_font *font;
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

static bool game_init(void *state, rb_app *app) {
    sandbox_state *game = state;
    game->start = rb_time_now();
    game->window_start = game->start;
    game->graph_visible = true;
    game->camera = rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
    /* A failure is logged; a zero handle draws a plain quad, so the sandbox goes on. */
    game->checker_filter = RB_TEXTURE_FILTER_NEAREST;
    game->checker = sandbox_create_checker(rb_app_renderer(app), game->checker_filter);
    game->glow = sandbox_create_glow(rb_app_renderer(app));
    /* NULL on failure (logged): the font's drawing is skipped. */
    game->font = rb_font_create_builtin(rb_app_allocator(app), rb_app_renderer(app));
    return true;
}

/* The renderer's shutdown would free them anyway: done here as a game unloading its assets. */
static void game_shutdown(void *state, rb_app *app) {
    sandbox_state *game = state;
    rb_renderer_destroy_texture(rb_app_renderer(app), game->checker);
    rb_renderer_destroy_texture(rb_app_renderer(app), game->glow);
    rb_font_destroy(game->font);
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
 * across the window by fractions of a pixel, so its motion stays smooth; the
 * checkerboard enlarged 16 times, sharp or blurred by its filter; a yellow
 * glow under the mouse, blending with whatever it covers (ADR 0017).
 */
static void sandbox_draw_quads(const sandbox_state *game, rb_app *app, f32 hue, f32 slide) {
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
 * Font
 * ========================================================================== */

#define SANDBOX_TEXT_SCALE  3.0f /* integer: the 8x8 pixels stay square and sharp */
#define SANDBOX_ATLAS_SCALE 2.0f

/*
 * One line placed glyph by glyph with the font's metrics: the pen starts at
 * (x, baseline) and moves by each advance. The engine's own text drawing
 * (strings, measuring, alignment) comes next; this shows what it builds on.
 * Bytes are taken as codepoints: ASCII only, anything else gets the box.
 */
static void sandbox_draw_glyphs(rb_renderer *renderer, const rb_font *font, f32 x, f32 baseline,
                                f32 scale, const char *text, rb_color color) {
    rb_texture atlas = rb_font_get_atlas(font);
    f32        pen = x;
    for (const char *c = text; *c != '\0'; ++c) {
        rb_glyph glyph = rb_font_get_glyph(font, (u8)*c);
        /* A space has no image: only the pen moves. */
        if (glyph.source.width > 0.0f) {
            rb_renderer_draw_texture_region(renderer, pen + glyph.offset.x * scale,
                                            baseline + glyph.offset.y * scale,
                                            glyph.source.width * scale, glyph.source.height * scale,
                                            atlas, glyph.source, color);
        }
        pen += glyph.advance * scale;
    }
}

/*
 * Under the row of squares: two lines of text, the second with characters the
 * font lacks (a box each), then the whole atlas on a dark panel, glyphs white
 * on transparent, the padding between them visible.
 */
static void sandbox_draw_font(const sandbox_state *game, rb_app *app) {
    if (game->font == NULL) {
        return;
    }
    rb_renderer    *renderer = rb_app_renderer(app);
    rb_font_metrics metrics = rb_font_get_metrics(game->font);
    const f32 line = (metrics.ascent + metrics.descent + metrics.line_gap) * SANDBOX_TEXT_SCALE;
    const f32 left = 32.0f;
    f32       baseline = 128.0f + metrics.ascent * SANDBOX_TEXT_SCALE;
    sandbox_draw_glyphs(renderer, game->font, left, baseline, SANDBOX_TEXT_SCALE,
                        "SCORE 120  LIVES 3  Blink!", rb_color_srgb(1.0f, 0.85f, 0.2f, 1.0f));
    baseline += line;
    sandbox_draw_glyphs(renderer, game->font, left, baseline, SANDBOX_TEXT_SCALE,
                        "Missing: \x7F \xE8 (ASCII only)", rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));

    /* The atlas's size from its last glyph: 14 per row, '~' ends the last one. */
    rb_glyph last = rb_font_get_glyph(game->font, '~');
    f32      width = 128.0f * SANDBOX_ATLAS_SCALE;
    f32      height = (last.source.y + last.source.height) * SANDBOX_ATLAS_SCALE;
    f32      top = baseline + metrics.descent * SANDBOX_TEXT_SCALE + 16.0f;
    rb_renderer_draw_quad(renderer, left - 8.0f, top - 8.0f, width + 16.0f, height + 16.0f,
                          rb_color_srgb(0.0f, 0.0f, 0.0f, 0.6f));
    rb_renderer_draw_texture(renderer, left, top, width, height, rb_font_get_atlas(game->font),
                             rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f));
}

/* ============================================================================
 * World field
 * ========================================================================== */

#define SANDBOX_FIELD_WIDTH  16.0f
#define SANDBOX_FIELD_HEIGHT 9.0f
#define SANDBOX_GRID_LINE    0.04f /* world units */

/*
 * Through the camera (ADR 0016): a dark field with a grid at every unit, a
 * square going around the center and the last click's marker. Nothing reaches
 * the bars, which keep the clear color: resizing the window shows them.
 */
static void sandbox_draw_world(const sandbox_state *game, rb_app *app, f32 turn) {
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

    /* Along a rectangle around the center, one side per quarter turn: no trigonometry yet. */
    f32     t = turn * 4.0f;
    u32     side = (u32)t;
    f32     along = t - (f32)side;
    rb_vec2 corners[4] = {{4.0f, 2.0f}, {12.0f, 2.0f}, {12.0f, 7.0f}, {4.0f, 7.0f}};
    rb_vec2 from = corners[side % 4];
    rb_vec2 to = corners[(side + 1) % 4];
    rb_vec2 at = rb_vec2_add(from, rb_vec2_scale(rb_vec2_sub(to, from), along));
    rb_renderer_draw_quad(renderer, at.x - 0.5f, at.y - 0.5f, 1.0f, 1.0f,
                          rb_color_srgb(0.3f, 0.7f, 1.0f, 1.0f));

    if (game->has_marker) {
        rb_renderer_draw_quad(renderer, game->marker.x - 0.25f, game->marker.y - 0.25f, 0.5f, 0.5f,
                              rb_color_srgb(1.0f, 0.35f, 0.3f, 1.0f));
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
    f32 slide = sandbox_phase(game->start, SANDBOX_SLIDE_PERIOD_NS);
    sandbox_draw_world(game, app, slide);
    sandbox_draw_quads(game, app, hue, slide);
    sandbox_draw_font(game, app);
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
    .shutdown = game_shutdown,
};
