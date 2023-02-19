#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_brush.h"
#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_material.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ed_dpen.h"

/* Definition of the most important info from a color */
typedef struct ColorTemplate {
  const char *name;
  float line[4];
  float fill[4];
} ColorTemplate;

/* Add color an ensure duplications (matched by name) */
static int dpen_lineart_material(Main *dmain,
                                    Object *ob,
                                    const ColorTemplate *pct,
                                    const bool fill)
{
  int index;
  Material *ma = dune_dpen_object_material_ensure_by_name(dmain, ob, pct->name, &index);

  copy_v4_v4(ma->dpen_style->stroke_rgba, pct->line);
  srgb_to_linearrgb_v4(ma->dpen_style->stroke_rgba, ma->dpen_style->stroke_rgba);

  copy_v4_v4(ma->dpen_style->fill_rgba, pct->fill);
  srgb_to_linearrgb_v4(ma->dpen_style->fill_rgba, ma->dpen_style->fill_rgba);

  if (fill) {
    ma->dpen_style->flag |= DPEN_MATERIAL_FILL_SHOW;
  }

  return index;
}

/* ***************************************************************** */
/* Color Data */

static const ColorTemplate dpen_stroke_material_black = {
    "Black",
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};

/* ***************************************************************** */
/* LineArt API */

void ed_dpen_create_lineart(dContext *C, Object *ob)
{
  Main *dmain = ctx_data_main(C);
  DPenData *dpd = (DPenData *)ob->data;

  /* create colors */
  int color_black = dpen_lineart_material(dmain, ob, &dpen_stroke_material_black, false);

  /* set first color as active and in brushes */
  ob->actcol = color_black + 1;

  /* layers */
  DPenLayer *lines = dune_dpen_layer_addnew(dpd, "Lines", true, false);

  /* frames */
  dune_dpen_frame_addnew(lines, 0);

  /* update depsgraph */
  /* To trigger modifier update, this is still needed although we don't have any strokes. */
  DEG_id_tag_update(&dpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  dpd->flag |= DPEN_DATA_CACHE_IS_DIRTY;
}
