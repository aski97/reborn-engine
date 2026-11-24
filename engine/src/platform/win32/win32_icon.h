/*
 * Window icon helpers for Win32 (ADR 0020). No OS header: pure functions,
 * unit-tested without a window. The HICON itself is made in win32_platform.c.
 */
#ifndef RB_WIN32_ICON_H
#define RB_WIN32_ICON_H

#include <reborn/image.h>

/*
 * The image to show at `size` pixels: the smallest at least that large (the
 * system shrinks it a little), else the largest (enlarged). `count` > 0.
 */
u32 win32_icon_pick(const rb_image *images, u32 count, u32 size);

/* RGBA to the BGRA a 32-bit DIB holds, same layout: width * height * 4 bytes into `out`. */
void win32_icon_to_bgra(const rb_image *image, u8 *out);

#endif /* RB_WIN32_ICON_H */
