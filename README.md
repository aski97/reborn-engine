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

## Commands

A running game takes commands: the engine's own (`help`, `status`, `pause`, `resume`, `step`,
`quit`, `wait`, and the `input.*` group) and the game's (`blink.state`...). Each answers with one
JSON line. Start the game
listening on a local port, then send commands with `reborn-cmd`, one argument per command:

```sh
./build/debug/bin/blink --listen 7777 &       # PowerShell: Start-Process .\build\debug\bin\blink.exe "--listen 7777"
./build/debug/bin/reborn-cmd 7777 pause "step 10" status
```
```text
{"id":1,"ok":true,"from":"socket","line":"pause","result":{"step":212}}
{"id":2,"ok":true,"from":"socket","line":"step 10","result":{"ran":10,"step":222}}
{"id":3,"ok":true,"from":"socket","line":"status","result":{"frame":431,"step":222,"paused":true,...}}
```

`reborn-cmd` exits with 0 when every command succeeded, 1 when one failed (`"ok":false` and an
`"error"`), 2 when no game answers. `help` lists every command with its arguments.

The engine's commands, in every game (each game's README lists its own):

| Command | What it does | Result |
|---|---|---|
| `help [name]` | every command, or one | `commands`: name → `usage`, `summary` |
| `status` | the state of the run | `frame`, `step`, `paused`, `game_time`, `step_seconds`, `window` (`width`, `height`, `minimized`) |
| `pause` | stops the updates; events and rendering go on | `step` |
| `resume` | starts the updates again | `step` |
| `step [count]` | pauses, then runs `count` steps (1 by default, at most 100000) at once | `ran`, `step` |
| `quit` | ends the run | — |
| `wait <steps>` | scripts only: runs the next line after this many steps | `until_step` |
| `input.key <key> <down\|up>` | presses or releases a key in the next step | `step` |
| `input.tap <key>` | presses a key in the next step, releases it in the one after | `step`, `release_step` |
| `input.button <button> <down\|up> [<x> <y>]` | presses or releases a mouse button in the next step, at a point if given | `step` |
| `input.click <x> <y> [button]` | moves there and presses (left) in the next step, releases in the one after | `step`, `release_step` |
| `input.mouse <x> <y>` | moves the mouse in the next step | `step` |
| `input.wheel <dy> [dx]` | turns the wheel in the next step (notches, positive up and right) | `step` |
| `input.real [on\|off]` | turns keyboard and mouse on or off at once; no argument: as now | `real` |
| `input.state` | what the engine sees | `keys` and `buttons` down (`real` or `injected`), `mouse` (`x`, `y`, `in_window`), `real`, `waiting`, `next_step` |

Keys are named as `rb_key_name` spells them, any case, `_` for a space (`space`, `a`, `f1`,
`left_ctrl`, `keypad_1`); buttons `left`, `right`, `middle`, `x1`, `x2`; positions in window
pixels. Injected input waits for the next step, so a paused game sees it at the next `step`.

The same commands can come from a file, one per line (`#` starts a comment), with the replies on
stdout:

```sh
printf 'pause\nstep 60\nblink.state\nquit\n' > test.txt
./build/debug/bin/blink --script test.txt > replies.jsonl
```

Details and the full list: [docs/features/commands.md](docs/features/commands.md).

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
tools/                   programs that work with the games (reborn-cmd)
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
