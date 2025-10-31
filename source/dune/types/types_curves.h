#pragma once

#include "types_id.h"
#include "types_customdata.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
namespace dune {
class CurvesGeomRuntime;
}  // namespace dune
using CurvesGeomRuntimeHandle = dune::CurvesGeomRuntime;
#else
typedef struct CurvesGeomRuntimeHandle CurvesGeomRuntimeHandle;
#endif

typedef enum CurveType {
  CURVE_TYPE_CATMULL_ROM = 0,
  CURVE_TYPE_POLY = 1,
  CURVE_TYPE_BEZIER = 2,
  CURVE_TYPE_NURBS = 3,
} CurveType;

typedef enum HandleType {
  /* The handle can be moved anywhere, and doesn't influence the point's other handle. */
  BEZIER_HANDLE_FREE = 0,
  /* The location is automatically calculated to be smooth. */
  BEZIER_HANDLE_AUTO = 1,
  /* The location is calculated to point to the next/previous control point. */
  BEZIER_HANDLE_VECTOR = 2,
  /* The location is constrained to point in the opposite direction as the other handle. */
  BEZIER_HANDLE_ALIGN = 3,
} HandleType;

/* Method used to calc a NURBS curve's knot vector. */
typedef enum KnotsMode {
  NURBS_KNOT_MODE_NORMAL = 0,
  NURBS_KNOT_MODE_ENDPOINT = 1,
  NURBS_KNOT_MODE_BEZIER = 2,
  NURBS_KNOT_MODE_ENDPOINT_BEZIER = 3,
} KnotsMode;

/* A reusable data struct for geometry consisting of many curves. All control point data is
 * stored contiguously for better efficiency. Data for each curve is stored as a slice of the
 * main point_data array.
 *
 * The data struct is meant to be embedded in other data-blocks to allow reusing
 * curve-processing algorithms for multiple Dune data-block types. */
typedef struct CurvesGeometry {
  /* A runtime ptr to the "position" attribute data.
   * This data is owned by point_data. */
  float (*position)[3];
  /* A runtime ptr to the "radius" attribute data.
   * This data is owned by point_data. */
  float *radius;

  /* The type of each curve. CurveType.
   * This data is owned by curve_data.  */
  int8_t *curve_type;

  /* The start index of each curve in the point data. The size of each curve can be calculated by
   * subtracting the offset from the next offset. That is valid even for the last curve because
   * this array is allocated with a length one larger than the number of splines. This is allowed
   * to be null when there are no curves.
   *
   * This is *not* stored in CustomData because its size is one larger than curve_data. */
  int *curve_offsets;

  /* All attributes stored on control points (ATTR_DOMAIN_POINT).
   * This might not contain a layer for positions if there are no points. */
  CustomData point_data;

  /* All attributes stored on curves (ATTR_DOMAIN_CURVE). */
  CustomData curve_data;

  /* The total num of ctrl points in all curves. */
  int point_size;
  /* The num of curves in the data-block */
  int curve_size;

  /* Runtime data for curves, stored as a pointer to allow defining this as a C++ class. */
  CurvesGeometryRuntimeHandle *runtime;
} CurvesGeometry;

typedef struct Curves {
  Id id;
  /* Anim data (must be immediately after id). */
  struct AnimData *adt;

  CurvesGeometry geometry;

  int flag;
  int attributes_active_index;

  /* Materials. */
  struct Material **mat;
  short totcol;
  short _pad2[3];

  /* Used as base mesh when curves represent e.g. hair or fur. This surface is used in edit modes.
   * When set, the curves will have attributes that indicate a position on this surface. This is
   * used for deforming the curves when the surface is deformed dynamically.
   * This is expected to be a mesh ob. */
  struct Ob *surface;

  /* Drw Cache. */
  void *batch_cache;
} Curves;

/* Curves.flag */
enum {
  HA_DS_EXPAND = (1 << 0),
};

/* Only one material supported currently. */
#define CURVES_MATERIAL_NR 1

#ifdef __cplusplus
}
#endif
