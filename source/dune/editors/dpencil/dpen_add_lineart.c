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

  copy_v4_v4(ma->pen_style->stroke_rgba, pct->line);
  srgb_to_linearrgb_v4(ma->pen_style->stroke_rgba, ma->pen_style->stroke_rgba);

  copy_v4_v4(ma->pen_style->fill_rgba, pct->fill);
  srgb_to_linearrgb_v4(ma->pen_style->fill_rgba, ma->pen_style->fill_rgba);

  if (fill) {
    ma->dpen_style->flag |= PEN_MATERIAL_FILL_SHOW;
  }

  return index;
}

/* Color Data */
static const ColorTemplate pen_stroke_material_black = {
    "Black",
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};

/* LineArt API */
void ed_pen_create_lineart(Cxt *C, Object *ob)
{
  Main *main = cxt_data_main(C);
  PenData *pd = (PenData *)ob->data;

  /* create colors */
  int color_black = pen_lineart_material(main, ob, &pen_stroke_material_black, false);

  /* set first color as active and in brushes */
  ob->actcol = color_black + 1;

  /* layers */
  PenLayer *lines = dune_pen_layer_addnew(pd, "Lines", true, false);

  /* frames */
  dune_dpen_frame_addnew(lines, 0);

  /* update depsgraph */
  /* To trigger modifier update, this is still needed although we don't have any strokes. */
  graph_id_tag_update(&pd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  pd->flag |= PEN_DATA_CACHE_IS_DIRTY;
}
