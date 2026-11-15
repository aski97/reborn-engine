# =============================================================================
# reborn_set_compiler_options(<target>)
#
# Applies the project-wide warning set, build-configuration defines and
# optional sanitizers to <target>. Every target in the repository calls this,
# so all code is held to the same standard.
# =============================================================================
function(reborn_set_compiler_options target)

    # -------------------------------------------------------------------------
    # Warnings
    # -------------------------------------------------------------------------
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow                 # a local silently hiding an outer variable is a common bug source
            -Wstrict-prototypes      # in C, `void f()` accepts ANY arguments; require `void f(void)`
            -Wmissing-prototypes     # non-static functions must be declared in a header (or made static)
            -Wdouble-promotion       # catches float -> double promotions that cost performance
            -Wimplicit-fallthrough
            -Wformat=2               # validates printf-style format strings, including our logger later
        )
        if(REBORN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(REBORN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Sanitizers
    # -------------------------------------------------------------------------
    if(REBORN_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug"
       AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)

        # On Windows the ASan runtime is a DLL that is not on PATH: without a
        # copy next to the exe, the program dies at startup with 0xC0000135.
        get_target_property(target_type ${target} TYPE)
        if(WIN32 AND target_type STREQUAL "EXECUTABLE")
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
                OUTPUT_VARIABLE clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${clang_resource_dir}/lib/windows/clang_rt.asan_dynamic-x86_64.dll"
                    $<TARGET_FILE_DIR:${target}>
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Configuration defines
    # -------------------------------------------------------------------------
    # Engine code branches on RB_DEBUG / RB_RELEASE instead of NDEBUG, so what
    # stays enabled in release builds (asserts, logging levels) is our decision
    # and not a side effect of the standard library's assert().
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug>:RB_DEBUG=1>
        $<$<NOT:$<CONFIG:Debug>>:RB_RELEASE=1>
    )
endfunction()
