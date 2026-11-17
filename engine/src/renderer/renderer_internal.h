/*
 * Renderer functions for the application layer (ADR 0013, Q1 and V1), not for
 * games: the app drives the frame, the game only renders inside it.
 *
 *   if (rb_renderer_begin_frame(renderer)) {
 *       ... the game's render ...
 *       rb_renderer_end_frame(renderer);
 *   }
 */
#ifndef RB_RENDERER_INTERNAL_H
#define RB_RENDERER_INTERNAL_H

#include <reborn/renderer.h>

/*
 * Starts a frame: waits until the GPU is done with the frame that last used
 * the same resources, takes a swapchain image and starts drawing into it,
 * cleared to the clear color. With vsync, the wait for a free image is what
 * paces the loop.
 *
 * False when there is nothing to show this frame: the window is minimized or
 * has no pixels, the swapchain is out of date or could not be rebuilt (logged
 * once), or the renderer stopped after an unrecoverable error (logged once).
 * Rebuilds the swapchain first when the window's pixel size changed or Vulkan
 * reported it out of date.
 */
bool rb_renderer_begin_frame(rb_renderer *renderer);

/*
 * Ends the frame begun by a successful rb_renderer_begin_frame (required, and
 * only then): submits it to the GPU and queues the image to be shown.
 */
void rb_renderer_end_frame(rb_renderer *renderer);

#endif /* RB_RENDERER_INTERNAL_H */
