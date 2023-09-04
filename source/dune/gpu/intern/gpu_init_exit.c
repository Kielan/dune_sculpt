#include "gpu_init_exit.h" /* interface */
#include "dune_global.h"
#include "lib_sys_types.h"
#include "gpu_batch.h"
#include "gpu_buffers.h"
#include "gpu_cxt.h"
#include "gpu_immediate.h"

#include "intern/gpu_codegen.h"
#include "intern/gpu_material_lib.h"
#include "intern/gpu_private.h"
#include "intern/gpu_shader_create_info_private.hh"
#include "intern/gpu_shader_dependency_private.h"

/* although the order of initialization and shutdown should not matter
 * (except for the extensions), I chose alphabetical and reverse alphabetical order */

static bool initialized = false;

void gpu_init(void)
{
  /* can't avoid calling this multiple times, see wm_window_ghostwindow_add */
  if (initialized) {
    return;
  }

  initialized = true;

  gpu_shader_dependency_init();
  gpu_shader_create_info_init();

  gpu_codegen_init();
  gpu_material_lib_init();

  gpu_batch_init();

#ifndef GPU_STANDALONE
  gpu_pbvh_init();
#endif
}

void gpu_exit(void)
{
#ifndef GPU_STANDALONE
  gpu_pbvh_exit();
#endif

  gpu_batch_exit();

  gpu_material_lib_exit();
  gpu_codegen_exit();

  gpu_shader_dependency_exit();
  gpu_shader_create_info_exit();

  initialized = false;
}

bool gpu_is_init(void)
{
  return initialized;
}
