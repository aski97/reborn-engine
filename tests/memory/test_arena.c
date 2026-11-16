#include "rb_test.h"

#include <reborn/memory.h>

#include <string.h>

#include "memory/asan.h"
#include "platform/platform_os.h"

/* ============================================================================
 * Lifetime
 * ========================================================================== */

RB_TEST(arena, create_reserves_without_committing) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_GIB(1)));
    RB_EXPECT(arena.base != NULL);
    RB_EXPECT(arena.reserved >= RB_GIB(1));
    RB_EXPECT_EQ_UINT(arena.reserved % rb_platform_reserve_granularity(), 0);
    RB_EXPECT_EQ_UINT(arena.committed, 0);
    RB_EXPECT_EQ_UINT(arena.pos, 0);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, create_rounds_up_to_granularity) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, 1));
    RB_EXPECT_EQ_UINT(arena.reserved, rb_platform_reserve_granularity());
    rb_arena_destroy(&arena);
}

RB_TEST(arena, destroy_zeroes_the_arena) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_arena_push(&arena, 100, 8) != NULL);
    rb_arena_destroy(&arena);
    RB_EXPECT(arena.base == NULL);
    RB_EXPECT_EQ_UINT(arena.pos, 0);
    RB_EXPECT_EQ_UINT(arena.reserved, 0);
}

/* ============================================================================
 * Allocation
 * ========================================================================== */

RB_TEST(arena, push_respects_alignment) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    static const usize aligns[] = {1, 2, 4, 8, 16, 64, 4096};
    for (usize i = 0; i < RB_ARRAY_COUNT(aligns); ++i) {
        /* An odd size first, so the next start is misaligned unless the arena fixes it. */
        RB_REQUIRE(rb_arena_push(&arena, 3, 1) != NULL);
        void *ptr = rb_arena_push(&arena, 24, aligns[i]);
        RB_REQUIRE(ptr != NULL);
        RB_EXPECT_EQ_UINT((usize)ptr % aligns[i], 0);
    }
    rb_arena_destroy(&arena);
}

RB_TEST(arena, push_type_and_array_use_the_type_alignment) {
    typedef struct wide {
        u8  tag;
        u64 value;
    } wide;
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_arena_push(&arena, 1, 1) != NULL);
    wide *one = RB_ARENA_PUSH_TYPE(&arena, wide);
    RB_REQUIRE(one != NULL);
    RB_EXPECT_EQ_UINT((usize)one % _Alignof(wide), 0);
    wide *many = RB_ARENA_PUSH_ARRAY(&arena, wide, 10);
    RB_REQUIRE(many != NULL);
    RB_EXPECT_EQ_UINT((usize)many % _Alignof(wide), 0);
    /* The whole array is usable. */
    many[9].value = 42;
    RB_EXPECT_EQ_UINT(many[9].value, 42);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, allocations_do_not_overlap) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u8 *a = rb_arena_push(&arena, 10, 1);
    u8 *b = rb_arena_push(&arena, 10, 1);
    RB_REQUIRE(a != NULL && b != NULL);
    RB_EXPECT(b >= a + 10);
    rb_arena_destroy(&arena);
}

/* Reused memory is not zero by itself: the arena must clear it. */
RB_TEST(arena, push_zeroes_reused_memory) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u8 *first = rb_arena_push(&arena, 64, 1);
    RB_REQUIRE(first != NULL);
    for (usize i = 0; i < 64; ++i) {
        first[i] = 0xEE;
    }
    rb_arena_reset(&arena);
    u8 *second = rb_arena_push(&arena, 64, 1);
    RB_REQUIRE(second == first);
    bool all_zero = true;
    for (usize i = 0; i < 64; ++i) {
        all_zero = all_zero && second[i] == 0;
    }
    RB_EXPECT(all_zero);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, push_uninit_gives_writable_memory) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u32 *values = rb_arena_push_uninit(&arena, 100 * sizeof(u32), _Alignof(u32));
    RB_REQUIRE(values != NULL);
    values[0] = 1;
    values[99] = 2;
    RB_EXPECT_EQ_UINT(values[0] + values[99], 3);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, zero_size_push_gives_a_pointer) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_EXPECT(rb_arena_push(&arena, 0, 8) != NULL);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Commit
 * ========================================================================== */

RB_TEST(arena, commits_in_64_kib_steps) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_arena_push(&arena, 1, 1) != NULL);
    RB_EXPECT_EQ_UINT(arena.committed, RB_KIB(64));
    /* Just past the first step. */
    RB_REQUIRE(rb_arena_push(&arena, RB_KIB(64), 1) != NULL);
    RB_EXPECT_EQ_UINT(arena.committed, RB_KIB(128));
    rb_arena_destroy(&arena);
}

RB_TEST(arena, reset_keeps_memory_committed) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_arena_push(&arena, RB_KIB(200), 1) != NULL);
    usize committed = arena.committed;
    rb_arena_reset(&arena);
    RB_EXPECT_EQ_UINT(arena.pos, 0);
    RB_EXPECT_EQ_UINT(arena.committed, committed);
    rb_arena_destroy(&arena);
}

/* The last step is cut at the reservation: on Linux 3 pages, not a full 64 KiB. */
RB_TEST(arena, commit_stops_at_the_reservation) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, 3 * rb_platform_page_size()));
    RB_REQUIRE(rb_arena_push(&arena, arena.reserved - 64, 1) != NULL);
    RB_EXPECT_EQ_UINT(arena.committed, arena.reserved);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Failure
 * ========================================================================== */

RB_TEST(arena, push_past_the_reservation_fails) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_KIB(64)));
    RB_REQUIRE(rb_arena_push(&arena, 100, 1) != NULL);
    usize pos = arena.pos;
    RB_EXPECT(rb_arena_push(&arena, arena.reserved, 1) == NULL);
    /* A failed push leaves the arena as it was, and smaller pushes still work. */
    RB_EXPECT_EQ_UINT(arena.pos, pos);
    RB_EXPECT(rb_arena_push(&arena, 100, 1) != NULL);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, huge_size_fails_without_wrapping) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_KIB(64)));
    RB_EXPECT(rb_arena_push(&arena, SIZE_MAX, 1) == NULL);
    RB_EXPECT(rb_arena_push(&arena, SIZE_MAX - 8, 16) == NULL);
    rb_arena_destroy(&arena);
}

/* SIZE_MAX / 2 + 1 elements of 4 bytes would wrap to a tiny size. */
RB_TEST(arena, array_size_overflow_fails) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_KIB(64)));
    RB_EXPECT(rb_arena_push_array(&arena, 4, 4, SIZE_MAX / 2 + 1) == NULL);
    RB_EXPECT_EQ_UINT(arena.pos, 0);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, reservation_too_large_fails) {
    rb_arena arena;
    RB_EXPECT(!rb_arena_create(&arena, SIZE_MAX));
    RB_EXPECT(arena.base == NULL);
}

/* ============================================================================
 * Temp scopes
 * ========================================================================== */

RB_TEST(arena, temp_end_restores_the_position) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_REQUIRE(rb_arena_push(&arena, 10, 1) != NULL);
    usize         before = arena.pos;
    rb_arena_temp temp = rb_arena_temp_begin(&arena);
    RB_REQUIRE(rb_arena_push(&arena, 1000, 1) != NULL);
    rb_arena_temp_end(temp);
    RB_EXPECT_EQ_UINT(arena.pos, before);
    rb_arena_destroy(&arena);
}

RB_TEST(arena, temp_scopes_nest) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_arena_temp outer = rb_arena_temp_begin(&arena);
    RB_REQUIRE(rb_arena_push(&arena, 10, 1) != NULL);
    usize         middle = arena.pos;
    rb_arena_temp inner = rb_arena_temp_begin(&arena);
    RB_REQUIRE(rb_arena_push(&arena, 10, 1) != NULL);
    rb_arena_temp_end(inner);
    RB_EXPECT_EQ_UINT(arena.pos, middle);
    rb_arena_temp_end(outer);
    RB_EXPECT_EQ_UINT(arena.pos, 0);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Buffer arena
 * ========================================================================== */

RB_TEST(arena, buffer_arena_never_commits_nor_grows) {
    static u8 buffer[256];
    rb_arena  arena;
    rb_arena_create_from_buffer(&arena, buffer, sizeof(buffer));
    RB_EXPECT_EQ_UINT(arena.committed, sizeof(buffer));
    u8 *ptr = rb_arena_push(&arena, 100, 1);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT(ptr >= buffer && ptr + 100 <= buffer + sizeof(buffer));
    RB_EXPECT(rb_arena_push(&arena, 200, 1) == NULL);
    rb_arena_destroy(&arena);
    /* Still the caller's: usable after destroy (and not poisoned under ASan). */
    buffer[0] = 1;
    RB_EXPECT_EQ_UINT(buffer[0], 1);
}

/* Alignment is of the address: a buffer starting at an odd address must not matter. */
RB_TEST(arena, buffer_arena_aligns_addresses) {
    static u64 storage[32];
    rb_arena   arena;
    rb_arena_create_from_buffer(&arena, (u8 *)storage + 1, sizeof(storage) - 1);
    void *ptr = rb_arena_push(&arena, 8, 8);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT((usize)ptr % 8, 0);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * Allocator adapter
 * ========================================================================== */

RB_TEST(arena_allocator, alloc_comes_from_the_arena) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    RB_EXPECT(allocator.ctx == &arena);
    u8 *ptr = rb_alloc(allocator, 100, 16);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT((usize)ptr % 16, 0);
    RB_EXPECT(ptr >= arena.base && ptr + 100 == arena.base + arena.pos);
    rb_arena_destroy(&arena);
}

/* rb_alloc must clear memory reused after a reset, as rb_arena_push does. */
RB_TEST(arena_allocator, alloc_zeroes_reused_memory) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 64, 1);
    RB_REQUIRE(first != NULL);
    memset(first, 0xEE, 64);
    rb_arena_reset(&arena);
    u8 *second = rb_alloc(allocator, 64, 1);
    RB_REQUIRE(second == first);
    bool all_zero = true;
    for (usize i = 0; i < 64; ++i) {
        all_zero = all_zero && second[i] == 0;
    }
    RB_EXPECT(all_zero);
    rb_arena_destroy(&arena);
}

/* The space comes back: the same request gets the same address again. */
RB_TEST(arena_allocator, free_of_the_last_allocation_pops_it) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    RB_REQUIRE(rb_alloc(allocator, 10, 1) != NULL);
    usize before = arena.pos;
    u8   *last = rb_alloc(allocator, 100, 8);
    RB_REQUIRE(last != NULL);
    rb_free(allocator, last, 100, 8);
    RB_EXPECT(arena.pos >= before && arena.pos < before + 100);
    RB_EXPECT(rb_alloc(allocator, 100, 8) == last);
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator, free_of_another_allocation_keeps_the_position) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 100, 8);
    RB_REQUIRE(first != NULL);
    RB_REQUIRE(rb_alloc(allocator, 100, 8) != NULL);
    usize pos = arena.pos;
    rb_free(allocator, first, 100, 8);
    RB_EXPECT_EQ_UINT(arena.pos, pos);
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator, resize_of_the_last_allocation_grows_in_place) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *ptr = rb_alloc(allocator, 4, 8);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);
    u8 *grown = rb_resize(allocator, ptr, 4, 64, 8);
    RB_EXPECT(grown == ptr);
    RB_EXPECT(memcmp(ptr, "abcd", 4) == 0);
    RB_EXPECT(ptr + 64 == arena.base + arena.pos);
    RB_EXPECT_EQ_UINT(ptr[63], 0);
    rb_arena_destroy(&arena);
}

/* Growing in place past the committed memory commits more, as a push would. */
RB_TEST(arena_allocator, growing_in_place_commits) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *ptr = rb_alloc(allocator, 16, 8);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT_EQ_UINT(arena.committed, RB_KIB(64));
    u8 *grown = rb_resize(allocator, ptr, 16, RB_KIB(100), 8);
    RB_EXPECT(grown == ptr);
    RB_EXPECT_EQ_UINT(arena.committed, RB_KIB(128));
    ptr[RB_KIB(100) - 1] = 1;
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator, resize_of_another_allocation_copies) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 4, 8);
    u8          *second = rb_alloc(allocator, 4, 8);
    RB_REQUIRE(first != NULL && second != NULL);
    memcpy(first, "abcd", 4);
    u8 *moved = rb_resize(allocator, first, 4, 64, 8);
    RB_REQUIRE(moved != NULL);
    RB_EXPECT(moved > second);
    RB_EXPECT_EQ_UINT((usize)moved % 8, 0);
    RB_EXPECT(memcmp(moved, "abcd", 4) == 0);
    RB_EXPECT_EQ_UINT(moved[63], 0);
    rb_arena_destroy(&arena);
}

/* Shrinking never moves: the last block gives its tail back, another keeps it until reset. */
RB_TEST(arena_allocator, resize_shrinks_in_place) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 64, 8);
    u8          *last = rb_alloc(allocator, 64, 8);
    RB_REQUIRE(first != NULL && last != NULL);
    usize pos = arena.pos;
    RB_EXPECT(rb_resize(allocator, first, 64, 8, 8) == first);
    RB_EXPECT_EQ_UINT(arena.pos, pos);
    RB_EXPECT(rb_resize(allocator, last, 64, 8, 8) == last);
    RB_EXPECT(last + 8 == arena.base + arena.pos);
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator, resize_past_the_reservation_fails) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_KIB(64)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *ptr = rb_alloc(allocator, 4, 8);
    RB_REQUIRE(ptr != NULL);
    memcpy(ptr, "abcd", 4);
    usize pos = arena.pos;
    RB_EXPECT(rb_resize(allocator, ptr, 4, arena.reserved + 1, 8) == NULL);
    RB_EXPECT_EQ_UINT(arena.pos, pos);
    RB_EXPECT(memcmp(ptr, "abcd", 4) == 0);
    rb_arena_destroy(&arena);
}

/*
 * A block allocated before a temp scope is not popped inside it: the position
 * would go below the scope's start, and the scope's end would assert.
 */
RB_TEST(arena_allocator, free_does_not_pop_below_a_temp_scope) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *outer = rb_alloc(allocator, 100, 8);
    RB_REQUIRE(outer != NULL);
    rb_arena_temp temp = rb_arena_temp_begin(&arena);
    rb_free(allocator, outer, 100, 8);
    RB_EXPECT_EQ_UINT(arena.pos, temp.pos);
    /* A block of the scope itself is popped as usual. */
    u8 *inner = rb_alloc(allocator, 100, 8);
    RB_REQUIRE(inner != NULL);
    rb_free(allocator, inner, 100, 8);
    RB_EXPECT(rb_alloc(allocator, 100, 8) == inner);
    rb_arena_temp_end(temp);
    RB_EXPECT_EQ_UINT(arena.pos, temp.pos);
    rb_arena_destroy(&arena);
}

/* Growing in place would extend the outer block into the scope, which ends it. */
RB_TEST(arena_allocator, resize_does_not_grow_into_a_temp_scope) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *outer = rb_alloc(allocator, 16, 8);
    RB_REQUIRE(outer != NULL);
    rb_arena_temp temp = rb_arena_temp_begin(&arena);
    u8           *moved = rb_resize(allocator, outer, 16, 32, 8);
    RB_EXPECT(moved != NULL && moved != outer);
    rb_arena_temp_end(temp);
    RB_EXPECT(outer + 16 == arena.base + arena.pos);
    rb_arena_destroy(&arena);
}

/* Each scope restores the floor of the one around it; reset clears it. */
RB_TEST(arena_allocator, temp_scopes_restore_the_floor) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    RB_EXPECT_EQ_UINT(arena.floor, 0);
    rb_arena_temp outer = rb_arena_temp_begin(&arena);
    RB_REQUIRE(rb_arena_push(&arena, 10, 1) != NULL);
    rb_arena_temp inner = rb_arena_temp_begin(&arena);
    RB_EXPECT_EQ_UINT(arena.floor, inner.pos);
    rb_arena_temp_end(inner);
    RB_EXPECT_EQ_UINT(arena.floor, outer.pos);
    rb_arena_temp_end(outer);
    RB_EXPECT_EQ_UINT(arena.floor, 0);
    RB_REQUIRE(rb_arena_push(&arena, 10, 1) != NULL);
    (void)rb_arena_temp_begin(&arena);
    rb_arena_reset(&arena);
    RB_EXPECT_EQ_UINT(arena.floor, 0);
    rb_arena_destroy(&arena);
}

/* ============================================================================
 * ASan poisoning (ASan builds only)
 *
 * __asan_address_is_poisoned asks the shadow memory directly, so these check
 * what ASan would report without having to crash the test.
 * ========================================================================== */

#if RB_ASAN_ENABLED

RB_TEST(arena_asan, allocation_is_valid_and_its_neighbours_poisoned) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u8 *a = rb_arena_push(&arena, 13, 1);
    u8 *b = rb_arena_push(&arena, 13, 1);
    RB_REQUIRE(a != NULL && b != NULL);
    RB_EXPECT(!__asan_address_is_poisoned(a));
    RB_EXPECT(!__asan_address_is_poisoned(a + 12));
    /* One past the end of `a`: the red zone, before `b`. */
    RB_EXPECT(__asan_address_is_poisoned(a + 13));
    RB_EXPECT(__asan_address_is_poisoned(b - 1));
    /* One past the last allocation: the free tail. */
    RB_EXPECT(__asan_address_is_poisoned(b + 13));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_asan, reset_poisons_old_allocations) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u8 *a = rb_arena_push(&arena, 64, 8);
    RB_REQUIRE(a != NULL);
    rb_arena_reset(&arena);
    RB_EXPECT(__asan_address_is_poisoned(a));
    RB_EXPECT(__asan_address_is_poisoned(a + 63));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_asan, temp_end_poisons_only_the_scope) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    u8 *kept = rb_arena_push(&arena, 32, 8);
    RB_REQUIRE(kept != NULL);
    rb_arena_temp temp = rb_arena_temp_begin(&arena);
    u8           *scratch = rb_arena_push(&arena, 32, 8);
    RB_REQUIRE(scratch != NULL);
    rb_arena_temp_end(temp);
    RB_EXPECT(!__asan_address_is_poisoned(kept + 31));
    RB_EXPECT(__asan_address_is_poisoned(scratch));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_asan, buffer_is_unpoisoned_on_destroy) {
    static u8 buffer[128];
    rb_arena  arena;
    rb_arena_create_from_buffer(&arena, buffer, sizeof(buffer));
    RB_EXPECT(__asan_address_is_poisoned(buffer + 100));
    rb_arena_destroy(&arena);
    RB_EXPECT(!__asan_address_is_poisoned(buffer + 100));
}

RB_TEST(arena_allocator_asan, free_poisons_the_block) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 32, 8);
    u8          *last = rb_alloc(allocator, 32, 8);
    RB_REQUIRE(first != NULL && last != NULL);
    /* Not the last one: kept in place, but a use after free is reported. */
    rb_free(allocator, first, 32, 8);
    RB_EXPECT(__asan_address_is_poisoned(first));
    RB_EXPECT(__asan_address_is_poisoned(first + 31));
    /* The last one: popped, back to the poisoned free tail. */
    rb_free(allocator, last, 32, 8);
    RB_EXPECT(__asan_address_is_poisoned(last));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator_asan, shrink_poisons_the_tail) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *first = rb_alloc(allocator, 64, 8);
    u8          *last = rb_alloc(allocator, 64, 8);
    RB_REQUIRE(first != NULL && last != NULL);
    RB_REQUIRE(rb_resize(allocator, first, 64, 13, 8) == first);
    RB_REQUIRE(rb_resize(allocator, last, 64, 16, 8) == last);
    /* 13 bytes: the shadow can say "first 5 bytes of this granule are valid". */
    RB_EXPECT(!__asan_address_is_poisoned(first + 12));
    RB_EXPECT(__asan_address_is_poisoned(first + 13));
    RB_EXPECT(__asan_address_is_poisoned(first + 63));
    RB_EXPECT(!__asan_address_is_poisoned(last + 15));
    RB_EXPECT(__asan_address_is_poisoned(last + 16));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator_asan, grow_in_place_unpoisons_only_the_new_size) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *ptr = rb_alloc(allocator, 16, 8);
    RB_REQUIRE(ptr != NULL);
    RB_REQUIRE(rb_resize(allocator, ptr, 16, 40, 8) == ptr);
    RB_EXPECT(!__asan_address_is_poisoned(ptr + 39));
    RB_EXPECT(__asan_address_is_poisoned(ptr + 40));
    rb_arena_destroy(&arena);
}

RB_TEST(arena_allocator_asan, copy_poisons_the_old_block) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(1)));
    rb_allocator allocator = rb_arena_allocator(&arena);
    u8          *old = rb_alloc(allocator, 16, 8);
    RB_REQUIRE(old != NULL);
    RB_REQUIRE(rb_alloc(allocator, 16, 8) != NULL);
    u8 *moved = rb_resize(allocator, old, 16, 64, 8);
    RB_REQUIRE(moved != NULL && moved != old);
    RB_EXPECT(__asan_address_is_poisoned(old));
    RB_EXPECT(!__asan_address_is_poisoned(moved + 63));
    RB_EXPECT(__asan_address_is_poisoned(moved + 64));
    rb_arena_destroy(&arena);
}

#endif /* RB_ASAN_ENABLED */
