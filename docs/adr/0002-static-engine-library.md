# 0002 — Static engine library

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
The engine is consumed by one or more executables. It can be linked statically
(copied into the exe) or dynamically (DLL/.so loaded at runtime).

## Options
1. **Static** — no symbol exports, no ABI or cross-module heap issues, LTO possible. Every change needs relink and restart.
2. **Shared** — reusable across executables, enables plugins/hot-reload. Requires `dllexport/dllimport`, care with malloc/free across modules, stable struct layouts.

## Decision
Static. The `RB_API` macro marks every public function and the `REBORN_SHARED`
CMake option builds a DLL, so switching later costs one flag.

## Consequences
- Hot-reload will target **game code** loaded as a DLL, not the engine; game state is
  allocated by the engine so it survives reloads.
