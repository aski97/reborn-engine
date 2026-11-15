# Reborn Engine

A game engine written in C17 with Vulkan, built step by step as a learning project.
2D first, 3D later. Platforms: Windows and Linux.

- **No external libraries**, except where building them makes no sense (e.g. the Vulkan loader).
- **Every decision is documented** in [`docs/`](docs/README.md).
- **LLM-friendly**: engine state and logs are designed to be inspected and driven by agents.

## Requirements

| Tool       | Version | Notes |
|------------|---------|-------|
| Clang      | ≥ 17    | primary compiler |
| CMake      | ≥ 3.25  | build system |
| Ninja      | any     | build executor used by the presets |
| Vulkan SDK | ≥ 1.3   | headers, loader, validation layers, `glslc` |

## Build

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/sandbox
```

Presets: `debug`, `release`, `debug-asan`.

### Checking Linux without a Linux machine

Optional, development only: building the engine never requires Docker. With Docker installed,
this builds the presets in an Ubuntu container with the same Clang major as Windows, then runs
the sandbox and the tests:

```powershell
./scripts/linux-build.ps1 debug release debug-asan
```

Details in [docs/features/build-system.md](docs/features/build-system.md).

## Tests

Build first: the list of tests is refreshed after every build, so a new test needs no re-configure.

```sh
cmake --build --preset debug
ctest --preset debug                  # every test, each in its own process
```

Use `debug-asan` often: AddressSanitizer and UBSan catch bugs that no check asserts.

| Command | What it does |
|---|---|
| `ctest --preset debug -R version` | only tests whose name matches `version` |
| `ctest --preset debug -j8` | run in parallel |
| `ctest --preset debug --rerun-failed` | only the tests that failed last time |
| `ctest --preset debug -V` | full output, passing tests included |
| `./build/debug/bin/reborn_tests` | every test in one process |
| `./build/debug/bin/reborn_tests --list` | test names (`suite.name`) |
| `./build/debug/bin/reborn_tests --run version.string_matches_header` | one test (use this in a debugger) |

Each `ctest` run also writes a JUnit report to `build/<preset>/junit.xml`.

To add a test, create it under `tests/` at the same path as the code in `engine/src/`
(e.g. `tests/memory/test_arena.c`), list it in `add_executable(reborn_tests ...)` in
`tests/CMakeLists.txt`, and rebuild. Macros and design: [docs/features/unit-testing.md](docs/features/unit-testing.md).

## Layout

```
engine/include/reborn/   public API   → #include <reborn/...>
engine/src/              implementation
sandbox/                 test application using the engine
tests/                   unit tests (mirrors engine/src/) and the rb_test framework
cmake/                   CMake modules
docker/, scripts/        optional Linux check (dev tooling, not needed to build)
docs/features/           one page per engine feature
docs/adr/                architecture decision records
```

## Conventions

- `rb_` prefix for functions and types, `RB_` for macros.
- [Conventional Commits](https://www.conventionalcommits.org/) and [SemVer](https://semver.org/).
- Changes are tracked in [CHANGELOG.md](CHANGELOG.md), the roadmap in [TODO.md](TODO.md).

## License

[MIT](LICENSE) © 2026 Vittorio Belardo
