
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLT_translation.h"

#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_math_bits.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_object_types.h"

#include "BKE_camera.h"
#include "BKE_colortools.h"
#include "BKE_global.h"
#include "BKE_layer.h"
#include "BKE_node.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_debug.h"
#include "DEG_depsgraph_query.h"

#include "RNA_access.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#include "RE_bake.h"
#include "RE_engine.h"
#include "RE_pipeline.h"

#include "DRW_engine.h"

#include "pipeline.h"
#include "render_result.h"
#include "render_types.h"

/* Render Engine Types */

ListBase R_engines = {NULL, NULL};

void RE_engines_init(void)
{
  DRW_engines_register();
}

void RE_engines_init_experimental()
{
  DRW_engines_register_experimental();
}

void RE_engines_exit(void)
{
  RenderEngineType *type, *next;

  DRW_engines_free();

  for (type = R_engines.first; type; type = next) {
    next = type->next;

    BLI_remlink(&R_engines, type);

    if (!(type->flag & RE_INTERNAL)) {
      if (type->rna_ext.free) {
        type->rna_ext.free(type->rna_ext.data);
      }

      MEM_freeN(type);
    }
  }
}

void RE_engines_register(RenderEngineType *render_type)
{
  if (render_type->draw_engine) {
    DRW_engine_register(render_type->draw_engine);
  }
  BLI_addtail(&R_engines, render_type);
}

RenderEngineType *RE_engines_find(const char *idname)
{
  RenderEngineType *type;

  type = BLI_findstring(&R_engines, idname, offsetof(RenderEngineType, idname));
  if (!type) {
    type = BLI_findstring(&R_engines, "BLENDER_EEVEE", offsetof(RenderEngineType, idname));
  }

  return type;
}

bool RE_engine_is_external(const Render *re)
{
  return (re->engine && re->engine->type && re->engine->type->render);
}

bool RE_engine_is_opengl(RenderEngineType *render_type)
{
  /* TODO: refine? Can we have ogl render engine without ogl render pipeline? */
  return (render_type->draw_engine != NULL) && DRW_engine_render_support(render_type->draw_engine);
}

bool RE_engine_supports_alembic_procedural(const RenderEngineType *render_type, Scene *scene)
{
  if ((render_type->flag & RE_USE_ALEMBIC_PROCEDURAL) == 0) {
    return false;
  }

  if (BKE_scene_uses_cycles(scene) && !BKE_scene_uses_cycles_experimental_features(scene)) {
    return false;
  }

  return true;
}

/* Create, Free */

RenderEngine *RE_engine_create(RenderEngineType *type)
{
  RenderEngine *engine = MEM_callocN(sizeof(RenderEngine), "RenderEngine");
  engine->type = type;

  BLI_mutex_init(&engine->update_render_passes_mutex);

  return engine;
}

static void engine_depsgraph_free(RenderEngine *engine)
{
  if (engine->depsgraph) {
    /* Need GPU context since this might free GPU buffers. */
    const bool use_gpu_context = (engine->type->flag & RE_USE_GPU_CONTEXT);
    if (use_gpu_context) {
      DRW_render_context_enable(engine->re);
    }

    DEG_graph_free(engine->depsgraph);
    engine->depsgraph = NULL;

    if (use_gpu_context) {
      DRW_render_context_disable(engine->re);
    }
  }
}

void RE_engine_free(RenderEngine *engine)
{
#ifdef WITH_PYTHON
  if (engine->py_instance) {
    BPY_DECREF_RNA_INVALIDATE(engine->py_instance);
  }
#endif

  engine_depsgraph_free(engine);

  BLI_mutex_end(&engine->update_render_passes_mutex);

  MEM_freeN(engine);
}

/* Bake Render Results */

static RenderResult *render_result_from_bake(RenderEngine *engine, int x, int y, int w, int h)
{
  /* Create render result with specified size. */
  RenderResult *rr = MEM_callocN(sizeof(RenderResult), __func__);

  rr->rectx = w;
  rr->recty = h;
  rr->tilerect.xmin = x;
  rr->tilerect.ymin = y;
  rr->tilerect.xmax = x + w;
  rr->tilerect.ymax = y + h;

  /* Add single baking render layer. */
  RenderLayer *rl = MEM_callocN(sizeof(RenderLayer), "bake render layer");
  rl->rectx = w;
  rl->recty = h;
  BLI_addtail(&rr->layers, rl);

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
  RenderPass *rpass = RE_pass_find_by_name(rr->layers.first, RE_PASSNAME_COMBINED, "");

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
  if ((engine->flag & RE_ENGINE_HIGHLIGHT_TILES) == 0) {
    return;
  }

  Render *re = engine->re;

  BLI_mutex_lock(&re->highlighted_tiles_mutex);

  if (re->highlighted_tiles == NULL) {
    re->highlighted_tiles = BLI_gset_new(
        BLI_ghashutil_inthash_v4_p, BLI_ghashutil_inthash_v4_cmp, "highlighted tiles");
  }

  if (highlight) {
    HighlightedTile **tile_in_set;
    if (!BLI_gset_ensure_p_ex(re->highlighted_tiles, tile, (void ***)&tile_in_set)) {
      *tile_in_set = MEM_mallocN(sizeof(HighlightedTile), __func__);
      **tile_in_set = *tile;
    }
  }
  else {
    BLI_gset_remove(re->highlighted_tiles, tile, MEM_freeN);
  }

  BLI_mutex_unlock(&re->highlighted_tiles_mutex);
}

RenderResult *RE_engine_begin_result(
    RenderEngine *engine, int x, int y, int w, int h, const char *layername, const char *viewname)
{
  if (engine->bake.pixels) {
    RenderResult *result = render_result_from_bake(engine, x, y, w, h);
    BLI_addtail(&engine->fullresult, result);
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

    BLI_addtail(&engine->fullresult, result);

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
    BLI_rw_mutex_lock(&re->resultmutex, THREAD_LOCK_WRITE);
    if (!re->result->passes_allocated) {
      render_result_passes_allocated_ensure(re->result);
    }
    BLI_rw_mutex_unlock(&re->resultmutex);
  }
}

void RE_engine_update_result(RenderEngine *engine, RenderResult *result)
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

void RE_engine_add_pass(RenderEngine *engine,
                        const char *name,
                        int channels,
                        const char *chan_id,
                        const char *layername)
{
  Render *re = engine->re;

  if (!re || !re->result) {
    return;
  }

  RE_create_render_pass(re->result, name, channels, chan_id, layername, NULL, false);
}

void RE_engine_end_result(
    RenderEngine *engine, RenderResult *result, bool cancel, bool highlight, bool merge_results)
{
  Render *re = engine->re;

  if (!result) {
    return;
  }

  if (engine->bake.pixels) {
    render_result_to_bake(engine, result);
    BLI_remlink(&engine->fullresult, result);
    render_result_free(result);
    return;
  }
