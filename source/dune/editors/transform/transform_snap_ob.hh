#pragma once

#include "lib_math_geom.h"

#define MAX_CLIPPLANE_LEN 6

#define SNAP_TO_EDGE_ELEMENTS \
  (SCE_SNAP_TO_EDGE | SCE_SNAP_TO_EDGE_ENDPOINT | SCE_SNAP_TO_EDGE_MIDPOINT | \
   SCE_SNAP_TO_EDGE_PERPENDICULAR)

struct SnapObCxt {
  Scene *scene;

  struct SnapCache {
    virtual ~SnapCache(){};
  };
  dune::Map<const MeshEdit*, std::unique_ptr<SnapCache>> meshedit_caches;

  /* Filter data, returns true to check this val */
  struct {
    struct {
      bool (*test_vert_fn)(MeshVert *, void *user_data);
      bool (*test_edge_fn)(MeshEdge *, void *user_data);
      bool (*test_face_fn)(MeshFace *, void *user_data);
      void *user_data;
    } edit_mesh;
  } cba;

  struct {
    Graph *graph;
    const RgnView3D *rv3d;
    const View3D *v3d;

    eSnapMode snap_to_flag;
    SnapObParams params;

    dune::float3 ray_start;
    dune::float3 ray_dir;

    dune::float3 init_co;
    dune::float3 curr_co;

    dune::float2 win_size; /* win x and y */
    dune::float2 mval;

    dune::Vector<dune::float4, MAX_CLIPPLANE_LEN> clip_planes;
    dune::float4 occlusion_plane;
    dune::float4 occlusion_plane_in_front;

    /* read/write */
    uint ob_index;

    bool has_occlusion_plane;
    bool has_occlusion_plane_in_front;
    bool use_occlusion_test_edit;
  } runtime;

  /* Output. */
  struct {
    /* Location of snapped point on target surface. */
    dune::float3 loc;
    /* Normal of snapped point on target surface. */
    dune::float3 no;
    /* Index of snapped element on target ob (-1 when no valid index is found). */
    int index;
    /* Matrix of target ob (may not be Ob.ob_to_world with dup-instances). */
    dune::float4x4 obmat;
    /* List of SnapObHitDepth (caller must free). */
    List *hit_list;
    /* Snapped ob. */
    Ob *ob;
    /* Snapped data. */
    const Id *data;

    float ray_depth_max;
    float ray_depth_max_in_front;
    union {
      float dist_px_sq;
      float dist_nearest_sq;
    };
  } ret;
};

struct RayCastAll_Data {
  void *bvhdata;

  /* internal vars for adding depths */
  BVHTreeRayCastCb raycast_cb;

  const dune::float4x4 *obmat;

  float len_diff;
  float local_scale;

  uint ob_uuid;

  /* output data */
  List *hit_list;
};

class SnapData {
 public:
  /* Read-only. */
  DistProjectedAABBPrecalc nearest_precalc;
  dune::Vector<dune::float4, MAX_CLIPPLANE_LEN + 1> clip_planes;
  dune::float4x4 pmat_local;
  dune::float4x4 obmat_;
  const bool is_persp;
  const bool use_backface_culling;

  /* Read and write. */
  BVHTreeNearest nearest_point;

 public:
  /* Constructor. */
  SnapData(SnapObCxt *scxt,
           const dune::float4x4 &obmat = dune::float4x4::id());

  void clip_planes_enable(SnapObCxt *scxt,
                          const Ob *ob_eval,
                          bool skip_occlusion_plane = false);
  bool snap_boundbox(const dune::float3 &min, const dune::float3 &max);
  bool snap_point(const dune::float3 &co, int index = -1);
  bool snap_edge(const dune::float3 &va, const dune::float3 &vb, int edge_index = -1);
  eSnapMode snap_edge_points_impl(SnapObCxt *scxt, int edge_index, float dist_px_sq_orig);
  static void register_result(SnapObCxt *scxt,
                              Ob *ob_eval,
                              const Id *id_eval,
                              const dune::float4x4 &obmat,
                              BVHTreeNearest *r_nearest);
  void register_result(SnapObCxt *scxt, Ob *ob_eval, const Id *id_eval);
  static void register_result_raycast(SnapObCxt *scxt,
                                      Ob *ob_eval,
                                      const Id *id_eval,
                                      const dune::float4x4 &obmat,
                                      const BVHTreeRayHit *hit,
                                      const bool is_in_front);

  virtual void get_vert_co(const int /*index*/, const float ** /*r_co*/){};
  virtual void get_edge_verts_index(const int /*index*/, int /*r_v_index*/[2]){};
  virtual void copy_vert_no(const int /*index*/, float /*r_no*/[3]){};
};

/* transform_snap_ob.cc */
void raycast_all_cb(void *userdata, int index, const BVHTreeRay *ray, BVHTreeRayHit *hit);

bool raycast_tri_backface_culling_test(
    const float dir[3], const float v0[3], const float v1[3], const float v2[3], float no[3]);

void cb_snap_vert(void *userdata,
                  int index,
                  const DistProjectedAABBPrecalc *precalc,
                  const float (*clip_plane)[4],
                  const int clip_plane_len,
                  BVHTreeNearest *nearest);

void cb_snap_edge(void *userdata,
                  int index,
                  const DistProjectedAABBPrecalc *precalc,
                  const float (*clip_plane)[4],
                  const int clip_plane_len,
                  BVHTreeNearest *nearest);

bool nearest_world_tree(SnapObCxt *scxt,
                        BVHTree *tree,
                        BVHTreeNearestPointCb nearest_cb,
                        const dune::float4x4 &obmat,
                        void *treedata,
                        BVHTreeNearest *r_nearest);

eSnapMode snap_ob_center(SnapObCxt *scxt,
                         Ob *ob_eval,
                         const dune::float4x4 &obmat,
                         eSnapMode snap_to_flag);

/* transform_snap_ob_armature.cc */
eSnapMode snapArmature(SnapObCxt *scxt,
                       Ob *ob_eval,
                       const dune::float4x4 &obmat,
                       bool is_ob_active);

/* transform_snap_ob_camera.cc */
eSnapMode snapCamera(SnapObCxt *scxt,
                     Ob *ob,
                     const dune::float4x4 &obmat,
                     eSnapMode snap_to_flag);

/* transform_snap_ob_curve.cc */
eSnapMode snapCurve(SnapObCxt *sctx, Ob *ob_eval, const dune::float4x4 &obmat);

/* transform_snap_ob_meshedit.cc */
eSnapMode snap_ob_meshedit(SnapObCxt *scxt,
                           Ob *ob_eval,
                           const Id *id,
                           const dune::float4x4 &obmat,
                           eSnapMode snap_to_flag,
                           bool use_hide);

eSnapMode snap_polygon_editmesh(SnapObCxt *scxt,
                                Ob*ob_eval,
                                const Id *id,
                                const dune::float4x4 &obmat,
                                eSnapMode snap_to_flag,
                                int polygon);

eSnapMode snap_edge_points_meshedit(SnapObCxt *scxt,
                                    Ob *ob_eval,
                                    const Id *id,
                                    const dune::float4x4 &obmat,
                                    float dist_px_sq_orig,
                                    int edge);

/* transform_snap_ob_mesh.cc */
eSnapMode snap_ob_mesh(SnapObCxt *scxt,
                       Ob *ob_eval,
                       const Id *id,
                       const dune::float4x4 &obmat,
                       eSnapMode snap_to_flag,
                       bool use_hide);

eSnapMode snap_polygon_mesh(SnapObCxt *scxt,
                            Ob *ob_eval,
                            const Id *id,
                            const dune::float4x4 &obmat,
                            eSnapMode snap_to_flag,
                            int polygon);

eSnapMode snap_edge_points_mesh(SnapObCxt *scxt,
                                Ob *ob_eval,
                                const Id *id,
                                const dune::float4x4 &obmat,
                                float dist_px_sq_orig,
                                int edge);
