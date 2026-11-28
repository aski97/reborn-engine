/*
 * Blink's view layer without a window: the effects, the HUD's scale (ADR 0021)
 * and the window icon (ADR 0020).
 */
#include "rb_test.h"

#include "blink_view.h"

#define EXPECT_F32_EQ(a, b) RB_EXPECT_NEAR((f64)(a), (f64)(b), 0.0)

/* ============================================================================
 * Effects
 * ========================================================================== */

static blink_events one_event(blink_event event) {
    blink_events events = {.count = 1};
    events.items[0] = event;
    return events;
}

static const blink_events no_events = {0};

static u32 effects_of(const blink_view *view, blink_effect_type type) {
    u32 count = 0;
    for (u32 i = 0; i < BLINK_MAX_EFFECTS; ++i) {
        count += view->effects[i].type == type;
    }
    return count;
}

/* A state for the log lines: the view only reads it. */
static blink_state any_state(void) {
    blink_state state;
    blink_start(&state, &blink_default_tuning, 1);
    return state;
}

RB_TEST(blink_view, each_event_starts_its_effect_where_it_happened) {
    blink_view   view = {0};
    blink_state  state = any_state();
    rb_rect      square = {2.0f, 3.0f, 1.0f, 1.0f};
    blink_events hit = one_event((blink_event){
        .type = BLINK_EVENT_HIT, .point = {2.5f, 3.5f}, .square = square, .color = 4});
    blink_view_update(&view, &state, &hit, 0.0f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_POP), 1);
    RB_EXPECT(view.effects[0].area.x == square.x && view.effects[0].area.width == square.width);
    RB_EXPECT_EQ_UINT(view.effects[0].color, 4);

    blink_events outside =
        one_event((blink_event){.type = BLINK_EVENT_CLICK_OUTSIDE, .point = {8.0f, 4.0f}});
    blink_view_update(&view, &state, &outside, 0.0f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_MARK), 1);
    /* Centered on the click. */
    RB_EXPECT_NEAR((f64)(view.effects[1].area.x + view.effects[1].area.width * 0.5f), 8.0, 1e-6);

    blink_events lost = one_event((blink_event){.type = BLINK_EVENT_EXPIRED, .square = square});
    blink_view_update(&view, &state, &lost, 0.0f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_LOST), 1);

    /* The others start none. */
    blink_events started = one_event((blink_event){.type = BLINK_EVENT_STARTED});
    blink_view_update(&view, &state, &started, 0.0f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_NONE), BLINK_MAX_EFFECTS - 3);
}

/* Timed by the steps, not the wall clock: a paused game holds them, a replay repeats them. */
RB_TEST(blink_view, effects_age_with_the_steps_and_end) {
    blink_view   view = {0};
    blink_state  state = any_state();
    blink_events hit = one_event((blink_event){.type = BLINK_EVENT_HIT});
    blink_view_update(&view, &state, &hit, 0.0f);
    blink_view_update(&view, &state, &no_events, 0.1f);
    EXPECT_F32_EQ(view.effects[0].age, 0.1f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_POP), 1);
    blink_view_update(&view, &state, &no_events, 10.0f);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_NONE), BLINK_MAX_EFFECTS);
}

/* Full: the effect nearest its end gives way, so the newest always shows. */
RB_TEST(blink_view, a_full_view_replaces_the_effect_nearest_its_end) {
    blink_view   view = {0};
    blink_state  state = any_state();
    blink_events hit = one_event((blink_event){.type = BLINK_EVENT_HIT});
    blink_view_update(&view, &state, &hit, 0.0f);
    blink_view_update(&view, &state, &no_events, 0.1f); /* the first one is now the oldest */
    for (u32 i = 1; i < BLINK_MAX_EFFECTS; ++i) {
        blink_view_update(&view, &state, &hit, 0.0f);
    }
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_POP), BLINK_MAX_EFFECTS);
    blink_events outside = one_event((blink_event){.type = BLINK_EVENT_CLICK_OUTSIDE});
    blink_view_update(&view, &state, &outside, 0.0f);
    RB_EXPECT(view.effects[0].type == BLINK_EFFECT_MARK);
    RB_EXPECT_EQ_UINT(effects_of(&view, BLINK_EFFECT_POP), BLINK_MAX_EFFECTS - 1);
}

/* ============================================================================
 * HUD and icon
 * ========================================================================== */

/* Whole numbers only: the builtin font's pixels must cover whole window pixels. */
RB_TEST(blink_view, hud_scale_is_one_per_180_pixels) {
    EXPECT_F32_EQ(blink_view_hud_scale(720.0f), 4.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(1080.0f), 6.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(1440.0f), 8.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(2160.0f), 12.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(719.0f), 3.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(562.0f), 3.0f);
}

/* A tiny or minimized window still gets readable text, not a scale of 0. */
RB_TEST(blink_view, hud_scale_is_at_least_one) {
    EXPECT_F32_EQ(blink_view_hud_scale(359.0f), 1.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(100.0f), 1.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(0.0f), 1.0f);
}

static const u8 *pixel(const u8 *pixels, u32 side, u32 x, u32 y) {
    return &pixels[(y * side + x) * 4];
}

RB_TEST(blink_view, icon_is_a_rounded_tile_with_the_square_in_the_middle) {
    static u8 pixels[32 * 32 * 4];
    blink_view_draw_icon(32, pixels);
    /* Outside the rounded corner: transparent. */
    RB_EXPECT_EQ_UINT(pixel(pixels, 32, 0, 0)[3], 0);
    /* The middle: the orange square, opaque. */
    const u8 *middle = pixel(pixels, 32, 16, 16);
    RB_EXPECT_EQ_UINT(middle[0], 250);
    RB_EXPECT_EQ_UINT(middle[1], 179);
    RB_EXPECT_EQ_UINT(middle[2], 51);
    RB_EXPECT_EQ_UINT(middle[3], 255);
    /* The middle of the left edge: the light border, opaque. */
    const u8 *edge = pixel(pixels, 32, 0, 16);
    RB_EXPECT_EQ_UINT(edge[0], 96);
    RB_EXPECT_EQ_UINT(edge[3], 255);
    /* Between border and square: the dark tile. */
    RB_EXPECT_EQ_UINT(pixel(pixels, 32, 4, 16)[0], 24);
}

/* Same picture at every size: the small one is not a crop of the large one. */
RB_TEST(blink_view, icon_scales_with_its_side) {
    static u8 small[16 * 16 * 4];
    static u8 large[64 * 64 * 4];
    blink_view_draw_icon(16, small);
    blink_view_draw_icon(64, large);
    RB_EXPECT_EQ_UINT(pixel(small, 16, 8, 8)[0], 250);
    RB_EXPECT_EQ_UINT(pixel(large, 64, 32, 32)[0], 250);
    RB_EXPECT_EQ_UINT(pixel(small, 16, 0, 0)[3], 0);
    RB_EXPECT_EQ_UINT(pixel(large, 64, 0, 0)[3], 0);
}
