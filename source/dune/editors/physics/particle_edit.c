#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BLI_kdtree.h"
#include "BLI_lasso_2d.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_rect.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "BKE_bvhutils.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"

#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_particle.h"
#include "ED_physics.h"
#include "ED_screen.h"
#include "ED_select_utils.h"
#include "ED_view3d.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_state.h"

#include "UI_resources.h"

#include "WM_api.h"
#include "WM_message.h"
#include "WM_toolsystem.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "DEG_depsgraph_query.h"

#include "PIL_time_utildefines.h"

#include "physics_intern.h"

#include "particle_edit_utildefines.h"

/* -------------------------------------------------------------------- */
/** \name Public Utilities
 * \{ */

bool PE_poll(bContext *C)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);

  if (!scene || !ob || !(ob->mode & OB_MODE_PARTICLE_EDIT)) {
    return false;
  }

  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  if (edit == NULL) {
    return false;
  }
  if (edit->psmd_eval == NULL || edit->psmd_eval->mesh_final == NULL) {
    return false;
  }

  return true;
}

bool PE_hair_poll(bContext *C)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);

  if (!scene || !ob || !(ob->mode & OB_MODE_PARTICLE_EDIT)) {
    return false;
  }

  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  if (edit == NULL || edit->psys == NULL) {
    return false;
  }
  if (edit->psmd_eval == NULL || edit->psmd_eval->mesh_final == NULL) {
    return false;
  }

  return true;
}

bool PE_poll_view3d(bContext *C)
{
  ScrArea *area = CTX_wm_area(C);
  ARegion *region = CTX_wm_region(C);

  return (PE_poll(C) && (area && area->spacetype == SPACE_VIEW3D) &&
          (region && region->regiontype == RGN_TYPE_WINDOW));
}

void PE_free_ptcache_edit(PTCacheEdit *edit)
{
  POINT_P;

  if (edit == 0) {
    return;
  }

  if (edit->points) {
    LOOP_POINTS {
      if (point->keys) {
        MEM_freeN(point->keys);
      }
    }

    MEM_freeN(edit->points);
  }

  if (edit->mirror_cache) {
    MEM_freeN(edit->mirror_cache);
  }

  if (edit->emitter_cosnos) {
    MEM_freeN(edit->emitter_cosnos);
    edit->emitter_cosnos = 0;
  }

  if (edit->emitter_field) {
    BLI_kdtree_3d_free(edit->emitter_field);
    edit->emitter_field = 0;
  }

  psys_free_path_cache(edit->psys, edit);

  MEM_freeN(edit);
}

int PE_minmax(
    Depsgraph *depsgraph, Scene *scene, ViewLayer *view_layer, float min[3], float max[3])
{
  Object *ob = OBACT(view_layer);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  ParticleSystem *psys;
  ParticleSystemModifierData *psmd_eval = NULL;
  POINT_P;
  KEY_K;
  float co[3], mat[4][4];
  int ok = 0;

  if (!edit) {
    return ok;
  }

  if ((psys = edit->psys)) {
    psmd_eval = edit->psmd_eval;
  }
  else {
    unit_m4(mat);
  }

  LOOP_VISIBLE_POINTS {
    if (psys) {
      psys_mat_hair_to_global(
          ob, psmd_eval->mesh_final, psys->part->from, psys->particles + p, mat);
    }

    LOOP_SELECTED_KEYS {
      copy_v3_v3(co, key->co);
      mul_m4_v3(mat, co);
      DO_MINMAX(co, min, max);
      ok = 1;
    }
  }

  if (!ok) {
    BKE_object_minmax(ob, min, max, true);
    ok = 1;
  }

  return ok;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Mode Helpers
 * \{ */

int PE_start_edit(PTCacheEdit *edit)
{
  if (edit) {
    edit->edited = 1;
    if (edit->psys) {
      edit->psys->flag |= PSYS_EDITED;
    }
    return 1;
  }

  return 0;
}

ParticleEditSettings *PE_settings(Scene *scene)
{
  return scene->toolsettings ? &scene->toolsettings->particle : NULL;
}

static float pe_brush_size_get(const Scene *UNUSED(scene), ParticleBrushData *brush)
{
#if 0 /* TODO: Here we can enable unified brush size, needs more work. */
  UnifiedPaintSettings *ups = &scene->toolsettings->unified_paint_settings;
  float size = (ups->flag & UNIFIED_PAINT_SIZE) ? ups->size : brush->size;
#endif

  return brush->size;
}

PTCacheEdit *PE_get_current_from_psys(ParticleSystem *psys)
{
  if (psys->part && psys->part->type == PART_HAIR) {
    if ((psys->flag & PSYS_HAIR_DYNAMICS) != 0 && (psys->pointcache->flag & PTCACHE_BAKED) != 0) {
      return psys->pointcache->edit;
    }
    return psys->edit;
  }
  if (psys->pointcache->flag & PTCACHE_BAKED) {
    return psys->pointcache->edit;
  }
  return NULL;
}

/* NOTE: Similar to creation of edit, but only updates pointers in the
 * existing struct.
 */
static void pe_update_hair_particle_edit_pointers(PTCacheEdit *edit)
{
  ParticleSystem *psys = edit->psys;
  ParticleData *pa = psys->particles;
  for (int p = 0; p < edit->totpoint; p++) {
    PTCacheEditPoint *point = &edit->points[p];
    HairKey *hair_key = pa->hair;
    for (int k = 0; k < point->totkey; k++) {
      PTCacheEditKey *key = &point->keys[k];
      key->co = hair_key->co;
      key->time = &hair_key->time;
      key->flag = hair_key->editflag;
      if (!(psys->flag & PSYS_GLOBAL_HAIR)) {
        key->flag |= PEK_USE_WCO;
        hair_key->editflag |= PEK_USE_WCO;
      }
      hair_key++;
    }
    pa++;
  }
}

/* always gets at least the first particlesystem even if PSYS_CURRENT flag is not set
 *
 * NOTE: this function runs on poll, therefore it can runs many times a second
 * keep it fast! */
static PTCacheEdit *pe_get_current(Depsgraph *depsgraph, Scene *scene, Object *ob, bool create)
{
  ParticleEditSettings *pset = PE_settings(scene);
  PTCacheEdit *edit = NULL;
  ListBase pidlist;
  PTCacheID *pid;

  if (pset == NULL || ob == NULL) {
    return NULL;
  }

  pset->scene = scene;
  pset->object = ob;

  BKE_ptcache_ids_from_object(&pidlist, ob, NULL, 0);

  /* in the case of only one editable thing, set pset->edittype accordingly */
  if (BLI_listbase_is_single(&pidlist)) {
    pid = pidlist.first;
    switch (pid->type) {
      case PTCACHE_TYPE_PARTICLES:
        pset->edittype = PE_TYPE_PARTICLES;
        break;
      case PTCACHE_TYPE_SOFTBODY:
        pset->edittype = PE_TYPE_SOFTBODY;
        break;
      case PTCACHE_TYPE_CLOTH:
        pset->edittype = PE_TYPE_CLOTH;
        break;
    }
  }

  for (pid = pidlist.first; pid; pid = pid->next) {
    if (pset->edittype == PE_TYPE_PARTICLES && pid->type == PTCACHE_TYPE_PARTICLES) {
      ParticleSystem *psys = pid->calldata;

      if (psys->flag & PSYS_CURRENT) {
        if (psys->part && psys->part->type == PART_HAIR) {
          if (psys->flag & PSYS_HAIR_DYNAMICS && psys->pointcache->flag & PTCACHE_BAKED) {
            if (create && !psys->pointcache->edit) {
              PE_create_particle_edit(depsgraph, scene, ob, pid->cache, NULL);
            }
            edit = pid->cache->edit;
          }
          else {
            if (create && !psys->edit) {
              if (psys->flag & PSYS_HAIR_DONE) {
                PE_create_particle_edit(depsgraph, scene, ob, NULL, psys);
              }
            }
            edit = psys->edit;
          }
        }
        else {
          if (create && pid->cache->flag & PTCACHE_BAKED && !pid->cache->edit) {
            PE_create_particle_edit(depsgraph, scene, ob, pid->cache, psys);
          }
          edit = pid->cache->edit;
        }

        break;
      }
    }
    else if (pset->edittype == PE_TYPE_SOFTBODY && pid->type == PTCACHE_TYPE_SOFTBODY) {
      if (create && pid->cache->flag & PTCACHE_BAKED && !pid->cache->edit) {
        pset->flag |= PE_FADE_TIME;
        /* Nice to have but doesn't work: `pset->brushtype = PE_BRUSH_COMB;`. */
        PE_create_particle_edit(depsgraph, scene, ob, pid->cache, NULL);
      }
      edit = pid->cache->edit;
      break;
    }
    else if (pset->edittype == PE_TYPE_CLOTH && pid->type == PTCACHE_TYPE_CLOTH) {
      if (create && pid->cache->flag & PTCACHE_BAKED && !pid->cache->edit) {
        pset->flag |= PE_FADE_TIME;
        /* Nice to have but doesn't work: `pset->brushtype = PE_BRUSH_COMB;`. */
        PE_create_particle_edit(depsgraph, scene, ob, pid->cache, NULL);
      }
      edit = pid->cache->edit;
      break;
    }
  }

  /* Don't consider inactive or render dependency graphs, since they might be evaluated for a
   * different number of children. or have different pointer to evaluated particle system or
   * modifier which will also cause troubles. */
  if (edit && DEG_is_active(depsgraph)) {
    edit->pid = *pid;
    if (edit->flags & PT_CACHE_EDIT_UPDATE_PARTICLE_FROM_EVAL) {
      if (edit->psys != NULL && edit->psys_eval != NULL) {
        psys_copy_particles(edit->psys, edit->psys_eval);
        pe_update_hair_particle_edit_pointers(edit);
      }
      edit->flags &= ~PT_CACHE_EDIT_UPDATE_PARTICLE_FROM_EVAL;
    }
  }

  BLI_freelistN(&pidlist);

  return edit;
}

PTCacheEdit *PE_get_current(Depsgraph *depsgraph, Scene *scene, Object *ob)
{
  return pe_get_current(depsgraph, scene, ob, false);
}

PTCacheEdit *PE_create_current(Depsgraph *depsgraph, Scene *scene, Object *ob)
{
  return pe_get_current(depsgraph, scene, ob, true);
}

void PE_current_changed(Depsgraph *depsgraph, Scene *scene, Object *ob)
{
  if (ob->mode == OB_MODE_PARTICLE_EDIT) {
    PE_create_current(depsgraph, scene, ob);
  }
}

void PE_hide_keys_time(Scene *scene, PTCacheEdit *edit, float cfra)
{
  ParticleEditSettings *pset = PE_settings(scene);
  POINT_P;
  KEY_K;

  if (pset->flag & PE_FADE_TIME && pset->selectmode == SCE_SELECT_POINT) {
    LOOP_POINTS {
      LOOP_KEYS {
        if (fabsf(cfra - *key->time) < pset->fade_frames) {
          key->flag &= ~PEK_HIDE;
        }
        else {
          key->flag |= PEK_HIDE;
          // key->flag &= ~PEK_SELECT;
        }
      }
    }
  }
  else {
    LOOP_POINTS {
      LOOP_KEYS {
        key->flag &= ~PEK_HIDE;
      }
    }
  }
}

static int pe_x_mirror(Object *ob)
{
  if (ob->type == OB_MESH) {
    return (((Mesh *)ob->data)->symmetry & ME_SYMMETRY_X);
  }

  return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Common Struct Passed to Callbacks
 * \{ */

typedef struct PEData {
  ViewContext vc;
  ViewDepths *depths;

  const bContext *context;
  Main *bmain;
  Scene *scene;
  ViewLayer *view_layer;
  Object *ob;
  Mesh *mesh;
  PTCacheEdit *edit;
  BVHTreeFromMesh shape_bvh;
  Depsgraph *depsgraph;

  RNG *rng;

  const int *mval;
  const rcti *rect;
  float rad;
  float dval;
  int select;
  eSelectOp sel_op;

  float *dvec;
  float combfac;
  float pufffac;
  float cutfac;
  float smoothfac;
  float weightfac;
  float growfac;
  int totrekey;

  int invert;
  int tot;
  float vec[3];

  int select_action;
  int select_toggle_action;
  bool is_changed;

  void *user_data;
} PEData;

static void PE_set_data(bContext *C, PEData *data)
{
  memset(data, 0, sizeof(*data));

  data->context = C;
  data->bmain = CTX_data_main(C);
  data->scene = CTX_data_scene(C);
  data->view_layer = CTX_data_view_layer(C);
  data->ob = CTX_data_active_object(C);
  data->depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  data->edit = PE_get_current(data->depsgraph, data->scene, data->ob);
}

static void PE_set_view3d_data(bContext *C, PEData *data)
{
  PE_set_data(C, data);

  ED_view3d_viewcontext_init(C, &data->vc, data->depsgraph);

  if (!XRAY_ENABLED(data->vc.v3d)) {
    ED_view3d_depth_override(data->depsgraph,
                             data->vc.region,
                             data->vc.v3d,
                             data->vc.obact,
                             V3D_DEPTH_OBJECT_ONLY,
                             &data->depths);
  }
}

static bool PE_create_shape_tree(PEData *data, Object *shapeob)
{
  Object *shapeob_eval = DEG_get_evaluated_object(data->depsgraph, shapeob);
  const Mesh *mesh = BKE_object_get_evaluated_mesh(shapeob_eval);

  memset(&data->shape_bvh, 0, sizeof(data->shape_bvh));

  if (!mesh) {
    return false;
  }

  return (BKE_bvhtree_from_mesh_get(&data->shape_bvh, mesh, BVHTREE_FROM_LOOPTRI, 4) != NULL);
}

static void PE_free_shape_tree(PEData *data)
{
  free_bvhtree_from_mesh(&data->shape_bvh);
}

static void PE_create_random_generator(PEData *data)
{
  uint rng_seed = (uint)(PIL_check_seconds_timer_i() & UINT_MAX);
  rng_seed ^= POINTER_AS_UINT(data->ob);
  rng_seed ^= POINTER_AS_UINT(data->edit);
  data->rng = BLI_rng_new(rng_seed);
}

static void PE_free_random_generator(PEData *data)
{
  if (data->rng != NULL) {
    BLI_rng_free(data->rng);
    data->rng = NULL;
  }
}

static void PE_data_free(PEData *data)
{
  PE_free_random_generator(data);
  PE_free_shape_tree(data);
  if (data->depths) {
    ED_view3d_depths_free(data->depths);
    data->depths = NULL;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Selection Utilities
 * \{ */

static bool key_test_depth(const PEData *data, const float co[3], const int screen_co[2])
{
  View3D *v3d = data->vc.v3d;
  ViewDepths *vd = data->depths;
  float depth;

  /* nothing to do */
  if (XRAY_ENABLED(v3d)) {
    return true;
  }

  /* used to calculate here but all callers have  the screen_co already, so pass as arg */
#if 0
  if (ED_view3d_project_int_global(data->vc.region,
                                   co,
                                   screen_co,
                                   V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_WIN |
                                       V3D_PROJ_TEST_CLIP_NEAR) != V3D_PROJ_RET_OK) {
    return 0;
  }
#endif

  /* check if screen_co is within bounds because brush_cut uses out of screen coords */
  if (screen_co[0] >= 0 && screen_co[0] < vd->w && screen_co[1] >= 0 && screen_co[1] < vd->h) {
    BLI_assert(vd && vd->depths);
    /* we know its not clipped */
    depth = vd->depths[screen_co[1] * vd->w + screen_co[0]];
  }
  else {
    return 0;
  }

  float win[3];
  ED_view3d_project_v3(data->vc.region, co, win);

  if (win[2] - 0.00001f > depth) {
    return 0;
  }
  return 1;
}

static bool key_inside_circle(const PEData *data, float rad, const float co[3], float *distance)
{
  float dx, dy, dist;
  int screen_co[2];

  /* TODO: should this check V3D_PROJ_TEST_CLIP_BB too? */
  if (ED_view3d_project_int_global(data->vc.region, co, screen_co, V3D_PROJ_TEST_CLIP_WIN) !=
      V3D_PROJ_RET_OK) {
    return 0;
  }

  dx = data->mval[0] - screen_co[0];
  dy = data->mval[1] - screen_co[1];
  dist = sqrtf(dx * dx + dy * dy);

  if (dist > rad) {
    return 0;
  }

  if (key_test_depth(data, co, screen_co)) {
    if (distance) {
      *distance = dist;
    }

    return 1;
  }

  return 0;
}

static bool key_inside_rect(PEData *data, const float co[3])
{
  int screen_co[2];

  if (ED_view3d_project_int_global(data->vc.region, co, screen_co, V3D_PROJ_TEST_CLIP_WIN) !=
      V3D_PROJ_RET_OK) {
    return 0;
  }

  if (screen_co[0] > data->rect->xmin && screen_co[0] < data->rect->xmax &&
      screen_co[1] > data->rect->ymin && screen_co[1] < data->rect->ymax) {
    return key_test_depth(data, co, screen_co);
  }

  return 0;
}

static bool key_inside_test(PEData *data, const float co[3])
{
  if (data->mval) {
    return key_inside_circle(data, data->rad, co, NULL);
  }
  return key_inside_rect(data, co);
}

static bool point_is_selected(PTCacheEditPoint *point)
{
  KEY_K;

  if (point->flag & PEP_HIDE) {
    return 0;
  }

  LOOP_SELECTED_KEYS {
    return 1;
  }

  return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Iterators
 * \{ */

typedef void (*ForPointFunc)(PEData *data, int point_index);
typedef void (*ForHitPointFunc)(PEData *data, int point_index, float mouse_distance);

typedef void (*ForKeyFunc)(PEData *data, int point_index, int key_index, bool is_inside);

typedef void (*ForKeyMatFunc)(PEData *data,
                              const float mat[4][4],
                              const float imat[4][4],
                              int point_index,
                              int key_index,
                              PTCacheEditKey *key);
typedef void (*ForHitKeyMatFunc)(PEData *data,
                                 float mat[4][4],
                                 float imat[4][4],
                                 int point_index,
                                 int key_index,
                                 PTCacheEditKey *key,
                                 float mouse_distance);

enum eParticleSelectFlag {
  PSEL_NEAREST = (1 << 0),
  PSEL_ALL_KEYS = (1 << 1),
};

static void for_mouse_hit_keys(PEData *data, ForKeyFunc func, const enum eParticleSelectFlag flag)
{
  ParticleEditSettings *pset = PE_settings(data->scene);
  PTCacheEdit *edit = data->edit;
  POINT_P;
  KEY_K;
  int nearest_point, nearest_key;
  float dist = data->rad;

  /* in path select mode we have no keys */
  if (pset->selectmode == SCE_SELECT_PATH) {
    return;
  }

  nearest_point = -1;
  nearest_key = -1;

  LOOP_VISIBLE_POINTS {
    if (pset->selectmode == SCE_SELECT_END) {
      if (point->totkey) {
        /* only do end keys */
        key = point->keys + point->totkey - 1;

        if (flag & PSEL_NEAREST) {
          if (key_inside_circle(data, dist, KEY_WCO, &dist)) {
            nearest_point = p;
            nearest_key = point->totkey - 1;
          }
        }
        else {
          const bool is_inside = key_inside_test(data, KEY_WCO);
          if (is_inside || (flag & PSEL_ALL_KEYS)) {
            func(data, p, point->totkey - 1, is_inside);
          }
        }
      }
    }
    else {
      /* do all keys */
      LOOP_VISIBLE_KEYS {
        if (flag & PSEL_NEAREST) {
          if (key_inside_circle(data, dist, KEY_WCO, &dist)) {
            nearest_point = p;
            nearest_key = k;
          }
        }
        else {
          const bool is_inside = key_inside_test(data, KEY_WCO);
          if (is_inside || (flag & PSEL_ALL_KEYS)) {
            func(data, p, k, is_inside);
          }
        }
      }
    }
  }

  /* do nearest only */
  if (flag & PSEL_NEAREST) {
    if (nearest_point != -1) {
      func(data, nearest_point, nearest_key, true);
    }
  }
}

static void foreach_mouse_hit_point(PEData *data, ForHitPointFunc func, int selected)
{
  ParticleEditSettings *pset = PE_settings(data->scene);
  PTCacheEdit *edit = data->edit;
  POINT_P;
  KEY_K;

  /* all is selected in path mode */
  if (pset->selectmode == SCE_SELECT_PATH) {
    selected = 0;
  }

  LOOP_VISIBLE_POINTS {
    if (pset->selectmode == SCE_SELECT_END) {
      if (point->totkey) {
        /* only do end keys */
        key = point->keys + point->totkey - 1;

        if (selected == 0 || key->flag & PEK_SELECT) {
          float mouse_distance;
          if (key_inside_circle(data, data->rad, KEY_WCO, &mouse_distance)) {
            func(data, p, mouse_distance);
          }
        }
      }
    }
    else {
      /* do all keys */
      LOOP_VISIBLE_KEYS {
        if (selected == 0 || key->flag & PEK_SELECT) {
          float mouse_distance;
          if (key_inside_circle(data, data->rad, KEY_WCO, &mouse_distance)) {
            func(data, p, mouse_distance);
            break;
          }
        }
      }
    }
  }
}

typedef struct KeyIterData {
  PEData *data;
  PTCacheEdit *edit;
  int selected;
  ForHitKeyMatFunc func;
} KeyIterData;

static void foreach_mouse_hit_key_iter(void *__restrict iter_data_v,
                                       const int iter,
                                       const TaskParallelTLS *__restrict UNUSED(tls))
{
  KeyIterData *iter_data = (KeyIterData *)iter_data_v;
  PEData *data = iter_data->data;
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = &edit->points[iter];
  if (point->flag & PEP_HIDE) {
    return;
  }
  ParticleSystem *psys = edit->psys;
  ParticleSystemModifierData *psmd_eval = iter_data->edit->psmd_eval;
  ParticleEditSettings *pset = PE_settings(data->scene);
  const int selected = iter_data->selected;
  float mat[4][4], imat[4][4];
  unit_m4(mat);
  unit_m4(imat);
  if (pset->selectmode == SCE_SELECT_END) {
    if (point->totkey) {
      /* only do end keys */
      PTCacheEditKey *key = point->keys + point->totkey - 1;

      if (selected == 0 || key->flag & PEK_SELECT) {
        float mouse_distance;
        if (key_inside_circle(data, data->rad, KEY_WCO, &mouse_distance)) {
          if (edit->psys && !(edit->psys->flag & PSYS_GLOBAL_HAIR)) {
            psys_mat_hair_to_global(
                data->ob, psmd_eval->mesh_final, psys->part->from, psys->particles + iter, mat);
            invert_m4_m4(imat, mat);
          }
          iter_data->func(data, mat, imat, iter, point->totkey - 1, key, mouse_distance);
        }
      }
    }
  }
  else {
    /* do all keys */
    PTCacheEditKey *key;
    int k;
    LOOP_VISIBLE_KEYS {
      if (selected == 0 || key->flag & PEK_SELECT) {
        float mouse_distance;
        if (key_inside_circle(data, data->rad, KEY_WCO, &mouse_distance)) {
          if (edit->psys && !(edit->psys->flag & PSYS_GLOBAL_HAIR)) {
            psys_mat_hair_to_global(
                data->ob, psmd_eval->mesh_final, psys->part->from, psys->particles + iter, mat);
            invert_m4_m4(imat, mat);
          }
          iter_data->func(data, mat, imat, iter, k, key, mouse_distance);
        }
      }
    }
  }
}

static void foreach_mouse_hit_key(PEData *data, ForHitKeyMatFunc func, int selected)
{
  PTCacheEdit *edit = data->edit;
  ParticleEditSettings *pset = PE_settings(data->scene);
  /* all is selected in path mode */
  if (pset->selectmode == SCE_SELECT_PATH) {
    selected = 0;
  }

  KeyIterData iter_data;
  iter_data.data = data;
  iter_data.edit = edit;
  iter_data.selected = selected;
  iter_data.func = func;

  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);
  BLI_task_parallel_range(0, edit->totpoint, &iter_data, foreach_mouse_hit_key_iter, &settings);
}

static void foreach_selected_point(PEData *data, ForPointFunc func)
{
  PTCacheEdit *edit = data->edit;
  POINT_P;

  LOOP_SELECTED_POINTS {
    func(data, p);
  }
}
