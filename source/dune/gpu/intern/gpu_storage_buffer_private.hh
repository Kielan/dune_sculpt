#pragma once

#include "lib_sys_types.h"

struct GPUStorageBuf;

namespace dune {
namespace gpu {

#ifdef DEBUG
#  define DEBUG_NAME_LEN 64
#else
#  define DEBUG_NAME_LEN 8
#endif

/**
 * Implementation of Storage Buffers.
 * Base class which is then specialized for each implementation (GL, VK, ...).
 */
class StorageBuf {
 protected:
  /** Data size in bytes. */
  size_t size_in_bytes_;
  /** Continuous memory block to copy to GPU. This data is owned by the StorageBuf. */
  void *data_ = NULL;
  /** Debugging name */
  char name_[DEBUG_NAME_LEN];

 public:
  StorageBuf(size_t size, const char *name);
  virtual ~StorageBuf();

  virtual void update(const void *data) = 0;
  virtual void bind(int slot) = 0;
  virtual void unbind() = 0;
  virtual void clear(eGPUTextureFormat internal_format,
                     eGPUDataFormat data_format,
                     void *data) = 0;
};

/* Syntactic sugar. */
static inline GPUStorageBuf *wrap(StorageBuf *vert)
{
  return reinterpret_cast<GPUStorageBuf *>(vert);
}
static inline StorageBuf *unwrap(GPUStorageBuf *vert)
{
  return reinterpret_cast<StorageBuf *>(vert);
}
static inline const StorageBuf *unwrap(const GPUStorageBuf *vert)
{
  return reinterpret_cast<const StorageBuf *>(vert);
}

#undef DEBUG_NAME_LEN

}  // namespace gpu
}  // namespace dune
