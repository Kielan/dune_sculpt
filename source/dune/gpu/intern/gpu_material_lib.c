/* GPU material lib parsing and code generation. */

#include <stdio.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_dynstr.h"
#include "lib_ghash.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "gpu_material_lib.h"

/* List of all gpu_shader_material_*.glsl files used by GLSL materials. These
 * will be parsed to make all fns in them available to use for gpu_link().
 *
 * If a file uses fns from another file, it must be added to the list of
 * dependencies, and be placed after that file in the list. */

extern char datatoc_gpu_shader_material_add_shader_glsl[];
extern char datatoc_gpu_shader_material_ambient_occlusion_glsl[];
extern char datatoc_gpu_shader_material_anisotropic_glsl[];
extern char datatoc_gpu_shader_material_attribute_glsl[];
extern char datatoc_gpu_shader_material_background_glsl[];
extern char datatoc_gpu_shader_material_bevel_glsl[];
extern char datatoc_gpu_shader_material_wavelength_glsl[];
extern char datatoc_gpu_shader_material_blackbody_glsl[];
extern char datatoc_gpu_shader_material_bright_contrast_glsl[];
extern char datatoc_gpu_shader_material_bump_glsl[];
extern char datatoc_gpu_shader_material_camera_glsl[];
extern char datatoc_gpu_shader_material_clamp_glsl[];
extern char datatoc_gpu_shader_material_color_ramp_glsl[];
extern char datatoc_gpu_shader_material_color_util_glsl[];
extern char datatoc_gpu_shader_material_combine_hsv_glsl[];
extern char datatoc_gpu_shader_material_combine_rgb_glsl[];
extern char datatoc_gpu_shader_material_combine_xyz_glsl[];
extern char datatoc_gpu_shader_material_diffuse_glsl[];
extern char datatoc_gpu_shader_material_displacement_glsl[];
extern char datatoc_gpu_shader_material_eevee_specular_glsl[];
extern char datatoc_gpu_shader_material_emission_glsl[];
extern char datatoc_gpu_shader_material_float_curve_glsl[];
extern char datatoc_gpu_shader_material_fractal_noise_glsl[];
extern char datatoc_gpu_shader_material_fresnel_glsl[];
extern char datatoc_gpu_shader_material_gamma_glsl[];
extern char datatoc_gpu_shader_material_geometry_glsl[];
extern char datatoc_gpu_shader_material_glass_glsl[];
extern char datatoc_gpu_shader_material_glossy_glsl[];
extern char datatoc_gpu_shader_material_hair_info_glsl[];
extern char datatoc_gpu_shader_material_hash_glsl[];
extern char datatoc_gpu_shader_material_holdout_glsl[];
extern char datatoc_gpu_shader_material_hue_sat_val_glsl[];
extern char datatoc_gpu_shader_material_invert_glsl[];
extern char datatoc_gpu_shader_material_layer_weight_glsl[];
extern char datatoc_gpu_shader_material_light_falloff_glsl[];
extern char datatoc_gpu_shader_material_light_path_glsl[];
extern char datatoc_gpu_shader_material_mapping_glsl[];
extern char datatoc_gpu_shader_material_map_range_glsl[];
extern char datatoc_gpu_shader_material_math_glsl[];
extern char datatoc_gpu_shader_material_math_util_glsl[];
extern char datatoc_gpu_shader_material_mix_rgb_glsl[];
extern char datatoc_gpu_shader_material_mix_shader_glsl[];
extern char datatoc_gpu_shader_material_noise_glsl[];
extern char datatoc_gpu_shader_material_normal_glsl[];
extern char datatoc_gpu_shader_material_normal_map_glsl[];
extern char datatoc_gpu_shader_material_object_info_glsl[];
extern char datatoc_gpu_shader_material_output_aov_glsl[];
extern char datatoc_gpu_shader_material_output_material_glsl[];
extern char datatoc_gpu_shader_material_output_world_glsl[];
extern char datatoc_gpu_shader_material_particle_info_glsl[];
extern char datatoc_gpu_shader_material_point_info_glsl[];
extern char datatoc_gpu_shader_material_principled_glsl[];
extern char datatoc_gpu_shader_material_refraction_glsl[];
extern char datatoc_gpu_shader_material_rgb_curves_glsl[];
extern char datatoc_gpu_shader_material_rgb_to_bw_glsl[];
extern char datatoc_gpu_shader_material_separate_hsv_glsl[];
extern char datatoc_gpu_shader_material_separate_rgb_glsl[];
extern char datatoc_gpu_shader_material_separate_xyz_glsl[];
extern char datatoc_gpu_shader_material_set_glsl[];
extern char datatoc_gpu_shader_material_shader_to_rgba_glsl[];
extern char datatoc_gpu_shader_material_squeeze_glsl[];
extern char datatoc_gpu_shader_material_subsurface_scattering_glsl[];
extern char datatoc_gpu_shader_material_tangent_glsl[];
extern char datatoc_gpu_shader_material_tex_brick_glsl[];
extern char datatoc_gpu_shader_material_tex_checker_glsl[];
extern char datatoc_gpu_shader_material_tex_environment_glsl[];
extern char datatoc_gpu_shader_material_tex_gradient_glsl[];
extern char datatoc_gpu_shader_material_tex_image_glsl[];
extern char datatoc_gpu_shader_material_tex_magic_glsl[];
extern char datatoc_gpu_shader_material_tex_musgrave_glsl[];
extern char datatoc_gpu_shader_material_tex_noise_glsl[];
extern char datatoc_gpu_shader_material_tex_sky_glsl[];
extern char datatoc_gpu_shader_material_texture_coordinates_glsl[];
extern char datatoc_gpu_shader_material_tex_voronoi_glsl[];
extern char datatoc_gpu_shader_material_tex_wave_glsl[];
extern char datatoc_gpu_shader_material_tex_white_noise_glsl[];
extern char datatoc_gpu_shader_material_toon_glsl[];
extern char datatoc_gpu_shader_material_translucent_glsl[];
extern char datatoc_gpu_shader_material_transparent_glsl[];
extern char datatoc_gpu_shader_material_uv_map_glsl[];
extern char datatoc_gpu_shader_material_vector_curves_glsl[];
extern char datatoc_gpu_shader_material_vector_displacement_glsl[];
extern char datatoc_gpu_shader_material_vector_math_glsl[];
extern char datatoc_gpu_shader_material_vector_rotate_glsl[];
extern char datatoc_gpu_shader_material_velvet_glsl[];
extern char datatoc_gpu_shader_material_vertex_color_glsl[];
extern char datatoc_gpu_shader_material_volume_absorption_glsl[];
extern char datatoc_gpu_shader_material_volume_info_glsl[];
extern char datatoc_gpu_shader_material_volume_principled_glsl[];
extern char datatoc_gpu_shader_material_volume_scatter_glsl[];
extern char datatoc_gpu_shader_material_wireframe_glsl[];
extern char datatoc_gpu_shader_material_world_normals_glsl[];

static GPUMaterialLibrary gpu_shader_material_math_util_lib = {
    .code = datatoc_gpu_shader_material_math_util_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_color_util_lib = {
    .code = datatoc_gpu_shader_material_color_util_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_hash_lib = {
    .code = datatoc_gpu_shader_material_hash_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_noise_lib = {
    .code = datatoc_gpu_shader_material_noise_glsl,
    .dependencies = {&gpu_shader_material_hash_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_fractal_noise_lib = {
    .code = datatoc_gpu_shader_material_fractal_noise_glsl,
    .dependencies = {&gpu_shader_material_noise_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_add_shader_lib = {
    .code = datatoc_gpu_shader_material_add_shader_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_ambient_occlusion_lib = {
    .code = datatoc_gpu_shader_material_ambient_occlusion_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_glossy_lib = {
    .code = datatoc_gpu_shader_material_glossy_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_anisotropic_lib = {
    .code = datatoc_gpu_shader_material_anisotropic_glsl,
    .dependencies = {&gpu_shader_material_glossy_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_attribute_lib = {
    .code = datatoc_gpu_shader_material_attribute_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_background_lib = {
    .code = datatoc_gpu_shader_material_background_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_bevel_lib = {
    .code = datatoc_gpu_shader_material_bevel_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_wavelength_lib = {
    .code = datatoc_gpu_shader_material_wavelength_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_blackbody_lib = {
    .code = datatoc_gpu_shader_material_blackbody_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_bright_contrast_lib = {
    .code = datatoc_gpu_shader_material_bright_contrast_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_bump_lib = {
    .code = datatoc_gpu_shader_material_bump_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_camera_lib = {
    .code = datatoc_gpu_shader_material_camera_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_clamp_lib = {
    .code = datatoc_gpu_shader_material_clamp_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_color_ramp_lib = {
    .code = datatoc_gpu_shader_material_color_ramp_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_combine_hsv_lib = {
    .code = datatoc_gpu_shader_material_combine_hsv_glsl,
    .dependencies = {&gpu_shader_material_color_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_combine_rgb_lib = {
    .code = datatoc_gpu_shader_material_combine_rgb_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_combine_xyz_lib = {
    .code = datatoc_gpu_shader_material_combine_xyz_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_diffuse_lib = {
    .code = datatoc_gpu_shader_material_diffuse_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_displacement_lib = {
    .code = datatoc_gpu_shader_material_displacement_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_eevee_specular_lib = {
    .code = datatoc_gpu_shader_material_eevee_specular_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_emission_lib = {
    .code = datatoc_gpu_shader_material_emission_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_float_curve_lib = {
    .code = datatoc_gpu_shader_material_float_curve_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_fresnel_lib = {
    .code = datatoc_gpu_shader_material_fresnel_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_gamma_lib = {
    .code = datatoc_gpu_shader_material_gamma_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_tangent_lib = {
    .code = datatoc_gpu_shader_material_tangent_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_geometry_lib = {
    .code = datatoc_gpu_shader_material_geometry_glsl,
    .dependencies = {&gpu_shader_material_tangent_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_glass_lib = {
    .code = datatoc_gpu_shader_material_glass_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_hair_info_lib = {
    .code = datatoc_gpu_shader_material_hair_info_glsl,
    .dependencies = {&gpu_shader_material_hash_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_holdout_lib = {
    .code = datatoc_gpu_shader_material_holdout_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_hue_sat_val_lib = {
    .code = datatoc_gpu_shader_material_hue_sat_val_glsl,
    .dependencies = {&gpu_shader_material_color_util_library, NULL},
};

static GPUMaterialLib gpu_shader_material_invert_lib = {
    .code = datatoc_gpu_shader_material_invert_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_layer_weight_lib = {
    .code = datatoc_gpu_shader_material_layer_weight_glsl,
    .dependencies = {&gpu_shader_material_fresnel_library, NULL},
};

static GPUMaterialLib gpu_shader_material_light_falloff_lib = {
    .code = datatoc_gpu_shader_material_light_falloff_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_light_path_lib = {
    .code = datatoc_gpu_shader_material_light_path_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_mapping_lib = {
    .code = datatoc_gpu_shader_material_mapping_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
}

static GPUMaterialLib gpu_shader_material_map_range_lib = {
    .code = datatoc_gpu_shader_material_map_range_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_math_lib = {
    .code = datatoc_gpu_shader_material_math_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_mix_rgb_lib = {
    .code = datatoc_gpu_shader_material_mix_rgb_glsl,
    .dependencies = {&gpu_shader_material_color_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_mix_shader_lib = {
    .code = datatoc_gpu_shader_material_mix_shader_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_normal_lib = {
    .code = datatoc_gpu_shader_material_normal_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_normal_map_lib = {
    .code = datatoc_gpu_shader_material_normal_map_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_object_info_lib = {
    .code = datatoc_gpu_shader_material_object_info_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_output_aov_lib = {
    .code = datatoc_gpu_shader_material_output_aov_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_output_material_lib = {
    .code = datatoc_gpu_shader_material_output_material_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_output_world_lib = {
    .code = datatoc_gpu_shader_material_output_world_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_particle_info_lib = {
    .code = datatoc_gpu_shader_material_particle_info_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_point_info_lib = {
    .code = datatoc_gpu_shader_material_point_info_glsl,
    .dependencies = {&gpu_shader_material_hash_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_principled_lib = {
    .code = datatoc_gpu_shader_material_principled_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_refraction_lib = {
    .code = datatoc_gpu_shader_material_refraction_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_rgb_curves_lib = {
    .code = datatoc_gpu_shader_material_rgb_curves_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_rgb_to_bw_lib = {
    .code = datatoc_gpu_shader_material_rgb_to_bw_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_separate_hsv_lib = {
    .code = datatoc_gpu_shader_material_separate_hsv_glsl,
    .dependencies = {&gpu_shader_material_color_util_library, NULL},
};

static GPUMaterialLib gpu_shader_material_separate_rgb_lib = {
    .code = datatoc_gpu_shader_material_separate_rgb_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_separate_xyz_lib = {
    .code = datatoc_gpu_shader_material_separate_xyz_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_set_lib = {
    .code = datatoc_gpu_shader_material_set_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_shader_to_rgba_lib = {
    .code = datatoc_gpu_shader_material_shader_to_rgba_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_squeeze_lib = {
    .code = datatoc_gpu_shader_material_squeeze_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_subsurface_scattering_lib = {
    .code = datatoc_gpu_shader_material_subsurface_scattering_glsl,
    .dependencies = {&gpu_shader_material_diffuse_library, NULL},
};

static GPUMaterialLib gpu_shader_material_tex_brick_lib = {
    .code = datatoc_gpu_shader_material_tex_brick_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib,
                     &gpu_shader_material_hash_lib,
                     NULL},
};

static GPUMaterialLib gpu_shader_material_tex_checker_lib = {
    .code = datatoc_gpu_shader_material_tex_checker_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_tex_environment_lib = {
    .code = datatoc_gpu_shader_material_tex_environment_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_tex_gradient_lib = {
    .code = datatoc_gpu_shader_material_tex_gradient_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_tex_image_lib = {
    .code = datatoc_gpu_shader_material_tex_image_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_tex_magic_lib = {
    .code = datatoc_gpu_shader_material_tex_magic_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_tex_musgrave_lib = {
    .code = datatoc_gpu_shader_material_tex_musgrave_glsl,
    .dependencies = {&gpu_shader_material_noise_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_tex_noise_lib = {
    .code = datatoc_gpu_shader_material_tex_noise_glsl,
    .dependencies = {&gpu_shader_material_fractal_noise_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_tex_sky_lib = {
    .code = datatoc_gpu_shader_material_tex_sky_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_texture_coordinates_lib = {
    .code = datatoc_gpu_shader_material_texture_coordinates_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_tex_voronoi_lib = {
    .code = datatoc_gpu_shader_material_tex_voronoi_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib,
                     &gpu_shader_material_hash_lib,
                     NULL},
};

static GPUMaterialLib gpu_shader_material_tex_wave_lib = {
    .code = datatoc_gpu_shader_material_tex_wave_glsl,
    .dependencies = {&gpu_shader_material_fractal_noise_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_tex_white_noise_lib = {
    .code = datatoc_gpu_shader_material_tex_white_noise_glsl,
    .dependencies = {&gpu_shader_material_hash_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_toon_lib = {
    .code = datatoc_gpu_shader_material_toon_glsl,
    .dependencies = {&gpu_shader_material_diffuse_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_translucent_lib = {
    .code = datatoc_gpu_shader_material_translucent_glsl,
    .dependencies = {&gpu_shader_material_diffuse_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_transparent_lib = {
    .code = datatoc_gpu_shader_material_transparent_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_uv_map_lib = {
    .code = datatoc_gpu_shader_material_uv_map_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_vector_curves_lib = {
    .code = datatoc_gpu_shader_material_vector_curves_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_vector_displacement_lib = {
    .code = datatoc_gpu_shader_material_vector_displacement_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_vector_math_lib = {
    .code = datatoc_gpu_shader_material_vector_math_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_vector_rotate_lib = {
    .code = datatoc_gpu_shader_material_vector_rotate_glsl,
    .dependencies = {&gpu_shader_material_math_util_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_velvet_lib = {
    .code = datatoc_gpu_shader_material_velvet_glsl,
    .dependencies = {&gpu_shader_material_diffuse_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_vertex_color_lib = {
    .code = datatoc_gpu_shader_material_vertex_color_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_volume_absorption_lib = {
    .code = datatoc_gpu_shader_material_volume_absorption_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_volume_info_lib = {
    .code = datatoc_gpu_shader_material_volume_info_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_volume_principled_lib = {
    .code = datatoc_gpu_shader_material_volume_principled_glsl,
    .dependencies = {&gpu_shader_material_blackbody_lib, NULL},
};

static GPUMaterialLib gpu_shader_material_volume_scatter_lib = {
    .code = datatoc_gpu_shader_material_volume_scatter_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_wireframe_lib = {
    .code = datatoc_gpu_shader_material_wireframe_glsl,
    .dependencies = {NULL},
};

static GPUMaterialLib gpu_shader_material_world_normals_lib = {
    .code = datatoc_gpu_shader_material_world_normals_glsl,
    .dependencies = {&gpu_shader_material_texture_coordinates_lib, NULL},
};

static GPUMaterialLib *gpu_material_libs[] = {
    &gpu_shader_material_math_util_lib,
    &gpu_shader_material_color_util_lib,
    &gpu_shader_material_hash_lib,
    &gpu_shader_material_noise_lib,
    &gpu_shader_material_float_curve_lib,
    &gpu_shader_material_fractal_noise_lib,
    &gpu_shader_material_add_shader_lib,
    &gpu_shader_material_ambient_occlusion_lib,
    &gpu_shader_material_glossy_lib,
    &gpu_shader_material_anisotropic_lib,
    &gpu_shader_material_attribute_lib,
    &gpu_shader_material_background_lib,
    &gpu_shader_material_bevel_lib,
    &gpu_shader_material_wavelength_lib,
    &gpu_shader_material_blackbody_lib,
    &gpu_shader_material_bright_contrast_lib,
    &gpu_shader_material_bump_lib,
    &gpu_shader_material_camera_lib,
    &gpu_shader_material_clamp_lib,
    &gpu_shader_material_color_ramp_lib,
    &gpu_shader_material_combine_hsv_lib,
    &gpu_shader_material_combine_rgb_lib,
    &gpu_shader_material_combine_xyz_lib,
    &gpu_shader_material_diffuse_lib,
    &gpu_shader_material_displacement_lib,
    &gpu_shader_material_eevee_specular_lib,
    &gpu_shader_material_emission_lib,
    &gpu_shader_material_fresnel_lib,
    &gpu_shader_material_gamma_lib,
    &gpu_shader_material_tangent_lib,
    &gpu_shader_material_geometry_lib,
    &gpu_shader_material_glass_lib,
    &gpu_shader_material_hair_info_lib,
    &gpu_shader_material_holdout_lib,
    &gpu_shader_material_hue_sat_val_lib,
    &gpu_shader_material_invert_lib,
    &gpu_shader_material_layer_weight_lib,
    &gpu_shader_material_light_falloff_lib,
    &gpu_shader_material_light_path_lib,
    &gpu_shader_material_mapping_lib,
    &gpu_shader_material_map_range_lib,
    &gpu_shader_material_math_lib,
    &gpu_shader_material_mix_rgb_lib,
    &gpu_shader_material_mix_shader_lib,
    &gpu_shader_material_normal_lib,
    &gpu_shader_material_normal_map_lib,
    &gpu_shader_material_object_info_lib,
    &gpu_shader_material_output_aov_lib,
    &gpu_shader_material_output_material_lib,
    &gpu_shader_material_output_world_lib,
    &gpu_shader_material_particle_info_lib,
    &gpu_shader_material_point_info_lib,
    &gpu_shader_material_principled_lib,
    &gpu_shader_material_refraction_lib,
    &gpu_shader_material_rgb_curves_lib,
    &gpu_shader_material_rgb_to_bw_lib,
    &gpu_shader_material_separate_hsv_lib,
    &gpu_shader_material_separate_rgb_lib,
    &gpu_shader_material_separate_xyz_lib,
    &gpu_shader_material_set_lib,
    &gpu_shader_material_shader_to_rgba_lib,
    &gpu_shader_material_squeeze_lib,
    &gpu_shader_material_subsurface_scattering_lib,
    &gpu_shader_material_tex_brick_lib,
    &gpu_shader_material_tex_checker_lib,
    &gpu_shader_material_tex_environment_lib,
    &gpu_shader_material_tex_gradient_lib,
    &gpu_shader_material_tex_image_lib,
    &gpu_shader_material_tex_magic_lib,
    &gpu_shader_material_tex_musgrave_lib,
    &gpu_shader_material_tex_noise_lib,
    &gpu_shader_material_tex_sky_lib,
    &gpu_shader_material_texture_coordinates_lib,
    &gpu_shader_material_tex_voronoi_lib,
    &gpu_shader_material_tex_wave_lib,
    &gpu_shader_material_tex_white_noise_lib,
    &gpu_shader_material_toon_lib,
    &gpu_shader_material_translucent_lib,
    &gpu_shader_material_transparent_lib,
    &gpu_shader_material_uv_map_lib,
    &gpu_shader_material_vector_curves_lib,
    &gpu_shader_material_vector_displacement_lib,
    &gpu_shader_material_vector_math_lib,
    &gpu_shader_material_vector_rotate_lib,
    &gpu_shader_material_velvet_lib,
    &gpu_shader_material_vertex_color_lib,
    &gpu_shader_material_volume_absorption_lib,
    &gpu_shader_material_volume_info_lib,
    &gpu_shader_material_volume_principled_lib,
    &gpu_shader_material_volume_scatter_lib,
    &gpu_shader_material_wireframe_lib,
    &gpu_shader_material_world_normals_lib,
    NULL};

/* GLSL code parsing for finding function definitions.
 * These are stored in a hash for lookup when creating a material. */

static GHash *FUNCTION_HASH = NULL;

const char *gpu_str_skip_token(const char *str, char *token, int max)
{
  int len = 0;

  /* skip a variable/function name */
  while (*str) {
    if (ELEM(*str, ' ', '(', ')', ',', ';', '\t', '\n', '\r')) {
      break;
    }

    if (token && len < max - 1) {
      *token = *str;
      token++;
      len++;
    }
    str++;
  }

  if (token) {
    *token = '\0';
  }

  /* skip the next special characters:
   * note the missing ')' */
  while (*str) {
    if (ELEM(*str, ' ', '(', ',', ';', '\t', '\n', '\r')) {
      str++;
    }
    else {
      break;
    }
  }

  return str;
}

/* Indices match the eGPUType enum */
static const char *GPU_DATATYPE_STR[17] = {
    "",
    "float",
    "vec2",
    "vec3",
    "vec4",
    NULL,
    NULL,
    NULL,
    NULL,
    "mat3",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "mat4",
};

const char *gpu_data_type_to_string(const eGPUType type)
{
  return GPU_DATATYPE_STR[type];
}

static void gpu_parse_material_lib(GHash *hash, GPUMaterialLib *lib)
{
  GPUFn *fn;
  eGPUType type;
  GPUFnQual qual;
  int i;
  const char *code = lib->code;

  while ((code = strstr(code, "void "))) {
    function = mem_callocn(sizeof(GPUFn), "GPUFunction");
    function->lib = library;

    code = gpu_str_skip_token(code, NULL, 0);
    code = gpu_str_skip_token(code, function->name, MAX_FUNCTION_NAME);

    /* get params */
    while (*code && *code != ')') {
      if (lib_str_startswith(code, "const ")) {
        code = gpu_str_skip_token(code, NULL, 0);
      }

      /* test if it's an input or output */
      qual = FN_QUAL_IN;
      if (lib_str_startswith(code, "out ")) {
        qual = FN_QUAL_OUT;
      }
      if (lib_str_startswith(code, "inout ")) {
        qual = FN_QUAL_INOUT;
      }
      if ((qual != FN_QUAL_IN) || lib_str_startswith(code, "in ")) {
        code = gpu_str_skip_token(code, NULL, 0);
      }

      /* test for type */
      type = GPU_NONE;
      for (i = 1; i < ARRAY_SIZE(GPU_DATATYPE_STR); i++) {
        if (GPU_DATATYPE_STR[i] && lib_str_startswith(code, GPU_DATATYPE_STR[i])) {
          type = i;
          break;
        }
      }

      if (!type && lib_str_startswith(code, "samplerCube")) {
        type = GPU_TEXCUBE;
      }
      if (!type && lib_str_startswith(code, "sampler2DShadow")) {
        type = GPU_SHADOW2D;
      }
      if (!type && lib_str_startswith(code, "sampler1DArray")) {
        type = GPU_TEX1D_ARRAY;
      }
      if (!type && lib_str_startswith(code, "sampler2DArray")) {
        type = GPU_TEX2D_ARRAY;
      }
      if (!type && lib_str_startswith(code, "sampler2D")) {
        type = GPU_TEX2D;
      }
      if (!type && lib_str_startswith(code, "sampler3D")) {
        type = GPU_TEX3D;
      }

      if (!type && lib_str_startswith(code, "Closure")) {
        type = GPU_CLOSURE;
      }

      if (type) {
        /* add parameter */
        code = gpu_str_skip_token(code, NULL, 0);
        code = gpu_str_skip_token(code, NULL, 0);
        fn->paramqual[fn->totparam] = qual;
        fn->paramtype[fn->totparam] = type;
        fn->totparam++;
      }
      else {
        fprintf(stderr, "GPU invalid function parameter in %s.\n", function->name);
        break;
      }
    }

    if (fn->name[0] == '\0' || fn->totparam == 0) {
      fprintf(stderr, "GPU fns parse error.\n");
      mem_freen(fn);
      break;
    }

    lib_ghash_insert(hash, fn->name, fn);
  }
}

/* Module */
void gpu_material_lib_init(void)
{
  /* Only parse GLSL shader files once. */
  if (FN_HASH) {
    return;
  }

  FN_HASH = lib_ghash_str_new("GPU_lookup_fn gh");
  for (int i = 0; gpu_material_libs[i]; i++) {
    gpu_parse_material_lib(FN_HASH, gpu_material_libs[i]);
  }
}

void gpu_material_lib_exit(void)
{
  if (FN_HASH) {
    lib_ghash_free(FN_HASH, NULL, MEM_freeN);
    FN_HASH = NULL;
  }
}

/* Code Generation */
static void gpu_material_use_lib_with_dependencies(GSet *used_libs,
                                                   GPUMaterialLib *lib)
{
  if (lib_gset_add(used_libs, lib->code)) {
    for (int i = 0; lib->dependencies[i]; i++) {
      gpu_material_use_lib_with_dependencies(used_libs, lib->dependencies[i]);
    }
  }
}

GPUFn *gpu_material_lib_use_fn(GSet *used_libs, const char *name)
{
  GPUFn *fn = lib_ghash_lookup(FN_HASH, (const void *)name);
  if (fn) {
    gpu_material_use_lib_with_dependencies(used_libs, fn->lib);
  }
  return function;
}

char *gpu_material_lib_generate_code(GSet *used_libs, const char *frag_lib)
{
  DynStr *ds = lib_dynstr_new();

  if (frag_lib) {
    lib_dynstr_append(ds, frag_lib);
  }

  /* Always include those because they may be needed by the execution function. */
  gpu_material_use_lib_with_dependencies(used_libs,
                                         &gpu_shader_material_world_normals_library);

  /* Add library code in order, for dependencies. */
  for (int i = 0; gpu_material_libraries[i]; i++) {
    GPUMaterialLib *lib = gpu_material_libs[i];
    if (lib_gset_haskey(used_libs, lib->code)) {
      lib_dynstr_append(ds, lib->code);
    }
  }

  char *result = lib_dynstr_get_cstring(ds);
  lib_dynstr_free(ds);

  return result;
}
