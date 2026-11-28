/*
 * What the renderer works out for a sprite before the GPU sees it (ADR 0027):
 * the quad as a corner and two edges, and the region of the texture it shows.
 * Backend-independent and pure, like the camera's math: tested without a GPU.
 */
#ifndef RB_SPRITE_INTERNAL_H
#define RB_SPRITE_INTERNAL_H

#include <reborn/defines.h>
#include <reborn/math.h>

/*
 * A parallelogram as the shader draws it: corner (u, v) of the unit square,
 * u and v in 0..1, lands on origin + u * axis_x + v * axis_y. The texture's
 * region follows the same corners, so it turns with the quad.
 */
typedef struct rb_sprite_quad {
    rb_vec2 origin; /* the rectangle's top-left corner, turned */
    rb_vec2 axis_x; /* its top edge, left to right: (width, 0) turned */
    rb_vec2 axis_y; /* its left edge, top to bottom: (0, height) turned */
} rb_sprite_quad;

/*
 * `rect` turned by `radians` about its center, clockwise on screen for a
 * positive angle (ADR 0026). At 0 the result is exactly (x, y), (width, 0),
 * (0, height): upright quads land on the same pixels as before rotation.
 */
rb_sprite_quad rb_sprite_quad_make(rb_rect rect, f32 radians);

/*
 * The region a sprite shows, in the texture's pixels: `source`, or the whole
 * `width` x `height` texture when `source` has no size (both 0, as when the
 * field is left out).
 */
rb_rect rb_sprite_source(rb_rect source, u32 width, u32 height);

#endif /* RB_SPRITE_INTERNAL_H */
