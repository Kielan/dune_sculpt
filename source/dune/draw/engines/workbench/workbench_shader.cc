#include "draw_render.h"

#include <string>

#include "workbench_engine.h"
#include "workbench_private.h"

/* Maximum number of variations. */
#define MAX_LIGHTING 3

enum eDBenchTextureType {
  TEXTURE_SH_NONE = 0,
  TEXTURE_SH_SINGLE,
  TEXTURE_SH_TILED,
  TEXTURE_SH_MAX,
};

static struct {
  struct GPUShader
      *opaque_prepass_sh_cache[GPU_SHADER_CFG_LEN][WORKBENCH_DATATYPE_MAX][TEXTURE_SH_MAX];
  struct GPUShader *transp_prepass_sh_cache[GPU_SHADER_CFG_LEN][WORKBENCH_DATATYPE_MAX]
                                           [MAX_LIGHTING][TEXTURE_SH_MAX];

  struct GPUShader *opaque_composite_sh[MAX_LIGHTING];
  struct GPUShader *oit_resolve_sh;
  struct GPUShader *outline_sh;
  struct GPUShader *merge_infront_sh;

  struct GPUShader *shadow_depth_pass_sh[2];
  struct GPUShader *shadow_depth_fail_sh[2][2];

  struct GPUShader *cavity_sh[2][2];

  struct GPUShader *dof_prepare_sh;
  struct GPUShader *dof_downsample_sh;
  struct GPUShader *dof_blur1_sh;
  struct GPUShader *dof_blur2_sh;
  struct GPUShader *dof_resolve_sh;

  struct GPUShader *aa_accum_sh;
  struct GPUShader *smaa_sh[3];

  struct GPUShader *volume_sh[2][2][3][2];

} e_data = {{{{nullptr}}}};

/* -------------------------------------------------------------------- */
/** Conversions **/

static const char *workbench_lighting_mode_to_str(int light)
{
  switch (light) {
    default:
      lib_assert_msg(0, "Error: Unknown lighting mode.");
      ATTR_FALLTHROUGH;
    case V3D_LIGHTING_STUDIO:
      return "_studio";
    case V3D_LIGHTING_MATCAP:
      return "_matcap";
    case V3D_LIGHTING_FLAT:
      return "_flat";
      return "";
  }
}

static const char *workbench_datatype_mode_to_str(eWORKBENCH_DataType datatype)
{
  switch (datatype) {
    default:
      lib_assert_msg(0, "Error: Unknown data mode.");
      ATTR_FALLTHROUGH;
    case WORKBENCH_DATATYPE_MESH:
      return "_mesh";
    case WORKBENCH_DATATYPE_HAIR:
      return "_hair";
    case WORKBENCH_DATATYPE_POINTCLOUD:
      return "_ptcloud";
  }
}

static const char *workbench_volume_interp_to_str(eWORKBENCH_VolumeInterpType interp_type)
{
  switch (interp_type) {
    default:
      lib_assert_msg(0, "Error: Unknown lighting mode.");
      ATTR_FALLTHROUGH;
    case WORKBENCH_VOLUME_INTERP_LINEAR:
      return "_linear";
    case WORKBENCH_VOLUME_INTERP_CUBIC:
      return "_cubic";
    case WORKBENCH_VOLUME_INTERP_CLOSEST:
      return "_closest";
  }
}

static const char *workbench_texture_type_to_str(eWORKBENCH_TextureType tex_type)
{
  switch (tex_type) {
    default:
      lib_assert_msg(0, "Error: Unknown texture mode.");
      ATTR_FALLTHROUGH;
    case TEXTURE_SH_NONE:
      return "_tex_none";
    case TEXTURE_SH_TILED:
      return "_tex_tile";
    case TEXTURE_SH_SINGLE:
      return "_tex_single";
  }
}

static eDBenchTextureType workbench_texture_type_get(bool textured, bool tiled)
{
  return textured ? (tiled ? TEXTURE_SH_TILED : TEXTURE_SH_SINGLE) : TEXTURE_SH_NONE;
}

/* -------------------------------------------------------------------- */
/** Shader request **/

static GPUShader *workbench_shader_get_ex(DBenchPrivateData *wpd,
                                          bool transp,
                                          eDBenchDataType datatype,
                                          bool textured,
                                          bool tiled)
{
  eDBenchTextureType tex_type = workbench_texture_type_get(textured, tiled);
  int light = wpd->shading.light;
  lib_assert(light < MAX_LIGHTING);
  struct GPUShader **shader =
      (transp) ? &e_data.transp_prepass_sh_cache[wpd->sh_cfg][datatype][light][tex_type] :
                 &e_data.opaque_prepass_sh_cache[wpd->sh_cfg][datatype][tex_type];

  if (*shader == nullptr) {
    std::string create_info_name = "workbench";
    create_info_name += (transp) ? "_transp" : "_opaque";
    if (transp) {
      create_info_name += workbench_lighting_mode_to_str(light);
    }
    create_info_name += workbench_datatype_mode_to_str(datatype);
    create_info_name += workbench_texture_type_to_str(tex_type);
    create_info_name += (wpd->sh_cfg == GPU_SHADER_CFG_CLIPPED) ? "_clip" : "_no_clip";

    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return *shader;
}

GPUShader *workbench_shader_opaque_get(DBenchPrivateData *wpd, eDBenchDataType datatype)
{
  return workbench_shader_get_ex(wpd, false, datatype, false, false);
}

GPUShader *workbench_shader_opaque_image_get(DBenchPrivateData *wpd,
                                             eDBenchDataType datatype,
                                             bool tiled)
{
  return workbench_shader_get_ex(wpd, false, datatype, true, tiled);
}

GPUShader *workbench_shader_transparent_get(DBenchPrivateData *wpd,
                                            eDBenchDataType datatype)
{
  return workbench_shader_get_ex(wpd, true, datatype, false, false);
}

GPUShader *workbench_shader_transparent_image_get(DBenchPrivateData *wpd,
                                                  eDBenchDataType datatype,
                                                  bool tiled)
{
  return workbench_shader_get_ex(wpd, true, datatype, true, tiled);
}

GPUShader *workbench_shader_composite_get(DBenchPrivateData *wpd)
{
  int light = wpd->shading.light;
  struct GPUShader **shader = &e_data.opaque_composite_sh[light];
  lib_assert(light < MAX_LIGHTING);

  if (*shader == nullptr) {
    std::string create_info_name = "workbench_composite";
    create_info_name += workbench_lighting_mode_to_str(light);
    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return *shader;
}

GPUShader *workbench_shader_merge_infront_get(DBenchPrivateData *UNUSED(wpd))
{
  if (e_data.merge_infront_sh == nullptr) {
    e_data.merge_infront_sh = gpu_shader_create_from_info_name("workbench_merge_infront");
  }
  return e_data.merge_infront_sh;
}

GPUShader *workbench_shader_transparent_resolve_get(DBenchPrivateData *UNUSED(wpd))
{
  if (e_data.oit_resolve_sh == nullptr) {
    e_data.oit_resolve_sh = gpu_shader_create_from_info_name("workbench_transparent_resolve");
  }
  return e_data.oit_resolve_sh;
}

static GPUShader *workbench_shader_shadow_pass_get_ex(bool depth_pass, bool manifold, bool cap)
{
  struct GPUShader **shader = (depth_pass) ? &e_data.shadow_depth_pass_sh[manifold] :
                                             &e_data.shadow_depth_fail_sh[manifold][cap];

  if (*shader == nullptr) {
    std::string create_info_name = "workbench_shadow";
    create_info_name += (depth_pass) ? "_pass" : "_fail";
    create_info_name += (manifold) ? "_manifold" : "_no_manifold";
    create_info_name += (cap) ? "_caps" : "_no_caps";
#if DEBUG_SHADOW_VOLUME
    create_info_name += "_debug";
#endif
    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return *shader;
}

GPUShader *workbench_shader_shadow_pass_get(bool manifold)
{
  return workbench_shader_shadow_pass_get_ex(true, manifold, false);
}

GPUShader *workbench_shader_shadow_fail_get(bool manifold, bool cap)
{
  return workbench_shader_shadow_pass_get_ex(false, manifold, cap);
}

GPUShader *workbench_shader_cavity_get(bool cavity, bool curvature)
{
  lib_assert(cavity || curvature);
  struct GPUShader **shader = &e_data.cavity_sh[cavity][curvature];

  if (*shader == nullptr) {
    std::string create_info_name = "workbench_effect";
    create_info_name += (cavity) ? "_cavity" : "";
    create_info_name += (curvature) ? "_curvature" : "";
    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return *shader;
}

GPUShader *workbench_shader_outline_get(void)
{
  if (e_data.outline_sh == nullptr) {
    e_data.outline_sh = gpu_shader_create_from_info_name("workbench_effect_outline");
  }
  return e_data.outline_sh;
}

void workbench_shader_depth_of_field_get(GPUShader **prepare_sh,
                                         GPUShader **downsample_sh,
                                         GPUShader **blur1_sh,
                                         GPUShader **blur2_sh,
                                         GPUShader **resolve_sh)
{
  if (e_data.dof_prepare_sh == nullptr) {
    e_data.dof_prepare_sh = gpu_shader_create_from_info_name("workbench_effect_dof_prepare");
    e_data.dof_downsample_sh = gpu_shader_create_from_info_name("workbench_effect_dof_downsample");
#if 0 /* TODO: finish COC min_max optimization */
    e_data.dof_flatten_v_sh = gpu_shader_create_from_info_name("workbench_effect_dof_flatten_v");
    e_data.dof_flatten_h_sh = gpu_shader_create_from_info_name("workbench_effect_dof_flatten_h");
    e_data.dof_dilate_v_sh = gpu_shader_create_from_info_name("workbench_effect_dof_dilate_v");
    e_data.dof_dilate_h_sh = gpu_shader_create_from_info_name("workbench_effect_dof_dilate_h");
#endif
    e_data.dof_blur1_sh = gpu_shader_create_from_info_name("workbench_effect_dof_blur1");
    e_data.dof_blur2_sh = gpu_shader_create_from_info_name("workbench_effect_dof_blur2");
    e_data.dof_resolve_sh = gpu_shader_create_from_info_name("workbench_effect_dof_resolve");
  }

  *prepare_sh = e_data.dof_prepare_sh;
  *downsample_sh = e_data.dof_downsample_sh;
  *blur1_sh = e_data.dof_blur1_sh;
  *blur2_sh = e_data.dof_blur2_sh;
  *resolve_sh = e_data.dof_resolve_sh;
}

GPUShader *workbench_shader_antialiasing_accumulation_get(void)
{
  if (e_data.aa_accum_sh == nullptr) {
    e_data.aa_accum_sh = gpu_shader_create_from_info_name("workbench_taa");
  }
  return e_data.aa_accum_sh;
}

GPUShader *workbench_shader_antialiasing_get(int stage)
{
  lib_assert(stage < 3);
  GPUShader **shader = &e_data.smaa_sh[stage];

  if (*shader == nullptr) {
    std::string create_info_name = "workbench_smaa_stage_";
    create_info_name += std::to_string(stage);
    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return e_data.smaa_sh[stage];
}

GPUShader *workbench_shader_volume_get(bool slice,
                                       bool coba,
                                       eDBenchVolumeInterpType interp_type,
                                       bool smoke)
{
  GPUShader **shader = &e_data.volume_sh[slice][coba][interp_type][smoke];

  if (*shader == nullptr) {
    std::string create_info_name = "workbench_volume";
    create_info_name += (smoke) ? "_smoke" : "_object";
    create_info_name += workbench_volume_interp_to_str(interp_type);
    create_info_name += (coba) ? "_coba" : "_no_coba";
    create_info_name += (slice) ? "_slice" : "_no_slice";
    *shader = gpu_shader_create_from_info_name(create_info_name.c_str());
  }
  return *shader;
}

/* -------------------------------------------------------------------- */
/** Cleanup **/

void workbench_shader_free(void)
{
  for (int j = 0; j < sizeof(e_data.opaque_prepass_sh_cache) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.opaque_prepass_sh_cache[0][0][0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.transp_prepass_sh_cache) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.transp_prepass_sh_cache[0][0][0][0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < ARRAY_SIZE(e_data.opaque_composite_sh); j++) {
    struct GPUShader **sh_array = &e_data.opaque_composite_sh[0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < ARRAY_SIZE(e_data.shadow_depth_pass_sh); j++) {
    struct GPUShader **sh_array = &e_data.shadow_depth_pass_sh[0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.shadow_depth_fail_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.shadow_depth_fail_sh[0][0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.cavity_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.cavity_sh[0][0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < ARRAY_SIZE(e_data.smaa_sh); j++) {
    struct GPUShader **sh_array = &e_data.smaa_sh[0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.volume_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.volume_sh[0][0][0][0];
    DRAW_SHADER_FREE_SAFE(sh_array[j]);
  }

  DRAW_SHADER_FREE_SAFE(e_data.oit_resolve_sh);
  DRAW_SHADER_FREE_SAFE(e_data.outline_sh);
  DRAW_SHADER_FREE_SAFE(e_data.merge_infront_sh);

  DRAW_SHADER_FREE_SAFE(e_data.dof_prepare_sh);
  DRAW_SHADER_FREE_SAFE(e_data.dof_downsample_sh);
  DRAW_SHADER_FREE_SAFE(e_data.dof_blur1_sh);
  DRAW_SHADER_FREE_SAFE(e_data.dof_blur2_sh);
  DRAW_SHADER_FREE_SAFE(e_data.dof_resolve_sh);

  DRAW_SHADER_FREE_SAFE(e_data.aa_accum_sh);
}
