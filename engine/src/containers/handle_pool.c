#include "containers/handle_pool.h"

/* Stored in place of a generation: never equal to the 12-bit generation of any id. */
#define RB_HANDLE_RETIRED (RB_HANDLE_MAX_GENERATION + 1u)

bool rb_handle_pool_init(rb_handle_pool *pool, u16 *generations, u32 *free_slots, u32 capacity) {
    if (capacity == 0 || capacity > RB_HANDLE_MAX_CAPACITY) {
        return false;
    }

    pool->generations = generations;
    pool->free_slots = free_slots;
    pool->capacity = capacity;
    pool->free_count = capacity;
    pool->retired_count = 0;

    /*
     * Filled in reverse so that popping from the top yields 0, 1, 2...: the
     * first window gets slot 0, which keeps behaviour predictable in tests.
     */
    for (u32 i = 0; i < capacity; ++i) {
        generations[i] = 1;
        free_slots[i] = capacity - 1u - i;
    }
    return true;
}

u32 rb_handle_pool_alloc(rb_handle_pool *pool) {
    if (pool->free_count == 0) {
        return RB_HANDLE_INVALID;
    }
    u32 index = pool->free_slots[--pool->free_count];
    return ((u32)pool->generations[index] << RB_HANDLE_INDEX_BITS) | index;
}

bool rb_handle_pool_valid(const rb_handle_pool *pool, u32 id) {
    u32 index = rb_handle_index(id);
    /* id 0 needs no special case: its generation 0 never matches a slot. */
    return index < pool->capacity && pool->generations[index] == rb_handle_generation(id);
}

bool rb_handle_pool_free(rb_handle_pool *pool, u32 id) {
    if (!rb_handle_pool_valid(pool, id)) {
        return false;
    }

    u32 index = rb_handle_index(id);
    /* Bumping the generation is what turns every copy of this id into a stale one. */
    u32 next = (u32)pool->generations[index] + 1u;
    pool->generations[index] = (u16)next;

    if (next == RB_HANDLE_RETIRED) {
        /* Reusing it would restart at generation 1 and revive ids from 4095 reuses ago. */
        ++pool->retired_count;
    } else {
        pool->free_slots[pool->free_count++] = index;
    }
    return true;
}

u32 rb_handle_pool_count(const rb_handle_pool *pool) {
    return pool->capacity - pool->free_count - pool->retired_count;
}
