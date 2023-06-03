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
static void rna_def_palettecolors(DuneApi *dapi, ApiProp *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "PaletteColors");
  srna = RNA_def_struct(brna, "PaletteColors", NULL);
  RNA_def_struct_sdna(srna, "Palette");
  RNA_def_struct_ui_text(srna, "Palette Splines", "Collection of palette colors");

  func = RNA_def_function(srna, "new", "rna_Palette_color_new");
  RNA_def_function_ui_description(func, "Add a new color to the palette");
  parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The newly created color");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Palette_color_remove");
  RNA_def_function_ui_description(func, "Remove a color from the palette");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The color to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

  func = RNA_def_function(srna, "clear", "rna_Palette_color_clear");
  RNA_def_function_ui_description(func, "Remove all colors from the palette");

  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "PaletteColor");
  RNA_def_property_pointer_funcs(
      prop, "rna_Palette_active_color_get", "rna_Palette_active_color_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Active Palette Color", "");
}

static void rna_def_palettecolor(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "PaletteColor", NULL);
  RNA_def_struct_ui_text(srna, "Palette Color", "");

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  RNA_def_property_range(prop, 0.0, 1.0);
  RNA_def_property_float_sdna(prop, NULL, "rgb");
  RNA_def_property_flag(prop, PROP_LIB_EXCEPTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Color", "");
  RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

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
  StructRNA *srna;
  PropertyRNA *prop;

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
