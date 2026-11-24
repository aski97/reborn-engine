/*
 * _NET_WM_ICON data (ADR 0020). No OS header: pure functions, unit-tested
 * without an X server.
 *
 * The property is a list of 32-bit values: width, height, then each pixel as
 * 0xAARRGGBB, for every image one after the other. Xlib takes format-32
 * property data as an array of `long` (64 bits on 64-bit Linux), using only
 * the low 32 bits of each: an array of u32 would be read two values at a time.
 */
#ifndef RB_XLIB_ICON_H
#define RB_XLIB_ICON_H

#include <reborn/image.h>

/* How many values the images take: 2 + width * height each. */
usize xlib_icon_length(const rb_image *images, u32 count);

/* Writes xlib_icon_length(images, count) values into `out`. */
void xlib_icon_fill(const rb_image *images, u32 count, unsigned long *out);

#endif /* RB_XLIB_ICON_H */
