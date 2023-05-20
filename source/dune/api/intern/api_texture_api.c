#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib_utildefines.h"

#include "api_define.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_cxt.h"
#  include "dune_global.h"
#  include "lib_math.h"
#  include "type_scene_types.h"
#  include "imbuf.h"
#  include "imbuf_types.h"
#  include "render_pipeline.h"
#  include "render_texture.h"

static void texture_evaluate(struct Tex *tex, float value[3], float r_color[4])
{
  TexResult texres = {0.0f};

  /* TODO(sergey): always use color management now. */
  multitex_ext(tex, value, NULL, NULL, 1, &texres, 0, NULL, true, false);

  copy_v3_v3(r_color, texres.trgba);
  r_color[3] = texres.tin;
}

#else

void RNA_api_texture(StructRNA *srna)
{
  FunctionRNA *func;
  PropertyRNA *parm;

  func = RNA_def_function(srna, "evaluate", "texture_evaluate");
  RNA_def_function_ui_description(
      func, "Evaluate the texture at the a given coordinate and returns the result");

  parm = RNA_def_float_vector(
      func,
      "value",
      3,
      NULL,
      -FLT_MAX,
      FLT_MAX,
      "The coordinates (x,y,z) of the texture, in case of a 3D texture, the z value is the slice "
      "of the texture that is evaluated. For 2D textures such as images, the z value is ignored",
      "",
      -1e4,
      1e4);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  /* return location and normal */
  parm = RNA_def_float_vector(
      func,
      "result",
      4,
      NULL,
      -FLT_MAX,
      FLT_MAX,
      "The result of the texture where (x,y,z,w) are (red, green, blue, intensity). "
      "For grayscale textures, often intensity only will be used",
      NULL,
      -1e4,
      1e4);
  RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_function_output(func, parm);
}

#endif
