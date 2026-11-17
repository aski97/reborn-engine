# Linux build environment: verifies that the engine builds and runs on Linux
# from a Windows host. See docs/features/build-system.md and ADR 0005.
#
#   docker build -f docker/linux.Dockerfile -t reborn-linux docker
#
# The build context is docker/ (not the repo root) so that `build/` and the
# sources are never sent to the daemon: sources are bind-mounted at run time.

FROM ubuntu:26.04

# Kept equal to the Clang major used on Windows, so that warnings and
# sanitizer behaviour match on both platforms.
ARG LLVM_VERSION=23

# Ubuntu ships an older Clang than Windows: LLVM's own APT repository provides
# the matching version. curl and gpg are only needed to fetch its signing key.
# libc6-dev and binutils are listed explicitly because --no-install-recommends
# would otherwise leave Clang without libc headers or a linker.
# libx11-dev provides the Xlib headers and library for the Linux platform
# backend (ADR 0006); no X server is installed, so windows cannot open here.
# libvulkan-dev provides the Vulkan headers and the loader (libvulkan1), which
# the engine opens at run time (ADR 0013). There is no GPU here: mesa-vulkan-drivers
# brings lavapipe, a Vulkan 1.3+ driver running on the CPU, so renderer tests can
# create instances; vulkan-validationlayers lets them run with validation.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg \
        cmake ninja-build libc6-dev binutils \
        libx11-dev \
        libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers \
    && . /etc/os-release \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${LLVM_VERSION} main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-${LLVM_VERSION} \
        libclang-rt-${LLVM_VERSION}-dev \
    && apt-get purge -y --auto-remove curl gnupg \
    && rm -rf /var/lib/apt/lists/*

# The presets ask for plain `clang`; the LLVM packages only install clang-NN.
RUN ln -s /usr/bin/clang-${LLVM_VERSION} /usr/local/bin/clang

WORKDIR /src
