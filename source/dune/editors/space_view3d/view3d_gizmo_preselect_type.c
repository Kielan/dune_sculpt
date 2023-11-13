/* Preselection Gizmo
 * Use for tools to hover over data before activation.
 * This is a slight misuse of gizmo's, since clicking performs no action */

#include "mem_guardedalloc.h"

#include "lib_math.h"

#include "types_mesh.h"
#include "types_view3d.h"

#include "dune_cxt.h"
#include "dune_editmesh.h"
#include "dune_global.h"
#include "dune_layer.h"

#include "graph.h"
#include "graph_query.h"

#include "api_access.h"
#include "api_define.h"

#include "win_api.h"
#include "win_types.h"

#include "mesh.h"

#include "ed_gizmo_lib.h"
#include "ed_mesh.h"
#include "ed_screen.h"
#include "ed_view3d.h"

/* Shared Internal API */
/* Check if drawing should be performed, clear the pre-selection in the case it's disabled.
 * Wo this, the gizmo would be visible while transforming. See T92954.
 *
 * NOTE: This is a workaround for the gizmo system, since typically poll
 * would be used for this purpose. The problem with using poll is once the gizmo is visible again
 * is there is a visible flicker showing the previous location before cursor motion causes the
 * pre selection to be updated. While this is only a glitch, it's distracting.
 * The gizmo system it's self could support this use case by tracking which gizmos draw and ensure
 * gizmos always run WinGizmoType.test_sel before drawing, however pre-selection is already
 * outside the scope of what gizmos are meant to be used for, so keep this workaround localized
 * to this gizmo type unless this seems worth supporting for more typical use-cases.
 * Longer term it may be better to use WinPaintCursor instead of gizmos (as snapping preview does) */
static bool gizmo_preselect_poll_for_draw(const Cxt *C, WinGizmo *gz)
{
  if (G.moving == false) {
    RgnView3D *rv3d = cxt_win_rgn_view3d(C);
    if (!(rv3d && (rv3d->rflag & RV3D_NAVIGATING))) {
      return true;
    }
  }
  ed_view3d_gizmo_mesh_preselect_clear(gz);
  return false;
}

/* Mesh Element (Vert/Edge/Face) Pre-Select Gizmo API **/
typedef struct MeshElemGizmo3D {
  WinGizmo gizmo;
  Base **bases;
  uint bases_len;
  int base_index;
  int vert_index;
  int edge_index;
  int face_index;
  struct EditMesh_PreSelElem *psel;
} MeshElemGizmo3D;

static void gizmo_preselect_elem_draw(const Cxt *C, WinGizmo *gz)
{
  if (!gizmo_preselect_poll_for_draw(C, gz)) {
    return;
  }

  MeshElemGizmo3D *gz_ele = (MeshElemGizmo3D *)gz;
  if (gz_ele->base_index != -1) {
    Obj *ob = gz_ele->bases[gz_ele->base_index]->obj;
    EDBM_preselect_elem_draw(gz_ele->psel, ob->obmat);
  }
}

static int gizmo_preselect_elem_test_sel(Cxt *C, WinGizmo *gz, const int mval[2])
{
  WinEv *ev = cxt_win(C)->evstate;
  MeshElemGizmo3D *gz_ele = (MeshElemGizmo3D *)gz;

  /* Hack: Switch action mode based on key input */
  const bool is_ctrl_pressed = (ev->mod & KM_CTRL) != 0;
  const bool is_shift_pressed = (ev->mod & KM_SHIFT) != 0;
  EDBM_preselect_action_set(gz_ele->psel, PRESELECT_ACTION_TRANSFORM);
  if (is_ctrl_pressed && !is_shift_pressed) {
    EDBM_preselect_action_set(gz_ele->psel, PRESELECT_ACTION_CREATE);
  }
  if (!is_ctrl_pressed && is_shift_pressed) {
    EDBM_preselect_action_set(gz_ele->psel, PRESELECT_ACTION_DELETE);
  }

  struct {
    Obj *ob;
    MeshElem *ele;
    float dist;
    int base_index;
  } best = {
      .dist = ed_view3d_sel_dist_px(),
  };

  {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    View3D *v3d = cxt_win_view3d(C);
    if (((gz_ele->bases)) == NULL || (gz_ele->bases[0] != view_layer->basact)) {
      MEM_SAFE_FREE(gz_ele->bases);
      gz_ele->bases = dune_view_layer_array_from_bases_in_edit_mode(
          view_layer, v3d, &gz_ele->bases_len);
    }
  }

  ViewCxt vc;
  em_setup_viewcxt(C, &vc);
  copy_v2_v2_int(vc.mval, mval);

  {
    /* TODO: support faces. */
    int base_index_vert = -1;
    int base_index_edge = -1;
    int base_index_face = -1;
    MVert *eve_test;
    MEdge *eed_test;
    MFace *efa_test;

    if (EDBM_unified_findnearest_from_raycast(&vc,
                                              gz_ele->bases,
                                              gz_ele->bases_len,
                                              false,
                                              true,
                                              &base_index_vert,
                                              &base_index_edge,
                                              &base_index_face,
                                              &eve_test,
                                              &eed_test,
                                              &efa_test)) {
      if (EDBM_preselect_action_get(gz_ele->psel) == PRESELECT_ACTION_DELETE) {
        /* Delete action */
        if (efa_test) {
          best.ele = (DuneMeshElem *)efa_test;
          best.base_index = base_index_face;
        }
      }

      else {
        /* Transform and create action */
        if (eed_test) {
          best.ele = (DuneMeshElem *)eed_test;
          best.base_index = base_index_edge;
        }
      }

      /* All actions use same vertex pre-selection. */
      /* Re-topology should always prioritize edge pre-selection.
       * Only pre-select a vertex when the cursor is really close to it. */
      if (eve_test) {
        MVert *vert = (MVert *)eve_test;
        float vert_p_co[2], vert_co[3];
        const float mval_f[2] = {UNPACK2(vc.mval)};
        mul_v3_m4v3(vert_co, gz_ele->bases[base_index_vert]->obj->obmat, vert->co);
        ed_view3d_project_v2(vc.rgn, vert_co, vert_p_co);
        float len = len_v2v2(vert_p_co, mval_f);
        if (len < 35) {
          best.ele = (MElem *)eve_test;
          best.base_index = base_index_vert;
        }
        if (!mesh_vert_is_boundary(vert) &&
            EDBM_preselect_action_get(gz_ele->psel) != PRESELECT_ACTION_DELETE) {
          best.ele = (MElem *)eve_test;
          best.base_index = base_index_vert;
        }
      }

      /* Check above should never fail, if it does it's an internal error. */
      lib_assert(best.base_index != -1);

      Base *base = gz_ele->bases[best.base_index];
      best.ob = base->object;
    }
  }

  Mesh *mesh = NULL;

  gz_ele->base_index = -1;
  gz_ele->vert_index = -1;
  gz_ele->edge_index = -1;
  gz_ele->face_index = -1;

  if (best.ele) {
    gz_ele->base_index = best.base_index;
    mesh = mesh_editmesh_from_obj(gz_ele->bases[gz_ele->base_index]->obj)->mesh;
    mesh_elem_index_ensure(mesh, best.ele->head.htype);

    if (best.ele->head.htype == MESH_VERT) {
      gz_ele->vert_index = mesh_elem_index_get(best.ele);
    }
    else if (best.ele->head.htype == MESH_EDGE) {
      gz_ele->edge_index = mesh_elem_index_get(best.ele);
    }
    else if (best.ele->head.htype == MESH_FACE) {
      gz_ele->face_index = mesh_elem_index_get(best.ele);
    }
  }

  if (best.ele) {
    const float(*coords)[3] = NULL;
    {
      Object *ob = gz_ele->bases[gz_ele->base_index]->obj;
      Graph *graph = cxt_data_ensure_eval_graph(C);
      Mesh *me_eval = (Mesh *)graph_get_eval_id(graph, ob->data);
      if (me_eval->runtime.edit_data) {
        coords = me_eval->runtime.edit_data->vertexCos;
      }
    }
    EDBM_preselect_elem_update_from_single(gz_ele->psel, mesh, best.ele, coords);
    EDBM_preselect_elem_update_preview(gz_ele->psel, &vc, mesh, best.ele, mval);
  }
  else {
    EDBM_preselect_elem_clear(gz_ele->psel);
    EDBM_preselect_preview_clear(gz_ele->psel);
  }

  api_int_set(gz->ptr, "obj_index", gz_ele->base_index);
  api_int_set(gz->ptr, "vert_index", gz_ele->vert_index);
  api_int_set(gz->ptr, "edge_index", gz_ele->edge_index);
  api_int_set(gz->ptr, "face_index", gz_ele->face_index);

  if (best.ele) {
    ARgn *rgn = cxt_win_rgn(C);
    ed_rgn_tag_redraw_editor_overlays(rgn);
  }

  // return best.eed ? 0 : -1;
  return -1;
}

static void gizmo_preselect_elem_setup(WinGizmo *gz)
{
  MeshElemGizmo3D *gz_ele = (MeshElemGizmo3D *)gz;
  if (gz_ele->psel == NULL) {
    gz_ele->psel = EDBM_preselect_elem_create();
  }
  gz_ele->base_index = -1;
}

static void gizmo_preselect_elem_free(WinGizmo *gz)
{
  MeshElemGizmo3D *gz_ele = (MeshElemGizmo3D *)gz;
  EDBM_preselect_elem_destroy(gz_ele->psel);
  gz_ele->psel = NULL;
  MEM_SAFE_FREE(gz_ele->bases);
}

static int gizmo_preselect_elem_invoke(Cxt *UNUSED(C),
                                       WinGizmo *UNUSED(gz),
                                       const WinEv *UNUSED(event))
{
  return OP_PASS_THROUGH;
}

static void GIZMO_GT_mesh_preselect_elem_3d(WinGizmoType *gzt)
{
  /* ids */
  gzt->idname = "GIZMO_GT_mesh_preselect_elem_3d";

  /* api cbs */
  gzt->invoke = gizmo_preselect_elem_invoke;
  gzt->draw = gizmo_preselect_elem_draw;
  gzt->test_sel = gizmo_preselect_elem_test_sel;
  gzt->setup = gizmo_preselect_elem_setup;
  gzt->free = gizmo_preselect_elem_free;

  gzt->struct_size = sizeof(MeshElemGizmo3D);

  api_def_int(gzt->sapi, "obj_index", -1, -1, INT_MAX, "Obj Index", "", -1, INT_MAX);
  api_def_int(gzt->sapi, "vert_index", -1, -1, INT_MAX, "Vert Index", "", -1, INT_MAX);
  api_def_int(gzt->sapi, "edge_index", -1, -1, INT_MAX, "Edge Index", "", -1, INT_MAX);
  api_def_int(gzt->sapi, "face_index", -1, -1, INT_MAX, "Face Index", "", -1, INT_MAX);
}


/* Mesh Edge-Ring Pre-Select Gizmo API */
typedef struct MeshEdgeRingGizmo3D {
  WinGizmo gizmo;
  Base **bases;
  uint bases_len;
  int base_index;
  int edge_index;
  struct EditMesh_PreSelEdgeRing *psel;
} MeshEdgeRingGizmo3D;

static void gizmo_preselect_edgering_draw(const Cxt *C, WinGizmo *gz)
{
  if (!gizmo_preselect_poll_for_draw(C, gz)) {
    return;
  }

  MeshEdgeRingGizmo3D *gz_ring = (MeshEdgeRingGizmo3D *)gz;
  if (gz_ring->base_index != -1) {
    Obj *ob = gz_ring->bases[gz_ring->base_index]->obj;
    EDBM_preselect_edgering_draw(gz_ring->psel, ob->obmat);
  }
}

static int gizmo_preselect_edgering_test_sel(Cxt *C, WinGizmo *gz, const int mval[2])
{
  MeshEdgeRingGizmo3D *gz_ring = (MeshEdgeRingGizmo3D *)gz;
  struct {
    Obj *ob;
    MeshEdge *eed;
    float dist;
    int base_index;
  } best = {
      .dist = ed_view3d_sel_dist_px(),
  };

  struct {
    int base_index;
    int edge_index;
  } prev = {
      .base_index = gz_ring->base_index,
      .edge_index = gz_ring->edge_index,
  };

  {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    View3D *v3d = cxt_win_view3d(C);
    if (((gz_ring->bases)) == NULL || (gz_ring->bases[0] != view_layer->basact)) {
      MEM_SAFE_FREE(gz_ring->bases);
      gz_ring->bases = dune_view_layer_array_from_bases_in_edit_mode(
          view_layer, v3d, &gz_ring->bases_len);
    }
  }

  ViewCxt vc;
  em_setup_viewcxt(C, &vc);
  copy_v2_v2_int(vc.mval, mval);

  uint base_index;
  MeshEdge *eed_test = EDBM_edge_find_nearest_ex(
      &vc, &best.dist, NULL, false, false, NULL, gz_ring->bases, gz_ring->bases_len, &base_index);

  if (eed_test) {
    best.ob = gz_ring->bases[base_index]->object;
    best.eed = eed_test;
    best.base_index = base_index;
  }

  Mesh *mesh = NULL;
  if (best.eed) {
    gz_ring->base_index = best.base_index;
    mesh = dune_editmesh_from_obj(gz_ring->bases[gz_ring->base_index]->obj)->bm;
    mesh_elem_index_ensure(mesh, Mesh_EDGE);
    gz_ring->edge_index = mesh_elem_index_get(best.eed);
  }
  else {
    gz_ring->base_index = -1;
    gz_ring->edge_index = -1;
  }

  if ((prev.base_index == gz_ring->base_index) && (prev.edge_index == gz_ring->edge_index)) {
    /* pass (only recalculate on change) */
  }
  else {
    if (best.eed) {
      Obj *ob = gz_ring->bases[gz_ring->base_index]->obj;
      Scene *scene_eval = (Scene *)graph_get_eval_id(vc.graph, &vc.scene->id);
      Obj *ob_eval = graph_get_eval_obj(vc.graph, ob);
      MeshEdit *meshedit_eval = dune_editmesh_from_obj(ob_eval);
      /* Re-allocate coords each update isn't ideal, however we can't be sure
       * the mesh hasn't been edited since last update. */
      bool is_alloc = false;
      const float(*coords)[3] = dune_editmesh_vert_coords_when_deformed(
          vc.graph, meshedit_eval, scene_eval, ob_eval, NULL, &is_alloc);
      EDBM_preselect_edgering_update_from_edge(gz_ring->psel, mesh, best.eed, 1, coords);
      if (is_alloc) {
        mem_free((void *)coords);
      }
    }
    else {
      EDBM_preselect_edgering_clear(gz_ring->psel);
    }

    api_int_set(gz->ptr, "object_index", gz_ring->base_index);
    api_int_set(gz->ptr, "edge_index", gz_ring->edge_index);

    ARgn *rgn = cxt_win_rgn(C);
    ed_rgn_tag_redraw_editor_overlays(rgn);
  }

  // return best.eed ? 0 : -1;
  return -1;
}

static void gizmo_preselect_edgering_setup(WinGizmo *gz)
{
  MeshEdgeRingGizmo3D *gz_ring = (MeshEdgeRingGizmo3D *)gz;
  if (gz_ring->psel == NULL) {
    gz_ring->psel = EDBM_preselect_edgering_create();
  }
  gz_ring->base_index = -1;
}

static void gizmo_preselect_edgering_free(WinGizmo *gz)
{
  MeshEdgeRingGizmo3D *gz_ring = (MeshEdgeRingGizmo3D *)gz;
  EDBM_preselect_edgering_destroy(gz_ring->psel);
  gz_ring->psel = NULL;
  MEM_SAFE_FREE(gz_ring->bases);
}

static int gizmo_preselect_edgering_invoke(Cxt *UNUSED(C),
                                           WinGizmo *UNUSED(gz),
                                           const WinEv *UNUSED(ev))
{
  return OP_PASS_THROUGH;
}

static void GIZMO_GT_mesh_preselect_edgering_3d(WinGizmoType *gzt)
{
  /* ids */
  gzt->idname = "GIZMO_GT_mesh_preselect_edgering_3d";

  /* api cbs */
  gzt->invoke = gizmo_preselect_edgering_invoke;
  gzt->draw = gizmo_preselect_edgering_draw;
  gzt->test_sel = gizmo_preselect_edgering_test_sel;
  gzt->setup = gizmo_preselect_edgering_setup;
  gzt->free = gizmo_preselect_edgering_free;

  gzt->struct_size = sizeof(MeshEdgeRingGizmo3D);

  api_def_int(gzt->sapi, "object_index", -1, -1, INT_MAX, "Obj Index", "", -1, INT_MAX);
  api_def_int(gzt->sapi, "edge_index", -1, -1, INT_MAX, "Edge Index", "", -1, INT_MAX);
}

/* Gizmo API */
void ed_gizmotypes_preselect_3d(void)
{
  win_gizmotype_append(GIZMO_GT_mesh_preselect_elem_3d);
  win_gizmotype_append(GIZMO_GT_mesh_preselect_edgering_3d);
}

/* Gizmo Accessors
 * This avoids each user of the gizmo needing to write their own lookups to access
 * the information from this gizmo. */

void ed_view3d_gizmo_mesh_preselect_get_active(Cxt *C,
                                               WinGizmo *gz,
                                               Base **r_base,
                                               MeshElem **r_ele)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);

  const int obj_index = api_int_get(gz->ptr, "obj_index");

  /* weak, allocate an array just to access the index. */
  Base *base = NULL;
  Obj *obedit = NULL;
  {
    uint bases_len;
    Base **bases = dune_view_layer_array_from_bases_in_edit_mode(
        view_layer, cxt_win_view3d(C), &bases_len);
    if (obj_index < bases_len) {
      base = bases[obj_index];
      obedit = base->obj;
    }
    mem_free(bases);
  }

  *r_base = base;
  *r_ele = NULL;

  if (obedit) {
    MeshEdit *meshEdit = dune_mesh_edit_from_obj(obedit);
    Mesh *duneMesh = em->bm;
    ApiProp *prop;

    /* Ring select only defines edge, check props exist first. */
    prop = api_struct_find_prop(gz->ptr, "vert_index");
    const int vert_index = prop ? api_prop_int_get(gz->ptr, prop) : -1;
    prop = api_struct_find_prop(gz->ptr, "edge_index");
    const int edge_index = prop ? api_prop_int_get(gz->ptr, prop) : -1;
    prop = api_struct_find_prop(gz->ptr, "face_index");
    const int face_index = prop ? api_prop_int_get(gz->ptr, prop) : -1;

    if (vert_index != -1) {
      *r_ele = (MeshElem *)mesh_vert_at_index_find(mesh, vert_index);
    }
    else if (edge_index != -1) {
      *r_ele = (MeshElem *)mesh_edge_at_index_find(mesh, edge_index);
    }
    else if (face_index != -1) {
      *r_ele = (MeshElem *)mesh_face_at_index_find(mesh, face_index);
    }
  }
}

void ed_view3d_gizmo_mesh_preselect_clear(WinGizmo *gz)
{
  if (STREQ(gz->type->idname, "GIZMO_GT_mesh_preselect_elem_3d")) {
    MeshElemGizmo3D *gz_ele = (MeshElemGizmo3D *)gz;
    gz_ele->base_index = -1;
    gz_ele->vert_index = -1;
    gz_ele->edge_index = -1;
    gz_ele->face_index = -1;
  }
  else if (STREQ(gz->type->idname, "GIZMO_GT_mesh_preselect_edgering_3d")) {
    MeshEdgeRingGizmo3D *gz_ele = (MeshEdgeRingGizmo3D *)gz;
    gz_ele->base_index = -1;
    gz_ele->edge_index = -1;
  }
  else {
    lib_assert_unreachable();
  }

  const char *prop_ids[] = {"obj_index", "vert_index", "edge_index", "face_index"};
  for (int i = 0; i < ARRAY_SIZE(prop_ids); i++) {
    ApiProp *prop = api_struct_find_prop(gz->ptr, prop_ids[i]);
    if (prop == NULL) {
      continue;
    }
    api_prop_int_set(gz->ptr, prop, -1);
  }
}
