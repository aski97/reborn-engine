/*
 * Win32 scancode to rb_key (ADR 0006, F1). No OS header: the table is pure
 * data and is unit-tested without windows.
 */
#ifndef RB_WIN32_INPUT_H
#define RB_WIN32_INPUT_H

#include <reborn/input.h>

/*
 * `scancode` as Win32 puts it in a key message's lParam: bits 16-23 hold the
 * set 1 scancode, bit 24 the "extended" flag (the key's scancode had the E0
 * prefix). Pass (HIWORD(lParam) & (KF_EXTENDED | 0xFF)), i.e. 0x1xx for an
 * extended key. RB_KEY_UNKNOWN for keys the enum does not name.
 */
rb_key win32_key_from_scancode(u32 scancode);

#endif /* RB_WIN32_INPUT_H */
