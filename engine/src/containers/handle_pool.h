/*
 * Generational handle pool (ADR 0007).
 *
 * Hands out u32 ids that name a slot and detect stale use: every slot has a
 * generation that changes when it is freed, and an id is valid only while its
 * generation matches the slot's. The pool stores no user data: each system
 * keeps its own arrays indexed by rb_handle_index(id).
 */
#ifndef RB_CONTAINERS_HANDLE_POOL_H
#define RB_CONTAINERS_HANDLE_POOL_H

#include <reborn/defines.h>

/* ============================================================================
 * Id encoding
 * ========================================================================== */

/*
 * id = generation << 20 | index. 20 index bits are enough for entities;
 * 12 generation bits wrap after 4095 reuses, which is why exhausted slots are
 * retired instead of wrapping.
 */
#define RB_HANDLE_INDEX_BITS      20u
#define RB_HANDLE_GENERATION_BITS 12u
#define RB_HANDLE_INDEX_MASK      ((1u << RB_HANDLE_INDEX_BITS) - 1u)
#define RB_HANDLE_MAX_CAPACITY    (1u << RB_HANDLE_INDEX_BITS)
#define RB_HANDLE_MAX_GENERATION  ((1u << RB_HANDLE_GENERATION_BITS) - 1u)

/* Generations start at 1, so no live id is ever 0: zeroed handles are invalid. */
#define RB_HANDLE_INVALID 0u

RB_INLINE u32 rb_handle_index(u32 id) {
    return id & RB_HANDLE_INDEX_MASK;
}

RB_INLINE u32 rb_handle_generation(u32 id) {
    return id >> RB_HANDLE_INDEX_BITS;
}

/* ============================================================================
 * Pool
 * ========================================================================== */

typedef struct rb_handle_pool {
    /* Current generation of each slot; RB_HANDLE_MAX_GENERATION + 1 marks a retired slot. */
    u16 *generations;
    /* Stack of free slot indices: the top is at free_slots[free_count - 1]. */
    u32 *free_slots;
    u32  capacity;
    u32  free_count;
    u32  retired_count;
} rb_handle_pool;

/*
 * Both arrays must hold `capacity` elements and outlive the pool: the caller
 * owns the memory (ADR 0006, B2). Fails if capacity is 0 or above
 * RB_HANDLE_MAX_CAPACITY. Slots are handed out from index 0 upwards.
 */
bool rb_handle_pool_init(rb_handle_pool *pool, u16 *generations, u32 *free_slots, u32 capacity);

/* Returns RB_HANDLE_INVALID when every slot is in use or retired. */
u32 rb_handle_pool_alloc(rb_handle_pool *pool);

/* Returns false, and changes nothing, if the id is not live (stale, freed twice, garbage). */
bool rb_handle_pool_free(rb_handle_pool *pool, u32 id);

bool rb_handle_pool_valid(const rb_handle_pool *pool, u32 id);

/* Number of live ids. */
u32 rb_handle_pool_count(const rb_handle_pool *pool);

#endif /* RB_CONTAINERS_HANDLE_POOL_H */
