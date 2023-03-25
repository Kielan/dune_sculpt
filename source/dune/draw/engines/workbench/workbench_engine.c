/**
 * Workbench Engine:
 *
 * Optimized engine to draw the working viewport with solid and transparent geometry.
 */

#include "draw_render.h"

#include "lib_alloca.h"

#include "dune_editmesh.h"
#include "dune_modifier.h"
#include "dune_object.h"
#include "dune_paint.h"
#include "dune_particle.h"

#include "types_curves.h"
#include "types_fluid.h"
#include "types_image.h"
#include "types_mesh.h"
#include "types_modifier.h"
#include "types_node.h"

#include "workbench_engine.h"
#include "workbench_private.h"

#define WORKBENCH_ENGINE "BLENDER_WORKBENCH"

void workbench_engine_init(void *ved)
{
  DBenchData *vedata = ved;
  DBenchStorageList *stl = vedata->stl;
  DBenchTextureList *txl = vedata->txl;

  workbench_private_data_alloc(stl);
  DBenchPrivateData *wpd = stl->wpd;
  workbench_private_data_init(wpd);
  workbench_update_world_ubo(wpd);

  if (txl->dummy_image_tx == NULL) {
    const float fpixel[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    txl->dummy_image_tx = draw_texture_create_2d(1, 1, GPU_RGBA8, 0, fpixel);
  }
  wpd->dummy_image_tx = txl->dummy_image_tx;

  if (OBJECT_ID_PASS_ENABLED(wpd)) {
    wpd->object_id_tx = draw_texture_pool_query_fullscreen(GPU_R16UI, &draw_engine_workbench);
  }
  else {
    /* Don't free because it's a pool texture. */
    wpd->object_id_tx = NULL;
  }

  workbench_opaque_engine_init(vedata);
  workbench_transparent_engine_init(vedata);
  workbench_dof_engine_init(vedata);
  workbench_antialiasing_engine_init(vedata);
  workbench_volume_engine_init(vedata);
}

void workbench_cache_init(void *ved)
{
  DBenchData *vedata = ved;

  workbench_opaque_cache_init(vedata);
  workbench_transparent_cache_init(vedata);
  workbench_shadow_cache_init(vedata);
  workbench_cavity_cache_init(vedata);
  workbench_outline_cache_init(vedata);
  workbench_dof_cache_init(vedata);
  workbench_antialiasing_cache_init(vedata);
  workbench_volume_cache_init(vedata);
}

/* TODO: draw_cache_object_surface_material_get needs a refactor to allow passing NULL
 * instead of gpumat_array. Avoiding all this boilerplate code. */
static struct GPUBatch **workbench_object_surface_material_get(Object *ob)
{
  const int materials_len = DRW_cache_object_material_count_get(ob);
  struct GPUMaterial **gpumat_array = lib_array_alloca(gpumat_array, materials_len);
  memset(gpumat_array, 0, sizeof(*gpumat_array) * materials_len);

  return draw_cache_object_surface_material_get(ob, gpumat_array, materials_len);
}

static void workbench_cache_sculpt_populate(WORKBENCH_PrivateData *wpd,
                                            Object *ob,
                                            eV3DShadingColorType color_type)
{
  const bool use_single_drawcall = !ELEM(color_type, V3D_SHADING_MATERIAL_COLOR);
  lib_assert(color_type != V3D_SHADING_TEXTURE_COLOR);

  if (use_single_drawcall) {
    DrawShadingGroup *grp = workbench_material_setup(wpd, ob, 0, color_type, NULL);
    draw_shgroup_call_sculpt(grp, ob, false, false);
  }
  else {
    const int materials_len = DRW_cache_object_material_count_get(ob);
    struct DRWShadingGroup **shgrps = BLI_array_alloca(shgrps, materials_len);
    for (int i = 0; i < materials_len; i++) {
      shgrps[i] = workbench_material_setup(wpd, ob, i + 1, color_type, NULL);
    }
    DRW_shgroup_call_sculpt_with_materials(shgrps, materials_len, ob);
  }
}

BLI_INLINE void workbench_object_drawcall(DRWShadingGroup *grp, struct GPUBatch *geom, Object *ob)
{
  if (ob->type == OB_POINTCLOUD) {
    /* Draw range to avoid drawcall batching messing up the instance attribute. */
    DRW_shgroup_call_instance_range(grp, ob, geom, 0, 0);
  }
  else {
    DRW_shgroup_call(grp, geom, ob);
  }
}

static void workbench_cache_texpaint_populate(DBenchPrivateData *wpd, Object *ob)
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const Scene *scene = draw_ctx->scene;
  const ImagePaintSettings *imapaint = &scene->toolsettings->imapaint;
  const bool use_single_drawcall = imapaint->mode == IMAGEPAINT_MODE_IMAGE;

  if (use_single_drawcall) {
    struct GPUBatch *geom = draw_cache_mesh_surface_texpaint_single_get(ob);
    if (geom) {
      Image *ima = imapaint->canvas;
      eGPUSamplerState state = GPU_SAMPLER_REPEAT;
      SET_FLAG_FROM_TEST(state, imapaint->interp == IMAGEPAINT_INTERP_LINEAR, GPU_SAMPLER_FILTER);

      DrawShadingGroup *grp = workbench_image_setup(wpd, ob, 0, ima, NULL, state);
      workbench_object_drawcall(grp, geom, ob);
    }
  }
  else {
    struct GPUBatch **geoms = draw_cache_mesh_surface_texpaint_get(ob);
    if (geoms) {
      const int materials_len = draw_cache_object_material_count_get(ob);
      for (int i = 0; i < materials_len; i++) {
        if (geoms[i] == NULL) {
          continue;
        }
        DrawShadingGroup *grp = workbench_image_setup(wpd, ob, i + 1, NULL, NULL, 0);
        workbench_object_drawcall(grp, geoms[i], ob);
      }
    }
  }
}

static void workbench_cache_common_populate(DBenchPrivateData *wpd,
                                            Object *ob,
                                            eV3DShadingColorType color_type,
                                            bool *r_transp)
{
  const bool use_tex = ELEM(color_type, V3D_SHADING_TEXTURE_COLOR);
  const bool use_vcol = ELEM(color_type, V3D_SHADING_VERTEX_COLOR);
  const bool use_single_drawcall = !ELEM(
      color_type, V3D_SHADING_MATERIAL_COLOR, V3D_SHADING_TEXTURE_COLOR);

  if (use_single_drawcall) {
    struct GPUBatch *geom;
    if (use_vcol) {
      if (ob->mode & OB_MODE_VERTEX_PAINT) {
        geom = draw_cache_mesh_surface_vertpaint_get(ob);
      }
      else {
        if (U.experimental.use_sculpt_vertex_colors) {
          geom = draw_cache_mesh_surface_sculptcolors_get(ob);
        }
        else {
          geom = draw_cache_mesh_surface_vertpaint_get(ob);
        }
      }
    }
    else {
      geom = draw_cache_object_surface_get(ob);
    }

    if (geom) {
      DrawShadingGroup *grp = workbench_material_setup(wpd, ob, 0, color_type, r_transp);
      workbench_object_drawcall(grp, geom, ob);
    }
  }
  else {
    struct GPUBatch **geoms = (use_tex) ? draw_cache_mesh_surface_texpaint_get(ob) :
                                          workbench_object_surface_material_get(ob);
    if (geoms) {
      const int materials_len = draw_cache_object_material_count_get(ob);
      for (int i = 0; i < materials_len; i++) {
        if (geoms[i] == NULL) {
          continue;
        }
        DrawShadingGroup *grp = workbench_material_setup(wpd, ob, i + 1, color_type, r_transp);
        workbench_object_drawcall(grp, geoms[i], ob);
      }
    }
  }
}

static void workbench_cache_hair_populate(DBenchPrivateData *wpd,
                                          Object *ob,
                                          ParticleSystem *psys,
                                          ModifierData *md,
                                          eV3DShadingColorType color_type,
                                          bool use_texpaint_mode,
                                          const int matnr)
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const Scene *scene = draw_ctx->scene;

  const ImagePaintSettings *imapaint = use_texpaint_mode ? &scene->toolsettings->imapaint : NULL;
  Image *ima = (imapaint && imapaint->mode == IMAGEPAINT_MODE_IMAGE) ? imapaint->canvas : NULL;
  eGPUSamplerState state = 0;
  state |= (imapaint && imapaint->interp == IMAGEPAINT_INTERP_LINEAR) ? GPU_SAMPLER_FILTER : 0;
  DrawShadingGroup *grp = (use_texpaint_mode) ?
                             workbench_image_hair_setup(wpd, ob, matnr, ima, NULL, state) :
                             workbench_material_hair_setup(wpd, ob, matnr, color_type);

  draw_shgroup_hair_create_sub(ob, psys, md, grp, NULL);
}

static const CustomData *workbench_mesh_get_loop_custom_data(const Mesh *mesh)
{
  if (mesh->runtime.wrapper_type == ME_WRAPPER_TYPE_BMESH) {
    lib_assert(mesh->edit_mesh != NULL);
    lib_assert(mesh->edit_mesh->bm != NULL);
    return &mesh->edit_mesh->bm->ldata;
  }
  return &mesh->ldata;
}

static const CustomData *workbench_mesh_get_vert_custom_data(const Mesh *mesh)
{
  if (mesh->runtime.wrapper_type == ME_WRAPPER_TYPE_BMESH) {
    lib_assert(mesh->edit_mesh != NULL);
    lib_assert(mesh->edit_mesh->bm != NULL);
    return &mesh->edit_mesh->bm->vdata;
  }
  return &mesh->vdata;
}

/**
 * Decide what color-type to draw the object with.
 * In some cases it can be overwritten by #workbench_material_setup().
 */
static eV3DShadingColorType workbench_color_type_get(WORKBENCH_PrivateData *wpd,
                                                     Object *ob,
                                                     bool *r_sculpt_pbvh,
                                                     bool *r_texpaint_mode,
                                                     bool *r_draw_shadow)
{
  eV3DShadingColorType color_type = wpd->shading.color_type;
  const Mesh *me = (ob->type == OB_MESH) ? ob->data : NULL;
  const CustomData *ldata = (me == NULL) ? NULL : workbench_mesh_get_loop_custom_data(me);
  const CustomData *vdata = (me == NULL) ? NULL : workbench_mesh_get_vert_custom_data(me);

  const DRWContextState *draw_ctx = draw_ctx_state_get();
  const bool is_active = (ob == draw_ctx->obact);
  const bool is_sculpt_pbvh = dune_sculptsession_use_pbvh_draw(ob, draw_ctx->v3d) &&
                              !draw_state_is_image_render();
  const bool is_render = draw_state_is_image_render() && (draw_ctx->v3d == NULL);
  const bool is_texpaint_mode = is_active && (wpd->ctx_mode == CTX_MODE_PAINT_TEXTURE);
  const bool is_vertpaint_mode = is_active && (wpd->ctx_mode == CTX_MODE_PAINT_VERTEX);

  if (color_type == V3D_SHADING_TEXTURE_COLOR) {
    if (ob->dt < OB_TEXTURE) {
      color_type = V3D_SHADING_MATERIAL_COLOR;
    }
    else if ((me == NULL) || !CustomData_has_layer(ldata, CD_MLOOPUV)) {
      /* Disable color mode if data layer is unavailable. */
      color_type = V3D_SHADING_MATERIAL_COLOR;
    }
  }
  else if (color_type == V3D_SHADING_VERTEX_COLOR) {
    if (U.experimental.use_sculpt_vertex_colors) {
      if ((me == NULL) || !CustomData_has_layer(vdata, CD_PROP_COLOR)) {
        color_type = V3D_SHADING_OBJECT_COLOR;
      }
    }
    else {
      if ((me == NULL) || !CustomData_has_layer(ldata, CD_MLOOPCOL)) {
        color_type = V3D_SHADING_OBJECT_COLOR;
      }
    }
  }

  if (r_sculpt_pbvh) {
    *r_sculpt_pbvh = is_sculpt_pbvh;
  }
  if (r_texpaint_mode) {
    *r_texpaint_mode = false;
  }

  if (!is_sculpt_pbvh && !is_render) {
    /* Force texture or vertex mode if object is in paint mode. */
    if (is_texpaint_mode && me && CustomData_has_layer(ldata, CD_MLOOPUV)) {
      color_type = V3D_SHADING_TEXTURE_COLOR;
      if (r_texpaint_mode) {
        *r_texpaint_mode = true;
      }
    }
    else if (is_vertpaint_mode && me && CustomData_has_layer(ldata, CD_MLOOPCOL)) {
      color_type = V3D_SHADING_VERTEX_COLOR;
    }
  }

  if (is_sculpt_pbvh && color_type == V3D_SHADING_TEXTURE_COLOR) {
    /* Force use of material color for sculpt. */
    color_type = V3D_SHADING_MATERIAL_COLOR;
  }

  if (r_draw_shadow) {
    *r_draw_shadow = (ob->dtx & OB_DRAW_NO_SHADOW_CAST) == 0 && SHADOW_ENABLED(wpd);
    /* Currently unsupported in sculpt mode. We could revert to the slow
     * method in this case but I'm not sure if it's a good idea given that
     * sculpted meshes are heavy to begin with. */
    if (is_sculpt_pbvh) {
      *r_draw_shadow = false;
    }

    if (is_active && draw_object_use_hide_faces(ob)) {
      *r_draw_shadow = false;
    }
  }

  return color_type;
}

void workbench_cache_populate(void *ved, Object *ob)
{
  DBenchData *vedata = ved;
  DBenchStorageList *stl = vedata->stl;
  DBenchPrivateData *wpd = stl->wpd;

  if (!draw_object_is_renderable(ob)) {
    return;
  }

  if (ob->type == OB_MESH && ob->modifiers.first != NULL) {
    bool use_texpaint_mode;
    int color_type = workbench_color_type_get(wpd, ob, NULL, &use_texpaint_mode, NULL);

    LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
      if (md->type != eModifierType_ParticleSystem) {
        continue;
      }
      ParticleSystem *psys = ((ParticleSystemModifierData *)md)->psys;
      if (!draw_object_is_visible_psys_in_active_context(ob, psys)) {
        continue;
      }
      ParticleSettings *part = psys->part;
      const int draw_as = (part->draw_as == PART_DRAW_REND) ? part->ren_as : part->draw_as;

      if (draw_as == PART_DRAW_PATH) {
        workbench_cache_hair_populate(
            wpd, ob, psys, md, color_type, use_texpaint_mode, part->omat);
      }
    }
  }

  if (!(ob->base_flag & BASE_FROM_DUPLI)) {
    ModifierData *md = dune_modifiers_findby_type(ob, eModifierType_Fluid);
    if (md && dune_modifier_is_enabled(wpd->scene, md, eModifierMode_Realtime)) {
      FluidModifierData *fmd = (FluidModifierData *)md;
      if (fmd->domain) {
        workbench_volume_cache_populate(vedata, wpd->scene, ob, md, V3D_SHADING_SINGLE_COLOR);
        if (fmd->domain->type == FLUID_DOMAIN_TYPE_GAS) {
          return; /* Do not draw solid in this case. */
        }
      }
    }
  }

  if (!(draw_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF)) {
    return;
  }

  if ((ob->dt < OB_SOLID) && !DRW_state_is_scene_render()) {
    return;
  }

  if (ELEM(ob->type, OB_MESH, OB_SURF, OB_MBALL, OB_POINTCLOUD)) {
    bool use_sculpt_pbvh, use_texpaint_mode, draw_shadow, has_transp_mat = false;
    eV3DShadingColorType color_type = workbench_color_type_get(
        wpd, ob, &use_sculpt_pbvh, &use_texpaint_mode, &draw_shadow);

    if (use_sculpt_pbvh) {
      workbench_cache_sculpt_populate(wpd, ob, color_type);
    }
    else if (use_texpaint_mode) {
      workbench_cache_texpaint_populate(wpd, ob);
    }
    else {
      workbench_cache_common_populate(wpd, ob, color_type, &has_transp_mat);
    }

    if (draw_shadow) {
      workbench_shadow_cache_populate(vedata, ob, has_transp_mat);
    }
  }
  else if (ob->type == OB_CURVES) {
    int color_type = workbench_color_type_get(wpd, ob, NULL, NULL, NULL);
    workbench_cache_hair_populate(wpd, ob, NULL, NULL, color_type, false, CURVES_MATERIAL_NR);
  }
  else if (ob->type == OB_VOLUME) {
    if (wpd->shading.type != OB_WIRE) {
      int color_type = workbench_color_type_get(wpd, ob, NULL, NULL, NULL);
      workbench_volume_cache_populate(vedata, wpd->scene, ob, NULL, color_type);
    }
  }
}

void workbench_cache_finish(void *ved)
{
  DBenchData *vedata = ved;
  DBenchStorageList *stl = vedata->stl;
  DBenchFramebufferList *fbl = vedata->fbl;
  DBenchPrivateData *wpd = stl->wpd;

  /* TODO: Only do this when really needed. */
  {
    /* HACK we allocate the in front depth here to avoid the overhead when if is not needed. */
    DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();
    DefaultTextureList *dtxl = draw_viewport_texture_list_get();

    draw_texture_ensure_fullscreen_2d(&dtxl->depth_in_front, GPU_DEPTH24_STENCIL8, 0);

    gpu_framebuffer_ensure_config(&dfbl->in_front_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(dtxl->depth_in_front),
                                      GPU_ATTACHMENT_TEXTURE(dtxl->color),
                                  });

    gpu_framebuffer_ensure_config(&fbl->opaque_infront_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(dtxl->depth_in_front),
                                      GPU_ATTACHMENT_TEXTURE(wpd->material_buffer_tx),
                                      GPU_ATTACHMENT_TEXTURE(wpd->normal_buffer_tx),
                                      GPU_ATTACHMENT_TEXTURE(wpd->object_id_tx),
                                  });

    gpu_framebuffer_ensure_config(&fbl->transp_accum_infront_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(dtxl->depth_in_front),
                                      GPU_ATTACHMENT_TEXTURE(wpd->accum_buffer_tx),
                                      GPU_ATTACHMENT_TEXTURE(wpd->reveal_buffer_tx),
                                  });
  }

  if (wpd->object_id_tx) {
    gpu_framebuffer_ensure_config(&fbl->id_clear_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(wpd->object_id_tx),
                                  });
  }
  else {
    GPU_FRAMEBUFFER_FREE_SAFE(fbl->id_clear_fb);
  }

  workbench_update_material_ubos(wpd);

  /* TODO: don't free reuse next redraw. */
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      for (int k = 0; k < WORKBENCH_DATATYPE_MAX; k++) {
        if (wpd->prepass[i][j][k].material_hash) {
          lib_ghash_free(wpd->prepass[i][j][k].material_hash, NULL, NULL);
          wpd->prepass[i][j][k].material_hash = NULL;
        }
      }
    }
  }
}

void workbench_draw_sample(void *ved)
{
  DBenchData *vedata = ved;
  DBenchFramebufferList *fbl = vedata->fbl;
  DBenchPrivateData *wpd = vedata->stl->wpd;
  DBenchPassList *psl = vedata->psl;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  const float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float clear_col_with_alpha[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  const bool do_render = workbench_antialiasing_setup(vedata);
  const bool xray_is_visible = wpd->shading.xray_alpha > 0.0f;
  const bool do_transparent_infront_pass = !DRW_pass_is_empty(psl->transp_accum_infront_ps);
  const bool do_transparent_pass = !DRW_pass_is_empty(psl->transp_accum_ps);
  const bool do_opaque_infront_pass = !DRW_pass_is_empty(psl->opaque_infront_ps);
  const bool do_opaque_pass = !DRW_pass_is_empty(psl->opaque_ps) || do_opaque_infront_pass;

  if (dfbl->in_front_fb) {
    gpu_framebuffer_bind(dfbl->in_front_fb);
    gpu_framebuffer_clear_depth(dfbl->in_front_fb, 1.0f);
  }

  if (do_render) {
    gpu_framebuffer_bind(dfbl->default_fb);
    gpu_framebuffer_clear_color_depth_stencil(dfbl->default_fb, wpd->background_color, 1.0f, 0x00);

    if (fbl->id_clear_fb) {
      gpu_framebuffer_bind(fbl->id_clear_fb);
      gpu_framebuffer_clear_color(fbl->id_clear_fb, clear_col);
    }

    if (do_opaque_pass) {
      gpu_framebuffer_bind(fbl->opaque_fb);
      draw_draw_pass(psl->opaque_ps);

      if (psl->shadow_ps[0]) {
        draw_draw_pass(psl->shadow_ps[0]);
        draw_draw_pass(psl->shadow_ps[1]);
      }

      if (do_opaque_infront_pass) {
        gpu_framebuffer_bind(fbl->opaque_infront_fb);
        draw_draw_pass(psl->opaque_infront_ps);

        gpu_framebuffer_bind(fbl->opaque_fb);
        draw_draw_pass(psl->merge_infront_ps);
      }

      gpu_framebuffer_bind(dfbl->default_fb);
      draw_draw_pass(psl->composite_ps);

      if (psl->cavity_ps) {
        gpu_framebuffer_bind(dfbl->color_only_fb);
        draw_draw_pass(psl->cavity_ps);
      }
    }

    workbench_volume_draw_pass(vedata);

    if (xray_is_visible) {
      if (do_transparent_pass) {
        gpu_framebuffer_bind(fbl->transp_accum_fb);
        gpu_framebuffer_clear_color(fbl->transp_accum_fb, clear_col_with_alpha);

        draw_draw_pass(psl->transp_accum_ps);

        gpu_framebuffer_bind(dfbl->color_only_fb);
        draw_draw_pass(psl->transp_resolve_ps);
      }

      if (do_transparent_infront_pass) {
        gpu_framebuffer_bind(fbl->transp_accum_infront_fb);
        gpu_framebuffer_clear_color(fbl->transp_accum_infront_fb, clear_col_with_alpha);

        draw_draw_pass(psl->transp_accum_infront_ps);

        gpu_framebuffer_bind(dfbl->color_only_fb);
        draw_draw_pass(psl->transp_resolve_ps);
      }
    }

    workbench_transparent_draw_depth_pass(vedata);

    if (psl->outline_ps) {
      gpu_framebuffer_bind(dfbl->color_only_fb);
      draw_draw_pass(psl->outline_ps);
    }

    workbench_dof_draw_pass(vedata);
  }

  workbench_antialiasing_draw_pass(vedata);
}

/* Viewport rendering. */
static void workbench_draw_scene(void *ved)
{
  DBenchData *vedata = ved;
  DBenchPrivateData *wpd = vedata->stl->wpd;

  if (draw_state_is_opengl_render()) {
    while (wpd->taa_sample < max_ii(1, wpd->taa_sample_len)) {
      workbench_update_world_ubo(wpd);

      workbench_draw_sample(vedata);
    }
  }
  else {
    workbench_draw_sample(vedata);
  }

  workbench_draw_finish(vedata);
}

void workbench_draw_finish(void *ved)
{
  DBenchData *vedata = ved;
  workbench_volume_draw_finish(vedata);
  /* Reset default view. */
  draw_view_set_active(NULL);
}

static void workbench_engine_free(void)
{
  workbench_shader_free();
}

static void workbench_view_update(void *vedata)
{
  DBenchData *data = vedata;
  workbench_antialiasing_view_updated(data);
}

static void workbench_id_update(void *UNUSED(vedata), struct ID *id)
{
  if (GS(id->name) == ID_OB) {
    WORKBENCH_ObjectData *oed = (WORKBENCH_ObjectData *)DRW_drawdata_get(id,
                                                                         &draw_engine_workbench);
    if (oed != NULL && oed->dd.recalc != 0) {
      oed->shadow_bbox_dirty = (oed->dd.recalc & ID_RECALC_ALL) != 0;
      oed->dd.recalc = 0;
    }
  }
}

static const DrawEngineDataSize workbench_data_size = DRW_VIEWPORT_DATA_SIZE(WORKBENCH_Data);

DrawEngineType draw_engine_workbench = {
    NULL,
    NULL,
    N_("Workbench"),
    &workbench_data_size,
    &workbench_engine_init,
    &workbench_engine_free,
    NULL, /* instance_free */
    &workbench_cache_init,
    &workbench_cache_populate,
    &workbench_cache_finish,
    &workbench_draw_scene,
    &workbench_view_update,
    &workbench_id_update,
    &workbench_render,
    NULL,
};

RenderEngineType draw_engine_viewport_workbench_type = {
    NULL,
    NULL,
    WORKBENCH_ENGINE,
    N_("Workbench"),
    RE_INTERNAL | RE_USE_STEREO_VIEWPORT | RE_USE_GPU_CONTEXT,
    NULL,
    &draw_render_to_image,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &workbench_render_update_passes,
    &draw_engine_workbench,
    {NULL, NULL, NULL},
};

#undef WORKBENCH_ENGINE
