/*
 * Graphics pipeline (ADR 0015).
 */
#include "renderer/vulkan/vulkan_pipeline.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

/*
 * SPIR-V compiled at build time by glslc (ADR 0015, D1 and E1): each file is
 * a brace-enclosed list of 32-bit words, `{0x07230203, ...}`, generated into
 * the build folder from engine/src/renderer/vulkan/shaders/.
 */
static const u32 vertex_spirv[] =
#include "quad.vert.inc"
    ;
static const u32 fragment_spirv[] =
#include "quad.frag.inc"
    ;

/* Needed only while the pipeline is created: the driver compiles the code into the pipeline. */
static VkShaderModule pipeline_create_module(const rb_vk_device *device, const u32 *code,
                                             usize size, const char *name) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, /* bytes, not words */
        .pCode = code,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult       result = device->vk.vkCreateShaderModule(device->handle, &info, NULL, &module);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateShaderModule (%s) failed: %s", name, rb_vk_result_name(result));
        return VK_NULL_HANDLE;
    }
    return module;
}

static bool pipeline_create_layout(rb_vk_pipeline *pipeline, const rb_vk_device *device) {
    /* Both stages read the quad: the vertex shader its rectangle, the fragment shader its color. */
    VkPushConstantRange push = {
        .stageFlags = RB_VK_QUAD_PUSH_STAGES,
        .offset = 0,
        .size = sizeof(rb_vk_quad_push),
    };
    VkPipelineLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    VkResult result =
        device->vk.vkCreatePipelineLayout(device->handle, &info, NULL, &pipeline->layout);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreatePipelineLayout failed: %s", rb_vk_result_name(result));
        pipeline->layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool pipeline_create_handle(rb_vk_pipeline *pipeline, const rb_vk_device *device,
                                   VkShaderModule vertex, VkShaderModule fragment) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment,
            .pName = "main",
        },
    };
    /* No vertex buffers: the vertex shader makes its vertices from gl_VertexIndex (F1). */
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        /* Every 3 vertices make one triangle, none shared. */
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    /* Counts only: the rectangles are dynamic state, set every frame (G1). */
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        /* 2D has no back side: a triangle is drawn whichever way its vertices turn. */
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    /* One sample per pixel: no antialiasing. */
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    /*
     * Straight alpha (H2, rb_color): new * a + old * (1 - a). The image's own
     * alpha is not shown by the window, so its blend only needs to be sane.
     */
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    /* The window's size changes: set per frame instead of rebuilding the pipeline (G1). */
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = RB_ARRAY_COUNT(dynamic_states),
        .pDynamicStates = dynamic_states,
    };
    /* Dynamic rendering (ADR 0013, D3): the formats replace a render pass. */
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &pipeline->color_format,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = RB_ARRAY_COUNT(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = pipeline->layout,
    };
    /* No pipeline cache: drivers keep their own between runs. */
    VkResult result = device->vk.vkCreateGraphicsPipelines(device->handle, VK_NULL_HANDLE, 1, &info,
                                                           NULL, &pipeline->handle);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateGraphicsPipelines failed: %s", rb_vk_result_name(result));
        pipeline->handle = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool rb_vk_pipeline_create(rb_vk_pipeline *pipeline, const rb_vk_device *device,
                           VkFormat color_format) {
    RB_ASSERT(pipeline != NULL && device != NULL && device->handle != VK_NULL_HANDLE);
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->color_format = color_format;

    VkShaderModule vertex =
        pipeline_create_module(device, vertex_spirv, sizeof(vertex_spirv), "vertex");
    VkShaderModule fragment =
        pipeline_create_module(device, fragment_spirv, sizeof(fragment_spirv), "fragment");
    bool ok = vertex != VK_NULL_HANDLE && fragment != VK_NULL_HANDLE &&
              pipeline_create_layout(pipeline, device) &&
              pipeline_create_handle(pipeline, device, vertex, fragment);
    if (fragment != VK_NULL_HANDLE) {
        device->vk.vkDestroyShaderModule(device->handle, fragment, NULL);
    }
    if (vertex != VK_NULL_HANDLE) {
        device->vk.vkDestroyShaderModule(device->handle, vertex, NULL);
    }
    if (!ok) {
        rb_vk_pipeline_destroy(pipeline, device);
    }
    return ok;
}

void rb_vk_pipeline_destroy(rb_vk_pipeline *pipeline, const rb_vk_device *device) {
    RB_ASSERT(pipeline != NULL && device != NULL);
    if (pipeline->handle != VK_NULL_HANDLE) {
        device->vk.vkDestroyPipeline(device->handle, pipeline->handle, NULL);
    }
    if (pipeline->layout != VK_NULL_HANDLE) {
        device->vk.vkDestroyPipelineLayout(device->handle, pipeline->layout, NULL);
    }
    memset(pipeline, 0, sizeof(*pipeline));
}
