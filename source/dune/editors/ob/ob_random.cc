#include "mem_guardedalloc.h"

#include "types_layer.h"
#include "types_ob.h"
#include "types_scene.h"

#include "lib_math_vector.h"
#include "lib_rand.h"

#include "dune_cxt.hh"
#include "dune_layer.h"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_transverts.hh"

#include "ob_intern.h"

/* Generic randomize vertices fns */
static bool ob_rand_transverts(TransVertStore *tvs,
                               const float offset,
                               const float uniform,
                               const float normal_factor,
                               const uint seed)
{
  bool use_normal = (normal_factor != 0.0f);
  RNG *rng;
  TransVert *tv;
  int a;

  if (!tvs || !(tvs->transverts)) {
    return false;
  }

  rng = lib_rng_new(seed);

  tv = tvs->transverts;
  for (a = 0; a < tvs->transverts_tot; a++, tv++) {
    const float t = max_ff(0.0f, uniform + ((1.0f - uniform) * lib_rng_get_float(rng)));
    float vec[3];
    lib_rng_get_float_unit_v3(rng, vec);

    if (use_normal && (tv->flag & TX_VERT_USE_NORMAL)) {
      float no[3];

      /* avoid >90d rotation to align with normal */
      if (dot_v3v3(vec, tv->normal) < 0.0f) {
        negate_v3_v3(no, tv->normal);
      }
      else {
        copy_v3_v3(no, tv->normal);
      }

      interp_v3_v3v3_slerp_safe(vec, vec, no, normal_factor);
    }

    madd_v3_v3fl(tv->loc, vec, offset * t);
  }

  lib_rng_free(rng);

  return true;
}

static int ob_rand_verts_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_active = cxt_data_edit_ob(C);
  const int ob_mode = ob_active->mode;

  const float offset = api_float_get(op->ptr, "offset");
  const float uniform = api_float_get(op->ptr, "uniform");
  const float normal_factor = api_float_get(op->ptr, "normal");
  const uint seed = api_int_get(op->ptr, "seed");

  bool changed_multi = false;
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len, eObMode(ob_mode));
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob_iter = obs[ob_index];

    TransVertStore tvs = {nullptr};

    if (ob_iter) {
      int mode = TM_ALL_JOINTS;

      if (normal_factor != 0.0f) {
        mode |= TX_VERT_USE_NORMAL;
      }

      ed_transverts_create_from_obedit(&tvs, ob_iter, mode);
      if (tvs.transverts_tot == 0) {
        continue;
      }

      int seed_iter = seed;
      /* This gives a consistent result regardless of ob order. */
      if (ob_index) {
        seed_iter += lib_ghashutil_strhash_p(ob_iter->id.name);
      }

      ob_rand_transverts(&tvs, offset, uniform, normal_factor, seed_iter);

      ed_transverts_update_obedit(&tvs, ob_iter);
      ed_transverts_free(&tvs);

      win_ev_add_notifier(C, NC_OB | ND_DRW, ob_iter);
      changed_multi = true;
    }
  }
  mem_free(obs);

  return changed_multi ? OP_FINISHED : OP_CANCELLED;
}

void TRANSFORM_OT_vertex_random(WinOpType *ot)
{
  /* ids */
  ot->name = "Randomize";
  ot->description = "Randomize vertices";
  ot->idname = "TRANSFORM_OT_vertex_random";

  /* api cbs */
  ot->ex = ob_rand_verts_ex;
  ot->poll = ed_transverts_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_float_distance(
      ot->sapi, "offset", 0.0f, -FLT_MAX, FLT_MAX, "Amount", "Distance to offset", -10.0f, 10.0f);
  api_def_float_factor(ot->sapi,
                       "uniform",
                       0.0f,
                       0.0f,
                       1.0f,
                       "Uniform",
                       "Increase for uniform offset distance",
                       0.0f,
                       1.0f);
  api_def_float_factor(ot->sapi,
                       "normal",
                       0.0f,
                       0.0f,
                       1.0f,
                       "Normal",
                       "Align offset direction to normals",
                       0.0f,
                       1.0f);
  api_def_int(
      ot->sapi, "seed", 0, 0, 10000, "Random Seed", "Seed for the random number generator", 0, 50);

  /* Set generic modal cbs. */
  win_op_type_modal_from_ex_for_ob_edit_coords(ot);
}
