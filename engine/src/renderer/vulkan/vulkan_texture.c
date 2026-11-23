/*
 * Textures (ADR 0017).
 */
#include "renderer/vulkan/vulkan_texture.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include "renderer/vulkan/vulkan_memory.h"

/* ============================================================================
 * Upload (C1): staging buffer, one-time command buffer, wait on a fence
 * ========================================================================== */

/* A layout transition of the whole image (one level, one layer), synchronization2. */
static void texture_barrier(const rb_vk_device *device, VkCommandBuffer commands, VkImage image,
                            VkImageLayout old_layout, VkImageLayout new_layout,
                            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
    };
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    device->vk.vkCmdPipelineBarrier2(commands, &dependency);
}

/*
 * The staging buffer into the image: the GPU's copy engine rearranges the rows
 * into its own tiling, which the CPU does not know.
 */
static bool texture_record_upload(const rb_vk_texture *texture, const rb_vk_device *device,
                                  VkCommandBuffer commands, VkBuffer staging) {
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VkResult result = device->vk.vkBeginCommandBuffer(commands, &begin);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkBeginCommandBuffer (upload) failed: %s", rb_vk_result_name(result));
        return false;
    }
    /* UNDEFINED: nothing to keep, the copy overwrites every pixel. */
    texture_barrier(device, commands, texture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region = {
        /* 0: rows follow each other with no padding, width pixels long. */
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {texture->width, texture->height, 1},
    };
    device->vk.vkCmdCopyBufferToImage(commands, staging, texture->image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    /*
     * The copy's writes before any fragment shader's reads. The second scope
     * covers everything submitted later on the queue: every frame that draws
     * the texture.
     */
    texture_barrier(device, commands, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    result = device->vk.vkEndCommandBuffer(commands);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkEndCommandBuffer (upload) failed: %s", rb_vk_result_name(result));
        return false;
    }
    return true;
}

static bool texture_submit_and_wait(const rb_vk_device *device, VkCommandBuffer commands,
                                    VkFence fence) {
    VkCommandBufferSubmitInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &buffer_info,
    };
    /* The frames' queue (ADR 0013, N1): the upload runs after the frames submitted before it. */
    VkResult result = device->vk.vkQueueSubmit2(device->queue, 1, &submit, fence);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkQueueSubmit2 (upload) failed: %s", rb_vk_result_name(result));
        return false;
    }
    result = device->vk.vkWaitForFences(device->handle, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkWaitForFences (upload) failed: %s", rb_vk_result_name(result));
        return false;
    }
    return true;
}

/* Copies `pixels` into the texture's image and leaves it ready for shaders. Blocks until done. */
static bool texture_upload(const rb_vk_texture *texture, const rb_vk_device *device,
                           const u8 *pixels) {
    VkDeviceSize size = (VkDeviceSize)texture->width * texture->height * 4;
    rb_vk_buffer staging;
    if (!rb_vk_buffer_create_host(&staging, device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        return false;
    }
    bool ok = rb_vk_buffer_write(&staging, device, pixels, size);

    /* Made and freed per upload: uploads are rare (loading), the objects cheap. */
    VkCommandPool           pool = VK_NULL_HANDLE;
    VkCommandBuffer         commands = VK_NULL_HANDLE;
    VkFence                 fence = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device->queue_family,
    };
    VkResult result = VK_SUCCESS;
    if (ok) {
        result = device->vk.vkCreateCommandPool(device->handle, &pool_info, NULL, &pool);
        ok = result == VK_SUCCESS;
        if (!ok) {
            RB_LOG_ERROR("vkCreateCommandPool (upload) failed: %s", rb_vk_result_name(result));
            pool = VK_NULL_HANDLE;
        }
    }
    if (ok) {
        VkCommandBufferAllocateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        result = device->vk.vkAllocateCommandBuffers(device->handle, &buffer_info, &commands);
        ok = result == VK_SUCCESS;
        if (!ok) {
            RB_LOG_ERROR("vkAllocateCommandBuffers (upload) failed: %s", rb_vk_result_name(result));
        }
    }
    if (ok) {
        VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        result = device->vk.vkCreateFence(device->handle, &fence_info, NULL, &fence);
        ok = result == VK_SUCCESS;
        if (!ok) {
            RB_LOG_ERROR("vkCreateFence (upload) failed: %s", rb_vk_result_name(result));
            fence = VK_NULL_HANDLE;
        }
    }
    ok = ok && texture_record_upload(texture, device, commands, staging.handle);
    ok = ok && texture_submit_and_wait(device, commands, fence);

    /* After the wait: the GPU no longer reads the staging buffer. */
    if (fence != VK_NULL_HANDLE) {
        device->vk.vkDestroyFence(device->handle, fence, NULL);
    }
    if (pool != VK_NULL_HANDLE) {
        device->vk.vkDestroyCommandPool(device->handle, pool, NULL);
    }
    rb_vk_buffer_destroy(&staging, device);
    return ok;
}

/* ============================================================================
 * One texture
 * ========================================================================== */

/* The GPU must be done with it. VK_NULL_HANDLE members are skipped; zeroes *texture. */
static void texture_destroy(rb_vk_texture *texture, const rb_vk_textures *textures,
                            const rb_vk_device *device) {
    if (texture->set != VK_NULL_HANDLE) {
        /* Allowed by the pool's FREE_DESCRIPTOR_SET_BIT. */
        device->vk.vkFreeDescriptorSets(device->handle, textures->pool, 1, &texture->set);
    }
    if (texture->view != VK_NULL_HANDLE) {
        device->vk.vkDestroyImageView(device->handle, texture->view, NULL);
    }
    /* The image before its memory: memory must not be freed under a bound resource. */
    if (texture->image != VK_NULL_HANDLE) {
        device->vk.vkDestroyImage(device->handle, texture->image, NULL);
    }
    if (texture->memory != VK_NULL_HANDLE) {
        device->vk.vkFreeMemory(device->handle, texture->memory, NULL);
    }
    memset(texture, 0, sizeof(*texture));
}

/* The image and its memory, device-local where the GPU allows it. */
static bool texture_create_image(rb_vk_texture *texture, const rb_vk_device *device) {
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = RB_VK_TEXTURE_FORMAT,
        .extent = {texture->width, texture->height, 1},
        .mipLevels = 1, /* no smaller copies: no mipmaps (E) */
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        /* The GPU's own arrangement: fast to sample, unknown to the CPU, hence the staging copy. */
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        /* The only other choice, PREINITIALIZED, is for linear tiling. */
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = device->vk.vkCreateImage(device->handle, &info, NULL, &texture->image);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateImage (%ux%u) failed: %s", texture->width, texture->height,
                     rb_vk_result_name(result));
        texture->image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements requirements;
    device->vk.vkGetImageMemoryRequirements(device->handle, texture->image, &requirements);
    if (!rb_vk_memory_allocate(device, &requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                               "texture", &texture->memory)) {
        return false;
    }
    result = device->vk.vkBindImageMemory(device->handle, texture->image, texture->memory, 0);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkBindImageMemory failed: %s", rb_vk_result_name(result));
        return false;
    }
    return true;
}

static bool texture_create_view(rb_vk_texture *texture, const rb_vk_device *device) {
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = RB_VK_TEXTURE_FORMAT,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
    };
    VkResult result = device->vk.vkCreateImageView(device->handle, &info, NULL, &texture->view);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateImageView (texture) failed: %s", rb_vk_result_name(result));
        texture->view = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* A set from the pool, pointed at the texture's view and the sampler of its filter. */
static bool texture_create_set(rb_vk_texture *texture, const rb_vk_textures *textures,
                               const rb_vk_device *device, rb_texture_filter filter) {
    VkDescriptorSetAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = textures->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &textures->set_layout,
    };
    VkResult result = device->vk.vkAllocateDescriptorSets(device->handle, &info, &texture->set);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkAllocateDescriptorSets failed: %s", rb_vk_result_name(result));
        texture->set = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorImageInfo image = {
        .sampler = textures->samplers[filter],
        .imageView = texture->view,
        /* The layout the image will be in whenever a shader reads it. */
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture->set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image,
    };
    device->vk.vkUpdateDescriptorSets(device->handle, 1, &write, 0, NULL);
    return true;
}

/* False (logged): *texture is zeroed, nothing left created. */
static bool texture_create(rb_vk_texture *texture, const rb_vk_textures *textures,
                           const rb_vk_device *device, const rb_texture_desc *desc) {
    memset(texture, 0, sizeof(*texture));
    texture->width = desc->width;
    texture->height = desc->height;
    bool ok = texture_create_image(texture, device) &&
              texture_upload(texture, device, desc->pixels) &&
              texture_create_view(texture, device) &&
              texture_create_set(texture, textures, device, desc->filter);
    if (!ok) {
        texture_destroy(texture, textures, device);
    }
    return ok;
}

/* ============================================================================
 * Shared objects
 * ========================================================================== */

static bool textures_create_set_layout(rb_vk_textures *textures, const rb_vk_device *device) {
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        /* Image and sampler together: `sampler2D` in GLSL. */
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VkResult result =
        device->vk.vkCreateDescriptorSetLayout(device->handle, &info, NULL, &textures->set_layout);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateDescriptorSetLayout failed: %s", rb_vk_result_name(result));
        textures->set_layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool textures_create_pool(rb_vk_textures *textures, const rb_vk_device *device) {
    /* Every texture slot and the white texture: a set each, never more. */
    const u32            sets = RB_RENDERER_MAX_TEXTURES + 1;
    VkDescriptorPoolSize size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = sets,
    };
    VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        /*
         * Sets are freed one by one as textures go. All sets share one layout,
         * so the pool cannot fragment (the spec rules VK_ERROR_FRAGMENTED_POOL
         * out then).
         */
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = sets,
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    VkResult result =
        device->vk.vkCreateDescriptorPool(device->handle, &info, NULL, &textures->pool);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateDescriptorPool failed: %s", rb_vk_result_name(result));
        textures->pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool textures_create_sampler(VkSampler *sampler, const rb_vk_device *device,
                                    VkFilter filter) {
    VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter, /* texture enlarged */
        .minFilter = filter, /* texture shrunk */
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        /*
         * Linear filtering at an edge blends with the pixel beyond it: with
         * repeat, the opposite edge; in an atlas, the neighbor. Clamp repeats
         * the edge itself.
         */
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE, /* for surfaces seen at an angle: 3D */
        .maxAnisotropy = 1.0f,
        .minLod = 0.0f,
        .maxLod = 0.0f, /* one level */
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE, /* 0..1 across the texture, whatever its size */
    };
    VkResult result = device->vk.vkCreateSampler(device->handle, &info, NULL, sampler);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateSampler failed: %s", rb_vk_result_name(result));
        *sampler = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool rb_vk_textures_create(rb_vk_textures *textures, const rb_vk_device *device) {
    RB_ASSERT(textures != NULL && device != NULL && device->handle != VK_NULL_HANDLE);
    memset(textures, 0, sizeof(*textures));
    /* Cannot fail: the capacity is a valid constant. */
    (void)rb_handle_pool_init(&textures->handles, textures->generations, textures->free_slots,
                              RB_RENDERER_MAX_TEXTURES);
    static const u8 white[4] = {255, 255, 255, 255};
    bool ok = textures_create_set_layout(textures, device) &&
              textures_create_pool(textures, device) &&
              textures_create_sampler(&textures->samplers[RB_TEXTURE_FILTER_NEAREST], device,
                                      VK_FILTER_NEAREST) &&
              textures_create_sampler(&textures->samplers[RB_TEXTURE_FILTER_LINEAR], device,
                                      VK_FILTER_LINEAR) &&
              texture_create(&textures->white, textures, device,
                             &(rb_texture_desc){1, 1, white, RB_TEXTURE_FILTER_NEAREST});
    return ok;
}

void rb_vk_textures_destroy(rb_vk_textures *textures, const rb_vk_device *device) {
    RB_ASSERT(textures != NULL && device != NULL);
    u32 alive = rb_handle_pool_count(&textures->handles);
    if (alive > 0) {
        /* Not a leak: the game may leave its textures to the renderer's shutdown. */
        RB_LOG_DEBUG("Freeing %u textures still alive", alive);
        for (u32 i = 0; i < RB_RENDERER_MAX_TEXTURES; ++i) {
            if (textures->slots[i].image != VK_NULL_HANDLE) {
                texture_destroy(&textures->slots[i], textures, device);
            }
        }
    }
    texture_destroy(&textures->white, textures, device);
    for (u32 i = 0; i < RB_ARRAY_COUNT(textures->samplers); ++i) {
        if (textures->samplers[i] != VK_NULL_HANDLE) {
            device->vk.vkDestroySampler(device->handle, textures->samplers[i], NULL);
        }
    }
    /* Frees the sets still allocated from it. */
    if (textures->pool != VK_NULL_HANDLE) {
        device->vk.vkDestroyDescriptorPool(device->handle, textures->pool, NULL);
    }
    if (textures->set_layout != VK_NULL_HANDLE) {
        device->vk.vkDestroyDescriptorSetLayout(device->handle, textures->set_layout, NULL);
    }
    memset(textures, 0, sizeof(*textures));
}

/* ============================================================================
 * Textures by handle
 * ========================================================================== */

u32 rb_vk_textures_add(rb_vk_textures *textures, const rb_vk_device *device,
                       const rb_texture_desc *desc) {
    RB_ASSERT(textures != NULL && device != NULL && desc != NULL && desc->pixels != NULL);
    RB_ASSERT(desc->filter == RB_TEXTURE_FILTER_NEAREST ||
              desc->filter == RB_TEXTURE_FILTER_LINEAR);
    u32 limit = device->properties.limits.maxImageDimension2D;
    if (desc->width == 0 || desc->height == 0 || desc->width > limit || desc->height > limit) {
        RB_LOG_ERROR("Texture size %ux%u not allowed: 1 to %u pixels per side", desc->width,
                     desc->height, limit);
        return RB_HANDLE_INVALID;
    }
    u32 id = rb_handle_pool_alloc(&textures->handles);
    if (id == RB_HANDLE_INVALID) {
        RB_LOG_ERROR("No free texture slot: %u alive (RB_RENDERER_MAX_TEXTURES)",
                     rb_handle_pool_count(&textures->handles));
        return RB_HANDLE_INVALID;
    }
    rb_vk_texture *texture = &textures->slots[rb_handle_index(id)];
    if (!texture_create(texture, textures, device, desc)) {
        rb_handle_pool_free(&textures->handles, id);
        return RB_HANDLE_INVALID;
    }
    RB_LOG_DEBUG("Texture 0x%08x created: %ux%u, %s", id, desc->width, desc->height,
                 desc->filter == RB_TEXTURE_FILTER_NEAREST ? "nearest" : "linear");
    return id;
}

bool rb_vk_textures_remove(rb_vk_textures *textures, const rb_vk_device *device, u32 id) {
    RB_ASSERT(textures != NULL && device != NULL);
    if (!rb_handle_pool_valid(&textures->handles, id)) {
        return false;
    }
    texture_destroy(&textures->slots[rb_handle_index(id)], textures, device);
    rb_handle_pool_free(&textures->handles, id);
    RB_LOG_DEBUG("Texture 0x%08x destroyed", id);
    return true;
}

const rb_vk_texture *rb_vk_textures_get(const rb_vk_textures *textures, u32 id) {
    RB_ASSERT(textures != NULL);
    if (!rb_handle_pool_valid(&textures->handles, id)) {
        return &textures->white;
    }
    return &textures->slots[rb_handle_index(id)];
}
