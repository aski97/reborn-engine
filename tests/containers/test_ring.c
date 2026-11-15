#include "rb_test.h"

#include "containers/ring.h"

/* ============================================================================
 * Empty and full
 * ========================================================================== */

RB_TEST(ring, pop_fails_when_empty) {
    rb_ring ring;
    rb_ring_init(&ring, 4);
    u32 index = 0;
    RB_EXPECT(!rb_ring_pop_index(&ring, &index));
}

RB_TEST(ring, push_fails_when_full) {
    rb_ring ring;
    rb_ring_init(&ring, 4);
    u32 index = 0;
    for (u32 i = 0; i < 4; ++i) {
        RB_REQUIRE(rb_ring_push_index(&ring, &index));
    }
    RB_EXPECT(!rb_ring_push_index(&ring, &index));
    RB_EXPECT_EQ_UINT(ring.count, 4);
}

RB_TEST(ring, zero_capacity_is_full_and_empty) {
    rb_ring ring;
    rb_ring_init(&ring, 0);
    u32 index = 0;
    RB_EXPECT(!rb_ring_push_index(&ring, &index));
    RB_EXPECT(!rb_ring_pop_index(&ring, &index));
}

/* ============================================================================
 * Order and wrap-around
 * ========================================================================== */

RB_TEST(ring, pops_in_push_order) {
    rb_ring ring;
    rb_ring_init(&ring, 4);
    u32 index = 0;
    for (u32 i = 0; i < 3; ++i) {
        RB_REQUIRE(rb_ring_push_index(&ring, &index));
        RB_EXPECT_EQ_UINT(index, i);
    }
    for (u32 i = 0; i < 3; ++i) {
        RB_REQUIRE(rb_ring_pop_index(&ring, &index));
        RB_EXPECT_EQ_UINT(index, i);
    }
}

RB_TEST(ring, indices_wrap_around) {
    rb_ring ring;
    rb_ring_init(&ring, 4);
    u32 index = 0;
    for (u32 i = 0; i < 3; ++i) {
        RB_REQUIRE(rb_ring_push_index(&ring, &index)); /* slots 0, 1, 2 */
    }
    for (u32 i = 0; i < 2; ++i) {
        RB_REQUIRE(rb_ring_pop_index(&ring, &index)); /* frees 0, 1 */
    }

    const u32 expected[] = {3, 0, 1};
    for (u32 i = 0; i < RB_ARRAY_COUNT(expected); ++i) {
        RB_REQUIRE(rb_ring_push_index(&ring, &index));
        RB_EXPECT_EQ_UINT(index, expected[i]);
    }
    RB_EXPECT(!rb_ring_push_index(&ring, &index));
}

/* The way the ring is meant to be used: a typed array next to it. */
RB_TEST(ring, keeps_fifo_order_of_values_across_wrap) {
    rb_ring ring;
    i32     values[3];
    rb_ring_init(&ring, RB_ARRAY_COUNT(values));

    i32 next_in = 0;
    i32 next_out = 0;
    u32 index = 0;
    /* Pushing 2 and popping 1 per round walks head and tail around the array several times. */
    for (u32 round = 0; round < 10; ++round) {
        while (ring.count < ring.capacity && rb_ring_push_index(&ring, &index)) {
            values[index] = next_in++;
        }
        RB_REQUIRE(rb_ring_pop_index(&ring, &index));
        RB_EXPECT_EQ_INT(values[index], next_out++);
    }
    while (rb_ring_pop_index(&ring, &index)) {
        RB_EXPECT_EQ_INT(values[index], next_out++);
    }
    RB_EXPECT_EQ_INT(next_out, next_in);
}
