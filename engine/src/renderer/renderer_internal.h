/*
 * Renderer functions for the application layer (ADR 0013, Q1), not for games:
 * the app drives the frame, the game only renders inside it.
 */
#ifndef RB_RENDERER_INTERNAL_H
#define RB_RENDERER_INTERNAL_H

#include <reborn/renderer.h>

/*
 * Start of a frame: whether it can be shown. False while the window is
 * minimized or has no pixels, or after the swapchain could not be rebuilt
 * (logged once). Rebuilds the swapchain when the window's pixel size changed
 * since it was built.
 */
bool rb_renderer_prepare_frame(rb_renderer *renderer);

#endif /* RB_RENDERER_INTERNAL_H */
