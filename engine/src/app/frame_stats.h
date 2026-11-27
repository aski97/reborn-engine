/*
 * Frame statistics (ADR 0025): per-frame times summed over a window, published
 * when the window closes. Pure arithmetic on times passed in, so tests drive
 * it with made-up clocks, as with the step clock.
 *
 *   rb_frame_stats_init(&stats, RB_NS_PER_S, rb_time_now());
 *   // at the end of every frame
 *   rb_frame_stats_add(&stats, &sample, rb_time_now());
 *   stats.published   // the last closed window
 */
#ifndef RB_APP_FRAME_STATS_H
#define RB_APP_FRAME_STATS_H

#include <reborn/app.h>
#include <reborn/defines.h>

/* Interval histogram: 0.1 ms buckets up to 100 ms, then one for anything longer. */
#define RB_FRAME_STATS_BUCKET_NS 100000u
#define RB_FRAME_STATS_BUCKETS   1000u

/* One frame's times, in nanoseconds. */
typedef struct rb_frame_sample {
    u64 interval_ns; /* from this frame's start to the next one's */
    u64 wait_ns;     /* waiting for the GPU, the swapchain or the pacer: not work */
    u64 update_ns;   /* the steps: pump, events, update */
    u64 render_ns;   /* the game's render callback */
    u32 steps;       /* steps run in the frame */
} rb_frame_sample;

typedef struct rb_frame_stats {
    u64 window_ns;
    u64 window_start_ns;
    /* The open window's sums and maxima. */
    u32 frames;
    u32 steps;
    u64 interval_sum, interval_max;
    u64 work_sum, work_max;
    u64 update_sum, update_max;
    u64 render_sum, render_max;
    u32 histogram[RB_FRAME_STATS_BUCKETS + 1]; /* the last: longer than the range */
    /* The last closed window; zero before the first closes. */
    rb_app_frame_stats published;
} rb_frame_stats;

/* Opens the first window at `now_ns`. */
void rb_frame_stats_init(rb_frame_stats *stats, u64 window_ns, u64 now_ns);

/*
 * Adds a finished frame. When `now_ns` is a window or more past its start, the
 * window is published and a new one opens at `now_ns`: true then.
 */
bool rb_frame_stats_add(rb_frame_stats *stats, const rb_frame_sample *sample, u64 now_ns);

#endif /* RB_APP_FRAME_STATS_H */
