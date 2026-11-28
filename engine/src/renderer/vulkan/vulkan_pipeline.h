/*
 * Graphics pipeline (ADR 0015): the shaders and the fixed-function state the
 * GPU runs a draw with, baked into one immutable object at startup. One for
 * now: quads, colored and textured (ADR 0017).
 */
#ifndef RB_VULKAN_PIPELINE_H
#define RB_VULKAN_PIPELINE_H

#include <reborn/defines.h>

#include <stddef.h>

#include "renderer/vulkan/vulkan_device.h"

/*
 * The shaders' push constants (ADR 0015, F1; ADR 0016; ADR 0027). Mirrors the
 * block in shaders/quad.glsl byte for byte: std430 starts a mat4 and a vec4 on
 * 16-byte boundaries, so `color` comes right after `view`, where it needs no
 * padding, and the vec2s (8-byte boundaries) follow. Pushed in two parts, since
 * push constants persist across draws: the view when the space changes, the
 * quad (from `color`) with each draw.
 */
typedef struct rb_vk_quad_push {
    f32 view[16];     /* the current space to normalized coordinates, column-major (rb_mat4) */
    f32 color[4];     /* linear, straight alpha; multiplies the texture */
    f32 origin[2];    /* the quad's top-left corner, turned (rb_sprite_quad) */
    f32 axis_x[2];    /* its top edge, turned */
    f32 axis_y[2];    /* its left edge, turned */
    f32 uv_offset[2]; /* the region's top-left corner, as fractions of the texture (ADR 0018) */
    f32 uv_size[2];   /* the region's size, as fractions of the texture */
} rb_vk_quad_push;

/* A mismatch would draw garbage without any error: checked at compile time. */
_Static_assert(offsetof(rb_vk_quad_push, color) == 64, "must match shaders/quad.glsl");
_Static_assert(offsetof(rb_vk_quad_push, origin) == 80, "must match shaders/quad.glsl");
_Static_assert(offsetof(rb_vk_quad_push, axis_x) == 88, "must match shaders/quad.glsl");
_Static_assert(offsetof(rb_vk_quad_push, axis_y) == 96, "must match shaders/quad.glsl");
_Static_assert(offsetof(rb_vk_quad_push, uv_offset) == 104, "must match shaders/quad.glsl");
_Static_assert(offsetof(rb_vk_quad_push, uv_size) == 112, "must match shaders/quad.glsl");
/* Vulkan guarantees at least 128 bytes of push constants. */
_Static_assert(sizeof(rb_vk_quad_push) == 120, "must match shaders/quad.glsl");

/* The part pushed with each draw. */
#define RB_VK_QUAD_PUSH_OFFSET offsetof(rb_vk_quad_push, color)
#define RB_VK_QUAD_PUSH_SIZE   (sizeof(rb_vk_quad_push) - RB_VK_QUAD_PUSH_OFFSET)

#define RB_VK_QUAD_PUSH_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)

typedef struct rb_vk_pipeline {
    /* What the shaders receive besides the vertices: the texture's set, the push constants. */
    VkPipelineLayout layout;
    VkPipeline       handle;
    /* The image format it draws into (dynamic rendering bakes it in): a change needs a rebuild. */
    VkFormat color_format;
} rb_vk_pipeline;

/*
 * Builds the quad pipeline for images of `color_format`, with the texture's
 * set at set 0 (`texture_layout`, rb_vk_textures.set_layout: must outlive the
 * pipeline). False (logged): *pipeline is zeroed.
 */
bool rb_vk_pipeline_create(rb_vk_pipeline *pipeline, const rb_vk_device *device,
                           VkFormat color_format, VkDescriptorSetLayout texture_layout);

/* The GPU must be done with it. Zeroes *pipeline; a zeroed pipeline is ignored. */
void rb_vk_pipeline_destroy(rb_vk_pipeline *pipeline, const rb_vk_device *device);

#endif /* RB_VULKAN_PIPELINE_H */
