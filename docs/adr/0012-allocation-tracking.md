# 0012 — Allocation tracking

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
Roadmap §6 asks for allocation tracking by tag and a leak report. All engine code that frees
objects one at a time goes through `rb_allocator` (ADR 0010); arenas are pushed directly and free
everything at once. A future profiler (backlog: resource usage) wants current, peak and count per
subsystem. No threads yet.

## Options
**A. Where the tag lives**
1. A tracking allocator wrapping a parent (decorator), named after its owner — no API change, no
   globals; allocations through an unwrapped allocator are not counted.
2. A tag argument on every `rb_alloc` / `rb_resize` / `rb_free`, counted by the helpers in a
   global table indexed by an enum — the call site says what the memory is for, any allocator is
   counted; one more argument everywhere, generic code (containers) must be given a tag or use its
   own, a free with the wrong tag skews the counters.
3. The tag stored in the `rb_allocator` value by its owner, counted by the helpers in a global
   table — generic code stays tag-free; per-owner granularity only, `rb_allocator` grows to three
   fields.

**B. What is recorded**
1. Counters per tag: live bytes and blocks, peak bytes, allocation and free totals — a few
   additions per call; the leak report names the tag, not the line.
2. Counters plus a record per block with its call site (debug builds) — the report names the
   leaking line, double frees are caught; the call site must travel through the vtable (an ADR 0010
   interface change) and every block needs a record.

**C. Arenas** (pushed directly, freed all at once: they cannot leak)
1. A high-water mark (`peak`) per arena, logged at the end of the run — shows how to size
   reservations.
2. Not tracked.

**D. General-purpose memory for the game**
1. `rb_app_allocator(app)`: the engine chooses where the game's objects freed one at a time come
   from (the heap today); the game tags them `RB_MEMORY_TAG_GAME`.
2. None: the game uses arenas, or picks `rb_heap_allocator()` itself.

## Decision
**A2, B1, C1, D1.**
- **Tags** (`reborn/memory.h`): `rb_memory_tag`, an enum ending in `RB_MEMORY_TAG_COUNT`, so the
  stats table is an array indexed by tag. `GENERAL` (no subsystem: tools, tests), `PLATFORM`,
  `GAME`; a subsystem adds its tag when it starts allocating. `rb_memory_tag_name`.
- **Signatures:** the tag is the last argument of `rb_alloc`, `_uninit`, `rb_alloc_array`,
  `rb_resize`, `_uninit`, `rb_free` and of the `RB_ALLOC_*` / `RB_FREE_*` macros; it must be the
  same in alloc, resize and free, like size and alignment. Out of range is asserted. The vtable
  does not change: allocators never see tags.
- **Counting** (`memory/tracking.c`) in the helpers, so every allocator is counted once whatever
  it is. Per tag: `live_bytes`, `peak_bytes`, `live_blocks`, `alloc_count`, `free_count`
  (`rb_memory_stats`, read with `rb_memory_get_stats`). A resize of `NULL` counts as an
  allocation; failed calls count nothing. A free larger than what its tag holds is asserted: a
  wrong size or tag would otherwise make the counters wrap. Always on, release builds included:
  the profiler will read them.
- **Wrappers** (failing allocator) forward to their parent's vtable, not to the helpers, or every
  allocation would be counted twice.
- **Arenas:** `rb_arena.peak`, the highest `pos` reached (red zones included in ASan builds);
  reset and temp scopes do not lower it.
- **Report:** at the end of the run, after the platform shutdown and before the arenas are
  destroyed (also when `init` failed). INFO per tag that allocated: peak and totals; ERROR per tag
  with live blocks; INFO per app arena: peak of reserved. A leak does not change the exit code.
  Sizes print exactly when round (`64 MiB`), otherwise with two decimals (`33.25 KiB`).
- **Game:** `rb_app_allocator(app)` returns the heap allocator; what the game allocates with it
  must be freed before its `shutdown` returns.

## Consequences
- Every allocation through the interface is accounted for, whichever allocator serves it; a
  subsystem's footprint is visible without a profiler.
- Every call site names its tag. Generic code (containers) takes a tag from its owner along with
  the allocator.
- A block taken through `rb_arena_allocator` and dropped without a free is reported as a leak,
  even though the arena reclaims it: code taking an `rb_allocator` frees what it allocates; code
  relying on reset pushes on the arena directly.
- A free with the wrong tag goes unnoticed unless that tag holds less than the size freed.
- The stats table is global: with threads it needs atomics or per-thread tables.
