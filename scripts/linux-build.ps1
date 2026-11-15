<#
.SYNOPSIS
    Builds and runs the engine on Linux inside Docker (see docs/features/build-system.md).
.EXAMPLE
    ./scripts/linux-build.ps1                          # debug preset
    ./scripts/linux-build.ps1 debug release debug-asan
    ./scripts/linux-build.ps1 -Shell                   # interactive bash in the container
#>
param(
    [switch]$Shell,
    [Parameter(ValueFromRemainingArguments)][string[]]$Presets
)

# No $ErrorActionPreference = 'Stop': in Windows PowerShell 5.1 it can turn a
# native tool's stderr into a terminating error. Exit codes are checked instead.
$root   = Split-Path -Parent $PSScriptRoot
$image  = 'reborn-linux'
$volume = 'reborn-linux-build'

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

# One container for all presets; `set -e` stops at the first failure.
$steps = 'set -e; for p in "$@"; do cmake --preset "$p" && cmake --build --preset "$p" && "./build/$p/bin/sandbox"; done'
docker run --rm @mounts $image sh -c $steps sh @Presets
exit $LASTEXITCODE
