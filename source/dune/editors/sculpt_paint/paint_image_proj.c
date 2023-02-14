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

/* Check if a screen-space location is occluded by any other faces
 * check, pixelScreenCo must be in screen-space, its Z-Depth only needs to be used for comparison
 * and doesn't need to be correct in relation to X and Y coords
 * (this is the case in perspective view) */
static bool project_bucket_point_occluded(const ProjPaintState *ps,
                                          LinkNode *bucketFace,
                                          const int orig_face,
                                          const float pixelScreenCo[4])
{
  int isect_ret;
  const bool do_clip = RV3D_CLIPPING_ENABLED(ps->v3d, ps->rv3d);

  /* we could return false for 1 face buckets, as long as this function assumes
   * that the point its testing is only every originated from an existing face */

  for (; bucketFace; bucketFace = bucketFace->next) {
    const int tri_index = POINTER_AS_INT(bucketFace->link);

    if (orig_face != tri_index) {
      const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
      const float *vtri_ss[3] = {
          ps->screenCoords[ps->mloop_eval[lt->tri[0]].v],
          ps->screenCoords[ps->mloop_eval[lt->tri[1]].v],
          ps->screenCoords[ps->mloop_eval[lt->tri[2]].v],
      };
      float w[3];

      if (do_clip) {
        const float *vtri_co[3] = {
            ps->mvert_eval[ps->mloop_eval[lt->tri[0]].v].co,
            ps->mvert_eval[ps->mloop_eval[lt->tri[1]].v].co,
            ps->mvert_eval[ps->mloop_eval[lt->tri[2]].v].co,
        };
        isect_ret = project_paint_occlude_ptv_clip(
            pixelScreenCo, UNPACK3(vtri_ss), UNPACK3(vtri_co), w, ps->is_ortho, ps->rv3d);
      }
      else {
        isect_ret = project_paint_occlude_ptv(pixelScreenCo, UNPACK3(vtri_ss), w, ps->is_ortho);
      }

      if (isect_ret >= 1) {
        /* TODO: we may want to cache the first hit,
         * it is not possible to swap the face order in the list anymore */
        return true;
      }
    }
  }
  return false;
}

/* Basic line intersection, could move to math_geom.c, 2 points with a horizontal line
 * 1 for an intersection, 2 if the first point is aligned, 3 if the second point is aligned. */
#define ISECT_TRUE 1
#define ISECT_TRUE_P1 2
#define ISECT_TRUE_P2 3
static int line_isect_y(const float p1[2], const float p2[2], const float y_level, float *x_isect)
{
  float y_diff;

  /* are we touching the first point? - no interpolation needed */
  if (y_level == p1[1]) {
    *x_isect = p1[0];
    return ISECT_TRUE_P1;
  }
  /* are we touching the second point? - no interpolation needed */
  if (y_level == p2[1]) {
    *x_isect = p2[0];
    return ISECT_TRUE_P2;
  }

  /** yuck, horizontal line, we can't do much here. */
  y_diff = fabsf(p1[1] - p2[1]);

  if (y_diff < 0.000001f) {
    *x_isect = (p1[0] + p2[0]) * 0.5f;
    return ISECT_TRUE;
  }

  if (p1[1] > y_level && p2[1] < y_level) {
    /* (p1[1] - p2[1]); */
    *x_isect = (p2[0] * (p1[1] - y_level) + p1[0] * (y_level - p2[1])) / y_diff;
    return ISECT_TRUE;
  }
  if (p1[1] < y_level && p2[1] > y_level) {
    /* (p2[1] - p1[1]); */
    *x_isect = (p2[0] * (y_level - p1[1]) + p1[0] * (p2[1] - y_level)) / y_diff;
    return ISECT_TRUE;
  }
  return 0;
}

static int line_isect_x(const float p1[2], const float p2[2], const float x_level, float *y_isect)
{
  float x_diff;

  if (x_level == p1[0]) { /* are we touching the first point? - no interpolation needed */
    *y_isect = p1[1];
    return ISECT_TRUE_P1;
  }
  if (x_level == p2[0]) { /* are we touching the second point? - no interpolation needed */
    *y_isect = p2[1];
    return ISECT_TRUE_P2;
  }

  /* yuck, horizontal line, we can't do much here */
  x_diff = fabsf(p1[0] - p2[0]);

  /* yuck, vertical line, we can't do much here */
  if (x_diff < 0.000001f) {
    *y_isect = (p1[0] + p2[0]) * 0.5f;
    return ISECT_TRUE;
  }

  if (p1[0] > x_level && p2[0] < x_level) {
    /* (p1[0] - p2[0]); */
    *y_isect = (p2[1] * (p1[0] - x_level) + p1[1] * (x_level - p2[0])) / x_diff;
    return ISECT_TRUE;
  }
  if (p1[0] < x_level && p2[0] > x_level) {
    /* (p2[0] - p1[0]); */
    *y_isect = (p2[1] * (x_level - p1[0]) + p1[1] * (p2[0] - x_level)) / x_diff;
    return ISECT_TRUE;
  }
  return 0;
}

/* simple func use for comparing UV locations to check if there are seams.
 * Its possible this gives incorrect results, when the UVs for 1 face go into the next
 * tile, but do not do this for the adjacent face, it could return a false positive.
 * This is so unlikely that Id not worry about it. */
#ifndef PROJ_DEBUG_NOSEAMBLEED
static bool cmp_uv(const float vec2a[2], const float vec2b[2])
{
  /* if the UV's are not between 0.0 and 1.0 */
  float xa = fmodf(vec2a[0], 1.0f);
  float ya = fmodf(vec2a[1], 1.0f);

  float xb = fmodf(vec2b[0], 1.0f);
  float yb = fmodf(vec2b[1], 1.0f);

  if (xa < 0.0f) {
    xa += 1.0f;
  }
  if (ya < 0.0f) {
    ya += 1.0f;
  }

  if (xb < 0.0f) {
    xb += 1.0f;
  }
  if (yb < 0.0f) {
    yb += 1.0f;
  }

  return ((fabsf(xa - xb) < PROJ_GEOM_TOLERANCE) && (fabsf(ya - yb) < PROJ_GEOM_TOLERANCE)) ?
             true :
             false;
}
#endif

/* set min_px and max_px to the image space bounds of the UV coords
 * return zero if there is no area in the returned rectangle */
#ifndef PROJ_DEBUG_NOSEAMBLEED
static bool pixel_bounds_uv(const float uv_quad[4][2],
                            rcti *bounds_px,
                            const int ibuf_x,
                            const int ibuf_y)
{
  /* UV bounds */
  float min_uv[2], max_uv[2];

  INIT_MINMAX2(min_uv, max_uv);

  minmax_v2v2_v2(min_uv, max_uv, uv_quad[0]);
  minmax_v2v2_v2(min_uv, max_uv, uv_quad[1]);
  minmax_v2v2_v2(min_uv, max_uv, uv_quad[2]);
  minmax_v2v2_v2(min_uv, max_uv, uv_quad[3]);

  bounds_px->xmin = (int)(ibuf_x * min_uv[0]);
  bounds_px->ymin = (int)(ibuf_y * min_uv[1]);

  bounds_px->xmax = (int)(ibuf_x * max_uv[0]) + 1;
  bounds_px->ymax = (int)(ibuf_y * max_uv[1]) + 1;

  // printf("%d %d %d %d\n", min_px[0], min_px[1], max_px[0], max_px[1]);

  /* face uses no UV area when quantized to pixels? */
  return (bounds_px->xmin == bounds_px->xmax || bounds_px->ymin == bounds_px->ymax) ? false : true;
}
#endif

static bool pixel_bounds_array(
    float (*uv)[2], rcti *bounds_px, const int ibuf_x, const int ibuf_y, int tot)
{
  /* UV bounds */
  float min_uv[2], max_uv[2];

  if (tot == 0) {
    return false;
  }

  INIT_MINMAX2(min_uv, max_uv);

  while (tot--) {
    minmax_v2v2_v2(min_uv, max_uv, (*uv));
    uv++;
  }

  bounds_px->xmin = (int)(ibuf_x * min_uv[0]);
  bounds_px->ymin = (int)(ibuf_y * min_uv[1]);

  bounds_px->xmax = (int)(ibuf_x * max_uv[0]) + 1;
  bounds_px->ymax = (int)(ibuf_y * max_uv[1]) + 1;

  // printf("%d %d %d %d\n", min_px[0], min_px[1], max_px[0], max_px[1]);

  /* face uses no UV area when quantized to pixels? */
  return (bounds_px->xmin == bounds_px->xmax || bounds_px->ymin == bounds_px->ymax) ? false : true;
}

#ifndef PROJ_DEBUG_NOSEAMBLEED

static void project_face_winding_init(const ProjPaintState *ps, const int tri_index)
{
  /* detect the winding of faces in uv space */
  const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
  const float *lt_tri_uv[3] = {PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv, lt)};
  float winding = cross_tri_v2(lt_tri_uv[0], lt_tri_uv[1], lt_tri_uv[2]);

  if (winding > 0) {
    ps->faceWindingFlags[tri_index] |= PROJ_FACE_WINDING_CW;
  }

  ps->faceWindingFlags[tri_index] |= PROJ_FACE_WINDING_INIT;
}

/* This function returns 1 if this face has a seam along the 2 face-vert indices
 * 'orig_i1_fidx' and 'orig_i2_fidx' */
static bool check_seam(const ProjPaintState *ps,
                       const int orig_face,
                       const int orig_i1_fidx,
                       const int orig_i2_fidx,
                       int *other_face,
                       int *orig_fidx)
{
  const MLoopTri *orig_lt = &ps->mlooptri_eval[orig_face];
  const float *orig_lt_tri_uv[3] = {PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv, orig_lt)};
  /* vert indices from face vert order indices */
  const uint i1 = ps->mloop_eval[orig_lt->tri[orig_i1_fidx]].v;
  const uint i2 = ps->mloop_eval[orig_lt->tri[orig_i2_fidx]].v;
  LinkNode *node;
  /* index in face */
  int i1_fidx = -1, i2_fidx = -1;

  for (node = ps->vertFaces[i1]; node; node = node->next) {
    const int tri_index = POINTER_AS_INT(node->link);

    if (tri_index != orig_face) {
      const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
      const int lt_vtri[3] = {PS_LOOPTRI_AS_VERT_INDEX_3(ps, lt)};
      /* could check if the 2 faces images match here,
       * but then there wouldn't be a way to return the opposite face's info */

      /* We need to know the order of the verts in the adjacent face
       * set the i1_fidx and i2_fidx to (0,1,2,3) */
      i1_fidx = BKE_MESH_TESSTRI_VINDEX_ORDER(lt_vtri, i1);
      i2_fidx = BKE_MESH_TESSTRI_VINDEX_ORDER(lt_vtri, i2);

      /* Only need to check if 'i2_fidx' is valid because
       * we know i1_fidx is the same vert on both faces. */
      if (i2_fidx != -1) {
        const float *lt_tri_uv[3] = {PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv, lt)};
        Image *tpage = project_paint_face_paint_image(ps, tri_index);
        Image *orig_tpage = project_paint_face_paint_image(ps, orig_face);
        int tile = project_paint_face_paint_tile(tpage, lt_tri_uv[0]);
        int orig_tile = project_paint_face_paint_tile(orig_tpage, orig_lt_tri_uv[0]);

        BLI_assert(i1_fidx != -1);

        /* This IS an adjacent face!, now lets check if the UVs are ok */

        /* set up the other face */
        *other_face = tri_index;

        /* we check if difference is 1 here, else we might have a case of edge 2-0 for a tri */
        *orig_fidx = (i1_fidx < i2_fidx && (i2_fidx - i1_fidx == 1)) ? i1_fidx : i2_fidx;

        /* initialize face winding if needed */
        if ((ps->faceWindingFlags[tri_index] & PROJ_FACE_WINDING_INIT) == 0) {
          project_face_winding_init(ps, tri_index);
        }

        /* first test if they have the same image */
        if ((orig_tpage == tpage) && (orig_tile == tile) &&
            cmp_uv(orig_lt_tri_uv[orig_i1_fidx], lt_tri_uv[i1_fidx]) &&
            cmp_uv(orig_lt_tri_uv[orig_i2_fidx], lt_tri_uv[i2_fidx])) {
          /* if faces don't have the same winding in uv space,
           * they are on the same side so edge is boundary */
          if ((ps->faceWindingFlags[tri_index] & PROJ_FACE_WINDING_CW) !=
              (ps->faceWindingFlags[orig_face] & PROJ_FACE_WINDING_CW)) {
            return true;
          }

          // printf("SEAM (NONE)\n");
          return false;
        }
        // printf("SEAM (UV GAP)\n");
        return true;
      }
    }
  }
  // printf("SEAM (NO FACE)\n");
  *other_face = -1;
  return true;
}

static VertSeam *find_adjacent_seam(const ProjPaintState *ps,
                                    uint loop_index,
                                    uint vert_index,
                                    VertSeam **r_seam)
{
  ListBase *vert_seams = &ps->vertSeams[vert_index];
  VertSeam *seam = vert_seams->first;
  VertSeam *adjacent = NULL;

  while (seam->loop != loop_index) {
    seam = seam->next;
  }

  if (r_seam) {
    *r_seam = seam;
  }

  /* Circulate through the (sorted) vert seam array, in the direction of the seam normal,
   * until we find the first opposing seam, matching in UV space. */
  if (seam->normal_cw) {
    LISTBASE_CIRCULAR_BACKWARD_BEGIN (vert_seams, adjacent, seam) {
      if ((adjacent->normal_cw != seam->normal_cw) && cmp_uv(adjacent->uv, seam->uv)) {
        break;
      }
    }
    LISTBASE_CIRCULAR_BACKWARD_END(vert_seams, adjacent, seam);
  }
  else {
    LISTBASE_CIRCULAR_FORWARD_BEGIN (vert_seams, adjacent, seam) {
      if ((adjacent->normal_cw != seam->normal_cw) && cmp_uv(adjacent->uv, seam->uv)) {
        break;
      }
    }
    LISTBASE_CIRCULAR_FORWARD_END(vert_seams, adjacent, seam);
  }

  BLI_assert(adjacent);

  return adjacent;
}

/* Computes the normal of two seams at their intersection,
 * and returns the angle between the seam and its normal. */
static float compute_seam_normal(VertSeam *seam, VertSeam *adj, float r_no[2])
{
  const float PI_2 = M_PI * 2.0f;
  float angle[2];
  float angle_rel, angle_no;

  if (seam->normal_cw) {
    angle[0] = adj->angle;
    angle[1] = seam->angle;
  }
  else {
    angle[0] = seam->angle;
    angle[1] = adj->angle;
  }

  angle_rel = angle[1] - angle[0];

  if (angle_rel < 0.0f) {
    angle_rel += PI_2;
  }

  angle_rel *= 0.5f;

  angle_no = angle_rel + angle[0];

  if (angle_no > M_PI) {
    angle_no -= PI_2;
  }

  r_no[0] = cosf(angle_no);
  r_no[1] = sinf(angle_no);

  return angle_rel;
}
/* Calculate outset UV's, this is not the same as simply scaling the UVs,
 * since the outset coords are a margin that keep an even distance from the original UV's,
 * note that the image aspect is taken into account */
static void uv_image_outset(const ProjPaintState *ps,
                            float (*orig_uv)[2],
                            float (*puv)[2],
                            uint tri_index,
                            const int ibuf_x,
                            const int ibuf_y)
{
  int fidx[2];
  uint loop_index;
  uint vert[2];
  const MLoopTri *ltri = &ps->mlooptri_eval[tri_index];

  float ibuf_inv[2];

  ibuf_inv[0] = 1.0f / (float)ibuf_x;
  ibuf_inv[1] = 1.0f / (float)ibuf_y;

  for (fidx[0] = 0; fidx[0] < 3; fidx[0]++) {
    LoopSeamData *seam_data;
    float(*seam_uvs)[2];
    float ang[2];

    if ((ps->faceSeamFlags[tri_index] & (PROJ_FACE_SEAM0 << fidx[0])) == 0) {
      continue;
    }

    loop_index = ltri->tri[fidx[0]];

    seam_data = &ps->loopSeamData[loop_index];
    seam_uvs = seam_data->seam_uvs;

    if (seam_uvs[0][0] != FLT_MAX) {
      continue;
    }

    fidx[1] = (fidx[0] == 2) ? 0 : fidx[0] + 1;

    vert[0] = ps->mloop_eval[loop_index].v;
    vert[1] = ps->mloop_eval[ltri->tri[fidx[1]]].v;

    for (uint i = 0; i < 2; i++) {
      VertSeam *seam;
      VertSeam *adj = find_adjacent_seam(ps, loop_index, vert[i], &seam);
      float no[2];
      float len_fact;
      float tri_ang;

      ang[i] = compute_seam_normal(seam, adj, no);
      tri_ang = ang[i] - M_PI_2;

      if (tri_ang > 0.0f) {
        const float dist = ps->seam_bleed_px * tanf(tri_ang);
        seam_data->corner_dist_sq[i] = square_f(dist);
      }
      else {
        seam_data->corner_dist_sq[i] = 0.0f;
      }

      len_fact = cosf(tri_ang);
      len_fact = UNLIKELY(len_fact < FLT_EPSILON) ? FLT_MAX : (1.0f / len_fact);

      /* Clamp the length factor, see: T62236. */
      len_fact = MIN2(len_fact, 10.0f);

      mul_v2_fl(no, ps->seam_bleed_px * len_fact);

      add_v2_v2v2(seam_data->seam_puvs[i], puv[fidx[i]], no);

      mul_v2_v2v2(seam_uvs[i], seam_data->seam_puvs[i], ibuf_inv);
    }

    /* Handle convergent normals (can self-intersect). */
    if ((ang[0] + ang[1]) < M_PI) {
      if (isect_seg_seg_v2_simple(orig_uv[fidx[0]], seam_uvs[0], orig_uv[fidx[1]], seam_uvs[1])) {
        float isect_co[2];

        isect_seg_seg_v2_point(
            orig_uv[fidx[0]], seam_uvs[0], orig_uv[fidx[1]], seam_uvs[1], isect_co);

        copy_v2_v2(seam_uvs[0], isect_co);
        copy_v2_v2(seam_uvs[1], isect_co);
      }
    }
  }
}

static void insert_seam_vert_array(const ProjPaintState *ps,
                                   MemArena *arena,
                                   const int tri_index,
                                   const int fidx1,
                                   const int ibuf_x,
                                   const int ibuf_y)
{
  const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
  const float *lt_tri_uv[3] = {PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv, lt)};
  const int fidx[2] = {fidx1, ((fidx1 + 1) % 3)};
  float vec[2];

  VertSeam *vseam = BLI_memarena_alloc(arena, sizeof(VertSeam[2]));

  vseam->prev = NULL;
  vseam->next = NULL;

  vseam->tri = tri_index;
  vseam->loop = lt->tri[fidx[0]];

  sub_v2_v2v2(vec, lt_tri_uv[fidx[1]], lt_tri_uv[fidx[0]]);
  vec[0] *= ibuf_x;
  vec[1] *= ibuf_y;
  vseam->angle = atan2f(vec[1], vec[0]);

  /* If face windings are not initialized, something must be wrong. */
  BLI_assert((ps->faceWindingFlags[tri_index] & PROJ_FACE_WINDING_INIT) != 0);
  vseam->normal_cw = (ps->faceWindingFlags[tri_index] & PROJ_FACE_WINDING_CW);

  copy_v2_v2(vseam->uv, lt_tri_uv[fidx[0]]);

  vseam[1] = vseam[0];
  vseam[1].angle += vseam[1].angle > 0.0f ? -M_PI : M_PI;
  vseam[1].normal_cw = !vseam[1].normal_cw;
  copy_v2_v2(vseam[1].uv, lt_tri_uv[fidx[1]]);

  for (uint i = 0; i < 2; i++) {
    uint vert = ps->mloop_eval[lt->tri[fidx[i]]].v;
    ListBase *list = &ps->vertSeams[vert];
    VertSeam *item = list->first;

    while (item && item->angle < vseam[i].angle) {
      item = item->next;
    }

    BLI_insertlinkbefore(list, item, &vseam[i]);
  }
}

/**
 * Be tricky with flags, first 4 bits are #PROJ_FACE_SEAM0 to 4,
 * last 4 bits are #PROJ_FACE_NOSEAM0 to 4. `1 << i` - where i is `(0..3)`.
 *
 * If we're multi-threading, make sure threads are locked when this is called.
 */
static void project_face_seams_init(const ProjPaintState *ps,
                                    MemArena *arena,
                                    const int tri_index,
                                    const uint vert_index,
                                    bool init_all,
                                    const int ibuf_x,
                                    const int ibuf_y)
{
  /* vars for the other face, we also set its flag */
  int other_face, other_fidx;
  /* next fidx in the face (0,1,2,3) -> (1,2,3,0) or (0,1,2) -> (1,2,0) for a tri */
  int fidx[2] = {2, 0};
  const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
  LinkNode *node;

  /* initialize face winding if needed */
  if ((ps->faceWindingFlags[tri_index] & PROJ_FACE_WINDING_INIT) == 0) {
    project_face_winding_init(ps, tri_index);
  }

  do {
    if (init_all || (ps->mloop_eval[lt->tri[fidx[0]]].v == vert_index) ||
        (ps->mloop_eval[lt->tri[fidx[1]]].v == vert_index)) {
      if ((ps->faceSeamFlags[tri_index] &
           (PROJ_FACE_SEAM0 << fidx[0] | PROJ_FACE_NOSEAM0 << fidx[0])) == 0) {
        if (check_seam(ps, tri_index, fidx[0], fidx[1], &other_face, &other_fidx)) {
          ps->faceSeamFlags[tri_index] |= PROJ_FACE_SEAM0 << fidx[0];
          insert_seam_vert_array(ps, arena, tri_index, fidx[0], ibuf_x, ibuf_y);

          if (other_face != -1) {
            /* Check if the other seam is already set.
             * We don't want to insert it in the list twice. */
            if ((ps->faceSeamFlags[other_face] & (PROJ_FACE_SEAM0 << other_fidx)) == 0) {
              ps->faceSeamFlags[other_face] |= PROJ_FACE_SEAM0 << other_fidx;
              insert_seam_vert_array(ps, arena, other_face, other_fidx, ibuf_x, ibuf_y);
            }
          }
        }
        else {
          ps->faceSeamFlags[tri_index] |= PROJ_FACE_NOSEAM0 << fidx[0];
          ps->faceSeamFlags[tri_index] |= PROJ_FACE_SEAM_INIT0 << fidx[0];

          if (other_face != -1) {
            /* second 4 bits for disabled */
            ps->faceSeamFlags[other_face] |= PROJ_FACE_NOSEAM0 << other_fidx;
            ps->faceSeamFlags[other_face] |= PROJ_FACE_SEAM_INIT0 << other_fidx;
          }
        }
      }
    }

    fidx[1] = fidx[0];
  } while (fidx[0]--);

  if (init_all) {
    char checked_verts = 0;

    fidx[0] = 2;
    fidx[1] = 0;

    do {
      if ((ps->faceSeamFlags[tri_index] & (PROJ_FACE_SEAM_INIT0 << fidx[0])) == 0) {
        for (uint i = 0; i < 2; i++) {
          uint vert;

          if ((checked_verts & (1 << fidx[i])) != 0) {
            continue;
          }

          vert = ps->mloop_eval[lt->tri[fidx[i]]].v;

          for (node = ps->vertFaces[vert]; node; node = node->next) {
            const int tri = POINTER_AS_INT(node->link);

            project_face_seams_init(ps, arena, tri, vert, false, ibuf_x, ibuf_y);
          }

          checked_verts |= 1 << fidx[i];
        }

        ps->faceSeamFlags[tri_index] |= PROJ_FACE_SEAM_INIT0 << fidx[0];
      }

      fidx[1] = fidx[0];
    } while (fidx[0]--);
  }
}
#endif  // PROJ_DEBUG_NOSEAMBLEED

/* Converts a UV location to a 3D screen-space location
 * Takes a 'uv' and 3 UV coords, and sets the values of pixelScreenCo
 *
 * This is used for finding a pixels location in screen-space for painting */
static void screen_px_from_ortho(const float uv[2],
                                 const float v1co[3],
                                 const float v2co[3],
                                 const float v3co[3], /* Screenspace coords */
                                 const float uv1co[2],
                                 const float uv2co[2],
                                 const float uv3co[2],
                                 float pixelScreenCo[4],
                                 float w[3])
{
  barycentric_weights_v2(uv1co, uv2co, uv3co, uv, w);
  interp_v3_v3v3v3(pixelScreenCo, v1co, v2co, v3co, w);
}

/* same as screen_px_from_ortho except we
 * do perspective correction on the pixel coordinate */
static void screen_px_from_persp(const float uv[2],
                                 const float v1co[4],
                                 const float v2co[4],
                                 const float v3co[4], /* screen-space coords */
                                 const float uv1co[2],
                                 const float uv2co[2],
                                 const float uv3co[2],
                                 float pixelScreenCo[4],
                                 float w[3])
{
  float w_int[3];
  float wtot_inv, wtot;
  barycentric_weights_v2(uv1co, uv2co, uv3co, uv, w);

  /* re-weight from the 4th coord of each screen vert */
  w_int[0] = w[0] * v1co[3];
  w_int[1] = w[1] * v2co[3];
  w_int[2] = w[2] * v3co[3];

  wtot = w_int[0] + w_int[1] + w_int[2];

  if (wtot > 0.0f) {
    wtot_inv = 1.0f / wtot;
    w_int[0] *= wtot_inv;
    w_int[1] *= wtot_inv;
    w_int[2] *= wtot_inv;
  }
  else {
    /* Dummy values for zero area face. */
    w[0] = w[1] = w[2] = w_int[0] = w_int[1] = w_int[2] = 1.0f / 3.0f;
  }
  /* done re-weighting */

  /* do interpolation based on projected weight */
  interp_v3_v3v3v3(pixelScreenCo, v1co, v2co, v3co, w_int);
}

/**
 * Set a direction vector based on a screen location.
 * (use for perspective view, else we can simply use `ps->viewDir`)
 *
 * Similar functionality to #ED_view3d_win_to_vector
 *
 * \param r_dir: Resulting direction (length is undefined).
 */
static void screen_px_to_vector_persp(int winx,
                                      int winy,
                                      const float projmat_inv[4][4],
                                      const float view_pos[3],
                                      const float co_px[2],
                                      float r_dir[3])
{
  r_dir[0] = 2.0f * (co_px[0] / winx) - 1.0f;
  r_dir[1] = 2.0f * (co_px[1] / winy) - 1.0f;
  r_dir[2] = -0.5f;
  mul_project_m4_v3((float(*)[4])projmat_inv, r_dir);
  sub_v3_v3(r_dir, view_pos);
}

/**
 * Special function to return the factor to a point along a line in pixel space.
 *
 * This is needed since we can't use #line_point_factor_v2 for perspective screen-space coords.
 *
 * \param p: 2D screen-space location.
 * \param v1, v2: 3D object-space locations.
 */
static float screen_px_line_point_factor_v2_persp(const ProjPaintState *ps,
                                                  const float p[2],
                                                  const float v1[3],
                                                  const float v2[3])
{
  const float zero[3] = {0};
  float v1_proj[3], v2_proj[3];
  float dir[3];

  screen_px_to_vector_persp(ps->winx, ps->winy, ps->projectMatInv, ps->viewPos, p, dir);

  sub_v3_v3v3(v1_proj, v1, ps->viewPos);
  sub_v3_v3v3(v2_proj, v2, ps->viewPos);

  project_plane_v3_v3v3(v1_proj, v1_proj, dir);
  project_plane_v3_v3v3(v2_proj, v2_proj, dir);

  return line_point_factor_v2(zero, v1_proj, v2_proj);
}

static void project_face_pixel(const float *lt_tri_uv[3],
                               ImBuf *ibuf_other,
                               const float w[3],
                               uchar rgba_ub[4],
                               float rgba_f[4])
{
  float uv_other[2], x, y;

  interp_v2_v2v2v2(uv_other, UNPACK3(lt_tri_uv), w);

  /* use */
  uvco_to_wrapped_pxco(uv_other, ibuf_other->x, ibuf_other->y, &x, &y);

  if (ibuf_other->rect_float) { /* from float to float */
    bilinear_interpolation_color_wrap(ibuf_other, NULL, rgba_f, x, y);
  }
  else { /* from char to float */
    bilinear_interpolation_color_wrap(ibuf_other, rgba_ub, NULL, x, y);
  }
}

/* run this outside project_paint_uvpixel_init since pixels with mask 0 don't need init */
static float project_paint_uvpixel_mask(const ProjPaintState *ps,
                                        const int tri_index,
                                        const float w[3])
{
  float mask;

  /* Image Mask */
  if (ps->do_layer_stencil) {
    /* another UV maps image is masking this one's */
    ImBuf *ibuf_other;
    Image *other_tpage = ps->stencil_ima;

    if (other_tpage && (ibuf_other = BKE_image_acquire_ibuf(other_tpage, NULL, NULL))) {
      const MLoopTri *lt_other = &ps->mlooptri_eval[tri_index];
      const float *lt_other_tri_uv[3] = {ps->mloopuv_stencil_eval[lt_other->tri[0]].uv,
                                         ps->mloopuv_stencil_eval[lt_other->tri[1]].uv,
                                         ps->mloopuv_stencil_eval[lt_other->tri[2]].uv};

      /* #BKE_image_acquire_ibuf - TODO: this may be slow. */
      uchar rgba_ub[4];
      float rgba_f[4];

      project_face_pixel(lt_other_tri_uv, ibuf_other, w, rgba_ub, rgba_f);

      if (ibuf_other->rect_float) { /* from float to float */
        mask = ((rgba_f[0] + rgba_f[1] + rgba_f[2]) * (1.0f / 3.0f)) * rgba_f[3];
      }
      else { /* from char to float */
        mask = ((rgba_ub[0] + rgba_ub[1] + rgba_ub[2]) * (1.0f / (255.0f * 3.0f))) *
               (rgba_ub[3] * (1.0f / 255.0f));
      }

      BKE_image_release_ibuf(other_tpage, ibuf_other, NULL);

      if (!ps->do_layer_stencil_inv) {
        /* matching the gimps layer mask black/white rules, white==full opacity */
        mask = (1.0f - mask);
      }

      if (mask == 0.0f) {
        return 0.0f;
      }
    }
    else {
      return 0.0f;
    }
  }
  else {
    mask = 1.0f;
  }

  if (ps->do_mask_cavity) {
    const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
    const int lt_vtri[3] = {PS_LOOPTRI_AS_VERT_INDEX_3(ps, lt)};
    float ca1, ca2, ca3, ca_mask;
    ca1 = ps->cavities[lt_vtri[0]];
    ca2 = ps->cavities[lt_vtri[1]];
    ca3 = ps->cavities[lt_vtri[2]];

    ca_mask = w[0] * ca1 + w[1] * ca2 + w[2] * ca3;
    ca_mask = BKE_curvemapping_evaluateF(ps->cavity_curve, 0, ca_mask);
    CLAMP(ca_mask, 0.0f, 1.0f);
    mask *= ca_mask;
  }

  /* calculate mask */
  if (ps->do_mask_normal) {
    const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
    const int lt_vtri[3] = {PS_LOOPTRI_AS_VERT_INDEX_3(ps, lt)};
    const MPoly *mp = &ps->mpoly_eval[lt->poly];
    float no[3], angle_cos;

    if (mp->flag & ME_SMOOTH) {
      const float *no1, *no2, *no3;
      no1 = ps->vert_normals[lt_vtri[0]];
      no2 = ps->vert_normals[lt_vtri[1]];
      no3 = ps->vert_normals[lt_vtri[2]];

      no[0] = w[0] * no1[0] + w[1] * no2[0] + w[2] * no3[0];
      no[1] = w[0] * no1[1] + w[1] * no2[1] + w[2] * no3[1];
      no[2] = w[0] * no1[2] + w[1] * no2[2] + w[2] * no3[2];
      normalize_v3(no);
    }
    else {
      /* In case the normalizing per pixel isn't optimal,
       * we could cache or access from evaluated mesh. */
      normal_tri_v3(no,
                    ps->mvert_eval[lt_vtri[0]].co,
                    ps->mvert_eval[lt_vtri[1]].co,
                    ps->mvert_eval[lt_vtri[2]].co);
    }

    if (UNLIKELY(ps->is_flip_object)) {
      negate_v3(no);
    }

    /* now we can use the normal as a mask */
    if (ps->is_ortho) {
      angle_cos = dot_v3v3(ps->viewDir, no);
    }
    else {
      /* Annoying but for the perspective view we need to get the pixels location in 3D space :/ */
      float viewDirPersp[3];
      const float *co1, *co2, *co3;
      co1 = ps->mvert_eval[lt_vtri[0]].co;
      co2 = ps->mvert_eval[lt_vtri[1]].co;
      co3 = ps->mvert_eval[lt_vtri[2]].co;

      /* Get the direction from the viewPoint to the pixel and normalize */
      viewDirPersp[0] = (ps->viewPos[0] - (w[0] * co1[0] + w[1] * co2[0] + w[2] * co3[0]));
      viewDirPersp[1] = (ps->viewPos[1] - (w[0] * co1[1] + w[1] * co2[1] + w[2] * co3[1]));
      viewDirPersp[2] = (ps->viewPos[2] - (w[0] * co1[2] + w[1] * co2[2] + w[2] * co3[2]));
      normalize_v3(viewDirPersp);
      if (UNLIKELY(ps->is_flip_object)) {
        negate_v3(viewDirPersp);
      }

      angle_cos = dot_v3v3(viewDirPersp, no);
    }

    /* If back-face culling is disabled, allow painting on back faces. */
    if (!ps->do_backfacecull) {
      angle_cos = fabsf(angle_cos);
    }

    if (angle_cos <= ps->normal_angle__cos) {
      /* Outsize the normal limit. */
      return 0.0f;
    }
    if (angle_cos < ps->normal_angle_inner__cos) {
      mask *= (ps->normal_angle - acosf(angle_cos)) / ps->normal_angle_range;
    } /* otherwise no mask normal is needed, we're within the limit */
  }

  /* This only works when the opacity doesn't change while painting, stylus pressure messes with
   * this so don't use it. */
  // if (ps->is_airbrush == 0) mask *= BKE_brush_alpha_get(ps->brush);

  return mask;
}

static int project_paint_pixel_sizeof(const short tool)
{
  if (ELEM(tool, PAINT_TOOL_CLONE, PAINT_TOOL_SMEAR)) {
    return sizeof(ProjPixelClone);
  }
  return sizeof(ProjPixel);
}

static int project_paint_undo_subtiles(const TileInfo *tinf, int tx, int ty)
{
  ProjPaintImage *pjIma = tinf->pjima;
  int tile_index = tx + ty * tinf->tile_width;
  bool generate_tile = false;

  /* double check lock to avoid locking */
  if (UNLIKELY(!pjIma->undoRect[tile_index])) {
    if (tinf->lock) {
      BLI_spin_lock(tinf->lock);
    }
    if (LIKELY(!pjIma->undoRect[tile_index])) {
      pjIma->undoRect[tile_index] = TILE_PENDING;
      generate_tile = true;
    }
    if (tinf->lock) {
      BLI_spin_unlock(tinf->lock);
    }
  }

  if (generate_tile) {
    ListBase *undo_tiles = ED_image_paint_tile_list_get();
    volatile void *undorect;
    if (tinf->masked) {
      undorect = ED_image_paint_tile_push(undo_tiles,
                                          pjIma->ima,
                                          pjIma->ibuf,
                                          tinf->tmpibuf,
                                          &pjIma->iuser,
                                          tx,
                                          ty,
                                          &pjIma->maskRect[tile_index],
                                          &pjIma->valid[tile_index],
                                          true,
                                          false);
    }
    else {
      undorect = ED_image_paint_tile_push(undo_tiles,
                                          pjIma->ima,
                                          pjIma->ibuf,
                                          tinf->tmpibuf,
                                          &pjIma->iuser,
                                          tx,
                                          ty,
                                          NULL,
                                          &pjIma->valid[tile_index],
                                          true,
                                          false);
    }

    BKE_image_mark_dirty(pjIma->ima, pjIma->ibuf);
    /* tile ready, publish */
    if (tinf->lock) {
      BLI_spin_lock(tinf->lock);
    }
    pjIma->undoRect[tile_index] = undorect;
    if (tinf->lock) {
      BLI_spin_unlock(tinf->lock);
    }
  }

  return tile_index;
}

/* run this function when we know a bucket's, face's pixel can be initialized,
 * return the ProjPixel which is added to 'ps->bucketRect[bucket_index]' */
static ProjPixel *project_paint_uvpixel_init(const ProjPaintState *ps,
                                             MemArena *arena,
                                             const TileInfo *tinf,
                                             int x_px,
                                             int y_px,
                                             const float mask,
                                             const int tri_index,
                                             const float pixelScreenCo[4],
                                             const float world_spaceCo[3],
                                             const float w[3])
{
  ProjPixel *projPixel;
  int x_tile, y_tile;
  int x_round, y_round;
  int tile_offset;
  /* Volatile is important here to ensure pending check is not optimized away by compiler. */
  volatile int tile_index;

  ProjPaintImage *projima = tinf->pjima;
  ImBuf *ibuf = projima->ibuf;
  /* wrap pixel location */

  x_px = mod_i(x_px, ibuf->x);
  y_px = mod_i(y_px, ibuf->y);

  BLI_assert(ps->pixel_sizeof == project_paint_pixel_sizeof(ps->tool));
  projPixel = BLI_memarena_alloc(arena, ps->pixel_sizeof);

  /* calculate the undo tile offset of the pixel, used to store the original
   * pixel color and accumulated mask if any */
  x_tile = x_px >> ED_IMAGE_UNDO_TILE_BITS;
  y_tile = y_px >> ED_IMAGE_UNDO_TILE_BITS;

  x_round = x_tile * ED_IMAGE_UNDO_TILE_SIZE;
  y_round = y_tile * ED_IMAGE_UNDO_TILE_SIZE;
  // memset(projPixel, 0, size);

  tile_offset = (x_px - x_round) + (y_px - y_round) * ED_IMAGE_UNDO_TILE_SIZE;
  tile_index = project_paint_undo_subtiles(tinf, x_tile, y_tile);

  /* other thread may be initializing the tile so wait here */
  while (projima->undoRect[tile_index] == TILE_PENDING) {
    /* pass */
  }

  BLI_assert(tile_index <
             (ED_IMAGE_UNDO_TILE_NUMBER(ibuf->x) * ED_IMAGE_UNDO_TILE_NUMBER(ibuf->y)));
  BLI_assert(tile_offset < (ED_IMAGE_UNDO_TILE_SIZE * ED_IMAGE_UNDO_TILE_SIZE));

  projPixel->valid = projima->valid[tile_index];

  if (ibuf->rect_float) {
    projPixel->pixel.f_pt = ibuf->rect_float + ((x_px + y_px * ibuf->x) * 4);
    projPixel->origColor.f_pt = (float *)projima->undoRect[tile_index] + 4 * tile_offset;
    zero_v4(projPixel->newColor.f);
  }
  else {
    projPixel->pixel.ch_pt = (uchar *)(ibuf->rect + (x_px + y_px * ibuf->x));
    projPixel->origColor.uint_pt = (uint *)projima->undoRect[tile_index] + tile_offset;
    projPixel->newColor.uint = 0;
  }

  /* Screen-space unclamped, we could keep its z and w values but don't need them at the moment. */
  if (ps->brush->mtex.brush_map_mode == MTEX_MAP_MODE_3D) {
    copy_v3_v3(projPixel->worldCoSS, world_spaceCo);
  }

  copy_v2_v2(projPixel->projCoSS, pixelScreenCo);

  projPixel->x_px = x_px;
  projPixel->y_px = y_px;

  projPixel->mask = (ushort)(mask * 65535);
  if (ps->do_masking) {
    projPixel->mask_accum = projima->maskRect[tile_index] + tile_offset;
  }
  else {
    projPixel->mask_accum = NULL;
  }

  /* which bounding box cell are we in?, needed for undo */
  projPixel->bb_cell_index = ((int)(((float)x_px / (float)ibuf->x) * PROJ_BOUNDBOX_DIV)) +
                             ((int)(((float)y_px / (float)ibuf->y) * PROJ_BOUNDBOX_DIV)) *
                                 PROJ_BOUNDBOX_DIV;

  /* done with view3d_project_float inline */
  if (ps->tool == PAINT_TOOL_CLONE) {
    if (ps->poly_to_loop_uv_clone) {
      ImBuf *ibuf_other;
      Image *other_tpage = project_paint_face_clone_image(ps, tri_index);

      if (other_tpage && (ibuf_other = BKE_image_acquire_ibuf(other_tpage, NULL, NULL))) {
        const MLoopTri *lt_other = &ps->mlooptri_eval[tri_index];
        const float *lt_other_tri_uv[3] = {
            PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv_clone, lt_other)};

        /* #BKE_image_acquire_ibuf - TODO: this may be slow. */

        if (ibuf->rect_float) {
          if (ibuf_other->rect_float) { /* from float to float */
            project_face_pixel(
                lt_other_tri_uv, ibuf_other, w, NULL, ((ProjPixelClone *)projPixel)->clonepx.f);
          }
          else { /* from char to float */
            uchar rgba_ub[4];
            float rgba[4];
            project_face_pixel(lt_other_tri_uv, ibuf_other, w, rgba_ub, NULL);
            if (ps->use_colormanagement) {
              srgb_to_linearrgb_uchar4(rgba, rgba_ub);
            }
            else {
              rgba_uchar_to_float(rgba, rgba_ub);
            }
            straight_to_premul_v4_v4(((ProjPixelClone *)projPixel)->clonepx.f, rgba);
          }
        }
        else {
          if (ibuf_other->rect_float) { /* float to char */
            float rgba[4];
            project_face_pixel(lt_other_tri_uv, ibuf_other, w, NULL, rgba);
            premul_to_straight_v4(rgba);
            if (ps->use_colormanagement) {
              linearrgb_to_srgb_uchar3(((ProjPixelClone *)projPixel)->clonepx.ch, rgba);
            }
            else {
              rgb_float_to_uchar(((ProjPixelClone *)projPixel)->clonepx.ch, rgba);
            }
            ((ProjPixelClone *)projPixel)->clonepx.ch[3] = rgba[3] * 255;
          }
          else { /* char to char */
            project_face_pixel(
                lt_other_tri_uv, ibuf_other, w, ((ProjPixelClone *)projPixel)->clonepx.ch, NULL);
          }
        }

        BKE_image_release_ibuf(other_tpage, ibuf_other, NULL);
      }
      else {
        if (ibuf->rect_float) {
          ((ProjPixelClone *)projPixel)->clonepx.f[3] = 0;
        }
        else {
          ((ProjPixelClone *)projPixel)->clonepx.ch[3] = 0;
        }
      }
    }
    else {
      float co[2];
      sub_v2_v2v2(co, projPixel->projCoSS, ps->cloneOffset);

      /* no need to initialize the bucket, we're only checking buckets faces and for this
       * the faces are already initialized in project_paint_delayed_face_init(...) */
      if (ibuf->rect_float) {
        if (!project_paint_PickColor(ps, co, ((ProjPixelClone *)projPixel)->clonepx.f, NULL, 1)) {
          /* zero alpha - ignore */
          ((ProjPixelClone *)projPixel)->clonepx.f[3] = 0;
        }
      }
      else {
        if (!project_paint_PickColor(ps, co, NULL, ((ProjPixelClone *)projPixel)->clonepx.ch, 1)) {
          /* zero alpha - ignore */
          ((ProjPixelClone *)projPixel)->clonepx.ch[3] = 0;
        }
      }
    }
  }

#ifdef PROJ_DEBUG_PAINT
  if (ibuf->rect_float) {
    projPixel->pixel.f_pt[0] = 0;
  }
  else {
    projPixel->pixel.ch_pt[0] = 0;
  }
#endif
  /* pointer arithmetic */
  projPixel->image_index = projima - ps->projImages;

  return projPixel;
}

static bool line_clip_rect2f(const rctf *cliprect,
                             const rctf *rect,
                             const float l1[2],
                             const float l2[2],
                             float l1_clip[2],
                             float l2_clip[2])
{
  /* first account for horizontal, then vertical lines */
  /* Horizontal. */
  if (fabsf(l1[1] - l2[1]) < PROJ_PIXEL_TOLERANCE) {
    /* is the line out of range on its Y axis? */
    if (l1[1] < rect->ymin || l1[1] > rect->ymax) {
      return false;
    }
    /* line is out of range on its X axis */
    if ((l1[0] < rect->xmin && l2[0] < rect->xmin) || (l1[0] > rect->xmax && l2[0] > rect->xmax)) {
      return false;
    }

    /* This is a single point  (or close to). */
    if (fabsf(l1[0] - l2[0]) < PROJ_PIXEL_TOLERANCE) {
      if (BLI_rctf_isect_pt_v(rect, l1)) {
        copy_v2_v2(l1_clip, l1);
        copy_v2_v2(l2_clip, l2);
        return true;
      }
      return false;
    }

    copy_v2_v2(l1_clip, l1);
    copy_v2_v2(l2_clip, l2);
    CLAMP(l1_clip[0], rect->xmin, rect->xmax);
    CLAMP(l2_clip[0], rect->xmin, rect->xmax);
    return true;
  }
  if (fabsf(l1[0] - l2[0]) < PROJ_PIXEL_TOLERANCE) {
    /* is the line out of range on its X axis? */
    if (l1[0] < rect->xmin || l1[0] > rect->xmax) {
      return false;
    }

    /* line is out of range on its Y axis */
    if ((l1[1] < rect->ymin && l2[1] < rect->ymin) || (l1[1] > rect->ymax && l2[1] > rect->ymax)) {
      return false;
    }

    /* This is a single point  (or close to). */
    if (fabsf(l1[1] - l2[1]) < PROJ_PIXEL_TOLERANCE) {
      if (BLI_rctf_isect_pt_v(rect, l1)) {
        copy_v2_v2(l1_clip, l1);
        copy_v2_v2(l2_clip, l2);
        return true;
      }
      return false;
    }

    copy_v2_v2(l1_clip, l1);
    copy_v2_v2(l2_clip, l2);
    CLAMP(l1_clip[1], rect->ymin, rect->ymax);
    CLAMP(l2_clip[1], rect->ymin, rect->ymax);
    return true;
  }

  float isect;
  short ok1 = 0;
  short ok2 = 0;

  /* Done with vertical lines */

  /* are either of the points inside the rectangle ? */
  if (BLI_rctf_isect_pt_v(rect, l1)) {
    copy_v2_v2(l1_clip, l1);
    ok1 = 1;
  }

  if (BLI_rctf_isect_pt_v(rect, l2)) {
    copy_v2_v2(l2_clip, l2);
    ok2 = 1;
  }

  /* line inside rect */
  if (ok1 && ok2) {
    return true;
  }

  /* top/bottom */
  if (line_isect_y(l1, l2, rect->ymin, &isect) && (isect >= cliprect->xmin) &&
      (isect <= cliprect->xmax)) {
    if (l1[1] < l2[1]) { /* line 1 is outside */
      l1_clip[0] = isect;
      l1_clip[1] = rect->ymin;
      ok1 = 1;
    }
    else {
      l2_clip[0] = isect;
      l2_clip[1] = rect->ymin;
      ok2 = 2;
    }
  }

  if (ok1 && ok2) {
    return true;
  }

  if (line_isect_y(l1, l2, rect->ymax, &isect) && (isect >= cliprect->xmin) &&
      (isect <= cliprect->xmax)) {
    if (l1[1] > l2[1]) { /* line 1 is outside */
      l1_clip[0] = isect;
      l1_clip[1] = rect->ymax;
      ok1 = 1;
    }
    else {
      l2_clip[0] = isect;
      l2_clip[1] = rect->ymax;
      ok2 = 2;
    }
  }

  if (ok1 && ok2) {
    return true;
  }

  /* left/right */
  if (line_isect_x(l1, l2, rect->xmin, &isect) && (isect >= cliprect->ymin) &&
      (isect <= cliprect->ymax)) {
    if (l1[0] < l2[0]) { /* line 1 is outside */
      l1_clip[0] = rect->xmin;
      l1_clip[1] = isect;
      ok1 = 1;
    }
    else {
      l2_clip[0] = rect->xmin;
      l2_clip[1] = isect;
      ok2 = 2;
    }
  }

  if (ok1 && ok2) {
    return true;
  }

  if (line_isect_x(l1, l2, rect->xmax, &isect) && (isect >= cliprect->ymin) &&
      (isect <= cliprect->ymax)) {
    if (l1[0] > l2[0]) { /* line 1 is outside */
      l1_clip[0] = rect->xmax;
      l1_clip[1] = isect;
      ok1 = 1;
    }
    else {
      l2_clip[0] = rect->xmax;
      l2_clip[1] = isect;
      ok2 = 2;
    }
  }

  if (ok1 && ok2) {
    return true;
  }
  return false;
}

/**
 * Scale the tri about its center
 * scaling by #PROJ_FACE_SCALE_SEAM (0.99x) is used for getting fake UV pixel coords that are on
 * the edge of the face but slightly inside it occlusion tests don't return hits on adjacent faces.
 */
#ifndef PROJ_DEBUG_NOSEAMBLEED

static void scale_tri(float insetCos[3][3], const float *origCos[3], const float inset)
{
  float cent[3];
  cent[0] = (origCos[0][0] + origCos[1][0] + origCos[2][0]) * (1.0f / 3.0f);
  cent[1] = (origCos[0][1] + origCos[1][1] + origCos[2][1]) * (1.0f / 3.0f);
  cent[2] = (origCos[0][2] + origCos[1][2] + origCos[2][2]) * (1.0f / 3.0f);

  sub_v3_v3v3(insetCos[0], origCos[0], cent);
  sub_v3_v3v3(insetCos[1], origCos[1], cent);
  sub_v3_v3v3(insetCos[2], origCos[2], cent);

  mul_v3_fl(insetCos[0], inset);
  mul_v3_fl(insetCos[1], inset);
  mul_v3_fl(insetCos[2], inset);

  add_v3_v3(insetCos[0], cent);
  add_v3_v3(insetCos[1], cent);
  add_v3_v3(insetCos[2], cent);
}
#endif  // PROJ_DEBUG_NOSEAMBLEED

static float len_squared_v2v2_alt(const float v1[2], const float v2_1, const float v2_2)
{
  float x, y;

  x = v1[0] - v2_1;
  y = v1[1] - v2_2;
  return x * x + y * y;
}

/**
 * \note Use a squared value so we can use #len_squared_v2v2
 * be sure that you have done a bounds check first or this may fail.
 *
 * Only give \a bucket_bounds as an arg because we need it elsewhere.
 */
static bool project_bucket_isect_circle(const float cent[2],
                                        const float radius_squared,
                                        const rctf *bucket_bounds)
{

  /* Would normally to a simple intersection test,
   * however we know the bounds of these 2 already intersect so we only need to test
   * if the center is inside the vertical or horizontal bounds on either axis,
   * this is even less work than an intersection test.
   */
#if 0
  if (BLI_rctf_isect_pt_v(bucket_bounds, cent)) {
    return true;
  }
#endif

  if ((bucket_bounds->xmin <= cent[0] && bucket_bounds->xmax >= cent[0]) ||
      (bucket_bounds->ymin <= cent[1] && bucket_bounds->ymax >= cent[1])) {
    return true;
  }

  /* out of bounds left */
  if (cent[0] < bucket_bounds->xmin) {
    /* lower left out of radius test */
    if (cent[1] < bucket_bounds->ymin) {
      return (len_squared_v2v2_alt(cent, bucket_bounds->xmin, bucket_bounds->ymin) <
              radius_squared) ?
                 true :
                 false;
    }
    /* top left test */
    if (cent[1] > bucket_bounds->ymax) {
      return (len_squared_v2v2_alt(cent, bucket_bounds->xmin, bucket_bounds->ymax) <
              radius_squared) ?
                 true :
                 false;
    }
  }
  else if (cent[0] > bucket_bounds->xmax) {
    /* lower right out of radius test */
    if (cent[1] < bucket_bounds->ymin) {
      return (len_squared_v2v2_alt(cent, bucket_bounds->xmax, bucket_bounds->ymin) <
              radius_squared) ?
                 true :
                 false;
    }
    /* top right test */
    if (cent[1] > bucket_bounds->ymax) {
      return (len_squared_v2v2_alt(cent, bucket_bounds->xmax, bucket_bounds->ymax) <
              radius_squared) ?
                 true :
                 false;
    }
  }

  return false;
}

/* Note for #rect_to_uvspace_ortho() and #rect_to_uvspace_persp()
 * in ortho view this function gives good results when bucket_bounds are outside the triangle
 * however in some cases, perspective view will mess up with faces
 * that have minimal screen-space area (viewed from the side).
 *
 * for this reason its not reliable in this case so we'll use the Simple Barycentric'
 * functions that only account for points inside the triangle.
 * however switching back to this for ortho is always an option. */

static void rect_to_uvspace_ortho(const rctf *bucket_bounds,
                                  const float *v1coSS,
                                  const float *v2coSS,
                                  const float *v3coSS,
                                  const float *uv1co,
                                  const float *uv2co,
                                  const float *uv3co,
                                  float bucket_bounds_uv[4][2],
                                  const int flip)
{
  float uv[2];
  float w[3];

  /* get the UV space bounding box */
  uv[0] = bucket_bounds->xmax;
  uv[1] = bucket_bounds->ymin;
  barycentric_weights_v2(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 3 : 0], uv1co, uv2co, uv3co, w);

  // uv[0] = bucket_bounds->xmax; // set above
  uv[1] = bucket_bounds->ymax;
  barycentric_weights_v2(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 2 : 1], uv1co, uv2co, uv3co, w);

  uv[0] = bucket_bounds->xmin;
  // uv[1] = bucket_bounds->ymax; // set above
  barycentric_weights_v2(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 1 : 2], uv1co, uv2co, uv3co, w);

  // uv[0] = bucket_bounds->xmin; // set above
  uv[1] = bucket_bounds->ymin;
  barycentric_weights_v2(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 0 : 3], uv1co, uv2co, uv3co, w);
}

/* same as above but use barycentric_weights_v2_persp */
static void rect_to_uvspace_persp(const rctf *bucket_bounds,
                                  const float *v1coSS,
                                  const float *v2coSS,
                                  const float *v3coSS,
                                  const float *uv1co,
                                  const float *uv2co,
                                  const float *uv3co,
                                  float bucket_bounds_uv[4][2],
                                  const int flip)
{
  float uv[2];
  float w[3];

  /* get the UV space bounding box */
  uv[0] = bucket_bounds->xmax;
  uv[1] = bucket_bounds->ymin;
  barycentric_weights_v2_persp(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 3 : 0], uv1co, uv2co, uv3co, w);

  // uv[0] = bucket_bounds->xmax; // set above
  uv[1] = bucket_bounds->ymax;
  barycentric_weights_v2_persp(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 2 : 1], uv1co, uv2co, uv3co, w);

  uv[0] = bucket_bounds->xmin;
  // uv[1] = bucket_bounds->ymax; // set above
  barycentric_weights_v2_persp(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 1 : 2], uv1co, uv2co, uv3co, w);

  // uv[0] = bucket_bounds->xmin; // set above
  uv[1] = bucket_bounds->ymin;
  barycentric_weights_v2_persp(v1coSS, v2coSS, v3coSS, uv, w);
  interp_v2_v2v2v2(bucket_bounds_uv[flip ? 0 : 3], uv1co, uv2co, uv3co, w);
}

/* This works as we need it to but we can save a few steps and not use it */

#if 0
static float angle_2d_clockwise(const float p1[2], const float p2[2], const float p3[2])
{
  float v1[2], v2[2];

  v1[0] = p1[0] - p2[0];
  v1[1] = p1[1] - p2[1];
  v2[0] = p3[0] - p2[0];
  v2[1] = p3[1] - p2[1];

  return -atan2f(v1[0] * v2[1] - v1[1] * v2[0], v1[0] * v2[0] + v1[1] * v2[1]);
}
#endif

#define ISECT_1 (1)
#define ISECT_2 (1 << 1)
#define ISECT_3 (1 << 2)
#define ISECT_4 (1 << 3)
#define ISECT_ALL3 ((1 << 3) - 1)
#define ISECT_ALL4 ((1 << 4) - 1)

/* limit must be a fraction over 1.0f */
static bool IsectPT2Df_limit(
    const float pt[2], const float v1[2], const float v2[2], const float v3[2], const float limit)
{
  return ((area_tri_v2(pt, v1, v2) + area_tri_v2(pt, v2, v3) + area_tri_v2(pt, v3, v1)) /
          (area_tri_v2(v1, v2, v3))) < limit;
}

/**
 * Clip the face by a bucket and set the uv-space bucket_bounds_uv
 * so we have the clipped UV's to do pixel intersection tests with
 */
static int float_z_sort_flip(const void *p1, const void *p2)
{
  return (((float *)p1)[2] < ((float *)p2)[2] ? 1 : -1);
}

static int float_z_sort(const void *p1, const void *p2)
{
  return (((float *)p1)[2] < ((float *)p2)[2] ? -1 : 1);
}

/* assumes one point is within the rectangle */
static bool line_rect_clip(const rctf *rect,
                           const float l1[4],
                           const float l2[4],
                           const float uv1[2],
                           const float uv2[2],
                           float uv[2],
                           bool is_ortho)
{
  float min = FLT_MAX, tmp;
  float xlen = l2[0] - l1[0];
  float ylen = l2[1] - l1[1];

  /* 0.1 might seem too much, but remember, this is pixels! */
  if (xlen > 0.1f) {
    if ((l1[0] - rect->xmin) * (l2[0] - rect->xmin) <= 0) {
      tmp = rect->xmin;
      min = min_ff((tmp - l1[0]) / xlen, min);
    }
    else if ((l1[0] - rect->xmax) * (l2[0] - rect->xmax) < 0) {
      tmp = rect->xmax;
      min = min_ff((tmp - l1[0]) / xlen, min);
    }
  }

  if (ylen > 0.1f) {
    if ((l1[1] - rect->ymin) * (l2[1] - rect->ymin) <= 0) {
      tmp = rect->ymin;
      min = min_ff((tmp - l1[1]) / ylen, min);
    }
    else if ((l1[1] - rect->ymax) * (l2[1] - rect->ymax) < 0) {
      tmp = rect->ymax;
      min = min_ff((tmp - l1[1]) / ylen, min);
    }
  }

  if (min == FLT_MAX) {
    return false;
  }

  tmp = (is_ortho) ? 1.0f : (l1[3] + min * (l2[3] - l1[3]));

  uv[0] = (uv1[0] + min / tmp * (uv2[0] - uv1[0]));
  uv[1] = (uv1[1] + min / tmp * (uv2[1] - uv1[1]));

  return true;
}

static void project_bucket_clip_face(const bool is_ortho,
                                     const bool is_flip_object,
                                     const rctf *cliprect,
                                     const rctf *bucket_bounds,
                                     const float *v1coSS,
                                     const float *v2coSS,
                                     const float *v3coSS,
                                     const float *uv1co,
                                     const float *uv2co,
                                     const float *uv3co,
                                     float bucket_bounds_uv[8][2],
                                     int *tot,
                                     bool cull)
{
  int inside_bucket_flag = 0;
  int inside_face_flag = 0;
  int flip;
  bool collinear = false;

  float bucket_bounds_ss[4][2];

  /* detect pathological case where face the three vertices are almost collinear in screen space.
   * mostly those will be culled but when flood filling or with
   * smooth shading it's a possibility */
  if (min_fff(dist_squared_to_line_v2(v1coSS, v2coSS, v3coSS),
              dist_squared_to_line_v2(v2coSS, v3coSS, v1coSS),
              dist_squared_to_line_v2(v3coSS, v1coSS, v2coSS)) < PROJ_PIXEL_TOLERANCE) {
    collinear = true;
  }

  /* get the UV space bounding box */
  inside_bucket_flag |= BLI_rctf_isect_pt_v(bucket_bounds, v1coSS);
  inside_bucket_flag |= BLI_rctf_isect_pt_v(bucket_bounds, v2coSS) << 1;
  inside_bucket_flag |= BLI_rctf_isect_pt_v(bucket_bounds, v3coSS) << 2;

  if (inside_bucket_flag == ISECT_ALL3) {
    /* is_flip_object is used here because we use the face winding */
    flip = (((line_point_side_v2(v1coSS, v2coSS, v3coSS) > 0.0f) != is_flip_object) !=
            (line_point_side_v2(uv1co, uv2co, uv3co) > 0.0f));

    /* All screen-space points are inside the bucket bounding box,
     * this means we don't need to clip and can simply return the UVs. */
    if (flip) { /* facing the back? */
      copy_v2_v2(bucket_bounds_uv[0], uv3co);
      copy_v2_v2(bucket_bounds_uv[1], uv2co);
      copy_v2_v2(bucket_bounds_uv[2], uv1co);
    }
    else {
      copy_v2_v2(bucket_bounds_uv[0], uv1co);
      copy_v2_v2(bucket_bounds_uv[1], uv2co);
      copy_v2_v2(bucket_bounds_uv[2], uv3co);
    }

    *tot = 3;
    return;
  }
  /* Handle pathological case here,
   * no need for further intersections below since triangle area is almost zero. */
  if (collinear) {
    int flag;

    (*tot) = 0;

    if (cull) {
      return;
    }

    if (inside_bucket_flag & ISECT_1) {
      copy_v2_v2(bucket_bounds_uv[*tot], uv1co);
      (*tot)++;
    }

    flag = inside_bucket_flag & (ISECT_1 | ISECT_2);
    if (flag && flag != (ISECT_1 | ISECT_2)) {
      if (line_rect_clip(
              bucket_bounds, v1coSS, v2coSS, uv1co, uv2co, bucket_bounds_uv[*tot], is_ortho)) {
        (*tot)++;
      }
    }

    if (inside_bucket_flag & ISECT_2) {
      copy_v2_v2(bucket_bounds_uv[*tot], uv2co);
      (*tot)++;
    }

    flag = inside_bucket_flag & (ISECT_2 | ISECT_3);
    if (flag && flag != (ISECT_2 | ISECT_3)) {
      if (line_rect_clip(
              bucket_bounds, v2coSS, v3coSS, uv2co, uv3co, bucket_bounds_uv[*tot], is_ortho)) {
        (*tot)++;
      }
    }

    if (inside_bucket_flag & ISECT_3) {
      copy_v2_v2(bucket_bounds_uv[*tot], uv3co);
      (*tot)++;
    }

    flag = inside_bucket_flag & (ISECT_3 | ISECT_1);
    if (flag && flag != (ISECT_3 | ISECT_1)) {
      if (line_rect_clip(
              bucket_bounds, v3coSS, v1coSS, uv3co, uv1co, bucket_bounds_uv[*tot], is_ortho)) {
        (*tot)++;
      }
    }

    if ((*tot) < 3) {
      /* no intersections to speak of, but more probable is that all face is just outside the
       * rectangle and culled due to float precision issues. Since above tests have failed,
       * just dump triangle as is for painting */
      *tot = 0;
      copy_v2_v2(bucket_bounds_uv[*tot], uv1co);
      (*tot)++;
      copy_v2_v2(bucket_bounds_uv[*tot], uv2co);
      (*tot)++;
      copy_v2_v2(bucket_bounds_uv[*tot], uv3co);
      (*tot)++;
      return;
    }

    return;
  }

  /* Get the UV space bounding box. */
  /* Use #IsectPT2Df_limit here so we catch points are touching the triangles edge
   * (or a small fraction over) */
  bucket_bounds_ss[0][0] = bucket_bounds->xmax;
  bucket_bounds_ss[0][1] = bucket_bounds->ymin;
  inside_face_flag |= (IsectPT2Df_limit(
                           bucket_bounds_ss[0], v1coSS, v2coSS, v3coSS, 1 + PROJ_GEOM_TOLERANCE) ?
                           ISECT_1 :
                           0);

  bucket_bounds_ss[1][0] = bucket_bounds->xmax;
  bucket_bounds_ss[1][1] = bucket_bounds->ymax;
  inside_face_flag |= (IsectPT2Df_limit(
                           bucket_bounds_ss[1], v1coSS, v2coSS, v3coSS, 1 + PROJ_GEOM_TOLERANCE) ?
                           ISECT_2 :
                           0);

  bucket_bounds_ss[2][0] = bucket_bounds->xmin;
  bucket_bounds_ss[2][1] = bucket_bounds->ymax;
  inside_face_flag |= (IsectPT2Df_limit(
                           bucket_bounds_ss[2], v1coSS, v2coSS, v3coSS, 1 + PROJ_GEOM_TOLERANCE) ?
                           ISECT_3 :
                           0);

  bucket_bounds_ss[3][0] = bucket_bounds->xmin;
  bucket_bounds_ss[3][1] = bucket_bounds->ymin;
  inside_face_flag |= (IsectPT2Df_limit(
                           bucket_bounds_ss[3], v1coSS, v2coSS, v3coSS, 1 + PROJ_GEOM_TOLERANCE) ?
                           ISECT_4 :
                           0);

  flip = ((line_point_side_v2(v1coSS, v2coSS, v3coSS) > 0.0f) !=
          (line_point_side_v2(uv1co, uv2co, uv3co) > 0.0f));

  if (inside_face_flag == ISECT_ALL4) {
    /* Bucket is totally inside the screen-space face, we can safely use weights. */

    if (is_ortho) {
      rect_to_uvspace_ortho(
          bucket_bounds, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, bucket_bounds_uv, flip);
    }
    else {
      rect_to_uvspace_persp(
          bucket_bounds, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, bucket_bounds_uv, flip);
    }

    *tot = 4;
    return;
  }

  {
    /* The Complicated Case!
     *
     * The 2 cases above are where the face is inside the bucket
     * or the bucket is inside the face.
     *
     * we need to make a convex poly-line from the intersection between the screen-space face
     * and the bucket bounds.
     *
     * There are a number of ways this could be done, currently it just collects all
     * intersecting verts, and line intersections, then sorts them clockwise, this is
     * a lot easier than evaluating the geometry to do a correct clipping on both shapes.
     */

    /* Add a bunch of points, we know must make up the convex hull
     * which is the clipped rect and triangle */

    /* Maximum possible 6 intersections when using a rectangle and triangle */

    /* The 3rd float is used to store angle for qsort(), NOT as a Z location */
    float isectVCosSS[8][3];
    float v1_clipSS[2], v2_clipSS[2];
    float w[3];

    /* calc center */
    float cent[2] = {0.0f, 0.0f};
    // float up[2] = {0.0f, 1.0f};
    bool doubles;

    (*tot) = 0;

    if (inside_face_flag & ISECT_1) {
      copy_v2_v2(isectVCosSS[*tot], bucket_bounds_ss[0]);
      (*tot)++;
    }
    if (inside_face_flag & ISECT_2) {
      copy_v2_v2(isectVCosSS[*tot], bucket_bounds_ss[1]);
      (*tot)++;
    }
    if (inside_face_flag & ISECT_3) {
      copy_v2_v2(isectVCosSS[*tot], bucket_bounds_ss[2]);
      (*tot)++;
    }
    if (inside_face_flag & ISECT_4) {
      copy_v2_v2(isectVCosSS[*tot], bucket_bounds_ss[3]);
      (*tot)++;
    }

    if (inside_bucket_flag & ISECT_1) {
      copy_v2_v2(isectVCosSS[*tot], v1coSS);
      (*tot)++;
    }
    if (inside_bucket_flag & ISECT_2) {
      copy_v2_v2(isectVCosSS[*tot], v2coSS);
      (*tot)++;
    }
    if (inside_bucket_flag & ISECT_3) {
      copy_v2_v2(isectVCosSS[*tot], v3coSS);
      (*tot)++;
    }

    if ((inside_bucket_flag & (ISECT_1 | ISECT_2)) != (ISECT_1 | ISECT_2)) {
      if (line_clip_rect2f(cliprect, bucket_bounds, v1coSS, v2coSS, v1_clipSS, v2_clipSS)) {
        if ((inside_bucket_flag & ISECT_1) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v1_clipSS);
          (*tot)++;
        }
        if ((inside_bucket_flag & ISECT_2) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v2_clipSS);
          (*tot)++;
        }
      }
    }

    if ((inside_bucket_flag & (ISECT_2 | ISECT_3)) != (ISECT_2 | ISECT_3)) {
      if (line_clip_rect2f(cliprect, bucket_bounds, v2coSS, v3coSS, v1_clipSS, v2_clipSS)) {
        if ((inside_bucket_flag & ISECT_2) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v1_clipSS);
          (*tot)++;
        }
        if ((inside_bucket_flag & ISECT_3) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v2_clipSS);
          (*tot)++;
        }
      }
    }

    if ((inside_bucket_flag & (ISECT_3 | ISECT_1)) != (ISECT_3 | ISECT_1)) {
      if (line_clip_rect2f(cliprect, bucket_bounds, v3coSS, v1coSS, v1_clipSS, v2_clipSS)) {
        if ((inside_bucket_flag & ISECT_3) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v1_clipSS);
          (*tot)++;
        }
        if ((inside_bucket_flag & ISECT_1) == 0) {
          copy_v2_v2(isectVCosSS[*tot], v2_clipSS);
          (*tot)++;
        }
      }
    }

    if ((*tot) < 3) { /* no intersections to speak of */
      *tot = 0;
      return;
    }

    /* now we have all points we need, collect their angles and sort them clockwise */

    for (int i = 0; i < (*tot); i++) {
      cent[0] += isectVCosSS[i][0];
      cent[1] += isectVCosSS[i][1];
    }
    cent[0] = cent[0] / (float)(*tot);
    cent[1] = cent[1] / (float)(*tot);

    /* Collect angles for every point around the center point */

#if 0 /* uses a few more cycles than the above loop */
    for (int i = 0; i < (*tot); i++) {
      isectVCosSS[i][2] = angle_2d_clockwise(up, cent, isectVCosSS[i]);
    }
#endif

    /* Abuse this var for the loop below */
    v1_clipSS[0] = cent[0];
    v1_clipSS[1] = cent[1] + 1.0f;

    for (int i = 0; i < (*tot); i++) {
      v2_clipSS[0] = isectVCosSS[i][0] - cent[0];
      v2_clipSS[1] = isectVCosSS[i][1] - cent[1];
      isectVCosSS[i][2] = atan2f(v1_clipSS[0] * v2_clipSS[1] - v1_clipSS[1] * v2_clipSS[0],
                                 v1_clipSS[0] * v2_clipSS[0] + v1_clipSS[1] * v2_clipSS[1]);
    }

    if (flip) {
      qsort(isectVCosSS, *tot, sizeof(float[3]), float_z_sort_flip);
    }
    else {
      qsort(isectVCosSS, *tot, sizeof(float[3]), float_z_sort);
    }

    doubles = true;
    while (doubles == true) {
      doubles = false;

      for (int i = 0; i < (*tot); i++) {
        if (fabsf(isectVCosSS[(i + 1) % *tot][0] - isectVCosSS[i][0]) < PROJ_PIXEL_TOLERANCE &&
            fabsf(isectVCosSS[(i + 1) % *tot][1] - isectVCosSS[i][1]) < PROJ_PIXEL_TOLERANCE) {
          for (int j = i; j < (*tot) - 1; j++) {
            isectVCosSS[j][0] = isectVCosSS[j + 1][0];
            isectVCosSS[j][1] = isectVCosSS[j + 1][1];
          }
          /* keep looking for more doubles */
          doubles = true;
          (*tot)--;
        }
      }

      /* its possible there is only a few left after remove doubles */
      if ((*tot) < 3) {
        // printf("removed too many doubles B\n");
        *tot = 0;
        return;
      }
    }

    if (is_ortho) {
      for (int i = 0; i < (*tot); i++) {
        barycentric_weights_v2(v1coSS, v2coSS, v3coSS, isectVCosSS[i], w);
        interp_v2_v2v2v2(bucket_bounds_uv[i], uv1co, uv2co, uv3co, w);
      }
    }
    else {
      for (int i = 0; i < (*tot); i++) {
        barycentric_weights_v2_persp(v1coSS, v2coSS, v3coSS, isectVCosSS[i], w);
        interp_v2_v2v2v2(bucket_bounds_uv[i], uv1co, uv2co, uv3co, w);
      }
    }
  }

#ifdef PROJ_DEBUG_PRINT_CLIP
  /* include this at the bottom of the above function to debug the output */

  {
    /* If there are ever any problems, */
    float test_uv[4][2];
    int i;
    if (is_ortho) {
      rect_to_uvspace_ortho(
          bucket_bounds, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, test_uv, flip);
    }
    else {
      rect_to_uvspace_persp(
          bucket_bounds, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, test_uv, flip);
    }
    printf("(  [(%f,%f), (%f,%f), (%f,%f), (%f,%f)], ",
           test_uv[0][0],
           test_uv[0][1],
           test_uv[1][0],
           test_uv[1][1],
           test_uv[2][0],
           test_uv[2][1],
           test_uv[3][0],
           test_uv[3][1]);

    printf("  [(%f,%f), (%f,%f), (%f,%f)], ",
           uv1co[0],
           uv1co[1],
           uv2co[0],
           uv2co[1],
           uv3co[0],
           uv3co[1]);

    printf("[");
    for (int i = 0; i < (*tot); i++) {
      printf("(%f, %f),", bucket_bounds_uv[i][0], bucket_bounds_uv[i][1]);
    }
    printf("]),\\\n");
  }
#endif
}

/*
 * # This script creates faces in a blender scene from printed data above.
 *
 * project_ls = [
 * ...(output from above block)...
 * ]
 *
 * from Blender import Scene, Mesh, Window, sys, Mathutils
 *
 * import bpy
 *
 * V = Mathutils.Vector
 *
 * def main():
 *     sce = bpy.data.scenes.active
 *
 *     for item in project_ls:
 *         bb = item[0]
 *         uv = item[1]
 *         poly = item[2]
 *
 *         me = bpy.data.meshes.new()
 *         ob = sce.objects.new(me)
 *
 *         me.verts.extend([V(bb[0]).xyz, V(bb[1]).xyz, V(bb[2]).xyz, V(bb[3]).xyz])
 *         me.faces.extend([(0,1,2,3),])
 *         me.verts.extend([V(uv[0]).xyz, V(uv[1]).xyz, V(uv[2]).xyz])
 *         me.faces.extend([(4,5,6),])
 *
 *         vs = [V(p).xyz for p in poly]
 *         print len(vs)
 *         l = len(me.verts)
 *         me.verts.extend(vs)
 *
 *         i = l
 *         while i < len(me.verts):
 *             ii = i + 1
 *             if ii == len(me.verts):
 *                 ii = l
 *             me.edges.extend([i, ii])
 *             i += 1
 *
 * if __name__ == '__main__':
 *     main()
 */

#undef ISECT_1
#undef ISECT_2
#undef ISECT_3
#undef ISECT_4
#undef ISECT_ALL3
#undef ISECT_ALL4

/* checks if pt is inside a convex 2D polyline, the polyline must be ordered rotating clockwise
 * otherwise it would have to test for mixed (line_point_side_v2 > 0.0f) cases */
static bool IsectPoly2Df(const float pt[2], const float uv[][2], const int tot)
{
  int i;
  if (line_point_side_v2(uv[tot - 1], uv[0], pt) < 0.0f) {
    return false;
  }

  for (i = 1; i < tot; i++) {
    if (line_point_side_v2(uv[i - 1], uv[i], pt) < 0.0f) {
      return false;
    }
  }

  return true;
}
static bool IsectPoly2Df_twoside(const float pt[2], const float uv[][2], const int tot)
{
  const bool side = (line_point_side_v2(uv[tot - 1], uv[0], pt) > 0.0f);

  for (int i = 1; i < tot; i++) {
    if ((line_point_side_v2(uv[i - 1], uv[i], pt) > 0.0f) != side) {
      return false;
    }
  }

  return true;
}

/* One of the most important function for projection painting,
 * since it selects the pixels to be added into each bucket.
 *
 * initialize pixels from this face where it intersects with the bucket_index,
 * optionally initialize pixels for removing seams */
static void project_paint_face_init(const ProjPaintState *ps,
                                    const int thread_index,
                                    const int bucket_index,
                                    const int tri_index,
                                    const int image_index,
                                    const rctf *clip_rect,
                                    const rctf *bucket_bounds,
                                    ImBuf *ibuf,
                                    ImBuf **tmpibuf)
{
  /* Projection vars, to get the 3D locations into screen space. */
  MemArena *arena = ps->arena_mt[thread_index];
  LinkNode **bucketPixelNodes = ps->bucketRect + bucket_index;
  LinkNode *bucketFaceNodes = ps->bucketFaces[bucket_index];
  bool threaded = (ps->thread_tot > 1);

  TileInfo tinf = {
      ps->tile_lock,
      ps->do_masking,
      ED_IMAGE_UNDO_TILE_NUMBER(ibuf->x),
      tmpibuf,
      ps->projImages + image_index,
  };

  const MLoopTri *lt = &ps->mlooptri_eval[tri_index];
  const int lt_vtri[3] = {PS_LOOPTRI_AS_VERT_INDEX_3(ps, lt)};
  const float *lt_tri_uv[3] = {PS_LOOPTRI_AS_UV_3(ps->poly_to_loop_uv, lt)};

  /* UV/pixel seeking data */
  /* Image X/Y-Pixel */
  int x, y;
  float mask;
  /* Image floating point UV - same as x, y but from 0.0-1.0 */
  float uv[2];

  /* vert co screen-space, these will be assigned to lt_vtri[0-2] */
  const float *v1coSS, *v2coSS, *v3coSS;

  /* Vertex screen-space coords. */
  const float *vCo[3];

  float w[3], wco[3];

  /* for convenience only, these will be assigned to lt_tri_uv[0],1,2 or lt_tri_uv[0],2,3 */
  float *uv1co, *uv2co, *uv3co;
  float pixelScreenCo[4];
  bool do_3d_mapping = ps->brush->mtex.brush_map_mode == MTEX_MAP_MODE_3D;

  /* Image-space bounds. */
  rcti bounds_px;
  /* Variables for getting UV-space bounds. */

  /* Bucket bounds in UV space so we can init pixels only for this face. */
  float lt_uv_pxoffset[3][2];
  float xhalfpx, yhalfpx;
  const float ibuf_xf = (float)ibuf->x, ibuf_yf = (float)ibuf->y;

  /* for early loop exit */
  int has_x_isect = 0, has_isect = 0;

  float uv_clip[8][2];
  int uv_clip_tot;
  const bool is_ortho = ps->is_ortho;
  const bool is_flip_object = ps->is_flip_object;
  const bool do_backfacecull = ps->do_backfacecull;
  const bool do_clip = RV3D_CLIPPING_ENABLED(ps->v3d, ps->rv3d);

  vCo[0] = ps->mvert_eval[lt_vtri[0]].co;
  vCo[1] = ps->mvert_eval[lt_vtri[1]].co;
  vCo[2] = ps->mvert_eval[lt_vtri[2]].co;

  /* Use lt_uv_pxoffset instead of lt_tri_uv so we can offset the UV half a pixel
   * this is done so we can avoid offsetting all the pixels by 0.5 which causes
   * problems when wrapping negative coords */
  xhalfpx = (0.5f + (PROJ_PIXEL_TOLERANCE * (1.0f / 3.0f))) / ibuf_xf;
  yhalfpx = (0.5f + (PROJ_PIXEL_TOLERANCE * (1.0f / 4.0f))) / ibuf_yf;

  /* Note about (PROJ_GEOM_TOLERANCE/x) above...
   * Needed to add this offset since UV coords are often quads aligned to pixels.
   * In this case pixels can be exactly between 2 triangles causing nasty
   * artifacts.
   *
   * This workaround can be removed and painting will still work on most cases
   * but since the first thing most people try is painting onto a quad- better make it work.
   */

  lt_uv_pxoffset[0][0] = lt_tri_uv[0][0] - xhalfpx;
  lt_uv_pxoffset[0][1] = lt_tri_uv[0][1] - yhalfpx;

  lt_uv_pxoffset[1][0] = lt_tri_uv[1][0] - xhalfpx;
  lt_uv_pxoffset[1][1] = lt_tri_uv[1][1] - yhalfpx;

  lt_uv_pxoffset[2][0] = lt_tri_uv[2][0] - xhalfpx;
  lt_uv_pxoffset[2][1] = lt_tri_uv[2][1] - yhalfpx;

  {
    uv1co = lt_uv_pxoffset[0]; /* was lt_tri_uv[i1]; */
    uv2co = lt_uv_pxoffset[1]; /* was lt_tri_uv[i2]; */
    uv3co = lt_uv_pxoffset[2]; /* was lt_tri_uv[i3]; */

    v1coSS = ps->screenCoords[lt_vtri[0]];
    v2coSS = ps->screenCoords[lt_vtri[1]];
    v3coSS = ps->screenCoords[lt_vtri[2]];

    /* This function gives is a concave polyline in UV space from the clipped tri. */
    project_bucket_clip_face(is_ortho,
                             is_flip_object,
                             clip_rect,
                             bucket_bounds,
                             v1coSS,
                             v2coSS,
                             v3coSS,
                             uv1co,
                             uv2co,
                             uv3co,
                             uv_clip,
                             &uv_clip_tot,
                             do_backfacecull || ps->do_occlude);

    /* Sometimes this happens, better just allow for 8 intersections
     * even though there should be max 6 */
#if 0
    if (uv_clip_tot > 6) {
      printf("this should never happen! %d\n", uv_clip_tot);
    }
#endif

    if (pixel_bounds_array(uv_clip, &bounds_px, ibuf->x, ibuf->y, uv_clip_tot)) {
#if 0
      project_paint_undo_tiles_init(
          &bounds_px, ps->projImages + image_index, tmpibuf, tile_width, threaded, ps->do_masking);
#endif
      /* clip face and */

      has_isect = 0;
      for (y = bounds_px.ymin; y < bounds_px.ymax; y++) {
        // uv[1] = (((float)y) + 0.5f) / (float)ibuf->y;
        /* use pixel offset UV coords instead */
        uv[1] = (float)y / ibuf_yf;

        has_x_isect = 0;
        for (x = bounds_px.xmin; x < bounds_px.xmax; x++) {
          // uv[0] = (((float)x) + 0.5f) / ibuf->x;
          /* use pixel offset UV coords instead */
          uv[0] = (float)x / ibuf_xf;

          /* Note about IsectPoly2Df_twoside, checking the face or uv flipping doesn't work,
           * could check the poly direction but better to do this */
          if ((do_backfacecull == true && IsectPoly2Df(uv, uv_clip, uv_clip_tot)) ||
              (do_backfacecull == false && IsectPoly2Df_twoside(uv, uv_clip, uv_clip_tot))) {

            has_x_isect = has_isect = 1;

            if (is_ortho) {
              screen_px_from_ortho(
                  uv, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, pixelScreenCo, w);
            }
            else {
              screen_px_from_persp(
                  uv, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, pixelScreenCo, w);
            }

            /* A pity we need to get the world-space pixel location here
             * because it is a relatively expensive operation. */
            if (do_clip || do_3d_mapping) {
              interp_v3_v3v3v3(wco,
                               ps->mvert_eval[lt_vtri[0]].co,
                               ps->mvert_eval[lt_vtri[1]].co,
                               ps->mvert_eval[lt_vtri[2]].co,
                               w);
              if (do_clip && ED_view3d_clipping_test(ps->rv3d, wco, true)) {
                /* Watch out that no code below this needs to run */
                continue;
              }
            }

            /* Is this UV visible from the view? - raytrace */
            /* project_paint_PickFace is less complex, use for testing */
            // if (project_paint_PickFace(ps, pixelScreenCo, w, &side) == tri_index) {
            if ((ps->do_occlude == false) ||
                !project_bucket_point_occluded(ps, bucketFaceNodes, tri_index, pixelScreenCo)) {
              mask = project_paint_uvpixel_mask(ps, tri_index, w);

              if (mask > 0.0f) {
                BLI_linklist_prepend_arena(
                    bucketPixelNodes,
                    project_paint_uvpixel_init(
                        ps, arena, &tinf, x, y, mask, tri_index, pixelScreenCo, wco, w),
                    arena);
              }
            }
          }
          //#if 0
          else if (has_x_isect) {
            /* assuming the face is not a bow-tie - we know we can't intersect again on the X */
            break;
          }
          //#endif
        }

#if 0 /* TODO: investigate why this doesn't work sometimes! it should! */
        /* no intersection for this entire row,
         * after some intersection above means we can quit now */
        if (has_x_isect == 0 && has_isect) {
          break;
        }
#endif
      }
    }
  }

#ifndef PROJ_DEBUG_NOSEAMBLEED
  if (ps->seam_bleed_px > 0.0f && !(ps->faceSeamFlags[tri_index] & PROJ_FACE_DEGENERATE)) {
    int face_seam_flag;

    if (threaded) {
      /* Other threads could be modifying these vars. */
      BLI_thread_lock(LOCK_CUSTOM1);
    }

    face_seam_flag = ps->faceSeamFlags[tri_index];

    /* are any of our edges un-initialized? */
    if ((face_seam_flag & PROJ_FACE_SEAM_INIT0) == 0 ||
        (face_seam_flag & PROJ_FACE_SEAM_INIT1) == 0 ||
        (face_seam_flag & PROJ_FACE_SEAM_INIT2) == 0) {
      project_face_seams_init(ps, arena, tri_index, 0, true, ibuf->x, ibuf->y);
      face_seam_flag = ps->faceSeamFlags[tri_index];
#  if 0
      printf("seams - %d %d %d %d\n",
             flag & PROJ_FACE_SEAM0,
             flag & PROJ_FACE_SEAM1,
             flag & PROJ_FACE_SEAM2);
#  endif
    }

    if ((face_seam_flag & (PROJ_FACE_SEAM0 | PROJ_FACE_SEAM1 | PROJ_FACE_SEAM2)) == 0) {

      if (threaded) {
        /* Other threads could be modifying these vars. */
        BLI_thread_unlock(LOCK_CUSTOM1);
      }
    }
    else {
      /* we have a seam - deal with it! */

      /* Inset face coords.
       * - screen-space in orthographic view.
       * - world-space in perspective view.
       */
      float insetCos[3][3];

      /* Vertex screen-space coords. */
      const float *vCoSS[3];

      /* Store the screen-space coords of the face,
       * clipped by the bucket's screen aligned rectangle. */
      float bucket_clip_edges[2][2];
      float edge_verts_inset_clip[2][3];
      /* face edge pairs - loop through these:
       * ((0,1), (1,2), (2,3), (3,0)) or ((0,1), (1,2), (2,0)) for a tri */
      int fidx1, fidx2;

      float seam_subsection[4][2];
      float fac1, fac2;

      /* Pixelspace UVs. */
      float lt_puv[3][2];

      lt_puv[0][0] = lt_uv_pxoffset[0][0] * ibuf->x;
      lt_puv[0][1] = lt_uv_pxoffset[0][1] * ibuf->y;

      lt_puv[1][0] = lt_uv_pxoffset[1][0] * ibuf->x;
      lt_puv[1][1] = lt_uv_pxoffset[1][1] * ibuf->y;

      lt_puv[2][0] = lt_uv_pxoffset[2][0] * ibuf->x;
      lt_puv[2][1] = lt_uv_pxoffset[2][1] * ibuf->y;

      if ((ps->faceSeamFlags[tri_index] & PROJ_FACE_SEAM0) ||
          (ps->faceSeamFlags[tri_index] & PROJ_FACE_SEAM1) ||
          (ps->faceSeamFlags[tri_index] & PROJ_FACE_SEAM2)) {
        uv_image_outset(ps, lt_uv_pxoffset, lt_puv, tri_index, ibuf->x, ibuf->y);
      }

      /* ps->loopSeamUVs can't be modified when threading, now this is done we can unlock. */
      if (threaded) {
        /* Other threads could be modifying these vars */
        BLI_thread_unlock(LOCK_CUSTOM1);
      }

      vCoSS[0] = ps->screenCoords[lt_vtri[0]];
      vCoSS[1] = ps->screenCoords[lt_vtri[1]];
      vCoSS[2] = ps->screenCoords[lt_vtri[2]];

      /* PROJ_FACE_SCALE_SEAM must be slightly less than 1.0f */
      if (is_ortho) {
        scale_tri(insetCos, vCoSS, PROJ_FACE_SCALE_SEAM);
      }
      else {
        scale_tri(insetCos, vCo, PROJ_FACE_SCALE_SEAM);
      }

      for (fidx1 = 0; fidx1 < 3; fidx1++) {
        /* next fidx in the face (0,1,2) -> (1,2,0) */
        fidx2 = (fidx1 == 2) ? 0 : fidx1 + 1;

        if ((face_seam_flag & (1 << fidx1)) && /* 1<<fidx1 -> PROJ_FACE_SEAM# */
            line_clip_rect2f(clip_rect,
                             bucket_bounds,
                             vCoSS[fidx1],
                             vCoSS[fidx2],
                             bucket_clip_edges[0],
                             bucket_clip_edges[1])) {
          /* Avoid div by zero. */
          if (len_squared_v2v2(vCoSS[fidx1], vCoSS[fidx2]) > FLT_EPSILON) {
            uint loop_idx = ps->mlooptri_eval[tri_index].tri[fidx1];
            LoopSeamData *seam_data = &ps->loopSeamData[loop_idx];
            float(*seam_uvs)[2] = seam_data->seam_uvs;

            if (is_ortho) {
              fac1 = line_point_factor_v2(bucket_clip_edges[0], vCoSS[fidx1], vCoSS[fidx2]);
              fac2 = line_point_factor_v2(bucket_clip_edges[1], vCoSS[fidx1], vCoSS[fidx2]);
            }
            else {
              fac1 = screen_px_line_point_factor_v2_persp(
                  ps, bucket_clip_edges[0], vCo[fidx1], vCo[fidx2]);
              fac2 = screen_px_line_point_factor_v2_persp(
                  ps, bucket_clip_edges[1], vCo[fidx1], vCo[fidx2]);
            }

            interp_v2_v2v2(seam_subsection[0], lt_uv_pxoffset[fidx1], lt_uv_pxoffset[fidx2], fac1);
            interp_v2_v2v2(seam_subsection[1], lt_uv_pxoffset[fidx1], lt_uv_pxoffset[fidx2], fac2);

            interp_v2_v2v2(seam_subsection[2], seam_uvs[0], seam_uvs[1], fac2);
            interp_v2_v2v2(seam_subsection[3], seam_uvs[0], seam_uvs[1], fac1);

            /* if the bucket_clip_edges values Z values was kept we could avoid this
             * Inset needs to be added so occlusion tests won't hit adjacent faces */
            interp_v3_v3v3(edge_verts_inset_clip[0], insetCos[fidx1], insetCos[fidx2], fac1);
            interp_v3_v3v3(edge_verts_inset_clip[1], insetCos[fidx1], insetCos[fidx2], fac2);

            if (pixel_bounds_uv(seam_subsection, &bounds_px, ibuf->x, ibuf->y)) {
              /* bounds between the seam rect and the uvspace bucket pixels */

              has_isect = 0;
              for (y = bounds_px.ymin; y < bounds_px.ymax; y++) {
                // uv[1] = (((float)y) + 0.5f) / (float)ibuf->y;
                /* use offset uvs instead */
                uv[1] = (float)y / ibuf_yf;

                has_x_isect = 0;
                for (x = bounds_px.xmin; x < bounds_px.xmax; x++) {
                  const float puv[2] = {(float)x, (float)y};
                  bool in_bounds;
                  // uv[0] = (((float)x) + 0.5f) / (float)ibuf->x;
                  /* use offset uvs instead */
                  uv[0] = (float)x / ibuf_xf;

                  /* test we're inside uvspace bucket and triangle bounds */
                  if (equals_v2v2(seam_uvs[0], seam_uvs[1])) {
                    in_bounds = isect_point_tri_v2(uv, UNPACK3(seam_subsection));
                  }
                  else {
                    in_bounds = isect_point_quad_v2(uv, UNPACK4(seam_subsection));
                  }

                  if (in_bounds) {
                    if ((seam_data->corner_dist_sq[0] > 0.0f) &&
                        (len_squared_v2v2(puv, seam_data->seam_puvs[0]) <
                         seam_data->corner_dist_sq[0]) &&
                        (len_squared_v2v2(puv, lt_puv[fidx1]) > ps->seam_bleed_px_sq)) {
                      in_bounds = false;
                    }
                    else if ((seam_data->corner_dist_sq[1] > 0.0f) &&
                             (len_squared_v2v2(puv, seam_data->seam_puvs[1]) <
                              seam_data->corner_dist_sq[1]) &&
                             (len_squared_v2v2(puv, lt_puv[fidx2]) > ps->seam_bleed_px_sq)) {
                      in_bounds = false;
                    }
                  }

                  if (in_bounds) {
                    float pixel_on_edge[4];
                    float fac;

                    if (is_ortho) {
                      screen_px_from_ortho(
                          uv, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, pixelScreenCo, w);
                    }
                    else {
                      screen_px_from_persp(
                          uv, v1coSS, v2coSS, v3coSS, uv1co, uv2co, uv3co, pixelScreenCo, w);
                    }

                    /* We need the coord of the pixel on the edge, for the occlusion query. */
                    fac = resolve_quad_u_v2(uv, UNPACK4(seam_subsection));
                    interp_v3_v3v3(
                        pixel_on_edge, edge_verts_inset_clip[0], edge_verts_inset_clip[1], fac);

                    if (!is_ortho) {
                      pixel_on_edge[3] = 1.0f;
                      /* cast because of const */
                      mul_m4_v4((float(*)[4])ps->projectMat, pixel_on_edge);
                      pixel_on_edge[0] = (float)(ps->winx * 0.5f) +
                                         (ps->winx * 0.5f) * pixel_on_edge[0] / pixel_on_edge[3];
                      pixel_on_edge[1] = (float)(ps->winy * 0.5f) +
                                         (ps->winy * 0.5f) * pixel_on_edge[1] / pixel_on_edge[3];
                      /* Use the depth for bucket point occlusion */
                      pixel_on_edge[2] = pixel_on_edge[2] / pixel_on_edge[3];
                    }

                    if ((ps->do_occlude == false) ||
                        !project_bucket_point_occluded(
                            ps, bucketFaceNodes, tri_index, pixel_on_edge)) {
                      /* A pity we need to get the world-space pixel location here
                       * because it is a relatively expensive operation. */
                      if (do_clip || do_3d_mapping) {
                        interp_v3_v3v3v3(wco, vCo[0], vCo[1], vCo[2], w);

                        if (do_clip && ED_view3d_clipping_test(ps->rv3d, wco, true)) {
                          /* Watch out that no code below
                           * this needs to run */
                          continue;
                        }
                      }

                      mask = project_paint_uvpixel_mask(ps, tri_index, w);

                      if (mask > 0.0f) {
                        BLI_linklist_prepend_arena(
                            bucketPixelNodes,
                            project_paint_uvpixel_init(
                                ps, arena, &tinf, x, y, mask, tri_index, pixelScreenCo, wco, w),
                            arena);
                      }
                    }
                  }
                  else if (has_x_isect) {
                    /* assuming the face is not a bow-tie - we know
                     * we can't intersect again on the X */
                    break;
                  }
                }

#  if 0 /* TODO: investigate why this doesn't work sometimes! it should! */
                /* no intersection for this entire row,
                 * after some intersection above means we can quit now */
                if (has_x_isect == 0 && has_isect) {
                  break;
                }
#  endif
              }
            }
          }
        }
      }
    }
  }
#else
  UNUSED_VARS(vCo, threaded);
#endif /* PROJ_DEBUG_NOSEAMBLEED */
}

/**
 * Takes floating point screen-space min/max and
 * returns int min/max to be used as indices for ps->bucketRect, ps->bucketFlags
 */
