#include <stdlib.h>

#include "api_define.h"

#include "api_internal.h"

#include "types_vfont.h"

#include "types_wm.h"

#ifdef API_RUNTIME

#  include "dune_vfont.h"
#  include "type_object.h"

#  include "graph.h"

#  include "wm_api.h"

/* Matching function in api_id.c */
static int api_VectorFont_filepath_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  VFont *vfont = (VFont *)ptr->owner_id;
  if (dune_vfont_is_builtin(vfont)) {
    return 0;
  }
  return PROP_EDITABLE;
}

static void api_VectorFont_reload_update(Main *UNUSED(main),
                                         Scene *UNUSED(scene),
                                         ApiPtr *ptr)
{
  VFont *vf = (VFont *)ptr->owner_id;
  dune_vfont_free_data(vf);

  /* update */
  wm_main_add_notifier(NC_GEOM | ND_DATA, NULL);
  graph_id_tag_update(&vf->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
}

#else

void api_def_vfont(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "VectorFont", "ID");
  api_def_struct_ui_text(sapi, "Vector Font", "Vector font for Text objects");
  api_def_struct_stype(sapi, "VFont");
  api_def_struct_ui_icon(sapi, ICON_FILE_FONT);

  prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "filepath");
  api_def_prop_editable_fn(prop, "api_VectorFont_filepath_editable");
  api_def_prop_ui_text(prop, "File Path", "");
  api_def_prop_update(prop, NC_GEOM | ND_DATA, "api_VectorFont_reload_update");

  prop = api_def_prop(sapi, "packed_file", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "packedfile");
  api_def_prop_ui_text(prop, "Packed File", "");

  api_api_vfont(sapi);
}

#endif
