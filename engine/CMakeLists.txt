# -----------------------------------------------------------------------------
# Generated headers
# -----------------------------------------------------------------------------

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/reborn/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/reborn/version.h
)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------

set(REBORN_SOURCES
    src/app/app.c
    src/app/frame_timing.c
    src/containers/handle_pool.c
    src/containers/ring.c
    src/core/assert.c
    src/core/log.c
    src/core/version.c
    src/memory/allocator.c
    src/memory/arena.c
    src/memory/heap_allocator.c
    src/platform/clock.c
    src/platform/input.c
    src/platform/platform.c
)

# One backend per OS, chosen here rather than with #ifdef in the sources (ADR 0006).
if(WIN32)
    list(APPEND REBORN_SOURCES
        src/platform/win32/win32_clock.c
        src/platform/win32/win32_error.c
        src/platform/win32/win32_input.c
        src/platform/win32/win32_memory.c
        src/platform/win32/win32_os.c
        src/platform/win32/win32_platform.c
    )
elseif(LINUX)
    list(APPEND REBORN_SOURCES
        src/platform/linux/linux_clock.c
        src/platform/linux/linux_memory.c
        src/platform/linux/linux_os.c
        src/platform/linux/xlib_input.c
        src/platform/linux/xlib_platform.c
    )
endif()

# -----------------------------------------------------------------------------
# Library
# -----------------------------------------------------------------------------

if(REBORN_SHARED)
    add_library(reborn SHARED ${REBORN_SOURCES})
    # RB_SHARED is PUBLIC so consumers see `dllimport`; RB_BUILD_DLL is PRIVATE
    # so only the engine's own translation units see `dllexport`.
    target_compile_definitions(reborn PUBLIC RB_SHARED=1 PRIVATE RB_BUILD_DLL=1)
    # Hide every symbol by default on ELF so that only RB_API functions are
    # exported, matching Windows DLL semantics.
    set_target_properties(reborn PROPERTIES C_VISIBILITY_PRESET hidden)
else()
    add_library(reborn STATIC ${REBORN_SOURCES})
endif()

add_library(Reborn::Engine ALIAS reborn)

target_include_directories(reborn
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
        # Internal headers: reachable by engine sources, invisible to consumers.
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    # CMake's default library list already contains these, but it is a toolchain
    # convenience: linking them explicitly keeps the dependency visible (ADR 0006).
    target_link_libraries(reborn PRIVATE user32 gdi32)
elseif(LINUX)
    # Xlib is the only Linux system dependency (ADR 0006, `libx11-dev`). REQUIRED stops at
    # configure time with a clear message when it is missing, instead of a compile error later.
    find_package(X11 REQUIRED)
    target_link_libraries(reborn PRIVATE X11::X11)
endif()

reborn_set_compiler_options(reborn)
