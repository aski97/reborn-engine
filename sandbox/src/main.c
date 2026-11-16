/*
 * Entry point. The application layer does the rest: version check, logger,
 * systems, loop (ADR 0011); the game is in game.c.
 */
#include <reborn/app.h>

#include "game.h"

int main(void) {
    return rb_app_run(&sandbox_game);
}
