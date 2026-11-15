/*
 * OS-independent part of the platform layer. Windows and real OS events are
 * tested later, once logging and the application lifecycle exist (ADR 0006).
 */
#include "rb_test.h"

#include <reborn/log.h>

#include "platform/platform_internal.h"

static rb_event make_close_event(u32 window_id) {
    return (rb_event){.type = RB_EVENT_WINDOW_CLOSE_REQUESTED, .window = {window_id}};
}

RB_TEST(platform, common_starts_with_no_windows_and_no_events) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));

    rb_event ev;
    RB_EXPECT(!rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&common.window_pool), 0);
    RB_EXPECT_EQ_UINT(common.dropped_events, 0);
}

RB_TEST(platform, events_come_out_in_order_with_their_window) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));

    for (u32 id = 1; id <= 3; ++id) {
        rb_event ev = make_close_event(id);
        rb_platform_common_push_event(&common, &ev);
    }
    for (u32 id = 1; id <= 3; ++id) {
        rb_event ev;
        RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
        RB_EXPECT_EQ_INT(ev.type, RB_EVENT_WINDOW_CLOSE_REQUESTED);
        RB_EXPECT_EQ_UINT(ev.window.id, id);
    }
}

RB_TEST(platform, full_queue_drops_newest_and_counts_it) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));

    for (u32 i = 0; i < RB_EVENT_QUEUE_CAPACITY + 2u; ++i) {
        rb_event ev = make_close_event(i);
        rb_platform_common_push_event(&common, &ev);
    }
    RB_EXPECT_EQ_UINT(common.dropped_events, 2);

    /* The oldest event survived: what was already queued keeps its order. */
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_UINT(ev.window.id, 0);
}

static void count_warnings(const rb_log_record *record, void *user) {
    if (record->level == RB_LOG_LEVEL_WARN) {
        ++*(u32 *)user;
    }
}

RB_TEST(platform, queue_overflow_warns_once_per_burst) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 warnings = 0;
    RB_REQUIRE(rb_log_add_sink(count_warnings, &warnings, RB_LOG_LEVEL_TRACE));

    rb_event ev = make_close_event(1);
    for (u32 i = 0; i < RB_EVENT_QUEUE_CAPACITY + 3u; ++i) {
        rb_platform_common_push_event(&common, &ev);
    }
    RB_EXPECT_EQ_UINT(warnings, 1);

    /* Room again: the next push succeeds and ends the burst; the next drop warns again. */
    rb_event out;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &out));
    rb_platform_common_push_event(&common, &ev);
    rb_platform_common_push_event(&common, &ev);
    RB_EXPECT_EQ_UINT(warnings, 2);
    RB_EXPECT_EQ_UINT(common.dropped_events, 4);

    rb_log_remove_sink(count_warnings, &warnings);
}
