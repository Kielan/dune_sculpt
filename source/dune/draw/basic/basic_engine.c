/* Simple engine for drwing color and/or depth.
 * When we only need simple flat shaders. */

#include "drw_render.h"

#include "dune_ob.h"
#include "dune_paint.h"
#include "dune_particle.h"

#include "lib_alloca.h"

#include "types_particle.h"

#include "gpu_shader.h"

#include "basic_engine.h"
#include "basic_private.h"

#define BASIC_ENGINE "DUNE_BASIC"

/* LISTS */

/* GPUViewport.storage
 * Is freed every time the viewport engine changes. */
typedef struct BASIC_StorageList {
  struct BASIC_PrivateData *g_data;
} BASIC_StorageList;

typedef struct BASIC_PassList {
  struct DRWPass *depth_pass[2];
  struct DRWPass *depth_pass_pointcloud[2];
  struct DRWPass *depth_pass_cull[2];
} BASIC_PassList;

typedef struct BASIC_Data {
  void *engine_type;
  DRWViewportEmptyList *fbl;
  DRWViewportEmptyList *txl;
  BASIC_PassList *psl;
  BASIC_StorageList *stl;
} BASIC_Data;

/* STATIC */

typedef struct BASIC_PrivateData {
  DRWShadingGroup *depth_shgrp[2];
  DRWShadingGroup *depth_shgrp_cull[2];
  DRWShadingGroup *depth_hair_shgrp[2];
  DRWShadingGroup *depth_pointcloud_shgrp[2];
  bool use_material_slot_selection;
} BASIC_PrivateData; /* Transient data */

static void basic_cache_init(void *vedata)
{
  BASIC_PassList *psl = ((BASIC_Data *)vedata)->psl;
  BASIC_StorageList *stl = ((BASIC_Data *)vedata)->stl;
  DrwShadingGroup *grp;

  const DrwCxtState *dre_cxt = drw_cxt_state_get();

  if (!stl->g_data) {
    /* Alloc transient ptrs */
    stl->g_data = mem_calloc(sizeof(*stl->g_data), __func__);
  }

  stl->g_data->use_material_slot_sel = drw_state_is_material_sel();

  /* Twice for normal and in front obs. */
  for (int i = 0; i < 2; i++) {
    DrwState clip_state = (drw_ctx->sh_cfg == GPU_SHADER_CFG_CLIPPED) ? DRW_STATE_CLIP_PLANES : 0;
    DreState infront_state = (drw_state_is_select() && (i == 1)) ? DRW_STATE_IN_FRONT_SELECT : 0;
    DrwState state = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;

    GPUShader *sh = drw_state_is_sel() ?
                    BASIC_shaders_depth_conservative_sh_get(drw_cxt->sh_cfg) :
                    BASIC_shaders_depth_sh_get(draw_ctx->sh_cfg);

    DRW_PASS_CREATE(psl->depth_pass[i], state | clip_state | infront_state);
    stl->g_data->depth_shgrp[i] = grp = drw_shgroup_create(sh, psl->depth_pass[i]);
    drw_shgroup_uniform_vec2(grp, "sizeViewport", drw_viewport_size_get(), 1);
    drw_shgroup_uniform_vec2(grp, "sizeViewportInv", drw_viewport_invert_size_get(), 1);

    sh = drw_state_is_select() ?
             BASIC_shaders_pointcloud_depth_conservative_sh_get(draw_ctx->sh_cfg) :
             BASIC_shaders_pointcloud_depth_sh_get(draw_ctx->sh_cfg);
    DRW_PASS_CREATE(psl->depth_pass_pointcloud[i], state | clip_state | infront_state);
    stl->g_data->depth_pointcloud_shgrp[i] = grp = drw_shgroup_create(
        sh, psl->depth_pass_pointcloud[i]);
    drw_shgroup_uniform_vec2(grp, "sizeViewport", drw_viewport_size_get(), 1);
    drw_shgroup_uniform_vec2(grp, "sizeViewportInv", drw_viewport_invert_size_get(), 1);

    stl->g_data->depth_hair_shgrp[i] = grp = drw_shgroup_create(
        BASIC_shaders_depth_sh_get(drw_cxt->sh_cfg), psl->depth_pass[i]);

    sh = drw_state_is_sel() ? BASIC_shaders_depth_conservative_sh_get(drw_cxt->sh_cfg) :
                                 BASIC_shaders_depth_sh_get(drw_cxt->sh_cfg);
    state |= DRW_STATE_CULL_BACK;
    DRW_PASS_CREATE(psl->depth_pass_cull[i], state | clip_state | infront_state);
    stl->g_data->depth_shgrp_cull[i] = grp = drw_shgroup_create(sh, psl->depth_pass_cull[i]);
    DRW_shgroup_uniform_vec2(grp, "sizeViewport", drw_viewport_size_get(), 1);
    DRW_shgroup_uniform_vec2(grp, "sizeViewportInv", drw_viewport_invert_size_get(), 1);
  }
}

/* TODO: DRW_cache_object_surface_material_get needs a refactor to allow passing NULL
 * instead of gpumat_array. Avoiding all this boilerplate code. */
static struct GPUBatch **basic_ob_surface_material_get(Ob *ob)
{
  const int materials_len = drw_cache_ob_material_count_get(ob);
  struct GPUMaterial **gpumat_array = lib_array_alloca(gpumat_array, materials_len);
  memset(gpumat_array, 0, sizeof(*gpumat_array) * materials_len);

  return drw_cache_ob_surface_material_get(ob, gpumat_array, materials_len);
}

static void basic_cache_populate_particles(void *vedata, Ob *ob)
{
  const bool do_in_front = (ob->dtx & OB_DRW_IN_FRONT) != 0;
  BASIC_StorageList *stl = ((BASIC_Data *)vedata)->stl;
  for (ParticleSys *psys = ob->particlesys.first; psys != NULL; psys = psys->next) {
    if (!drw_ob_is_visible_psys_in_active_cxt(ob, psys)) {
      continue;
    }
    ParticleSettings *part = psys->part;
    const int drw_as = (part->drw_as == PART_DRE_REND) ? part->ren_as : part->drw_as;
    if (drw_as == PART_DRW_PATH) {
      struct GPUBatch *hairs = drw_cache_particles_get_hair(ob, psys, NULL);
      if (stl->g_data->use_material_slot_sel) {
        const short material_slot = part->omat;
        drw_sel_load_id(ob->runtime.sel_id | (material_slot << 16));
      }
      drw_shgroup_call(stl->g_data->depth_hair_shgrp[do_in_front], hairs, NULL);
    }
  }
}

static void basic_cache_populate(void *vedata, Ob *ob)
{
  BASIC_StorageList *stl = ((BASIC_Data *)vedata)->stl;

  /* TODO: fix sel of smoke domains. */

  if (!drw_ob_is_renderable(ob) || (ob->dt < OB_SOLID)) {
    return;
  }

  const rwCxtState *drw_ctx = drw_cxt_state_get();
  if (ob != draw_ctx->object_edit) {
    basic_cache_populate_particles(vedata, ob);
  }

  /* Make flat ob selectable in ortho view if wireframe is enabled. */
  const bool do_in_front = (ob->dtx & OB_DRW_IN_FRONT) != 0;
  if ((drw_ctx->v3d->overlay.flag & V3D_OVERLAY_WIREFRAMES) ||
      (drw_ctx->v3d->shading.type == OB_WIRE) || (ob->dtx & OB_DRAWWIRE) || (ob->dt == OB_WIRE)) {
    int flat_axis = 0;
    bool is_flat_ob_viewed_from_side = ((draw_ctx->rv3d->persp == RV3D_ORTHO) &&
                                            drw_ob_is_flat(ob, &flat_axis) &&
                                            drw_ob_axis_orthogonal_to_view(ob, flat_axis));

    if (is_flat_ob_viewed_from_side) {
      /* Avoid losing flat objects when in ortho views (see T56549) */
      struct GPUBatch *geom = dra_cache_ob_all_edges_get(ob);
      if (geom) {
        drw_shgroup_call(stl->g_data->depth_shgrp[do_in_front], geom, ob);
      }
      return;
    }
  }

  const bool use_sculpt_pbvh = dune_sculptsession_use_pbvh_drw(ob, drw_ctx->v3d) &&
                               !drw_state_is_img_render();
  const bool do_cull = (drw_cxt->v3d &&
                        (drw_cxt->v3d->shading.flag & V3D_SHADING_BACKFACE_CULLING));

  DRWShadingGroup *shgrp = NULL;

  if (ob->type == OB_POINTCLOUD) {
    shgrp = stl->g_data->depth_pointcloud_shgrp[do_in_front];
  }
  else {
    shgrp = (do_cull) ? stl->g_data->depth_shgrp_cull[do_in_front] :
                        stl->g_data->depth_shgrp[do_in_front];
  }

  if (use_sculpt_pbvh) {
    drw_shgroup_call_sculpt(shgrp, ob, false, false);
  }
  else {
    if (stl->g_data->use_material_slot_selection && dune_ob_supports_material_slots(ob)) {
      struct GPUBatch **geoms = basic_ob_surface_material_get(ob);
      if (geoms) {
        const int materials_len = drw_cache_ob_material_count_get(ob);
        for (int i = 0; i < materials_len; i++) {
          if (geoms[i] == NULL) {
            continue;
          }
          const short material_slot_sel_id = i + 1;
          drw_sel_load_id(ob->runtime.sel_id | (material_slot_sel_id << 16));
          drw_shgroup_call(shgrp, geoms[i], ob);
        }
      }
    }
    else {
      struct GPUBatch *geom = drw_cache_ob_surface_get(ob);
      if (geom) {
        drw_shgroup_call(shgrp, geom, ob);
      }
    }
  }
}

static void basic_cache_finish(void *vedata)
{
  BASIC_StorageList *stl = ((BASIC_Data *)vedata)->stl;

  UNUSED_VARS(stl);
}

static void basic_drw_scene(void *vedata)
{
  basic_PassList *psl = ((BASIC_Data *)vedata)->psl;

  drw_pass(psl->depth_pass[0]);
  drw_pass(psl->depth_pass_pointcloud[0]);
  drw_pass(psl->depth_pass_cull[0]);
  drw_pass(psl->depth_pass[1]);
  drw_pass(psl->depth_pass_pointcloud[1]);
  drw_pass(psl->depth_pass_cull[1]);
}

static void basic_engine_free(void)
{
  basic_shaders_free();
}

static const DrwEngineDataSize basic_data_size = DRW_VIEWPORT_DATA_SIZE(BASIC_Data);

DrwEngineType drw_engine_basic_type = {
    NULL,
    NULL,
    N_("Basic"),
    &basic_data_size,
    NULL,
    &basic_engine_free,
    NULL, /* instance_free */
    &basic_cache_init,
    &basic_cache_populate,
    &basic_cache_finish,
    &basic_drw_scene,
    NULL,
    NULL,
    NULL,
    NULL,
};

#undef BASIC_ENGINE
