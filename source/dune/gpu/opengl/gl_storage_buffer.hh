#pragma once

#include "mem_guardedalloc.h"

#include "gpu_storage_buffer_private.hh"

#include "glew-mx.h"

namespace dune {
namespace gpu {

/* Implementation of Storage Buffers using OpenGL. */
class GLStorageBuf : public StorageBuf {
 private:
  /* Slot to which this UBO is currently bound. -1 if not bound. */
  int slot_ = -1;
  /* OpenGL Object handle. */
  GLuint ssbo_id_ = 0;
  /* Usage type. */
  GPUUsageType usage_;

 public:
  GLStorageBuf(size_t size, GPUUsageType usage, const char *name);
  ~GLStorageBuf();

  void update(const void *data) override;
  void bind(int slot) override;
  void unbind() override;
  void clear(eGPUTextureFormat internal_format, eGPUDataFormat data_format, void *data) override;

  /* Special internal function to bind SSBOs to indirect argument targets. */
  void bind_as(GLenum target);

 private:
  void init();

  MEM_CXX_CLASS_ALLOC_FUNCS("GLStorageBuf");
};

}  // namespace gpu
}  // namespace dune
