#!/bin/sh
# Builds and tests the engine on Linux inside Docker; --window also opens the sandbox
# (see docs/features/build-system.md).
#
#   scripts/linux-build.sh                          # debug preset: configure, build, ctest
#   scripts/linux-build.sh debug release debug-asan
#   scripts/linux-build.sh --window [preset]        # build, open the sandbox window on the host X server
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

# The container has no X server, so the sandbox cannot open a window there: the
# unit tests are the pass/fail gate, and --window runs the sandbox by hand.
if [ "${1:-}" = "--window" ]; then
    shift
    [ $# -eq 0 ] && set -- debug
    if [ $# -ne 1 ]; then
        echo 'error: --window takes a single preset' >&2
        exit 1
    fi
    if [ -z "${DISPLAY:-}" ]; then
        echo 'error: --window needs an X server on the host ($DISPLAY is not set)' >&2
        exit 1
    fi
    # Shares the host's X socket. The X server may refuse clients from the
    # container until they are allowed (e.g. `xhost +local:`): left to the user
    # on purpose, since it changes the host's access control.
    exec docker run --rm -v "$root:/src:ro" -v "$volume:/src/build" \
        -v /tmp/.X11-unix:/tmp/.X11-unix -e DISPLAY="$DISPLAY" "$image" sh -c \
        'set -e; cmake --preset "$1" && cmake --build --preset "$1" && "./build/$1/bin/sandbox"' \
        sh "$1"
fi

[ $# -eq 0 ] && set -- debug

# One container for all presets; `set -e` stops at the first failure.
exec docker run --rm -v "$root:/src:ro" -v "$volume:/src/build" "$image" sh -c \
    'set -e; for p in "$@"; do cmake --preset "$p" && cmake --build --preset "$p" && ctest --preset "$p"; done' \
    sh "$@"
