#pragma once

#include "types_id.h"
#include "types_list.h"

/* Notice that we need to have this file although no struct defines.
 * Edge flags and usage flags are used by with scene/ob/pen mod bits, and those values
 * needs to stay consistent throughout. */

/* These flags are used for 1 time calc, not stroke sel afterwards. */
typedef enum eLineartMainFlags {
  LRT_INTERSECTION_AS_CONTOUR = (1 << 0),
  LRT_EVERYTHING_AS_CONTOUR = (1 << 1),
  LRT_ALLOW_DUP_OBS = (1 << 2),
  LRT_ALLOW_OVERLAPPING_EDGES = (1 << 3),
  LRT_ALLOW_CLIPPING_BOUNDARIES = (1 << 4),
  LRT_REMOVE_DOUBLES = (1 << 5),
  LRT_LOOSE_AS_CONTOUR = (1 << 6),
  LRT_PEN_INVERT_SOURCE_VGROUP = (1 << 7),
  LRT_PEN_MATCH_OUTPUT_VGROUP = (1 << 8),
  LRT_FILTER_FACE_MARK = (1 << 9),
  LRT_FILTER_FACE_MARK_INVERT = (1 << 10),
  LRT_FILTER_FACE_MARK_BOUNDARIES = (1 << 11),
  LRT_CHAIN_LOOSE_EDGES = (1 << 12),
  LRT_CHAIN_GEOMETRY_SPACE = (1 << 13),
  LRT_ALLOW_OVERLAP_EDGE_TYPES = (1 << 14),
  LRT_USE_CREASE_ON_SMOOTH_SURFACES = (1 << 15),
  LRT_USE_CREASE_ON_SHARP_EDGES = (1 << 16),
  LRT_USE_CUSTOM_CAMERA = (1 << 17),
  LRT_FILTER_FACE_MARK_KEEP_CONTOUR = (1 << 18),
  LRT_USE_BACK_FACE_CULLING = (1 << 19),
  LRT_USE_IMAGE_BOUNDARY_TRIMMING = (1 << 20),
  LRT_CHAIN_PRESERVE_DETAILS = (1 << 22),
} eLineartMainFlags;

typedef enum eLineartEdgeFlag {
  LRT_EDGE_FLAG_EDGE_MARK = (1 << 0),
  LRT_EDGE_FLAG_CONTOUR = (1 << 1),
  LRT_EDGE_FLAG_CREASE = (1 << 2),
  LRT_EDGE_FLAG_MATERIAL = (1 << 3),
  LRT_EDGE_FLAG_INTERSECTION = (1 << 4),
  LRT_EDGE_FLAG_LOOSE = (1 << 5),
  LRT_EDGE_FLAG_CHAIN_PICKED = (1 << 6),
  LRT_EDGE_FLAG_CLIPPED = (1 << 7),
  /* Limited to 8 bits, DON'T ADD ANYMORE until improvements on the data structure. */
} eLineartEdgeFlag;

#define LRT_EDGE_FLAG_ALL_TYPE 0x3f
