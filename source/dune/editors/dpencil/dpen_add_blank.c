#include "lib_math.h"
#include "lib_utildefines.h"

#include "types_dpen.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"

#include "dune_context.h"
#include "dune_dpen.h"
#include "dune_dpen_geom.h"
#include "dune_main.h"
#include "dune_material.h"

#include "i18n_translation.h"

#include "DEG_depsgraph.h"

#include "ed_dpen.h"

/* Definition of the most important info from a color */
typedef struct ColorTemplate {
  const char *name;
  float line[4];
  float fill[4];
} ColorTemplate;

/* Add color an ensure duplications (matched by name) */
static int dpen_stroke_material(Main *dmain, Object *ob, const ColorTemplate *pct)
{
  int index;
  Material *ma = dune_dpen_object_material_ensure_by_name(dmain, ob, DATA_(pct->name), &index);

  copy_v4_v4(ma->dpen_style->stroke_rgba, pct->line);
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
    N_("Black"),
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};

/* ***************************************************************** */
/* Blank API */

void ed_dpen_create_blank(DContext *C, Object *ob, float UNUSED(mat[4][4]))
{
  Main *dmain = ctx_data_main(C);
  Scene *scene = ctx_data_scene(C);
  DPenData *dpd = (DPenData *)ob->data;

  /* create colors */
  int color_black = dpen_stroke_material(dmain, ob, &dpen_stroke_material_black);

  /* set first color as active and in brushes */
  ob->actcol = color_black + 1;

  /* layers */
  DPenLayer *layer = dune_dpen_layer_addnew(dpd, "DPen_Layer", true, false);

  /* frames */
  dune_dpen_frame_addnew(layer, scene->r.cfra);

  /* update depsgraph */
  DEG_id_tag_update(&dpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  dpd->flag |= DPEN_DATA_CACHE_IS_DIRTY;
}
