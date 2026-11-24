# Reborn Engine

A game engine written in C17 with Vulkan, built step by step as a learning project.
2D first, 3D later. Platforms: Windows and Linux.

**Status: pre-alpha.** v0.1.0 has the foundations (windows, input, memory, game loop, a Vulkan
renderer drawing colored quads); it is not yet usable to make a game. See the [roadmap](TODO.md).

- **No external libraries**, except where building them makes no sense (e.g. the Vulkan loader).
- **Every decision is documented** in [`docs/`](docs/README.md).
- **LLM-friendly**: engine state and logs are designed to be inspected and driven by agents.

## Scope

The engine provides the minimal, general features games need, built with care: what any game of
its kind would use (windows, input, memory, rendering, text, audio, assets...). The rest lives
elsewhere:

- **In the game** (`games/<name>/`): its rules, content and logic no other game would use. A piece
  that may or may not be general starts in the game and moves into the engine when a second game
  needs it.
- **Out of scope**: store and platform-service SDKs (Steam, GOG, consoles) and anything tied to a
  single shipped title. They belong in the repositories of the games that ship with them.

## Requirements

| Tool       | Version | Notes |
|------------|---------|-------|
| Clang      | ≥ 17    | compiler, called `clang` by the presets |
| CMake      | ≥ 3.25  | build system |
| Ninja      | any     | build executor used by the presets |
| Vulkan     | ≥ 1.3   | headers, loader, validation layers, `glslc`, a GPU driver |

### Windows

In PowerShell:

```powershell
winget install -e --id LLVM.LLVM
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id KhronosGroup.VulkanSDK
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Add `C:\Program Files\LLVM\bin` to `PATH`, then open a new terminal. Clang uses the Windows SDK
and C runtime from the Visual Studio Build Tools on its own: no developer prompt is needed.

### Linux

On Ubuntu or Debian (other distributions have the same packages under their own names):

```sh
sudo apt install clang cmake ninja-build libx11-dev \
    libvulkan-dev vulkan-validationlayers glslc mesa-vulkan-drivers
```

If the distribution's Clang is older than 17, install a newer one from [apt.llvm.org](https://apt.llvm.org/)
and make `clang` point to it. `debug-asan` also needs the sanitizer runtime
(`libclang-rt-<version>-dev` on Ubuntu). With an NVIDIA GPU, its proprietary driver provides Vulkan.

## Build

The same commands in PowerShell on Windows and in a shell on Linux:

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/sandbox             # every engine feature on show
./build/debug/bin/blink               # a game: see games/README.md
```

Presets: `debug`, `release`, `debug-asan`. Details in [docs/features/build-system.md](docs/features/build-system.md).

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
(e.g. `tests/memory/test_arena.c`), list it in `reborn_add_test_executable(reborn_tests ...)` in
`tests/CMakeLists.txt`, and rebuild. Each game has its own tests (`blink_tests`), run by the same
`ctest`. Macros and design: [docs/features/unit-testing.md](docs/features/unit-testing.md).

## Layout

```
engine/include/reborn/   public API   → #include <reborn/...>
engine/src/              implementation
sandbox/                 test application using the engine
games/                   our games, one folder each (games/README.md)
tests/                   unit tests (mirrors engine/src/) and the rb_test framework
cmake/                   CMake modules
docker/, scripts/        development tooling, not needed to build
docs/features/           one page per engine feature
docs/adr/                architecture decision records
```

## Conventions

- `rb_` prefix for functions and types, `RB_` for macros.
- [Conventional Commits](https://www.conventionalcommits.org/) and [SemVer](https://semver.org/).
- Changes are tracked in [CHANGELOG.md](CHANGELOG.md), the roadmap in [TODO.md](TODO.md).

## License

[MIT](LICENSE) © 2026 Vittorio Belardo
