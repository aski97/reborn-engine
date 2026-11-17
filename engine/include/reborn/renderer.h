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
 * Work in progress (roadmap §7): every frame shows the window cleared to the
 * clear color, nothing is drawn yet. Pointer parameters must not be NULL
 * (RB_ASSERT in debug builds).
 */
#ifndef RB_RENDERER_H
#define RB_RENDERER_H

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

/* Counted since startup. Only messages from the validation layers count, not driver notes. */
typedef struct rb_renderer_validation_stats {
    bool enabled;  /* requested and available */
    u32  errors;   /* API misuse: a bug in the engine or the game */
    u32  warnings; /* legal but suspicious use */
} rb_renderer_validation_stats;

RB_API rb_renderer_validation_stats rb_renderer_get_validation_stats(const rb_renderer *renderer);

#endif /* RB_RENDERER_H */
