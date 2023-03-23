/** Engine for drawing a selection map where the pixels indicate the selection indices. **/

#include "draw_engine.h"
#include "draw_render.h"

#include "dgraph_query.h"

#include "ed_view3d.h"

#include "ui_interface.h"

#include "dune_object.h"
#include "dune_paint.h"

#include "types_space.h"

#include "overlay_engine.h"
#include "overlay_private.h"

/* -------------------------------------------------------------------- */
/** Engine Callbacks **/

static void overlay_engine_init(void *vedata)
{
  OverlayData *data = vedata;
  OverlayStorageList *stl = data->stl;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const RegionView3D *rv3d = draw_ctx->rv3d;
  const View3D *v3d = draw_ctx->v3d;
  const Scene *scene = draw_ctx->scene;
  const ToolSettings *ts = scene->toolsettings;

  overlay_shader_lib_ensure();

  if (!stl->pd) {
    /* Allocate transient pointers. */
    stl->pd = mem_callocn(sizeof(*stl->pd), __func__);
  }

  OverlayPrivateData *pd = stl->pd;
  pd->space_type = v3d != NULL ? SPACE_VIEW3D : draw_ctx->space_data->spacetype;

  if (pd->space_type == SPACE_IMAGE) {
    const SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;
    pd->hide_overlays = (sima->overlay.flag & SI_OVERLAY_SHOW_OVERLAYS) == 0;
    pd->clipping_state = 0;
    overlay_grid_init(data);
    overlay_edit_uv_init(data);
    return;
  }
  if (pd->space_type == SPACE_NODE) {
    pd->hide_overlays = true;
    pd->clipping_state = 0;
    return;
  }

  pd->hide_overlays = (v3d->flag2 & V3D_HIDE_OVERLAYS) != 0;
  pd->ctx_mode = CTX_data_mode_enum_ex(
      draw_ctx->object_edit, draw_ctx->obact, draw_ctx->object_mode);

  if (!pd->hide_overlays) {
    pd->overlay = v3d->overlay;
    pd->v3d_flag = v3d->flag;
    pd->v3d_gridflag = v3d->gridflag;
  }
  else {
    memset(&pd->overlay, 0, sizeof(pd->overlay));
    pd->v3d_flag = 0;
    pd->v3d_gridflag = 0;
    pd->overlay.flag = V3D_OVERLAY_HIDE_TEXT | V3D_OVERLAY_HIDE_MOTION_PATHS |
                       V3D_OVERLAY_HIDE_BONES | V3D_OVERLAY_HIDE_OBJECT_XTRAS |
                       V3D_OVERLAY_HIDE_OBJECT_ORIGINS;
    pd->overlay.wireframe_threshold = v3d->overlay.wireframe_threshold;
    pd->overlay.wireframe_opacity = v3d->overlay.wireframe_opacity;
  }

  if (v3d->shading.type == OB_WIRE) {
    pd->overlay.flag |= V3D_OVERLAY_WIREFRAMES;
  }

  if (ts->sculpt) {
    if (ts->sculpt->flags & SCULPT_HIDE_FACE_SETS) {
      pd->overlay.sculpt_mode_face_sets_opacity = 0.0f;
    }
    if (ts->sculpt->flags & SCULPT_HIDE_MASK) {
      pd->overlay.sculpt_mode_mask_opacity = 0.0f;
    }
  }

  pd->use_in_front = (v3d->shading.type <= OB_SOLID) ||
                     dune_scene_uses_blender_workbench(draw_ctx->scene);
  pd->wireframe_mode = (v3d->shading.type == OB_WIRE);
  pd->clipping_state = RV3D_CLIPPING_ENABLED(v3d, rv3d) ? DRW_STATE_CLIP_PLANES : 0;
  pd->xray_opacity = XRAY_ALPHA(v3d);
  pd->xray_enabled = XRAY_ACTIVE(v3d);
  pd->xray_enabled_and_not_wire = pd->xray_enabled && v3d->shading.type > OB_WIRE;
  pd->clear_in_front = (v3d->shading.type != OB_SOLID);
  pd->cfra = DEG_get_ctime(draw_ctx->dgraph);

  overlay_antialiasing_init(vedata);

  switch (stl->pd->ctx_mode) {
    case CTX_MODE_EDIT_MESH:
      overlay_edit_mesh_init(vedata);
      break;
    default:
      /* Nothing to do. */
      break;
  }
  overlay_facing_init(vedata);
  overlay_grid_init(vedata);
  overlay_image_init(vedata);
  overlay_outline_init(vedata);
  overlay_wireframe_init(vedata);
  overlay_paint_init(vedata);
}

static void overlay_cache_init(void *vedata)
{
  OverlayData *data = vedata;
  OverlayStorageList *stl = data->stl;
  OverlayPrivateData *pd = stl->pd;

  if (pd->space_type == SPACE_IMAGE) {
    overlay_background_cache_init(vedata);
    overlay_grid_cache_init(vedata);
    overlay_edit_uv_cache_init(vedata);
    return;
  }
  if (pd->space_type == SPACE_NODE) {
    OVERLAY_background_cache_init(vedata);
    return;
  }

  switch (pd->ctx_mode) {
    case CTX_MODE_EDIT_MESH:
      OVERLAY_edit_mesh_cache_init(vedata);
      /* `pd->edit_mesh.flag` is valid after calling `OVERLAY_edit_mesh_cache_init`. */
      const bool draw_edit_weights = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_WEIGHT);
      if (draw_edit_weights) {
        OVERLAY_paint_cache_init(vedata);
      }
      break;
    case CTX_MODE_EDIT_SURFACE:
    case CTX_MODE_EDIT_CURVE:
      OVERLAY_edit_curve_cache_init(vedata);
      break;
    case CTX_MODE_EDIT_TEXT:
      OVERLAY_edit_text_cache_init(vedata);
      break;
    case CTX_MODE_EDIT_ARMATURE:
      break;
    case CTX_MODE_EDIT_METABALL:
      break;
    case CTX_MODE_EDIT_LATTICE:
      OVERLAY_edit_lattice_cache_init(vedata);
      break;
    case CTX_MODE_PARTICLE:
      OVERLAY_edit_particle_cache_init(vedata);
      break;
    case CTX_MODE_POSE:
    case CTX_MODE_PAINT_WEIGHT:
    case CTX_MODE_PAINT_VERTEX:
    case CTX_MODE_PAINT_TEXTURE:
      OVERLAY_paint_cache_init(vedata);
      break;
    case CTX_MODE_SCULPT:
      OVERLAY_sculpt_cache_init(vedata);
      break;
    case CTX_MODE_EDIT_GPENCIL:
    case CTX_MODE_PAINT_GPENCIL:
    case CTX_MODE_SCULPT_GPENCIL:
    case CTX_MODE_VERTEX_GPENCIL:
    case CTX_MODE_WEIGHT_GPENCIL:
      OVERLAY_edit_gpencil_cache_init(vedata);
      break;
    case CTX_MODE_SCULPT_CURVES:
    case CTX_MODE_OBJECT:
    case CTX_MODE_EDIT_CURVES:
      break;
    default:
      lib_assert_msg(0, "Draw mode invalid");
      break;
  }
  overlay_antialiasing_cache_init(vedata);
  overlay_armature_cache_init(vedata);
  overlay_background_cache_init(vedata);
  overlay_fade_cache_init(vedata);
  OVERLAY_mode_transfer_cache_init(vedata);
  overlay_extra_cache_init(vedata);
  OVERLAY_facing_cache_init(vedata);
  OVERLAY_gpencil_cache_init(vedata);
  OVERLAY_grid_cache_init(vedata);
  OVERLAY_image_cache_init(vedata);
  OVERLAY_metaball_cache_init(vedata);
  OVERLAY_motion_path_cache_init(vedata);
  OVERLAY_outline_cache_init(vedata);
  OVERLAY_particle_cache_init(vedata);
  OVERLAY_wireframe_cache_init(vedata);
  OVERLAY_volume_cache_init(vedata);
}
