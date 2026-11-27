/* Frame statistics (ADR 0025, B4), on made-up times. */
#include "rb_test.h"

#include <reborn/clock.h>

#include "app/frame_stats.h"

#define MS RB_NS_PER_MS

/* `count` frames of `interval` each, from `*now` on; returns whether the last closed a window. */
static bool add_frames(rb_frame_stats *stats, u64 *now, u32 count, rb_frame_sample sample) {
    bool closed = false;
    for (u32 i = 0; i < count; ++i) {
        *now += sample.interval_ns;
        closed = rb_frame_stats_add(stats, &sample, *now);
    }
    return closed;
}

RB_TEST(frame_stats, nothing_is_published_before_the_window_closes) {
    rb_frame_stats stats;
    u64            now = 1000;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    RB_EXPECT(!add_frames(&stats, &now, 10, (rb_frame_sample){.interval_ns = 16 * MS}));
    RB_EXPECT_EQ_UINT(stats.published.frames, 0);
    RB_EXPECT_NEAR(stats.published.fps, 0.0, 1e-12);
}

RB_TEST(frame_stats, a_window_of_even_frames) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    /* 100 frames of 10 ms, 2 of work (8 waiting), one step each. */
    rb_frame_sample sample = {.interval_ns = 10 * MS,
                              .wait_ns = 8 * MS,
                              .update_ns = MS / 2,
                              .render_ns = MS,
                              .steps = 1};
    RB_EXPECT(add_frames(&stats, &now, 100, sample));
    const rb_app_frame_stats *out = &stats.published;
    RB_EXPECT_NEAR(out->seconds, 1.0, 1e-9);
    RB_EXPECT_EQ_UINT(out->frames, 100);
    RB_EXPECT_EQ_UINT(out->steps, 100);
    RB_EXPECT_NEAR(out->fps, 100.0, 1e-9);
    RB_EXPECT_NEAR(out->steps_per_second, 100.0, 1e-9);
    RB_EXPECT_NEAR(out->frame_avg_ms, 10.0, 1e-9);
    RB_EXPECT_NEAR(out->frame_p99_ms, 10.0, 1e-9);
    RB_EXPECT_NEAR(out->frame_max_ms, 10.0, 1e-9);
    RB_EXPECT_NEAR(out->work_avg_ms, 2.0, 1e-9);
    RB_EXPECT_NEAR(out->work_max_ms, 2.0, 1e-9);
    RB_EXPECT_NEAR(out->update_avg_ms, 0.5, 1e-9);
    RB_EXPECT_NEAR(out->render_avg_ms, 1.0, 1e-9);
}

/* The example of the ADR: an average of about 16.7 ms hides a 33 ms frame; the maximum does not. */
RB_TEST(frame_stats, one_stutter_shows_in_the_maximum_and_at_60_frames_in_p99) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 33 * MS});
    RB_EXPECT(add_frames(&stats, &now, 59, (rb_frame_sample){.interval_ns = 164 * MS / 10}));
    const rb_app_frame_stats *out = &stats.published;
    RB_EXPECT_EQ_UINT(out->frames, 60);
    RB_EXPECT_NEAR(out->frame_avg_ms, (33.0 + 59 * 16.4) / 60.0, 1e-9);
    RB_EXPECT_NEAR(out->frame_max_ms, 33.0, 1e-9);
    /* Fewer than 100 frames: 99% of them means all of them. */
    RB_EXPECT_NEAR(out->frame_p99_ms, 33.0, 1e-9);
}

RB_TEST(frame_stats, p99_skips_the_worst_percent_with_many_frames) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    /* 1000 frames: the 10 worst may lie above the 99th percentile, the 11th may not. */
    add_frames(&stats, &now, 10, (rb_frame_sample){.interval_ns = 50 * MS / 1000});
    add_frames(&stats, &now, 980, (rb_frame_sample){.interval_ns = 950 * MS / 1000});
    add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 2 * MS});
    add_frames(&stats, &now, 9, (rb_frame_sample){.interval_ns = 7 * MS});
    /* Past the second only now: the window closes with the frame after. */
    RB_EXPECT(add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 7 * MS}));
    const rb_app_frame_stats *out = &stats.published;
    RB_EXPECT_EQ_UINT(out->frames, 1001);
    /* Rank 1001 - 10 = 991: the 2 ms frame, its bucket's upper edge 2.1 ms. */
    RB_EXPECT_NEAR(out->frame_p99_ms, 2.1, 1e-9);
    RB_EXPECT_NEAR(out->frame_max_ms, 7.0, 1e-9);
}

RB_TEST(frame_stats, p99_past_the_histogram_is_the_maximum) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 150 * MS});
    RB_EXPECT(add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 900 * MS}));
    RB_EXPECT_NEAR(stats.published.frame_p99_ms, 900.0, 1e-9);
}

RB_TEST(frame_stats, a_wait_longer_than_its_frame_is_no_work) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    RB_EXPECT(
        add_frames(&stats, &now, 1,
                   (rb_frame_sample){.interval_ns = RB_NS_PER_S, .wait_ns = 2 * RB_NS_PER_S}));
    RB_EXPECT_NEAR(stats.published.work_avg_ms, 0.0, 1e-12);
}

RB_TEST(frame_stats, each_window_starts_afresh) {
    rb_frame_stats stats;
    u64            now = 0;
    rb_frame_stats_init(&stats, RB_NS_PER_S, now);
    RB_EXPECT(add_frames(&stats, &now, 10, (rb_frame_sample){.interval_ns = 100 * MS}));
    RB_EXPECT_NEAR(stats.published.frame_max_ms, 100.0, 1e-9);

    /* The second window's frames alone: the first's maximum is gone. */
    RB_EXPECT(!add_frames(&stats, &now, 99, (rb_frame_sample){.interval_ns = 10 * MS}));
    RB_EXPECT_NEAR(stats.published.frame_max_ms, 100.0, 1e-9);
    RB_EXPECT(add_frames(&stats, &now, 1, (rb_frame_sample){.interval_ns = 10 * MS}));
    RB_EXPECT_EQ_UINT(stats.published.frames, 100);
    RB_EXPECT_NEAR(stats.published.frame_max_ms, 10.0, 1e-9);
    RB_EXPECT_NEAR(stats.published.seconds, 1.0, 1e-9);
}
