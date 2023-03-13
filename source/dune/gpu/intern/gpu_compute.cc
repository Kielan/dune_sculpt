#include "gpu_compute.h"

#include "gpu_backend.hh"
#include "gpu_storage_buffer_private.hh"

void gpu_compute_dispatch(GPUShader *shader,
                          uint groups_x_len,
                          uint groups_y_len,
                          uint groups_z_len)
{
  dune::gpu::GPUBackend &gpu_backend = *dune::gpu::GPUBackend::get();
  gpu_shader_bind(shader);
  gpu_backend.compute_dispatch(groups_x_len, groups_y_len, groups_z_len);
}

void gpu_compute_dispatch_indirect(GPUShader *shader, GPUStorageBuf *indirect_buf_)
{
  dune::gpu::GPUBackend &gpu_backend = *dune::gpu::GPUBackend::get();
  dune::gpu::StorageBuf *indirect_buf = reinterpret_cast<dune::gpu::StorageBuf *>(
      indirect_buf_);

  gpu_shader_bind(shader);
  gpu_backend.compute_dispatch_indirect(indirect_buf);
}
