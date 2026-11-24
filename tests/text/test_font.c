/*
 * Fonts through the public API (ADR 0018): the builtin font's atlas, glyphs
 * and metrics, and its memory. Needs a renderer, so a window: built on Windows
 * only, like test_renderer.c.
 */
#include "rb_test.h"

#include <reborn/font.h>
#include <reborn/platform.h>
#include <reborn/renderer.h>

#include "containers/handle_pool.h"
#include "failing_allocator.h"
#include "renderer/renderer_internal.h"

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

static rb_platform_state *platform;
static rb_window          window;

static rb_renderer *start(void) {
    platform = rb_platform_startup(rb_heap_allocator());
    if (platform == NULL) {
        return NULL;
    }
    window = rb_window_create(platform,
                              &(rb_window_desc){.title = "Font test", .width = 320, .height = 240});
    rb_renderer *renderer = NULL;
    if (rb_window_is_valid(platform, window)) {
        renderer = rb_renderer_startup(rb_heap_allocator(),
                                       &(rb_renderer_config){.validation = true, .vsync = true},
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
    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT_EQ_UINT(stats.errors, 0);
    RB_EXPECT_EQ_UINT(stats.warnings, 0);
    rb_renderer_shutdown(renderer);
    rb_window_destroy(platform, window);
    rb_platform_shutdown(platform);
}

/* Inside the atlas, whole pixels, the size of a cell. */
static bool glyph_in_atlas(rb_glyph glyph) {
    rb_rect r = glyph.source;
    return r.x >= 0.0f && r.y >= 0.0f && r.x + r.width <= 128.0f && r.y + r.height <= 62.0f &&
           r.width == 8.0f && r.height == 8.0f;
}

RB_TEST(font, builtin_font_has_an_atlas_and_its_metrics) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    rb_font *font = rb_font_create_builtin(rb_heap_allocator(), renderer);
    RB_EXPECT(font != NULL);
    if (font != NULL) {
        RB_EXPECT(rb_renderer_texture_is_valid(renderer, rb_font_get_atlas(font)));
        rb_font_metrics metrics = rb_font_get_metrics(font);
        EXPECT_F32_NEAR(metrics.ascent, 7.0, 0.0);
        EXPECT_F32_NEAR(metrics.descent, 1.0, 0.0);
        EXPECT_F32_NEAR(metrics.line_gap, 2.0, 0.0);
        rb_font_destroy(font);
    }
    stop(renderer);
}

/* Each visible glyph has its own cell in the atlas; neighbors never overlap. */
RB_TEST(font, builtin_glyphs_have_their_own_cells) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    rb_font *font = rb_font_create_builtin(rb_heap_allocator(), renderer);
    RB_REQUIRE(font != NULL);
    rb_glyph a = rb_font_get_glyph(font, 'A');
    rb_glyph b = rb_font_get_glyph(font, 'B');
    RB_EXPECT(glyph_in_atlas(a));
    RB_EXPECT(glyph_in_atlas(b));
    RB_EXPECT(a.source.x != b.source.x || a.source.y != b.source.y);
    EXPECT_F32_NEAR(a.advance, 8.0, 0.0);
    EXPECT_F32_NEAR(a.offset.y, -7.0, 0.0);
    for (u32 c = '!'; c <= '~'; ++c) {
        RB_EXPECT(glyph_in_atlas(rb_font_get_glyph(font, c)));
    }
    rb_font_destroy(font);
    stop(renderer);
}

/* A space advances the pen without an image; an unknown character gets the box. */
RB_TEST(font, space_is_empty_and_unknown_characters_get_the_missing_glyph) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    rb_font *font = rb_font_create_builtin(rb_heap_allocator(), renderer);
    RB_REQUIRE(font != NULL);
    rb_glyph space = rb_font_get_glyph(font, ' ');
    EXPECT_F32_NEAR(space.source.width, 0.0, 0.0);
    EXPECT_F32_NEAR(space.advance, 8.0, 0.0);
    rb_glyph missing = rb_font_get_glyph(font, 0x20AC); /* € */
    RB_EXPECT(glyph_in_atlas(missing));
    rb_glyph newline = rb_font_get_glyph(font, '\n');
    RB_EXPECT(missing.source.x == newline.source.x && missing.source.y == newline.source.y);
    rb_font_destroy(font);
    stop(renderer);
}

/* The font and the atlas's pixels go back to the allocator, under the text tag. */
RB_TEST(font, memory_is_freed_through_its_allocator) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    rb_memory_stats      before = rb_memory_get_stats(RB_MEMORY_TAG_TEXT);
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 0);
    rb_font *font = rb_font_create_builtin(rb_failing_allocator_get(&failing), renderer);
    RB_EXPECT(font != NULL);
    RB_EXPECT_EQ_UINT(failing.allocation_count, 2); /* the font, the atlas's pixels */
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_TEXT).live_blocks,
                      before.live_blocks + 1); /* the pixels are gone after the upload */
    rb_font_destroy(font);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
    RB_EXPECT_EQ_UINT(rb_memory_get_stats(RB_MEMORY_TAG_TEXT).live_blocks, before.live_blocks);
    stop(renderer);
}

/* Each allocation fails once: nothing leaks, no texture is left behind. */
RB_TEST(font, every_failed_allocation_leaves_nothing) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    for (u32 n = 1; n <= 2; ++n) {
        rb_failing_allocator failing;
        rb_failing_allocator_init(&failing, rb_heap_allocator(), n);
        RB_EXPECT(rb_font_create_builtin(rb_failing_allocator_get(&failing), renderer) == NULL);
        RB_EXPECT_EQ_UINT(failing.failure_count, 1);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0);
    }
    /* A texture created and leaked would still hold its slot: a new one would take the next. */
    rb_font *font = rb_font_create_builtin(rb_heap_allocator(), renderer);
    RB_REQUIRE(font != NULL);
    RB_EXPECT_EQ_UINT(rb_handle_index(rb_font_get_atlas(font).id), 0); /* the first slot */
    rb_font_destroy(font);
    stop(renderer);
}

/* Glyphs drawn from the atlas, the way text drawing will, across frames. */
RB_TEST(font, glyphs_are_drawn_without_validation_messages) {
    rb_renderer *renderer = start();
    RB_REQUIRE(renderer != NULL);
    rb_font *font = rb_font_create_builtin(rb_heap_allocator(), renderer);
    RB_REQUIRE(font != NULL);
    rb_color white = rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f);
    for (u32 frame = 0; frame < 3; ++frame) {
        if (!rb_renderer_begin_frame(renderer)) {
            RB_EXPECT(false);
            break;
        }
        f32 pen = 10.0f;
        for (const char *c = "Score 120"; *c != '\0'; ++c) {
            rb_glyph glyph = rb_font_get_glyph(font, (u8)*c);
            rb_renderer_draw_texture_region(renderer, pen + glyph.offset.x * 2.0f,
                                            40.0f + glyph.offset.y * 2.0f,
                                            glyph.source.width * 2.0f, glyph.source.height * 2.0f,
                                            rb_font_get_atlas(font), glyph.source, white);
            pen += glyph.advance * 2.0f;
        }
        rb_renderer_end_frame(renderer);
    }
    rb_font_destroy(font);
    stop(renderer);
}
