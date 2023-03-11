#pragma DUNE_REQUIRE(gpu_shader_colorspace_lib.glsl)

#ifndef USE_GPU_SHADER_CREATE_INFO
uniform vec4 color;
out vec4 fragColor;
#endif

void main()
{
  fragColor = dune_srgb_to_framebuffer_space(color);
}
