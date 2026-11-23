/*
 * 2D camera (ADR 0016): pure math on the CPU, shared by every backend and by
 * the game's mouse conversion, so both agree on where the field is.
 */
#include "renderer/camera2d_internal.h"

#include <reborn/assert.h>

#include <math.h>

/* NaN fails `> 0` too: a camera that would divide by zero or poison every vertex. */
#define CAMERA_IS_VALID(camera)                                                                    \
    ((camera)->size.x > 0.0f && (camera)->size.y > 0.0f && (camera)->zoom > 0.0f)

rb_camera2d rb_camera2d_make(rb_vec2 center, rb_vec2 size) {
    return (rb_camera2d){.center = center, .size = size, .zoom = 1.0f};
}

rb_camera2d_field rb_camera2d_field_rect(const rb_camera2d *camera, u32 width, u32 height) {
    RB_ASSERT(camera != NULL);
    RB_ASSERT(CAMERA_IS_VALID(camera));
    /* Pixels per world unit: the axis that runs out of room first sets it, the other gets bars. */
    f32 per_x = (f32)width / camera->size.x;
    f32 per_y = (f32)height / camera->size.y;
    f32 scale = per_x < per_y ? per_x : per_y;

    /* Never negative here: the cast to unsigned is defined (a negative float would not be). */
    rb_camera2d_field field;
    field.width = (u32)roundf(camera->size.x * scale);
    field.height = (u32)roundf(camera->size.y * scale);
    /* The limiting axis comes out as the whole target, give or take a rounding error. */
    field.width = field.width < width ? field.width : width;
    field.height = field.height < height ? field.height : height;
    /* Odd leftovers put the extra pixel in the right and bottom bars. */
    field.x = (width - field.width) / 2;
    field.y = (height - field.height) / 2;
    return field;
}

rb_mat4 rb_camera2d_projection(const rb_camera2d *camera) {
    RB_ASSERT(camera != NULL);
    RB_ASSERT(CAMERA_IS_VALID(camera));
    rb_vec2 half = rb_vec2_scale(camera->size, 0.5f / camera->zoom);
    rb_vec2 top_left = rb_vec2_sub(camera->center, half);
    rb_vec2 bottom_right = rb_vec2_add(camera->center, half);
    return rb_mat4_ortho(top_left.x, bottom_right.x, top_left.y, bottom_right.y);
}

rb_vec2 rb_camera2d_screen_to_world(const rb_camera2d *camera, rb_vec2 target, rb_vec2 point) {
    RB_ASSERT(camera != NULL);
    RB_ASSERT(CAMERA_IS_VALID(camera));
    /* Negative or NaN sizes count as none; the casts below need non-negative values. */
    if (!(target.x >= 1.0f && target.y >= 1.0f)) {
        return camera->center;
    }
    /* The same whole-pixel rectangle the renderer draws into, so a click lands where it looks. */
    rb_camera2d_field field = rb_camera2d_field_rect(camera, (u32)target.x, (u32)target.y);
    if (field.width == 0 || field.height == 0) {
        return camera->center;
    }
    rb_vec2 shown = rb_vec2_scale(camera->size, 1.0f / camera->zoom);
    rb_vec2 top_left = rb_vec2_sub(camera->center, rb_vec2_scale(shown, 0.5f));
    /* Where the point is across the field, 0 at its top-left corner and 1 at the opposite one. */
    rb_vec2 across = {
        (point.x - (f32)field.x) / (f32)field.width,
        (point.y - (f32)field.y) / (f32)field.height,
    };
    return rb_vec2_add(top_left, rb_vec2_mul(across, shown));
}
