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

## Layout

```
engine/include/reborn/   public API   → #include <reborn/...>
engine/src/              implementation
sandbox/                 test application using the engine
cmake/                   CMake modules
docs/features/           one page per engine feature
docs/adr/                architecture decision records
```

## Conventions

- `rb_` prefix for functions and types, `RB_` for macros.
- [Conventional Commits](https://www.conventionalcommits.org/) and [SemVer](https://semver.org/).
- Changes are tracked in [CHANGELOG.md](CHANGELOG.md), the roadmap in [TODO.md](TODO.md).
