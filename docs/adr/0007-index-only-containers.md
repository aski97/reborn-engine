# 0007 — Generational handles and index-only containers

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
The platform layer needs a pool of window handles (ADR 0006) and a FIFO event queue. Renderer
resources, assets, audio voices, entities and jobs will need the same structures later.
C has no generics: a reusable container must either ignore the element type (`void *` + size),
generate typed code with macros, or avoid touching elements altogether.

## Options
**A. Making a container generic**
1. `void *` + element size (as `qsort`) — one implementation, plain code; no type checking, size
   known only at runtime.
2. Macros generating typed code — type-safe and fast; unreadable compiler errors, hard to debug.
3. Index-only: the container manages indices, the user owns a typed array indexed by them —
   type-safe, no macros, plain code; two steps per operation, and only possible for structures
   whose logic does not depend on the elements (not for a dynamic array or a hash map).

**B. Handle encoding**
1. `u32`, 16-bit index / 16-bit generation — 65 536 slots.
2. `u32`, 20-bit index / 12-bit generation — ~1 M slots, a slot wraps after 4 096 reuses.
3. `u32`, 24/8 — ~16 M slots, wraps after 256 reuses.
4. `u64` — wrap-around practically impossible; twice the size per handle.

**C. Generation wrap-around**
When a generation wraps, a very old handle can become valid again.
1. Accept the risk.
2. Retire the slot: once its generation is exhausted it is never reused.

**D. Location**
1. `engine/src/containers/`, internal — a folder per subsystem; future containers go there.
2. `engine/src/core/` — no new folder; `core` becomes a catch-all.
3. Public in `engine/include/reborn/` — usable by game code; any change is an API break.

## Decision
**A3, B2, C2, D1.**
- **Handle pool** (`containers/handle_pool.h`): only ids live in the pool (generation per slot,
  stack of free slots); each system keeps its data in its own arrays indexed by the slot.
  Public handle types wrap the id in their own struct (`typedef struct rb_window { u32 id; }`),
  so the compiler rejects a texture where a window is expected.
  - `id = generation << 20 | index`; generations start at 1, so id 0 is never valid and a
    zero-initialized handle is always invalid.
  - A slot whose generation would exceed 4 095 is retired: one slot lost per 4 095 reuses,
    and a stale handle can never match again.
  - The caller provides the arrays (ADR 0006, B2).
- **Ring buffer** (`containers/ring.h`): FIFO index logic (`head`, `count`, `capacity`); push
  returns the slot to write, pop the slot to read. Any capacity, not only powers of two.
- **Free slots** are a plain stack inside the pool: three lines, not worth an abstraction.
- **Containers that own memory** (dynamic array, hash map) need an allocator: they get their own
  ADR with roadmap §6, where `void *` vs macros is decided on a concrete case.
- Internal for now: promoting a header to `reborn/` is easy, withdrawing a public API is not.

## Consequences
- Systems stay decoupled: the platform never sees the renderer's per-window data, nor the
  reverse.
- Using a stale handle is detected instead of being undefined behaviour.
- A pool holds at most 2^20 slots and, with heavy churn, slowly loses retired slots
  (capacity × 4 095 frees before it is exhausted).
- Using a ring takes two steps (`push_index`, then write the element).
