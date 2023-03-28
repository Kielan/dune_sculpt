#include "workbench_private.h"

#include "types_fluid.h"
#include "types_modifier.h"
#include "types_object_force.h"
#include "types_volume.h"

#include "lib_dynstr.h"
#include "lib_listbase.h"
#include "lib_rand.h"
#include "lib_string_utils.h"

#include "dune_fluid.h"
#include "dune_global.h"
#include "dune_object.h"
#include "dune_volume.h"
#include "dune_volume_render.h"

void workbench_volume_engine_init(DBenchData *vedata)
{
  DBenchTextureList *txl = vedata->txl;

  if (txl->dummy_volume_tx == NULL) {
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    txl->dummy_volume_tx = gpu_texture_create_3d(
        "dummy_volume", 1, 1, 1, 1, GPU_RGBA8, GPU_DATA_FLOAT, zero);
    txl->dummy_shadow_tx = gpu_texture_create_3d(
        "dummy_shadow", 1, 1, 1, 1, GPU_RGBA8, GPU_DATA_FLOAT, one);
    txl->dummy_coba_tx = gpu_texture_create_1d("dummy_coba", 1, 1, GPU_RGBA8, zero);
  }
}

void workbench_volume_cache_init(DBenchData *vedata)
{
  vedata->psl->volume_ps = draw_pass_create(
      "Volumes", DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_ALPHA_PREMUL | DRW_STATE_CULL_FRONT);

  vedata->stl->wpd->volumes_do = false;
}

static void workbench_volume_modifier_cache_populate(DBenchData *vedata,
                                                     Object *ob,
                                                     ModifierData *md)
{
  FluidModifierData *fmd = (FluidModifierData *)md;
  FluidDomainSettings *fds = fmd->domain;
  DBenchPrivateData *wpd = vedata->stl->wpd;
  DBenchTextureList *txl = vedata->txl;
  DefaultTextureList *dtxl = draw_viewport_texture_list_get();
  DrawShadingGroup *grp = NULL;

  if (!fds->fluid) {
    return;
  }

  wpd->volumes_do = true;
  if (fds->use_coba) {
    draw_smoke_ensure_coba_field(fmd);
  }
  else if (fds->type == FLUID_DOMAIN_TYPE_GAS) {
    draw_smoke_ensure(fmd, fds->flags & FLUID_DOMAIN_USE_NOISE);
  }
  else {
    return;
  }

  if ((!fds->use_coba && (fds->tex_density == NULL && fds->tex_color == NULL)) ||
      (fds->use_coba && fds->tex_field == NULL)) {
    return;
  }

  const bool use_slice = (fds->axis_slice_method == AXIS_SLICE_SINGLE);
  const bool show_phi = ELEM(fds->coba_field,
                             FLUID_DOMAIN_FIELD_PHI,
                             FLUID_DOMAIN_FIELD_PHI_IN,
                             FLUID_DOMAIN_FIELD_PHI_OUT,
                             FLUID_DOMAIN_FIELD_PHI_OBSTACLE);
  const bool show_flags = (fds->coba_field == FLUID_DOMAIN_FIELD_FLAGS);
  const bool show_pressure = (fds->coba_field == FLUID_DOMAIN_FIELD_PRESSURE);
  eDBenchVolumeInterpType interp_type = WORKBENCH_VOLUME_INTERP_LINEAR;

  switch ((FLUID_DisplayInterpolationMethod)fds->interp_method) {
    case FLUID_DISPLAY_INTERP_LINEAR:
      interp_type = WORKBENCH_VOLUME_INTERP_LINEAR;
      break;
    case FLUID_DISPLAY_INTERP_CUBIC:
      interp_type = WORKBENCH_VOLUME_INTERP_CUBIC;
      break;
    case FLUID_DISPLAY_INTERP_CLOSEST:
      interp_type = WORKBENCH_VOLUME_INTERP_CLOSEST;
      break;
  }

  GPUShader *sh = workbench_shader_volume_get(use_slice, fds->use_coba, interp_type, true);

  if (use_slice) {
    float invviewmat[4][4];
    draw_view_viewmat_get(NULL, invviewmat, true);

    const int axis = (fds->slice_axis == SLICE_AXIS_AUTO) ?
                         axis_dominant_v3_single(invviewmat[2]) :
                         fds->slice_axis - 1;
    float dim[3];
    dune_object_dimensions_get(ob, dim);
    /* 0.05f to achieve somewhat the same opacity as the full view. */
    float step_length = max_ff(1e-16f, dim[axis] * 0.05f);

    grp = draw_shgroup_create(sh, vedata->psl->volume_ps);
    draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
    draw_shgroup_uniform_float_copy(grp, "slicePosition", fds->slice_depth);
    draw_shgroup_uniform_int_copy(grp, "sliceAxis", axis);
    draw_shgroup_uniform_float_copy(grp, "stepLength", step_length);
    draw_shgroup_state_disable(grp, DRAW_STATE_CULL_FRONT);
  }
  else {
    double noise_ofs;
    lib_halton_1d(3, 0.0, wpd->taa_sample, &noise_ofs);
    float dim[3], step_length, max_slice;
    float slice_ct[3] = {fds->res[0], fds->res[1], fds->res[2]};
    mul_v3_fl(slice_ct, max_ff(0.001f, fds->slice_per_voxel));
    max_slice = max_fff(slice_ct[0], slice_ct[1], slice_ct[2]);
    dune_object_dimensions_get(ob, dim);
    invert_v3(slice_ct);
    mul_v3_v3(dim, slice_ct);
    step_length = len_v3(dim);

    grp = draw_shgroup_create(sh, vedata->psl->volume_ps);
    draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
    draw_shgroup_uniform_int_copy(grp, "samplesLen", max_slice);
    draw_shgroup_uniform_float_copy(grp, "stepLength", step_length);
    draw_shgroup_uniform_float_copy(grp, "noiseOfs", noise_ofs);
    draw_shgroup_state_enable(grp, DRAW_STATE_CULL_FRONT);
  }

  if (fds->use_coba) {
    if (show_flags) {
      draw_shgroup_uniform_texture(grp, "flagTexture", fds->tex_field);
    }
    else {
      draw_shgroup_uniform_texture(grp, "densityTexture", fds->tex_field);
    }
    if (!show_phi && !show_flags && !show_pressure) {
      draw_shgroup_uniform_texture(grp, "transferTexture", fds->tex_coba);
    }
    draw_shgroup_uniform_float_copy(grp, "gridScale", fds->grid_scale);
    draw_shgroup_uniform_bool_copy(grp, "showPhi", show_phi);
    draw_shgroup_uniform_bool_copy(grp, "showFlags", show_flags);
    draw_shgroup_uniform_bool_copy(grp, "showPressure", show_pressure);
  }
  else {
    static float white[3] = {1.0f, 1.0f, 1.0f};
    bool use_constant_color = ((fds->active_fields & FLUID_DOMAIN_ACTIVE_COLORS) == 0 &&
                               (fds->active_fields & FLUID_DOMAIN_ACTIVE_COLOR_SET) != 0);
    draw_shgroup_uniform_texture(
        grp, "densityTexture", (fds->tex_color) ? fds->tex_color : fds->tex_density);
    draw_shgroup_uniform_texture(grp, "shadowTexture", fds->tex_shadow);
    draw_shgroup_uniform_texture(
        grp, "flameTexture", (fds->tex_flame) ? fds->tex_flame : txl->dummy_volume_tx);
    draw_shgroup_uniform_texture(
        grp, "flameColorTexture", (fds->tex_flame) ? fds->tex_flame_coba : txl->dummy_coba_tx);
    draw_shgroup_uniform_vec3(
        grp, "activeColor", (use_constant_color) ? fds->active_color : white, 1);
  }
  draw_shgroup_uniform_texture_ref(grp, "depthBuffer", &dtxl->depth);
  draw_shgroup_uniform_float_copy(grp, "densityScale", 10.0f * fds->display_thickness);

  if (use_slice) {
    draw_shgroup_call(grp, draw_cache_quad_get(), ob);
  }
  else {
    draw_shgroup_call(grp, draw_cache_cube_get(), ob);
  }

  lib_addtail(&wpd->smoke_domains, lib_genericNodeN(fmd));
}

static void workbench_volume_material_color(DBenchPrivateData *wpd,
                                            Object *ob,
                                            eV3DShadingColorType color_type,
                                            float color[3])
{
  Material *ma = dune_object_material_get_eval(ob, VOLUME_MATERIAL_NR);
  DBenchUBOMaterial ubo_data;
  workbench_material_ubo_data(wpd, ob, ma, &ubo_data, color_type);
  copy_v3_v3(color, ubo_data.base_color);
}

static void workbench_volume_object_cache_populate(DBenchData *vedata,
                                                   Object *ob,
                                                   eV3DShadingColorType color_type)
{
  /* Create 3D textures. */
  Volume *volume = ob->data;
  dune_volume_load(volume, G.main);
  const VolumeGrid *volume_grid = dune_volume_grid_active_get_for_read(volume);
  if (volume_grid == NULL) {
    return;
  }
  DrawVolumeGrid *grid = draw_volume_batch_cache_get_grid(volume, volume_grid);
  if (grid == NULL) {
    return;
  }

  DBenchPrivateData *wpd = vedata->stl->wpd;
  DBenchTextureList *txl = vedata->txl;
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  DrawShadingGroup *grp = NULL;

  wpd->volumes_do = true;
  const bool use_slice = (volume->display.axis_slice_method == AXIS_SLICE_SINGLE);
  eDBenchVolumeInterpType interp_type = WORKBENCH_VOLUME_INTERP_LINEAR;

  switch ((VolumeDisplayInterpMethod)volume->display.interpolation_method) {
    case VOLUME_DISPLAY_INTERP_LINEAR:
      interp_type = WORKBENCH_VOLUME_INTERP_LINEAR;
      break;
    case VOLUME_DISPLAY_INTERP_CUBIC:
      interp_type = WORKBENCH_VOLUME_INTERP_CUBIC;
      break;
    case VOLUME_DISPLAY_INTERP_CLOSEST:
      interp_type = WORKBENCH_VOLUME_INTERP_CLOSEST;
      break;
  }

  /* Create shader. */
  GPUShader *sh = workbench_shader_volume_get(use_slice, false, interp_type, false);

  /* Compute color. */
  float color[3];
  workbench_volume_material_color(wpd, ob, color_type, color);

  /* Combined texture to object, and object to world transform. */
  float texture_to_world[4][4];
  mul_m4_m4m4(texture_to_world, ob->obmat, grid->texture_to_object);

  if (use_slice) {
    float invviewmat[4][4];
    draw_view_viewmat_get(NULL, invviewmat, true);

    const int axis = (volume->display.slice_axis == SLICE_AXIS_AUTO) ?
                         axis_dominant_v3_single(invviewmat[2]) :
                         volume->display.slice_axis - 1;

    float dim[3];
    dune_object_dimensions_get(ob, dim);
    /* 0.05f to achieve somewhat the same opacity as the full view. */
    float step_length = max_ff(1e-16f, dim[axis] * 0.05f);

    const float slice_position = volume->display.slice_depth;

    grp = draw_shgroup_create(sh, vedata->psl->volume_ps);
    draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
    draw_shgroup_uniform_float_copy(grp, "slicePosition", slice_position);
    draw_shgroup_uniform_int_copy(grp, "sliceAxis", axis);
    draw_shgroup_uniform_float_copy(grp, "stepLength", step_length);
    draw_shgroup_state_disable(grp, DRAW_STATE_CULL_FRONT);
  }
  else {
    /* Compute world space dimensions for step size. */
    float world_size[3];
    mat4_to_size(world_size, texture_to_world);
    abs_v3(world_size);

    /* Compute step parameters. */
    double noise_ofs;
    lib_halton_1d(3, 0.0, wpd->taa_sample, &noise_ofs);
    float step_length, max_slice;
    int resolution[3];
    gpu_texture_get_mipmap_size(grid->texture, 0, resolution);
    float slice_ct[3] = {resolution[0], resolution[1], resolution[2]};
    mul_v3_fl(slice_ct, max_ff(0.001f, 5.0f));
    max_slice = max_fff(slice_ct[0], slice_ct[1], slice_ct[2]);
    invert_v3(slice_ct);
    mul_v3_v3(slice_ct, world_size);
    step_length = len_v3(slice_ct);

    /* Set uniforms. */
    grp = draw_shgroup_create(sh, vedata->psl->volume_ps);
    draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
    draw_shgroup_uniform_int_copy(grp, "samplesLen", max_slice);
    draw_shgroup_uniform_float_copy(grp, "stepLength", step_length);
    draw_shgroup_uniform_float_copy(grp, "noiseOfs", noise_ofs);
    draw_shgroup_state_enable(grp, DRAW_STATE_CULL_FRONT);
  }

  /* Compute density scale. */
  const float density_scale = volume->display.density *
                              dune_volume_density_scale(volume, ob->obmat);

  draw_shgroup_uniform_texture(grp, "densityTexture", grid->texture);
  /* TODO: implement shadow texture, see manta_smoke_calc_transparency. */
  draw_shgroup_uniform_texture(grp, "shadowTexture", txl->dummy_shadow_tx);
  draw_shgroup_uniform_vec3_copy(grp, "activeColor", color);

  draw_shgroup_uniform_texture_ref(grp, "depthBuffer", &dtxl->depth);
  draw_shgroup_uniform_float_copy(grp, "densityScale", density_scale);

  draw_shgroup_uniform_mat4(grp, "volumeObjectToTexture", grid->object_to_texture);
  draw_shgroup_uniform_mat4(grp, "volumeTextureToObject", grid->texture_to_object);

  draw_shgroup_call(grp, draw_cache_cube_get(), ob);
}

void workbench_volume_cache_populate(DBenchData *vedata,
                                     Scene *UNUSED(scene),
                                     Object *ob,
                                     ModifierData *md,
                                     eV3DShadingColorType color_type)
{
  if (md == NULL) {
    workbench_volume_object_cache_populate(vedata, ob, color_type);
  }
  else {
    workbench_volume_modifier_cache_populate(vedata, ob, md);
  }
}

void workbench_volume_draw_pass(DBenchData *vedata)
{
  DBenchPassList *psl = vedata->psl;
  DBenchPrivateData *wpd = vedata->stl->wpd;
  DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();

  if (wpd->volumes_do) {
    gpu_framebuffer_bind(dfbl->color_only_fb);
    draw_draw_pass(psl->volume_ps);
  }
}

void workbench_volume_draw_finish(DBenchData *vedata)
{
  DBenchPrivateData *wpd = vedata->stl->wpd;

  /* Free Smoke Textures after rendering */
  /* XXX This is a waste of processing and GPU bandwidth if nothing
   * is updated. But the problem is since Textures are stored in the
   * modifier we don't want them to take precious VRAM if the
   * modifier is not used for display. We should share them for
   * all viewport in a redraw at least. */
  LISTBASE_FOREACH (LinkData *, link, &wpd->smoke_domains) {
    FluidModifierData *fmd = (FluidModifierData *)link->data;
    draw_smoke_free(fmd);
  }
  lib_freelistn(&wpd->smoke_domains);
}
