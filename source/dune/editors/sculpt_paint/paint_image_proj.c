/** Functions to paint images in 2D and 3D. */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#ifdef WIN32
#  include "lib_winstuff.h"
#endif

#include "lib_blenlib.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_math_bits.h"
#include "lib_math_color_blend.h"
#include "lib_memarena.h"
#include "lib_task.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "atomic_ops.h"

#include "I18N_translation.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "types_brush.h"
#include "types_material_types.h"
#include "types_mesh_types.h"
#include "types_meshdata_types.h"
#include "types_node_types.h"
#include "types_object_types.h"

#include "dune_brush.h"
#include "dune_camera.h"
#include "dune_colorband.h"
#include "dune_colortools.h"
#include "dune_context.h"
#include "dune_customdata.h"
#include "dune_global.h"
#include "dune_idprop.h"
#include "dune_image.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_material.h"
#include "dune_mesh.h"
#include "dune_mesh_mapping.h"
#include "dune_mesh_runtime.h"
#include "dune_node.h"
#include "dune_paint.h"
#include "dune_report.h"
#include "dune_scene.h"
#include "dune_screen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ed_node.h"
#include "ed_object.h"
#include "ed_paint.h"
#include "ed_screen.h"
#include "ed_uvedit.h"
#include "ed_view3d.h"
#include "ed_view3d_offscreen.h"

#include "gpu_capabilities.h"
#include "gpu_init_exit.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "IMB_colormanagement.h"

//#include "bmesh_tools.h"

#include "paint_intern.h"

static void partial_redraw_array_init(ImagePaintPartialRedraw *pr);

/* Defines and Structs */
/* unit_float_to_uchar_clamp as inline function */
LIB_INLINE uchar f_to_char(const float val)
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
  MemArena *arena_mt[DUNE_MAX_THREADS];
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
   * note These UV layers are aligned to \a mpoly_eval
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

typedef struct VertSeam {
  struct VertSeam *next, *prev;
  int tri;
  uint loop;
  float angle;
  bool normal_cw;
  float uv[2];
} VertSeam;

/* -------------------------------------------------------------------- */
/** \name MLoopTri accessor functions.
 * \{ */

BLI_INLINE const MPoly *ps_tri_index_to_mpoly(const ProjPaintState *ps, int tri_index)
{
  return &ps->mpoly_eval[ps->mlooptri_eval[tri_index].poly];
}

#define PS_LOOPTRI_AS_VERT_INDEX_3(ps, lt) \
  ps->mloop_eval[lt->tri[0]].v, ps->mloop_eval[lt->tri[1]].v, ps->mloop_eval[lt->tri[2]].v,

#define PS_LOOPTRI_AS_UV_3(uvlayer, lt) \
  uvlayer[lt->poly][lt->tri[0]].uv, uvlayer[lt->poly][lt->tri[1]].uv, \
      uvlayer[lt->poly][lt->tri[2]].uv,

#define PS_LOOPTRI_ASSIGN_UV_3(uv_tri, uvlayer, lt) \
  { \
    (uv_tri)[0] = uvlayer[lt->poly][lt->tri[0]].uv; \
    (uv_tri)[1] = uvlayer[lt->poly][lt->tri[1]].uv; \
    (uv_tri)[2] = uvlayer[lt->poly][lt->tri[2]].uv; \
  } \
  ((void)0)

/** \} */

/* Finish projection painting structs */

static int project_paint_face_paint_tile(Image *ima, const float *uv)
{
  if (ima == NULL || ima->source != IMA_SRC_TILED) {
    return 0;
  }

  /* Currently, faces are assumed to belong to one tile, so checking the first loop is enough. */
  int tx = (int)uv[0];
  int ty = (int)uv[1];
  return 1001 + 10 * ty + tx;
}

static TexPaintSlot *project_paint_face_paint_slot(const ProjPaintState *ps, int tri_index)
{
  const MPoly *mp = ps_tri_index_to_mpoly(ps, tri_index);
  Material *ma = ps->mat_array[mp->mat_nr];
  return ma ? ma->texpaintslot + ma->paint_active_slot : NULL;
}

static Image *project_paint_face_paint_image(const ProjPaintState *ps, int tri_index)
{
  if (ps->do_stencil_brush) {
    return ps->stencil_ima;
  }

  const MPoly *mp = ps_tri_index_to_mpoly(ps, tri_index);
  Material *ma = ps->mat_array[mp->mat_nr];
  TexPaintSlot *slot = ma ? ma->texpaintslot + ma->paint_active_slot : NULL;
  return slot ? slot->ima : ps->canvas_ima;
}

static TexPaintSlot *project_paint_face_clone_slot(const ProjPaintState *ps, int tri_index)
{
  const MPoly *mp = ps_tri_index_to_mpoly(ps, tri_index);
  Material *ma = ps->mat_array[mp->mat_nr];
  return ma ? ma->texpaintslot + ma->paint_clone_slot : NULL;
}

static Image *project_paint_face_clone_image(const ProjPaintState *ps, int tri_index)
{
  const MPoly *mp = ps_tri_index_to_mpoly(ps, tri_index);
  Material *ma = ps->mat_array[mp->mat_nr];
  TexPaintSlot *slot = ma ? ma->texpaintslot + ma->paint_clone_slot : NULL;
  return slot ? slot->ima : ps->clone_ima;
}

/**
 * Fast projection bucket array lookup, use the safe version for bound checking.
 */
static int project_bucket_offset(const ProjPaintState *ps, const float projCoSS[2])
{
  /* If we were not dealing with screen-space 2D coords we could simple do...
   * ps->bucketRect[x + (y*ps->buckets_y)] */

  /* please explain?
   * projCoSS[0] - ps->screenMin[0]   : zero origin
   * ... / ps->screen_width           : range from 0.0 to 1.0
   * ... * ps->buckets_x              : use as a bucket index
   *
   * Second multiplication does similar but for vertical offset
   */
  return ((int)(((projCoSS[0] - ps->screenMin[0]) / ps->screen_width) * ps->buckets_x)) +
         (((int)(((projCoSS[1] - ps->screenMin[1]) / ps->screen_height) * ps->buckets_y)) *
          ps->buckets_x);
}

static int project_bucket_offset_safe(const ProjPaintState *ps, const float projCoSS[2])
{
  int bucket_index = project_bucket_offset(ps, projCoSS);

  if (bucket_index < 0 || bucket_index >= ps->buckets_x * ps->buckets_y) {
    return -1;
  }
  return bucket_index;
}

static float VecZDepthOrtho(
    const float pt[2], const float v1[3], const float v2[3], const float v3[3], float w[3])
{
  barycentric_weights_v2(v1, v2, v3, pt, w);
  return (v1[2] * w[0]) + (v2[2] * w[1]) + (v3[2] * w[2]);
}

static float VecZDepthPersp(
    const float pt[2], const float v1[4], const float v2[4], const float v3[4], float w[3])
{
  float wtot_inv, wtot;
  float w_tmp[3];

  barycentric_weights_v2_persp(v1, v2, v3, pt, w);
  /* for the depth we need the weights to match what
   * barycentric_weights_v2 would return, in this case its easiest just to
   * undo the 4th axis division and make it unit-sum
   *
   * don't call barycentric_weights_v2() because our callers expect 'w'
   * to be weighted from the perspective */
  w_tmp[0] = w[0] * v1[3];
  w_tmp[1] = w[1] * v2[3];
  w_tmp[2] = w[2] * v3[3];

  wtot = w_tmp[0] + w_tmp[1] + w_tmp[2];

  if (wtot != 0.0f) {
    wtot_inv = 1.0f / wtot;

    w_tmp[0] = w_tmp[0] * wtot_inv;
    w_tmp[1] = w_tmp[1] * wtot_inv;
    w_tmp[2] = w_tmp[2] * wtot_inv;
  }
  else { /* dummy values for zero area face */
    w_tmp[0] = w_tmp[1] = w_tmp[2] = 1.0f / 3.0f;
  }
  /* done mimicking barycentric_weights_v2() */

  return (v1[2] * w_tmp[0]) + (v2[2] * w_tmp[1]) + (v3[2] * w_tmp[2]);
}

/* Return the top-most face index that the screen space coord 'pt' touches (or -1) */
static int project_paint_PickFace(const ProjPaintState *ps, const float pt[2], float w[3])
{
  LinkNode *node;
  float w_tmp[3];
  int bucket_index;
  int best_tri_index = -1;
  float z_depth_best = FLT_MAX, z_depth;

  bucket_index = project_bucket_offset_safe(ps, pt);
  if (bucket_index == -1) {
    return -1;
  }

  /* we could return 0 for 1 face buckets, as long as this function assumes
   * that the point its testing is only every originated from an existing face */

  for (node = ps->bucketFaces[bucket_index]; node; node = node->next) {
    const int tri_index = POINTER_AS_INT(node->link);
    const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
    const float *vtri_ss[3] = {
        ps->screenCoords[ps->mloop_eval[lt->tri[0]].v],
        ps->screenCoords[ps->mloop_eval[lt->tri[1]].v],
        ps->screenCoords[ps->mloop_eval[lt->tri[2]].v],
    };

    if (isect_point_tri_v2(pt, UNPACK3(vtri_ss))) {
      if (ps->is_ortho) {
        z_depth = VecZDepthOrtho(pt, UNPACK3(vtri_ss), w_tmp);
      }
      else {
        z_depth = VecZDepthPersp(pt, UNPACK3(vtri_ss), w_tmp);
      }

      if (z_depth < z_depth_best) {
        best_tri_index = tri_index;
        z_depth_best = z_depth;
        copy_v3_v3(w, w_tmp);
      }
    }
  }

  /** will be -1 or a valid face. */
  return best_tri_index;
}

/* Converts a uv coord into a pixel location wrapping if the uv is outside 0-1 range */
static void uvco_to_wrapped_pxco(const float uv[2], int ibuf_x, int ibuf_y, float *x, float *y)
{
  /* use */
  *x = fmodf(uv[0], 1.0f);
  *y = fmodf(uv[1], 1.0f);

  if (*x < 0.0f) {
    *x += 1.0f;
  }
  if (*y < 0.0f) {
    *y += 1.0f;
  }

  *x = *x * ibuf_x - 0.5f;
  *y = *y * ibuf_y - 0.5f;
}

/* Set the top-most face color that the screen space coord 'pt' touches
 * (or return 0 if none touch) */
static bool project_paint_PickColor(
    const ProjPaintState *ps, const float pt[2], float *rgba_fp, uchar *rgba, const bool interp)
{
  const MLoopTri *lt;
  const float *lt_tri_uv[3];
  float w[3], uv[2];
  int tri_index;
  Image *ima;
  ImBuf *ibuf;
  int xi, yi;

  tri_index = project_paint_PickFace(ps, pt, w);

  if (tri_index == -1) {
    return false;
  }

  lt = &ps->mlooptri_eval[tri_index];
  PS_LOOPTRI_ASSIGN_UV_3(lt_tri_uv, ps->poly_to_loop_uv, lt);

  interp_v2_v2v2v2(uv, UNPACK3(lt_tri_uv), w);

  ima = project_paint_face_paint_image(ps, tri_index);
  /** we must have got the imbuf before getting here. */
  int tile_number = project_paint_face_paint_tile(ima, lt_tri_uv[0]);
  /* XXX get appropriate ImageUser instead */
  ImageUser iuser;
  BKE_imageuser_default(&iuser);
  iuser.tile = tile_number;
  iuser.framenr = ima->lastframe;
  ibuf = BKE_image_acquire_ibuf(ima, &iuser, NULL);
  if (ibuf == NULL) {
    return false;
  }

  if (interp) {
    float x, y;
    uvco_to_wrapped_pxco(uv, ibuf->x, ibuf->y, &x, &y);

    if (ibuf->rect_float) {
      if (rgba_fp) {
        bilinear_interpolation_color_wrap(ibuf, NULL, rgba_fp, x, y);
      }
      else {
        float rgba_tmp_f[4];
        bilinear_interpolation_color_wrap(ibuf, NULL, rgba_tmp_f, x, y);
        premul_float_to_straight_uchar(rgba, rgba_tmp_f);
      }
    }
    else {
      if (rgba) {
        bilinear_interpolation_color_wrap(ibuf, rgba, NULL, x, y);
      }
      else {
        uchar rgba_tmp[4];
        bilinear_interpolation_color_wrap(ibuf, rgba_tmp, NULL, x, y);
        straight_uchar_to_premul_float(rgba_fp, rgba_tmp);
      }
    }
  }
  else {
    // xi = (int)((uv[0]*ibuf->x) + 0.5f);
    // yi = (int)((uv[1]*ibuf->y) + 0.5f);
    // if (xi < 0 || xi >= ibuf->x  ||  yi < 0 || yi >= ibuf->y) return false;

    /* wrap */
    xi = mod_i((int)(uv[0] * ibuf->x), ibuf->x);
    yi = mod_i((int)(uv[1] * ibuf->y), ibuf->y);

    if (rgba) {
      if (ibuf->rect_float) {
        const float *rgba_tmp_fp = ibuf->rect_float + (xi + yi * ibuf->x * 4);
        premul_float_to_straight_uchar(rgba, rgba_tmp_fp);
      }
      else {
        *((uint *)rgba) = *(uint *)(((char *)ibuf->rect) + ((xi + yi * ibuf->x) * 4));
      }
    }

    if (rgba_fp) {
      if (ibuf->rect_float) {
        copy_v4_v4(rgba_fp, (ibuf->rect_float + ((xi + yi * ibuf->x) * 4)));
      }
      else {
        uchar *tmp_ch = ((uchar *)ibuf->rect) + ((xi + yi * ibuf->x) * 4);
        straight_uchar_to_premul_float(rgba_fp, tmp_ch);
      }
    }
  }
  BKE_image_release_ibuf(ima, ibuf, NULL);
  return true;
}

/**
 * Check if 'pt' is in front of the 3 verts on the Z axis (used for screen-space occlusion test)
 * \return
 * -  `0`:   no occlusion
 * - `-1`: no occlusion but 2D intersection is true
 * -  `1`: occluded
 * -  `2`: occluded with `w[3]` weights set (need to know in some cases)
 */
static int project_paint_occlude_ptv(const float pt[3],
                                     const float v1[4],
                                     const float v2[4],
                                     const float v3[4],
                                     float w[3],
                                     const bool is_ortho)
{
  /* if all are behind us, return false */
  if (v1[2] > pt[2] && v2[2] > pt[2] && v3[2] > pt[2]) {
    return 0;
  }

  /* do a 2D point in try intersection */
  if (!isect_point_tri_v2(pt, v1, v2, v3)) {
    return 0;
  }

  /* From here on we know there IS an intersection */
  /* if ALL of the verts are in front of us then we know it intersects ? */
  if (v1[2] < pt[2] && v2[2] < pt[2] && v3[2] < pt[2]) {
    return 1;
  }

  /* we intersect? - find the exact depth at the point of intersection */
  /* Is this point is occluded by another face? */
  if (is_ortho) {
    if (VecZDepthOrtho(pt, v1, v2, v3, w) < pt[2]) {
      return 2;
    }
  }
  else {
    if (VecZDepthPersp(pt, v1, v2, v3, w) < pt[2]) {
      return 2;
    }
  }
  return -1;
}

static int project_paint_occlude_ptv_clip(const float pt[3],
                                          const float v1[4],
                                          const float v2[4],
                                          const float v3[4],
                                          const float v1_3d[3],
                                          const float v2_3d[3],
                                          const float v3_3d[3],
                                          float w[3],
                                          const bool is_ortho,
                                          RegionView3D *rv3d)
{
  float wco[3];
  int ret = project_paint_occlude_ptv(pt, v1, v2, v3, w, is_ortho);

  if (ret <= 0) {
    return ret;
  }

  if (ret == 1) { /* weights not calculated */
    if (is_ortho) {
      barycentric_weights_v2(v1, v2, v3, pt, w);
    }
    else {
      barycentric_weights_v2_persp(v1, v2, v3, pt, w);
    }
  }

  /* Test if we're in the clipped area, */
  interp_v3_v3v3v3(wco, v1_3d, v2_3d, v3_3d, w);

  if (!ED_view3d_clipping_test(rv3d, wco, true)) {
    return 1;
  }

  return -1;
}
