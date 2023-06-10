#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_define.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "types_scene.h"

#  include "dune_camera.h"
#  include "dune_context.h"
#  include "dune_object.h"

static void api_camera_view_frame(struct Camera *camera,
                                  struct Scene *scene,
                                  float r_vec1[3],
                                  float r_vec2[3],
                                  float r_vec3[3],
                                  float r_vec4[3])
{
  float vec[4][3];

  dune_camera_view_frame(scene, camera, vec);

  copy_v3_v3(r_vec1, vec[0]);
  copy_v3_v3(r_vec2, vec[1]);
  copy_v3_v3(r_vec3, vec[2]);
  copy_v3_v3(r_vec4, vec[3]);
}

#else

void api_camera(ApiStruct *sapi)
{
  FunctionRNA *func;
  PropertyRNA *parm;

  func = RNA_def_function(srna, "view_frame", "rna_camera_view_frame");
  RNA_def_function_ui_description(
      func, "Return 4 points for the cameras frame (before object transformation)");

  RNA_def_pointer(func,
                  "scene",
                  "Scene",
                  "",
                  "Scene to use for aspect calculation, when omitted 1:1 aspect is used");

  /* return location and normal */
  parm = RNA_def_float_vector(
      func, "result_1", 3, NULL, -FLT_MAX, FLT_MAX, "Result", NULL, -1e4, 1e4);
  RNA_def_property_flag(parm, PROP_THICK_WRAP);
  RNA_def_function_output(func, parm);

  parm = RNA_def_float_vector(
      func, "result_2", 3, NULL, -FLT_MAX, FLT_MAX, "Result", NULL, -1e4, 1e4);
  RNA_def_property_flag(parm, PROP_THICK_WRAP);
  RNA_def_function_output(func, parm);

  parm = RNA_def_float_vector(
      func, "result_3", 3, NULL, -FLT_MAX, FLT_MAX, "Result", NULL, -1e4, 1e4);
  RNA_def_property_flag(parm, PROP_THICK_WRAP);
  RNA_def_function_output(func, parm);

  parm = RNA_def_float_vector(
      func, "result_4", 3, NULL, -FLT_MAX, FLT_MAX, "Result", NULL, -1e4, 1e4);
  RNA_def_property_flag(parm, PROP_THICK_WRAP);
  RNA_def_function_output(func, parm);
}

#endif
