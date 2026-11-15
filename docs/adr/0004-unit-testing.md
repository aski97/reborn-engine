# 0004 — Unit testing

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
Core modules (memory, containers, math, logging) need unit tests from the start.
C has no built-in test framework and no reflection to discover tests.

## Options
1. **Own framework, auto-registration** — one header, no dependencies; tests register themselves
   before `main` via `__attribute__((constructor))` (Clang/GCC) or a `.CRT$XCU` section (MSVC).
2. **Own framework, manual list** — pure standard C, but tests are easy to forget.
3. **utest.h / greatest / acutest** — ready-made single headers. First external dependency.
4. **Unity / cmocka** — mature, mocking support. Heavier; cmocka is awkward on Windows.

## Decision
Own framework (`rb_test.h`) with auto-registration, run through CTest.

## Consequences
- `ctest --preset <name>` runs everything; `--output-junit` gives machine-readable results for CI and LLM agents.
- Tests link the static engine and may include private headers from `engine/src/`.
- Auto-registration relies on compiler-specific features, isolated in `rb_test.h`.
- Platform and renderer code needs separate integration tests (backlog).
