/* GL implementation of GPUBatch.
 * The only specificity of GL here is that it caches a list of
 * Vertex Array Objects based on the bound shader interface. */

#include "lib_assert.h"

#include "glew-mx.h"

#include "gpu_batch_private.hh"
#include "gpu_shader_private.hh"

#include "gl_backend.hh"
#include "gl_cxt.hh"
#include "gl_debug.hh"
#include "gl_index_buffer.hh"
#include "gl_primitive.hh"
#include "gl_vertex_array.hh"

#include "gl_batch.hh"

using namespace dune::gpu;

/* VAO Cache
 * Each GLBatch has a small cache of VAO objects that are used to avoid VAO reconfiguration.
 * TODO: Could be revisited to avoid so much cross refs. */

GLVaoCache::GLVaoCache()
{
  init();
}

GLVaoCache::~GLVaoCache()
{
  this->clear();
}

void GLVaoCache::init()
{
  cxt_ = nullptr;
  interface_ = nullptr;
  is_dynamic_vao_count = false;
  for (int i = 0; i < GPU_VAO_STATIC_LEN; i++) {
    static_vaos.interfaces[i] = nullptr;
    static_vaos.vao_ids[i] = 0;
  }
  vao_base_instance_ = 0;
  base_instance_ = 0;
  vao_id_ = 0;
}

void GLVaoCache::insert(const GLShaderInterface *interface, GLuint vao)
{
  /* Now insert the cache. */
  if (!is_dynamic_vao_count) {
    int i; /* find first unused slot */
    for (i = 0; i < GPU_VAO_STATIC_LEN; i++) {
      if (static_vaos.vao_ids[i] == 0) {
        break;
      }
    }

    if (i < GPU_VAO_STATIC_LEN) {
      static_vaos.interfaces[i] = interface;
      static_vaos.vao_ids[i] = vao;
    }
    else {
      /* Erase previous entries, they will be added back if drawn again. */
      for (int i = 0; i < GPU_VAO_STATIC_LEN; i++) {
        if (static_vaos.interfaces[i] != nullptr) {
          const_cast<GLShaderInterface *>(static_vaos.interfaces[i])->ref_remove(this);
          cxt_->vao_free(static_vaos.vao_ids[i]);
        }
      }
      /* Not enough place switch to dynamic. */
      is_dynamic_vao_count = true;
      /* Init dynamic arrays and let the branch below set the values. */
      dynamic_vaos.count = GPU_BATCH_VAO_DYN_ALLOC_COUNT;
      dynamic_vaos.interfaces = (const GLShaderInterface **)mem_callocn(
          dynamic_vaos.count * sizeof(GLShaderInterface *), "dyn vaos interfaces");
      dynamic_vaos.vao_ids = (GLuint *)mem_callocn(dynamic_vaos.count * sizeof(GLuint),
                                                   "dyn vaos ids");
    }
  }

  if (is_dynamic_vao_count) {
    int i; /* find first unused slot */
    for (i = 0; i < dynamic_vaos.count; i++) {
      if (dynamic_vaos.vao_ids[i] == 0) {
        break;
      }
    }

    if (i == dynamic_vaos.count) {
      /* Not enough place, realloc the array. */
      i = dynamic_vaos.count;
      dynamic_vaos.count += GPU_BATCH_VAO_DYN_ALLOC_COUNT;
      dynamic_vaos.interfaces = (const GLShaderInterface **)mem_recallocn(
          (void *)dynamic_vaos.interfaces, sizeof(GLShaderInterface *) * dynamic_vaos.count);
      dynamic_vaos.vao_ids = (GLuint *)mem_recallocn(dynamic_vaos.vao_ids,
                                                     sizeof(GLuint) * dynamic_vaos.count);
    }
    dynamic_vaos.interfaces[i] = interface;
    dynamic_vaos.vao_ids[i] = vao;
  }

  const_cast<GLShaderInterface *>(interface)->ref_add(this);
}

void GLVaoCache::remove(const GLShaderInterface *interface)
{
  const int count = (is_dynamic_vao_count) ? dynamic_vaos.count : GPU_VAO_STATIC_LEN;
  GLuint *vaos = (is_dynamic_vao_count) ? dynamic_vaos.vao_ids : static_vaos.vao_ids;
  const GLShaderInterface **interfaces = (is_dynamic_vao_count) ? dynamic_vaos.interfaces :
                                                                  static_vaos.interfaces;
  for (int i = 0; i < count; i++) {
    if (interfaces[i] == interface) {
      cxt_->vao_free(vaos[i]);
      vaos[i] = 0;
      interfaces[i] = nullptr;
      break; /* cannot have duplicates */
    }
  }
}

void GLVaoCache::clear()
{
  GLContext *cxt = GLContext::get();
  const int count = (is_dynamic_vao_count) ? dynamic_vaos.count : GPU_VAO_STATIC_LEN;
  GLuint *vaos = (is_dynamic_vao_count) ? dynamic_vaos.vao_ids : static_vaos.vao_ids;
  const GLShaderInterface **interfaces = (is_dynamic_vao_count) ? dynamic_vaos.interfaces :
                                                                  static_vaos.interfaces;
  /* Early out, nothing to free. */
  if (context_ == nullptr) {
    return;
  }

  if (context_ == ctx) {
    glDeleteVertexArrays(count, vaos);
    glDeleteVertexArrays(1, &vao_base_instance_);
  }
  else {
    /* TODO(fclem): Slow way. Could avoid multiple mutex lock here */
    for (int i = 0; i < count; i++) {
      context_->vao_free(vaos[i]);
    }
    context_->vao_free(vao_base_instance_);
  }

  for (int i = 0; i < count; i++) {
    if (interfaces[i] != nullptr) {
      const_cast<GLShaderInterface *>(interfaces[i])->ref_remove(this);
    }
  }

  if (is_dynamic_vao_count) {
    mem_freen((void *)dynamic_vaos.interfaces);
    mem_freen(dynamic_vaos.vao_ids);
  }

  if (context_) {
    context_->vao_cache_unregister(this);
  }
  /* Reinit. */
  this->init();
}

GLuint GLVaoCache::lookup(const GLShaderInterface *interface)
{
  const int count = (is_dynamic_vao_count) ? dynamic_vaos.count : GPU_VAO_STATIC_LEN;
  const GLShaderInterface **interfaces = (is_dynamic_vao_count) ? dynamic_vaos.interfaces :
                                                                  static_vaos.interfaces;
  for (int i = 0; i < count; i++) {
    if (interfaces[i] == interface) {
      return (is_dynamic_vao_count) ? dynamic_vaos.vao_ids[i] : static_vaos.vao_ids[i];
    }
  }
  return 0;
}

void GLVaoCache::context_check()
{
  GLContext *ctx = GLContext::get();
  lib_assert(ctx);

  if (context_ != ctx) {
    if (context_ != nullptr) {
      /* IMPORTANT: Trying to draw a batch in multiple different context will trash the VAO cache.
       * This has major performance impact and should be avoided in most cases. */
      context_->vao_cache_unregister(this);
    }
    this->clear();
    context_ = ctx;
    context_->vao_cache_register(this);
  }
}

GLuint GLVaoCache::base_instance_vao_get(GPUBatch *batch, int i_first)
{
  this->context_check();
  /* Make sure the interface is up to date. */
  Shader *shader = GLContext::get()->shader;
  GLShaderInterface *interface = static_cast<GLShaderInterface *>(shader->interface);
  if (interface_ != interface) {
    vao_get(batch);
    /* Trigger update. */
    base_instance_ = 0;
  }
  /**
   * There seems to be a nasty bug when drawing using the same VAO reconfiguring (T71147).
   * We just use a throwaway VAO for that. Note that this is likely to degrade performance.
   */
#ifdef __APPLE__
  glDeleteVertexArrays(1, &vao_base_instance_);
  vao_base_instance_ = 0;
  base_instance_ = 0;
#endif

  if (vao_base_instance_ == 0) {
    glGenVertexArrays(1, &vao_base_instance_);
  }

  if (base_instance_ != i_first) {
    base_instance_ = i_first;
    GLVertArray::update_bindings(vao_base_instance_, batch, interface_, i_first);
  }
  return vao_base_instance_;
}

GLuint GLVaoCache::vao_get(GPUBatch *batch)
{
  this->context_check();

  Shader *shader = GLContext::get()->shader;
  GLShaderInterface *interface = static_cast<GLShaderInterface *>(shader->interface);
  if (interface_ != interface) {
    interface_ = interface;
    vao_id_ = this->lookup(interface_);

    if (vao_id_ == 0) {
      /* Cache miss, create a new VAO. */
      glGenVertexArrays(1, &vao_id_);
      this->insert(interface_, vao_id_);
      GLVertArray::update_bindings(vao_id_, batch, interface_, 0);
    }
  }

  return vao_id_;
}

/* -------------------------------------------------------------------- */
/** Drawing */

void glBatch::bind(int i_first)
{
  glContext::get()->state_manager->apply_state();

  if (flag & GPU_BATCH_DIRTY) {
    flag &= ~GPU_BATCH_DIRTY;
    vao_cache_.clear();
  }

#if GPU_TRACK_INDEX_RANGE
  /* Can be removed if GL 4.3 is required. */
  if (!glContext::fixed_restart_index_support && (elem != nullptr)) {
    glPrimitiveRestartIndex(this->elem_()->restart_index());
  }
#endif

  /* Can be removed if GL 4.2 is required. */
  if (!glContext::base_instance_support && (i_first > 0)) {
    glBindVertexArray(vao_cache_.base_instance_vao_get(this, i_first));
  }
  else {
    glBindVertexArray(vao_cache_.vao_get(this));
  }
}

void glBatch::draw(int v_first, int v_count, int i_first, int i_count)
{
  GL_CHECK_RESOURCES("Batch");

  this->bind(i_first);

  lib_assert(v_count > 0 && i_count > 0);

  GLenum gl_type = to_gl(prim_type);

  if (elem) {
    const GLIndexBuf *el = this->elem_();
    GLenum index_type = to_gl(el->index_type_);
    GLint base_index = el->index_base_;
    void *v_first_ofs = el->offset_ptr(v_first);

    if (glContext::base_instance_support) {
      glDrawElementsInstancedBaseVertexBaseInstance(
          gl_type, v_count, index_type, v_first_ofs, i_count, base_index, i_first);
    }
    else {
      glDrawElementsInstancedBaseVertex(
          gl_type, v_count, index_type, v_first_ofs, i_count, base_index);
    }
  }
  else {
#ifdef __APPLE__
    glDisable(GL_PRIMITIVE_RESTART);
#endif
    if (glContext::base_instance_support) {
      glDrawArraysInstancedBaseInstance(gl_type, v_first, v_count, i_count, i_first);
    }
    else {
      glDrawArraysInstanced(gl_type, v_first, v_count, i_count);
    }
#ifdef __APPLE__
    glEnable(GL_PRIMITIVE_RESTART);
#endif
  }
}
