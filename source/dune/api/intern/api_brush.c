#include <stdlib.h>

#include "types_brush.h"
#include "types_pen.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_texture.h"
#include "types_workspace.h"

#include "lib_math.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "imbuf.h"

#include "wm_types.h"

static const EnumPropItem prop_direction_items[] = {
    {0, "ADD", ICON_ADD, "Add", "Add effect of brush"},
    {BRUSH_DIR_IN, "SUBTRACT", ICON_REMOVE, "Subtract", "Subtract effect of brush"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME
static const EnumPropItem prop_smooth_direction_items[] = {
    {0, "SMOOTH", ICON_ADD, "Smooth", "Smooth the surface"},
    {BRUSH_DIR_IN,
     "ENHANCE_DETAILS",
     ICON_REMOVE,
     "Enhance Details",
     "Enhance the surface detail"},
    {0, NULL, 0, NULL, NULL},
};
#endif

static const EnumPropItem sculpt_stroke_method_items[] = {
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

static const EnumPropItem api_enum_brush_texture_slot_map_all_mode_items[] = {
    {MTEX_MAP_MODE_VIEW, "VIEW_PLANE", 0, "View Plane", ""},
    {MTEX_MAP_MODE_AREA, "AREA_PLANE", 0, "Area Plane", ""},
    {MTEX_MAP_MODE_TILED, "TILED", 0, "Tiled", ""},
    {MTEX_MAP_MODE_3D, "3D", 0, "3D", ""},
    {MTEX_MAP_MODE_RANDOM, "RANDOM", 0, "Random", ""},
    {MTEX_MAP_MODE_STENCIL, "STENCIL", 0, "Stencil", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME
static const EnumPropItem api_enum_brush_texture_slot_map_texture_mode_items[] = {
    {MTEX_MAP_MODE_VIEW, "VIEW_PLANE", 0, "View Plane", ""},
    {MTEX_MAP_MODE_TILED, "TILED", 0, "Tiled", ""},
    {MTEX_MAP_MODE_3D, "3D", 0, "3D", ""},
    {MTEX_MAP_MODE_RANDOM, "RANDOM", 0, "Random", ""},
    {MTEX_MAP_MODE_STENCIL, "STENCIL", 0, "Stencil", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

/* clang-format off */
const EnumPropItem api_enum_brush_sculpt_tool_items[] = {
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

const EnumPropItem api_enum_brush_uv_sculpt_tool_items[] = {
    {UV_SCULPT_TOOL_GRAB, "GRAB", 0, "Grab", "Grab UVs"},
    {UV_SCULPT_TOOL_RELAX, "RELAX", 0, "Relax", "Relax UVs"},
    {UV_SCULPT_TOOL_PINCH, "PINCH", 0, "Pinch", "Pinch UVs"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_vertex_tool_items[] = {
    {VPAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {VPAINT_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {VPAINT_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {VPAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_weight_tool_items[] = {
    {WPAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {WPAINT_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {WPAINT_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {WPAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_image_tool_items[] = {
    {PAINT_TOOL_DRAW, "DRAW", ICON_BRUSH_TEXDRAW, "Draw", ""},
    {PAINT_TOOL_SOFTEN, "SOFTEN", ICON_BRUSH_SOFTEN, "Soften", ""},
    {PAINT_TOOL_SMEAR, "SMEAR", ICON_BRUSH_SMEAR, "Smear", ""},
    {PAINT_TOOL_CLONE, "CLONE", ICON_BRUSH_CLONE, "Clone", ""},
    {PAINT_TOOL_FILL, "FILL", ICON_BRUSH_TEXFILL, "Fill", ""},
    {PAINT_TOOL_MASK, "MASK", ICON_BRUSH_TEXMASK, "Mask", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_pen_types_items[] = {
    {PAINT_TOOL_DRAW,
     "DRAW",
     ICON_STROKE,
     "Draw",
     "The brush is of type used for drawing strokes"},
    {PAINT_TOOL_FILL, "FILL", ICON_COLOR, "Fill", "The brush is of type used for filling areas"},
    {PAINT_TOOL_ERASE,
     "ERASE",
     ICON_PANEL_CLOSE,
     "Erase",
     "The brush is used for erasing strokes"},
    {PAINT_TOOL_TINT,
     "TINT",
     ICON_BRUSH_TEXDRAW,
     "Tint",
     "The brush is of type used for tinting strokes"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_pen_vertex_types_items[] = {
    {PVERTEX_TOOL_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {PVERTEX_TOOL_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {PVERTEX_TOOL_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {PVERTEX_TOOL_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {PVERTEX_TOOL_REPLACE, "REPLACE", ICON_BRUSH_BLUR, "Replace", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_pen_sculpt_types_items[] = {
    {PSCULPT_TOOL_SMOOTH, "SMOOTH", ICON_PBRUSH_SMOOTH, "Smooth", "Smooth stroke points"},
    {PSCULPT_TOOL_THICKNESS,
     "THICKNESS",
     ICON_PBRUSH_THICKNESS,
     "Thickness",
     "Adjust thickness of strokes"},
    {PSCULPT_TOOL_STRENGTH,
     "STRENGTH",
     ICON_PBRUSH_STRENGTH,
     "Strength",
     "Adjust color strength of strokes"},
    {PSCULPT_TOOL_RANDOMIZE,
     "RANDOMIZE",
     ICON_PBRUSH_RANDOMIZE,
     "Randomize",
     "Introduce jitter/randomness into strokes"},
    {PSCULPT_TOOL_GRAB,
     "GRAB",
     ICON_PBRUSH_GRAB,
     "Grab",
     "Translate the set of points initially within the brush circle"},
    {PSCULPT_TOOL_PUSH,
     "PUSH",
     ICON_GPBRUSH_PUSH,
     "Push",
     "Move points out of the way, as if combing them"},
    {PSCULPT_TOOL_TWIST,
     "TWIST",
     ICON_GPBRUSH_TWIST,
     "Twist",
     "Rotate points around the midpoint of the brush"},
    {PSCULPT_TOOL_PINCH,
     "PINCH",
     ICON_PBRUSH_PINCH,
     "Pinch",
     "Pull points towards the midpoint of the brush"},
    {PSCULPT_TOOL_CLONE,
     "CLONE",
     ICON_PBRUSH_CLONE,
     "Clone",
     "Paste copies of the strokes stored on the clipboard"},
    {0, NULL, 0, NULL, NULL}};

const EnumPropItem api_enum_brush_pen_weight_types_items[] = {
    {PWEIGHT_TOOL_DRAW,
     "WEIGHT",
     ICON_PBRUSH_WEIGHT,
     "Weight",
     "Weight Paint for Vertex Groups"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_brush_curves_sculpt_tool_items[] = {
    {CURVES_SCULPT_TOOL_COMB, "COMB", ICON_NONE, "Comb", ""},
    {CURVES_SCULPT_TOOL_DELETE, "DELETE", ICON_NONE, "Delete", ""},
    {CURVES_SCULPT_TOOL_SNAKE_HOOK, "SNAKE_HOOK", ICON_NONE, "Snake Hook", ""},
    {CURVES_SCULPT_TOOL_ADD, "ADD", ICON_NONE, "Add", ""},
    {CURVES_SCULPT_TOOL_TEST1, "TEST1", ICON_NONE, "Test 1", ""},
    {CURVES_SCULPT_TOOL_TEST2, "TEST2", ICON_NONE, "Test 2", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
static EnumPropItem api_enum_pen_brush_eraser_modes_items[] = {
    {PEN_BRUSH_ERASER_SOFT,
     "SOFT",
     0,
     "Dissolve",
     "Erase strokes, fading their points strength and thickness"},
    {PEN_BRUSH_ERASER_HARD, "HARD", 0, "Point", "Erase stroke points"},
    {PEN_BRUSH_ERASER_STROKE, "STROKE", 0, "Stroke", "Erase entire strokes"},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropItem api_enum_pen_fill_draw_modes_items[] = {
    {PEN_FILL_DMODE_BOTH,
     "BOTH",
     0,
     "All",
     "Use both visible strokes and edit lines as fill boundary limits"},
    {PEN_FILL_DMODE_STROKE, "STROKE", 0, "Strokes", "Use visible strokes as fill boundary limits"},
    {PEN_FILL_DMODE_CONTROL, "CONTROL", 0, "Edit Lines", "Use edit lines as fill boundary limits"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropItem api_enum_pen_fill_layers_modes_items[] = {
    {PEN_FILL_GPLMODE_VISIBLE, "VISIBLE", 0, "Visible", "Visible layers"},
    {PEN_FILL_GPLMODE_ACTIVE, "ACTIVE", 0, "Active", "Only active layer"},
    {PEN_FILL_GPLMODE_ABOVE, "ABOVE", 0, "Layer Above", "Layer above active"},
    {PEN_FILL_GPLMODE_BELOW, "BELOW", 0, "Layer Below", "Layer below active"},
    {PEN_FILL_GPLMODE_ALL_ABOVE, "ALL_ABOVE", 0, "All Above", "All layers above active"},
    {PEN_FILL_GPLMODE_ALL_BELOW, "ALL_BELOW", 0, "All Below", "All layers below active"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropItem api_enum_pen_fill_direction_items[] = {
    {0, "NORMAL", ICON_ADD, "Normal", "Fill internal area"},
    {BRUSH_DIR_IN, "INVERT", ICON_REMOVE, "Inverted", "Fill inverted area"},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropItem api_enum_pen_brush_modes_items[] = {
    {PEN_BRUSH_MODE_ACTIVE, "ACTIVE", 0, "Active", "Use current mode"},
    {PEN_BRUSH_MODE_MATERIAL, "MATERIAL", 0, "Material", "Use always material mode"},
    {PEN_BRUSH_MODE_VERTEXCOLOR, "VERTEXCOLOR", 0, "Vertex Color", "Use always Vertex Color mode"},
    {0, NULL, 0, NULL, NULL}};

static EnumPropItem api_enum_pen_brush_paint_icons_items[] = {
    {PEN_BRUSH_ICON_PEN, "PEN", ICON_GPBRUSH_PEN, "Pen", ""},
    {PEN_BRUSH_ICON_PEN, "PEN", ICON_GPBRUSH_PEN, "Pen", ""},
    {PEN_BRUSH_ICON_INK, "INK", ICON_GPBRUSH_INK, "Ink", ""},
    {PEN_BRUSH_ICON_INKNOISE, "INKNOISE", ICON_GPBRUSH_INKNOISE, "Ink Noise", ""},
    {PEN_BRUSH_ICON_BLOCK, "BLOCK", ICON_GPBRUSH_BLOCK, "Block", ""},
    {PEN_BRUSH_ICON_MARKER, "MARKER", ICON_GPBRUSH_MARKER, "Marker", ""},
    {PEN_BRUSH_ICON_AIRBRUSH, "AIRBRUSH", ICON_GPBRUSH_AIRBRUSH, "Airbrush", ""},
    {PEN_BRUSH_ICON_CHISEL, "CHISEL", ICON_GPBRUSH_CHISEL, "Chisel", ""},
    {PEN_BRUSH_ICON_FILL, "FILL", ICON_GPBRUSH_FILL, "Fill", ""},
    {PEN_BRUSH_ICON_ERASE_SOFT, "SOFT", ICON_GPBRUSH_ERASE_SOFT, "Eraser Soft", ""},
    {PEN_BRUSH_ICON_ERASE_HARD, "HARD", ICON_GPBRUSH_ERASE_HARD, "Eraser Hard", ""},
    {PEN_BRUSH_ICON_ERASE_STROKE, "STROKE", ICON_GPBRUSH_ERASE_STROKE, "Eraser Stroke", ""},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropItem api_enum_pen_brush_sculpt_icons_items[] = {
    {PEM_BRUSH_ICON_GPBRUSH_SMOOTH, "SMOOTH", ICON_GPBRUSH_SMOOTH, "Smooth", ""},
    {PEN_BRUSH_ICON_GPBRUSH_THICKNESS, "THICKNESS", ICON_GPBRUSH_THICKNESS, "Thickness", ""},
    {PEM_BRUSH_ICON_GPBRUSH_STRENGTH, "STRENGTH", ICON_GPBRUSH_STRENGTH, "Strength", ""},
    {PEN_BRUSH_ICON_GPBRUSH_RANDOMIZE, "RANDOMIZE", ICON_GPBRUSH_RANDOMIZE, "Randomize", ""},
    {PEN_BRUSH_ICON_GPBRUSH_GRAB, "GRAB", ICON_GPBRUSH_GRAB, "Grab", ""},
    {PEN_BRUSH_ICON_GPBRUSH_PUSH, "PUSH", ICON_GPBRUSH_PUSH, "Push", ""},
    {PEN_BRUSH_ICON_GPBRUSH_TWIST, "TWIST", ICON_GPBRUSH_TWIST, "Twist", ""},
    {PEN_BRUSH_ICON_GPBRUSH_PINCH, "PINCH", ICON_GPBRUSH_PINCH, "Pinch", ""},
    {PEN_BRUSH_ICON_GPBRUSH_CLONE, "CLONE", ICON_GPBRUSH_CLONE, "Clone", ""},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropItem api_enum_pen_brush_weight_icons_items[] = {
    {PEN_BRUSH_ICON_GPBRUSH_WEIGHT, "DRAW", ICON_PBRUSH_WEIGHT, "Draw", ""},
    {0, NULL, 0, NULL, NULL},
};
static EnumPropItem api_enum_pen_brush_vertex_icons_items[] = {
    {PEN_BRUSH_ICON_VERTEX_DRAW, "DRAW", ICON_BRUSH_MIX, "Draw", ""},
    {PEN_BRUSH_ICON_VERTEX_BLUR, "BLUR", ICON_BRUSH_BLUR, "Blur", ""},
    {PEN_BRUSH_ICON_VERTEX_AVERAGE, "AVERAGE", ICON_BRUSH_BLUR, "Average", ""},
    {PEN_BRUSH_ICON_VERTEX_SMEAR, "SMEAR", ICON_BRUSH_BLUR, "Smear", ""},
    {PEN_BRUSH_ICON_VERTEX_REPLACE, "REPLACE", ICON_BRUSH_MIX, "Replace", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "api_access.h"

#  include "dune_brush.h"
#  include "dune_colorband.h"
#  include "dune_pen.h"
#  include "dune_icons.h"
#  include "dune_material.h"
#  include "dune_paint.h"

#  include "wm_api.h"

static bool api_BrushCapabilitiesSculpt_has_accumulate_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_ACCUMULATE(br->sculpt_tool);
}

static bool api_BrushCapabilitiesSculpt_has_topology_rake_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_TOPOLOGY_RAKE(br->sculpt_tool);
}

static bool api_BrushCapabilitiesSculpt_has_auto_smooth_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(
      br->sculpt_tool, SCULPT_TOOL_MASK, SCULPT_TOOL_SMOOTH, SCULPT_TOOL_PAINT, SCULPT_TOOL_SMEAR);
}

static bool api_BrushCapabilitiesSculpt_has_height_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return br->sculpt_tool == SCULPT_TOOL_LAYER;
}

static bool api_BrushCapabilitiesSculpt_has_jitter_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED) && !(br->flag & BRUSH_DRAG_DOT) &&
          !ELEM(br->sculpt_tool,
                SCULPT_TOOL_GRAB,
                SCULPT_TOOL_ROTATE,
                SCULPT_TOOL_SNAKE_HOOK,
                SCULPT_TOOL_THUMB));
}

static bool api_BrushCapabilitiesSculpt_has_normal_weight_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_NORMAL_WEIGHT(br->sculpt_tool);
}

static bool api_BrushCapabilitiesSculpt_has_rake_factor_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return SCULPT_TOOL_HAS_RAKE(br->sculpt_tool);
}

static bool api_BrushCapabilities_has_overlay_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(
      br->mtex.brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_TILED, MTEX_MAP_MODE_STENCIL);
}

static bool api_BrushCapabilitiesSculpt_has_persistence_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_LAYER, SCULPT_TOOL_CLOTH);
}

static bool api_BrushCapabilitiesSculpt_has_pinch_factor_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_BLOB, SCULPT_TOOL_CREASE, SCULPT_TOOL_SNAKE_HOOK);
}

static bool api_BrushCapabilitiesSculpt_has_plane_offset_get(ApiPtr *ptr)
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

static bool api_BrushCapabilitiesSculpt_has_random_texture_angle_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!ELEM(br->sculpt_tool,
                SCULPT_TOOL_GRAB,
                SCULPT_TOOL_ROTATE,
                SCULPT_TOOL_SNAKE_HOOK,
                SCULPT_TOOL_THUMB));
}

static bool api_TextureCapabilities_has_random_texture_angle_get(ApiPtr *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return ELEM(mtex->brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_AREA, MTEX_MAP_MODE_RANDOM);
}

static bool api_BrushCapabilities_has_random_texture_angle_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !(br->flag & BRUSH_ANCHORED);
}

static bool api_BrushCapabilitiesSculpt_has_sculpt_plane_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool,
               SCULPT_TOOL_INFLATE,
               SCULPT_TOOL_MASK,
               SCULPT_TOOL_PINCH,
               SCULPT_TOOL_SMOOTH);
}

static bool api_BrushCapabilitiesSculpt_has_color_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->sculpt_tool, SCULPT_TOOL_PAINT);
}

static bool api_BrushCapabilitiesSculpt_has_secondary_color_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return dune_brush_sculpt_has_secondary_color(br);
}

static bool api_BrushCapabilitiesSculpt_has_smooth_stroke_get(ApiPtr *ptr)
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

static bool api_BrushCapabilities_has_smooth_stroke_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED) && !(br->flag & BRUSH_DRAG_DOT) &&
          !(br->flag & BRUSH_LINE) && !(br->flag & BRUSH_CURVE));
}

static bool api_BrushCapabilitiesSculpt_has_space_attenuation_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ((br->flag & (BRUSH_SPACE | BRUSH_LINE | BRUSH_CURVE)) && !ELEM(br->sculpt_tool,
                                                                         SCULPT_TOOL_GRAB,
                                                                         SCULPT_TOOL_ROTATE,
                                                                         SCULPT_TOOL_SMOOTH,
                                                                         SCULPT_TOOL_SNAKE_HOOK));
}

static bool api_BrushCapabilitiesImagePaint_has_space_attenuation_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (br->flag & (BRUSH_SPACE | BRUSH_LINE | BRUSH_CURVE)) &&
         br->imagepaint_tool != PAINT_TOOL_FILL;
}

static bool api_BrushCapabilitiesImagePaint_has_color_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->imagepaint_tool, PAINT_TOOL_DRAW, PAINT_TOOL_FILL);
}

static bool api_BrushCapabilitiesVertexPaint_has_color_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->vertexpaint_tool, VPAINT_TOOL_DRAW);
}

static bool api_BrushCapabilitiesWeightPaint_has_weight_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return ELEM(br->weightpaint_tool, WPAINT_TOOL_DRAW);
}

static bool api_BrushCapabilities_has_spacing_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return (!(br->flag & BRUSH_ANCHORED));
}

static bool api_BrushCapabilitiesSculpt_has_strength_pressure_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool, SCULPT_TOOL_GRAB, SCULPT_TOOL_SNAKE_HOOK);
}

static bool api_TextureCapabilities_has_texture_angle_get(ApiPtr *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return mtex->brush_map_mode != MTEX_MAP_MODE_3D;
}

static bool api_BrushCapabilitiesSculpt_has_direction_get(ApiPtr *ptr)
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

static bool api_BrushCapabilitiesSculpt_has_gravity_get(ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  return !ELEM(br->sculpt_tool, SCULPT_TOOL_MASK, SCULPT_TOOL_SMOOTH);
}

static bool api_BrushCapabilitiesSculpt_has_tilt_get(ApiPtr *ptr)
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

static bool api_TextureCapabilities_has_texture_angle_source_get(ApiPtr *ptr)
{
  MTex *mtex = (MTex *)ptr->data;
  return ELEM(mtex->brush_map_mode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_AREA, MTEX_MAP_MODE_RANDOM);
}

static bool api_BrushCapabilitiesImagePaint_has_accumulate_get(PointerRNA *ptr)
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

static bool api_BrushCapabilitiesImagePaint_has_radius_get(ApiPtr *ptr)
{
  /* only support for draw tool */
  Brush *br = (Brush *)ptr->data;

  return (br->imagepaint_tool != PAINT_TOOL_FILL);
}

static ApiPtr api_Sculpt_tool_capabilities_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiBrushCapabilitiesSculpt, ptr->owner_id);
}

static ApiPtr api_Imapaint_tool_capabilities_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiBrushCapabilitiesImagePaint, ptr->owner_id);
}

static ApiPtr api_Vertexpaint_tool_capabilities_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiBrushCapabilitiesVertexPaint, ptr->owner_id);
}

static ApiPtr api_Weightpaint_tool_capabilities_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiBrushCapabilitiesWeightPaint, ptr->owner_id);
}

static ApiPtr api_Brush_capabilities_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiBrushCapabilities, ptr->owner_id);
}

static void api_Brush_reset_icon(Brush *br)
{
  Id *id = &br->id;

  if (br->flag & BRUSH_CUSTOM_ICON) {
    return;
  }

  if (id->icon_id >= BIFICONID_LAST) {
    dune_icon_id_delete(id);
    dune_previewimg_id_free(id);
  }

  id->icon_id = 0;
}

static void api_Brush_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Brush *br = (Brush *)ptr->data;
  wm_main_add_notifier(NC_BRUSH | NA_EDITED, br);
  /*em_main_add_notifier(NC_SPACE|ND_SPACE_VIEW3D, NULL); */
}

static void api_Brush_material_update(Cxt *UNUSED(C), ApiPtr *UNUSED(ptr))
{
  /* number of material users changed */
  wm_main_add_notifier(NC_SPACE | ND_SPACE_PROPS, NULL);
}

static void api_Brush_main_tex_update(Cxt *C, ApiPtr *ptr)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Brush *br = (Brush *)ptr->data;
  dune_paint_invalidate_overlay_tex(scene, view_layer, br->mtex.tex);
  api_Brush_update(main, scene, ptr);
}

static void api_Brush_secondary_tex_update(Cxt *C, ApiPtr *ptr)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Brush *br = (Brush *)ptr->data;
  dune_paint_invalidate_overlay_tex(scene, view_layer, br->mask_mtex.tex);
  api_Brush_update(main, scene, ptr);
}

static void api_Brush_size_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  dune_paint_invalidate_overlay_all();
  api_Brush_update(main, scene, ptr);
}

static void api_Brush_update_and_reset_icon(Main *main, Scene *scene, ApiPtr *ptr)
{
  Brush *br = ptr->data;
  api_Brush_reset_icon(br);
  api_Brush_update(main, scene, ptr);
}

static void api_Brush_stroke_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  wm_main_add_notifier(NC_SCENE | ND_TOOLSETTINGS, scene);
  api_Brush_update(main, scene, ptr);
}

static void api_Brush_icon_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Brush *br = (Brush *)ptr->data;

  if (br->icon_imbuf) {
    imbuf_freeimbuf(br->icon_imbuf);
    br->icon_imbuf = NULL;
  }

  br->id.icon_id = 0;

  if (br->flag & BRUSH_CUSTOM_ICON) {
    dune_icon_changed(dune_icon_id_ensure(&br->id));
  }

  wm_main_add_notifier(NC_BRUSH | NA_EDITED, br);
}

static bool api_Brush_imagetype_poll(ApiPtr *UNUSED(ptr), ApiPtr value)
{
  Image *image = (Image *)value.owner_id;
  return image->type != IMA_TYPE_R_RESULT && image->type != IMA_TYPE_COMPOSITE;
}

static void api_TextureSlot_brush_angle_update(Cxt *C, ApiPtr *ptr)
{
  Scene *scene = cxt_data_scene(C);
  MTex *mtex = ptr->data;
  /* skip invalidation of overlay for stencil mode */
  if (mtex->mapping != MTEX_MAP_MODE_STENCIL) {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    dune_paint_invalidate_overlay_tex(scene, view_layer, mtex->tex);
  }

  api_TextureSlot_update(C, ptr);
}

static void api_Brush_set_size(ApiPtr *ptr, int value)
{
  Brush *brush = ptr->data;

  /* scale unprojected radius so it stays consistent with brush size */
  dune_brush_scale_unprojected_radius(&brush->unprojected_radius, value, brush->size);
  brush->size = value;
}

static void api_Brush_use_gradient_set(ApiPtr *ptr, bool value)
{
  Brush *br = (Brush *)ptr->data;

  if (value) {
    br->flag |= BRUSH_USE_GRADIENT;
  }
  else {
    br->flag &= ~BRUSH_USE_GRADIENT;
  }

  if ((br->flag & BRUSH_USE_GRADIENT) && br->gradient == NULL) {
    br->gradient = dune_colorband_add(true);
  }
}

static void api_Brush_set_unprojected_radius(ApiPtr *ptr, float value)
{
  Brush *brush = ptr->data;

  /* scale brush size so it stays consistent with unprojected_radius */
  dune_brush_scale_size(&brush->size, value, brush->unprojected_radius);
  brush->unprojected_radius = value;
}

static const EnumPropItem *api_Brush_direction_itemf(Cxt *C,
                                                     ApiPtr *ptr,
                                                     ApiProp *UNUSED(prop),
                                                     bool *UNUSED(r_free))
{
  ePaintMode mode = dune_paintmode_get_active_from_cxt(C);

  /* sculpt mode */
  static const EnumPropItem prop_flatten_contrast_items[] = {
      {BRUSH_DIR_IN, "CONTRAST", ICON_ADD, "Contrast", "Subtract effect of brush"},
      {0, "FLATTEN", ICON_REMOVE, "Flatten", "Add effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_fill_deepen_items[] = {
      {0, "FILL", ICON_ADD, "Fill", "Add effect of brush"},
      {BRUSH_DIR_IN, "DEEPEN", ICON_REMOVE, "Deepen", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_scrape_peaks_items[] = {
      {0, "SCRAPE", ICON_ADD, "Scrape", "Add effect of brush"},
      {BRUSH_DIR_IN, "PEAKS", ICON_REMOVE, "Peaks", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_pinch_magnify_items[] = {
      {BRUSH_DIR_IN, "MAGNIFY", ICON_ADD, "Magnify", "Subtract effect of brush"},
      {0, "PINCH", ICON_REMOVE, "Pinch", "Add effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_inflate_deflate_items[] = {
      {0, "INFLATE", ICON_ADD, "Inflate", "Add effect of brush"},
      {BRUSH_DIR_IN, "DEFLATE", ICON_REMOVE, "Deflate", "Subtract effect of brush"},
      {0, NULL, 0, NULL, NULL},
  };

  /* texture paint mode */
  static const EnumPropItem prop_soften_sharpen_items[] = {
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
              return DummyApi_DEFAULT_items;

            default:
              return DummyApi_DEFAULT_items;
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
          return DummyApi_DEFAULT_items;
      }

    case PAINT_MODE_TEXTURE_2D:
    case PAINT_MODE_TEXTURE_3D:
      switch (me->imagepaint_tool) {
        case PAINT_TOOL_SOFTEN:
          return prop_soften_sharpen_items;

        default:
          return DummyApi_DEFAULT_items;
      }

    default:
      return DummyApi_DEFAULT_items;
  }
}

static const EnumPropItem *api_Brush_stroke_itemf(Cxt *C,
                                                  ApiPtr *UNUSED(ptr),
                                                  ApiProp *UNUSED(prop),
                                                  bool *UNUSED(r_free))
{
  ePaintMode mode = dune_paintmode_get_active_from_cxt(C);

  static const EnumPropItem brush_stroke_method_items[] = {
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
/* Pen Drawing Brushes Settings */
static char *api_BrushpenSettings_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings.pen_paint.brush.pen_settings");
}

static void api_BrushpenSettings_default_eraser_update(Main *main,
                                                       Scene *scene,
                                                       ApiPtr *UNUSED(ptr))
{
  ToolSettings *ts = scene->toolsettings;
  Paint *paint = &ts->pen_paint->paint;
  Brush *brush_cur = paint->brush;

  /* disable default eraser in all brushes */
  for (Brush *brush = main->brushes.first; brush; brush = brush->id.next) {
    if ((brush != brush_cur) && (brush->ob_mode == OB_MODE_PAINT_PEN) &&
        (brush->pen_tool == PAINT_TOOL_ERASE)) {
      brush->pen_settings->flag &= ~PEN_BRUSH_DEFAULT_ERASER;
    }
  }
}

static void api_BrushpenSettings_use_material_pin_update(Cxt *C, ApiPtr *ptr)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Object *ob = OBACT(view_layer);
  Brush *brush = (Brush *)ptr->owner_id;

  if (brush->pen_settings->flag & PEN_BRUSH_MATERIAL_PINNED) {
    Material *material = dune_object_material_get(ob, ob->actcol);
    dune_pen_brush_material_set(brush, material);
  }
  else {
    dune_pen_brush_material_set(brush, NULL);
  }

  /* number of material users changed */
  wm_event_add_notifier(C, NC_SPACE | ND_SPACE_PROPS, NULL);
}

static void api_BrushpenSettings_eraser_mode_update(Main *UNUSED(main),
                                                    Scene *scene,
                                                    ApiPtr *UNUSED(ptr))
{
  ToolSettings *ts = scene->toolsettings;
  Paint *paint = &ts->pen_paint->paint;
  Brush *brush = paint->brush;

  /* set eraser icon */
  if ((brush) && (brush->pen_tool == PAINT_TOOL_ERASE)) {
    switch (brush->pen_settings->eraser_mode) {
      case PEN_BRUSH_ERASER_SOFT:
        brush->pen_settings->icon_id = PEN_BRUSH_ICON_ERASE_SOFT;
        break;
      case PEN_BRUSH_ERASER_HARD:
        brush->pen_settings->icon_id = GP_BRUSH_ICON_ERASE_HARD;
        break;
      case PEN_BRUSH_ERASER_STROKE:
        brush->pen_settings->icon_id = GP_BRUSH_ICON_ERASE_STROKE;
        break;
      default:
        brush->pen_settings->icon_id = GP_BRUSH_ICON_ERASE_SOFT;
        break;
    }
  }
}

static bool api_BrushPenSettings_material_poll(ApiPtr *UNUSED(ptr), ApiPtr value)
{
  Material *ma = (Material *)value.data;

  /* GP materials only */
  return (ma->pen_style != NULL);
}

static bool api_PenBrush_pin_mode_get(ApiPtr *ptr)
{
  Brush *brush = (Brush *)ptr->owner_id;
  if ((brush != NULL) && (brush->pen_settings != NULL)) {
    return (brush->pen_settings->brush_draw_mode != GP_BRUSH_MODE_ACTIVE);
  }
  return false;
}

static void api_PenBrush_pin_mode_set(ApiPtr *UNUSED(ptr), bool UNUSED(value))
{
  /* All data is set in update. Keep this function only to avoid RNA compilation errors. */
  return;
}

static void api_PenBrush_pin_mode_update(Cxt *C, ApiPtr *ptr)
{
  Brush *brush = (Brush *)ptr->owner_id;
  if ((brush != NULL) && (brush->pen_settings != NULL)) {
    if (brush->pen_settings->brush_draw_mode != PEN_BRUSH_MODE_ACTIVE) {
      /* If not active, means that must be set to off. */
      brush->pen_settings->brush_draw_mode = PEN_BRUSH_MODE_ACTIVE;
    }
    else {
      ToolSettings *ts = cxt_data_tool_settings(C);
      brush->pen_settings->brush_draw_mode = PEN_USE_VERTEX_COLOR(ts) ?
                                                     PEN_BRUSH_MODE_VERTEXCOLOR :
                                                     PEN_BRUSH_MODE_MATERIAL;
    }
  }
}

static const EnumPropItem *api_BrushTextureSlot_map_mode_itemf(Cxt *C,
                                                               ApiPtr *UNUSED(ptr),
                                                               ApiProp *UNUSED(prop),
                                                               abool *UNUSED(r_free))
{

  if (C == NULL) {
    return api_enum_brush_texture_slot_map_all_mode_items;
  }

#  define api_enum_brush_texture_slot_map_sculpt_mode_items \
    api_enum_brush_texture_slot_map_all_mode_items;

  const ePaintMode mode = dune_paintmode_get_active_from_context(C);
  if (mode == PAINT_MODE_SCULPT) {
    return api_enum_brush_texture_slot_map_sculpt_mode_items;
  }
  return api_enum_brush_texture_slot_map_texture_mode_items;

#  undef api_enum_brush_texture_slot_map_sculpt_mode_items
}

#else

static void api_def_brush_texture_slot(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_mask_paint_map_mode_items[] = {
      {MTEX_MAP_MODE_VIEW, "VIEW_PLANE", 0, "View Plane", ""},
      {MTEX_MAP_MODE_TILED, "TILED", 0, "Tiled", ""},
      {MTEX_MAP_MODE_RANDOM, "RANDOM", 0, "Random", ""},
      {MTEX_MAP_MODE_STENCIL, "STENCIL", 0, "Stencil", ""},
      {0, NULL, 0, NULL, NULL},
  };

#  define TEXTURE_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns(prop, "api_TextureCapabilities_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  sapi = api_def_struct(dapi, "BrushTextureSlot", "TextureSlot");
  api_def_struct_stype(sapi, "MTex");
  api_def_struct_ui_text(
      sapi, "Brush Texture Slot", "Texture slot for textures in a Brush data-block");

  prop = api_def_prop(sapi, "angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "rot");
  api_def_prop_range(prop, 0, M_PI * 2);
  api_def_prop_ui_text(prop, "Angle", "Brush texture rotation");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_TextureSlot_brush_angle_update");

  prop = api_def_prop(sapi, "map_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "brush_map_mode");
  api_def_prop_enum_items(prop, api_enum_brush_texture_slot_map_all_mode_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_BrushTextureSlot_map_mode_itemf");
  api_def_prope_ui_text(prop, "Mode", "");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_TextureSlot_update");

  prop = api_def_prop(sapi, "mask_map_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "brush_map_mode");
  api_def_prop_enum_items(prop, prop_mask_paint_map_mode_items);
  api_def_prop_ui_text(prop, "Mode", "");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_TextureSlot_update");

  prop = api_def_prop(sapi, "use_rake", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "brush_angle_mode", MTEX_ANGLE_RAKE);
  api_def_prop_ui_text(prop, "Rake", "");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  prop = api_def_prop(sapi, "use_random", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "brush_angle_mode", MTEX_ANGLE_RANDOM);
  api_def_prop_ui_text(prop, "Random", "");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_TextureSlot_update");

  prop = api_def_prop(sapi, "random_angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, 0, M_PI * 2);
  api_def_prop_ui_text(prop, "Random Angle", "Brush texture random angle");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_TextureSlot_update");

  TEXTURE_CAPABILITY(has_texture_angle_source, "Has Texture Angle Source");
  TEXTURE_CAPABILITY(has_random_texture_angle, "Has Random Texture Angle");
  TEXTURE_CAPABILITY(has_texture_angle, "Has Texture Angle Source");
}

static void api_def_sculpt_capabilities(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCapabilitiesSculpt", NULL);
  api_def_struct_stype(sapi, "Brush");
  api_def_struct_nested(dapi, sapi, "Brush");
  api_def_struct_ui_text(sapi,
                         "Sculpt Capabilities",
                         "Read-only indications of which brush operations "
                         "are supported by the current sculpt tool");

#  define SCULPT_TOOL_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns( \
        prop, "api_BrushCapabilitiesSculpt_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  SCULPT_TOOL_CAPABILITY(has_accumulate, "Has Accumulate");
  SCULPT_TOOL_CAPABILITY(has_auto_smooth, "Has Auto Smooth");
  SCULPT_TOOL_CAPABILITY(has_topology_rake, "Has Topology Rake");
  SCULPT_TOOL_CAPABILITY(has_height, "Has Height");
  SCULPT_TOOL_CAPABILITY(has_jitter, "Has Jitter");
  SCULPT_TOOL_CAPABILITY(has_normal_weight, "Has Crease/Pinch Factor");
  SCULPT_TOOL_CAPABILITY(has_rake_factor, "Has Rake Factor");
  SCULPT_TOOL_CAPABILITY(has_persistence, "Has Persistence");
  SCULPT_TOOL_CAPABILITY(has_pinch_factor, "Has Pinch Factor");
  SCULPT_TOOL_CAPABILITY(has_plane_offset, "Has Plane Offset");
  SCULPT_TOOL_CAPABILITY(has_random_texture_angle, "Has Random Texture Angle");
  SCULPT_TOOL_CAPABILITY(has_sculpt_plane, "Has Sculpt Plane");
  SCULPT_TOOL_CAPABILITY(has_color, "Has Color");
  SCULPT_TOOL_CAPABILITY(has_secondary_color, "Has Secondary Color");
  SCULPT_TOOL_CAPABILITY(has_smooth_stroke, "Has Smooth Stroke");
  SCULPT_TOOL_CAPABILITY(has_space_attenuation, "Has Space Attenuation");
  SCULPT_TOOL_CAPABILITY(has_strength_pressure, "Has Strength Pressure");
  SCULPT_TOOL_CAPABILITY(has_direction, "Has Direction");
  SCULPT_TOOL_CAPABILITY(has_gravity, "Has Gravity");
  SCULPT_TOOL_CAPABILITY(has_tilt, "Has Tilt");

#  undef SCULPT_CAPABILITY
}

static void api_def_brush_capabilities(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCapabilities", NULL);
  api_def_struct_stype(sapi, "Brush");
  api_def_struct_nested(dapi, sapi, "Brush");
  api_def_struct_ui_text(
      sapi, "Brush Capabilities", "Read-only indications of supported operations");

#  define BRUSH_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns(prop, "api_BrushCapabilities_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  BRUSH_CAPABILITY(has_overlay, "Has Overlay");
  BRUSH_CAPABILITY(has_random_texture_angle, "Has Random Texture Angle");
  BRUSH_CAPABILITY(has_spacing, "Has Spacing");
  BRUSH_CAPABILITY(has_smooth_stroke, "Has Smooth Stroke");

#  undef BRUSH_CAPABILITY
}

static void api_def_image_paint_capabilities(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCapabilitiesImagePaint", NULL);
  api_def_struct_stype(sapi, "Brush");
  api_def_struct_nested(dapi, sapi, "Brush");
  api_def_struct_ui_text(
      sapi, "Image Paint Capabilities", "Read-only indications of supported operations");

#  define IMAPAINT_TOOL_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns( \
        prop, "api_BrushCapabilitiesImagePaint_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  IMAPAINT_TOOL_CAPABILITY(has_accumulate, "Has Accumulate");
  IMAPAINT_TOOL_CAPABILITY(has_space_attenuation, "Has Space Attenuation");
  IMAPAINT_TOOL_CAPABILITY(has_radius, "Has Radius");
  IMAPAINT_TOOL_CAPABILITY(has_color, "Has Color");

#  undef IMAPAINT_TOOL_CAPABILITY
}

static void api_def_vertex_paint_capabilities(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCapabilitiesVertexPaint", NULL);
  api_def_struct_stype(sapi, "Brush");
  api_def_struct_nested(dapi, sapi, "Brush");
  apo_def_struct_ui_text(
      srna, "Vertex Paint Capabilities", "Read-only indications of supported operations");

#  define VPAINT_TOOL_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns( \
        prop, "api_BrushCapabilitiesVertexPaint_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  VPAINT_TOOL_CAPABILITY(has_color, "Has Color");

#  undef VPAINT_TOOL_CAPABILITY
}

static void api_def_weight_paint_capabilities(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCapabilitiesWeightPaint", NULL);
  api_def_struct_stype(sapi, "Brush");
  api_def_struct_nested(dapi, sapi, "Brush");
  api_def_struct_ui_text(
      sapi, "Weight Paint Capabilities", "Read-only indications of supported operations");

#  define WPAINT_TOOL_CAPABILITY(prop_name_, ui_name_) \
    prop = api_def_prop(sapi, #prop_name_, PROP_BOOL, PROP_NONE); \
    api_def_prop_clear_flag(prop, PROP_EDITABLE); \
    api_def_prop_bool_fns( \
        prop, "api_BrushCapabilitiesWeightPaint_" #prop_name_ "_get", NULL); \
    api_def_prop_ui_text(prop, ui_name_, NULL)

  WPAINT_TOOL_CAPABILITY(has_weight, "Has Weight");

#  undef WPAINT_TOOL_CAPABILITY
}

static void api_def_pen_options(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* modes */
  static EnumPropItem paint_mode_types_items[] = {
      {PAINT_MODE_STROKE, "STROKE", 0, "Stroke", "Vertex Color affects to Stroke only"},
      {PAINT_MODE_FILL, "FILL", 0, "Fill", "Vertex Color affects to Fill only"},
      {PAINT_MODE_BOTH, "BOTH", 0, "Stroke & Fill", "Vertex Color affects to Stroke and Fill"},
      {0, NULL, 0, NULL, NULL},
  };

  static EnumPropItem api_enum_pen_brush_caps_types_items[] = {
      {PEN_STROKE_CAP_ROUND, "ROUND", ICON_GP_CAPS_ROUND, "Round", ""},
      {PEN_STROKE_CAP_FLAT, "FLAT", ICON_GP_CAPS_FLAT, "Flat", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "BrushpenSettings", NULL);
  api_def_struct_stype(sapi, "BrushpenSettings");
  api_def_struct_path_fn(sapi, "api_BrushpenSettings_path");
  api_def_struct_ui_text(sapi, "Pen Brush Settings", "Settings for grease pencil brush");

  /* Strength factor for new strokes */
  prop = api_def_prop(sapi, "pen_strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "draw_strength");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Strength", "Color strength for new strokes (affect alpha factor of color)");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Jitter factor for new strokes */
  prop = api_def_prop(sapi, "pen_jitter", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "draw_jitter");
  api_def_prop_range(prop, 0.0f, 1.0f);
  apo_def_prop_ui_text(prop, "Jitter", "Jitter factor for new strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* Randomness factor for pressure */
  prop = api_def_prop(sapi, "random_pressure", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "draw_random_press");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Pressure Randomness", "Randomness factor for pressure in new strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Randomness factor for strength */
  prop = api_def_prop(sapi, "random_strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "draw_random_strength");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Strength Randomness", "Randomness factor strength in new strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* Angle when brush is full size */
  prop = api_def_prop(sapi, "angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "draw_angle");
  api_def_prop_range(prop, -M_PI_2, M_PI_2);
  api_def_prop_ui_text(prop,
                           "Angle",
                           "Direction of the stroke at which brush gives maximal thickness "
                           "(0° for horizontal)");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Factor to change brush size depending of angle */
  prop = api_def_prop(sapi, "angle_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "draw_angle_factor");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Angle Factor",
      "Reduce brush thickness by this factor when stroke is perpendicular to 'Angle' direction");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Smoothing factor for new strokes */
  prop = api_def_prop(sapi, "pen_smooth_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "draw_smoothfac");
  api_def_prop_range(prop, 0.0, 2.0f);
  api_def_prop_ui_text(
      prop,
      "Smooth",
      "Amount of smoothing to apply after finish newly created strokes, to reduce jitter/noise");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Iterations of the Smoothing factor */
  prop = api_def_prop(sapi, "pen_smooth_steps", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "draw_smoothlvl");
  api_def_prop_range(prop, 1, 3);
  api_def_prop_ui_text(prop, "Iterations", "Number of times to smooth newly created strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* Subdivision level for new strokes */
  prop = api_def_prop(sapi, "pen_subdivision_steps", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "draw_subdivide");
  api_def_prop_range(prop, 0, 3);
  api_def_prop_ui_text(
      prop,
      "Subdivision Steps",
      "Number of times to subdivide newly created strokes, for less jagged strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Simplify factor */
  prop = api_def_prop(sapi, "simplify_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "simplify_f");
  api_def_prop_range(prop, 0, 100.0);
  api_def_prop_ui_range(prop, 0, 100.0, 1.0f, 3);
  api_def_prop_ui_text(prop, "Simplify", "Factor of Simplify using adaptive algorithm");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* Curves for pressure */
  prop = api_def_prop(sapi, "curve_sensitivity", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_sensitivity");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Curve Sensitivity", "Curve used for the sensitivity");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_strength", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_strength");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Curve Strength", "Curve used for the strength");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_jitter", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_jitter");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Curve Jitter", "Curve used for the jitter effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_pressure", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_pressure");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_strength", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_strength");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_uv", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_uv");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_hue", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_hue");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_saturation", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_saturation");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  prop = api_def_prop(sapi, "curve_random_value", PROP_POINTER, PROP_NONE
  api_def_prop_ptr_stype(prop, NULL, "curve_rand_value");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Random Curve", "Curve used for modulating effect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Fill threshold for transparency. */
  prop = api_def_prop(sapi, "fill_threshold", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "fill_threshold");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Threshold", "Threshold to consider color transparent for filling");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* fill leak size */
  prop = api_def_prop(sapi, "fill_leak", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "fill_leak");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Leak Size", "Size in pixels to consider the leak closed");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* fill factor size */
  prop = api_def_prop(sapi, "fill_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "fill_factor");
  api_def_prop_range(prop, PEN_MIN_FILL_FAC, PEN_MAX_FILL_FAC);
  api_def_prop_ui_text(
      prop,
      "Precision",
      "Factor for fill boundary accuracy, higher values are more accurate but slower");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* fill simplify steps */
  prop = api_def_prop(sapi, "fill_simplify_level", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "fill_simplylvl");
  api_def_prop_range(prop, 0, 10);
  api_def_prop_ui_text(
      prop, "Simplify", "Number of simplify steps (large values reduce fill accuracy)");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  prop = api_def_prop(sapi, "uv_random", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "uv_random");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "UV Random", "Random factor for auto-generated UV rotation");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* gradient control */
  prop = api_def_prop(sapi, "hardness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "hardeness");
  api_def_prop_range(prop, 0.001f, 1.0f);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Hardness",
      "Gradient from the center of Dot and Box strokes (set to 1 for a solid stroke)");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* gradient shape ratio */
  prop = api_def_prop(sapi, "aspect", PROP_FLOAT, PROP_XYZ);m
  api_def_prop_float_stype(prop, NULL, "aspect_ratio");
  api_def_prop_array(prop, 2);
  api_def_prop_range(prop, 0.01f, 1.0f);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Aspect", "");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  prop = api_def_prop(sapi, "input_samples", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "input_samples");
  api_def_prop_range(prop, 0, GP_MAX_INPUT_SAMPLES);
  api_def_prop_ui_text(
      prop,
      "Input Samples",
      "Generate intermediate points for very fast mouse movements. Set to 0 to disable");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* active smooth factor while drawing */
  prop = api_def_prop(sapi, "active_smooth_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_sdna(prop, NULL, "active_smooth");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Active Smooth", "Amount of smoothing while drawing");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "eraser_strength_factor", PROP_FLOAT, PROP_PERCENTAGE);
  api_def_prop_float_stype(prop, NULL, "era_strength_f");
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0, 100.0, 10, 1);
  api_def_prop_ui_text(prop, "Affect Stroke Strength", "Amount of erasing for strength");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  prop = api_def_prop(sapi, "eraser_thickness_factor", PROP_FLOAT, PROP_PERCENTAGE);
  api_def_prop_float_stype(prop, NULL, "era_thickness_f");
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0, 100.0, 10, 1);
  api_def_prop
      (prop, "Affect Stroke Thickness", "Amount of erasing for thickness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_GPENCIL | ND_DATA, NULL);

  /* brush standard icon */
  prop = api_def_prop(sapi, "gpencil_paint_icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "icon_id");
  api_def_prop_enum_items(prop, api_enum_pen_brush_paint_icons_items);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Grease Pencil Icon", "");

  prop = api_def_prop(sapi, "pen_sculpt_icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "icon_id");
  api_def_prop_enum_items(prop, api_enum_pen_brush_sculpt_icons_items);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Grease Pencil Icon", "");

  prop = api_def_prop(sapi, "pen_weight_icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "icon_id");
  api_def_prop_enum_items(prop, api_enum_pen_brush_weight_icons_items);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Pen Icon", "");

  prop = api_def_prop(sapi, "pen_vertex_icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "icon_id");
  api_def_prop_enum_items(prop, api_enum_pen_brush_vertex_icons_items)
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Grease Pencil Icon", "");

  /* Mode type. */
  prop = api_def_prop(sapi, "vertex_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "vertex_mode");
  api_def_prop_enum_items(prop, gppaint_mode_types_items);
  api_def_prop_ui_text(prop, "Mode Type", "Defines how vertex color affect to the strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  /* Vertex Color mix factor. */
  prop = api_def_prop(sapi, "vertex_color_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "vertex_factor");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Vertex Color Factor", "Factor used to mix vertex color to get final color");

  /* Hue randomness. */
  prop = api_def_prop(sapi, "random_hue_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "random_hue");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_ui_text(prop, "Hue", "Random factor to modify original hue");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* Saturation randomness. */
  prop = api_def_prop(sapi, "random_saturation_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "random_saturation");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_ui_text(prop, "Saturation", "Random factor to modify original saturation");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* Value randomness. */
  prop = api_def_prop(sapi, "random_value_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "random_value");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_ui_text(prop, "Value", "Random factor to modify original value");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* Factor to extend stroke extremes in Fill tool. */
  prop = api_def_prop(sapi, "extend_stroke_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "fill_extend_fac");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_ui_text(
      prop, "Stroke Extension", "Strokes end extension for closing gaps, use zero to disable");
  api_def_param_clear_flags(prop, PROP_ANIMATABLE, 0);

  /* Number of pixels to dilate fill area. Negative values contract the filled area. */
  prop = api_def_prop(sapi, "dilate", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "dilate_pixels");
  api_def_prop_range(prop, -40, 40);
  api_def_prop_int_default(prop, 1);
  apo_def_prop_ui_text(
      prop, "Dilate/Contract", "Number of pixels to expand or contract fill area");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Flags */
  prop = api_def_prop(sapi, "use_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_USE_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use tablet pressure");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_strength_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_USE_STRENGTH_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Use Pressure Strength", "Use tablet pressure for color strength");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  apo_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_jitter_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_USE_JITTER_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure Jitter", "Use tablet pressure for jitter");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_hue", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_HUE_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_sat", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_SAT_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_val", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_VAL_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_radius", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_PRESS_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_strength", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_STRENGTH_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_stroke_random_uv", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_UV_AT_STROKE);
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_ui_text(prop, "Stroke Random", "Use randomness at stroke level");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_hue", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", GP_BRUSH_USE_HUE_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_sat", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_SAT_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_val", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_VAL_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_radius", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", PEN_BRUSH_USE_PRESSURE_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_strength", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", GP_BRUSH_USE_STRENGTH_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_random_press_uv", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", GP_BRUSH_USE_UV_RAND_PRESS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Use pressure to modulate randomness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "use_settings_stabilizer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_STABILIZE_MOUSE);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(prop,
                       "Use Stabilizer",
                       "Draw lines with a delay to allow smooth strokes. Press Shift key to "
                       "override while drawing");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "eraser_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "eraser_mode");
  api_def_prop_enum_items(prop, api_enum_pen_brush_eraser_modes_items);
  api_def_prop_ui_text(prop, "Mode", "Eraser Mode");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(
      prop, NC_PEN | ND_DATA, "api_BrushpenSettings_eraser_mode_update");

  prop = api_def_prop(sapi, "caps_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "caps_type");
  api_def_prop_enum_items(prop, api_enum_pen_brush_caps_types_items);
  api_def_prop_ui_text(prop, "Caps Type", "The shape of the start and end of the stroke");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "fill_draw_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "fill_draw_mode");
  api_def_prop_enum_items(prop, api_enum_pen_fill_draw_modes_items);
  api_def_prop_ui_text(prop, "Mode", "Mode to draw boundary limits");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "fill_layer_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "fill_layer_mode");
  api_def_prop_enum_items(prop, api_enum_gpencil_fill_layers_modes_items);
  api_def_prop_ui_text(prop, "Layer Mode", "Layers used as boundaries");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "fill_direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "fill_direction");
  api_def_prop_enum_items(prop, api_enum_pen_fill_direction_items);
  api_def_prop_ui_text(prop, "Direction", "Direction of the fill");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "pin_draw_mode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "api_PenBrush_pin_mode_get", "api_PenBrush_pin_mode_set");
  api_def_prop_ui_icon(prop, ICON_UNPINNED, 1);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_PenBrush_pin_mode_update");
  api_def_prop_ui_text(prop, "Pin Mode", "Pin the mode to the brush");

  prop = api_def_prop(sapi, "brush_draw_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "brush_draw_mode");
  api_def_prop_enum_items(prop, api_enum_pen_brush_modes_items);
  api_def_prop_ui_text(prop, "Mode", "Preselected mode when using this brush");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_trim", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_TRIM_STROKE);
  api_def_prop_bool_default(prop, false);
  api_def_prop_ui_text(prop, "Trim Stroke Ends", "Trim intersecting stroke ends");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_sdna(prop, NULL, "sculpt_flag");
  api_def_prop_enum_items(prop, prop_direction_items);
  api_def_prop_ui_text(prop, "Direction", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_edit_position", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "sculpt_mode_flag", PEN_SCULPT_FLAGMODE_APPLY_POSITION);
  api_def_prop_ui_text(prop, "Affect Position", "The brush affects the position of the point");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_edit_strength", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "sculpt_mode_flag", PEN_SCULPT_FLAGMODE_APPLY_STRENGTH);
  api_def_prop_ui_text(
      prop, "Affect Strength", "The brush affects the color strength of the point");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  apu_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_edit_thickness", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "sculpt_mode_flag", PEN_SCULPT_FLAGMODE_APPLY_THICKNESS);
  api_def_prop_ui_text(
      prop, "Affect Thickness", "The brush affects the thickness of the point");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_edit_uv", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "sculpt_mode_flag", PEN_SCULPT_FLAGMODE_APPLY_UV);
  api_def_prop_ui_text(prop, "Affect UV", "The brush affects the UV rotation of the point");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  /* Material */
  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Material");
  api_def_prop_ptr_fns(prop, NULL, NULL, NULL, "api_BrushpenSettings_material_poll");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK | PROP_CXT_UPDATE);
  api_def_prop_ui_text(prop, "Material", "Material used for strokes drawn using this brush");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_Brush_material_update");

  prop = api_def_prop(sapi, "show_fill_boundary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_FILL_SHOW_HELPLINES);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(prop, "Show Lines", "Show help lines for filling to see boundaries");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "show_fill_extend", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_FILL_SHOW_EXTENDLINES);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(prop, "Show Extend Lines", "Show help lines for stroke extension");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "show_fill", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", PEN_BRUSH_FILL_HIDE);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(
      prop, "Show Fill", "Show transparent lines to use as boundary for filling");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_fill_limit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_FILL_FIT_DISABLE);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(prop, "Limit to Viewport", "Fill only visible areas in viewport");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_default_eraser", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_DEFAULT_ERASER);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_icon(prop, ICON_UNPINNED, 1);
  api_def_prop_ui_text(
      prop, "Default Eraser", "Use this brush when enable eraser with fast switch key");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(
      prop, NC_PEN | ND_DATA, "api_BrushpenSettings_default_eraser_update");

  prop = api_def_prop(sapi, "use_settings_postprocess", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_GROUP_SETTINGS);
  api_def_prop_ui_text(
      prop, "Use Post-Process Settings", "Additional post processing options for new strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_settings_random", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_GROUP_RANDOM);
  api_def_prop_ui_text(prop, "Random Settings", "Random brush settings");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_material_pin", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_MATERIAL_PINNED);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_ui_icon(prop, ICON_UNPINNED, 1);
  api_def_prop_ui_text(prop, "Pin Material", "Keep material assigned to brush");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(
      prop, NC_PEN | ND_DATA, "api_BrushpenSettings_use_material_pin_update");

  prop = api_def_prop(sapi, "show_lasso", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", PEN_BRUSH_DISSABLE_LASSO);
  api_def_prop_ui_text(
      prop, "Show Lasso", "Do not display fill color while drawing the stroke");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_occlude_eraser", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BRUSH_OCCLUDE_ERASER);
  api_def_prop_ui_text(prop, "Occlude Eraser", "Erase only strokes visible and not occluded");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
}

static void api_def_curves_sculpt_options(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BrushCurvesSculptSettings", NULL);
  api_def_struct_stype(sapi, "BrushCurvesSculptSettings");
  api_def_struct_ui_text(sapi, "Curves Sculpt Brush Settings", "");

  prop = api_def_prop(sapi, "add_amount", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, INT32_MAX);
  api_def_prop_ui_text(prop, "Add Amount", "Number of curves added by the Add brush");
}

static void api_def_brush(DuneApi *dapi)
{
  ApiStruct *srna;
  ApiProp *prop;

  static const EnumPropItem prop_blend_items[] = {
      {IMB_BLEND_MIX, "MIX", 0, "Mix", "Use Mix blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_DARKEN, "DARKEN", 0, "Darken", "Use Darken blending mode while painting"},
      {IMB_BLEND_MUL, "MUL", 0, "Multiply", "Use Multiply blending mode while painting"},
      {IMB_BLEND_COLORBURN,
       "COLORBURN",
       0,
       "Color Burn",
       "Use Color Burn blending mode while painting"},
      {IMB_BLEND_LINEARBURN,
       "LINEARBURN",
       0,
       "Linear Burn",
       "Use Linear Burn blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_LIGHTEN, "LIGHTEN", 0, "Lighten", "Use Lighten blending mode while painting"},
      {IMB_BLEND_SCREEN, "SCREEN", 0, "Screen", "Use Screen blending mode while painting"},
      {IMB_BLEND_COLORDODGE,
       "COLORDODGE",
       0,
       "Color Dodge",
       "Use Color Dodge blending mode while painting"},
      {IMB_BLEND_ADD, "ADD", 0, "Add", "Use Add blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_OVERLAY, "OVERLAY", 0, "Overlay", "Use Overlay blending mode while painting"},
      {IMB_BLEND_SOFTLIGHT,
       "SOFTLIGHT",
       0,
       "Soft Light",
       "Use Soft Light blending mode while painting"},
      {IMB_BLEND_HARDLIGHT,
       "HARDLIGHT",
       0,
       "Hard Light",
       "Use Hard Light blending mode while painting"},
      {IMB_BLEND_VIVIDLIGHT,
       "VIVIDLIGHT",
       0,
       "Vivid Light",
       "Use Vivid Light blending mode while painting"},
      {IMB_BLEND_LINEARLIGHT,
       "LINEARLIGHT",
       0,
       "Linear Light",
       "Use Linear Light blending mode while painting"},
      {IMB_BLEND_PINLIGHT,
       "PINLIGHT",
       0,
       "Pin Light",
       "Use Pin Light blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_DIFFERENCE,
       "DIFFERENCE",
       0,
       "Difference",
       "Use Difference blending mode while painting"},
      {IMB_BLEND_EXCLUSION,
       "EXCLUSION",
       0,
       "Exclusion",
       "Use Exclusion blending mode while painting"},
      {IMB_BLEND_SUB, "SUB", 0, "Subtract", "Use Subtract blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_HUE, "HUE", 0, "Hue", "Use Hue blending mode while painting"},
      {IMB_BLEND_SATURATION,
       "SATURATION",
       0,
       "Saturation",
       "Use Saturation blending mode while painting"},
      {IMB_BLEND_COLOR, "COLOR", 0, "Color", "Use Color blending mode while painting"},
      {IMB_BLEND_LUMINOSITY, "LUMINOSITY", 0, "Value", "Use Value blending mode while painting"},
      {0, "", ICON_NONE, NULL, NULL},
      {IMB_BLEND_ERASE_ALPHA, "ERASE_ALPHA", 0, "Erase Alpha", "Erase alpha while painting"},
      {IMB_BLEND_ADD_ALPHA, "ADD_ALPHA", 0, "Add Alpha", "Add alpha while painting"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_sculpt_plane_items[] = {
      {SCULPT_DISP_DIR_AREA, "AREA", 0, "Area Plane", ""},
      {SCULPT_DISP_DIR_VIEW, "VIEW", 0, "View Plane", ""},
      {SCULPT_DISP_DIR_X, "X", 0, "X Plane", ""},
      {SCULPT_DISP_DIR_Y, "Y", 0, "Y Plane", ""},
      {SCULPT_DISP_DIR_Z, "Z", 0, "Z Plane", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_mask_tool_items[] = {
      {BRUSH_MASK_DRAW, "DRAW", 0, "Draw", ""},
      {BRUSH_MASK_SMOOTH, "SMOOTH", 0, "Smooth", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_blur_mode_items[] = {
      {KERNEL_BOX, "BOX", 0, "Box", ""},
      {KERNEL_GAUSSIAN, "GAUSSIAN", 0, "Gaussian", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_gradient_items[] = {
      {BRUSH_GRADIENT_PRESSURE, "PRESSURE", 0, "Pressure", ""},
      {BRUSH_GRADIENT_SPACING_REPEAT, "SPACING_REPEAT", 0, "Repeat", ""},
      {BRUSH_GRADIENT_SPACING_CLAMP, "SPACING_CLAMP", 0, "Clamp", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_gradient_fill_items[] = {
      {BRUSH_GRADIENT_LINEAR, "LINEAR", 0, "Linear", ""},
      {BRUSH_GRADIENT_RADIAL, "RADIAL", 0, "Radial", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_mask_pressure_items[] = {
      {0, "NONE", 0, "Off", ""},
      {BRUSH_MASK_PRESSURE_RAMP, "RAMP", ICON_STYLUS_PRESSURE, "Ramp", ""},
      {BRUSH_MASK_PRESSURE_CUTOFF, "CUTOFF", ICON_STYLUS_PRESSURE, "Cutoff", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_size_unit_items[] = {
      {0, "VIEW", 0, "View", "Measure brush size relative to the view"},
      {BRUSH_LOCK_SIZE, "SCENE", 0, "Scene", "Measure brush size relative to the scene"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem color_gradient_items[] = {
      {0, "COLOR", 0, "Color", "Paint with a single color"},
      {BRUSH_USE_GRADIENT, "GRADIENT", 0, "Gradient", "Paint with a gradient"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_spacing_unit_items[] = {
      {0, "VIEW", 0, "View", "Calculate brush spacing relative to the view"},
      {BRUSH_SCENE_SPACING,
       "SCENE",
       0,
       "Scene",
       "Calculate brush spacing relative to the scene using the stroke location"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_jitter_unit_items[] = {
      {BRUSH_ABSOLUTE_JITTER, "VIEW", 0, "View", "Jittering happens in screen space, in pixels"},
      {0, "BRUSH", 0, "Brush", "Jittering happens relative to the brush size"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem falloff_shape_unit_items[] = {
      {0, "SPHERE", 0, "Sphere", "Apply brush influence in a Sphere, outwards from the center"},
      {PAINT_FALLOFF_SHAPE_TUBE,
       "PROJECTED",
       0,
       "Projected",
       "Apply brush influence in a 2D circle, projected from the view"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_curve_preset_items[] = {
      {BRUSH_CURVE_CUSTOM, "CUSTOM", ICON_RNDCURVE, "Custom", ""},
      {BRUSH_CURVE_SMOOTH, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", ""},
      {BRUSH_CURVE_SMOOTHER, "SMOOTHER", ICON_SMOOTHCURVE, "Smoother", ""},
      {BRUSH_CURVE_SPHERE, "SPHERE", ICON_SPHERECURVE, "Sphere", ""},
      {BRUSH_CURVE_ROOT, "ROOT", ICON_ROOTCURVE, "Root", ""},
      {BRUSH_CURVE_SHARP, "SHARP", ICON_SHARPCURVE, "Sharp", ""},
      {BRUSH_CURVE_LIN, "LIN", ICON_LINCURVE, "Linear", ""},
      {BRUSH_CURVE_POW4, "POW4", ICON_SHARPCURVE, "Sharper", ""},
      {BRUSH_CURVE_INVSQUARE, "INVSQUARE", ICON_INVERSESQUARECURVE, "Inverse Square", ""},
      {BRUSH_CURVE_CONSTANT, "CONSTANT", ICON_NOCURVE, "Constant", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_deformation_target_items[] = {
      {BRUSH_DEFORM_TARGET_GEOMETRY,
       "GEOMETRY",
       0,
       "Geometry",
       "Brush deformation displaces the vertices of the mesh"},
      {BRUSH_DEFORM_TARGET_CLOTH_SIM,
       "CLOTH_SIM",
       0,
       "Cloth Simulation",
       "Brush deforms the mesh by deforming the constraints of a cloth simulation"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_elastic_deform_type_items[] = {
      {BRUSH_ELASTIC_DEFORM_GRAB, "GRAB", 0, "Grab", ""},
      {BRUSH_ELASTIC_DEFORM_GRAB_BISCALE, "GRAB_BISCALE", 0, "Bi-Scale Grab", ""},
      {BRUSH_ELASTIC_DEFORM_GRAB_TRISCALE, "GRAB_TRISCALE", 0, "Tri-Scale Grab", ""},
      {BRUSH_ELASTIC_DEFORM_SCALE, "SCALE", 0, "Scale", ""},
      {BRUSH_ELASTIC_DEFORM_TWIST, "TWIST", 0, "Twist", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_snake_hook_deform_type_items[] = {
      {BRUSH_SNAKE_HOOK_DEFORM_FALLOFF,
       "FALLOFF",
       0,
       "Radius Falloff",
       "Applies the brush falloff in the tip of the brush"},
      {BRUSH_SNAKE_HOOK_DEFORM_ELASTIC,
       "ELASTIC",
       0,
       "Elastic",
       "Modifies the entire mesh using elastic deform"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_cloth_deform_type_items[] = {
      {BRUSH_CLOTH_DEFORM_DRAG, "DRAG", 0, "Drag", ""},
      {BRUSH_CLOTH_DEFORM_PUSH, "PUSH", 0, "Push", ""},
      {BRUSH_CLOTH_DEFORM_PINCH_POINT, "PINCH_POINT", 0, "Pinch Point", ""},
      {BRUSH_CLOTH_DEFORM_PINCH_PERPENDICULAR,
       "PINCH_PERPENDICULAR",
       0,
       "Pinch Perpendicular",
       ""},
      {BRUSH_CLOTH_DEFORM_INFLATE, "INFLATE", 0, "Inflate", ""},
      {BRUSH_CLOTH_DEFORM_GRAB, "GRAB", 0, "Grab", ""},
      {BRUSH_CLOTH_DEFORM_EXPAND, "EXPAND", 0, "Expand", ""},
      {BRUSH_CLOTH_DEFORM_SNAKE_HOOK, "SNAKE_HOOK", 0, "Snake Hook", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_cloth_force_falloff_type_items[] = {
      {BRUSH_CLOTH_FORCE_FALLOFF_RADIAL, "RADIAL", 0, "Radial", ""},
      {BRUSH_CLOTH_FORCE_FALLOFF_PLANE, "PLANE", 0, "Plane", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_boundary_falloff_type_items[] = {
      {BRUSH_BOUNDARY_FALLOFF_CONSTANT,
       "CONSTANT",
       0,
       "Constant",
       "Applies the same deformation in the entire boundary"},
      {BRUSH_BOUNDARY_FALLOFF_RADIUS,
       "RADIUS",
       0,
       "Brush Radius",
       "Applies the deformation in a localized area limited by the brush radius"},
      {BRUSH_BOUNDARY_FALLOFF_LOOP,
       "LOOP",
       0,
       "Loop",
       "Applies the brush falloff in a loop pattern"},
      {BRUSH_BOUNDARY_FALLOFF_LOOP_INVERT,
       "LOOP_INVERT",
       0,
       "Loop and Invert",
       "Applies the falloff radius in a loop pattern, inverting the displacement direction in "
       "each pattern repetition"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_cloth_simulation_area_type_items[] = {
      {BRUSH_CLOTH_SIMULATION_AREA_LOCAL,
       "LOCAL",
       0,
       "Local",
       "Simulates only a specific area around the brush limited by a fixed radius"},
      {BRUSH_CLOTH_SIMULATION_AREA_GLOBAL, "GLOBAL", 0, "Global", "Simulates the entire mesh"},
      {BRUSH_CLOTH_SIMULATION_AREA_DYNAMIC,
       "DYNAMIC",
       0,
       "Dynamic",
       "The active simulation area moves with the brush"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_smooth_deform_type_items[] = {
      {BRUSH_SMOOTH_DEFORM_LAPLACIAN,
       "LAPLACIAN",
       0,
       "Laplacian",
       "Smooths the surface and the volume"},
      {BRUSH_SMOOTH_DEFORM_SURFACE,
       "SURFACE",
       0,
       "Surface",
       "Smooths the surface of the mesh, preserving the volume"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_pose_deform_type_items[] = {
      {BRUSH_POSE_DEFORM_ROTATE_TWIST, "ROTATE_TWIST", 0, "Rotate/Twist", ""},
      {BRUSH_POSE_DEFORM_SCALE_TRASLATE, "SCALE_TRANSLATE", 0, "Scale/Translate", ""},
      {BRUSH_POSE_DEFORM_SQUASH_STRETCH, "SQUASH_STRETCH", 0, "Squash & Stretch", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_pose_origin_type_items[] = {
      {BRUSH_POSE_ORIGIN_TOPOLOGY,
       "TOPOLOGY",
       0,
       "Topology",
       "Sets the rotation origin automatically using the topology and shape of the mesh as a "
       "guide"},
      {BRUSH_POSE_ORIGIN_FACE_SETS,
       "FACE_SETS",
       0,
       "Face Sets",
       "Creates a pose segment per face sets, starting from the active face set"},
      {BRUSH_POSE_ORIGIN_FACE_SETS_FK,
       "FACE_SETS_FK",
       0,
       "Face Sets FK",
       "Simulates an FK deformation using the Face Set under the cursor as control"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_smear_deform_type_items[] = {
      {BRUSH_SMEAR_DEFORM_DRAG, "DRAG", 0, "Drag", ""},
      {BRUSH_SMEAR_DEFORM_PINCH, "PINCH", 0, "Pinch", ""},
      {BRUSH_SMEAR_DEFORM_EXPAND, "EXPAND", 0, "Expand", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_slide_deform_type_items[] = {
      {BRUSH_SLIDE_DEFORM_DRAG, "DRAG", 0, "Drag", ""},
      {BRUSH_SLIDE_DEFORM_PINCH, "PINCH", 0, "Pinch", ""},
      {BRUSH_SLIDE_DEFORM_EXPAND, "EXPAND", 0, "Expand", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem brush_boundary_deform_type_items[] = {
      {BRUSH_BOUNDARY_DEFORM_BEND, "BEND", 0, "Bend", ""},
      {BRUSH_BOUNDARY_DEFORM_EXPAND, "EXPAND", 0, "Expand", ""},
      {BRUSH_BOUNDARY_DEFORM_INFLATE, "INFLATE", 0, "Inflate", ""},
      {BRUSH_BOUNDARY_DEFORM_GRAB, "GRAB", 0, "Grab", ""},
      {BRUSH_BOUNDARY_DEFORM_TWIST, "TWIST", 0, "Twist", ""},
      {BRUSH_BOUNDARY_DEFORM_SMOOTH, "SMOOTH", 0, "Smooth", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Brush", "ID");
  api_def_struct_ui_text(
      sapi, "Brush", "Brush data-block for storing brush settings for painting and sculpting");
  api_def_struct_ui_icon(sapi, ICON_BRUSH_DATA);

  /* enums */
  prop = api_def_prop(sapi, "blend", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_blend_items);
  api_def_prop_ui_text(prop, "Blending Mode", "Brush blending mode");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  /**
   * Begin per-mode tool properties.
   *
   * keep in sync with #dune_paint_get_tool_prop_id_from_paintmode
   */
  prop = api_def_prop(sapi, "sculpt_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_brush_sculpt_tool_items);
  api_def_prop_ui_text(prop, "Sculpt Tool", "");
  api_def_prop_update(prop, 0, "api_Brush_update_and_reset_icon");

  prop = api_def_prop(sapi, "uv_sculpt_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_brush_uv_sculpt_tool_items);
  api_def_prop_ui_text(prop, "Sculpt Tool", "");
  api_def_prop_update(prop, 0, "rna_Brush_update_and_reset_icon");

  prop = api_def_prop(sapi, "vertex_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "vertexpaint_tool");
  api_def_prop_enum_items(prop, api_enum_brush_vertex_tool_items);
  api_def_prop_ui_text(prop, "Vertex Paint Tool", "");
  api_def_prop_update(prop, 0, "api_Brush_update_and_reset_icon");

  prop = api_def_prop(sapi, "weight_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "weightpaint_tool");
  api_def_prop_enum_items(prop, api_enum_brush_weight_tool_items);
  api_def_prop_ui_text(prop, "Weight Paint Tool", "");
  api_def_prop_update(prop, 0, "api_Brush_update_and_reset_icon");

  prop = api_def_prop(sapi, "image_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "imagepaint_tool");
  api_def_prop_enum_items(prop, api_enum_brush_image_tool_items);
  api_def_prop_ui_text(prop, "Image Paint Tool", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, "api_Brush_update_and_reset_icon");

  prop = api_def_prop(sapi, "gpencil_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "pen_tool");
  api_def_prop_enum_items(prop, api_enum_brush_pen_types_items);
  api_def_prop_ui_text(prop, "Pen Draw Tool", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "pen_vertex_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "pen_vertex_tool");
  api_def_prop_enum_items(prop, api_enum_brush_pen_vertex_types_items);
  api_def_prop_ui_text(prop, "Grease Pencil Vertex Paint Tool", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "pen_sculpt_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "pen_sculpt_tool");
  api_def_prop_enum_items(prop, api_enum_brush_pen_sculpt_types_items);
  api_def_prop_ui_text(prop, "Pen Sculpt Paint Tool", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "pen_weight_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "pen_weight_tool");
  api_def_prop_enum_items(prop, api_enum_brush_pen_weight_types_items);
  api_def_prop_ui_text(prop, "Grease Pencil Weight Paint Tool", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "curves_sculpt_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_brush_curves_sculpt_tool_items);
  api_def_prop_ui_text(prop, "Curves Sculpt Tool", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  /** End per mode tool properties. */

  prop = api_def_prop(sapi, "direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_direction_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_Brush_direction_itemf");
  api_def_prop_ui_text(prop, "Direction", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "stroke_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, sculpt_stroke_method_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_Brush_stroke_itemf");
  api_def_prop_ui_text(prop, "Stroke Method", "");
  api_def_prop_update(prop, 0, "api_Brush_stroke_update");

  prop = api_def_prop(sapi, "sculpt_plane", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_sculpt_plane_items);
  api_def_prop_ui_text(prop, "Sculpt Plane", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "mask_tool", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_mask_tool_items);
  api_def_prop_ui_text(prop, "Mask Tool", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "curve_preset", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_curve_preset_items);
  api_def_prop_ui_text(prop, "Curve Preset", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "deform_target", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_deformation_target_items);
  api_def_prop_ui_text(
      prop, "Deformation Target", "How the deformation of the brush will affect the object");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "elastic_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_elastic_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "snake_hook_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_snake_hook_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_cloth_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_force_falloff_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_cloth_force_falloff_type_items);
  api_def_prop_ui_text(
      prop, "Force Falloff", "Shape used in the brush to apply force to the cloth");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_simulation_area_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_cloth_simulation_area_type_items);
  api_def_prop_ui_text(
      prop,
      "Simulation Area",
      "Part of the mesh that is going to be simulated when the stroke is active");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "boundary_falloff_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_boundary_falloff_type_items);
  api_def_prop_ui_text(
      prop, "Boundary Falloff", "How the brush falloff is applied across the boundary");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "smooth_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_smooth_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "smear_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_smear_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "slide_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_slide_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "boundary_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_boundary_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "pose_deform_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_pose_deform_type_items);
  api_def_prop_ui_text(prop, "Deformation", "Deformation type that is used in the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "pose_origin_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_pose_origin_type_items);
  api_def_prop_ui_text(prop,
                       "Rotation Origins",
                       "Method to set the rotation origins for the segments of the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "jitter_unit", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, brush_jitter_unit_items);
  api_def_prop_ui_text(
      prop, "Jitter Unit", "Jitter in screen space or relative to brush size");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "falloff_shape", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "falloff_shape");
  api_def_prop_enum_items(prop, falloff_shape_unit_items);
  api_def_prop_ui_text(prop, "Falloff Shape", "Use projected or spherical falloff");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* number values */
  prop = api_def_prop(sapi, "size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_fns(prop, NULL, "rna_Brush_set_size", NULL);
  api_def_prop_range(prop, 1, MAX_BRUSH_PIXEL_RADIUS * 10);
  api_def_prop_ui_range(prop, 1, MAX_BRUSH_PIXEL_RADIUS, 1, -1);
  api_def_prop_ui_text(prop, "Radius", "Radius of the brush in pixels");
  api_def_prop_update(prop, 0, "rna_Brush_size_update");

  prop = api_def_prop(sapi, "unprojected_radius", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_fns(prop, NULL, "rna_Brush_set_unprojected_radius", NULL);
  api_def_prop_range(prop, 0.001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001, 1, 1, -1);
  api_def_prop_ui_text(prop, "Unprojected Radius", "Radius of brush in Blender units");
  api_def_prop_update(prop, 0, "rna_Brush_size_update");

  prop = api_def_prop(sapi, "jitter", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "jitter");
  api_def_prop_range(prop, 0.0f, 1000.0f);
  api_def_prop_ui_range(prop, 0.0f, 2.0f, 0.1, 4);
  api_def_prop_ui_text(prop, "Jitter", "Jitter the position of the brush while painting");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "jitter_absolute", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "jitter_absolute");
  api_def_prop_range(prop, 0, 1000000);
  api_def_prop_ui_text(
      prop, "Jitter", "Jitter the position of the brush in pixels while painting");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "spacing", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "spacing");
  api_def_prop_range(prop, 1, 1000);
  api_def_prop_ui_range(prop, 1, 500, 5, -1);
  api_def_prop_ui_text(
      prop, "Spacing", "Spacing between brush daubs as a percentage of brush diameter");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "grad_spacing", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "gradient_spacing");
  api_def_prop_range(prop, 1, 10000);
  api_def_prop_ui_range(prop, 1, 10000, 5, -1);
  api_def_prop_ui_text(
      prop, "Gradient Spacing", "Spacing before brush gradient goes full circle");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "smooth_stroke_radius", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 10, 200);
  api_def_prop_ui_text(
      prop, "Smooth Stroke Radius", "Minimum distance from last point before stroke continues");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "smooth_stroke_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.5, 0.99);
  api_def_prop_ui_text(prop, "Smooth Stroke Factor", "Higher values give a smoother stroke");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "rate", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "rate");
  api_def_prop_range(prop, 0.0001f, 10000.0f);
  api_def_prop_ui_range(prop, 0.01f, 1.0f, 1, 3);
  api_def_prop_ui_text(prop, "Rate", "Interval between paints for Airbrush");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "rgb");
  api_def_prop_ui_text(prop, "Color", "");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "secondary_color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "secondary_rgb");
  api_def_prop_ui_text(prop, "Secondary Color", "");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(prop, "Weight", "Vertex weight when brush is applied");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "alpha");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 
  api_def_prop_ui_text(
      prop, "Strength", "How powerful the effect of the brush is when applied");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "flow", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "flow");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  apu_def_prop_ui_text(prop, "Flow", "Amount of paint that is applied per stroke sample");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "wet_mix", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "wet_mix");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Wet Mix", "Amount of paint that is picked from the surface into the brush color");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "wet_persistence", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "wet_persistence");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop,
      "Wet Persistence",
      "Amount of wet paint that stays in the brush after applying paint to the surface");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "density", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "density");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Density", "Amount of random elements that are going to be affected by the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "tip_scale_x", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "tip_scale_x");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(prop, "Tip Scale X", "Scale of the brush tip in the X axis");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "use_hardness_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_HARDNESS_PRESSURE
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure for Hardness", "Use pressure to modulate hardness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "invert_hardness_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_HARDNESS_PRESSURE_INVERT);
  api_def_prop_ui_icon(prop, ICON_ARROW_LEFTRIGHT, 0);
  api_def_prop_ui_text(
      prop, "Invert Pressure for Hardness", "Invert the modulation of pressure in hardness");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_flow_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_FLOW_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure for Flow", "Use pressure to modulate flow");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "invert_flow_pressure", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_FLOW_PRESSURE_INVERT);
  api_def_prop_ui_icon(prop, ICON_ARROW_LEFTRIGHT, 0);
  api_def_prop_ui_text(
      prop, "Invert Pressure for Flow", "Invert the modulation of pressure in flow");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_wet_mix_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_WET_MIX_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure for Wet Mix", "Use pressure to modulate wet mix");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "invert_wet_mix_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_WET_MIX_PRESSURE_INVERT);
  api_def_prop_ui_icon(prop, ICON_ARROW_LEFTRIGHT, 0);
  api_def_prop_ui_text(
      prop, "Invert Pressure for Wet Mix", "Invert the modulation of pressure in wet mix");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_wet_persistence_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_WET_PERSISTENCE_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Use Pressure for Wet Persistence", "Use pressure to modulate wet persistence");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "invert_wet_persistence_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "paint_flags", BRUSH_PAINT_WET_PERSISTENCE_PRESSURE_INVERT);
  api_def_prop_ui_icon(prop, ICON_ARROW_LEFTRIGHT, 0);
  api_def_prop_ui_text(prop,
                      "Invert Pressure for Wet Persistence",
                      "Invert the modulation of pressure in wet persistence");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_density_pressure", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_DENSITY_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure for Density", "Use pressure to modulate density");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "invert_density_pressure", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "paint_flags", BRUSH_PAINT_DENSITY_PRESSURE_INVERT);
  api_def_prop_ui_icon(prop, ICON_ARROW_LEFTRIGHT, 0);
  api_def_prop_ui_text(
      prop, "Invert Pressure for Density", "Invert the modulation of pressure in density");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "dash_ratio", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "dash_ratio");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Dash Ratio", "Ratio of samples in a cycle that the brush is enabled");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "dash_samples", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "dash_samples");
  api_def_prop_range(prop, 1, 10000);
  apo_def_prop_ui_range(prop, 1, 10000, 5, -1);
  api_def_prop_ui_text(
      prop, "Dash Length", "Length of a dash cycle measured in stroke samples");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "plane_offset", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "plane_offset");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, -2.0f, 2.0f);
  api_def_prop_ui_range(prop, -0.5f, 0.5f, 0.001, 3);
  api_def_prop_ui_text(
      prop,
      "Plane Offset",
      "Adjust plane on which the brush acts towards or away from the object surface");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "plane_trim", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "plane_trim");
  api_def_prop_range(prop, 0, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Plane Trim",
      "If a vertex is further away from offset plane than this, then it is not affected");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "height", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "height");
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_range(prop, 0, 1.0f);
  api_def_prop_ui_range(prop, 0, 0.2f, 1, 3);
  api_def_prop_ui_text(
      prop, "Brush Height", "Affectable height of brush (layer height for layer tool, i.e.)");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "texture_sample_bias", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "texture_sample_bias");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, -1, 1);
  api_def_prop_ui_text(prop, "Texture Sample Bias", "Value added to texture samples");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "normal_weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "normal_weight");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Normal Weight", "How much grab will pull vertices out of surface during a grab");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "elastic_deform_volume_preservation", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "elastic_deform_volume_preservation");
  api_def_prop_range(prop, 0.0f, 0.9f);
  api_def_prop_ui_range(prop, 0.0f, 0.9f, 0.01f, 3);
  api_def_prop_ui_text(prop,
                           "Volume Preservation",
                           "Poisson ratio for elastic deformation. Higher values preserve volume "
                           "more, but also lead to more bulging");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "rake_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "rake_factor");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(prop, "Rake", "How much grab will follow cursor rotation");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = apo_def_prop(sapi, "crease_pinch_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "crease_pinch_factor");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Crease Brush Pinch Factor", "How much the crease brush pinches");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "pose_offset", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "pose_offset");
  api_def_prop_range(prop, 0.0f, 2.0f);
  api_def_prop_ui_text(
      prop, "Pose Origin Offset", "Offset of the pose origin in relation to the brush radius");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "disconnected_distance_max", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "disconnected_distance_max");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(prop,
                           "Max Element Distance",
                           "Maximum distance to search for disconnected loose parts in the mesh");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "boundary_offset", PROP_FLOAT, PROP_FACTOR);
  apk_def_prop_float_stype(prop, NULL, "boundary_offset");
  api_def_prop_range(prop, 0.0f, 30.0f);
  api_def_prop_ui_text(prop,
                           "Boundary Origin Offset",
                           "Offset of the boundary origin in relation to the brush radius");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "surface_smooth_shape_preservation", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "surface_smooth_shape_preservation");
  wpi_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Shape Preservation", "How much of the original shape is preserved when smoothing");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "surface_smooth_current_vertex", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "surface_smooth_current_vertex");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Per Vertex Displacement",
      "How much the position of each individual vertex influences the final result");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "surface_smooth_iterations", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "surface_smooth_iterations");
  api_def_prop_range(prop, 1, 10);
  api_def_prop_ui_range(prop, 1, 10, 1, 3);
  api_def_prop_ui_text(prop, "Iterations", "Number of smoothing iterations per brush step");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "multiplane_scrape_angle", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "multiplane_scrape_angle");
  api_def_prop_range(prop, 0.0f, 160.0f);
  api_def_prop_ui_text(prop, "Plane Angle", "Angle between the planes of the crease");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "pose_smooth_iterations", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "pose_smooth_iterations");l
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(
      prop,
      "Smooth Iterations",
      "Smooth iterations applied after calculating the pose factor of each vertex");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "pose_ik_segments", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "pose_ik_segments");
  api_def_prop_range(prop, 1, 20);
  api_def_prop_ui_range(prop, 1, 20, 1, 3);
  api_def_prop_ui_text(
      prop,
      "Pose IK Segments",
      "Number of segments of the inverse kinematics chain that will deform the mesh");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "tip_roundness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "tip_roundness");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Tip Roundness", "Roundness of the brush tip");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_mass", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "cloth_mass");
  api_def_prop_range(prop, 0.01f, 2.0f);
  api_def_prop_ui_text(prop, "Cloth Mass", "Mass of each simulation particle");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_damping", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "cloth_damping");
  api_def_prop_range(prop, 0.01f, 1.0f);
  api_def_prop_ui_text(
      prop, "Cloth Damping", "How much the applied forces are propagated through the cloth");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_sim_limit", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "cloth_sim_limit");
  api_def_prop_range(prop, 0.1f, 10.0f);
  api_def_prop_ui_text(
      prop,
      "Simulation Limit",
      "Factor added relative to the size of the radius to limit the cloth simulation effects");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_sim_falloff", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "cloth_sim_falloff");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Simulation Falloff",
                       "Area to apply deformation falloff to the effects of the simulation");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cloth_constraint_softbody_strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "cloth_constraint_softbody_strength");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Soft Body Plasticity",
      "How much the cloth preserves the original shape, acting as a soft body");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "hardness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "hardness");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Hardness", "How close the brush falloff starts from the edge of the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(
      sapi, "automasking_boundary_edges_propagation_steps", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "automasking_boundary_edges_propagation_steps");
  api_def_prop_range(prop, 1, 20);
  api_def_prop_ui_range(prop, 1, 20, 1, 3);
  api_def_prop_ui_text(prop,
                       "Propagation Steps",
                       "Distance where boundary edge automasking is going to protect vertices "
                       "from the fully masked edge");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "auto_smooth_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "autosmooth_factor");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Auto-Smooth", "Amount of smoothing to automatically apply to each stroke");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "topology_rake_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "topology_rake_factor");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(prop,
                       "Topology Rake",
                       "Automatically align edges to the brush direction to "
                       "generate cleaner topology and define sharp features. "
                       "Best used on low-poly meshes as it has a performance impact");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "tilt_strength_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "tilt_strength_factor");
  api_def_prop_float_default(prop, 0);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Tilt Strength", "How much the tilt of the pen will affect the brush");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "normal_radius_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "normal_radius_factor");
  api_def_prop_range(prop, 0.0f, 2.0f);
  api_def_prop_ui_range(prop, 0.0f, 2.0f, 0.001, 3);
  api_def_prop_ui_text(prop,
                       "Normal Radius",
                       "Ratio between the brush radius and the radius that is going to be "
                       "used to sample the normal");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "area_radius_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "area_radius_factor");
  api_def_prop_range(prop, 0.0f, 2.0f);
  api_def_prop_ui_range(prop, 0.0f, 2.0f, 0.001, 3);
  api_def_prop_ui_text(prop,
                       "Area Radius",
                       "Ratio between the brush radius and the radius that is going to be "
                       "used to sample the area center");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "wet_paint_radius_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "wet_paint_radius_factor");
  api_def_prop_range(prop, 0.0f, 2.0f);
  api_def_prop_ui_range(prop, 0.0f, 2.0f, 0.001, 3);
  api_def_prop_ui_text(prop,
                       "Wet Paint Radius",
                       "Ratio between the brush radius and the radius that is going to be "
                       "used to sample the color to blend in wet paint");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "stencil_pos", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "stencil_pos");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Stencil Position", "Position of stencil in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "stencil_dimension", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "stencil_dimension");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Stencil Dimensions", "Dimensions of stencil in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "mask_stencil_pos", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "mask_stencil_pos");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Mask Stencil Position", "Position of mask stencil in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "mask_stencil_dimension", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "mask_stencil_dimension");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(
      prop, "Mask Stencil Dimensions", "Dimensions of mask stencil in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "sharp_threshold", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0, 1.0, 1, 3);
  api_def_prop_float_stype(prop, NULL, "sharp_threshold");
  api_def_prop_ui_text(
      prop, "Sharp Threshold", "Threshold below which, no sharpening is done");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "fill_threshold", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0, 1.0, 1, 3);
  api_def_prop_float_stype(prop, NULL, "fill_threshold");
  api_def_prop_ui_text(
      prop, "Fill Threshold", "Threshold above which filling is not propagated");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "blur_kernel_radius", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "blur_kernel_radius");
  api_def_prop_range(prop, 1, 10000);
  api_def_prop_ui_range(prop, 1, 50, 1, -1);
  api_def_prop_ui_text(
      prop, "Kernel Radius", "Radius of kernel used for soften and sharpen in pixels");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "blur_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_blur_mode_items);
  api_def_prop_ui_text(prop, "Blur Mode", "");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "falloff_angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "falloff_angle");
  api_def_prop_range(prop, 0, M_PI_2);
  api_def_prop_ui_text(
      prop,
      "Falloff Angle",
      "Paint most on faces pointing towards the view according to this angle");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  /* flag */
  prop = api_def_prop(sapi, "use_airbrush", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypes(prop, NULL, "flag", BRUSH_AIRBRUSH);
  api_def_prop_ui_text(
      prop, "Airbrush", "Keep applying paint effect while holding mouse (spray)");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_original_normal", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ORIGINAL_NORMAL);
  api_def_prop_ui_text(prop,
                       "Original Normal",
                       "When locked keep using normal of surface where stroke was initiated");
  api_def_prop_update(prop, 0, "rna_Brush_update");

  prop = api_def_prop(sapi, "use_original_plane", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ORIGINAL_PLANE);
  api_def_prop_ui_text(
      prop,
      "Original Plane",
      "When locked keep using the plane origin of surface where stroke was initiated");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_automasking_topology", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypes(prop, NULL, "automasking_flags", BRUSH_AUTOMASKING_TOPOLOGY);
  ali_def_prop_ui_text(prop,
                       "Topology Auto-Masking",
                       "Affect only vertices connected to the active vertex under the brush");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = apo_def_prop(sapi, "use_automasking_face_sets", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "automasking_flags", BRUSH_AUTOMASKING_FACE_SETS);
  api_def_prop_ui_text(prop,
                       "Face Sets Auto-Masking",
                       "Affect only vertices that share Face Sets with the active vertex");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_automasking_boundary_edges", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "automasking_flags", BRUSH_AUTOMASKING_BOUNDARY_EDGES);
  api_def_prop_ui_text(
      prop, "Mesh Boundary Auto-Masking", "Do not affect non manifold boundary edges");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_automasking_boundary_face_sets", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "automasking_flags", BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS);
  api_def_prop_ui_text(prop,
                       "Face Sets Boundary Automasking",
                       "Do not affect vertices that belong to a Face Set boundary");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_scene_spacing", PROP_ENUM, PROP_NONE);
  apo_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, brush_spacing_unit_items);
  api_def_prop_ui_text(
      prop, "Spacing Distance", "Calculate the brush spacing using view or scene distance");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_grab_active_vertex", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_GRAB_ACTIVE_VERTEX);
  api_def_prop_ui_text(
      prop,
      "Grab Active Vertex",
      "Apply the maximum grab strength to the active vertex instead of the cursor location");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_grab_silhouette", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_GRAB_SILHOUETTE);
  api_def_prop_ui_text(
      prop, "Grab Silhouette", "Grabs trying to automask the silhouette of the object");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_paint_antialiasing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "sampling_flag", BRUSH_PAINT_ANTIALIASING);
  api_def_prop_ui_text(prop, "Anti-Aliasing", "Smooths the edges of the strokes");

  prop = api_def_prop(sapi, "use_multiplane_scrape_dynamic", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "flag2", BRUSH_MULTIPLANE_SCRAPE_DYNAMIC);
  api_def_prop_ui_text(prop,
                       "Dynamic Mode",
                       "The angle between the planes changes during the stroke to fit the "
                       "surface under the cursor");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "show_multiplane_scrape_planes_preview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_MULTIPLANE_SCRAPE_PLANES_PREVIEW);
  api_def_prop_ui_text(
      prop, "Show Cursor Preview", "Preview the scrape planes in the cursor during the stroke");
  apk_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pose_ik_anchored", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_POSE_IK_ANCHORED);
  api_def_prop_ui_text(
      prop, "Keep Anchor Point", "Keep the position of the last segment in the IK chain fixed");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pose_lock_rotation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_POSE_USE_LOCK_ROTATION);
  api_def_prop_ui_text(prop,
                       "Lock Rotation When Scaling",
                       "Do not rotate the segment when using the scale deform mode");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_connected_only", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypes(prop, NULL, "flag2", BRUSH_USE_CONNECTED_ONLY);
  api_def_prop_ui_text(prop, "Connected Only", "Affect only topologically connected elements");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_cloth_pin_simulation_boundary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_CLOTH_PIN_SIMULATION_BOUNDARY);
  api_def_prop_ui_text(
      prop,
      "Pin Simulation Boundary",
      "Lock the position of the vertices in the simulation falloff area to avoid artifacts and "
      "create a softer transition with unaffected areas");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_cloth_collision", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_CLOTH_USE_COLLISION);
  api_def_prop_ui_text(prop, "Enable Collision", "Collide with objects during the simulation");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "invert_to_scrape_fill", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_INVERT_TO_SCRAPE_FILL);
  api_def_prop_ui_text(prop,
                       "Invert to Scrape or Fill",
                       "Use Scrape or Fill tool when inverting this brush instead of "
                       "inverting its displacement direction");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_strength", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ALPHA_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Strength Pressure", "Enable tablet pressure sensitivity for strength");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_offset_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_OFFSET_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Plane Offset Pressure", "Enable tablet pressure sensitivity for offset");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_area_radius", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag2", BRUSH_AREA_RADIUS_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Area Radius Pressure", "Enable tablet pressure sensitivity for area radius");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_size", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_SIZE_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Size Pressure", "Enable tablet pressure sensitivity for size");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_jitter", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_JITTER_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Jitter Pressure", "Enable tablet pressure sensitivity for jitter");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_spacing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_SPACING_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Spacing Pressure", "Enable tablet pressure sensitivity for spacing");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_pressure_masking", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mask_pressure");
  api_def_prop_enum_items(prop, brush_mask_pressure_items);
  api_def_prop_ui_text(
      prop, "Mask Pressure Mode", "Pen pressure makes texture influence smaller");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_inverse_smooth_pressure", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_INVERSE_SMOOTH_PRESSURE);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(
      prop, "Inverse Smooth Pressure", "Lighter pressure causes more smoothing to be applied");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapo, "use_plane_trim", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_PLANE_TRIM);
  api_def_prop_ui_text(prop, "Use Plane Trim", "Enable Plane Trim");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_frontface", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_FRONTFACE);
  api_def_prop_ui_text(
      prop, "Use Front-Face", "Brush only affects vertices that face the viewer");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_frontface_falloff", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_FRONTFACE_FALLOFF);
  api_def_prop_ui_text(
      prop, "Use Front-Face Falloff", "Blend brush influence by how much they face the front");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_anchor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ANCHORED);
  api_def_prop_ui_text(prop, "Anchored", "Keep the brush anchored to the initial location");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_space", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_SPACE);
  api_def_prop_ui_text(
      prop, "Space", "Limit brush application to the distance specified by spacing");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_line", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_LINE);
  api_def_prop_ui_text(prop, "Line", "Draw a line with dabs separated according to spacing");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_CURVE);
  api_def_prop_ui_text(
      prop,
      "Curve",
      "Define the stroke curve with a bezier curve. Dabs are separated according to spacing");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_smooth_stroke", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_SMOOTH_STROKE);
  api_def_prop_ui_text(
      prop, "Smooth Stroke", "Brush lags behind mouse and follows a smoother path");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_persistent", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_PERSISTENT);
  api_def_prop_ui_text(prop, "Persistent", "Sculpt on a persistent layer of the mesh");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_accumulate", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ACCUMULATE);
  api_def_prop_ui_text(prop, "Accumulate", "Accumulate stroke daubs on top of each other");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_space_attenuation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_SPACE_ATTEN);
  api_def_prop_ui_text(
      prop,
      "Adjust Strength for Spacing",
      "Automatically adjust strength to give consistent results for different spacings");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* adaptive space is not implemented yet */
  prop = api_def_prop(sapi, "use_adaptive_space", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_ADAPTIVE_SPACE);
  api_def_prop_ui_text(prop,
                       "Adaptive Spacing",
                       "Space daubs according to surface orientation instead of screen space");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_locked_size", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, brush_size_unit_items);
  api_def_prop_ui_text(
      prop, "Radius Unit", "Measure brush size relative to the view or the scene");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "color_type", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_sdna(prop, NULL, "flag");
  api_def_prop_enum_items(prop, color_gradient_items);
  api_def_prop_enum_fns(prop, NULL, "rna_Brush_use_gradient_set", NULL);
  api_def_prop_ui_text(prop, "Color Type", "Use single color or gradient when painting");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_edge_to_edge", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_EDGE_TO_EDGE);
  api_def_prop_ui_text(prop, "Edge-to-Edge", "Drag anchor brush from edge-to-edge");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_restore_mesh", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_DRAG_DOT);
  api_def_prop_ui_text(prop, "Restore Mesh", "Allow a single dot to be carefully positioned");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* only for projection paint & vertex paint, TODO: other paint modes. */
  prop = api_def_prop(sapi, "use_alpha", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "flag", BRUSH_LOCK_ALPHA);
  api_def_prop_ui_text(
      prop, "Affect Alpha", "When this is disabled, lock alpha while painting");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Curve", "Editable falloff curve");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "paint_curve", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Paint Curve", "Active paint curve");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "gradient", PROP_PTR, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "gradient");
  api_def_prop_struct_type(prop, "ColorRamp");
  api_def_prop_ui_text(prop, "Gradient", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* gradient source */
  prop = api_def_prop(sapi, "gradient_stroke_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_gradient_items);
  api_def_prop_ui_text(prop, "Gradient Stroke Mode", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "gradient_fill_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, brush_gradient_fill_items);
  api_def_prop_ui_text(prop, "Gradient Fill Mode", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* overlay flags */
  prop = api_def_prop(sapi, "use_primary_overlay", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "overlay_flags", BRUSH_OVERLAY_PRIMARY);
  api_def_prop_ui_text(prop, "Use Texture Overlay", "Show texture in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_secondary_overlay", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "overlay_flags", BRUSH_OVERLAY_SECONDARY);
  api_def_prop_ui_text(prop, "Use Texture Overlay", "Show texture in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_cursor_overlay", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "overlay_flags", BRUSH_OVERLAY_CURSOR);
  api_def_prop_ui_text(prop, "Use Cursor Overlay", "Show cursor in viewport");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_cursor_overlay_override", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "overlay_flags", BRUSH_OVERLAY_CURSOR_OVERRIDE_ON_STROKE);
  api_def_prop_ui_text(prop, "Override Overlay", "Don't show overlay during a stroke");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_primary_overlay_override", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "overlay_flags", BRUSH_OVERLAY_PRIMARY_OVERRIDE_ON_STROKE);
  api_def_prop_ui_text(prop, "Override Overlay", "Don't show overlay during a stroke");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_secondary_overlay_override", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "overlay_flags", BRUSH_OVERLAY_SECONDARY_OVERRIDE_ON_STROKE);
  api_def_prop_ui_text(prop, "Override Overlay", "Don't show overlay during a stroke");
  api_def_prop_update(prop, 0, "api_Brush_update");

  /* paint mode flags */
  prop = api_def_prop(sapi, "use_paint_sculpt", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_SCULPT);
  api_def_prop_ui_text(prop, "Use Sculpt", "Use this brush in sculpt mode");

  prop = api_def_prop(sapi, "use_paint_uv_sculpt", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_EDIT);
  api_def_prop_ui_text(prop, "Use UV Sculpt", "Use this brush in UV sculpt mode");

  prop = api_def_prop(sapi, "use_paint_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_VERTEX_PAINT);
  api_def_prop_ui_text(prop, "Use Vertex", "Use this brush in vertex paint mode");

  prop = api_def_prop(sapi, "use_paint_weight", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_WEIGHT_PAINT);
  api_def_prop_ui_text(prop, "Use Weight", "Use this brush in weight paint mode");

  prop = api_def_prop(sapi, "use_paint_image", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_TEXTURE_PAINT);
  api_def_prop_ui_text(prop, "Use Texture", "Use this brush in texture paint mode");

  prop = api_def_prop(sapi, "use_paint_pen", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_PAINT_PEN);
  api_def_prop_ui_text(prop, "Use Paint", "Use this brush in grease pencil drawing mode");

  prop = api_def_prop(sapi, "use_vertex_pen", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ob_mode", OB_MODE_VERTEX_GPENCIL);
  api_def_prop_ui_text(
      prop, "Use Vertex", "Use this brush in pen vertex color mode");

  /* texture */
  prop = api_def_prop(sapi, "texture_slot", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "BrushTextureSlot");
  api_def_prop_ptr_stype(prop, NULL, "mtex");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Texture Slot", "");

  prop = api_def_prop(sapi, "texture", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "mtex.tex");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_CXT_UPDATE);
  api_def_prop_ui_text(prop, "Texture", "");
  api_def_prop_update(prop, NC_TEXTURE, "api_Brush_main_tex_update");

  prop = api_def_prop(sapi, "mask_texture_slot", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "BrushTextureSlot");
  api_def_prop_ptr_stype(prop, NULL, "mask_mtex");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Mask Texture Slot", "");

  prop = api_def_prop(sapi, "mask_texture", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "mask_mtex.tex");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_CXT_UPDATE);
  api_def_prop_ui_text(prop, "Mask Texture", "");
  api_def_prop_update(prop, NC_TEXTURE, "api_Brush_secondary_tex_update");

  prop = api_def_prop(sapi, "texture_overlay_alpha", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "texture_overlay_alpha");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Texture Overlay Alpha", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "mask_overlay_alpha", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "mask_overlay_alpha");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Mask Texture Overlay Alpha", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cursor_overlay_alpha", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "cursor_overlay_alpha");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Mask Texture Overlay Alpha", "");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cursor_color_add", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "add_col");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Add Color", "Color of cursor when adding");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "cursor_color_subtract", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "sub_col");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Subtract Color", "Color of cursor when subtracting");
  api_def_prop_update(prop, 0, "api_Brush_update");

  prop = api_def_prop(sapi, "use_custom_icon", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BRUSH_CUSTOM_ICON);
  api_def_prop_ui_text(prop, "Custom Icon", "Set the brush icon from an image file");
  api_def_prop_update(prop, 0, "api_Brush_icon_update");

  prop = api_def_prop(sapi, "icon_filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "icon_filepath");
  api_def_prop_ui_text(prop, "Brush Icon Filepath", "File path to brush icon");
  api_def_prop_update(prop, 0, "api_Brush_icon_update");

  /* clone tool */
  prop = api_def_prop(sapi, "clone_image", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "clone.image");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Clone Image", "Image for clone tool");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, "api_Brush_update");
  api_def_prop_ptr_fns(prop, NULL, NULL, NULL, "api_Brush_imagetype_poll");

  prop = api_def_prop(sapi, "clone_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "clone.alpha");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Clone Alpha", "Opacity of clone image display");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, "api_Brush_update");

  prop = api_def_prop(sapi, "clone_offset", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "clone.offset");
  api_def_prop_ui_text(prop, "Clone Offset", "");
  api_def_prop_ui_range(prop, -1.0f, 1.0f, 10.0f, 3);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, "api_Brush_update");

  prop = api_def_prop(sapi, "brush_capabilities", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "BrushCapabilities");
  api_def_prop_ptr_fns(prop, "api_Brush_capabilities_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Brush Capabilities", "Brush's capabilities");

  /* brush capabilities (mode-dependent) */
  prop = api_def_prop(sapi, "sculpt_capabilities", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "BrushCapabilitiesSculpt");
  api_def_prop_ptr_fns(prop, "api_Sculpt_tool_capabilities_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Sculpt Capabilities", "");

  prop = RNA_def_property(srna, "image_paint_capabilities", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_struct_type(prop, "BrushCapabilitiesImagePaint");
  RNA_def_property_pointer_funcs(prop, "api_Imapaint_tool_capabilities_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Image Paint Capabilities", "");

  prop = RNA_def_property(srna, "vertex_paint_capabilities", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_struct_type(prop, "BrushCapabilitiesVertexPaint");
  RNA_def_property_pointer_funcs(prop, "rna_Vertexpaint_tool_capabilities_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Vertex Paint Capabilities", "");

  prop = RNA_def_property(srna, "weight_paint_capabilities", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_struct_type(prop, "BrushCapabilitiesWeightPaint");
  RNA_def_property_pointer_funcs(prop, "rna_Weightpaint_tool_capabilities_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Weight Paint Capabilities", "");

  prop = RNA_def_property(srna, "gpencil_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "BrushGpencilSettings");
  RNA_def_property_pointer_sdna(prop, NULL, "gpencil_settings");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Gpencil Settings", "");

  prop = RNA_def_property(srna, "curves_sculpt_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "BrushCurvesSculptSettings");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Curves Sculpt Settings", "");
}

/**
 * A brush stroke is a list of changes to the brush that
 * can occur during a stroke
 *
 * - 3D location of the brush
 * - 2D mouse location
 * - Tablet pressure
 * - Direction flip
 * - Tool switch
 * - Time
 */
static void rna_def_operator_stroke_element(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "OperatorStrokeElement", "PropertyGroup");
  RNA_def_struct_ui_text(srna, "Operator Stroke Element", "");

  prop = RNA_def_property(srna, "location", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Location", "");

  prop = RNA_def_property(srna, "mouse", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 2);
  RNA_def_property_ui_text(prop, "Mouse", "");

  prop = RNA_def_property(srna, "mouse_event", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 2);
  RNA_def_property_ui_text(prop, "Mouse Event", "");

  prop = RNA_def_property(srna, "pressure", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Pressure", "Tablet pressure");

  prop = RNA_def_property(srna, "size", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_range(prop, 0.0f, FLT_MAX);
  RNA_def_property_ui_text(prop, "Brush Size", "Brush size in screen space");

  prop = RNA_def_property(srna, "pen_flip", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_ui_text(prop, "Flip", "");

  prop = RNA_def_property(srna, "x_tilt", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Tilt X", "");

  prop = RNA_def_property(srna, "y_tilt", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Tilt Y", "");

  /* used in uv painting */
  prop = RNA_def_property(srna, "time", PROP_FLOAT, PROP_UNSIGNED);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_ui_text(prop, "Time", "");

  /* used for Grease Pencil sketching sessions */
  prop = RNA_def_property(srna, "is_start", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_ui_text(prop, "Is Stroke Start", "");

  /* XXX: Tool (this will be for pressing a modifier key for a different brush,
   *      e.g. switching to a Smooth brush in the middle of the stroke */

  /* XXX: i don't think blender currently supports the ability to properly do a remappable modifier
   *      in the middle of a stroke */
}

void RNA_def_brush(BlenderRNA *brna)
{
  rna_def_brush(brna);
  rna_def_brush_capabilities(brna);
  rna_def_sculpt_capabilities(brna);
  rna_def_image_paint_capabilities(brna);
  rna_def_vertex_paint_capabilities(brna);
  rna_def_weight_paint_capabilities(brna);
  rna_def_gpencil_options(brna);
  rna_def_curves_sculpt_options(brna);
  rna_def_brush_texture_slot(brna);
  rna_def_operator_stroke_element(brna);
}

#endif
