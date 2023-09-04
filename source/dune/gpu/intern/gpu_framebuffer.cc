#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_base.h"
#include "lib_utildefines.h"

#include "gpu_batch.h"
#include "gpu_capabilities.h"
#include "gpu_shader.h"
#include "gpu_texture.h"

#include "gpu_backend.hh"
#include "gpu_cxt_private.hh"
#include "gpu_private.h"
#include "gpu_texture_private.hh"

#include "gpu_framebuffer_private.hh"

namespace dune::gpu {

/* Constructor / Destructor  */
FrameBuffer::FrameBuffer(const char *name)
{
  if (name) {
    lib_strncpy(name_, name, sizeof(name_));
  }
  else {
    name_[0] = '\0';
  }
  /* Force config on first use. */
  dirty_attachments_ = true;
  dirty_state_ = true;

  for (GPUAttachment &attachment : attachments_) {
    attachment.tex = nullptr;
    attachment.mip = -1;
    attachment.layer = -1;
  }
}

FrameBuffer::~FrameBuffer()
{
  for (GPUAttachment &attachment : attachments_) {
    if (attachment.tex != nullptr) {
      reinterpret_cast<Texture *>(attachment.tex)->detach_from(this);
    }
  }

#ifndef GPU_NO_USE_PY_REFERENCES
  if (this->py_ref) {
    *this->py_ref = nullptr;
  }
#endif
}

/* Attachments Management **/
void FrameBuffer::attachment_set(GPUAttachmentType type, const GPUAttachment &new_attachment)
{
  if (new_attachment.mip == -1) {
    return; /* GPU_ATTACHMENT_LEAVE */
  }

  if (type >= GPU_FB_MAX_ATTACHMENT) {
    fprintf(stderr,
            "GPUFramebuffer: Error: Trying to attach texture to type %d but maximum slot is %d.\n",
            type - GPU_FB_COLOR_ATTACHMENT0,
            GPU_FB_MAX_COLOR_ATTACHMENT);
    return;
  }

  if (new_attachment.tex) {
    if (new_attachment.layer > 0) {
      lib_assert(gpu_texture_cube(new_attachment.tex) || GPU_texture_array(new_attachment.tex));
    }
    if (gpu_texture_stencil(new_attachment.tex)) {
      lib_assert(ELEM(type, GPU_FB_DEPTH_STENCIL_ATTACHMENT));
    }
    else if (gpu_texture_depth(new_attachment.tex)) {
      lib_assert(ELEM(type, GPU_FB_DEPTH_ATTACHMENT));
    }
  }

  GPUAttachment &attachment = attachments_[type];

  if (attachment.tex == new_attachment.tex && attachment.layer == new_attachment.layer &&
      attachment.mip == new_attachment.mip) {
    return; /* Exact same texture already bound here. */
  }
  /* Unbind previous and bind new. */
  /* TODO: cleanup the casts. */
  if (attachment.tex) {
    reinterpret_cast<Texture *>(attachment.tex)->detach_from(this);
  }

  attachment = new_attachment;

  /* Might be null if this is for unbinding. */
  if (attachment.tex) {
    reinterpret_cast<Texture *>(attachment.tex)->attach_to(this, type);
  }
  else {
    /* GPU_ATTACHMENT_NONE */
  }

  dirty_attachments_ = true;
}

void FrameBuffer::attachment_remove(GPUAttachmentType type)
{
  attachments_[type] = GPU_ATTACHMENT_NONE;
  dirty_attachments_ = true;
}

void FrameBuffer::recursive_downsample(int max_lvl,
                                       void (*cb)(void *userData, int level),
                                       void *userData)
{
  /* Bind to make sure the frame-buffer is up to date. */
  this->bind(true);

  /* FIXME: This assumes all mips are defined which may not be the case. */
  max_lvl = min_ii(max_lvl, floor(log2(max_ii(width_, height_))));

  for (int mip_lvl = 1; mip_lvl <= max_lvl; mip_lvl++) {
    /* Replace attached mip-level for each attachment. */
    for (GPUAttachment &attachment : attachments_) {
      Texture *tex = reinterpret_cast<Texture *>(attachment.tex);
      if (tex != nullptr) {
        /* Some Intel HDXXX have issue with rendering to a mipmap that is below
         * the texture GL_TEXTURE_MAX_LEVEL. So even if it not correct, in this case
         * we allow GL_TEXTURE_MAX_LEVEL to be one level lower. In practice it does work! */
        int mip_max = (gpu_mip_render_workaround()) ? mip_lvl : (mip_lvl - 1);
        /* Restrict fetches only to previous level. */
        tex->mip_range_set(mip_lvl - 1, mip_max);
        /* Bind next level. */
        attachment.mip = mip_lvl;
      }
    }
    /* Update the internal attachments and viewport size. */
    dirty_attachments_ = true;
    this->bind(true);

    cb(userData, mip_lvl);
  }

  for (GPUAttachment &attachment : attachments_) {
    if (attachment.tex != nullptr) {
      /* Reset mipmap level range. */
      reinterpret_cast<Texture *>(attachment.tex)->mip_range_set(0, max_lvl);
      /* Reset base level. NOTE: might not be the one bound at the start of this function. */
      attachment.mip = 0;
    }
  }
  dirty_attachments_ = true;
}

}  // namespace dune::gpu

/* C-API */
using namespace dune;
using namespace dune::gpu;

GPUFrameBuffer *gpu_framebuffer_create(const char *name)
{
  /* We generate the FB object later at first use in order to
   * create the frame-buffer in the right opengl cxt. */
  return wrap(GPUBackend::get()->framebuffer_alloc(name));
}

void gpu_framebuffer_free(GPUFrameBuffer *gpu_fb)
{
  delete unwrap(gpu_fb);
}

/* Binding */
void gpu_framebuffer_bind(GPUFrameBuffer *gpu_fb)
{
  const bool enable_srgb = true;
  unwrap(gpu_fb)->bind(enable_srgb);
}

void gpu_framebuffer_bind_no_srgb(GPUFrameBuffer *gpu_fb)
{
  const bool enable_srgb = false;
  unwrap(gpu_fb)->bind(enable_srgb);
}

void gpu_backbuffer_bind(eGPUBackBuffer buffer)
{
  Cxt *cxt = Cxt::get();

  if (buffer == GPU_BACKBUFFER_LEFT) {
    cxt->back_left->bind(false);
  }
  else {
    cxt->back_right->bind(false);
  }
}

void gpu_framebuffer_restore()
{
  Cxt::get()->back_left->bind(false);
}

GPUFrameBuffer *gpu_framebuffer_active_get()
{
  Cxt *cxt = Cxt::get();
  return wrap(cxt ? cxt->active_fb : nullptr);
}

GPUFrameBuffer *gpu_framebuffer_back_get()
{
  Cxt *ctx = Cxt::get();
  return wrap(cxt ? cxt->back_left : nullptr);
}

bool gpu_framebuffer_bound(GPUFrameBuffer *gpu_fb)
{
  return (gpu_fb == gpu_framebuffer_active_get());
}

/* ---------- Attachment Management ----------- */

bool gpu_framebuffer_check_valid(GPUFrameBuffer *gpu_fb, char err_out[256])
{
  return unwrap(gpu_fb)->check(err_out);
}

void gpu_framebuffer_texture_attach_ex(GPUFrameBuffer *gpu_fb, GPUAttachment attachment, int slot)
{
  Texture *tex = reinterpret_cast<Texture *>(attachment.tex);
  GPUAttachmentType type = tex->attachment_type(slot);
  unwrap(gpu_fb)->attachment_set(type, attachment);
}

void gpu_framebuffer_texture_attach(GPUFrameBuffer *fb, GPUTexture *tex, int slot, int mip)
{
  GPUAttachment attachment = GPU_ATTACHMENT_TEXTURE_MIP(tex, mip);
  gpu_framebuffer_texture_attach_ex(fb, attachment, slot);
}

void gpu_framebuffer_texture_layer_attach(
    GPUFrameBuffer *fb, GPUTexture *tex, int slot, int layer, int mip)
{
  GPUAttachment attachment = GPU_ATTACHMENT_TEXTURE_LAYER_MIP(tex, layer, mip);
  gpu_framebuffer_texture_attach_ex(fb, attachment, slot);
}

void gpu_framebuffer_texture_cubeface_attach(
    GPUFrameBuffer *fb, GPUTexture *tex, int slot, int face, int mip)
{
  GPUAttachment attachment = GPU_ATTACHMENT_TEXTURE_CUBEFACE_MIP(tex, face, mip);
  gpu_framebuffer_texture_attach_ex(fb, attachment, slot);
}

void gpu_framebuffer_texture_detach(GPUFrameBuffer *fb, GPUTexture *tex)
{
  unwrap(tex)->detach_from(unwrap(fb));
}

void gpu_framebuffer_config_array(GPUFrameBuffer *gpu_fb,
                                  const GPUAttachment *config,
                                  int config_len)
{
  FrameBuffer *fb = unwrap(gpu_fb);

  const GPUAttachment &depth_attachment = config[0];
  Span<GPUAttachment> color_attachments(config + 1, config_len - 1);

  if (depth_attachment.mip == -1) {
    /* GPU_ATTACHMENT_LEAVE */
  }
  else if (depth_attachment.tex == nullptr) {
    /* GPU_ATTACHMENT_NONE: Need to clear both targets. */
    fb->attachment_set(GPU_FB_DEPTH_STENCIL_ATTACHMENT, depth_attachment);
    fb->attachment_set(GPU_FB_DEPTH_ATTACHMENT, depth_attachment);
  }
  else {
    GPUAttachmentType type = GPU_texture_stencil(depth_attachment.tex) ?
                                 GPU_FB_DEPTH_STENCIL_ATTACHMENT :
                                 GPU_FB_DEPTH_ATTACHMENT;
    fb->attachment_set(type, depth_attachment);
  }

  GPUAttachmentType type = GPU_FB_COLOR_ATTACHMENT0;
  for (const GPUAttachment &attachment : color_attachments) {
    fb->attachment_set(type, attachment);
    ++type;
  }
}

/* ---------- Viewport & Scissor Region ----------- */

void gpu_framebuffer_viewport_set(GPUFrameBuffer *gpu_fb, int x, int y, int width, int height)
{
  int viewport_rect[4] = {x, y, width, height};
  unwrap(gpu_fb)->viewport_set(viewport_rect);
}

void gpu_framebuffer_viewport_get(GPUFrameBuffer *gpu_fb, int r_viewport[4])
{
  unwrap(gpu_fb)->viewport_get(r_viewport);
}

void gpu_framebuffer_viewport_reset(GPUFrameBuffer *gpu_fb)
{
  unwrap(gpu_fb)->viewport_reset();
}

/* ---------- Frame-buffer Operations ----------- */

void gpu_framebuffer_clear(GPUFrameBuffer *gpu_fb,
                           eGPUFrameBufferBits buffers,
                           const float clear_col[4],
                           float clear_depth,
                           uint clear_stencil)
{
  unwrap(gpu_fb)->clear(buffers, clear_col, clear_depth, clear_stencil);
}

void gpu_framebuffer_multi_clear(GPUFrameBuffer *gpu_fb, const float (*clear_cols)[4])
{
  unwrap(gpu_fb)->clear_multi(clear_cols);
}

void gpu_clear_color(float red, float green, float blue, float alpha)
{
  float clear_col[4] = {red, green, blue, alpha};
  Context::get()->active_fb->clear(GPU_COLOR_BIT, clear_col, 0.0f, 0x0);
}

void gpu_clear_depth(float depth)
{
  float clear_col[4] = {0};
  Context::get()->active_fb->clear(GPU_DEPTH_BIT, clear_col, depth, 0x0);
}

void gpu_framebuffer_read_depth(
    GPUFrameBuffer *gpu_fb, int x, int y, int w, int h, eGPUDataFormat format, void *data)
{
  int rect[4] = {x, y, w, h};
  unwrap(gpu_fb)->read(GPU_DEPTH_BIT, format, rect, 1, 1, data);
}

void gpu_framebuffer_read_color(GPUFrameBuffer *gpu_fb,
                                int x,
                                int y,
                                int w,
                                int h,
                                int channels,
                                int slot,
                                eGPUDataFormat format,
                                void *data)
{
  int rect[4] = {x, y, w, h};
  unwrap(gpu_fb)->read(GPU_COLOR_BIT, format, rect, channels, slot, data);
}

/* TODO: rename to read_color. */
void gpu_frontbuffer_read_pixels(
    int x, int y, int w, int h, int channels, eGPUDataFormat format, void *data)
{
  int rect[4] = {x, y, w, h};
  Context::get()->front_left->read(GPU_COLOR_BIT, format, rect, channels, 0, data);
}

/* TODO: port as texture operation. */
void gpu_framebuffer_blit(GPUFrameBuffer *gpufb_read,
                          int read_slot,
                          GPUFrameBuffer *gpufb_write,
                          int write_slot,
                          eGPUFrameBufferBits blit_buffers)
{
  FrameBuffer *fb_read = unwrap(gpufb_read);
  FrameBuffer *fb_write = unwrap(gpufb_write);
  lib_assert(blit_buffers != 0);

  FrameBuffer *prev_fb = Context::get()->active_fb;

#ifndef NDEBUG
  GPUTexture *read_tex, *write_tex;
  if (blit_buffers & (GPU_DEPTH_BIT | GPU_STENCIL_BIT)) {
    read_tex = fb_read->depth_tex();
    write_tex = fb_write->depth_tex();
  }
  else {
    read_tex = fb_read->color_tex(read_slot);
    write_tex = fb_write->color_tex(write_slot);
  }

  if (blit_buffers & GPU_DEPTH_BIT) {
    lib_assert(gpu_texture_depth(read_tex) && GPU_texture_depth(write_tex));
    lib_assert(gpu_texture_format(read_tex) == GPU_texture_format(write_tex));
  }
  if (blit_buffers & GPU_STENCIL_BIT) {
    lib_assert(gpu_texture_stencil(read_tex) && GPU_texture_stencil(write_tex));
    lib_assert(gpu_texture_format(read_tex) == GPU_texture_format(write_tex));
  }
#endif

  fb_read->blit_to(blit_buffers, read_slot, fb_write, write_slot, 0, 0);

  /* FIXME: sRGB is not saved. */
  prev_fb->bind(true);
}

void gpu_framebuffer_recursive_downsample(GPUFrameBuffer *gpu_fb,
                                          int max_lvl,
                                          void (*callback)(void *userData, int level),
                                          void *userData)
{
  unwrap(gpu_fb)->recursive_downsample(max_lvl, callback, userData);
}

#ifndef GPU_NO_USE_PY_REFERENCES
void **gou_framebuffer_py_reference_get(GPUFrameBuffer *gpu_fb)
{
  return unwrap(gpu_fb)->py_ref;
}

void gpu_framebuffer_py_reference_set(GPUFrameBuffer *gpu_fb, void **py_ref)
{
  lib_assert(py_ref == nullptr || unwrap(gpu_fb)->py_ref == nullptr);
  unwrap(gpu_fb)->py_ref = py_ref;
}
#endif

/* -------------------------------------------------------------------- */
/** Frame-Buffer Stack
 *
 * Keeps track of frame-buffer binding operation to restore previously bound frame-buffers.
 **/

#define FRAMEBUFFER_STACK_DEPTH 16

static struct {
  GPUFrameBuffer *framebuffers[FRAMEBUFFER_STACK_DEPTH];
  uint top;
} FrameBufferStack = {{nullptr}};

void gpu_framebuffer_push(GPUFrameBuffer *fb)
{
  lib_assert(FrameBufferStack.top < FRAMEBUFFER_STACK_DEPTH);
  FrameBufferStack.framebuffers[FrameBufferStack.top] = fb;
  FrameBufferStack.top++;
}

GPUFrameBuffer *gpu_framebuffer_pop()
{
  lib_assert(FrameBufferStack.top > 0);
  FrameBufferStack.top--;
  return FrameBufferStack.framebuffers[FrameBufferStack.top];
}

uint gpu_framebuffer_stack_level_get()
{
  return FrameBufferStack.top;
}

#undef FRAMEBUFFER_STACK_DEPTH

/* -------------------------------------------------------------------- */
/** GPUOffScreen
 *
 * Container that holds a frame-buffer and its textures.
 * Might be bound to multiple contexts.
 **/

#define MAX_CTX_FB_LEN 3

struct GPUOffScreen {
  struct {
    Context *ctx;
    GPUFrameBuffer *fb;
  } framebuffers[MAX_CTX_FB_LEN];

  GPUTexture *color;
  GPUTexture *depth;
};

/**
 * Returns the correct frame-buffer for the current context.
 */
static GPUFrameBuffer *gpu_offscreen_fb_get(GPUOffScreen *ofs)
{
  Context *ctx = Context::get();
  lib_assert(ctx);

  for (auto &framebuffer : ofs->framebuffers) {
    if (framebuffer.fb == nullptr) {
      framebuffer.ctx = ctx;
      gpu_framebuffer_ensure_config(&framebuffer.fb,
                                    {
                                        GPU_ATTACHMENT_TEXTURE(ofs->depth),
                                        GPU_ATTACHMENT_TEXTURE(ofs->color),
                                    });
    }

    if (framebuffer.ctx == ctx) {
      return framebuffer.fb;
    }
  }

  /* List is full, this should never happen or
   * it might just slow things down if it happens
   * regularly. In this case we just empty the list
   * and start over. This is most likely never going
   * to happen under normal usage. */
  lib_assert(0);
  printf(
      "Warning: GPUOffscreen used in more than 3 GPUContext. "
      "This may create performance drop.\n");

  for (auto &framebuffer : ofs->framebuffers) {
    gpu_framebuffer_free(framebuffer.fb);
    framebuffer.fb = nullptr;
  }

  return gpu_offscreen_fb_get(ofs);
}

GPUOffScreen *gpu_offscreen_create(
    int width, int height, bool depth, eGPUTextureFormat format, char err_out[256])
{
  GPUOffScreen *ofs = mem_cnew<GPUOffScreen>(__func__);

  /* Sometimes areas can have 0 height or width and this will
   * create a 1D texture which we don't want. */
  height = max_ii(1, height);
  width = max_ii(1, width);

  ofs->color = gpu_texture_create_2d("ofs_color", width, height, 1, format, nullptr);

  if (depth) {
    ofs->depth = gpu_texture_create_2d(
        "ofs_depth", width, height, 1, GPU_DEPTH24_STENCIL8, nullptr);
  }

  if ((depth && !ofs->depth) || !ofs->color) {
    const char error[] = "GPUTexture: Texture allocation failed.";
    if (err_out) {
      lib_snprintf(err_out, 256, error);
    }
    else {
      fprintf(stderr, error);
    }
    gpu_offscreen_free(ofs);
    return nullptr;
  }

  GPUFrameBuffer *fb = gpu_offscreen_fb_get(ofs);

  /* check validity at the very end! */
  if (!gpu_framebuffer_check_valid(fb, err_out)) {
    gpu_offscreen_free(ofs);
    return nullptr;
  }
  gpu_framebuffer_restore();
  return ofs;
}

void gpu_offscreen_free(GPUOffScreen *ofs)
{
  for (auto &framebuffer : ofs->framebuffers) {
    if (framebuffer.fb) {
      gpu_framebuffer_free(framebuffer.fb);
    }
  }
  if (ofs->color) {
    gpu_texture_free(ofs->color);
  }
  if (ofs->depth) {
    gpu_texture_free(ofs->depth);
  }

  mem_freen(ofs);
}

void gpu_offscreen_bind(GPUOffScreen *ofs, bool save)
{
  if (save) {
    GPUFrameBuffer *fb = gpu_framebuffer_active_get();
    gpu_framebuffer_push(fb);
  }
  unwrap(gpu_offscreen_fb_get(ofs))->bind(false);
}

void gpu_offscreen_unbind(GPUOffScreen *UNUSED(ofs), bool restore)
{
  GPUFrameBuffer *fb = nullptr;
  if (restore) {
    fb = GPU_framebuffer_pop();
  }

  if (fb) {
    gpu_framebuffer_bind(fb);
  }
  else {
    gpu_framebuffer_restore();
  }
}

void gpu_offscreen_draw_to_screen(GPUOffScreen *ofs, int x, int y)
{
  Context *ctx = Context::get();
  FrameBuffer *ofs_fb = unwrap(gpu_offscreen_fb_get(ofs));
  ofs_fb->blit_to(GPU_COLOR_BIT, 0, ctx->active_fb, 0, x, y);
}

void gpu_offscreen_read_pixels(GPUOffScreen *ofs, eGPUDataFormat format, void *pixels)
{
  lib_assert(ELEM(format, GPU_DATA_UBYTE, GPU_DATA_FLOAT));

  const int w = GPU_texture_width(ofs->color);
  const int h = GPU_texture_height(ofs->color);

  GPUFrameBuffer *ofs_fb = gpu_offscreen_fb_get(ofs);
  gpu_framebuffer_read_color(ofs_fb, 0, 0, w, h, 4, 0, format, pixels);
}

int gpu_offscreen_width(const GPUOffScreen *ofs)
{
  return gpu_texture_width(ofs->color);
}

int gpu_offscreen_height(const GPUOffScreen *ofs)
{
  return gpu_texture_height(ofs->color);
}

GPUTexture *gpu_offscreen_color_texture(const GPUOffScreen *ofs)
{
  return ofs->color;
}

void gpu_offscreen_viewport_data_get(GPUOffScreen *ofs,
                                     GPUFrameBuffer **r_fb,
                                     GPUTexture **r_color,
                                     GPUTexture **r_depth)
{
  *r_fb = gpu_offscreen_fb_get(ofs);
  *r_color = ofs->color;
  *r_depth = ofs->depth;
}
