#include "lib_math.h"
#include "lib_utildefines.h"

#include "types_pen.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"

#include "dune_cxt.h"
#include "dune_pen.h"
#include "dune_pen_geom.h"
#include "dune_main.h"
#include "dune_material.h"

#include "lang.h"

#include "graph.h"

#include "ed_pen.h"

/* Definition of the most important info from a color */
typedef struct ColorTemplate {
  const char *name;
  float line[4];
  float fill[4];
} ColorTemplate;

/* Add color an ensure duplications (matched by name) */
static int pen_stroke_material(Main *main, Object *ob, const ColorTemplate *pct)
{
  int index;
  Material *ma = dune_pen_object_material_ensure_by_name(main, ob, DATA_(pct->name), &index);

  copy_v4_v4(ma->pen_style->stroke_rgba, pct->line);
  srgb_to_linearrgb_v4(ma->dpen_style->stroke_rgba, ma->pen_style->stroke_rgba);

  copy_v4_v4(ma->pen_style->fill_rgba, pct->fill);
  srgb_to_linearrgb_v4(ma->dpen_style->fill_rgba, ma->pen_style->fill_rgba);

  return index;
}

/* Stroke Geometry */

/* Color Data */
static const ColorTemplate dpen_stroke_material_black = {
    N_("Black"),
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};

/* Blank API */
void ed_pen_create_blank(Cxt *C, Object *ob, float UNUSED(mat[4][4]))
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  PenData *pd = (PenData *)ob->data;

  /* create colors */
  int color_black = dpen_stroke_material(main, ob, &pen_stroke_material_black);

  /* set first color as active and in brushes */
  ob->actcol = color_black + 1;

  /* layers */
  PenLayer *layer = dune_pen_layer_addnew(pd, "Pen_Layer", true, false);

  /* frames */
  dune_pen_frame_addnew(layer, scene->r.cfra);

  /* update graph */
  graph_id_tag_update(&dpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  pd->flag |= PEN_DATA_CACHE_IS_DIRTY;
}
