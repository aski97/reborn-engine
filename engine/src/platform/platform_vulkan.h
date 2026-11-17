/*
 * Vulkan surfaces (ADR 0013, J1): the one place where the platform meets
 * Vulkan. A surface wraps an OS window, and creating it takes the backend's
 * native handles (HWND, Display * and Window), which never leave the platform.
 * Implemented by each backend next to its windows (win32_platform.c,
 * xlib_platform.c).
 */
#ifndef RB_PLATFORM_VULKAN_H
#define RB_PLATFORM_VULKAN_H

#include <reborn/platform.h>

#include "renderer/vulkan/vulkan_api.h"

/*
 * The instance extension that creates surfaces on this OS
 * ("VK_KHR_win32_surface", "VK_KHR_xlib_surface"). The instance must enable it,
 * with VK_KHR_surface, before rb_platform_vulkan_create_surface can work.
 */
const char *rb_platform_vulkan_surface_extension(void);

/*
 * Creates a surface for `window`. The OS function comes from
 * `get_instance_proc_addr` for `instance`: the platform never links Vulkan
 * either. The caller destroys the surface (vkDestroySurfaceKHR) before the
 * window. False (logged) for a stale window or a Vulkan error; *surface is
 * then VK_NULL_HANDLE.
 */
bool rb_platform_vulkan_create_surface(const rb_platform_state *state, rb_window window,
                                       VkInstance                instance,
                                       PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                       VkSurfaceKHR             *surface);

#endif /* RB_PLATFORM_VULKAN_H */
