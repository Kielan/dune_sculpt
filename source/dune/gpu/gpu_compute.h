#pragma once

#include "lib_sys_types.h"

#include "gpu_shader.h"
#include "gpu_storage_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

void gpu_compute_dispatch(GpuShader *shader,
                          uint groups_x_len,
                          uint groups_y_len,
                          uint groups_z_len);

void gpu_compute_dispatch_indirect(GpuShader *shader, GpuStorageBuf *indirect_buf);

#ifdef __cplusplus
}
#endif
