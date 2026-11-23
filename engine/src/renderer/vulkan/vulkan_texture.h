/*
 * Textures (ADR 0017): images in device memory that the quad shader samples.
 *
 * A texture is an image (the pixels, in the GPU's own tiling), its memory, a
 * view (how shaders see it: 2D, RGBA) and a descriptor set (binding 0: the
 * view with a sampler), which a draw binds so the fragment shader reads it.
 * What every texture shares lives next to them: the set layout the pipeline
 * is built with, the pool the sets come from, the two samplers, and a 1x1
 * white texture that plain quads are drawn with.
 */
#ifndef RB_VULKAN_TEXTURE_H
#define RB_VULKAN_TEXTURE_H

#include <reborn/defines.h>
#include <reborn/renderer.h>

#include "containers/handle_pool.h"
#include "renderer/vulkan/vulkan_device.h"

/* sRGB: the sampler decodes the color to linear, alpha stays linear (F1). */
#define RB_VK_TEXTURE_FORMAT VK_FORMAT_R8G8B8A8_SRGB

typedef struct rb_vk_texture {
    VkImage         image;
    VkDeviceMemory  memory; /* one allocation per texture (B1) */
    VkImageView     view;
    VkDescriptorSet set; /* from rb_vk_textures.pool, written once at creation */
    u32             width;
    u32             height;
} rb_vk_texture;

typedef struct rb_vk_textures {
    /* Binding 0: one combined image sampler, read by the fragment shader. */
    VkDescriptorSetLayout set_layout;
    /* Sets for RB_RENDERER_MAX_TEXTURES textures and the white one, freed one by one. */
    VkDescriptorPool pool;
    VkSampler        samplers[2]; /* indexed by rb_texture_filter */
    rb_vk_texture    white;       /* not in the handle pool: no handle names it */
    rb_handle_pool   handles;
    u16              generations[RB_RENDERER_MAX_TEXTURES];
    u32              free_slots[RB_RENDERER_MAX_TEXTURES];
    rb_vk_texture    slots[RB_RENDERER_MAX_TEXTURES]; /* indexed by rb_handle_index */
} rb_vk_textures;

/*
 * The shared objects and the white texture, which needs an upload: the
 * device's queue must be usable. False (logged): *textures holds what was
 * created, for rb_vk_textures_destroy.
 */
bool rb_vk_textures_create(rb_vk_textures *textures, const rb_vk_device *device);

/*
 * The GPU must be done with every texture (device idle). Frees the textures
 * still alive (logged), then the shared objects; zeroes *textures. A zeroed
 * *textures is ignored.
 */
void rb_vk_textures_destroy(rb_vk_textures *textures, const rb_vk_device *device);

/*
 * Creates a texture from `desc` and uploads its pixels, waiting for the copy.
 * Its id, or RB_HANDLE_INVALID on failure (logged): size 0 or beyond the GPU's
 * limit, no free slot, a Vulkan error.
 */
u32 rb_vk_textures_add(rb_vk_textures *textures, const rb_vk_device *device,
                       const rb_texture_desc *desc);

/*
 * Frees the texture `id` names. The GPU must be done with it: no recorded or
 * running frame may use it. False, changing nothing, when `id` is not live.
 */
bool rb_vk_textures_remove(rb_vk_textures *textures, const rb_vk_device *device, u32 id);

/* The texture `id` names; the white one when it names none (zero, stale). */
const rb_vk_texture *rb_vk_textures_get(const rb_vk_textures *textures, u32 id);

#endif /* RB_VULKAN_TEXTURE_H */
