#include "draw_render.h"

#include "workbench_private.h"

#include "lib_memblock.h"

#include "types_userdef.h"

#include "ed_screen.h"
#include "ed_view3d.h"

#include "ui_resources.h"

#include "gpu_uniform_buffer.h"

/* -------------------------------------------------------------------- */
/** World Data */

GPUUniformBuf *workbench_material_ubo_alloc(WorkbenchPrivateData *wpd)
{
  struct GPUUniformBuf **ubo = lib_memblock_alloc(wpd->material_ubo);
  if (*ubo == NULL) {
    *ubo = gpu_uniformbuf_create(sizeof(WORKBENCH_UBO_Material) * MAX_MATERIAL);
  }
  return *ubo;
}

static void workbench_ubo_free(void *elem)
{
  GPUUniformBuf **ubo = elem;
  DRAW_UBO_FREE_SAFE(*ubo);
}

static void workbench_view_layer_data_free(void *storage)
{
  WorkbenchViewLayerData *vldata = (WORKBENCH_ViewLayerData *)storage;

  DRAW_UBO_FREE_SAFE(vldata->dof_sample_ubo);
  DRAW_UBO_FREE_SAFE(vldata->world_ubo);
  DRAW_UBO_FREE_SAFE(vldata->cavity_sample_ubo);
  DRAW_TEXTURE_FREE_SAFE(vldata->cavity_jitter_tx);

  lib_memblock_destroy(vldata->material_ubo_data, NULL);
  lib_memblock_destroy(vldata->material_ubo, workbench_ubo_free);
}

static WORKBENCH_ViewLayerData *workbench_view_layer_data_ensure_ex(struct ViewLayer *view_layer)
{
  WORKBENCH_ViewLayerData **vldata = (WORKBENCH_ViewLayerData **)
      draw_view_layer_engine_data_ensure_ex(view_layer,
                                           (DrawEngineType *)&workbench_view_layer_data_ensure_ex,
                                           &workbench_view_layer_data_free);

  if (*vldata == NULL) {
    *vldata = MEM_callocN(sizeof(**vldata), "WORKBENCH_ViewLayerData");
    size_t matbuf_size = sizeof(WORKBENCH_UBO_Material) * MAX_MATERIAL;
    (*vldata)->material_ubo_data = BLI_memblock_create_ex(matbuf_size, matbuf_size * 2);
    (*vldata)->material_ubo = BLI_memblock_create_ex(sizeof(void *), sizeof(void *) * 8);
    (*vldata)->world_ubo = GPU_uniformbuf_create_ex(sizeof(WORKBENCH_UBO_World), NULL, "wb_World");
  }

  return *vldata;
}

/** \} */

static void workbench_studiolight_data_update(WORKBENCH_PrivateData *wpd, WORKBENCH_UBO_World *wd)
{
  StudioLight *studiolight = wpd->studio_light;
  float view_matrix[4][4], rot_matrix[4][4];
  DRW_view_viewmat_get(NULL, view_matrix, false);

  if (USE_WORLD_ORIENTATION(wpd)) {
    axis_angle_to_mat4_single(rot_matrix, 'Z', -wpd->shading.studiolight_rot_z);
    mul_m4_m4m4(rot_matrix, view_matrix, rot_matrix);
    swap_v3_v3(rot_matrix[2], rot_matrix[1]);
    negate_v3(rot_matrix[2]);
  }
  else {
    unit_m4(rot_matrix);
  }

  if (U.edit_studio_light) {
    studiolight = dune_studiolight_studio_edit_get();
  }

  /* Studio Lights. */
  for (int i = 0; i < 4; i++) {
    WORKBENCH_UBO_Light *light = &wd->lights[i];

    SolidLight *sl = (studiolight) ? &studiolight->light[i] : NULL;
    if (sl && sl->flag) {
      copy_v3_v3(light->light_direction, sl->vec);
      mul_mat3_m4_v3(rot_matrix, light->light_direction);
      /* We should predivide the power by PI but that makes the lights really dim. */
      copy_v3_v3(light->specular_color, sl->spec);
      copy_v3_v3(light->diffuse_color, sl->col);
      light->wrapped = sl->smooth;
    }
    else {
      copy_v3_fl3(light->light_direction, 1.0f, 0.0f, 0.0f);
      copy_v3_fl(light->specular_color, 0.0f);
      copy_v3_fl(light->diffuse_color, 0.0f);
      light->wrapped = 0.0f;
    }
  }

  if (studiolight) {
    copy_v3_v3(wd->ambient_color, studiolight->light_ambient);
  }
  else {
    copy_v3_fl(wd->ambient_color, 1.0f);
  }

  wd->use_specular = workbench_is_specular_highlight_enabled(wpd);
}

void workbench_private_data_alloc(DBenchStorageList *stl)
{
  if (!stl->wpd) {
    stl->wpd = mem_callocn(sizeof(*stl->wpd), __func__);
    stl->wpd->taa_sample_len_previous = -1;
    stl->wpd->view_updated = true;
  }
}

void workbench_private_data_init(DBenchPrivateData *wpd)
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  RegionView3D *rv3d = draw_ctx->rv3d;
  View3D *v3d = draw_ctx->v3d;
  Scene *scene = draw_ctx->scene;
  DBenchViewLayerData *vldata = workbench_view_layer_data_ensure_ex(draw_ctx->view_layer);

  wpd->is_playback = draw_state_is_playback();
  wpd->is_navigating = draw_state_is_navigating();

  wpd->ctx_mode = ctx_data_mode_enum_ex(
      draw_ctx->object_edit, draw_ctx->obact, draw_ctx->object_mode);

  wpd->preferences = &U;
  wpd->scene = scene;
  wpd->sh_cfg = draw_ctx->sh_cfg;

  /* FIXME: This reproduce old behavior when workbench was separated in 2 engines.
   * But this is a workaround for a missing update tagging. */
  DrawState clip_state = RV3D_CLIPPING_ENABLED(v3d, rv3d) ? DRW_STATE_CLIP_PLANES : 0;
  if (clip_state != wpd->clip_state) {
    wpd->view_updated = true;
  }
  wpd->clip_state = clip_state;

  wpd->vldata = vldata;
  wpd->world_ubo = vldata->world_ubo;

  wpd->taa_sample_len = workbench_antialiasing_sample_count_get(wpd);

  wpd->volumes_do = false;
  lib_listbase_clear(&wpd->smoke_domains);

  /* FIXME: This reproduce old behavior when workbench was separated in 2 engines.
   * But this is a workaround for a missing update tagging. */
  if ((rv3d != NULL) && (rv3d->rflag & RV3D_GPULIGHT_UPDATE)) {
    wpd->view_updated = true;
    rv3d->rflag &= ~RV3D_GPULIGHT_UPDATE;
  }

  if (!v3d || (v3d->shading.type == OB_RENDER && BKE_scene_uses_blender_workbench(scene))) {
    short shading_flag = scene->display.shading.flag;
    if (XRAY_FLAG_ENABLED((&scene->display))) {
      /* Disable shading options that aren't supported in transparency mode. */
      shading_flag &= ~(V3D_SHADING_SHADOW | V3D_SHADING_CAVITY | V3D_SHADING_DEPTH_OF_FIELD);
    }

    /* FIXME: This reproduce old behavior when workbench was separated in 2 engines.
     * But this is a workaround for a missing update tagging from operators. */
    if ((XRAY_ENABLED(wpd) != XRAY_ENABLED(&scene->display)) ||
        (shading_flag != wpd->shading.flag)) {
      wpd->view_updated = true;
    }

    wpd->shading = scene->display.shading;
    wpd->shading.flag = shading_flag;
    if (XRAY_FLAG_ENABLED((&scene->display))) {
      wpd->shading.xray_alpha = XRAY_ALPHA((&scene->display));
    }
    else {
      wpd->shading.xray_alpha = 1.0f;
    }

    if (scene->r.alphamode == R_ALPHAPREMUL) {
      copy_v4_fl(wpd->background_color, 0.0f);
    }
    else if (scene->world) {
      World *wo = scene->world;
      copy_v4_fl4(wpd->background_color, wo->horr, wo->horg, wo->horb, 1.0f);
    }
    else {
      copy_v4_fl4(wpd->background_color, 0.0f, 0.0f, 0.0f, 1.0f);
    }
  }
  else {
    short shading_flag = v3d->shading.flag;
    if (XRAY_ENABLED(v3d)) {
      /* Disable shading options that aren't supported in transparency mode. */
      shading_flag &= ~(V3D_SHADING_SHADOW | V3D_SHADING_CAVITY | V3D_SHADING_DEPTH_OF_FIELD);
    }

    /* FIXME: This reproduce old behavior when workbench was separated in 2 engines.
     * But this is a workaround for a missing update tagging from operators. */
    if (XRAY_ENABLED(v3d) != XRAY_ENABLED(wpd) || shading_flag != wpd->shading.flag) {
      wpd->view_updated = true;
    }

    wpd->shading = v3d->shading;
    wpd->shading.flag = shading_flag;
    if (wpd->shading.type < OB_SOLID) {
      wpd->shading.light = V3D_LIGHTING_FLAT;
      wpd->shading.color_type = V3D_SHADING_OBJECT_COLOR;
      wpd->shading.xray_alpha = 0.0f;
    }
    else if (XRAY_ENABLED(v3d)) {
      wpd->shading.xray_alpha = XRAY_ALPHA(v3d);
    }
    else {
      wpd->shading.xray_alpha = 1.0f;
    }

    /* No background. The overlays will draw the correct one. */
    copy_v4_fl(wpd->background_color, 0.0f);
  }

  wpd->cull_state = CULL_BACKFACE_ENABLED(wpd) ? DRW_STATE_CULL_BACK : 0;

  if (wpd->shading.light == V3D_LIGHTING_MATCAP) {
    wpd->studio_light = dune_studiolight_find(wpd->shading.matcap, STUDIOLIGHT_TYPE_MATCAP);
  }
  else {
    wpd->studio_light = dune_studiolight_find(wpd->shading.studio_light, STUDIOLIGHT_TYPE_STUDIO);
  }

  /* If matcaps are missing, use this as fallback. */
  if (UNLIKELY(wpd->studio_light == NULL)) {
    wpd->studio_light = dune_studiolight_find(wpd->shading.studio_light, STUDIOLIGHT_TYPE_STUDIO);
  }

  {
    /* Material UBOs. */
    wpd->material_ubo_data = vldata->material_ubo_data;
    wpd->material_ubo = vldata->material_ubo;
    wpd->material_chunk_count = 1;
    wpd->material_chunk_curr = 0;
    wpd->material_index = 1;
    /* Create default material ubo. */
    wpd->material_ubo_data_curr = BLI_memblock_alloc(wpd->material_ubo_data);
    wpd->material_ubo_curr = workbench_material_ubo_alloc(wpd);
    /* Init default material used by vertex color & texture. */
    workbench_material_ubo_data(
        wpd, NULL, NULL, &wpd->material_ubo_data_curr[0], V3D_SHADING_MATERIAL_COLOR);
  }
}

void workbench_update_world_ubo(DBenchPrivateData *wpd)
{
  WORKBENCH_UBO_World wd;

  copy_v2_v2(wd.viewport_size, draw_viewport_size_get());
  copy_v2_v2(wd.viewport_size_inv, draw_viewport_invert_size_get());
  copy_v3_v3(wd.object_outline_color, wpd->shading.object_outline_color);
  wd.object_outline_color[3] = 1.0f;
  wd.ui_scale = draw_state_is_image_render() ? 1.0f : G_draw.block.sizePixel;
  wd.matcap_orientation = (wpd->shading.flag & V3D_SHADING_MATCAP_FLIP_X) != 0;

  workbench_studiolight_data_update(wpd, &wd);
  workbench_shadow_data_update(wpd, &wd);
  workbench_cavity_data_update(wpd, &wd);

  gpu_uniformbuf_update(wpd->world_ubo, &wd);
}

void workbench_update_material_ubos(DBenchPrivateData *UNUSED(wpd))
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  WorkbenchViewLayerData *vldata = workbench_view_layer_data_ensure_ex(draw_ctx->view_layer);

  lib_memblock_iter iter, iter_data;
  lib_memblock_iternew(vldata->material_ubo, &iter);
  lib_memblock_iternew(vldata->material_ubo_data, &iter_data);
  WORKBENCH_UBO_Material *matchunk;
  while ((matchunk = lib_memblock_iterstep(&iter_data))) {
    GPUUniformBuf **ubo = lib_memblock_iterstep(&iter);
    lib_assert(*ubo != NULL);
    gpu_uniformbuf_update(*ubo, matchunk);
  }

  lib_memblock_clear(vldata->material_ubo, workbench_ubo_free);
  lib_memblock_clear(vldata->material_ubo_data, NULL);
}
