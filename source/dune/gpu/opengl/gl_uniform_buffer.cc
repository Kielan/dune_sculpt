#include "dune_global.h"

#include "lib_string.h"

#include "gpu_backend.hh"
#include "gpu_cxt_private.hh"

#include "gl_backend.hh"
#include "gl_debug.hh"
#include "gl_uniform_buffer.hh"

namespace dune::gpu {

/* Creation & Deletion */

GLUniformBuf::GLUniformBuf(size_t size, const char *name) : UniformBuf(size, name)
{
  /* Do not create ubo GL buffer here to allow allocation from any thread. */
  lib_assert(size <= GLCxt::max_ubo_size);
}

GLUniformBuf::~GLUniformBuf()
{
  GLCxt::buf_free(ubo_id_);
}

/* Data upload / update */
void GLUniformBuf::init()
{
  lib_assert(GLCxt::get());

  glGenBuffers(1, &ubo_id_);
  glBindBuffer(GL_UNIFORM_BUFFER, ubo_id_);
  glBufferData(GL_UNIFORM_BUFFER, size_in_bytes_, nullptr, GL_DYNAMIC_DRAW);

  debug::object_label(GL_UNIFORM_BUFFER, ubo_id_, name_);
}

void GLUniformBuf::update(const void *data)
{
  if (ubo_id_ == 0) {
    this->init();
  }
  glBindBuffer(GL_UNIFORM_BUFFER, ubo_id_);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, size_in_bytes_, data);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

/* Usage */
void GLUniformBuf::bind(int slot)
{
  if (slot >= GLCxt::max_ubo_binds) {
    fprintf(stderr,
            "Error: Trying to bind \"%s\" ubo to slot %d which is above the reported limit of %d.",
            name_,
            slot,
            GLCxt::max_ubo_binds);
    return;
  }

  if (ubo_id_ == 0) {
    this->init();
  }

  if (data_ != nullptr) {
    this->update(data_);
    MEM_SAFE_FREE(data_);
  }

  slot_ = slot;
  glBindBufferBase(GL_UNIFORM_BUFFER, slot_, ubo_id_);

#ifdef DEBUG
  lib_assert(slot < 16);
  GLCxt::get()->bound_ubo_slots |= 1 << slot;
#endif
}

void GLUniformBuf::unbind()
{
#ifdef DEBUG
  /* NOTE: This only unbinds the last bound slot. */
  glBindBufferBase(GL_UNIFORM_BUFFER, slot_, 0);
  /* Hope that the context did not change. */
  GLCxt::get()->bound_ubo_slots &= ~(1 << slot_);
#endif
  slot_ = 0;
}

}  // namespace dune::gpu
