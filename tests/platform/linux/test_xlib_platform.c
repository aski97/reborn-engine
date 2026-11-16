/* Xlib platform startup (ADR 0010, K1). Built on Linux only. */

/* unsetenv is POSIX, hidden by -std=c17 unless asked. Before any header. */
#define _POSIX_C_SOURCE 200809L

#include "rb_test.h"

#include <stdlib.h>

#include <reborn/platform.h>

#include "failing_allocator.h"

/*
 * Without $DISPLAY, XOpenDisplay fails after the state was allocated: the
 * failure path must give it back. Works with or without an X server around.
 */
RB_TEST(xlib_platform, startup_without_display_frees_the_state) {
    unsetenv("DISPLAY");
    rb_failing_allocator counting;
    rb_failing_allocator_init(&counting, rb_heap_allocator(), 0);
    RB_EXPECT(rb_platform_startup(rb_failing_allocator_get(&counting)) == NULL);
    RB_EXPECT_EQ_UINT(counting.allocation_count, 1);
    RB_EXPECT_EQ_UINT(counting.free_count, 1);
    RB_EXPECT_EQ_UINT(counting.live_bytes, 0);
}
