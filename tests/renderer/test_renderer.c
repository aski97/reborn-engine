/*
 * Renderer startup and shutdown through the public API (ADR 0013).
 *
 * Needs Vulkan 1.3: a GPU driver on Windows; lavapipe (mesa-vulkan-drivers)
 * in the Linux container, which has no GPU. The validation tests also need
 * the layers: Vulkan SDK on Windows, `vulkan-validationlayers` on Linux.
 */
#include "rb_test.h"

#include <reborn/renderer.h>

#include "failing_allocator.h"

RB_TEST(renderer, starts_and_stops_without_validation) {
    rb_renderer *renderer =
        rb_renderer_startup(rb_heap_allocator(), &(rb_renderer_config){.validation = false});
    RB_REQUIRE(renderer != NULL);
    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT(!stats.enabled);
    rb_renderer_shutdown(renderer);
}

RB_TEST(renderer, starts_with_validation_and_no_errors) {
    rb_renderer *renderer =
        rb_renderer_startup(rb_heap_allocator(), &(rb_renderer_config){.validation = true});
    RB_REQUIRE(renderer != NULL);
    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT(stats.enabled);
    RB_EXPECT_EQ_UINT(stats.errors, 0);
    RB_EXPECT_EQ_UINT(stats.warnings, 0);
    rb_renderer_shutdown(renderer);
}

RB_TEST(renderer, state_is_freed_through_its_allocator) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_renderer *renderer = rb_renderer_startup(rb_failing_allocator_get(&failing),
                                                &(rb_renderer_config){.validation = true});
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(failing.live_bytes > 0);
    rb_renderer_shutdown(renderer);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
    RB_EXPECT_EQ_UINT(failing.free_count, failing.allocation_count);
}

/* Every allocation, the temporary lists of layers and extensions included, fails once. */
RB_TEST(renderer, every_failed_allocation_leaves_nothing) {
    for (u32 n = 1;; ++n) {
        rb_failing_allocator failing;
        rb_failing_allocator_init(&failing, rb_heap_allocator(), n);
        rb_renderer *renderer = rb_renderer_startup(rb_failing_allocator_get(&failing),
                                                    &(rb_renderer_config){.validation = true});
        if (renderer == NULL) {
            RB_EXPECT_EQ_UINT(failing.failure_count, 1);
            RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
            continue;
        }
        rb_renderer_shutdown(renderer);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
        /* n went past the last allocation: every one of them has failed once. */
        RB_EXPECT_EQ_UINT(failing.failure_count, 0);
        RB_EXPECT(n > 1);
        break;
    }
}
