# 0010 — Memory allocation

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
Systems must not allocate their own state (ADR 0006, B2): the engine chooses where memory comes
from. Today the caller `malloc`s a size it queried; ADR 0006 planned the move to an allocator
passed at startup (B3). The game loop (§4) needs a per-frame arena, future containers need an
allocator (ADR 0007), and error paths need a way to make allocations fail in tests. ASan must still
see overflows inside custom allocators. Targets are 64-bit only (`defines.h`). No threads yet.

## Options
**A. Memory from the OS**
1. Reserve and commit as separate steps (`VirtualAlloc` `MEM_RESERVE` / `MEM_COMMIT`;
   `mmap(PROT_NONE)` / `mprotect`) — address ranges cost no RAM until committed; four calls.
2. One call that reserves and commits — simpler; arenas must be fixed-size or chained.
3. `malloc` as the only source — no platform code; no reservations, no page control.

**B. Arena growth**
1. Fixed buffer given by the caller — predictable, fits tests and stack buffers; sized up front.
2. Large reservation, committed on demand — contiguous, never moves, so pointers into it stay
   valid; needs A1.
3. Chain of blocks from a parent allocator — any source; not contiguous, waste at each block's tail.

**C. Allocator interface**
1. `{ void *ctx; const rb_allocator_vtable *vtable; }` — any allocator plugs in (arena, heap,
   failing, tracking); one indirect call per allocation.
2. Enum of kinds + `switch` — direct calls; closed set, test allocators would live in the engine.
3. No interface, concrete types — simplest; nothing to swap in for tests or tracking.

**D. Interface operations**
1. One `realloc`-style function (Lua, Zig) — one entry point; calls are cryptic.
2. `alloc` / `resize` / `free`, all sized (the caller passes the size back) — readable; no
   per-allocation size header.
3. Unsized `free` (malloc-like) — familiar; every allocator stores a size per allocation.

**E. Alignment**
1. Explicit `align` on every call, typed macros using `_Alignof(T)` — exact; one more argument.
2. Fixed 16 bytes — simpler; wastes space, too little for cache lines or AVX.

**F. Releasing arena memory**
1. Reset plus save/restore markers (temp scopes) — nested scratch use within a frame.
2. Reset only — simpler.

**G. Failed allocation** (out of memory, reservation full)
1. Return `NULL`, log, the caller propagates — environment failures are returned (CLAUDE.md);
   testable with a failing allocator.
2. FATAL log and abort — simpler callers; error paths cannot be tested.

**H. Zeroing**
1. Zeroed by default, `_uninit` variants for hot paths — no reads of uninitialized memory, which
   ASan does not detect (MSan does not exist on Windows); a `memset` per allocation.
2. Uninitialized by default, `_zero` variants — faster by default; easy to forget.

**I. ASan**
1. Poison the arena's free tail — catches overflow past the last allocation, use after reset.
2. I1 plus poisoned red zones between allocations (ASan builds only) — also catches overflow into
   the neighbour; layout differs between ASan and normal builds.
3. Nothing — overflows inside an arena go unnoticed.

**J. General-purpose allocator** (objects freed one at a time)
1. Aligned `malloc` wrapper (`_aligned_malloc` on Windows: the UCRT lacks C11 `aligned_alloc`)
   — small, battle-tested.
2. None yet, arenas only — containers that free individually would have nothing to use.
3. Own allocator (free lists, TLSF) — later, if profiling asks for it.

**K. Platform startup (ADR 0006, B3)**
1. `rb_platform_state *rb_platform_startup(rb_allocator)`, `NULL` on failure; the state keeps the
   allocator and shutdown frees through it; the size query goes away.
2. `bool rb_platform_startup(rb_allocator, rb_platform_state **out)` — an out-parameter.
3. Keep both the size query and the allocator form — two ways to do one thing.

**L. Layout**
1. One public header `reborn/memory.h`; code in `engine/src/memory/`; virtual memory internal in
   `platform/platform_os.h`; failing allocator in `tests/support/` (ADR 0004).
2. Separate `allocator.h` and `arena.h` headers.

## Decision
**A1, B2 (B1 included), C1, D2, E1, F1, G1, H1, I2, J1, K1, L1.**
- **Reservation size:** chosen by the arena's creator as an upper bound; reserving costs address
  space only (128 TiB per process on x64) and a small kernel record. Exceeding it fails (G1): the
  arena never moves, because moving it would leave every pointer into it dangling. If one ever must
  grow past its bound, chaining a second reservation can be added inside `rb_arena` without an API
  change, so callers must not rely on two allocations being adjacent. When it would be needed is
  tracked in `TODO.md` (Backlog, Core).
- **B1 for free:** a caller-given buffer is an arena whose reservation is already fully committed.
- **Virtual memory** (`platform/platform_os.h`): `rb_platform_page_size`,
  `rb_platform_reserve_granularity`, `rb_platform_memory_reserve` / `_commit` / `_decommit` /
  `_release`. Whole pages only (asserted). Linux reserves without `MAP_NORESERVE`, so the commit
  charge is taken (and can fail) at commit as on Windows, and decommits by mapping a fresh
  `PROT_NONE` range over the pages, which drops the charge too.
- **Arena** (`reborn/memory.h`, `memory/arena.c`): a caller-owned `rb_arena` value;
  `rb_arena_create` (reservation rounded up to the granularity) / `_create_from_buffer` (B1) /
  `_destroy`; `rb_arena_push` (zeroed, H1), `_push_uninit`, `_push_array` (fails on a size that
  overflows), `RB_ARENA_PUSH_TYPE` / `_ARRAY` (`_Alignof`, E1); `rb_arena_reset` keeps memory
  committed; temp scopes `rb_arena_temp_begin` / `_end` (F1). Alignment applies to the address, so
  a buffer may start anywhere. Commit in 64 KiB steps, clamped to the reservation.
- **ASan** (`memory/asan.h`, I2): committed free memory is poisoned and unpoisoned as it is handed
  out; reset and temp end poison again. ASan builds add a 16-byte poisoned red zone before every
  allocation and align starts to ASan's 8-byte granule. Only committed memory is ever poisoned
  (poisoning a 64 GiB reservation would commit 8 GiB of shadow); destroy unpoisons, since the
  shadow outlives the range. Verified: a one-byte overflow into the next allocation is reported as
  `use-after-poison`, and goes silently into the neighbour without ASan.
- **Allocator interface** (`reborn/memory.h`, `memory/allocator.c`): `rb_allocator_vtable` with
  `alloc` (uninitialized), `resize` (keeps min(old, new) bytes, may move) and `free`, each taking
  `ctx` plus the block's size and alignment; `rb_allocator` = `{ctx, vtable}`, by value. Free takes
  the alignment too, so an allocator may bucket by it. Helpers do what each allocator would repeat:
  `rb_alloc` (zeroed), `_uninit`, `rb_alloc_array` (overflow check), `rb_resize` (grown bytes
  zeroed, H1) / `_uninit`, `rb_free`; `RB_ALLOC_TYPE` / `_ARRAY`, `RB_FREE_TYPE` / `_ARRAY`. Freeing
  `NULL` does nothing, resizing `NULL` allocates (as `realloc`), size 0 is an ordinary block.
- **Arena adapter** (`rb_arena_allocator`): free pops the block only when it is the last one (back
  to its start minus the red zone), resize shrinks in place, grows in place when the block is the
  last one and fits, and otherwise copies. Neither moves the position below the innermost open temp
  scope's start (`rb_arena.floor`, saved and restored by temp scopes, cleared by reset): popping a
  block allocated before the scope would make its end look out of order. ASan builds poison freed
  and shrunk bytes, freed blocks that stay in place included (use after free is reported).
- **Heap allocator** (`rb_heap_allocator`, `memory/heap_allocator.c`) over
  `rb_platform_heap_alloc` / `_resize` / `_free` (`platform_os.h`): `_aligned_malloc` /
  `_aligned_realloc` / `_aligned_free` on Windows; `posix_memalign` on Linux, because C11
  `aligned_alloc` wants the size to be a multiple of the alignment (ASan enforces it), and resize
  through `realloc` up to 16-byte alignment, allocate-copy-free above. Size 0 takes 1 byte (runtimes
  disagree on 0, and `_aligned_realloc(p, 0)` frees). Failures logged by the allocator.
- **Failing allocator** (`tests/support/failing_allocator.h`): `rb_failing_allocator` wraps a
  parent and fails its N-th allocation (1-based; 0 never fails). Growing resizes count as
  allocations, shrinking ones and frees do not. Counters: `allocation_count`, `failure_count`,
  `free_count`, `live_bytes` (for leak checks on error paths).

- **Platform startup** (K1): `rb_platform_state *rb_platform_startup(rb_allocator)` allocates
  the state zeroed, keeps the allocator in the shared state and frees through it on shutdown or on
  a failed startup; `rb_platform_state_size` is gone. The sandbox gives it an engine-lifetime arena
  (64 MiB reserved) until the application layer (§4) owns one.

## Consequences
- Every system and container takes an `rb_allocator`; tests can inject failures anywhere.
- Per-frame memory costs one pointer bump and one reset.
- Arena memory cannot be freed object by object (only the last allocation can be popped).
- Through an arena, a block resized inside a temp scope moves into the scope and dies at its end,
  even if it was allocated before it.
- No thread safety: an arena or allocator is used by one thread at a time until threading exists.
