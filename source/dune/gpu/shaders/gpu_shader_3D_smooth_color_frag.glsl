#pragma DUNE_REQUIRE(gpu_shader_colorspace_lib.glsl)

#ifndef USE_GPU_SHADER_CREATE_INFO
in vec4 finalColor;
out vec4 fragColor;
#endif

void main()
{
  fragColor = finalColor;
  fragColor = dune_srgb_to_framebuffer_space(fragColor);
}
