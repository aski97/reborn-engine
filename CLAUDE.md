# Reborn Engine — notes for LLM agents

C17 + Vulkan game engine for Windows and Linux. It is a learning project:
the owner wants to understand and approve every architectural choice.

## Workflow
- Before introducing a new architectural choice, present the alternatives with pros/cons
  and let the owner decide. Record the decision in `docs/adr/`: one ADR per topic, *Proposed*
  (editable) until implemented, then *Accepted* (immutable). See `docs/adr/README.md`.
- No external libraries without explicit approval.
- Keep `CHANGELOG.md` (`[Unreleased]`) and `TODO.md` up to date.
- Every major feature has a page in `docs/features/` (from `_template.md`). Create it with the
  feature, update it (including the *History* table) when the feature changes, and update the index.
- Everything in the repository is in English. Docs are concise.

## Build
```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/bin/sandbox
ctest --preset debug      # unit tests; JUnit in build/debug/junit.xml
```
New engine code gets tests in `tests/` (same path as in `engine/src/`), see `docs/features/unit-testing.md`.
`build/<preset>/compile_commands.json` lists the exact flags for every file.
Linux check from Windows (Docker): `./scripts/linux-build.ps1 debug release debug-asan`.

## Code conventions
- C17, no extensions. Use `<reborn/defines.h>` types (`u32`, `f32`, `usize`...) when size matters.
- Prefixes: functions and types `rb_`, macros `RB_`.
- Public functions live in `engine/include/reborn/` and are marked `RB_API`.
- Platform-specific code only in `engine/src/platform/<os>/`.
- File-local functions are `static`.
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
