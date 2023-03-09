/**
 * GPU Capabilities & workarounds
 * This module expose the reported implementation limits & enabled
 * workaround for drivers that needs specific code-paths.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int gpu_max_texture_size(void);
int gpu_max_texture_layers(void);
int gpu_max_textures(void);
int gpu_max_textures_vert(void);
int gpu_max_textures_geom(void);
int gpu_max_textures_frag(void);
int gpu_max_work_group_count(int index);
int gpu_max_work_group_size(int index);
int gpu_max_uniforms_vert(void);
int gpu_max_uniforms_frag(void);
int gpu_max_batch_indices(void);
int gpu_max_batch_vertices(void);
int gpu_max_vertex_attribs(void);
int gpu_max_varying_floats(void);
int gpu_max_shader_storage_buffer_bindings(void);

int gpu_extensions_len(void);
const char *gpu_extension_get(int i);
int gpu_texture_size_with_limit(int res, bool limit_gl_texture_size);

bool gpu_mip_render_workaround(void);
bool gpu_depth_blitting_workaround(void);
bool gpu_use_main_context_workaround(void);
bool gpu_use_hq_normals_workaround(void);
bool gpu_crappy_amd_driver(void);

bool gpu_compute_shader_support(void);
bool gpu_shader_storage_buffer_objects_support(void);
bool gpu_shader_image_load_store_support(void);

bool gpu_mem_stats_supported(void);
void gpu_mem_stats_get(int *totalmem, int *freemem);

/**
 * Return support for the active context + window.
 */
bool GPU_stereo_quadbuffer_support(void);

#ifdef __cplusplus
}
#endif
