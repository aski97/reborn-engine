/*
 * Failing allocator (ADR 0010, G1): an rb_allocator that forwards to a parent
 * and fails one chosen allocation, so a test can walk every error path.
 *
 *   rb_failing_allocator failing;
 *   for (u32 n = 1;; ++n) {
 *       rb_failing_allocator_init(&failing, rb_heap_allocator(), n);
 *       bool ok = thing_create(rb_failing_allocator_get(&failing), &thing);
 *       if (!ok) {
 *           RB_EXPECT_EQ_UINT(failing.live_bytes, 0);   // nothing leaked on failure
 *           continue;
 *       }
 *       thing_destroy(&thing);
 *       break;                                          // n passed every allocation
 *   }
 *
 * Counted as allocations: every alloc, and every resize that grows (it may
 * need new memory, so it can fail like an alloc). Shrinking resizes and frees
 * are forwarded, never failed nor counted as allocations.
 */
#ifndef RB_TEST_FAILING_ALLOCATOR_H
#define RB_TEST_FAILING_ALLOCATOR_H

#include <reborn/memory.h>

typedef struct rb_failing_allocator {
    rb_allocator parent;
    u32          fail_at; /* 1-based index of the allocation that fails; 0: none */
    /* Counters, read by the test. */
    u32   allocation_count; /* allocations attempted, the failed one included */
    u32   failure_count;
    u32   free_count;
    usize live_bytes; /* allocated minus freed, as the caller reported the sizes */
} rb_failing_allocator;

/* Counters start at zero. The parent's state must outlive `failing`. */
void rb_failing_allocator_init(rb_failing_allocator *failing, rb_allocator parent, u32 fail_at);

/* `failing` must outlive the returned allocator. */
rb_allocator rb_failing_allocator_get(rb_failing_allocator *failing);

#endif /* RB_TEST_FAILING_ALLOCATOR_H */
