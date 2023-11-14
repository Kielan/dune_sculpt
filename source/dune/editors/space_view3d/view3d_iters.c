#include "types_armature.h"
#include "types_curve.h"
#include "types_lattice.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_meta.h"
#include "types_ob.h"
#include "types_scene.h"

#include "lib_math_geom.h"
#include "lib_rect.h"
#include "lib_utildefines.h"

#include "dune_DerivedMesh.h"
#include "dune_action.h"
#include "dune_armature.h"
#include "dune_curve.h"
#include "dune_displist.h"
#include "dune_editmesh.h"
#include "dune_mesh_iters.h"
#include "dune_mesh_runtime.h"
#include "dune_mesh_wrapper.h"
#include "dune_mod.h"

#include "graph.h"
#include "graph_query.h"

#include "mesh.h"

#include "ed_armature.h"
#include "ed_screen.h"
#include "ed_view3d.h"

/* Internal Clipping Utils */
/* Calc clipping planes to use when V3D_PROJ_TEST_CLIP_CONTENT is enabled
 * Planes are selected from the viewpoint using `clip_flag`
 * to detect which planes should be applied (maximum 6).
 * return The number of planes written into `planes`. */
static int content_planes_from_clip_flag(const ARgn *rgn,
                                         const Ob *ob,
                                         const eV3DProjTest clip_flag,
                                         float planes[6][4])
{
  lib_assert(clip_flag & V3D_PROJ_TEST_CLIP_CONTENT);

  float *clip_xmin = NULL, *clip_xmax = NULL;
  float *clip_ymin = NULL, *clip_ymax = NULL;
  float *clip_zmin = NULL, *clip_zmax = NULL;

  int planes_len = 0;

  /* The order of `planes` has been selected based on the likelihood of points being fully
   * outside the plane to increase the chance of an early exit in clip_segment_v3_plane_n.
   * With "near" being most likely and "far" being unlikely.
   * Otherwise the order of axes in `planes` isn't significant. */

  if (clip_flag & V3D_PROJ_TEST_CLIP_NEAR) {
    clip_zmin = planes[planes_len++];
  }
  if (clip_flag & V3D_PROJ_TEST_CLIP_WIN) {
    clip_xmin = planes[planes_len++];
    clip_xmax = planes[planes_len++];
    clip_ymin = planes[planes_len++];
    clip_ymax = planes[planes_len++];
  }
  if (clip_flag & V3D_PROJ_TEST_CLIP_FAR) {
    clip_zmax = planes[planes_len++];
  }

  lib_assert(planes_len <= 6);
  if (planes_len != 0) {
    RgnView3D *rv3d = rgn->rhndata;
    float projmat[4][4];
    ed_view3d_ob_project_mat_get(rv3d, ob, projmat);
    planes_from_projmat(projmat, clip_xmin, clip_xmax, clip_ymin, clip_ymax, clip_zmin, clip_zmax);
  }
  return planes_len;
}

/* Edge projection is more involved since part of the edge may be behind the view
 * or extend beyond the far limits. In the case of single points, these can be ignored.
 * However it just may still be visible on screen, so constrained the edge to planes
 * defined by the port to ensure both ends of the edge can be projected, see T32214
 * note This is unrelated to V3D_PROJ_TEST_CLIP_BB which must be checked separately. */
static bool view3d_project_segment_to_screen_with_content_clip_planes(
    const ARgn *rgn,
    const float v_a[3],
    const float v_b[3],
    const eV3DProjTest clip_flag,
    const rctf *win_rect,
    const float content_planes[][4],
    const int content_planes_len,
    /* Output. */
    float r_screen_co_a[2],
    float r_screen_co_b[2])
{
  /* Clipping already handled, no need to check in projection. */
  eV3DProjTest clip_flag_nowin = clip_flag & ~V3D_PROJ_TEST_CLIP_WIN;

  const eV3DProjStatus status_a = ed_view3d_project_float_ob(
      rgn, v_a, r_screen_co_a, clip_flag_nowin);
  const eV3DProjStatus status_b = ed_view3d_project_float_ob(
      rgn, v_b, r_screen_co_b, clip_flag_nowin);

  if ((status_a == V3D_PROJ_RET_OK) && (status_b == V3D_PROJ_RET_OK)) {
    if (clip_flag & V3D_PROJ_TEST_CLIP_WIN) {
      if (!lib_rctf_isect_segment(win_rect, r_screen_co_a, r_screen_co_b)) {
        return false;
      }
    }
  }
  else {
    if (content_planes_len == 0) {
      return false;
    }

    /* Both too near, ignore. */
    if ((status_a & V3D_PROJ_TEST_CLIP_NEAR) && (status_b & V3D_PROJ_TEST_CLIP_NEAR)) {
      return false;
    }

    /* Both too far, ignore. */
    if ((status_a & V3D_PROJ_TEST_CLIP_FAR) && (status_b & V3D_PROJ_TEST_CLIP_FAR)) {
      return false;
    }

    /* Simple cases have been ruled out, clip by viewport planes, then re-project. */
    float v_a_clip[3], v_b_clip[3];
    if (!clip_segment_v3_plane_n(
            v_a, v_b, content_planes, content_planes_len, v_a_clip, v_b_clip)) {
      return false;
    }

    if ((ed_view3d_project_float_ob(rgn, v_a_clip, r_screen_co_a, clip_flag_nowin) !=
         V3D_PROJ_RET_OK) ||
        (ed_view3d_project_float_ob(rgn, v_b_clip, r_screen_co_b, clip_flag_nowin) !=
         V3D_PROJ_RET_OK)) {
      return false;
    }

    /* No need for V3D_PROJ_TEST_CLIP_WIN check here,
     * clipping the segment by planes handle this. */
  }

  return true;
}

/* Project an edge, points that fail to project are tagged with IS_CLIPPED. */
static bool view3d_project_segment_to_screen_with_clip_tag(const ARgn *rgn,
                                                           const float v_a[3],
                                                           const float v_b[3],
                                                           const eV3DProjTest clip_flag,
                                                           /* Output. */
                                                           float r_screen_co_a[2],
                                                           float r_screen_co_b[2])
{
  int count = 0;

  if (ed_view3d_project_float_ob(rgn, v_a, r_screen_co_a, clip_flag) == V3D_PROJ_RET_OK) {
    count++;
  }
  else {
    r_screen_co_a[0] = IS_CLIPPED; /* weak */
    /* screen_co_a[1]: intentionally don't set this so we get errors on misuse */
  }

  if (ed_view3d_project_float_ob(rgn, v_b, r_screen_co_b, clip_flag) == V3D_PROJ_RET_OK) {
    count++;
  }
  else {
    r_screen_co_b[0] = IS_CLIPPED; /* weak */
    /* screen_co_b[1]: intentionally don't set this so we get errors on misuse */
  }

  /* Caller may want to know this value, for now it's not needed. */
  return count != 0;
}

/* Private User Data Structs */
typedef struct foreachScreenObVert_userData {
  void (*fn)(void *userData, MVert *mv, const float screen_co[2], int index);
  void *userData;
  ViewCxt vc;
  eV3DProjTest clip_flag;
} foreachScreenObVert_userData;

typedef struct foreachScreenVert_userData {
  void (*fn)(void *userData, MVert *eve, const float screen_co[2], int index);
  void *userData;
  ViewCxt vc;
  eV3DProjTest clip_flag;
} foreachScreenVert_userData;

/* user data structs for derived mesh cbs */
typedef struct foreachScreenEdge_userData {
  void (*fn)(void *userData,
               MeshEdge *eed,
               const float screen_co_a[2],
               const float screen_co_b[2],
               int index);
  void *userData;
  ViewCxt vc;
  eV3DProjTest clip_flag;

  rctf win_rect; /* copy of: vc.rgn->winx/winy, use for faster tests, minx/y will always be 0 */

  /* Clip plans defined by the view bounds,
   * use when V3D_PROJ_TEST_CLIP_CONTENT is enabled */
  float content_planes[6][4];
  int content_planes_len;
} foreachScreenEdge_userData;

typedef struct foreachScreenFace_userData {
  void (*fn)(void *userData, MFace *efa, const float screen_co_b[2], int index);
  void *userData;
  ViewCxt vc;
  eV3DProjTest clip_flag;
} foreachScreenFace_userData;

/* foreach fns should be called while drawing or directly after
 * if not, ed_view3d_init_mats_rv3d() can be used for sel tools
 * but would not give correct results with dupli's for eg. which don't
 * use the ob matrix in the usual way. */

/* Edit-Mesh: For Each Screen Vertex */
static void meshob_foreachScreenVert__mapFn(void *userData,
                                            int index,
                                            const float co[3],
                                            const float UNUSED(no[3]))
{
  foreachScreenObVert_userData *data = userData;
  struct MVert *mv = &((Mesh *)(data->vc.obact->data))->mvert[index];

  if (!(mv->flag & ME_HIDE)) {
    float screen_co[2];

    if (ed_view3d_project_float_ob(data->vc.rgn, co, screen_co, data->clip_flag) !=
        V3D_PROJ_RET_OK) {
      return;
    }

    data->fn(data->userData, mv, screen_co, index);
  }
}

void meshob_foreachScreenVert(
    ViewCxt *vc,
    void (*fn)(void *userData, MVert *eve, const float screen_co[2], int index),
    void *userData,
    eV3DProjTest clip_flag)
{
  lib_assert((clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) == 0);
  foreachScreenObVert_userData data;
  Mesh *me;

  Scene *scene_eval = graph_get_eval_scene(vc->graph);
  Ob *ob_eval = graph_get_eval_ob(vc->graph, vc->obact);

  me = mesh_get_eval_final(vc->graph, scene_eval, ob_eval, &CD_MASK_BAREMESH);

  ed_view3d_check_mats_rv3d(vc->rv3d);

  data.vc = *vc;
  data.fn = fn;
  data.userData = userData;
  data.clip_flag = clip_flag;

  if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
    ed_view3d_clipping_local(vc->rv3d, vc->obact->obmat);
  }

  dune_mesh_foreach_mapped_vert(me, meshob_foreachScreenVert__mapFn, &data, MESH_FOREACH_NOP);
}

static void mesh_foreachScreenVert__mapFn(void *userData,
                                            int index,
                                            const float co[3],
                                            const float UNUSED(no[3]))
{
  foreachScreenVert_userData *data = userData;
  MVert *eve = mesh_vert_at_index(data->vc.em->mesh, index);
  if (UNLIKELY(mesh_elem_flag_test(eve, MESH_ELEM_HIDDEN))) {
    return;
  }

  float screen_co[2];
  if (ed_view3d_project_float_ob(data->vc.rgn, co, screen_co, data->clip_flag) !=
      V3D_PROJ_RET_OK) {
    return;
  }

  data->fn(data->userData, eve, screen_co, index);
}

void mesh_foreachScreenVert(
    ViewCxt *vc,
    void (*fn)(void *userData, MVert *eve, const float screen_co[2], int index),
    void *userData,
    eV3DProjTest clip_flag)
{
  foreachScreenVert_userData data;

  Mesh *me = editmesh_get_eval_cage_from_orig(
      vc->graph, vc->scene, vc->obedit, &CD_MASK_BAREMESH);
  me = dune_mesh_wrapper_ensure_subdivision(vc->obedit, me);

  ed_view3d_check_mats_rv3d(vc->rv3d);

  data.vc = *vc;
  data.fn = fn;
  data.userData = userData;
  data.clip_flag = clip_flag;

  if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
    ed_view3d_clipping_local(vc->rv3d, vc->obedit->obmat); /* for local clipping lookups */
  }

  mesh_elem_table_ensure(vc->em->mesh, MESH_VERT);
  dune_mesh_foreach_mapped_vert(me, mesh_foreachScreenVert__mapFn, &data, MESH_FOREACH_NOP);
}

/* Edit-Mesh: For Each Screen Mesh Edge */
static void mesh_foreachScreenEdge__mapFn(void *userData,
                                          int index,
                                          const float v_a[3],
                                          const float v_b[3])
{
  foreachScreenEdge_userData *data = userData;
  MEdge *eed = mesh_edge_at_index(data->vc.em->mesh, index);
  if (UNLIKELY(mesh_elem_flag_test(eed, MESH_ELEM_HIDDEN))) {
    return;
  }

  float screen_co_a[2], screen_co_b[2];
  if (!view3d_project_segment_to_screen_with_content_clip_planes(data->vc.rgn,
                                                                 v_a,
                                                                 v_b,
                                                                 data->clip_flag,
                                                                 &data->win_rect,
                                                                 data->content_planes,
                                                                 data->content_planes_len,
                                                                 screen_co_a,
                                                                 screen_co_b)) {
    return;
  }

  data->fn(data->userData, eed, screen_co_a, screen_co_b, index);
}

void mesh_foreachScreenEdge(ViewCxt *vc,
                            void (*fn)(void *userData,
                                         MEdge *eed,
                                         const float screen_co_a[2],
                                         const float screen_co_b[2],
                                         int index),
                            void *userData,
                            eV3DProjTest clip_flag)
{
  foreachScreenEdge_userData data;

  Mesh *me = editmesh_get_eval_cage_from_orig(
      vc->depsgraph, vc->scene, vc->obedit, &CD_MASK_BAREMESH);
  me = dune_mesh_wrapper_ensure_subdivision(vc->obedit, me);

  ed_view3d_check_mats_rv3d(vc->rv3d);

  data.vc = *vc;

  data.win_rect.xmin = 0;
  data.win_rect.ymin = 0;
  data.win_rect.xmax = vc->rgn->winx;
  data.win_rect.ymax = vc->rgnn->winy;

  data.fn = fn;
  data.userData = userData;
  data.clip_flag = clip_flag;

  if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
    ed_view3d_clipping_local(vc->rv3d, vc->obedit->obmat); /* for local clipping lookups */
  }

  if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
    data.content_planes_len = content_planes_from_clip_flag(
        vc->rgn, vc->obedit, clip_flag, data.content_planes);
  }
  else {
    data.content_planes_len = 0;
  }

  mesh_elem_table_ensure(vc->em->mesh, MESH_EDGE);
  dune_mesh_foreach_mapped_edge(me, vc->em->mesh->totedge, mesh_foreachScreenEdge__mapFn, &data);
}

/* Edit-Mesh: For Each Screen Edge (Bounding Box Clipped) */
/* Only call for bound-box clipping.
 * Otherwise call mesh_foreachScreenEdge__mapFn */
static void mesh_foreachScreenEdge_clip_bb_segment__mapFn(void *userData,
                                                          int index,
                                                          const float v_a[3],
                                                          const float v_b[3])
{
  foreachScreenEdge_userData *data = userData;
  MEdge *eed = mesh_edge_at_index(data->vc.em->mesh, index);
  if (UNLIKELY(mesh_elem_flag_test(eed, MESH_ELEM_HIDDEN))) {
    return;
  }

  lib_assert(data->clip_flag & V3D_PROJ_TEST_CLIP_BB);

  float v_a_clip[3], v_b_clip[3];
  if (!clip_segment_v3_plane_n(v_a, v_b, data->vc.rv3d->clip_local, 4, v_a_clip, v_b_clip)) {
    return;
  }

  float screen_co_a[2], screen_co_b[2];
  if (!view3d_project_segment_to_screen_with_content_clip_planes(data->vc.rgn,
                                                                 v_a_clip,
                                                                 v_b_clip,
                                                                 data->clip_flag,
                                                                 &data->win_rect,
                                                                 data->content_planes,
                                                                 data->content_planes_len,
                                                                 screen_co_a,
                                                                 screen_co_b)) {
    return;
  }

  data->fn(data->userData, eed, screen_co_a, screen_co_b, index);
}

void mesh_foreachScreenEdge_clip_bb_segment(ViewCxt *vc,
                                            void (*fn)(void *userData,
                                                       MEdge *eed,
                                                       const float screen_co_a[2],
                                                       const float screen_co_b[2],
                                                       int index),
                                            void *userData,
                                            eV3DProjTest clip_flag)
{
  foreachScreenEdge_userData data;

  Mesh *me = editmesh_get_eval_cage_from_orig(
      vc->graph, vc->scene, vc->obedit, &CD_MASK_BAREMESH);
  me = dune_mesh_wrapper_ensure_subdivision(vc->obedit, me);

  ed_view3d_check_mats_rv3d(vc->rv3d);

  data.vc = *vc;

  data.win_rect.xmin = 0;
  data.win_rect.ymin = 0;
  data.win_rect.xmax = vc->rgn->winx;
  data.win_rect.ymax = vc->rgn->winy;

  data.fn = fn;
  data.userData = userData;
  data.clip_flag = clip_flag;

  if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
    data.content_planes_len = content_planes_from_clip_flag(
        vc->rgn, vc->obedit, clip_flag, data.content_planes);
  }
  else {
    data.content_planes_len = 0;
  }

  mesh_elem_table_ensure(vc->em->mesh, MESH_EDGE);

  if ((clip_flag & V3D_PROJ_TEST_CLIP_BB) && (vc->rv3d->clipbb != NULL)) {
    ed_view3d_clipping_local(vc->rv3d, vc->obedit->obmat); /* for local clipping lookups. */
    dune_mesh_foreach_mapped_edge(
        me, vc->em->mesh->totedge, mesh_foreachScreenEdge_clip_bb_segment__mapFn, &data);
  }
  else {
    dune_mesh_foreach_mapped_edge(me, vc->em->mesh->totedge, mesh_foreachScreenEdge__mapFn, &data);
  }
}

/* EditMesh: For Each Screen Face Center */
static void mesh_foreachScreenFace__mapFn(void *userData,
                                          int index,
                                          const float cent[3],
                                          const float UNUSED(no[3]))
{
  foreachScreenFace_userData *data = userData;
  MFace *efa = mesh_face_at_index(data->vc.em->mesh, index);
  if (UNLIKELY(mesh_elem_flag_test(efa, MESH_ELEM_HIDDEN))) {
    return;
  }

  float screen_co[2];
  if (ed_view3d_project_float_ob(data->vc.region, cent, screen_co, data->clip_flag) !=
      V3D_PROJ_RET_OK) {
    return;
  }

  data->fn(data->userData, efa, screen_co, index);
}

void mesh_foreachScreenFace(
    ViewCxt *vc,
    void (*fn)(void *userData, MFace *efa, const float screen_co_b[2], int index),
    void *userData,
    const eV3DProjTest clip_flag)
{
  lib_assert((clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) == 0);
  foreachScreenFace_userData data;

  Mesh *me = editmesh_get_eval_cage_from_orig(
      vc->graph, vc->scene, vc->obedit, &CD_MASK_BAREMESH);
  me = dune_mesh_wrapper_ensure_subdivision(vc->obedit, me);
  ed_view3d_check_mats_rv3d(vc->rv3d);

  data.vc = *vc;
  data.fn = fn;
  data.userData = userData;
  data.clip_flag = clip_flag;

  mesh_elem_table_ensure(vc->em->mesh, MESH_FACE);

  if (dune_mods_uses_subsurf_facedots(vc->scene, vc->obedit)) {
    dune_mesh_foreach_mapped_subdiv_face_center(
        me, mesh_foreachScreenFace__mapFn, &data, MESH_FOREACH_NOP);
  }
  else {
    dune_mesh_foreach_mapped_face_center(
        me, mesh_foreachScreenFace__mapFn, &data, MESH_FOREACH_NOP);
  }
}

/* Edit-Nurbs: For Each Screen Vertex */
void nurbs_foreachScreenVert(ViewCxt *vc,
                             void (*fn)(void *userData,
                                        Nurb *nu,
                                        Point *bp,
                                        BezTriple *bezt,
                                        int beztindex,
                                        bool handles_visible,
                                        const float screen_co_b[2]),
                             void *userData,
                             const eV3DProjTest clip_flag)
{
  Curve *cu = vc->obedit->data;
  Nurb *nu;
  int i;
  List *nurbs = dune_curve_editNurbs_get(cu);
  /* If no point in the triple is selected, the handles are invisible. */
  const bool only_selected = (vc->v3d->overlay.handle_display == CURVE_HANDLE_SELECTED);

  ed_view3d_check_mats_rv3d(vc->rv3d);

  if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
    ed_view3d_clipping_local(vc->rv3d, vc->obedit->obmat); /* for local clipping lookups */
  }

  for (nu = nurbs->first; nu; nu = nu->next) {
    if (nu->type == CU_BEZIER) {
      for (i = 0; i < nu->pntsu; i++) {
        BezTriple *bezt = &nu->bezt[i];

        if (bezt->hide == 0) {
          const bool handles_visible = (vc->v3d->overlay.handle_display != CURVE_HANDLE_NONE) &&
                                       (!only_selected || BEZT_ISSEL_ANY(bezt));
          float screen_co[2];

          if (!handles_visible) {
            if (ed_view3d_project_float_object(vc->rgn,
                                               bezt->vec[1],
                                               screen_co,
                                               V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
                V3D_PROJ_RET_OK) {
              fn(userData, nu, NULL, bezt, 1, false, screen_co);
            }
          }
          else {
            if (ed_view3d_project_float_ob(vc->rgn,
                                           bezt->vec[0],
                                           screen_co,
                                           V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
                V3D_PROJ_RET_OK) {
              fn(userData, nu, NULL, bezt, 0, true, screen_co);
            }
            if (ed_view3d_project_float_ob(vc->rgn,
                                           bezt->vec[1],
                                           screen_co,
                                           V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
                V3D_PROJ_RET_OK) {
              fn(userData, nu, NULL, bezt, 1, true, screen_co);
            }
            if (ed_view3d_project_float_ob(vc->rgn,
                                           bezt->vec[2],
                                           screen_co,
                                           V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
                V3D_PROJ_RET_OK) {
              fn(userData, nu, NULL, bezt, 2, true, screen_co);
            }
          }
        }
      }
    }
    else {
      for (i = 0; i < nu->pntsu * nu->pntsv; i++) {
        Point *point = &nu->point[i];

        if (point->hide == 0) {
          float screen_co[2];
          if (ed_view3d_project_float_ob(
                  vc->rgn, point->vec, screen_co, V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
              V3D_PROJ_RET_OK) {
            fn(userData, nu, point, NULL, -1, false, screen_co);
          }
        }
      }
    }
  }
}

/* EditMeta: For Each Screen Meta-Element */
void mball_foreachScreenElem(struct ViewCxt *vc,
                             void (*fn)(void *userData,
                                        struct MetaElem *ml,
                                        const float screen_co_b[2]),
                             void *userData,
                             const eV3DProjTest clip_flag)
{
  MetaBall *mb = (MetaBall *)vc->obedit->data;
  MetaElem *ml;

  ed_view3d_check_mats_rv3d(vc->rv3d);

  for (ml = mb->editelems->first; ml; ml = ml->next) {
    float screen_co[2];
    if (ed_view3d_project_float_ob(vc->rgn, &ml->x, screen_co, clip_flag) ==
        V3D_PROJ_RET_OK) {
      fn(userData, ml, screen_co);
    }
  }
}

/* Edit-Lattice: For Each Screen Vertex */
void lattice_foreachScreenVert(ViewCxt *vc,
                               void (*fn)(void *userData, Point *point, const float screen_co[2]),
                               void *userData,
                               const eV3DProjTest clip_flag)
{
  Ob *obedit = vc->obedit;
  Lattice *lt = obedit->data;
  Point *point = lt->editlatt->latt->def;
  DispList *dl = obedit->runtime.curve_cache ?
                     dune_displist_find(&obedit->runtime.curve_cache->disp, DL_VERTS) :
                     NULL;
  const float *co = dl ? dl->verts : NULL;
  int i, N = lt->editlatt->latt->pntsu * lt->editlatt->latt->pntsv * lt->editlatt->latt->pntsw;

  ed_view3d_check_mats_rv3d(vc->rv3d);

  if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
    ed_view3d_clipping_local(vc->rv3d, obedit->obmat); /* for local clipping lookups */
  }

  for (i = 0; i < N; i++, point++, co += 3) {
    if (point->hide == 0) {
      float screen_co[2];
      if (ed_view3d_project_float_ob(vc->rgn, dl ? co : bp->vec, screen_co, clip_flag) ==
          V3D_PROJ_RET_OK) {
        fn(userData, point, screen_co);
      }
    }
  }
}

/* Edit-Armature: For Each Screen Bone */
void armature_foreachScreenBone(struct ViewCxt *vc,
                                void (*fn)(void *userData,
                                           struct EditBone *ebone,
                                           const float screen_co_a[2],
                                           const float screen_co_b[2]),
                                void *userData,
                                const eV3DProjTest clip_flag)
{
  Armature *arm = vc->obedit->data;
  EditBone *ebone;

  ed_view3d_check_mats_rv3d(vc->rv3d);

  float content_planes[6][4];
  int content_planes_len;
  rctf win_rect;

  if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
    content_planes_len = content_planes_from_clip_flag(
        vc->rgn, vc->obedit, clip_flag, content_planes);
    win_rect.xmin = 0;
    win_rect.ymin = 0;
    win_rect.xmax = vc->rgn->winx;
    win_rect.ymax = vc->rgn->winy;
  }
  else {
    content_planes_len = 0;
  }

  for (ebone = arm->edbo->first; ebone; ebone = ebone->next) {
    if (!EBONE_VISIBLE(arm, ebone)) {
      continue;
    }

    float screen_co_a[2], screen_co_b[2];
    const float *v_a = ebone->head, *v_b = ebone->tail;

    if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
      if (!view3d_project_segment_to_screen_with_content_clip_planes(vc->rgn,
                                                                     v_a,
                                                                     v_b,
                                                                     clip_flag,
                                                                     &win_rect,
                                                                     content_planes,
                                                                     content_planes_len,
                                                                     screen_co_a,
                                                                     screen_co_b)) {
        continue;
      }
    }
    else {
      if (!view3d_project_segment_to_screen_with_clip_tag(
              vc->rgn, v_a, v_b, clip_flag, screen_co_a, screen_co_b)) {
        continue;
      }
    }

    fn(userData, ebone, screen_co_a, screen_co_b);
  }
}

/* Pose: For Each Screen Bone */
void pose_foreachScreenBone(struct ViewCxt *vc,
                            void (*fn)(void *userData,
                                       struct PoseChannel *pchan,
                                       const float screen_co_a[2],
                                       const float screen_co_b[2]),
                            void *userData,
                            const eV3DProjTest clip_flag)
{
  /* Almost _exact_ copy of armature_foreachScreenBone */
  const Ob *ob_eval = graph_get_eval_ob(vc->graph, vc->obact);
  const Armature *arm_eval = ob_eval->data;
  Pose *pose = vc->obact->pose;
  PoseChannel *pchan;

  ed_view3d_check_mats_rv3d(vc->rv3d);

  float content_planes[6][4];
  int content_planes_len;
  rctf win_rect;

  if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
    content_planes_len = content_planes_from_clip_flag(
        vc->rgn, ob_eval, clip_flag, content_planes);
    win_rect.xmin = 0;
    win_rect.ymin = 0;
    win_rect.xmax = vc->region->winx;
    win_rect.ymax = vc->region->winy;
  }
  else {
    content_planes_len = 0;
  }

  for (pchan = pose->chanbase.first; pchan; pchan = pchan->next) {
    if (!PBONE_VISIBLE(arm_eval, pchan->bone)) {
      continue;
    }

    PoseChannel *pchan_eval = dune_pose_channel_find_name(ob_eval->pose, pchan->name);
    float screen_co_a[2], screen_co_b[2];
    const float *v_a = pchan_eval->pose_head, *v_b = pchan_eval->pose_tail;

    if (clip_flag & V3D_PROJ_TEST_CLIP_CONTENT) {
      if (!view3d_project_segment_to_screen_with_content_clip_planes(vc->rgn,
                                                                     v_a,
                                                                     v_b,
                                                                     clip_flag,
                                                                     &win_rect,
                                                                     content_planes,
                                                                     content_planes_len,
                                                                     screen_co_a,
                                                                     screen_co_b)) {
        continue;
      }
    }
    else {
      if (!view3d_project_segment_to_screen_with_clip_tag(
              vc->re my gn, v_a, v_b, clip_flag, screen_co_a, screen_co_b)) {
        continue;
      }
    }

    fn(userData, pchan, screen_co_a, screen_co_b);
  }
}
