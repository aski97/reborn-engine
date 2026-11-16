/*
 * OS-independent part of the platform layer: event queue and window state.
 * Windows and real OS events are tested later, once the application lifecycle
 * exists (ADR 0006).
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

/* ============================================================================
 * Window state
 * ========================================================================== */

static rb_window_size make_size(u32 width, u32 height) {
    return (rb_window_size){width, height, width * 2u, height * 2u};
}

/* A live window in `common`, as a backend would open it, at (100, 50). */
static u32 open_window(rb_platform_common *common, rb_window_size size) {
    u32 id = rb_handle_pool_alloc(&common->window_pool);
    rb_platform_common_window_opened(common, id, size, (rb_window_position){100, 50});
    return id;
}

static u32 count_events(rb_platform_common *common) {
    u32      count = 0;
    rb_event ev;
    while (rb_platform_common_poll_event(common, &ev)) {
        ++count;
    }
    return count;
}

RB_TEST(platform, opened_window_has_its_size_and_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(1280, 720));

    const rb_window_state *ws = rb_platform_common_window_state(&common, id);
    RB_REQUIRE(ws != NULL);
    RB_EXPECT_EQ_UINT(ws->size.width, 1280);
    RB_EXPECT_EQ_UINT(ws->size.pixel_height, 1440);
    RB_EXPECT_EQ_INT(ws->position.x, 100);
    RB_EXPECT_EQ_INT(ws->position.y, 50);
    RB_EXPECT(!ws->focused);
    RB_EXPECT(!ws->minimized);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(platform, stale_handle_has_no_state) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));
    RB_REQUIRE(rb_handle_pool_free(&common.window_pool, id));

    RB_EXPECT(rb_platform_common_window_state(&common, id) == NULL);
    RB_EXPECT(rb_platform_common_window_state(&common, RB_HANDLE_INVALID) == NULL);
}

RB_TEST(platform, resize_sends_event_with_new_size) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    rb_platform_common_window_resized(&common, id, make_size(800, 600));
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_WINDOW_RESIZED);
    RB_EXPECT_EQ_UINT(ev.window.id, id);
    RB_EXPECT_EQ_UINT(ev.resize.width, 800);
    RB_EXPECT_EQ_UINT(ev.resize.pixel_height, 1200);
    RB_EXPECT_EQ_UINT(rb_platform_common_window_state(&common, id)->size.height, 600);
}

RB_TEST(platform, unchanged_size_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    /* What Win32 does on ShowWindow and X11 on a move. */
    rb_platform_common_window_resized(&common, id, make_size(640, 480));
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(platform, pixel_only_change_is_a_resize) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    /* A move to a monitor with another DPI: same logical size, more pixels. */
    rb_platform_common_window_resized(&common, id, (rb_window_size){640, 480, 960, 720});
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_WINDOW_RESIZED);
    RB_EXPECT_EQ_UINT(ev.resize.pixel_width, 960);
}

RB_TEST(platform, consecutive_resizes_merge_into_the_newest) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    /* A drag: far more resizes than the queue holds. */
    for (u32 i = 1; i <= RB_EVENT_QUEUE_CAPACITY * 2u; ++i) {
        rb_platform_common_window_resized(&common, id, make_size(640 + i, 480));
    }
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_UINT(ev.resize.width, 640 + RB_EVENT_QUEUE_CAPACITY * 2u);
    RB_EXPECT(!rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_UINT(common.dropped_events, 0);
}

RB_TEST(platform, resizes_do_not_merge_across_windows_or_other_events) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 a = open_window(&common, make_size(640, 480));
    u32 b = open_window(&common, make_size(640, 480));

    rb_platform_common_window_resized(&common, a, make_size(700, 480));
    rb_platform_common_window_resized(&common, b, make_size(700, 480)); /* other window */
    rb_platform_common_window_resized(&common, b, make_size(710, 480)); /* merges */
    rb_platform_common_window_focused(&common, b, true);
    rb_platform_common_window_resized(&common, b, make_size(720, 480)); /* after focus */

    const rb_event_type types[] = {RB_EVENT_WINDOW_RESIZED, RB_EVENT_WINDOW_RESIZED,
                                   RB_EVENT_WINDOW_FOCUS_GAINED, RB_EVENT_WINDOW_RESIZED};
    const u32           windows[] = {a, b, b, b};
    const u32           widths[] = {700, 710, 0, 720};
    for (u32 i = 0; i < RB_ARRAY_COUNT(types); ++i) {
        rb_event ev;
        RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
        RB_EXPECT_EQ_INT(ev.type, types[i]);
        RB_EXPECT_EQ_UINT(ev.window.id, windows[i]);
        if (ev.type == RB_EVENT_WINDOW_RESIZED) {
            RB_EXPECT_EQ_UINT(ev.resize.width, widths[i]);
        }
    }
    rb_event ev;
    RB_EXPECT(!rb_platform_common_poll_event(&common, &ev));
}

RB_TEST(platform, move_sends_event_only_on_change) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    rb_platform_common_window_moved(&common, id, (rb_window_position){100, 50}); /* unchanged */
    RB_EXPECT_EQ_UINT(count_events(&common), 0);

    /* A monitor left of the primary one: negative coordinates are valid. */
    rb_platform_common_window_moved(&common, id, (rb_window_position){-1200, 30});
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_WINDOW_MOVED);
    RB_EXPECT_EQ_UINT(ev.window.id, id);
    RB_EXPECT_EQ_INT(ev.position.x, -1200);
    RB_EXPECT_EQ_INT(ev.position.y, 30);
    RB_EXPECT_EQ_INT(rb_platform_common_window_state(&common, id)->position.x, -1200);
}

RB_TEST(platform, consecutive_moves_merge_into_the_newest) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    /* A drag on X11: one move per mouse move. */
    for (i32 i = 1; i <= (i32)RB_EVENT_QUEUE_CAPACITY * 2; ++i) {
        rb_platform_common_window_moved(&common, id, (rb_window_position){100 + i, 50});
    }
    rb_event ev;
    RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_WINDOW_MOVED);
    RB_EXPECT_EQ_INT(ev.position.x, 100 + (i32)RB_EVENT_QUEUE_CAPACITY * 2);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT_EQ_UINT(common.dropped_events, 0);
}

RB_TEST(platform, focus_and_minimize_send_events_only_on_change) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    rb_platform_common_window_focused(&common, id, false);   /* already unfocused */
    rb_platform_common_window_minimized(&common, id, false); /* already restored */
    RB_EXPECT_EQ_UINT(count_events(&common), 0);

    rb_platform_common_window_focused(&common, id, true);
    rb_platform_common_window_focused(&common, id, true);
    rb_platform_common_window_minimized(&common, id, true);
    rb_platform_common_window_minimized(&common, id, true);
    rb_platform_common_window_focused(&common, id, false);
    rb_platform_common_window_minimized(&common, id, false);

    const rb_event_type expected[] = {RB_EVENT_WINDOW_FOCUS_GAINED, RB_EVENT_WINDOW_MINIMIZED,
                                      RB_EVENT_WINDOW_FOCUS_LOST, RB_EVENT_WINDOW_RESTORED};
    for (u32 i = 0; i < RB_ARRAY_COUNT(expected); ++i) {
        rb_event ev;
        RB_REQUIRE(rb_platform_common_poll_event(&common, &ev));
        RB_EXPECT_EQ_INT(ev.type, expected[i]);
        RB_EXPECT_EQ_UINT(ev.window.id, id);
    }
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(platform, state_updates_even_when_queue_is_full) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_window(&common, make_size(640, 480));

    rb_event filler = make_close_event(id);
    for (u32 i = 0; i < RB_EVENT_QUEUE_CAPACITY; ++i) {
        rb_platform_common_push_event(&common, &filler);
    }
    rb_platform_common_window_resized(&common, id, make_size(800, 600));
    rb_platform_common_window_focused(&common, id, true);

    /* The events were dropped, the state was not: queries stay right. */
    RB_EXPECT_EQ_UINT(common.dropped_events, 2);
    const rb_window_state *ws = rb_platform_common_window_state(&common, id);
    RB_EXPECT_EQ_UINT(ws->size.width, 800);
    RB_EXPECT(ws->focused);
}
