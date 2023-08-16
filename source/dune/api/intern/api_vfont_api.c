#include "types_packedFile.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "dune_packedFile.h"

#include "api_internal.h"

#ifdef api_RUNTIME

static void api_VectorFont_pack(VFont *vfont, Main *main, ReportList *reports)
{
  vfont->packedfile = dune_packedfile_new(
      reports, vfont->filepath, ID_DUNE_PATH(main, &vfont->id));
}

static void api_VectorFont_unpack(VFont *vfont, Main *main, ReportList *reports, int method)
{
  if (!vfont->packedfile) {
    dune_report(reports, RPT_ERROR, "Font not packed");
  } else {
    /* reports its own error on failure */
    dune_packedfile_unpack_vfont(main, reports, vfont, method);
  }
}

#else

void api_vfont(ApiStruct *sapi)
{
  ApiFn *fn;

  fn = api_def_fn(sapi, "pack", "api_VectorFont_pack");
  api_def_fn_ui_description(fn, "Pack the font into the current dune file");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);

  fn = api_def_fn(sapi, "unpack", "api_VectorFont_unpack");
  api_def_fn_ui_description(fn, "Unpack the font to the samples filename");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);
  api_def_enum(
      fn, "method", api_enum_unpack_method_items, PF_USE_LOCAL, "method", "How to unpack");
}

#endif
