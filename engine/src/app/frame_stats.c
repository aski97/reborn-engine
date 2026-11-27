/* Frame statistics (ADR 0025, B4). */
#include "app/frame_stats.h"

#include <reborn/assert.h>
#include <reborn/clock.h>

#include <string.h>

void rb_frame_stats_init(rb_frame_stats *stats, u64 window_ns, u64 now_ns) {
    RB_ASSERT(stats != NULL);
    RB_ASSERT(window_ns > 0);
    memset(stats, 0, sizeof(*stats));
    stats->window_ns = window_ns;
    stats->window_start_ns = now_ns;
}

static f64 frame_stats_ms(u64 ns) {
    return (f64)ns / (f64)RB_NS_PER_MS;
}

static f64 frame_stats_average_ms(u64 sum_ns, u32 frames) {
    return frames > 0 ? frame_stats_ms(sum_ns) / (f64)frames : 0.0;
}

/*
 * The interval that 99% of the frames do not exceed: walking the buckets from
 * the shortest, the first where the count reaches that share. A bucket stands
 * for its upper edge, which the exact maximum caps: at 60 frames the answer is
 * the worst frame, and it should read the same as the maximum.
 */
static u64 frame_stats_p99_ns(const rb_frame_stats *stats) {
    /* At least 99%: the frames minus at most 1% of them, rounded down. */
    u32 rank = stats->frames - stats->frames / 100;
    u32 counted = 0;
    for (u32 bucket = 0; bucket < RB_FRAME_STATS_BUCKETS; ++bucket) {
        counted += stats->histogram[bucket];
        if (counted >= rank) {
            u64 edge = (u64)(bucket + 1) * RB_FRAME_STATS_BUCKET_NS;
            return edge < stats->interval_max ? edge : stats->interval_max;
        }
    }
    /* In the overflow bucket: past the histogram's range, only the maximum is known. */
    return stats->interval_max;
}

static void frame_stats_publish(rb_frame_stats *stats, u64 now_ns) {
    rb_app_frame_stats *out = &stats->published;
    f64                 seconds = rb_time_to_seconds(now_ns - stats->window_start_ns);
    u32                 frames = stats->frames;
    *out = (rb_app_frame_stats){
        .seconds = seconds,
        .frames = frames,
        .steps = stats->steps,
        .fps = (f64)frames / seconds,
        .steps_per_second = (f64)stats->steps / seconds,
        .frame_avg_ms = frame_stats_average_ms(stats->interval_sum, frames),
        .frame_p99_ms = frame_stats_ms(frame_stats_p99_ns(stats)),
        .frame_max_ms = frame_stats_ms(stats->interval_max),
        .work_avg_ms = frame_stats_average_ms(stats->work_sum, frames),
        .work_max_ms = frame_stats_ms(stats->work_max),
        .update_avg_ms = frame_stats_average_ms(stats->update_sum, frames),
        .update_max_ms = frame_stats_ms(stats->update_max),
        .render_avg_ms = frame_stats_average_ms(stats->render_sum, frames),
        .render_max_ms = frame_stats_ms(stats->render_max),
    };
}

/* Adds `value` to a sum and raises the maximum. */
static void frame_stats_track(u64 *sum, u64 *max, u64 value) {
    *sum += value;
    if (value > *max) {
        *max = value;
    }
}

bool rb_frame_stats_add(rb_frame_stats *stats, const rb_frame_sample *sample, u64 now_ns) {
    RB_ASSERT(stats != NULL);
    RB_ASSERT(sample != NULL);
    /* A wait longer than its frame comes from clock granularity: no negative work. */
    u64 work = sample->interval_ns > sample->wait_ns ? sample->interval_ns - sample->wait_ns : 0;
    ++stats->frames;
    stats->steps += sample->steps;
    frame_stats_track(&stats->interval_sum, &stats->interval_max, sample->interval_ns);
    frame_stats_track(&stats->work_sum, &stats->work_max, work);
    frame_stats_track(&stats->update_sum, &stats->update_max, sample->update_ns);
    frame_stats_track(&stats->render_sum, &stats->render_max, sample->render_ns);
    u64 bucket = sample->interval_ns / RB_FRAME_STATS_BUCKET_NS;
    ++stats->histogram[bucket < RB_FRAME_STATS_BUCKETS ? bucket : RB_FRAME_STATS_BUCKETS];

    if (now_ns - stats->window_start_ns < stats->window_ns) {
        return false;
    }
    frame_stats_publish(stats, now_ns);
    rb_app_frame_stats published = stats->published;
    rb_frame_stats_init(stats, stats->window_ns, now_ns);
    stats->published = published;
    return true;
}
