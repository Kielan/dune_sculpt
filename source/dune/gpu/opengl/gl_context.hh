#pragma once

#include "gpu_cxt_private.hh"

#include "gpu_framebuffer.h"

#include "lib_set.hh"
#include "lib_vector.hh"

#include "gl_state.hh"

#include "glew-mx.h"

#include <mutex>

namespace dune {
namespace gpu {

class GLVaoCache;

class GLSharedOrphanLists {
 public:
  /* Mutex for the below structures. */
  std::mutex lists_mutex;
  /* Buffers and textures are shared across context. Any context can free them. */
  Vector<GLuint> textures;
  Vector<GLuint> buffers;

 public:
  void orphans_clear();
};

class GLCxt : public Cxt {
 public:
  /* Capabilities. */
  static GLint max_cubemap_size;
  static GLint max_texture_3d_size;
  static GLint max_ubo_size;
  static GLint max_ubo_binds;
  static GLint max_ssbo_size;
  static GLint max_ssbo_binds;

  /* Extensions. */
  static bool base_instance_support;
  static bool clear_texture_support;
  static bool copy_image_support;
  static bool debug_layer_support;
  static bool direct_state_access_support;
  static bool explicit_location_support;
  static bool geometry_shader_invocations;
  static bool fixed_restart_index_support;
  static bool layered_rendering_support;
  static bool native_barycentric_support;
  static bool multi_bind_support;
  static bool multi_draw_indirect_support;
  static bool shader_draw_parameters_support;
  static bool stencil_texturing_support;
  static bool texture_cube_map_array_support;
  static bool texture_filter_anisotropic_support;
  static bool texture_gather_support;
  static bool texture_storage_support;
  static bool vertex_attrib_binding_support;

  /* Workarounds. */
  static bool debug_layer_workaround;
  static bool unused_fb_slot_workaround;
  static bool generate_mipmap_workaround;
  static float derivative_signs[2];

  /* VBO for missing vertex attrib binding. Avoid undefined behavior on some implementation. */
  GLuint default_attr_vbo_;

  /* Used for debugging purpose. Bitflags of all bound slots. */
  uint16_t bound_ubo_slots;

 private:
  /* GPUBatch & GPUFramebuffer have refs to the cxt they are from, in the case the
   * cxt is destroyed, we need to remove any ref to it */
  Set<GLVaoCache *> vao_caches_;
  Set<GPUFrameBuffer *> framebuffers_;
  /* Mutex for the below structures. */
  std::mutex lists_mutex_;
  /* VertexArrays and framebuffers are not shared across cxt. */
  Vector<GLuint> orphaned_vertarrays_;
  Vector<GLuint> orphaned_framebuffers_;
  /* GLBackend owns this data. */
  GLSharedOrphanLists &shared_orphan_list_;

 public:
  GLCxt(void *ghost_window, GLSharedOrphanLists &shared_orphan_list);
  ~GLCxt();

  static void check_error(const char *info);

  void activate() override;
  void deactivate() override;

  void flush() override;
  void finish() override;

  void memory_statistics_get(int *total_mem, int *free_mem) override;

  static GLCxt *get()
  {
    return static_cast<GLCxt *>(Cxt::get());
  }

  static GLStateManager *state_manager_active_get()
  {
    GLCxt *cxt = GLCxt::get();
    return static_cast<GLStateManager *>(cxt->state_manager);
  };

  /* These need to be called with the context the id was created with. */
  void vao_free(GLuint vao_id);
  void fbo_free(GLuint fbo_id);
  /* These can be called by any threads even without OpenGL cxt. Deletion will be delayed. */
  static void buf_free(GLuint buf_id);
  static void tex_free(GLuint tex_id);

  void vao_cache_register(GLVaoCache *cache);
  void vao_cache_unregister(GLVaoCache *cache);

  void debug_group_begin(const char *name, int index) override;
  void debug_group_end() override;

 private:
  static void orphans_add(Vector<GLuint> &orphan_list, std::mutex &list_mutex, GLuint id);
  void orphans_clear();

  MEM_CXX_CLASS_ALLOC_FNS("GLCxt")
};

}  // namespace gpu
}  // namespace dune
