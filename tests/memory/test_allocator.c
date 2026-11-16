#include "rb_test.h"

#include <reborn/memory.h>

#include <string.h>

/* ============================================================================
 * Spy allocator
 *
 * Bump-allocates from a buffer filled with 0xCD and records every call, so the
 * tests see exactly what the helpers pass to an allocator and what they do to
 * its memory (zeroing) on their own.
 * ========================================================================== */

#define SPY_GARBAGE 0xCD

typedef struct spy_allocator {
    _Alignas(64) u8 buffer[1024];
    usize used;
    bool  fail;
    u32   alloc_calls;
    u32   resize_calls;
    u32   free_calls;
    usize last_size;
    usize last_align;
} spy_allocator;

static void *spy_alloc(void *ctx, usize size, usize align) {
    spy_allocator *spy = ctx;
    ++spy->alloc_calls;
    spy->last_size = size;
    spy->last_align = align;
    usize start = (spy->used + align - 1) & ~(align - 1);
    if (spy->fail || start + size > sizeof(spy->buffer)) {
        return NULL;
    }
    spy->used = start + size;
    return spy->buffer + start;
}

/* Always moves, like a heap that found no room in place. */
static void *spy_resize(void *ctx, void *ptr, usize old_size, usize new_size, usize align) {
    spy_allocator *spy = ctx;
    ++spy->resize_calls;
    if (spy->fail) {
        return NULL;
    }
    u8 *moved = spy_alloc(spy, new_size, align);
    --spy->alloc_calls;
    if (moved != NULL) {
        memcpy(moved, ptr, old_size < new_size ? old_size : new_size);
    }
    return moved;
}

static void spy_free(void *ctx, void *ptr, usize size, usize align) {
    RB_UNUSED(ptr);
    spy_allocator *spy = ctx;
    ++spy->free_calls;
    spy->last_size = size;
    spy->last_align = align;
}

static rb_allocator spy_init(spy_allocator *spy) {
    static const rb_allocator_vtable vtable = {
        .alloc = spy_alloc,
        .resize = spy_resize,
        .free = spy_free,
    };
    *spy = (spy_allocator){0};
    memset(spy->buffer, SPY_GARBAGE, sizeof(spy->buffer));
    return (rb_allocator){.ctx = spy, .vtable = &vtable};
}

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

RB_TEST(allocator, alloc_passes_size_and_alignment) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    void                *ptr = rb_alloc(allocator, 24, 16, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT(spy.alloc_calls, 1);
    RB_EXPECT_EQ_UINT(spy.last_size, 24);
    RB_EXPECT_EQ_UINT(spy.last_align, 16);
    RB_EXPECT_EQ_UINT((usize)ptr % 16, 0);
}

/* The spy hands out 0xCD bytes: zero afterwards means the helper cleared them. */
RB_TEST(allocator, alloc_zeroes_memory) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc(allocator, 100, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT(bytes_equal(ptr, 100, 0));
}

RB_TEST(allocator, alloc_uninit_leaves_memory_as_is) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc_uninit(allocator, 100, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT(bytes_equal(ptr, 100, SPY_GARBAGE));
}

RB_TEST(allocator, failed_alloc_returns_null) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    spy.fail = true;
    RB_EXPECT(rb_alloc(allocator, 100, 8, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT(rb_alloc_uninit(allocator, 100, 8, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT(rb_alloc_array(allocator, 4, 4, 10, RB_MEMORY_TAG_GENERAL) == NULL);
}

RB_TEST(allocator, typed_macros_use_the_type_size_and_alignment) {
    typedef struct wide {
        u8  tag;
        u64 value;
    } wide;
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);

    wide *one = RB_ALLOC_TYPE(allocator, wide, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(one != NULL);
    RB_EXPECT_EQ_UINT(spy.last_size, sizeof(wide));
    RB_EXPECT_EQ_UINT(spy.last_align, _Alignof(wide));
    RB_EXPECT_EQ_UINT(one->value, 0);

    wide *many = RB_ALLOC_ARRAY(allocator, wide, 10, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(many != NULL);
    RB_EXPECT_EQ_UINT(spy.last_size, 10 * sizeof(wide));
    RB_EXPECT_EQ_UINT(spy.last_align, _Alignof(wide));

    RB_FREE_ARRAY(allocator, many, wide, 10, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(spy.last_size, 10 * sizeof(wide));
    RB_FREE_TYPE(allocator, one, wide, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(spy.last_size, sizeof(wide));
    RB_EXPECT_EQ_UINT(spy.last_align, _Alignof(wide));
    RB_EXPECT_EQ_UINT(spy.free_calls, 2);
}

/* The overflowing count is caught before the allocator is ever asked. */
RB_TEST(allocator, array_size_overflow_fails) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    RB_EXPECT(rb_alloc_array(allocator, 4, 4, SIZE_MAX / 2 + 1, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT(RB_ALLOC_ARRAY(allocator, u64, SIZE_MAX / 8 + 1, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT_EQ_UINT(spy.alloc_calls, 0);
}

RB_TEST(allocator, empty_array_is_a_valid_allocation) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    RB_EXPECT(RB_ALLOC_ARRAY(allocator, u32, 0, RB_MEMORY_TAG_GENERAL) != NULL);
    RB_EXPECT_EQ_UINT(spy.last_size, 0);
}

/* ============================================================================
 * Resize
 * ========================================================================== */

RB_TEST(allocator, resize_keeps_contents_and_zeroes_grown_bytes) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc(allocator, 4, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);
    u8 *grown = rb_resize(allocator, ptr, 4, 16, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(grown != NULL);
    RB_EXPECT_EQ_UINT(spy.resize_calls, 1);
    RB_EXPECT(memcmp(grown, "abcd", 4) == 0);
    RB_EXPECT(bytes_equal(grown + 4, 12, 0));
}

RB_TEST(allocator, resize_uninit_leaves_grown_bytes_as_is) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc(allocator, 4, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    u8 *grown = rb_resize_uninit(allocator, ptr, 4, 16, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(grown != NULL);
    RB_EXPECT(bytes_equal(grown, 4, 0));
    RB_EXPECT(bytes_equal(grown + 4, 12, SPY_GARBAGE));
}

RB_TEST(allocator, shrinking_resize_keeps_the_prefix) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc(allocator, 8, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcdefgh", 8);
    u8 *shrunk = rb_resize(allocator, ptr, 8, 3, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(shrunk != NULL);
    RB_EXPECT(memcmp(shrunk, "abc", 3) == 0);
}

/* As realloc(NULL, n): an array can start empty and grow with the same call. */
RB_TEST(allocator, resize_of_null_allocates) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_resize(allocator, NULL, 0, 32, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT(spy.alloc_calls, 1);
    RB_EXPECT_EQ_UINT(spy.resize_calls, 0);
    RB_EXPECT(bytes_equal(ptr, 32, 0));
}

RB_TEST(allocator, failed_resize_keeps_the_old_block) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    u8                  *ptr = rb_alloc(allocator, 4, 1, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);
    spy.fail = true;
    RB_EXPECT(rb_resize(allocator, ptr, 4, 64, 1, RB_MEMORY_TAG_GENERAL) == NULL);
    RB_EXPECT(memcmp(ptr, "abcd", 4) == 0);
}

/* ============================================================================
 * Free
 * ========================================================================== */

RB_TEST(allocator, free_passes_size_and_alignment) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    void                *ptr = rb_alloc(allocator, 40, 8, RB_MEMORY_TAG_GENERAL);
    RB_REQUIRE(ptr != NULL);
    rb_free(allocator, ptr, 40, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(spy.free_calls, 1);
    RB_EXPECT_EQ_UINT(spy.last_size, 40);
    RB_EXPECT_EQ_UINT(spy.last_align, 8);
}

/* Cleanup code may free what a failed allocation returned. */
RB_TEST(allocator, free_of_null_does_nothing) {
    static spy_allocator spy;
    rb_allocator         allocator = spy_init(&spy);
    rb_free(allocator, NULL, 0, 1, RB_MEMORY_TAG_GENERAL);
    rb_free(allocator, NULL, 64, 8, RB_MEMORY_TAG_GENERAL);
    RB_EXPECT_EQ_UINT(spy.free_calls, 0);
}
