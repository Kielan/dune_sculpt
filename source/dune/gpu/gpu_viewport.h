#pragma once

#include <stdbool.h>

#include "types_scene.h"
#include "types_vec.h"

#include "gpu_framebuffer.h"
#include "gpu_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLA_PIXEL_OFS 0.375f

typedef struct GHash GHash;
typedef struct GPUViewport GPUViewport;

struct DRWData;
struct DefaultFramebufferList;
struct DefaultTextureList;
struct GPUFrameBuffer;

GPUViewport *GPU_viewport_create(void);
GPUViewport *GPU_viewport_stereo_create(void);
void GPU_viewport_bind(GPUViewport *viewport, int view, const rcti *rect);
void GPU_viewport_unbind(GPUViewport *viewport);
/**
 * Merge and draw the buffers of \a viewport into the currently active framebuffer, performing
 * color transform to display space.
 *
 * \param rect: Coordinates to draw into. By swapping min and max values, drawing can be done
 * with inversed axis coordinates (upside down or sideways).
 */
void GPU_viewport_draw_to_screen(GPUViewport *viewport, int view, const rcti *rect);
/**
 * Version of #GPU_viewport_draw_to_screen() that lets caller decide if display colorspace
 * transform should be performed.
 */
void GPU_viewport_draw_to_screen_ex(GPUViewport *viewport,
                                    int view,
                                    const rcti *rect,
                                    bool display_colorspace,
                                    bool do_overlay_merge);
/**
 * Must be executed inside Draw-manager OpenGL Context.
 */
void GPU_viewport_free(GPUViewport *viewport);

void GPU_viewport_colorspace_set(GPUViewport *viewport,
                                 ColorManagedViewSettings *view_settings,
                                 const ColorManagedDisplaySettings *display_settings,
                                 float dither);

/**
 * Should be called from DRW after DRW_opengl_context_enable.
 */
void GPU_viewport_bind_from_offscreen(GPUViewport *viewport,
                                      struct GPUOffScreen *ofs,
                                      bool is_xr_surface);
/**
 * Clear vars assigned from offscreen, so we don't free data owned by `GPUOffScreen`.
 */
void GPU_viewport_unbind_from_offscreen(GPUViewport *viewport,
                                        struct GPUOffScreen *ofs,
                                        bool display_colorspace,
                                        bool do_overlay_merge);

struct DRWData **GPU_viewport_data_get(GPUViewport *viewport);

/**
 * Merge the stereo textures. `color` and `overlay` texture will be modified.
 */
void gpu_viewport_stereo_composite(GPUViewport *viewport, Stereo3dFormat *stereo_format);

void gpu_viewport_tag_update(GpuViewport *viewport);
bool gpu_viewport_do_update(GpuViewport *viewport);

int gpu_viewport_active_view_get(GpuViewport *viewport);
bool gpu_viewport_is_stereo_get(GpuViewport *viewport);

GpuTexture *gpu_viewport_color_texture(GpuViewport *viewport, int view);
GpuTexture *gpu_viewport_overlay_texture(GpuViewport *viewport, int view);
GpuTexture *gpu_viewport_depth_texture(GpuViewport *viewport);

/** Overlay frame-buffer for drawing outside of DRW module. */
GpuFrameBuffer *gpu_viewport_framebuffer_overlay_get(GpuViewport *viewport);

#ifdef __cplusplus
}
#endif
