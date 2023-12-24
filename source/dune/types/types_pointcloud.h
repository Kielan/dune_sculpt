#pragma once

#include "types_id.h"
#include "types_customdata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PointCloud {
  Id id;
  struct AnimData *adt; /* animation data (must be immediately after id) */

  int flag;
  int _pad1[1];

  /* Geometry */
  float (*co)[3];
  float *radius;
  int totpoint;
  int _pad2[1];

  /* Custom Data */
  struct CustomData pdata;
  int attributes_active_index;
  int _pad4;

  /* Material */
  struct Material **mat;
  short totcol;
  short _pad3[3];

  /* Drw Cache */
  void *batch_cache;
} PointCloud;

/* PointCloud.flag */
enum {
  PT_DS_EXPAND = (1 << 0),
};

/* Only one material supported currently. */
#define POINTCLOUD_MATERIAL_NR 1

#ifdef __cplusplus
}
#endif
