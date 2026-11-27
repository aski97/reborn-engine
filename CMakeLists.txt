cmake_minimum_required(VERSION 3.25)

# The project version is defined here and only here. It reaches C code through
# the generated header <reborn/version.h> (see engine/include/reborn/version.h.in).
project(RebornEngine
    VERSION 0.2.0
    DESCRIPTION "A learning-oriented 2D/3D game engine written in C with Vulkan"
    LANGUAGES C
)

# -----------------------------------------------------------------------------
# Language
# -----------------------------------------------------------------------------

# -std=c17 rather than -std=gnu17: compiler extensions must be opted into
# explicitly, which keeps the code portable across Clang, GCC and MSVC.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Emits build/<preset>/compile_commands.json, consumed by clangd and by tools
# that need the exact flags each translation unit is compiled with.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------

option(REBORN_SHARED              "Build the engine as a shared library (DLL/.so)" OFF)
option(REBORN_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"              OFF)
option(REBORN_ENABLE_SANITIZERS   "Enable AddressSanitizer + UBSan in Debug"       OFF)
option(REBORN_BUILD_SANDBOX       "Build the sandbox application"                  ON)
option(REBORN_BUILD_GAMES         "Build the games under games/"                   ON)
option(REBORN_BUILD_TOOLS         "Build the tools under tools/ (reborn-cmd)"      ON)
option(REBORN_BUILD_TESTS         "Build the unit tests, registered in CTest"       ON)

# -----------------------------------------------------------------------------
# Output layout
# -----------------------------------------------------------------------------

# Executables and shared libraries share one directory so that, when
# REBORN_SHARED is ON, the OS loader finds the engine DLL next to the exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(RebornCompilerOptions)
include(RebornTesting)

if(REBORN_BUILD_TESTS)
    # enable_testing() rather than include(CTest): the latter also adds CDash
    # dashboard targets and a BUILD_TESTING option we do not use.
    # Called here, at the top level and before any directory with tests (the
    # games have theirs), so that `ctest` works from build/<preset>/.
    enable_testing()
endif()

add_subdirectory(engine)

if(REBORN_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# Each game builds its executable and, with REBORN_BUILD_TESTS, its tests (ADR 0014).
if(REBORN_BUILD_GAMES)
    add_subdirectory(games)
endif()

# Programs that work with the games from outside: reborn-cmd sends them commands (ADR 0022).
if(REBORN_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(REBORN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
