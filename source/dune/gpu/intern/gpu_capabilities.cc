/* Wrap OpenGL features such as textures, shaders and GLSL
 * with checks for drivers and GPU support. */

#include "types_userdef.h"

#include "gpu_capabilities.h"

#include "gpu_cxt_private.hh"

#include "gpu_capabilities_private.hh"

namespace dune::gpu {

GPUCapabilities GCaps;

}

using namespace dune::gpu;

/* Capabilities */
int gpu_max_texture_size()
{
  return GCaps.max_texture_size;
}

int gpu_texture_size_with_limit(int res, bool limit_gl_texture_size)
{
  int size = gpu_max_texture_size();
  int reslimit = (limit_gl_texture_size && (U.glreslimit != 0)) ? min_ii(U.glreslimit, size) :
                                                                  size;
  return min_ii(reslimit, res);
}

int gpu_max_texture_layers()
{
  return GCaps.max_texture_layers;
}

int gpu_max_textures_vert()
{
  return GCaps.max_textures_vert;
}

int gpu_max_textures_geom()
{
  return GCaps.max_textures_geom;
}

int gpu_max_textures_frag()
{
  return GCaps.max_textures_frag;
}

int gpu_max_textures()
{
  return GCaps.max_textures;
}

int gpu_max_work_group_count(int index)
{
  return GCaps.max_work_group_count[index];
}

int gpu_max_work_group_size(int index)
{
  return GCaps.max_work_group_size[index];
}

int gpu_max_uniforms_vert()
{
  return GCaps.max_uniforms_vert;
}

int gpu_max_uniforms_frag()
{
  return GCaps.max_uniforms_frag;
}

int gpu_max_batch_indices()
{
  return GCaps.max_batch_indices;
}

int gpu_max_batch_vertices()
{
  return GCaps.max_batch_vertices;
}

int gpu_max_vertex_attribs()
{
  return GCaps.max_vertex_attribs;
}

int gpu_max_varying_floats()
{
  return GCaps.max_varying_floats;
}

int gpu_extensions_len()
{
  return GCaps.extensions_len;
}

const char *gpu_extension_get(int i)
{
  return GCaps.extension_get ? GCaps.extension_get(i) : "\0";
}

bool gpu_mip_render_workaround()
{
  return GCaps.mip_render_workaround;
}

bool gpu_depth_blitting_workaround()
{
  return GCaps.depth_blitting_workaround;
}

bool gpu_use_main_cxt_workaround()
{
  return GCaps.use_main_context_workaround;
}

bool gpu_crappy_amd_driver()
{
  /* Currently are the same drivers with the `unused_fb_slot` problem. */
  return GCaps.broken_amd_driver;
}

bool gpu_use_hq_normals_workaround()
{
  return GCaps.use_hq_normals_workaround;
}

bool gpu_compute_shader_support()
{
  return GCaps.compute_shader_support;
}

bool gpu_shader_storage_buffer_objects_support()
{
  return GCaps.shader_storage_buffer_objects_support;
}

bool gpu_shader_image_load_store_support()
{
  return GCaps.shader_image_load_store_support;
}

int gpu_max_shader_storage_buffer_bindings()
{
  return GCaps.max_shader_storage_buffer_bindings;
}

/* Memory statistics */
bool gpu_mem_stats_supported()
{
  return GCaps.mem_stats_support;
}

void gpu_mem_stats_get(int *totalmem, int *freemem)
{
  Cxt::get()->memory_statistics_get(totalmem, freemem);
}

bool gpu_stereo_quadbuffer_support()
{
  return Cxt::get()->front_right != nullptr;
}
