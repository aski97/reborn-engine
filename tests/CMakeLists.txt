# -----------------------------------------------------------------------------
# Test executable
# -----------------------------------------------------------------------------

# The runner, compiler options and CTest discovery come from
# cmake/RebornTesting.cmake, shared with the games' tests.
reborn_add_test_executable(reborn_tests
    app/test_app.c
    app/test_frame_timing.c
    containers/test_handle_pool.c
    containers/test_ring.c
    core/test_assert.c
    core/test_assert_disabled.c
    core/test_color.c
    core/test_log.c
    core/test_version.c
    math/test_math.c
    math/test_random.c
    memory/test_allocator.c
    memory/test_arena.c
    memory/test_heap_allocator.c
    memory/test_tracking.c
    platform/test_clock.c
    platform/test_input.c
    platform/test_library.c
    platform/test_memory.c
    platform/test_platform.c
    renderer/test_camera2d.c
    renderer/vulkan/test_vulkan_debug.c
    renderer/vulkan/test_vulkan_device.c
    renderer/vulkan/test_vulkan_instance.c
    renderer/vulkan/test_vulkan_loader.c
    renderer/vulkan/test_vulkan_memory.c
    renderer/vulkan/test_vulkan_swapchain.c
    text/test_font_atlas.c
    text/test_font_builtin.c
    text/test_text.c
    text/test_utf8.c
    # Test helpers (ADR 0004) and their own tests.
    support/failing_allocator.c
    support/test_failing_allocator.c
)

# Backend tests: only the backend of the target OS is in the engine (ADR 0006).
if(WIN32)
    target_sources(reborn_tests PRIVATE
        # Open a real window: Windows only, the Linux test container has no X server.
        app/test_app_run.c
        renderer/test_renderer.c
        renderer/vulkan/test_vulkan_renderer.c
        text/test_font.c
        platform/win32/test_win32_icon.c
        platform/win32/test_win32_input.c
        platform/win32/test_win32_platform.c
    )
elseif(LINUX)
    target_sources(reborn_tests PRIVATE
        platform/linux/test_xlib_icon.c
        platform/linux/test_xlib_input.c
        platform/linux/test_xlib_platform.c
    )
endif()

target_include_directories(reborn_tests PRIVATE
    support
    # Tests may exercise internal headers, not just the public API (ADR 0004).
    ${PROJECT_SOURCE_DIR}/engine/src
)

# Vulkan::Headers: the renderer's internal headers include vulkan.h (ADR 0013, A1).
# Found again here because imported targets are visible only in the directory that
# found them and its subdirectories, and tests/ is a sibling of engine/.
find_package(Vulkan REQUIRED)
target_link_libraries(reborn_tests PRIVATE Vulkan::Headers)
