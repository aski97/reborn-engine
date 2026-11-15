#include "containers/ring.h"

void rb_ring_init(rb_ring *ring, u32 capacity) {
    ring->head = 0;
    ring->count = 0;
    ring->capacity = capacity;
}

bool rb_ring_push_index(rb_ring *ring, u32 *out_index) {
    if (ring->count == ring->capacity) {
        return false;
    }
    /* In 64 bits: head + count can exceed u32 when capacity is above 2^31. */
    *out_index = (u32)(((u64)ring->head + ring->count) % ring->capacity);
    ++ring->count;
    return true;
}

bool rb_ring_pop_index(rb_ring *ring, u32 *out_index) {
    if (ring->count == 0) {
        return false;
    }
    *out_index = ring->head;
    /* A compare instead of %: pop runs once per event, and head + 1 cannot overflow. */
    ring->head = (ring->head + 1u == ring->capacity) ? 0u : ring->head + 1u;
    --ring->count;
    return true;
}

bool rb_ring_newest_index(const rb_ring *ring, u32 *out_index) {
    if (ring->count == 0) {
        return false;
    }
    /* Same 64-bit sum as push: the newest is the slot before the next free one. */
    *out_index = (u32)(((u64)ring->head + ring->count - 1u) % ring->capacity);
    return true;
}
