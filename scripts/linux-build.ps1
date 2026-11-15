<#
.SYNOPSIS
    Builds and tests the engine on Linux inside Docker; -Window also opens the sandbox (see docs/features/build-system.md).
.EXAMPLE
    ./scripts/linux-build.ps1                          # debug preset: configure, build, ctest
    ./scripts/linux-build.ps1 debug release debug-asan
    ./scripts/linux-build.ps1 -Window                  # build debug, open the sandbox window via WSLg
    ./scripts/linux-build.ps1 -Window release
    ./scripts/linux-build.ps1 -Shell                   # interactive bash in the container
#>
param(
    [switch]$Shell,
    [switch]$Window,
    [Parameter(ValueFromRemainingArguments)][string[]]$Presets
)

# No $ErrorActionPreference = 'Stop': in Windows PowerShell 5.1 it can turn a
# native tool's stderr into a terminating error. Exit codes are checked instead.
$root   = Split-Path -Parent $PSScriptRoot
$image  = 'reborn-linux'
$volume = 'reborn-linux-build'

# A missing command does not set $LASTEXITCODE: without this check the script
# would exit with the code of whatever native tool ran before it, often 0.
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host 'error: docker not found on PATH (is Docker Desktop installed and running?)'
    exit 1
}

# Cheap when nothing changed (every layer is cached), and keeps the image in
# sync with the Dockerfile without a separate step to remember.
docker build --quiet -f "$root/docker/linux.Dockerfile" -t $image "$root/docker" | Out-Null
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Sources are read-only: the container can never touch the Windows tree.
# build/ is a named volume, so the Windows build/ stays untouched and Linux
# rebuilds are incremental.
$mounts = @('-v', "${root}:/src:ro", '-v', "${volume}:/src/build")

# Docker cannot create the volume's mount point inside a read-only mount, so
# build/ must already exist on the host (it is git-ignored; empty is fine).
New-Item -ItemType Directory -Force "$root/build" | Out-Null

if ($Shell) {
    docker run --rm -it @mounts $image bash
    exit $LASTEXITCODE
}

if (-not $Presets) { $Presets = @('debug') }

# The container has no X server, so the sandbox cannot open a window there: the
# unit tests are the pass/fail gate, and -Window runs the sandbox by hand.
if ($Window) {
    if ($Presets.Count -ne 1) {
        Write-Host 'error: -Window takes a single preset'
        exit 1
    }
    # Docker Desktop exposes WSLg's X server (XWayland) at this path inside its
    # VM: mounting it lets Xlib in the container show windows on the Windows
    # desktop. The script blocks until the window is closed.
    $display = @('-v', '/run/desktop/mnt/host/wslg/.X11-unix:/tmp/.X11-unix', '-e', 'DISPLAY=:0')
    $run = 'set -e; cmake --preset "$1" && cmake --build --preset "$1" && "./build/$1/bin/sandbox"'
    docker run --rm @mounts @display $image sh -c $run sh $Presets[0]
    exit $LASTEXITCODE
}

# One container for all presets; `set -e` stops at the first failure.
$steps = 'set -e; for p in "$@"; do cmake --preset "$p" && cmake --build --preset "$p" && ctest --preset "$p"; done'
docker run --rm @mounts $image sh -c $steps sh @Presets
exit $LASTEXITCODE
