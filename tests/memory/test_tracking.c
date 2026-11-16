/*
 * Allocation tracking by tag (ADR 0012). Each test runs in its own process
 * (ADR 0004) but resets the table anyway: exact counters must not depend on
 * what ran before in the same process.
 */
#include "rb_test.h"

#include <reborn/log.h>
#include <reborn/memory.h>

#include <string.h>

#include "failing_allocator.h"
#include "memory/memory_internal.h"

/* ============================================================================
 * Tags
 * ========================================================================== */

RB_TEST(memory_tracking, tags_have_names) {
    RB_EXPECT_EQ_STR(rb_memory_tag_name(RB_MEMORY_TAG_GENERAL), "general");
    RB_EXPECT_EQ_STR(rb_memory_tag_name(RB_MEMORY_TAG_PLATFORM), "platform");
    RB_EXPECT_EQ_STR(rb_memory_tag_name(RB_MEMORY_TAG_GAME), "game");
    RB_EXPECT_EQ_STR(rb_memory_tag_name(RB_MEMORY_TAG_COUNT), "unknown");
    RB_EXPECT_EQ_STR(rb_memory_tag_name((rb_memory_tag)-1), "unknown");
}

/* ============================================================================
 * Counting
 * ========================================================================== */

RB_TEST(memory_tracking, alloc_and_free_are_counted_under_their_tag) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    void        *a = rb_alloc(heap, 100, 8, RB_MEMORY_TAG_GAME);
    void        *b = rb_alloc_uninit(heap, 28, 4, RB_MEMORY_TAG_GAME);
    RB_REQUIRE(a != NULL && b != NULL);

    rb_memory_stats game = rb_memory_get_stats(RB_MEMORY_TAG_GAME);
    RB_EXPECT_EQ_UINT(game.live_bytes, 128);
    RB_EXPECT_EQ_UINT(game.peak_bytes, 128);
    RB_EXPECT_EQ_UINT(game.live_blocks, 2);
    RB_EXPECT_EQ_UINT(game.alloc_count, 2);
    RB_EXPECT_EQ_UINT(game.free_count, 0);
    /* Other tags see nothing. */
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).alloc_count, 0);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_PLATFORM).alloc_count, 0);

    rb_free(heap, a, 100, 8, RB_MEMORY_TAG_GAME);
    game = rb_memory_get_stats(RB_MEMORY_TAG_GAME);
    RB_EXPECT_EQ_UINT(game.live_bytes, 28);
    RB_EXPECT_EQ_UINT(game.peak_bytes, 128);
    RB_EXPECT_EQ_UINT(game.live_blocks, 1);
    RB_EXPECT_EQ_UINT(game.free_count, 1);
    rb_free(heap, b, 28, 4, RB_MEMORY_TAG_GAME);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GAME).live_blocks, 0);
}

RB_TEST(memory_tracking, array_counts_its_total_size) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    u32         *items = RB_ALLOC_ARRAY(heap, u32, 10, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(items != NULL);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).live_bytes, 40);
    RB_FREE_ARRAY(heap, items, u32, 10, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).live_bytes, 0);
}

RB_TEST(memory_tracking, resize_moves_bytes_not_blocks) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    void        *ptr = rb_alloc(heap, 16, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    ptr = rb_resize(heap, ptr, 16, 256, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    ptr = rb_resize_uninit(heap, ptr, 256, 32, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);

    rb_memory_stats stats = rb_memory_get_stats(RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(stats.live_bytes, 32);
    RB_EXPECT_EQ_UINT(stats.peak_bytes, 256);
    RB_EXPECT_EQ_UINT(stats.live_blocks, 1);
    RB_EXPECT_EQ_UINT(stats.alloc_count, 1);
    rb_free(heap, ptr, 32, 8, RB_MEMORY_TAG_GENERAL);
}

RB_TEST(memory_tracking, resize_of_null_counts_as_an_allocation) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    void        *ptr = rb_resize(heap, NULL, 0, 64, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    rb_memory_stats stats = rb_memory_get_stats(RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(stats.live_blocks, 1);
    RB_EXPECT_EQ_UINT(stats.alloc_count, 1);
    RB_EXPECT_EQ_UINT(stats.live_bytes, 64);
    rb_free(heap, ptr, 64, 8, RB_MEMORY_TAG_GENERAL);
}

RB_TEST(memory_tracking, failures_and_null_frees_count_nothing) {
    rb_memory_stats_reset();
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 1);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    RB_EXPECT(rb_alloc(allocator, 64, 8, RB_MEMORY_TAG_GENERAL) == NULL);
    rb_free(allocator, NULL, 64, 8, RB_MEMORY_TAG_GENERAL);

    rb_memory_stats stats = rb_memory_get_stats(RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(stats.alloc_count, 0);
    RB_EXPECT_EQ_UINT(stats.free_count, 0);
    RB_EXPECT_EQ_UINT(stats.live_bytes, 0);
}

/* A failed grow leaves the block as it was: its bytes must stay counted as they were. */
RB_TEST(memory_tracking, failed_resize_keeps_the_old_size) {
    rb_memory_stats_reset();
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 2);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    void        *ptr = rb_alloc(allocator, 16, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT(rb_resize(allocator, ptr, 16, 64, 8, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).live_bytes, 16);
    rb_free(allocator, ptr, 16, 8, RB_MEMORY_TAG_GENERAL);
}

/* A wrapper forwards to its parent's vtable: one allocation, counted once. */
RB_TEST(memory_tracking, wrapped_allocator_is_counted_once) {
    rb_memory_stats_reset();
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    void        *ptr = rb_alloc(allocator, 64, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).alloc_count, 1);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).live_bytes, 64);
    rb_free(allocator, ptr, 64, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).free_count, 1);
}

/* An arena reclaims the block at reset, but the tag still holds it: the caller never freed it. */
RB_TEST(memory_tracking, arena_block_without_free_stays_live) {
    rb_memory_stats_reset();
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_alloc(rb_arena_allocator(&arena), 64, 8, RB_MEMORY_TAG_GENERAL) != NULL);
    rb_arena_reset(&arena);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_GENERAL).live_blocks, 1);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Report
 * ========================================================================== */

typedef struct report_log {
    u32  info_lines;
    u32  error_lines;
    char last_error[256];
} report_log;

static void capture_report(const rb_log_record *record, void *user) {
    report_log *log = user;
    if (strncmp(record->message, "Memory ", 7) != 0) {
        return;
    }
    if (record->level == RB_LOG_LEVEL_ERROR) {
        ++log->error_lines;
        strncpy(log->last_error, record->message, sizeof(log->last_error) - 1);
    } else if (record->level == RB_LOG_LEVEL_INFO) {
        ++log->info_lines;
    }
}

RB_TEST(memory_tracking, report_is_clean_when_everything_is_freed) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    rb_free(heap, rb_alloc(heap, 8, 8, RB_MEMORY_TAG_GAME), 8, 8, RB_MEMORY_TAG_GAME);

    report_log log = {0};
    RB_REQUIRE(rb_log_add_sink(capture_report, &log, RB_LOG_LEVEL_TRACE));
    bool clean = rb_memory_log_report();
    rb_log_remove_sink(capture_report, &log);
    RB_EXPECT(clean);
    /* One line for the tag that allocated, none for the others. */
    RB_EXPECT_EQ_UINT(log.info_lines, 1);
    RB_EXPECT_EQ_UINT(log.error_lines, 0);
}

RB_TEST(memory_tracking, report_names_the_leaking_tag) {
    rb_memory_stats_reset();
    rb_allocator heap = rb_heap_allocator();
    void        *leaked = rb_alloc(heap, 1536, 8, RB_MEMORY_TAG_GAME);
    RB_REQUIRE(leaked != NULL);

    report_log log = {0};
    RB_REQUIRE(rb_log_add_sink(capture_report, &log, RB_LOG_LEVEL_TRACE));
    bool clean = rb_memory_log_report();
    rb_log_remove_sink(capture_report, &log);
    RB_EXPECT(!clean);
    RB_EXPECT_EQ_UINT(log.error_lines, 1);
    RB_EXPECT_EQ_STR(log.last_error, "Memory leak (game): 1 block, 1.50 KiB never freed");
    rb_free(heap, leaked, 1536, 8, RB_MEMORY_TAG_GAME);
}

RB_TEST(memory_tracking, format_size_is_exact_when_round) {
    char buf[32];
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), 0), "0 bytes");
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), 1023), "1023 bytes");
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), RB_KIB(64)), "64 KiB");
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), RB_KIB(33) + 256), "33.25 KiB");
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), RB_MIB(256)), "256 MiB");
    RB_EXPECT_EQ_STR(rb_memory_format_size(buf, sizeof(buf), RB_GIB(1) + RB_MIB(512)), "1.50 GiB");
}
