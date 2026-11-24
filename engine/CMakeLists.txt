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
    src/core/color.c
    src/core/log.c
    src/core/version.c
    src/memory/allocator.c
    src/memory/arena.c
    src/memory/heap_allocator.c
    src/memory/tracking.c
    src/platform/clock.c
    src/platform/input.c
    src/platform/platform.c
    src/renderer/camera2d.c
    src/renderer/vulkan/vulkan_debug.c
    src/renderer/vulkan/vulkan_device.c
    src/renderer/vulkan/vulkan_frame.c
    src/renderer/vulkan/vulkan_instance.c
    src/renderer/vulkan/vulkan_loader.c
    src/renderer/vulkan/vulkan_memory.c
    src/renderer/vulkan/vulkan_pipeline.c
    src/renderer/vulkan/vulkan_renderer.c
    src/renderer/vulkan/vulkan_result.c
    src/renderer/vulkan/vulkan_swapchain.c
    src/renderer/vulkan/vulkan_texture.c
    src/text/font.c
    src/text/font_atlas.c
    src/text/font_builtin.c
)

# One backend per OS, chosen here rather than with #ifdef in the sources (ADR 0006).
if(WIN32)
    list(APPEND REBORN_SOURCES
        src/platform/win32/win32_clock.c
        src/platform/win32/win32_error.c
        src/platform/win32/win32_input.c
        src/platform/win32/win32_library.c
        src/platform/win32/win32_memory.c
        src/platform/win32/win32_os.c
        src/platform/win32/win32_platform.c
    )
elseif(LINUX)
    list(APPEND REBORN_SOURCES
        src/platform/linux/linux_clock.c
        src/platform/linux/linux_library.c
        src/platform/linux/linux_memory.c
        src/platform/linux/linux_os.c
        src/platform/linux/xlib_input.c
        src/platform/linux/xlib_platform.c
    )
endif()

# -----------------------------------------------------------------------------
# Shaders
# -----------------------------------------------------------------------------

# GLSL compiled to SPIR-V at build time by glslc (ADR 0015, C1 and D1): from the Vulkan SDK on
# Windows, from the `glslc` package on Linux. REQUIRED stops at configure time when it is missing.
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(REBORN_SHADERS
    src/renderer/vulkan/shaders/quad.vert
    src/renderer/vulkan/shaders/quad.frag
)

# Each shader becomes `<name>.inc`, the SPIR-V words as a C initializer list (-mfmt=c), included
# by the renderer (E1): no file to find at run time. The depfile lists the files a shader
# #includes (quad.glsl), so editing one recompiles every shader that includes it.
set(REBORN_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
set(REBORN_SHADER_OUTPUTS)
foreach(shader IN LISTS REBORN_SHADERS)
    get_filename_component(shader_name ${shader} NAME)
    set(shader_output ${REBORN_SHADER_DIR}/${shader_name}.inc)
    add_custom_command(
        OUTPUT ${shader_output}
        COMMAND Vulkan::glslc --target-env=vulkan1.3 -Werror -mfmt=c
            -MD -MF ${shader_output}.d
            -o ${shader_output} ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
        DEPENDS ${shader}
        DEPFILE ${shader_output}.d
        COMMENT "Compiling shader ${shader_name}"
        VERBATIM
    )
    list(APPEND REBORN_SHADER_OUTPUTS ${shader_output})
endforeach()

# -----------------------------------------------------------------------------
# Library
# -----------------------------------------------------------------------------

if(REBORN_SHARED)
    add_library(reborn SHARED ${REBORN_SOURCES} ${REBORN_SHADER_OUTPUTS})
    # RB_SHARED is PUBLIC so consumers see `dllimport`; RB_BUILD_DLL is PRIVATE
    # so only the engine's own translation units see `dllexport`.
    target_compile_definitions(reborn PUBLIC RB_SHARED=1 PRIVATE RB_BUILD_DLL=1)
    # Hide every symbol by default on ELF so that only RB_API functions are
    # exported, matching Windows DLL semantics.
    set_target_properties(reborn PROPERTIES C_VISIBILITY_PRESET hidden)
else()
    add_library(reborn STATIC ${REBORN_SOURCES} ${REBORN_SHADER_OUTPUTS})
endif()

add_library(Reborn::Engine ALIAS reborn)

target_include_directories(reborn
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
        # Internal headers: reachable by engine sources, invisible to consumers.
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # Compiled shaders, included by the renderer only.
        ${REBORN_SHADER_DIR}
)

if(WIN32)
    # CMake's default library list already contains these, but it is a toolchain
    # convenience: linking them explicitly keeps the dependency visible (ADR 0006).
    target_link_libraries(reborn PRIVATE user32 gdi32)
elseif(LINUX)
    # Xlib is the only Linux system dependency (ADR 0006, `libx11-dev`). REQUIRED stops at
    # configure time with a clear message when it is missing, instead of a compile error later.
    find_package(X11 REQUIRED)
    # dlopen lives in libc since glibc 2.34; CMAKE_DL_LIBS names libdl for older ones (ADR 0013).
    # glibc keeps the C math functions in a separate libm (powf: reborn/color.h, ADR 0013).
    target_link_libraries(reborn PRIVATE X11::X11 ${CMAKE_DL_LIBS} m)
endif()

# Headers only (ADR 0013, A1): from the Vulkan SDK on Windows (VULKAN_SDK), from
# `libvulkan-dev` on Linux. Not Vulkan::Vulkan: the loader is opened at run time
# (B2), so the executable starts and reports a missing Vulkan itself.
find_package(Vulkan REQUIRED)
target_link_libraries(reborn PRIVATE Vulkan::Headers)

reborn_set_compiler_options(reborn)
