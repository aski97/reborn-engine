# =============================================================================
# reborn_add_test_executable(<target> <sources>...)
#
# A test executable built on rb_test.h (ADR 0004): the framework's runner plus
# <sources>, the project's compiler options, and one CTest entry per test,
# discovered after every build. Used by the engine's tests and by each game's.
#
# Test sources are compiled straight into the executable, never into a static
# library: the linker drops library objects nothing references, and a test
# file is referenced only by its own constructor, so its tests would vanish
# without any error.
#
# reborn_set_compiler_options() comes before the discovery step: on Windows
# ASan builds it adds the POST_BUILD copy of the ASan runtime, without which
# --list cannot even start.
# =============================================================================
function(reborn_add_test_executable target)
    add_executable(${target} ${PROJECT_SOURCE_DIR}/tests/framework/rb_test.c ${ARGN})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/framework)
    target_link_libraries(${target} PRIVATE Reborn::Engine)
    reborn_set_compiler_options(${target})

    # After every build, ask the executable for its tests and write one add_test()
    # per test, so CTest runs each one in its own process. Doing it at build time
    # (not configure time) means adding a test never requires re-running CMake.
    set(test_list_file ${CMAKE_CURRENT_BINARY_DIR}/${target}_list.cmake)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D TEST_EXECUTABLE=$<TARGET_FILE:${target}>
            -D OUTPUT_FILE=${test_list_file}
            -P ${PROJECT_SOURCE_DIR}/cmake/RebornDiscoverTests.cmake
        VERBATIM
    )

    # CTest includes this file on every run. The list does not exist until the
    # first build: in that case register a test that fails with a clear name,
    # instead of letting CTest abort on a missing include.
    set(test_include_file ${CMAKE_CURRENT_BINARY_DIR}/${target}_include.cmake)
    file(WRITE ${test_include_file}
"if(EXISTS \"${test_list_file}\")
    include(\"${test_list_file}\")
else()
    add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)
endif()
")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${test_include_file})
endfunction()
