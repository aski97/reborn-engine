/* Win32 platform startup and shutdown (ADR 0010, K1). Built on Windows only. */
#include "rb_test.h"

#include <reborn/platform.h>

#include "failing_allocator.h"

/* No window is opened: registering the window class needs no desktop interaction. */
RB_TEST(win32_platform, shutdown_frees_the_state_through_its_allocator) {
    rb_failing_allocator counting;
    rb_failing_allocator_init(&counting, rb_heap_allocator(), 0);
    rb_platform_state *platform = rb_platform_startup(rb_failing_allocator_get(&counting));
    RB_REQUIRE(platform != NULL);
    RB_EXPECT_EQ_UINT(counting.allocation_count, 1);
    RB_EXPECT(counting.live_bytes > 0);
    rb_platform_shutdown(platform);
    RB_EXPECT_EQ_UINT(counting.free_count, 1);
    RB_EXPECT_EQ_UINT(counting.live_bytes, 0);
}

/* Shutdown unregisters the window class: a second startup must be able to register it again. */
RB_TEST(win32_platform, starts_again_after_shutdown) {
    for (int i = 0; i < 2; ++i) {
        rb_platform_state *platform = rb_platform_startup(rb_heap_allocator());
        RB_REQUIRE(platform != NULL);
        rb_platform_shutdown(platform);
    }
}
