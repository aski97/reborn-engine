/*
 * Renderer startup and shutdown through the public API (ADR 0013).
 *
 * Each test opens a window for the surface: built on Windows only, like the
 * application runs (the Linux container has no X server; Linux is checked with
 * the sandbox under Xvfb). Needs a Vulkan 1.3 driver, and the Vulkan SDK's
 * validation layers for the validation tests.
 */
#include "rb_test.h"

#include <reborn/platform.h>
#include <reborn/renderer.h>

#include "failing_allocator.h"

static rb_platform_state *platform;
static rb_window          window;

static bool open_window(void) {
    platform = rb_platform_startup(rb_heap_allocator());
    if (platform == NULL) {
        return false;
    }
    window = rb_window_create(
        platform, &(rb_window_desc){.title = "Renderer test", .width = 320, .height = 240});
    if (!rb_window_is_valid(platform, window)) {
        rb_platform_shutdown(platform);
        return false;
    }
    return true;
}

static void close_window(void) {
    rb_window_destroy(platform, window);
    rb_platform_shutdown(platform);
}

static rb_renderer *start(rb_allocator allocator, bool validation) {
    return rb_renderer_startup(allocator,
                               &(rb_renderer_config){.validation = validation, .vsync = true},
                               platform, window);
}

RB_TEST(renderer, starts_and_stops_without_validation) {
    RB_REQUIRE(open_window());
    rb_renderer *renderer = start(rb_heap_allocator(), false);
    RB_EXPECT(renderer != NULL);
    if (renderer != NULL) {
        RB_EXPECT(!rb_renderer_get_validation_stats(renderer).enabled);
        rb_renderer_shutdown(renderer);
    }
    close_window();
}

/* ADR 0025: vsync asks for FIFO, which every driver offers; the swapchain fits the window. */
RB_TEST(renderer, info_describes_gpu_and_swapchain) {
    RB_REQUIRE(open_window());
    rb_renderer *renderer = start(rb_heap_allocator(), false);
    RB_EXPECT(renderer != NULL);
    if (renderer != NULL) {
        rb_renderer_info info = rb_renderer_get_info(renderer);
        RB_REQUIRE(info.gpu_name != NULL && info.gpu_type != NULL && info.present_mode != NULL);
        RB_EXPECT(info.gpu_name[0] != '\0');
        RB_EXPECT_EQ_STR(info.present_mode, "FIFO");
        RB_EXPECT(info.vsync);
        rb_window_size size = rb_window_get_size(platform, window);
        RB_EXPECT_EQ_UINT(info.width, size.pixel_width);
        RB_EXPECT_EQ_UINT(info.height, size.pixel_height);
        RB_EXPECT(info.image_count >= 2);
        rb_renderer_shutdown(renderer);
    }
    close_window();
}

RB_TEST(renderer, starts_with_validation_and_no_errors) {
    RB_REQUIRE(open_window());
    rb_renderer *renderer = start(rb_heap_allocator(), true);
    RB_EXPECT(renderer != NULL);
    if (renderer != NULL) {
        rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
        RB_EXPECT(stats.enabled);
        RB_EXPECT_EQ_UINT(stats.errors, 0);
        RB_EXPECT_EQ_UINT(stats.warnings, 0);
        rb_renderer_shutdown(renderer);
    }
    close_window();
}

/* A window destroyed before startup: a logged failure, not a crash. */
RB_TEST(renderer, stale_window_fails) {
    RB_REQUIRE(open_window());
    rb_window stale = window;
    rb_window_destroy(platform, stale);
    rb_failing_allocator counting;
    rb_failing_allocator_init(&counting, rb_heap_allocator(), 0);
    RB_EXPECT(rb_renderer_startup(rb_failing_allocator_get(&counting),
                                  &(rb_renderer_config){.validation = true}, platform,
                                  stale) == NULL);
    RB_EXPECT_EQ_UINT(counting.live_bytes, 0);
    rb_platform_shutdown(platform);
}

RB_TEST(renderer, state_is_freed_through_its_allocator) {
    RB_REQUIRE(open_window());
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_renderer *renderer = start(rb_failing_allocator_get(&failing), true);
    RB_EXPECT(renderer != NULL);
    if (renderer != NULL) {
        RB_EXPECT(failing.live_bytes > 0);
        rb_renderer_shutdown(renderer);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
        RB_EXPECT_EQ_UINT(failing.free_count, failing.allocation_count);
    }
    close_window();
}

/*
 * Every allocation fails once: the state, and the temporary lists of layers,
 * extensions, GPUs, queue families, formats. Each failure must unwind
 * everything created before it.
 */
RB_TEST(renderer, every_failed_allocation_leaves_nothing) {
    RB_REQUIRE(open_window());
    for (u32 n = 1;; ++n) {
        rb_failing_allocator failing;
        rb_failing_allocator_init(&failing, rb_heap_allocator(), n);
        rb_renderer *renderer = start(rb_failing_allocator_get(&failing), true);
        if (renderer == NULL) {
            RB_EXPECT_EQ_UINT(failing.failure_count, 1);
            RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
            if (failing.failure_count != 1) {
                break; /* failed for another reason: looping would never end */
            }
            continue;
        }
        RB_EXPECT_EQ_UINT(rb_renderer_get_validation_stats(renderer).errors, 0);
        rb_renderer_shutdown(renderer);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
        /* n went past the last allocation: every one of them has failed once. */
        RB_EXPECT_EQ_UINT(failing.failure_count, 0);
        RB_EXPECT(n > 1);
        break;
    }
    close_window();
}
