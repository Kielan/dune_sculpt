#include <cstdlib>
#include <cstring>

#include "TYPES_cachefile.h"
#include "TYPES_light.h"
#include "TYPES_material.h"
#include "TYPES_node.h"
#include "TYPES_object.h"
#include "TYPES_scene.h"
#include "TYPES_screen.h"
#include "TYPES_space.h"
#include "TYPES_view3d.h"
#include "TYPES_windowmanager.h"
#include "TYPES_world.h"

#include "DRW_engine.h"

#include "LIB_listbase.h"
#include "LIB_threads.h"
#include "LIB_utildefines.h"

#include "DUNE_context.h"
#include "DUNE_icons.h"
#include "DUNE_main.h"
#include "DUNE_material.h"
#include "DUNE_node.h"
#include "DUNE_paint.h"
#include "DUNE_scene.h"

#include "NOD_composite.h"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "ED_node.h"
#include "ED_paint.h"
#include "ED_render.h"
#include "ED_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "WM_api.h"

#include <cstdio>

/* -------------------------------------------------------------------- */
/** Render Engines **/

void ED_render_view3d_update(Depsgraph *depsgraph,
                             wmWindow *window,
                             ScrArea *area,
                             const bool updated)
{
  Main *duneMain = DEG_get_duneMain(depsgraph);
  Scene *scene = DEG_get_input_scene(depsgraph);
  ViewLayer *view_layer = DEG_get_input_view_layer(depsgraph);

  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->regiontype != RGN_TYPE_WINDOW) {
      continue;
    }

    View3D *v3d = static_cast<View3D *>(area->spacedata.first);
    RegionView3D *rv3d = static_cast<RegionView3D *>(region->regiondata);
    RenderEngine *engine = rv3d->render_engine;

    /* call update if the scene changed, or if the render engine
     * tagged itself for update (e.g. because it was busy at the
     * time of the last update) */
    if (engine && (updated || (engine->flag & RE_ENGINE_DO_UPDATE))) {
      /* Create temporary context to execute callback in. */
      duneContext *C = CTX_create();
      CTX_data_main_set(C, duneMain);
      CTX_data_scene_set(C, scene);
      CTX_wm_manager_set(C, static_cast<wmWindowManager *>(duneMain->wm.first));
      CTX_wm_window_set(C, window);
      CTX_wm_screen_set(C, WM_window_get_active_screen(window));
      CTX_wm_area_set(C, area);
      CTX_wm_region_set(C, region);

      engine->flag &= ~RE_ENGINE_DO_UPDATE;
      /* NOTE: Important to pass non-updated depsgraph, This is because this function is called
       * from inside dependency graph evaluation. Additionally, if we pass fully evaluated one
       * we will lose updates stored in the graph. */
      engine->type->view_update(engine, C, CTX_data_depsgraph_pointer(C));

      CTX_free(C);
    }
    else {
      RenderEngineType *engine_type = ED_view3d_engine_type(scene, v3d->shading.type);
      if (updated) {
        DRWUpdateContext drw_context = {nullptr};
        drw_context.duneMain = duneMain;
        drw_context.depsgraph = depsgraph;
        drw_context.scene = scene;
        drw_context.view_layer = view_layer;
        drw_context.region = region;
        drw_context.v3d = v3d;
        drw_context.engine_type = engine_type;
        DRW_notify_view_update(&drw_context);
      }
    }
  }
}

void ED_render_scene_update(const DEGEditorUpdateContext *update_ctx, const bool updated)
{
  Main *duneMain = update_ctx->duneMain;
  static bool recursive_check = false;

  /* don't do this render engine update if we're updating the scene from
   * other threads doing e.g. rendering or baking jobs */
  if (!LIB_thread_is_main()) {
    return;
  }

  /* don't call this recursively for frame updates */
  if (recursive_check) {
    return;
  }

  /* Do not call if no WM available, see T42688. */
  if (LIB_listbase_is_empty(&duneMain->wm)) {
    return;
  }

  recursive_check = true;

  wmWindowManager *wm = static_cast<wmWindowManager *>(duneMain->wm.first);
  LISTBASE_FOREACH (wmWindow *, window, &wm->windows) {
    duneScreen *screen = WM_window_get_active_screen(window);

    LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
      if (area->spacetype == SPACE_VIEW3D) {
        ED_render_view3d_update(update_ctx->depsgraph, window, area, updated);
      }
    }
  }

  recursive_check = false;
}

void ED_render_engine_area_exit(Main *duneMain, ScrArea *area)
{
  /* clear all render engines in this area */
  ARegion *region;
  wmWindowManager *wm = static_cast<wmWindowManager *>(duneMain->wm.first);

  if (area->spacetype != SPACE_VIEW3D) {
    return;
  }

  for (region = static_cast<ARegion *>(area->regionbase.first); region; region = region->next) {
    if (region->regiontype != RGN_TYPE_WINDOW || !(region->regiondata)) {
      continue;
    }
    ED_view3d_stop_render_preview(wm, region);
  }
}

void ED_render_engine_changed(Main *duneMain, const bool update_scene_data)
{
  /* on changing the render engine type, clear all running render engines */
  for (duneScreen *screen = static_cast<duneScreen *>(duneMain->screens.first); screen;
       screen = static_cast<bScreen *>(screen->id.next)) {
    LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
      ED_render_engine_area_exit(bmain, area);
    }
  }
  RE_FreePersistentData(nullptr);
  /* Inform all render engines and draw managers. */
  DEGEditorUpdateContext update_ctx = {nullptr};
  update_ctx.duneMain = duneMain;
  for (Scene *scene = static_cast<Scene *>(duneMain->scenes.first); scene;
       scene = static_cast<Scene *>(scene->id.next)) {
    update_ctx.scene = scene;
    LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
      /* TDODO(sergey): Iterate over depsgraphs instead? */
      update_ctx.depsgraph = DUNE_scene_ensure_depsgraph(bmain, scene, view_layer);
      update_ctx.view_layer = view_layer;
      ED_render_id_flush_update(&update_ctx, &scene->id);
    }
    if (scene->nodetree && update_scene_data) {
      ntreeCompositUpdateRLayers(scene->nodetree);
    }
  }

  /* Update CacheFiles to ensure that procedurals are properly taken into account. */
  LISTBASE_FOREACH (CacheFile *, cachefile, &duneMain->cachefiles) {
    /* Only update cache-files which are set to use a render procedural.
     * We do not use DUNE_cachefile_uses_render_procedural here as we need to update regardless of
     * the current engine or its settings. */
    if (cachefile->use_render_procedural) {
      DEG_id_tag_update(&cachefile->id, ID_RECALC_COPY_ON_WRITE);
      /* Rebuild relations so that modifiers are reconnected to or disconnected from the
       * cache-file. */
      DEG_relations_tag_update(bmain);
    }
  }
}

void ED_render_view_layer_changed(Main *duneMain, duneScreen *screen)
{
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    ED_render_engine_area_exit(duneMain, area);
  }
}

/* -------------------------------------------------------------------- */
/** Updates
 *
 * ED_render_id_flush_update gets called from DEG_id_tag_update,
 * to do editor level updates when the ID changes.
 * When these ID blocks are in the dependency graph,
 * we can get rid of the manual dependency checks.
 **/

static void material_changed(Main *UNUSED(duneMain), Material *ma)
{
  /* icons */
  DUNE_icon_changed(DUNE_icon_id_ensure(&ma->id));
}

static void lamp_changed(Main *UNUSED(duneMain), Light *la)
{
  /* icons */
  DUNE_icon_changed(DUNE_icon_id_ensure(&la->id));
}

static void texture_changed(Main *duneMain, Tex *tex)
{
  Scene *scene;
  ViewLayer *view_layer;
  duneNode *node;

  /* icons */
  DUNE_icon_changed(DUNE_icon_id_ensure(&tex->id));

  for (scene = static_cast<Scene *>(duneMain->scenes.first); scene;
       scene = static_cast<Scene *>(scene->id.next)) {
    /* paint overlays */
    for (view_layer = static_cast<ViewLayer *>(scene->view_layers.first); view_layer;
         view_layer = view_layer->next) {
      DUNE_paint_invalidate_overlay_tex(scene, view_layer, tex);
    }
    /* find compositing nodes */
    if (scene->use_nodes && scene->nodetree) {
      for (node = static_cast<duneNode *>(scene->nodetree->nodes.first); node; node = node->next) {
        if (node->id == &tex->id) {
          ED_node_tag_update_id(&scene->id);
        }
      }
    }
  }
}

static void world_changed(Main *UNUSED(duneMain), World *wo)
{
  /* icons */
  DUNE_icon_changed(DUNE_icon_id_ensure(&wo->id));
}

static void image_changed(Main *duneMain, Image *ima)
{
  Tex *tex;

  /* icons */
  DUNE_icon_changed(DUNE_icon_id_ensure(&ima->id));

  /* textures */
  for (tex = static_cast<Tex *>(duneMain->textures.first); tex;
       tex = static_cast<Tex *>(tex->id.next)) {
    if (tex->type == TEX_IMAGE && tex->ima == ima) {
      texture_changed(bmain, tex);
    }
  }
}

static void scene_changed(Main *duneMain, Scene *scene)
{
  Object *ob;

  /* glsl */
  for (ob = static_cast<Object *>(duneMain->objects.first); ob;
       ob = static_cast<Object *>(ob->id.next)) {
    if (ob->mode & OB_MODE_TEXTURE_PAINT) {
      DUNE_texpaint_slots_refresh_object(scene, ob);
      ED_paint_proj_mesh_data_check(scene, ob, nullptr, nullptr, nullptr, nullptr);
    }
  }
}

void ED_render_id_flush_update(const DEGEditorUpdateContext *update_ctx, ID *id)
{
  /* this can be called from render or baking thread when a python script makes
   * changes, in that case we don't want to do any editor updates, and making
   * GPU changes is not possible because OpenGL only works in the main thread */
  if (!LIB_thread_is_main()) {
    return;
  }
  Main *duneMain = update_ctx->duneMain;
  /* Internal ID update handlers. */
  switch (GS(id->name)) {
    case ID_MA:
      material_changed(duneMain, (Material *)id);
      break;
    case ID_TE:
      texture_changed(duneMain, (Tex *)id);
      break;
    case ID_WO:
      world_changed(duneMain, (World *)id);
      break;
    case ID_LA:
      lamp_changed(duneMain, (Light *)id);
      break;
    case ID_IM:
      image_changed(duneMain, (Image *)id);
      break;
    case ID_SCE:
      scene_changed(duneMain, (Scene *)id);
      break;
    default:
      break;
  }
}
