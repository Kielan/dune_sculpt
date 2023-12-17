#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MeshFace;
struct MeshLoop;
struct Ob;
struct Scene;
struct SpaceImg;
struct WinOpType;

/* find nearest */
typedef struct UvNearestHit {
  /* Only for `*_multi(..)` versions of functions. */
  struct Ob *ob;
  /* Always set if we have a hit. */
  struct MeshFace *efa;
  struct MeshLoop *l;
  /* Needs to be set before calling nearest functions.
   *
   * l When uv_nearest_hit_init_dist_px or #uv_nearest_hit_init_max are used,
   * this val is pixels squared. */
  float dist_sq;

  /** Scale the UVs to account for aspect ratio from the image view. */
  float scale[2];
} UvNearestHit;

UvNearestHit uv_nearest_hit_init_dist_px(const struct View2D *v2d, const float dist_px);
UvNearestHit uv_nearest_hit_init_max(const struct View2D *v2d);

bool uv_find_nearest_vert(struct Scene *scene,
                          struct Ob *obedit,
                          const float co[2],
                          float penalty_dist,
                          struct UvNearestHit *hit);
bool uv_find_nearest_vert_multi(struct Scene *scene,
                                struct Ob **obs,
                                uint obs_len,
                                const float co[2],
                                float penalty_dist,
                                struct UvNearestHit *hit);

bool uv_find_nearest_edge(struct Scene *scene,
                          struct Ob *obedit,
                          const float co[2],
                          float penalty,
                          struct UvNearestHit *hit);
bool uv_find_nearest_edge_multi(struct Scene *scene,
                                struct Ob **obs,
                                uint obs_len,
                                const float co[2],
                                float penalty,
                                struct UvNearestHit *hit);

/* param only_in_face: when true, only hit faces which `co` is inside.
 * This gives users a result they might expect, especially when zoomed in.
 *
 * Concave faces can cause odd behavior, in practice this isn't often an issue.
 * The center can be outside the face, in this case the distance to the center
 * could cause the face to be considered too far away.
 * If this becomes an issue we could track the distance to the faces closest edge */
bool uv_find_nearest_face_ex(struct Scene *scene,
                             struct Ob *obedit,
                             const float co[2],
                             struct UvNearestHit *hit,
                             bool only_in_face);
bool uv_find_nearest_face(struct Scene *scene,
                          struct Ob *obedit,
                          const float co[2],
                          struct UvNearestHit *hit);
bool uv_find_nearest_face_multi_ex(struct Scene *scene,
                                   struct Ob **obs,
                                   uint obs_len,
                                   const float co[2],
                                   struct UvNearestHit *hit,
                                   bool only_in_face);
bool uv_find_nearest_face_multi(struct Scene *scene,
                                struct Ob **obs,
                                uint obs_len,
                                const float co[2],
                                struct UvNearestHit *hit);

BMLoop *uv_find_nearest_loop_from_vert(struct Scene *scene,
                                       struct Ob *obedit,
                                       struct MeshVert *v,
                                       const float co[2]);
BMLoop *uv_find_nearest_loop_from_edge(struct Scene *scene,
                                       struct Ob *obedit,
                                       struct MeshEdge *e,
                                       const float co[2]);

bool uvedit_vert_is_edge_sel_any_other(const struct Scene *scene,
                                          struct MeshLoop *l,
                                          MeshUVOffsets offsets);
bool uvedit_vert_is_face_sel_any_other(const struct Scene *scene,
                                          struct BMLoop *l,
                                          MeshUVOffsets offsets);
bool uvedit_vert_is_all_other_faces_sel(const struct Scene *scene,
                                             struct MeshLoop *l,
                                             MeshUVOffsets offsets);

/* util tool fns */
void uvedit_live_unwrap_update(struct SpaceImg *simg,
                               struct Scene *scene,
                               struct Ob *obedit);

/* ops */
void UV_OT_avg_islands_scale(struct WinOpType *ot);
void UV_OT_cube_project(struct WinOpType *ot);
void UV_OT_cylinder_project(struct wmOpType *ot);
void UV_OT_project_from_view(struct wmOpType *ot);
void UV_OT_minimize_stretch(struct wmOpType *ot);
void UV_OT_pack_islands(struct wmOpType *ot);
void UV_OT_reset(struct wmOpType *ot);
void UV_OT_sphere_project(struct wmOpType *ot);
void UV_OT_unwrap(struct wmOpType *ot);
void UV_OT_rip(struct wmOpType *ot);
void UV_OT_stitch(struct wmOpType *ot);
void UV_OT_smart_project(struct wmOpType *ot);

/* uvedit_copy_paste.cc */
void UV_OT_copy(wmOpType *ot);
void UV_OT_paste(wmOpType *ot);

/* `uvedit_path.cc` */
void UV_OT_shortest_path_pick(struct wmOpType *ot);
void UV_OT_shortest_path_sel(struct wmOpType *ot);

/* `uvedit_select.cc` */
bool uvedit_sel_is_any_sel(const struct Scene *scene, struct Object *obedit);
bool uvedit_sel_is_any_sel_multi(const struct Scene *scene,
                                         struct Object **objects,
                                         uint objects_len);
/**
 * \warning This returns first selected UV,
 * not ideal in many cases since there could be multiple.
 */
const float *uvedit_first_selected_uv_from_vertex(struct Scene *scene,
                                                  struct BMVert *eve,
                                                  BMUVOffsets offsets);

void UV_OT_sel_all(struct wmOperatorType *ot);
void UV_OT_sel(struct wmOperatorType *ot);
void UV_OT_sel_loop(struct wmOperatorType *ot);
void UV_OT_sel_edge_ring(struct wmOperatorType *ot);
void UV_OT_sel_linked(struct wmOperatorType *ot);
void UV_OT_sel_linked_pick(struct wmOperatorType *ot);
void UV_OT_sel_split(struct wmOperatorType *ot);
void UV_OT_sel_pinned(struct wmOperatorType *ot);
void UV_OT_sel_box(struct wmOperatorType *ot);
void UV_OT_sel_lasso(struct wmOperatorType *ot);
void UV_OT_sel_circle(struct wmOperatorType *ot);
void UV_OT_sel_more(struct wmOperatorType *ot);
void UV_OT_sel_less(struct wmOperatorType *ot);
void UV_OT_sel_overlap(struct wmOperatorType *ot);
void UV_OT_sel_similar(struct wmOperatorType *ot);
/* Used only when UV sync select is disabled. */
void UV_OT_sel_mode(struct wmOperatorType *ot);

#ifdef __cplusplus
}
#endif
