#include <stdlib.h>

#include "types_brush.h"
#include "types_collection.h"
#include "types_pen_legacy.h"
#include "types_layer.h"
#include "types_linestyle.h"
#include "types_mod.h"
#include "types_particle.h"
#include "types_rigidbody.h"
#include "types_scene.h"
#include "types_screen.h" /* TransformOrientation */
#include "types_userdef.h"
#include "types_view3d.h"
#include "types_world.h"

#include "imbuf_colormanagement.h"
#include "imbuf_types.h"

#include "lib_list.h"
#include "lib_math.h"

#include "lang_translation.h"

#include "dune_armature.h"
#include "dune_editmesh.h"
#include "dune_idtype.h"
#include "dune_paint.h"
#include "dune_volume.h"

#include "ed_pen_legacy.h"
#include "ed_object.h"
#include "ed_uvedit.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

/* Include for Bake Options */
#include "render_engine.h"
#include "render_pipeline.h"

#ifdef WITH_FFMPEG
#  include "dune_writeffmpeg.h"
#  include "ffmpeg_compat.h"
#  include <libavcodec/avcodec.h>
#  include <libavformat/avformat.h>
#endif

#include "ed_render.h"
#include "ed_transform.h"

#include "wm_api.h"
#include "wm_types.h"

#include "lib_threads.h"

#include "graph.h"

#ifdef WITH_OPENEXR
const EnumPropItem api_enum_exr_codec_items[] = {
    {R_IMF_EXR_CODEC_NONE, "NONE", 0, "None", ""},
    {R_IMF_EXR_CODEC_PXR24, "PXR24", 0, "Pxr24 (lossy)", ""},
    {R_IMF_EXR_CODEC_ZIP, "ZIP", 0, "ZIP (lossless)", ""},
    {R_IMF_EXR_CODEC_PIZ, "PIZ", 0, "PIZ (lossless)", ""},
    {R_IMF_EXR_CODEC_RLE, "RLE", 0, "RLE (lossless)", ""},
    {R_IMF_EXR_CODEC_ZIPS, "ZIPS", 0, "ZIPS (lossless)", ""},
    {R_IMF_EXR_CODEC_B44, "B44", 0, "B44 (lossy)", ""},
    {R_IMF_EXR_CODEC_B44A, "B44A", 0, "B44A (lossy)", ""},
    {R_IMF_EXR_CODEC_DWAA, "DWAA", 0, "DWAA (lossy)", ""},
    {R_IMF_EXR_CODEC_DWAB, "DWAB", 0, "DWAB (lossy)", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifndef API_RUNTIME
static const EnumPropItem uv_sculpt_relaxation_items[] = {
    {UV_SCULPT_TOOL_RELAX_LAPLACIAN,
     "LAPLACIAN",
     0,
     "Laplacian",
     "Use Laplacian method for relaxation"},
    {UV_SCULPT_TOOL_RELAX_HC, "HC", 0, "HC", "Use HC method for relaxation"},
    {UV_SCULPT_TOOL_RELAX_COTAN,
     "COTAN",
     0,
     "Geometry",
     "Use Geometry (cotangent) relaxation, making UVs follow the underlying 3D geometry"},
    {0, NULL, 0, NULL, NULL},
};
#endif

const EnumPropItem api_enum_snap_source_items[] = {
    {SCE_SNAP_SOURCE_CLOSEST, "CLOSEST", 0, "Closest", "Snap closest point onto target"},
    {SCE_SNAP_SOURCE_CENTER, "CENTER", 0, "Center", "Snap transformation center onto target"},
    {SCE_SNAP_SOURCE_MEDIAN, "MEDIAN", 0, "Median", "Snap median onto target"},
    {SCE_SNAP_SOURCE_ACTIVE, "ACTIVE", 0, "Active", "Snap active onto target"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_proportional_falloff_items[] = {
    {PROP_SMOOTH, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", "Smooth falloff"},
    {PROP_SPHERE, "SPHERE", ICON_SPHERECURVE, "Sphere", "Spherical falloff"},
    {PROP_ROOT, "ROOT", ICON_ROOTCURVE, "Root", "Root falloff"},
    {PROP_INVSQUARE,
     "INVERSE_SQUARE",
     ICON_INVERSESQUARECURVE,
     "Inverse Square",
     "Inverse Square falloff"},
    {PROP_SHARP, "SHARP", ICON_SHARPCURVE, "Sharp", "Sharp falloff"},
    {PROP_LIN, "LINEAR", ICON_LINCURVE, "Linear", "Linear falloff"},
    {PROP_CONST, "CONSTANT", ICON_NOCURVE, "Constant", "Constant falloff"},
    {PROP_RANDOM, "RANDOM", ICON_RNDCURVE, "Random", "Random falloff"},
    {0, NULL, 0, NULL, NULL},
};

/* subset of the enum - only curves, missing random and const */
const EnumPropItem api_enum_proportional_falloff_curve_only_items[] = {
    {PROP_SMOOTH, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", "Smooth falloff"},
    {PROP_SPHERE, "SPHERE", ICON_SPHERECURVE, "Sphere", "Spherical falloff"},
    {PROP_ROOT, "ROOT", ICON_ROOTCURVE, "Root", "Root falloff"},
    {PROP_INVSQUARE, "INVERSE_SQUARE", ICON_ROOTCURVE, "Inverse Square", "Inverse Square falloff"},
    {PROP_SHARP, "SHARP", ICON_SHARPCURVE, "Sharp", "Sharp falloff"},
    {PROP_LIN, "LINEAR", ICON_LINCURVE, "Linear", "Linear falloff"},
    {0, NULL, 0, NULL, NULL},
};

/* Keep for operators, not used here. */
const EnumPropItem api_enum_mesh_select_mode_items[] = {
    {SCE_SELECT_VERTEX, "VERT", ICON_VERTEXSEL, "Vertex", "Vertex selection mode"},
    {SCE_SELECT_EDGE, "EDGE", ICON_EDGESEL, "Edge", "Edge selection mode"},
    {SCE_SELECT_FACE, "FACE", ICON_FACESEL, "Face", "Face selection mode"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_mesh_select_mode_uv_items[] = {
    {UV_SELECT_VERTEX, "VERTEX", ICON_UV_VERTEXSEL, "Vertex", "Vertex selection mode"},
    {UV_SELECT_EDGE, "EDGE", ICON_UV_EDGESEL, "Edge", "Edge selection mode"},
    {UV_SELECT_FACE, "FACE", ICON_UV_FACESEL, "Face", "Face selection mode"},
    {UV_SELECT_ISLAND, "ISLAND", ICON_UV_ISLANDSEL, "Island", "Island selection mode"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_snap_element_items[] = {
    {SCE_SNAP_MODE_INCREMENT,
     "INCREMENT",
     ICON_SNAP_INCREMENT,
     "Increment",
     "Snap to increments of grid"},
    {SCE_SNAP_MODE_VERTEX, "VERTEX", ICON_SNAP_VERTEX, "Vertex", "Snap to vertices"},
    {SCE_SNAP_MODE_EDGE, "EDGE", ICON_SNAP_EDGE, "Edge", "Snap to edges"},
    {SCE_SNAP_MODE_FACE_RAYCAST,
     "FACE", /* TODO(@gfxcoder): replace with "FACE_RAYCAST" as "FACE" is not descriptive. */
     ICON_SNAP_FACE,
     "Face Project",
     "Snap by projecting onto faces"},
    {SCE_SNAP_MODE_FACE_NEAREST,
     "FACE_NEAREST",
     ICON_SNAP_FACE_NEAREST,
     "Face Nearest",
     "Snap to nearest point on faces"},
    {SCE_SNAP_MODE_VOLUME, "VOLUME", ICON_SNAP_VOLUME, "Volume", "Snap to volume"},
    {SCE_SNAP_MODE_EDGE_MIDPOINT,
     "EDGE_MIDPOINT",
     ICON_SNAP_MIDPOINT,
     "Edge Center",
     "Snap to the middle of edges"},
    {SCE_SNAP_MODE_EDGE_PERPENDICULAR,
     "EDGE_PERPENDICULAR",
     ICON_SNAP_PERPENDICULAR,
     "Edge Perpendicular",
     "Snap to the nearest point on an edge"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_snap_node_element_items[] = {
    {SCE_SNAP_MODE_GRID, "GRID", ICON_SNAP_GRID, "Grid", "Snap to grid"},
    {SCE_SNAP_MODE_NODE_X, "NODE_X", ICON_NODE_SIDE, "Node X", "Snap to left/right node border"},
    {SCE_SNAP_MODE_NODE_Y, "NODE_Y", ICON_NODE_TOP, "Node Y", "Snap to top/bottom node border"},
    {SCE_SNAP_MODE_NODE_X | SCE_SNAP_MODE_NODE_Y,
     "NODE_XY",
     ICON_NODE_CORNER,
     "Node X / Y",
     "Snap to any node border"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
static const EnumPropItem snap_uv_element_items[] = {
    {SCE_SNAP_MODE_INCREMENT,
     "INCREMENT",
     ICON_SNAP_INCREMENT,
     "Increment",
     "Snap to increments of grid"},
    {SCE_SNAP_MODE_VERTEX, "VERTEX", ICON_SNAP_VERTEX, "Vertex", "Snap to vertices"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_scene_display_aa_methods[] = {
    {SCE_DISPLAY_AA_OFF,
     "OFF",
     0,
     "No Anti-Aliasing",
     "Scene will be rendering without any anti-aliasing"},
    {SCE_DISPLAY_AA_FXAA,
     "FXAA",
     0,
     "Single Pass Anti-Aliasing",
     "Scene will be rendered using a single pass anti-aliasing method (FXAA)"},
    {SCE_DISPLAY_AA_SAMPLES_5,
     "5",
     0,
     "5 Samples",
     "Scene will be rendered using 5 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_8,
     "8",
     0,
     "8 Samples",
     "Scene will be rendered using 8 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_11,
     "11",
     0,
     "11 Samples",
     "Scene will be rendered using 11 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_16,
     "16",
     0,
     "16 Samples",
     "Scene will be rendered using 16 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_32,
     "32",
     0,
     "32 Samples",
     "Scene will be rendered using 32 anti-aliasing samples"},
    {0, NULL, 0, NULL, NULL},
};
#endif

const EnumPropItem api_enum_curve_fit_method_items[] = {
    {CURVE_PAINT_FIT_METHOD_REFIT,
     "REFIT",
     0,
     "Refit",
     "Incrementally refit the curve (high quality)"},
    {CURVE_PAINT_FIT_METHOD_SPLIT,
     "SPLIT",
     0,
     "Split",
     "Split the curve until the tolerance is met (fast)"},
    {0, NULL, 0, NULL, NULL},
};

/* workaround for duplicate enums,
 * have each enum line as a define then conditionally set it or no */

#define R_IMF_ENUM_BMP \
  {R_IMF_IMTYPE_BMP, "BMP", ICON_FILE_IMAGE, "BMP", "Output image in bitmap format"},
#define R_IMF_ENUM_IRIS \
  {R_IMF_IMTYPE_IRIS, "IRIS", ICON_FILE_IMAGE, "Iris", "Output image in SGI IRIS format"},
#define R_IMF_ENUM_PNG \
  {R_IMF_IMTYPE_PNG, "PNG", ICON_FILE_IMAGE, "PNG", "Output image in PNG format"},
#define R_IMF_ENUM_JPEG \
  {R_IMF_IMTYPE_JPEG90, "JPEG", ICON_FILE_IMAGE, "JPEG", "Output image in JPEG format"},
#define R_IMF_ENUM_TAGA \
  {R_IMF_IMTYPE_TARGA, "TARGA", ICON_FILE_IMAGE, "Targa", "Output image in Targa format"},
#define R_IMF_ENUM_TAGA_RAW \
  {R_IMF_IMTYPE_RAWTGA, \
   "TARGA_RAW", \
   ICON_FILE_IMAGE, \
   "Targa Raw", \
   "Output image in uncompressed Targa format"},

#if 0 /* UNUSED (so far) */
#  define R_IMF_ENUM_DDS \
    {R_IMF_IMTYPE_DDS, "DDS", ICON_FILE_IMAGE, "DDS", "Output image in DDS format"},
#endif

#ifdef WITH_OPENJPEG
#  define R_IMF_ENUM_JPEG2K \
    {R_IMF_IMTYPE_JP2, \
     "JPEG2000", \
     ICON_FILE_IMAGE, \
     "JPEG 2000", \
     "Output image in JPEG 2000 format"},
#else
#  define R_IMF_ENUM_JPEG2K
#endif

#ifdef WITH_CINEON
#  define R_IMF_ENUM_CINEON \
    {R_IMF_IMTYPE_CINEON, "CINEON", ICON_FILE_IMAGE, "Cineon", "Output image in Cineon format"},
#  define R_IMF_ENUM_DPX \
    {R_IMF_IMTYPE_DPX, "DPX", ICON_FILE_IMAGE, "DPX", "Output image in DPX format"},
#else
#  define R_IMF_ENUM_CINEON
#  define R_IMF_ENUM_DPX
#endif

#ifdef WITH_OPENEXR
#  define R_IMF_ENUM_EXR_MULTILAYER \
    {R_IMF_IMTYPE_MULTILAYER, \
     "OPEN_EXR_MULTILAYER", \
     ICON_FILE_IMAGE, \
     "OpenEXR MultiLayer", \
     "Output image in multilayer OpenEXR format"},
#  define R_IMF_ENUM_EXR \
    {R_IMF_IMTYPE_OPENEXR, \
     "OPEN_EXR", \
     ICON_FILE_IMAGE, \
     "OpenEXR", \
     "Output image in OpenEXR format"},
#else
#  define R_IMF_ENUM_EXR_MULTILAYER
#  define R_IMF_ENUM_EXR
#endif

#define R_IMF_ENUM_HDR \
  {R_IMF_IMTYPE_RADHDR, \
   "HDR", \
   ICON_FILE_IMAGE, \
   "Radiance HDR", \
   "Output image in Radiance HDR format"},

#define R_IMF_ENUM_TIFF \
  {R_IMF_IMTYPE_TIFF, "TIFF", ICON_FILE_IMAGE, "TIFF", "Output image in TIFF format"},

#ifdef WITH_WEBP
#  define R_IMF_ENUM_WEBP \
    {R_IMF_IMTYPE_WEBP, "WEBP", ICON_FILE_IMAGE, "WebP", "Output image in WebP format"},
#else
#  define R_IMF_ENUM_WEBP
#endif

#define IMAGE_TYPE_ITEMS_IMAGE_ONLY \
  R_IMF_ENUM_BMP \
  /* DDS save not supported yet R_IMF_ENUM_DDS */ \
  R_IMF_ENUM_IRIS \
  R_IMF_ENUM_PNG \
  R_IMF_ENUM_JPEG \
  R_IMF_ENUM_JPEG2K \
  R_IMF_ENUM_TAGA \
  R_IMF_ENUM_TAGA_RAW \
  API_ENUM_ITEM_SEPR_COLUMN, R_IMF_ENUM_CINEON R_IMF_ENUM_DPX R_IMF_ENUM_EXR_MULTILAYER \
                                 R_IMF_ENUM_EXR R_IMF_ENUM_HDR R_IMF_ENUM_TIFF R_IMF_ENUM_WEBP

#ifdef API_RUNTIME
static const EnumPropItem image_only_type_items[] = {

    IMAGE_TYPE_ITEMS_IMAGE_ONLY

    {0, NULL, 0, NULL, NULL},
};
#endif

const EnumPropItem api_enum_image_type_items[] = {
    API_ENUM_ITEM_HEADING(N_("Image"), NULL),

    IMAGE_TYPE_ITEMS_IMAGE_ONLY

    API_ENUM_ITEM_HEADING(N_("Movie"), NULL),
    {R_IMF_IMTYPE_AVIJPEG,
     "AVI_JPEG",
     ICON_FILE_MOVIE,
     "AVI JPEG",
     "Output video in AVI JPEG format"},
    {R_IMF_IMTYPE_AVIRAW, "AVI_RAW", ICON_FILE_MOVIE, "AVI Raw", "Output video in AVI Raw format"},
#ifdef WITH_FFMPEG
    {R_IMF_IMTYPE_FFMPEG,
     "FFMPEG",
     ICON_FILE_MOVIE,
     "FFmpeg Video",
     "The most versatile way to output video files"},
#endif
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_image_color_mode_items[] = {
    {R_IMF_PLANES_BW,
     "BW",
     0,
     "BW",
     "Images get saved in 8-bit grayscale (only PNG, JPEG, TGA, TIF)"},
    {R_IMF_PLANES_RGB, "RGB", 0, "RGB", "Images are saved with RGB (color) data"},
    {R_IMF_PLANES_RGBA,
     "RGBA",
     0,
     "RGBA",
     "Images are saved with RGB and Alpha data (if supported)"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME
#  define IMAGE_COLOR_MODE_BW api_enum_image_color_mode_items[0]
#  define IMAGE_COLOR_MODE_RGB api_enum_image_color_mode_items[1]
#  define IMAGE_COLOR_MODE_RGBA api_enum_image_color_mode_items[2]
#endif

const EnumPropItem api_enum_image_color_depth_items[] = {
    /* 1 (monochrome) not used */
    {R_IMF_CHAN_DEPTH_8, "8", 0, "8", "8-bit color channels"},
    {R_IMF_CHAN_DEPTH_10, "10", 0, "10", "10-bit color channels"},
    {R_IMF_CHAN_DEPTH_12, "12", 0, "12", "12-bit color channels"},
    {R_IMF_CHAN_DEPTH_16, "16", 0, "16", "16-bit color channels"},
    /* 24 not used */
    {R_IMF_CHAN_DEPTH_32, "32", 0, "32", "32-bit color channels"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_normal_space_items[] = {
    {R_BAKE_SPACE_OBJECT, "OBJECT", 0, "Object", "Bake the normals in object space"},
    {R_BAKE_SPACE_TANGENT, "TANGENT", 0, "Tangent", "Bake the normals in tangent space"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_normal_swizzle_items[] = {
    {R_BAKE_POSX, "POS_X", 0, "+X", ""},
    {R_BAKE_POSY, "POS_Y", 0, "+Y", ""},
    {R_BAKE_POSZ, "POS_Z", 0, "+Z", ""},
    {R_BAKE_NEGX, "NEG_X", 0, "-X", ""},
    {R_BAKE_NEGY, "NEG_Y", 0, "-Y", ""},
    {R_BAKE_NEGZ, "NEG_Z", 0, "-Z", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_margin_type_items[] = {
    {R_BAKE_ADJACENT_FACES,
     "ADJACENT_FACES",
     0,
     "Adjacent Faces",
     "Use pixels from adjacent faces across UV seams"},
    {R_BAKE_EXTEND, "EXTEND", 0, "Extend", "Extend border pixels outwards"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_target_items[] = {
    {R_BAKE_TARGET_IMAGE_TEXTURES,
     "IMAGE_TEXTURES",
     0,
     "Image Textures",
     "Bake to image data-blocks associated with active image texture nodes in materials"},
    {R_BAKE_TARGET_VERTEX_COLORS,
     "VERTEX_COLORS",
     0,
     "Active Color Attribute",
     "Bake to the active color attribute on meshes"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_save_mode_items[] = {
    {R_BAKE_SAVE_INTERNAL,
     "INTERNAL",
     0,
     "Internal",
     "Save the baking map in an internal image data-block"},
    {R_BAKE_SAVE_EXTERNAL, "EXTERNAL", 0, "External", "Save the baking map in an external file"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_bake_view_from_items[] = {
    {R_BAKE_VIEW_FROM_ABOVE_SURFACE,
     "ABOVE_SURFACE",
     0,
     "Above Surface",
     "Cast rays from above the surface"},
    {R_BAKE_VIEW_FROM_ACTIVE_CAMERA,
     "ACTIVE_CAMERA",
     0,
     "Active Camera",
     "Use the active camera's position to cast rays"},
    {0, NULL, 0, NULL, NULL},
};

#define R_IMF_VIEWS_ENUM_IND \
  {R_IMF_VIEWS_INDIVIDUAL, \
   "INDIVIDUAL", \
   0, \
   "Individual", \
   "Individual files for each view with the prefix as defined by the scene views"},
#define R_IMF_VIEWS_ENUM_S3D \
  {R_IMF_VIEWS_STEREO_3D, "STEREO_3D", 0, "Stereo 3D", "Single file with an encoded stereo pair"},
#define R_IMF_VIEWS_ENUM_MV \
  {R_IMF_VIEWS_MULTIVIEW, "MULTIVIEW", 0, "Multi-View", "Single file with all the views"},

const EnumPropItem api_enum_views_format_items[] = {
    R_IMF_VIEWS_ENUM_IND R_IMF_VIEWS_ENUM_S3D{0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_views_format_multilayer_items[] = {
    R_IMF_VIEWS_ENUM_IND R_IMF_VIEWS_ENUM_MV{0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_views_format_multiview_items[] = {
    R_IMF_VIEWS_ENUM_IND R_IMF_VIEWS_ENUM_S3D R_IMF_VIEWS_ENUM_MV{0, NULL, 0, NULL, NULL},
};

#undef R_IMF_VIEWS_ENUM_IND
#undef R_IMF_VIEWS_ENUM_S3D
#undef R_IMF_VIEWS_ENUM_MV

const EnumPropItem api_enum_stereo3d_display_items[] = {
    {S3D_DISPLAY_ANAGLYPH,
     "ANAGLYPH",
     0,
     "Anaglyph",
     "Render views for left and right eyes as two differently filtered colors in a single image "
     "(anaglyph glasses are required)"},
    {S3D_DISPLAY_INTERLACE,
     "INTERLACE",
     0,
     "Interlace",
     "Render views for left and right eyes interlaced in a single image (3D-ready monitor is "
     "required)"},
    {S3D_DISPLAY_PAGEFLIP,
     "TIMESEQUENTIAL",
     0,
     "Time Sequential",
     "Render alternate eyes (also known as page flip, quad buffer support in the graphic card is "
     "required)"},
    {S3D_DISPLAY_SIDEBYSIDE,
     "SIDEBYSIDE",
     0,
     "Side-by-Side",
     "Render views for left and right eyes side-by-side"},
    {S3D_DISPLAY_TOPBOTTOM,
     "TOPBOTTOM",
     0,
     "Top-Bottom",
     "Render views for left and right eyes one above another"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_stereo3d_anaglyph_type_items[] = {
    {S3D_ANAGLYPH_REDCYAN, "RED_CYAN", 0, "Red-Cyan", ""},
    {S3D_ANAGLYPH_GREENMAGENTA, "GREEN_MAGENTA", 0, "Green-Magenta", ""},
    {S3D_ANAGLYPH_YELLOWBLUE, "YELLOW_BLUE", 0, "Yellow-Blue", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_stereo3d_interlace_type_items[] = {
    {S3D_INTERLACE_ROW, "ROW_INTERLEAVED", 0, "Row Interleaved", ""},
    {S3D_INTERLACE_COLUMN, "COLUMN_INTERLEAVED", 0, "Column Interleaved", ""},
    {S3D_INTERLACE_CHECKERBOARD, "CHECKERBOARD_INTERLEAVED", 0, "Checkerboard Interleaved", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_pass_filter_type_items[] = {
    {R_BAKE_PASS_FILTER_NONE, "NONE", 0, "None", ""},
    {R_BAKE_PASS_FILTER_EMIT, "EMIT", 0, "Emit", ""},
    {R_BAKE_PASS_FILTER_DIRECT, "DIRECT", 0, "Direct", ""},
    {R_BAKE_PASS_FILTER_INDIRECT, "INDIRECT", 0, "Indirect", ""},
    {R_BAKE_PASS_FILTER_COLOR, "COLOR", 0, "Color", ""},
    {R_BAKE_PASS_FILTER_DIFFUSE, "DIFFUSE", 0, "Diffuse", ""},
    {R_BAKE_PASS_FILTER_GLOSSY, "GLOSSY", 0, "Glossy", ""},
    {R_BAKE_PASS_FILTER_TRANSM, "TRANSMISSION", 0, "Transmission", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_view_layer_aov_type_items[] = {
    {AOV_TYPE_COLOR, "COLOR", 0, "Color", ""},
    {AOV_TYPE_VALUE, "VALUE", 0, "Value", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_transform_pivot_items_full[] = {
    {V3D_AROUND_CENTER_BOUNDS,
     "BOUNDING_BOX_CENTER",
     ICON_PIVOT_BOUNDBOX,
     "Bounding Box Center",
     "Pivot around bounding box center of selected object(s)"},
    {V3D_AROUND_CURSOR, "CURSOR", ICON_PIVOT_CURSOR, "3D Cursor", "Pivot around the 3D cursor"},
    {V3D_AROUND_LOCAL_ORIGINS,
     "INDIVIDUAL_ORIGINS",
     ICON_PIVOT_INDIVIDUAL,
     "Individual Origins",
     "Pivot around each object's own origin"},
    {V3D_AROUND_CENTER_MEDIAN,
     "MEDIAN_POINT",
     ICON_PIVOT_MEDIAN,
     "Median Point",
     "Pivot around the median point of selected objects"},
    {V3D_AROUND_ACTIVE,
     "ACTIVE_ELEMENT",
     ICON_PIVOT_ACTIVE,
     "Active Element",
     "Pivot around active object"},
    {0, NULL, 0, NULL, NULL},
};

/* Icons could be made a consistent set of images. */
const EnumPropItem api_enum_transform_orientation_items[] = {
    {V3D_ORIENT_GLOBAL,
     "GLOBAL",
     ICON_ORIENTATION_GLOBAL,
     "Global",
     "Align the transformation axes to world space"},
    {V3D_ORIENT_LOCAL,
     "LOCAL",
     ICON_ORIENTATION_LOCAL,
     "Local",
     "Align the transformation axes to the selected objects' local space"},
    {V3D_ORIENT_NORMAL,
     "NORMAL",
     ICON_ORIENTATION_NORMAL,
     "Normal",
     "Align the transformation axes to average normal of selected elements "
     "(bone Y axis for pose mode)"},
    {V3D_ORIENT_GIMBAL,
     "GIMBAL",
     ICON_ORIENTATION_GIMBAL,
     "Gimbal",
     "Align each axis to the Euler rotation axis as used for input"},
    {V3D_ORIENT_VIEW,
     "VIEW",
     ICON_ORIENTATION_VIEW,
     "View",
     "Align the transformation axes to the window"},
    {V3D_ORIENT_CURSOR,
     "CURSOR",
     ICON_ORIENTATION_CURSOR,
     "Cursor",
     "Align the transformation axes to the 3D cursor"},
    {V3D_ORIENT_PARENT,
     "PARENT",
     ICON_BLANK1,
     "Parent",
     "Align the transformation axes to the object's parent space"},
    // {V3D_ORIENT_CUSTOM, "CUSTOM", 0, "Custom", "Use a custom transform orientation"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_string_utils.h"

#  include "type_anim.h"
#  include "types_cachefile.h"
#  include "type_color.h"
#  include "types_mesh.h"
#  include "types_node.h"
#  include "types_object.h"
#  include "types_text.h"
#  include "types_workspace.h"

#  include "api_access.h"

#  include "mem_guardedalloc.h"

#  include "dune_animsys.h"
#  include "dune_brush.h"
#  include "dune_collection.h"
#  include "dune_colortools.h"
#  include "dune_context.h"
#  include "dune_freestyle.h"
#  include "dune_global.h"
#  include "dune_pen_legacy.h"
#  include "dune_idprop.h"
#  include "dune_image.h"
#  include "dune_image_format.h"
#  include "dune_layer.h"
#  include "dune_main.h"
#  include "dune_mesh.h"
#  include "dune_node.h"
#  include "dune_pointcache.h"
#  include "dune_scene.h"
#  include "dune_screen.h"
#  include "dune_unit.h"

#  include "NOD_composite.h"

#  include "ed_image.h"
#  include "ed_info.h"
#  include "ed_keyframing.h"
#  include "ed_mesh.h"
#  include "ed_node.h"
#  include "ed_scene.h"
#  include "ed_view3d.h"

#  include "graph_build.h"
#  include "graph_query.h"

#  include "seq_relations.h"
#  include "seq.h"
#  include "seq_sound.h"

#  ifdef WITH_FREESTYLE
#    include "FRS_freestyle.h"
#  endif

static void api_ToolSettings_snap_mode_set(struct ApiPtr *ptr, int value)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;
  if (value != 0) {
    ts->snap_mode = value;
  }
}

/* Pen update cache */
static void api_Pen_update(Main *UNUSED(main), Scene *scene, ApiPtr *UNUSED(ptr))
{
  if (scene != NULL) {
    ed_pen_tag_scene_pen(scene);
  }
}

static void api_pen_extend_selection(Cxt *C, ApiPtr *UNUSED(ptr))
{
  /* Extend selection to all points in all selected strokes. */
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  dune_view_layer_synced_ensure(scene, view_layer);
  Object *ob = dune_view_layer_active_object_get(view_layer);
  if ((ob) && (ob->type == OB_PEN_LEGACY)) {
    PenData *pd = (PenData *)ob->data;
    CXT_DATA_BEGIN (C, PenStroke *, ps, editable_pen_strokes) {
      if ((ps->flag & PEN_STROKE_SELECT) && (ps->totpoints > 1)) {
        PenPoint *pt;
        for (int i = 0; i < ps->totpoints; i++) {
          pt = &ps->points[i];
          pt->flag |= PEN_SPOINT_SELECT;
        }
      }
    }
    CXT_DATA_END;

    pd->flag |= PEN_DATA_CACHE_IS_DIRTY;
    graph_id_tag_update(&pd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  }
}

static void api_pen_selectmode_update(Cxt *C, ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;
  /* If the mode is not Stroke, don't extend selection. */
  if ((ts->pen_selectmode_edit & PEN_SELECTMODE_STROKE) == 0) {
    return;
  }

  api_pen_extend_selection(C, ptr);
}

static void api_pen_mask_point_update(Cxt *UNUSED(C), ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_STROKE;
  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_SEGMENT;
}

static void api_pen_mask_stroke_update(Cxt *C, ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_POINT;
  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_SEGMENT;

  api_pen_extend_selection(C, ptr);
}

static void api_pen_mask_segment_update(bContext *UNUSED(C), PointerRNA *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_POINT;
  ts->pen_selectmode_sculpt &= ~PEN_SCULPT_MASK_SELECTMODE_STROKE;
}

static void api_pen_vertex_mask_point_update(Cxt *C, ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_STROKE;
  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_SEGMENT;

  ed_pen_tag_scene_pen(cxt_data_scene(C));
}

static void api_pen_vertex_mask_stroke_update(Cxt *C, ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_POINT;
  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_SEGMENT;

  api_pen_extend_selection(C, ptr);

  ed_pen_tag_scene_pen(cxt_data_scene(C));
}

static void api_pen_vertex_mask_segment_update(Cxt *C, ApiPtr *ptr)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;

  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_POINT;
  ts->pen_selectmode_vertex &= ~PEN_VERTEX_MASK_SELECTMODE_STROKE;

  ed_pen_tag_scene_pen(cxt_data_scene(C));
}

/* Read-only Iterator of all the scene objects. */
static void apo_Scene_objects_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  iter->internal.custom = mem_callocn(sizeof(LibIter), __func__);

  dune_scene_objects_iter_begin(iter->internal.custom, (void *)scene);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_Scene_objects_next(CollectionPropIter *iter)
{
  dune_scene_objects_iter_next(iter->internal.custom);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_Scene_objects_end(CollectionPropIter *iter)
{
  dune_scene_objects_iter_end(iter->internal.custom);
  mem_freen(iter->internal.custom);
}

static ApiPtr api_Scene_objects_get(CollectionPropIter *iter)
{
  Object *ob = ((LibIter *)iter->internal.custom)->current;
  return api_ptr_inherit_refine(&iter->parent, &ApiObject, ob);
}

/* End of read-only Iterator of all the scene objects. */
static void api_Scene_set_set(ApiPtr *ptr,
                              ApiPtr value,
                              struct ReportList *UNUSED(reports))
{
  Scene *scene = (Scene *)ptr->data;
  Scene *set = (Scene *)value.data;
  Scene *nested_set;

  for (nested_set = set; nested_set; nested_set = nested_set->set) {
    if (nested_set == scene) {
      return;
    }
    /* prevent eternal loops, set can point to next, and next to set, without problems usually */
    if (nested_set->set == set) {
      return;
    }
  }

  id_lib_extern((Id *)set);
  scene->set = set;
}

void api_Scene_set_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  graph_relations_tag_update(main);
  graph_id_tag_update_ex(main, &scene->id, ID_RECALC_BASE_FLAGS);
  if (scene->set != NULL) {
    /* Objects which are pulled into main scene's graph needs to have
     * their base flags updated.
     */
    graph_id_tag_update_ex(main, &scene->set->id, ID_RECALC_BASE_FLAGS);
  }
}

static void api_Scene_camera_update(Main *main, Scene *UNUSED(scene_unused), ApiPtr *ptr)
{
  WindowManager *wm = main->wm.first;
  Scene *scene = (Scene *)ptr->data;

  wm_windows_scene_data_sync(&wm->windows, scene);
  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(main);
}

static void api_Scene_fps_update(Main *main, Scene *UNUSED(active_scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  graph_id_tag_update(&scene->id, ID_RECALC_AUDIO_FPS | ID_RECALC_SEQ_STRIPS);
  /* NOTE: Tag via dependency graph will take care of all the updates ion the evaluated domain,
   * however, changes in FPS actually modifies an original skip length,
   * so this we take care about here. */
  seq_sound_update_length(main, scene);
}

static void api_Scene_listener_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  graph_id_tag_update(ptr->owner_id, ID_RECALC_AUDIO_LISTENER);
}

static void api_Scene_volume_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  graph_id_tag_update(&scene->id, ID_RECALC_AUDIO_VOLUME | ID_RECALC_SEQ_STRIPS);
}

static const char *api_Scene_statistics_string_get(Scene *scene,
                                                   Main *main,
                                                   ReportList *reports,
                                                   ViewLayer *view_layer)
{
  if (!dune_scene_has_view_layer(scene, view_layer)) {
    dune_reportf(reports,
                RPT_ERROR,
                "View Layer '%s' not found in scene '%s'",
                view_layer->name,
                scene->id.name + 2);
    return "";
  }

  return ed_info_statistics_string(main, scene, view_layer);
}

static void api_Scene_framelen_update(Main *UNUSED(main),
                                      Scene *UNUSED(active_scene),
                                      ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  scene->r.framelen = (float)scene->r.framapto / (float)scene->r.images;
}

static void api_Scene_frame_current_set(ApiPtr *ptr, int value)
{
  Scene *data = (Scene *)ptr->data;

  /* if negative frames aren't allowed, then we can't use them */
  FRAMENUMBER_MIN_CLAMP(value);
  data->r.cfra = value;
}

static float api_Scene_frame_float_get(ApiPtr *ptr)
{
  Scene *data = (Scene *)ptr->data;
  return (float)data->r.cfra + data->r.subframe;
}

static void api_Scene_frame_float_set(ApiPtr *ptr, float value)
{
  Scene *data = (Scene *)ptr->data;
  /* if negative frames aren't allowed, then we can't use them */
  FRAMENUMBER_MIN_CLAMP(value);
  data->r.cfra = (int)value;
  data->r.subframe = value - data->r.cfra;
}

static float api_Scene_frame_current_final_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;

  return dune_scene_frame_to_ctime(scene, (float)scene->r.cfra);
}

static void api_Scene_start_frame_set(ApiPtr *ptr, int value)
{
  Scene *data = (Scene *)ptr->data;
  /* MINFRAME not MINAFRAME, since some output formats can't taken negative frames */
  CLAMP(value, MINFRAME, MAXFRAME);
  data->r.sfra = value;

  if (value > data->r.efra) {
    data->r.efra = MIN2(value, MAXFRAME);
  }
}

static void api_Scene_end_frame_set(ApiPtr *ptr, int value)
{
  Scene *data = (Scene *)ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  data->r.efra = value;

  if (data->r.sfra > value) {
    data->r.sfra = MAX2(value, MINFRAME);
  }
}

static void api_Scene_use_preview_range_set(ApiPtr *ptr, bool value)
{
  Scene *data = (Scene *)ptr->data;

  if (value) {
    /* copy range from scene if not set before */
    if ((data->r.psfra == data->r.pefra) && (data->r.psfra == 0)) {
      data->r.psfra = data->r.sfra;
      data->r.pefra = data->r.efra;
    }

    data->r.flag |= SCER_PRV_RANGE;
  }
  else {
    data->r.flag &= ~SCER_PRV_RANGE;
  }
}

static void api_Scene_preview_range_start_frame_set(ApiPtr *ptr, int value)
{
  Scene *data = (Scene *)ptr->data;

  /* check if enabled already */
  if ((data->r.flag & SCER_PRV_RANGE) == 0) {
    /* set end of preview range to end frame, then clamp as per normal */
    /* TODO: or just refuse to set instead? */
    data->r.pefra = data->r.efra;
  }
  CLAMP(value, MINAFRAME, MAXFRAME);
  data->r.psfra = value;

  if (value > data->r.pefra) {
    data->r.pefra = MIN2(value, MAXFRAME);
  }
}

static void api_Scene_preview_range_end_frame_set(ApiPtr *ptr, int value)
{
  Scene *data = (Scene *)ptr->data;

  /* check if enabled already */
  if ((data->r.flag & SCER_PRV_RANGE) == 0) {
    /* set start of preview range to start frame, then clamp as per normal */
    /* TODO: or just refuse to set instead? */
    data->r.psfra = data->r.sfra;
  }
  CLAMP(value, MINAFRAME, MAXFRAME);
  data->r.pefra = value;

  if (data->r.psfra > value) {
    data->r.psfra = MAX2(value, MINAFRAME);
  }
}

static void api_Scene_show_subframe_update(Main *UNUSED(main),
                                           Scene *UNUSED(current_scene),
                                           ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  scene->r.subframe = 0.0f;
}

static void api_Scene_frame_update(Main *UNUSED(main),
                                   Scene *UNUSED(current_scene),
                                   ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  graph_id_tag_update(&scene->id, ID_RECALC_FRAME_CHANGE);
  wm_main_add_notifier(NC_SCENE | ND_FRAME, scene);
}

static ApiPtr api_Scene_active_keying_set_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  return api_pr_inherit_refine(ptr, &ApiKeyingSet, ANIM_scene_get_active_keyingset(scene));
}

static void api_Scene_active_keying_set_set(ApiPtr *ptr,
                                            ApiPtr value,
                                            struct ReportList *UNUSED(reports))
{
  Scene *scene = (Scene *)ptr->data;
  KeyingSet *ks = (KeyingSet *)value.data;

  scene->active_keyingset = anim_scene_get_keyingset_index(scene, ks);
}

/* get KeyingSet index stuff for list of Keying Sets editing UI
 * - active_keyingset-1 since 0 is reserved for 'none'
 * - don't clamp, otherwise can never set builtin's types as active... */
static int api_Scene_active_keying_set_index_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  return scene->active_keyingset - 1;
}

/* get KeyingSet index stuff for list of Keying Sets editing UI
 * - value+1 since 0 is reserved for 'none */
static void api_Scene_active_keying_set_index_set(ApiPtr *ptr, int value)
{
  Scene *scene = (Scene *)ptr->data;
  scene->active_keyingset = value + 1;
}

/* XXX: evil... builtin_keyingsets is defined in keyingsets.c! */
/* TODO: make API function to retrieve this... */
extern List builtin_keyingsets;

static void api_Scene_all_keyingsets_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;

  /* start going over the scene KeyingSets first, while we still have pointer to it
   * but only if we have any Keying Sets to use...
   */
  if (scene->keyingsets.first) {
    api_iter_list_begin(iter, &scene->keyingsets, NULL);
  }
  else {
    api_iter_list_begin(iter, &builtin_keyingsets, NULL);
  }
}

static void api_Scene_all_keyingsets_next(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.list;
  KeyingSet *ks = (KeyingSet *)internal->link;

  /* If we've run out of links in Scene list,
   * jump over to the builtins list unless we're there already. */
  if ((ks->next == NULL) && (ks != builtin_keyingsets.last)) {
    internal->link = (Link *)builtin_keyingsets.first;
  } else {
    internal->link = (Link *)ks->next;
  }

  iter->valid = (internal->link != NULL);
}

static char *api_SceneEEVEE_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("eevee");
}

static char *api_ScenePen_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("pen_settings");
}

static int api_RenderSettings_stereoViews_skip(CollectionPropIter *iter,
                                               void *UNUSED(data))
{
  ListIter *internal = &iter->internal.list;
  SceneRenderView *srv = (SceneRenderView *)internal->link;

  if (STR_ELEM(srv->name, STEREO_LEFT_NAME, STEREO_RIGHT_NAME)) {
    return 0;
  }

  return 1;
};

static void api_RenderSettings_stereoViews_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  api_iter_list_begin(iter, &rd->views, api_RenderSettings_stereoViews_skip);
}

static char *api_RenderSettings_path(const PointerRNA *UNUSED(ptr))
{
  return lib_strdup("render");
}

static char *api_BakeSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("render.bake");
}

static char *api_ImageFormatSettings_path(const ApiPtr *ptr)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;
  Id *id = ptr->owner_id;

  switch (GS(id->name)) {
    case ID_SCE: {
      Scene *scene = (Scene *)id;

      if (&scene->r.im_format == imf) {
        return lib_strdup("render.image_settings");
      } else if (&scene->r.bake.im_format == imf) {
        return lib_strdup("render.bake.image_settings");
      }
      return lib_strdup("..");
    }
    case ID_NT: {
      NodeTree *ntree = (NodeTree *)id;
      Node *node;

      for (node = ntree->nodes.first; node; node = node->next) {
        if (node->type == CMP_NODE_OUTPUT_FILE) {
          if (&((NodeImageMultiFile *)node->storage)->format == imf) {
            char node_name_esc[sizeof(node->name) * 2];
            lib_str_escape(node_name_esc, node->name, sizeof(node_name_esc));
            return lib_sprintfn("nodes[\"%s\"].format", node_name_esc);
          } else {
            NodeSocket *sock;

            for (sock = node->inputs.first; sock; sock = sock->next) {
              NodeImageMultiFileSocket *sockdata = sock->storage;
              if (&sockdata->format == imf) {
                char node_name_esc[sizeof(node->name) * 2];
                lib_str_escape(node_name_esc, node->name, sizeof(node_name_esc));

                char socketdata_path_esc[sizeof(sockdata->path) * 2];
                lib_str_escape(socketdata_path_esc, sockdata->path, sizeof(socketdata_path_esc));

                return lib_sprintfN(
                    "nodes[\"%s\"].file_slots[\"%s\"].format", node_name_esc, socketdata_path_esc);
              }
            }
          }
        }
      }
      return lib_strdup("..");
    }
    default:
      return lib_strdup("..");
  }
}

static int api_RenderSettings_threads_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  return dune_render_num_threads(rd);
}

static int api_RenderSettings_threads_mode_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  int override = lib_system_num_threads_override_get();

  if (override > 0) {
    return R_FIXED_THREADS;
  } else {
    return (rd->mode & R_FIXED_THREADS);
  }
}

static bool api_RenderSettings_is_movie_format_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  return dune_imtype_is_movie(rd->im_format.imtype);
}

static void api_ImageFormatSettings_file_format_set(ApiPtr *ptr, int value)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;
  Id *id = ptr->owner_id;
  imf->imtype = value;

  const bool is_render = (id && GS(id->name) == ID_SCE);
  /* see note below on why this is */
  const char chan_flag = dune_imtype_valid_channels(imf->imtype, true) |
                         (is_render ? IMA_CHAN_FLAG_BW : 0);

  /* ensure depth and color settings match */
  if ((imf->planes == R_IMF_PLANES_BW) && !(chan_flag & IMA_CHAN_FLAG_BW)) {
    imf->planes = R_IMF_PLANES_RGBA;
  }
  if ((imf->planes == R_IMF_PLANES_RGBA) && !(chan_flag & IMA_CHAN_FLAG_RGBA)) {
    imf->planes = R_IMF_PLANES_RGB;
  }

  /* ensure usable depth */
  {
    const int depth_ok = dune_imtype_valid_depths(imf->imtype);
    if ((imf->depth & depth_ok) == 0) {
      /* set first available depth */
      char depth_ls[] = {
          R_IMF_CHAN_DEPTH_32,
          R_IMF_CHAN_DEPTH_24,
          R_IMF_CHAN_DEPTH_16,
          R_IMF_CHAN_DEPTH_12,
          R_IMF_CHAN_DEPTH_10,
          R_IMF_CHAN_DEPTH_8,
          R_IMF_CHAN_DEPTH_1,
          0,
      };
      int i;

      for (i = 0; depth_ls[i]; i++) {
        if (depth_ok & depth_ls[i]) {
          imf->depth = depth_ls[i];
          break;
        }
      }
    }
  }

  if (id && GS(id->name) == ID_SCE) {
    Scene *scene = (Scene *)ptr->owner_id;
    RenderData *rd = &scene->r;
#  ifdef WITH_FFMPEG
    dune_ffmpeg_image_type_verify(rd, imf);
#  endif
    (void)rd;
  }
}

static const EnumPropItem *api_ImageFormatSettings_file_format_itemf(Cxt *UNUSED(C),
                                                                     ApiPtr *ptr,
                                                                     ApiProp *UNUSED(prop),
                                                                     bool *UNUSED(r_free))
{
  Id *id = ptr->owner_id;
  if (id && GS(id->name) == ID_SCE) {
    return api_enum_image_type_items;
  } else {
    return image_only_type_items;
  }
}

static const EnumPropItem *api_ImageFormatSettings_color_mode_itemf(Cxt *UNUSED(C),
                                                                    ApiPtr *ptr,
                                                                    ApiProp *UNUSED(prop),
                                                                    bool *r_free)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;
  Id *id = ptr->owner_id;
  const bool is_render = (id && GS(id->name) == ID_SCE);

  /* NOTE: we need to act differently for render
   * where 'BW' will force grayscale even if the output format writes
   * as RGBA, this is age old blender convention and not sure how useful
   * it really is but keep it for now. */
  char chan_flag = dune_imtype_valid_channels(imf->imtype, true) |
                   (is_render ? IMA_CHAN_FLAG_BW : 0);

#  ifdef WITH_FFMPEG
  /* a WAY more crappy case than B&W flag: depending on codec, file format MIGHT support
   * alpha channel. for example MPEG format with h264 codec can't do alpha channel, but
   * the same MPEG format with QTRLE codec can easily handle alpha channel.
   * not sure how to deal with such cases in a nicer way (sergey) */
  if (is_render) {
    Scene *scene = (Scene *)ptr->owner_id;
    RenderData *rd = &scene->r;

    if (dune_ffmpeg_alpha_channel_is_supported(rd)) {
      chan_flag |= IMA_CHAN_FLAG_RGBA;
    }
  }
#  endif

  if (chan_flag == (IMA_CHAN_FLAG_BW | IMA_CHAN_FLAG_RGB | IMA_CHAN_FLAG_RGBA)) {
    return api_enum_image_color_mode_items;
  } else {
    int totitem = 0;
    EnumPropItem *item = NULL;

    if (chan_flag & IMA_CHAN_FLAG_BW) {
      api_enum_item_add(&item, &totitem, &IMAGE_COLOR_MODE_BW);
    }
    if (chan_flag & IMA_CHAN_FLAG_RGB) {
      api_enum_item_add(&item, &totitem, &IMAGE_COLOR_MODE_RGB);
    }
    if (chan_flag & IMA_CHAN_FLAG_RGBA) {
      api_enum_item_add(&item, &totitem, &IMAGE_COLOR_MODE_RGBA);
    }

    api_enum_item_end(&item, &totitem);
    *r_free = true;

    return item;
  }
}

static const EnumPropItem *api_ImageFormatSettings_color_depth_itemf(Cxt *UNUSED(C),
                                                                     ApiPtr *ptr,
                                                                     ApiProp *UNUSED(prop),
                                                                     bool *r_free)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;

  if (imf == NULL) {
    return api_enum_image_color_depth_items;
  }
  else {
    const int depth_ok = dune_imtype_valid_depths(imf->imtype);
    const int is_float = ELEM(
        imf->imtype, R_IMF_IMTYPE_RADHDR, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER);

    const EnumPropItem *item_8bit = &api_enum_image_color_depth_items[0];
    const EnumPropItem *item_10bit = &api_enum_image_color_depth_items[1];
    const EnumPropItem *item_12bit = &api_enum_image_color_depth_items[2];
    const EnumPropItem *item_16bit = &api_enum_image_color_depth_items[3];
    const EnumPropItem *item_32bit = &api_enum_image_color_depth_items[4];

    int totitem = 0;
    EnumPropItem *item = NULL;
    EnumPropItem tmp = {0, "", 0, "", ""};

    if (depth_ok & R_IMF_CHAN_DEPTH_8) {
      api_enum_item_add(&item, &totitem, item_8bit);
    }

    if (depth_ok & R_IMF_CHAN_DEPTH_10) {
      api_enum_item_add(&item, &totitem, item_10bit);
    }

    if (depth_ok & R_IMF_CHAN_DEPTH_12) {
      api_enum_item_add(&item, &totitem, item_12bit);
    }

    if (depth_ok & R_IMF_CHAN_DEPTH_16) {
      if (is_float) {
        tmp = *item_16bit;
        tmp.name = "Float (Half)";
        api_enum_item_add(&item, &totitem, &tmp);
      } else {
        api_enum_item_add(&item, &totitem, item_16bit);
      }
    }

    if (depth_ok & R_IMF_CHAN_DEPTH_32) {
      if (is_float) {
        tmp = *item_32bit;
        tmp.name = "Float (Full)";
        api_enum_item_add(&item, &totitem, &tmp);
      } else {
        api_enum_item_add(&item, &totitem, item_32bit);
      }
    }

    api_enum_item_end(&item, &totitem);
    *r_free = true;

    return item;
  }
}

static const EnumPropItem *api_ImageFormatSettings_views_format_itemf(
    Cxt *UNUSED(C), ApiPtr *ptr, ApiProp *UNUSED(prop), bool *UNUSED(r_free))
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;

  if (imf == NULL) {
    return api_enum_views_format_items;
  } else if (imf->imtype == R_IMF_IMTYPE_OPENEXR) {
    return api_enum_views_format_multiview_items;
  } else if (imf->imtype == R_IMF_IMTYPE_MULTILAYER) {
    return api_enum_views_format_multilayer_items;
  } else {
    return api_enum_views_format_items;
  }
}

#  ifdef WITH_OPENEXR
/* OpenEXR */

static const EnumPropItem *api_ImageFormatSettings_exr_codec_itemf(Cxt *UNUSED(C),
                                                                   ApiPtr *ptr,
                                                                   ApiProp *UNUSED(prop),
                                                                   bool *r_free)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;

  EnumPropItem *item = NULL;
  int i = 1, totitem = 0;

  if (imf->depth == 16) {
    return rna_enum_exr_codec_items; /* All compression types are defined for half-float. */
  }

  for (i = 0; i < R_IMF_EXR_CODEC_MAX; i++) {
    if (ELEM(i, R_IMF_EXR_CODEC_B44, R_IMF_EXR_CODEC_B44A)) {
      continue; /* B44 and B44A are not defined for 32 bit floats */
    }

    api_enum_item_add(&item, &totitem, &api_enum_exr_codec_items[i]);
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

#  endif

static bool api_ImageFormatSettings_has_linear_colorspace_get(ApiPtr *ptr)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;
  return dune_imtype_requires_linear_float(imf->imtype);
}

static void api_ImageFormatSettings_color_management_set(ApiPtr *ptr, int value)
{
  ImageFormatData *imf = (ImageFormatData *)ptr->data;

  if (imf->color_management != value) {
    imf->color_management = value;

    /* Copy from scene when enabling override. */
    if (imf->color_management == R_IMF_COLOR_MANAGEMENT_OVERRIDE) {
      Id *owner_id = ptr->owner_id;
      if (owner_id && GS(owner_id->name) == ID_NT) {
        /* For compositing nodes, find the corresponding scene. */
        owner_id = dune_id_owner_get(owner_id);
      }
      if (owner_id && GS(owner_id->name) == ID_SCE) {
        dune_image_format_color_management_copy_from_scene(imf, (Scene *)owner_id);
      }
    }
  }
}

static int api_SceneRender_file_ext_length(ApiPtr *ptr)
{
  const RenderData *rd = (RenderData *)ptr->data;
  const char *ext_array[DUNE_IMAGE_PATH_EXT_MAX];
  int ext_num = dune_image_path_ext_from_imformat(&rd->im_format, ext_array);
  return ext_num ? strlen(ext_array[0]) : 0;
}

static void api_SceneRender_file_ext_get(ApiPtr *ptr, char *value)
{
  const RenderData *rd = (RenderData *)ptr->data;
  const char *ext_array[DUNE_IMAGE_PATH_EXT_MAX];
  int ext_num = dune_image_path_ext_from_imformat(&rd->im_format, ext_array);
  strcpy(value, ext_num ? ext_array[0] : "");
}

#  ifdef WITH_FFMPEG
static void api_FFmpegSettings_lossless_output_set(ApiPtr *ptr, bool value)
{
  Scene *scene = (Scene *)ptr->owner_id;
  RenderData *rd = &scene->r;

  if (value) {
    rd->ffcodecdata.flags |= FFMPEG_LOSSLESS_OUTPUT;
  } else {
    rd->ffcodecdata.flags &= ~FFMPEG_LOSSLESS_OUTPUT;
  }
}
#  endif

static int api_RenderSettings_active_view_index_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  return rd->actview;
}

static void api_RenderSettings_active_view_index_set(ApiPtr *ptr, int value)
{
  RenderData *rd = (RenderData *)ptr->data;
  rd->actview = value;
}

static void api_RenderSettings_active_view_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  RenderData *rd = (RenderData *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&rd->views) - 1);
}

static ApiPtr api_RenderSettings_active_view_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  SceneRenderView *srv = lib_findlink(&rd->views, rd->actview);

  return api_ptr_inherit_refine(ptr, &Api_SceneRenderView, srv);
}

static void api_RenderSettings_active_view_set(ApiPtr *ptr,
                                               ApiPtr value,
                                               struct ReportList *UNUSED(reports))
{
  RenderData *rd = (RenderData *)ptr->data;
  SceneRenderView *srv = (SceneRenderView *)value.data;
  const int index = lib_findindex(&rd->views, srv);
  if (index != -1) {
    rd->actview = index;
  }
}

static SceneRenderView *api_RenderView_new(Id *id, RenderData *UNUSED(rd), const char *name)
{
  Scene *scene = (Scene *)id;
  SceneRenderView *srv = dune_scene_add_render_view(scene, name);

  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);

  return srv;
}

static void api_RenderView_remove(
    Id *id, RenderData *UNUSED(rd), Main *UNUSED(bmain), ReportList *reports, PointerRNA *srv_ptr)
{
  SceneRenderView *srv = srv_ptr->data;
  Scene *scene = (Scene *)id;

  if (!dune_scene_remove_render_view(scene, srv)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Render view '%s' could not be removed from scene '%s'",
                srv->name,
                scene->id.name + 2);
    return;
  }

  API_PTR_INVALIDATE(srv_ptr);

  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);
}

static void api_RenderSettings_views_format_set(ApiPtr *ptr, int value)
{
  RenderData *rd = (RenderData *)ptr->data;

  if (rd->views_format == SCE_VIEWS_FORMAT_MULTIVIEW && value == SCE_VIEWS_FORMAT_STEREO_3D) {
    /* make sure the actview is visible */
    if (rd->actview > 1) {
      rd->actview = 1;
    }
  }

  rd->views_format = value;
}

static void api_RenderSettings_engine_set(ApiPtr *ptr, int value)
{
  RenderData *rd = (RenderData *)ptr->data;
  RenderEngineType *type = lib_findlink(&R_engines, value);

  if (type) {
    STRNCPY_UTF8(rd->engine, type->idname);
    graph_id_tag_update(ptr->owner_id, ID_RECALC_COPY_ON_WRITE);
  }
}

static const EnumPropItem *api_RenderSettings_engine_itemf(Cxt *UNUSED(C),
                                                           ApiPtr *UNUSED(ptr),
                                                           ApiProp *UNUSED(prop),
                                                           bool *r_free)
{
  RenderEngineType *type;
  EnumPropItem *item = NULL;
  EnumPropItem tmp = {0, "", 0, "", ""};
  int a = 0, totitem = 0;

  for (type = R_engines.first; type; type = type->next, a++) {
    tmp.value = a;
    tmp.identifier = type->idname;
    tmp.name = type->name;
    api_enum_item_add(&item, &totitem, &tmp);
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int api_RenderSettings_engine_get(ApiPtr *ptr)
{
  RenderData *rd = (RenderData *)ptr->data;
  RenderEngineType *type;
  int a = 0;

  for (type = R_engines.first; type; type = type->next, a++) {
    if (STREQ(type->idname, rd->engine)) {
      return a;
    }
  }

  return 0;
}

static void api_RenderSettings_engine_update(Main *main,
                                             Scene *UNUSED(unused),
                                             ApiPtr *UNUSED(ptr))
{
  ed_render_engine_changed(main, true);
}

static void api_Scene_update_render_engine(Main *main)
{
  ed_render_engine_changed(main, true);
}

static bool api_RenderSettings_multiple_engines_get(ApiPtr *UNUSED(ptr))
{
  return (lib_list_count(&R_engines) > 1);
}

static bool api_RenderSettings_use_spherical_stereo_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  return dune_scene_use_spherical_stereo(scene);
}

void api_Scene_render_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
}

static void api_Scene_world_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Scene *screen = (Scene *)ptr->owner_id;

  api_Scene_render_update(main, scene, ptr);
  wm_main_add_notifier(NC_WORLD | ND_WORLD, &screen->id);
  graph_relations_tag_update(main);
}

static void api_Scene_mesh_quality_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  FOREACH_SCENE_OBJECT_BEGIN (scene, ob) {
    if (ELEM(ob->type, OB_MESH, OB_CURVES_LEGACY, OB_VOLUME, OB_MBALL)) {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }
  FOREACH_SCENE_OBJECT_END;

  api_Scene_render_update(main, scene, ptr);
}

void api_Scene_freestyle_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
}

void api_Scene_use_freestyle_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  if (scene->nodetree) {
    ntreeCompositUpdateRLayers(scene->nodetree);
  }
}

void api_Scene_use_view_map_cache_update(Main *UNUSED(main),
                                         Scene *UNUSED(scene),
                                         ApiPtr *UNUSED(ptr))
{
#  ifdef WITH_FREESTYLE
  FRS_free_view_map_cache();
#  endif
}

void api_ViewLayer_name_set(ApiPtr *ptr, const char *value)
{
  Scene *scene = (Scene *)ptr->owner_id;
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  lib_assert(dune_id_is_in_global_main(&scene->id));
  dune_view_layer_rename(G_MAIN, scene, view_layer, value);
}

static void api_SceneRenderView_name_set(ApiPtr *ptr, const char *value)
{
  Scene *scene = (Scene *)ptr->owner_id;
  SceneRenderView *rv = (SceneRenderView *)ptr->data;
  STRNCPY_UTF8(rv->name, value);
  lib_uniquename(&scene->r.views,
                 rv,
                 DATA_("RenderView"),
                 '.',
                 offsetof(SceneRenderView, name),
                 sizeof(rv->name));
}

void api_ViewLayer_material_override_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  api_Scene_render_update(main, scene, ptr);
  graph_relations_tag_update(main);
}

void api_ViewLayer_pass_update(Main *main, Scene *activescene, ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  if (scene->nodetree) {
    ntreeCompositUpdateRLayers(scene->nodetree);
  }

  ViewLayer *view_layer = NULL;
  if (ptr->type == &ApiViewLayer) {
    view_layer = (ViewLayer *)ptr->data;
  } else if (ptr->type == &Api_AOV) {
    ViewLayerAOV *aov = (ViewLayerAOV *)ptr->data;
    view_layer = dune_view_layer_find_with_aov(scene, aov);
  } else if (ptr->type == &ApiLightgroup) {
    ViewLayerLightgroup *lightgroup = (ViewLayerLightgroup *)ptr->data;
    view_layer = dune_view_layer_find_with_lightgroup(scene, lightgroup);
  }

  if (view_layer) {
    RenderEngineType *engine_type = render_engines_find(scene->r.engine);
    if (engine_type->update_render_passes) {
      RenderEngine *engine = render_engine_create(engine_type);
      if (engine) {
        dune_view_layer_verify_aov(engine, scene, view_layer);
      }
      render_engine_free(engine);
      engine = NULL;
    }
  }

  api_Scene_render_update(main, activescene, ptr);
}

static char *api_ViewLayerEEVEE_path(const ApiPtr *ptr)
{
  const ViewLayerEEVEE *view_layer_eevee = (ViewLayerEEVEE *)ptr->data;
  const ViewLayer *view_layer = (ViewLayer *)((uint8_t *)view_layer_eevee -
                                              offsetof(ViewLayer, eevee));
  char api_path[sizeof(view_layer->name) * 3];

  const size_t view_layer_path_len = api_ViewLayer_path_buffer_get(
      view_layer, api_path, sizeof(api_path));

  lib_strncpy(api_path + view_layer_path_len, ".eevee", sizeof(api_path) - view_layer_path_len);

  return lib_strdup(api_path);
}

static char *api_SceneRenderView_path(const ApiPtr *ptr)
{
  const SceneRenderView *srv = (SceneRenderView *)ptr->data;
  char srv_name_esc[sizeof(srv->name) * 2];
  lib_str_escape(srv_name_esc, srv->name, sizeof(srv_name_esc));
  return lib_sprintfn("render.views[\"%s\"]", srv_name_esc);
}

static void api_Scene_use_nodes_update(Cxt *C, ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  if (scene->use_nodes && scene->nodetree == NULL) {
    ed_node_composit_default(C, scene);
  }
  graph_relations_tag_update(cxt_data_main(C));
}

static void api_Phys_relations_update(Main *main,
                                      Scene *UNUSED(scene),
                                      ApiPtr *UNUSED(ptr))
{
  graph_relations_tag_update(bmain);
}

static void api_Phys_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  FOREACH_SCENE_OBJECT_BEGIN (scene, ob) {
    dune_ptcache_object_reset(scene, ob, PTCACHE_RESET_DEPSGRAPH);
  }
  FOREACH_SCENE_OBJECT_END;

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
}

static void api_Scene_editmesh_select_mode_set(ApiPtr *ptr, const bool *value)
{
  ToolSettings *ts = (ToolSettings *)ptr->data;
  int flag = (value[0] ? SCE_SELECT_VERTEX : 0) | (value[1] ? SCE_SELECT_EDGE : 0) |
             (value[2] ? SCE_SELECT_FACE : 0);

  if (flag) {
    ts->selectmode = flag;

    /* Update select mode in all the workspaces in mesh edit mode. */
    wmWindowManager *wm = G_MAIN->wm.first;
    LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
      const Scene *scene = wm_window_get_active_scene(win);
      ViewLayer *view_layer = wm_window_get_active_view_layer(win);
      if (view_layer) {
        dune_view_layer_synced_ensure(scene, view_layer);
        Object *object = dune_view_layer_active_object_get(view_layer);
        if (object) {
          Mesh *me = dune_mesh_from_object(object);
          if (me && me->edit_mesh && me->edit_mesh->selectmode != flag) {
            me->edit_mesh->selectmode = flag;
            EDBM_selectmode_set(me->edit_mesh);
          }
        }
      }
    }
  }
}

static void api_Scene_editmesh_select_mode_update(Cxt *C, ApiPtr *UNUSED(ptr))
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Mesh *me = NULL;

  dune_view_layer_synced_ensure(scene, view_layer);
  Object *object = dune_view_layer_active_object_get(view_layer);
  if (object) {
    me = dune_mesh_from_object(object);
    if (me && me->edit_mesh == NULL) {
      me = NULL;
    }
  }

  if (me) {
    graph_id_tag_update(&me->id, ID_RECALC_SELECT);
    wm_main_add_notifier(NC_SCENE | ND_TOOLSETTINGS, NULL);
  }
}

static void api_Scene_uv_select_mode_update(Cxt *C, ApiPtr *UNUSED(ptr))
{
  /* Makes sure that the UV selection states are consistent with the current UV select mode and
   * sticky mode. */
  ed_uvedit_selectmode_clean_multi(C);
}

static void object_simplify_update(Object *ob)
{
  ModifierData *md;
  ParticleSystem *psys;

  if ((ob->id.tag & LIB_TAG_DOIT) == 0) {
    return;
  }

  ob->id.tag &= ~LIB_TAG_DOIT;

  for (md = ob->mods.first; md; md = md->next) {
    if (ELEM(
      md->type, eModType_Subsurf, eModType_Multires, eModType_ParticleSystem))
    {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }

  for (psys = ob->particlesystem.first; psys; psys = psys->next) {
    psys->recalc |= ID_RECALC_PSYS_CHILD;
  }

  if (ob->instance_collection) {
    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (ob->instance_collection, ob_collection) {
      object_simplify_update(ob_collection);
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
  }

  if (ob->type == OB_VOLUME) {
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  }
}

static void api_Scene_use_simplify_update(Main *main, Scene *UNUSED(scene), PointerRNA *ptr)
{
  Scene *sce = (Scene *)ptr->owner_id;
  Scene *sce_iter;
  Base *base;

  dune_main_id_tag_listbase(&main->objects, LIB_TAG_DOIT, true);
  FOREACH_SCENE_OBJECT_BEGIN (sce, ob) {
    object_simplify_update(ob);
  }
  FOREACH_SCENE_OBJECT_END;

  for (SETLOOPER_SET_ONLY(sce, sce_iter, base)) {
    object_simplify_update(base->object);
  }

  wm_main_add_notifier(NC_GEOM | ND_DATA, NULL);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
  graph_id_tag_update(&sce->id, ID_RECALC_COPY_ON_WRITE);
}

static void api_Scene_simplify_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Scene *sce = (Scene *)ptr->owner_id;

  if (sce->r.mode & R_SIMPLIFY) {
    api_Scene_use_simplify_update(main, scene, ptr);
  }
}

static void api_Scene_use_persistent_data_update(Main *UNUSED(main),
                                                 Scene *UNUSED(scene),
                                                 ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;

  if (!(scene->r.mode & R_PERSISTENT_DATA)) {
    render_FreePersistentData(scene);
  }
}

/* Scene.transform_orientation_slots */
static void api_Scene_transform_orientation_slots_begin(CollectionPropIter *iter,
                                                        ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  TransformOrientationSlot *orient_slot = &scene->orientation_slots[0];
  api_iter_array_begin(
      iter, orient_slot, sizeof(*orient_slot), ARRAY_SIZE(scene->orientation_slots), 0, NULL);
}

static int api_Scene_transform_orientation_slots_length(ApiPtr *UNUSED(ptr))
{
  return ARRAY_SIZE(((Scene *)NULL)->orientation_slots);
}

static bool api_Scene_use_audio_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  return (scene->audio.flag & AUDIO_MUTE) != 0;
}

static void api_Scene_use_audio_set(ApiPtr *ptr, bool value)
{
  Scene *scene = (Scene *)ptr->data;

  if (value) {
    scene->audio.flag |= AUDIO_MUTE;
  } else {
    scene->audio.flag &= ~AUDIO_MUTE;
  }
}

static void api_Scene_use_audio_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  graph_id_tag_update(ptr->owner_id, ID_RECALC_AUDIO_MUTE);
}

static int api_Scene_sync_mode_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->data;
  if (scene->audio.flag & AUDIO_SYNC) {
    return AUDIO_SYNC;
  }
  return scene->flag & SCE_FRAME_DROP;
}

static void api_Scene_sync_mode_set(ApiPtr *ptr, int value)
{
  Scene *scene = (Scene *)ptr->data;

  if (value == AUDIO_SYNC) {
    scene->audio.flag |= AUDIO_SYNC;
  } else if (value == SCE_FRAME_DROP) {
    scene->audio.flag &= ~AUDIO_SYNC;
    scene->flag |= SCE_FRAME_DROP;
  } else {
    scene->audio.flag &= ~AUDIO_SYNC;
    scene->flag &= ~SCE_FRAME_DROP;
  }
}

static void api_View3DCursor_rotation_mode_set(ApiPtr *ptr, int value)
{
  View3DCursor *cursor = ptr->data;

  /* use API Method for conversions... */
  dune_rotMode_change_values(cursor->rotation_quaternion,
                            cursor->rotation_euler,
                            cursor->rotation_axis,
                            &cursor->rotation_angle,
                            cursor->rotation_mode,
                            (short)value);

  /* finally, set the new rotation type */
  cursor->rotation_mode = value;
}

static void api_View3DCursor_rotation_axis_angle_get(ApiPtr *ptr, float *value)
{
  View3DCursor *cursor = ptr->data;
  value[0] = cursor->rotation_angle;
  copy_v3_v3(&value[1], cursor->rotation_axis);
}

static void api_View3DCursor_rotation_axis_angle_set(ApiPtr *ptr, const float *value)
{
  View3DCursor *cursor = ptr->data;
  cursor->rotation_angle = value[0];
  copy_v3_v3(cursor->rotation_axis, &value[1]);
}

static void api_View3DCursor_matrix_get(ApiPtr *ptr, float *values)
{
  const View3DCursor *cursor = ptr->data;
  dune_scene_cursor_to_mat4(cursor, (float(*)[4])values);
}

static void api_View3DCursor_matrix_set(ApiPtr *ptr, const float *values)
{
  View3DCursor *cursor = ptr->data;
  float unit_mat[4][4];
  normalize_m4_m4(unit_mat, (const float(*)[4])values);
  dune_scene_cursor_from_mat4(cursor, unit_mat, false);
}

static char *api_TransformOrientationSlot_path(const ApiPtr *ptr)
{
  const Scene *scene = (Scene *)ptr->owner_id;
  const TransformOrientationSlot *orientation_slot = ptr->data;

  if (!ELEM(NULL, scene, orientation_slot)) {
    for (int i = 0; i < ARRAY_SIZE(scene->orientation_slots); i++) {
      if (&scene->orientation_slots[i] == orientation_slot) {
        return lib_sprintfn("transform_orientation_slots[%d]", i);
      }
    }
  }

  /* Should not happen, but in case, just return default path. */
  lib_assert_unreachable();
  return lib_strdup("transform_orientation_slots[0]");
}

static char *api_View3DCursor_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("cursor");
}

static TimeMarker *api_TimeLine_add(Scene *scene, const char name[], int frame)
{
  TimeMarker *marker = mem_callocn(sizeof(TimeMarker), "TimeMarker");
  marker->flag = SELECT;
  marker->frame = frame;
  STRNCPY_UTF8(marker->name, name);
  lib_addtail(&scene->markers, marker);

  wm_main_add_notifier(NC_SCENE | ND_MARKERS, NULL);
  wm_main_add_notifier(NC_ANIMATION | ND_MARKERS, NULL);

  return marker;
}

static void api_TimeLine_remove(Scene *scene, ReportList *reports, ApiPtr *marker_ptr)
{
  TimeMarker *marker = marker_ptr->data;
  if (lib_remlink_safe(&scene->markers, marker) == false) {
    dune_reportf(reports,
                RPT_ERROR,
                "Timeline marker '%s' not found in scene '%s'",
                marker->name,
                scene->id.name + 2);
    return;
  }

  mem_freen(marker);
  API_PTR_INVALIDATE(marker_ptr);

  wm_main_add_notifier(NC_SCENE | ND_MARKERS, NULL);
  wm_main_add_notifier(NC_ANIMATION | ND_MARKERS, NULL);
}

static void api_TimeLine_clear(Scene *scene)
{
  lib_freelistn(&scene->markers);

  wm_main_add_notifier(NC_SCENE | ND_MARKERS, NULL);
  wm_main_add_notifier(NC_ANIMATION | ND_MARKERS, NULL);
}

static KeyingSet *api_Scene_keying_set_new(Scene *sce,
                                           ReportList *reports,
                                           const char idname[],
                                           const char name[])
{
  KeyingSet *ks = NULL;

  /* call the API func, and set the active keyingset index */
  ks = dune_keyingset_add(&sce->keyingsets, idname, name, KEYINGSET_ABSOLUTE, 0);

  if (ks) {
    sce->active_keyingset = lib_list_count(&sce->keyingsets);
    return ks;
  } else {
    dune_report(reports, RPT_ERROR, "Keying set could not be added");
    return NULL;
  }
}

static void api_UnifiedPaintSettings_update(Cxt *C, ApiPtr *UNUSED(ptr))
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Brush *br = dune_paint_brush(dune_paint_get_active(scene, view_layer));
  wm_main_add_notifier(NC_BRUSH | NA_EDITED, br);
}

static void api_UnifiedPaintSettings_size_set(ApiPtr *ptr, int value)
{
  UnifiedPaintSettings *ups = ptr->data;

  /* scale unprojected radius so it stays consistent with brush size */
  dune_brush_scale_unprojected_radius(&ups->unprojected_radius, value, ups->size);
  ups->size = value;
}

static void api_UnifiedPaintSettings_unprojected_radius_set(ApiPtr *ptr, float value)
{
  UnifiedPaintSettings *ups = ptr->data;

  /* scale brush size so it stays consistent with unprojected_radius */
  dune_brush_scale_size(&ups->size, value, ups->unprojected_radius);
  ups->unprojected_radius = value;
}

static void api_UnifiedPaintSettings_radius_update(Cxt *C, PointerRNA *ptr)
{
  /* changing the unified size should invalidate the overlay but also update the brush */
  dune_paint_invalidate_overlay_all();
  api_UnifiedPaintSettings_update(C, ptr);
}

static char *api_UnifiedPaintSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings.unified_paint_settings");
}

static char *api_CurvePaintSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings.curve_paint_settings");
}

static char *api_SeqToolSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings.seq_tool_settings");
}

/* generic function to recalc geometry */
static void api_EditMesh_update(Cxt *C, ApiPtr *UNUSED(ptr))
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Mesh *me = NULL;

  dune_view_layer_synced_ensure(scene, view_layer);
  Object *object = dune_view_layer_active_object_get(view_layer);
  if (object) {
    me = dune_mesh_from_object(object);
    if (me && me->edit_mesh == NULL) {
      me = NULL;
    }
  }

  if (me) {
    graph_id_tag_update(&me->id, ID_RECALC_GEOMETRY);
    wm_main_add_notifier(NC_GEOM | ND_DATA, me);
  }
}

static char *api_MeshStatVis_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings.statvis");
}

/* NOTE: without this, when Multi-Paint is activated/deactivated, the colors
 * will not change right away when multiple bones are selected, this function
 * is not for general use and only for the few cases where changing scene
 * settings and NOT for general purpose updates, possibly this should be
 * given its own notifier. */
static void api_Scene_update_active_object_data(Cxt *C, ApiPtr *UNUSED(ptr))
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  dune_view_layer_synced_ensure(scene, view_layer);
  Object *ob = dune_view_layer_active_object_get(view_layer);

  if (ob) {
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    wm_main_add_notifier(NC_OBJECT | ND_DRAW, &ob->id);
  }
}

static void api_SceneCamera_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  Object *camera = scene->camera;

  seq_cache_cleanup(scene);

  if (camera && (camera->type == OB_CAMERA)) {
    graph_id_tag_update(&camera->id, ID_RECALC_GEOMETRY);
  }
}

static void api_SceneSeq_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  seq_cache_cleanup((Scene *)ptr->owner_id);
}

static char *api_ToolSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tool_settings");
}

ApiPtr api_FreestyleLineSet_linestyle_get(ApiPtr *ptr)
{
  FreestyleLineSet *lineset = (FreestyleLineSet *)ptr->data;

  return api_ptr_inherit_refine(ptr, &ApiFreestyleLineStyle, lineset->linestyle);
}

void api_FreestyleLineSet_linestyle_set(ApiPtr *ptr,
                                        ApiPtr value,
                                        struct ReportList *UNUSED(reports))
{
  FreestyleLineSet *lineset = (FreestyleLineSet *)ptr->data;

  if (lineset->linestyle) {
    id_us_min(&lineset->linestyle->id);
  }
  lineset->linestyle = (FreestyleLineStyle *)value.data;
  id_us_plus(&lineset->linestyle->id);
}

FreestyleLineSet *api_FreestyleSettings_lineset_add(Id *id,
                                                    FreestyleSettings *config,
                                                    Main *main,
                                                    const char *name)
{
  Scene *scene = (Scene *)id;
  FreestyleLineSet *lineset = dune_freestyle_lineset_add(main, (FreestyleConfig *)config, name);

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);

  return lineset;
}

void api_FreestyleSettings_lineset_remove(Id *id,
                                          FreestyleSettings *config,
                                          ReportList *reports,
                                          ApiPtr *lineset_ptr)
{
  FreestyleLineSet *lineset = lineset_ptr->data;
  Scene *scene = (Scene *)id;

  if (!dune_freestyle_lineset_delete((FreestyleConfig *)config, lineset)) {
    dune_reportf(reports, RPT_ERROR, "Line set '%s' could not be removed", lineset->name);
    return;
  }

  API_PTR_INVALIDATE(lineset_ptr);

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);
}

ApiPtr api_FreestyleSettings_active_lineset_get(ApiPtr *ptr)
{
  FreestyleConfig *config = (FreestyleConfig *)ptr->data;
  FreestyleLineSet *lineset = dune_freestyle_lineset_get_active(config);
  return api_ptr_inherit_refine(ptr, &ApiFreestyleLineSet, lineset);
}

void api_FreestyleSettings_active_lineset_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  FreestyleConfig *config = (FreestyleConfig *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&config->linesets) - 1);
}

int api_FreestyleSettings_active_lineset_index_get(ApiPtr *ptr)
{
  FreestyleConfig *config = (FreestyleConfig *)ptr->data;
  return dune_freestyle_lineset_get_active_index(config);
}

void api_FreestyleSettings_active_lineset_index_set(ApiPtr *ptr, int value)
{
  FreestyleConfig *config = (FreestyleConfig *)ptr->data;
  dune_freestyle_lineset_set_active_index(config, value);
}

FreestyleModuleConfig *api_FreestyleSettings_module_add(Id *id, FreestyleSettings *config)
{
  Scene *scene = (Scene *)id;
  FreestyleModuleConfig *module = dune_freestyle_module_add((FreestyleConfig *)config);

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);

  return module;
}

void api_FreestyleSettings_module_remove(Id *id,
                                         FreestyleSettings *config,
                                         ReportList *reports,
                                         ApiPtr *module_ptr)
{
  Scene *scene = (Scene *)id;
  FreestyleModuleConfig *module = module_ptr->data;

  if (!dune_freestyle_module_delete((FreestyleConfig *)config, module)) {
    if (module->script) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "Style module '%s' could not be removed",
                  module->script->id.name + 2);
    } else {
      dune_report(reports, RPT_ERROR, "Style module could not be removed");
    }
    return;
  }

  API_PTR_INVALIDATE(module_ptr);

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_SCENE | ND_RENDER_OPTIONS, NULL);
}

static void api_Stereo3dFormat_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  if (id && GS(id->name) == ID_IM) {
    Image *ima = (Image *)id;
    ImBuf *ibuf;
    void *lock;

    if (!dune_image_is_stereo(ima)) {
      return;
    }

    ibuf = dune_image_acquire_ibuf(ima, NULL, &lock);

    if (ibuf) {
      dune_image_signal(main, ima, NULL, IMA_SIGNAL_FREE);
    }
    dune_image_release_ibuf(ima, ibuf, lock);
  }
}

static ViewLayer *api_ViewLayer_new(Id *id, Scene *UNUSED(sce), Main *main, const char *name)
{
  Scene *scene = (Scene *)id;
  ViewLayer *view_layer = dune_view_layer_add(scene, name, NULL, VIEWLAYER_ADD_NEW);

  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  graph_relations_tag_update(main);
  wm_main_add_notifier(NC_SCENE | ND_LAYER, NULL);

  return view_layer;
}

static void api_ViewLayer_remove(
    Id *id, Scene *UNUSED(sce), Main *main, ReportList *reports, PointerRNA *sl_ptr)
{
  Scene *scene = (Scene *)id;
  ViewLayer *view_layer = sl_ptr->data;

  if (ed_scene_view_layer_delete(main, scene, view_layer, reports)) {
    API_PTR_INVALIDATE(sl_ptr);
  }
}

void api_ViewLayer_active_aov_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;

  *min = 0;
  *max = max_ii(0, BLI_listbase_count(&view_layer->aovs) - 1);
}

int api_ViewLayer_active_aov_index_get(PointerRNA *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  return lib_findindex(&view_layer->aovs, view_layer->active_aov);
}

void api_ViewLayer_active_aov_index_set(ApiPtr *ptr, int value)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  ViewLayerAOV *aov = lib_findlink(&view_layer->aovs, value);
  view_layer->active_aov = aov;
}

void api_ViewLayer_active_lightgroup_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&view_layer->lightgroups) - 1);
}

int api_ViewLayer_active_lightgroup_index_get(ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  return lib_findindex(&view_layer->lightgroups, view_layer->active_lightgroup);
}

void api_ViewLayer_active_lightgroup_index_set(ApiPtr *ptr, int value)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  ViewLayerLightgroup *lightgroup = lib_findlink(&view_layer->lightgroups, value);
  view_layer->active_lightgroup = lightgroup;
}

static void api_ViewLayerLightgroup_name_get(ApiPtr *ptr, char *value)
{
  ViewLayerLightgroup *lightgroup = (ViewLayerLightgroup *)ptr->data;
  strcpy(value, lightgroup->name);
}

static int api_ViewLayerLightgroup_name_length(ApiPtt *ptr)
{
  ViewLayerLightgroup *lightgroup = (ViewLayerLightgroup *)ptr->data;
  return strlen(lightgroup->name);
}

static void api_ViewLayerLightgroup_name_set(ApiPtr *ptr, const char *value)
{
  ViewLayerLightgroup *lightgroup = (ViewLayerLightgroup *)ptr->data;
  Scene *scene = (Scene *)ptr->owner_id;
  ViewLayer *view_layer = dune_view_layer_find_with_lightgroup(scene, lightgroup);

  dune_view_layer_rename_lightgroup(scene, view_layer, lightgroup, value);
}

/* Fake value, used internally (not saved to DNA). */
#  define V3D_ORIENT_DEFAULT -1

static int api_TransformOrientationSlot_type_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  TransformOrientationSlot *orient_slot = ptr->data;
  if (orient_slot != &scene->orientation_slots[SCE_ORIENT_DEFAULT]) {
    if ((orient_slot->flag & SELECT) == 0) {
      return V3D_ORIENT_DEFAULT;
    }
  }
  return dune_scene_orientation_slot_get_index(orient_slot);
}

void api_TransformOrientationSlot_type_set(ApiPtr *ptr, int value)
{
  Scene *scene = (Scene *)ptr->owner_id;
  TransformOrientationSlot *orient_slot = ptr->data;

  if (orient_slot != &scene->orientation_slots[SCE_ORIENT_DEFAULT]) {
    if (value == V3D_ORIENT_DEFAULT) {
      orient_slot->flag &= ~SELECT;
      return;
    } else {
      orient_slot->flag |= SELECT;
    }
  }

  dune_scene_orientation_slot_set_index(orient_slot, value);
}

static ApiPtr api_TransformOrientationSlot_get(ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  TransformOrientationSlot *orient_slot = ptr->data;
  TransformOrientation *orientation;
  if (orient_slot->type < V3D_ORIENT_CUSTOM) {
    orientation = NULL;
  } else {
    orientation = dune_scene_transform_orientation_find(scene, orient_slot->index_custom);
  }
  return api_ptr_inherit_refine(ptr, &ApiTransformOrientation, orientation);
}

static const EnumPropItem *api_TransformOrientation_impl_itemf(Scene *scene,
                                                               const bool include_default,
                                                               bool *r_free)
{
  EnumPropItem tmp = {0, "", 0, "", ""};
  EnumPropItem *item = NULL;
  int i = V3D_ORIENT_CUSTOM, totitem = 0;

  if (include_default) {
    tmp.id = "DEFAULT";
    tmp.name = N_("Default");
    tmp.description = N_("Use the scene orientation");
    tmp.value = V3D_ORIENT_DEFAULT;
    tmp.icon = ICON_OBJECT_ORIGIN;
    api_enum_item_add(&item, &totitem, &tmp);
    tmp.icon = 0;

    api_enum_item_add_separator(&item, &totitem);
  }

  api_enum_items_add(&item, &totitem, api_enum_transform_orientation_items);

  const List *transform_orientations = scene ? &scene->transform_spaces : NULL;

  if (transform_orientations && (lib_list_is_empty(transform_orientations) == false)) {
    api_enum_item_add_separator(&item, &totitem);

    LIST_FOREACH (TransformOrientation *, ts, transform_orientations) {
      tmp.id = ts->name;
      tmp.name = ts->name;
      tmp.value = i++;
      api_enum_item_add(&item, &totitem, &tmp);
    }
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}
const EnumPropItem *api_TransformOrientation_itemf(Cxt *C,
                                                   ApiPtr *ptr,
                                                   ApiProp *UNUSED(prop),
                                                   bool *r_free)
{
  if (C == NULL) {
    return api_enum_transform_orientation_items;
  }

  Scene *scene;
  if (ptr->owner_id && (GS(ptr->owner_id->name) == ID_SCE)) {
    scene = (Scene *)ptr->owner_id;
  } else {
    scene = cxt_data_scene(C);
  }
  return api_TransformOrientation_impl_itemf(scene, false, r_free);
}

const EnumPropItem *api_TransformOrientation_with_scene_itemf(Cxt *C,
                                                              ApiPtr *ptr,
                                                              ApiProp *UNUSED(prop),
                                                              bool *r_free)
{
  if (C == NULL) {
    return api_enum_transform_orientation_items;
  }

  Scene *scene = (Scene *)ptr->owner_id;
  TransformOrientationSlot *orient_slot = ptr->data;
  bool include_default = (orient_slot != &scene->orientation_slots[SCE_ORIENT_DEFAULT]);
  return api_TransformOrientation_impl_itemf(scene, include_default, r_free);
}

#  undef V3D_ORIENT_DEFAULT

static const EnumPropItem *api_UnitSettings_itemf_wrapper(const int system,
                                                         const int type,
                                                         bool *r_free)
{
  const void *usys;
  int len;
  dune_unit_system_get(system, type, &usys, &len);

  EnumPropItem *items = NULL;
  int totitem = 0;

  EnumPropItem adaptive = {0};
  adaptive.id = "ADAPTIVE";
  adaptive.name = N_("Adaptive");
  adaptive.value = USER_UNIT_ADAPTIVE;
  api_enum_item_add(&items, &totitem, &adaptive);

  for (int i = 0; i < len; i++) {
    if (!dune_unit_is_suppressed(usys, i)) {
      EnumPropItem tmp = {0};
      tmp.id = dune_unit_identifier_get(usys, i);
      tmp.name = dune_unit_display_name_get(usys, i);
      tmp.value = i;
      api_enum_item_add(&items, &totitem, &tmp);
    }
  }

  api_enum_item_end(&items, &totitem);
  *r_free = true;

  return items;
}

const EnumPropItem *api_UnitSettings_length_unit_itemf(Cxt *UNUSED(C),
                                                       ApiPtr *ptr,
                                                       ApiProp *UNUSED(prop),
                                                       bool *r_free)
{
  UnitSettings *units = ptr->data;
  return api_UnitSettings_itemf_wrapper(units->system, B_UNIT_LENGTH, r_free);
}

const EnumPropItem *api_UnitSettings_mass_unit_itemf(Cxt *UNUSED(C),
                                                     ApiPtr *ptr,
                                                     ApiProp *UNUSED(prop),
                                                     bool *r_free)
{
  UnitSettings *units = ptr->data;
  return api_UnitSettings_itemf_wrapper(units->system, B_UNIT_MASS, r_free);
}

const EnumPropItem *api_UnitSettings_time_unit_itemf(Cxt *UNUSED(C),
                                                     ApiPtr *ptr,
                                                     ApiProp *UNUSED(prop),
                                                     bool *r_free)
{
  UnitSettings *units = ptr->data;
  return api_UnitSettings_itemf_wrapper(units->system, B_UNIT_TIME, r_free);
}

const EnumPropItem *api_UnitSettings_temperature_unit_itemf(Cxt *UNUSED(C),
                                                            ApiPtr *ptr,
                                                            ApiProp *UNUSED(prop),
                                                            bool *r_free)
{
  UnitSettings *units = ptr->data;
  return api_UnitSettings_itemf_wrapper(units->system, B_UNIT_TEMPERATURE, r_free);
}

static void api_UnitSettings_system_update(Main *UNUSED(main),
                                           Scene *scene,
                                           Apitr *UNUSED(ptr))
{
  UnitSettings *unit = &scene->unit;
  if (unit->system == USER_UNIT_NONE) {
    unit->length_unit = USER_UNIT_ADAPTIVE;
    unit->mass_unit = USER_UNIT_ADAPTIVE;
  } else {
    unit->length_unit = dune_unit_base_of_type_get(unit->system, B_UNIT_LENGTH);
    unit->mass_unit = dune_unit_base_of_type_get(unit->system, B_UNIT_MASS);
  }
}

static char *api_UnitSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("unit_settings");
}

static char *api_FFmpegSettings_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("render.ffmpeg");
}

#  ifdef WITH_FFMPEG
/* FFMpeg Codec setting update hook. */
static void api_FFmpegSettings_codec_update(Main *UNUSED(main),
                                            Scene *UNUSED(scene),
                                            ApiPtr *ptr)
{
  FFMpegCodecData *codec_data = (FFMpegCodecData *)ptr->data;
  if (!ELEM(codec_data->codec,
            AV_CODEC_ID_H264,
            AV_CODEC_ID_MPEG4,
            AV_CODEC_ID_VP9,
            AV_CODEC_ID_DNXHD))
  {
    /* Constant Rate Factor (CRF) setting is only available for H264,
     * MPEG4 and WEBM/VP9 codecs. So changing encoder quality mode to
     * CBR as CRF is not supported. */
    codec_data->constant_rate_factor = FFM_CRF_NONE;
  }
}
#  endif

#else
/* Pen Interpolation tool settings */
static void api_def_pen_interpolate(DuneApi *dapi)
{
  ApiSruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "PenInterpolateSettings", NULL);
  api_def_struct_stype(sapi, "Pen_Interpolate_Settings");
  api_def_struct_ui_text(sapi,
                         "Dune Pen Interpolate Settings",
                         "Settings for Pen interpolation tools");

  /* Custom curve-map. */
  prop = api_def_prop(sapi, "interpolation_curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "custom_ipo");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(
      prop,
      "Interpolation Curve",
      "Custom curve to control 'sequence' interpolation between Pen frames");
}

static void api_def_transform_orientation(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "TransformOrientation", NULL);

  prop = api_def_prop(sapi, "matrix", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "mat");
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_3x3);
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_ui_text(prop, "Name", "Name of the custom transform orientation");
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);
}

static void spi_def_transform_orientation_slot(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "TransformOrientationSlot", NULL);
  api_def_struct_stype(sapi, "TransformOrientationSlot");
  api_def_struct_path_fn(sapi, "api_TransformOrientationSlot_path");
  api_def_struct_ui_text(sapi, "Orientation Slot", "");

  /* Orientations */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_transform_orientation_items);
  api_def_prop_enum_fns(prop,
                        "api_TransformOrientationSlot_type_get",
                        "api_TransformOrientationSlot_type_set",
                        "api_TransformOrientation_with_scene_itemf");
  api_def_prop_ui_text(prop, "Orientation", "Transformation orientation");
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);
   
  prop = api_def_prop(sapi, "custom_orientation", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "TransformOrientation");
  api_def_prop_ptr_fns(prop, "api_TransformOrientationSlot_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Current Transform Orientation", "");

  /* flag */
  prop = api_def_prop(sapi, "use", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Use", "Use scene orientation instead of a custom setting");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);
}

static void api_def_view3d_cursor(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "View3DCursor", NULL);
  api_def_struct_stype(sapi, "View3DCursor");
  api_def_struct_path_fn(sapi, "api_View3DCursor_path");
  api_def_struct_ui_text(sapi, "3D Cursor", "");
  api_def_struct_ui_icon(sapi, ICON_CURSOR);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);

  prop = api_def_prop(sapi, "location", PROP_FLOAT, PROP_XYZ_LENGTH);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "location");
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_ui_range(prop, -10000.0, 10000.0, 10, 4);
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "rotation_quaternion", PROP_FLOAT, PROP_QUATERNION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "rotation_quaternion");
  api_def_prop_ui_text(
      prop, "Quaternion Rotation", "Rotation in quaternions (keep normalized)");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "rotation_axis_angle", PROP_FLOAT, PROP_AXISANGLE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_array(prop, 4);
  api_def_prop_float_fns(prop,
                        "api_View3DCursor_rotation_axis_angle_get",
                        "api_View3DCursor_rotation_axis_angle_set",
                        NULL);
  api_def_prop_float_array_default(prop, rna_default_axis_angle);
  api_def_prop_ui_text(
      prop, "Axis-Angle Rotation", "Angle of Rotation for Axis-Angle rotation representation");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "rotation_euler", PROP_FLOAT, PROP_EULER);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "rotation_euler");
  api_def_prop_ui_text(prop, "Euler Rotation", "3D rotation");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "rotation_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_stype(prop, NULL, "rotation_mode");
  api_def_prop_enum_items(prop, api_enum_object_rotation_mode_items);
  api_def_prop_enum_fns(prop, NULL, "api_View3DCursor_rotation_mode_set", NULL);
  api_def_prop_ui_text(prop, "Rotation Mode", "");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  /* Matrix access to avoid having to check current rotation mode. */
  prop = api_def_prop(sapi, "matrix", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_flag(prop, PROP_THICK_WRAP); /* no reference to original data */
  api_def_prop_ui_text(
      prop, "Transform Matrix", "Matrix combining location and rotation of the cursor");
  api_def_prop_float_fns(
      prop, "api_View3DCursor_matrix_get", "api_View3DCursor_matrix_set", NULL);
  api_def_prop_update(prop, NC_WINDOW, NULL);
}

static void api_def_tool_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* the construction of this enum is quite special - everything is stored as bitflags,
   * with 1st position only for on/off (and exposed as boolean), while others are mutually
   * exclusive options but which will only have any effect when autokey is enabled  */
  static const EnumPropItem auto_key_items[] = {
      {AUTOKEY_MODE_NORMAL & ~AUTOKEY_ON, "ADD_REPLACE_KEYS", 0, "Add & Replace", ""},
      {AUTOKEY_MODE_EDITKEYS & ~AUTOKEY_ON, "REPLACE_KEYS", 0, "Replace", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem draw_groupuser_items[] = {
      {OB_DRAW_GROUPUSER_NONE, "NONE", 0, "None", ""},
      {OB_DRAW_GROUPUSER_ACTIVE,
       "ACTIVE",
       0,
       "Active",
       "Show vertices with no weights in the active group"},
      {OB_DRAW_GROUPUSER_ALL, "ALL", 0, "All", "Show vertices with no weights in any group"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem vertex_group_select_items[] = {
      {WT_VGROUP_ALL, "ALL", 0, "All", "All Vertex Groups"},
      {WT_VGROUP_BONE_DEFORM,
       "BONE_DEFORM",
       0,
       "Deform",
       "Vertex Groups assigned to Deform Bones"},
      {WT_VGROUP_BONE_DEFORM_OFF,
       "OTHER_DEFORM",
       0,
       "Other",
       "Vertex Groups assigned to non Deform Bones"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pen_stroke_placement_items[] = {
      {PEN_PROJECT_VIEWSPACE,
       "ORIGIN",
       ICON_OBJECT_ORIGIN,
       "Origin",
       "Draw stroke at Object origin"},
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_CURSOR,
       "CURSOR",
       ICON_PIVOT_CURSOR,
       "3D Cursor",
       "Draw stroke at 3D cursor location"},
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_DEPTH_VIEW,
       "SURFACE",
       ICON_SNAP_FACE,
       "Surface",
       "Stick stroke to surfaces"},
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_DEPTH_STROKE,
       "STROKE",
       ICON_STROKE,
       "Stroke",
       "Stick stroke to other strokes"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pen_stroke_snap_items[] = {
      {0, "NONE", 0, "All Points", "Snap to all points"},
      {PEN_PROJECT_DEPTH_STROKE_ENDPOINTS,
       "ENDS",
       0,
       "End Points",
       "Snap to first and last points and interpolate"},
      {PEN_PROJECT_DEPTH_STROKE_FIRST, "FIRST", 0, "First Point", "Snap to first point"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pen_selectmode_items[] = {
      {PEN_SELECTMODE_POINT, "POINT", ICON_PEN_SELECT_POINTS, "Point", "Select only points"},
      {PEN_SELECTMODE_STROKE,
       "STROKE",
       ICON_PEN_SELECT_STROKES,
       "Stroke",
       "Select all stroke points"},
      {PEN_SELECTMODE_SEGMENT,
       "SEGMENT", ICON_PEN_SELECT_BETWEEN_STROKES,
       "Segment",
       "Select all stroke points between other strokes"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem annotation_stroke_placement_view2d_items[] = {
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_CURSOR,
       "IMAGE",
       ICON_IMAGE_DATA,
       "Image",
       "Stick stroke to the image"},
      /* Weird, GP_PROJECT_VIEWALIGN is inverted. */
      {0, "VIEW", ICON_RESTRICT_VIEW_ON, "View", "Stick stroke to the view"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem annotation_stroke_placement_view3d_items[] = {
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_CURSOR,
       "CURSOR",
       ICON_PIVOT_CURSOR,
       "3D Cursor",
       "Draw stroke at 3D cursor location"},
      /* Weird, GP_PROJECT_VIEWALIGN is inverted. */
      {0, "VIEW", ICON_RESTRICT_VIEW_ON, "View", "Stick stroke to the view"},
      {PEN_PROJECT_VIEWSPACE | PEN_PROJECT_DEPTH_VIEW,
       "SURFACE",
       ICON_FACESEL,
       "Surface",
       "Stick stroke to surfaces"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem uv_sticky_mode_items[] = {
      {SI_STICKY_DISABLE,
       "DISABLED",
       ICON_STICKY_UVS_DISABLE,
       "Disabled",
       "Sticky vertex selection disabled"},
      {SI_STICKY_LOC,
       "SHARED_LOCATION",
       ICON_STICKY_UVS_LOC,
       "Shared Location",
       "Select UVs that are at the same location and share a mesh vertex"},
      {SI_STICKY_VERTEX,
       "SHARED_VERTEX",
       ICON_STICKY_UVS_VERT,
       "Shared Vertex",
       "Select UVs that share a mesh vertex, whether or not they are at the same location"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "ToolSettings", NULL);
  api_def_struct_path_fn(sapi, "api_ToolSettings_path");
  api_def_struct_ui_text(sapi, "Tool Settings", "");

  prop = api_def_prop(sapi, "sculpt", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Sculpt");
  api_def_prop_ui_text(prop, "Sculpt", "");

  prop = api_def_prop(sapi, "curves_sculpt", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "CurvesSculpt");
  api_def_prop_ui_text(prop, "Curves Sculpt", "");

  prop = api_def_prop(sapi, "use_auto_normalize", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "auto_normalize", 1);
  api_def_prop_ui_text(prop,
                       "Weight Paint Auto-Normalize",
                       "Ensure all bone-deforming vertex groups add up "
                       "to 1.0 while weight painting");
  api_def_prop_update(prop, 0, "api_scene_update_active_object_data");

  prop = api_def_prop(sapi, "use_lock_relative", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "wpaint_lock_relative", 1);
  api_def_prop_ui_text(prop,
                       "Weight Paint Lock-Relative",
                       "Display bone-deforming groups as if all locked deform groups "
                       "were deleted, and the remaining ones were re-normalized");
  api_def_prop_update(prop, 0, "api_Scene_update_active_object_data");

  prop = api_def_prop(sapi, "use_multipaint", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "multipaint", 1);
  api_def_prop_ui_text(prop,
                       "Weight Paint Multi-Paint",
                       "Paint across the weights of all selected bones, "
                       "maintaining their relative influence");
  api_def_prop_update(prop, 0, "api_Scene_update_active_object_data");

  prop = api_def_prop(sapi, "vertex_group_user", PROP_ENUM, PROP_NONE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_enum_stype(prop, NULL, "weightuser");
  api_def_prop_enum_items(prop, draw_groupuser_items);
  api_def_prop_ui_text(prop, "Mask Non-Group Vertices", "Display unweighted vertices");
  api_def_prop_update(prop, 0, "api_Scene_update_active_object_data");

  prop = api_def_prop(sapi, "vertex_group_subset", PROP_ENUM, PROP_NONE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_enum_stype(prop, NULL, "vgroupsubset");
  api_def_prop_enum_items(prop, vertex_group_select_items);
  api_def_prop_ui_text(prop, "Subset", "Filter Vertex groups for Display");
  api_def_prop_update(prop, 0, "api_Scene_update_active_object_data");

  prop = api_def_prop(sapi, "vertex_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "vpaint");
  api_def_prop_ui_text(prop, "Vertex Paint", "");

  prop = api_def_prop(sapi, "weight_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "wpaint");
  api_def_prop_ui_text(prop, "Weight Paint", "");

  prop = api_def_prop(sapi, "image_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "imapaint");
  api_def_prop_ui_text(prop, "Image Paint", "");

  prop = api_def_prop(sapi, "paint_mode", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "paint_mode");
  api_def_prop_ui_text(prop, "Paint Mode", "");

  prop = api_def_prop(sapi, "uv_sculpt", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "uvsculpt");
  api_def_prop_ui_text(prop, "UV Sculpt", "");

  prop = api_def_prop(sapi, "pen_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_paint");
  api_def_prop_ui_text(prop, "Pen Paint", "");

  prop = api_def_prop(sapi, "pen_vertex_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_vertexpaint");
  api_def_prop_ui_text(prop, "Pen Vertex Paint", "");

  prop = api_def_prop(sapi, "pen_sculpt_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_sculptpaint");
  api_def_prop_ui_text(prop, "Pen Sculpt Paint", "");

  prop = api_def_prop(sapi, "pen_weight_paint", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_weightpaint");
  api_def_prop_ui_text(prop, "Pen Weight Paint", "");

  prop = api_def_prop(sapi, "particle_edit", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "particle");
  api_def_prop_ui_text(prop, "Particle Edit", "");

  prop = api_def_prop(sapi, "uv_sculpt_lock_borders", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uv_sculpt_settings", UV_SCULPT_LOCK_BORDERS);
  api_def_prop_ui_text(prop, "Lock Borders", "Disable editing of boundary edges");

  prop = api_def_prop(sapi, "uv_sculpt_all_islands", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uv_sculpt_settings", UV_SCULPT_ALL_ISLANDS);
  api_def_prop_ui_text(prop, "Sculpt All Islands", "Brush operates on all islands");

  prop = api_def_prop(sapi, "uv_relax_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "uv_relax_method");
  api_def_prop_enum_items(prop, uv_sculpt_relaxation_items);
  api_def_prop_ui_text(prop, "Relaxation Method", "Algorithm used for UV relaxation");

  prop = api_def_prop(sapi, "lock_object_mode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "object_flag", SCE_OBJECT_MODE_LOCK);
  api_def_prop_ui_text(prop,
                       "Lock Object Modes",
                       "Restrict selection to objects using the same mode as the active "
                       "object, to prevent accidental mode switch when selecting");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  static const EnumPropItem workspace_tool_items[] = {
      {SCE_WORKSPACE_TOOL_DEFAULT, "DEFAULT", 0, "Active Tool", ""},
      {SCE_WORKSPACE_TOOL_FALLBACK, "FALLBACK", 0, "Select", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "workspace_tool_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "workspace_tool_type");
  api_def_prop_enum_items(prop, workspace_tool_items);
  api_def_prop_translation_cxt(prop, LANG_CXT_EDITOR_VIEW3D);
  api_def_prop_ui_text(prop, "Drag", "Action when dragging in the viewport");

  /* Transform */
  prop = api_def_prop(sapi, "use_proportional_edit", PROP_BOOL, PROP_NONE);
  apo_def_prop_bool_stype(prop, NULL, "proportional_edit", PROP_EDIT_USE);
  api_def_prop_ui_text(prop, "Proportional Editing", "Proportional edit mode");
  api_def_prop_ui_icon(prop, ICON_PROP_ON, 0);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_proportional_edit_objects", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_objects", 0);
  api_def_prop_ui_text(
      prop, "Proportional Editing Objects", "Proportional editing object mode");
  api_def_prop_ui_icon(prop, ICON_PROP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_proportional_projected", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_edit", PROP_EDIT_PROJECTED);
  api_def_prop_ui_text(
      prop, "Projected from View", "Proportional Editing using screen space locations");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_proportional_connected", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_edit", PROP_EDIT_CONNECTED);
  api_def_prop_ui_text(
      prop, "Connected Only", "Proportional Editing using connected geometry only");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_proportional_edit_mask", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_mask", 0);
  api_def_prop_ui_text(prop, "Proportional Editing Objects", "Proportional editing mask mode");
  api_def_prop_ui_icon(prop, ICON_PROP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_de_prop(sapi, "use_proportional_action", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_action", 0);
  api_def_prop_ui_text(
      prop, "Proportional Editing Actions", "Proportional editing in action editor");
  api_def_prop_ui_icon(prop, ICON_PROP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_proportional_fcurve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "proportional_fcurve", 0);
  api_def_prop_ui_text(
      prop, "Proportional Editing F-Curves", "Proportional editing in F-Curve editor");
  api_def_prop_ui_icon(prop, ICON_PROP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "lock_markers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "lock_markers", 0);
  api_def_prop_ui_text(prop, "Lock Markers", "Prevent marker editing");

  prop = api_def_prop(sapi, "proportional_edit_falloff", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "prop_mode");
  api_def_prop_enum_items(prop, api_enum_proportional_falloff_items);
  api_def_prop_ui_text(
      prop, "Proportional Editing Falloff", "Falloff type for proportional editing mode");
  /* Abusing id_curve :/ */
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_CURVE_LEGACY);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "proportional_size", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "proportional_size");
  api_def_prop_ui_text(
      prop, "Proportional Size", "Display size for proportional editing circle");
  api_def_prop_range(prop, 0.00001, 5000.0);

  prop = api_def_prop(sapi, "proportional_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "proportional_size");
  api_def_prop_ui_text(
      prop, "Proportional Size", "Display size for proportional editing circle");
  api_def_prop_range(prop, 0.00001, 5000.0);

  prop = apo_def_prop(sapi, "double_threshold", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "doublimit");
  api_def_prop_ui_text(prop, "Merge Threshold", "Threshold distance for Auto Merge");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_range(prop, 0.0, 0.1, 0.01, 6);

  /* Pivot Point */
  prop = api_def_prop(sapi, "transform_pivot_point", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "transform_pivot_point");
  api_def_prop_enum_items(prop, api_enum_transform_pivot_items_full);
  api_def_prop_ui_text(prop, "Transform Pivot Point", "Pivot center for rotation/scaling");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_transform_pivot_point_align", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transform_flag", SCE_XFORM_AXIS_ALIGN);
  api_def_prop_ui_text(
      prop,
      "Only Locations",
      "Only transform object locations, without affecting rotation or scaling");
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);

  prop = api_def_prop(sapi, "use_transform_data_origin", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transform_flag", SCE_XFORM_DATA_ORIGIN);
  api_def_prop_ui_text(
      prop, "Transform Origins", "Transform object origins, while leaving the shape in place");
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);

  prop = api_def_prop(sapi, "use_transform_skip_children", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transform_flag", SCE_XFORM_SKIP_CHILDREN);
  api_def_prop_ui_text(
      prop, "Transform Parents", "Transform the parents, leaving the children in place");
  api_def_prop_update(prop, NC_SCENE | ND_TRANSFORM, NULL);

  prop = api_def_prop(sapi, "use_transform_correct_face_attributes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uvcalc_flag", UVCALC_TRANSFORM_CORRECT);
  api_def_prop_ui_text(prop,
                      "Correct Face Attributes",
                      "Correct data such as UVs and color attributes when transforming");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_transform_correct_keep_connected", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "uvcalc_flag", UVCALC_TRANSFORM_CORRECT_KEEP_CONNECTED);
  api_def_prop_ui_text(
      prop,
      "Keep Connected",
      "During the Face Attributes correction, merge attributes connected to the same vertex");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_mesh_automerge", PROP_BOOL
      , PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "automerge", AUTO_MERGE);
  api_def_prop_ui_text(
      prop, "Auto Merge Vertices", "Automatically merge vertices moved to the same location");
  api_def_prop_ui_icon(prop, ICON_AUTOMERGE_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_mesh_automerge_and_split", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "automerge", AUTO_MERGE_AND_SPLIT);
  api_def_prop_ui_text(prop, "Split Edges & Faces", "Automatically split edges and faces");
  api_def_prop_ui_icon(prop, ICON_AUTOMERGE_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_styoe(prop, NULL, "snap_flag", SCE_SNAP);
  api_def_prop_ui_text(prop, "Snap", "Snap during transform");
  api_def_prop_ui_icon(prop, ICON_SNAP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_node", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag_node", SCE_SNAP);
  api_def_prop_ui_text(prop, "Snap", "Snap Node during transform");
  api_def_prop_ui_icon(prop, ICON_SNAP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_sequencer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag_seq", SCE_SNAP);
  api_def_prop_ui_text(prop, "Use Snapping", "Snap to strip edges or current frame");
  api_def_prop_ui_icon(prop, ICON_SNAP_OFF, 1);
  api_def_prop_bool_default(prop, true);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* Publish message-bus. */

  prop = api_def_prop(sapi, "use_snap_uv", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_uv_flag", SCE_SNAP);
  api_def_prop_ui_text(prop, "Snap", "Snap UV during transform");
  api_def_prop_ui_icon(prop, ICON_SNAP_OFF, 1);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_align_rotation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_ROTATE);
  api_def_prop_ui_text(
      prop, "Align Rotation to Target", "Align rotation with the snapping target");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_grid_absolute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_ABS_GRID);
  api_def_prop_ui_text(
      prop,
      "Absolute Grid Snap",
      "Absolute grid alignment while translating (based on the pivot center)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "snap_elements", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "snap_mode");
  api_def_prop_enum_items(prop, api_enum_snap_element_items);
  api_def_prop_enum_fns(prop, NULL, "api_ToolSettings_snap_mode_set", NULL);
  api_def_prop_flag(prop, PROP_ENUM_FLAG);
  api_def_prop_ui_text(prop, "Snap Element", "Type of element to snap to");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "snap_face_nearest_steps", PROP_INT, PROP_FACTOR);
  api_def_prop_int_stype(prop, NULL, "snap_face_nearest_steps");
  api_def_prop_range(prop, 1, 100);
  api_def_prop_ui_text(
      prop,
      "Face Nearest Steps",
      "Number of steps to break transformation into for face nearest snapping");

  prop = api_def_prop(sapi, "use_snap_to_same_target", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_KEEP_ON_SAME_OBJECT);
  api_def_prop_ui_text(
      prop,
      "Snap to Same Target",
      "Snap only to target that source was initially near (Face Nearest Only)");

  /* node editor uses own set of snap modes */
  prop = api_def_prop(sapi, "snap_node_element", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "snap_node_mode");
  api_def_prop_enum_items(prop, api_enum_snap_node_element_items);
  api_def_prop_ui_text(prop, "Snap Node Element", "Type of element to snap to");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  /* image editor uses own set of snap modes */
  prop = api_def_prop(sapi, "snap_uv_element", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "snap_uv_mode");
  api_def_prop_enum_items(prop, snap_uv_element_items);
  api_def_prop_ui_text(prop, "Snap UV Element", "Type of element to snap to");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_uv_grid_absolute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_uv_flag", SCE_SNAP_ABS_GRID);
  api_def_prop_ui_text(
      prop,
      "Absolute Grid Snap",
      "Absolute grid alignment while translating (based on the pivot center)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  /* TODO: Rename `snap_target` to `snap_source` to avoid previous ambiguity of "target"
   * (now, "source" is geometry to be moved and "target" is geometry to which moved geometry is
   * snapped). */
  prop = api_def_prop(sapi, "snap_target", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "snap_target");
  api_def_prop_enum_items(prop, api_enum_snap_source_items);
  api_def_prop_ui_text(prop, "Snap Target", "Which part to snap onto the target");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_peel_object", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_PEEL_OBJECT);
  api_def_prop_ui_text(
      prop, "Snap Peel Object", "Consider objects as whole when finding volume center");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_project", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_PROJECT);
  api_def_prop_ui_text(prop,
                       "Project Individual Elements",
                       "Project individual elements on the surface of other objects (Always "
                       "enabled with Face Nearest)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_backface_culling", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_BACKFACE_CULLING);
  api_def_prop_ui_text(prop, "Backface Culling", "Exclude back facing geometry from snapping");
  wpi_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  /* TODO: Rename `use_snap_self` to `use_snap_active`, because active is correct but
   * self is not (breaks API). This only makes a difference when more than one mesh is edited. */
  prop = api_def_prop(sapi, "use_snap_self", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "snap_flag", SCE_SNAP_NOT_TO_ACTIVE);
  api_def_prop_ui_text(
      prop, "Snap onto Active", "Snap onto itself only if enabled (Edit Mode Only)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_edit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_TO_INCLUDE_EDITED);
  api_def_prop_ui_text(
      prop, "Snap onto Edited", "Snap onto non-active objects in Edit Mode (Edit Mode Only)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_nonedit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_TO_INCLUDE_NONEDITED);
  api_def_prop_ui_text(
      prop, "Snap onto Non-edited", "Snap onto objects not in Edit Mode (Edit Mode Only)");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_selectable", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SCE_SNAP_TO_ONLY_SELECTABLE);
  api_def_prop_ui_text(
      prop, "Snap onto Selectable Only", "Snap only onto objects that are selectable");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_translate", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "snap_transform_mode_flag", SCE_SNAP_TRANSFORM_MODE_TRANSLATE);
  api_def_prop_ui_text(
      prop, "Use Snap for Translation", "Move is affected by snapping settings");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_rotate", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "snap_transform_mode_flag", SCE_SNAP_TRANSFORM_MODE_ROTATE);
  api_def_prop_bool_default(prop, false);
  api_def_prop_ui_text(
      prop, "Use Snap for Rotation", "Rotate is affected by the snapping settings");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "use_snap_scale", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "snap_transform_mode_flag", SCE_SNAP_TRANSFORM_MODE_SCALE);
  api_def_prop_bool_default(prop, false);
  api_def_prop_ui_text(prop, "Use Snap for Scale", "Scale is affected by snapping settings");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  /* Dune Pen */
  prop = api_def_prop(sapi, "use_pen_draw_additive", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pen_flags", PEN_TOOL_FLAG_RETAIN_LAST);
  api_def_prop_ui_text(prop,
                       "Use Additive Drawing",
                       "When creating new frames, the strokes from the previous/active frame "
                       "are included as the basis for the new one");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_pen_draw_onback", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pen_flags", GP_TOOL_FLAG_PAINT_ONBACK);
  api_def_prop_ui_text(
      prop,
      "Draw Strokes on Back",
      "When draw new strokes, the new stroke is drawn below of all strokes in the layer");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_pen_thumbnail_list", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "pen_flags", PEN_TOOL_FLAG_THUMBNAIL_LIST);
  api_def_prop_ui_text(
      prop, "Compact List", "Show compact list of color instead of thumbnails");
 api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_gpencil_weight_data_add", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pen_flags", GP_TOOL_FLAG_CREATE_WEIGHTS);
  api_def_prop_ui_text(prop,
                       "Add weight data for new strokes",
                       "When creating new strokes, the weight data is added according to the "
                       "current vertex group and weight, "
                           "if no vertex group selected, weight is not added");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

  prop = api_def_prop(sapi, "use_pen_automerge_strokes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pen_flags", PEN_TOOL_FLAG_AUTOMERGE_STROKE);
  api_def_prop_bool_default(prop, false);
  api_def_prop_ui_icon(prop, ICON_AUTOMERGE_OFF, 1);
  api_def_prop_ui_text(
      prop,
      "Automerge",
      "Join by distance last drawn stroke with previous strokes in the active layer");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);
  prop = api_def_prop(sapi, "pen_sculpt", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_sculpt");
  api_def_prop_struct_type(prop, "PenSculptSettings");
  api_def_prop_ui_text(
      prop, "Pen Sculpt", "Settings for stroke sculpting tools and brushes");

  prop = api_def_prop(sapi, "pen_interpolate", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "pen_interpolate");
  api_def_prop_struct_type(prop, "PenInterpolateSettings");
  api_def_prop_ui_text(
      prop, "Pen Interpolate", "Settings for Pen Interpolation tools");

  /* Pen - 3D View Stroke Placement */
  prop = api_def_prop(sapi, "pen_stroke_placement_view3d", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "pen_v3d_align");
  api_def_prop_enum_items(prop, pen_stroke_placement_items);
  api_def_prop_ui_text(prop, "Stroke Placement (3D View)", "");
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  prop = api_def_prop(sapi, "pen_stroke_snap_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "pen_v3d_align");
  api_def_prop_enum_items(prop, pen_stroke_snap_items);
  api_def_prop_ui_text(prop, "Stroke Snap", "");
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);
    
  prop = api_def_prop(sapi, "use_pen_stroke_endpoints", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_v3d_align", PEN_PROJECT_DEPTH_STROKE_ENDPOINTS);
  api_def_prop_ui_text(
      prop, "Only Endpoints", "Only use the first and last parts of the stroke for snapping");
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Pen - Select mode Edit */
  prop = api_def_prop(sapi, "pen_selectmode_edit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "or _selectmode_edit");
  api_def_prop_enum_items(prop, pen_selectmode_items);
  api_def_prop_ui_text(prop, "Select Mode", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "api_pen_selectmode_update");

  /* Pen - Select mode Sculpt */
  prop = api_def_prop(sapi, "use_pen_select_mask_point", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sytype(
      prop, NULL, "pen_selectmode_sculpt", PEN_SCULPT_MASK_SELECTMODE_POINT);
  api_def_prop_ui_text(prop, "Selection Mask", "Only sculpt selected stroke points");
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_POINTS, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "api_pen_mask_point_update");

  prop = api_def_prop(sapi, "use_pen_select_mask_stroke", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_selectmode_sculpt", PEN_SCULPT_MASK_SELECTMODE_STROKE);
  api_def_prop_ui_text(prop, "Selection Mask", "Only sculpt selected stroke");
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_STROKES, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "api_pe_mask_stroke_update");

  prop = api_def_prop(sapi, "use_pen_select_mask_segment", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_selectmode_sculpt", PEN_SCULPT_MASK_SELECTMODE_SEGMENT);
  api_def_prop_ui_text(
      prop, "Selection Mask", "Only sculpt selected stroke points between other strokes");
  api_def_prop_ui_icon(prop, ICON_PEN_SELECT_BETWEEN_STROKES, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_Gpencil_mask_segment_update");

  /* Pen - Select mode Vertex Paint */
  prop = api_def_prop(sapi, "use_pen_vertex_select_mask_point", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_selectmode_vertex", GP_VERTEX_MASK_SELECTMODE_POINT);
  api_def_prop_ui_text(prop, "Selection Mask", "Only paint selected stroke points");
  api_def_prop_ui_icon(prop, ICON_GP_SELECT_POINTS, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_update(
      prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_Gpencil_vertex_mask_point_update");

  prop = api_def_prop(sapi, "use_pen_vertex_select_mask_stroke", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_selectmode_vertex", GP_VERTEX_MASK_SELECTMODE_STROKE);
  api_def_prop_ui_text(prop, "Selection Mask", "Only paint selected stroke");
  api_def_prop_ui_icon(prop, ICON_GP_SELECT_STROKES, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_update(
      prop, NC_SPACE | ND_SPACE_VIEW3D, "api_pen_vertex_mask_stroke_update");

  prop = api_def_prop(sapi, "use_pen_vertex_select_mask_segment", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "pen_selectmode_vertex", GP_VERTEX_MASK_SELECTMODE_SEGMENT);
  api_def_prop_ui_text(
      prop, "Selection Mask", "Only paint selected stroke points between other strokes");
  api_def_prop_ui_icon(prop, ICON_GP_SELECT_BETWEEN_STROKES, 0);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_update(
      prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_Gpencil_vertex_mask_segment_update");

  /* Annotations - 2D Views Stroke Placement */
  prop = api_def_prop(sapi, "annotation_stroke_placement_view2d", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_sdna(prop, NULL, "gpencil_v2d_align");
  api_def_prop_enum_items(prop, annotation_stroke_placement_view2d_items);
  api_def_prop_ui_text(prop, "Stroke Placement (2D View)", "");
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Annotations - 3D View Stroke Placement */
  /* XXX: Do we need to decouple the stroke_endpoints setting too? */
  prop = api_def_prop(sapi, "annotation_stroke_placement_view3d", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "annotate_v3d_align");
  api_def_prop_enum_items(prop, annotation_stroke_placement_view3d_items);
  api_def_prop_enum_default(prop, PEN_PROJECT_VIEWSPACE | PEN_PROJECT_CURSOR);
  api_def_prop_ui_text(prop,
                       "Annotation Stroke Placement (3D View)",
                       "How annotation strokes are orientated in 3D space");
  api_def_prop_update(prop, NC_PEN | ND_DATA, NULL);

  /* Annotations - Stroke Thickness */
  prop = api_def_prop(sapi, "annotation_thickness", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "annotate_thickness");
  api_def_prop_range(prop, 1, 10);
  api_def_prop_ui_text(prop, "Annotation Stroke Thickness", "Thickness of annotation strokes");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  /* Auto Keying */
  prop = api_def_prop(sapi, "use_keyframe_insert_auto", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_mode", AUTOKEY_ON);
  api_def_prop_ui_text(
      prop, "Auto Keying", "Automatic keyframe insertion for Objects, Bones and Masks");
  api_def_prop_ui_icon(prop, ICON_REC, 0);

  prop = api_def_prop(sapi, "auto_keying_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "autokey_mode");
  api_def_prop_enum_items(prop, auto_key_items);
  api_def_prop_ui_text(prop,
                       "Auto-Keying Mode",
                       "Mode of automatic keyframe insertion for Objects, Bones and Masks");

  prop = api_def_prop(sapi, "use_record_with_nla", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", ANIMRECORD_FLAG_WITHNLA);
  api_def_prop_ui_text(
      prop,
      "Layered",
      "Add a new NLA Track + Strip for every loop/pass made over the animation "
      "to allow non-destructive tweaking");

  prop = api_def_prop(sapi, "use_keyframe_insert_keyingset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_ONLYKEYINGSET);
  api_def_prop_ui_text(prop,
                       "Auto Keyframe Insert Keying Set",
                       "Automatic keyframe insertion using active Keying Set only");
  api_def_prop_ui_icon(prop, ICON_KEYINGSET, 0);

  prop = api_def_prop(sapi, "use_keyframe_cycle_aware", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_CYCLEAWARE);
  api_def_prop_ui_text(
      prop,
      "Cycle-Aware Keying",
      "For channels with cyclic extrapolation, keyframe insertion is automatically "
      "remapped inside the cycle time range, and keeps ends in sync. Curves newly added to "
      "actions with a Manual Frame Range and Cyclic Animation are automatically made cyclic");

  /* Keyframing */
  prop = api_def_prop(sapi, "keyframe_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "keyframe_type");
  api_def_prop_enum_items(prop, api_enum_beztriple_keyframe_type_items);
  api_def_prop_ui_text(
      prop, "New Keyframe Type", "Type of keyframes to create when inserting keyframes");

  /* UV */
  prop = api_def_prop(sapi, "uv_select_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_s(prop, NULL, "uv_selectmode");
  api_def_prop_enum_items(prop, api_enum_mesh_select_mode_uv_items);
  api_def_prop_ui_text(prop, "UV Selection Mode", "UV selection and display mode");
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, "api_Scene_uv_select_mode_update");

  prop = api_def_prop(sapi, "uv_sticky_select_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sapi(prop, NULL, "uv_sticky");
  api_def_prop_enum_items(prop, uv_sticky_mode_items);
  api_def_prop_ui_text(
      prop, "Sticky Selection Mode", "Method for extending UV vertex selection");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

  prop = api_def_prop(sapi, "use_uv_select_sync", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uv_flag", UV_SYNC_SELECTION);
  api_def_prop_ui_text(
      prop, "UV Sync Selection", "Keep UV and edit mode mesh selection in sync");
  api_def_prop_ui_icon(prop, ICON_UV_SYNC_SELECT, 0);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

  prop = api_def_prop(api, "show_uv_local_view", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "uv_flag", UV_SHOW_SAME_IMAGE);
  api_def_prop_ui_text(
      prop, "UV Local View", "Display only faces with the currently displayed image assigned");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

  /* Mesh */
  prop = api_def_prop(sapi, "mesh_select_mode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selectmode", 1);
  api_def_prop_array(prop, 3);
  api_def_prop_bool_fns(prop, NULL, "api_Scene_editmesh_select_mode_set");
  api_def_prop_ui_text(prop, "Mesh Selection Mode", "Which mesh elements selection works on");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_Scene_editmesh_select_mode_update");

  prop = api_def_prop(sapi, "vertex_group_weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "vgroup_weight");
  api_def_prop_ui_text(prop, "Vertex Group Weight", "Weight to assign in vertex groups");

  prop = api_def_prop(sapi, "use_edge_path_live_unwrap", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_mode_live_unwrap", 1);
  api_def_prop_ui_text(prop, "Live Unwrap", "Changing edge seams recalculates UV unwrap");

  prop = api_def_prop(sapi, "normal_vector", PROP_FLOAT, PROP_XYZ);
  api_def_prop_ui_text(prop, "Normal Vector", "Normal Vector used to copy, add or multiply");
  api_def_prop_ui_range(prop, -10000.0, 10000.0, 1, 3);

  /* Unified Paint Settings */
  prop = api_def_prop(sapi, "unified_paint_settings", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "UnifiedPaintSettings");
  api_def_prop_ui_text(prop, "Unified Paint Settings", NULL);

  /* Curve Paint Settings */
  prop = api_def_prop(sapi, "curve_paint_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "CurvePaintSettings");
  api_def_prop_ui_text(prop, "Curve Paint Settings", NULL);

  /* Mesh Statistics */
  prop = api_def_prop(sapi, "statvis", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "MeshStatVis");
  api_def_prop_ui_text(prop, "Mesh Statistics Visualization", NULL);

  /* CurveProfile */
  prop = api_def_prop(sapi, "custom_bevel_profile_preset", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "custom_bevel_profile_preset");
  api_def_prop_struct_type(prop, "CurveProfile");
  api_def_prop_ui_text(prop, "Curve Profile Widget", "Used for defining a profile's path");

  /* Sequencer tool settings */
  prop = api_def_prop(sapi, "seq_tool_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "SeqToolSettings");
  api_def_prop_ui_text(prop, "Sequencer Tool Settings", NULL);
}

static void api_def_seq_tool_settings(DuneApi *dapi)
{
  EnumPropItem scale_fit_methods[] = {
      {SEQ_SCALE_TO_FIT, "FIT", 0, "Scale to Fit", "Scale image to fit within the canvas"},
      {SEQ_SCALE_TO_FILL, "FILL", 0, "Scale to Fill", "Scale image to completely fill the canvas"},
      {SEQ_STRETCH_TO_FILL, "STRETCH", 0, "Stretch to Fill", "Stretch image to fill the canvas"},
      {SEQ_USE_ORIGINAL_SIZE,
       "ORIGINAL",
       0,
       "Use Original Size",
       "Keep image at its original size"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem scale_overlap_modes[] = {
      {SEQ_OVERLAP_EXPAND, "EXPAND", 0, "Expand", "Move strips so transformed strips fit"},
      {SEQ_OVERLAP_OVERWRITE,
       "OVERWRITE",
       0,
       "Overwrite",
       "Trim or split strips to resolve overlap"},
      {SEQ_OVERLAP_SHUFFLE,
       "SHUFFLE",
       0,
       "Shuffle",
       "Move transformed strips to nearest free space to resolve overlap"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pivot_points[] = {
      {V3D_AROUND_CENTER_BOUNDS, "CENTER", ICON_PIVOT_BOUNDBOX, "Bounding Box Center", ""},
      {V3D_AROUND_CENTER_MEDIAN, "MEDIAN", ICON_PIVOT_MEDIAN, "Median Point", ""},
      {V3D_AROUND_CURSOR, "CURSOR", ICON_PIVOT_CURSOR, "2D Cursor", "Pivot around the 2D cursor"},
      {V3D_AROUND_LOCAL_ORIGINS,
       "INDIVIDUAL_ORIGINS",
       ICON_PIVOT_INDIVIDUAL,
       "Individual Origins",
       "Pivot around each selected island's own median point"},
      {0, NULL, 0, NULL, NULL},

  };
  sapi = api_def_struct(dapi, "SeqToolSettings", NULL);
  api_def_struct_path_fn(sapi, "api_SeqToolSettings_path");
  api_def_struct_ui_text(sapi, "Seq Tool Settings", "");

  /* Add strip settings. */
  prop = api_def_prop(sapi, "fit_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, scale_fit_methods);
  api_def_prop_ui_text(prop, "Fit Method", "Scale fit method");

  /* Transform snapping. */
  prop = api_def_prop(sapi, "snap_to_current_frame", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_mode", SEQ_SNAP_TO_CURRENT_FRAME);
  api_def_prop_ui_text(prop, "Current Frame", "Snap to current frame");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "snap_to_hold_offset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_mode", SEQ_SNAP_TO_STRIP_HOLD);
  api_def_prop_ui_text(prop, "Hold Offset", "Snap to strip hold offsets");
  api_def_prop_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

  prop = api_def_prop(sapi, "snap_ignore_muted", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypr(prop, NULL, "snap_flag", SEQ_SNAP_IGNORE_MUTED);
  api_def_prop_ui_text(prop, "Ignore Muted Strips", "Don't snap to hidden strips");

  prop = api_def_prop(sapi, "snap_ignore_sound", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stupe(prop, NULL, "snap_flag", SEQ_SNAP_IGNORE_SOUND);
  api_def_prop_ui_text(prop, "Ignore Sound Strips", "Don't snap to sound strips");

  prop = api_def_prop(sapi, "use_snap_current_frame_to_strips", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "snap_flag", SEQ_SNAP_CURRENT_FRAME_TO_STRIPS);
  api_def_prop_ui_text(
      prop, "Snap Current Frame to Strips", "Snap current frame to strip start or end");

  prop = api_def_prop(sapi, "snap_distance", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "snap_distance");
  api_def_prop_int_default(prop, 15);
  api_def_prop_ui_range(prop, 0, 50, 1, 1);
  api_def_prop_ui_text(prop, "Snapping Distance", "Maximum distance for snapping in pixels");

  /* Transform overlap handling. */
  prop = api_def_prop(sapi, "overlap_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, scale_overlap_modes);
  api_def_prop_ui_text(prop, "Overlap Mode", "How to resolve overlap after transformation");

  prop = api_def_prop(sapi, "pivot_point", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, pivot_points);
  api_def_prop_ui_text(prop, "Pivot Point", "Rotation or scaling pivot point");
}

static void api_def_unified_paint_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem brush_size_unit_items[] = {
      {0, "VIEW", 0, "View", "Measure brush size relative to the view"},
      {UNIFIED_PAINT_BRUSH_LOCK_SIZE,
       "SCENE",
       0,
       "Scene",
       "Measure brush size relative to the scene"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "UnifiedPaintSettings", NULL);
  api_def_struct_path_fn(sapi, "api_UnifiedPaintSettings_path");
  api_def_struct_ui_text(
      sapi, "Unified Paint Settings", "Overrides for some of the active brush's settings");

  /* high-level flags to enable or disable unified paint settings */
  prop = api_def_prop(sapi, "use_unified_size", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", UNIFIED_PAINT_SIZE);
  api_def_prop_ui_text(prop,
                       "Use Unified Radius",
                       "Instead of per-brush radius, the radius is shared across brushes");

  prop = api_def_prop(sapi, "use_unified_strength", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", UNIFIED_PAINT_ALPHA);
  api_def_prop_ui_text(prop,
                       "Use Unified Strength",
                       "Instead of per-brush strength, the strength is shared across brushes");

  prop = api_def_prop(sapi, "use_unified_weight", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", UNIFIED_PAINT_WEIGHT);
  api_def_prop_ui_text(prop,
                       "Use Unified Weight",
                       "Instead of per-brush weight, the weight is shared across brushes");

  prop = api_def_prop(sapi, "use_unified_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", UNIFIED_PAINT_COLOR);
  api_def_prop_ui_text(
      prop, "Use Unified Color", "Instead of per-brush color, the color is shared across brushes");

  /* unified paint settings that override the equivalent settings
   * from the active brush */
  prop = api_def_prop(sapi, "size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_fns(prop, NULL, "api_UnifiedPaintSettings_size_set", NULL);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 1, MAX_BRUSH_PIXEL_RADIUS * 10);
  api_def_prop_ui_range(prop, 1, MAX_BRUSH_PIXEL_RADIUS, 1, -1);
  api_def_prop_ui_text(prop, "Radius", "Radius of the brush");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_radius_update");

  prop = api_def_prop(sapi, "unprojected_radius", PROP_FLOAT, PROP_DISTANCE);
  aoi_def_prop_float_fns(
      prop, NULL, "api_UnifiedPaintSettings_unprojected_radius_set", NULL);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 0.001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001, 1, 1, -1);
  api_def_prop_ui_text(prop, "Unprojected Radius", "Radius of brush in Blender units");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_radius_update");

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "alpha");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(
      prop, "Strength", "How powerful the effect of the brush is when applied");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_update");

  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "weight");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.001, 3);
  api_def_prop_ui_text(prop, "Weight", "Weight to assign in vertex groups");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_update");

  prop = api_def_prop(dapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "rgb");
  api_def_prop_ui_text(prop, "Color", "");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_update");

  prop = api_def_prop(sapi, "secondary_color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "secondary_rgb");
  api_def_prop_ui_text(prop, "Secondary Color", "");
  api_def_prop_update(prop, 0, "api_UnifiedPaintSettings_update");

  prop = api_def_prop(sapi, "use_locked_size", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, brush_size_unit_items);
  api_def_prop_ui_text(
      prop, "Radius Unit", "Measure brush size relative to the view or the scene");
}

static void api_def_curve_paint_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "CurvePaintSettings", NULL);
  api_def_struct_path_fn(sapi, "api_CurvePaintSettings_path");
  api_def_struct_ui_text(sapi, "Curve Paint Settings", "");

  static const EnumPropItem curve_type_items[] = {
      {CU_POLY, "POLY", 0, "Poly", ""},
      {CU_BEZIER, "BEZIER", 0, "Bezier", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "curve_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "curve_type");
  api_def_prop_enum_items(prop, curve_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of curve to use for new strokes");

  prop = api_def_prop(sapi, "use_corners_detect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CURVE_PAINT_FLAG_CORNERS_DETECT);
  api_def_prop_ui_text(prop, "Detect Corners", "Detect corners and use non-aligned handles");

  prop = api_def_prop(sapi, "use_pressure_radius", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CURVE_PAINT_FLAG_PRESSURE_RADIUS);
  api_def_prop_ui_icon(prop, ICON_STYLUS_PRESSURE, 0);
  api_def_prop_ui_text(prop, "Use Pressure", "Map tablet pressure to curve radius");

  prop = api_def_prop(sapi, "use_stroke_endpoints", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CURVE_PAINT_FLAG_DEPTH_STROKE_ENDPOINTS);
  api_def_prop_ui_text(prop, "Only First", "Use the start of the stroke for the depth");

  prop = api_def_prop(sapi, "use_offset_absolute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CURVE_PAINT_FLAG_DEPTH_STROKE_OFFSET_ABS);
  api_def_prop_ui_text(
      prop, "Absolute Offset", "Apply a fixed offset (don't scale by the radius)");

  prop = api_def_prop(sapi, "error_threshold", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 100);
  api_def_prop_ui_text(prop, "Tolerance", "Allow deviation for a smoother, less precise line");

  prop = api_def_prop(sapi, "fit_method", PROP_ENUM, PROP_PIXEL);
  api_def_prop_enum_stype(prop, NULL, "fit_method");
  api_def_prop_enum_items(prop, api_enum_curve_fit_method_items);
  api_def_prop_ui_text(prop, "Method", "Curve fitting method");

  prop = api_def_prop(sapi, "corner_angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, 0, M_PI);
  api_def_prop_ui_text(prop, "Corner Angle", "Angles above this are considered corners");

  prop = api_def_prop(sapi, "radius_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0f, 10.0, 10, 2);
  api_def_prop_ui_text(
      prop,
      "Radius Min",
      "Minimum radius when the minimum pressure is applied (also the minimum when tapering)");

  prop = api_def_prop(sapi, "radius_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0f, 10.0, 10, 2);
  api_def_prop_ui_text(
      prop,
      "Radius Max",
      "Radius to use when the maximum pressure is applied (or when a tablet isn't used)");

  prop = api_def_prop(sapi, "radius_taper_start", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_range(prop, 0.0f, 1.0, 1, 2);
  api_def_prop_ui_text(
      prop, "Radius Min", "Taper factor for the radius of each point along the curve");

  prop = api_def_prop(sapi, "radius_taper_end", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 10.0);
  api_def_prop_ui_range(prop, 0.0f, 1.0, 1, 2);
  api_def_prop_ui_text(
      prop, "Radius Max", "Taper factor for the radius of each point along the curve");

  prop = api_def_prop(sapi, "surface_offset", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, -10.0, 10.0);
  api_def_prop_ui_range(prop, -1.0f, 1.0, 1, 2);
  api_def_prop_ui_text(prop, "Offset", "Offset the stroke from the surface");

  static const EnumPropItem depth_mode_items[] = {
      {CURVE_PAINT_PROJECT_CURSOR, "CURSOR", 0, "Cursor", ""},
      {CURVE_PAINT_PROJECT_SURFACE, "SURFACE", 0, "Surface", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "depth_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "depth_mode");
  api_def_prop_enum_items(prop, depth_mode_items);
  api_def_prop_ui_text(prop, "Depth", "Method of projecting depth");

  static const EnumPropItem surface_plane_items[] = {
      {CURVE_PAINT_SURFACE_PLANE_NORMAL_VIEW,
       "NORMAL_VIEW",
       0,
       "Normal to Surface",
       "Draw in a plane perpendicular to the surface"},
      {CURVE_PAINT_SURFACE_PLANE_NORMAL_SURFACE,
       "NORMAL_SURFACE",
       0,
       "Tangent to Surface",
       "Draw in the surface plane"},
      {CURVE_PAINT_SURFACE_PLANE_VIEW,
       "VIEW",
       0,
       "View",
       "Draw in a plane aligned to the viewport"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "surface_plane", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "surface_plane");
  api_def_prop_enum_items(prop, surface_plane_items);
  api_def_prop_ui_text(prop, "Plane", "Plane for projected stroke");
}

static void api_def_statvis(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem stat_type[] = {
      {SCE_STATVIS_OVERHANG, "OVERHANG", 0, "Overhang", ""},
      {SCE_STATVIS_THICKNESS, "THICKNESS", 0, "Thickness", ""},
      {SCE_STATVIS_INTERSECT, "INTERSECT", 0, "Intersect", ""},
      {SCE_STATVIS_DISTORT, "DISTORT", 0, "Distortion", ""},
      {SCE_STATVIS_SHARP, "SHARP", 0, "Sharp", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MeshStatVis", NULL);
  api_def_struct_path_fn(sapi, "api_MeshStatVis_path");
  api_def_struct_ui_text(sapi, "Mesh Visualize Statistics", "");

  prop = api_def_prop(spi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, stat_type);
  api_def_prop_ui_text(prop, "Type", "Type of data to visualize/check");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  /* overhang */
  prop = api_def_prop(sapi, "overhang_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "overhang_min");
  api_def_prop_range(prop, 0.0f, DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Overhang Min", "Minimum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = api_def_prop(sapi, "overhang_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "overhang_max");
  api_def_prop_range(prop, 0.0f, DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Overhang Max", "Maximum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = api_def_prop(sapi, "overhang_axis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "overhang_axis");
  api_def_prop_enum_items(prop, api_enum_object_axis_items);
  api_def_prop_ui_text(prop, "Axis", "");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  /* thickness */
  prop = api_def_prop(sapi, "thickness_min", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "thickness_min");
  api_def_prop_range(prop, 0.0f, 1000.0);
  api_def_prop_ui_range(prop, 0.0f, 100.0, 0.001, 3);
  api_def_prop_ui_text(prop, "Thickness Min", "Minimum for measuring thickness");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = aoi_def_prop(sapi, "thickness_max", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "thickness_max");
  api_def_prop_range(prop, 0.0f, 1000.0);
  api_def_prop_ui_range(prop, 0.0f, 100.0, 0.001, 3);
  api_def_prop_ui_text(prop, "Thickness Max", "Maximum for measuring thickness");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = api_def_prop(sapi, "thickness_samples", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "thickness_samples");
  api_def_prop_range(prop, 1, 32);
  api_def_prop_ui_text(prop, "Samples", "Number of samples to test per face");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  /* distort */
  prop = api_def_prop(sapi, "distort_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "distort_min");
  api_def_prop_range(prop, 0.0f, DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Distort Min", "Minimum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = api_def_prop(sapi, "distort_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "distort_max");
  api_def_prop_range(prop, 0.0f, DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Distort Max", "Maximum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  /* sharp */
  prop = api_def_prop(sapi, "sharp_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "sharp_min");
  api_def_prop_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Distort Min", "Minimum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");

  prop = api_def_prop(sapi, "sharp_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "sharp_max");
  api_def_prop_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f));
  api_def_prop_ui_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f), 10, 3);
  api_def_prop_ui_text(prop, "Distort Max", "Maximum angle to display");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, 0, "api_EditMesh_update");
}

static void api_def_unit_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem unit_systems[] = {
      {USER_UNIT_NONE, "NONE", 0, "None", ""},
      {USER_UNIT_METRIC, "METRIC", 0, "Metric", ""},
      {USER_UNIT_IMPERIAL, "IMPERIAL", 0, "Imperial", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem rotation_units[] = {
      {0, "DEGREES", 0, "Degrees", "Use degrees for measuring angles and rotations"},
      {USER_UNIT_ROT_RADIANS, "RADIANS", 0, "Radians", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "UnitSettings", NULL);
  api_def_struct_ui_text(sapi, "Unit Settings", "");
  api_def_struct_nested(dapi, sapi, "Scene");
  api_def_struct_path_fn(sapi, "api_UnitSettings_path");

  /* Units */
  prop = api_def_prop(sapi, "system", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, unit_systems);
  api_def_prop_ui_text(
      prop, "Unit System", "The unit system to use for user interface controls");
  api_def_prop_update(prop, NC_WINDOW, "api_UnitSettings_system_update");

  prop = api_def_prop(sapi, "system_rotation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, rotation_units);
  api_def_prop_ui_text(
      prop, "Rotation Units", "Unit to use for displaying/editing rotation values");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "scale_length", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_ui_text(
      prop,
      "Unit Scale",
      "Scale to use when converting between Blender units and dimensions."
      " When working at microscopic or astronomical scale, a small or large unit scale"
      " respectively can be used to avoid numerical precision problems");
  api_def_prop_range(prop, 1e-9f, 1e+9f);
  api_def_prop_ui_range(prop, 0.001, 100.0, 0.1, 6);
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "use_separate", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_UNIT_OPT_SPLIT);
  api_def_prop_ui_text(prop, "Separate Units", "Display units in pairs (e.g. 1m 0cm)");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "length_unit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, DummyApi_DEFAULT_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_UnitSettings_length_unit_itemf");
  api_def_prop_ui_text(prop, "Length Unit", "Unit that will be used to display length values");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "mass_unit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, DummyApi_DEFAULT_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_UnitSettings_mass_unit_itemf");
  api_def_prop_ui_text(prop, "Mass Unit", "Unit that will be used to display mass values");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "time_unit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, DummyApi_DEFAULT_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "rna_UnitSettings_time_unit_itemf");
  api_def_prop_ui_text(prop, "Time Unit", "Unit that will be used to display time values");
  api_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "temperature_unit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, DummyApi_DEFAULT_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_UnitSettings_temperature_unit_itemf");
  api_def_prop_ui_text(
      prop, "Temperature Unit", "Unit that will be used to display temperature values");
  api_def_prop_update(prop, NC_WINDOW, NULL);
}

static void api_def_view_layer_eevee(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  sapi = api_def_struct(dapi, "ViewLayerEEVEE", NULL);
  api_def_struct_path_fn(sapi, "api_ViewLayerEEVEE_path");
  api_def_struct_ui_text(sapi, "Eevee Settings", "View layer settings for Eevee");

  
  prop = api_def_prop(sapi, "use_pass_volume_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "render_passes", EEVEE_RENDER_PASS_VOLUME_LIGHT);
  api_def_prop_ui_text(prop, "Volume Light", "Deliver volume direct light pass");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");

  prop = api_def_prop(sapi, "use_pass_bloom", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "render_passes", EEVEE_RENDER_PASS_BLOOM);
  api_def_prop_ui_text(prop, "Bloom", "Deliver bloom pass");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
}

static void api_def_view_layer_aovs(DuneApi *dapi, PropertyRNA *cprop)
{
  ApiStruct *sapi;
  // ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "AOVs");
  sapi = api_def_struct(dapi, "AOVs", NULL);
  api_def_struct_stype(sapi, "ViewLayer");
  api_def_struct_ui_text(sapi, "List of AOVs", "Collection of AOVs");

  fn = api_def_fn(sapi, "add", "dune_view_layer_add_aov");
  parm = api_def_ptr(fn, "aov", "AOV", "", "Newly created AOV");
  api_def_fn_return(fn, parm);

  /* Defined in `api_layer.c`. */
  fn = api_def_fn(sapi, "remove", "api_ViewLayer_remove_aov");
  parm = api_def_ptr(fn, "aov", "AOV", "", "AOV to remove");
  api_def_fn_ui_description(fn, "Remove an AOV");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_view_layer_aov(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  sapi = api_def_struct(dapi, "AOV", NULL);
  api_def_struct_stype(sapi, "ViewLayerAOV");
  api_def_struct_ui_text(sapi, "Shader AOV", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "name");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Name", "Name of the AOV");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "is_valid", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", AOV_CONFLICT);
  api_def_prop_ui_text(prop, "Valid", "Is the name of the AOV conflicting");

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  apj_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_view_layer_aov_type_items);
  api_def_prop_enum_default(prop, AOV_TYPE_COLOR);
  api_def_prop_ui_text(prop, "Type", "Data type of the AOV");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
}

static void api_def_view_layer_lightgroups(DuneApo *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  /*  ApiProp *prop; */

  ApiFn *fn;
  ApiParam *parm;

  api_def_prop_sapi(cprop, "Lightgroups");
  sapi = api_def_struct(dapi, "Lightgroups", NULL);
  api_def_struct_stype(sapi, "ViewLayer");
  api_def_struct_ui_text(sapi, "List of Lightgroups", "Collection of Lightgroups");

  fn = api_def_fn(sapi, "add", "dune_view_layer_add_lightgroup");
  parm = api_def_ptr(fb, "lightgroup", "Lightgroup", "", "Newly created Lightgroup");
  api_def_fn_return(fn, parm);
  parm = api_def_string(fn, "name", NULL, 0, "Name", "Name of newly created lightgroup");
}

static void api_def_view_layer_lightgroup(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  sapi = api_def_struct(dapi, "Lightgroup", NULL);
  api_def_struct_stype(sapi, "ViewLayerLightgroup");
  api_def_struct_ui_text(sapi, "Light Group", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_string_fns(prop,
                          "api_ViewLayerLightgroup_name_get",
                          "api_ViewLayerLightgroup_name_length",
                          "api_ViewLayerLightgroup_name_set");
  api_def_prop_ui_text(prop, "Name", "Name of the Lightgroup");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  api_def_struct_name_prop(sapi, prop);a
}

void api_def_view_layer_common(DuneApi *dapi, ApiStruct *sapi, const bool scene)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  if (scene) {
    api_def_prop_string_fns(prop, NULL, NULL, "api_ViewLayer_name_set");
  } else {
    api_def_prop_string_stype(prop, NULL, "name");
  }
  api_def_prop_ui_text(prop, "Name", "View layer name");
  api_def_struct_name_prop(sapi, prop);
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  if (scene) {
    prop = api_def_prop(sapi, "material_override", PROP_PTR, PROP_NONE);
    api_def_prop_ptr_stype(prop, NULL, "mat_override");
    api_def_prop_struct_type(prop, "Material");
    api_def_prop_flag(prop, PROP_EDITABLE);
    api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
    api_def_prop_ui_text(
        prop, "Material Override", "Material to override all other materials in this view layer");
    api_def_prop_update(
        prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_material_override_update");

    prop = api_def_prop(sapi, "samples", PROP_INT, PROP_UNSIGNED);
    api_def_prop_ui_text(prop,
                        "Samples",
                        "Override number of render samples for this view layer, "
                        "0 will use the scene setting");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

    prop = api_def_prop(sapi, "pass_alpha_threshold", PROP_FLOAT, PROP_FACTOR);
    api_def_prop_ui_text(
        prop,
        "Alpha Threshold",
        "Z, Index, normal, UV and vector passes are only affected by surfaces with "
        "alpha transparency equal to or higher than this threshold");
    apj_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

    prop = api_def_prop(sapi, "eevee", PROP_PTR, PROP_NONE);
    api_def_prop_flag(prop, PROP_NEVER_NULL);
    api_def_prop_struct_type(prop, "ViewLayerEEVEE");
    api_def_prop_ui_text(prop, "Eevee Settings", "View layer settings for Eevee");

    prop = api_def_prop(sapi, "aovs", PROP_COLLECTION, PROP_NONE);
    api_def_prop_collection_stype(prop, NULL, "aovs", NULL);
    api_def_prop_struct_type(prop, "AOV");
    api_def_prop_ui_text(prop, "Shader AOV", "");
    api_def_view_layer_aovs(dapi, prop);

    prop = api_def_prop(sapi, "active_aov", PROP_PTR, PROP_NONE);
    api_def_prop_struct_type(prop, "AOV");
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
    api_def_prop_ui_text(prop, "Shader AOV", "Active AOV");

    prop = api_def_prop(sapi, "active_aov_index", PROP_INT, PROP_UNSIGNED);
    api_def_prop_int_fns(prop,
                         "api_ViewLayer_active_aov_index_get",
                         "api_ViewLayer_active_aov_index_set",
                         "api_ViewLayer_active_aov_index_range");
    api_def_prop_ui_text(prop, "Active AOV Index", "Index of active AOV");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

    prop = api_def_prop(sapi, "lightgroups", PROP_COLLECTION, PROP_NONE);
    api_def_prop_collection_stype(prop, NULL, "lightgroups", NULL);
    api_def_prop_struct_type(prop, "Lightgroup");
    api_def_prop_ui_text(prop, "Light Groups", "");
    api_def_view_layer_lightgroups(dapi, prop);

    prop = api_def_prop(sapi, "active_lightgroup", PROP_PTR, PROP_NONE);
    api_def_prop_struct_type(prop, "Lightgroup");
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
    api_def_prop_ui_text(prop, "Light Groups", "Active Lightgroup");

    prop = api_def_prop(sapi, "active_lightgroup_index", PROP_INT, PROP_UNSIGNED);
    api_def_prop_int_fns(prop,
                         "api_ViewLayer_active_lightgroup_index_get",
                         "api_ViewLayer_active_lightgroup_index_set",
                         "api_ViewLayer_active_lightgroup_index_range");
    api_def_prop_ui_text(prop, "Active Lightgroup Index", "Index of active lightgroup");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

    prop = api_def_prop(sapi, "use_pass_cryptomatte_object", PROP_BOOLEAN, PROP_NONE);
    api_def_prop_bool_stype(prop, NULL, "cryptomatte_flag", VIEW_LAYER_CRYPTOMATTE_OBJECT);
    api_def_prop_ui_text(
        prop,
        "Cryptomatte Object",
        "Render cryptomatte object pass, for isolating objects in compositing");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");

    prop = api_def_prop(sapi, "use_pass_cryptomatte_material", PROP_BOOLEAN, PROP_NONE);
    api_def_prop_bool_stype(prop, NULL, "cryptomatte_flag", VIEW_LAYER_CRYPTOMATTE_MATERIAL);
    api_def_prop_ui_text(
        prop,
        "Cryptomatte Material",
        "Render cryptomatte material pass, for isolating materials in compositing");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");

    prop = api_def_prop(sapi, "use_pass_cryptomatte_asset", PROP_BOOLEAN, PROP);
    api_def_prop_bool_stype(prop, NULL, "cryptomatte_flag", VIEW_LAYER_CRYPTOMATTE_ASSET);
    api_def_prop_ui_text(
        prop,
        "Cryptomatte Asset",
        "Render cryptomatte asset pass, for isolating groups of objects with the same parent");
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");

    prop = api_def_prop(sapi, "pass_cryptomatte_depth", PROP_INT, PROP_NONE);
    api_def_prop_int_stype(prop, NULL, "cryptomatte_levels");
    api_def_prop_int_default(prop, 6);
    api_def_prop_range(prop, 2.0, 16.0);
    apj_def_prop_ui_text(
        prop, "Cryptomatte Levels", "Sets how many unique objects can be distinguished per pixel");
    api_def_prop_ui_range(prop, 2.0, 16.0, 2.0, 0.0);
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");

    prop = api_def_prop(sapi, "use_pass_cryptomatte_accurate", PROP_BOOL, PROP_NONE);
    api_def_prop_bool_stype(prop, NULL, "cryptomatte_flag", VIEW_LAYER_CRYPTOMATTE_ACCURATE);
    api_def_prop_bool_default(prop, true);
    api_def_prop_ui_text(
        prop, "Cryptomatte Accurate", "Generate a more accurate cryptomatte pass");
    ali_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  }

  prop = api_def_prop(sapi, "use_solid", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_SOLID);
  api_def_prop_ui_text(prop, "Solid", "Render Solid faces in this Layer");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }
  prop = api_def_prop(sapi, "use_sky", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_SKY);
  api_def_prop_ui_text(prop, "Sky", "Render Sky in this Layer");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_render_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_ao", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_AO);
  api_def_prop_ui_text(prop, "Ambient Occlusion", "Render Ambient Occlusion in this Layer");
  if (scene) {
    ali_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_render_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_strand", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_STRAND);
  api_def_prop_ui_text(prop, "Strand", "Render Strands in this Layer");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_volumes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_VOLUMES);
  api_def_prop_ui_text(prop, "Volumes", "Render volumes in this Layer");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_motion_blur", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "layflag", SCE_LAY_MOTION_BLUR);
  api_def_prop_ui_text(
      prop, "Motion Blur", "Render motion blur in this Layer, if enabled in the scene");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  /* passes */
  prop = api_def_prop(sapi, "use_pass_combined", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_COMBINED);
  api_def_prop_ui_text(prop, "Combined", "Deliver full combined RGBA buffer");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_Z);
  api_def_prop_ui_text(prop, "Z", "Deliver Z values pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_vector", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_VECTOR);
  api_def_prop_ui_text(prop, "Vector", "Deliver speed vector pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_position", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_POSITION);
  api_def_prop_ui_text(prop, "Position", "Deliver position pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_pro_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_normal", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_NORMAL);
  qpi_def_prop_ui_text(prop, "Normal", "Deliver normal pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_uv", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_UV);
  api_def_prop_ui_text(prop, "UV", "Deliver texture UV pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_mist", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_MIST);
  api_def_prop_ui_text(prop, "Mist", "Deliver mist factor pass (0.0 to 1.0)");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_object_index", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_INDEXOB);
  api_def_prop_ui_text(prop, "Object Index", "Deliver object index pass");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SCENE);
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_material_index", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_INDEXMA);
  api_def_prop_ui_text(prop, "Material Index", "Deliver material index pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }
  prop = api_def_prop(sapi, "use_pass_shadow", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_SHADOW);
  api_def_prop_ui_text(prop, "Shadow", "Deliver shadow pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_ambient_occlusion", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_AO);
  api_def_prop_ui_text(prop, "Ambient Occlusion", "Deliver Ambient Occlusion pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_emit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_EMIT);
  api_def_prop_ui_text(prop, "Emit", "Deliver emission pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_environment", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_ENVIRONMENT);
  api_def_prop_ui_text(prop, "Environment", "Deliver environment lighting pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_diffuse_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_DIFFUSE_DIRECT);
  api_def_prop_ui_text(prop, "Diffuse Direct", "Deliver diffuse direct pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_diffuse_indirect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_DIFFUSE_INDIRECT);
  api_def_prop_ui_text(prop, "Diffuse Indirect", "Deliver diffuse indirect pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_diffuse_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_DIFFUSE_COLOR);
  api_def_prop_ui_text(prop, "Diffuse Color", "Deliver diffuse color pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_glossy_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_GLOSSY_DIRECT);
  api_def_prop_ui_text(prop, "Glossy Direct", "Deliver glossy direct pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_glossy_indirect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_GLOSSY_INDIRECT);
  api_def_prop_ui_text(prop, "Glossy Indirect", "Deliver glossy indirect pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_glossy_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_GLOSSY_COLOR);
  api_def_prop_ui_text(prop, "Glossy Color", "Deliver glossy color pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_transmission_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_TRANSM_DIRECT);
  api_def_prop_ui_text(prop, "Transmission Direct", "Deliver transmission direct pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_transmission_indirect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_TRANSM_INDIRECT);
  api_def_prop_ui_text(prop, "Transmission Indirect", "Deliver transmission indirect pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_transmission_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_TRANSM_COLOR);
  api_def_prop_ui_text(prop, "Transmission Color", "Deliver transmission color pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_subsurface_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_SUBSURFACE_DIRECT);
  api_def_prop_ui_text(prop, "Subsurface Direct", "Deliver subsurface direct pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_subsurface_indirect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_SUBSURFACE_INDIRECT);
  api_def_prop_ui_text(prop, "Subsurface Indirect", "Deliver subsurface indirect pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }

  prop = api_def_prop(sapi, "use_pass_subsurface_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "passflag", SCE_PASS_SUBSURFACE_COLOR);
  api_def_prop_ui_text(prop, "Subsurface Color", "Deliver subsurface color pass");
  if (scene) {
    api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_ViewLayer_pass_update");
  } else {
    api_def_prop_clear_flag(prop, PROP_EDITABLE);
  }
}

static void api_def_freestyle_modules(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "FreestyleModules");
  sapi = api_def_struct(dapi, "FreestyleModules", NULL);
  api_def_struct_stype(sapi, "FreestyleSettings");
  api_def_struct_ui_text(
      sapi, "Style Modules", "A list of style modules (to be applied from top to bottom)");

  fn = api_def_fn(sapi, "new", "api_FreestyleSettings_module_add");
  api_def_fn_ui_description(fn, "Add a style module to scene render layer Freestyle settings");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  parm = api_def_ptr(
      fn, "module", "FreestyleModuleSettings", "", "Newly created style module");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_FreestyleSettings_module_remove");
  api_def_fn_ui_description(
      fn, "Remove a style module from scene render layer Freestyle settings");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_REPORTS);
  parm = api_def_ptr(fn, "module", "FreestyleModuleSettings", "", "Style module to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_freestyle_linesets(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "Linesets");
  sapi = api_def_struct(dapi, "Linesets", NULL);
  api_def_struct_stype(sapi, "FreestyleSettings");
  api_def_struct_ui_text(
      sapi, "Line Sets", "Line sets for associating lines and style parameters");

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "FreestyleLineSet");
  api_def_prop_ptr_fns(
      prop, "api_FreestyleSettings_active_lineset_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Active Line Set", "Active line set being displayed");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_fns(prop,
                       "api_FreestyleSettings_active_lineset_index_get",
                       "api_FreestyleSettings_active_lineset_index_set",
                       "api_FreestyleSettings_active_lineset_index_range");
  api_def_prop_ui_text(prop, "Active Line Set Index", "Index of active line set slot");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  fn = api_def_fn(sapi, "new", "api_FreestyleSettings_lineset_add");
  api_def_fn_ui_description(fn, "Add a line set to scene render layer Freestyle settings");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_SELF_ID);
  parm = api_def_string(fn, "name", "LineSet", 0, "", "New name for the line set (not unique)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "lineset", "FreestyleLineSet", "", "Newly created line set");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_FreestyleSettings_lineset_remove");
  api_def_fn_ui_description(fn,
                                  "Remove a line set from scene render layer Freestyle settings");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_REPORTS);
  parm = api_def_ptr(fn, "lineset", "FreestyleLineSet", "", "Line set to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

void api_def_freestyle_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem edge_type_negation_items[] = {
      {0,
       "INCLUSIVE",
       0,
       "Inclusive",
       "Select feature edges satisfying the given edge type conditions"},
      {FREESTYLE_LINESET_FE_NOT,
       "EXCLUSIVE",
       0,
       "Exclusive",
       "Select feature edges not satisfying the given edge type conditions"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem edge_type_combination_items[] = {
      {0,
       "OR",
       0,
       "Logical OR",
       "Select feature edges satisfying at least one of edge type conditions"},
      {FREESTYLE_LINESET_FE_AND,
       "AND",
       0,
       "Logical AND",
       "Select feature edges satisfying all edge type conditions"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem collection_negation_items[] = {
      {0,
       "INCLUSIVE",
       0,
       "Inclusive",
       "Select feature edges belonging to some object in the group"},
      {FREESTYLE_LINESET_GR_NOT,
       "EXCLUSIVE",
       0,
       "Exclusive",
       "Select feature edges not belonging to any object in the group"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem face_mark_negation_items[] = {
      {0,
       "INCLUSIVE",
       0,
       "Inclusive",
       "Select feature edges satisfying the given face mark conditions"},
      {FREESTYLE_LINESET_FM_NOT,
       "EXCLUSIVE",
       0,
       "Exclusive",
       "Select feature edges not satisfying the given face mark conditions"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem face_mark_condition_items[] = {
      {0, "ONE", 0, "One Face", "Select a feature edge if either of its adjacent faces is marked"},
      {FREESTYLE_LINESET_FM_BOTH,
       "BOTH",
       0,
       "Both Faces",
       "Select a feature edge if both of its adjacent faces are marked"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem freestyle_ui_mode_items[] = {
      {FREESTYLE_CONTROL_SCRIPT_MODE,
       "SCRIPT",
       0,
       "Python Scripting",
       "Advanced mode for using style modules written in Python"},
      {FREESTYLE_CONTROL_EDITOR_MODE,
       "EDITOR",
       0,
       "Param Editor",
       "Basic mode for interactive style parameter editing"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem visibility_items[] = {
      {FREESTYLE_QI_VISIBLE, "VISIBLE", 0, "Visible", "Select visible feature edges"},
      {FREESTYLE_QI_HIDDEN, "HIDDEN", 0, "Hidden", "Select hidden feature edges"},
      {FREESTYLE_QI_RANGE,
       "RANGE",
       0,
       "Quantitative Invisibility",
       "Select feature edges within a range of quantitative invisibility (QI) values"},
      {0, NULL, 0, NULL, NULL},
  };

  /* FreestyleLineSet */
  sapi = api_def_struct(dapi, "FreestyleLineSet", NULL);
  api_def_struct_ui_text(
      sapu, "Freestyle Line Set", "Line set for associating lines and style parameters");

  /* access to line style settings is redirected through functions
   * to allow proper id-buttons functionality */
  prop = api_def_prop(sapi, "linestyle", PROP_PRR, PROP_NONE);
  api_def_prop_struct_type(prop, "FreestyleLineStyle");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
  api_def_prop_ptr_fns(prop,
                       "api_FreestyleLineSet_linestyle_get",
                       "api_FreestyleLineSet_linestyle_set",
                       NULL,
                       NULL);
  api_def_prop_ui_text(prop, "Line Style", "Line style settings");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "name");
  api_def_prop_ui_text(prop, "Line Set Name", "Line set name");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "show_render", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_LINESET_ENABLED);
  api_def_prop_ui_text(
      prop, "Render", "Enable or disable this line set during stroke rendering");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_by_visibility", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selection", FREESTYLE_SEL_VISIBILITY);
  api_def_prop_ui_text(
      prop, "Selection by Visibility", "Select feature edges based on visibility");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_by_edge_types", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selection", FREESTYLE_SEL_EDGE_TYPES);
  api_def_prop_ui_text(
      prop, "Selection by Edge Types", "Select feature edges based on edge types");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_by_collection", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selection", FREESTYLE_SEL_GROUP);
  api_def_prop_ui_text(
      prop, "Selection by Collection", "Select feature edges based on a collection of objects");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_by_image_border", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selection", FREESTYLE_SEL_IMAGE_BORDER);
  api_def_prop_ui_text(prop,
                       "Selection by Image Border",
                       "Select feature edges by image border (less memory consumption)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_by_face_marks", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "selection", FREESTYLE_SEL_FACE_MARK);
  api_def_prop_ui_text(prop, "Selection by Face Marks", "Select feature edges by face marks");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "edge_type_negation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, edge_type_negation_items);
  api_def_prop_ui_text(
      prop,
      "Edge Type Negation",
      "Specify either inclusion or exclusion of feature edges selected by edge types");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  prop = api_def_prop(sapi, "edge_type_combination", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, edge_type_combination_items);
  api_def_prop_ui_text(
      prop,
      "Edge Type Combination",
      "Specify a logical combination of selection conditions on feature edge types");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "collection", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "group");
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Collection", "A collection of objects based on which feature edges are selected");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "collection_negation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, collection_negation_items);
  api_def_prop_ui_text(prop,
                       "Collection Negation",
                       "Specify either inclusion or exclusion of feature edges belonging to a "
                       "collection of objects");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "face_mark_negation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, face_mark_negation_items);
  api_def_prop_ui_text(
      prop,
      "Face Mark Negation",
      "Specify either inclusion or exclusion of feature edges selected by face marks");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "face_mark_condition", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_sdna(prop, NULL, "flags");
  api_def_prop_enum_items(prop, face_mark_condition_items);
  api_def_prop_ui_text(prop,
                       "Face Mark Condition",
                       "Specify a feature edge selection condition based on face marks");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_silhouette", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_SILHOUETTE);
  api_def_prop_ui_text(
      prop,
      "Silhouette",
      "Select silhouettes (edges at the boundary of visible and hidden faces)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_border", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_BORDER);
  api_def_prop_ui_text(prop, "Border", "Select border edges (open mesh edges)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_crease", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_CREASE);
  api_def_prop_ui_text(prop,
                       "Crease",
                       "Select crease edges (those between two faces making an angle smaller "
                       "than the Crease Angle)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_ridge_valley", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_RIDGE_VALLEY);
  api_def_prop_ui_text(
      prop,
      "Ridge & Valley",
      "Select ridges and valleys (boundary lines between convex and concave areas of surface)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_suggestive_contour", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_SUGGESTIVE_CONTOUR);
  api_def_prop_ui_text(
      prop, "Suggestive Contour", "Select suggestive contours (almost silhouette/contour edges)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_material_boundary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_MATERIAL_BOUNDARY);
  api_def_prop_ui_text(prop, "Material Boundary", "Select edges at material boundaries");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_contour", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_CONTOUR);
  api_def_prop_ui_text(prop, "Contour", "Select contours (outer silhouettes of each object)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_external_contour", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_EXTERNAL_CONTOUR);
  api_def_prop_ui_text(
      prop,
      "External Contour",
      "Select external contours (outer silhouettes of occluding and occluded objects)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "select_edge_mark", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edge_types", FREESTYLE_FE_EDGE_MARK);
  api_def_prop_ui_text(
      prop, "Edge Mark", "Select edge marks (edges annotated by Freestyle edge marks)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_silhouette", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_SILHOUETTE);
  api_def_prop_ui_text(prop, "Silhouette", "Exclude silhouette edges");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_border", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_BORDER);
  api_def_prop_ui_text(prop, "Border", "Exclude border edges");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_crease", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_CREASE);
  api_def_prop_ui_text(prop, "Crease", "Exclude crease edges");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_ridge_valley", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_RIDGE_VALLEY);
  api_def_prop_ui_text(prop, "Ridge & Valley", "Exclude ridges and valleys");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_suggestive_contour", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_SUGGESTIVE_CONTOUR);
  api_def_prop_ui_text(prop, "Suggestive Contour", "Exclude suggestive contours");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_material_boundary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_MATERIAL_BOUNDARY);
  api_def_prop_ui_text(prop, "Material Boundary", "Exclude edges at material boundaries");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_contour", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_CONTOUR);
  api_def_prop_ui_text(prop, "Contour", "Exclude contours");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_external_contour", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_EXTERNAL_CONTOUR);
  api_def_prop_ui_text(prop, "External Contour", "Exclude external contours");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "exclude_edge_mark", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "exclude_edge_types", FREESTYLE_FE_EDGE_MARK);
  api_def_prop_ui_text(prop, "Edge Mark", "Exclude edge marks");
  api_def_prop_ui_icon(prop, ICON_X, 0);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "visibility", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "qi");
  api_def_prop_enum_items(prop, visibility_items);
  api_def_prop_ui_text(
      prop, "Visibility", "Determine how to use visibility for feature edge selection");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "qi_start", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "qi_start");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(prop, "Start", "First QI value of the QI range");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "qi_end", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "qi_end");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(prop, "End", "Last QI value of the QI range");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  /* FreestyleModuleSettings */
  sapi = api_def_struct(dapi, "FreestyleModuleSettings", NULL);
  api_def_struct_stype(sapi, "FreestyleModuleConfig");
  api_def_struct_ui_text(
      sapi, "Freestyle Module", "Style module configuration for specifying a style module");

  prop = api_def_prop(sapi, "script", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Text");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Style Module", "Python script to define a style module");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "is_displayed", 1);
  api_def_prop_ui_text(
      prop, "Use", "Enable or disable this style module during stroke rendering");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  /* FreestyleSettings */
  sapi = api_def_struct(dapi, "FreestyleSettings", NULL);
  api_def_struct_stype(sapi, "FreestyleConfig");
  api_def_struct_nested(dapi, sapi, "ViewLayer");
  api_def_struct_ui_text(
      sapi, "Freestyle Settings", "Freestyle settings for a ViewLayer data-block");

  prop = api_def_prop(sapi, "modules", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "modules", NULL);
  api_def_prop_struct_type(prop, "FreestyleModuleSettings");
  api_def_prop_ui_text(
      prop, "Style Modules", "A list of style modules (to be applied from top to bottom)");
  api_def_freestyle_modules(dapi, prop);

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, freestyle_ui_mode_items);
  api_def_prop_ui_text(prop, "Control Mode", "Select the Freestyle control mode");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = ali_def_prop(sapi, "use_culling", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_CULLING);
  api_def_prop_ui_text(prop, "Culling", "If enabled, out-of-view edges are ignored");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use_suggestive_contours", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_SUGGESTIVE_CONTOURS_FLAG);
  api_def_prop_ui_text(prop, "Suggestive Contours", "Enable suggestive contours");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use_ridges_and_valleys", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_RIDGES_AND_VALLEYS_FLAG);
  api_def_prop_ui_text(prop, "Ridges and Valleys", "Enable ridges and valleys");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use_material_boundaries", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_MATERIAL_BOUNDARIES_FLAG);
  api_def_prop_ui_text(prop, "Material Boundaries", "Enable material boundaries");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use_smoothness", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_FACE_SMOOTHNESS_FLAG);
  api_def_prop_ui_text(
      prop, "Face Smoothness", "Take face smoothness into account in view map calculation");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "use_view_map_cache", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_VIEW_MAP_CACHE);
  api_def_prop_ui_text(
      prop,
      "View Map Cache",
      "Keep the computed view map and avoid recalculating it if mesh geometry is unchanged");
  api_def_prop_update(
      prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_use_view_map_cache_update");

  prop = api_def_prop(sapi, "as_render_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FREESTYLE_AS_RENDER_PASS);
  api_def_prop_ui_text(
      prop,
      "As Render Pass",
      "Renders Freestyle output to a separate pass instead of overlaying it on the Combined pass");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_ViewLayer_pass_update");

  prop = api_def_prop(sapi, "sphere_radius", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "sphere_radius");
  apk_def_prop_float_default(prop, 1.0);
  api_def_prop_range(prop, 0.0, 1000.0);
  api_def_prop_ui_text(prop, "Sphere Radius", "Sphere radius for computing curvatures");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "kr_derivative_epsilon", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_default(prop, 0.0);
  api_def_prop_float_stype(prop, NULL, "dkr_epsilon");
  api_def_prop_range(prop, -1000.0, 1000.0);
  api_def_prop_ui_text(
      prop, "Kr Derivative Epsilon", "Kr derivative epsilon for computing suggestive contours");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "crease_angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "crease_angle");
  apj_def_prop_range(prop, 0.0, DEG2RAD(180.0));
  api_def_prop_ui_text(prop, "Crease Angle", "Angular threshold for detecting crease edges");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "linesets", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "linesets", NULL);
  api_def_prop_struct_type(prop, "FreestyleLineSet");
  api_def_prop_ui_text(prop, "Line Sets", "");
  api_def_freestyle_linesets(dapi, prop);
}

static void api_def_bake_data(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BakeSettings", NULL);
  api_def_struct_stype(sapi, "BakeData");
  api_def_struct_nested(dapi, sapi, "RenderSettings");
  api_def_struct_ui_text(sapi, "Bake Data", "Bake data for a Scene data-block");
  api_def_struct_path_fn(sapi, "api_BakeSettings_path");

  prop = api_def_prop(sapi, "cage_object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "Cage Object",
      "Object to use as cage "
      "instead of calculating the cage from the active object with cage extrusion");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_ui_text(prop, "File Path", "Image filepath to use when saving externally");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
    
  prop = api_def_prop(sapi, "width", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 4, 10000);
  api_def_prop_ui_text(prop, "Width", "Horizontal dimension of the baking map");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "height", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 4, 10000);
  api_def_prop_ui_text(prop, "Height", "Vertical dimension of the baking map");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "margin", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, SHRT_MAX);
  api_def_prop_ui_range(prop, 0, 64, 1, 1);
  api_def_prop_ui_text(prop, "Margin", "Extends the baked result as a post process filter");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "margin_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_bake_margin_type_items);
  api_def_prop_ui_text(prop, "Margin Type", "Algorithm to extend the baked result");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "max_ray_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 1, 3);
  api_def_prop_ui_text(prop,
                       "Max Ray Distance",
                       "The maximum ray distance for matching points between the active and "
                       "selected objects. If zero, there is no limit");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "cage_extrusion", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 1, 3);
  api_def_prop_ui_text(
      prop,
      "Cage Extrusion",
      "Inflate the active object by the specified distance for baking. This helps matching to "
      "points nearer to the outside of the selected object meshes");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "normal_space", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "normal_space");
  api_def_prop_enum_items(prop, api_enum_normal_space_items);
  api_def_prop_ui_text(prop, "Normal Space", "Choose normal space for baking");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "normal_r", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "normal_swizzle[0]");
  api_def_prop_enum_items(prop, api_enum_normal_swizzle_items);
  api_def_prop_ui_text(prop, "Normal Space", "Axis to bake in red channel");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "normal_g", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "normal_swizzle[1]");
  api_def_prop_enum_items(prop, api_enum_normal_swizzle_items);
  api_def_prop_ui_text(prop, "Normal Space", "Axis to bake in green channel");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "normal_b", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "normal_swizzle[2]");
  api_def_prop_enum_items(prop, api_enum_normal_swizzle_items);
  api_def_prop_ui_text(prop, "Normal Space", "Axis to bake in blue channel");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "image_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "im_format");
  api_def_prop_struct_type(prop, "ImageFormatSettings");
  api_def_prop_ui_text(prop, "Image Format", "");

  prop = api_def_prop(sapi, "target", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_bake_target_items);
  api_def_prop_ui_text(prop, "Target", "Where to output the baked map");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "save_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "save_mode");
  api_def_prop_enum_items(prop, api_enum_bake_save_mode_items);
  api_def_prop_ui_text(prop, "Save Mode", "Where to save baked image textures");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "view_from", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_bake_view_from_items);
  api_def_prop_ui_text(prop, "View From", "Source of reflection ray directions");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* flags */
  prop = api_def_prop(sapi, "use_selected_to_active", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_BAKE_TO_ACTIVE);
  api_def_prop_ui_text(prop,
                       "Selected to Active",
                       "Bake shading on the surface of selected objects to the active object");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_clear", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_BAKE_CLEAR);
  api_def_prop_ui_text(prop, "Clear", "Clear Images before baking (internal only)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_split_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_BAKE_SPLIT_MAT);
  api_def_prop_ui_text(
      prop, "Split Materials", "Split external images per material (external only)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_automatic_name", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_BAKE_AUTO_NAME);
  api_def_prop_ui_text(
      prop,
      "Automatic Name",
      "Automatically name the output file with the pass type (external only)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_cage", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_BAKE_CAGE);
  api_def_prop_ui_text(prop, "Cage", "Cast rays to active object from a cage");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* custom passes flags */
  prop = api_def_prop(sapi, "use_pass_emit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_EMIT);
  api_def_prop_ui_text(prop, "Emit", "Add emission contribution");

  prop = api_def_prop(sapi, "use_pass_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_DIRECT);
  api_def_prop_ui_text(prop, "Direct", "Add direct lighting contribution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_pass_indirect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_INDIRECT);
  api_def_prop_ui_text(prop, "Indirect", "Add indirect lighting contribution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_pass_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_COLOR);
  api_def_prop_ui_text(prop, "Color", "Color the pass");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_pass_diffuse", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_DIFFUSE);
  api_def_prop_ui_text(prop, "Diffuse", "Add diffuse contribution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_pass_glossy", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_GLOSSY);
  api_def_prop_ui_text(prop, "Glossy", "Add glossy contribution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_pass_transmission", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pass_filter", R_BAKE_PASS_FILTER_TRANSM);
  api_def_prop_ui_text(prop, "Transmission", "Add transmission contribution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = ai_def_prop(sapi, "pass_filter", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "pass_filter");
  api_def_prop_enum_items(prop, api_enum_bake_pass_filter_type_items);
  api_def_prop_flag(prop, PROP_ENUM_FLAG);
  api_def_prop_ui_text(prop, "Pass Filter", "Passes to include in the active baking pass");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
}

static void api_def_view_layers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "ViewLayers");
  sapi = api_def_struct(dapi, "ViewLayers", NULL);
  api_def_struct_stype(sapi, "Scene");
  api_def_struct_ui_text(sapi, "Render Layers", "Collection of render layers");

  fn = api_def_fn(sapi, "new", "api_ViewLayer_new");
  api_def_fn_ui_description(fn, "Add a view layer to scene");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN);
  parm = api_def_string(
      fn, "name", "ViewLayer", 0, "", "New name for the view layer (not unique)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "result", "ViewLayer", "", "Newly created view layer");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_ViewLayer_remove");
  api_def_fn_ui_description(fn, "Remove a view");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN | FN_USE_REPORTS);
  parm = api_def_ptr(fn, "layer", "ViewLayer", "", "View layer to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

/* Render Views - MultiView */
static void api_def_scene_render_view(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "SceneRenderView", NULL);
  api_def_struct_ui_text(
      sapi, "Scene Render View", "Render viewpoint for 3D stereo and multiview rendering");
  api_def_struct_ui_icon(sapi, ICON_RESTRICT_RENDER_OFF);
  api_def_struct_path_fn(sapi, "api_SceneRenderView_path");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop, NULL, NULL, "api_SceneRenderView_name_set");
  api_def_prop_ui_text(prop, "Name", "Render view name");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "file_suffix", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "suffix");
  api_def_prop_ui_text(prop, "File Suffix", "Suffix added to the render images for this view");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "camera_suffix", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "suffix");
  api_def_prop_ui_text(
      prop,
      "Camera Suffix",
      "Suffix to identify the cameras to use, and added to the render images for this view");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "viewflag", SCE_VIEW_DISABLE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Enabled", "Disable or enable the render view");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
}

static void api_def_render_views(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapo;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "RenderViews");
  sapi = api_def_struct(dapi, "RenderViews", NULL);
  api_def_struct_stype(sapi, "RenderData");
  api_def_struct_ui_text(sapi, "Render Views", "Collection of render views");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "actview");
  api_def_prop_int_fns(prop,
                       "api_RenderSettings_active_view_index_get",
                       "api_RenderSettings_active_view_index_set",
                       "api_RenderSettings_active_view_index_range");
  api_def_prop_ui_text(prop, "Active View Index", "Active index in render view array");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "SceneRenderView");
  api_def_prop_ptr_fns(prop,
                       "api_RenderSettings_active_view_get",
                       "api_RenderSettings_active_view_set",
                       NULL,
                       NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Active Render View", "Active Render View");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  func = api_def_fn(sapi, "new", "api_RenderView_new");
  api_def_fn_ui_description(fn, "Add a render view to scene");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  parm = api_def_string(fn, "name", "RenderView", 0, "", "New name for the marker (not unique)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "result", "SceneRenderView", "", "Newly created render view");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_RenderView_remove");
  api_def_fn_ui_description(fn, "Remove a render view");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS | FN_USE_SELF_ID);
  parm = api_def_ptr(fn, "view", "SceneRenderView", "", "Render view to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_image_format_stereo3d_format(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* api_enum_stereo3d_display_items, without (S3D_DISPLAY_PAGEFLIP) */
  static const EnumPropItem stereo3d_display_items[] = {
      {S3D_DISPLAY_ANAGLYPH,
       "ANAGLYPH",
       0,
       "Anaglyph",
       "Render views for left and right eyes as two differently filtered colors in a single image "
       "(anaglyph glasses are required)"},
      {S3D_DISPLAY_INTERLACE,
       "INTERLACE",
       0,
       "Interlace",
       "Render views for left and right eyes interlaced in a single image (3D-ready monitor is "
       "required)"},
      {S3D_DISPLAY_SIDEBYSIDE,
       "SIDEBYSIDE",
       0,
       "Side-by-Side",
       "Render views for left and right eyes side-by-side"},
      {S3D_DISPLAY_TOPBOTTOM,
       "TOPBOTTOM",
       0,
       "Top-Bottom",
       "Render views for left and right eyes one above another"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Stereo3dFormat", NULL);
  api_def_struct_stype(sapi, "Stereo3dFormat");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Stereo Output", "Settings for stereo output");

  prop = api_def_prop(sapi, "display_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "display_mode");
  ali_def_prop_enum_items(prop, stereo3d_display_items);
  api_def_prop_ui_text(prop, "Stereo Mode", "");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");

  prop = api_def_prop(sapi, "anaglyph_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_stereo3d_anaglyph_type_items);
  api_def_prop_ui_text(prop, "Anaglyph Type", "");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");

  prop = api_def_prop(sapi, "interlace_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_stereo3d_interlace_type_items);
  api_def_prop_ui_text(prop, "Interlace Type", "");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");

  prop = api_def_prop(sapi, "use_interlace_swap", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", S3D_INTERLACE_SWAP);
  api_def_prop_ui_text(prop, "Swap Left/Right", "Swap left and right stereo channels");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");

  prop = api_def_prop(sapi, "use_sidebyside_crosseyed", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", S3D_SIDEBYSIDE_CROSSEYED);
  api_def_prop_ui_text(prop, "Cross-Eyed", "Right eye should see left image and vice versa");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");

  prop = api_def_prop(sapi, "use_squeezed_frame", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", S3D_SQUEEZED_FRAME);
  api_def_prop_ui_text(prop, "Squeezed Frame", "Combine both views in a squeezed image");
  api_def_prop_update(prop, NC_IMAGE | ND_DISPLAY, "api_Stereo3dFormat_update");
}

/* use for render output and image save operator,
 * NOTE: there are some cases where the members act differently when this is
 * used from a scene, video formats can only be selected for render output
 * for example, this is checked by seeing if the ptr->owner_id is a Scene id */
static void api_def_scene_image_format_data(DuneApi *dapi)
{

#  ifdef WITH_OPENJPEG
  static const EnumPropItem jp2_codec_items[] = {
      {R_IMF_JP2_CODEC_JP2, "JP2", 0, "JP2", ""},
      {R_IMF_JP2_CODEC_J2K, "J2K", 0, "J2K", ""},
      {0, NULL, 0, NULL, NULL},
  };
#  endif

  static const EnumPropItem tiff_codec_items[] = {
      {R_IMF_TIFF_CODEC_NONE, "NONE", 0, "None", ""},
      {R_IMF_TIFF_CODEC_DEFLATE, "DEFLATE", 0, "Deflate", ""},
      {R_IMF_TIFF_CODEC_LZW, "LZW", 0, "LZW", ""},
      {R_IMF_TIFF_CODEC_PACKBITS, "PACKBITS", 0, "Pack Bits", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem color_management_items[] = {
      {R_IMF_COLOR_MANAGEMENT_FOLLOW_SCENE, "FOLLOW_SCENE", 0, "Follow Scene", ""},
      {R_IMF_COLOR_MANAGEMENT_OVERRIDE, "OVERRIDE", 0, "Override", ""},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  api_def_image_format_stereo3d_format(dapi);

  sapi = api_def_struct(dapi, "ImageFormatSettings", NULL);
  api_def_struct_stype(sapi, "ImageFormatData");
  api_def_struct_nested(dapi, sapi, "Scene");
  api_def_struct_path_fn(sapi, "api_ImageFormatSettings_path");
  api_def_struct_ui_text(sapi, "Image Format", "Settings for image formats");

  prop = api_def_prop(sapi, "file_format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "imtype");
  api_def_prop_enum_items(prop, api_enum_image_type_items);
  api_def_prop_enum_fns(prop,
                        NULL,
                        "api_ImageFormatSettings_file_format_set",
                        "api_ImageFormatSettings_file_format_itemf");
  api_def_prop_ui_text(prop, "File Format", "File format to save the rendered images as");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "color_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "planes");
  api_def_prop_enum_items(prop, api_enum_image_color_mode_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_ImageFormatSettings_color_mode_itemf");
  api_def_prop_ui_text(
      prop,
      "Color Mode",
      "Choose BW for saving grayscale images, RGB for saving red, green and blue channels, "
      "and RGBA for saving red, green, blue and alpha channels");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "color_depth", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "depth");
  api_def_prop_enum_items(prop, api_enum_image_color_depth_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_ImageFormatSettings_color_depth_itemf");
  api_def_prop_ui_text(prop, "Color Depth", "Bit depth per channel");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* was 'file_quality' */
  prop = api_def_prop(sapi, "quality", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "quality");
  api_def_prop_range(prop, 0, 100); /* 0 is needed for compression. */
  api_def_prop_ui_text(
      prop, "Quality", "Quality for image formats that support lossy compression");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* was shared with file_quality */
  prop = api_def_prop(sapi, "compression", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "compress");
  api_def_prop_range(prop, 0, 100); /* 0 is needed for compression. */
  api_def_prop_ui_text(prop,
                       "Compression",
                       "Amount of time to determine best compression: "
                       "0 = no compression with fast file output, "
                       "100 = maximum lossless compression with slow file output");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* flag */
  prop = api_def_prop(sapi, "use_zbuffer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_IMF_FLAG_ZBUF);
  api_def_prop_ui_text(
      prop, "Z Buffer", "Save the z-depth per pixel (32-bit unsigned integer z-buffer)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_preview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", R_IMF_FLAG_PREVIEW_JPG);
  api_def_prop_ui_text(
      prop, "Preview", "When rendering animations, save JPG preview images in same directory");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* format specific */

#  ifdef WITH_OPENEXR
  /* OpenEXR */
  prop = api_def_prop(sapi, "exr_codec", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "exr_codec");
  api_def_prop_enum_items(prop, api_enum_exr_codec_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_ImageFormatSettings_exr_codec_itemf");
  api_def_prop_ui_text(prop, "Codec", "Codec settings for OpenEXR");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
#  endif

#  ifdef WITH_OPENJPEG
  /* Jpeg 2000 */
  prop = api_def_prop(sapi, "use_jpeg2k_ycc", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "jp2_flag", R_IMF_JP2_FLAG_YCC);
  api_def_prop_ui_text(
      prop, "YCC", "Save luminance-chrominance-chrominance channels instead of RGB colors");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_jpeg2k_cinema_preset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "jp2_flag", R_IMF_JP2_FLAG_CINE_PRESET);
  api_def_prop_ui_text(prop, "Cinema", "Use Openjpeg Cinema Preset");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_jpeg2k_cinema_48", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "jp2_flag", R_IMF_JP2_FLAG_CINE_48);
  api_def_prop_ui_text(prop, "Cinema (48)", "Use Openjpeg Cinema Preset (48fps)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "jpeg2k_codec", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "jp2_codec");
  api_def_prop_enum_items(prop, jp2_codec_items);
  api_def_prop_ui_text(prop, "Codec", "Codec settings for Jpeg2000");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
#  endif

  /* TIFF */
  prop = api_def_prop(sapi, "tiff_codec", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "tiff_codec");
  api_def_prop_enum_items(prop, tiff_codec_items);
  api_def_prop_ui_text(prop, "Compression", "Compression mode for TIFF");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Cineon and DPX */
  prop = api_def_prop(sapi, "use_cineon_log", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "cineon_flag", R_IMF_CINEON_FLAG_LOG);
  api_def_prop_ui_text(prop, "Log", "Convert to logarithmic color space");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "cineon_black", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "cineon_black");
  api_def_prop_range(prop, 0, 1024);
  api_def_prop_ui_text(prop, "Black", "Log conversion reference blackpoint");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "cineon_white", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "cineon_white");
  api_def_prop_range(prop, 0, 1024);
  api_def_prop_ui_text(prop, "White", "Log conversion reference whitepoint");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "cineon_gamma", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "cineon_gamma");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(prop, "Gamma", "Log conversion gamma");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* multiview */
  prop = api_def_prop(sapi, "views_format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "views_format");
  api_def_prop_enum_items(prop, api_enum_views_format_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_ImageFormatSettings_views_format_itemf");
  qpi_def_prop_ui_text(prop, "Views Format", "Format of multiview media");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "stereo_3d_format", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "stereo3d_format");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "Stereo3dFormat");
  api_def_prop_ui_text(prop, "Stereo 3D Format", "Settings for stereo 3D");

  /* color management */
  prop = api_def_prop(sapi, "color_management", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, color_management_items);
  api_def_prop_ui_text(
      prop, "Color Management", "Which color management settings to use for file saving");
  api_def_prop_enum_fns(prop, NULL, "api_ImageFormatSettings_color_management_set", NULL);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "view_settings", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ColorManagedViewSettings");
  api_def_prop_ui_text(
      prop, "View Settings", "Color management settings applied on image before saving");

  prop = api_def_prop(sapi, "display_settings", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ColorManagedDisplaySettings");
  api_def_prop_ui_text(
      prop, "Display Settings", "Settings of device saved image would be displayed on");

  prop = api_def_prop(sapi, "linear_colorspace_settings", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ColorManagedInputColorspaceSettings");
  api_def_prop_ui_text(prop, "Color Space Settings", "Output color space settings");

  prop = api_def_prop(sapi, "has_linear_colorspace", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_ImageFormatSettings_has_linear_colorspace_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Has Linear Color Space", "File format expects linear color space");
}

static void api_def_scene_ffmpeg_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

#  ifdef WITH_FFMPEG
  /* Container types */
  static const EnumPropItem ffmpeg_format_items[] = {
      {FFMPEG_MPEG1, "MPEG1", 0, "MPEG-1", ""},
      {FFMPEG_MPEG2, "MPEG2", 0, "MPEG-2", ""},
      {FFMPEG_MPEG4, "MPEG4", 0, "MPEG-4", ""},
      {FFMPEG_AVI, "AVI", 0, "AVI", ""},
      {FFMPEG_MOV, "QUICKTIME", 0, "QuickTime", ""},
      {FFMPEG_DV, "DV", 0, "DV", ""},
      {FFMPEG_OGG, "OGG", 0, "Ogg", ""},
      {FFMPEG_MKV, "MKV", 0, "Matroska", ""},
      {FFMPEG_FLV, "FLASH", 0, "Flash", ""},
      {FFMPEG_WEBM, "WEBM", 0, "WebM", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem ffmpeg_codec_items[] = {
      {AV_CODEC_ID_NONE, "NONE", 0, "No Video", "Disables video output, for audio-only renders"},
      {AV_CODEC_ID_DNXHD, "DNXHD", 0, "DNxHD", ""},
      {AV_CODEC_ID_DVVIDEO, "DV", 0, "DV", ""},
      {AV_CODEC_ID_FFV1, "FFV1", 0, "FFmpeg video codec #1", ""},
      {AV_CODEC_ID_FLV1, "FLASH", 0, "Flash Video", ""},
      {AV_CODEC_ID_H264, "H264", 0, "H.264", ""},
      {AV_CODEC_ID_HUFFYUV, "HUFFYUV", 0, "HuffYUV", ""},
      {AV_CODEC_ID_MPEG1VIDEO, "MPEG1", 0, "MPEG-1", ""},
      {AV_CODEC_ID_MPEG2VIDEO, "MPEG2", 0, "MPEG-2", ""},
      {AV_CODEC_ID_MPEG4, "MPEG4", 0, "MPEG-4 (divx)", ""},
      {AV_CODEC_ID_PNG, "PNG", 0, "PNG", ""},
      {AV_CODEC_ID_QTRLE, "QTRLE", 0, "QT rle / QT Animation", ""},
      {AV_CODEC_ID_THEORA, "THEORA", 0, "Theora", ""},
      {AV_CODEC_ID_VP9, "WEBM", 0, "WebM / VP9", ""},
      {AV_CODEC_ID_AV1, "AV1", 0, "AV1", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* Recommendations come from the FFmpeg wiki, https://trac.ffmpeg.org/wiki/Encode/VP9.
   * The label for BEST has been changed to "Slowest" so that it fits the "Encoding Speed"
   * property label in the UI. */
  static const EnumPropItem ffmpeg_preset_items[] = {
      {FFM_PRESET_BEST,
       "BEST",
       0,
       "Slowest",
       "Recommended if you have lots of time and want the best compression efficiency"},
      {FFM_PRESET_GOOD, "GOOD", 0, "Good", "The default and recommended for most applications"},
      {FFM_PRESET_REALTIME, "REALTIME", 0, "Realtime", "Recommended for fast encoding"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem ffmpeg_crf_items[] = {
      {FFM_CRF_NONE,
       "NONE",
       0,
       "Constant Bitrate",
       "Configure constant bit rate, rather than constant output quality"},
      {FFM_CRF_LOSSLESS, "LOSSLESS", 0, "Lossless", ""},
      {FFM_CRF_PERC_LOSSLESS, "PERC_LOSSLESS", 0, "Perceptually Lossless", ""},
      {FFM_CRF_HIGH, "HIGH", 0, "High Quality", ""},
      {FFM_CRF_MEDIUM, "MEDIUM", 0, "Medium Quality", ""},
      {FFM_CRF_LOW, "LOW", 0, "Low Quality", ""},
      {FFM_CRF_VERYLOW, "VERYLOW", 0, "Very Low Quality", ""},
      {FFM_CRF_LOWEST, "LOWEST", 0, "Lowest Quality", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem ffmpeg_audio_codec_items[] = {
      {AV_CODEC_ID_NONE, "NONE", 0, "No Audio", "Disables audio output, for video-only renders"},
      {AV_CODEC_ID_AAC, "AAC", 0, "AAC", ""},
      {AV_CODEC_ID_AC3, "AC3", 0, "AC3", ""},
      {AV_CODEC_ID_FLAC, "FLAC", 0, "FLAC", ""},
      {AV_CODEC_ID_MP2, "MP2", 0, "MP2", ""},
      {AV_CODEC_ID_MP3, "MP3", 0, "MP3", ""},
      {AV_CODEC_ID_OPUS, "OPUS", 0, "Opus", ""},
      {AV_CODEC_ID_PCM_S16LE, "PCM", 0, "PCM", ""},
      {AV_CODEC_ID_VORBIS, "VORBIS", 0, "Vorbis", ""},
      {0, NULL, 0, NULL, NULL},
  };
#  endif

  static const EnumPropItem audio_channel_items[] = {
      {FFM_CHANNELS_MONO, "MONO", 0, "Mono", "Set audio channels to mono"},
      {FFM_CHANNELS_STEREO, "STEREO", 0, "Stereo", "Set audio channels to stereo"},
      {FFM_CHANNELS_SURROUND4, "SURROUND4", 0, "4 Channels", "Set audio channels to 4 channels"},
      {FFM_CHANNELS_SURROUND51,
       "SURROUND51",
       0,
       "5.1 Surround",
       "Set audio channels to 5.1 surround sound"},
      {FFM_CHANNELS_SURROUND71,
       "SURROUND71",
       0,
       "7.1 Surround",
       "Set audio channels to 7.1 surround sound"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "FFmpegSettings", NULL);
  api_def_struct_stype(sapi, "FFMpegCodecData");
  api_def_struct_path_fn(sapi, "api_FFmpegSettings_path");
  api_def_struct_ui_text(sapi, "FFmpeg Settings", "FFmpeg related settings for the scene");

#  ifdef WITH_FFMPEG
  prop = api_def_prop(sapi, "format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "type");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, ffmpeg_format_items);
  api_def_prop_enum_default(prop, FFMPEG_MKV);
  api_def_prop_ui_text(prop, "Container", "Output file container");

  prop = api_def_prop(sapi, "codec", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "codec");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, ffmpeg_codec_items);
  api_def_prop_enum_default(prop, AV_CODEC_ID_H264);
  api_def_prop_ui_text(prop, "Video Codec", "FFmpeg codec to use for video output");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_FFmpegSettings_codec_update");

  prop = api_def_prop(sapi, "video_bitrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "video_bitrate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Bitrate", "Video bitrate (kbit/s)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "minrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "rc_min_rate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Min Rate", "Rate control: min rate (kbit/s)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "maxrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "rc_max_rate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Max Rate", "Rate control: max rate (kbit/s)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "muxrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "mux_rate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 100000000);
  api_def_prop_ui_text(prop, "Mux Rate", "Mux rate (bits/second)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gopsize", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "gop_size");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 500);
  api_def_prop_int_default(prop, 25);
  api_def_prop_ui_text(prop,
                       "Keyframe Interval",
                       "Distance between key frames, also known as GOP size; "
                       "influences file size and seekability");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "max_b_frames", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "max_b_frames");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 16);
  api_def_prop_ui_text(
      prop,
      "Max B-Frames",
      "Maximum number of B-frames between non-B-frames; influences file size and seekability");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_max_b_frames", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FFMPEG_USE_MAX_B_FRAMES);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Use Max B-Frames", "Set a maximum number of B-frames");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "buffersize", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "rc_buffer_size");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 2000);
  api_def_prop_ui_text(prop, "Buffersize", "Rate control: buffer size (kb)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "packetsize", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "mux_packet_size");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 16384);
  api_def_prop_ui_text(prop, "Mux Packet Size", "Mux packet size (byte)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "constant_rate_factor", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "constant_rate_factor");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, ffmpeg_crf_items);
  api_def_prop_enum_default(prop, FFM_CRF_MEDIUM);
  api_def_prop_ui_text(
      prop,
      "Output Quality",
      "Constant Rate Factor (CRF); tradeoff between video quality and file size");
  api_def_property_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ffmpeg_preset", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "ffmpeg_preset");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, ffmpeg_preset_items);
  api_def_prop_enum_default(prop, FFM_PRESET_GOOD);
  api_def_prop_ui_text(
      prop, "Encoding Speed", "Tradeoff between encoding speed and compression ratio");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_autosplit", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FFMPEG_AUTOSPLIT_OUTPUT);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Autosplit Output", "Autosplit output at 2GB boundary");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_lossless_output", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", FFMPEG_LOSSLESS_OUTPUT);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_fns(prop, NULL, "rna_FFmpegSettings_lossless_output_set");
  api_def_prop_ui_text(prop, "Lossless Output", "Use lossless output for video streams");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* FFMPEG Audio. */
  prop = api_def_prop(sapi, "audio_codec", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_sdna(prop, NULL, "audio_codec");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, ffmpeg_audio_codec_items);
  api_def_prop_ui_text(prop, "Audio Codec", "FFmpeg audio codec to use");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "audio_bitrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "audio_bitrate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 32, 384);
  api_def_prop_ui_text(prop, "Bitrate", "Audio bitrate (kb/s)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "audio_volume", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "audio_volume");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Volume", "Audio volume");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SOUND);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
#  endif

  /* the following two "ffmpeg" settings are general audio settings */
  prop = api_def_prop(sapi, "audio_mixrate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "audio_mixrate");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 8000, 192000);
  api_def_prop_ui_text(prop, "Samplerate", "Audio samplerate(samples/s)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "audio_channels", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "audio_channels");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, audio_channel_items);
  api_def_prop_ui_text(prop, "Audio Channels", "Audio channel count");
}

static void api_def_scene_render_data(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* Bake */
  static const EnumPropItem bake_mode_items[] = {
      //{RE_BAKE_AO, "AO", 0, "Ambient Occlusion", "Bake ambient occlusion"},
      {RE_BAKE_NORMALS, "NORMALS", 0, "Normals", "Bake normals"},
      {RE_BAKE_DISPLACEMENT, "DISPLACEMENT", 0, "Displacement", "Bake displacement"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem bake_margin_type_items[] = {
      {R_BAKE_ADJACENT_FACES,
       "ADJACENT_FACES",
       0,
       "Adjacent Faces",
       "Use pixels from adjacent faces across UV seams"},
      {R_BAKE_EXTEND, "EXTEND", 0, "Extend", "Extend border pixels outwards"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pixel_size_items[] = {
      {0, "AUTO", 0, "Automatic", "Automatic pixel size, depends on the user interface scale"},
      {1, "1", 0, "1x", "Render at full resolution"},
      {2, "2", 0, "2x", "Render at 50% resolution"},
      {4, "4", 0, "4x", "Render at 25% resolution"},
      {8, "8", 0, "8x", "Render at 12.5% resolution"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem threads_mode_items[] = {
      {0,
       "AUTO",
       0,
       "Auto-Detect",
       "Automatically determine the number of threads, based on CPUs"},
      {R_FIXED_THREADS, "FIXED", 0, "Fixed", "Manually determine the number of threads"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem engine_items[] = {
      {0, "BLENDER_EEVEE", 0, "Eevee", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem freestyle_thickness_items[] = {
      {R_LINE_THICKNESS_ABSOLUTE,
       "ABSOLUTE",
       0,
       "Absolute",
       "Specify unit line thickness in pixels"},
      {R_LINE_THICKNESS_RELATIVE,
       "RELATIVE",
       0,
       "Relative",
       "Unit line thickness is scaled by the proportion of the present vertical image "
       "resolution to 480 pixels"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem views_format_items[] = {
      {SCE_VIEWS_FORMAT_STEREO_3D,
       "STEREO_3D",
       0,
       "Stereo 3D",
       "Single stereo camera system, adjust the stereo settings in the camera panel"},
      {SCE_VIEWS_FORMAT_MULTIVIEW,
       "MULTIVIEW",
       0,
       "Multi-View",
       "Multi camera system, adjust the cameras individually"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem hair_shape_type_items[] = {
      {SCE_HAIR_SHAPE_STRAND, "STRAND", 0, "Strand", ""},
      {SCE_HAIR_SHAPE_STRIP, "STRIP", 0, "Strip", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem meta_input_items[] = {
      {0, "SCENE", 0, "Scene", "Use metadata from the current scene"},
      {R_STAMP_STRIPMETA,
       "STRIPS",
       0,
       "Sequencer Strips",
       "Use metadata from the strips in the sequencer"},
      {0, NULL, 0, NULL, NULL},
  };

  api_def_scene_ffmpeg_settings(dapi);

  sapi = api_def_struct(dapi, "RenderSettings", NULL);
  api_def_struct_stype(sapi, "RenderData");
  api_def_struct_nested(dapi, sapi, "Scene");
  api_def_struct_path_fn(sapi, "api_RenderSettings_path");
  api_def_struct_ui_text(sapi, "Render Data", "Rendering settings for a Scene data-block");

  /* Render Data */
  prop = api_def_prop(sapi, "image_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "im_format");
  api_def_prop_struct_type(prop, "ImageFormatSettings");
  api_def_prop_ui_text(prop, "Image Format", "");

  prop = api_def_prop(sapi, "resolution_x", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "xsch");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 4, 65536);
  api_def_prop_ui_text(
      prop, "Resolution X", "Number of horizontal pixels in the rendered image");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_SceneCamera_update");

  prop = api_def_prop(sapi, "resolution_y", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "ysch");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 4, 65536);
  api_def_prop_ui_text(
      prop, "Resolution Y", "Number of vertical pixels in the rendered image");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_SceneCamera_update");

  prop = api_def_prop(sapi, "resolution_percentage", PROP_INT, PROP_PERCENTAGE);
  api_def_prop_int_stype(prop, NULL, "size");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_range(prop, 1, 100, 10, 1);
  api_def_prop_ui_text(prop, "Resolution %", "Percentage scale for render resolution");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_SceneSequencer_update");

  prop = api_def_prop(sapi, "preview_pixel_size", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "preview_pixel_size");
  api_def_prop_enum_items(prop, pixel_size_items);
  api_def_prop_ui_text(prop, "Pixel Size", "Pixel size for viewport rendering");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "pixel_aspect_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "xasp");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1.0f, 200.0f);
  api_def_prop_ui_text(prop,
                       "Pixel Aspect X",
                       "Horizontal aspect ratio - for anamorphic or non-square pixel output");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_SceneCamera_update");

  prop = api_def_prop(sapi, "pixel_aspect_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "yasp");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1.0f, 200.0f);
  api_def_prop_ui_text(
      prop, "Pixel Aspect Y", "Vertical aspect ratio - for anamorphic or non-square pixel output");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_SceneCamera_update");

  prop = api_def_prop(sapi, "ffmpeg", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "FFmpegSettings");
  api_def_prop_ptr_stype(prop, NULL, "ffcodecdata");
  api_def_prop_flag(prop, PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "FFmpeg Settings", "FFmpeg related settings for the scene");

  prop = api_def_prop(sapi, "fps", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "frs_sec");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_range(prop, 1, 240, 1, -1);
  api_def_prop_ui_text(prop, "FPS", "Framerate, expressed in frames per second");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_fps_update");

  prop = api_def_prop(sapi, "fps_base", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "frs_sec_base");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1e-5f, 1e6f);
  /* Important to show at least 3 decimal points because multiple presets set this to 1.001. */
  api_def_prop_ui_range(prop, 0.1f, 120.0f, 2, 3);
  api_def_prop_ui_text(prop, "FPS Base", "Framerate base");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_fps_update");

  /* frame mapping */
  prop = api_def_prop(sapi, "frame_map_old", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "framapto");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prope_range(prop, 1, 900);
  api_def_prop_ui_text(prop, "Frame Map Old", "Old mapping value in frames");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, "rna_Scene_framelen_update");

  prop = api_def_prop(sapi, "frame_map_new", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "images");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 1, 900);
  api_def_prop_ui_text(prop, "Frame Map New", "How many frames the Map Old will last");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, "rna_Scene_framelen_update");

  prop = api_def_prop(sapi, "dither_intensity", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "dither_intensity");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 2.0, 0.1, 2);
  api_def_prop_ui_text(
      prop,
      "Dither Intensity",
      "Amount of dithering noise added to the rendered image to break up banding");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "filter_size", PROP_FLOAT, PROP_PIXEL);
  api_def_prop_float_stype(prop, NULL, "gauss");
  api_def_prop_range(prop, 0.0f, 500.0f);
  api_def_prop_ui_range(prop, 0.01f, 10.0f, 1, 2);
  api_def_prop_ui_text(
      prop, "Filter Size", "Width over which the reconstruction filter combines samples");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "film_transparent", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "alphamode", R_ALPHAPREMUL);
  api_def_prop_ui_text(
      prop,
      "Transparent",
      "World background is transparent, for compositing the render over another background");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_render_update");

  prop = api_def_prop(sapi, "use_freestyle", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_EDGE_FRS);
  api_def_prop_ui_text(prop, "Edge", "Draw stylized strokes using Freestyle");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_use_freestyle_update");

  /* threads */
  prop = api_def_prop(sapi, "threads", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "threads");
  api_def_prop_range(prop, 1, DUNE_MAX_THREADS);
  api_def_prop_int_fns(prop, "api_RenderSettings_threads_get", NULL, NULL);
  api_def_prop_ui_text(prop,
                       "Threads",
                       "Maximum number of CPU cores to use simultaneously while rendering "
                       "(for multi-core/CPU systems)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "threads_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, threads_mode_items);
  api_def_prop_enum_fns(prop, "api_RenderSettings_threads_mode_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Threads Mode", "Determine the amount of render threads used");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* motion blur */
  prop = api_def_prop(sapi, "use_motion_blur", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_MBLUR);
  api_def_prop_ui_text(prop, "Motion Blur", "Use multi-sampled 3D scene motion blur");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_render_update");

  prop = api_def_prop(sapi, "motion_blur_shutter", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "blurfac");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.01f, 1.0f, 1, 2);
  api_def_prop_ui_text(prop, "Shutter", "Time taken in frames between shutter open and close");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "rna_Scene_render_update");

  prop = api_def_prop(sapi, "motion_blur_shutter_curve", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "mblur_shutter_curve");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(
      prop, "Shutter Curve", "Curve defining the shutter's openness over time");

  /* Hairs */
  prop = api_def_prop(sapi, "hair_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, hair_shape_type_items);
  api_def_prop_ui_text(prop, "Curves Shape Type", "Curves shape type");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_CURVES);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_render_update");

  prop = api_def_prop(sapi, "hair_subdiv", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 0, 3);
  api_def_prop_ui_text(
      prop, "Additional Subdivision", "Additional subdivision along the curves");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_render_update");

  /* Performance */
  prop = api_def_prop(sapi, "use_high_quality_normals", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "perf_flag", SCE_PERF_HQ_NORMALS);
  api_def_prop_ui_text(prop,
                       "High Quality Normals",
                       "Use high quality tangent space at the cost of lower performance");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_mesh_quality_update");

  /* border */
  prop = api_def_prop(sapi, "use_border", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_BORDER);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Render Region", "Render a user-defined render region, within the frame size");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "border_min_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "border.xmin");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Region Minimum X", "Minimum X value for the render region");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "border_min_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "border.ymin");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Region Minimum Y", "Minimum Y value for the render region");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "border_max_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "border.xmax");
  api_def_prop_range(prop, 0.0f, 1.0f);
  qpi_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Region Maximum X", "Maximum X value for the render region");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "border_max_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "border.ymax");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Region Maximum Y", "Maximum Y value for the render region");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_crop_to_border", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_CROP);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Crop to Render Region", "Crop the rendered frame to the defined render region size");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_placeholder", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_TOUCH);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop,
      "Placeholders",
      "Create empty placeholder files while rendering frames (similar to Unix 'touch')");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_overwrite", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "mode", R_NO_OVERWRITE);
  api_def_prop_ui_text(prop, "Overwrite", "Overwrite existing files while rendering");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_compositing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_DOCOMP);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                       "Compositing",
                       "Process the render result through the compositing pipeline, "
                       "if compositing nodes are enabled");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_sequencer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_DOSEQ);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                       "Seq",
                       "Process the render (and composited) result through the video sequence "
                       "editor pipeline, if seq strips exist");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_file_extension", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_EXTENSION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop,
      "File Extensions",
      "Add the file format extensions to the rendered file name (eg: filename + .jpg)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

#  if 0 /* moved */
  prop = api_def_prop(sapi, "file_format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "imtype");
  api_def_prop_enum_items(prop, api_enum_image_type_items);
  api_def_prop_enum_fns(prop, NULL, "api_RenderSettings_file_format_set", NULL);
  api_def_prop_ui_text(prop, "File Format", "File format to save the rendered images as");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
#  endif

  prop = api_def_prop(sapi, "file_extension", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_SceneRender_file_ext_get", "api_SceneRender_file_ext_length", NULL);
  api_def_prop_ui_text(prop, "Extension", "The file extension used for saving renders");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "is_movie_format", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_RenderSettings_is_movie_format_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Movie Format", "When true the format is a movie");
  prop = api_def_prop(sapi, "use_lock_interface", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_lock_interface", 1);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, true);
  api_def_prop_ui_text(
      prop,
      "Lock Interface",
      "Lock interface during rendering in favor of giving more memory to the renderer");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "pic");
  api_def_prop_ui_text(prop,
                       "Output Path",
                       "Directory/name to save animations, # characters defines the position "
                       "and length of frame numbers");
  api_def_prop_flag(prop, PROP_PATH_OUTPUT);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Render result EXR cache. */
  prop = api_def_prop(sapi, "use_render_cache", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_EXR_CACHE_FILE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                       "Cache Result",
                       "Save render cache to EXR files (useful for heavy compositing, "
                       "Note: affects indirectly rendered scenes)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Bake */
  prop = api_def_prop(sapi, "bake_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "bake_mode");
  api_def_prop_enum_items(prop, bake_mode_items);
  api_def_prop_ui_text(prop, "Bake Type", "Choose shading information to bake into the image");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bake_selected_to_active", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "bake_flag", R_BAKE_TO_ACTIVE);
  api_def_prop_ui_text(prop,
                       "Selected to Active",
                       "Bake shading on the surface of selected objects to the active object");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bake_clear", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "bake_flag", R_BAKE_CLEAR);
  api_def_prop_ui_text(prop, "Clear", "Clear Images before baking");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bake_margin", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "bake_margin");
  api_def_prop_range(prop, 0, 64);
  api_def_prop_ui_text(prop, "Margin", "Extends the baked result as a post process filter");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bake_margin_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "bake_margin_type");
  api_def_prop_enum_items(prop, bake_margin_type_items);
  api_def_prop_ui_text(prop, "Margin Type", "Algorithm to generate the margin");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bake_bias", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bake_biasdist");
  api_def_prop_range(prop, 0.0, 1000.0);
  api_def_prop_ui_text(
      prop, "Bias", "Bias towards faces further away from the object (in Blender units)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bake_multires", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "bake_flag", R_BAKE_MULTIRES);
  api_def_prop_ui_text(prop, "Bake from Multires", "Bake directly from multires object");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bake_lores_mesh", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "bake_flag", R_BAKE_LORES_MESH);
  api_def_prop_ui_text(
      prop, "Low Resolution Mesh", "Calculate heights against unsubdivided low resolution mesh");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sali, "bake_samples", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "bake_samples");
  api_def_prop_range(prop, 64, 1024);
  api_def_prop_ui_text(
      prop, "Samples", "Number of samples used for ambient occlusion baking from multires");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bake_user_scale", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "bake_flag", R_BAKE_USERSCALE);
  api_def_prop_ui_text(prop, "User Scale", "Use a user scale for the derivative map");

  prop = api_def_prop(sapi, "bake_user_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bake_user_scale");
  api_def_prop_range(prop, 0.0, 1000.0);
  api_def_prop_ui_text(prop,
                       "Scale",
                       "Instead of automatically normalizing to the range 0 to 1, "
                       "apply a user scale to the derivative map");

  /* stamp */
  prop = api_def_prop(sapi, "use_stamp_time", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_TIME);
  api_def_prop_ui_text(
      prop, "Stamp Time", "Include the rendered frame timecode as HH:MM:SS.FF in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_date", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_DATE);
  api_def_prop_ui_text(prop, "Stamp Date", "Include the current date in image/video metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_frame", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_FRAME);
  api_def_prop_ui_text(prop, "Stamp Frame", "Include the frame number in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_frame_range", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_FRAME_RANGE);
  api_def_prop_ui_text(
      prop, "Stamp Frame", "Include the rendered frame range in image/video metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_camera", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_CAMERA);
  api_def_prop_ui_text(
      prop, "Stamp Camera", "Include the name of the active camera in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_lens", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_CAMERALENS);
  api_def_prop_ui_text(
      prop, "Stamp Lens", "Include the active camera's lens in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_scene", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_SCENE);
  api_def_prop_ui_text(
      prop, "Stamp Scene", "Include the name of the active scene in image/video metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_note", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_NOTE);
  api_def_prop_ui_text(prop, "Stamp Note", "Include a custom note in image/video metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_marker", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_MARKER);
  api_def_prop_ui_text(
      prop, "Stamp Marker", "Include the name of the last marker in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_filename", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_FILENAME);
  api_def_prop_ui_text(
      prop, "Stamp Filename", "Include the .dune filename in image/video metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(dapi, "use_stamp_sequencer_strip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_SEQSTRIP);
  api_def_prop_ui_text(prop,
                       "Stamp Sequence Strip",
                       "Include the name of the foreground sequence strip in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_render_time", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_RENDERTIME);
  api_def_prop_ui_text(prop, "Stamp Render Time", "Include the render time in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "stamp_note_text", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "stamp_udata");
  api_def_prop_ui_text(prop, "Stamp Note Text", "Custom text to appear in the stamp note");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_DRAW);
  api_def_prop_ui_text(
      prop, "Stamp Output", "Render the stamp info text in the rendered image");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_labels", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "stamp", R_STAMP_HIDE_LABELS);
  api_def_prop_ui_text(
      prop, "Stamp Labels", "Display stamp labels (\"Camera\" in front of camera name, etc.)");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "metadata_input", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "stamp");
  api_def_prop_enum_items(prop, meta_input_items);
  api_def_prop_ui_text(prop, "Metadata Input", "Where to take the metadata from");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_memory", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_MEMORY);
  api_def_prop_ui_text(
      prop, "Stamp Peak Memory", "Include the peak memory usage in image metadata");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_stamp_hostname", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "stamp", R_STAMP_HOSTNAME);
  api_def_prop_ui_text(
      prop, "Stamp Hostname", "Include the hostname of the machine that rendered the frame");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "stamp_font_size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "stamp_font_id");
  api_def_prop_range(prop, 8, 64);
  api_def_prop_ui_text(prop, "Font Size", "Size of the font used when rendering stamp text");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "stamp_foreground", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "fg_stamp");
  api_def_prop_array(prop, 4);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Text Color", "Color to use for stamp text");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "stamp_background", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "bg_stamp");
  api_def_prop_array(prop, 4);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Background", "Color to use behind stamp text");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* seq draw options */
  prop = api_def_prop(sapi, "seq_gl_preview", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "seq_prev_type");
  api_def_prop_enum_items(prop, rna_enum_shading_type_items);
  api_def_prop_ui_text(
      prop, "Seq Preview Shading", "Display method used in the seq view");
  api_def_prop_update(prop, NC_SCENE | ND_SEQ, "api_SceneSeq_update");

  prop = api_def_prop(sapi, "use_seq_override_scene_strip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "seq_flag", R_SEQ_OVERRIDE_SCENE_SETTINGS);
  api_def_prop_ui_text(prop,
                       "Override Scene Settings",
                       "Use workbench render settings from the seq scene, instead of "
                       "each individual scene used in the strip");
  api_def_prop_update(prop, NC_SCENE | ND_SEQ, "api_SceneSeq_update");

  prop = api_def_prop(sapi, "use_single_layer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_SINGLE_LAYER);
  api_def_prop_ui_text(prop,
                       "Render Single Layer",
                       "Only render the active layer. Only affects rendering from the "
                       "interface, ignored for rendering from command line");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* views (stereoscopy et al) */
  prop = api_def_prop(sapi, "views", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "SceneRenderView");
  api_def_prop_ui_text(prop, "Render Views", "");
  api_def_render_views(dapi, prop);

  prop = api_def_prop(sapi, "stereo_views", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "views", NULL);
  api_def_prop_collection_fns(prop,
                              "api_RenderSettings_stereoViews_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  api_def_prop_struct_type(prop, "SceneRenderView");
  api_def_prop_ui_text(prop, "Render Views", "");

  prop = api_def_prop(sapi, "use_multiview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "scemode", R_MULTIVIEW);
  api_def_prop_ui_text(prop, "Multiple Views", "Use multiple views in the scene");
  apu_def_prop_update(prop, NC_WINDOW, NULL);

  prop = api_def_prop(sapi, "views_format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, views_format_items);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Setup Stereo Mode", "");
  api_def_prop_enum_fns(prop, NULL, "api_RenderSettings_views_format_set", NULL);
  api_def_prop_update(prop, NC_WINDOW, NULL);

  /* engine */
  prop = api_def_prop(sapi, "engine", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, engine_items);
  api_def_prop_enum_fns(prop,
                        "api_RenderSettings_engine_get",
                        "api_RenderSettings_engine_set",
                        "api_RenderSettings_engine_itemf");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Engine", "Engine to use for rendering");
  api_def_prop_update(prop, NC_WINDOW, "api_RenderSettings_engine_update");

  prop = api_def_prop(sapi, "has_multiple_engines", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_RenderSettings_multiple_engines_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Multiple Engines", "More than one rendering engine is available");

  prop = api_def_prop(sapi, "use_spherical_stereo", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_RenderSettings_use_spherical_stereo_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Use Spherical Stereo", "Active render engine supports spherical stereo rendering");

  /* simplify */
  prop = api_def_proptype(sapi, "use_simplify", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_SIMPLIFY);
  api_def_prop_ui_text(
      prop, "Use Simplify", "Enable simplification of scene for quicker preview renders");
  api_def_prop_update(prop, 0, "api_Scene_use_simplify_update");

  prop = api_def_prop(sapi, "simplify_subdivision", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "simplify_subsurf");
  api_def_prop_ui_range(prop, 0, 6, 1, -1);
  api_def_prop_ui_text(prop, "Simplify Subdivision", "Global maximum subdivision level");
  api_def_prop_update(prop, 0, "api_Scene_simplify_update");

  prop = api_def_prop(sapi, "simplify_child_particles", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "simplify_particles");
  api_def_prop_ui_text(prop, "Simplify Child Particles", "Global child particles percentage");
  api_def_prop_update(prop, 0, "api_Scene_simplify_update");

  prop = api_def_prop(sapi, "simplify_subdivision_render", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "simplify_subsurf_render");
  api_def_prop_ui_range(prop, 0, 6, 1, -1);
  api_def_prop_ui_text(
      prop, "Simplify Subdivision", "Global maximum subdivision level during rendering");
  api_def_prop_update(prop, 0, "api_Scene_simplify_update");

  prop = api_def_prop(sapi, "simplify_child_particles_render", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "simplify_particles_render");
  api_def_prop_ui_text(
      prop, "Simplify Child Particles", "Global child particles percentage during rendering");
  api_def_prop_update(prop, 0, "api_Scene_simplify_update");

  prop = api_def_prop(sapi, "simplify_volumes", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0, 1.0f);
  api_def_prop_ui_text(
      prop, "Simplify Volumes", "Resolution percentage of volume objects in viewport");
  api_def_prop_update(prop, 0, "api_Scene_simplify_update");

  /* EEVEE - Simplify Options */
  prop = api_def_prop(sapi, "simplify_shadows_render", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_default(prop, 1.0);
  api_def_prop_range(prop, 0.0, 1.0f);
  api_def_prop_ui_text(
      prop, "Simplify Shadows", "Resolution percentage of shadows in viewport");
  api_def_prop_update(prop, 0, "api_scene_simplify_update");

  prop = api_def_prop(sapi, "simplify_shadows", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_default(prop, 1.0);
  api_def_prop_range(prop, 0.0, 1.0f);
  api_def_prop_ui_text(
      prop, "Simplify Shadows", "Resolution percentage of shadows in viewport");
  api_def_prop_update(prop, 0, "api_scene_simplify_update");

  /* Dune Pen - Simplify Options */
  prop = api_def_prop(sapi, "simplify_pen", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_ENABLE);
  api_def_prop_ui_text(prop, "Simplify", "Simplify Pen drawing");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_onplay", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_ON_PLAY);
  api_def_prop_ui_text(
      prop, "Playback Only", "Simplify Pen only during animation playback");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_antialiasing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_AA);
  api_def_prop_ui_text(prop, "Antialiasing", "Use Antialiasing to smooth stroke edges");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_view_fill", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_FILL);
  api_def_prop_ui_text(prop, "Fill", "Display fill strokes in the viewport");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_mod", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(
      prop, NULL, "simplify_pen", SIMPLIFY_PEN_MOD);
  api_def_prop_ui_text(prop, "Mods", "Display mods");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_shader_fx", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_FX);
  api_def_prop_ui_text(prop, "Shader Effects", "Display Shader Effects");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  prop = api_def_prop(sapi, "simplify_pen_tint", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "simplify_pen", SIMPLIFY_PEN_TINT);
  api_def_prop_ui_text(prop, "Layers Tinting", "Display layer tint");
  api_def_prop_update(prop, NC_PEN | ND_DATA, "api_pen_update");

  /* persistent data */
  prop = api_def_prop(sapi, "use_persistent_data", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", R_PERSISTENT_DATA);
  api_def_prop_ui_text(prop,
                       "Persistent Data",
                       "Keep render data around for faster re-renders and animation renders, "
                       "at the cost of increased memory usage");
  api_def_prop_update(prop, 0, "api_scene_use_persistent_data_update");

  /* Freestyle line thickness options */
  prop = api_def_prop(sapi, "line_thickness_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "line_thickness_mode");
  api_def_prop_enum_items(prop, freestyle_thickness_items);
  api_def_prop_ui_text(
      prop, "Line Thickness Mode", "Line thickness mode for Freestyle line drawing");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  prop = api_def_prop(sapi, "line_thickness", PROP_FLOAT, PROP_PIXEL);
  api_def_prop_float_stype(prop, NULL, "unit_line_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Line Thickness", "Line thickness in pixels");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_freestyle_update");

  /* Bake Settings */
  prop = api_def_prop(sapi, "bake", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "bake");
  api_def_prop_struct_type(prop, "BakeSettings");
  api_def_prop_ui_text(prop, "Bake Data", "");

  /* Nestled Data. */
  /* *** Non-Animated *** */
  api_define_animate_stype(false);
  api_def_bake_data(dapi);
  api_define_animate_stype(true);

  /* *** Animated *** */
  /* Scene API */
  api_api_scene_render(sapi);
}

/* scene.objects */
static void api_def_scene_objects(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  api_def_prop_sapi(cprop, "SceneObjects");
  sapi = api_def_struct(dapi, "SceneObjects", NULL);
  api_def_struct_stype(sapi, "Scene");
  api_def_struct_ui_text(sapi, "Scene Objects", "All of the scene objects");
}

/* scene.timeline_markers */
static void api_def_timeline_markers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "TimelineMarkers");
  sapi = api_def_struct(dapi, "TimelineMarkers", NULL);
  api_def_struct_stype(sapi, "Scene");
  api_def_struct_ui_text(sapi, "Timeline Markers", "Collection of timeline markers");

  fn = api_def_fn(sapi, "new", "api_TimeLine_add");
  api_def_fn_ui_description(fn, "Add a keyframe to the curve");
  parm = api_def_string(fn, "name", "Marker", 0, "", "New name for the marker (not unique)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "frame",
                     1,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The frame for the new marker",
                     -MAXFRAME,
                     MAXFRAME);
  parm = api_def_ptr(fn, "marker", "TimelineMarker", "", "Newly created timeline marker");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_TimeLine_remove");
  api_def_fn_ui_description(fn, "Remove a timeline marker");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "marker", "TimelineMarker", "", "Timeline marker to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  apo_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "clear", "api_TimeLine_clear");
  api_def_fn_ui_description(fb, "Remove all timeline markers");
}

/* scene.keying_sets */
static void api_def_scene_keying_sets(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "KeyingSets");
  sapi = api_def_struct(dapi, "KeyingSets", NULL);
  api_def_struct_stype(sapi, "Scene");
  api_def_struct_ui_text(sapi, "Keying Sets", "Scene keying sets");

  /* Add Keying Set */
  fn = api_def_fn(sapi, "new", "api_Scene_keying_set_new");
  api_def_fn_ui_description(fn, "Add a new Keying Set to Scene");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  /* name */
  api_def_string(fn
      , "idname", "KeyingSet", 64, "IDName", "Internal identifier of Keying Set");
  api_def_string(fn, "name", "KeyingSet", 64, "Name", "User visible name of Keying Set");
  /* returns the new KeyingSet */
  parm = api_def_ptr(fn, "keyingset", "KeyingSet", "", "Newly created Keying Set");
  api_def_fn_return(fn, parm);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyingSet");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(
      prop, "api_Scene_active_keying_set_get", "api_Scene_active_keying_set_set", NULL, NULL);
  apo_def_prop_ui_text(
      prop, "Active Keying Set", "Active Keying Set used to insert/delete keyframes");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_NONE);
  apo_def_prop_int_stype(prop, NULL, "active_keyingset");
  api_def_prop_int_fns(prop,
                       "api_Scene_active_keying_set_index_get",
                       "api_Scene_active_keying_set_index_set",
                       NULL);
  api_def_prop_ui_text(
      prop,
      "Active Keying Set Index",
      "Current Keying Set index (negative for 'builtin' and positive for 'absolute')");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);
}

static void api_def_scene_keying_sets_all(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_prop_sapi(cprop, "KeyingSetsAll");
  sapi = api_def_struct(dapi, "KeyingSetsAll", NULL);
  api_def_struct_stype(sapi, "Scene");
  api_def_struct_ui_text(sapo, "Keying Sets All", "All available keying sets");

  /* NOTE: no add/remove available here, without screwing up this amalgamated list... */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyingSet");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(
      prop, "api_Scene_active_keying_set_get", "api_Scene_active_keying_set_set", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Active Keying Set", "Active Keying Set used to insert/delete keyframes");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "active_keyingset");
  api_def_prop_int_fns(prop,
                       "api_Scene_active_keying_set_index_get",
                       "api_Scene_active_keying_set_index_set",
                       NULL);
  api_def_prop_ui_text(
      prop,
      "Active Keying Set Index",
      "Current Keying Set index (negative for 'builtin' and positive for 'absolute')");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);
}

/* Runtime property, used to remember uv indices, used only in UV stitch for now. */
static void api_def_selected_uv_element(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "SelectedUvElement", "PropGroup");
  api_def_struct_ui_text(sapi, "Selected UV Element", "");

  /* store the index to the UV element selected */
  prop = api_def_prop(sapi, "element_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_ui_text(prop, "Element Index", "");

  prop = api_def_prop(sapi, "face_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_ui_text(prop, "Face Index", "");
}

static void api_def_display_safe_areas(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "DisplaySafeAreas", NULL);
  api_def_struct_ui_text(sapi, "Safe Areas", "Safe areas used in 3D view and the sequencer");
  api_def_struct_stype(sapi, "DisplaySafeAreas");

  /* SAFE AREAS */
  prop = api_def_prop(sapi, "title", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "title");
  api_def_prop_array(prop, 2);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Title Safe Margins", "Safe area for text and graphics");
  api_def_prop_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "action", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "action");
  api_def_prop_array(prop, 2);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Action Safe Margins", "Safe area for general elements");
  api_def_prop_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "title_center", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "title_center");
  api_def_prop_array(prop, 2);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Center Title Safe Margins",
                       "Safe area for text and graphics in a different aspect ratio");
  api_def_prop_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "action_center", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "action_center");
  api_def_prop_array(prop, 2);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Center Action Safe Margins",
                       "Safe area for general elements in a different aspect ratio");
  api_def_prop_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);
}

static void api_def_scene_display(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "SceneDisplay", NULL);
  api_def_struct_ui_text(sapi, "Scene Display", "Scene display settings for 3D viewport");
  api_def_struct_stype(sapi, "SceneDisplay");

  prop = api_def_prop(sapi, "light_direction", PROP_FLOAT, PROP_DIRECTION);
  api_def_prop_float_stype(prop, NULL, "light_direction");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Light Direction", "Direction of the light for shadows and highlights");
  api_def_prop_update(prop, NC_SCENE | NA_EDITED, "rna_Scene_set_update");

  prop = api_def_prop(sapi, "shadow_shift", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_ui_text(prop, "Shadow Shift", "Shadow termination angle");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.00f, 1.0f, 1, 2);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | NA_EDITED, "rna_Scene_set_update");

  prop = api_def_prop(sapi, "shadow_focus", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_default(prop, 0.0);
  api_def_prop_ui_text(prop, "Shadow Focus", "Shadow factor hardness");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 1, 2);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_SCENE | NA_EDITED, "rna_Scene_set_update");

  prop = api_def_prop(sapo, "matcap_ssao_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_ui_text(
      prop, "Distance", "Distance of object that contribute to the Cavity/Edge effect");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);

  prop = api_def_prop(sapi, "matcap_ssao_attenuation", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop, "Attenuation", "Attenuation constant");
  api_def_prop_range(prop, 1.0f, 100000.0f);
  api_def_prop_ui_range(prop, 1.0f, 100.0f, 1, 3);

  prop = api_def_prop(sapi, "matcap_ssao_samples", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Samples", "Number of samples");
  api_def_prop_range(prop, 1, 500);

  prop = api_def_prop(sapi, "render_aa", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_scene_display_aa_methods);
  api_def_prop_ui_text(
      prop, "Render Anti-Aliasing", "Method of anti-aliasing when rendering final image");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapo, "viewport_aa", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_scene_display_aa_methods);
  api_def_prop_ui_text(
      prop, "Viewport Anti-Aliasing", "Method of anti-aliasing when rendering 3d viewport");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  /* OpenGL render engine settings. */
  prop = api_def_prop(sapi, "shading", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Shading Settings", "Shading settings for OpenGL render engine");
}

static void api_def_scene_eevee(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem eevee_shadow_size_items[] = {
      {64, "64", 0, "64 px", ""},
      {128, "128", 0, "128 px", ""},
      {256, "256", 0, "256 px", ""},
      {512, "512", 0, "512 px", ""},
      {1024, "1024", 0, "1024 px", ""},
      {2048, "2048", 0, "2048 px", ""},
      {4096, "4096", 0, "4096 px", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem eevee_shadow_pool_size_items[] = {
      {16, "16", 0, "16 MB", ""},
      {32, "32", 0, "32 MB", ""},
      {64, "64", 0, "64 MB", ""},
      {128, "128", 0, "128 MB", ""},
      {256, "256", 0, "256 MB", ""},
      {512, "512", 0, "512 MB", ""},
      {1024, "1024", 0, "1 GB", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem eevee_gi_visibility_size_items[] = {
      {8, "8", 0, "8 px", ""},
      {16, "16", 0, "16 px", ""},
      {32, "32", 0, "32 px", ""},
      {64, "64", 0, "64 px", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem eevee_volumetric_tile_size_items[] = {
      {2, "2", 0, "2 px", ""},
      {4, "4", 0, "4 px", ""},
      {8, "8", 0, "8 px", ""},
      {16, "16", 0, "16 px", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem eevee_motion_blur_position_items[] = {
      {SCE_EEVEE_MB_START, "START", 0, "Start on Frame", "The shutter opens at the current frame"},
      {SCE_EEVEE_MB_CENTER,
       "CENTER",
       0,
       "Center on Frame",
       "The shutter is open during the current frame"},
      {SCE_EEVEE_MB_END, "END", 0, "End on Frame", "The shutter closes at the current frame"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "SceneEEVEE", NULL);
  api_def_struct_path_fn(sapi, "api_SceneEEVEE_path");
  api_def_struct_ui_text(sapi, "Scene Display", "Scene display settings for 3D viewport");

  /* Indirect Lighting */
  prop = api_def_prop(sapi, "gi_diffuse_bounces", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Diffuse Bounces",
                       "Number of times the light is reinjected inside light grids, "
                       "0 disable indirect diffuse light");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "gi_cubemap_resolution", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_shadow_size_items);
  api_def_prop_ui_text(prop, "Cubemap Size", "Size of every cubemaps");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "gi_visibility_resolution", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_gi_visibility_size_items);
  api_def_prop_ui_text(prop,
                       "Irradiance Visibility Size",
                       "Size of the shadow map applied to each irradiance sample");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "gi_irradiance_smoothing", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 5, 2);
  api_def_prop_ui_text(prop,
                       "Irradiance Smoothing",
                       "Smoother irradiance interpolation but introduce light bleeding");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gi_glossy_clamp", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Clamp Glossy",
                       "Clamp pixel intensity to reduce noise inside glossy reflections "
                       "from reflection cubemaps (0 to disable)");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "gi_filter_quality", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Filter Quality", "Take more samples during cubemap filtering to remove artifacts");
  api_def_prop_range(prop, 1.0f, 8.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "gi_show_irradiance", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_styps(prop, NULL, "flag", SCE_EEVEE_SHOW_IRRADIANCE);
  api_def_prop_ui_icon(prop, ICON_HIDE_ON, 1);
  api_def_prop_ui_text(
      prop, "Show Irradiance Cache", "Display irradiance samples in the viewport");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gi_show_cubemaps", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SHOW_CUBEMAPS);
  api_def_prop_ui_icon(prop, ICON_HIDE_ON, 1);
  api_def_prop_ui_text(
      prop, "Show Cubemap Cache", "Display captured cubemaps in the viewport");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gi_irradiance_display_size", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "gi_irradiance_draw_size");
  api_def_prop_range(prop, 0.05f, 10.0f);
  api_def_prop_ui_text(prop,
                       "Irradiance Display Size",
                       "Size of the irradiance sample spheres to debug captured light");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gi_cubemap_display_size", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "gi_cubemap_draw_size");
  api_def_prop_range(prop, 0.05f, 10.0f);
  api_def_prop_ui_text(
      prop, "Cubemap Display Size", "Size of the cubemap spheres to debug captured light");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gi_auto_bake", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_GI_AUTOBAKE);
  api_def_prop_ui_text(prop, "Auto Bake", "Auto bake indirect lighting when editing probes");

  prop = api_def_prop(sapi, "gi_cache_info", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "light_cache_info");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Light Cache Info", "Info on current cache status");

  /* Temporal Anti-Aliasing (super sampling) */
  prop = api_def_prop(sapi, "taa_samples", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Viewport Samples", "Number of samples, unlimited if 0");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  api_def_prop_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "taa_render_samples", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Render Samples", "Number of samples per pixel for rendering");
  api_def_prop_range(prop, 1, INT_MAX);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBR);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  api_def_prop_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_taa_reprojection", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_TAA_REPROJECTION);
  api_def_prop_ui_text(prop,
                       "Viewport Denoising",
                       "Denoise image using temporal reprojection "
                       "(can leave some ghosting)");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
  api_def_prop_flag(prop, PROP_ANIMATABLE);

  /* Screen Space Subsurface Scattering */
  prop = api_def_prop(sapi, "sss_samples", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Samples", "Number of samples to compute the scattering effect");
  api_def_prop_range(prop, 1, 32);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "sss_jitter_threshold", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Jitter Threshold", "Rotate samples that are below this threshold");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Screen Space Reflection */
  prop = api_def_prop(sapi, "use_ssr", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SSR_ENABLED);
  api_def_prop_ui_text(prop, "Screen Space Reflections", "Enable screen space reflection");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_ssr_refraction", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SSR_REFRACTION);
  api_def_prop_ui_text(prop, "Screen Space Refractions", "Enable screen space Refractions");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_ssr_halfres", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SSR_HALF_RESOLUTION);
  api_def_prop_ui_text(prop, "Half Res Trace", "Raytrace at a lower resolution");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ssr_quality", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Trace Precision", "Precision of the screen space ray-tracing");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ssr_max_roughness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Max Roughness", "Do not raytrace reflections for roughness above this value");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ssr_thickness", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_ui_text(prop, "Thickness", "Pixel thickness used to detect intersection");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 5, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ssr_border_fade", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Edge Fading", "Screen percentage used to fade the SSR");
  api_def_prop_range(prop, 0.0f, 0.5f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "ssr_firefly_fac", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop, "Clamp", "Clamp pixel intensity to remove noise (0 to disable)");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Volumetrics */
  prop = api_def_prop(sapi, "volumetric_start", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_ui_text(prop, "Start", "Start distance of the volumetric effect");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_end", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_ui_text(prop, "End", "End distance of the volumetric effect");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_tile_size", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_volumetric_tile_size_items);
  api_def_prop_ui_text(prop,
                           "Tile Size",
                           "Control the quality of the volumetric effects "
                           "(lower size increase vram usage and quality)");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_samples", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Samples", "Number of samples to compute volumetric effects");
  api_def_prop_range(prop, 1, 256);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_sample_distribution", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Exponential Sampling", "Distribute more samples closer to the camera");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_volumetric_lights", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_VOLUMETRIC_LIGHTS);
  api_def_prop_ui_text(
      prop, "Volumetric Lighting", "Enable scene light interactions with volumetrics");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_light_clamp", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(prop, "Clamp", "Maximum light contribution, reducing noise");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_volumetric_shadows", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_VOLUMETRIC_SHADOWS);
  api_def_prop_ui_text(
      prop, "Volumetric Shadows", "Generate shadows from volumetric material (Very expensive)");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "volumetric_shadow_samples", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, 128);
  api_def_prop_ui_text(
      prop, "Volumetric Shadow Samples", "Number of samples to compute volumetric shadowing");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Ambient Occlusion */
  prop = api_def_prop(sapi, "use_gtao", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypd(prop, NULL, "flag", SCE_EEVEE_GTAO_ENABLED);
  api_def_prop_ui_text(prop,
                       "Ambient Occlusion",
                       "Enable ambient occlusion to simulate medium scale indirect shadowing");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB;
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_gtao_bent_normals", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_GTAO_BENT_NORMALS);
  api_def_prop_ui_text(
      prop, "Bent Normals", "Compute main non occluded direction to sample the environment");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_gtao_bounce", PROP_BOOL, PROP_NONE);
  api_def_prop_bool(prop, NULL, "flag", SCE_EEVEE_GTAO_BOUNCE);
  api_def_prop_ui_text(prop,
                       "Bounces Approximation",
                       "An approximation to simulate light bounces "
                       "giving less occlusion on brighter objects");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gtao_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Factor", "Factor for ambient occlusion blending");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.1f, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gtao_quality", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Trace Precision", "Precision of the horizon search");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "gtao_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_ui_text(
      prop, "Distance", "Distance of object that contribute to the ambient occlusion effect");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Depth of Field */
  prop = api_def_prop(sapi, "bokeh_max_size", PROP_FLOAT, PROP_PIXEL);
  api_def_prop_ui_text(
      prop, "Max Size", "Max size of the bokeh shape for the depth of field (lower is faster)");
  api_def_prop_range(prop, 0.0f, 2000.0f);
  api_def_prop_ui_range(prop, 0.0f, 200.0f, 100.0f, 1);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "bokeh_threshold", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Sprite Threshold", "Brightness threshold for using sprite base depth of field");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 10, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sali, "bokeh_neighbor_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop,
                       "Neighbor Rejection",
                       "Maximum brightness to consider when rejecting bokeh sprites "
                       "based on neighborhood (lower is faster)");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 40.0f, 10, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bokeh_denoise_fac", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Denoise Amount", "Amount of flicker removal applied to bokeh highlights");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 10, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bokeh_high_quality_slight_defocus", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_DOF_HQ_SLIGHT_FOCUS);
  api_def_prop_ui_text(prop,
                       "High Quality Slight Defocus",
                       "Sample all pixels in almost in-focus regions to eliminate noise");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_bokeh_jittered", PROP_BOOL, PROP_NONE);
  apj_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_DOF_JITTER);
  api_def_prop_ui_text(prop,
                       "Jitter Camera",
                       "Jitter camera position to create accurate blurring "
                       "using render samples");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bokeh_overblur", PROP_FLOAT, PROP_PERCENTAGE);
  api_def_prop_ui_text(prop,
                       "Over-blur",
                       "Apply blur to each jittered sample to reduce "
                       "under-sampling artifacts");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_range(prop, 0.0f, 20.0f, 1, 1);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  /* Bloom */
  prop = api_def_prop(sapi, "use_bloom", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_BLOOM_ENABLED);
  api_def_prop_ui_text(prop, "Bloom", "High brightness pixels generate a glowing effect");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_threshold", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Threshold", "Filters out pixels under this level of brightness");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "Color applied to the bloom effect");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_knee", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Knee", "Makes transition between under/over-threshold gradual");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_radius", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Radius", "Bloom spread distance");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_clamp", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Clamp", "Maximum intensity a bloom pixel can have (0 to disable)");
  api_def_prop_range(prop, 0.0f, 100000.0f);
  api_def_prop_ui_range(prop, 0.0f, 1000.0f, 1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "bloom_intensity", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Intensity", "Blend factor");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_range(prop, 0.0f, 0.1f, 1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Motion blur */
  prop = api_def_prop(sapi, "use_motion_blur", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_MOTION_BLUR_ENABLED);
  api_def_prop_ui_text(prop, "Motion Blur", "Enable motion blur effect (only in camera view)");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "motion_blur_shutter", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Shutter", "Time taken in frames between shutter open and close");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.01f, 1.0f, 1, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "motion_blur_depth_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Background Separation",
                       "Lower values will reduce background"
                       " bleeding onto foreground elements");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.01f, 1000.0f, 1, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "motion_blur_max", PROP_INT, PROP_PIXEL);
  api_def_prop_ui_text(prop, "Max Blur", "Maximum blur distance a pixel can spread over");
  api_def_prop_range(prop, 0, 2048);
  api_def_prop_ui_range(prop, 0, 512, 1, -1);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "motion_blur_steps", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Motion steps",
                       "Controls accuracy of motion blur, "
                       "more steps means longer render time");
  api_def_prop_range(prop, 1, INT_MAX);
  api_def_prop_ui_range(prop, 1, 64, 1, -1);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "motion_blur_position", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_motion_blur_position_items);
  api_def_prop_ui_text(prop,
                       "Motion Blur Position",
                       "Offset for the shutter's time interval, "
                       "allows to change the motion blur trails");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Shadows */
  prop = api_def_prop(sapi, "use_shadows", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SHADOW_ENABLED);
  api_def_prop_ui_text(prop, "Shadows", "Enable shadow casting from lights");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "shadow_cube_size", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_shadow_size_items);
  api_def_prop_ui_text(
      prop, "Cube Shadows Resolution", "Size of point and area light shadow maps");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "shadow_cascade_size", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_shadow_size_items);
  api_def_prop_ui_text(
      prop, "Directional Shadows Resolution", "Size of sun light shadow maps");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "shadow_pool_size", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, eevee_shadow_pool_size_items);
  api_def_prop_ui_text(prop,
                       "Shadow Pool Size",
                       "Size of the shadow pool, "
                       "a bigger pool size allows for more shadows in the scene "
                       "but might not fit into GPU memory");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_shadow_high_bitdepth", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SHADOW_HIGH_BITDEPTH);
  api_def_prop_ui_text(prop, "High Bit Depth", "Use 32-bit shadows");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "use_soft_shadows", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_SHADOW_SOFT);
  api_def_prop_ui_text(
      prop, "Soft Shadows", "Randomize shadowmaps origin to create soft shadows");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  prop = api_def_prop(sapi, "light_threshold", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_ui_text(prop,
                       "Light Threshold",
                       "Minimum light intensity for a light to contribute to the lighting");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.1, 3);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Overscan */
  prop = api_def_prop(sapi, "use_overscan", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_EEVEE_OVERSCAN);
  api_def_prop_ui_text(prop,
                       "Overscan",
                       "Internally render past the image border to avoid "
                       "screen-space effects disappearing");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);

  prop = api_def_prop(sapi, "overscan_size", PROP_FLOAT, PROP_PERCENTAGE);
  api_def_prop_float_stype(prop, NULL, "overscan");
  api_def_prop_ui_text(prop,
                       "Overscan Size",
                       "Percentage of render size to add as overscan to the "
                       "internal render buffers");
  api_def_prop_range(prop, 0.0f, 50.0f);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 1, 2);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
}

static void api_def_scene_pen(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ScenePen", NULL);
  api_def_struct_path_fn(sapi, "api_ScenePen_path");
  api_def_struct_ui_text(sapi, "Pen Render", "Render settings");

  prop = api_def_prop(sapi, "antialias_threshold", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "smaa_threshold");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 2.0f, 1, 3);
  api_def_prop_ui_text(prop,
                       "Anti-Aliasing Threshold",
                       "Threshold for edge detection algorithm (higher values might over-blur "
                       "some part of the image)");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);
}

void api_def_scene(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem audio_distance_model_items[] = {
      {0, "NONE", 0, "None", "No distance attenuation"},
      {1, "INVERSE", 0, "Inverse", "Inverse distance model"},
      {2, "INVERSE_CLAMPED", 0, "Inverse Clamped", "Inverse distance model with clamping"},
      {3, "LINEAR", 0, "Linear", "Linear distance model"},
      {4, "LINEAR_CLAMPED", 0, "Linear Clamped", "Linear distance model with clamping"},
      {5, "EXPONENT", 0, "Exponential", "Exponential distance model"},
      {6,
       "EXPONENT_CLAMPED",
       0,
       "Exponential Clamped",
       "Exponential distance model with clamping"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem sync_mode_items[] = {
      {0, "NONE", 0, "Play Every Frame", "Do not sync, play every frame"},
      {SCE_FRAME_DROP, "FRAME_DROP", 0, "Frame Dropping", "Drop frames if playback is too slow"},
      {AUDIO_SYNC, "AUDIO_SYNC", 0, "Sync to Audio", "Sync to audio playback, dropping frames"},
      {0, NULL, 0, NULL, NULL},
  };

  /* Struct definition */
  sapi = api_def_struct(dapi, "Scene", "ID");
  api_def_struct_ui_text(sapi,
                         "Scene",
                         "Scene data-block, consisting in objects and "
                         "defining time and render related settings");
  api_def_struct_ui_icon(sapi, ICON_SCENE_DATA);
  api_def_struct_clear_flag(sapi, STRUCT_ID_REFCOUNT);

  /* Global Settings */
  prop = api_def_prop(sapi, "camera", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ptr_fns(prop, NULL, NULL, NULL, "api_Camera_object_poll");
  api_def_prop_ui_text(prop, "Camera", "Active camera, used for rendering the scene");
  api_def_prop_update(prop, NC_SCENE | NA_EDITED, "api_Scene_camera_update");

  prop = api_def_prop(sapi, "background_set", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "set");
  api_def_prop_struct_type(prop, "Scene");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ptr_fns(prop, NULL, "api_Scene_set_set", NULL, NULL);
  api_def_prop_ui_text(prop, "Background Scene", "Background set scene");
  api_def_prop_update(prop, NC_SCENE | NA_EDITED, "api_Scene_set_update");

  prop = api_def_prop(sapi, "world", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "World", "World used for rendering the scene");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_WORLD);
  api_def_prop_update(prop, NC_SCENE | ND_WORLD, "api_Scene_world_update");

  prop = api_def_prop(sapi, "objects", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ui_text(prop, "Objects", "");
  api_def_prop_collection_fns(prop,
                              "api_Scene_objects_begin",
                              "api_Scene_objects_next",
                              "api_Scene_objects_end",
                              "api_Scene_objects_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_scene_objects(dapi, prop);

  /* Frame Range Stuff */
  prop = api_def_prop(sapi, "frame_current", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.cfra");
  api_def_prop_range(prop, MINAFRAME, MAXFRAME);
  api_def_prop_int_fns(prop, NULL, "api_Scene_frame_current_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Current Frame",
      "Current frame, to update animation data from python frame_set() instead");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, "api_Scene_frame_update");

  prop = api_def_prop(sapi, "frame_subframe", PROP_FLOAT, PROP_TIME);
  api_def_prop_float_stype(prop, NULL, "r.subframe");
  api_def_prop_ui_text(prop, "Current Subframe", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.01, 2);
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, "api_Scene_frame_update");

  prop = api_def_prop(sapi, "frame_float", PROP_FLOAT, PROP_TIME);
  api_def_prop_ui_text(prop, "Current Subframe", "");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, MINAFRAME, MAXFRAME);
  api_def_prop_ui_range(prop, MINAFRAME, MAXFRAME, 0.1, 2);
  api_def_prop_float_fns(
      prop, "api_Scene_frame_float_get", "api_Scene_frame_float_set", NULL);
  wpi_def_prop_update(prop, NC_SCENE | ND_FRAME, "api_Scene_frame_update");

  prop = api_def_prop(sapi, "frame_start", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.sfra");
  api_def_prop_int_fns(prop, NULL, "api_Scene_start_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "Start Frame", "First frame of the playback/rendering range");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME_RANGE, NULL);

  prop = api_def_prop(sapi, "frame_end", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.efra");
  api_def_prop_int_fns(prop, NULL, "api_Scene_end_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "End Frame", "Final frame of the playback/rendering range");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME_RANGE, NULL);

  prop = api_def_prop(sapi, "frame_step", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.frame_step");
  api_def_prop_range(prop, 0, MAXFRAME);
  api_def_prop_ui_range(prop, 1, 100, 1, -1);
  api_def_prop_ui_text();
  api_def_prop(sapi, "frame_current_final", PROP_FLOAT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_range(prop, MINAFRAME, MAXFRAME);
  api_def_prop_float_fns(prop, "rna_Scene_frame_current_final_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Current Frame Final", "Current frame with subframe and time remapping applied");

  prop = api_def_prop(sapi, "lock_frame_selection_to_range", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "r.flag", SCER_LOCK_FRAME_SELECTION);
  api_def_prop_ui_text(prop,
                       "Lock Frame Selection",
                       "Don't allow frame to be selected with mouse outside of frame range");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, NULL);

  /* Preview Range (frame-range for UI playback) */
  prop = api_def_prop(sapi, "use_preview_range", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "r.flag", SCER_PRV_RANGE);
  api_def_prop_bool_fns(prop, NULL, "api_Scene_use_preview_range_set");
  api_def_prop_ui_text(
      prop,
      "Use Preview Range",
      "Use an alternative start/end frame range for animation playback and view renders");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, NULL);
  api_def_prop_ui_icon(prop, ICON_PREVIEW_RANGE, 0);

  prop = api_def_prop(sapi, "frame_preview_start", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.psfra");
  api_def_prop_int_fns(prop, NULL, "api_Scene_preview_range_start_frame_set", NULL);
  api_def_prop_ui_text(
      prop, "Preview Range Start Frame", "Alternative start frame for UI playback");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, NULL);

  prop = api_def_prop(sapi, "frame_preview_end", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "r.pefra");
  api_def_prop_int_fns(prop, NULL, "api_Scene_preview_range_end_frame_set", NULL);
  api_def_prop_ui_text(
      prop, "Preview Range End Frame", "Alternative end frame for UI playback");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, NULL);

  /* Sub-frame for motion-blur debug. */
  prop = api_def_prop(sapi, "show_subframe", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "r.flag", SCER_SHOW_SUBFRAME);
  api_def_prop_ui_text(
      prop, "Show Subframe", "Show current scene subframe and allow set it using interface tools");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, "api_Scene_show_subframe_update");

  /* Timeline / Time Navigation settings */
  prop = api_def_prop(sapi, "show_keys_from_selected_only", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", SCE_KEYS_NO_SELONLY);
  api_def_prop_ui_text(prop,
                           "Only Keyframes from Selected Channels",
                           "Consider keyframes for active object and/or its selected bones only "
                           "(in timeline and when jumping between keyframes)");
  api_def_prop_update(prop, NC_SCENE | ND_FRAME, NULL);

  /* Stamp */
  prop = api_def_prop(sapi, "use_stamp_note", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "r.stamp_udata");
  api_def_
      prop_ui_text(prop, "Stamp Note", "User defined note for the render stamping");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, NULL);

  /* Animation Data (for Scene) */
  api_def_animdata_common(srna);

  /* Readonly Properties */
  prop = api_def_prop(sapi, "is_nla_tweakmode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_NLA_EDIT_ON);
  api_def_prop_clear_flag(prop, PROP_EDITABLE); /* DO NOT MAKE THIS EDITABLE, OR NLA EDITOR BREAKS */
  api_def_prop_ui_text(
      prop,
      "NLA Tweak Mode",
      "Whether there is any action referenced by NLA being edited (strictly read-only)");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_GRAPH, NULL);

  /* Frame dropping flag for playback and sync enum */
#  if 0 /* XXX: Is this actually needed? */
  prop = api_def_prop(sapi, "use_frame_drop", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SCE_FRAME_DROP);
  api_def_prop_ui_text(
      prop, "Frame Dropping", "Play back dropping frames if frame display is too slow");
  api_def_prop_update(prop, NC_SCENE, NULL);
#  endif

  prop = api_def_prop(sapi, "sync_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_fns(prop, "api_Scene_sync_mode_get", "api_Scene_sync_mode_set", NULL);
  api_def_prop_enum_items(prop, sync_mode_items);
  api_def_prop_enum_default(prop, AUDIO_SYNC);
  api_def_prop_ui_text(prop, "Sync Mode", "How to sync playback");
  api_def_prop_update(prop, NC_SCENE, NULL);

  /* Nodes (Compositing) */
  prop = api_def_prop(sapi, "node_tree", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "nodetree");
  api_def_prop_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Node Tree", "Compositing node tree");
  prop = api_def_prop(sapi, "use_nodes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_nodes", 1);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_ui_text(prop, "Use Nodes", "Enable the compositing node tree");
  api_def_prop_update(prop, NC_SCENE | ND_RENDER_OPTIONS, "api_Scene_use_nodes_update");

  /* Seq */
  prop = api_def_prop(sapi, "seq_editor", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ed");
  api_def_prop_struct_type(prop, "SeqEditor");
  api_def_prop_ui_text(prop, "Seq Editor", "");

  /* Keying Sets */
  prop = api_def_prop(sapi, "keying_sets", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "keyingsets", NULL);
  api_def_prop_struct_type(prop, "KeyingSet");
  api_def_prop_ui_text(prop, "Absolute Keying Sets", "Absolute Keying Sets for this Scene");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);
  api_def_scene_keying_sets(dapi, prop);

  prop = api_def_prop(sapi, "keying_sets_all", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_Scene_all_keyingsets_begin",
                              "api_Scene_all_keyingsets_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "KeyingSet");
  api_def_prop_ui_text(
      prop,
      "All Keying Sets",
      "All Keying Sets available for use (Builtins and Absolute Keying Sets for this Scene)");
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET, NULL);
  api_def_scene_keying_sets_all(dapi, prop);

  /* Rigid Body Simulation */
  prop = api_def_prop(sapi, "rigidbody_world", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "rigidbody_world");
  api_def_prop_struct_type(prop, "RigidBodyWorld");
  api_def_prop_ui_text(prop, "Rigid Body World", "");
  api_def_prop_update(prop, NC_SCENE, "api_Phys_relations_update");

  /* Tool Settings */
  prop = api_def_prop(sapi, "tool_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "toolsettings");
  api_def_prop_struct_type(prop, "ToolSettings");
  api_def_prop_ui_text(prop, "Tool Settings", "");

  /* Unit Settings */
  prop = api_def_prop(sapi, "unit_settings", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "unit");
  api_def_prop_struct_type(prop, "UnitSettings");
  api_def_prop_ui_text(prop, "Unit Settings", "Unit editing settings");

  /* Physics Settings */
  prop = api_def_prop(sapi, "gravity", PROP_FLOAT, PROP_ACCELERATION);
  api_def_prop_float_stype(prop, NULL, "physics_settings.gravity");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_range(prop, -200.0f, 200.0f, 1, 2);
  api_def_prop_ui_text(prop, "Gravity", "Constant acceleration in a given direction");
  api_def_prop_update(prop, 0, "api_Phys_update");

  prop = api_def_prop(sapi, "use_gravity", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "phys_settings.flag", PHYS_GLOBAL_GRAVITY);
  api_def_prop_ui_text(prop, "Global Gravity", "Use global gravity for all dynamics");
  api_def_prop_update(prop, 0, "api_Phys_update");

  /* Render Data */
  prop = api_def_prop(sapi, "render", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "r");
  api_def_prop_struct_type(prop, "RenderSettings");
  api_def_prop_ui_text(prop, "Render Data", "");

  /* Safe Areas */
  prop = api_def_prop(sapi, "safe_areas", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_
      stype(prop, NULL, "safe_areas");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "DisplaySafeAreas");
  api_def_prop_ui_text(prop, "Safe Areas", "");

  /* Markers */
  prop = api_def_prop(sapi, "timeline_markers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "markers", NULL);
  api_def_prop_struct_type(prop, "TimelineMarker");
  api_def_prop_ui_text(
      prop, "Timeline Markers", "Markers used in all timelines for the current scene");
  api_def_timeline_markers(dapi, prop);

  /* Transform Orientations */
  prop = api_def_prop(sapi, "transform_orientation_slots", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_Scene_transform_orientation_slots_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Scene_transform_orientation_slots_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "TransformOrientationSlot");
  api_def_prop_ui_text(prop, "Transform Orientation Slots", "");

  /* 3D View Cursor */
  prop = api_def_prop(sapi, "cursor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "cursor");
  api_def_prop_struct_type(prop, "View3DCursor");
  api_def_prop_ui_text(prop, "3D Cursor", "");

  /* Audio Settings */
  prop = api_def_property(srna, "use_audio", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_fns(prop, "rna_Scene_use_audio_get", "rna_Scene_use_audio_set");
  RNA_def_property_ui_text(
      prop, "Audio Muted", "Play back of audio from Sequence Editor will be muted");
  RNA_def_property_update(prop, NC_SCENE, "rna_Scene_use_audio_update");

#  if 0 /* XXX: Is this actually needed? */
  prop = RNA_def_property(srna, "use_audio_sync", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "audio.flag", AUDIO_SYNC);
  RNA_def_property_ui_text(
      prop,
      "Audio Sync",
      "Play back and sync with audio clock, dropping frames if frame display is too slow");
  RNA_def_property_update(prop, NC_SCENE, NULL);
#  endif

  prop = RNA_def_property(srna, "use_audio_scrub", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "audio.flag", AUDIO_SCRUB);
  RNA_def_property_ui_text(
      prop, "Audio Scrubbing", "Play audio from Sequence Editor while scrubbing");
  RNA_def_property_update(prop, NC_SCENE, NULL);

  prop = RNA_def_property(srna, "audio_doppler_speed", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "audio.speed_of_sound");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_range(prop, 0.01f, FLT_MAX);
  RNA_def_property_ui_text(
      prop, "Speed of Sound", "Speed of sound for Doppler effect calculation");
  RNA_def_property_update(prop, NC_SCENE, "rna_Scene_listener_update");

  prop = RNA_def_prop(sapi, "audio_doppler_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_prop_float_stype(prop, NULL, "audio.doppler_factor");
  RNA_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_prop_range(prop, 0.0, FLT_MAX);
  RNA_def_prop_ui_text(prop, "Doppler Factor", "Pitch factor for Doppler effect calculation");
  RNA_def_prop_update(prop, NC_SCENE, "rna_Scene_listener_update");

  prop = api_def_prop(sapi, "audio_distance_model", PROP_ENUM, PROP_NONE);
  apk_def_prop_enum_bitflag_sdna(prop, NULL, "audio.distance_model");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, audio_distance_model_items);
  api_def_prop_ui_text(
      prop, "Distance Model", "Distance model for distance attenuation calculation");
  api_def_prop_update(prop, NC_SCENE, "api_Scene_listener_update");

  prop = api_def_prop(sapi, "audio_volume", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "audio.volume");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Volume", "Audio volume");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SOUND);
  api_def_prop_update(prop, NC_SCENE, NULL);
  api_def_prop_update(prop, NC_SCENE, "api_Scene_volume_update");

  fn = api_def_fn(sapi, "update_render_engine", "api_Scene_update_render_engine");
  api_def_fn_flag(fn, FN_NO_SELF | FUNC_USE_MAIN);
  api_def_fn_ui_description(fn, "Trigger a render engine update");

  /* Statistics */
  fn = apj_def_fn(sapi, "statistics", "api_Scene_statistics_string_get");
  api_def_function_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);
  parm = api_def_pointer(fn, "view_layer", "ViewLayer", "View Layer", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_string(fn, "statistics", NULL, 0, "Statistics", "");
  api_def_fn_return(fn, parm);

  /* Pen */
  prop = api_def_prop(sapi, "dune_pen", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "gpd");
  api_def_prop_struct_type(prop, "Pen");
  api_def_prop_ptr_fns(
      prop, NULL, NULL, NULL, "api_pen_datablocks_annotations_poll");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_REFCOUNT);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Annotations", "Dune Pen data-block used for annotations in the 3D view");
  api_def_prop_update(prop, NC_PEN | ND_DATA | NA_EDITED, NULL);

  /* active MovieClip */
  prop = api_def_prop(sapi, "active_clip", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "clip");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "MovieClip");
  api_def_prop_ui_text(prop,
                       "Active Movie Clip",
                       "Active Movie Clip that can be used by motion tracking constraints "
                       "or as a camera's background image");
  api_def_prop_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

  /* color management */
  prop = api_def_prop(sapi, "view_settings", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "view_settings");
  api_def_prop_struct_type(prop, "ColorManagedViewSettings");
  api_def_prop_ui_text(
      prop, "View Settings", "Color management settings applied on image before saving");

  prop = api_def_prop(sapi, "display_settings", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "display_settings");
  api_def_prop_struct_type(prop, "ColorManagedDisplaySettings");
  api_def_prop_ui_text(
      prop, "Display Settings", "Settings of device saved image would be displayed on");

  prop = api_def_prop(sapi, "seq_colorspace_settings", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "seq_colorspace_settings");
  api_def_prop_struct_type(prop, "ColorManagedSeqColorspaceSettings");
  api_def_prop_ui_text(
      prop, "Seq Color Space Settings", "Settings of color space sequencer is working in");

  /* Layer and Collections */
  prop = api_def_prop(sapi, "view_layers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "view_layers", NULL);
  api_def_prop_struct_type(prop, "ViewLayer");
  api_def_prop_ui_text(prop, "View Layers", "");
  api_def_view_layers(brna, prop);

  prop = api_def_prop(sapi, "collection", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "master_collection");
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop,
                           "Collection",
                           "Scene root collection that owns all the objects and other collections "
                           "instantiated in the scene");

  /* Scene Display */
  prop = api_def_prop(sapi, "display", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "display");
  api_def_prop_struct_type(prop, "SceneDisplay");
  api_def_prop_ui_text(prop, "Scene Display", "Scene display settings for 3D viewport");

  /* EEVEE */
  prop = api_def_prop(sapi, "eevee", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "SceneEEVEE");
  api_def_prop_ui_text(prop, "Eevee", "Eevee settings for the scene");

  /* Grease Pencil */
  prop = api_def_prop(sapi, "grease_pencil_settings", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "SceneGpencil");
  api_def_prop_ui_text(prop, "Grease Pencil", "Grease Pencil settings for the scene");

  /* Nestled Data. */
  /* *** Non-Animated *** */
  api_define_animate_stype(false);
  api_def_tool_settings(dapi);
  api_def_pen_interpolate(dapi);
  api_def_unified_paint_settings(dapi);
  api_def_curve_paint_settings(dapi);
  api_def_seq_tool_settings(dapi);
  api_def_statvis(dapi);
  api_def_unit_settings(dapi);
  api_def_scene_image_format_data(dapi);
  api_def_transform_orientation(dapi);
  api_def_transform_orientation_slot(dapi);
  api_def_view3d_cursor(dapi);
  api_def_selected_uv_element(dapi);
  api_def_display_safe_areas(dapi);
  api_def_scene_display(dapi);
  api_def_scene_eevee(dapi);
  api_def_view_layer_aov(dapi);
  api_def_view_layer_lightgroup(dapi);
  api_def_view_layer_eevee(dapi);
  api_def_scene_pen(dapi);
  api_define_animate_stype(true);
  /* *** Animated *** */
  api_def_scene_render_data(dapi);
  api_def_scene_render_view(dapi);

  /* Scene API */
  api_scene(sapi);
}

#endif
