#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "DNA_anim_types.h"
#include "DNA_collection_types.h"
#include "DNA_image_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_timecode.h"

#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_animsys.h" /* <------ should this be here?, needed for sequencer update */
#include "BKE_callbacks.h"
#include "BKE_camera.h"
#include "BKE_colortools.h"
#include "BKE_context.h" /* XXX needed by wm_window.h */
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_image_save.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_remap.h"
#include "BKE_mask.h"
#include "BKE_modifier.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_pointcache.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_sound.h"
#include "BKE_writeavi.h" /* <------ should be replaced once with generic movie module */

#include "NOD_composite.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_debug.h"
#include "DEG_depsgraph_query.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"
#include "PIL_time.h"

#include "RE_engine.h"
#include "RE_pipeline.h"
#include "RE_texture.h"

#include "SEQ_relations.h"
#include "SEQ_render.h"

#include "../../windowmanager/WM_api.h"    /* XXX */
#include "../../windowmanager/wm_window.h" /* XXX */
#include "GPU_context.h"

#ifdef WITH_FREESTYLE
#  include "FRS_freestyle.h"
#endif

#include "DEG_depsgraph.h"

/* internal */
#include "pipeline.h"
#include "render_result.h"
#include "render_types.h"

/* render flow
 *
 * 1) Initialize state
 * - state data, tables
 * - movie/image file init
 * - everything that doesn't change during animation
 *
 * 2) Initialize data
 * - camera, world, matrices
 * - make render verts, faces, halos, strands
 * - everything can change per frame/field
 *
 * 3) Render Processor
 * - multiple layers
 * - tiles, rect, baking
 * - layers/tiles optionally to disk or directly in Render Result
 *
 * 4) Composite Render Result
 * - also read external files etc
 *
 * 5) Image Files
 * - save file or append in movie
 */

/* -------------------------------------------------------------------- */
/** \name Globals
 * \{ */

/* here we store all renders */
static struct {
  ListBase renderlist;
} RenderGlobal = {{NULL, NULL}};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Callbacks
 * \{ */

static void render_callback_exec_null(Render *re, Main *bmain, eCbEvent evt)
{
  if (re->r.scemode & R_BUTS_PREVIEW) {
    return;
  }
  BKE_callback_exec_null(bmain, evt);
}

static void render_callback_exec_id(Render *re, Main *bmain, ID *id, eCbEvent evt)
{
  if (re->r.scemode & R_BUTS_PREVIEW) {
    return;
  }
  BKE_callback_exec_id(bmain, id, evt);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Allocation & Free
 * \{ */

static int do_write_image_or_movie(Render *re,
                                   Main *bmain,
                                   Scene *scene,
                                   bMovieHandle *mh,
                                   const int totvideos,
                                   const char *name_override);

/* default callbacks, set in each new render */
static void result_nothing(void *UNUSED(arg), RenderResult *UNUSED(rr))
{
}
static void result_rcti_nothing(void *UNUSED(arg),
                                RenderResult *UNUSED(rr),
                                struct rcti *UNUSED(rect))
{
}
static void current_scene_nothing(void *UNUSED(arg), Scene *UNUSED(scene))
{
}
static void stats_nothing(void *UNUSED(arg), RenderStats *UNUSED(rs))
{
}
static void float_nothing(void *UNUSED(arg), float UNUSED(val))
{
}
static int default_break(void *UNUSED(arg))
{
  return G.is_break == true;
}

static void stats_background(void *UNUSED(arg), RenderStats *rs)
{
  if (rs->infostr == NULL) {
    return;
  }

  uintptr_t mem_in_use, peak_memory;
  float megs_used_memory, megs_peak_memory;
  char info_time_str[32];

  mem_in_use = MEM_get_memory_in_use();
  peak_memory = MEM_get_peak_memory();

  megs_used_memory = (mem_in_use) / (1024.0 * 1024.0);
  megs_peak_memory = (peak_memory) / (1024.0 * 1024.0);

  fprintf(stdout,
          TIP_("Fra:%d Mem:%.2fM (Peak %.2fM) "),
          rs->cfra,
          megs_used_memory,
          megs_peak_memory);

  BLI_timecode_string_from_time_simple(
      info_time_str, sizeof(info_time_str), PIL_check_seconds_timer() - rs->starttime);
  fprintf(stdout, TIP_("| Time:%s | "), info_time_str);

  fprintf(stdout, "%s", rs->infostr);

  /* Flush stdout to be sure python callbacks are printing stuff after blender. */
  fflush(stdout);

  /* NOTE: using G_MAIN seems valid here???
   * Not sure it's actually even used anyway, we could as well pass NULL? */
  BKE_callback_exec_null(G_MAIN, BKE_CB_EVT_RENDER_STATS);

  fputc('\n', stdout);
  fflush(stdout);
}

void RE_FreeRenderResult(RenderResult *rr)
{
  render_result_free(rr);
}

float *RE_RenderLayerGetPass(RenderLayer *rl, const char *name, const char *viewname)
{
  RenderPass *rpass = RE_pass_find_by_name(rl, name, viewname);
  return rpass ? rpass->rect : NULL;
}

RenderLayer *RE_GetRenderLayer(RenderResult *rr, const char *name)
{
  if (rr == NULL) {
    return NULL;
  }

  return BLI_findstring(&rr->layers, name, offsetof(RenderLayer, name));
}

bool RE_HasSingleLayer(Render *re)
{
  return (re->r.scemode & R_SINGLE_LAYER);
}

RenderResult *RE_MultilayerConvert(
    void *exrhandle, const char *colorspace, bool predivide, int rectx, int recty)
{
  return render_result_new_from_exr(exrhandle, colorspace, predivide, rectx, recty);
}

RenderLayer *render_get_active_layer(Render *re, RenderResult *rr)
{
  ViewLayer *view_layer = BLI_findlink(&re->view_layers, re->active_view_layer);

  if (view_layer) {
    RenderLayer *rl = BLI_findstring(&rr->layers, view_layer->name, offsetof(RenderLayer, name));

    if (rl) {
      return rl;
    }
  }

  return rr->layers.first;
}

static bool render_scene_has_layers_to_render(Scene *scene, ViewLayer *single_layer)
{
  if (single_layer) {
    return true;
  }

  ViewLayer *view_layer;
  for (view_layer = scene->view_layers.first; view_layer; view_layer = view_layer->next) {
    if (view_layer->flag & VIEW_LAYER_RENDER) {
      return true;
    }
  }
  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public Render API
 * \{ */

Render *RE_GetRender(const char *name)
{
  Render *re;

  /* search for existing renders */
  for (re = RenderGlobal.renderlist.first; re; re = re->next) {
    if (STREQLEN(re->name, name, RE_MAXNAME)) {
      break;
    }
  }

  return re;
}

RenderResult *RE_AcquireResultRead(Render *re)
{
  if (re) {
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_READ);
    return re->result;
  }

  return NULL;
}

RenderResult *RE_AcquireResultWrite(Render *re)
{
  if (re) {
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
    render_result_passes_allocated_ensure(re->result);
    return re->result;
  }

  return NULL;
}

void RE_ClearResult(Render *re)
{
  if (re) {
    render_result_free(re->result);
    re->result = NULL;
  }
}

void RE_SwapResult(Render *re, RenderResult **rr)
{
  /* for keeping render buffers */
  if (re) {
    SWAP(RenderResult *, re->result, *rr);
  }
}

void RE_ReleaseResult(Render *re)
{
  if (re) {
    BLI_rw_mutex_unlock(&re->resultmutex);
  }
}

Scene *RE_GetScene(Render *re)
{
  if (re) {
    return re->scene;
  }
  return NULL;
}

void RE_SetScene(Render *re, Scene *sce)
{
  if (re) {
    re->scene = sce;
  }
}

void RE_AcquireResultImageViews(Render *re, RenderResult *rr)
{
  memset(rr, 0, sizeof(RenderResult));

  if (re) {
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_READ);

    if (re->result) {
      RenderLayer *rl;
      RenderView *rv, *rview;

      rr->rectx = re->result->rectx;
      rr->recty = re->result->recty;

      /* creates a temporary duplication of views */
      render_result_views_shallowcopy(rr, re->result);

      rv = rr->views.first;
      rr->have_combined = (rv->rectf != NULL);

      /* active layer */
      rl = render_get_active_layer(re, re->result);

      if (rl) {
        if (rv->rectf == NULL) {
          for (rview = (RenderView *)rr->views.first; rview; rview = rview->next) {
            rview->rectf = RE_RenderLayerGetPass(rl, RE_PASSNAME_COMBINED, rview->name);
          }
        }

        if (rv->rectz == NULL) {
          for (rview = (RenderView *)rr->views.first; rview; rview = rview->next) {
            rview->rectz = RE_RenderLayerGetPass(rl, RE_PASSNAME_Z, rview->name);
          }
        }
      }

      rr->layers = re->result->layers;
      rr->xof = re->disprect.xmin;
      rr->yof = re->disprect.ymin;
      rr->stamp_data = re->result->stamp_data;
    }
  }
}

void RE_ReleaseResultImageViews(Render *re, RenderResult *rr)
{
  if (re) {
    if (rr) {
      render_result_views_shallowdelete(rr);
    }
    BLI_rw_mutex_unlock(&re->resultmutex);
  }
}

void RE_AcquireResultImage(Render *re, RenderResult *rr, const int view_id)
{
  memset(rr, 0, sizeof(RenderResult));

  if (re) {
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_READ);

    if (re->result) {
      RenderLayer *rl;
      RenderView *rv;

      rr->rectx = re->result->rectx;
      rr->recty = re->result->recty;

      /* actview view */
      rv = RE_RenderViewGetById(re->result, view_id);
      rr->have_combined = (rv->rectf != NULL);

      rr->rectf = rv->rectf;
      rr->rectz = rv->rectz;
      rr->rect32 = rv->rect32;

      /* active layer */
      rl = render_get_active_layer(re, re->result);

      if (rl) {
        if (rv->rectf == NULL) {
          rr->rectf = RE_RenderLayerGetPass(rl, RE_PASSNAME_COMBINED, rv->name);
        }

        if (rv->rectz == NULL) {
          rr->rectz = RE_RenderLayerGetPass(rl, RE_PASSNAME_Z, rv->name);
        }
      }

      rr->layers = re->result->layers;
      rr->views = re->result->views;

      rr->xof = re->disprect.xmin;
      rr->yof = re->disprect.ymin;

      rr->stamp_data = re->result->stamp_data;
    }
  }
}

void RE_ReleaseResultImage(Render *re)
{
  if (re) {
    BLI_rw_mutex_unlock(&re->resultmutex);
  }
}

void RE_ResultGet32(Render *re, unsigned int *rect)
{
  RenderResult rres;
  const int view_id = BKE_scene_multiview_view_id_get(&re->r, re->viewname);

  RE_AcquireResultImageViews(re, &rres);
  render_result_rect_get_pixels(&rres,
                                rect,
                                re->rectx,
                                re->recty,
                                &re->scene->view_settings,
                                &re->scene->display_settings,
                                view_id);
  RE_ReleaseResultImageViews(re, &rres);
}

void RE_AcquiredResultGet32(Render *re,
                            RenderResult *result,
                            unsigned int *rect,
                            const int view_id)
{
  render_result_rect_get_pixels(result,
                                rect,
                                re->rectx,
                                re->recty,
                                &re->scene->view_settings,
                                &re->scene->display_settings,
                                view_id);
}

RenderStats *RE_GetStats(Render *re)
{
  return &re->i;
}

Render *RE_NewRender(const char *name)
{
  Render *re;

  /* only one render per name exists */
  re = RE_GetRender(name);
  if (re == NULL) {

    /* new render data struct */
    re = MEM_callocN(sizeof(Render), "new render");
    BLI_addtail(&RenderGlobal.renderlist, re);
    BLI_strncpy(re->name, name, RE_MAXNAME);
    BLI_rw_mutex_init(&re->resultmutex);
    BLI_mutex_init(&re->engine_draw_mutex);
    BLI_mutex_init(&re->highlighted_tiles_mutex);
  }

  RE_InitRenderCB(re);

  return re;
}

/* MAX_ID_NAME + sizeof(Library->name) + space + null-terminator. */
#define MAX_SCENE_RENDER_NAME (MAX_ID_NAME + 1024 + 2)

static void scene_render_name_get(const Scene *scene, const size_t max_size, char *render_name)
{
  if (ID_IS_LINKED(scene)) {
    BLI_snprintf(render_name, max_size, "%s %s", scene->id.lib->id.name, scene->id.name);
  }
  else {
    BLI_snprintf(render_name, max_size, "%s", scene->id.name);
  }
}

Render *RE_GetSceneRender(const Scene *scene)
{
  char render_name[MAX_SCENE_RENDER_NAME];
  scene_render_name_get(scene, sizeof(render_name), render_name);
  return RE_GetRender(render_name);
}

Render *RE_NewSceneRender(const Scene *scene)
{
  char render_name[MAX_SCENE_RENDER_NAME];
  scene_render_name_get(scene, sizeof(render_name), render_name);
  return RE_NewRender(render_name);
}

void RE_InitRenderCB(Render *re)
{
  /* set default empty callbacks */
  re->display_init = result_nothing;
  re->display_clear = result_nothing;
  re->display_update = result_rcti_nothing;
  re->current_scene_update = current_scene_nothing;
  re->progress = float_nothing;
  re->test_break = default_break;
  if (G.background) {
    re->stats_draw = stats_background;
  }
  else {
    re->stats_draw = stats_nothing;
  }
  /* clear callback handles */
  re->dih = re->dch = re->duh = re->sdh = re->prh = re->tbh = NULL;
}

void RE_FreeRender(Render *re)
{
  if (re->engine) {
    RE_engine_free(re->engine);
  }

  BLI_rw_mutex_end(&re->resultmutex);
  BLI_mutex_end(&re->engine_draw_mutex);
  BLI_mutex_end(&re->highlighted_tiles_mutex);

  BLI_freelistN(&re->view_layers);
  BLI_freelistN(&re->r.views);

  BKE_curvemapping_free_data(&re->r.mblur_shutter_curve);

  if (re->highlighted_tiles != NULL) {
    BLI_gset_free(re->highlighted_tiles, MEM_freeN);
  }

  /* main dbase can already be invalid now, some database-free code checks it */
  re->main = NULL;
  re->scene = NULL;

  render_result_free(re->result);
  render_result_free(re->pushedresult);

  BLI_remlink(&RenderGlobal.renderlist, re);
  MEM_freeN(re);
}

void RE_FreeAllRender(void)
{
  while (RenderGlobal.renderlist.first) {
    RE_FreeRender(RenderGlobal.renderlist.first);
  }

#ifdef WITH_FREESTYLE
  /* finalize Freestyle */
  FRS_exit();
#endif
}

void RE_FreeAllRenderResults(void)
{
  Render *re;

  for (re = RenderGlobal.renderlist.first; re; re = re->next) {
    render_result_free(re->result);
    render_result_free(re->pushedresult);

    re->result = NULL;
    re->pushedresult = NULL;
  }
}

void RE_FreeAllPersistentData(void)
{
  Render *re;
  for (re = RenderGlobal.renderlist.first; re != NULL; re = re->next) {
    if (re->engine != NULL) {
      BLI_assert(!(re->engine->flag & RE_ENGINE_RENDERING));
      RE_engine_free(re->engine);
      re->engine = NULL;
    }
  }
}

static void re_free_persistent_data(Render *re)
{
  /* If engine is currently rendering, just wait for it to be freed when it finishes rendering. */
  if (re->engine && !(re->engine->flag & RE_ENGINE_RENDERING)) {
    RE_engine_free(re->engine);
    re->engine = NULL;
  }
}

void RE_FreePersistentData(const Scene *scene)
{
  /* Render engines can be kept around for quick re-render, this clears all or one scene. */
  if (scene) {
    Render *re = RE_GetSceneRender(scene);
    if (re) {
      re_free_persistent_data(re);
    }
  }
  else {
    for (Render *re = RenderGlobal.renderlist.first; re; re = re->next) {
      re_free_persistent_data(re);
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Initialize State
 * \{ */

static void re_init_resolution(Render *re, Render *source, int winx, int winy, rcti *disprect)
{
  re->winx = winx;
  re->winy = winy;
  if (source && (source->r.mode & R_BORDER)) {
    /* NOTE(@sergey): doesn't seem original bordered `disprect` is storing anywhere
     * after insertion on black happening in #do_render_engine(),
     * so for now simply re-calculate `disprect` using border from source renderer. */

    re->disprect.xmin = source->r.border.xmin * winx;
    re->disprect.xmax = source->r.border.xmax * winx;

    re->disprect.ymin = source->r.border.ymin * winy;
    re->disprect.ymax = source->r.border.ymax * winy;

    re->rectx = BLI_rcti_size_x(&re->disprect);
    re->recty = BLI_rcti_size_y(&re->disprect);

    /* copy border itself, since it could be used by external engines */
    re->r.border = source->r.border;
  }
  else if (disprect) {
    re->disprect = *disprect;
    re->rectx = BLI_rcti_size_x(&re->disprect);
    re->recty = BLI_rcti_size_y(&re->disprect);
  }
  else {
    re->disprect.xmin = re->disprect.ymin = 0;
    re->disprect.xmax = winx;
    re->disprect.ymax = winy;
    re->rectx = winx;
    re->recty = winy;
  }
}

void render_copy_renderdata(RenderData *to, RenderData *from)
{
  BLI_freelistN(&to->views);
  BKE_curvemapping_free_data(&to->mblur_shutter_curve);

  *to = *from;

  BLI_duplicatelist(&to->views, &from->views);
  BKE_curvemapping_copy_data(&to->mblur_shutter_curve, &from->mblur_shutter_curve);
}

void RE_InitState(Render *re,
                  Render *source,
                  RenderData *rd,
                  ListBase *render_layers,
                  ViewLayer *single_layer,
                  int winx,
                  int winy,
                  rcti *disprect)
{
  bool had_freestyle = (re->r.mode & R_EDGE_FRS) != 0;

  re->ok = true; /* maybe flag */

  re->i.starttime = PIL_check_seconds_timer();

  /* copy render data and render layers for thread safety */
  render_copy_renderdata(&re->r, rd);
  BLI_freelistN(&re->view_layers);
  BLI_duplicatelist(&re->view_layers, render_layers);
  re->active_view_layer = 0;

  if (source) {
    /* reuse border flags from source renderer */
    re->r.mode &= ~(R_BORDER | R_CROP);
    re->r.mode |= source->r.mode & (R_BORDER | R_CROP);

    /* dimensions shall be shared between all renderers */
    re->r.xsch = source->r.xsch;
    re->r.ysch = source->r.ysch;
    re->r.size = source->r.size;
  }

  re_init_resolution(re, source, winx, winy, disprect);

  /* disable border if it's a full render anyway */
  if (re->r.border.xmin == 0.0f && re->r.border.xmax == 1.0f && re->r.border.ymin == 0.0f &&
      re->r.border.ymax == 1.0f) {
    re->r.mode &= ~R_BORDER;
  }

  if (re->rectx < 1 || re->recty < 1 ||
      (BKE_imtype_is_movie(rd->im_format.imtype) && (re->rectx < 16 || re->recty < 16))) {
    BKE_report(re->reports, RPT_ERROR, "Image too small");
    re->ok = 0;
    return;
  }

  if (single_layer) {
    int index = BLI_findindex(render_layers, single_layer);
    if (index != -1) {
      re->active_view_layer = index;
      re->r.scemode |= R_SINGLE_LAYER;
    }
  }

  /* if preview render, we try to keep old result */
  BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);

  if (re->r.scemode & R_BUTS_PREVIEW) {
    if (had_freestyle || (re->r.mode & R_EDGE_FRS)) {
      /* freestyle manipulates render layers so always have to free */
      render_result_free(re->result);
      re->result = NULL;
    }
    else if (re->result) {
      ViewLayer *active_render_layer = BLI_findlink(&re->view_layers, re->active_view_layer);
      RenderLayer *rl;
      bool have_layer = false;

      for (rl = re->result->layers.first; rl; rl = rl->next) {
        if (STREQ(rl->name, active_render_layer->name)) {
          have_layer = true;
        }
      }

      if (re->result->rectx == re->rectx && re->result->recty == re->recty && have_layer) {
        /* keep render result, this avoids flickering black tiles
         * when the preview changes */
      }
      else {
        /* free because resolution changed */
        render_result_free(re->result);
        re->result = NULL;
      }
    }
  }
  else {

    /* make empty render result, so display callbacks can initialize */
    render_result_free(re->result);
    re->result = MEM_callocN(sizeof(RenderResult), "new render result");
    re->result->rectx = re->rectx;
    re->result->recty = re->recty;
    render_result_view_new(re->result, "");
  }

  BLI_rw_mutex_unlock(&re->resultmutex);

  RE_init_threadcount(re);

  RE_point_density_fix_linking();
}

void render_update_anim_renderdata(Render *re, RenderData *rd, ListBase *render_layers)
{
  /* filter */
  re->r.gauss = rd->gauss;

  /* motion blur */
  re->r.blurfac = rd->blurfac;

  /* freestyle */
  re->r.line_thickness_mode = rd->line_thickness_mode;
  re->r.unit_line_thickness = rd->unit_line_thickness;

  /* render layers */
  BLI_freelistN(&re->view_layers);
  BLI_duplicatelist(&re->view_layers, render_layers);

  /* render views */
  BLI_freelistN(&re->r.views);
  BLI_duplicatelist(&re->r.views, &rd->views);
}

void RE_display_init_cb(Render *re, void *handle, void (*f)(void *handle, RenderResult *rr))
{
  re->display_init = f;
  re->dih = handle;
}
void RE_display_clear_cb(Render *re, void *handle, void (*f)(void *handle, RenderResult *rr))
{
  re->display_clear = f;
  re->dch = handle;
}
void RE_display_update_cb(Render *re,
                          void *handle,
                          void (*f)(void *handle, RenderResult *rr, rcti *rect))
{
  re->display_update = f;
  re->duh = handle;
}
void RE_current_scene_update_cb(Render *re, void *handle, void (*f)(void *handle, Scene *scene))
{
  re->current_scene_update = f;
  re->suh = handle;
}
void RE_stats_draw_cb(Render *re, void *handle, void (*f)(void *handle, RenderStats *rs))
{
  re->stats_draw = f;
  re->sdh = handle;
}
void RE_progress_cb(Render *re, void *handle, void (*f)(void *handle, float))
{
  re->progress = f;
  re->prh = handle;
}

void RE_draw_lock_cb(Render *re, void *handle, void (*f)(void *handle, bool lock))
{
  re->draw_lock = f;
  re->dlh = handle;
}

void RE_test_break_cb(Render *re, void *handle, int (*f)(void *handle))
{
  re->test_break = f;
  re->tbh = handle;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name OpenGL Context
 * \{ */

void RE_gl_context_create(Render *re)
{
  /* Needs to be created in the main ogl thread. */
  re->gl_context = WM_opengl_context_create();
  /* So we activate the window's one afterwards. */
  wm_window_reset_drawable();
}

void RE_gl_context_destroy(Render *re)
{
  /* Needs to be called from the thread which used the ogl context for rendering. */
  if (re->gl_context) {
    if (re->gpu_context) {
      WM_opengl_context_activate(re->gl_context);
      GPU_context_active_set(re->gpu_context);
      GPU_context_discard(re->gpu_context);
      re->gpu_context = NULL;
    }

    WM_opengl_context_dispose(re->gl_context);
    re->gl_context = NULL;
  }
}

void *RE_gl_context_get(Render *re)
{
  return re->gl_context;
}

void *RE_gpu_context_get(Render *re)
{
  if (re->gpu_context == NULL) {
    re->gpu_context = GPU_context_create(NULL);
  }
  return re->gpu_context;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Render & Composite Scenes (Implementation & Public API)
 *
 * Main high-level functions defined here are:
 * - #RE_RenderFrame
 * - #RE_RenderAnim
 * \{ */

/* ************  This part uses API, for rendering Blender scenes ********** */

/* make sure disprect is not affected by the render border */
static void render_result_disprect_to_full_resolution(Render *re)
{
  re->disprect.xmin = re->disprect.ymin = 0;
  re->disprect.xmax = re->winx;
  re->disprect.ymax = re->winy;
  re->rectx = re->winx;
  re->recty = re->winy;
}

static void render_result_uncrop(Render *re)
{
  /* when using border render with crop disabled, insert render result into
   * full size with black pixels outside */
  if (re->result && (re->r.mode & R_BORDER)) {
    if ((re->r.mode & R_CROP) == 0) {
      RenderResult *rres;

      /* backup */
      const rcti orig_disprect = re->disprect;
      const int orig_rectx = re->rectx, orig_recty = re->recty;

      BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);

      /* sub-rect for merge call later on */
      re->result->tilerect = re->disprect;

      /* weak is: it chances disprect from border */
      render_result_disprect_to_full_resolution(re);

      rres = render_result_new(re, &re->disprect, RR_ALL_LAYERS, RR_ALL_VIEWS);
      rres->stamp_data = BKE_stamp_data_copy(re->result->stamp_data);

      render_result_clone_passes(re, rres, NULL);
      render_result_passes_allocated_ensure(rres);

      render_result_merge(rres, re->result);
      render_result_free(re->result);
      re->result = rres;

      /* weak... the display callback wants an active renderlayer pointer... */
      re->result->renlay = render_get_active_layer(re, re->result);

      BLI_rw_mutex_unlock(&re->resultmutex);

      re->display_init(re->dih, re->result);
      re->display_update(re->duh, re->result, NULL);

      /* restore the disprect from border */
      re->disprect = orig_disprect;
      re->rectx = orig_rectx;
      re->recty = orig_recty;
    }
    else {
      /* set offset (again) for use in compositor, disprect was manipulated. */
      re->result->xof = 0;
      re->result->yof = 0;
    }
  }
}

/* Render scene into render result, with a render engine. */
static void do_render_engine(Render *re)
{
  Object *camera = RE_GetCamera(re);
  /* also check for camera here */
  if (camera == NULL) {
    BKE_report(re->reports, RPT_ERROR, "Cannot render, no camera");
    G.is_break = true;
    return;
  }

  /* now use renderdata and camera to set viewplane */
  RE_SetCamera(re, camera);

  re->current_scene_update(re->suh, re->scene);
  RE_engine_render(re, false);

  /* when border render, check if we have to insert it in black */
  render_result_uncrop(re);
}

/* Render scene into render result, within a compositor node tree.
 * Uses the same image dimensions, does not recursively perform compositing. */
static void do_render_compositor_scene(Render *re, Scene *sce, int cfra)
{
  Render *resc = RE_NewSceneRender(sce);
  int winx = re->winx, winy = re->winy;

  sce->r.cfra = cfra;

  BKE_scene_camera_switch_update(sce);

  /* exception: scene uses own size (unfinished code) */
  if (0) {
    winx = (sce->r.size * sce->r.xsch) / 100;
    winy = (sce->r.size * sce->r.ysch) / 100;
  }

  /* initial setup */
  RE_InitState(resc, re, &sce->r, &sce->view_layers, NULL, winx, winy, &re->disprect);

  /* We still want to use 'rendercache' setting from org (main) scene... */
  resc->r.scemode = (resc->r.scemode & ~R_EXR_CACHE_FILE) | (re->r.scemode & R_EXR_CACHE_FILE);

  /* still unsure entity this... */
  resc->main = re->main;
  resc->scene = sce;

  /* copy callbacks */
  resc->display_update = re->display_update;
  resc->duh = re->duh;
  resc->test_break = re->test_break;
  resc->tbh = re->tbh;
  resc->stats_draw = re->stats_draw;
  resc->sdh = re->sdh;
  resc->current_scene_update = re->current_scene_update;
  resc->suh = re->suh;

  do_render_engine(resc);
}

/* helper call to detect if this scene needs a render,
 * or if there's a any render layer to render. */
static int compositor_needs_render(Scene *sce, int this_scene)
{
  bNodeTree *ntree = sce->nodetree;
  bNode *node;

  if (ntree == NULL) {
    return 1;
  }
  if (sce->use_nodes == false) {
    return 1;
  }
  if ((sce->r.scemode & R_DOCOMP) == 0) {
    return 1;
  }

  for (node = ntree->nodes.first; node; node = node->next) {
    if (node->type == CMP_NODE_R_LAYERS && (node->flag & NODE_MUTED) == 0) {
      if (this_scene == 0 || node->id == NULL || node->id == &sce->id) {
        return 1;
      }
    }
  }
  return 0;
}

/* Render all scenes within a compositor node tree. */
static void do_render_compositor_scenes(Render *re)
{
  bNode *node;
  int cfra = re->scene->r.cfra;
  Scene *restore_scene = re->scene;

  if (re->scene->nodetree == NULL) {
    return;
  }

  bool changed_scene = false;

  /* now foreach render-result node we do a full render */
  /* results are stored in a way compositor will find it */
  GSet *scenes_rendered = BLI_gset_ptr_new(__func__);
  for (node = re->scene->nodetree->nodes.first; node; node = node->next) {
    if (node->type == CMP_NODE_R_LAYERS && (node->flag & NODE_MUTED) == 0) {
      if (node->id && node->id != (ID *)re->scene) {
        Scene *scene = (Scene *)node->id;
        if (!BLI_gset_haskey(scenes_rendered, scene) &&
            render_scene_has_layers_to_render(scene, false)) {
          do_render_compositor_scene(re, scene, cfra);
          BLI_gset_add(scenes_rendered, scene);
          node->typeinfo->updatefunc(restore_scene->nodetree, node);

          if (scene != re->scene) {
            changed_scene = true;
          }
        }
      }
    }
  }
  BLI_gset_free(scenes_rendered, NULL);

  if (changed_scene) {
    /* If rendered another scene, switch back to the current scene with compositing nodes. */
    re->current_scene_update(re->suh, re->scene);
  }
}

/* bad call... need to think over proper method still */
static void render_compositor_stats(void *arg, const char *str)
{
  Render *re = (Render *)arg;

  RenderStats i;
  memcpy(&i, &re->i, sizeof(i));
  i.infostr = str;
  re->stats_draw(re->sdh, &i);
}

/* Render compositor nodes, along with any scenes required for them.
 * The result will be output into a compositing render layer in the render result. */
static void do_render_compositor(Render *re)
{
  bNodeTree *ntree = re->pipeline_scene_eval->nodetree;
  int update_newframe = 0;

  if (compositor_needs_render(re->pipeline_scene_eval, 1)) {
    /* save memory... free all cached images */
    ntreeFreeCache(ntree);

    /* render the frames
     * it could be optimized to render only the needed view
     * but what if a scene has a different number of views
     * than the main scene? */
    do_render_engine(re);
  }
  else {
    re->i.cfra = re->r.cfra;

    /* ensure new result gets added, like for regular renders */
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);

    render_result_free(re->result);
    if ((re->r.mode & R_CROP) == 0) {
      render_result_disprect_to_full_resolution(re);
    }
    re->result = render_result_new(re, &re->disprect, RR_ALL_LAYERS, RR_ALL_VIEWS);

    BLI_rw_mutex_unlock(&re->resultmutex);

    /* scene render process already updates animsys */
    update_newframe = 1;
  }

  /* swap render result */
  if (re->r.scemode & R_SINGLE_LAYER) {
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
    render_result_single_layer_end(re);
    BLI_rw_mutex_unlock(&re->resultmutex);
  }

  if (!re->test_break(re->tbh)) {

    if (ntree) {
      ntreeCompositTagRender(re->pipeline_scene_eval);
    }

    if (ntree && re->scene->use_nodes && re->r.scemode & R_DOCOMP) {
      /* checks if there are render-result nodes that need scene */
      if ((re->r.scemode & R_SINGLE_LAYER) == 0) {
        do_render_compositor_scenes(re);
      }

      if (!re->test_break(re->tbh)) {
        ntree->stats_draw = render_compositor_stats;
        ntree->test_break = re->test_break;
        ntree->progress = re->progress;
        ntree->sdh = re;
        ntree->tbh = re->tbh;
        ntree->prh = re->prh;

        if (update_newframe) {
          /* If we have consistent depsgraph now would be a time to update them. */
        }

        RenderView *rv;
        for (rv = re->result->views.first; rv; rv = rv->next) {
          ntreeCompositExecTree(
              re->pipeline_scene_eval, ntree, &re->r, true, G.background == 0, rv->name);
        }

        ntree->stats_draw = NULL;
        ntree->test_break = NULL;
        ntree->progress = NULL;
        ntree->tbh = ntree->sdh = ntree->prh = NULL;
      }
    }
  }

  /* weak... the display callback wants an active renderlayer pointer... */
  if (re->result != NULL) {
    re->result->renlay = render_get_active_layer(re, re->result);
    re->display_update(re->duh, re->result, NULL);
  }
}

static void renderresult_stampinfo(Render *re)
{
  RenderResult rres;
  RenderView *rv;
  int nr;

  /* this is the basic trick to get the displayed float or char rect from render result */
  nr = 0;
  for (rv = re->result->views.first; rv; rv = rv->next, nr++) {
    RE_SetActiveRenderView(re, rv->name);
    RE_AcquireResultImage(re, &rres, nr);

    Object *ob_camera_eval = DEG_get_evaluated_object(re->pipeline_depsgraph, RE_GetCamera(re));
    BKE_image_stamp_buf(re->scene,
                        ob_camera_eval,
                        (re->r.stamp & R_STAMP_STRIPMETA) ? rres.stamp_data : NULL,
                        (unsigned char *)rres.rect32,
                        rres.rectf,
                        rres.rectx,
                        rres.recty,
                        4);
    RE_ReleaseResultImage(re);
  }
}
