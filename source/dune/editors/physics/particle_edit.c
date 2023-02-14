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

static void foreach_selected_key(PEData *data, ForKeyFunc func)
{
  PTCacheEdit *edit = data->edit;
  POINT_P;
  KEY_K;

  LOOP_VISIBLE_POINTS {
    LOOP_SELECTED_KEYS {
      func(data, p, k, true);
    }
  }
}

static void foreach_point(PEData *data, ForPointFunc func)
{
  PTCacheEdit *edit = data->edit;
  POINT_P;

  LOOP_POINTS {
    func(data, p);
  }
}

static int count_selected_keys(Scene *scene, PTCacheEdit *edit)
{
  ParticleEditSettings *pset = PE_settings(scene);
  POINT_P;
  KEY_K;
  int sel = 0;

  LOOP_VISIBLE_POINTS {
    if (pset->selectmode == SCE_SELECT_POINT) {
      LOOP_SELECTED_KEYS {
        sel++;
      }
    }
    else if (pset->selectmode == SCE_SELECT_END) {
      if (point->totkey) {
        key = point->keys + point->totkey - 1;
        if (key->flag & PEK_SELECT) {
          sel++;
        }
      }
    }
  }

  return sel;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Particle Edit Mirroring
 * \{ */

static void PE_update_mirror_cache(Object *ob, ParticleSystem *psys)
{
  PTCacheEdit *edit;
  ParticleSystemModifierData *psmd_eval;
  KDTree_3d *tree;
  KDTreeNearest_3d nearest;
  HairKey *key;
  PARTICLE_P;
  float mat[4][4], co[3];
  int index, totpart;

  edit = psys->edit;
  psmd_eval = edit->psmd_eval;
  totpart = psys->totpart;

  if (!psmd_eval->mesh_final) {
    return;
  }

  tree = BLI_kdtree_3d_new(totpart);

  /* Insert particles into KD-tree. */
  LOOP_PARTICLES
  {
    key = pa->hair;
    psys_mat_hair_to_orco(ob, psmd_eval->mesh_final, psys->part->from, pa, mat);
    copy_v3_v3(co, key->co);
    mul_m4_v3(mat, co);
    BLI_kdtree_3d_insert(tree, p, co);
  }

  BLI_kdtree_3d_balance(tree);

  /* lookup particles and set in mirror cache */
  if (!edit->mirror_cache) {
    edit->mirror_cache = MEM_callocN(sizeof(int) * totpart, "PE mirror cache");
  }

  LOOP_PARTICLES
  {
    key = pa->hair;
    psys_mat_hair_to_orco(ob, psmd_eval->mesh_final, psys->part->from, pa, mat);
    copy_v3_v3(co, key->co);
    mul_m4_v3(mat, co);
    co[0] = -co[0];

    index = BLI_kdtree_3d_find_nearest(tree, co, &nearest);

    /* this needs a custom threshold still, duplicated for editmode mirror */
    if (index != -1 && index != p && (nearest.dist <= 0.0002f)) {
      edit->mirror_cache[p] = index;
    }
    else {
      edit->mirror_cache[p] = -1;
    }
  }

  /* make sure mirrors are in two directions */
  LOOP_PARTICLES
  {
    if (edit->mirror_cache[p]) {
      index = edit->mirror_cache[p];
      if (edit->mirror_cache[index] != p) {
        edit->mirror_cache[p] = -1;
      }
    }
  }

  BLI_kdtree_3d_free(tree);
}

static void PE_mirror_particle(
    Object *ob, Mesh *mesh, ParticleSystem *psys, ParticleData *pa, ParticleData *mpa)
{
  HairKey *hkey, *mhkey;
  PTCacheEditPoint *point, *mpoint;
  PTCacheEditKey *key, *mkey;
  PTCacheEdit *edit;
  float mat[4][4], mmat[4][4], immat[4][4];
  int i, mi, k;

  edit = psys->edit;
  i = pa - psys->particles;

  /* find mirrored particle if needed */
  if (!mpa) {
    if (!edit->mirror_cache) {
      PE_update_mirror_cache(ob, psys);
    }

    if (!edit->mirror_cache) {
      return; /* something went wrong! */
    }

    mi = edit->mirror_cache[i];
    if (mi == -1) {
      return;
    }
    mpa = psys->particles + mi;
  }
  else {
    mi = mpa - psys->particles;
  }

  point = edit->points + i;
  mpoint = edit->points + mi;

  /* make sure they have the same amount of keys */
  if (pa->totkey != mpa->totkey) {
    if (mpa->hair) {
      MEM_freeN(mpa->hair);
    }
    if (mpoint->keys) {
      MEM_freeN(mpoint->keys);
    }

    mpa->hair = MEM_dupallocN(pa->hair);
    mpa->totkey = pa->totkey;
    mpoint->keys = MEM_dupallocN(point->keys);
    mpoint->totkey = point->totkey;

    mhkey = mpa->hair;
    mkey = mpoint->keys;
    for (k = 0; k < mpa->totkey; k++, mkey++, mhkey++) {
      mkey->co = mhkey->co;
      mkey->time = &mhkey->time;
      mkey->flag &= ~PEK_SELECT;
    }
  }

  /* mirror positions and tags */
  psys_mat_hair_to_orco(ob, mesh, psys->part->from, pa, mat);
  psys_mat_hair_to_orco(ob, mesh, psys->part->from, mpa, mmat);
  invert_m4_m4(immat, mmat);

  hkey = pa->hair;
  mhkey = mpa->hair;
  key = point->keys;
  mkey = mpoint->keys;
  for (k = 0; k < pa->totkey; k++, hkey++, mhkey++, key++, mkey++) {
    copy_v3_v3(mhkey->co, hkey->co);
    mul_m4_v3(mat, mhkey->co);
    mhkey->co[0] = -mhkey->co[0];
    mul_m4_v3(immat, mhkey->co);

    if (key->flag & PEK_TAG) {
      mkey->flag |= PEK_TAG;
    }

    mkey->length = key->length;
  }

  if (point->flag & PEP_TAG) {
    mpoint->flag |= PEP_TAG;
  }
  if (point->flag & PEP_EDIT_RECALC) {
    mpoint->flag |= PEP_EDIT_RECALC;
  }
}

static void PE_apply_mirror(Object *ob, ParticleSystem *psys)
{
  PTCacheEdit *edit;
  ParticleSystemModifierData *psmd_eval;
  POINT_P;

  if (!psys) {
    return;
  }

  edit = psys->edit;
  psmd_eval = edit->psmd_eval;

  if (psmd_eval == NULL || psmd_eval->mesh_final == NULL) {
    return;
  }

  if (!edit->mirror_cache) {
    PE_update_mirror_cache(ob, psys);
  }

  if (!edit->mirror_cache) {
    return; /* something went wrong */
  }

  /* we delay settings the PARS_EDIT_RECALC for mirrored particles
   * to avoid doing mirror twice */
  LOOP_POINTS {
    if (point->flag & PEP_EDIT_RECALC) {
      PE_mirror_particle(ob, psmd_eval->mesh_final, psys, psys->particles + p, NULL);

      if (edit->mirror_cache[p] != -1) {
        edit->points[edit->mirror_cache[p]].flag &= ~PEP_EDIT_RECALC;
      }
    }
  }

  LOOP_POINTS {
    if (point->flag & PEP_EDIT_RECALC) {
      if (edit->mirror_cache[p] != -1) {
        edit->points[edit->mirror_cache[p]].flag |= PEP_EDIT_RECALC;
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Calculation
 * \{ */

typedef struct DeflectEmitterIter {
  Object *object;
  ParticleSystem *psys;
  PTCacheEdit *edit;
  float dist;
  float emitterdist;
} DeflectEmitterIter;

static void deflect_emitter_iter(void *__restrict iter_data_v,
                                 const int iter,
                                 const TaskParallelTLS *__restrict UNUSED(tls))
{
  DeflectEmitterIter *iter_data = (DeflectEmitterIter *)iter_data_v;
  PTCacheEdit *edit = iter_data->edit;
  PTCacheEditPoint *point = &edit->points[iter];
  if ((point->flag & PEP_EDIT_RECALC) == 0) {
    return;
  }
  Object *object = iter_data->object;
  ParticleSystem *psys = iter_data->psys;
  ParticleSystemModifierData *psmd_eval = iter_data->edit->psmd_eval;
  PTCacheEditKey *key;
  int k;
  float hairimat[4][4], hairmat[4][4];
  int index;
  float *vec, *nor, dvec[3], dot, dist_1st = 0.0f;
  const float dist = iter_data->dist;
  const float emitterdist = iter_data->emitterdist;
  psys_mat_hair_to_object(
      object, psmd_eval->mesh_final, psys->part->from, psys->particles + iter, hairmat);

  LOOP_KEYS {
    mul_m4_v3(hairmat, key->co);
  }

  LOOP_KEYS {
    if (k == 0) {
      dist_1st = len_v3v3((key + 1)->co, key->co);
      dist_1st *= dist * emitterdist;
    }
    else {
      index = BLI_kdtree_3d_find_nearest(edit->emitter_field, key->co, NULL);

      vec = edit->emitter_cosnos + index * 6;
      nor = vec + 3;

      sub_v3_v3v3(dvec, key->co, vec);

      dot = dot_v3v3(dvec, nor);
      copy_v3_v3(dvec, nor);

      if (dot > 0.0f) {
        if (dot < dist_1st) {
          normalize_v3(dvec);
          mul_v3_fl(dvec, dist_1st - dot);
          add_v3_v3(key->co, dvec);
        }
      }
      else {
        normalize_v3(dvec);
        mul_v3_fl(dvec, dist_1st - dot);
        add_v3_v3(key->co, dvec);
      }
      if (k == 1) {
        dist_1st *= 1.3333f;
      }
    }
  }

  invert_m4_m4(hairimat, hairmat);

  LOOP_KEYS {
    mul_m4_v3(hairimat, key->co);
  }
}

/* tries to stop edited particles from going through the emitter's surface */
static void pe_deflect_emitter(Scene *scene, Object *ob, PTCacheEdit *edit)
{
  ParticleEditSettings *pset = PE_settings(scene);
  ParticleSystem *psys;
  const float dist = ED_view3d_select_dist_px() * 0.01f;

  if (edit == NULL || edit->psys == NULL || (pset->flag & PE_DEFLECT_EMITTER) == 0 ||
      (edit->psys->flag & PSYS_GLOBAL_HAIR)) {
    return;
  }

  psys = edit->psys;

  if (edit->psmd_eval == NULL || edit->psmd_eval->mesh_final == NULL) {
    return;
  }

  DeflectEmitterIter iter_data;
  iter_data.object = ob;
  iter_data.psys = psys;
  iter_data.edit = edit;
  iter_data.dist = dist;
  iter_data.emitterdist = pset->emitterdist;

  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);
  BLI_task_parallel_range(0, edit->totpoint, &iter_data, deflect_emitter_iter, &settings);
}

typedef struct ApplyLengthsIterData {
  PTCacheEdit *edit;
} ApplyLengthsIterData;

static void apply_lengths_iter(void *__restrict iter_data_v,
                               const int iter,
                               const TaskParallelTLS *__restrict UNUSED(tls))
{
  ApplyLengthsIterData *iter_data = (ApplyLengthsIterData *)iter_data_v;
  PTCacheEdit *edit = iter_data->edit;
  PTCacheEditPoint *point = &edit->points[iter];
  if ((point->flag & PEP_EDIT_RECALC) == 0) {
    return;
  }
  PTCacheEditKey *key;
  int k;
  LOOP_KEYS {
    if (k) {
      float dv1[3];
      sub_v3_v3v3(dv1, key->co, (key - 1)->co);
      normalize_v3(dv1);
      mul_v3_fl(dv1, (key - 1)->length);
      add_v3_v3v3(key->co, (key - 1)->co, dv1);
    }
  }
}

/* force set distances between neighboring keys */
static void PE_apply_lengths(Scene *scene, PTCacheEdit *edit)
{
  ParticleEditSettings *pset = PE_settings(scene);

  if (edit == 0 || (pset->flag & PE_KEEP_LENGTHS) == 0) {
    return;
  }

  if (edit->psys && edit->psys->flag & PSYS_GLOBAL_HAIR) {
    return;
  }

  ApplyLengthsIterData iter_data;
  iter_data.edit = edit;

  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);
  BLI_task_parallel_range(0, edit->totpoint, &iter_data, apply_lengths_iter, &settings);
}

typedef struct IterateLengthsIterData {
  PTCacheEdit *edit;
  ParticleEditSettings *pset;
} IterateLengthsIterData;

static void iterate_lengths_iter(void *__restrict iter_data_v,
                                 const int iter,
                                 const TaskParallelTLS *__restrict UNUSED(tls))
{
  IterateLengthsIterData *iter_data = (IterateLengthsIterData *)iter_data_v;
  PTCacheEdit *edit = iter_data->edit;
  PTCacheEditPoint *point = &edit->points[iter];
  if ((point->flag & PEP_EDIT_RECALC) == 0) {
    return;
  }
  ParticleEditSettings *pset = iter_data->pset;
  float tlen;
  float dv0[3] = {0.0f, 0.0f, 0.0f};
  float dv1[3] = {0.0f, 0.0f, 0.0f};
  float dv2[3] = {0.0f, 0.0f, 0.0f};
  for (int j = 1; j < point->totkey; j++) {
    PTCacheEditKey *key;
    int k;
    float mul = 1.0f / (float)point->totkey;
    if (pset->flag & PE_LOCK_FIRST) {
      key = point->keys + 1;
      k = 1;
      dv1[0] = dv1[1] = dv1[2] = 0.0;
    }
    else {
      key = point->keys;
      k = 0;
      dv0[0] = dv0[1] = dv0[2] = 0.0;
    }

    for (; k < point->totkey; k++, key++) {
      if (k) {
        sub_v3_v3v3(dv0, (key - 1)->co, key->co);
        tlen = normalize_v3(dv0);
        mul_v3_fl(dv0, (mul * (tlen - (key - 1)->length)));
      }
      if (k < point->totkey - 1) {
        sub_v3_v3v3(dv2, (key + 1)->co, key->co);
        tlen = normalize_v3(dv2);
        mul_v3_fl(dv2, mul * (tlen - key->length));
      }
      if (k) {
        add_v3_v3((key - 1)->co, dv1);
      }
      add_v3_v3v3(dv1, dv0, dv2);
    }
  }
}

/* try to find a nice solution to keep distances between neighboring keys */
static void pe_iterate_lengths(Scene *scene, PTCacheEdit *edit)
{
  ParticleEditSettings *pset = PE_settings(scene);
  if (edit == 0 || (pset->flag & PE_KEEP_LENGTHS) == 0) {
    return;
  }
  if (edit->psys && edit->psys->flag & PSYS_GLOBAL_HAIR) {
    return;
  }

  IterateLengthsIterData iter_data;
  iter_data.edit = edit;
  iter_data.pset = pset;

  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);
  BLI_task_parallel_range(0, edit->totpoint, &iter_data, iterate_lengths_iter, &settings);
}

void recalc_lengths(PTCacheEdit *edit)
{
  POINT_P;
  KEY_K;

  if (edit == 0) {
    return;
  }

  LOOP_EDITED_POINTS {
    key = point->keys;
    for (k = 0; k < point->totkey - 1; k++, key++) {
      key->length = len_v3v3(key->co, (key + 1)->co);
    }
  }
}

void recalc_emitter_field(Depsgraph *UNUSED(depsgraph), Object *UNUSED(ob), ParticleSystem *psys)
{
  PTCacheEdit *edit = psys->edit;
  Mesh *mesh = edit->psmd_eval->mesh_final;
  float *vec, *nor;
  int i, totface;

  if (!mesh) {
    return;
  }

  if (edit->emitter_cosnos) {
    MEM_freeN(edit->emitter_cosnos);
  }

  BLI_kdtree_3d_free(edit->emitter_field);

  totface = mesh->totface;
  // int totvert = dm->getNumVerts(dm); /* UNUSED */

  edit->emitter_cosnos = MEM_callocN(sizeof(float[6]) * totface, "emitter cosnos");

  edit->emitter_field = BLI_kdtree_3d_new(totface);

  vec = edit->emitter_cosnos;
  nor = vec + 3;

  const float(*vert_normals)[3] = BKE_mesh_vertex_normals_ensure(mesh);

  for (i = 0; i < totface; i++, vec += 6, nor += 6) {
    MFace *mface = &mesh->mface[i];
    MVert *mvert;

    mvert = &mesh->mvert[mface->v1];
    copy_v3_v3(vec, mvert->co);
    copy_v3_v3(nor, vert_normals[mface->v1]);

    mvert = &mesh->mvert[mface->v2];
    add_v3_v3v3(vec, vec, mvert->co);
    add_v3_v3(nor, vert_normals[mface->v2]);

    mvert = &mesh->mvert[mface->v3];
    add_v3_v3v3(vec, vec, mvert->co);
    add_v3_v3(nor, vert_normals[mface->v3]);

    if (mface->v4) {
      mvert = &mesh->mvert[mface->v4];
      add_v3_v3v3(vec, vec, mvert->co);
      add_v3_v3(nor, vert_normals[mface->v4]);

      mul_v3_fl(vec, 0.25);
    }
    else {
      mul_v3_fl(vec, 1.0f / 3.0f);
    }

    normalize_v3(nor);

    BLI_kdtree_3d_insert(edit->emitter_field, i, vec);
  }

  BLI_kdtree_3d_balance(edit->emitter_field);
}

static void PE_update_selection(Depsgraph *depsgraph, Scene *scene, Object *ob, int useflag)
{
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  HairKey *hkey;
  POINT_P;
  KEY_K;

  /* flag all particles to be updated if not using flag */
  if (!useflag) {
    LOOP_POINTS {
      point->flag |= PEP_EDIT_RECALC;
    }
  }

  /* flush edit key flag to hair key flag to preserve selection
   * on save */
  if (edit->psys) {
    LOOP_POINTS {
      hkey = edit->psys->particles[p].hair;
      LOOP_KEYS {
        hkey->editflag = key->flag;
        hkey++;
      }
    }
  }

  psys_cache_edit_paths(depsgraph, scene, ob, edit, CFRA, G.is_rendering);

  /* disable update flag */
  LOOP_POINTS {
    point->flag &= ~PEP_EDIT_RECALC;
  }

  DEG_id_tag_update(&ob->id, ID_RECALC_SELECT);
}

void update_world_cos(Object *ob, PTCacheEdit *edit)
{
  ParticleSystem *psys = edit->psys;
  ParticleSystemModifierData *psmd_eval = edit->psmd_eval;
  POINT_P;
  KEY_K;
  float hairmat[4][4];

  if (psys == 0 || psys->edit == 0 || psmd_eval == NULL || psmd_eval->mesh_final == NULL) {
    return;
  }

  LOOP_POINTS {
    if (!(psys->flag & PSYS_GLOBAL_HAIR)) {
      psys_mat_hair_to_global(
          ob, psmd_eval->mesh_final, psys->part->from, psys->particles + p, hairmat);
    }

    LOOP_KEYS {
      copy_v3_v3(key->world_co, key->co);
      if (!(psys->flag & PSYS_GLOBAL_HAIR)) {
        mul_m4_v3(hairmat, key->world_co);
      }
    }
  }
}
static void update_velocities(PTCacheEdit *edit)
{
  /* TODO: get frs_sec properly. */
  float vec1[3], vec2[3], frs_sec, dfra;
  POINT_P;
  KEY_K;

  /* hair doesn't use velocities */
  if (edit->psys || !edit->points || !edit->points->keys->vel) {
    return;
  }

  frs_sec = edit->pid.flag & PTCACHE_VEL_PER_SEC ? 25.0f : 1.0f;

  LOOP_EDITED_POINTS {
    LOOP_KEYS {
      if (k == 0) {
        dfra = *(key + 1)->time - *key->time;

        if (dfra <= 0.0f) {
          continue;
        }

        sub_v3_v3v3(key->vel, (key + 1)->co, key->co);

        if (point->totkey > 2) {
          sub_v3_v3v3(vec1, (key + 1)->co, (key + 2)->co);
          project_v3_v3v3(vec2, vec1, key->vel);
          sub_v3_v3v3(vec2, vec1, vec2);
          madd_v3_v3fl(key->vel, vec2, 0.5f);
        }
      }
      else if (k == point->totkey - 1) {
        dfra = *key->time - *(key - 1)->time;

        if (dfra <= 0.0f) {
          continue;
        }

        sub_v3_v3v3(key->vel, key->co, (key - 1)->co);

        if (point->totkey > 2) {
          sub_v3_v3v3(vec1, (key - 2)->co, (key - 1)->co);
          project_v3_v3v3(vec2, vec1, key->vel);
          sub_v3_v3v3(vec2, vec1, vec2);
          madd_v3_v3fl(key->vel, vec2, 0.5f);
        }
      }
      else {
        dfra = *(key + 1)->time - *(key - 1)->time;

        if (dfra <= 0.0f) {
          continue;
        }

        sub_v3_v3v3(key->vel, (key + 1)->co, (key - 1)->co);
      }
      mul_v3_fl(key->vel, frs_sec / dfra);
    }
  }
}

void PE_update_object(Depsgraph *depsgraph, Scene *scene, Object *ob, int useflag)
{
  /* use this to do partial particle updates, not usable when adding or
   * removing, then a full redo is necessary and calling this may crash */
  ParticleEditSettings *pset = PE_settings(scene);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  POINT_P;

  if (!edit) {
    return;
  }

  /* flag all particles to be updated if not using flag */
  if (!useflag) {
    LOOP_POINTS {
      point->flag |= PEP_EDIT_RECALC;
    }
  }

  /* do post process on particle edit keys */
  pe_iterate_lengths(scene, edit);
  pe_deflect_emitter(scene, ob, edit);
  PE_apply_lengths(scene, edit);
  if (pe_x_mirror(ob)) {
    PE_apply_mirror(ob, edit->psys);
  }
  if (edit->psys) {
    update_world_cos(ob, edit);
  }
  if (pset->flag & PE_AUTO_VELOCITY) {
    update_velocities(edit);
  }

  /* Only do this for emitter particles because drawing PE_FADE_TIME is not respected in 2.8 yet
   * and flagging with PEK_HIDE will prevent selection. This might get restored once this is
   * supported in drawing (but doesn't make much sense for hair anyways). */
  if (edit->psys && edit->psys->part->type == PART_EMITTER) {
    PE_hide_keys_time(scene, edit, CFRA);
  }

  /* regenerate path caches */
  psys_cache_edit_paths(depsgraph, scene, ob, edit, CFRA, G.is_rendering);

  /* disable update flag */
  LOOP_POINTS {
    point->flag &= ~PEP_EDIT_RECALC;
  }

  if (edit->psys) {
    edit->psys->flag &= ~PSYS_HAIR_UPDATED;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Selections
 * \{ */

/*-----selection callbacks-----*/

static void select_key(PEData *data, int point_index, int key_index, bool UNUSED(is_inside))
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  PTCacheEditKey *key = point->keys + key_index;

  if (data->select) {
    key->flag |= PEK_SELECT;
  }
  else {
    key->flag &= ~PEK_SELECT;
  }

  point->flag |= PEP_EDIT_RECALC;
  data->is_changed = true;
}

static void select_key_op(PEData *data, int point_index, int key_index, bool is_inside)
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  PTCacheEditKey *key = point->keys + key_index;
  const bool is_select = key->flag & PEK_SELECT;
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(key->flag, sel_op_result, PEK_SELECT);
    point->flag |= PEP_EDIT_RECALC;
    data->is_changed = true;
  }
}

static void select_keys(PEData *data,
                        int point_index,
                        int UNUSED(key_index),
                        bool UNUSED(is_inside))
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  KEY_K;

  LOOP_KEYS {
    if (data->select) {
      key->flag |= PEK_SELECT;
    }
    else {
      key->flag &= ~PEK_SELECT;
    }
  }

  point->flag |= PEP_EDIT_RECALC;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name De-Select All Operator
 * \{ */

static bool select_action_apply(PTCacheEditPoint *point, PTCacheEditKey *key, int action)
{
  bool changed = false;
  switch (action) {
    case SEL_SELECT:
      if ((key->flag & PEK_SELECT) == 0) {
        key->flag |= PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
      }
      break;
    case SEL_DESELECT:
      if (key->flag & PEK_SELECT) {
        key->flag &= ~PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
      }
      break;
    case SEL_INVERT:
      if ((key->flag & PEK_SELECT) == 0) {
        key->flag |= PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
      }
      else {
        key->flag &= ~PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
      }
      break;
  }
  return changed;
}

static int pe_select_all_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  POINT_P;
  KEY_K;
  int action = RNA_enum_get(op->ptr, "action");

  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;
    LOOP_VISIBLE_POINTS {
      LOOP_SELECTED_KEYS {
        action = SEL_DESELECT;
        break;
      }

      if (action == SEL_DESELECT) {
        break;
      }
    }
  }

  bool changed = false;
  LOOP_VISIBLE_POINTS {
    LOOP_VISIBLE_KEYS {
      changed |= select_action_apply(point, key, action);
    }
  }

  if (changed) {
    PE_update_selection(depsgraph, scene, ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);
  }
  return OPERATOR_FINISHED;
}

void PARTICLE_OT_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All";
  ot->idname = "PARTICLE_OT_select_all";
  ot->description = "(De)select all particles' keys";

  /* api callbacks */
  ot->exec = pe_select_all_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_select_all(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Pick Select Operator
 * \{ */

struct NearestParticleData {
  PTCacheEditPoint *point;
  PTCacheEditKey *key;
};

static void nearest_key_fn(PEData *data, int point_index, int key_index, bool UNUSED(is_inside))
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  PTCacheEditKey *key = point->keys + key_index;

  struct NearestParticleData *user_data = data->user_data;
  user_data->point = point;
  user_data->key = key;
  data->is_changed = true;
}

static bool pe_nearest_point_and_key(bContext *C,
                                     const int mval[2],
                                     PTCacheEditPoint **r_point,
                                     PTCacheEditKey **r_key)
{
  struct NearestParticleData user_data = {NULL};

  PEData data;
  PE_set_view3d_data(C, &data);
  data.mval = mval;
  data.rad = ED_view3d_select_dist_px();

  data.user_data = &user_data;
  for_mouse_hit_keys(&data, nearest_key_fn, PSEL_NEAREST);
  bool found = data.is_changed;
  PE_data_free(&data);

  *r_point = user_data.point;
  *r_key = user_data.key;
  return found;
}

bool PE_mouse_particles(bContext *C, const int mval[2], const struct SelectPick_Params *params)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);

  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);

  if (!PE_start_edit(edit)) {
    return false;
  }

  PTCacheEditPoint *point;
  PTCacheEditKey *key;

  bool changed = false;
  bool found = pe_nearest_point_and_key(C, mval, &point, &key);

  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->select_passthrough) && (key->flag & PEK_SELECT)) {
      found = false;
    }
    else if (found || params->deselect_all) {
      /* Deselect everything. */
      changed |= PE_deselect_all_visible_ex(edit);
    }
  }

  if (found) {
    switch (params->sel_op) {
      case SEL_OP_ADD: {
        if ((key->flag & PEK_SELECT) == 0) {
          key->flag |= PEK_SELECT;
          point->flag |= PEP_EDIT_RECALC;
          changed = true;
        }
        break;
      }
      case SEL_OP_SUB: {
        if ((key->flag & PEK_SELECT) != 0) {
          key->flag &= ~PEK_SELECT;
          point->flag |= PEP_EDIT_RECALC;
          changed = true;
        }
        break;
      }
      case SEL_OP_XOR: {
        key->flag ^= PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
        break;
      }
      case SEL_OP_SET: {
        if ((key->flag & PEK_SELECT) == 0) {
          key->flag |= PEK_SELECT;
          point->flag |= PEP_EDIT_RECALC;
          changed = true;
        }
        break;
      }
      case SEL_OP_AND: {
        BLI_assert_unreachable(); /* Doesn't make sense for picking. */
        break;
      }
    }
  }

  if (changed) {
    PE_update_selection(depsgraph, scene, ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);
  }

  return changed || found;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Root Operator
 * \{ */

static void select_root(PEData *data, int point_index)
{
  PTCacheEditPoint *point = data->edit->points + point_index;
  PTCacheEditKey *key = point->keys;

  if (point->flag & PEP_HIDE) {
    return;
  }

  if (data->select_action != SEL_TOGGLE) {
    data->is_changed = select_action_apply(point, key, data->select_action);
  }
  else if (key->flag & PEK_SELECT) {
    data->select_toggle_action = SEL_DESELECT;
  }
}

static int select_roots_exec(bContext *C, wmOperator *op)
{
  PEData data;
  int action = RNA_enum_get(op->ptr, "action");

  PE_set_data(C, &data);

  if (action == SEL_TOGGLE) {
    data.select_action = SEL_TOGGLE;
    data.select_toggle_action = SEL_SELECT;

    foreach_point(&data, select_root);

    action = data.select_toggle_action;
  }

  data.select_action = action;
  foreach_point(&data, select_root);

  if (data.is_changed) {
    PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);
  }
  return OPERATOR_FINISHED;
}

void PARTICLE_OT_select_roots(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Roots";
  ot->idname = "PARTICLE_OT_select_roots";
  ot->description = "Select roots of all visible particles";

  /* api callbacks */
  ot->exec = select_roots_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_action(ot, SEL_SELECT, false);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Tip Operator
 * \{ */

static void select_tip(PEData *data, int point_index)
{
  PTCacheEditPoint *point = data->edit->points + point_index;
  PTCacheEditKey *key;

  if (point->totkey == 0) {
    return;
  }

  key = &point->keys[point->totkey - 1];

  if (point->flag & PEP_HIDE) {
    return;
  }

  if (data->select_action != SEL_TOGGLE) {
    data->is_changed = select_action_apply(point, key, data->select_action);
  }
  else if (key->flag & PEK_SELECT) {
    data->select_toggle_action = SEL_DESELECT;
  }
}

static int select_tips_exec(bContext *C, wmOperator *op)
{
  PEData data;
  int action = RNA_enum_get(op->ptr, "action");

  PE_set_data(C, &data);

  if (action == SEL_TOGGLE) {
    data.select_action = SEL_TOGGLE;
    data.select_toggle_action = SEL_SELECT;

    foreach_point(&data, select_tip);

    action = data.select_toggle_action;
  }

  data.select_action = action;
  foreach_point(&data, select_tip);

  if (data.is_changed) {
    PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void PARTICLE_OT_select_tips(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Tips";
  ot->idname = "PARTICLE_OT_select_tips";
  ot->description = "Select tips of all visible particles";

  /* api callbacks */
  ot->exec = select_tips_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_action(ot, SEL_SELECT, false);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Random Operator
 * \{ */

enum { RAN_HAIR, RAN_POINTS };

static const EnumPropertyItem select_random_type_items[] = {
    {RAN_HAIR, "HAIR", 0, "Hair", ""},
    {RAN_POINTS, "POINTS", 0, "Points", ""},
    {0, NULL, 0, NULL, NULL},
};

static int select_random_exec(bContext *C, wmOperator *op)
{
  PEData data;
  int type;

  /* used by LOOP_VISIBLE_POINTS, LOOP_VISIBLE_KEYS and LOOP_KEYS */
  PTCacheEdit *edit;
  PTCacheEditPoint *point;
  PTCacheEditKey *key;
  int p;
  int k;

  const float randfac = RNA_float_get(op->ptr, "ratio");
  const int seed = WM_operator_properties_select_random_seed_increment_get(op);
  const bool select = (RNA_enum_get(op->ptr, "action") == SEL_SELECT);
  RNG *rng;

  type = RNA_enum_get(op->ptr, "type");

  PE_set_data(C, &data);
  data.select_action = SEL_SELECT;
  edit = PE_get_current(data.depsgraph, data.scene, data.ob);

  rng = BLI_rng_new_srandom(seed);

  switch (type) {
    case RAN_HAIR:
      LOOP_VISIBLE_POINTS {
        int flag = ((BLI_rng_get_float(rng) < randfac) == select) ? SEL_SELECT : SEL_DESELECT;
        LOOP_KEYS {
          data.is_changed |= select_action_apply(point, key, flag);
        }
      }
      break;
    case RAN_POINTS:
      LOOP_VISIBLE_POINTS {
        LOOP_VISIBLE_KEYS {
          int flag = ((BLI_rng_get_float(rng) < randfac) == select) ? SEL_SELECT : SEL_DESELECT;
          data.is_changed |= select_action_apply(point, key, flag);
        }
      }
      break;
  }

  BLI_rng_free(rng);

  if (data.is_changed) {
    PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);
  }
  return OPERATOR_FINISHED;
}
