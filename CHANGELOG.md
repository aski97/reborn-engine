# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Project layout: `engine/` (`reborn` library), `sandbox/`, `cmake/`, `docs/`.
- CMake build with `debug`, `release`, `debug-asan` presets (Clang + Ninja, C17).
- Engine built as a static library; `REBORN_SHARED` option and `RB_API` macro ready for a DLL build.
- `reborn/defines.h`: fixed-width types, platform/compiler detection, utilities.
- Single-source version, exposed to C through a generated `reborn/version.h`.
- Docs: feature pages, ADRs, roadmap, `CLAUDE.md` for LLM agents.
