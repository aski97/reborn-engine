/* Application layer: startup, game loop, shutdown (ADR 0011). */
#include <reborn/app.h>

#include <reborn/assert.h>
#include <reborn/clock.h>
#include <reborn/log.h>
#include <reborn/version.h>

#include <stddef.h>
#include <stdio.h>

#include "app/app_internal.h"
#include "app/frame_timing.h"
#include "memory/memory_internal.h"
#include "renderer/renderer_internal.h"

/*
 * Longest frame delta the simulation sees. Longer gaps (suspend, a debugger
 * break, the Win32 modal loop while dragging) are dropped instead of being
 * replayed as a burst of steps; it also bounds the steps per frame, so an
 * update slower than its step slows the game down instead of spiralling.
 */
#define APP_MAX_FRAME_DELTA_NS (250 * RB_NS_PER_MS)

#define APP_MAX_UPDATE_RATE 1000u

/*
 * Why the loop ends. A value, not the log text: code can branch on it (a close
 * veto, a debug API), and the compiler checks every name.
 */
typedef enum app_quit_reason {
    APP_QUIT_NONE,           /* still running */
    APP_QUIT_WINDOW_CLOSED,  /* close requested on the main window */
    APP_QUIT_GAME_REQUESTED, /* rb_app_quit */
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
    /* Totals for the closing log line. Frames count loop iterations, rendered or not. */
    u64 frame_count;
    u64 step_count;
};

/* ============================================================================
 * Configuration
 * ========================================================================== */

rb_app_config rb_app_default_config(void) {
    return (rb_app_config){
        .log = {.console_level = RB_LOG_LEVEL_INFO, .file_level = RB_LOG_LEVEL_INFO},
        .window = {.title = "Reborn", .width = 1280, .height = 720},
#if defined(RB_DEBUG)
        .renderer = {.validation = true, .vsync = true},
#else
        .renderer = {.validation = false, .vsync = true},
#endif
        .update_rate = 60,
        /* The present paces frames with vsync (ADR 0013, X2). */
        .max_fps = RB_APP_FPS_UNCAPPED,
        .state_size = 0,
        .state_align = _Alignof(max_align_t),
        .persistent_reserve = RB_MIB(256),
        .frame_reserve = RB_MIB(64),
    };
}

/*
 * Every field as configure left it, before validation: whatever happens next,
 * the log shows what the engine received (one day from a file).
 */
static void app_log_config(const rb_app_config *config) {
    char cap[16] = "uncapped";
    if (config->max_fps != RB_APP_FPS_UNCAPPED) {
        snprintf(cap, sizeof(cap), "%u fps", config->max_fps);
    }
    char state[32];
    char persistent[32];
    char frame[32];
    RB_LOG_INFO("Config: window \"%s\" %ux%u",
                config->window.title != NULL ? config->window.title : "(no title)",
                config->window.width, config->window.height);
    RB_LOG_INFO("Config: %u updates/s, frame cap %s", config->update_rate, cap);
    RB_LOG_INFO("Config: renderer validation %s, vsync %s",
                config->renderer.validation ? "on" : "off", config->renderer.vsync ? "on" : "off");
    RB_LOG_INFO("Config: state %s aligned to %zu, arenas %s persistent + %s frame reserved",
                rb_memory_format_size(state, sizeof(state), config->state_size),
                config->state_align,
                rb_memory_format_size(persistent, sizeof(persistent), config->persistent_reserve),
                rb_memory_format_size(frame, sizeof(frame), config->frame_reserve));
    if (config->log.file_path != NULL) {
        RB_LOG_INFO("Config: log console %s, file \"%s\" %s",
                    rb_log_level_name(config->log.console_level), config->log.file_path,
                    rb_log_level_name(config->log.file_level));
    } else {
        RB_LOG_INFO("Config: log console %s, no file",
                    rb_log_level_name(config->log.console_level));
    }
}

/* Values may one day come from a file: logged and refused, not asserted. */
bool rb_app_config_validate(const rb_app_config *config) {
    RB_ASSERT(config != NULL);
    bool ok = true;
    /* Above the bound a step would round to under a microsecond, or to 0 ns. */
    if (config->update_rate == 0 || config->update_rate > APP_MAX_UPDATE_RATE) {
        RB_LOG_ERROR("App config: update_rate %u is outside 1..%u", config->update_rate,
                     APP_MAX_UPDATE_RATE);
        ok = false;
    }
    if (config->state_align == 0 || (config->state_align & (config->state_align - 1)) != 0) {
        RB_LOG_ERROR("App config: state_align %zu is not a power of two", config->state_align);
        ok = false;
    }
    if (config->persistent_reserve == 0 || config->frame_reserve == 0) {
        RB_LOG_ERROR("App config: arena reservations must be above 0");
        ok = false;
    }
    if (config->window.title == NULL || config->window.width == 0 || config->window.height == 0) {
        RB_LOG_ERROR("App config: the main window needs a title and a non-zero size");
        ok = false;
    }
    return ok;
}

/* ============================================================================
 * Startup and shutdown
 * ========================================================================== */

/*
 * Everything up to the game's init. On failure, whatever was started is
 * stopped again: the caller only has to report.
 */
static bool app_startup(rb_app *app, const rb_app_config *config) {
    if (!rb_arena_create(&app->persistent, config->persistent_reserve)) {
        return false;
    }
    if (!rb_arena_create(&app->frame, config->frame_reserve)) {
        rb_arena_destroy(&app->persistent);
        return false;
    }
    app->platform = rb_platform_startup(rb_arena_allocator(&app->persistent));
    if (app->platform != NULL) {
        app->window = rb_window_create(app->platform, &config->window);
        if (rb_window_is_valid(app->platform, app->window)) {
            app->renderer = rb_renderer_startup(rb_arena_allocator(&app->persistent),
                                                &config->renderer, app->platform, app->window);
            if (app->renderer != NULL) {
                app->state =
                    rb_arena_push(&app->persistent, config->state_size, config->state_align);
                if (app->state != NULL) {
                    return true;
                }
                rb_renderer_shutdown(app->renderer);
            }
            rb_window_destroy(app->platform, app->window);
        }
        rb_platform_shutdown(app->platform);
    }
    rb_arena_destroy(&app->frame);
    rb_arena_destroy(&app->persistent);
    return false;
}

static void app_log_arena(const char *name, const rb_arena *arena) {
    char peak[32];
    char reserved[32];
    RB_LOG_INFO("Arena %s: peak %s of %s reserved", name,
                rb_memory_format_size(peak, sizeof(peak), arena->peak),
                rb_memory_format_size(reserved, sizeof(reserved), arena->reserved));
}

/*
 * Once every system has given its memory back, and while the arenas still
 * exist to read their peaks (ADR 0012). A leak is logged, not fatal: the run
 * itself went fine.
 */
static void app_log_memory(const rb_app *app) {
    rb_memory_log_report();
    app_log_arena("persistent", &app->persistent);
    app_log_arena("frame", &app->frame);
}

static void app_shutdown(rb_app *app) {
    rb_renderer_shutdown(app->renderer);
    rb_window_destroy(app->platform, app->window);
    rb_platform_shutdown(app->platform);
    app_log_memory(app);
    rb_arena_destroy(&app->frame);
    rb_arena_destroy(&app->persistent);
}

/* ============================================================================
 * Loop
 * ========================================================================== */

/* No default: -Wswitch then flags a reason added to the enum but not here. */
static const char *app_quit_reason_text(app_quit_reason reason) {
    switch (reason) {
    case APP_QUIT_NONE:
        return "none";
    case APP_QUIT_WINDOW_CLOSED:
        return "main window close requested";
    case APP_QUIT_GAME_REQUESTED:
        return "requested by the game";
    }
    return "unknown";
}

static bool app_quit_requested(const rb_app *app) {
    return app->quit_reason != APP_QUIT_NONE;
}

/* Only the first request counts: it is the one that ends the loop. */
static void app_request_quit(rb_app *app, app_quit_reason reason) {
    RB_ASSERT(reason != APP_QUIT_NONE);
    if (!app_quit_requested(app)) {
        app->quit_reason = reason;
        RB_LOG_INFO("Quit: %s", app_quit_reason_text(reason));
    }
}

/*
 * One fixed step: pump, events, update (ADR 0011, D1). Pumping per step, not
 * per frame, makes rb_key_was_pressed cover exactly the time since the
 * previous update: a frame with two steps does not report a press twice, a
 * frame with none does not lose it.
 */
static void app_step(rb_app *app, f64 dt) {
    rb_platform_pump(app->platform);
    rb_event event;
    while (rb_platform_poll_event(app->platform, &event)) {
        if (app->callbacks->on_event != NULL) {
            app->callbacks->on_event(app->state, app, &event);
        }
        if (event.type == RB_EVENT_WINDOW_CLOSE_REQUESTED && event.window.id == app->window.id) {
            app_request_quit(app, APP_QUIT_WINDOW_CLOSED);
        }
    }
    /* Every event of the step was still delivered; only the update is skipped. */
    if (app_quit_requested(app)) {
        return;
    }
    app->callbacks->update(app->state, app, dt);
    ++app->step_count;
}

static void app_loop(rb_app *app, const rb_app_config *config) {
    u64 step_ns = RB_NS_PER_S / config->update_rate;
    f64 dt = rb_time_to_seconds(step_ns);
    u64 frame_ns = config->max_fps == RB_APP_FPS_UNCAPPED ? 0 : RB_NS_PER_S / config->max_fps;

    u64            now = rb_time_now();
    rb_step_clock  clock;
    rb_frame_pacer pacer;
    rb_step_clock_init(&clock, step_ns, APP_MAX_FRAME_DELTA_NS, now);
    rb_frame_pacer_init(&pacer, frame_ns, now);
    RB_LOG_DEBUG("Loop started");

    while (!app_quit_requested(app)) {
        rb_step_clock_advance(&clock, rb_time_now());
        rb_arena_reset(&app->frame);

        while (!app_quit_requested(app) && rb_step_clock_consume(&clock)) {
            app_step(app, dt);
        }
        if (app_quit_requested(app)) {
            break;
        }

        /*
         * Nothing to show while minimized or without a swapchain: the
         * simulation keeps going. Also rebuilds the swapchain after a resize.
         * With vsync, begin waits for a free image: that paces the loop.
         */
        bool visible = rb_renderer_begin_frame(app->renderer);
        if (visible) {
            if (app->callbacks->render != NULL) {
                app->callbacks->render(app->state, app, rb_step_clock_alpha(&clock));
            }
            rb_renderer_end_frame(app->renderer);
        }
        ++app->frame_count;

        now = rb_time_now();
        u64 wait = rb_frame_pacer_wait(&pacer, now);
        if (!visible) {
            /* Even uncapped: without a render, frames before the next step are empty spins. */
            u64 next_step = rb_step_clock_next_step_at(&clock);
            if (next_step > now && next_step - now > wait) {
                wait = next_step - now;
            }
        }
        rb_time_sleep(wait);
    }
}

/* ============================================================================
 * Run
 * ========================================================================== */

/* Everything between logger startup and shutdown. */
static int app_run_logged(const rb_app_callbacks *callbacks, const rb_app_config *config) {
    app_log_config(config);
    if (!rb_app_config_validate(config)) {
        RB_LOG_FATAL("App config is invalid");
        return 1;
    }

    rb_app app = {.callbacks = callbacks};
    if (!app_startup(&app, config)) {
        RB_LOG_FATAL("App startup failed");
        return 1;
    }
    RB_LOG_INFO("App started");

    if (callbacks->init != NULL) {
        RB_LOG_DEBUG("Game init");
        if (!callbacks->init(app.state, &app)) {
            RB_LOG_FATAL("App init failed");
            app_shutdown(&app);
            return 1;
        }
    }

    /* A quit from init skips the loop entirely: no "Loop started" for a loop that never runs. */
    u64 start = rb_time_now();
    if (!app_quit_requested(&app)) {
        app_loop(&app, config);
    }
    f64 seconds = rb_time_to_seconds(rb_time_now() - start);

    if (callbacks->shutdown != NULL) {
        RB_LOG_DEBUG("Game shutdown");
        callbacks->shutdown(app.state, &app);
    }
    RB_LOG_INFO("App stopped after %.2f s: %llu frames, %llu steps", seconds,
                (unsigned long long)app.frame_count, (unsigned long long)app.step_count);
    app_shutdown(&app);
    return 0;
}

int rb_app_run_checked(const rb_app_callbacks *callbacks, u32 header_version) {
    RB_ASSERT(callbacks != NULL);

    /*
     * First, before reading the callbacks: with other headers even the layout
     * of rb_app_callbacks and rb_app_config may differ. The logger is not
     * started yet, so this goes to stderr only.
     */
    if (header_version != RB_VERSION_NUMBER) {
        RB_LOG_FATAL("Version mismatch: game built with headers %u.%u.%u, engine library %s",
                     header_version >> 22, (header_version >> 12) & 0x3ffu, header_version & 0xfffu,
                     rb_version_string());
        return 1;
    }
    RB_ASSERT(callbacks->update != NULL);

    /* Before the logger, so configure can choose its outputs; its own logs go to stderr. */
    rb_app_config config = rb_app_default_config();
    if (callbacks->configure != NULL && !callbacks->configure(&config)) {
        RB_LOG_FATAL("App configure failed");
        return 1;
    }

    /* A file that cannot be opened is no reason to stop: the console is added anyway. */
    if (!rb_log_startup(&config.log)) {
        RB_LOG_WARN("Logger startup failed: continuing with the console only");
    }
    RB_LOG_INFO("Reborn Engine v%s", rb_version_string());

    int code = app_run_logged(callbacks, &config);

    RB_LOG_INFO("Exiting with code %d", code);
    rb_log_shutdown();
    return code;
}

/* ============================================================================
 * Accessors
 * ========================================================================== */

rb_platform_state *rb_app_platform(rb_app *app) {
    RB_ASSERT(app != NULL);
    return app->platform;
}

rb_window rb_app_window(const rb_app *app) {
    RB_ASSERT(app != NULL);
    return app->window;
}

rb_renderer *rb_app_renderer(rb_app *app) {
    RB_ASSERT(app != NULL);
    return app->renderer;
}

rb_arena *rb_app_persistent_arena(rb_app *app) {
    RB_ASSERT(app != NULL);
    return &app->persistent;
}

rb_arena *rb_app_frame_arena(rb_app *app) {
    RB_ASSERT(app != NULL);
    return &app->frame;
}

/* The heap today; the game does not depend on it, so the source can change (ADR 0012, D1). */
rb_allocator rb_app_allocator(rb_app *app) {
    RB_ASSERT(app != NULL);
    return rb_heap_allocator();
}

void rb_app_quit(rb_app *app) {
    RB_ASSERT(app != NULL);
    app_request_quit(app, APP_QUIT_GAME_REQUESTED);
}
