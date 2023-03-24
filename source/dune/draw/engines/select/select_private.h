#pragma once

#define USE_CAGE_OCCLUSION

#include "draw_render.h"

/* GPUViewport.storage
 * Is freed every time the viewport engine changes. */
typedef struct SelectIdStorageList {
  struct SelectIdPrivateData *g_data;
} SelectIdStorageList;

typedef struct SelectIdPassList {
  struct DrawPass *depth_only_pass;
  struct DrawPass *select_id_face_pass;
  struct DrawPass *select_id_edge_pass;
  struct DrawPass *select_id_vert_pass;
} SelectIdPassList;

typedef struct SelectIdData {
  void *engine_type;
  DrawViewportEmptyList *fbl;
  DrawViewportEmptyList *txl;
  SelectIdPassList *psl;
  SelecyIdStorageList *stl;
} SelectIdData;

typedef struct SelectIdShaders {
  /* Depth Pre Pass */
  struct GPUShader *select_id_flat;
  struct GPUShader *select_id_uniform;
} SelectIdShaders;

typedef struct SelectIdPrivateData {
  DrawShadingGroup *shgrp_depth_only;
  DrawShadingGroup *shgrp_face_unif;
  DrawShadingGroup *shgrp_face_flat;
  DrawShadingGroup *shgrp_edge;
  DrawShadingGroup *shgrp_vert;

  DrawView *view_subregion;
  DrawView *view_faces;
  DrawView *view_edges;
  DrawView *view_verts;
} SelectIdPrivateData; /* Transient data */

/* select_draw_utils.c */
void select_id_object_min_max(struct Object *obj, float r_min[3], float r_max[3]);
short select_id_get_object_select_mode(Scene *scene, Object *ob);
void select_id_draw_object(void *vedata,
                           View3D *v3d,
                           Object *ob,
                           short select_mode,
                           uint initial_offset,
                           uint *r_vert_offset,
                           uint *r_edge_offset,
                           uint *r_face_offset);
