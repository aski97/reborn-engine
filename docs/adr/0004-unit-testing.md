# 0004 — Unit testing

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
Core modules (memory, containers, math, logging) need unit tests from the start.
C has no built-in test framework and no reflection to discover tests.
Start simple; revisit the runner when real use cases appear (memory, containers).

## Options
**A. Framework**
1. Own framework, auto-registration — one header, no dependencies; tests register themselves
   before `main` via `__attribute__((constructor))` (Clang/GCC) or a `.CRT$XCU` section (MSVC).
2. Own framework, manual list — pure standard C, but tests are easy to forget.
3. utest.h / greatest / acutest — ready-made single headers. First external dependency.
4. Unity / cmocka — mature, mocking support. Heavier; cmocka is awkward on Windows.

**B. Tests in CTest**
1. One executable, one CTest entry — simplest; a crash kills every test, CTest/JUnit see one test.
2. One executable per module — isolation per module; more targets, a crash still stops its module.
3. One executable, one CTest entry per test (discovered via `--list` after each build) — every test
   in its own process, real `ctest -j`, per-test JUnit; needs a small CMake script.

**C. Stopping a test on a fatal check (`RB_REQUIRE_*`)**
1. `return` inside the macro — trivial; only works in the test body, not in helpers.
2. `setjmp`/`longjmp` back to the runner — works from helpers, enables testing `RB_ASSERT`; skips cleanup.

**D. Fixtures**
1. None: tests call their helpers explicitly.
2. Setup/teardown per suite.

**E. Test location**
1. Separate `tests/` tree mirroring `engine/src/` — engine sources stay clean, test code can never
   reach the library; the test is far from the code and must be moved with it.
2. Next to the code (`arena.c` + `arena_test.c`) — easy to find, moves together; the engine target
   must filter `*_test.c` out, and one slip links auto-registered tests into the game.
3. Inside the source under `#ifdef RB_TESTING` — can test `static` functions; the engine is built
   twice (or the tested binary is not the shipped one), framework header in engine code.

## Decision
**A1, B3, C1, D1, E1.**
- Own framework (`rb_test.h`): no dependency, and small enough to understand entirely.
- One CTest entry per test: crash isolation is worth the discovery script in C, where a bad pointer
  is the typical failure.
- `return`-based `REQUIRE` and no fixtures are enough until a test needs more.
- With auto-registration, keeping test code physically out of `engine/` is the safest layout;
  the mirrored path keeps tests easy to find.

Other kinds of tests get their own place, because they do not fit the unit runner:

| Kind | Location | Why separate |
|---|---|---|
| Integration (window, renderer) | `tests/integration/`, own executable | needs a window/GPU; excluded in CI via a CTest label |
| Fuzz targets | `fuzz/`, one executable per target | libFuzzer provides its own `main` |
| Benchmarks | `bench/` | meaningful only in release, not pass/fail |
| Test helpers (e.g. failing allocator) | `tests/support/` | test-only, must not ship in the engine |

## Consequences
- `ctest --preset <name>` runs everything; `--output-junit` gives machine-readable results for CI and LLM agents.
- A crash fails only its own test; `ctest -R`, `--rerun-failed` and JUnit work per test.
- Adding a test needs a rebuild but never a re-configure.
- Tests link the static engine and may include private headers from `engine/src/`;
  `static` functions are tested through the internal API that uses them.
- `RB_REQUIRE_*` cannot be used in helper functions; testing that `RB_ASSERT` fires needs C2 later.
- Registration only covers Clang and GCC (`__attribute__((constructor))`, which Clang also maps to
  `.CRT$XCU` on Windows); MSVC `cl` is out until the engine supports it.
- If linking one test executable gets slow, split it per module inside `tests/`; discovery is unchanged.
- Platform and renderer code needs separate integration tests (backlog).
