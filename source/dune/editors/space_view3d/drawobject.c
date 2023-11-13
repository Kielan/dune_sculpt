#include "types_mesh.h"
#include "types_object.h"
#include "types_scene.h"

#include "lib_math.h"

#include "dune_DerivedMesh.h"
#include "dune_editmesh.h"
#include "dune_global.h"
#include "dune_object.h"

#include "graph.h"
#include "graph_query.h"

#include "gpu_batch.h"
#include "gpu_immediate.h"
#include "gpu_shader.h"
#include "gpu_state.h"

#include "ed_mesh.h"

#include "ui_resources.h"

#include "draw_engine.h"

#include "view3d_intern.h" /* bad level include */

#ifdef VIEW3D_CAMERA_BORDER_HACK
uchar view3d_camera_border_hack_col[3];
bool view3d_camera_border_hack_test = false;
#endif

/* BACKBUF SEL (BBS) */
void ed_draw_object_facemap(Graph *graph,
                            Object *ob,
                            const float col[4],
                            const int facemap)
{
  /* happens on undo */
  if (ob->type != OB_MESH || !ob->data) {
    return;
  }

  const Mesh *me = ob->data;
  {
    Object *ob_eval = graph_get_eval_object(graph, ob);
    const Mesh *me_eval = dune_object_get_eval_mesh(ob_eval);
    if (me_eval != NULL) {
      me = me_eval;
    }
  }

  gpu_front_facing(ob->transflag & OB_NEG_SCALE);

  /* Just to create the data to pass to immediate mode! (sigh) */
  const int *facemap_data = CustomData_get_layer(&me->pdata, CD_FACEMAP);
  if (facemap_data) {
    gpu_blend(GPU_BLEND_ALPHA);

    const MVert *mvert = me->mvert;
    const MPoly *mpoly = me->mpoly;
    const MLoop *mloop = me->mloop;

    int mpoly_len = me->totpoly;
    int mloop_len = me->totloop;

    facemap_data = CustomData_get_layer(&me->pdata, CD_FACEMAP);

    /* Make a batch and free it each time for now. */
    const int looptris_len = poly_to_tri_count(mpoly_len, mloop_len);
    const int vbo_len_capacity = looptris_len * 3;
    int vbo_len_used = 0;

    GPUVertFormat format_pos = {0};
    const uint pos_id = gpu_vertformat_attr_add(
        &format_pos, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

    GPUVertBuf *vbo_pos = gpu_vertbuf_create_with_format(&format_pos);
    gpu_vertbuf_data_alloc(vbo_pos, vbo_len_capacity);

    GPUVertBufRaw pos_step;
    gpu_vertbuf_attr_get_raw_data(vbo_pos, pos_id, &pos_step);

    const MPoly *mp;
    int i;
    if (me->runtime.looptris.array) {
      const MLoopTri *mlt = me->runtime.looptris.array;
      for (mp = mpoly, i = 0; i < mpoly_len; i++, mp++) {
        if (facemap_data[i] == facemap) {
          for (int j = 2; j < mp->totloop; j++) {
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[mloop[mlt->tri[0]].v].co);
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[mloop[mlt->tri[1]].v].co);
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[mloop[mlt->tri[2]].v].co);
            vbo_len_used += 3;
            mlt++;
          }
        }
        else {
          mlt += mp->totloop - 2;
        }
      }
    }
    else {
      /* No tessellation data, fan-fill. */
      for (mp = mpoly, i = 0; i < mpoly_len; i++, mp++) {
        if (facemap_data[i] == facemap) {
          const MLoop *ml_start = &mloop[mp->loopstart];
          const MLoop *ml_a = ml_start + 1;
          const MLoop *ml_b = ml_start + 2;
          for (int j = 2; j < mp->totloop; j++) {
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[ml_start->v].co);
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[ml_a->v].co);
            copy_v3_v3(gpu_vertbuf_raw_step(&pos_step), mvert[ml_b->v].co);
            vbo_len_used += 3;

            ml_a++;
            ml_b++;
          }
        }
      }
    }

    if (vbo_len_capacity != vbo_len_used) {
      gpu_vertbuf_data_resize(vbo_pos, vbo_len_used);
    }

    GPUBatch *draw_batch = gpu_batch_create(GPU_PRIM_TRIS, vbo_pos, NULL);
    gpu_batch_program_set_builtin(draw_batch, GPU_SHADER_3D_UNIFORM_COLOR);
    gpu_batch_uniform_4fv(draw_batch, "color", col);
    gpu_batch_draw(draw_batch);
    gpu_batch_discard(draw_batch);
    gpu_vertbuf_discard(vbo_pos);

    gpu_blend(GPU_BLEND_NONE);
  }
}
