/*
 * Sprite geometry (ADR 0027): the CPU turns the quad, the shader only places
 * corners along two edges. Pivot, flipping or scale, when a game needs them,
 * change this file and nothing on the GPU side.
 */
#include "renderer/sprite_internal.h"

rb_sprite_quad rb_sprite_quad_make(rb_rect rect, f32 radians) {
    /*
     * Where the top edge points: (cos, sin) from the C library, not the GPU's,
     * accurate at any angle the float holds, where Vulkan guarantees GPU sine
     * and cosine only within [-pi, pi]. The left edge points a quarter turn
     * further, clockwise: (x, y) turned by a quarter is (-y, x). The upright
     * edges (width, 0) and (0, height), turned as rb_vec2_rotate would.
     */
    rb_vec2 along = rb_vec2_from_angle(radians);
    rb_vec2 axis_x = rb_vec2_scale(along, rect.width);
    rb_vec2 axis_y = rb_vec2_scale((rb_vec2){-along.y, along.x}, rect.height);
    /*
     * The center stays put. From the top-left corner it is `half` upright and
     * half the turned diagonal after: the corner moves by the difference.
     * Written as a difference, not as center - turned, because at 0 the two
     * halves are equal bit for bit and the difference is exactly 0, while
     * (x + w/2) - w/2 may round away from x.
     */
    rb_vec2 half = {rect.width * 0.5f, rect.height * 0.5f};
    rb_vec2 turned = rb_vec2_scale(rb_vec2_add(axis_x, axis_y), 0.5f);
    rb_vec2 origin = rb_vec2_add((rb_vec2){rect.x, rect.y}, rb_vec2_sub(half, turned));
    return (rb_sprite_quad){origin, axis_x, axis_y};
}

rb_rect rb_sprite_source(rb_rect source, u32 width, u32 height) {
    if (source.width == 0.0f && source.height == 0.0f) {
        return (rb_rect){0.0f, 0.0f, (f32)width, (f32)height};
    }
    return source;
}
