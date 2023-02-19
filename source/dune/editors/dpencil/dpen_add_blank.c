#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"
#include "BKE_material.h"

#include "DEG_depsgraph.h"

#include "ED_gpencil.h"

/* Definition of the most important info from a color */
typedef struct ColorTemplate {
  const char *name;
  float line[4];
  float fill[4];
} ColorTemplate;

/* Add color an ensure duplications (matched by name) */
static int dpen_stroke_material(Main *bmain, Object *ob, const ColorTemplate *pct)
{
  int index;
  Material *ma = dune_dpen_object_material_ensure_by_name(dmain, ob, pct->name, &index);

  copy_v4_v4(ma->gp_style->stroke_rgba, pct->line);
  srgb_to_linearrgb_v4(ma->dpen_style->stroke_rgba, ma->dpen_style->stroke_rgba);

  copy_v4_v4(ma->gp_style->fill_rgba, pct->fill);
  srgb_to_linearrgb_v4(ma->dpen_style->fill_rgba, ma->dpen_style->fill_rgba);

  return index;
}

/* ***************************************************************** */
/* Stroke Geometry */

/* ***************************************************************** */
/* Color Data */

static const ColorTemplate dpen_stroke_material_black = {
    "Black",
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};

/* ***************************************************************** */
/* Blank API */

void ed_dpen_create_blank(dContext *C, Object *ob, float UNUSED(mat[4][4]))
{
  Main *dmain = ctx_data_main(C);
  Scene *scene = ctx_data_scene(C);
  DPenData *dpd = (DPenData *)ob->data;

  /* create colors */
  int color_black = dpen_stroke_material(dmain, ob, &dpen_stroke_material_black);

  /* set first color as active and in brushes */
  ob->actcol = color_black + 1;

  /* layers */
  DPenLayer *layer = dune_dpen_layer_addnew(dpd, "DPEN_Layer", true, false);

  /* frames */
  dune_dpen_frame_addnew(layer, CFRA);

  /* update depsgraph */
  DEG_id_tag_update(&dpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  dpd->flag |= DPEN_DATA_CACHE_IS_DIRTY;
}
