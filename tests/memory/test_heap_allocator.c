#include "rb_test.h"

#include <reborn/memory.h>

#include <string.h>

#include "memory/asan.h"

/* Up to a page and beyond: GPU uploads and page-aligned buffers ask for that much. */
static const usize heap_aligns[] = {1, 2, 4, 8, 16, 32, 64, 256, 4096, 65536};

static bool bytes_equal(const u8 *bytes, usize count, u8 value) {
    for (usize i = 0; i < count; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Allocation
 * ========================================================================== */

RB_TEST(heap_allocator, is_stateless) {
    rb_allocator a = rb_heap_allocator();
    rb_allocator b = rb_heap_allocator();
    RB_EXPECT(a.vtable != NULL);
    RB_EXPECT(a.vtable == b.vtable);
    RB_EXPECT(a.ctx == NULL);
}

RB_TEST(heap_allocator, alloc_respects_alignment) {
    rb_allocator heap = rb_heap_allocator();
    for (usize i = 0; i < RB_ARRAY_COUNT(heap_aligns); ++i) {
        u8 *ptr = rb_alloc(heap, 24, heap_aligns[i], RB_MEMORY_TAG_GENERAL);
        RB_REQUIRE(ptr != NULL);
        RB_EXPECT_EQ_UINT((usize)ptr % heap_aligns[i], 0);
        RB_EXPECT(bytes_equal(ptr, 24, 0));
        rb_free(heap, ptr, 24, heap_aligns[i], RB_MEMORY_TAG_GENERAL);
    }
}

RB_TEST(heap_allocator, typed_array_is_usable) {
    rb_allocator heap = rb_heap_allocator();
    u64         *values = RB_ALLOC_ARRAY(heap, u64, 1000, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(values != NULL);
    values[0] = 1;
    values[999] = 2;
    RB_EXPECT_EQ_UINT(values[0] + values[999], 3);
    RB_FREE_ARRAY(heap, values, u64, 1000, RB_MEMORY_TAG_GENERAL);
}

/* Size 0 is a real block, so freeing it is not a special case. */
RB_TEST(heap_allocator, zero_size_gives_a_freeable_pointer) {
    rb_allocator heap = rb_heap_allocator();
    void        *ptr = rb_alloc(heap, 0, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    rb_free(heap, ptr, 0, 8, RB_MEMORY_TAG_GENERAL);

    u8 *grown = rb_resize(heap, rb_alloc(heap, 0, 8, RB_MEMORY_TAG_GENERAL), 0, 16, 8,
                          RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(grown != NULL);
    RB_EXPECT(bytes_equal(grown, 16, 0));
    u8 *empty = rb_resize(heap, grown, 16, 0, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(empty != NULL);
    rb_free(heap, empty, 0, 8, RB_MEMORY_TAG_GENERAL);
}

/* ============================================================================
 * Resize
 * ========================================================================== */

/* Large enough to move on every heap; aligned above 16 takes Linux's copy path. */
RB_TEST(heap_allocator, resize_keeps_contents_and_alignment) {
    rb_allocator heap = rb_heap_allocator();
    for (usize i = 0; i < RB_ARRAY_COUNT(heap_aligns); ++i) {
        usize align = heap_aligns[i];
        u8   *ptr = rb_alloc(heap, 16, align, RB_MEMORY_TAG_GENERAL);
        RB_REQUIRE(ptr != NULL);
        memcpy(ptr, "0123456789abcdef", 16);

        u8 *grown = rb_resize(heap, ptr, 16, RB_KIB(256), align, RB_MEMORY_TAG_GENERAL);
        RB_REQUIRE(grown != NULL);
        RB_EXPECT_EQ_UINT((usize)grown % align, 0);
        RB_EXPECT(memcmp(grown, "0123456789abcdef", 16) == 0);
        RB_EXPECT(bytes_equal(grown + 16, RB_KIB(256) - 16, 0));

        u8 *shrunk = rb_resize(heap, grown, RB_KIB(256), 8, align, RB_MEMORY_TAG_GENERAL);
        RB_REQUIRE(shrunk != NULL);
        RB_EXPECT_EQ_UINT((usize)shrunk % align, 0);
        RB_EXPECT(memcmp(shrunk, "01234567", 8) == 0);
        rb_free(heap, shrunk, 8, align, RB_MEMORY_TAG_GENERAL);
    }
}

/* ============================================================================
 * Failure
 *
 * Not under ASan: its malloc aborts on an impossible size instead of returning
 * NULL (allocator_may_return_null=0 by default).
 * ========================================================================== */

#if !RB_ASAN_ENABLED

RB_TEST(heap_allocator, impossible_size_fails) {
    rb_allocator heap = rb_heap_allocator();
    RB_EXPECT(rb_alloc(heap, SIZE_MAX / 2, 16, RB_MEMORY_TAG_GENERAL) == NULL);
    u8 *ptr = rb_alloc(heap, 4, 16, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);
    RB_EXPECT(rb_resize(heap, ptr, 4, SIZE_MAX / 2, 16, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT(memcmp(ptr, "abcd", 4) == 0);
    rb_free(heap, ptr, 4, 16, RB_MEMORY_TAG_GENERAL);
}

#endif /* !RB_ASAN_ENABLED */
