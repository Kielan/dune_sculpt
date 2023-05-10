#include <stdlib.h>

#include "DNA_brush_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"
#include "DNA_workspace_types.h"

#include "BLI_math.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "IMB_imbuf.h"

#include "WM_types.h"

static const EnumPropertyItem prop_direction_items[] = {
    {0, "ADD", ICON_ADD, "Add", "Add effect of brush"},
    {BRUSH_DIR_IN, "SUBTRACT", ICON_REMOVE, "Subtract", "Subtract effect of brush"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef RNA_RUNTIME
static const EnumPropertyItem prop_smooth_direction_items[] = {
    {0, "SMOOTH", ICON_ADD, "Smooth", "Smooth the surface"},
    {BRUSH_DIR_IN,
     "ENHANCE_DETAILS",
     ICON_REMOVE,
     "Enhance Details",
     "Enhance the surface detail"},
    {0, NULL, 0, NULL, NULL},
};
#endif

static const EnumPropertyItem sculpt_stroke_method_items[] = {
    {0, "DOTS", 0, "Dots", "Apply paint on each mouse move step"},
    {BRUSH_DRAG_DOT, "DRAG_DOT", 0, "Drag Dot", "Allows a single dot to be carefully positioned"},
    {BRUSH_SPACE,
     "SPACE",
     0,
     "Space",
     "Limit brush application to the distance specified by spacing"},
    {BRUSH_AIRBRUSH,
     "AIRBRUSH",
     0,
     "Airbrush",
     "Keep applying paint effect while holding mouse (spray)"},
    {BRUSH_ANCHORED, "ANCHORED", 0, "Anchored", "Keep the brush anchored to the initial location"},
    {BRUSH_LINE, "LINE", 0, "Line", "Draw a line with dabs separated according to spacing"},
    {BRUSH_CURVE,
     "CURVE",
     0,
     "Curve",
     "Define the stroke curve with a bezier curve (dabs are separated according to spacing)"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_brush_texture_slot_map_all_mode_items[] = {
    {MTEX_MAP_MODE_VIEW, "VIEW_PLANE", 0, "View Plane", ""},
    {MTEX_MAP_MODE_AREA, "AREA_PLANE", 0, "Area Plane", ""},
    {MTEX_MAP_MODE_TILED, "TILED", 0, "Tiled", ""},
    {MTEX_MAP_MODE_3D, "3D", 0, "3D", ""},
    {MTEX_MAP_MODE_RANDOM, "RANDOM", 0, "Random", ""},
    {MTEX_MAP_MODE_STENCIL, "STENCIL", 0, "Stencil", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef RNA_RUNTIME
static const EnumPropertyItem rna_enum_brush_texture_slot_map_texture_mode_items[] = {
    {MTEX_MAP_MODE_VIEW, "VIEW_PLANE", 0, "View Plane", ""},
    {MTEX_MAP_MODE_TILED, "TILED", 0, "Tiled", ""},
    {MTEX_MAP_MODE_3D, "3D", 0, "3D", ""},
    {MTEX_MAP_MODE_RANDOM, "RANDOM", 0, "Random", ""},
    {MTEX_MAP_MODE_STENCIL, "STENCIL", 0, "Stencil", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

/* clang-format off */
const EnumPropertyItem rna_enum_brush_sculpt_tool_items[] = {
    {SCULPT_TOOL_DRAW, "DRAW", ICON_BRUSH_SCULPT_DRAW, "Draw", ""},
    {SCULPT_TOOL_DRAW_SHARP, "DRAW_SHARP", ICON_BRUSH_SCULPT_DRAW, "Draw Sharp", ""},
    {SCULPT_TOOL_CLAY, "CLAY", ICON_BRUSH_CLAY, "Clay", ""},
    {SCULPT_TOOL_CLAY_STRIPS, "CLAY_STRIPS", ICON_BRUSH_CLAY_STRIPS, "Clay Strips", ""},
    {SCULPT_TOOL_CLAY_THUMB, "CLAY_THUMB", ICON_BRUSH_CLAY_STRIPS, "Clay Thumb", ""},
    {SCULPT_TOOL_LAYER, "LAYER", ICON_BRUSH_LAYER, "Layer", ""},
    {SCULPT_TOOL_INFLATE, "INFLATE", ICON_BRUSH_INFLATE, "Inflate", ""},
    {SCULPT_TOOL_BLOB, "BLOB", ICON_BRUSH_BLOB, "Blob", ""},
    {SCULPT_TOOL_CREASE, "CREASE", ICON_BRUSH_CREASE, "Crease", ""},
    {0, "", 0, NULL, NULL},
    {SCULPT_TOOL_SMOOTH, "SMOOTH", ICON_BRUSH_SMOOTH, "Smooth", ""},
    {SCULPT_TOOL_FLATTEN, "FLATTEN", ICON_BRUSH_FLATTEN, "Flatten", ""},
    {SCULPT_TOOL_FILL, "FILL", ICON_BRUSH_FILL, "Fill", ""},
    {SCULPT_TOOL_SCRAPE, "SCRAPE", ICON_BRUSH_SCRAPE, "Scrape", ""},
    {SCULPT_TOOL_MULTIPLANE_SCRAPE, "MULTIPLANE_SCRAPE", ICON_BRUSH_SCRAPE, "Multi-plane Scrape", ""},
    {SCULPT_TOOL_PINCH, "PINCH", ICON_BRUSH_PINCH, "Pinch", ""},
    {0, "", 0, NULL, NULL},
    {SCULPT_TOOL_GRAB, "GRAB", ICON_BRUSH_GRAB, "Grab", ""},
    {SCULPT_TOOL_ELASTIC_DEFORM, "ELASTIC_DEFORM", ICON_BRUSH_GRAB, "Elastic Deform", ""},
    {SCULPT_TOOL_SNAKE_HOOK, "SNAKE_HOOK", ICON_BRUSH_SNAKE_HOOK, "Snake Hook", ""},
    {SCULPT_TOOL_THUMB, "THUMB", ICON_BRUSH_THUMB, "Thumb", ""},
    {SCULPT_TOOL_POSE, "POSE", ICON_BRUSH_GRAB, "Pose", ""},
    {SCULPT_TOOL_NUDGE, "NUDGE", ICON_BRUSH_NUDGE, "Nudge", ""},
    {SCULPT_TOOL_ROTATE, "ROTATE", ICON_BRUSH_ROTATE, "Rotate", ""},
    {SCULPT_TOOL_SLIDE_RELAX, "TOPOLOGY", ICON_BRUSH_GRAB, "Slide Relax", ""},
    {SCULPT_TOOL_BOUNDARY, "BOUNDARY", ICON_BRUSH_GRAB, "Boundary", ""},
    {0, "", 0, NULL, NULL},
    {SCULPT_TOOL_CLOTH, "CLOTH", ICON_BRUSH_SCULPT_DRAW, "Cloth", ""},
    {SCULPT_TOOL_SIMPLIFY, "SIMPLIFY", ICON_BRUSH_DATA, "Simplify", ""},
    {SCULPT_TOOL_MASK, "MASK", ICON_BRUSH_MASK, "Mask", ""},
    {SCULPT_TOOL_DISPLACEMENT_ERASER, "DISPLACEMENT_ERASER", ICON_BRUSH_SCULPT_DRAW, "Multires Displacement Eraser", ""},
    {SCULPT_TOOL_DISPLACEMENT_SMEAR, "DISPLACEMENT_SMEAR", ICON_BRUSH_SCULPT_DRAW, "Multires Displacement Smear", ""},
    {SCULPT_TOOL_PAINT, "PAINT", ICON_BRUSH_SCULPT_DRAW, "Paint", ""},
    {SCULPT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_SCULPT_DRAW, "Smear", ""},
    {SCULPT_TOOL_DRAW_FACE_SETS, "DRAW_FACE_SETS", ICON_BRUSH_MASK, "Draw Face Sets", ""},
    {0, NULL, 0, NULL, NULL},
};
/* clang-format on */

const EnumPropertyItem rna_enum_brush_uv_sculpt_tool_items[] = {
    {UV_SCULPT_TOOL_GRAB, "GRAB", 0, "Grab", "Grab UVs"},
    {UV_SCULPT_TOOL_RELAX, "RELAX", 0, "Relax", "Relax UVs"},
    {UV_SCULPT_TOOL_PINCH, "PINCH", 0, "Pinch", "Pinch UVs"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_vertex_tool_items[] = {
    {VPAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {VPAINT_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {VPAINT_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {VPAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_weight_tool_items[] = {
    {WPAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {WPAINT_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {WPAINT_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {WPAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_image_tool_items[] = {
    {PAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_TEXDRAW, "Draw", ""},
    {PAINT_TOOL_SOFTEN, "SOFTEN", ICON_BRUSH_SOFTEN, "Soften", ""},
    {PAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_SMEAR, "Smear", ""},
    {PAINT_TOOL_CLONE, "CLONE", ICON_BRUSH_CLONE, "Clone", ""},
    {PAINT_TOOL_FILL, "FILL", ICON_BRUSH_TEXFILL, "Fill", ""},
    {PAINT_TOOL_MASK, "MASK", ICON_BRUSH_TEXMASK, "Mask", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_gpencil_types_items[] = {
    {GPAINT_TOOL_DRAW,
     "DRAW",
     ICON_STROKE,
     "Draw",
     "The brush is of type used for drawing strokes"},
    {GPAINT_TOOL_FILL, "FILL", ICON_COLOR, "Fill", "The brush is of type used for filling areas"},
    {GPAINT_TOOL_ERASE,
     "ERASE",
     ICON_PANEL_CLOSE,
     "Erase",
     "The brush is used for erasing strokes"},
    {GPAINT_TOOL_TINT,
     "TINT",
     ICON_BRUSH_TEXDRAW,
     "Tint",
     "The brush is of type used for tinting strokes"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_gpencil_vertex_types_items[] = {
    {GPVERTEX_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {GPVERTEX_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {GPVERTEX_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {GPVERTEX_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {GPVERTEX_TOOL_REPLACE, "REPLACE", ICON_BRUSH_BLUR, "Replace", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_gpencil_sculpt_types_items[] = {
    {GPSCULPT_TOOL_SMOOTH, "SMOOTH", ICON_GPBRUSH_SMOOTH, "Smooth", "Smooth stroke points"},
    {GPSCULPT_TOOL_THICKNESS,
     "THICKNESS",
     ICON_GPBRUSH_THICKNESS,
     "Thickness",
     "Adjust thickness of strokes"},
    {GPSCULPT_TOOL_STRENGTH,
     "STRENGTH",
     ICON_GPBRUSH_STRENGTH,
     "Strength",
     "Adjust color strength of strokes"},
    {GPSCULPT_TOOL_RANDOMIZE,
     "RANDOMIZE",
     ICON_GPBRUSH_RANDOMIZE,
     "Randomize",
     "Introduce jitter/randomness into strokes"},
    {GPSCULPT_TOOL_GRAB,
     "GRAB",
     ICON_GPBRUSH_GRAB,
     "Grab",
     "Translate the set of points initially within the brush circle"},
    {GPSCULPT_TOOL_PUSH,
     "PUSH",
     ICON_GPBRUSH_PUSH,
     "Push",
     "Move points out of the way, as if combing them"},
    {GPSCULPT_TOOL_TWIST,
     "TWIST",
     ICON_GPBRUSH_TWIST,
     "Twist",
     "Rotate points around the midpoint of the brush"},
    {GPSCULPT_TOOL_PINCH,
     "PINCH",
     ICON_GPBRUSH_PINCH,
     "Pinch",
     "Pull points towards the midpoint of the brush"},
    {GPSCULPT_TOOL_CLONE,
     "CLONE",
     ICON_GPBRUSH_CLONE,
     "Clone",
     "Paste copies of the strokes stored on the clipboard"},
    {0, NULL, 0, NULL, NULL}};

const EnumPropertyItem rna_enum_brush_gpencil_weight_types_items[] = {
    {GPWEIGHT_TOOL_DRAW,
     "WEIGHT",
     ICON_GPBRUSH_WEIGHT,
     "Weight",
     "Weight Paint for Vertex Groups"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_brush_curves_sculpt_tool_items[] = {
    {CURVES_SCULPT_TOOL_COMB, "COMB", ICON_NONE, "Comb", ""},
    {CURVES_SCULPT_TOOL_DELETE, "DELETE", ICON_NONE, "Delete", ""},
    {CURVES_SCULPT_TOOL_SNAKE_HOOK, "SNAKE_HOOK", ICON_NONE, "Snake Hook", ""},
    {CURVES_SCULPT_TOOL_ADD, "ADD", ICON_NONE, "Add", ""},
    {CURVES_SCULPT_TOOL_TEST1, "TEST1", ICON_NONE, "Test 1", ""},
    {CURVES_SCULPT_TOOL_TEST2, "TEST2", ICON_NONE, "Test 2", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifndef RNA_RUNTIME
static EnumPropertyItem rna_enum_gpencil_brush_eraser_modes_items[] = {
    {GP_BRUSH_ERASER_SOFT,
     "SOFT",
     0,
     "Dissolve",
     "Erase strokes, fading their points strength and thickness"},
    {GP_BRUSH_ERASER_HARD, "HARD", 0, "Point", "Erase stroke points"},
    {GP_BRUSH_ERASER_STROKE, "STROKE", 0, "Stroke", "Erase entire strokes"},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropertyItem rna_enum_gpencil_fill_draw_modes_items[] = {
    {GP_FILL_DMODE_BOTH,
     "BOTH",
     0,
     "All",
     "Use both visible strokes and edit lines as fill boundary limits"},
    {GP_FILL_DMODE_STROKE, "STROKE", 0, "Strokes", "Use visible strokes as fill boundary limits"},
    {GP_FILL_DMODE_CONTROL, "CONTROL", 0, "Edit Lines", "Use edit lines as fill boundary limits"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropertyItem rna_enum_gpencil_fill_layers_modes_items[] = {
    {GP_FILL_GPLMODE_VISIBLE, "VISIBLE", 0, "Visible", "Visible layers"},
    {GP_FILL_GPLMODE_ACTIVE, "ACTIVE", 0, "Active", "Only active layer"},
    {GP_FILL_GPLMODE_ABOVE, "ABOVE", 0, "Layer Above", "Layer above active"},
    {GP_FILL_GPLMODE_BELOW, "BELOW", 0, "Layer Below", "Layer below active"},
    {GP_FILL_GPLMODE_ALL_ABOVE, "ALL_ABOVE", 0, "All Above", "All layers above active"},
    {GP_FILL_GPLMODE_ALL_BELOW, "ALL_BELOW", 0, "All Below", "All layers below active"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropertyItem rna_enum_gpencil_fill_direction_items[] = {
    {0, "NORMAL", ICON_ADD, "Normal", "Fill internal area"},
    {BRUSH_DIR_IN, "INVERT", ICON_REMOVE, "Inverted", "Fill inverted area"},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropertyItem rna_enum_gpencil_brush_modes_items[] = {
    {GP_BRUSH_MODE_ACTIVE, "ACTIVE", 0, "Active", "Use current mode"},
    {GP_BRUSH_MODE_MATERIAL, "MATERIAL", 0, "Material", "Use always material mode"},
    {GP_BRUSH_MODE_VERTEXCOLOR, "VERTEXCOLOR", 0, "Vertex Color", "Use always Vertex Color mode"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropertyItem rna_enum_gpencil_brush_paint_icons_items[] = {
    {GP_BRUSH_ICON_PENCIL, "PENCIL", ICON_GPBRUSH_PENCIL, "Pencil", ""},
    {GP_BRUSH_ICON_PEN, "PEN", ICON_GPBRUSH_PEN, "Pen", ""},
    {GP_BRUSH_ICON_INK, "INK", ICON_GPBRUSH_INK, "Ink", ""},
    {GP_BRUSH_ICON_INKNOISE, "INKNOISE", ICON_GPBRUSH_INKNOISE, "Ink Noise", ""},
    {GP_BRUSH_ICON_BLOCK, "BLOCK", ICON_GPBRUSH_BLOCK, "Block", ""},
    {GP_BRUSH_ICON_MARKER, "MARKER", ICON_GPBRUSH_MARKER, "Marker", ""},
    {GP_BRUSH_ICON_AIRBRUSH, "AIRBRUSH", ICON_GPBRUSH_AIRBRUSH, "Airbrush", ""},
    {GP_BRUSH_ICON_CHISEL, "CHISEL", ICON_GPBRUSH_CHISEL, "Chisel", ""},
    {GP_BRUSH_ICON_FILL, "FILL", ICON_GPBRUSH_FILL, "Fill", ""},
    {GP_BRUSH_ICON_ERASE_SOFT, "SOFT", ICON_GPBRUSH_ERASE_SOFT, "Eraser Soft", ""},
    {GP_BRUSH_ICON_ERASE_HARD, "HARD", ICON_GPBRUSH_ERASE_HARD, "Eraser Hard", ""},
    {GP_BRUSH_ICON_ERASE_STROKE, "STROKE", ICON_GPBRUSH_ERASE_STROKE, "Eraser Stroke", ""},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropertyItem rna_enum_gpencil_brush_sculpt_icons_items[] = {
    {GP_BRUSH_ICON_GPBRUSH_SMOOTH, "SMOOTH", ICON_GPBRUSH_SMOOTH, "Smooth", ""},
    {GP_BRUSH_ICON_GPBRUSH_THICKNESS, "THICKNESS", ICON_GPBRUSH_THICKNESS, "Thickness", ""},
    {GP_BRUSH_ICON_GPBRUSH_STRENGTH, "STRENGTH", ICON_GPBRUSH_STRENGTH, "Strength", ""},
    {GP_BRUSH_ICON_GPBRUSH_RANDOMIZE, "RANDOMIZE", ICON_GPBRUSH_RANDOMIZE, "Randomize", ""},
    {GP_BRUSH_ICON_GPBRUSH_GRAB, "GRAB", ICON_GPBRUSH_GRAB, "Grab", ""},
    {GP_BRUSH_ICON_GPBRUSH_PUSH, "PUSH", ICON_GPBRUSH_PUSH, "Push", ""},
    {GP_BRUSH_ICON_GPBRUSH_TWIST, "TWIST", ICON_GPBRUSH_TWIST, "Twist", ""},
    {GP_BRUSH_ICON_GPBRUSH_PINCH, "PINCH", ICON_GPBRUSH_PINCH, "Pinch", ""},
    {GP_BRUSH_ICON_GPBRUSH_CLONE, "CLONE", ICON_GPBRUSH_CLONE, "Clone", ""},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropertyItem rna_enum_gpencil_brush_weight_icons_items[] = {
    {GP_BRUSH_ICON_GPBRUSH_WEIGHT, "DRAW", ICON_GPBRUSH_WEIGHT, "Draw", ""},
    {0, NULL, 0, NULL, NULL},
};
static EnumPropertyItem rna_enum_gpencil_brush_vertex_icons_items[] = {
    {GP_BRUSH_ICON_VERTEX_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {GP_BRUSH_ICON_VERTEX_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {GP_BRUSH_ICON_VERTEX_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {GP_BRUSH_ICON_VERTEX_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {GP_BRUSH_ICON_VERTEX_REPLACE, "REPLACE", ICON_BRUSH_MIX, "Replace", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef RNA_RUNTIME

#  include "MEM_guardedalloc.h"

#  include "RNA_access.h"

#  include "BKE_brush.h"
#  include "BKE_colorband.h"
#  include "BKE_gpencil.h"
#  include "BKE_icons.h"
#  include "BKE_material.h"
#  include "BKE_paint.h"

#  include "WM_api.h"

static bool rna_BrushCapabilitiesSculpt_has_accumulate_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_ACCUMULATE(br->sculpt_tool);
}

static bool rna_BrushCapabilitiesSculpt_has_topology_rake_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_TOPOLOGY_RAKE(br->sculpt_tool);
}

static bool rna_BrushCapabilitiesSculpt_has_auto_smooth_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(
      br->sculpt_tool, SCULPT_TOOL_MASK, SCULPT_TOOL_SMOOTH, SCULPT_TOOL_PAINT, SCULPT_TOOL_SMEAR);
}

static bool rna_BrushCapabilitiesSculpt_has_height_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return br->sculpt_tool == SCULPT_TOOL_LAYER;
}

static bool rna_BrushCapabilitiesSculpt_has_jitter_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED) && !(br->flag & BRUSH_DRAG_DOT) &&
          !ELEM(br->sculpt_tool,
                SCULPT_TOOL_GRAB,
                SCULPT_TOOL_ROTATE,
                SCULPT_TOOL_SNAKE_HOOK,
                SCULPT_TOOL_THUMB));
}

static bool rna_BrushCapabilitiesSculpt_has_normal_weight_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_NORMAL_WEIGHT(br->sculpt_tool);
}

static bool rna_BrushCapabilitiesSculpt_has_rake_factor_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_RAKE(br->sculpt_tool);
}

static bool rna_BrushCapabilities_has_overlay_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(
      br->mtex.brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_TILED, MTEX_MAP_MODE_STENCIL);
}

static bool rna_BrushCapabilitiesSculpt_has_persistence_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_LAYER, SCULPT_TOOL_CLOTH);
}

static bool rna_BrushCapabilitiesSculpt_has_pinch_factor_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_BLOB, SCULPT_TOOL_CREASE, SCULPT_TOOL_SNAKE_HOOK);
}

static bool rna_BrushCapabilitiesSculpt_has_plane_offset_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool,
              SCULPT_TOOL_CLAY,
              SCULPT_TOOL_CLAY_STRIPS,
              SCULPT_TOOL_CLAY_THUMB,
              SCULPT_TOOL_FILL,
              SCULPT_TOOL_FLATTEN,
              SCULPT_TOOL_SCRAPE);
}

static bool rna_BrushCapabilitiesSculpt_has_random_texture_angle_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!ELEM(br->sculpt_tool,
                SCULPT_TOOL_GRAB,
                SCULPT_TOOL_ROTATE,
                SCULPT_TOOL_SNAKE_HOOK,
                SCULPT_TOOL_THUMB));
}

static bool rna_TextureCapabilities_has_random_texture_angle_get(PointerRNA *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return ELEM(mtex->brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_AREA, MTEX_MAP_MODE_RANDOM);
}

static bool rna_BrushCapabilities_has_random_texture_angle_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !(br->flag & BRUSH_ANCHORED);
}

static bool rna_BrushCapabilitiesSculpt_has_sculpt_plane_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool,
               SCULPT_TOOL_INFLATE,
               SCULPT_TOOL_MASK,
               SCULPT_TOOL_PINCH,
               SCULPT_TOOL_SMOOTH);
}

static bool rna_BrushCapabilitiesSculpt_has_color_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_PAINT);
}

static bool rna_BrushCapabilitiesSculpt_has_secondary_color_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return BKE_brush_sculpt_has_secondary_color(br);
}

static bool rna_BrushCapabilitiesSculpt_has_smooth_stroke_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED) && !(br->flag & BRUSH_DRAG_DOT) &&
          !(br->flag & BRUSH_LINE) && !(br->flag & BRUSH_CURVE) &&
          !ELEM(br->sculpt_tool,
                SCULPT_TOOL_GRAB,
                SCULPT_TOOL_ROTATE,
                SCULPT_TOOL_SNAKE_HOOK,
                SCULPT_TOOL_THUMB));
}

static bool rna_BrushCapabilities_has_smooth_stroke_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED) && !(br->flag & BRUSH_DRAG_DOT) &&
          !(br->flag & BRUSH_LINE) && !(br->flag & BRUSH_CURVE));
}

static bool rna_BrushCapabilitiesSculpt_has_space_attenuation_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ((br->flag & (BRUSH_SPACE | BRUSH_LINE | BRUSH_CURVE)) && !ELEM(br->sculpt_tool,
                                                                         SCULPT_TOOL_GRAB,
                                                                         SCULPT_TOOL_ROTATE,
                                                                         SCULPT_TOOL_SMOOTH,
                                                                         SCULPT_TOOL_SNAKE_HOOK));
}

static bool rna_BrushCapabilitiesImagePaint_has_space_attenuation_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (br->flag & (BRUSH_SPACE | BRUSH_LINE | BRUSH_CURVE)) &&
         br->imagepaint_tool != PAINT_TOOL_FILL;
}

static bool rna_BrushCapabilitiesImagePaint_has_color_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->imagepaint_tool, PAINT_TOOL_DRAW, PAINT_TOOL_FILL);
}

static bool rna_BrushCapabilitiesVertexPaint_has_color_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->vertexpaint_tool, VPAINT_TOOL_DRAW);
}

static bool rna_BrushCapabilitiesWeightPaint_has_weight_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->weightpaint_tool, WPAINT_TOOL_DRAW);
}

static bool rna_BrushCapabilities_has_spacing_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED));
}

static bool rna_BrushCapabilitiesSculpt_has_strength_pressure_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool, SCULPT_TOOL_GRAB, SCULPT_TOOL_SNAKE_HOOK);
}

static bool rna_TextureCapabilities_has_texture_angle_get(PointerRNA *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return mtex->brush_map_mode != MTEX_MAP_MODE_3D;
}

static bool rna_BrushCapabilitiesSculpt_has_direction_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool,
               SCULPT_TOOL_DRAW,
               SCULPT_TOOL_DRAW_SHARP,
               SCULPT_TOOL_CLAY,
               SCULPT_TOOL_CLAY_STRIPS,
               SCULPT_TOOL_SMOOTH,
               SCULPT_TOOL_LAYER,
               SCULPT_TOOL_INFLATE,
               SCULPT_TOOL_BLOB,
               SCULPT_TOOL_CREASE,
               SCULPT_TOOL_FLATTEN,
               SCULPT_TOOL_FILL,
               SCULPT_TOOL_SCRAPE,
               SCULPT_TOOL_CLAY,
               SCULPT_TOOL_PINCH,
               SCULPT_TOOL_MASK);
}

static bool rna_BrushCapabilitiesSculpt_has_gravity_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool, SCULPT_TOOL_MASK, SCULPT_TOOL_SMOOTH);
}

static bool rna_BrushCapabilitiesSculpt_has_tilt_get(PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool,
              SCULPT_TOOL_DRAW,
              SCULPT_TOOL_DRAW_SHARP,
              SCULPT_TOOL_FLATTEN,
              SCULPT_TOOL_FILL,
              SCULPT_TOOL_SCRAPE,
              SCULPT_TOOL_CLAY_STRIPS,
              SCULPT_TOOL_CLAY_THUMB);
}

static bool rna_TextureCapabilities_has_texture_angle_source_get(PointerRNA *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return ELEM(mtex->brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_AREA, MTEX_MAP_MODE_RANDOM);
}

static bool rna_BrushCapabilitiesImagePaint_has_accumulate_get(PointerRNA *ptr)
{
  /* only support for draw tool */
  Brush *br = (Brush *)ptr->data;

  return ((br->flag & BRUSH_AIRBRUSH) || (br->flag & BRUSH_DRAG_DOT) ||
          (br->flag & BRUSH_ANCHORED) || (br->imagepaint_tool == PAINT_TOOL_SOFTEN) ||
          (br->imagepaint_tool == PAINT_TOOL_SMEAR) || (br->imagepaint_tool == PAINT_TOOL_FILL) ||
          (br->mtex.tex && !ELEM(br->mtex.brush_map_mode,
                                 MTEX_MAP_MODE_TILED,
                                 MTEX_MAP_MODE_STENCIL,
                                 MTEX_MAP_MODE_3D))) ?
             false :
             true;
}

static bool rna_BrushCapabilitiesImagePaint_has_radius_get(PointerRNA *ptr)
{
  /* only support for draw tool */
  Brush *br = (Brush *)ptr->data;

  return (br->imagepaint_tool != PAINT_TOOL_FILL);
}

static PointerRNA rna_Sculpt_tool_capabilities_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_BrushCapabilitiesSculpt, ptr->owner_id);
}

static PointerRNA rna_Imapaint_tool_capabilities_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_BrushCapabilitiesImagePaint, ptr->owner_id);
}

static PointerRNA rna_Vertexpaint_tool_capabilities_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_BrushCapabilitiesVertexPaint, ptr->owner_id);
}

static PointerRNA rna_Weightpaint_tool_capabilities_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_BrushCapabilitiesWeightPaint, ptr->owner_id);
}

static PointerRNA rna_Brush_capabilities_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_BrushCapabilities, ptr->owner_id);
}

static void rna_Brush_reset_icon(Brush *br)
{
  ID *id = &br->id;

  if (br->flag & BRUSH_CUSTOM_ICON) {
    return;
  }

  if (id->icon_id >= BIFICONID_LAST) {
    BKE_icon_id_delete(id);
    BKE_previewimg_id_free(id);
  }

  id->icon_id = 0;
}

static void rna_Brush_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;
  WM_main_add_notifier(NC_BRUSH | NA_EDITED, br);
  /*WM_main_add_notifier(NC_SPACE|ND_SPACE_VIEW3D, NULL); */
}

static void rna_Brush_material_update(bContext *UNUSED(C), PointerRNA *UNUSED(ptr))
{
  /* number of material users changed */
  WM_main_add_notifier(NC_SPACE | ND_SPACE_PROPERTIES, NULL);
}

static void rna_Brush_main_tex_update(bContext *C, PointerRNA *ptr)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Brush *br = (Brush *)ptr->data;
  BKE_paint_invalidate_overlay_tex(scene, view_layer, br->mtex.tex);
  rna_Brush_update(bmain, scene, ptr);
}

static void rna_Brush_secondary_tex_update(bContext *C, PointerRNA *ptr)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Brush *br = (Brush *)ptr->data;
  BKE_paint_invalidate_overlay_tex(scene, view_layer, br->mask_mtex.tex);
  rna_Brush_update(bmain, scene, ptr);
}

static void rna_Brush_size_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  BKE_paint_invalidate_overlay_all();
  rna_Brush_update(bmain, scene, ptr);
}

static void rna_Brush_update_and_reset_icon(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  Brush *br = ptr->data;
  rna_Brush_reset_icon(br);
  rna_Brush_update(bmain, scene, ptr);
}

static void rna_Brush_stroke_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  WM_main_add_notifier(NC_SCENE | ND_TOOLSETTINGS, scene);
  rna_Brush_update(bmain, scene, ptr);
}

static void rna_Brush_icon_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;

  if (br->icon_imbuf) {
    IMB_freeImBuf(br->icon_imbuf);
    br->icon_imbuf = NULL;
  }

  br->id.icon_id = 0;

  if (br->flag & BRUSH_CUSTOM_ICON) {
    BKE_icon_changed(BKE_icon_id_ensure(&br->id));
  }

  WM_main_add_notifier(NC_BRUSH | NA_EDITED, br);
}

static bool rna_Brush_imagetype_poll(PointerRNA *UNUSED(ptr), PointerRNA value)
{
  Image *image = (Image *)value.owner_id;
  return image->type != IMA_TYPE_R_RESULT && image->type != IMA_TYPE_COMPOSITE;
}

static void rna_TextureSlot_brush_angle_update(bContext *C, PointerRNA *ptr)
{
  Scene *scene = CTX_data_scene(C);
  MTex *mtex = ptr->data;
  /* skip invalidation of overlay for stencil mode */
  if (mtex->mapping != MTEX_MAP_MODE_STENCIL) {
    ViewLayer *view_layer = CTX_data_view_layer(C);
    BKE_paint_invalidate_overlay_tex(scene, view_layer, mtex->tex);
  }

  rna_TextureSlot_update(C, ptr);
}

static void rna_Brush_set_size(PointerRNA *ptr, int value)
{
  Brush *brush = ptr->data;

  /* scale unprojected radius so it stays consistent with brush size */
  BKE_brush_scale_unprojected_radius(&brush->unprojected_radius, value, brush->size);
  brush->size = value;
}

static void rna_Brush_use_gradient_set(PointerRNA *ptr, bool value)
{
  Brush *br = (Brush *)ptr->data;

  if (value) {
    br->flag |= BRUSH_USE_GRADIENT;
  }
  else {
    br->flag &= ~BRUSH_USE_GRADIENT;
  }

  if ((br->flag & BRUSH_USE_GRADIENT) && br->gradient == NULL) {
    br->gradient = BKE_colorband_add(true);
  }
}

static void rna_Brush_set_unprojected_radius(PointerRNA *ptr, float value)
{
  Brush *brush = ptr->data;

  /* scale brush size so it stays consistent with unprojected_radius */
  BKE_brush_scale_size(&brush->size, value, brush->unprojected_radius);
  brush->unprojected_radius = value;
}

static const EnumPropertyItem *rna_Brush_direction_itemf(bContext *C,
                                                         PointerRNA *ptr,
                                                         PropertyRNA *UNUSED(prop),
                                                         bool *UNUSED(r_free))
{
  ePaintMode mode = BKE_paintmode_get_active_from_context(C);

  /* sculpt mode */
  static const EnumPropertyItem prop_flatten_contrast_items[] = {
      {BRUSH_DIR_IN, "CONTRAST", ICON_ADD, "Contrast", "Subtract effect of brush"},
      {0, "FLATTEN", ICON_REMOVE, "Flatten", "Add effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_fill_deepen_items[] = {
      {0, "FILL", ICON_ADD, "Fill", "Add effect of brush"},
      {BRUSH_DIR_IN, "DEEPEN", ICON_REMOVE, "Deepen", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_scrape_peaks_items[] = {
      {0, "SCRAPE", ICON_ADD, "Scrape", "Add effect of brush"},
      {BRUSH_DIR_IN, "PEAKS", ICON_REMOVE, "Peaks", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_pinch_magnify_items[] = {
      {BRUSH_DIR_IN, "MAGNIFY", ICON_ADD, "Magnify", "Subtract effect of brush"},
      {0, "PINCH", ICON_REMOVE, "Pinch", "Add effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_inflate_deflate_items[] = {
      {0, "INFLATE", ICON_ADD, "Inflate", "Add effect of brush"},
      {BRUSH_DIR_IN, "DEFLATE", ICON_REMOVE, "Deflate", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  /* texture paint mode */
  static const EnumPropertyItem prop_soften_sharpen_items[] = {
      {BRUSH_DIR_IN, "SHARPEN", ICON_ADD, "Sharpen", "Sharpen effect of brush"},
      {0, "SOFTEN", ICON_REMOVE, "Soften", "Blur effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  Brush *me = (Brush *)(ptr->data);

  switch (mode) {
    case PAINT_MODE_SCULPT:
      switch (me->sculpt_tool) {
        case SCULPT_TOOL_DRAW:
        case SCULPT_TOOL_DRAW_SHARP:
        case SCULPT_TOOL_CREASE:
        case SCULPT_TOOL_BLOB:
        case SCULPT_TOOL_LAYER:
        case SCULPT_TOOL_CLAY:
        case SCULPT_TOOL_CLAY_STRIPS:
          return prop_direction_items;
        case SCULPT_TOOL_SMOOTH:
          return prop_smooth_direction_items;
        case SCULPT_TOOL_MASK:
          switch ((BrushMaskTool)me->mask_tool) {
            case BRUSH_MASK_DRAW:
              return prop_direction_items;

            case BRUSH_MASK_SMOOTH:
              return DummyRNA_DEFAULT_items;

            default:
              return DummyRNA_DEFAULT_items;
          }

        case SCULPT_TOOL_FLATTEN:
          return prop_flatten_contrast_items;

        case SCULPT_TOOL_FILL:
          return prop_fill_deepen_items;

        case SCULPT_TOOL_SCRAPE:
          return prop_scrape_peaks_items;

        case SCULPT_TOOL_PINCH:
          return prop_pinch_magnify_items;

        case SCULPT_TOOL_INFLATE:
          return prop_inflate_deflate_items;

        default:
          return DummyRNA_DEFAULT_items;
      }

    case PAINT_MODE_TEXTURE_2D:
    case PAINT_MODE_TEXTURE_3D:
      switch (me->imagepaint_tool) {
        case PAINT_TOOL_SOFTEN:
          return prop_soften_sharpen_items;

        default:
          return DummyRNA_DEFAULT_items;
      }

    default:
      return DummyRNA_DEFAULT_items;
  }
}

static const EnumPropertyItem *rna_Brush_stroke_itemf(bContext *C,
                                                      PointerRNA *UNUSED(ptr),
                                                      PropertyRNA *UNUSED(prop),
                                                      bool *UNUSED(r_free))
{
  ePaintMode mode = BKE_paintmode_get_active_from_context(C);

  static const EnumPropertyItem brush_stroke_method_items[] = {
      {0, "DOTS", 0, "Dots", "Apply paint on each mouse move step"},
      {BRUSH_SPACE,
       "SPACE",
       0,
       "Space",
       "Limit brush application to the distance specified by spacing"},
      {BRUSH_AIRBRUSH,
       "AIRBRUSH",
       0,
       "Airbrush",
       "Keep applying paint effect while holding mouse (spray)"},
      {BRUSH_LINE, "LINE", 0, "Line", "Drag a line with dabs separated according to spacing"},
      {BRUSH_CURVE,
       "CURVE",
       0,
       "Curve",
       "Define the stroke curve with a bezier curve. Dabs are separated according to spacing"},
      {0, NULL, 0, NULL, NULL},
  };

  switch (mode) {
    case PAINT_MODE_SCULPT:
    case PAINT_MODE_TEXTURE_2D:
    case PAINT_MODE_TEXTURE_3D:
      return sculpt_stroke_method_items;

    default:
      return brush_stroke_method_items;
  }
}
