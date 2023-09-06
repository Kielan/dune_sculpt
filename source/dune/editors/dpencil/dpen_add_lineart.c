#include "lib_math.h"
#include "lib_utildefines.h"

#include "types_pen.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"

#include "dune_brush.h"
#include "dune_cxt.h"
#include "dune_pen.h"
#include "dune_pen_geom.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_material.h"

#include "graph.h"
#include "graph_query.h"

#include "ed_dpen.h"

/* Definition of the most important info from a color */
typedef struct ColorTemplate {
  const char *name;
  float line[4];
  float fill[4];
} ColorTemplate;

/* Add color an ensure duplications (matched by name) */
static int pen_lineart_material(Main *main,
                                Object *ob,
                                const ColorTemplate *pct,
                                const bool fill)
{
  int index;
  Material *ma = dune_pen_object_material_ensure_by_name(main, ob, pct->name, &index);

  copy_v4_v4(ma->dpen_style->stroke_rgba, pct->line);
  srgb_to_linearrgb_v4(ma->dpen_style->stroke_rgba, ma->pen_style->stroke_rgba);

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
