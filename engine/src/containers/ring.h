/*
 * Ring buffer index logic (ADR 0007).
 *
 * A FIFO that never touches elements: push tells where to write, pop where to
 * read, and the caller owns a typed array of `capacity` elements. One ring can
 * thus serve events, jobs or log lines without macros or void pointers.
 *
 *   rb_event events[256];
 *   rb_ring  ring;
 *   rb_ring_init(&ring, 256);
 *
 *   u32 i;
 *   if (rb_ring_push_index(&ring, &i)) events[i] = ev;
 *   while (rb_ring_pop_index(&ring, &i)) handle(&events[i]);
 */
#ifndef RB_CONTAINERS_RING_H
#define RB_CONTAINERS_RING_H

#include <reborn/defines.h>

typedef struct rb_ring {
    u32 head;  /* slot of the oldest element */
    u32 count; /* a count, not a tail index: full and empty would both be head == tail */
    u32 capacity;
} rb_ring;

/* Any capacity works, not only powers of two. A capacity of 0 is always full and empty. */
void rb_ring_init(rb_ring *ring, u32 capacity);

/* On success *out_index is the slot to write the new element to. False if full. */
bool rb_ring_push_index(rb_ring *ring, u32 *out_index);

/*
 * On success *out_index is the slot of the oldest element, which the caller
 * reads before the next push may overwrite it. False if empty.
 */
bool rb_ring_pop_index(rb_ring *ring, u32 *out_index);

/*
 * On success *out_index is the slot of the most recently pushed element, which
 * stays queued. Lets a producer merge a new element into the last one instead
 * of pushing (e.g. consecutive resize events). False if empty.
 */
bool rb_ring_newest_index(const rb_ring *ring, u32 *out_index);

#endif /* RB_CONTAINERS_RING_H */
