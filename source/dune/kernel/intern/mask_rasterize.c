/**
 *
 * This module exposes a rasterizer that works as a black box - implementation details
 * are confined to this file.
 *
 * The basic method to access is:
 * - create & initialize a handle from a #Mask datablock.
 * - execute pixel lookups.
 * - free the handle.
 *
 * This file is admittedly a bit confusticated,
 * in quite few areas speed was chosen over readability,
 * though it is commented - so shouldn't be so hard to see what's going on.
 *
 * Implementation:
 *
 * To rasterize the mask its converted into geometry that use a ray-cast for each pixel lookup.
 *
 * Initially 'kdopbvh' was used but this ended up being too slow.
 *
 * To gain some extra speed we take advantage of a few shortcuts
 * that can be made rasterizing masks specifically.
 *
 * - All triangles are known to be completely white -
 *   so no depth check is done on triangle intersection.
 * - All quads are known to be feather outlines -
 *   the 1 and 0 depths are known by the vertex order in the quad,
 * - There is no color - just a value for each mask pixel.
 * - The mask spacial structure always maps to space 0-1 on X and Y axis.
 * - Bucketing is used to speed up lookups for geometry.
 *
 * Other Details:
 * - used unsigned values all over for some extra speed on some arch's.
 * - anti-aliasing is faked, just ensuring at least one pixel feather - avoids oversampling.
 * - initializing the spacial structure doesn't need to be as optimized as pixel lookups are.
 * - mask lookups need not be pixel aligned so any sub-pixel values from x/y (0 - 1), can be found.
 *   (perhaps masks can be used as a vector texture in 3D later on)
 * Currently, to build the spacial structure we have to calculate
 * the total number of faces ahead of time.
 *
 * This is getting a bit complicated with the addition of unfilled splines and end capping -
 * If large changes are needed here we would be better off using an iterable
 * BLI_mempool for triangles and converting to a contiguous array afterwards.
 *
 * - Campbell
 */

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "STRUCTS_mask_types.h"
#include "STRUCTS_scene_types.h"
#include "STRUCTS_vec_types.h"

#include "LIB_memarena.h"
#include "LIB_scanfill.h"
#include "LIB_utildefines.h"

#include "LIB_linklist.h"
#include "LIB_listbase.h"
#include "LIB_math.h"
#include "LIB_rect.h"
#include "LIB_task.h"

#include "KERNEL_mask.h"

#include "LIB_strict_flags.h"

/* this is rather and annoying hack, use define to isolate it.
 * problem is caused by scanfill removing edges on us. */
#define USE_SCANFILL_EDGE_WORKAROUND

#define SPLINE_RESOL_CAP_PER_PIXEL 2
#define SPLINE_RESOL_CAP_MIN 8
#define SPLINE_RESOL_CAP_MAX 64

/* found this gives best performance for high detail masks, values between 2 and 8 work best */
#define BUCKET_PIXELS_PER_CELL 4

#define SF_EDGE_IS_BOUNDARY 0xff
#define SF_KEYINDEX_TEMP_ID ((unsigned int)-1)

#define TRI_TERMINATOR_ID ((unsigned int)-1)
#define TRI_VERT ((unsigned int)-1)

/* for debugging add... */
#ifndef NDEBUG
// printf("%u %u %u %u\n", _t[0], _t[1], _t[2], _t[3]);
#  define FACE_ASSERT(face, vert_max) \
    { \
      unsigned int *_t = face; \
      BLI_assert(_t[0] < vert_max); \
      BLI_assert(_t[1] < vert_max); \
      BLI_assert(_t[2] < vert_max); \
      BLI_assert(_t[3] < vert_max || _t[3] == TRI_VERT); \
    } \
    (void)0
#else
/* do nothing */
#  define FACE_ASSERT(face, vert_max)
#endif

static CLG_LogRef LOG = {"bke.mask_rasterize"};

static void rotate_point_v2(
    float r_p[2], const float p[2], const float cent[2], const float angle, const float asp[2])
{
  const float s = sinf(angle);
  const float c = cosf(angle);
  float p_new[2];

  /* translate point back to origin */
  r_p[0] = (p[0] - cent[0]) / asp[0];
  r_p[1] = (p[1] - cent[1]) / asp[1];

  /* rotate point */
  p_new[0] = ((r_p[0] * c) - (r_p[1] * s)) * asp[0];
  p_new[1] = ((r_p[0] * s) + (r_p[1] * c)) * asp[1];

  /* translate point back */
  r_p[0] = p_new[0] + cent[0];
  r_p[1] = p_new[1] + cent[1];
}

BLI_INLINE unsigned int clampis_uint(const unsigned int v,
                                     const unsigned int min,
                                     const unsigned int max)
{
  return v < min ? min : (v > max ? max : v);
}

/* --------------------------------------------------------------------- */
/* local structs for mask rasterizing                                    */
/* --------------------------------------------------------------------- */

/**
 * A single #MaskRasterHandle contains multiple #MaskRasterLayer's,
 * each #MaskRasterLayer does its own lookup which contributes to
 * the final pixel with its own blending mode and the final pixel
 * is blended between these.
 */

/* internal use only */
typedef struct MaskRasterLayer {
  /* geometry */
  unsigned int face_tot;
  unsigned int (*face_array)[4]; /* access coords tri/quad */
  float (*face_coords)[3];       /* xy, z 0-1 (1.0 == filled) */

  /* 2d bounds (to quickly skip bucket lookup) */
  rctf bounds;

  /* buckets */
  unsigned int **buckets_face;
  /* cache divide and subtract */
  float buckets_xy_scalar[2]; /* (1.0 / (buckets_width + FLT_EPSILON)) * buckets_x */
  unsigned int buckets_x;
  unsigned int buckets_y;

  /* copied direct from #MaskLayer.--- */
  /* blending options */
  float alpha;
  char blend;
  char blend_flag;
  char falloff;

} MaskRasterLayer;

typedef struct MaskRasterSplineInfo {
  /* body of the spline */
  unsigned int vertex_offset;
  unsigned int vertex_total;

  /* capping for non-filled, non cyclic splines */
  unsigned int vertex_total_cap_head;
  unsigned int vertex_total_cap_tail;

  bool is_cyclic;
} MaskRasterSplineInfo;

/**
 * opaque local struct for mask pixel lookup, each MaskLayer needs one of these
 */
struct MaskRasterHandle {
  MaskRasterLayer *layers;
  unsigned int layers_tot;

  /* 2d bounds (to quickly skip bucket lookup) */
  rctf bounds;
};

/* --------------------------------------------------------------------- */
/* alloc / free functions                                                */
/* --------------------------------------------------------------------- */

MaskRasterHandle *BKE_maskrasterize_handle_new(void)
{
  MaskRasterHandle *mr_handle;

  mr_handle = MEM_callocN(sizeof(MaskRasterHandle), "MaskRasterHandle");

  return mr_handle;
}

void BKE_maskrasterize_handle_free(MaskRasterHandle *mr_handle)
{
  const unsigned int layers_tot = mr_handle->layers_tot;
  MaskRasterLayer *layer = mr_handle->layers;

  for (uint i = 0; i < layers_tot; i++, layer++) {

    if (layer->face_array) {
      MEM_freeN(layer->face_array);
    }

    if (layer->face_coords) {
      MEM_freeN(layer->face_coords);
    }

    if (layer->buckets_face) {
      const unsigned int bucket_tot = layer->buckets_x * layer->buckets_y;
      unsigned int bucket_index;
      for (bucket_index = 0; bucket_index < bucket_tot; bucket_index++) {
        unsigned int *face_index = layer->buckets_face[bucket_index];
        if (face_index) {
          MEM_freeN(face_index);
        }
      }

      MEM_freeN(layer->buckets_face);
    }
  }

  MEM_freeN(mr_handle->layers);
  MEM_freeN(mr_handle);
}

static void maskrasterize_spline_differentiate_point_outset(float (*diff_feather_points)[2],
                                                            float (*diff_points)[2],
                                                            const unsigned int tot_diff_point,
                                                            const float ofs,
                                                            const bool do_test)
{
  unsigned int k_prev = tot_diff_point - 2;
  unsigned int k_curr = tot_diff_point - 1;
  unsigned int k_next = 0;

  unsigned int k;

  float d_prev[2];
  float d_next[2];
  float d[2];

  const float *co_prev;
  const float *co_curr;
  const float *co_next;

  const float ofs_squared = ofs * ofs;

  co_prev = diff_points[k_prev];
  co_curr = diff_points[k_curr];
  co_next = diff_points[k_next];

  /* precalc */
  sub_v2_v2v2(d_prev, co_prev, co_curr);
  normalize_v2(d_prev);

  for (k = 0; k < tot_diff_point; k++) {

    /* co_prev = diff_points[k_prev]; */ /* precalc */
    co_curr = diff_points[k_curr];
    co_next = diff_points[k_next];

    // sub_v2_v2v2(d_prev, co_prev, co_curr); /* precalc */
    sub_v2_v2v2(d_next, co_curr, co_next);

    // normalize_v2(d_prev); /* precalc */
    normalize_v2(d_next);

    if ((do_test == false) ||
        (len_squared_v2v2(diff_feather_points[k], diff_points[k]) < ofs_squared)) {

      add_v2_v2v2(d, d_prev, d_next);

      normalize_v2(d);

      diff_feather_points[k][0] = diff_points[k][0] + (d[1] * ofs);
      diff_feather_points[k][1] = diff_points[k][1] + (-d[0] * ofs);
    }

    /* use next iter */
    copy_v2_v2(d_prev, d_next);

    /* k_prev = k_curr; */ /* precalc */
    k_curr = k_next;
    k_next++;
  }
}

/* this function is not exact, sometimes it returns false positives,
 * the main point of it is to clear out _almost_ all bucket/face non-intersections,
 * returning true in corner cases is ok but missing an intersection is NOT.
 *
 * method used
 * - check if the center of the buckets bounding box is intersecting the face
 * - if not get the max radius to a corner of the bucket and see how close we
 *   are to any of the triangle edges.
 */
static bool layer_bucket_isect_test(const MaskRasterLayer *layer,
                                    unsigned int face_index,
                                    const unsigned int bucket_x,
                                    const unsigned int bucket_y,
                                    const float bucket_size_x,
                                    const float bucket_size_y,
                                    const float bucket_max_rad_squared)
{
  unsigned int *face = layer->face_array[face_index];
  float(*cos)[3] = layer->face_coords;

  const float xmin = layer->bounds.xmin + (bucket_size_x * (float)bucket_x);
  const float ymin = layer->bounds.ymin + (bucket_size_y * (float)bucket_y);
  const float xmax = xmin + bucket_size_x;
  const float ymax = ymin + bucket_size_y;

  const float cent[2] = {(xmin + xmax) * 0.5f, (ymin + ymax) * 0.5f};

  if (face[3] == TRI_VERT) {
    const float *v1 = cos[face[0]];
    const float *v2 = cos[face[1]];
    const float *v3 = cos[face[2]];

    if (isect_point_tri_v2(cent, v1, v2, v3)) {
      return true;
    }

    if ((dist_squared_to_line_segment_v2(cent, v1, v2) < bucket_max_rad_squared) ||
        (dist_squared_to_line_segment_v2(cent, v2, v3) < bucket_max_rad_squared) ||
        (dist_squared_to_line_segment_v2(cent, v3, v1) < bucket_max_rad_squared)) {
      return true;
    }

    // printf("skip tri\n");
    return false;
  }

  const float *v1 = cos[face[0]];
  const float *v2 = cos[face[1]];
  const float *v3 = cos[face[2]];
  const float *v4 = cos[face[3]];

  if (isect_point_tri_v2(cent, v1, v2, v3)) {
    return true;
  }
  if (isect_point_tri_v2(cent, v1, v3, v4)) {
    return true;
  }

  if ((dist_squared_to_line_segment_v2(cent, v1, v2) < bucket_max_rad_squared) ||
      (dist_squared_to_line_segment_v2(cent, v2, v3) < bucket_max_rad_squared) ||
      (dist_squared_to_line_segment_v2(cent, v3, v4) < bucket_max_rad_squared) ||
      (dist_squared_to_line_segment_v2(cent, v4, v1) < bucket_max_rad_squared)) {
    return true;
  }

  // printf("skip quad\n");
  return false;
}

static void layer_bucket_init_dummy(MaskRasterLayer *layer)
{
  layer->face_tot = 0;
  layer->face_coords = NULL;
  layer->face_array = NULL;

  layer->buckets_x = 0;
  layer->buckets_y = 0;

  layer->buckets_xy_scalar[0] = 0.0f;
  layer->buckets_xy_scalar[1] = 0.0f;

  layer->buckets_face = NULL;

  BLI_rctf_init(&layer->bounds, -1.0f, -1.0f, -1.0f, -1.0f);
}

static void layer_bucket_init(MaskRasterLayer *layer, const float pixel_size)
{
  MemArena *arena = BLI_memarena_new(MEM_SIZE_OPTIMAL(1 << 16), __func__);

  const float bucket_dim_x = BLI_rctf_size_x(&layer->bounds);
  const float bucket_dim_y = BLI_rctf_size_y(&layer->bounds);

  layer->buckets_x = (unsigned int)((bucket_dim_x / pixel_size) / (float)BUCKET_PIXELS_PER_CELL);
  layer->buckets_y = (unsigned int)((bucket_dim_y / pixel_size) / (float)BUCKET_PIXELS_PER_CELL);

  //      printf("bucket size %ux%u\n", layer->buckets_x, layer->buckets_y);

  CLAMP(layer->buckets_x, 8, 512);
  CLAMP(layer->buckets_y, 8, 512);

  layer->buckets_xy_scalar[0] = (1.0f / (bucket_dim_x + FLT_EPSILON)) * (float)layer->buckets_x;
  layer->buckets_xy_scalar[1] = (1.0f / (bucket_dim_y + FLT_EPSILON)) * (float)layer->buckets_y;

  {
    /* width and height of each bucket */
    const float bucket_size_x = (bucket_dim_x + FLT_EPSILON) / (float)layer->buckets_x;
    const float bucket_size_y = (bucket_dim_y + FLT_EPSILON) / (float)layer->buckets_y;
    const float bucket_max_rad = (max_ff(bucket_size_x, bucket_size_y) * (float)M_SQRT2) +
                                 FLT_EPSILON;
    const float bucket_max_rad_squared = bucket_max_rad * bucket_max_rad;

    unsigned int *face = &layer->face_array[0][0];
    float(*cos)[3] = layer->face_coords;

    const unsigned int bucket_tot = layer->buckets_x * layer->buckets_y;
    LinkNode **bucketstore = MEM_callocN(bucket_tot * sizeof(LinkNode *), __func__);
    unsigned int *bucketstore_tot = MEM_callocN(bucket_tot * sizeof(unsigned int), __func__);

    unsigned int face_index;

    for (face_index = 0; face_index < layer->face_tot; face_index++, face += 4) {
      float xmin;
      float xmax;
      float ymin;
      float ymax;

      if (face[3] == TRI_VERT) {
        const float *v1 = cos[face[0]];
        const float *v2 = cos[face[1]];
        const float *v3 = cos[face[2]];

        xmin = min_ff(v1[0], min_ff(v2[0], v3[0]));
        xmax = max_ff(v1[0], max_ff(v2[0], v3[0]));
        ymin = min_ff(v1[1], min_ff(v2[1], v3[1]));
        ymax = max_ff(v1[1], max_ff(v2[1], v3[1]));
      }
      else {
        const float *v1 = cos[face[0]];
        const float *v2 = cos[face[1]];
        const float *v3 = cos[face[2]];
        const float *v4 = cos[face[3]];

        xmin = min_ff(v1[0], min_ff(v2[0], min_ff(v3[0], v4[0])));
        xmax = max_ff(v1[0], max_ff(v2[0], max_ff(v3[0], v4[0])));
        ymin = min_ff(v1[1], min_ff(v2[1], min_ff(v3[1], v4[1])));
        ymax = max_ff(v1[1], max_ff(v2[1], max_ff(v3[1], v4[1])));
      }

      /* not essential but may as will skip any faces outside the view */
      if (!((xmax < 0.0f) || (ymax < 0.0f) || (xmin > 1.0f) || (ymin > 1.0f))) {

        CLAMP(xmin, 0.0f, 1.0f);
        CLAMP(ymin, 0.0f, 1.0f);
        CLAMP(xmax, 0.0f, 1.0f);
        CLAMP(ymax, 0.0f, 1.0f);

        {
          unsigned int xi_min = (unsigned int)((xmin - layer->bounds.xmin) *
                                               layer->buckets_xy_scalar[0]);
          unsigned int xi_max = (unsigned int)((xmax - layer->bounds.xmin) *
                                               layer->buckets_xy_scalar[0]);
          unsigned int yi_min = (unsigned int)((ymin - layer->bounds.ymin) *
                                               layer->buckets_xy_scalar[1]);
          unsigned int yi_max = (unsigned int)((ymax - layer->bounds.ymin) *
                                               layer->buckets_xy_scalar[1]);
          void *face_index_void = POINTER_FROM_UINT(face_index);

          unsigned int xi, yi;

          /* this should _almost_ never happen but since it can in extreme cases,
           * we have to clamp the values or we overrun the buffer and crash */
          if (xi_min >= layer->buckets_x) {
            xi_min = layer->buckets_x - 1;
          }
          if (xi_max >= layer->buckets_x) {
            xi_max = layer->buckets_x - 1;
          }
          if (yi_min >= layer->buckets_y) {
            yi_min = layer->buckets_y - 1;
          }
          if (yi_max >= layer->buckets_y) {
            yi_max = layer->buckets_y - 1;
          }

          for (yi = yi_min; yi <= yi_max; yi++) {
            unsigned int bucket_index = (layer->buckets_x * yi) + xi_min;
            for (xi = xi_min; xi <= xi_max; xi++, bucket_index++) {
              /* correct but do in outer loop */
              // unsigned int bucket_index = (layer->buckets_x * yi) + xi;

              BLI_assert(xi < layer->buckets_x);
              BLI_assert(yi < layer->buckets_y);
              BLI_assert(bucket_index < bucket_tot);

              /* Check if the bucket intersects with the face. */
              /* NOTE: there is a trade off here since checking box/tri intersections isn't as
               * optimal as it could be, but checking pixels against faces they will never
               * intersect with is likely the greater slowdown here -
               * so check if the cell intersects the face. */
              if (layer_bucket_isect_test(layer,
                                          face_index,
                                          xi,
                                          yi,
                                          bucket_size_x,
                                          bucket_size_y,
                                          bucket_max_rad_squared)) {
                BLI_linklist_prepend_arena(&bucketstore[bucket_index], face_index_void, arena);
                bucketstore_tot[bucket_index]++;
              }
            }
          }
        }
      }
    }

    if (1) {
      /* now convert linknodes into arrays for faster per pixel access */
      unsigned int **buckets_face = MEM_mallocN(bucket_tot * sizeof(*buckets_face), __func__);
      unsigned int bucket_index;

      for (bucket_index = 0; bucket_index < bucket_tot; bucket_index++) {
        if (bucketstore_tot[bucket_index]) {
          unsigned int *bucket = MEM_mallocN(
              (bucketstore_tot[bucket_index] + 1) * sizeof(unsigned int), __func__);
          LinkNode *bucket_node;

          buckets_face[bucket_index] = bucket;

          for (bucket_node = bucketstore[bucket_index]; bucket_node;
               bucket_node = bucket_node->next) {
            *bucket = POINTER_AS_UINT(bucket_node->link);
            bucket++;
          }
          *bucket = TRI_TERMINATOR_ID;
        }
        else {
          buckets_face[bucket_index] = NULL;
        }
      }

      layer->buckets_face = buckets_face;
    }

    MEM_freeN(bucketstore);
    MEM_freeN(bucketstore_tot);
  }

  BLI_memarena_free(arena);
}
