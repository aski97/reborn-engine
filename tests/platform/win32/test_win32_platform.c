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

/* ============================================================================
 * Window icon (ADR 0020)
 * ========================================================================== */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>

/* 16 and 32 pixels, opaque orange: the small and the big icon at 100% scale. */
static u8 icon_16[16 * 16 * 4];
static u8 icon_32[32 * 32 * 4];

static HWND find_window(const wchar_t *title) {
    return FindWindowW(NULL, title);
}

static void fill_orange(u8 *pixels, usize size) {
    for (usize i = 0; i < size; i += 4) {
        pixels[i] = 250;
        pixels[i + 1] = 150;
        pixels[i + 2] = 30;
        pixels[i + 3] = 255;
    }
}

RB_TEST(win32_platform, icon_is_set_before_the_window_shows) {
    fill_orange(icon_16, sizeof(icon_16));
    fill_orange(icon_32, sizeof(icon_32));
    rb_image           images[] = {{32, 32, icon_32}, {16, 16, icon_16}};
    rb_platform_state *platform = rb_platform_startup(rb_heap_allocator());
    RB_REQUIRE(platform != NULL);
    rb_window window =
        rb_window_create(platform, &(rb_window_desc){.title = "Reborn icon test",
                                                     .width = 320,
                                                     .height = 240,
                                                     .icons = images,
                                                     .icon_count = RB_ARRAY_COUNT(images)});
    rb_window plain = rb_window_create(
        platform, &(rb_window_desc){.title = "Reborn no icon test", .width = 320, .height = 240});
    RB_EXPECT(rb_window_is_valid(platform, window));

    HWND hwnd = find_window(L"Reborn icon test");
    RB_EXPECT(hwnd != NULL);
    RB_EXPECT(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0) != 0);
    RB_EXPECT(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) != 0);
    /* WM_GETICON answers only for icons set with WM_SETICON: none for the plain window. */
    HWND plain_hwnd = find_window(L"Reborn no icon test");
    RB_EXPECT(plain_hwnd != NULL);
    RB_EXPECT(SendMessageW(plain_hwnd, WM_GETICON, ICON_SMALL, 0) == 0);
    RB_EXPECT(SendMessageW(plain_hwnd, WM_GETICON, ICON_BIG, 0) == 0);

    rb_window_destroy(platform, window);
    rb_window_destroy(platform, plain);
    rb_platform_shutdown(platform);
}
