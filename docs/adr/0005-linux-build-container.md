# 0005 — Linux build container

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
Development happens on Windows, but the engine must also build and run on Linux. The local WSL
distro (Ubuntu 22.04) is shared with unrelated projects and ships CMake 3.22, below the 3.25
required by the presets. Windows uses Clang 23.

## Options
**Environment**
1. **Upgrade the existing WSL distro in place** — one distro. Two LTS hops, breaks the Python
   environments of other projects, hard to undo.
2. **Dedicated WSL distro** — clean, WSLg works out of the box for windows/Vulkan later. Set up by
   hand, not reproducible, not reusable in CI.
3. **Docker container** — environment described in a versioned file, disposable, reusable in CI,
   easy to try other distros. Needs Docker Desktop; GUI/GPU access is awkward.

**Base image and compiler**
1. **`ubuntu:24.04`** — CMake 3.28, Clang 18. Five Clang majors behind Windows.
2. **`ubuntu:26.04`** — CMake 4.2, Clang 21. Official packages only.
3. **`ubuntu:26.04` + apt.llvm.org** — same Clang major as Windows. One extra package source
   (LLVM's official repository) to trust.

**Invocation** — documented `docker` commands / helper scripts / Docker Compose.

**Build output** — named volume / anonymous volume / host folder `build/linux-<preset>`.

## Decision
Docker, defined in `docker/linux.Dockerfile`: `ubuntu:26.04` with Clang from apt.llvm.org, pinned
to the Windows major (`LLVM_VERSION`). Launched by `scripts/linux-build.ps1` (and a `.sh` mirror)
with sources mounted read-only and `build/` on the named volume `reborn-linux-build`.

## Consequences
- One command verifies every preset on Linux; the Windows `build/` is never touched.
- Same compiler major on both platforms: identical warnings and sanitizer behaviour.
- The image is built locally from the Dockerfile and not published. Whether CI pushes it to a
  registry (e.g. GHCR) to save build time is decided with the CI work.
- `LLVM_VERSION` must be bumped together with the Windows toolchain.
- Windowing and Vulkan are not covered: they will need WSLg or a real Linux machine.
