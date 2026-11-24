# -----------------------------------------------------------------------------
# Game
# -----------------------------------------------------------------------------

# Every layer but the glue and main(): the game and its tests share them.
set(BLINK_LAYERS
    src/blink_input.c
    src/blink_rules.c
    src/blink_view.c
)

add_executable(blink
    ${BLINK_LAYERS}
    src/blink_game.c
    src/main.c
)
target_link_libraries(blink PRIVATE Reborn::Engine)
reborn_set_compiler_options(blink)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------

# The rules, the input mapping and the icon, without a window: they run in the Linux container too.
if(REBORN_BUILD_TESTS)
    reborn_add_test_executable(blink_tests
        ${BLINK_LAYERS}
        tests/test_blink_input.c
        tests/test_blink_rules.c
        tests/test_blink_view.c
    )
    target_include_directories(blink_tests PRIVATE src)
endif()
