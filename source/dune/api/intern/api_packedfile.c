#include <stdlib.h>

#include "lib_utildefines.h"

#include "types_packedFile.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "dune_packedFile.h"

#include "api_internal.h"

const EnumPropItem api_enum_unpack_method_items[] = {
    {PF_REMOVE, "REMOVE", 0, "Remove Pack", ""},
    {PF_USE_LOCAL, "USE_LOCAL", 0, "Use Local File", ""},
    {PF_WRITE_LOCAL, "WRITE_LOCAL", 0, "Write Local File (overwrite existing)", ""},
    {PF_USE_ORIGINAL, "USE_ORIGINAL", 0, "Use Original File", ""},
    {PF_WRITE_ORIGINAL, "WRITE_ORIGINAL", 0, "Write Original File (overwrite existing)", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

static void api_PackedImage_data_get(ApiPtr *ptr, char *value)
{
  PackedFile *pf = (PackedFile *)ptr->data;
  memcpy(value, pf->data, (size_t)pf->size);
  value[pf->size] = '\0';
}

static int api_PackedImage_data_len(ApiPtr *ptr)
{
  PackedFile *pf = (PackedFile *)ptr->data;
  return pf->size; /* No need to include trailing NULL char here! */
}

#else

void api_def_packedfile(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "PackedFile", NULL);
  api_def_struct_ui_text(sapi, "Packed File", "External file packed into the .blend file");

  prop = api_def_prop(sapi, "size", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Size", "Size of packed file in bytes");

  prop = api_def_prop(sapi, "data", PROP_STRING, PROP_BYTESTRING);
  api_def_prop_string_fns(
      prop, "api_PackedImage_data_get", "rna_PackedImage_data_len", NULL);
  api_def_prop_ui_text(prop, "Data", "Raw data (bytes, exact content of the embedded file)");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
}

#endif
