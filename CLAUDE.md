# Reborn Engine — notes for LLM agents

C17 + Vulkan game engine for Windows and Linux. It is a learning project:
the owner wants to understand and approve every architectural choice.

## Workflow
- Before introducing a new architectural choice, present the alternatives with pros/cons
  and let the owner decide. Record the decision in `docs/adr/`: one ADR per topic, *Proposed*
  (editable) until implemented, then *Accepted* (immutable decision; editorial fixes allowed).
  See `docs/adr/README.md`.
- Docs and code never cite roadmap sections by number: name the work ("the game loop").
- Build supporting pieces (data structures, threading, interfaces, abstractions) only when the
  feature being implemented needs them, as part of that feature: built ahead of a concrete use,
  they fit guesses instead of our needs. Until then they stay in the `TODO.md` backlog, with the
  feature or condition that will call for them.
- From v0.2.0 the roadmap is a ladder of our own games under `games/` (ADR 0014): engine tasks come
  from what the current game needs. The sandbox keeps exercising every engine feature.
- Every game is built in layers (ADR 0014): rules (pure, actions in, events out), input, view,
  glue, `main.c`; rules and input tested in `games/<name>/tests/`. Each game has a `README.md`
  (how it plays, how to build and run it, its commands), listed in `games/README.md`. The engine's
  commands are listed in the main `README.md`: keep both lists in step with the code.
- No external libraries without explicit approval.
- Keep `CHANGELOG.md` (`[Unreleased]`) and `TODO.md` up to date. `[Unreleased]` is the difference
  from the last release (*Added*, *Changed*, *Fixed*, *Removed*): a change to something added
  since then edits its existing entry instead of adding a new one.
- Every major feature has a page in `docs/features/` (from `_template.md`). Create it with the
  feature, update it (including the *History* table) when the feature changes, and update the index.
- Everything in the repository is in English. Docs are concise.

## Build
```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/bin/sandbox
./build/debug/bin/blink   # the games: games/README.md
ctest --preset debug      # unit tests, the games' too; JUnit in build/debug/junit.xml
```
Driving a running game (`docs/features/commands.md`): start it with `--listen <port>` (or
`--script <file>`), then `./build/debug/bin/reborn-cmd <port> help "blink.state" "step 10"`:
one JSON reply per line, exit code 0 when all succeeded.
New engine code gets tests in `tests/` (same path as in `engine/src/`), see `docs/features/unit-testing.md`.
`build/<preset>/compile_commands.json` lists the exact flags for every file.
Linux check from Windows (Docker): `./scripts/linux-build.ps1 debug release debug-asan`
(tests only: the container has no X server, the sandbox runs on a Linux desktop).

## Code conventions
- C17, no extensions. Use `<reborn/defines.h>` types (`u32`, `f32`, `usize`...) when size matters.
- Prefixes: functions and types `rb_`, macros `RB_`.
- Public functions live in `engine/include/reborn/` and are marked `RB_API`.
- One folder per subsystem under `engine/src/` (`core`, `containers`, `platform`...).
- Platform-specific code only in `engine/src/platform/<os>/`; backend files carry the backend
  prefix (`win32_platform.c`).
- Includes: `<reborn/...>` and system headers with `<>`, internal headers with `""` relative to
  `engine/src/` (`"containers/ring.h"`).
- File-local functions are `static`.
- Diagnostics go through `<reborn/log.h>` (`RB_LOG_*`), never `printf`, see
  `docs/features/logging.md`.
- `RB_ASSERT` only for bugs in the calling code: required pointers at public entry points, and
  invariants whose violation would silently corrupt state. Failures caused by input or the
  environment (missing file, no X server, bad title) are logged and returned, never asserted.
  Conditions must have no side effects (not evaluated in release).
- Comments explain *why* (constraints, trade-offs, pitfalls), not what the code does.
  Decorative dividers to separate sections are welcome:
  ```c
  /* ============================================================================
   * Section name
   * ========================================================================== */
  ```
  In CMake: `# ---...---` lines around the section name.
- Formatting: `.clang-format` (never run it on `*.in` templates: it breaks `@VAR@` placeholders).
- Commits: Conventional Commits.
