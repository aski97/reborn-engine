# -----------------------------------------------------------------------------
# Test executable
# -----------------------------------------------------------------------------

# Test sources are compiled straight into the executable, never into a static
# library: the linker drops library objects nothing references, and a test
# file is referenced only by its own constructor, so its tests would vanish
# without any error.
add_executable(reborn_tests
    framework/rb_test.c
    app/test_app.c
    app/test_frame_timing.c
    containers/test_handle_pool.c
    containers/test_ring.c
    core/test_assert.c
    core/test_assert_disabled.c
    core/test_log.c
    core/test_version.c
    memory/test_allocator.c
    memory/test_arena.c
    memory/test_heap_allocator.c
    memory/test_tracking.c
    platform/test_clock.c
    platform/test_input.c
    platform/test_memory.c
    platform/test_platform.c
    # Test helpers (ADR 0004) and their own tests.
    support/failing_allocator.c
    support/test_failing_allocator.c
)

# Backend tests: only the backend of the target OS is in the engine (ADR 0006).
if(WIN32)
    target_sources(reborn_tests PRIVATE
        # Opens a real window: Windows only, the Linux test container has no X server.
        app/test_app_run.c
        platform/win32/test_win32_input.c
        platform/win32/test_win32_platform.c
    )
elseif(LINUX)
    target_sources(reborn_tests PRIVATE
        platform/linux/test_xlib_input.c
        platform/linux/test_xlib_platform.c
    )
endif()

target_include_directories(reborn_tests PRIVATE
    framework
    support
    # Tests may exercise internal headers, not just the public API (ADR 0004).
    ${PROJECT_SOURCE_DIR}/engine/src
)

target_link_libraries(reborn_tests PRIVATE Reborn::Engine)

# Must come before the discovery step: on Windows ASan builds it adds the
# POST_BUILD copy of the ASan runtime, without which --list cannot even start.
reborn_set_compiler_options(reborn_tests)

# -----------------------------------------------------------------------------
# CTest discovery (ADR 0004)
# -----------------------------------------------------------------------------

# After every build, ask the executable for its tests and write one add_test()
# per test, so CTest runs each one in its own process. Doing it at build time
# (not configure time) means adding a test never requires re-running CMake.
set(test_list_file ${CMAKE_CURRENT_BINARY_DIR}/reborn_tests_list.cmake)

add_custom_command(TARGET reborn_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -D TEST_EXECUTABLE=$<TARGET_FILE:reborn_tests>
        -D OUTPUT_FILE=${test_list_file}
        -P ${PROJECT_SOURCE_DIR}/cmake/RebornDiscoverTests.cmake
    VERBATIM
)

# CTest includes this file on every run. The list does not exist until the
# first build: in that case register a test that fails with a clear name,
# instead of letting CTest abort on a missing include.
set(test_include_file ${CMAKE_CURRENT_BINARY_DIR}/reborn_tests_include.cmake)
file(WRITE ${test_include_file}
"if(EXISTS \"${test_list_file}\")
    include(\"${test_list_file}\")
else()
    add_test(reborn_tests_NOT_BUILT reborn_tests_NOT_BUILT)
endif()
")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${test_include_file})
