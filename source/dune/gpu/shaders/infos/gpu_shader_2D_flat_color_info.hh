#include "gpu_shader_create_info.hh"

#include "gpu_interface_info.hh"

GPU_SHADER_CREATE_INFO(gpu_shader_2D_flat_color)
    .vertex_in(0, Type::VEC2, "pos")
    .vertex_in(1, Type::VEC4, "color")
    .vertex_out(flat_color_iface)
    .fragment_out(0, Type::VEC4, "fragColor")
    .push_constant(Type::MAT4, "ModelViewProjectionMatrix")
    .vertex_source("gpu_shader_2D_flat_color_vert.glsl")
    .fragment_source("gpu_shader_flat_color_frag.glsl")
    .additional_info("gpu_srgb_to_framebuffer_space")
    .do_static_compilation(true);
