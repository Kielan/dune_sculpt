#include "dune_global.h"

#include "gpu_capabilities.h"

#include "gl_backend.hh"
#include "gl_debug.hh"
#include "gl_state.hh"
#include "gl_texture.hh"

#include "gl_framebuffer.hh"

namespace dune::gpu {

/* Creation & Deletion **/
GLFrameBuffer::GLFrameBuffer(const char *name) : FrameBuffer(name)
{
  /* Just-In-Time init. See #GLFrameBuffer::init(). */
  immutable_ = false;
  fbo_id_ = 0;
}

GLFrameBuffer::GLFrameBuffer(
    const char *name, GLCxt *cxt, GLenum target, GLuint fbo, int w, int h)
    : FrameBuffer(name)
{
  cxt_ = cxt;
  state_manager_ = static_cast<GLStateManager *>(cxt->state_manager);
  immutable_ = true;
  fbo_id_ = fbo;
  gl_attachments_[0] = target;
  /* Never update an internal frame-buffer. */
  dirty_attachments_ = false;
  width_ = w;
  height_ = h;
  srgb_ = false;

  viewport_[0] = scissor_[0] = 0;
  viewport_[1] = scissor_[1] = 0;
  viewport_[2] = scissor_[2] = w;
  viewport_[3] = scissor_[3] = h;

  if (fbo_id_) {
    debug::object_label(GL_FRAMEBUFFER, fbo_id_, name_);
  }
}

GLFrameBuffer::~GLFrameBuffer()
{
  if (cxt_ == nullptr) {
    return;
  }

  /* Context might be partially freed. This happens when destroying the window frame-buffers. */
  if (cxt_ == Cxt::get()) {
    glDeleteFramebuffers(1, &fbo_id_);
  }
  else {
    cxt_->fbo_free(fbo_id_);
  }
  /* Restore default frame-buffer if this frame-buffer was bound. */
  if (cxt_->active_fb == this && cxt_->back_left != this) {
    /* If this assert triggers it means the frame-buffer is being freed while in use by another
     * cxt which, by the way, is TOTALLY UNSAFE! */
    lib_assert(context_ == Cxt::get());
    gpu_framebuffer_restore();
  }
}

void GLFrameBuffer::init()
{
  cxt_ = GLCxt::get();
  state_manager_ = static_cast<GLStateManager *>(context_->state_manager);
  glGenFramebuffers(1, &fbo_id_);
  /* Binding before setting the label is needed on some drivers.
   * This is not an issue since we call this function only before binding. */
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_id_);

  debug::object_label(GL_FRAMEBUFFER, fbo_id_, name_);
}

/* Config */
bool GLFrameBuffer::check(char err_out[256])
{
  this->bind(true);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

#define FORMAT_STATUS(X) \
  case X: { \
    err = #X; \
    break; \
  }

  const char *err;
  switch (status) {
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    FORMAT_STATUS(GL_FRAMEBUFFER_UNSUPPORTED);
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER);
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER);
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE);
    FORMAT_STATUS(GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS);
    FORMAT_STATUS(GL_FRAMEBUFFER_UNDEFINED);
    case GL_FRAMEBUFFER_COMPLETE:
      return true;
    default:
      err = "unknown";
      break;
  }

#undef FORMAT_STATUS

  const char *format = "GPUFrameBuffer: %s status %s\n";

  if (err_out) {
    lib_snprintf(err_out, 256, format, this->name_, err);
  }
  else {
    fprintf(stderr, format, this->name_, err);
  }

  return false;
}

void GLFrameBuffer::update_attachments()
{
  /* Default frame-buffers cannot have attachments. */
  lib_assert(immutable_ == false);

  /* First color texture OR the depth texture if no color is attached.
   * Used to determine frame-buffer color-space and dimensions. */
  GPUAttachmentType first_attachment = GPU_FB_MAX_ATTACHMENT;
  /* NOTE: Inverse iteration to get the first color texture. */
  for (GpuAttachmentType type = GPU_FB_MAX_ATTACHMENT - 1; type >= 0; --type) {
    GpuAttachment &attach = attachments_[type];
    GLenum gl_attachment = to_gl(type);

    if (type >= GPU_FB_COLOR_ATTACHMENT0) {
      gl_attachments_[type - GPU_FB_COLOR_ATTACHMENT0] = (attach.tex) ? gl_attachment : GL_NONE;
      first_attachment = (attach.tex) ? type : first_attachment;
    }
    else if (first_attachment == GPU_FB_MAX_ATTACHMENT) {
      /* Only use depth texture to get information if there is no color attachment. */
      first_attachment = (attach.tex) ? type : first_attachment;
    }

    if (attach.tex == nullptr) {
      glFramebufferTexture(GL_FRAMEBUFFER, gl_attachment, 0, 0);
      continue;
    }
    GLuint gl_tex = static_cast<GLTexture *>(unwrap(attach.tex))->tex_id_;
    if (attach.layer > -1 && gpu_texture_cube(attach.tex) && !gpu_texture_array(attach.tex)) {
      /* Could be avoided if ARB_direct_state_access is required. In this case
       * glFramebufferTextureLayer would bind the correct face. */
      GLenum gl_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + attach.layer;
      glFramebufferTexture2D(GL_FRAMEBUFFER, gl_attachment, gl_target, gl_tex, attach.mip);
    }
    else if (attach.layer > -1) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, gl_attachment, gl_tex, attach.mip, attach.layer);
    }
    else {
      /* The whole texture level is attached. The frame-buffer is potentially layered. */
      glFramebufferTexture(GL_FRAMEBUFFER, gl_attachment, gl_tex, attach.mip);
    }
    /* We found one depth buffer type. Stop here, otherwise we would
     * override it by setting GPU_FB_DEPTH_ATTACHMENT */
    if (type == GPU_FB_DEPTH_STENCIL_ATTACHMENT) {
      break;
    }
  }

  if (GLCxt::unused_fb_slot_workaround) {
    /* Fill normally un-occupied slots to avoid rendering artifacts on some hardware. */
    GLuint gl_tex = 0;
    /* NOTE: Inverse iteration to get the first color texture. */
    for (int i = ARRAY_SIZE(gl_attachments_) - 1; i >= 0; --i) {
      GPUAttachmentType type = GPU_FB_COLOR_ATTACHMENT0 + i;
      GPUAttachment &attach = attachments_[type];
      if (attach.tex != nullptr) {
        gl_tex = static_cast<GLTexture *>(unwrap(attach.tex))->tex_id_;
      }
      else if (gl_tex != 0) {
        GLenum gl_attachment = to_gl(type);
        gl_attachments_[i] = gl_attachment;
        glFramebufferTexture(GL_FRAMEBUFFER, gl_attachment, gl_tex, 0);
      }
    }
  }

  if (first_attachment != GPU_FB_MAX_ATTACHMENT) {
    GPUAttachment &attach = attachments_[first_attachment];
    int size[3];
    gpu_texture_get_mipmap_size(attach.tex, attach.mip, size);
    this->size_set(size[0], size[1]);
    srgb_ = (gpu_texture_format(attach.tex) == GPU_SRGB8_A8);
  }

  dirty_attachments_ = false;

  glDrawBuffers(ARRAY_SIZE(gl_attachments_), gl_attachments_);

  if (G.debug & G_DEBUG_GPU) {
    lib_assert(this->check(nullptr));
  }
}

void GLFrameBuffer::apply_state()
{
  if (dirty_state_ == false) {
    return;
  }

  glViewport(UNPACK4(viewport_));
  glScissor(UNPACK4(scissor_));

  if (scissor_test_) {
    glEnable(GL_SCISSOR_TEST);
  }
  else {
    glDisable(GL_SCISSOR_TEST);
  }

  dirty_state_ = false;
}

/* Binding */
void GLFrameBuffer::bind(bool enabled_srgb)
{
  if (!immutable_ && fbo_id_ == 0) {
    this->init();
  }

  if (cxt_ != GLCxt::get()) {
    lib_assert_msg(0, "Trying to use the same frame-buffer in multiple context");
    return;
  }

  if (cxt_->active_fb != this) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_id_);
    /* Internal frame-buffers have only one color output and needs to be set every time. */
    if (immutable_ && fbo_id_ == 0) {
      glDrawBuffer(gl_attachments_[0]);
    }
  }

  if (dirty_attachments_) {
    this->update_attachments();
    this->viewport_reset();
    this->scissor_reset();
  }

  if (cxt_->active_fb != this || enabled_srgb_ != enabled_srgb) {
    enabled_srgb_ = enabled_srgb;
    if (enabled_srgb && srgb_) {
      glEnable(GL_FRAMEBUFFER_SRGB);
    }
    else {
      glDisable(GL_FRAMEBUFFER_SRGB);
    }
    gpu_shader_set_framebuffer_srgb_target(enabled_srgb && srgb_);
  }

  if (cxt_->active_fb != this) {
    cxt_->active_fb = this;
    state_manager_->active_fb = this;
    dirty_state_ = true;
  }
}

/* Operations */
void GLFrameBuffer::clear(eGpuFrameBufferBits buffers,
                          const float clear_col[4],
                          float clear_depth,
                          uint clear_stencil)
{
  lib_assert(GLCxt::get() == cxt_);
  lib_assert(context_->active_fb == this);

  /* Save and restore the state. */
  eGpuWriteMask write_mask = gpu_write_mask_get();
  uint stencil_mask = gpu_stencil_mask_get();
  eGpuStencilTest stencil_test = gpu_stencil_test_get();

  if (buffers & GPU_COLOR_BIT) {
    gpu_color_mask(true, true, true, true);
    glClearColor(clear_col[0], clear_col[1], clear_col[2], clear_col[3]);
  }
  if (buffers & GPU_DEPTH_BIT) {
    gpu_depth_mask(true);
    glClearDepth(clear_depth);
  }
  if (buffers & GPU_STENCIL_BIT) {
    gpu_stencil_write_mask_set(0xFFu);
    gpu_stencil_test(GPU_STENCIL_ALWAYS);
    glClearStencil(clear_stencil);
  }

  cxt_->state_manager->apply_state();

  GLbitfield mask = to_gl(buffers);
  glClear(mask);

  if (buffers & (GPU_COLOR_BIT | GPU_DEPTH_BIT)) {
    gpu_write_mask(write_mask);
  }
  if (buffers & GPU_STENCIL_BIT) {
    gpu_stencil_write_mask_set(stencil_mask);
    gpu_stencil_test(stencil_test);
  }
}

void GLFrameBuffer::clear_attachment(GPUAttachmentType type,
                                     eGPUDataFormat data_format,
                                     const void *clear_value)
{
  lib_assert(GLCxt::get() == cxt_);
  lib_assert(cxt_->active_fb == this);

  /* Save and restore the state. */
  eGPUWriteMask write_mask = gpu_write_mask_get();
  gpu_color_mask(true, true, true, true);

  cxt_->state_manager->apply_state();

  if (type == GPU_FB_DEPTH_STENCIL_ATTACHMENT) {
    lib_assert(data_format == GPU_DATA_UINT_24_8);
    float depth = ((*(uint32_t *)clear_value) & 0x00FFFFFFu) / (float)0x00FFFFFFu;
    int stencil = ((*(uint32_t *)clear_value) >> 24);
    glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
  }
  else if (type == GPU_FB_DEPTH_ATTACHMENT) {
    if (data_format == GPU_DATA_FLOAT) {
      glClearBufferfv(GL_DEPTH, 0, (GLfloat *)clear_value);
    }
    else if (data_format == GPU_DATA_UINT) {
      float depth = *(uint32_t *)clear_value / (float)0xFFFFFFFFu;
      glClearBufferfv(GL_DEPTH, 0, &depth);
    }
    else {
      lib_assert_msg(0, "Unhandled data format");
    }
  }
  else {
    int slot = type - GPU_FB_COLOR_ATTACHMENT0;
    switch (data_format) {
      case GPU_DATA_FLOAT:
        glClearBufferfv(GL_COLOR, slot, (GLfloat *)clear_value);
        break;
      case GPU_DATA_UINT:
        glClearBufferuiv(GL_COLOR, slot, (GLuint *)clear_value);
        break;
      case GPU_DATA_INT:
        glClearBufferiv(GL_COLOR, slot, (GLint *)clear_value);
        break;
      default:
        BLI_assert_msg(0, "Unhandled data format");
        break;
    }
  }

  gpu_write_mask(write_mask);
}

void GLFrameBuffer::clear_multi(const float (*clear_cols)[4])
{
  /* WATCH: This can easily access clear_cols out of bounds it clear_cols is not big enough for
   * all attachments.
   * TODO: fix this insecurity? */
  int type = GPU_FB_COLOR_ATTACHMENT0;
  for (int i = 0; type < GPU_FB_MAX_ATTACHMENT; i++, type++) {
    if (attachments_[type].tex != nullptr) {
      this->clear_attachment(GPU_FB_COLOR_ATTACHMENT0 + i, GPU_DATA_FLOAT, clear_cols[i]);
    }
  }
}

void GLFrameBuffer::read(eGPUFrameBufferBits plane,
                         eGPUDataFormat data_format,
                         const int area[4],
                         int channel_len,
                         int slot,
                         void *r_data)
{
  GLenum format, type, mode;
  mode = gl_attachments_[slot];
  type = to_gl(data_format);

  switch (plane) {
    case GPU_DEPTH_BIT:
      format = GL_DEPTH_COMPONENT;
      lib_assert_msg(
          this->attachments_[GPU_FB_DEPTH_ATTACHMENT].tex != nullptr ||
              this->attachments_[GPU_FB_DEPTH_STENCIL_ATTACHMENT].tex != nullptr,
          "GPUFramebuffer: Error: Trying to read depth without a depth buffer attached.");
      break;
    case GPU_COLOR_BIT:
      lib_assert_msg(
          mode != GL_NONE,
          "GPUFramebuffer: Error: Trying to read a color slot without valid attachment.");
      format = channel_len_to_gl(channel_len);
      /* TODO: needed for selection buffers to work properly, this should be handled better. */
      if (format == GL_RED && type == GL_UNSIGNED_INT) {
        format = GL_RED_INTEGER;
      }
      break;
    case GPU_STENCIL_BIT:
      fprintf(stderr, "GPUFramebuffer: Error: Trying to read stencil bit. Unsupported.");
      return;
    default:
      fprintf(stderr, "GPUFramebuffer: Error: Trying to read more than one frame-buffer plane.");
      return;
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_id_);
  glReadBuffer(mode);
  glReadPixels(UNPACK4(area), format, type, r_data);
}

void GLFrameBuffer::blit_to(
    eGPUFrameBufferBits planes, int src_slot, FrameBuffer *dst_, int dst_slot, int x, int y)
{
  GLFrameBuffer *src = this;
  GLFrameBuffer *dst = static_cast<GLFrameBuffer *>(dst_);

  /* Frame-buffers must be up to date. This simplify this function. */
  if (src->dirty_attachments_) {
    src->bind(true);
  }
  if (dst->dirty_attachments_) {
    dst->bind(true);
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo_id_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->fbo_id_);

  if (planes & GPU_COLOR_BIT) {
    lib_assert(src->immutable_ == false || src_slot == 0);
    lib_assert(dst->immutable_ == false || dst_slot == 0);
    lib_assert(src->gl_attachments_[src_slot] != GL_NONE);
    lib_assert(dst->gl_attachments_[dst_slot] != GL_NONE);
    glReadBuffer(src->gl_attachments_[src_slot]);
    glDrawBuffer(dst->gl_attachments_[dst_slot]);
  }

  context_->state_manager->apply_state();

  int w = src->width_;
  int h = src->height_;
  GLbitfield mask = to_gl(planes);
  glBlitFramebuffer(0, 0, w, h, x, y, x + w, y + h, mask, GL_NEAREST);

  if (!dst->immutable_) {
    /* Restore the draw buffers. */
    glDrawBuffers(ARRAY_SIZE(dst->gl_attachments_), dst->gl_attachments_);
  }
  /* Ensure previous buffer is restored. */
  cxt_->active_fb = dst;
}

}  // namespace dune::gpu
