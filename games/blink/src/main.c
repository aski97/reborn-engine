/* Entry point: the application layer runs the loop; the game is in blink_game.c. */
#include <reborn/app.h>

#include "blink_game.h"

int main(int argc, char **argv) {
    return rb_app_run(&blink_game_callbacks, argc, argv);
}
