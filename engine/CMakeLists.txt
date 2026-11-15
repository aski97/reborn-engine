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
    src/core/version.c
)

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

reborn_set_compiler_options(reborn)
