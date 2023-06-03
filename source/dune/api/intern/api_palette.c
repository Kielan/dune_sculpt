#include <stdlib.h>

#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"

#include "api_internal.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "types_brush.h"

#  include "dune_paint.h"
#  include "dune_report.h"
static PaletteColor *api_Palette_color_new(Palette *palette)
{
  if (ID_IS_LINKED(palette) || ID_IS_OVERRIDE_LIB(palette)) {
    return NULL;
  }

  PaletteColor *color = dune_palette_color_add(palette);
  return color;
}

static void api_Palette_color_remove(Palette *palette, ReportList *reports, ApiPtr *color_ptr)
{
  if (ID_IS_LINKED(palette) || ID_IS_OVERRIDE_LIB(palette)) {
    return;
  }

  PaletteColor *color = color_ptr->data;

  if (lib_findindex(&palette->colors, color) == -1) {
    dune_reportf(
        reports, RPT_ERROR, "Palette '%s' does not contain color given", palette->id.name + 2);
    return;
  }

  dune_palette_color_remove(palette, color);

  API_PTR_INVALIDATE(color_ptr);
}

static void api_Palette_color_clear(Palette *palette)
{
  if (ID_IS_LINKED(palette) || ID_IS_OVERRIDE_LIB(palette)) {
    return;
  }

  dune_palette_clear(palette);
}

static ApiPtr api_Palette_active_color_get(ApiPtr *ptr)
{
  Palette *palette = ptr->data;
  PaletteColor *color;

  color = lib_findlink(&palette->colors, palette->active_color);

  if (color) {
    return api_ptr_inherit_refine(ptr, &ApiPaletteColor, color);
  }

  return api_ptr_inherit_refine(ptr, NULL, NULL);
}

static void api_Palette_active_color_set(ApiPtr *ptr,
                                         ApiPtr value,
                                         struct ReportList *UNUSED(reports))
{
  Palette *palette = ptr->data;
  PaletteColor *color = value.data;

  /* -1 is ok for an unset index */
  if (color == NULL) {
    palette->active_color = -1;
  }
  else {
    palette->active_color = lib_findindex(&palette->colors, color);
  }
}

#else

/* palette.colors */
static void api_def_palettecolors(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "PaletteColors");
  sapi = api_def_struct(dapi, "PaletteColors", NULL);
  api_def_struct_stype(sapi, "Palette");
  api_def_struct_ui_text(sapi, "Palette Splines", "Collection of palette colors");

  fn = api_def_fn(sapi, "new", "api_Palette_color_new");
  api_def_fn_ui_description(fn, "Add a new color to the palette");
  parm = api_def_ptr(fn, "color", "PaletteColor", "", "The newly created color");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(dapi, "remove", "api_Palette_color_remove");
  api_def_fn_ui_description(fn, "Remove a color from the palette");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "color", "PaletteColor", "", "The color to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "clear", "api_Palette_color_clear");
  api_def_fn_ui_description(fn, "Remove all colors from the palette");

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "PaletteColor");
  api_def_prop_ptr_fns(
      prop, "api_Palette_active_color_get", "api_Palette_active_color_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Active Palette Color", "");
}

static void api_def_palettecolor(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "PaletteColor", NULL);
  api_def_struct_ui_text(sapi, "Palette Color", "");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "rgb");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "value");
  api_def_prop_ui_text(prop, "Value", "");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_sdna(prop, NULL, "value");
  api_def_prop_ui_text(prop, "Weight", "");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);
}

static void rna_def_palette(BlenderRNA *brna)
{
  ApiStruct *srna;
  ApiProp *prop;

  srna = RNA_def_struct(brna, "Palette", "ID");
  RNA_def_struct_ui_text(srna, "Palette", "");
  RNA_def_struct_ui_icon(srna, ICON_COLOR);

  prop = RNA_def_property(srna, "colors", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "PaletteColor");
  rna_def_palettecolors(brna, prop);
}

void RNA_def_palette(BlenderRNA *brna)
{
  /* *** Non-Animated *** */
  RNA_define_animate_sdna(false);
  rna_def_palettecolor(brna);
  rna_def_palette(brna);
  RNA_define_animate_sdna(true);
}

#endif
