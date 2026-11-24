/*
 * 2D camera (ADR 0016): which part of the world the game shows, in world
 * units of its own choice, independent of the window's resolution.
 *
 *   rb_camera2d camera = rb_camera2d_make((rb_vec2){8, 4.5f}, (rb_vec2){16, 9});
 *   rb_renderer_set_camera(renderer, &camera);    // draws below are in world units
 *
 * `size` is the design area: always shown whole, as large as the window
 * allows, with bars where the window's aspect differs (letterbox). World y
 * points down, like window pixels. Stateless: callable from any thread.
 */
#ifndef RB_CAMERA2D_H
#define RB_CAMERA2D_H

#include <reborn/defines.h>
#include <reborn/math.h>

/* Build it with rb_camera2d_make, then change its fields: fields added later get their default. */
typedef struct rb_camera2d {
    rb_vec2 center; /* world point at the middle of the field */
    rb_vec2 size;   /* the design area in world units, positive */
    /*
     * An effect on top of the design area, positive: the area shown is
     * size / zoom around center. 2 shows half of it, everything twice as big.
     */
    f32 zoom;
} rb_camera2d;

/* A camera showing `size` around `center`, zoom 1. */
RB_API rb_camera2d rb_camera2d_make(rb_vec2 center, rb_vec2 size);

/*
 * The world point under `point`, given in window pixels as rb_mouse_position
 * (origin top left), for a window of `target` pixels (rb_window_get_size's
 * pixel size). Points on the bars map outside the area shown. A target with
 * no pixels returns `center`.
 */
RB_API rb_vec2 rb_camera2d_screen_to_world(const rb_camera2d *camera, rb_vec2 target,
                                           rb_vec2 point);

/*
 * The inverse: where the world point `point` appears in window pixels, for a
 * window of `target` pixels. Points outside the area shown land on the bars
 * or off the window. A target with no pixels returns (0, 0).
 */
RB_API rb_vec2 rb_camera2d_world_to_screen(const rb_camera2d *camera, rb_vec2 target,
                                           rb_vec2 point);

#endif /* RB_CAMERA2D_H */
