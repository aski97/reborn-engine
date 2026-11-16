#include "rb_test.h"

#include "failing_allocator.h"

#include <string.h>

/* ============================================================================
 * Failing the N-th allocation
 * ========================================================================== */

RB_TEST(failing_allocator, fails_only_the_nth_allocation) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 3);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    void        *ptrs[4];
    for (usize i = 0; i < RB_ARRAY_COUNT(ptrs); ++i) {
        ptrs[i] = rb_alloc(allocator, 8, 8, RB_MEMORY_TAG_GENERAL);
    }
    RB_EXPECT(ptrs[0] != NULL);
    RB_EXPECT(ptrs[1] != NULL);
    RB_EXPECT(ptrs[2] == NULL);
    RB_EXPECT(ptrs[3] != NULL);
    RB_EXPECT_EQ_UINT(failing.allocation_count, 4);
    RB_EXPECT_EQ_UINT(failing.failure_count, 1);
    for (usize i = 0; i < RB_ARRAY_COUNT(ptrs); ++i) {
        rb_free(allocator, ptrs[i], 8, 8, RB_MEMORY_TAG_GENERAL);
    }
}

RB_TEST(failing_allocator, zero_never_fails) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    for (u32 i = 0; i < 100; ++i) {
        void *ptr = rb_alloc(allocator, 8, 8, RB_MEMORY_TAG_GENERAL);
        RB_REQUIRE(ptr != NULL);
        rb_free(allocator, ptr, 8, 8, RB_MEMORY_TAG_GENERAL);
    }
    RB_EXPECT_EQ_UINT(failing.allocation_count, 100);
    RB_EXPECT_EQ_UINT(failing.failure_count, 0);
}

/* A growing resize may need new memory: it counts, and can fail, like an alloc. */
RB_TEST(failing_allocator, growing_resize_counts_as_an_allocation) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 2);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    u8          *ptr = rb_alloc(allocator, 4, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);

    RB_EXPECT(rb_resize(allocator, ptr, 4, 64, 8, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT_EQ_UINT(failing.allocation_count, 2);
    RB_EXPECT(memcmp(ptr, "abcd", 4) == 0);

    /* Shrinking is not counted. */
    u8 *shrunk = rb_resize(allocator, ptr, 4, 2, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(shrunk != NULL);
    RB_EXPECT_EQ_UINT(failing.allocation_count, 2);

    u8 *grown = rb_resize(allocator, shrunk, 2, 64, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(grown != NULL);
    RB_EXPECT_EQ_UINT(failing.allocation_count, 3);
    RB_EXPECT(memcmp(grown, "ab", 2) == 0);
    rb_free(allocator, grown, 64, 8, RB_MEMORY_TAG_GENERAL);
}

RB_TEST(failing_allocator, counts_live_bytes_and_frees) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    void        *a = rb_alloc(allocator, 10, 8, RB_MEMORY_TAG_GENERAL);
    void        *b = rb_alloc(allocator, 20, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(a != NULL && b != NULL);
    b = rb_resize(allocator, b, 20, 50, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(b != NULL);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 60);
    b = rb_resize(allocator, b, 50, 5, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(b != NULL);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 15);
    rb_free(allocator, a, 10, 8, RB_MEMORY_TAG_GENERAL);
    rb_free(allocator, b, 5, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
    RB_EXPECT_EQ_UINT(failing.free_count, 2);
    /* NULL never reaches the allocator. */
    rb_free(allocator, NULL, 8, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(failing.free_count, 2);
}

/* Memory comes from the parent: here an arena, whose position moves. */
RB_TEST(failing_allocator, forwards_to_the_parent) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_arena_allocator(&arena), 0);
    rb_allocator allocator = rb_failing_allocator_get(&failing);
    u8          *ptr = rb_alloc(allocator, 100, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT(ptr >= arena.base && ptr + 100 == arena.base + arena.pos);
    rb_free(allocator, ptr, 100, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT(arena.pos < 100);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Walking every error path
 *
 * The pattern the failing allocator exists for, on a small "system" that
 * allocates three blocks and must free what it got when one fails.
 * ========================================================================== */

typedef struct three_blocks {
    u8 *a;
    u8 *b;
    u8 *c;
} three_blocks;

static bool three_blocks_create(rb_allocator allocator, three_blocks *out) {
    *out = (three_blocks){0};
    out->a = rb_alloc(allocator, 16, 8, RB_MEMORY_TAG_GENERAL);
    out->b = rb_alloc(allocator, 32, 8, RB_MEMORY_TAG_GENERAL);
    out->c = rb_alloc(allocator, 64, 8, RB_MEMORY_TAG_GENERAL);
    if (out->a == NULL || out->b == NULL || out->c == NULL) {
        /* rb_free ignores NULL: one cleanup for every failure point. */
        rb_free(allocator, out->a, 16, 8, RB_MEMORY_TAG_GENERAL);
        rb_free(allocator, out->b, 32, 8, RB_MEMORY_TAG_GENERAL);
        rb_free(allocator, out->c, 64, 8, RB_MEMORY_TAG_GENERAL);
        return false;
    }
    return true;
}

static void three_blocks_destroy(rb_allocator allocator, three_blocks *blocks) {
    rb_free(allocator, blocks->a, 16, 8, RB_MEMORY_TAG_GENERAL);
    rb_free(allocator, blocks->b, 32, 8, RB_MEMORY_TAG_GENERAL);
    rb_free(allocator, blocks->c, 64, 8, RB_MEMORY_TAG_GENERAL);
}

RB_TEST(failing_allocator, walks_every_error_path) {
    u32 fail_at = 1;
    for (;; ++fail_at) {
        rb_failing_allocator failing;
        rb_failing_allocator_init(&failing, rb_heap_allocator(), fail_at);
        rb_allocator allocator = rb_failing_allocator_get(&failing);
        three_blocks blocks;
        if (!three_blocks_create(allocator, &blocks)) {
            RB_EXPECT_EQ_UINT(failing.failure_count, 1);
            RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
            continue;
        }
        RB_EXPECT_EQ_UINT(failing.failure_count, 0);
        three_blocks_destroy(allocator, &blocks);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
        break;
    }
    /* Failures at 1, 2 and 3; the 4th run passed every allocation. */
    RB_EXPECT_EQ_UINT(fail_at, 4);
}
