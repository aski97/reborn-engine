/*
 * What the renderer needs from a camera (ADR 0016): where its field lands in
 * the image and the projection that puts the world there. Backend-independent.
 */
#ifndef RB_CAMERA2D_INTERNAL_H
#define RB_CAMERA2D_INTERNAL_H

#include <reborn/camera2d.h>
#include <reborn/math.h>

/* A rectangle of whole pixels, origin at the image's top left. */
typedef struct rb_camera2d_field {
    u32 x, y;
    u32 width, height;
} rb_camera2d_field;

/*
 * The largest rectangle with the aspect of the camera's `size` that fits a
 * target of `width` x `height` pixels, centered: the rest are the bars. Whole
 * pixels, so viewport and scissor cover the same ones. Can be 0 wide or high
 * when the target is (or its aspect is extreme).
 */
rb_camera2d_field rb_camera2d_field_rect(const rb_camera2d *camera, u32 width, u32 height);

/* World to normalized coordinates over the field: the area shown, size / zoom around center. */
rb_mat4 rb_camera2d_projection(const rb_camera2d *camera);

#endif /* RB_CAMERA2D_INTERNAL_H */
