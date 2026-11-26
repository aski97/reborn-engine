/*
 * The application's side of the command system (ADR 0022): the engine's own
 * options and commands, and the script and server run once per frame.
 */
#include <reborn/app.h>

#include <reborn/assert.h>
#include <reborn/log.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app/app_state.h"

/* step's upper bound: about half an hour of game at 60 steps per second, in one frame. */
#define APP_MAX_STEPS_PER_COMMAND 100000u

/* ============================================================================
 * Command line
 * ========================================================================== */

/* For --help. Not a diagnostic: the output the user asked for, on stdout. */
static const char APP_USAGE[] =
    "Options:\n"
    "  --script <file>   run the file's commands, one per line; replies on stdout\n"
    "  --listen <port>   accept commands on 127.0.0.1:<port> (0: any free port, logged)\n"
    "  --help            print this and exit\n";

static bool args_parse_port(const char *text, u16 *port) {
    if (*text == '\0' || strlen(text) > 5) {
        return false;
    }
    u32 value = 0;
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        value = value * 10 + (u32)(*c - '0');
    }
    if (value > UINT16_MAX) {
        return false;
    }
    *port = (u16)value;
    return true;
}

/* Errors are logged before the logger starts: they reach stderr, which is what a typo needs. */
rb_app_args_result rb_app_parse_args(int argc, char **argv, rb_app_options *options) {
    RB_ASSERT(options != NULL);
    *options = (rb_app_options){0};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        bool        has_value = i + 1 < argc;
        if (strcmp(arg, "--help") == 0) {
            fputs(APP_USAGE, stdout);
            return RB_APP_ARGS_HELP;
        }
        if (strcmp(arg, "--script") == 0) {
            if (!has_value) {
                RB_LOG_FATAL("--script needs a file");
                return RB_APP_ARGS_INVALID;
            }
            options->script_path = argv[++i];
        } else if (strcmp(arg, "--listen") == 0) {
            if (!has_value || !args_parse_port(argv[i + 1], &options->port)) {
                RB_LOG_FATAL("--listen needs a port from 0 to 65535");
                return RB_APP_ARGS_INVALID;
            }
            options->listen = true;
            ++i;
        } else {
            RB_LOG_FATAL("Unknown option \"%s\" (--help lists them)", arg);
            return RB_APP_ARGS_INVALID;
        }
    }
    return RB_APP_ARGS_RUN;
}

/* ============================================================================
 * Engine commands
 * ========================================================================== */

static void cmd_help(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    const rb_commands *commands = app->commands;
    if (args->count == 1) {
        const rb_command_desc *desc = rb_commands_find(commands, args->values[0]);
        if (desc == NULL) {
            rb_command_reply_error(reply, "unknown command \"%s\"", args->values[0]);
            return;
        }
        rb_command_reply_str(reply, "name", desc->name);
        rb_command_reply_str(reply, "usage", desc->usage != NULL ? desc->usage : "");
        rb_command_reply_str(reply, "summary", desc->summary != NULL ? desc->summary : "");
        return;
    }
    rb_command_reply_begin_object(reply, "commands");
    for (u32 i = 0; i < commands->count; ++i) {
        const rb_command_desc *desc = &commands->table[i];
        rb_command_reply_begin_object(reply, desc->name);
        rb_command_reply_str(reply, "usage", desc->usage != NULL ? desc->usage : "");
        rb_command_reply_str(reply, "summary", desc->summary != NULL ? desc->summary : "");
        rb_command_reply_end_object(reply);
    }
    rb_command_reply_end_object(reply);
}

static void cmd_status(void *state, rb_app *app, const rb_command_args *args,
                       rb_command_reply *reply) {
    (void)state;
    (void)args;
    rb_command_reply_u64(reply, "frame", app->frame_count);
    rb_command_reply_u64(reply, "step", app->step_count);
    rb_command_reply_bool(reply, "paused", app->paused);
    /* The game's own time: steps times their length, whatever the wall clock did. */
    rb_command_reply_f64(reply, "game_time", (f64)app->step_count * app->step_dt);
    rb_command_reply_f64(reply, "step_seconds", app->step_dt);
    rb_window_size size = rb_window_get_size(app->platform, app->window);
    rb_command_reply_begin_object(reply, "window");
    rb_command_reply_u64(reply, "width", size.pixel_width);
    rb_command_reply_u64(reply, "height", size.pixel_height);
    rb_command_reply_bool(reply, "minimized", rb_window_is_minimized(app->platform, app->window));
    rb_command_reply_end_object(reply);
}

static void app_pause(rb_app *app, bool paused) {
    if (app->paused != paused) {
        app->paused = paused;
        RB_LOG_INFO("%s at step %" PRIu64, paused ? "Paused" : "Resumed", app->step_count);
    }
}

static void cmd_pause(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    (void)args;
    app_pause(app, true);
    rb_command_reply_u64(reply, "step", app->step_count);
}

static void cmd_resume(void *state, rb_app *app, const rb_command_args *args,
                       rb_command_reply *reply) {
    (void)state;
    (void)args;
    app_pause(app, false);
    rb_command_reply_u64(reply, "step", app->step_count);
}

static void cmd_step(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    u32 count = 1;
    if (args->count == 1 && !rb_command_arg_u32(args, 0, &count, reply)) {
        return;
    }
    if (count == 0 || count > APP_MAX_STEPS_PER_COMMAND) {
        rb_command_reply_error(reply, "count must be from 1 to %u", APP_MAX_STEPS_PER_COMMAND);
        return;
    }
    app_pause(app, true);
    u64 before = app->step_count;
    for (u32 i = 0; i < count && !rb_app_quit_requested(app); ++i) {
        /*
         * Each step gets a fresh frame arena, as if it had a frame of its own:
         * thousands of steps in one frame must not pile up the game's scratch
         * memory. Nothing of this frame holds frame memory yet: the commands
         * run before anything else.
         */
        rb_arena_reset(&app->frame);
        rb_app_step(app);
    }
    rb_command_reply_u64(reply, "ran", app->step_count - before);
    rb_command_reply_u64(reply, "step", app->step_count);
}

static void cmd_quit(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    (void)args;
    (void)reply;
    rb_app_request_quit(app, APP_QUIT_COMMAND);
}

static void cmd_wait(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    if (!app->running_script) {
        rb_command_reply_error(reply, "wait works only in scripts");
        return;
    }
    u32 steps;
    if (!rb_command_arg_u32(args, 0, &steps, reply)) {
        return;
    }
    if (app->paused && steps > 0) {
        rb_command_reply_error(reply, "paused: the wait would never end (resume first, or step)");
        return;
    }
    app->script_wait_until = app->step_count + steps;
    app->script_waiting = steps > 0;
    rb_command_reply_u64(reply, "until_step", app->script_wait_until);
}

static const rb_command_desc APP_COMMANDS[] = {
    {"help", "[name]", "the commands, or one of them", 0, 1, cmd_help},
    {"status", NULL, "frame, step, pause, game time, window", 0, 0, cmd_status},
    {"pause", NULL, "stop the updates; events and rendering go on", 0, 0, cmd_pause},
    {"resume", NULL, "start the updates again", 0, 0, cmd_resume},
    {"step", "[count]", "pause, then run count steps (1) at once", 0, 1, cmd_step},
    {"quit", NULL, "end the run", 0, 0, cmd_quit},
    {"wait", "<steps>", "scripts only: run the next line after this many steps", 1, 1, cmd_wait},
};

/* ============================================================================
 * Start and stop
 * ========================================================================== */

bool rb_app_commands_start(rb_app *app, const rb_app_options *options) {
    RB_ASSERT(app != NULL && options != NULL);
    app->commands = rb_arena_push(&app->persistent, sizeof(rb_commands), _Alignof(rb_commands));
    if (app->commands == NULL) {
        return false;
    }
    rb_commands_init(app->commands);
    for (u32 i = 0; i < RB_ARRAY_COUNT(APP_COMMANDS); ++i) {
        bool added = rb_commands_add(app->commands, &APP_COMMANDS[i], false);
        RB_ASSERT(added);
        (void)added;
    }
    rb_app_input_commands_add(app);
    if (options->script_path != NULL) {
        if (!rb_command_script_load(&app->script, &app->persistent, options->script_path)) {
            return false;
        }
        app->has_script = true;
    }
    if (options->listen) {
        if (!rb_command_server_start(&app->server, options->port)) {
            return false;
        }
        app->listening = true;
    }
    return true;
}

void rb_app_commands_stop(rb_app *app) {
    RB_ASSERT(app != NULL);
    if (app->listening) {
        rb_command_server_stop(&app->server);
        app->listening = false;
    }
}

bool rb_app_register_command(rb_app *app, const rb_command_desc *desc) {
    RB_ASSERT(app != NULL && desc != NULL);
    return rb_commands_add(app->commands, desc, true);
}

/* ============================================================================
 * Each frame
 * ========================================================================== */

/* Until a wait, the end, or a quit. Replies go to stdout, the log to stderr and its file. */
static void app_run_script(rb_app *app) {
    if (app->script_waiting) {
        if (app->step_count < app->script_wait_until) {
            return;
        }
        app->script_waiting = false;
    }
    const char *line;
    while (!app->script_waiting && !rb_app_quit_requested(app) &&
           rb_command_script_next(&app->script, &line)) {
        char from[32];
        snprintf(from, sizeof(from), "script:%u", app->script.line_number);
        app->running_script = true;
        const char *reply = rb_commands_run(app->commands, line, from, app->state, app);
        app->running_script = false;
        if (reply != NULL) {
            fputs(reply, stdout);
            /* A reader on a pipe sees each reply now, not when the buffer fills. */
            fflush(stdout);
        }
    }
    if (!app->script_waiting && app->script.position >= app->script.size) {
        app->has_script = false;
        RB_LOG_INFO("Script finished at line %u", app->script.line_number);
    }
}

static void app_run_server(rb_app *app) {
    char line[RB_COMMAND_LINE_MAX + 1];
    for (;;) {
        rb_command_server_next next = rb_command_server_next_line(&app->server, line);
        if (next == RB_COMMAND_SERVER_NONE) {
            return;
        }
        const char *reply = next == RB_COMMAND_SERVER_LINE
                                ? rb_commands_run(app->commands, line, "socket", app->state, app)
                                : rb_commands_reject_long_line(app->commands, "socket");
        if (reply != NULL) {
            rb_command_server_reply(&app->server, reply);
        }
    }
}

void rb_app_commands_run(rb_app *app) {
    RB_ASSERT(app != NULL);
    if (app->has_script) {
        app_run_script(app);
    }
    if (app->listening && !rb_app_quit_requested(app)) {
        app_run_server(app);
    }
}
