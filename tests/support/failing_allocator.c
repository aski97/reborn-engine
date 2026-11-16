#include "failing_allocator.h"

#include <reborn/assert.h>
#include <reborn/log.h>

/* Counts one allocation and says whether it is the one to fail. */
static bool failing_should_fail(rb_failing_allocator *failing) {
    ++failing->allocation_count;
    if (failing->allocation_count != failing->fail_at) {
        return false;
    }
    ++failing->failure_count;
    /* As a real allocator would (G1): the log shows where the error path started. */
    RB_LOG_ERROR("Allocation %u failed on purpose (failing allocator)", failing->allocation_count);
    return true;
}

static void *failing_alloc(void *ctx, usize size, usize align) {
    rb_failing_allocator *failing = ctx;
    if (failing_should_fail(failing)) {
        return NULL;
    }
    void *ptr = rb_alloc_uninit(failing->parent, size, align);
    if (ptr != NULL) {
        failing->live_bytes += size;
    }
    return ptr;
}

static void *failing_resize(void *ctx, void *ptr, usize old_size, usize new_size, usize align) {
    rb_failing_allocator *failing = ctx;
    if (new_size > old_size && failing_should_fail(failing)) {
        return NULL;
    }
    void *moved = rb_resize_uninit(failing->parent, ptr, old_size, new_size, align);
    if (moved != NULL) {
        failing->live_bytes = failing->live_bytes - old_size + new_size;
    }
    return moved;
}

static void failing_free(void *ctx, void *ptr, usize size, usize align) {
    rb_failing_allocator *failing = ctx;
    /* More freed than allocated: a size passed back wrong, the bug sized frees invite. */
    RB_ASSERT_MSG(size <= failing->live_bytes, "freeing %zu bytes, only %zu allocated", size,
                  failing->live_bytes);
    ++failing->free_count;
    failing->live_bytes -= size;
    rb_free(failing->parent, ptr, size, align);
}

void rb_failing_allocator_init(rb_failing_allocator *failing, rb_allocator parent, u32 fail_at) {
    RB_ASSERT(failing != NULL);
    RB_ASSERT(parent.vtable != NULL);
    *failing = (rb_failing_allocator){.parent = parent, .fail_at = fail_at};
}

rb_allocator rb_failing_allocator_get(rb_failing_allocator *failing) {
    RB_ASSERT(failing != NULL);
    static const rb_allocator_vtable vtable = {
        .alloc = failing_alloc,
        .resize = failing_resize,
        .free = failing_free,
    };
    return (rb_allocator){.ctx = failing, .vtable = &vtable};
}
