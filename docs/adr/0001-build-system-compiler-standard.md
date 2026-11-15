# 0001 — Build system, compiler and C standard

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
We need reproducible builds on Windows and Linux with one set of flags, good IDE/clangd
support, and room to grow (tools, shaders, 3D).

## Options
**Build system**
1. **CMake** — de facto standard, cross-platform, emits `compile_commands.json`. Own syntax; must be installed.
2. **Hand-written scripts** — fully transparent, no dependencies. Two scripts to maintain, poor IDE support.
3. **Premake** — readable Lua config. Smaller ecosystem.

**Compiler**
1. **Clang** — same compiler on both platforms, sanitizers, good diagnostics.
2. **MSVC** — best VS debugger integration, but Linux would need a second compiler.
3. **GCC/MinGW** — consistent with Linux, weaker fit with Windows tooling (PDB, RenderDoc).

**Standard**
1. **C99** — no `_Static_assert`, `_Alignas`, atomics.
2. **C17** — C11 plus fixes, supported everywhere.
3. **C23** — more expressive, incomplete MSVC support.

## Decision
CMake ≥ 3.25 with presets and Ninja; Clang; C17 without extensions.

## Consequences
- Identical flags and warnings on both platforms.
- `compile_commands.json` available to clangd and LLM agents.
- Moving to C23 later is a one-line change.
