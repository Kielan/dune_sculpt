#include <stdio.h>
#include <stdlib.h>

#include "api_define.h"

#include "lib_sys_types.h"

#include "lib_utildefines.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME
static void api_Lattice_transform(Lattice *lt, float mat[16], bool shape_keys)
{
  dune_lattice_transform(lt, (float(*)[4])mat, shape_keys);

  dune_id_tag_update(&lt->id, 0);
}

static void api_Lattice_update_gpu_tag(Lattice *lt)
{
  dune_lattice_batch_cache_dirty_tag(lt, DUNE_LATTICE_BATCH_DIRTY_ALL);
}

#else

void api_api_lattice(ApiStruct *sapi)
{
  Fn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "transform", "api_Lattice_transform");
  api_def_fn_ui_description(fn, "Transform lattice by a matrix");
  parm = api_def_float_matrix(fn, "matrix", 4, 4, NULL, 0.0f, 0.0f, "", "Matrix", 0.0f, 0.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "shape_keys", 0, "", "Transform Shape Keys");

  api_def_fn(sapi, "update_gpu_tag", "api_Lattice_update_gpu_tag");
}

#endif
