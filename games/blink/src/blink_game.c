/*
 * Blink's callbacks for the application layer (ADR 0011): the glue between
 * the engine and the game's layers (ADR 0014). Events become actions, each
 * fixed step runs the rules on them, each frame draws the state.
 */
#include "blink_game.h"

#include <reborn/clock.h>
#include <reborn/log.h>

#include "blink_input.h"
#include "blink_rules.h"
#include "blink_view.h"

/* Far more than a player can click in one step (1/60 s); beyond it, actions are dropped. */
#define BLINK_MAX_ACTIONS 16u

typedef struct blink_game {
    blink_state  rules;
    rb_camera2d  camera;
    blink_action actions[BLINK_MAX_ACTIONS]; /* gathered by on_event, used by the next update */
    u32          action_count;
} blink_game;

static bool game_configure(rb_app_config *config) {
    config->window.title = "Blink";
    /* The working directory: the repository root when run as in the README. */
    config->log.file_path = "blink.log.jsonl";
    config->log.file_level = RB_LOG_LEVEL_DEBUG;
    RB_APP_STATE(config, blink_game);
    return true;
}

static bool game_init(void *state, rb_app *app) {
    (void)app;
    blink_game         *game = state;
    const blink_tuning *tuning = &blink_default_tuning;
    /* The whole field, always: the camera adds bars when the window's aspect differs. */
    game->camera = rb_camera2d_make(rb_vec2_scale(tuning->field, 0.5f), tuning->field);
    /* Different every run; logged, it plays the whole session again. */
    u64 seed = rb_time_now();
    RB_LOG_INFO("Seed: %llu", (unsigned long long)seed);
    blink_start(&game->rules, tuning, seed);
    blink_view_report(&game->rules, BLINK_EVENT_STARTED);
    return true;
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
    blink_game *game = state;
    blink_view_draw(&game->rules, &game->camera, rb_app_renderer(app));
}

const rb_app_callbacks blink_game_callbacks = {
    .configure = game_configure,
    .init = game_init,
    .on_event = game_on_event,
    .update = game_update,
    .render = game_render,
};
