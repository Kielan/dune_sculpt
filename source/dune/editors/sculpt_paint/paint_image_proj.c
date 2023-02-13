/** \file
 * \ingroup edsculpt
 * \brief Functions to paint images in 2D and 3D.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#ifdef WIN32
#  include "BLI_winstuff.h"
#endif

#include "BLI_blenlib.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_math_bits.h"
#include "BLI_math_color_blend.h"
#include "BLI_memarena.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "atomic_ops.h"

#include "BLT_translation.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "DNA_brush_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"

#include "BKE_brush.h"
#include "BKE_camera.h"
#include "BKE_colorband.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_image.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_runtime.h"
#include "BKE_node.h"
#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ED_node.h"
#include "ED_object.h"
#include "ED_paint.h"
#include "ED_screen.h"
#include "ED_uvedit.h"
#include "ED_view3d.h"
#include "ED_view3d_offscreen.h"

#include "GPU_capabilities.h"
#include "GPU_init_exit.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "IMB_colormanagement.h"

//#include "bmesh_tools.h"

#include "paint_intern.h"

static void partial_redraw_array_init(ImagePaintPartialRedraw *pr);

/* Defines and Structs */
/* unit_float_to_uchar_clamp as inline function */
BLI_INLINE uchar f_to_char(const float val)
{
  return unit_float_to_uchar_clamp(val);
}

/* ProjectionPaint defines */

/* approx the number of buckets to have under the brush,
 * used with the brush size to set the ps->buckets_x and ps->buckets_y value.
 *
 * When 3 - a brush should have ~9 buckets under it at once
 * ...this helps for threading while painting as well as
 * avoiding initializing pixels that won't touch the brush */
#define PROJ_BUCKET_BRUSH_DIV 4

#define PROJ_BUCKET_RECT_MIN 4
#define PROJ_BUCKET_RECT_MAX 256

#define PROJ_BOUNDBOX_DIV 8
#define PROJ_BOUNDBOX_SQUARED (PROJ_BOUNDBOX_DIV * PROJ_BOUNDBOX_DIV)

//#define PROJ_DEBUG_PAINT 1
//#define PROJ_DEBUG_NOSEAMBLEED 1
//#define PROJ_DEBUG_PRINT_CLIP 1
#define PROJ_DEBUG_WINCLIP 1

#ifndef PROJ_DEBUG_NOSEAMBLEED
/* projectFaceSeamFlags options */
//#define PROJ_FACE_IGNORE  (1<<0)  /* When the face is hidden, back-facing or occluded. */
//#define PROJ_FACE_INIT    (1<<1)  /* When we have initialized the faces data */

/* If this face has a seam on any of its edges. */
#  define PROJ_FACE_SEAM0 (1 << 0)
#  define PROJ_FACE_SEAM1 (1 << 1)
#  define PROJ_FACE_SEAM2 (1 << 2)

#  define PROJ_FACE_NOSEAM0 (1 << 4)
#  define PROJ_FACE_NOSEAM1 (1 << 5)
#  define PROJ_FACE_NOSEAM2 (1 << 6)

/* If the seam is completely initialized, including adjacent seams. */
#  define PROJ_FACE_SEAM_INIT0 (1 << 8)
#  define PROJ_FACE_SEAM_INIT1 (1 << 9)
#  define PROJ_FACE_SEAM_INIT2 (1 << 10)

#  define PROJ_FACE_DEGENERATE (1 << 12)

/* face winding */
#  define PROJ_FACE_WINDING_INIT 1
#  define PROJ_FACE_WINDING_CW 2

/* a slightly scaled down face is used to get fake 3D location for edge pixels in the seams
 * as this number approaches  1.0f the likelihood increases of float precision errors where
 * it is occluded by an adjacent face */
#  define PROJ_FACE_SCALE_SEAM 0.99f
#endif /* PROJ_DEBUG_NOSEAMBLEED */

#define PROJ_SRC_VIEW 1
#define PROJ_SRC_IMAGE_CAM 2
#define PROJ_SRC_IMAGE_VIEW 3
#define PROJ_SRC_VIEW_FILL 4

#define PROJ_VIEW_DATA_ID "view_data"
/* viewmat + winmat + clip_start + clip_end + is_ortho */
#define PROJ_VIEW_DATA_SIZE (4 * 4 + 4 * 4 + 3)

#define PROJ_BUCKET_NULL 0
#define PROJ_BUCKET_INIT (1 << 0)
// #define PROJ_BUCKET_CLONE_INIT   (1<<1)

/* used for testing doubles, if a point is on a line etc */
#define PROJ_GEOM_TOLERANCE 0.00075f
#define PROJ_PIXEL_TOLERANCE 0.01f

/* vert flags */
#define PROJ_VERT_CULL 1

/* to avoid locking in tile initialization */
#define TILE_PENDING POINTER_FROM_INT(-1)

/**
 * This is mainly a convenience struct used so we can keep an array of images we use -
 * their imbufs, etc, in 1 array, When using threads this array is copied for each thread
 * because 'partRedrawRect' and 'touch' values would not be thread safe.
 */
typedef struct ProjPaintImage {
  Image *ima;
  ImageUser iuser;
  ImBuf *ibuf;
  ImagePaintPartialRedraw *partRedrawRect;
  /** Only used to build undo tiles during painting. */
  volatile void **undoRect;
  /** The mask accumulation must happen on canvas, not on space screen bucket.
   * Here we store the mask rectangle. */
  ushort **maskRect;
  /** Store flag to enforce validation of undo rectangle. */
  bool **valid;
  bool touch;
} ProjPaintImage;

/**
 * Handle for stroke (operator customdata)
 */
typedef struct ProjStrokeHandle {
  /* Support for painting from multiple views at once,
   * currently used to implement symmetry painting,
   * we can assume at least the first is set while painting. */
  struct ProjPaintState *ps_views[8];
  int ps_views_tot;
  int symmetry_flags;

  int orig_brush_size;

  bool need_redraw;

  /* trick to bypass regular paint and allow clone picking */
  bool is_clone_cursor_pick;

  /* In ProjPaintState, only here for convenience */
  Scene *scene;
  Brush *brush;
} ProjStrokeHandle;

typedef struct LoopSeamData {
  float seam_uvs[2][2];
  float seam_puvs[2][2];
  float corner_dist_sq[2];
} LoopSeamData;

/* Main projection painting struct passed to all projection painting functions */
typedef struct ProjPaintState {
  View3D *v3d;
  RegionView3D *rv3d;
  ARegion *region;
  Depsgraph *depsgraph;
  Scene *scene;
  /* PROJ_SRC_**** */
  int source;

  /* the paint color. It can change depending of inverted mode or not */
  float paint_color[3];
  float paint_color_linear[3];
  float dither;

  Brush *brush;
  short tool, blend, mode;

  float brush_size;
  Object *ob;
  /* for symmetry, we need to store modified object matrix */
  float obmat[4][4];
  float obmat_imat[4][4];
  /* end similarities with ImagePaintState */

  Image *stencil_ima;
  Image *canvas_ima;
  Image *clone_ima;
  float stencil_value;

  /* projection painting only */
  /** For multi-threading, the first item is sometimes used for non threaded cases too. */
  MemArena *arena_mt[BLENDER_MAX_THREADS];
  /** screen sized 2D array, each pixel has a linked list of ProjPixel's */
  LinkNode **bucketRect;
  /** bucketRect aligned array linkList of faces overlapping each bucket. */
  LinkNode **bucketFaces;
  /** store if the bucks have been initialized. */
  uchar *bucketFlags;

  /** store options per vert, now only store if the vert is pointing away from the view. */
  char *vertFlags;
  /** The size of the bucket grid, the grid span's screenMin/screenMax
   * so you can paint outsize the screen or with 2 brushes at once. */
  int buckets_x;
  int buckets_y;

  /** result of project_paint_pixel_sizeof(), constant per stroke. */
  int pixel_sizeof;

  /** size of projectImages array. */
  int image_tot;

  /** verts projected into floating point screen space. */
  float (*screenCoords)[4];
  /** 2D bounds for mesh verts on the screen's plane (screen-space). */
  float screenMin[2];
  float screenMax[2];
  /** Calculated from screenMin & screenMax. */
  float screen_width;
  float screen_height;
  /** From the area or from the projection render. */
  int winx, winy;

  /* options for projection painting */
  bool do_layer_clone;
  bool do_layer_stencil;
  bool do_layer_stencil_inv;
  bool do_stencil_brush;
  bool do_material_slots;

  /** Use ray-traced occlusion? - otherwise will paint right through to the back. */
  bool do_occlude;
  /** ignore faces with normals pointing away,
   * skips a lot of ray-casts if your normals are correctly flipped. */
  bool do_backfacecull;
  /** mask out pixels based on their normals. */
  bool do_mask_normal;
  /** mask out pixels based on cavity. */
  bool do_mask_cavity;
  /** what angle to mask at. */
  float normal_angle;
  /** cos(normal_angle), faster to compare. */
  float normal_angle__cos;
  float normal_angle_inner;
  float normal_angle_inner__cos;
  /** difference between normal_angle and normal_angle_inner, for easy access. */
  float normal_angle_range;

  /** quick access to (me->editflag & ME_EDIT_PAINT_FACE_SEL) */
  bool do_face_sel;
  bool is_ortho;
  /** the object is negative scaled. */
  bool is_flip_object;
  /** use masking during painting. Some operations such as airbrush may disable. */
  bool do_masking;
  /** only to avoid running. */
  bool is_texbrush;
  /** mask brush is applied before masking. */
  bool is_maskbrush;
#ifndef PROJ_DEBUG_NOSEAMBLEED
  float seam_bleed_px;
  float seam_bleed_px_sq;
#endif
  /* clone vars */
  float cloneOffset[2];

  /** Projection matrix, use for getting screen coords. */
  float projectMat[4][4];
  /** inverse of projectMat. */
  float projectMatInv[4][4];
  /** View vector, use for do_backfacecull and for ray casting with an ortho viewport. */
  float viewDir[3];
  /** View location in object relative 3D space, so can compare to verts. */
  float viewPos[3];
  float clip_start, clip_end;

  /* reproject vars */
  Image *reproject_image;
  ImBuf *reproject_ibuf;
  bool reproject_ibuf_free_float;
  bool reproject_ibuf_free_uchar;

  /* threads */
  int thread_tot;
  int bucketMin[2];
  int bucketMax[2];
  /** must lock threads while accessing these. */
  int context_bucket_index;

  struct CurveMapping *cavity_curve;
  BlurKernel *blurkernel;

  /* -------------------------------------------------------------------- */
  /* Vars shared between multiple views (keep last) */
  /**
   * This data is owned by `ProjStrokeHandle.ps_views[0]`,
   * all other views re-use the data.
   */

#define PROJ_PAINT_STATE_SHARED_MEMCPY(ps_dst, ps_src) \
  MEMCPY_STRUCT_AFTER(ps_dst, ps_src, is_shared_user)

#define PROJ_PAINT_STATE_SHARED_CLEAR(ps) MEMSET_STRUCT_AFTER(ps, 0, is_shared_user)

  bool is_shared_user;

  ProjPaintImage *projImages;
  /** cavity amount for vertices. */
  float *cavities;

#ifndef PROJ_DEBUG_NOSEAMBLEED
  /** Store info about faces, if they are initialized etc. */
  ushort *faceSeamFlags;
  /** save the winding of the face in uv space,
   * helps as an extra validation step for seam detection. */
  char *faceWindingFlags;
  /** expanded UVs for faces to use as seams. */
  LoopSeamData *loopSeamData;
  /** Only needed for when seam_bleed_px is enabled, use to find UV seams. */
  LinkNode **vertFaces;
  /** Seams per vert, to find adjacent seams. */
  ListBase *vertSeams;
#endif

  SpinLock *tile_lock;

  Mesh *me_eval;
  int totlooptri_eval;
  int totloop_eval;
  int totpoly_eval;
  int totedge_eval;
  int totvert_eval;

  const MVert *mvert_eval;
  const float (*vert_normals)[3];
  const MEdge *medge_eval;
  const MPoly *mpoly_eval;
  const MLoop *mloop_eval;
  const MLoopTri *mlooptri_eval;

  const MLoopUV *mloopuv_stencil_eval;

  /**
   * \note These UV layers are aligned to \a mpoly_eval
   * but each pointer references the start of the layer,
   * so a loop indirection is needed as well.
   */
  const MLoopUV **poly_to_loop_uv;
  /** other UV map, use for cloning between layers. */
  const MLoopUV **poly_to_loop_uv_clone;

  /* Actual material for each index, either from object or Mesh datablock... */
  Material **mat_array;

  bool use_colormanagement;
} ProjPaintState;

typedef union pixelPointer {
  /** float buffer. */
  float *f_pt;
  /** 2 ways to access a char buffer. */
  uint *uint_pt;
  uchar *ch_pt;
} PixelPointer;

typedef union pixelStore {
  uchar ch[4];
  uint uint;
  float f[4];
} PixelStore;

typedef struct ProjPixel {
  /** the floating point screen projection of this pixel. */
  float projCoSS[2];
  float worldCoSS[3];

  short x_px, y_px;

  /** if anyone wants to paint onto more than 65535 images they can bite me. */
  ushort image_index;
  uchar bb_cell_index;

  /* for various reasons we may want to mask out painting onto this pixel */
  ushort mask;

  /* Only used when the airbrush is disabled.
   * Store the max mask value to avoid painting over an area with a lower opacity
   * with an advantage that we can avoid touching the pixel at all, if the
   * new mask value is lower than mask_accum */
  ushort *mask_accum;

  /* horrible hack, store tile valid flag pointer here to re-validate tiles
   * used for anchored and drag-dot strokes */
  bool *valid;

  PixelPointer origColor;
  PixelStore newColor;
  PixelPointer pixel;
} ProjPixel;

typedef struct ProjPixelClone {
  struct ProjPixel __pp;
  PixelStore clonepx;
} ProjPixelClone;

/* undo tile pushing */
typedef struct {
  SpinLock *lock;
  bool masked;
  ushort tile_width;
  ImBuf **tmpibuf;
  ProjPaintImage *pjima;
} TileInfo;
