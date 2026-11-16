/*
 * Arena over a virtual memory reservation or a caller's buffer (ADR 0010, B2, B1),
 * and its rb_allocator adapter (C1).
 */
#include <reborn/memory.h>

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include "memory/asan.h"
#include "platform/platform_os.h"

/*
 * Commit in steps, not per push: a stream of small allocations then costs one
 * system call per 64 KiB. A multiple of the page size on every supported CPU.
 */
#define ARENA_COMMIT_STEP ((usize)RB_KIB(64))

/*
 * ASan builds only: a poisoned gap before every allocation catches overflows
 * into the neighbour (ADR 0010, I2), and allocations start on ASan's 8-byte
 * granule, the unit its shadow memory tracks: a granule shared by two
 * allocations could not be poisoned for one and valid for the other.
 */
#if RB_ASAN_ENABLED
    #define ARENA_RED_ZONE  ((usize)16)
    #define ARENA_MIN_ALIGN ((usize)8)
#else
    #define ARENA_RED_ZONE  ((usize)0)
    #define ARENA_MIN_ALIGN ((usize)1)
#endif

/* `align` must be a power of two: its bits below the one set are the remainder to clear. */
static usize arena_align_up(usize value, usize align) {
    return (value + align - 1) & ~(align - 1);
}

/* ============================================================================
 * Lifetime
 * ========================================================================== */

bool rb_arena_create(rb_arena *arena, usize reserve_size) {
    RB_ASSERT(arena != NULL);
    RB_ASSERT(reserve_size > 0);
    RB_ASSERT(ARENA_COMMIT_STEP % rb_platform_page_size() == 0);
    *arena = (rb_arena){0};

    usize granularity = rb_platform_reserve_granularity();
    if (reserve_size > SIZE_MAX - granularity) {
        RB_LOG_ERROR("Arena reservation of %zu bytes is too large", reserve_size);
        return false;
    }
    usize size = arena_align_up(reserve_size, granularity);
    void *base = rb_platform_memory_reserve(size);
    if (base == NULL) {
        return false;
    }
    *arena = (rb_arena){.base = base, .reserved = size, .owns_memory = true};
    return true;
}

void rb_arena_create_from_buffer(rb_arena *arena, void *buffer, usize size) {
    RB_ASSERT(arena != NULL);
    RB_ASSERT(buffer != NULL || size == 0);
    *arena = (rb_arena){.base = buffer, .committed = size, .reserved = size};
    RB_ASAN_POISON(buffer, size);
}

void rb_arena_destroy(rb_arena *arena) {
    RB_ASSERT(arena != NULL);
    /*
     * ASan's shadow memory outlives the range: poison left behind would be
     * reported against whoever uses these addresses next (the buffer's owner,
     * malloc reusing the address range).
     */
    RB_ASAN_UNPOISON(arena->base, arena->committed);
    if (arena->owns_memory) {
        rb_platform_memory_release(arena->base, arena->reserved);
    }
    *arena = (rb_arena){0};
}

/* ============================================================================
 * Allocation
 * ========================================================================== */

/* Commits up to at least `end`. The reservation is whole pages, so clamping to it stays whole. */
static bool arena_commit(rb_arena *arena, usize end) {
    usize target = arena_align_up(end, ARENA_COMMIT_STEP);
    if (target > arena->reserved) {
        target = arena->reserved;
    }
    u8   *first = arena->base + arena->committed;
    usize size = target - arena->committed;
    if (!rb_platform_memory_commit(first, size)) {
        return false;
    }
    /* Fresh pages belong to the free tail: poisoned until handed out. */
    RB_ASAN_POISON(first, size);
    arena->committed = target;
    return true;
}

/* Every forward move goes through here, so the high-water mark (ADR 0012, C1) misses none. */
static void arena_advance(rb_arena *arena, usize end) {
    arena->pos = end;
    if (end > arena->peak) {
        arena->peak = end;
    }
}

static void *arena_push(rb_arena *arena, usize size, usize align, bool zero) {
    RB_ASSERT(arena != NULL);
    RB_ASSERT(align > 0 && (align & (align - 1)) == 0);
    if (align < ARENA_MIN_ALIGN) {
        align = ARENA_MIN_ALIGN;
    }

    /*
     * Align the address, not the offset: a caller's buffer may start at any
     * address. User-space addresses stay far below SIZE_MAX, so the sum cannot
     * wrap.
     */
    usize base = (usize)arena->base;
    usize start = arena_align_up(base + arena->pos + ARENA_RED_ZONE, align) - base;
    /* Written so that nothing overflows: `start + size` could. */
    if (start > arena->reserved || size > arena->reserved - start) {
        RB_LOG_ERROR("Arena full: %zu bytes requested, %zu of %zu in use", size, arena->pos,
                     arena->reserved);
        return NULL;
    }
    usize end = start + size;
    if (end > arena->committed && !arena_commit(arena, end)) {
        return NULL;
    }

    u8 *ptr = arena->base + start;
    RB_ASAN_UNPOISON(ptr, size);
    /* Fresh pages are already zero, but memory reused after a reset is not. */
    if (zero) {
        memset(ptr, 0, size);
    }
    arena_advance(arena, end);
    return ptr;
}

void *rb_arena_push(rb_arena *arena, usize size, usize align) {
    return arena_push(arena, size, align, true);
}

void *rb_arena_push_uninit(rb_arena *arena, usize size, usize align) {
    return arena_push(arena, size, align, false);
}

void *rb_arena_push_array(rb_arena *arena, usize elem_size, usize align, usize count) {
    /* A count read from a file can make elem_size * count wrap to a small, "valid" size. */
    if (elem_size != 0 && count > SIZE_MAX / elem_size) {
        RB_LOG_ERROR("Arena array of %zu elements of %zu bytes overflows", count, elem_size);
        return NULL;
    }
    return arena_push(arena, elem_size * count, align, true);
}

/* ============================================================================
 * Release
 * ========================================================================== */

/* Back to `pos`: what lay after it is poisoned again, so a stale pointer is reported. */
static void arena_rewind(rb_arena *arena, usize pos) {
    RB_ASAN_POISON(arena->base + pos, arena->pos - pos);
    arena->pos = pos;
}

void rb_arena_reset(rb_arena *arena) {
    RB_ASSERT(arena != NULL);
    arena_rewind(arena, 0);
    arena->floor = 0;
}

/*
 * The floor protects the scope's start from the allocator adapter: popping a
 * block allocated before the scope would move pos below temp.pos, and the
 * scope's end would then be taken for an out-of-order end.
 */
rb_arena_temp rb_arena_temp_begin(rb_arena *arena) {
    RB_ASSERT(arena != NULL);
    rb_arena_temp temp = {.arena = arena, .pos = arena->pos, .floor = arena->floor};
    arena->floor = arena->pos;
    return temp;
}

void rb_arena_temp_end(rb_arena_temp temp) {
    RB_ASSERT(temp.arena != NULL);
    RB_ASSERT_MSG(temp.pos <= temp.arena->pos,
                  "temp scope at %zu ended after the arena went back to %zu (reset, or scopes "
                  "ended out of order)",
                  temp.pos, temp.arena->pos);
    arena_rewind(temp.arena, temp.pos);
    temp.arena->floor = temp.floor;
}

/* ============================================================================
 * Allocator adapter
 *
 * A block is identified by its offset from base and the size the caller
 * passes back. Only the last block, allocated above the floor, can move pos:
 * anything else is shrunk or freed where it is and its bytes stay used until
 * reset or temp end.
 * ========================================================================== */

/* Offset of a block of this arena. Rejects blocks of another arena, or freed by a reset. */
static usize arena_block_start(const rb_arena *arena, const void *ptr, usize size) {
    usize start = (usize)((const u8 *)ptr - arena->base);
    RB_ASSERT_MSG((const u8 *)ptr >= arena->base && start <= arena->pos &&
                      size <= arena->pos - start,
                  "block of %zu bytes is not in use in this arena", size);
    return start;
}

/*
 * Every push leaves ARENA_RED_ZONE bytes before its start, so start minus the
 * red zone is where pos stood before it, or after it when alignment added
 * padding: popping there never frees a live byte.
 */
static bool arena_block_is_top(const rb_arena *arena, usize start, usize size) {
    return start + size == arena->pos && start >= arena->floor + ARENA_RED_ZONE;
}

static void *arena_allocator_alloc(void *ctx, usize size, usize align) {
    return arena_push(ctx, size, align, false);
}

static void arena_allocator_free(void *ctx, void *ptr, usize size, usize align) {
    RB_UNUSED(align);
    rb_arena *arena = ctx;
    usize     start = arena_block_start(arena, ptr, size);
    if (arena_block_is_top(arena, start, size)) {
        arena_rewind(arena, start - ARENA_RED_ZONE);
    } else {
        /* Not reusable before reset, but ASan can still catch a use after free. */
        RB_ASAN_POISON(ptr, size);
    }
}

static void *arena_allocator_resize(void *ctx, void *ptr, usize old_size, usize new_size,
                                    usize align) {
    rb_arena *arena = ctx;
    usize     start = arena_block_start(arena, ptr, old_size);
    bool      top = arena_block_is_top(arena, start, old_size);

    if (new_size <= old_size) {
        RB_ASAN_POISON((u8 *)ptr + new_size, old_size - new_size);
        if (top) {
            arena->pos = start + new_size;
        }
        return ptr;
    }

    /* The same space check as a push, written so that nothing overflows. */
    if (top && new_size <= arena->reserved - start) {
        usize end = start + new_size;
        if (end > arena->committed && !arena_commit(arena, end)) {
            return NULL;
        }
        RB_ASAN_UNPOISON(ptr, new_size);
        arena_advance(arena, end);
        return ptr;
    }

    void *moved = arena_push(arena, new_size, align, false);
    if (moved == NULL) {
        return NULL;
    }
    memcpy(moved, ptr, old_size);
    arena_allocator_free(arena, ptr, old_size, align);
    return moved;
}

rb_allocator rb_arena_allocator(rb_arena *arena) {
    RB_ASSERT(arena != NULL);
    static const rb_allocator_vtable vtable = {
        .alloc = arena_allocator_alloc,
        .resize = arena_allocator_resize,
        .free = arena_allocator_free,
    };
    return (rb_allocator){.ctx = arena, .vtable = &vtable};
}
