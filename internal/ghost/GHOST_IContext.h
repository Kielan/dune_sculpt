#pragma once

#include "GHOST_Types.h"

/**
 * Interface for GHOST context.
 *
 * You can create a off-screen context (windowless) with the system's
 * createOffscreenContext method.
 * see createOffscreenContext
 */
struct GHOST_IContext {
 
};

/**
  * Activates the drawing context.
  * return A boolean success indicator.
  */
GHOST_TSuccess activateDrawingContext() = 0;

/**
 * Release the drawing context of the calling thread.
 * return A boolean success indicator.
 */
GHOST_TSuccess releaseDrawingContext() = 0;

unsigned int getDefaultFramebuffer() = 0;

GHOST_TSuccess getVulkanHandles(void *, void *, void *, uint32_t *) =                                              void *framebuffer,
                                             void *command_buffer,
                                             void *render_pass,
                                             void *extent,
                                             uint32_t *fb_id) = 0;

/**
 * Gets the Vulkan framebuffer related resource handles associated with the Vulkan context.
 * Needs to be called after each swap events as the framebuffer will change.
 * return  A boolean success indicator.
*/
GHOST_TSuccess getVulkanBackbuffer(void *image,

GHOST_TSuccess swapBuffers() = 0;
