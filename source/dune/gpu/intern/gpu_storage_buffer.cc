#include "mem_guardedalloc.h"
#include <cstring>

#include "lib_dunelib.h"
#include "lib_math_base.h"

#include "gpu_backend.hh"
#include "gpu_node_graph.h"

#include "gpu_material.h"
#include "gpu_vertex_buffer.h" /* For GPUUsageType. */

#include "gpu_storage_buffer.h"
#include "gpu_storage_buffer_private.hh"

/* Creation & Deletion */

namespace dune::gpu {

StorageBuf::StorageBuf(size_t size, const char *name)
{
  /* Make sure that UBO is padded to size of vec4 */
  lib_assert((size % 16) == 0);

  size_in_bytes_ = size;

  lib_strncpy(name_, name, sizeof(name_));
}

StorageBuf::~StorageBuf()
{
  MEM_SAFE_FREE(data_);
}

}  // namespace dune::gpu

/* C-API */

using namespace dune::gpu;

GPUStorageBuf *gpu_storagebuf_create_ex(size_t size,
                                        const void *data,
                                        GPUUsageType usage,
                                        const char *name)
{
  StorageBuf *ssbo = GPUBackend::get()->storagebuf_alloc(size, usage, name);
  /* Direct init. */
  if (data != nullptr) {
    ssbo->update(data);
  }
  return wrap(ssbo);
}

void gpu_storagebuf_free(GPUStorageBuf *ssbo)
{
  delete unwrap(ssbo);
}

void gpu_storagebuf_update(GPUStorageBuf *ssbo, const void *data)
{
  unwrap(ssbo)->update(data);
}

void gpu_storagebuf_bind(GPUStorageBuf *ssbo, int slot)
{
  unwrap(ssbo)->bind(slot);
}

void gpu_storagebuf_unbind(GPUStorageBuf *ssbo)
{
  unwrap(ssbo)->unbind();
}

void gpu_storagebuf_unbind_all()
{
  /* FIXME */
}

void gpu_storagebuf_clear(GPUStorageBuf *ssbo,
                          eGPUTextureFormat internal_format,
                          eGPUDataFormat data_format,
                          void *data)
{
  unwrap(ssbo)->clear(internal_format, data_format, data);
}

void gpu_storagebuf_clear_to_zero(GPUStorageBuf *ssbo)
{
  uint32_t data = 0u;
  gpu_storagebuf_clear(ssbo, GPU_R32UI, GPU_DATA_UINT, &data);
}
