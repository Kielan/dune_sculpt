#include "types_mod.h"
#include "types_particle.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_layer.h"
#include "dune_particle.h"
#include "dune_pointcache.h"

#include "ed_particle.hh"

#include "transform.hh"
#include "transform_snap.hh"

/* Own include. */
#include "transform_convert.hh"

/* Particle Edit Transform Creation */
static void createTransParticleVerts(Cxt * /*C*/, TransInfo *t)
{
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {

    TransData *td = nullptr;
    TransDataExtension *tx;
    dune_view_layer_synced_ensure(t->scene, t->view_layer);
    Ob *ob = dune_view_layer_active_ob_get(t->view_layer);
    ParticleEditSettings *pset = PE_settings(t->scene);
    PTCacheEdit *edit = PE_get_current(t->graph, t->scene, ob);
    ParticleSystem *psys = nullptr;
    PTCacheEditPoint *point;
    PTCacheEditKey *key;
    float mat[4][4];
    int i, k, transformparticle;
    int count = 0, hassel = 0;
    const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;

    if (edit == nullptr || t->settings->particle.selmode == SCE_SEL_PATH) {
      return;
    }

    psys = edit->psys;

    for (i = 0, point = edit->points; i < edit->totpoint; i++, point++) {
      point->flag &= ~PEP_TRANSFORM;
      transformparticle = 0;

      if ((point->flag & PEP_HIDE) == 0) {
        for (k = 0, key = point->keys; k < point->totkey; k++, key++) {
          if ((key->flag & PEK_HIDE) == 0) {
            if (key->flag & PEK_SEL) {
              hassel = 1;
              transformparticle = 1;
            }
            else if (is_prop_edit) {
              transformparticle = 1;
            }
          }
        }
      }

      if (transformparticle) {
        count += point->totkey;
        point->flag |= PEP_TRANSFORM;
      }
    }

    /* In prop mode we need at least 1 sel. */
    if (hasselected == 0) {
      return;
    }

    tc->data_len = count;
    td = tc->data = static_cast<TransData *>(
        mem_calloc(tc->data_len * sizeof(TransData), "TransObData(Particle Mode)"));

    if (t->mode == TFM_BAKE_TIME) {
      tx = tc->data_ext = static_cast<TransDataExtension *>(
          mem_calloc(tc->data_len * sizeof(TransDataExtension), "Particle_TransExtension"));
    }
    else {
      tx = tc->data_ext = nullptr;
    }

    unit_m4(mat);

    invert_m4_m4(ob->world_to_ob, ob->ob_to_world);

    for (i = 0, point = edit->points; i < edit->totpoint; i++, point++) {
      TransData *head, *tail;
      head = tail = td;

      if (!(point->flag & PEP_TRANSFORM)) {
        continue;
      }

      if (psys && !(psys->flag & PSYS_GLOBAL_HAIR)) {
        ParticleSystemModData *psmd_eval = edit->psmd_eval;
        psys_mat_hair_to_global(
            ob, psmd_eval->mesh_final, psys->part->from, psys->particles + i, mat);
      }

      for (k = 0, key = point->keys; k < point->totkey; k++, key++) {
        if (key->flag & PEK_USE_WCO) {
          copy_v3_v3(key->world_co, key->co);
          mul_m4_v3(mat, key->world_co);
          td->loc = key->world_co;
        }
        else {
          td->loc = key->co;
        }

        copy_v3_v3(td->iloc, td->loc);
        copy_v3_v3(td->center, td->loc);

        if (key->flag & PEK_SEL) {
          td->flag |= TD_SELECTED;
        }
        else if (!is_prop_edit) {
          td->flag |= TD_SKIP;
        }

        unit_m3(td->mtx);
        unit_m3(td->smtx);

        /* don't allow moving roots */
        if (k == 0 && pset->flag & PE_LOCK_FIRST && (!psys || !(psys->flag & PSYS_GLOBAL_HAIR))) {
          td->protectflag |= OB_LOCK_LOC;
        }

        td->ob = ob;
        td->ext = tx;
        if (t->mode == TFM_BAKE_TIME) {
          td->val = key->time;
          td->ival = *(key->time);
          /* abuse size and quat for min/max vals */
          td->flag |= TD_NO_EXT;
          if (k == 0) {
            tx->size = nullptr;
          }
          else {
            tx->size = (key - 1)->time;
          }

          if (k == point->totkey - 1) {
            tx->quat = nullptr;
          }
          else {
            tx->quat = (key + 1)->time;
          }
        }

        td++;
        if (tx) {
          tx++;
        }
        tail++;
      }
      if (is_prop_edit && head != tail) {
        calc_distanceCurveVerts(head, tail - 1, false);
      }
    }
  }
}

/* Node Transform Creation */
static void flushTransParticles(TransInfo *t)
{
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    Scene *scene = t->scene;
    ViewLayer *view_layer = t->view_layer;
    dune_view_layer_synced_ensure(scene, view_layer);
    Ob *ob = dune_view_layer_active_ob_get(view_layer);
    PTCacheEdit *edit = PE_get_current(t->graph, scene, ob);
    ParticleSystem *psys = edit->psys;
    PTCacheEditPoint *point;
    PTCacheEditKey *key;
    TransData *td;
    float mat[4][4], imat[4][4], co[3];
    int i, k;
    const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;

    /* we do transform in world space, so flush world space position
     * back to particle local space (only for hair particles) */
    td = tc->data;
    for (i = 0, point = edit->points; i < edit->totpoint; i++, point++, td++) {
      if (!(point->flag & PEP_TRANSFORM)) {
        continue;
      }

      if (psys && !(psys->flag & PSYS_GLOBAL_HAIR)) {
        ParticleSystemModData *psmd_eval = edit->psmd_eval;
        psys_mat_hair_to_global(
            ob, psmd_eval->mesh_final, psys->part->from, psys->particles + i, mat);
        invert_m4_m4(imat, mat);

        for (k = 0, key = point->keys; k < point->totkey; k++, key++) {
          copy_v3_v3(co, key->world_co);
          mul_m4_v3(imat, co);

          /* optimization for proportional edit */
          if (!is_prop_edit || !compare_v3v3(key->co, co, 0.0001f)) {
            copy_v3_v3(key->co, co);
            point->flag |= PEP_EDIT_RECALC;
          }
        }
      }
      else {
        point->flag |= PEP_EDIT_RECALC;
      }
    }

    PE_update_ob(t->graph, scene, ob, 1);
    dune_particle_batch_cache_dirty_tag(psys, DUNE_PARTICLE_BATCH_DIRTY_ALL);
    graph_id_tag_update(&ob->id, ID_RECALC_PSYS_REDO);
  }
}

/* Recalc Transform Particles Data */
static void recalcData_particles(TransInfo *t)
{
  if (t->state != TRANS_CANCEL) {
    transform_snap_project_individual_apply(t);
  }
  flushTransParticles(t);
}

TransConvertTypeInfo TransConvertType_Particle = {
    /*flags*/ T_POINTS,
    /*create_trans_data*/ createTransParticleVerts,
    /*recalc_data*/ recalcData_particles,
    /*special_aftertrans_update*/ nullptr,
};
