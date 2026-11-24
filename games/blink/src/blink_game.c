/*
 * Blink's callbacks for the application layer (ADR 0011): the glue between
 * the engine and the game's layers (ADR 0014). Events become actions, each
 * fixed step runs the rules on them, each frame draws the state.
 */
#include "blink_game.h"

#include <reborn/clock.h>
#include <reborn/font.h>
#include <reborn/log.h>

#include "blink_input.h"
#include "blink_rules.h"
#include "blink_view.h"

/* Far more than a player can click in one step (1/60 s); beyond it, actions are dropped. */
#define BLINK_MAX_ACTIONS 16u

typedef struct blink_game {
    blink_state  rules;
    rb_camera2d  camera;
    rb_font     *font;                       /* the builtin one, for the HUD */
    blink_action actions[BLINK_MAX_ACTIONS]; /* gathered by on_event, used by the next update */
    u32          action_count;
} blink_game;

/*
 * The icon's sizes (ADR 0020): the title bar at 100% and 200% scale, Alt+Tab
 * and the taskbar. Static: the pixels must outlive configure, until the engine
 * creates the window.
 */
static u8             icon_16[16 * 16 * 4];
static u8             icon_32[32 * 32 * 4];
static u8             icon_64[64 * 64 * 4];
static const rb_image icons[] = {{16, 16, icon_16}, {32, 32, icon_32}, {64, 64, icon_64}};

static bool game_configure(rb_app_config *config) {
    config->window.title = "Blink";
    blink_view_draw_icon(16, icon_16);
    blink_view_draw_icon(32, icon_32);
    blink_view_draw_icon(64, icon_64);
    config->window.icons = icons;
    config->window.icon_count = RB_ARRAY_COUNT(icons);
    /* The working directory: the repository root when run as in the README. */
    config->log.file_path = "blink.log.jsonl";
    config->log.file_level = RB_LOG_LEVEL_DEBUG;
    RB_APP_STATE(config, blink_game);
    return true;
}

/* ============================================================================
 * Commands (ADR 0022): what an agent needs to play and check the game
 * ========================================================================== */

static const char *blink_phase_name(blink_phase phase) {
    switch (phase) {
    case BLINK_PHASE_TITLE:
        return "title";
    case BLINK_PHASE_PLAYING:
        return "playing";
    case BLINK_PHASE_GAME_OVER:
        return "game_over";
    }
    return "unknown";
}

/*
 * The square in window pixels too: where a click must land, without the agent
 * knowing the camera. As drawn now, shrunk by the time left.
 */
static void cmd_state(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)args;
    const blink_game  *game = state;
    const blink_state *rules = &game->rules;
    rb_command_reply_str(reply, "phase", blink_phase_name(rules->phase));
    rb_command_reply_u64(reply, "score", rules->score);
    rb_command_reply_u64(reply, "lives", rules->lives);
    rb_command_reply_u64(reply, "best", rules->best);
    rb_command_reply_f32(reply, "phase_time", rules->phase_time);
    if (rules->phase != BLINK_PHASE_PLAYING) {
        return;
    }
    rb_rect        rect = blink_square_rect(rules);
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    rb_vec2        target = {(f32)size.pixel_width, (f32)size.pixel_height};
    rb_vec2        top_left =
        rb_camera2d_world_to_screen(&game->camera, target, (rb_vec2){rect.x, rect.y});
    rb_vec2 bottom_right = rb_camera2d_world_to_screen(
        &game->camera, target, (rb_vec2){rect.x + rect.width, rect.y + rect.height});
    rb_command_reply_begin_object(reply, "square");
    rb_command_reply_f32(reply, "x", rules->square.center.x);
    rb_command_reply_f32(reply, "y", rules->square.center.y);
    rb_command_reply_f32(reply, "side", rect.width);
    rb_command_reply_f32(reply, "remaining", rules->square.remaining);
    rb_command_reply_begin_object(reply, "pixels");
    rb_command_reply_f32(reply, "x", top_left.x);
    rb_command_reply_f32(reply, "y", top_left.y);
    rb_command_reply_f32(reply, "width", bottom_right.x - top_left.x);
    rb_command_reply_f32(reply, "height", bottom_right.y - top_left.y);
    rb_command_reply_f32(reply, "center_x", (top_left.x + bottom_right.x) * 0.5f);
    rb_command_reply_f32(reply, "center_y", (top_left.y + bottom_right.y) * 0.5f);
    rb_command_reply_end_object(reply);
    rb_command_reply_end_object(reply);
}

/* Back to the title with a chosen seed: the same squares, in the same places, every time. */
static void cmd_restart(void *state, rb_app *app, const rb_command_args *args,
                        rb_command_reply *reply) {
    (void)app;
    blink_game *game = state;
    u64         seed;
    if (!rb_command_arg_u64(args, 0, &seed, reply)) {
        return;
    }
    blink_start(&game->rules, &blink_default_tuning, seed);
    game->action_count = 0;
    RB_LOG_INFO("Seed: %llu (restart)", (unsigned long long)seed);
    rb_command_reply_u64(reply, "seed", seed);
}

static const rb_command_desc blink_commands[] = {
    {"blink.state", NULL, "phase, score, lives; the square in world units and window pixels", 0, 0,
     cmd_state},
    {"blink.restart", "<seed>", "a new session on the title, with this seed", 1, 1, cmd_restart},
};

static bool game_init(void *state, rb_app *app) {
    blink_game         *game = state;
    const blink_tuning *tuning = &blink_default_tuning;
    /* The whole field, always: the camera adds bars when the window's aspect differs. */
    game->camera = rb_camera2d_make(rb_vec2_scale(tuning->field, 0.5f), tuning->field);
    /* Different every run; logged, it plays the whole session again. */
    u64 seed = rb_time_now();
    RB_LOG_INFO("Seed: %llu", (unsigned long long)seed);
    blink_start(&game->rules, tuning, seed);
    /* Without text there is no score to show: not worth running (the failure is logged). */
    game->font = rb_font_create_builtin(rb_app_allocator(app), rb_app_renderer(app));
    if (game->font == NULL) {
        return false;
    }
    for (u32 i = 0; i < RB_ARRAY_COUNT(blink_commands); ++i) {
        if (!rb_app_register_command(app, &blink_commands[i])) {
            rb_font_destroy(game->font);
            return false;
        }
    }
    RB_LOG_INFO("Title: click or Space to play");
    return true;
}

static void game_shutdown(void *state, rb_app *app) {
    (void)app;
    blink_game *game = state;
    rb_font_destroy(game->font);
}

static void game_on_event(void *state, rb_app *app, const rb_event *event) {
    blink_game    *game = state;
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    rb_vec2        target = {(f32)size.pixel_width, (f32)size.pixel_height};
    blink_action   action;
    if (!blink_input_translate(event, &game->camera, target, &action)) {
        return;
    }
    if (game->action_count == BLINK_MAX_ACTIONS) {
        RB_LOG_WARN("More than %u actions in one step: dropped", BLINK_MAX_ACTIONS);
        return;
    }
    game->actions[game->action_count++] = action;
}

static void game_update(void *state, rb_app *app, f64 dt) {
    (void)app;
    blink_game  *game = state;
    blink_events events = blink_step(&game->rules, game->actions, game->action_count, (f32)dt);
    game->action_count = 0;
    blink_view_report(&game->rules, events);
}

static void game_render(void *state, rb_app *app, f64 alpha) {
    (void)alpha;
    blink_game    *game = state;
    rb_window_size size = rb_window_get_size(rb_app_platform(app), rb_app_window(app));
    blink_view_draw(&game->rules, &game->camera, game->font,
                    (rb_vec2){(f32)size.pixel_width, (f32)size.pixel_height}, rb_app_renderer(app));
}

const rb_app_callbacks blink_game_callbacks = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
    .shutdown = game_shutdown,
};
