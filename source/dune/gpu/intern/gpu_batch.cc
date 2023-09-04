/* GPU geometry batch
 * Contains VAOs + VBOs + Shader representing a drawable entity. */

#include "mem_guardedalloc.h"

#include "lib_math_base.h"

#include "gpu_batch.h"
#include "gpu_batch_presets.h"
#include "gpu_matrix.h"
#include "gpu_platform.h"
#include "gpu_shader.h"

#include "gpu_backend.hh"
#include "gpu_cxt_private.hh"
#include "gpu_index_buffer_private.hh"
#include "gpu_shader_private.hh"
#include "gpu_vertex_buffer_private.hh"

#include "gpu_batch_private.hh"

#include <cstring>

using namespace dune::gpu;

/* Creation & Deletion */
GPUBatch *gpu_batch_calloc()
{
  GPUBatch *batch = GPUBackend::get()->batch_alloc();
  memset(batch, 0, sizeof(*batch));
  return batch;
}

GPUBatch *gpu_batch_create_ex(GPUPrimType prim_type,
                              GPUVertBuf *verts,
                              GPUIndexBuf *elem,
                              eGPUBatchFlag owns_flag)
{
  GPUBatch *batch = gpu_batch_calloc();
  gpu_batch_init_ex(batch, prim_type, verts, elem, owns_flag);
  return batch;
}

void gpu_batch_init_ex(GPUBatch *batch,
                       GPUPrimType prim_type,
                       GPUVertBuf *verts,
                       GPUIndexBuf *elem,
                       eGPUBatchFlag owns_flag)
{
  lib_assert(verts != nullptr);
  /* Do not pass any other flag */
  lib_assert((owns_flag & ~(GPU_BATCH_OWNS_VBO | GPU_BATCH_OWNS_INDEX)) == 0);

  batch->verts[0] = verts;
  for (int v = 1; v < GPU_BATCH_VBO_MAX_LEN; v++) {
    batch->verts[v] = nullptr;
  }
  for (auto &v : batch->inst) {
    v = nullptr;
  }
  batch->elem = elem;
  batch->prim_type = prim_type;
  batch->flag = owns_flag | GPU_BATCH_INIT | GPU_BATCH_DIRTY;
  batch->shader = nullptr;
}

void gpu_batch_copy(GPUBatch *batch_dst, GPUBatch *batch_src)
{
  gpu_batch_init_ex(
      batch_dst, GPU_PRIM_POINTS, batch_src->verts[0], batch_src->elem, GPU_BATCH_INVALID);

  batch_dst->prim_type = batch_src->prim_type;
  for (int v = 1; v < GPU_BATCH_VBO_MAX_LEN; v++) {
    batch_dst->verts[v] = batch_src->verts[v];
  }
}

void gpu_batch_clear(GPUBatch *batch)
{
  if (batch->flag & GPU_BATCH_OWNS_INDEX) {
    gpu_indexbuf_discard(batch->elem);
  }
  if (batch->flag & GPU_BATCH_OWNS_VBO_ANY) {
    for (int v = 0; (v < GPU_BATCH_VBO_MAX_LEN) && batch->verts[v]; v++) {
      if (batch->flag & (GPU_BATCH_OWNS_VBO << v)) {
        GPU_VERTBUF_DISCARD_SAFE(batch->verts[v]);
      }
    }
  }
  if (batch->flag & GPU_BATCH_OWNS_INST_VBO_ANY) {
    for (int v = 0; (v < GPU_BATCH_INST_VBO_MAX_LEN) && batch->inst[v]; v++) {
      if (batch->flag & (GPU_BATCH_OWNS_INST_VBO << v)) {
        GPU_VERTBUF_DISCARD_SAFE(batch->inst[v]);
      }
    }
  }
  batch->flag = GPU_BATCH_INVALID;
}

void gpu_batch_discard(GPUBatch *batch)
{
  gpu_batch_clear(batch);

  delete static_cast<Batch *>(batch);
}

/* Buffers Management */
void gpu_batch_instbuf_set(GPUBatch *batch, GPUVertBuf *inst, bool own_vbo)
{
  lib_assert(inst);
  batch->flag |= GPU_BATCH_DIRTY;

  if (batch->inst[0] && (batch->flag & GPU_BATCH_OWNS_INST_VBO)) {
    lib_vertbuf_discard(batch->inst[0]);
  }
  batch->inst[0] = inst;

  SET_FLAG_FROM_TEST(batch->flag, own_vbo, GPU_BATCH_OWNS_INST_VBO);
}

void gpu_batch_elembuf_set(GPUBatch *batch, GPUIndexBuf *elem, bool own_ibo)
{
  lib_assert(elem);
  batch->flag |= GPU_BATCH_DIRTY;

  if (batch->elem && (batch->flag & GPU_BATCH_OWNS_INDEX)) {
    gpu_indexbuf_discard(batch->elem);
  }
  batch->elem = elem;

  SET_FLAG_FROM_TEST(batch->flag, own_ibo, GPU_BATCH_OWNS_INDEX);
}

int gpu_batch_instbuf_add_ex(GPUBatch *batch, GPUVertBuf *insts, bool own_vbo)
{
  lib_assert(insts);
  batch->flag |= GPU_BATCH_DIRTY;

  for (uint v = 0; v < GPU_BATCH_INST_VBO_MAX_LEN; v++) {
    if (batch->inst[v] == nullptr) {
      /* for now all VertexBuffers must have same vertex_len */
      if (batch->inst[0]) {
        /* Allow for different size of vertex buffer (will choose the smallest number of verts). */
        // lib_assert(insts->vertex_len == batch->inst[0]->vertex_len);
      }

      batch->inst[v] = insts;
      SET_FLAG_FROM_TEST(batch->flag, own_vbo, (eGPUBatchFlag)(GPU_BATCH_OWNS_INST_VBO << v));
      return v;
    }
  }
  /* we only make it this far if there is no room for another GPUVertBuf */
  lib_assert_msg(0, "Not enough Instance VBO slot in batch");
  return -1;
}

int gpu_batch_vertbuf_add_ex(GPUBatch *batch, GPUVertBuf *verts, bool own_vbo)
{
  lib_assert(verts);
  batch->flag |= GPU_BATCH_DIRTY;

  for (uint v = 0; v < GPU_BATCH_VBO_MAX_LEN; v++) {
    if (batch->verts[v] == nullptr) {
      /* for now all VertexBuffers must have same vertex_len */
      if (batch->verts[0] != nullptr) {
        /* This is an issue for the HACK inside DRW_vbo_request(). */
        // lib_assert(verts->vertex_len == batch->verts[0]->vertex_len);
      }
      batch->verts[v] = verts;
      SET_FLAG_FROM_TEST(batch->flag, own_vbo, (eGPUBatchFlag)(GPU_BATCH_OWNS_VBO << v));
      return v;
    }
  }
  /* we only make it this far if there is no room for another GPUVertBuf */
  lib_assert_msg(0, "Not enough VBO slot in batch");
  return -1;
}

bool gpu_batch_vertbuf_has(GPUBatch *batch, GPUVertBuf *verts)
{
  for (uint v = 0; v < GPU_BATCH_VBO_MAX_LEN; v++) {
    if (batch->verts[v] == verts) {
      return true;
    }
  }
  return false;
}

/* Uniform setters
 * TODO: port this to GPUShader */

void gpu_batch_set_shader(GPUBatch *batch, GPUShader *shader)
{
  batch->shader = shader;
  gpu_shader_bind(batch->shader);
}

/* Drawing / Drawcall functions */
void gpu_batch_draw(GPUBatch *batch)
{
  gpu_shader_bind(batch->shader);
  gpu_batch_draw_advanced(batch, 0, 0, 0, 0);
}

void gpu_batch_draw_range(GPUBatch *batch, int v_first, int v_count)
{
  gpu_shader_bind(batch->shader);
  gpu_batch_draw_advanced(batch, v_first, v_count, 0, 0);
}

void gpu_batch_draw_instanced(GPUBatch *batch, int i_count)
{
  lib_assert(batch->inst[0] == nullptr);

  gpu_shader_bind(batch->shader);
  gpu_batch_draw_advanced(batch, 0, 0, 0, i_count);
}

void gpu_batch_draw_advanced(
    GPUBatch *gpu_batch, int v_first, int v_count, int i_first, int i_count)
{
  gpu_assert(Cxt::get()->shader != nullptr);
  Batch *batch = static_cast<Batch *>(gpu_batch);

  if (v_count == 0) {
    if (batch->elem) {
      v_count = batch->elem_()->index_len_get();
    }
    else {
      v_count = batch->verts_(0)->vertex_len;
    }
  }
  if (i_count == 0) {
    i_count = (batch->inst[0]) ? batch->inst_(0)->vertex_len : 1;
    /* Meh. This is to be able to use different numbers of verts in instance VBO's. */
    if (batch->inst[1] != nullptr) {
      i_count = min_ii(i_count, batch->inst_(1)->vertex_len);
    }
  }

  if (v_count == 0 || i_count == 0) {
    /* Nothing to draw. */
    return;
  }

  batch->draw(v_first, v_count, i_first, i_count);
}

/* Utilities */
void gpu_batch_program_set_builtin_with_config(GPUBatch *batch,
                                               eGPUBuiltinShader shader_id,
                                               eGPUShaderConfig sh_cfg)
{
  GPUShader *shader = gpu_shader_get_builtin_shader_with_config(shader_id, sh_cfg);
  gpu_batch_set_shader(batch, shader);
}

void gpu_batch_program_set_builtin(GPUBatch *batch, eGPUBuiltinShader shader_id)
{
  gpu_batch_program_set_builtin_with_config(batch, shader_id, GPU_SHADER_CFG_DEFAULT);
}

void gpu_batch_program_set_imm_shader(GPUBatch *batch)
{
  gpu_batch_set_shader(batch, immGetShader());
}

/* Init/Exit */
void gpu_batch_init()
{
  gpu_batch_presets_init();
}

void gpu_batch_exit()
{
  gpu_batch_presets_exit();
}
