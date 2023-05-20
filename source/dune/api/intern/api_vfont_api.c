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

static void rna_VectorFont_unpack(VFont *vfont, Main *bmain, ReportList *reports, int method)
{
  if (!vfont->packedfile) {
    dune_report(reports, RPT_ERROR, "Font not packed");
  }
  else {
    /* reports its own error on failure */
    dune_packedfile_unpack_vfont(main, reports, vfont, method);
  }
}

#else

void RNA_api_vfont(StructRNA *srna)
{
  FunctionRNA *func;

  func = RNA_def_function(srna, "pack", "rna_VectorFont_pack");
  RNA_def_function_ui_description(func, "Pack the font into the current blend file");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_REPORTS);

  func = RNA_def_function(srna, "unpack", "rna_VectorFont_unpack");
  RNA_def_function_ui_description(func, "Unpack the font to the samples filename");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_REPORTS);
  RNA_def_enum(
      func, "method", rna_enum_unpack_method_items, PF_USE_LOCAL, "method", "How to unpack");
}

#endif
