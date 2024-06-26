#pragma once

#include "mem_guardedalloc.h"

#include "gpu_uniform_buffer_private.hh"

#include "glew-mx.h"

namespace dune {
namespace gpu {

/* Implementation of Uniform Buffers using OpenGL. */
class GLUniformBuf : public UniformBuf {
 private:
  /* Slot to which this UBO is currently bound. -1 if not bound. */
  int slot_ = -1;
  /* OpenGL Object handle. */
  GLuint ubo_id_ = 0;

 public:
  GLUniformBuf(size_t size, const char *name);
  ~GLUniformBuf();

  void update(const void *data) override;
  void bind(int slot) override;
  void unbind() override;

 private:
  void init();

  MEM_CXX_CLASS_ALLOC_FUNCS("GLUniformBuf");
};

}  // namespace gpu
}  // namespace dune
