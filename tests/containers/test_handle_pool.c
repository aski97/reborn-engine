#include "rb_test.h"

#include "containers/handle_pool.h"

#define TEST_CAPACITY 4u

typedef struct test_pool {
    rb_handle_pool pool;
    u16            generations[TEST_CAPACITY];
    u32            free_slots[TEST_CAPACITY];
} test_pool;

static bool test_pool_init(test_pool *t, u32 capacity) {
    return rb_handle_pool_init(&t->pool, t->generations, t->free_slots, capacity);
}

/* ============================================================================
 * Init
 * ========================================================================== */

RB_TEST(handle_pool, init_rejects_zero_capacity) {
    test_pool t;
    RB_EXPECT(!test_pool_init(&t, 0));
}

RB_TEST(handle_pool, init_rejects_capacity_above_index_bits) {
    /* Rejected before the arrays are touched, so tiny arrays are safe here. */
    test_pool t;
    RB_EXPECT(!test_pool_init(&t, RB_HANDLE_MAX_CAPACITY + 1u));
}

RB_TEST(handle_pool, starts_empty) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&t.pool), 0);
}

/* ============================================================================
 * Alloc
 * ========================================================================== */

RB_TEST(handle_pool, alloc_hands_out_slots_from_zero) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    for (u32 i = 0; i < TEST_CAPACITY; ++i) {
        u32 id = rb_handle_pool_alloc(&t.pool);
        RB_EXPECT(id != RB_HANDLE_INVALID);
        RB_EXPECT_EQ_UINT(rb_handle_index(id), i);
        RB_EXPECT_EQ_UINT(rb_handle_generation(id), 1);
    }
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&t.pool), TEST_CAPACITY);
}

RB_TEST(handle_pool, alloc_fails_when_full) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    for (u32 i = 0; i < TEST_CAPACITY; ++i) {
        rb_handle_pool_alloc(&t.pool);
    }
    RB_EXPECT_EQ_UINT(rb_handle_pool_alloc(&t.pool), RB_HANDLE_INVALID);
}

/* ============================================================================
 * Validity
 * ========================================================================== */

RB_TEST(handle_pool, allocated_id_is_valid) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    u32 id = rb_handle_pool_alloc(&t.pool);
    RB_EXPECT(rb_handle_pool_valid(&t.pool, id));
}

RB_TEST(handle_pool, invalid_id_is_never_valid) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    rb_handle_pool_alloc(&t.pool); /* slot 0 is live: id 0 must still be rejected */
    RB_EXPECT(!rb_handle_pool_valid(&t.pool, RB_HANDLE_INVALID));
}

RB_TEST(handle_pool, out_of_range_index_is_invalid) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    u32 id = (1u << RB_HANDLE_INDEX_BITS) | TEST_CAPACITY; /* generation 1, index past the end */
    RB_EXPECT(!rb_handle_pool_valid(&t.pool, id));
}

/* ============================================================================
 * Free and reuse
 * ========================================================================== */

RB_TEST(handle_pool, freed_id_becomes_stale) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    u32 id = rb_handle_pool_alloc(&t.pool);
    RB_EXPECT(rb_handle_pool_free(&t.pool, id));
    RB_EXPECT(!rb_handle_pool_valid(&t.pool, id));
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&t.pool), 0);
}

RB_TEST(handle_pool, double_free_is_rejected) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    u32 id = rb_handle_pool_alloc(&t.pool);
    RB_REQUIRE(rb_handle_pool_free(&t.pool, id));
    RB_EXPECT(!rb_handle_pool_free(&t.pool, id));
    /* A second free must not push the slot twice: it would be handed out to two owners. */
    u32 a = rb_handle_pool_alloc(&t.pool);
    u32 b = rb_handle_pool_alloc(&t.pool);
    RB_EXPECT(rb_handle_index(a) != rb_handle_index(b));
}

RB_TEST(handle_pool, reused_slot_gets_new_generation) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, TEST_CAPACITY));
    u32 old_id = rb_handle_pool_alloc(&t.pool);
    RB_REQUIRE(rb_handle_pool_free(&t.pool, old_id));
    u32 new_id = rb_handle_pool_alloc(&t.pool);

    /* The pool is a stack, so the slot just freed comes back first. */
    RB_EXPECT_EQ_UINT(rb_handle_index(new_id), rb_handle_index(old_id));
    RB_EXPECT_EQ_UINT(rb_handle_generation(new_id), rb_handle_generation(old_id) + 1u);
    RB_EXPECT(rb_handle_pool_valid(&t.pool, new_id));
    RB_EXPECT(!rb_handle_pool_valid(&t.pool, old_id));
    /* The stale id must not be able to destroy the new owner's slot. */
    RB_EXPECT(!rb_handle_pool_free(&t.pool, old_id));
    RB_EXPECT(rb_handle_pool_valid(&t.pool, new_id));
}

/* ============================================================================
 * Retirement
 * ========================================================================== */

RB_TEST(handle_pool, slot_is_retired_when_generations_run_out) {
    test_pool t;
    RB_REQUIRE(test_pool_init(&t, 1));

    /* Generations 1..4095 are each handed out once. */
    u32 first_id = RB_HANDLE_INVALID;
    for (u32 gen = 1; gen <= RB_HANDLE_MAX_GENERATION; ++gen) {
        u32 id = rb_handle_pool_alloc(&t.pool);
        RB_REQUIRE_EQ_UINT(rb_handle_generation(id), gen);
        if (gen == 1) {
            first_id = id;
        }
        RB_REQUIRE(rb_handle_pool_free(&t.pool, id));
    }

    RB_EXPECT_EQ_UINT(rb_handle_pool_alloc(&t.pool), RB_HANDLE_INVALID);
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&t.pool), 0);
    /* Without retirement the slot would wrap to generation 1 and revive this id. */
    RB_EXPECT(!rb_handle_pool_valid(&t.pool, first_id));
}
