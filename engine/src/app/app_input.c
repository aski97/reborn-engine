/*
 * The engine's input commands (ADR 0023): keys and mouse injected into the
 * next step, real input turned on or off, and what the engine sees.
 */
#include <reborn/assert.h>
#include <reborn/log.h>

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "app/app_state.h"

/*
 * Real positions go beyond the window only while a button is held outside it,
 * by at most a screen: a coordinate this large is a typo.
 */
#define APP_INPUT_MAX_COORD 1000000.0f

/* ============================================================================
 * Arguments
 * ========================================================================== */

static bool input_arg_key(const rb_command_args *args, u32 index, rb_key *out,
                          rb_command_reply *reply) {
    *out = rb_key_from_name(args->values[index]);
    if (*out == RB_KEY_UNKNOWN) {
        rb_command_reply_error(reply, "unknown key \"%s\" (e.g. a, 1, space, f1, left_ctrl)",
                               args->values[index]);
        return false;
    }
    return true;
}

static bool input_arg_button(const rb_command_args *args, u32 index, rb_mouse_button *out,
                             rb_command_reply *reply) {
    if (!rb_mouse_button_from_name(args->values[index], out)) {
        rb_command_reply_error(reply, "unknown button \"%s\" (left, right, middle, x1, x2)",
                               args->values[index]);
        return false;
    }
    return true;
}

/* A word between two choices: `yes` for true, `no` for false ("down"/"up", "on"/"off"). */
static bool input_arg_choice(const rb_command_args *args, u32 index, const char *yes,
                             const char *no, bool *out, rb_command_reply *reply) {
    const char *text = args->values[index];
    if (strcmp(text, yes) == 0 || strcmp(text, no) == 0) {
        *out = strcmp(text, yes) == 0;
        return true;
    }
    rb_command_reply_error(reply, "\"%s\" is neither %s nor %s", text, yes, no);
    return false;
}

/* Window pixels; decimals, as blink.state gives them, round to the nearest pixel. */
static bool input_arg_position(const rb_command_args *args, u32 index, rb_mouse_position *out,
                               rb_command_reply *reply) {
    f32 x;
    f32 y;
    if (!rb_command_arg_f32(args, index, &x, reply) ||
        !rb_command_arg_f32(args, index + 1, &y, reply)) {
        return false;
    }
    if (fabsf(x) > APP_INPUT_MAX_COORD || fabsf(y) > APP_INPUT_MAX_COORD) {
        rb_command_reply_error(reply, "position %g %g is too far from the window", (f64)x, (f64)y);
        return false;
    }
    *out = (rb_mouse_position){(i32)floorf(x + 0.5f), (i32)floorf(y + 0.5f)};
    return true;
}

/* ============================================================================
 * Queueing
 * ========================================================================== */

/* The step that sees input queued now: the next one to run (its count once it ran). */
static u64 input_next_step(const rb_app *app) {
    return app->step_count + 1;
}

static bool input_check_room(rb_app *app, u32 count, rb_command_reply *reply) {
    if (!rb_input_queue_has_room(&app->input_queue, count)) {
        rb_command_reply_error(reply, "%u injections already wait for their step: step first",
                               app->input_queue.count);
        return false;
    }
    return true;
}

static void input_push(rb_app *app, rb_injected_input input) {
    rb_input_queue_push(&app->input_queue, &input);
}

/* ============================================================================
 * Commands
 * ========================================================================== */

static void cmd_key(void *state, rb_app *app, const rb_command_args *args,
                    rb_command_reply *reply) {
    (void)state;
    rb_key key;
    bool   down;
    if (!input_arg_key(args, 0, &key, reply) ||
        !input_arg_choice(args, 1, "down", "up", &down, reply) ||
        !input_check_room(app, 1, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    input_push(
        app, (rb_injected_input){.step = step, .type = RB_INJECTED_KEY, .key = key, .down = down});
    rb_command_reply_u64(reply, "step", step);
}

/* Held for one step, so a game reading rb_key_is_down sees it down at least once. */
static void cmd_tap(void *state, rb_app *app, const rb_command_args *args,
                    rb_command_reply *reply) {
    (void)state;
    rb_key key;
    if (!input_arg_key(args, 0, &key, reply) || !input_check_room(app, 2, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    input_push(
        app, (rb_injected_input){.step = step, .type = RB_INJECTED_KEY, .key = key, .down = true});
    input_push(app, (rb_injected_input){
                        .step = step + 1, .type = RB_INJECTED_KEY, .key = key, .down = false});
    rb_command_reply_u64(reply, "step", step);
    rb_command_reply_u64(reply, "release_step", step + 1);
}

static void cmd_button(void *state, rb_app *app, const rb_command_args *args,
                       rb_command_reply *reply) {
    (void)state;
    if (args->count == 3) {
        rb_command_reply_error(reply, "x and y go together");
        return;
    }
    rb_mouse_button   button;
    bool              down;
    rb_mouse_position position = {0};
    bool              has_position = args->count == 4;
    if (!input_arg_button(args, 0, &button, reply) ||
        !input_arg_choice(args, 1, "down", "up", &down, reply) ||
        (has_position && !input_arg_position(args, 2, &position, reply)) ||
        !input_check_room(app, has_position ? 2 : 1, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    /* A real mouse gets there before it presses: games tracking the pointer see the move. */
    if (has_position) {
        input_push(
            app, (rb_injected_input){.step = step, .type = RB_INJECTED_MOVE, .position = position});
    }
    input_push(app, (rb_injected_input){.step = step,
                                        .type = RB_INJECTED_BUTTON,
                                        .button = button,
                                        .down = down,
                                        .has_position = has_position,
                                        .position = position});
    rb_command_reply_u64(reply, "step", step);
}

static void cmd_click(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    rb_mouse_position position;
    rb_mouse_button   button = RB_MOUSE_BUTTON_LEFT;
    if (!input_arg_position(args, 0, &position, reply) ||
        (args->count == 3 && !input_arg_button(args, 2, &button, reply)) ||
        !input_check_room(app, 3, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    input_push(app,
               (rb_injected_input){.step = step, .type = RB_INJECTED_MOVE, .position = position});
    input_push(app, (rb_injected_input){.step = step,
                                        .type = RB_INJECTED_BUTTON,
                                        .button = button,
                                        .down = true,
                                        .has_position = true,
                                        .position = position});
    input_push(app, (rb_injected_input){.step = step + 1,
                                        .type = RB_INJECTED_BUTTON,
                                        .button = button,
                                        .down = false,
                                        .has_position = true,
                                        .position = position});
    rb_command_reply_u64(reply, "step", step);
    rb_command_reply_u64(reply, "release_step", step + 1);
}

static void cmd_mouse(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    rb_mouse_position position;
    if (!input_arg_position(args, 0, &position, reply) || !input_check_room(app, 1, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    input_push(app,
               (rb_injected_input){.step = step, .type = RB_INJECTED_MOVE, .position = position});
    rb_command_reply_u64(reply, "step", step);
}

static void cmd_wheel(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    rb_mouse_wheel wheel = {0};
    if (!rb_command_arg_f32(args, 0, &wheel.y, reply) ||
        (args->count == 2 && !rb_command_arg_f32(args, 1, &wheel.x, reply)) ||
        !input_check_room(app, 1, reply)) {
        return;
    }
    u64 step = input_next_step(app);
    input_push(app, (rb_injected_input){.step = step, .type = RB_INJECTED_WHEEL, .wheel = wheel});
    rb_command_reply_u64(reply, "step", step);
}

/* At once, not at the next step: the pumps of a paused frame read the devices too. */
static void cmd_real(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    rb_platform_common *common = rb_platform_common_of(app->platform);
    if (args->count == 1) {
        bool on;
        if (!input_arg_choice(args, 0, "on", "off", &on, reply)) {
            return;
        }
        if (on == common->input.real_input_off) {
            rb_platform_common_set_real_input(common, app->window.id, on);
            if (on) {
                RB_LOG_INFO("Real input on");
            } else {
                /* A warning: someone at the keyboard will wonder why nothing answers. */
                RB_LOG_WARN("Real input off: keyboard and mouse ignored until \"input.real on\"");
            }
        }
    }
    rb_command_reply_bool(reply, "real", !common->input.real_input_off);
}

/* "Left Ctrl" -> "left_ctrl": the spelling the commands take, a valid JSON key as is. */
static void input_name_token(const char *name, char *out, usize size) {
    usize i = 0;
    for (; name[i] != '\0' && i + 1 < size; ++i) {
        out[i] = name[i] == ' ' ? '_' : (char)tolower((unsigned char)name[i]);
    }
    out[i] = '\0';
}

static void cmd_state(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    (void)args;
    const rb_input_state *input = &rb_platform_common_of(app->platform)->input;
    char                  token[32];
    /* Who holds each: an agent can tell its own keys from the ones someone presses. */
    rb_command_reply_begin_object(reply, "keys");
    for (u32 key = 0; key < RB_KEY_COUNT; ++key) {
        if (input->keys_down[key]) {
            input_name_token(rb_key_name((rb_key)key), token, sizeof(token));
            rb_command_reply_str(reply, token, input->keys_injected[key] ? "injected" : "real");
        }
    }
    rb_command_reply_end_object(reply);
    rb_command_reply_begin_object(reply, "buttons");
    for (u32 button = 0; button < RB_MOUSE_BUTTON_COUNT; ++button) {
        if (input->buttons_down[button]) {
            input_name_token(rb_mouse_button_name((rb_mouse_button)button), token, sizeof(token));
            rb_command_reply_str(reply, token,
                                 input->buttons_injected[button] ? "injected" : "real");
        }
    }
    rb_command_reply_end_object(reply);
    rb_command_reply_begin_object(reply, "mouse");
    rb_command_reply_i64(reply, "x", input->mouse_position.x);
    rb_command_reply_i64(reply, "y", input->mouse_position.y);
    rb_command_reply_bool(reply, "in_window", input->mouse_window == app->window.id);
    rb_command_reply_end_object(reply);
    rb_command_reply_bool(reply, "real", !input->real_input_off);
    rb_command_reply_u64(reply, "waiting", app->input_queue.count);
    rb_command_reply_u64(reply, "next_step", input_next_step(app));
}

static const rb_command_desc APP_INPUT_COMMANDS[] = {
    {"input.key", "<key> <down|up>", "press or release a key in the next step", 2, 2, cmd_key},
    {"input.tap", "<key>", "press a key in the next step, release it in the one after", 1, 1,
     cmd_tap},
    {"input.button", "<button> <down|up> [<x> <y>]",
     "press or release a mouse button in the next step, at a point if given", 2, 4, cmd_button},
    {"input.click", "<x> <y> [button]",
     "move there and press (left) in the next step, release in the one after", 2, 3, cmd_click},
    {"input.mouse", "<x> <y>", "move the mouse in the next step (window pixels)", 2, 2, cmd_mouse},
    {"input.wheel", "<dy> [dx]", "turn the wheel in the next step (notches, + up and right)", 1, 2,
     cmd_wheel},
    {"input.real", "[on|off]", "turn keyboard and mouse on or off at once (no argument: as now)", 0,
     1, cmd_real},
    {"input.state", NULL, "keys and buttons down, mouse, real input, injections waiting", 0, 0,
     cmd_state},
};

/* ============================================================================
 * Registration and each step
 * ========================================================================== */

void rb_app_input_commands_add(rb_app *app) {
    for (u32 i = 0; i < RB_ARRAY_COUNT(APP_INPUT_COMMANDS); ++i) {
        bool added = rb_commands_add(app->commands, &APP_INPUT_COMMANDS[i], false);
        RB_ASSERT(added);
        (void)added;
    }
}

void rb_app_input_apply(rb_app *app) {
    rb_input_queue_apply(&app->input_queue, rb_platform_common_of(app->platform), app->window.id,
                         input_next_step(app));
}
