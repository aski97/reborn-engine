/*
 * Application layer (ADR 0011): the engine owns the game loop and calls the
 * game back at each phase of its life.
 *
 *   typedef struct game_state { f32 x; } game_state;
 *
 *   static bool game_configure(rb_app_config *config) {
 *       config->window.title = "My game";
 *       RB_APP_STATE(config, game_state);
 *       return true;
 *   }
 *   static void game_update(void *state, rb_app *app, f64 dt) {
 *       game_state *game = state;
 *       if (rb_key_is_down(rb_app_platform(app), RB_KEY_D)) game->x += (f32)(100.0 * dt);
 *   }
 *
 *   static const rb_app_callbacks game = {.configure = game_configure, .update = game_update};
 *
 *   int main(int argc, char **argv) {
 *       return rb_app_run(&game, argc, argv);
 *   }
 *
 * Phases, in order:
 *   configure   before any system starts, the logger included: change the defaults
 *   init        systems, main window, renderer and state ready
 *   loop        per frame: commands (reborn/command.h), then
 *               per step: on_event for each event, then update(dt);
 *               per frame: render(alpha), skipped while minimized
 *   shutdown    loop over, every system still alive
 *
 * `state` is the game's own memory, allocated zeroed by the engine from the
 * size set in configure; it plays the role of `this`.
 */
#ifndef RB_APP_H
#define RB_APP_H

#include <reborn/command.h>
#include <reborn/defines.h>
#include <reborn/log.h>
#include <reborn/memory.h>
#include <reborn/platform.h>
#include <reborn/renderer.h>
#include <reborn/version.h>

/* Opaque: only the engine knows its fields; callbacks reach them through the accessors below. */
typedef struct rb_app rb_app;

/* ============================================================================
 * Configuration
 * ========================================================================== */

/* For max_fps: no frame cap. */
#define RB_APP_FPS_UNCAPPED 0u

/* Every field holds its default when configure is called. */
typedef struct rb_app_config {
    rb_log_config      log;         /* console INFO, no file */
    rb_window_desc     window;      /* main window; "Reborn", 1280x720 */
    rb_renderer_config renderer;    /* validation on in debug builds of the engine, vsync on */
    u32                update_rate; /* fixed steps per second, 1..1000; 60 */
    u32                max_fps;     /* frame cap on top of vsync; uncapped */
    usize              state_size;  /* game state; 0 */
    usize              state_align; /* power of two; the platform's maximum fundamental alignment */
    usize              persistent_reserve; /* address space of the persistent arena; 256 MiB */
    usize              frame_reserve;      /* address space of the frame arena; 64 MiB */
} rb_app_config;

/* Sets the state's size and alignment from its type: neither can be got wrong. */
#define RB_APP_STATE(config, T)                                                                    \
    ((config)->state_size = sizeof(T), (config)->state_align = _Alignof(T))

/* ============================================================================
 * Callbacks
 * ========================================================================== */

/* Only `update` is required; a NULL callback is skipped. */
typedef struct rb_app_callbacks {
    /* Before any system starts. False aborts the run (rb_app_run returns 1). */
    bool (*configure)(rb_app_config *config);
    /*
     * Once, with every system running. False aborts the run without calling
     * shutdown: undo here whatever init did before failing.
     */
    bool (*init)(void *state, rb_app *app);
    /*
     * Each event, at the start of the step that delivers it. The engine then
     * handles it too: a close request on the main window ends the loop.
     */
    void (*on_event)(void *state, rb_app *app, const rb_event *event);
    /* Once per step; `dt` is the fixed step in seconds (1 / update_rate). */
    void (*update)(void *state, rb_app *app, f64 dt);
    /*
     * Once per frame, after the frame's steps. `alpha` in [0, 1) is how far the
     * frame's time is past the last step, in steps: interpolate between the
     * previous and the current state to move smoothly between steps.
     */
    void (*render)(void *state, rb_app *app, f64 alpha);
    /* Once, after the loop, with every system still alive. Not called if init failed. */
    void (*shutdown)(void *state, rb_app *app);
} rb_app_callbacks;

/* ============================================================================
 * Run
 * ========================================================================== */

/*
 * What rb_app_run calls; call rb_app_run instead. `header_version` is the
 * RB_VERSION_NUMBER the game was compiled with, compared with the library's.
 */
RB_API int rb_app_run_checked(const rb_app_callbacks *callbacks, int argc, char **argv,
                              u32 header_version);

/*
 * Checks that the game was compiled against this engine's headers, reads the
 * command line, calls configure, starts the logger and the engine, runs the
 * loop until a quit and shuts everything down, logger last. Returns the exit
 * code for main: 0, or 1 when the versions differ, an option is unknown, or
 * configure, startup or init failed (logged).
 * Do not start the logger before: messages logged earlier go to stderr only.
 *
 * `argc` and `argv` as main received them (0 and NULL for none). The engine's
 * options (ADR 0022):
 *   --script <file>   run the file's commands, one per line; replies on stdout
 *   --listen <port>   accept commands on 127.0.0.1:<port> (0: any free port, logged)
 *   --help            print the options and exit
 *
 * Inline in the header, so it is compiled into the game: RB_VERSION_NUMBER is
 * then the game's headers' value, which the library alone cannot know.
 */
static inline int rb_app_run(const rb_app_callbacks *callbacks, int argc, char **argv) {
    return rb_app_run_checked(callbacks, argc, argv, RB_VERSION_NUMBER);
}

/* ============================================================================
 * Accessors (from inside a callback)
 * ========================================================================== */

/* For input and window queries (reborn/platform.h). */
RB_API rb_platform_state *rb_app_platform(rb_app *app);

RB_API rb_window rb_app_window(const rb_app *app);

RB_API rb_renderer *rb_app_renderer(rb_app *app);

/* Lives until the end of the run: memory the game keeps beyond init. */
RB_API rb_arena *rb_app_persistent_arena(rb_app *app);

/* Reset at the start of every frame: scratch memory for a step or a render. */
RB_API rb_arena *rb_app_frame_arena(rb_app *app);

/*
 * For objects freed one at a time. Tag them RB_MEMORY_TAG_GAME and free them
 * before shutdown returns: whatever is left is logged as a leak at the end of
 * the run (ADR 0012).
 */
RB_API rb_allocator rb_app_allocator(rb_app *app);

/* Ends the loop after the current step's events, or after the current frame. */
RB_API void rb_app_quit(rb_app *app);

/*
 * Paused by the pause or step command (ADR 0022): no update runs, events and
 * render still do. Games may show it; they need not.
 */
RB_API bool rb_app_is_paused(const rb_app *app);

/* Steps run since the start: the number the step and status commands report. */
RB_API u64 rb_app_step_index(const rb_app *app);

/*
 * Whether keyboard and mouse reach the game: false after `input.real off`
 * (ADR 0023), while an agent drives it with injected input alone. Games may
 * show it; injected input looks like real input to them either way.
 */
RB_API bool rb_app_real_input(const rb_app *app);

/* ============================================================================
 * Commands (ADR 0022)
 * ========================================================================== */

/*
 * Adds a game command, from init on. The name needs the game's prefix
 * ("blink.state"); the strings in `desc` must outlive the run (literals).
 * False, logged, for a bad or taken name or a full table.
 */
RB_API bool rb_app_register_command(rb_app *app, const rb_command_desc *desc);

#endif /* RB_APP_H */
