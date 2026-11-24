/*
 * The application's state, shared by the loop (app.c) and the engine's
 * commands (app_commands.c). Engine code only: games see rb_app as opaque.
 */
#ifndef RB_APP_STATE_H
#define RB_APP_STATE_H

#include <reborn/app.h>

#include "app/app_internal.h"
#include "command/command_internal.h"
#include "command/command_server.h"

/*
 * Why the loop ends. A value, not the log text: code can branch on it (a close
 * veto, a debug API), and the compiler checks every name.
 */
typedef enum app_quit_reason {
    APP_QUIT_NONE,           /* still running */
    APP_QUIT_WINDOW_CLOSED,  /* close requested on the main window */
    APP_QUIT_GAME_REQUESTED, /* rb_app_quit */
    APP_QUIT_COMMAND,        /* the quit command */
} app_quit_reason;

struct rb_app {
    const rb_app_callbacks *callbacks;
    void                   *state;
    rb_arena                persistent;
    rb_arena                frame;
    rb_platform_state      *platform;
    rb_window               window;
    rb_renderer            *renderer;
    app_quit_reason         quit_reason;
    f64                     step_dt; /* seconds per fixed step */
    /* Totals for the closing log line. Frames count loop iterations, rendered or not. */
    u64 frame_count;
    u64 step_count;

    /* Commands (ADR 0022). */
    bool              paused;
    rb_commands      *commands; /* from the persistent arena: large */
    bool              listening;
    rb_command_server server;
    bool              has_script; /* lines left to run */
    rb_command_script script;
    bool              script_waiting;
    u64               script_wait_until; /* the step count that ends the wait */
    bool              running_script;    /* the command running came from the script */
};

RB_INLINE bool rb_app_quit_requested(const rb_app *app) {
    return app->quit_reason != APP_QUIT_NONE;
}

/* Only the first request counts: it is the one that ends the loop. */
void rb_app_request_quit(rb_app *app, app_quit_reason reason);

/* Pump the platform, deliver the events to the game and the engine: a step without update. */
void rb_app_pump_events(rb_app *app);

/* One fixed step: pump, events, update. */
void rb_app_step(rb_app *app);

/*
 * The registry with the engine's commands, the script and the server, as the
 * options ask. False on failure (logged): a missing script, a port in use.
 */
bool rb_app_commands_start(rb_app *app, const rb_app_options *options);
void rb_app_commands_stop(rb_app *app);

/* Once per frame, before the steps: the script's next lines, then what the server received. */
void rb_app_commands_run(rb_app *app);

#endif /* RB_APP_STATE_H */
