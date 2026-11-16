/*
 * X11 keycode to rb_key (ADR 0006, P1). No OS header: the table is pure data
 * and is unit-tested without an X server.
 */
#ifndef RB_XLIB_INPUT_H
#define RB_XLIB_INPUT_H

#include <reborn/input.h>

/* `keycode` as in XKeyEvent. RB_KEY_UNKNOWN for keys the enum does not name. */
rb_key xlib_key_from_keycode(u32 keycode);

#endif /* RB_XLIB_INPUT_H */
