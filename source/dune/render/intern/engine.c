
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lang_translation.h"

#include "lib_ghash.h"
#include "lib_listbase.h"
#include "lib_math_bits.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "types_object.h"

#include "dune_camera.h"
#include "dune_colortools.h"
#include "dune_global.h"
#include "dune_layer.h"
#include "dune_node.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.h"
#include "graph_debug.h"
#include "graph_query.h"

#include "api_access.h"

#include "render_bake.h"
#include "render_engine.h"
#include "render_pipeline.h"

#include "draw_engine.h"

#include "pipeline.h"
#include "render_result.h"
#include "render_types.h"

/* Render Engine Types */

ListBase R_engines = {NULL, NULL};

void render_engines_init(void)
{
  draw_engines_register();
}

void render_engines_init_experimental()
{
  draw_engines_register_experimental();
}

void render_engines_exit(void)
{
  RenderEngineType *type, *next;

  draw_engines_free();

  for (type = R_engines.first; type; type = next) {
    next = type->next;

    lib_remlink(&R_engines, type);

    if (!(type->flag & RE_INTERNAL)) {
      if (type->api_ext.free) {
        type->api_ext.free(type->api_ext.data);
      }

      mem_freen(type);
    }
  }
}

void render_engines_register(RenderEngineType *render_type)
{
  if (render_type->draw_engine) {
    draw_engine_register(render_type->draw_engine);
  }
  lib_addtail(&R_engines, render_type);
}

RenderEngineType *render_engines_find(const char *idname)
{
  RenderEngineType *type;

  type = lib_findstring(&R_engines, idname, offsetof(RenderEngineType, idname));
  if (!type) {
    type = lib_findstring(&R_engines, "DUNE_EEVEE", offsetof(RenderEngineType, idname));
  }

  return type;
}

bool render_engine_is_external(const Render *re)
{
  return (re->engine && re->engine->type && re->engine->type->render);
}

bool render_engine_is_opengl(RenderEngineType *render_type)
{
  /* TODO: refine? Can we have ogl render engine without ogl render pipeline? */
  return (render_type->draw_engine != NULL) && draw_engine_render_support(render_type->draw_engine);
}

bool render_engine_supports_alembic_procedural(const RenderEngineType *render_type, Scene *scene)
{
  if ((render_type->flag & RENDER_USE_ALEMBIC_PROCEDURAL) == 0) {
    return false;
  }

  if (dune_scene_uses_cycles(scene) && !BKE_scene_uses_cycles_experimental_features(scene)) {
    return false;
  }

  return true;
}

/* Create, Free */

RenderEngine *render_engine_create(RenderEngineType *type)
{
  RenderEngine *engine = mem_callocn(sizeof(RenderEngine), "RenderEngine");
  engine->type = type;

  lib_mutex_init(&engine->update_render_passes_mutex);

  return engine;
}

static void engine_graph_free(RenderEngine *engine)
{
  if (engine->graph) {
    /* Need GPU context since this might free GPU buffers. */
    const bool use_gpu_ctx = (engine->type->flag & RE_USE_GPU_CONTEXT);
    if (use_gpu_ctx) {
      render_ctx_enable(engine->re);
    }

    graph_free(engine->graph);
    engine->graph = NULL;

    if (use_gpu_ctx) {
      draw_render_ctx_disable(engine->re);
    }
  }
}

void render_engine_free(RenderEngine *engine)
{
  engine_graph_free(engine);

  lib_mutex_end(&engine->update_render_passes_mutex);

  mem_freen(engine);
}

/* Bake Render Results */

static RenderResult *render_result_from_bake(RenderEngine *engine, int x, int y, int w, int h)
{
  /* Create render result with specified size. */
  RenderResult *rr = mem_callocn(sizeof(RenderResult), __func__);

  rr->rectx = w;
  rr->recty = h;
  rr->tilerect.xmin = x;
  rr->tilerect.ymin = y;
  rr->tilerect.xmax = x + w;
  rr->tilerect.ymax = y + h;

  /* Add single baking render layer. */
  RenderLayer *rl = mem_callocn(sizeof(RenderLayer), "bake render layer");
  rl->rectx = w;
  rl->recty = h;
  lib_addtail(&rr->layers, rl);

  /* Add render passes. */
  render_layer_add_pass(rr, rl, engine->bake.depth, RE_PASSNAME_COMBINED, "", "RGBA", true);

  RenderPass *primitive_pass = render_layer_add_pass(rr, rl, 4, "BakePrimitive", "", "RGBA", true);
  RenderPass *differential_pass = render_layer_add_pass(
      rr, rl, 4, "BakeDifferential", "", "RGBA", true);

  /* Fill render passes from bake pixel array, to be read by the render engine. */
  for (int ty = 0; ty < h; ty++) {
    size_t offset = ty * w * 4;
    float *primitive = primitive_pass->rect + offset;
    float *differential = differential_pass->rect + offset;

    size_t bake_offset = (y + ty) * engine->bake.width + x;
    const BakePixel *bake_pixel = engine->bake.pixels + bake_offset;

    for (int tx = 0; tx < w; tx++) {
      if (bake_pixel->object_id != engine->bake.object_id) {
        primitive[0] = int_as_float(-1);
        primitive[1] = int_as_float(-1);
      }
      else {
        primitive[0] = int_as_float(bake_pixel->seed);
        primitive[1] = int_as_float(bake_pixel->primitive_id);
        primitive[2] = bake_pixel->uv[0];
        primitive[3] = bake_pixel->uv[1];

        differential[0] = bake_pixel->du_dx;
        differential[1] = bake_pixel->du_dy;
        differential[2] = bake_pixel->dv_dx;
        differential[3] = bake_pixel->dv_dy;
      }

      primitive += 4;
      differential += 4;
      bake_pixel++;
    }
  }

  return rr;
}

static void render_result_to_bake(RenderEngine *engine, RenderResult *rr)
{
  RenderPass *rpass = render_pass_find_by_name(rr->layers.first, RENDER_PASSNAME_COMBINED, "");

  if (!rpass) {
    return;
  }

  /* Copy from tile render result to full image bake result. Just the pixels for the
   * object currently being baked, to preserve other objects when baking multiple. */
  const int x = rr->tilerect.xmin;
  const int y = rr->tilerect.ymin;
  const int w = rr->tilerect.xmax - rr->tilerect.xmin;
  const int h = rr->tilerect.ymax - rr->tilerect.ymin;
  const size_t pixel_depth = engine->bake.depth;
  const size_t pixel_size = pixel_depth * sizeof(float);

  for (int ty = 0; ty < h; ty++) {
    const size_t offset = ty * w;
    const size_t bake_offset = (y + ty) * engine->bake.width + x;

    const float *pass_rect = rpass->rect + offset * pixel_depth;
    const BakePixel *bake_pixel = engine->bake.pixels + bake_offset;
    float *bake_result = engine->bake.result + bake_offset * pixel_depth;

    for (int tx = 0; tx < w; tx++) {
      if (bake_pixel->object_id == engine->bake.object_id) {
        memcpy(bake_result, pass_rect, pixel_size);
      }
      pass_rect += pixel_depth;
      bake_result += pixel_depth;
      bake_pixel++;
    }
  }
}

/* Render Results */

static HighlightedTile highlighted_tile_from_result_get(Render *UNUSED(re), RenderResult *result)
{
  HighlightedTile tile;
  tile.rect = result->tilerect;

  return tile;
}

static void engine_tile_highlight_set(RenderEngine *engine,
                                      const HighlightedTile *tile,
                                      bool highlight)
{
  if ((engine->flag & RENDER_ENGINE_HIGHLIGHT_TILES) == 0) {
    return;
  }

  Render *re = engine->re;

  lib_mutex_lock(&re->highlighted_tiles_mutex);

  if (re->highlighted_tiles == NULL) {
    re->highlighted_tiles = lib_gset_new(
        lib_ghashutil_inthash_v4_p, lib_ghashutil_inthash_v4_cmp, "highlighted tiles");
  }

  if (highlight) {
    HighlightedTile **tile_in_set;
    if (!lib_gset_ensure_p_ex(re->highlighted_tiles, tile, (void ***)&tile_in_set)) {
      *tile_in_set = mem_mallocn(sizeof(HighlightedTile), __func__);
      **tile_in_set = *tile;
    }
  }
  else {
    lib_gset_remove(re->highlighted_tiles, tile, mem_freen);
  }

  lib_mutex_unlock(&re->highlighted_tiles_mutex);
}

RenderResult *render_engine_begin_result(
    RenderEngine *engine, int x, int y, int w, int h, const char *layername, const char *viewname)
{
  if (engine->bake.pixels) {
    RenderResult *result = render_result_from_bake(engine, x, y, w, h);
    lib_addtail(&engine->fullresult, result);
    return result;
  }

  Render *re = engine->re;
  RenderResult *result;
  rcti disprect;

  /* ensure the coordinates are within the right limits */
  CLAMP(x, 0, re->result->rectx);
  CLAMP(y, 0, re->result->recty);
  CLAMP(w, 0, re->result->rectx);
  CLAMP(h, 0, re->result->recty);

  if (x + w > re->result->rectx) {
    w = re->result->rectx - x;
  }
  if (y + h > re->result->recty) {
    h = re->result->recty - y;
  }

  /* allocate a render result */
  disprect.xmin = x;
  disprect.xmax = x + w;
  disprect.ymin = y;
  disprect.ymax = y + h;

  result = render_result_new(re, &disprect, layername, viewname);

  /* TODO: make this thread safe. */

  /* can be NULL if we CLAMP the width or height to 0 */
  if (result) {
    render_result_clone_passes(re, result, viewname);
    render_result_passes_allocated_ensure(result);

    lib_addtail(&engine->fullresult, result);

    result->tilerect.xmin += re->disprect.xmin;
    result->tilerect.xmax += re->disprect.xmin;
    result->tilerect.ymin += re->disprect.ymin;
    result->tilerect.ymax += re->disprect.ymin;
  }

  return result;
}

static void re_ensure_passes_allocated_thread_safe(Render *re)
{
  if (!re->result->passes_allocated) {
    lib_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
    if (!re->result->passes_allocated) {
      render_result_passes_allocated_ensure(re->result);
    }
    lib_rw_mutex_unlock(&re->resultmutex);
  }
}

void render_engine_update_result(RenderEngine *engine, RenderResult *result)
{
  if (engine->bake.pixels) {
    /* No interactive baking updates for now. */
    return;
  }

  Render *re = engine->re;

  if (result) {
    re_ensure_passes_allocated_thread_safe(re);
    render_result_merge(re->result, result);
    result->renlay = result->layers.first; /* weak, draws first layer always */
    re->display_update(re->duh, result, NULL);
  }
}

void render_engine_add_pass(RenderEngine *engine,
                            const char *name,
                            int channels,
                            const char *chan_id,
                            const char *layername)
{
  Render *re = engine->re;

  if (!re || !re->result) {
    return;
  }

  render_create_render_pass(re->result, name, channels, chan_id, layername, NULL, false);
}

void render_engine_end_result(
    RenderEngine *engine, RenderResult *result, bool cancel, bool highlight, bool merge_results)
{
  Render *re = engine->re;

  if (!result) {
    return;
  }

  if (engine->bake.pixels) {
    render_result_to_bake(engine, result);
    lib_remlink(&engine->fullresult, result);
    render_result_free(result);
    return;
  }
  if (re->engine && (re->engine->flag & RENDER_ENGINE_HIGHLIGHT_TILES)) {
    const HighlightedTile tile = highlighted_tile_from_result_get(re, result);

    engine_tile_highlight_set(engine, &tile, highlight);
  }

  if (!cancel || merge_results) {
    if (!(re->test_break(re->tbh) && (re->r.scemode & R_BUTS_PREVIEW))) {
      re_ensure_passes_allocated_thread_safe(re);
      render_result_merge(re->result, result);
    }

    /* draw */
    if (!re->test_break(re->tbh)) {
      result->renlay = result->layers.first; /* weak, draws first layer always */
      re->display_update(re->duh, result, NULL);
    }
  }

  /* free */
  lib_remlink(&engine->fullresult, result);
  render_result_free(result);
}

RenderResult *render_engine_get_result(RenderEngine *engine)
{
  return engine->re->result;
}

/* Cancel */
bool render_engine_test_break(RenderEngine *engine)
{
  Render *re = engine->re;

  if (re) {
    return re->test_break(re->tbh);
  }

  return 0;
}

/* Statistics */

void render_engine_update_stats(RenderEngine *engine, const char *stats, const char *info)
{
  Render *re = engine->re;

  /* stats draw callback */
  if (re) {
    re->i.statstr = stats;
    re->i.infostr = info;
    re->stats_draw(re->sdh, &re->i);
    re->i.infostr = NULL;
    re->i.statstr = NULL;
  }

  /* set engine text */
  engine->text[0] = '\0';

  if (stats && stats[0] && info && info[0]) {
    lib_snprintf(engine->text, sizeof(engine->text), "%s | %s", stats, info);
  }
  else if (info && info[0]) {
    lib_strncpy(engine->text, info, sizeof(engine->text));
  }
  else if (stats && stats[0]) {
    lib_strncpy(engine->text, stats, sizeof(engine->text));
  }
}

void render_engine_update_progress(RenderEngine *engine, float progress)
{
  Render *re = engine->re;

  if (re) {
    CLAMP(progress, 0.0f, 1.0f);
    re->progress(re->prh, progress);
  }
}

void render_engine_update_memory_stats(RenderEngine *engine, float mem_used, float mem_peak)
{
  Render *re = engine->re;

  if (re) {
    re->i.mem_used = mem_used;
    re->i.mem_peak = mem_peak;
  }
}

void render_engine_report(RenderEngine *engine, int type, const char *msg)
{
  Render *re = engine->re;

  if (re) {
    dune_report(engine->re->reports, type, msg);
  }
  else if (engine->reports) {
    dune_report(engine->reports, type, msg);
  }
}

void render_engine_set_error_message(RenderEngine *engine, const char *msg)
{
  Render *re = engine->re;
  if (re != NULL) {
    RenderResult *rr = render_AcquireResultRead(re);
    if (rr) {
      if (rr->error != NULL) {
        mem_freen(rr->error);
      }
      rr->error = lib_strdup(msg);
    }
    render_ReleaseResult(re);
  }
}

RenderPass *render_engine_pass_by_index_get(RenderEngine *engine, const char *layer_name, int index)
{
  Render *re = engine->re;
  if (re == NULL) {
    return NULL;
  }

  RenderPass *pass = NULL;

  RenderResult *rr = render_AcquireResultRead(re);
  if (rr != NULL) {
    const RenderLayer *layer = render_GetRenderLayer(rr, layer_name);
    if (layer != NULL) {
      pass = lib_findlink(&layer->passes, index);
    }
  }
  render_ReleaseResult(re);

  return pass;
}

const char *render_engine_active_view_get(RenderEngine *engine)
{
  Render *re = engine->re;
  return render_GetActiveRenderView(re);
}

void render_engine_active_view_set(RenderEngine *engine, const char *viewname)
{
  Render *re = engine->re;
  render_SetActiveRenderView(re, viewname);
}

float render_engine_get_camera_shift_x(RenderEngine *engine, Object *camera, bool use_spherical_stereo)
{
  /* When using spherical stereo, get camera shift without multiview,
   * leaving stereo to be handled by the engine. */
  Render *re = engine->re;
  if (use_spherical_stereo || re == NULL) {
    return dune_camera_multiview_shift_x(NULL, camera, NULL);
  }

  return dune_camera_multiview_shift_x(&re->r, camera, re->viewname);
}

void dune_engine_get_camera_model_matrix(RenderEngine *engine,
                                         Object *camera,
                                         bool use_spherical_stereo,
                                         float r_modelmat[16])
{
  /* When using spherical stereo, get model matrix without multiview,
   * leaving stereo to be handled by the engine. */
  Render *re = engine->re;
  if (use_spherical_stereo || re == NULL) {
    dune_camera_multiview_model_matrix(NULL, camera, NULL, (float(*)[4])r_modelmat);
  }
  else {
    dune_camera_multiview_model_matrix(&re->r, camera, re->viewname, (float(*)[4])r_modelmat);
  }
}

bool dune_engine_get_spherical_stereo(RenderEngine *engine, Object *camera)
{
  Render *re = engine->re;
  return dune_camera_multiview_spherical_stereo(re ? &re->r : NULL, camera) ? 1 : 0;
}

rcti *render_engine_get_current_tiles(Render *re, int *r_total_tiles, bool *r_needs_free)
{
  static rcti tiles_static[DUNE_MAX_THREADS];
  const int allocation_step = DUNE_MAX_THREADS;
  int total_tiles = 0;
  rcti *tiles = tiles_static;
  int allocation_size = DUNE_MAX_THREADS;

  lib_mutex_lock(&re->highlighted_tiles_mutex);

  *r_needs_free = false;

  if (re->highlighted_tiles == NULL) {
    *r_total_tiles = 0;
    lib_mutex_unlock(&re->highlighted_tiles_mutex);
    return NULL;
  }

  GSET_FOREACH_BEGIN (HighlightedTile *, tile, re->highlighted_tiles) {
    if (total_tiles >= allocation_size) {
      /* Just in case we're using crazy network rendering with more
       * workers than DUNE_MAX_THREADS.
       */
      allocation_size += allocation_step;
      if (tiles == tiles_static) {
        /* Can not realloc yet, tiles are pointing to a
         * stack memory.
         */
        tiles = mem_mallocn(allocation_size * sizeof(rcti), "current engine tiles");
      }
      else {
        tiles = mem_reallocn(tiles, allocation_size * sizeof(rcti));
      }
      *r_needs_free = true;
    }
    tiles[total_tiles] = tile->rect;

    total_tiles++;
  }
  GSET_FOREACH_END();

  lib_mutex_unlock(&re->highlighted_tiles_mutex);

  *r_total_tiles = total_tiles;

  return tiles;
}

RenderData *render_engine_get_render_data(Render *re)
{
  return &re->r;
}

bool render_engine_use_persistent_data(RenderEngine *engine)
{
  /* Re-rendering is not supported with GPU contexts, since the GPU context
   * is destroyed when the render thread exists. */
  return (engine->re->r.mode & R_PERSISTENT_DATA) && !(engine->type->flag & RENDER_USE_GPU_CONTEXT);
}

static bool engine_keep_graph(RenderEngine *engine)
{
  /* For persistent data or GPU engines like Eevee, reuse the depsgraph between
   * view layers and animation frames. For renderers like Cycles that create
   * their own copy of the scene, persistent data must be explicitly enabled to
   * keep memory usage low by default. */
  return (engine->re->r.mode & R_PERSISTENT_DATA) || (engine->type->flag & RE_USE_GPU_CONTEXT);
}

/* Graph */
static void engine_graph_init(RenderEngine *engine, ViewLayer *view_layer)
{
  Main *main = engine->re->main;
  Scene *scene = engine->re->scene;
  bool reuse_graph = false;

  /* Reuse graph from persistent data if possible. */
  if (engine->graph) {
    if (graph_get_main(engine->graph) != main ||
        graph_get_input_scene(engine->graph) != scene) {
      /* If main or scene changes, we need a completely new graph. */
      engine_graph_free(engine);
    }
    else if (graph_get_input_view_layer(engine->graph) != view_layer) {
      /* If only view layer changed, reuse depsgraph in the hope of reusing
       * objects shared between view layers. */
      graph_replace_owners(engine->graph, main, scene, view_layer);
      graph_tag_relations_update(engine->graph);
    }

    reuse_graph = true;
  }

  if (!engine->graph) {
    /* Ensure we only use persistent data for one scene / view layer at a time,
     * to avoid excessive memory usage. */
    render_FreePersistentData(NULL);

    /* Create new graph if not cached with persistent data. */
    engine->graph = graph_new(main, scene, view_layer, DAG_EVAL_RENDER);
    graph_debug_name_set(engine->graph, "RENDER");
  }

  if (engine->re->r.scemode & R_BUTS_PREVIEW) {
    /* Update for preview render. */
    Graph *graph = engine->graph;
    graph_relations_update(graph);

    /* Need gpu context since this might free GPU buffers. */
    const bool use_gpu_ctx = (engine->type->flag & RENDER_USE_GPU_CTX) && reuse_depsgraph;
    if (use_gpu_ctx) {
      draw_render_ctx_enable(engine->re);
    }

    graph_evaluate_on_framechange(graph, dune_scene_frame_get(scene));

    if (use_gpu_ctx) {
      draw_render_ctx_disable(engine->re);
    }
  }
  else {
    /* Go through update with full Python callbacks for regular render. */
    dune_scene_graph_update_for_newframe_ex(engine->graph, false);
  }

  engine->has_pen = draw_render_check_grease_pencil(engine->graph);
}

static void engine_graph_exit(RenderEngine *engine)
{
  if (engine->graph) {
    if (engine_keep_graph(engine)) {
      /* Clear recalc flags since the engine should have handled the updates for the currently
       * rendered framed by now. */
      graph_ids_clear_recalc(engine->graph, false);
    }
    else {
      /* Free immediately to save memory. */
      engine_graph_free(engine);
    }
  }
}

void render_engine_frame_set(RenderEngine *engine, int frame, float subframe)
{
  if (!engine->graph) {
    return;
  }

  /* Clear recalc flags before update so engine can detect what changed. */
  graph_ids_clear_recalc(engine->graph, false);

  Render *re = engine->re;
  double cfra = (double)frame + (double)subframe;

  CLAMP(cfra, MINAFRAME, MAXFRAME);
  dune_scene_frame_set(re->scene, cfra);
  dune_scene_graph_update_for_newframe_ex(engine->depsgraph, false);

  dune_scene_camera_switch_update(re->scene);
}

/* Bake */

void render_bake_engine_set_engine_parameters(Render *re, Main *bmain, Scene *scene)
{
  re->scene = scene;
  re->main = main;
  render_copy_renderdata(&re->r, &scene->r);
}

bool render_bake_has_engine(const Render *re)
{
  const RenderEngineType *type = render_engines_find(re->r.engine);
  return (type->bake != NULL);
}

bool render_bake_engine(Render *re,
                    Graph *graph,
                    Object *object,
                    const int object_id,
                    const BakePixel pixel_array[],
                    const BakeTargets *targets,
                    const eScenePassType pass_type,
                    const int pass_filter,
                    float result[])
{
  RenderEngineType *type = render_engines_find(re->r.engine);
  RenderEngine *engine;

  /* set render info */
  re->i.cfra = re->scene->r.cfra;
  lib_strncpy(re->i.scene_name, re->scene->id.name + 2, sizeof(re->i.scene_name) - 2);

  /* render */
  engine = re->engine;

  if (!engine) {
    engine = render_engine_create(type);
    re->engine = engine;
  }

  engine->flag |= RENDER_ENGINE_RENDERING;

  /* TODO: actually link to a parent which shouldn't happen */
  engine->re = re;

  engine->resolution_x = re->winx;
  engine->resolution_y = re->winy;

  if (type->bake) {
    engine->graph = graph;

    /* update is only called so we create the engine.session */
    if (type->update) {
      type->update(engine, re->main, engine->depsgraph);
    }

    for (int i = 0; i < targets->num_images; i++) {
      const BakeImage *image = targets->images + i;

      engine->bake.pixels = pixel_array + image->offset;
      engine->bake.result = result + image->offset * targets->num_channels;
      engine->bake.width = image->width;
      engine->bake.height = image->height;
      engine->bake.depth = targets->num_channels;
      engine->bake.object_id = object_id;

      type->bake(
          engine, engine->graph, object, pass_type, pass_filter, image->width, image->height);

      memset(&engine->bake, 0, sizeof(engine->bake));
    }

    engine->graph = NULL;
  }

  engine->flag &= ~RE_ENGINE_RENDERING;

  engine_graph_free(engine);

  render_engine_free(engine);
  re->engine = NULL;

  if (dune_reports_contain(re->reports, RPT_ERROR)) {
    G.is_break = true;
  }

  return true;
}

/* Render */

static void engine_render_view_layer(Render *re,
                                     RenderEngine *engine,
                                     ViewLayer *view_layer_iter,
                                     const bool use_engine,
                                     const bool use_pen)
{
  /* Lock UI so scene can't be edited while we read from it in this render thread. */
  if (re->draw_lock) {
    re->draw_lock(re->dlh, true);
  }

  /* Create graph with scene evaluated at render resolution. */
  ViewLayer *view_layer = lib_findstring(
      &re->scene->view_layers, view_layer_iter->name, offsetof(ViewLayer, name));
  engine_graph_init(engine, view_layer);

  /* Sync data to engine, within draw lock so scene data can be accessed safely. */
  if (use_engine) {
    if (engine->type->update) {
      engine->type->update(engine, re->main, engine->depsgraph);
    }
  }

  if (re->draw_lock) {
    re->draw_lock(re->dlh, false);
  }

  /* Perform render with engine. */
  if (use_engine) {
    const bool use_gpu_ctx = (engine->type->flag & RE_USE_GPU_CONTEXT);
    if (use_gpu_ctx) {
      draw_render_ctx_enable(engine->re);
    }

    lib_mutex_lock(&engine->re->engine_draw_mutex);
    re->engine->flag |= RENDER_ENGINE_CAN_DRAW;
    lib_mutex_unlock(&engine->re->engine_draw_mutex);

    engine->type->render(engine, engine->graph);

    lib_mutex_lock(&engine->re->engine_draw_mutex);
    re->engine->flag &= ~RENDER_ENGINE_CAN_DRAW;
    lib_mutex_unlock(&engine->re->engine_draw_mutex);

    if (use_gpu_context) {
      draw_render_ctx_disable(engine->re);
    }
  }

  /* Optionally composite grease pencil over render result.
   * Only do it if the passes are allocated (and the engine will not override the grease pencil
   * when reading its result from EXR file and writing to the Dune side. */
  if (engine->has_grease_pencil && use_grease_pencil && re->result->passes_allocated) {
    /* NOTE: External engine might have been requested to free its
     * dependency graph, which is only allowed if there is no grease
     * pencil (pipeline is taking care of that). */
    if (!render_engine_test_break(engine) && engine->graph != NULL) {
      draw_render_pen(engine, engine->depsgraph);
    }
  }

  /* Free dependency graph, if engine has not done it already. */
  engine_graph_exit(engine);
}

bool render_engine_render(Render *re, bool do_all)
{
  RenderEngineType *type = render_engines_find(re->r.engine);

  /* verify if we can render */
  if (!type->render) {
    return false;
  }
  if ((re->r.scemode & R_BUTS_PREVIEW) && !(type->flag & RE_USE_PREVIEW)) {
    return false;
  }
  if (do_all && !(type->flag & RE_USE_POSTPROCESS)) {
    return false;
  }
  if (!do_all && (type->flag & RE_USE_POSTPROCESS)) {
    return false;
  }

  /* Lock drawing in UI during data phase. */
  if (re->draw_lock) {
    re->draw_lock(re->dlh, true);
  }

  /* update animation here so any render layer animation is applied before
   * creating the render result */
  if ((re->r.scemode & (R_NO_FRAME_UPDATE | R_BUTS_PREVIEW)) == 0) {
    render_update_anim_renderdata(re, &re->scene->r, &re->scene->view_layers);
  }

  /* create render result */
  lib_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
  if (re->result == NULL || !(re->r.scemode & R_BUTS_PREVIEW)) {
    if (re->result) {
      render_result_free(re->result);
    }

    re->result = render_result_new(re, &re->disprect, RR_ALL_LAYERS, RR_ALL_VIEWS);
  }
  lib_rw_mutex_unlock(&re->resultmutex);

  if (re->result == NULL) {
    /* Clear UI drawing locks. */
    if (re->draw_lock) {
      re->draw_lock(re->dlh, false);
    }
    /* Too small image is handled earlier, here it could only happen if
     * there was no sufficient memory to allocate all passes.
     */
    dune_report(re->reports, RPT_ERROR, "Failed allocate render result, out of memory");
    G.is_break = true;
    return true;
  }

  /* set render info */
  re->i.cfra = re->scene->r.cfra;
  lib_strncpy(re->i.scene_name, re->scene->id.name + 2, sizeof(re->i.scene_name));

  /* render */
  RenderEngine *engine = re->engine;

  if (!engine) {
    engine = render_engine_create(type);
    re->engine = engine;
  }

  engine->flag |= RE_ENGINE_RENDERING;

  /* TODO: actually link to a parent which shouldn't happen */
  engine->re = re;

  if (re->flag & R_ANIMATION) {
    engine->flag |= RE_ENGINE_ANIMATION;
  }
  if (re->r.scemode & R_BUTS_PREVIEW) {
    engine->flag |= RE_ENGINE_PREVIEW;
  }
  engine->camera_override = re->camera_override;

  engine->resolution_x = re->winx;
  engine->resolution_y = re->winy;

  /* Clear UI drawing locks. */
  if (re->draw_lock) {
    re->draw_lock(re->dlh, false);
  }

  /* Render view layers. */
  bool delay_pen = false;

  if (type->render) {
    FOREACH_VIEW_LAYER_TO_RENDER_BEGIN (re, view_layer_iter) {
      engine_render_view_layer(re, engine, view_layer_iter, true, true);

      /* If render passes are not allocated the render engine deferred final pixels write for
       * later. Need to defer the dune pen for until after the engine has written the
       * render result to Dune. */
      delay_pen = engine->has_pen && !re->result->passes_allocated;

      if (render_engine_test_break(engine)) {
        break;
      }
    }
    FOREACH_VIEW_LAYER_TO_RENDER_END;
  }

  if (type->render_frame_finish) {
    type->render_frame_finish(engine);
  }

  /* Perform delayed pen rendering. */
  if (delay_pen) {
    FOREACH_VIEW_LAYER_TO_RENDER_BEGIN (re, view_layer_iter) {
      engine_render_view_layer(re, engine, view_layer_iter, false, true);
      if (render_engine_test_break(engine)) {
        break;
      }
    }
    FOREACH_VIEW_LAYER_TO_RENDER_END;
  }

  /* Clear tile data */
  engine->flag &= ~RE_ENGINE_RENDERING;

  render_result_free_list(&engine->fullresult, engine->fullresult.first);

  /* re->engine becomes zero if user changed active render engine during render */
  if (!engine_keep_graph(engine) || !re->engine) {
    engine_graph_free(engine);

    render_engine_free(engine);
    re->engine = NULL;
  }

  if (re->r.scemode & R_EXR_CACHE_FILE) {
    lib_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
    render_result_exr_file_cache_write(re);
    lib_rw_mutex_unlock(&re->resultmutex);
  }

  if (dune_reports_contain(re->reports, RPT_ERROR)) {
    G.is_break = true;
  }

#ifdef WITH_FREESTYLE
  if (re->r.mode & R_EDGE_FRS) {
    render_RenderFreestyleExternal(re);
  }
#endif

  return true;
}

void render_engine_update_render_passes(struct RenderEngine *engine,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer,
                                        update_render_passes_cb_t cb,
                                        void *cb_data)
{
  if (!(scene && view_layer && engine && cb && engine->type->update_render_passes)) {
    return;
  }

  lib_mutex_lock(&engine->update_render_passes_mutex);

  engine->update_render_passes_cb = cb;
  engine->update_render_passes_data = cb_data;
  engine->type->update_render_passes(engine, scene, view_layer);
  engine->update_render_passes_cb = NULL;
  engine->update_render_passes_data = NULL;

  lib_mutex_unlock(&engine->update_render_passes_mutex);
}

void render_engine_register_pass(struct RenderEngine *engine,
                                 struct Scene *scene,
                                 struct ViewLayer *view_layer,
                                 const char *name,
                                 int channels,
                                 const char *chanid,
                                 eNodeSocketDatatype type)
{
  if (!(scene && view_layer && engine && engine->update_render_passes_cb)) {
    return;
  }

  engine->update_render_passes_cb(
      engine->update_render_passes_data, scene, view_layer, name, channels, chanid, type);
}

void render_engine_free_blender_memory(RenderEngine *engine)
{
  /* Weak way to save memory, but not crash grease pencil.
   *
   * TODO: Find better solution for this.
   */
  if (engine->has_pen || engine_keep_graph(engine)) {
    return;
  }
  engine_graph_free(engine);
}

struct RenderEngine *render_engine_get(const Render *re)
{
  return re->engine;
}

bool render_engine_draw_acquire(Render *re)
{
  lib_mutex_lock(&re->engine_draw_mutex);

  RenderEngine *engine = re->engine;

  if (engine == NULL || engine->type->draw == NULL || (engine->flag & RE_ENGINE_CAN_DRAW) == 0) {
    lib_mutex_unlock(&re->engine_draw_mutex);
    return false;
  }

  return true;
}

void render_engine_draw_release(Render *re)
{
  lib_mutex_unlock(&re->engine_draw_mutex);
}

void render_engine_tile_highlight_set(
    RenderEngine *engine, int x, int y, int width, int height, bool highlight)
{
  HighlightedTile tile;
  lib_rcti_init(&tile.rect, x, x + width, y, y + height);

  engine_tile_highlight_set(engine, &tile, highlight);
}

void render_engine_tile_highlight_clear_all(RenderEngine *engine)
{
  if ((engine->flag & RE_ENGINE_HIGHLIGHT_TILES) == 0) {
    return;
  }

  Render *re = engine->re;

  lib_mutex_lock(&re->highlighted_tiles_mutex);

  if (re->highlighted_tiles != NULL) {
    lib_gset_clear(re->highlighted_tiles, MEM_freeN);
  }

  lib_mutex_unlock(&re->highlighted_tiles_mutex);
}

/* -------------------------------------------------------------------- */
/** OpenGL context manipulation.
 *
 * NOTE: Only used for Cycles's BLenderGPUDisplay integration with the draw manager. A subject
 * for re-consideration. Do not use this functionality.
 **/

bool render_engine_has_render_context(RenderEngine *engine)
{
  if (engine->re == NULL) {
    return false;
  }

  return render_gl_ctx_get(engine->re) != NULL;
}

void render_engine_render_ctx_enable(RenderEngine *engine)
{
  draw_render_ctx_enable(engine->re);
}

void render_engine_render_ctx_disable(RenderEngine *engine)
{
  draw_render_ctx_disable(engine->re);
}
