/* Compile time automation of shader compilation and validation. **/

#include <iostream>

#include "GHOST_C-api.h"

#include "gpu_cxt.h"
#include "gpu_init_exit.h"
#include "gpu_shader_create_info_private.hh"

#include "CLG_log.h"

namespace dune::gpu::shader_builder {

class ShaderBuilder {
 private:
  GHOST_SystemHandle ghost_system_;
  GHOST_ContextHandle ghost_context_;
  GPUCxt *gpu_cxt_ = nullptr;

 public:
  void init();
  bool bake_create_infos();
  void exit();
};

bool ShaderBuilder::bake_create_infos()
{
  return gpu_shader_create_info_compile_all();
}

void ShaderBuilder::init()
{
  CLG_init();

  GHOST_GLSettings glSettings = {0};
  ghost_system_ = GHOST_CreateSystem();
  ghost_context_ = GHOST_CreateOpenGLContext(ghost_system_, glSettings);
  GHOST_ActivateOpenGLContext(ghost_context_);

  gpu_cxt_ = gpu_cxt_create(nullptr);
  gpu_init();
}

void ShaderBuilder::exit()
{
  gpu_backend_exit();
  gpu_exit();

  gpu_cxt_discard(gpu_cxt_);

  GHOST_DisposeOpenGLContext(ghost_system_, ghost_context_);
  GHOST_DisposeSystem(ghost_system_);

  CLG_exit();
}

}  // namespace dune::gpu::shader_builder

/* brief Entry point for the shader_builder. */
int main(int argc, const char *argv[])
{
  if (argc < 2) {
    printf("Usage: %s <data_file_to>\n", argv[0]);
    exit(1);
  }

  int exit_code = 0;

  dune::gpu::shader_builder::ShaderBuilder builder;
  builder.init();
  if (!builder.bake_create_infos()) {
    exit_code = 1;
  }
  builder.exit();
  exit(exit_code);

  return exit_code;
}
