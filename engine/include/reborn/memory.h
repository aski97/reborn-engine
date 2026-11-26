/*
 * Memory (ADR 0010, 0012): the allocator interface and its tags, arenas, the heap
 * allocator.
 *
 *   rb_arena arena;
 *   if (!rb_arena_create(&arena, RB_GIB(1))) { ... }   // addresses only, no RAM yet
 *   rb_player *player = RB_ARENA_PUSH_TYPE(&arena, rb_player);
 *   u32 *ids = RB_ARENA_PUSH_ARRAY(&arena, u32, count);
 *   ...
 *   rb_arena_reset(&arena);                            // everything gone at once
 *   rb_arena_destroy(&arena);
 *
 *   rb_allocator heap = rb_heap_allocator();           // or rb_arena_allocator(&arena)
 *   u32 *items = RB_ALLOC_ARRAY(heap, u32, count, RB_MEMORY_TAG_GAME);
 *   RB_FREE_ARRAY(heap, items, u32, count, RB_MEMORY_TAG_GAME);   // size and tag passed back
 *
 * An arena hands out memory by moving a position forward and frees it only all
 * together (reset) or back to a saved position (temp scopes). It never moves:
 * pointers stay valid until reset, temp end or destroy. Two allocations are not
 * guaranteed to be adjacent. Code that must not care where its memory comes
 * from takes an rb_allocator. Not thread-safe: one thread at a time.
 */
#ifndef RB_MEMORY_H
#define RB_MEMORY_H

#include <reborn/defines.h>

/* ============================================================================
 * Tags and stats (ADR 0012)
 *
 * Every allocation through the interface names what it belongs to; the
 * helpers below count it under that tag, whichever allocator serves it.
 * ========================================================================== */

/* A subsystem adds its tag when it starts allocating. */
typedef enum rb_memory_tag {
    RB_MEMORY_TAG_GENERAL,  /* no subsystem: tools, tests */
    RB_MEMORY_TAG_PLATFORM, /* platform state */
    RB_MEMORY_TAG_GAME,     /* the game's own allocations */
    RB_MEMORY_TAG_RENDERER, /* renderer state, Vulkan queries */
    RB_MEMORY_TAG_TEXT,     /* fonts and their atlas pixels before upload */
    RB_MEMORY_TAG_IMAGE,    /* image encoding scratch, captured frames */
    RB_MEMORY_TAG_COUNT
} rb_memory_tag;

/* Counted since startup, through the rb_alloc... helpers only (arenas pushed directly are not). */
typedef struct rb_memory_stats {
    usize live_bytes;  /* allocated and not yet freed */
    usize peak_bytes;  /* highest live_bytes so far */
    usize live_blocks; /* blocks not yet freed: above 0 at shutdown is a leak */
    u64   alloc_count; /* successful allocations, resizes of NULL included */
    u64   free_count;
} rb_memory_stats;

/* "platform", "game"...; "unknown" out of range. */
RB_API const char *rb_memory_tag_name(rb_memory_tag tag);

RB_API rb_memory_stats rb_memory_get_stats(rb_memory_tag tag);

/* ============================================================================
 * Allocator interface (ADR 0010, C1, D2)
 *
 * Every operation is sized: the caller passes back the size and alignment it
 * allocated with, so no allocator needs a header per block. Rules shared by
 * every allocator:
 * - `align` is a power of two (asserted), and the same in alloc, resize, free.
 * - `tag` (ADR 0012) is the same in alloc, resize and free: a free under
 *   another tag skews both tags' counters.
 * - Failure returns NULL and is logged by the allocator (G1); after a failed
 *   resize the old block is untouched and still the caller's.
 * - A size of 0 gives a valid pointer that must not be dereferenced and is
 *   freed like any other. Freeing NULL does nothing.
 * ========================================================================== */

/*
 * The allocator's functions. `ctx` is the allocator's own state, the role
 * `this` plays in a method. Call them through the rb_alloc... helpers below,
 * which add zeroing, overflow checks and the NULL cases.
 */
typedef struct rb_allocator_vtable {
    /* `size` bytes, uninitialized. */
    void *(*alloc)(void *ctx, usize size, usize align);
    /* Keeps the first min(old, new) bytes, may move; the rest is uninitialized. */
    void *(*resize)(void *ctx, void *ptr, usize old_size, usize new_size, usize align);
    void (*free)(void *ctx, void *ptr, usize size, usize align);
} rb_allocator_vtable;

/*
 * Two pointers, passed and stored by value. The state behind `ctx` must
 * outlive every copy.
 */
typedef struct rb_allocator {
    void                      *ctx;
    const rb_allocator_vtable *vtable;
} rb_allocator;

/* `size` bytes aligned to `align`, zeroed (H1). NULL on failure (logged). */
RB_API void *rb_alloc(rb_allocator allocator, usize size, usize align, rb_memory_tag tag);

/* As rb_alloc without zeroing: for buffers about to be fully overwritten. */
RB_API void *rb_alloc_uninit(rb_allocator allocator, usize size, usize align, rb_memory_tag tag);

/* `count` elements of `elem_size` bytes, zeroed. NULL also when the total overflows (logged). */
RB_API void *rb_alloc_array(rb_allocator allocator, usize elem_size, usize align, usize count,
                            rb_memory_tag tag);

/*
 * Grows or shrinks a block, keeping its first min(old, new) bytes; grown bytes
 * are zeroed (H1). May move: use the returned pointer. NULL on failure (logged),
 * the old block still valid. A NULL `ptr` (with `old_size` 0) allocates.
 */
RB_API void *rb_resize(rb_allocator allocator, void *ptr, usize old_size, usize new_size,
                       usize align, rb_memory_tag tag);

/* As rb_resize, grown bytes left uninitialized. */
RB_API void *rb_resize_uninit(rb_allocator allocator, void *ptr, usize old_size, usize new_size,
                              usize align, rb_memory_tag tag);

/* `size`, `align` and `tag` as allocated (or as last resized). NULL does nothing. */
RB_API void rb_free(rb_allocator allocator, void *ptr, usize size, usize align, rb_memory_tag tag);

/* `_Alignof(T)` is the alignment the type needs (C11): no size or alignment to get wrong. */
#define RB_ALLOC_TYPE(allocator, T, tag) ((T *)rb_alloc((allocator), sizeof(T), _Alignof(T), (tag)))
#define RB_ALLOC_ARRAY(allocator, T, count, tag)                                                   \
    ((T *)rb_alloc_array((allocator), sizeof(T), _Alignof(T), (count), (tag)))
#define RB_FREE_TYPE(allocator, ptr, T, tag)                                                       \
    rb_free((allocator), (ptr), sizeof(T), _Alignof(T), (tag))
/* `count` as allocated: it fitted then, so the product cannot overflow now. */
#define RB_FREE_ARRAY(allocator, ptr, T, count, tag)                                               \
    rb_free((allocator), (ptr), sizeof(T) * (count), _Alignof(T), (tag))

/* ============================================================================
 * Arena
 * ========================================================================== */

/*
 * Owned by the caller (a local, a struct member). The fields are public only so
 * it can live there: read them in tests and debugging, never write them.
 */
typedef struct rb_arena {
    u8   *base;
    usize pos;       /* bytes handed out, from base */
    usize committed; /* bytes usable without asking the OS, from base */
    usize reserved;  /* upper bound: the reservation, or the buffer's size */
    usize floor;     /* innermost open temp scope's pos: rb_arena_allocator never frees below */
    usize peak;      /* highest pos so far, never lowered: what the reservation must hold */
    bool  owns_memory;
} rb_arena;

/*
 * Reserves `reserve_size` bytes of addresses (rounded up to the OS reservation
 * granularity) and commits nothing: RAM is taken as allocations reach it, in
 * 64 KiB steps. Pick an upper bound, not an expected size: exceeding it fails.
 * False on failure (logged); `arena` is then zeroed.
 */
RB_API bool rb_arena_create(rb_arena *arena, usize reserve_size);

/*
 * An arena over memory the caller owns (a static array, a block from another
 * allocator): fixed size, nothing is committed or freed. The buffer must
 * outlive the arena.
 */
RB_API void rb_arena_create_from_buffer(rb_arena *arena, void *buffer, usize size);

/* Releases the reservation (a buffer is left to its owner) and zeroes `arena`. */
RB_API void rb_arena_destroy(rb_arena *arena);

/*
 * `size` bytes aligned to `align` (a power of two), zeroed. NULL when the
 * reservation is full or the OS has no memory left (logged). A size of 0 gives
 * a valid pointer that must not be dereferenced.
 */
RB_API void *rb_arena_push(rb_arena *arena, usize size, usize align);

/* As rb_arena_push without zeroing: for buffers about to be fully overwritten. */
RB_API void *rb_arena_push_uninit(rb_arena *arena, usize size, usize align);

/* `count` elements of `elem_size` bytes, zeroed. NULL also when the total overflows (logged). */
RB_API void *rb_arena_push_array(rb_arena *arena, usize elem_size, usize align, usize count);

/* `_Alignof(T)` is the alignment the type needs (C11): no size or alignment to get wrong. */
#define RB_ARENA_PUSH_TYPE(arena, T) ((T *)rb_arena_push((arena), sizeof(T), _Alignof(T)))
#define RB_ARENA_PUSH_ARRAY(arena, T, count)                                                       \
    ((T *)rb_arena_push_array((arena), sizeof(T), _Alignof(T), (count)))

/*
 * Frees everything: the position goes back to the start. Committed memory
 * stays committed, so refilling it (next frame) costs no system call.
 */
RB_API void rb_arena_reset(rb_arena *arena);

/* ============================================================================
 * Temp scopes
 *
 *   rb_arena_temp temp = rb_arena_temp_begin(&arena);
 *   char *scratch = rb_arena_push(&arena, 4096, 1);
 *   ...
 *   rb_arena_temp_end(temp);   // scratch and everything after it is gone
 *
 * Scopes nest; end them in reverse order of begin, and not after a reset that
 * went below them (both are bugs, asserted).
 * ========================================================================== */

typedef struct rb_arena_temp {
    rb_arena *arena;
    usize     pos;
    usize     floor; /* the arena's floor before this scope, restored at end */
} rb_arena_temp;

RB_API rb_arena_temp rb_arena_temp_begin(rb_arena *arena);
RB_API void          rb_arena_temp_end(rb_arena_temp temp);

/* ============================================================================
 * Arena as an allocator
 *
 * Code taking an rb_allocator can use an arena. Free gives memory back only
 * when the block is the arena's last allocation (the position moves back);
 * any other block stays used until reset or temp end. Resize shrinks in place,
 * grows in place when the block is the last one and fits, and otherwise copies
 * to a new block. Neither moves the position below an open temp scope's start,
 * so a block allocated before the scope is never popped inside it.
 * ========================================================================== */

/* The arena must outlive the allocator. */
RB_API rb_allocator rb_arena_allocator(rb_arena *arena);

/* ============================================================================
 * Heap allocator (ADR 0010, J1)
 *
 * The C runtime's aligned allocation: for objects freed one at a time, any
 * alignment. Stateless: every call returns an equivalent value.
 * ========================================================================== */

RB_API rb_allocator rb_heap_allocator(void);

#endif /* RB_MEMORY_H */
