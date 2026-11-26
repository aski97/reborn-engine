/*
 * Device, swapchain, frames and the quad pipeline on a real window (ADR 0013,
 * J–W; ADR 0015). Built on Windows only, like test_renderer.c: the Linux
 * container has no X server.
 */
#include "rb_test.h"

#include <reborn/platform.h>

#include "renderer/renderer_internal.h"
#include "renderer/vulkan/vulkan_renderer.h"

static rb_platform_state *platform;
static rb_window          window;

static rb_renderer *start(bool vsync) {
    platform = rb_platform_startup(rb_heap_allocator());
    if (platform == NULL) {
        return NULL;
    }
    window = rb_window_create(
        platform, &(rb_window_desc){.title = "Vulkan renderer test", .width = 320, .height = 240});
    rb_renderer *renderer = NULL;
    if (rb_window_is_valid(platform, window)) {
        renderer = rb_renderer_startup(rb_heap_allocator(),
                                       &(rb_renderer_config){.validation = true, .vsync = vsync},
                                       platform, window);
        if (renderer != NULL) {
            return renderer;
        }
        rb_window_destroy(platform, window);
    }
    rb_platform_shutdown(platform);
    return NULL;
}

static void stop(rb_renderer *renderer) {
    rb_renderer_shutdown(renderer);
    rb_window_destroy(platform, window);
    rb_platform_shutdown(platform);
}

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

static void expect_no_validation_messages(const rb_renderer *renderer) {
    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT_EQ_UINT(stats.errors, 0);
    RB_EXPECT_EQ_UINT(stats.warnings, 0);
}

RB_TEST(vulkan_renderer, device_table_and_queue_are_loaded) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(renderer->surface != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.handle != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.queue != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.properties.apiVersion >= VK_API_VERSION_1_3);
#define TEST_EXPECT_LOADED(fn) RB_EXPECT(renderer->device.vk.fn != NULL);
    RB_VK_DEVICE_FUNCTIONS(TEST_EXPECT_LOADED)
    RB_VK_SWAPCHAIN_FUNCTIONS(TEST_EXPECT_LOADED)
#undef TEST_EXPECT_LOADED
    stop(renderer);
}

RB_TEST(vulkan_renderer, swapchain_matches_the_window) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_window_size         size = rb_window_get_size(platform, window);
    const rb_vk_swapchain *swapchain = &renderer->swapchain;
    RB_EXPECT(swapchain->handle != VK_NULL_HANDLE);
    RB_EXPECT_EQ_UINT(swapchain->extent.width, size.pixel_width);
    RB_EXPECT_EQ_UINT(swapchain->extent.height, size.pixel_height);
    RB_EXPECT_EQ_INT(swapchain->present_mode, VK_PRESENT_MODE_FIFO_KHR);
    RB_EXPECT_EQ_INT(swapchain->format.colorSpace, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    RB_EXPECT(swapchain->image_count >= 2);
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        RB_EXPECT(swapchain->images[i] != VK_NULL_HANDLE);
        RB_EXPECT(swapchain->views[i] != VK_NULL_HANDLE);
    }
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, recreating_replaces_the_swapchain) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    rb_window_size size = rb_window_get_size(platform, window);
    RB_EXPECT(rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                     renderer->surface, rb_heap_allocator(), size.pixel_width,
                                     size.pixel_height, true));
    RB_EXPECT(renderer->swapchain.handle != VK_NULL_HANDLE);
    RB_EXPECT(renderer->swapchain.handle != first);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, frames_in_flight_are_created) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    for (u32 i = 0; i < RB_VK_FRAMES_IN_FLIGHT; ++i) {
        const rb_vk_frame *frame = &renderer->frames[i];
        RB_EXPECT(frame->command_pool != VK_NULL_HANDLE);
        RB_EXPECT(frame->commands != VK_NULL_HANDLE);
        RB_EXPECT(frame->image_acquired != VK_NULL_HANDLE);
        RB_EXPECT(frame->done != VK_NULL_HANDLE);
    }
    for (u32 i = 0; i < renderer->swapchain.image_count; ++i) {
        RB_EXPECT(renderer->swapchain.rendered[i] != VK_NULL_HANDLE);
    }
    stop(renderer);
}

RB_TEST(vulkan_renderer, begin_frame_shows_a_visible_window) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    RB_EXPECT(rb_renderer_begin_frame(renderer));
    RB_EXPECT(renderer->in_frame);
    /* Same size: nothing to rebuild. */
    RB_EXPECT(renderer->swapchain.handle == first);
    rb_renderer_end_frame(renderer);
    RB_EXPECT(!renderer->in_frame);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/*
 * More frames than copies and than images: every copy and every image is
 * reused, the case where a missing wait or a shared semaphore shows up in
 * the validation layers.
 */
RB_TEST(vulkan_renderer, frames_cycle_through_their_copies) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    for (u32 i = 0; i < 3 * RB_VK_SWAPCHAIN_MAX_IMAGES; ++i) {
        RB_EXPECT_EQ_UINT(renderer->frame_index, i % RB_VK_FRAMES_IN_FLIGHT);
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        RB_EXPECT(renderer->image_index < renderer->swapchain.image_count);
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* Pretends the swapchain was built for another size, as after a resize. */
RB_TEST(vulkan_renderer, begin_frame_rebuilds_when_the_size_changed) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    /* A frame first: the rebuild must also wait for frames still on the GPU. */
    RB_REQUIRE(rb_renderer_begin_frame(renderer));
    rb_renderer_end_frame(renderer);
    VkSwapchainKHR first = renderer->swapchain.handle;
    renderer->built_width += 1;
    RB_EXPECT(rb_renderer_begin_frame(renderer));
    RB_EXPECT(renderer->swapchain.handle != first);
    rb_window_size size = rb_window_get_size(platform, window);
    RB_EXPECT_EQ_UINT(renderer->built_width, size.pixel_width);
    rb_renderer_end_frame(renderer);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* As after VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR, at the same size. */
RB_TEST(vulkan_renderer, begin_frame_rebuilds_a_stale_swapchain) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    renderer->swapchain_stale = true;
    RB_EXPECT(rb_renderer_begin_frame(renderer));
    RB_EXPECT(renderer->swapchain.handle != first);
    RB_EXPECT(!renderer->swapchain_stale);
    rb_renderer_end_frame(renderer);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, a_failed_renderer_shows_nothing) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    renderer->failed = true;
    RB_EXPECT(!rb_renderer_begin_frame(renderer));
    RB_EXPECT(!renderer->in_frame);
    stop(renderer);
}

RB_TEST(vulkan_renderer, clear_color_is_kept_linear) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_renderer_set_clear_color(renderer, rb_color_srgb(0.5f, 0.0f, 1.0f, 1.0f));
    EXPECT_F32_NEAR(renderer->clear_color.r, 0.214041f, 1e-5f);
    EXPECT_F32_NEAR(renderer->clear_color.g, 0.0f, 1e-7f);
    EXPECT_F32_NEAR(renderer->clear_color.b, 1.0f, 1e-6f);
    RB_EXPECT(rb_renderer_begin_frame(renderer));
    rb_renderer_end_frame(renderer);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* ============================================================================
 * Pipeline and quads (ADR 0015)
 * ========================================================================== */

RB_TEST(vulkan_renderer, pipeline_is_built_for_the_swapchain_format) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(renderer->pipeline.layout != VK_NULL_HANDLE);
    RB_EXPECT(renderer->pipeline.handle != VK_NULL_HANDLE);
    RB_EXPECT_EQ_INT(renderer->pipeline.color_format, renderer->swapchain.format.format);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* As if a rebuilt swapchain came with another format: the pipeline follows it. */
RB_TEST(vulkan_renderer, a_new_swapchain_format_rebuilds_the_pipeline) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkPipeline first = renderer->pipeline.handle;
    renderer->pipeline.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    renderer->swapchain_stale = true;
    RB_EXPECT(rb_renderer_begin_frame(renderer));
    RB_EXPECT(renderer->pipeline.handle != VK_NULL_HANDLE);
    RB_EXPECT(renderer->pipeline.handle != first);
    RB_EXPECT_EQ_INT(renderer->pipeline.color_format, renderer->swapchain.format.format);
    rb_renderer_end_frame(renderer);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/*
 * Every kind of quad a game may send, over more frames than copies: partly or
 * wholly outside the window, empty, fractional, translucent. Validation would
 * report a draw without a pipeline, viewport or push constants.
 */
RB_TEST(vulkan_renderer, quads_are_drawn_without_validation_messages) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_color red = rb_color_srgb(1.0f, 0.0f, 0.0f, 1.0f);
    for (u32 i = 0; i < 2 * RB_VK_FRAMES_IN_FLIGHT + 1; ++i) {
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        rb_renderer_draw_quad(renderer, 10.0f, 10.0f, 50.0f, 50.0f, red);
        rb_renderer_draw_quad(renderer, -20.0f, -20.0f, 40.0f, 40.0f, red);
        rb_renderer_draw_quad(renderer, 1000.0f, 1000.0f, 40.0f, 40.0f, red);
        rb_renderer_draw_quad(renderer, 30.0f, 30.0f, 0.0f, 0.0f, red);
        rb_renderer_draw_quad(renderer, 12.25f, 7.5f, 33.75f, 20.5f,
                              rb_color_srgb(0.0f, 0.5f, 1.0f, 0.5f));
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* ============================================================================
 * 2D camera (ADR 0016)
 * ========================================================================== */

/*
 * World, then pixels, then world again within each frame: viewport, scissor
 * and the view change between draws. Validation would report a bad viewport
 * or a push outside the layout's range.
 */
RB_TEST(vulkan_renderer, camera_switches_spaces_without_validation_messages) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    /* 4:3 window, 16:9 field: bars above and below. */
    rb_camera2d camera = rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
    rb_color    red = rb_color_srgb(1.0f, 0.0f, 0.0f, 1.0f);
    for (u32 i = 0; i < 2 * RB_VK_FRAMES_IN_FLIGHT + 1; ++i) {
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        rb_renderer_set_camera(renderer, &camera);
        rb_renderer_draw_quad(renderer, 0.0f, 0.0f, 16.0f, 9.0f, red);
        rb_renderer_draw_quad(renderer, -1.0f, 8.5f, 2.0f, 2.0f, red); /* across the field's edge */
        rb_renderer_set_camera(renderer, NULL);
        rb_renderer_draw_quad(renderer, 10.0f, 10.0f, 50.0f, 20.0f, red);
        camera.zoom = 2.0f;
        rb_renderer_set_camera(renderer, &camera);
        rb_renderer_draw_quad(renderer, 7.5f, 4.0f, 1.0f, 1.0f, red);
        camera.zoom = 1.0f;
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* A field so flat it rounds to no pixels: a 1-pixel viewport, an empty scissor, nothing drawn. */
RB_TEST(vulkan_renderer, an_empty_field_draws_nothing_without_validation_messages) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_camera2d camera = rb_camera2d_make((rb_vec2){0.0f, 0.0f}, (rb_vec2){10000.0f, 1.0f});
    RB_REQUIRE(rb_renderer_begin_frame(renderer));
    rb_renderer_set_camera(renderer, &camera);
    rb_renderer_draw_quad(renderer, -10.0f, -1.0f, 20.0f, 2.0f,
                          rb_color_srgb(1.0f, 0.0f, 0.0f, 1.0f));
    rb_renderer_end_frame(renderer);
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* ============================================================================
 * Textures (ADR 0017)
 * ========================================================================== */

/* 2x2: red, green / blue, half-transparent white. */
static const u8 test_pixels[2 * 2 * 4] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128,
};

static rb_texture create_test_texture(rb_renderer *renderer, rb_texture_filter filter) {
    return rb_renderer_create_texture(
        renderer,
        &(rb_texture_desc){.width = 2, .height = 2, .pixels = test_pixels, .filter = filter});
}

RB_TEST(vulkan_renderer, startup_creates_the_shared_texture_objects) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    const rb_vk_textures *textures = &renderer->textures;
    RB_EXPECT(textures->set_layout != VK_NULL_HANDLE);
    RB_EXPECT(textures->pool != VK_NULL_HANDLE);
    RB_EXPECT(textures->samplers[RB_TEXTURE_FILTER_NEAREST] != VK_NULL_HANDLE);
    RB_EXPECT(textures->samplers[RB_TEXTURE_FILTER_LINEAR] != VK_NULL_HANDLE);
    RB_EXPECT(textures->white.image != VK_NULL_HANDLE);
    RB_EXPECT(textures->white.set != VK_NULL_HANDLE);
    RB_EXPECT_EQ_UINT(textures->white.width, 1);
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&textures->handles), 0);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, a_texture_lives_until_destroyed) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_texture texture = create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST);
    RB_EXPECT(texture.id != 0);
    RB_EXPECT(rb_renderer_texture_is_valid(renderer, texture));
    const rb_vk_texture *slot = rb_vk_textures_get(&renderer->textures, texture.id);
    RB_EXPECT(slot != &renderer->textures.white);
    RB_EXPECT(slot->image != VK_NULL_HANDLE && slot->memory != VK_NULL_HANDLE);
    RB_EXPECT(slot->view != VK_NULL_HANDLE && slot->set != VK_NULL_HANDLE);
    RB_EXPECT_EQ_UINT(slot->width, 2);
    RB_EXPECT_EQ_UINT(slot->height, 2);

    rb_renderer_destroy_texture(renderer, texture);
    RB_EXPECT(!rb_renderer_texture_is_valid(renderer, texture));
    /* Stale now: the white texture stands in, a second destroy is ignored. */
    RB_EXPECT(rb_vk_textures_get(&renderer->textures, texture.id) == &renderer->textures.white);
    rb_renderer_destroy_texture(renderer, texture);
    rb_renderer_destroy_texture(renderer, (rb_texture){0});
    RB_EXPECT(!rb_renderer_texture_is_valid(renderer, (rb_texture){0}));
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, a_texture_size_outside_the_limits_fails) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    u32 limit = renderer->device.properties.limits.maxImageDimension2D;
    u32 sizes[][2] = {{0, 2}, {2, 0}, {limit + 1, 1}, {1, limit + 1}};
    for (usize i = 0; i < RB_ARRAY_COUNT(sizes); ++i) {
        /* Rejected before the pixels are read: two pixels are enough. */
        rb_texture texture = rb_renderer_create_texture(
            renderer,
            &(rb_texture_desc){.width = sizes[i][0], .height = sizes[i][1], .pixels = test_pixels});
        RB_EXPECT_EQ_UINT(texture.id, 0);
    }
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&renderer->textures.handles), 0);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* Every slot taken: the next one fails, a freed slot is handed out again. */
RB_TEST(vulkan_renderer, textures_fill_every_slot) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    static rb_texture textures[RB_RENDERER_MAX_TEXTURES];
    for (u32 i = 0; i < RB_RENDERER_MAX_TEXTURES; ++i) {
        textures[i] = create_test_texture(renderer, RB_TEXTURE_FILTER_LINEAR);
        RB_EXPECT(textures[i].id != 0);
    }
    RB_EXPECT_EQ_UINT(create_test_texture(renderer, RB_TEXTURE_FILTER_LINEAR).id, 0);
    rb_renderer_destroy_texture(renderer, textures[7]);
    rb_texture again = create_test_texture(renderer, RB_TEXTURE_FILTER_LINEAR);
    RB_EXPECT(again.id != 0 && again.id != textures[7].id);
    expect_no_validation_messages(renderer);
    /* The rest is left to shutdown, as a game may. */
    stop(renderer);
}

/*
 * Plain and textured quads mixed, both filters, a texture created inside a
 * frame, zero and stale handles, over more frames than copies: validation
 * would report a draw without a set, a set in the wrong layout, an image not
 * yet uploaded.
 */
RB_TEST(vulkan_renderer, textured_quads_are_drawn_without_validation_messages) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_texture nearest = create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST);
    rb_texture linear = create_test_texture(renderer, RB_TEXTURE_FILTER_LINEAR);
    rb_texture stale = create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST);
    rb_renderer_destroy_texture(renderer, stale);
    rb_color   white = rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f);
    rb_texture late = {0};
    for (u32 i = 0; i < 2 * RB_VK_FRAMES_IN_FLIGHT + 1; ++i) {
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        rb_renderer_draw_quad(renderer, 0.0f, 0.0f, 20.0f, 20.0f, white);
        rb_renderer_draw_texture(renderer, 20.0f, 0.0f, 64.0f, 64.0f, nearest, white);
        rb_renderer_draw_texture(renderer, 90.0f, 0.0f, 64.0f, 64.0f, nearest, white);
        rb_renderer_draw_texture(renderer, 20.0f, 70.0f, 64.0f, 64.0f, linear,
                                 rb_color_srgb(1.0f, 0.5f, 0.5f, 0.5f));
        rb_renderer_draw_quad(renderer, 0.0f, 30.0f, 10.0f, 10.0f, white);
        rb_renderer_draw_texture(renderer, 160.0f, 0.0f, 10.0f, 10.0f, (rb_texture){0}, white);
        rb_renderer_draw_texture(renderer, 160.0f, 20.0f, 10.0f, 10.0f, stale, white);
        if (i == 1) {
            late = create_test_texture(renderer, RB_TEXTURE_FILTER_LINEAR);
            RB_EXPECT(late.id != 0);
        }
        if (late.id != 0) {
            rb_renderer_draw_texture(renderer, 200.0f, 100.0f, 32.0f, 32.0f, late, white);
        }
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/*
 * Regions (ADR 0018): one texel, the whole texture, past its edge, mirrored
 * by a negative size, on a zero and a stale handle, in pixels and through a
 * camera. Validation would report the push constants overflowing their range.
 */
RB_TEST(vulkan_renderer, texture_regions_are_drawn_without_validation_messages) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_texture texture = create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST);
    rb_texture stale = create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST);
    rb_renderer_destroy_texture(renderer, stale);
    rb_camera2d camera = rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
    rb_color    white = rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f);
    rb_rect     regions[] = {
        {1.0f, 0.0f, 1.0f, 1.0f},  {0.0f, 0.0f, 2.0f, 2.0f},  {1.5f, 1.5f, 4.0f, 4.0f},
        {2.0f, 0.0f, -2.0f, 2.0f}, {0.25f, 0.5f, 0.5f, 0.5f},
    };
    for (u32 i = 0; i < 2 * RB_VK_FRAMES_IN_FLIGHT + 1; ++i) {
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        for (u32 r = 0; r < sizeof(regions) / sizeof(regions[0]); ++r) {
            rb_renderer_draw_texture_region(renderer, (f32)r * 40.0f, 0.0f, 32.0f, 32.0f, texture,
                                            regions[r], white);
        }
        rb_renderer_draw_texture_region(renderer, 0.0f, 40.0f, 10.0f, 10.0f, (rb_texture){0},
                                        regions[0], white);
        rb_renderer_draw_texture_region(renderer, 20.0f, 40.0f, 10.0f, 10.0f, stale, regions[0],
                                        white);
        rb_renderer_set_camera(renderer, &camera);
        rb_renderer_draw_texture_region(renderer, 1.0f, 1.0f, 2.0f, 2.0f, texture, regions[1],
                                        white);
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(!renderer->failed);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* Destroying the shared objects with textures alive frees them too: validation sees no leak. */
RB_TEST(vulkan_renderer, destroying_the_textures_frees_those_alive) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    for (u32 i = 0; i < 3; ++i) {
        RB_EXPECT(create_test_texture(renderer, RB_TEXTURE_FILTER_NEAREST).id != 0);
    }
    /* The pipeline keeps no reference to the set layout once built: destroying it is allowed. */
    rb_vk_textures_destroy(&renderer->textures, &renderer->device);
    RB_EXPECT_EQ_UINT(rb_handle_pool_count(&renderer->textures.handles), 0);
    RB_EXPECT(renderer->textures.pool == VK_NULL_HANDLE);
    expect_no_validation_messages(renderer);
    /* Shutdown finds them zeroed and skips them. */
    stop(renderer);
}

/* ============================================================================
 * Frame capture (ADR 0024)
 * ========================================================================== */

/* Channel `c` of pixel (x, y) in a captured image, within `eps` of `expected`. */
static bool pixel_near(const rb_image *image, u32 x, u32 y, const u8 expected[4], int eps) {
    const u8 *p = image->pixels + ((usize)y * image->width + x) * 4;
    for (int c = 0; c < 4; ++c) {
        int diff = (int)p[c] - expected[c];
        if (diff < -eps || diff > eps) {
            return false;
        }
    }
    return true;
}

RB_TEST(vulkan_renderer, swapchain_images_can_be_captured) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    /* Every desktop driver we run on offers it; without it the feature is gone. */
    RB_EXPECT(renderer->swapchain.capturable);
    RB_EXPECT(rb_vk_capture_supports(renderer->swapchain.format.format));
    stop(renderer);
}

RB_TEST(vulkan_renderer, nothing_captured_until_asked) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_REQUIRE(rb_renderer_begin_frame(renderer));
    rb_renderer_end_frame(renderer);
    RB_EXPECT(rb_renderer_captured_image(renderer) == NULL);
    stop(renderer);
}

/*
 * The clear color and a quad come back as the window shows them: sRGB bytes,
 * RGBA order whatever the swapchain's, opaque, at the swapchain's size.
 */
RB_TEST(vulkan_renderer, a_captured_frame_holds_what_was_drawn) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_renderer_set_clear_color(renderer, rb_color_srgb(0.2f, 0.4f, 0.6f, 1.0f));
    RB_REQUIRE(rb_renderer_begin_frame(renderer));
    rb_renderer_capture_frame(renderer);
    /* Drawn after the booking: still in the capture, taken when the frame ends. */
    rb_renderer_draw_quad(renderer, 8.0f, 8.0f, 16.0f, 16.0f,
                          rb_color_srgb(1.0f, 0.0f, 0.0f, 1.0f));
    rb_renderer_end_frame(renderer);

    const rb_image *image = rb_renderer_captured_image(renderer);
    RB_REQUIRE(image != NULL);
    RB_EXPECT_EQ_UINT(image->width, renderer->swapchain.extent.width);
    RB_EXPECT_EQ_UINT(image->height, renderer->swapchain.extent.height);
    static const u8 clear[4] = {51, 102, 153, 255};
    static const u8 red[4] = {255, 0, 0, 255};
    RB_EXPECT(pixel_near(image, 16, 16, red, 0));
    RB_EXPECT(pixel_near(image, 4, 4, clear, 1));
    RB_EXPECT(pixel_near(image, image->width - 1, image->height - 1, clear, 1));
    expect_no_validation_messages(renderer);
    stop(renderer);
}

/* A booking is for one frame: the next frame is not captured, the last capture stays. */
RB_TEST(vulkan_renderer, a_capture_is_for_one_frame) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_REQUIRE(rb_renderer_begin_frame(renderer));
    rb_renderer_capture_frame(renderer);
    rb_renderer_end_frame(renderer);
    const rb_image *first = rb_renderer_captured_image(renderer);
    RB_REQUIRE(first != NULL);
    for (u32 i = 0; i < 4; ++i) {
        if (!rb_renderer_begin_frame(renderer)) {
            continue;
        }
        RB_EXPECT(!renderer->capture.requested);
        rb_renderer_end_frame(renderer);
    }
    RB_EXPECT(rb_renderer_captured_image(renderer) == first);
    RB_EXPECT(renderer->capture.buffer.handle == VK_NULL_HANDLE);
    expect_no_validation_messages(renderer);
    stop(renderer);
}
