/*
 * The engine's statistics commands (ADR 0025): the numbers the sandbox's
 * overlay shows, as JSON for agents.
 */
#include <reborn/assert.h>
#include <reborn/memory.h>
#include <reborn/renderer.h>

#include "app/app_state.h"

/* `"key":{"avg":...,"max":...}`: the shape of every timed part of the frame. */
static void reply_avg_max(rb_command_reply *reply, const char *key, f64 avg, f64 max) {
    rb_command_reply_begin_object(reply, key);
    rb_command_reply_f64(reply, "avg", avg);
    rb_command_reply_f64(reply, "max", max);
    rb_command_reply_end_object(reply);
}

static void cmd_stats(void *state, rb_app *app, const rb_command_args *args,
                      rb_command_reply *reply) {
    (void)state;
    (void)args;
    rb_app_frame_stats stats = rb_app_get_frame_stats(app);
    rb_command_reply_f64(reply, "seconds", stats.seconds);
    rb_command_reply_u64(reply, "frames", stats.frames);
    rb_command_reply_u64(reply, "steps", stats.steps);
    rb_command_reply_f64(reply, "fps", stats.fps);
    rb_command_reply_f64(reply, "steps_per_second", stats.steps_per_second);
    rb_command_reply_begin_object(reply, "frame_ms");
    rb_command_reply_f64(reply, "avg", stats.frame_avg_ms);
    rb_command_reply_f64(reply, "p99", stats.frame_p99_ms);
    rb_command_reply_f64(reply, "max", stats.frame_max_ms);
    rb_command_reply_end_object(reply);
    reply_avg_max(reply, "work_ms", stats.work_avg_ms, stats.work_max_ms);
    reply_avg_max(reply, "update_ms", stats.update_avg_ms, stats.update_max_ms);
    reply_avg_max(reply, "render_ms", stats.render_avg_ms, stats.render_max_ms);
}

static void reply_arena(rb_command_reply *reply, const char *key, const rb_arena *arena) {
    rb_command_reply_begin_object(reply, key);
    rb_command_reply_u64(reply, "used", arena->pos);
    rb_command_reply_u64(reply, "peak", arena->peak);
    rb_command_reply_u64(reply, "committed", arena->committed);
    rb_command_reply_u64(reply, "reserved", arena->reserved);
    rb_command_reply_end_object(reply);
}

static void cmd_memory_stats(void *state, rb_app *app, const rb_command_args *args,
                             rb_command_reply *reply) {
    (void)state;
    (void)args;
    rb_command_reply_begin_object(reply, "tags");
    for (u32 tag = 0; tag < RB_MEMORY_TAG_COUNT; ++tag) {
        rb_memory_stats stats = rb_memory_get_stats((rb_memory_tag)tag);
        rb_command_reply_begin_object(reply, rb_memory_tag_name((rb_memory_tag)tag));
        rb_command_reply_u64(reply, "live", stats.live_bytes);
        rb_command_reply_u64(reply, "peak", stats.peak_bytes);
        rb_command_reply_u64(reply, "blocks", stats.live_blocks);
        rb_command_reply_end_object(reply);
    }
    rb_command_reply_end_object(reply);
    rb_command_reply_begin_object(reply, "arenas");
    reply_arena(reply, "persistent", &app->persistent);
    reply_arena(reply, "frame", &app->frame);
    rb_command_reply_end_object(reply);
    rb_process_memory process;
    if (rb_process_get_memory(&process)) {
        rb_command_reply_begin_object(reply, "process");
        rb_command_reply_u64(reply, "resident", process.resident_bytes);
        rb_command_reply_u64(reply, "peak_resident", process.peak_resident_bytes);
        rb_command_reply_end_object(reply);
    }
}

static void cmd_renderer_info(void *state, rb_app *app, const rb_command_args *args,
                              rb_command_reply *reply) {
    (void)state;
    (void)args;
    rb_renderer_info info = rb_renderer_get_info(app->renderer);
    rb_command_reply_begin_object(reply, "gpu");
    rb_command_reply_str(reply, "name", info.gpu_name);
    rb_command_reply_str(reply, "type", info.gpu_type);
    rb_command_reply_end_object(reply);
    rb_command_reply_str(reply, "present_mode", info.present_mode);
    rb_command_reply_bool(reply, "vsync", info.vsync);
    rb_command_reply_begin_object(reply, "swapchain");
    rb_command_reply_u64(reply, "width", info.width);
    rb_command_reply_u64(reply, "height", info.height);
    rb_command_reply_u64(reply, "images", info.image_count);
    rb_command_reply_end_object(reply);
    rb_renderer_validation_stats validation = rb_renderer_get_validation_stats(app->renderer);
    rb_command_reply_begin_object(reply, "validation");
    rb_command_reply_bool(reply, "enabled", validation.enabled);
    rb_command_reply_u64(reply, "errors", validation.errors);
    rb_command_reply_u64(reply, "warnings", validation.warnings);
    rb_command_reply_end_object(reply);
}

static const rb_command_desc APP_STATS_COMMANDS[] = {
    {"stats", NULL, "frame times, frames and steps per second over the last second", 0, 0,
     cmd_stats},
    {"memory.stats", NULL, "memory by tag, the arenas, the process", 0, 0, cmd_memory_stats},
    {"renderer.info", NULL, "GPU, present mode, swapchain, validation counts", 0, 0,
     cmd_renderer_info},
};

void rb_app_stats_commands_add(rb_app *app) {
    for (u32 i = 0; i < RB_ARRAY_COUNT(APP_STATS_COMMANDS); ++i) {
        bool added = rb_commands_add(app->commands, &APP_STATS_COMMANDS[i], false);
        RB_ASSERT(added);
        (void)added;
    }
}
