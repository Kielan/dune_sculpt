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

void PARTICLE_OT_select_random(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Random";
  ot->idname = "PARTICLE_OT_select_random";
  ot->description = "Select a randomly distributed set of hair or points";

  /* api callbacks */
  ot->exec = select_random_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_random(ot);
  ot->prop = RNA_def_enum(ot->srna,
                          "type",
                          select_random_type_items,
                          RAN_HAIR,
                          "Type",
                          "Select either hair or points");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Linked operator
 * \{ */

static int select_linked_exec(bContext *C, wmOperator *UNUSED(op))
{
  PEData data;
  PE_set_data(C, &data);
  data.select = true;

  foreach_selected_key(&data, select_keys);

  PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_select_linked(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Linked All";
  ot->idname = "PARTICLE_OT_select_linked";
  ot->description = "Select all keys linked to already selected ones";

  /* api callbacks */
  ot->exec = select_linked_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
}

static int select_linked_pick_exec(bContext *C, wmOperator *op)
{
  PEData data;
  int mval[2];
  int location[2];

  RNA_int_get_array(op->ptr, "location", location);
  mval[0] = location[0];
  mval[1] = location[1];

  PE_set_view3d_data(C, &data);
  data.mval = mval;
  data.rad = 75.0f;
  data.select = !RNA_boolean_get(op->ptr, "deselect");

  for_mouse_hit_keys(&data, select_keys, PSEL_NEAREST);
  PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);
  PE_data_free(&data);

  return OPERATOR_FINISHED;
}

static int select_linked_pick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  RNA_int_set_array(op->ptr, "location", event->mval);
  return select_linked_pick_exec(C, op);
}

void PARTICLE_OT_select_linked_pick(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Linked";
  ot->idname = "PARTICLE_OT_select_linked_pick";
  ot->description = "Select nearest particle from mouse pointer";

  /* api callbacks */
  ot->exec = select_linked_pick_exec;
  ot->invoke = select_linked_pick_invoke;
  ot->poll = PE_poll_view3d;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(
      ot->srna, "deselect", 0, "Deselect", "Deselect linked keys rather than selecting them");
  RNA_def_int_vector(ot->srna, "location", 2, NULL, 0, INT_MAX, "Location", "", 0, 16384);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Box Select Operator
 * \{ */

bool PE_deselect_all_visible_ex(PTCacheEdit *edit)
{
  bool changed = false;
  POINT_P;
  KEY_K;

  LOOP_VISIBLE_POINTS {
    LOOP_SELECTED_KEYS {
      if ((key->flag & PEK_SELECT) != 0) {
        key->flag &= ~PEK_SELECT;
        point->flag |= PEP_EDIT_RECALC;
        changed = true;
      }
    }
  }
  return changed;
}

bool PE_deselect_all_visible(bContext *C)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  if (!PE_start_edit(edit)) {
    return false;
  }
  return PE_deselect_all_visible_ex(edit);
}

bool PE_box_select(bContext *C, const rcti *rect, const int sel_op)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  PEData data;

  if (!PE_start_edit(edit)) {
    return false;
  }

  PE_set_view3d_data(C, &data);
  data.rect = rect;
  data.sel_op = sel_op;

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed = PE_deselect_all_visible_ex(edit);
  }

  if (BLI_rcti_is_empty(rect)) {
    /* pass */
  }
  else {
    for_mouse_hit_keys(&data, select_key_op, PSEL_ALL_KEYS);
  }

  bool is_changed = data.is_changed;
  PE_data_free(&data);

  if (is_changed) {
    PE_update_selection(depsgraph, scene, ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);
  }
  return is_changed;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Circle Select Operator
 * \{ */

static void pe_select_cache_free_generic_userdata(void *data)
{
  PE_data_free(data);
  MEM_freeN(data);
}

static void pe_select_cache_init_with_generic_userdata(bContext *C, wmGenericUserData *wm_userdata)
{
  struct PEData *data = MEM_callocN(sizeof(*data), __func__);
  wm_userdata->data = data;
  wm_userdata->free_fn = pe_select_cache_free_generic_userdata;
  wm_userdata->use_free = true;
  PE_set_view3d_data(C, data);
}

bool PE_circle_select(
    bContext *C, wmGenericUserData *wm_userdata, const int sel_op, const int mval[2], float rad)
{
  BLI_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);

  if (!PE_start_edit(edit)) {
    return false;
  }

  if (wm_userdata->data == NULL) {
    pe_select_cache_init_with_generic_userdata(C, wm_userdata);
  }

  PEData *data = wm_userdata->data;
  data->mval = mval;
  data->rad = rad;
  data->select = (sel_op != SEL_OP_SUB);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data->is_changed = PE_deselect_all_visible_ex(edit);
  }
  for_mouse_hit_keys(data, select_key, 0);

  if (data->is_changed) {
    PE_update_selection(depsgraph, scene, ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);
  }
  return data->is_changed;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Lasso Select Operator
 * \{ */

int PE_lasso_select(bContext *C, const int mcoords[][2], const int mcoords_len, const int sel_op)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  ARegion *region = CTX_wm_region(C);
  ParticleEditSettings *pset = PE_settings(scene);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  POINT_P;
  KEY_K;
  float co[3], mat[4][4];
  int screen_co[2];

  PEData data;

  unit_m4(mat);

  if (!PE_start_edit(edit)) {
    return OPERATOR_CANCELLED;
  }

  /* only for depths */
  PE_set_view3d_data(C, &data);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= PE_deselect_all_visible_ex(edit);
  }

  ParticleSystem *psys = edit->psys;
  ParticleSystemModifierData *psmd_eval = edit->psmd_eval;
  LOOP_VISIBLE_POINTS {
    if (edit->psys && !(psys->flag & PSYS_GLOBAL_HAIR)) {
      psys_mat_hair_to_global(
          ob, psmd_eval->mesh_final, psys->part->from, psys->particles + p, mat);
    }

    if (pset->selectmode == SCE_SELECT_POINT) {
      LOOP_VISIBLE_KEYS {
        copy_v3_v3(co, key->co);
        mul_m4_v3(mat, co);
        const bool is_select = key->flag & PEK_SELECT;
        const bool is_inside =
            ((ED_view3d_project_int_global(region, co, screen_co, V3D_PROJ_TEST_CLIP_WIN) ==
              V3D_PROJ_RET_OK) &&
             BLI_lasso_is_point_inside(
                 mcoords, mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED) &&
             key_test_depth(&data, co, screen_co));
        const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(key->flag, sel_op_result, PEK_SELECT);
          point->flag |= PEP_EDIT_RECALC;
          data.is_changed = true;
        }
      }
    }
    else if (pset->selectmode == SCE_SELECT_END) {
      if (point->totkey) {
        key = point->keys + point->totkey - 1;
        copy_v3_v3(co, key->co);
        mul_m4_v3(mat, co);
        const bool is_select = key->flag & PEK_SELECT;
        const bool is_inside =
            ((ED_view3d_project_int_global(region, co, screen_co, V3D_PROJ_TEST_CLIP_WIN) ==
              V3D_PROJ_RET_OK) &&
             BLI_lasso_is_point_inside(
                 mcoords, mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED) &&
             key_test_depth(&data, co, screen_co));
        const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(key->flag, sel_op_result, PEK_SELECT);
          point->flag |= PEP_EDIT_RECALC;
          data.is_changed = true;
        }
      }
    }
  }

  bool is_changed = data.is_changed;
  PE_data_free(&data);

  if (is_changed) {
    PE_update_selection(depsgraph, scene, ob, 1);
    WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Hide Operator
 * \{ */

static int hide_exec(bContext *C, wmOperator *op)
{
  Object *ob = CTX_data_active_object(C);
  Scene *scene = CTX_data_scene(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);

  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  POINT_P;
  KEY_K;

  if (RNA_boolean_get(op->ptr, "unselected")) {
    LOOP_UNSELECTED_POINTS {
      point->flag |= PEP_HIDE;
      point->flag |= PEP_EDIT_RECALC;

      LOOP_KEYS {
        key->flag &= ~PEK_SELECT;
      }
    }
  }
  else {
    LOOP_SELECTED_POINTS {
      point->flag |= PEP_HIDE;
      point->flag |= PEP_EDIT_RECALC;

      LOOP_KEYS {
        key->flag &= ~PEK_SELECT;
      }
    }
  }

  PE_update_selection(depsgraph, scene, ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_hide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Hide Selected";
  ot->idname = "PARTICLE_OT_hide";
  ot->description = "Hide selected particles";

  /* api callbacks */
  ot->exec = hide_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "unselected", 0, "Unselected", "Hide unselected rather than selected");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reveal Operator
 * \{ */

static int reveal_exec(bContext *C, wmOperator *op)
{
  Object *ob = CTX_data_active_object(C);
  Scene *scene = CTX_data_scene(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  const bool select = RNA_boolean_get(op->ptr, "select");
  POINT_P;
  KEY_K;

  LOOP_POINTS {
    if (point->flag & PEP_HIDE) {
      point->flag &= ~PEP_HIDE;
      point->flag |= PEP_EDIT_RECALC;

      LOOP_KEYS {
        SET_FLAG_FROM_TEST(key->flag, select, PEK_SELECT);
      }
    }
  }

  PE_update_selection(depsgraph, scene, ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_reveal(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reveal";
  ot->idname = "PARTICLE_OT_reveal";
  ot->description = "Show hidden particles";

  /* api callbacks */
  ot->exec = reveal_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "select", true, "Select", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Less Operator
 * \{ */

static void select_less_keys(PEData *data, int point_index)
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  KEY_K;

  LOOP_SELECTED_KEYS {
    if (k == 0) {
      if (((key + 1)->flag & PEK_SELECT) == 0) {
        key->flag |= PEK_TAG;
      }
    }
    else if (k == point->totkey - 1) {
      if (((key - 1)->flag & PEK_SELECT) == 0) {
        key->flag |= PEK_TAG;
      }
    }
    else {
      if ((((key - 1)->flag & (key + 1)->flag) & PEK_SELECT) == 0) {
        key->flag |= PEK_TAG;
      }
    }
  }

  LOOP_KEYS {
    if ((key->flag & PEK_TAG) && (key->flag & PEK_SELECT)) {
      key->flag &= ~(PEK_TAG | PEK_SELECT);
      point->flag |= PEP_EDIT_RECALC; /* redraw selection only */
      data->is_changed = true;
    }
  }
}

static int select_less_exec(bContext *C, wmOperator *UNUSED(op))
{
  PEData data;

  PE_set_data(C, &data);
  foreach_point(&data, select_less_keys);

  PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_select_less(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Less";
  ot->idname = "PARTICLE_OT_select_less";
  ot->description = "Deselect boundary selected keys of each particle";

  /* api callbacks */
  ot->exec = select_less_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select More Operator
 * \{ */

static void select_more_keys(PEData *data, int point_index)
{
  PTCacheEdit *edit = data->edit;
  PTCacheEditPoint *point = edit->points + point_index;
  KEY_K;

  LOOP_KEYS {
    if (key->flag & PEK_SELECT) {
      continue;
    }

    if (k == 0) {
      if ((key + 1)->flag & PEK_SELECT) {
        key->flag |= PEK_TAG;
      }
    }
    else if (k == point->totkey - 1) {
      if ((key - 1)->flag & PEK_SELECT) {
        key->flag |= PEK_TAG;
      }
    }
    else {
      if (((key - 1)->flag | (key + 1)->flag) & PEK_SELECT) {
        key->flag |= PEK_TAG;
      }
    }
  }

  LOOP_KEYS {
    if ((key->flag & PEK_TAG) && (key->flag & PEK_SELECT) == 0) {
      key->flag &= ~PEK_TAG;
      key->flag |= PEK_SELECT;
      point->flag |= PEP_EDIT_RECALC; /* redraw selection only */
      data->is_changed = true;
    }
  }
}

static int select_more_exec(bContext *C, wmOperator *UNUSED(op))
{
  PEData data;

  PE_set_data(C, &data);
  foreach_point(&data, select_more_keys);

  PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_SELECTED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_select_more(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select More";
  ot->idname = "PARTICLE_OT_select_more";
  ot->description = "Select keys linked to boundary selected keys of each particle";

  /* api callbacks */
  ot->exec = select_more_exec;
  ot->poll = PE_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Re-Key Operator
 * \{ */

static void rekey_particle(PEData *data, int pa_index)
{
  PTCacheEdit *edit = data->edit;
  ParticleSystem *psys = edit->psys;
  ParticleSimulationData sim = {0};
  ParticleData *pa = psys->particles + pa_index;
  PTCacheEditPoint *point = edit->points + pa_index;
  ParticleKey state;
  HairKey *key, *new_keys, *okey;
  PTCacheEditKey *ekey;
  float dval, sta, end;
  int k;

  sim.depsgraph = data->depsgraph;
  sim.scene = data->scene;
  sim.ob = data->ob;
  sim.psys = edit->psys;

  pa->flag |= PARS_REKEY;

  key = new_keys = MEM_callocN(data->totrekey * sizeof(HairKey), "Hair re-key keys");

  okey = pa->hair;
  /* root and tip stay the same */
  copy_v3_v3(key->co, okey->co);
  copy_v3_v3((key + data->totrekey - 1)->co, (okey + pa->totkey - 1)->co);

  sta = key->time = okey->time;
  end = (key + data->totrekey - 1)->time = (okey + pa->totkey - 1)->time;
  dval = (end - sta) / (float)(data->totrekey - 1);

  /* interpolate new keys from old ones */
  for (k = 1, key++; k < data->totrekey - 1; k++, key++) {
    state.time = (float)k / (float)(data->totrekey - 1);
    psys_get_particle_on_path(&sim, pa_index, &state, 0);
    copy_v3_v3(key->co, state.co);
    key->time = sta + k * dval;
  }

  /* replace keys */
  if (pa->hair) {
    MEM_freeN(pa->hair);
  }
  pa->hair = new_keys;

  point->totkey = pa->totkey = data->totrekey;

  if (point->keys) {
    MEM_freeN(point->keys);
  }
  ekey = point->keys = MEM_callocN(pa->totkey * sizeof(PTCacheEditKey), "Hair re-key edit keys");

  for (k = 0, key = pa->hair; k < pa->totkey; k++, key++, ekey++) {
    ekey->co = key->co;
    ekey->time = &key->time;
    ekey->flag |= PEK_SELECT;
    if (!(psys->flag & PSYS_GLOBAL_HAIR)) {
      ekey->flag |= PEK_USE_WCO;
    }
  }

  pa->flag &= ~PARS_REKEY;
  point->flag |= PEP_EDIT_RECALC;
}

static int rekey_exec(bContext *C, wmOperator *op)
{
  PEData data;

  PE_set_data(C, &data);

  data.dval = 1.0f / (float)(data.totrekey - 1);
  data.totrekey = RNA_int_get(op->ptr, "keys_number");

  foreach_selected_point(&data, rekey_particle);

  recalc_lengths(data.edit);
  PE_update_object(data.depsgraph, data.scene, data.ob, 1);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_rekey(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Rekey";
  ot->idname = "PARTICLE_OT_rekey";
  ot->description = "Change the number of keys of selected particles (root and tip keys included)";

  /* api callbacks */
  ot->exec = rekey_exec;
  ot->invoke = WM_operator_props_popup;
  ot->poll = PE_hair_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_int(ot->srna, "keys_number", 2, 2, INT_MAX, "Number of Keys", "", 2, 100);
}

static void rekey_particle_to_time(
    const bContext *C, Scene *scene, Object *ob, int pa_index, float path_time)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  ParticleSystem *psys;
  ParticleSimulationData sim = {0};
  ParticleData *pa;
  ParticleKey state;
  HairKey *new_keys, *key;
  PTCacheEditKey *ekey;
  int k;

  if (!edit || !edit->psys) {
    return;
  }

  psys = edit->psys;

  sim.depsgraph = depsgraph;
  sim.scene = scene;
  sim.ob = ob;
  sim.psys = psys;

  pa = psys->particles + pa_index;

  pa->flag |= PARS_REKEY;

  key = new_keys = MEM_dupallocN(pa->hair);

  /* interpolate new keys from old ones (roots stay the same) */
  for (k = 1, key++; k < pa->totkey; k++, key++) {
    state.time = path_time * (float)k / (float)(pa->totkey - 1);
    psys_get_particle_on_path(&sim, pa_index, &state, 0);
    copy_v3_v3(key->co, state.co);
  }

  /* replace hair keys */
  if (pa->hair) {
    MEM_freeN(pa->hair);
  }
  pa->hair = new_keys;

  /* update edit pointers */
  for (k = 0, key = pa->hair, ekey = edit->points[pa_index].keys; k < pa->totkey;
       k++, key++, ekey++) {
    ekey->co = key->co;
    ekey->time = &key->time;
  }

  pa->flag &= ~PARS_REKEY;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal Utilities
 * \{ */

static int remove_tagged_particles(Object *ob, ParticleSystem *psys, int mirror)
{
  PTCacheEdit *edit = psys->edit;
  ParticleData *pa, *npa = 0, *new_pars = 0;
  POINT_P;
  PTCacheEditPoint *npoint = 0, *new_points = 0;
  ParticleSystemModifierData *psmd_eval;
  int i, new_totpart = psys->totpart, removed = 0;

  if (mirror) {
    /* mirror tags */
    psmd_eval = edit->psmd_eval;

    LOOP_TAGGED_POINTS {
      PE_mirror_particle(ob, psmd_eval->mesh_final, psys, psys->particles + p, NULL);
    }
  }

  LOOP_TAGGED_POINTS {
    new_totpart--;
    removed++;
  }

  if (new_totpart != psys->totpart) {
    if (new_totpart) {
      npa = new_pars = MEM_callocN(new_totpart * sizeof(ParticleData), "ParticleData array");
      npoint = new_points = MEM_callocN(new_totpart * sizeof(PTCacheEditPoint),
                                        "PTCacheEditKey array");

      if (ELEM(NULL, new_pars, new_points)) {
        /* allocation error! */
        if (new_pars) {
          MEM_freeN(new_pars);
        }
        if (new_points) {
          MEM_freeN(new_points);
        }
        return 0;
      }
    }

    pa = psys->particles;
    point = edit->points;
    for (i = 0; i < psys->totpart; i++, pa++, point++) {
      if (point->flag & PEP_TAG) {
        if (point->keys) {
          MEM_freeN(point->keys);
        }
        if (pa->hair) {
          MEM_freeN(pa->hair);
        }
      }
      else {
        memcpy(npa, pa, sizeof(ParticleData));
        memcpy(npoint, point, sizeof(PTCacheEditPoint));
        npa++;
        npoint++;
      }
    }

    if (psys->particles) {
      MEM_freeN(psys->particles);
    }
    psys->particles = new_pars;

    if (edit->points) {
      MEM_freeN(edit->points);
    }
    edit->points = new_points;

    MEM_SAFE_FREE(edit->mirror_cache);

    if (psys->child) {
      MEM_freeN(psys->child);
      psys->child = NULL;
      psys->totchild = 0;
    }

    edit->totpoint = psys->totpart = new_totpart;
  }

  return removed;
}

static void remove_tagged_keys(Depsgraph *depsgraph, Object *ob, ParticleSystem *psys)
{
  PTCacheEdit *edit = psys->edit;
  ParticleData *pa;
  HairKey *hkey, *nhkey, *new_hkeys = 0;
  POINT_P;
  KEY_K;
  PTCacheEditKey *nkey, *new_keys;
  short new_totkey;

  if (pe_x_mirror(ob)) {
    /* mirror key tags */
    ParticleSystemModifierData *psmd = psys_get_modifier(ob, psys);
    ParticleSystemModifierData *psmd_eval = (ParticleSystemModifierData *)
        BKE_modifier_get_evaluated(depsgraph, ob, &psmd->modifier);

    LOOP_POINTS {
      LOOP_TAGGED_KEYS {
        PE_mirror_particle(ob, psmd_eval->mesh_final, psys, psys->particles + p, NULL);
        break;
      }
    }
  }

  LOOP_POINTS {
    new_totkey = point->totkey;
    LOOP_TAGGED_KEYS {
      new_totkey--;
    }
    /* We can't have elements with less than two keys. */
    if (new_totkey < 2) {
      point->flag |= PEP_TAG;
    }
  }
  remove_tagged_particles(ob, psys, pe_x_mirror(ob));

  LOOP_POINTS {
    pa = psys->particles + p;
    new_totkey = pa->totkey;

    LOOP_TAGGED_KEYS {
      new_totkey--;
    }

    if (new_totkey != pa->totkey) {
      nhkey = new_hkeys = MEM_callocN(new_totkey * sizeof(HairKey), "HairKeys");
      nkey = new_keys = MEM_callocN(new_totkey * sizeof(PTCacheEditKey), "particle edit keys");

      hkey = pa->hair;
      LOOP_KEYS {
        while (key->flag & PEK_TAG && hkey < pa->hair + pa->totkey) {
          key++;
          hkey++;
        }

        if (hkey < pa->hair + pa->totkey) {
          copy_v3_v3(nhkey->co, hkey->co);
          nhkey->editflag = hkey->editflag;
          nhkey->time = hkey->time;
          nhkey->weight = hkey->weight;

          nkey->co = nhkey->co;
          nkey->time = &nhkey->time;
          /* these can be copied from old edit keys */
          nkey->flag = key->flag;
          nkey->ftime = key->ftime;
          nkey->length = key->length;
          copy_v3_v3(nkey->world_co, key->world_co);
        }
        nkey++;
        nhkey++;
        hkey++;
      }

      if (pa->hair) {
        MEM_freeN(pa->hair);
      }

      if (point->keys) {
        MEM_freeN(point->keys);
      }

      pa->hair = new_hkeys;
      point->keys = new_keys;

      point->totkey = pa->totkey = new_totkey;

      /* flag for recalculating length */
      point->flag |= PEP_EDIT_RECALC;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Subdivide Operator
 * \{ */

/* works like normal edit mode subdivide, inserts keys between neighboring selected keys */
static void subdivide_particle(PEData *data, int pa_index)
{
  PTCacheEdit *edit = data->edit;
  ParticleSystem *psys = edit->psys;
  ParticleSimulationData sim = {0};
  ParticleData *pa = psys->particles + pa_index;
  PTCacheEditPoint *point = edit->points + pa_index;
  ParticleKey state;
  HairKey *key, *nkey, *new_keys;
  PTCacheEditKey *ekey, *nekey, *new_ekeys;

  int k;
  short totnewkey = 0;
  float endtime;

  sim.depsgraph = data->depsgraph;
  sim.scene = data->scene;
  sim.ob = data->ob;
  sim.psys = edit->psys;

  for (k = 0, ekey = point->keys; k < pa->totkey - 1; k++, ekey++) {
    if (ekey->flag & PEK_SELECT && (ekey + 1)->flag & PEK_SELECT) {
      totnewkey++;
    }
  }

  if (totnewkey == 0) {
    return;
  }

  pa->flag |= PARS_REKEY;

  nkey = new_keys = MEM_callocN((pa->totkey + totnewkey) * (sizeof(HairKey)),
                                "Hair subdivide keys");
  nekey = new_ekeys = MEM_callocN((pa->totkey + totnewkey) * (sizeof(PTCacheEditKey)),
                                  "Hair subdivide edit keys");

  key = pa->hair;
  endtime = key[pa->totkey - 1].time;

  for (k = 0, ekey = point->keys; k < pa->totkey - 1; k++, key++, ekey++) {

    memcpy(nkey, key, sizeof(HairKey));
    memcpy(nekey, ekey, sizeof(PTCacheEditKey));

    nekey->co = nkey->co;
    nekey->time = &nkey->time;

    nkey++;
    nekey++;

    if (ekey->flag & PEK_SELECT && (ekey + 1)->flag & PEK_SELECT) {
      nkey->time = (key->time + (key + 1)->time) * 0.5f;
      state.time = (endtime != 0.0f) ? nkey->time / endtime : 0.0f;
      psys_get_particle_on_path(&sim, pa_index, &state, 0);
      copy_v3_v3(nkey->co, state.co);

      nekey->co = nkey->co;
      nekey->time = &nkey->time;
      nekey->flag |= PEK_SELECT;
      if (!(psys->flag & PSYS_GLOBAL_HAIR)) {
        nekey->flag |= PEK_USE_WCO;
      }

      nekey++;
      nkey++;
    }
  }
  /* Tip still not copied. */
  memcpy(nkey, key, sizeof(HairKey));
  memcpy(nekey, ekey, sizeof(PTCacheEditKey));

  nekey->co = nkey->co;
  nekey->time = &nkey->time;

  if (pa->hair) {
    MEM_freeN(pa->hair);
  }
  pa->hair = new_keys;

  if (point->keys) {
    MEM_freeN(point->keys);
  }
  point->keys = new_ekeys;

  point->totkey = pa->totkey = pa->totkey + totnewkey;
  point->flag |= PEP_EDIT_RECALC;
  pa->flag &= ~PARS_REKEY;
}

static int subdivide_exec(bContext *C, wmOperator *UNUSED(op))
{
  PEData data;

  PE_set_data(C, &data);
  foreach_point(&data, subdivide_particle);

  recalc_lengths(data.edit);
  PE_update_selection(data.depsgraph, data.scene, data.ob, 1);
  PE_update_object(data.depsgraph, data.scene, data.ob, 1);
  DEG_id_tag_update(&data.ob->id, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_subdivide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Subdivide";
  ot->idname = "PARTICLE_OT_subdivide";
  ot->description = "Subdivide selected particles segments (adds keys)";

  /* api callbacks */
  ot->exec = subdivide_exec;
  ot->poll = PE_hair_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Remove Doubles Operator
 * \{ */

static int remove_doubles_exec(bContext *C, wmOperator *op)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  ParticleSystem *psys = edit->psys;
  ParticleSystemModifierData *psmd_eval;
  KDTree_3d *tree;
  KDTreeNearest_3d nearest[10];
  POINT_P;
  float mat[4][4], co[3], threshold = RNA_float_get(op->ptr, "threshold");
  int n, totn, removed, totremoved;

  if (psys->flag & PSYS_GLOBAL_HAIR) {
    return OPERATOR_CANCELLED;
  }

  edit = psys->edit;
  psmd_eval = edit->psmd_eval;
  totremoved = 0;

  do {
    removed = 0;

    tree = BLI_kdtree_3d_new(psys->totpart);

    /* insert particles into kd tree */
    LOOP_SELECTED_POINTS {
      psys_mat_hair_to_object(
          ob, psmd_eval->mesh_final, psys->part->from, psys->particles + p, mat);
      copy_v3_v3(co, point->keys->co);
      mul_m4_v3(mat, co);
      BLI_kdtree_3d_insert(tree, p, co);
    }

    BLI_kdtree_3d_balance(tree);

    /* tag particles to be removed */
    LOOP_SELECTED_POINTS {
      psys_mat_hair_to_object(
          ob, psmd_eval->mesh_final, psys->part->from, psys->particles + p, mat);
      copy_v3_v3(co, point->keys->co);
      mul_m4_v3(mat, co);

      totn = BLI_kdtree_3d_find_nearest_n(tree, co, nearest, 10);

      for (n = 0; n < totn; n++) {
        /* this needs a custom threshold still */
        if (nearest[n].index > p && nearest[n].dist < threshold) {
          if (!(point->flag & PEP_TAG)) {
            point->flag |= PEP_TAG;
            removed++;
          }
        }
      }
    }

    BLI_kdtree_3d_free(tree);

    /* remove tagged particles - don't do mirror here! */
    remove_tagged_particles(ob, psys, 0);
    totremoved += removed;
  } while (removed);

  if (totremoved == 0) {
    return OPERATOR_CANCELLED;
  }

  BKE_reportf(op->reports, RPT_INFO, "Removed %d double particle(s)", totremoved);

  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_remove_doubles(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Doubles";
  ot->idname = "PARTICLE_OT_remove_doubles";
  ot->description = "Remove selected particles close enough of others";

  /* api callbacks */
  ot->exec = remove_doubles_exec;
  ot->poll = PE_hair_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_float(ot->srna,
                "threshold",
                0.0002f,
                0.0f,
                FLT_MAX,
                "Merge Distance",
                "Threshold distance within which particles are removed",
                0.00001f,
                0.1f);
}

static int weight_set_exec(bContext *C, wmOperator *op)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  ParticleEditSettings *pset = PE_settings(scene);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  ParticleSystem *psys = edit->psys;
  POINT_P;
  KEY_K;
  HairKey *hkey;
  float weight;
  ParticleBrushData *brush = &pset->brush[pset->brushtype];
  float factor = RNA_float_get(op->ptr, "factor");

  weight = brush->strength;
  edit = psys->edit;

  LOOP_SELECTED_POINTS {
    ParticleData *pa = psys->particles + p;

    LOOP_SELECTED_KEYS {
      hkey = pa->hair + k;
      hkey->weight = interpf(weight, hkey->weight, factor);
    }
  }

  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_weight_set(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Weight Set";
  ot->idname = "PARTICLE_OT_weight_set";
  ot->description = "Set the weight of selected keys";

  /* api callbacks */
  ot->exec = weight_set_exec;
  ot->poll = PE_hair_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_float(ot->srna,
                "factor",
                1,
                0,
                1,
                "Factor",
                "Interpolation factor between current brush weight, and keys' weights",
                0,
                1);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cursor Drawing
 * \{ */

static void brush_drawcursor(bContext *C, int x, int y, void *UNUSED(customdata))
{
  Scene *scene = CTX_data_scene(C);
  ParticleEditSettings *pset = PE_settings(scene);
  ParticleBrushData *brush;

  if (!WM_toolsystem_active_tool_is_brush(C)) {
    return;
  }

  brush = &pset->brush[pset->brushtype];

  if (brush) {
    uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

    immUniformColor4ub(255, 255, 255, 128);

    GPU_line_smooth(true);
    GPU_blend(GPU_BLEND_ALPHA);

    imm_draw_circle_wire_2d(pos, (float)x, (float)y, pe_brush_size_get(scene, brush), 40);

    GPU_blend(GPU_BLEND_NONE);
    GPU_line_smooth(false);

    immUnbindProgram();
  }
}

static void toggle_particle_cursor(Scene *scene, bool enable)
{
  ParticleEditSettings *pset = PE_settings(scene);

  if (pset->paintcursor && !enable) {
    WM_paint_cursor_end(pset->paintcursor);
    pset->paintcursor = NULL;
  }
  else if (enable) {
    pset->paintcursor = WM_paint_cursor_activate(
        SPACE_VIEW3D, RGN_TYPE_WINDOW, PE_poll_view3d, brush_drawcursor, NULL);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Delete Operator
 * \{ */

enum { DEL_PARTICLE, DEL_KEY };

static const EnumPropertyItem delete_type_items[] = {
    {DEL_PARTICLE, "PARTICLE", 0, "Particle", ""},
    {DEL_KEY, "KEY", 0, "Key", ""},
    {0, NULL, 0, NULL, NULL},
};

static void set_delete_particle(PEData *data, int pa_index)
{
  PTCacheEdit *edit = data->edit;

  edit->points[pa_index].flag |= PEP_TAG;
}

static void set_delete_particle_key(PEData *data,
                                    int pa_index,
                                    int key_index,
                                    bool UNUSED(is_inside))
{
  PTCacheEdit *edit = data->edit;

  edit->points[pa_index].keys[key_index].flag |= PEK_TAG;
}

static int delete_exec(bContext *C, wmOperator *op)
{
  PEData data;
  int type = RNA_enum_get(op->ptr, "type");

  PE_set_data(C, &data);

  if (type == DEL_KEY) {
    foreach_selected_key(&data, set_delete_particle_key);
    remove_tagged_keys(data.depsgraph, data.ob, data.edit->psys);
    recalc_lengths(data.edit);
  }
  else if (type == DEL_PARTICLE) {
    foreach_selected_point(&data, set_delete_particle);
    remove_tagged_particles(data.ob, data.edit->psys, pe_x_mirror(data.ob));
    recalc_lengths(data.edit);
  }

  DEG_id_tag_update(&data.ob->id, ID_RECALC_GEOMETRY);
  BKE_particle_batch_cache_dirty_tag(data.edit->psys, BKE_PARTICLE_BATCH_DIRTY_ALL);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, data.ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_delete(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete";
  ot->idname = "PARTICLE_OT_delete";
  ot->description = "Delete selected particles or keys";

  /* api callbacks */
  ot->exec = delete_exec;
  ot->invoke = WM_menu_invoke;
  ot->poll = PE_hair_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_enum(ot->srna,
                          "type",
                          delete_type_items,
                          DEL_PARTICLE,
                          "Type",
                          "Delete a full particle or only keys");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mirror Operator
 * \{ */

static void PE_mirror_x(Depsgraph *depsgraph, Scene *scene, Object *ob, int tagged)
{
  Mesh *me = (Mesh *)(ob->data);
  ParticleSystemModifierData *psmd_eval;
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);
  ParticleSystem *psys = edit->psys;
  ParticleData *pa, *newpa, *new_pars;
  PTCacheEditPoint *newpoint, *new_points;
  POINT_P;
  KEY_K;
  HairKey *hkey;
  int *mirrorfaces = NULL;
  int rotation, totpart, newtotpart;

  if (psys->flag & PSYS_GLOBAL_HAIR) {
    return;
  }

  psmd_eval = edit->psmd_eval;
  if (!psmd_eval->mesh_final) {
    return;
  }

  const bool use_dm_final_indices = (psys->part->use_modifier_stack &&
                                     !psmd_eval->mesh_final->runtime.deformed_only);

  /* NOTE: this is not nice to use tessfaces but hard to avoid since pa->num uses tessfaces */
  BKE_mesh_tessface_ensure(me);

  /* NOTE: In case psys uses Mesh tessface indices, we mirror final Mesh itself, not orig mesh.
   * Avoids an (impossible) mesh -> orig -> mesh tessface indices conversion. */
  mirrorfaces = mesh_get_x_mirror_faces(
      ob, NULL, use_dm_final_indices ? psmd_eval->mesh_final : NULL);

  if (!edit->mirror_cache) {
    PE_update_mirror_cache(ob, psys);
  }

  totpart = psys->totpart;
  newtotpart = psys->totpart;
  LOOP_VISIBLE_POINTS {
    pa = psys->particles + p;

    if (!tagged) {
      if (point_is_selected(point)) {
        if (edit->mirror_cache[p] != -1) {
          /* already has a mirror, don't need to duplicate */
          PE_mirror_particle(ob, psmd_eval->mesh_final, psys, pa, NULL);
          continue;
        }
        point->flag |= PEP_TAG;
      }
    }

    if ((point->flag & PEP_TAG) && mirrorfaces[pa->num * 2] != -1) {
      newtotpart++;
    }
  }

  if (newtotpart != psys->totpart) {
    MFace *mtessface = use_dm_final_indices ? psmd_eval->mesh_final->mface : me->mface;

    /* allocate new arrays and copy existing */
    new_pars = MEM_callocN(newtotpart * sizeof(ParticleData), "ParticleData new");
    new_points = MEM_callocN(newtotpart * sizeof(PTCacheEditPoint), "PTCacheEditPoint new");

    if (psys->particles) {
      memcpy(new_pars, psys->particles, totpart * sizeof(ParticleData));
      MEM_freeN(psys->particles);
    }
    psys->particles = new_pars;

    if (edit->points) {
      memcpy(new_points, edit->points, totpart * sizeof(PTCacheEditPoint));
      MEM_freeN(edit->points);
    }
    edit->points = new_points;

    MEM_SAFE_FREE(edit->mirror_cache);

    edit->totpoint = psys->totpart = newtotpart;

    /* create new elements */
    newpa = psys->particles + totpart;
    newpoint = edit->points + totpart;

    for (p = 0, point = edit->points; p < totpart; p++, point++) {
      pa = psys->particles + p;
      const int pa_num = pa->num;

      if (point->flag & PEP_HIDE) {
        continue;
      }

      if (!(point->flag & PEP_TAG) || mirrorfaces[pa_num * 2] == -1) {
        continue;
      }

      /* duplicate */
      *newpa = *pa;
      *newpoint = *point;
      if (pa->hair) {
        newpa->hair = MEM_dupallocN(pa->hair);
      }
      if (point->keys) {
        newpoint->keys = MEM_dupallocN(point->keys);
      }

      /* rotate weights according to vertex index rotation */
      rotation = mirrorfaces[pa_num * 2 + 1];
      newpa->fuv[0] = pa->fuv[2];
      newpa->fuv[1] = pa->fuv[1];
      newpa->fuv[2] = pa->fuv[0];
      newpa->fuv[3] = pa->fuv[3];
      while (rotation--) {
        if (mtessface[pa_num].v4) {
          SHIFT4(float, newpa->fuv[0], newpa->fuv[1], newpa->fuv[2], newpa->fuv[3]);
        }
        else {
          SHIFT3(float, newpa->fuv[0], newpa->fuv[1], newpa->fuv[2]);
        }
      }

      /* assign face index */
      /* NOTE: mesh_get_x_mirror_faces generates -1 for non-found mirror,
       * same as DMCACHE_NOTFOUND. */
      newpa->num = mirrorfaces[pa_num * 2];

      if (use_dm_final_indices) {
        newpa->num_dmcache = DMCACHE_ISCHILD;
      }
      else {
        newpa->num_dmcache = psys_particle_dm_face_lookup(
            psmd_eval->mesh_final, psmd_eval->mesh_original, newpa->num, newpa->fuv, NULL);
      }

      /* update edit key pointers */
      key = newpoint->keys;
      for (k = 0, hkey = newpa->hair; k < newpa->totkey; k++, hkey++, key++) {
        key->co = hkey->co;
        key->time = &hkey->time;
      }

      /* map key positions as mirror over x axis */
      PE_mirror_particle(ob, psmd_eval->mesh_final, psys, pa, newpa);

      newpa++;
      newpoint++;
    }
  }

  LOOP_POINTS {
    point->flag &= ~PEP_TAG;
  }

  MEM_freeN(mirrorfaces);
}

static int mirror_exec(bContext *C, wmOperator *UNUSED(op))
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);

  PE_mirror_x(depsgraph, scene, ob, 0);

  update_world_cos(ob, edit);
  psys_free_path_cache(NULL, edit);

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE | NA_EDITED, ob);
  BKE_particle_batch_cache_dirty_tag(edit->psys, BKE_PARTICLE_BATCH_DIRTY_ALL);
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

  return OPERATOR_FINISHED;
}

static bool mirror_poll(bContext *C)
{
  if (!PE_hair_poll(C)) {
    return false;
  }

  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  PTCacheEdit *edit = PE_get_current(depsgraph, scene, ob);

  /* The operator only works for hairs emitted from faces. */
  return edit->psys->part->from == PART_FROM_FACE;
}

void PARTICLE_OT_mirror(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Mirror";
  ot->idname = "PARTICLE_OT_mirror";
  ot->description = "Duplicate and mirror the selected particles along the local X axis";

  /* api callbacks */
  ot->exec = mirror_exec;
  ot->poll = mirror_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
