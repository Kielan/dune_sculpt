/**
 * Descriptor type used to define shader structure, resources and interfaces.
 *
 * Some rule of thumb:
 * - Do not include anything else than this file in each descriptor file.
 */

#pragma once

#include "gpu_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

void gpu_shader_create_info_init(void);
void gpu_shader_create_info_exit(void);

bool gpu_shader_create_info_compile_all(void);

const GPUShaderCreateInfo *gpu_shader_create_info_get(const char *info_name);

#ifdef __cplusplus
}
#endif
