/* Window icon helpers for Win32 (ADR 0020). */
#include "platform/win32/win32_icon.h"

#include <reborn/assert.h>

/* The larger side: an icon is square in practice, and a tall one must not count as small. */
static u32 icon_side(const rb_image *image) {
    return image->width > image->height ? image->width : image->height;
}

u32 win32_icon_pick(const rb_image *images, u32 count, u32 size) {
    RB_ASSERT(images != NULL && count > 0);
    u32 best = 0;
    for (u32 i = 1; i < count; ++i) {
        u32  side = icon_side(&images[i]);
        u32  best_side = icon_side(&images[best]);
        bool fits = side >= size;
        bool best_fits = best_side >= size;
        /* Large enough beats too small; then the smaller of two that fit, the larger of two that do
         * not. */
        if ((fits && !best_fits) || (fits && side < best_side) ||
            (!fits && !best_fits && side > best_side)) {
            best = i;
        }
    }
    return best;
}

void win32_icon_to_bgra(const rb_image *image, u8 *out) {
    RB_ASSERT(image != NULL && image->pixels != NULL && out != NULL);
    usize count = (usize)image->width * image->height;
    for (usize i = 0; i < count; ++i) {
        const u8 *rgba = &image->pixels[i * 4];
        u8       *bgra = &out[i * 4];
        bgra[0] = rgba[2];
        bgra[1] = rgba[1];
        bgra[2] = rgba[0];
        bgra[3] = rgba[3];
    }
}
