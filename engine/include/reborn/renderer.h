/*
 * Renderer (ADR 0013): draws through Vulkan 1.3. A game runs under the
 * application layer (reborn/app.h), which starts the renderer after the main
 * window and stops it before; the game reaches it through rb_app_renderer and
 * configures it in rb_app_config.renderer. Driving it directly, as below, is
 * for tools and tests.
 *
 *   rb_renderer *renderer = rb_renderer_startup(rb_arena_allocator(&arena),
 *                                               &(rb_renderer_config){.validation = true,
 *                                                                     .vsync = true},
 *                                               platform, window);
 *   ...
 *   rb_renderer_shutdown(renderer);   // logs the validation totals, frees the state
 *
 * Each frame starts cleared to the clear color; the game's `render` draws
 * colored quads on top, in window pixels or, through a 2D camera, in world
 * units (rb_renderer_set_camera). Pointer parameters must not be NULL, unless
 * stated otherwise (RB_ASSERT in debug builds).
 */
#ifndef RB_RENDERER_H
#define RB_RENDERER_H

#include <reborn/camera2d.h>
#include <reborn/color.h>
#include <reborn/defines.h>
#include <reborn/memory.h>
#include <reborn/platform.h>

/* Opaque: Vulkan types never reach game code. */
typedef struct rb_renderer rb_renderer;

typedef struct rb_renderer_config {
    /*
     * Vulkan validation layers: every call is checked and problems are logged
     * and counted (rb_renderer_get_validation_stats). Slow: for development.
     * Needs the layers installed (Vulkan SDK, `vulkan-validationlayers`);
     * without them a warning is logged and the renderer runs unchecked.
     */
    bool validation;
    /*
     * Wait for the monitor's refresh before showing a frame: no tearing, and
     * the frame rate never exceeds the refresh rate. Off: frames are shown as
     * soon as they are ready (without tearing where the driver can), for
     * measuring performance.
     */
    bool vsync;
} rb_renderer_config;

/*
 * Draws into `window`. Takes its state from `allocator` (tag
 * RB_MEMORY_TAG_RENDERER) and keeps a copy of it and of `platform`: both, and
 * the window, must outlive the renderer. NULL on failure (logged): no Vulkan,
 * a version below 1.3, no GPU that can present to the window, a stale window,
 * no memory. Nothing is left allocated or open then.
 */
RB_API rb_renderer *rb_renderer_startup(rb_allocator allocator, const rb_renderer_config *config,
                                        const rb_platform_state *platform, rb_window window);

/* Logs the validation totals, destroys every Vulkan object, frees `renderer`. */
RB_API void rb_renderer_shutdown(rb_renderer *renderer);

/*
 * What every frame starts from, before anything is drawn; dark gray until set.
 * Used from the next frame begun: the current one, if any, has already been
 * cleared (under the application layer, rendering begins before `render`).
 */
RB_API void rb_renderer_set_clear_color(rb_renderer *renderer, rb_color color);

/*
 * The space of the draws that follow (ADR 0016). A camera: world units, drawn
 * only inside its field (the rest of the window, the bars, keeps what is
 * there). NULL: pixels of the window's client area. The camera is copied: later
 * changes need another call. Each frame begins in pixels. Only inside the
 * game's `render`.
 *
 *   rb_renderer_set_camera(renderer, &camera);   // the world
 *   ...
 *   rb_renderer_set_camera(renderer, NULL);      // the HUD, in pixels
 */
RB_API void rb_renderer_set_camera(rb_renderer *renderer, const rb_camera2d *camera);

/*
 * Draws a filled rectangle into the current frame, on top of what was drawn
 * before it (ADR 0015). In the current space (rb_renderer_set_camera): window
 * pixels by default, origin at the top left and y pointing down, like
 * rb_mouse_position; fractions are allowed. Alpha blends with what is
 * underneath: 1 covers, 0 leaves it untouched. Only inside the game's `render`
 * (a frame begun by the application layer).
 *
 *   rb_renderer_draw_quad(renderer, 100, 50, 200, 100, rb_color_srgb(1, 0, 0, 1));
 */
RB_API void rb_renderer_draw_quad(rb_renderer *renderer, f32 x, f32 y, f32 width, f32 height,
                                  rb_color color);

/* Counted since startup. Only messages from the validation layers count, not driver notes. */
typedef struct rb_renderer_validation_stats {
    bool enabled;  /* requested and available */
    u32  errors;   /* API misuse: a bug in the engine or the game */
    u32  warnings; /* legal but suspicious use */
} rb_renderer_validation_stats;

RB_API rb_renderer_validation_stats rb_renderer_get_validation_stats(const rb_renderer *renderer);

#endif /* RB_RENDERER_H */
