/* _NET_WM_ICON data (ADR 0020). */
#include "platform/linux/xlib_icon.h"

#include <reborn/assert.h>

usize xlib_icon_length(const rb_image *images, u32 count) {
    RB_ASSERT(images != NULL || count == 0);
    usize length = 0;
    for (u32 i = 0; i < count; ++i) {
        length += 2 + (usize)images[i].width * images[i].height;
    }
    return length;
}

void xlib_icon_fill(const rb_image *images, u32 count, unsigned long *out) {
    RB_ASSERT((images != NULL && out != NULL) || count == 0);
    for (u32 i = 0; i < count; ++i) {
        const rb_image *image = &images[i];
        *out++ = image->width;
        *out++ = image->height;
        usize pixels = (usize)image->width * image->height;
        for (usize p = 0; p < pixels; ++p) {
            const u8 *rgba = &image->pixels[p * 4];
            *out++ = (unsigned long)rgba[3] << 24 | (unsigned long)rgba[0] << 16 |
                     (unsigned long)rgba[1] << 8 | rgba[2];
        }
    }
}
