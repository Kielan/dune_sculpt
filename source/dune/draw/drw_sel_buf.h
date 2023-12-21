
#pragma once

#include "lib_sys_types.h" /* for bool and uint */

struct ARgn;
struct Base;
struct Graph;
struct Ob;
struct View3D;
struct rcti;

typedef struct SelIdObData {
  DrwData dd;

  uint drwn_index;
  bool is_drwn;
} SelIdObData;

struct ObOffsets {
  /* For convenience only. */
  union {
    uint offset;
    uint face_start;
  };
  union {
    uint face;
    uint edge_start;
  };
  union {
    uint edge;
    uint vert_start;
  };
  uint vert;
};

typedef struct SelIdCxt {
  /* All cxt obs */
  struct Ob **obs;

  /* Arr with only drwn obs. When a new ob is found w/in the rect,
   * it is added to the end of the list.
   * The list is reset to any viewport or cxt update. */
  struct Ob **obs_drwn;
  struct ObOffsets *index_offsets;
  uint obs_len;
  uint obs_drawn_len;

  /** Total number of element indices `index_offsets[object_drawn_len - 1].vert`. */
  uint index_drawn_len;

  short sel_mode;

  /* rect is used to check which obs whose indexes need to be drawn. */
  rcti last_rect;

  /* To check for updates. */
  float persmat[4][4];
  bool is_dirty;
} SelIdCxt;

/* drw_sel_buf.c */
bool drw_sel_buf_elem_get(uint sel_id, uint *r_elem, uint *r_base_index, char *r_elem_type);
uint drw_sel_buf_cxt_offset_for_ob_elem(struct Graph *graph,
                                        struct Ob *ob,
                                        char elem_type);
/** Main fn to read a block of pixels from the sel frame buf. */
uint *drw_sel_buf_read(struct Graph *graph,
                       struct ARgn *rgn,
                       struct View3D *v3d,
                       const rcti *rect,
                       uint *r_buf_len);
/* param rect: The rectangle to sample indices from (min/max inclusive).
 * returns a lib_bitmap the length of a bitmap_len or NULL on failure */
uint *drw_sel_buf_bitmap_from_rect(struct Graph *graph,
                                   struct ARgn *rgn,
                                   struct View3D *v3d,
                                   const struct rcti *rect,
                                   uint *r_bitmap_len);
/* param center: Circle center.
 * param radius: Circle radius.
 * param r_bitmap_len: Number of indices in the sel id buf.
 * returns a lib_bitmap the length of a r_bitmap_len or NULL on failure. */
uint *drw_sel_buf_bitmap_from_circle(struct Graph *graph,
                                     struct ARgn *rgn,
                                     struct View3D *v3d,
                                     const int center[2],
                                     int radius,
                                     uint *r_bitmap_len);
/* param poly: The polygon coords.
 * param poly_len: Length of the polygon.
 * param rect: Polygon boundaries.
 * returns a lib_bitmap. */
uint *drw_sel_buf_bitmap_from_poly(struct Graph *graph,
                                   struct ARgn *rgn,
                                   struct View3D *v3d,
                                   const int poly[][2],
                                   int poly_len,
                                   const struct rcti *rect,
                                   uint *r_bitmap_len);
/* Samples a single pixel. */
uint drw_sel_buf_sample_point(struct Graph *graph,
                              struct ARgn *rgn,
                              struct View3D *v3d,
                              const int center[2]);
/* Find the sel id closest to a center.
 * param dist: Use to init the distance,
 * when found, this val is set to the distance of the sel that's returned. */
uint drw_sel_buf_find_nearest_to_point(struct Graph *graph,
                                       struct ARgn *rgn,
                                       struct View3D *v3d,
                                       const int center[2];
                                       uint id_min,
                                       uint id_max,
                                       uint *dist);
void drw_sel_buf_cxt_create(struct Base **bases, uint bases_len, short sel_mode);
