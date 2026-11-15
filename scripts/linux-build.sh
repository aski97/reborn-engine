#!/bin/sh
# Builds, runs and tests the engine on Linux inside Docker (see docs/features/build-system.md).
#
#   scripts/linux-build.sh                          # debug preset
#   scripts/linux-build.sh debug release debug-asan
#   scripts/linux-build.sh --shell                  # interactive bash in the container
#
# Mirror of linux-build.ps1 for Linux/macOS hosts: keep the two in sync.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
image=reborn-linux
volume=reborn-linux-build

# Cheap when nothing changed (every layer is cached), and keeps the image in
# sync with the Dockerfile without a separate step to remember.
docker build --quiet -f "$root/docker/linux.Dockerfile" -t "$image" "$root/docker" >/dev/null

# Sources are read-only: the container can never touch the host tree.
# build/ is a named volume, so the host build/ stays untouched and Linux
# rebuilds are incremental.
# Docker cannot create the volume's mount point inside a read-only mount, so
# build/ must already exist on the host (it is git-ignored; empty is fine).
mkdir -p "$root/build"

if [ "${1:-}" = "--shell" ]; then
    exec docker run --rm -it -v "$root:/src:ro" -v "$volume:/src/build" "$image" bash
fi

[ $# -eq 0 ] && set -- debug

# One container for all presets; `set -e` stops at the first failure.
exec docker run --rm -v "$root:/src:ro" -v "$volume:/src/build" "$image" sh -c \
    'set -e; for p in "$@"; do cmake --preset "$p" && cmake --build --preset "$p" && "./build/$p/bin/sandbox" && ctest --preset "$p"; done' \
    sh "$@"
