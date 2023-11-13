#include <stdio.h>
#include <string.h>

#include "types_defaults.h"
#include "types_pen.h"
#include "types_lightprobe.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_view3d.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_asset.h"
#include "dune_cxt.h"
#include "dune_curve.h"
#include "dune_global.h"
#include "dune_icons.h"
#include "dune_idprop.h"
#include "dune_lattice.h"
#include "dune_layer.h"
#include "dune_lib_remap.h"
#include "dune_main.h"
#include "dune_mball.h"
#include "dune_mesh.h"
#include "dune_object.h"
#include "dune_scene.h"
#include "dune_screen.h"
#include "dune_workspace.h"

#include "ed_object.h"
#include "ed_outliner.h"
#include "ed_render.h"
#include "ed_screen.h"
#include "ed_space_api.h"
#include "ed_transform.h"

#include "gpu_matrix.h"

#include "draw_engine.h"

#include "win_api.h"
#include "win_message.h"
#include "win_toolsystem.h"
#include "win_types.h"

#include "render_engine.h"
#include "render_pipeline.h"

#include "api_access.h"

#include "ui.h"
#include "ui_resources.h
//include "BPY_extern.h"

#include "graph.h"
#include "graph_build.h"

#include "view3d_intern.h" /* own include */
#include "view3d_nav.h"

/* manage rgns */

RgnView3D *ed_view3d_cxt_rv3d(Cxt *C)
{
  RgnView3D *rv3d = cxt_wm_rgn_view3d(C);

  if (rv3d == NULL) {
    ScrArea *area = cxt_win_area(C);
    if (area && area->spacetype == SPACE_VIEW3D) {
      ARgn *rgn = dune_area_find_rgn_active_win(area);
      if (rgn) {
        rv3d = rgn->rgndata;
      }
    }
  }
  return rv3d;
}

bool ed_view3d_cxt_user_rgn(Cxt *C, View3D **r_v3d, ARgn **r_rgn)
{
  ScrArea *area = cxt_win_area(C);

  *r_v3d = NULL;
  *r_rgn = NULL;

  if (area && area->spacetype == SPACE_VIEW3D) {
    ARgn *rgn = cxt_win_rgn(C);
    View3D *v3d = (View3D *)area->spacedata.first;

    if (region) {
      RgnView3D *rv3d;
      if ((rgn->rgntype == RGN_TYPE_WIN) && (rv3d = rgn->rgndata) &&
          (rv3d->viewlock & RV3D_LOCK_ROTATION) == 0) {
        *r_v3d = v3d;
        *r_rgn = rgn;
        return true;
      }

      if (ed_view3d_area_user_rgn(area, v3d, r_rgn)) {
        *r_v3d = v3d;
        return true;
      }
    }
  }

  return false;
}

bool ed_view3d_area_user_rgn(const ScrArea *area, const View3D *v3d, ARgn **r_rgn)
{
  RgnView3D *rv3d = NULL;
  ARgn *rgn_unlock_user = NULL;
  ARgn *rgn_unlock = NULL;
  const List *rgn_list = (v3d == area->spacedata.first) ? &area->rgnbase :
                                                          &v3d->rgnbase;

  lib_assert(v3d->spacetype == SPACE_VIEW3D);

  LIST_FOREACH (ARgn *, rgn, rgn_list) {
    /* find the first unlocked rv3d */
    if (rgn->rgndata && rgn->rgntype == RGN_TYPE_WIN) {
      rv3d = rgn->rgndata;
      if ((rv3d->viewlock & RV3D_LOCK_ROTATION) == 0) {
        rgn_unlock = rgn;
        if (ELEM(rv3d->persp, RV3D_PERSP, RV3D_CAMOB)) {
          rgn_unlock_user = rhn;
          break;
        }
      }
    }
  }

  /* camera/perspective view get priority when the active rgn is locked */
  if (rgn_unlock_user) {
    *r_rgn = rgn_unlock_user;
    return true;
  }

  if (rgn_unlock) {
    *r_rgn = rgn_unlock;
    return true;
  }

  return false;
}

void ed_view3d_init_mats_rv3d(const struct Obj *ob, struct RgnView3D *rv3d)
{
  /* local viewmat and persmat, to calculate projections */
  mul_m4_m4m4(rv3d->viewmatob, rv3d->viewmat, ob->obmat);
  mul_m4_m4m4(rv3d->persmatob, rv3d->persmat, ob->obmat);

  /* initializes object space clipping, speeds up clip tests */
  ed_view3d_clipping_local(rv3d, ob->obmat);
}

void ed_view3d_init_mats_rv3d_gl(const struct Obj *ob, struct RgnView3D *rv3d)
{
  ed_view3d_init_mats_rv3d(ob, rv3d);

  /* we have to multiply instead of loading viewmatob to make
   * it work with duplis using displists, otherwise it will
   * override the dupli-matrix */
  gpu_matrix_mul(ob->obmat);
}

#ifdef DEBUG
void ed_view3d_clear_mats_rv3d(struct RgnView3D *rv3d)
{
  zero_m4(rv3d->viewmatob);
  zero_m4(rv3d->persmatob);
}

void ee_view3d_check_mats_rv3d(struct RgnView3D *rv3d)
{
  LIB_ASSERT_ZERO_M4(rv3d->viewmatob);
  LIB_ASSERT_ZERO_M4(rv3d->persmatob);
}
#endif

void ed_view3d_stop_render_preview(WinManager *wm, ARgn *rgn)
{
  RgnView3D *rv3d = rgn->rgndata;

  if (rv3d->render_engine) {
    win_jobs_kill_type(wm, rgn, WIN_JOB_TYPE_RENDER_PREVIEW);

    render_engine_free(rv3d->render_engine);
    rv3d->render_engine = NULL;
  }

  /* A bit overkill but this make sure the viewport is reset completely. (fclem) */
  win_draw_rgn_free(rgn, false);
}

void ed_view3d_shade_update(Main *main, View3D *v3d, ScrArea *area)
{
  WinMngr *wm = main->wm.first;

  if (v3d->shading.type != OB_RENDER) {
    ARgn *rgn;

    for (rgn = area->rgnbase.first; rgn; rgn = rgn->next) {
      if ((rgn->rgntype == RGN_TYPE_WIN) && rgn->rgndata) {
        ed_view3d_stop_render_preview(wm, rgn);
      }
    }
  }
}

/* default cbs for view3dspace* */
static SpaceLink *view3d_create(const ScrArea *UNUSED(area), const Scene *scene)
{
  ARgn *rgn;
  View3D *v3d;
  RgnView3D *rv3d;

  v3d = types_struct_default_alloc(View3D);

  if (scene) {
    v3d->camera = scene->camera;
  }

  /* header */
  region = mem_calloc(sizeof(ARgn), "header for view3d");

  lib_addtail(&v3d->regionbase, rgn);
  region->rgntype = RGN_TYPE_HEADER;
  rgn->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* tool header */
  rgn = mem_calloc(sizeof(ARgn), "tool header for view3d");

  lib_addtail(&v3d->regionbase, rgn);
  rgn->rgntype = RGN_TYPE_TOOL_HEADER;
  rgn->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;
  rgn->flag = RGN_FLAG_HIDDEN | RGN_FLAG_HIDDEN_BY_USER;

  /* tool shelf */
  rgn = mem_calloc(sizeof(ARgn), "toolshelf for view3d");

  lib_addtail(&v3d->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_TOOLS;
  rgn->alignment = RGN_ALIGN_LEFT;
  rgn->flag = RGN_FLAG_HIDDEN;

  /* buttons/list view */
  rgn = mem_calloc(sizeof(ARgn), "btns for view3d");

  lib_addtail(&v3d->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_UI;
  rgn->alignment = RGN_ALIGN_RIGHT;
  rgn->flag = RGN_FLAG_HIDDEN;

  /* main region */
  rgn = mem_calloc(sizeof(ARgn), "main rhn for view3d");

  lib_addtail(&v3d->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_WIN;

  rgn->rgndata = mem_calloc(sizeof(RgnView3D), "rgn view3d");
  rv3d = rgn->rgndata;
  rv3d->viewquat[0] = 1.0f;
  rv3d->persp = RV3D_PERSP;
  rv3d->view = RV3D_VIEW_USER;
  rv3d->dist = 10.0;

  return (SpaceLink *)v3d;
}

/* not spacelink itself */
static void view3d_free(SpaceLink *sl)
{
  View3D *vd = (View3D *)sl;

  if (vd->localvd) {
    mem_free(vd->localvd);
  }

  MEM_SAFE_FREE(vd->runtime.local_stats);

  if (vd->runtime.props_storage) {
    mem_free(vd->runtime.props_storage);
  }

  if (vd->shading.prop) {
    IDP_FreeProp(vd->shading.prop);
    vd->shading.prop = NULL;
  }
}

/* spacetype; init callback */
static void view3d_init(WinMngr *UNUSED(wm), ScrArea *UNUSED(area))
{
}

static void view3d_exit(WinMngr *UNUSED(wm), ScrArea *area)
{
  lib_assert(area->spacetype == SPACE_VIEW3D);
  View3D *v3d = area->spacedata.first;
  MEM_SAFE_FREE(v3d->runtime.local_stats);
}

static SpaceLink *view3d_duplicate(SpaceLink *sl)
{
  View3D *v3do = (View3D *)sl;
  View3D *v3dn = mem_dupalloc(sl);

  memset(&v3dn->runtime, 0x0, sizeof(v3dn->runtime));

  /* clear or remove stuff from old */
  if (v3dn->localvd) {
    v3dn->localvd = NULL;
  }

  v3dn->local_collections_uuid = 0;
  v3dn->flag &= ~(V3D_LOCAL_COLLECTIONS | V3D_XR_SESSION_MIRROR);

  if (v3dn->shading.type == OB_RENDER) {
    v3dn->shading.type = OB_SOLID;
  }

  if (v3dn->shading.prop) {
    v3dn->shading.prop = IDP_CopyProp(v3do->shading.prop);
  }

  /* copy or clear inside new stuff */
  return (SpaceLink *)v3dn;
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void view3d_main_rgn_init(WinMngr *wm, ARgn *rgn)
{
  List *list;
  WinKeyMap *keymap;

  /* object ops. */
  /* important to be before Pose keymap since they can both be enabled at once */
  keymap = win_keymap_ensure(wm->defaultconf, "Paint Face Mask (Weight, Vertex, Texture)", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Paint Vertex Selection (Weight, Vertex)", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* Before 'Weight/Vertex Paint' so adding curve points is not overridden. */
  keymap = win_keymap_ensure(wm->defaultconf, "Paint Curve", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* Before 'Pose' so weight paint menus aren't overridden by pose menus. */
  keymap = win_keymap_ensure(wm->defaultconf, "Weight Paint", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Vertex Paint", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* pose is not modal, operator poll checks for this */
  keymap = win_keymap_ensure(wm->defaultconf, "Pose", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Object Mode", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Curve", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Image Paint", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Sculpt", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Mesh", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Armature", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Metaball", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Lattice", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Particle", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Sculpt Curves", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* editfont keymap swallows all... */
  keymap = win_keymap_ensure(wm->defaultconf, "Font", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Object Non-modal", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Frames", 0, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* own keymap, last so modes can override it */
  keymap = win_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = won_keymap_ensure(wm->defaultconf, "3D View", SPACE_VIEW3D, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* add drop boxes */
  lb = win_dropboxmap_find("View3D", SPACE_VIEW3D, RGN_TYPE_WIN);

  win_ev_add_dropbox_handler(&rgn->handlers, list);
}

static void view3d_main_rgn_exit(WinMngr *wm, ARgn  *rgn)
{
  ed_view3d_stop_render_preview(wm, rgn);
}

static bool view3d_drop_in_main_rgn_poll(Cxt *C, const WinEv *ev)
{
  ScrArea *area = cxt_win_area(C);
  return ed_rgn_overlap_isect_any_xy(area, ev->xy) == false;
}

static IdType view3d_drop_id_in_main_rgn_poll_get_id_type(Cxt *C,
                                                              WinDrag *drag,
                                                              const WinEv *ev)
{
  const ScrArea *area = cxt_win_area(C);

  if (ed_rgn_overlap_isect_any_xy(area, ev->xy)) {
    return 0;
  }
  if (!view3d_drop_in_main_rgn_poll(C, ev)) {
    return 0;
  }

  Id *local_id = win_drag_get_local_id(drag, 0);
  if (local_id) {
    return GS(local_id->name);
  }

  WinDragAsset *asset_drag = win_drag_get_asset_data(drag, 0);
  if (asset_drag) {
    return asset_drag->id_type;
  }

  return 0;
}

static bool view3d_drop_id_in_main_rgn_poll(Cxt *C,
                                            WinDrag *drag,
                                            const WinEv *ev,
                                            IdType id_type)
{
  if (!view3d_drop_in_main_rgn_poll(C, ev)) {
    return false;
  }

  return win_drag_is_id_type(drag, id_type);
}

static void view3d_ob_drop_draw_activate(struct WinDropBox *drop, WinDrag *drag)
{
  V3DSnapCursorState *state = drop->draw_data;
  if (state) {
    return;
  }

  /* Don't use the snap cursor when linking the obj. Obj transform isn't editable then and
   * would be reset on reload. */
  if (win_drag_asset_will_import_linked(drag)) {
    return;
  }

  state = drop->draw_data = ed_view3d_cursor_snap_active();
  state->draw_plane = true;

  float dimensions[3] = {0.0f};
  if (drag->type == WIN_DRAG_ID) {
    Object *ob = (Object *)win_drag_get_local_id(drag, ID_OB);
    dune_object_dimensions_get(ob, dimensions);
  }
  else {
    struct AssetMetaData *meta_data = win_drag_get_asset_meta_data(drag, ID_OB);
    IdProp *dimensions_prop = dune_asset_metadata_idprop_find(meta_data, "dimensions");
    if (dimensions_prop) {
      copy_v3_v3(dimensions, IDP_Array(dimensions_prop));
    }
  }

  if (!is_zero_v3(dimensions)) {
    mul_v3_v3fl(state->box_dimensions, dimensions, 0.5f);
    ui_GetThemeColor4ubv(TH_GIZMO_PRIMARY, state->color_box);
    state->draw_box = true;
  }
}

static void view3d_ob_drop_draw_deactivate(struct WinDropBox *drop, WinDrag *UNUSED(drag))
{
  V3DSnapCursorState *state = drop->draw_data;
  if (state) {
    ed_view3d_cursor_snap_deactive(state);
    drop->draw_data = NULL;
  }
}

static bool view3d_ob_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  return view3d_drop_id_in_main_rgn_poll(C, drag, ev, ID_OB);
}
static bool view3d_ob_drop_poll_external_asset(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  if (!view3d_ob_drop_poll(C, drag, ev) || (drag->type != WIN_DRAG_ASSET)) {
    return false;
  }
  return true;
}

/* the term local here refers to not being an external asset,
 * poll will succeed for linked library objects. */
static bool view3d_ob_drop_poll_local_id(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  if (!view3d_ob_drop_poll(C, drag, ev) || (drag->type != WIN_DRAG_ID)) {
    return false;
  }
  return true;
}

static bool view3d_collection_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  return view3d_drop_id_in_main_rgn_poll(C, drag, ev, ID_GR);
}

static bool view3d_mat_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  return view3d_drop_id_in_main_rgn_poll(C, drag, ev, ID_MA);
}

static char *view3d_mat_drop_tooltip(Cxt *C,
                                     WinDrag *drag,
                                     const int xy[2],
                                     struct WinDropBox *drop)
{
  const char *name = win_drag_get_item_name(drag);
  ARgn *rgn = cxt_win_rgn(C);
  api_string_set(drop->ptr, "name", name);
  int mval[2] = {
      xy[0] - rgn->winrct.xmin,
      xy[1] - rgn->winrct.ymin,
  };
  return ed_obj_ot_drop_named_material_tooltip(C, drop->ptr, mval);
}

static bool view3d_world_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  return view3d_drop_id_in_main_rgn_poll(C, drag, ev, ID_WO);
}

static bool view3d_obj_data_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  IdType id_type = view3d_drop_id_in_main_rgn_poll_get_id_type(C, drag, ev);
  if (id_type && OB_DATA_SUPPORT_ID(id_type)) {
    return true;
  }
  return false;
}

static char *view3d_obj_data_drop_tooltip(Cxt *UNUSED(C),
                                          WinDrag *UNUSED(drag),
                                          const int UNUSED(xy[2]),
                                          WinDropBox *UNUSED(drop))
{
  return lib_strdup(TIP_("Create object instance from object-data"));
}

static bool view3d_ima_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  if (ed_rgn_overlap_isect_any_xy(cxt_win_area(C), ev->xy)) {
    return false;
  }
  if (drag->type == WIN_DRAG_PATH) {
    /* rule might not work? */
    return (ELEM(drag->icon, 0, ICON_FILE_IMAGE, ICON_FILE_MOVIE));
  }

  return win_drag_is_id_type(drag, ID_IM);
}

static bool view3d_ima_bg_is_camera_view(Cxt *C)
{
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  if ((rv3d && (rv3d->persp == RV3D_CAMOB))) {
    View3D *v3d = cxt_win_view3d(C);
    if (v3d && v3d->camera && v3d->camera->type == OB_CAMERA) {
      return true;
    }
  }
  return false;
}

static bool view3d_ima_bg_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  if (!view3d_ima_drop_poll(C, drag, ev)) {
    return false;
  }

  if (ed_view3d_is_object_under_cursor(C, ev->mval)) {
    return false;
  }

  return view3d_ima_bg_is_camera_view(C);
}

static bool view3d_ima_empty_drop_poll(bContext *C, wmDrag *drag, const wmEvent *event)
{
  if (!view3d_ima_drop_poll(C, drag, event)) {
    return false;
  }

  Object *ob = ed_view3d_give_obj_under_cursor(C, ev->mval);

  if (ob == NULL) {
    return true;
  }

  if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE) {
    return true;
  }

  return false;
}

static bool view3d_volume_drop_poll(Cxt *UNUSED(C),
                                    WinDrag *drag,
                                    const WinEv *UNUSED(ev))
{
  return (drag->type == WIN_DRAG_PATH) && (drag->icon == ICON_FILE_VOLUME);
}

static void view3d_ob_drop_matrix_from_snap(V3DSnapCursorState *snap_state,
                                            Object *ob,
                                            float obmat_final[4][4])
{
  V3DSnapCursorData *snap_data;
  snap_data = ed_view3d_cursor_snap_data_get(snap_state, NULL, 0, 0);
  lib_assert(snap_state->draw_box || snap_state->draw_plane);
  copy_m4_m3(obmat_final, snap_data->plane_omat);
  copy_v3_v3(obmat_final[3], snap_data->loc);

  float scale[3];
  mat4_to_size(scale, ob->obmat);
  rescale_m4(obmat_final, scale);

  BoundBox *bb = dune_object_boundbox_get(ob);
  if (bb) {
    float offset[3];
    dune_boundbox_calc_center_aabb(bb, offset);
    offset[2] = bb->vec[0][2];
    mul_mat3_m4_v3(obmat_final, offset);
    sub_v3_v3(obmat_final[3], offset);
  }
}

static void view3d_ob_drop_copy_local_id(WinDrag *drag, WinDropBox *drop)
{
  Id *id = win_drag_get_local_id(drag, ID_OB);

  api_string_set(drop->ptr, "name", id->name + 2);
  /* Don't duplicate Id's which were just imported. Only do that for existing, local IDs. */
  lib_assert(drag->type != WIN_DRAG_ASSET);

  V3DSnapCursorState *snap_state = ed_view3d_cursor_snap_state_get();
  float obmat_final[4][4];

  view3d_ob_drop_matrix_from_snap(snap_state, (Object *)id, obmat_final);

  api_float_set_array(drop->ptr, "matrix", &obmat_final[0][0]);
}

static void view3d_ob_drop_copy_external_asset(WinDrag *drag, WinDropBox *drop)
{
  /* Sel is handled here, de-sel objs before append,
   * using auto-select to ensure the new objs are sel'd.
   * This is done so OBJ_OT_transform_to_mouse (which runs after this drop handler)
   * can use the cxt setup here to place the objs. */
  lib_assert(drag->type == WIN_DRAG_ASSET);

  WinDragAsset *asset_drag = win_drag_get_asset_data(drag, 0);
  Cxt *C = asset_drag->evil_C;
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  dune_view_layer_base_desel_all(view_layer);

  Id *id = win_drag_asset_id_import(asset_drag, FILE_AUTOSEL);

  /* Only update relations for the current scene. */
  grapg_tag_update(cxt_data_main(C));
  win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

  api_string_set(drop->ptr, "name", id->name + 2);

  Base *base = dune_view_layer_base_find(view_layer, (Obj *)id);
  if (base != NULL) {
    dune_view_layer_base_sel_and_set_active(view_layer, base);
    win_main_add_notifier(NC_SCENE | ND_OB_ACTIVE, scene);
  }
  graph_id_tag_update(&scene->id, ID_RECALC_SEL);
  ee_outliner_sel_sync_from_object_tag(C);

  V3DSnapCursorState *snap_state = drop->draw_data;
  if (snap_state) {
    float obmat_final[4][4];

    view3d_ob_drop_matrix_from_snap(snap_state, (Obj *)id, obmat_final);

    api_float_set_array(drop->ptr, "matrix", &obmat_final[0][0]);
  }
}

static void view3d_collection_drop_copy(WinDrag *drag, WinDropBox *drop)
{
  Id *id = win_drag_get_local_id_or_import_from_asset(drag, ID_GR);

  api_int_set(drop->ptr, "session_uuid", (int)id->session_uuid);
}

static void view3d_id_drop_copy(WinDrag *drag, WinDropBox *drop)
{
  Id *id = win_drag_get_local_id_or_import_from_asset(drag, 0);

  api_string_set(drop->ptr, "name", id->name + 2);
}

static void view3d_id_drop_copy_with_type(WinDrag *drag, WinDropBox *drop)
{
  Id *id = win_drag_get_local_id_or_import_from_asset(drag, 0);

  api_string_set(drop->ptr, "name", id->name + 2);
  api_enum_set(drop->ptr, "type", GS(id->name));
}

static void view3d_id_path_drop_copy(WinDrag *drag, WinDropBox *drop)
{
  Id *id = win_drag_get_local_id_or_import_from_asset(drag, 0);

  if (id) {
    api_string_set(drop->ptr, "name", id->name + 2);
    api_struct_prop_unset(drop->ptr, "filepath");
  }
  else if (drag->path[0]) {
    api_string_set(drop->ptr, "filepath", drag->path);
    api_struct_prop_unset(drop->ptr, "image");
  }
}

static void view3d_lightcache_update(Cxt *C)
{
  return;
}

/* rgn dropbox def */
static void view3d_dropboxes(void)
{
  List *list = win_dropboxmap_find("View3D", SPACE_VIEW3D, RGN_TYPE_WIN);

  struct WinDropBox *drop;
  drop = win_dropbox_add(list,
                        "OBJ_OT_add_named",
                        view3d_ob_drop_poll_local_id,
                        view3d_ob_drop_copy_local_id,
                        win_drag_free_imported_drag_id,
                        NULL);

  drop->draw = win_drag_draw_item_name_fn;
  drop->draw_activate = view3d_ob_drop_draw_activate;
  drop->draw_deactivate = view3d_ob_drop_draw_deactivate;

  drop = win_dropbox_add(list,
                        "OBJ_OT_transform_to_mouse",
                        view3d_ob_drop_poll_external_asset,
                        view3d_ob_drop_copy_external_asset,
                        win_drag_free_imported_drag_id,
                        NULL);

  drop->draw = win_drag_draw_item_name_fn;
  drop->draw_activate = view3d_ob_drop_draw_activate;
  drop->draw_deactivate = view3d_ob_drop_draw_deactivate;

  win_dropbox_add(list,
                 "OBJ_OT_drop_named_material",
                 view3d_mat_drop_poll,
                 view3d_id_drop_copy,
                 win_drag_free_imported_drag_id,
                 view3d_mat_drop_tooltip);
  win_dropbox_add(list,
                 "VIEW3D_OT_background_image_add",
                 view3d_ima_bg_drop_poll,
                 view3d_id_path_drop_copy,
                 win_drag_free_imported_drag_id,
                 NULL);
  win_dropbox_add(list,
                 "OBJ_OT_drop_named_image",
                 view3d_ima_empty_drop_poll,
                 view3d_id_path_drop_copy,
                 win_drag_free_imported_drag_id,
                 NULL);
  win_dropbox_add(list,
                 "OBJ_OT_volume_import",
                 view3d_volume_drop_poll,
                 view3d_id_path_drop_copy,
                 win_drag_free_imported_drag_id,
                 NULL);
  win_dropbox_add(list,
                 "OBJ_OT_collection_instance_add",
                 view3d_collection_drop_poll,
                 view3d_collection_drop_copy,
                 win_drag_free_imported_drag_id,
                 NULL);
  28'_dropbox_add(list,
                 "OBJ_OT_data_instance_add",
                 view3d_object_data_drop_poll,
                 view3d_id_drop_copy_with_type,
                 win_drag_free_imported_drag_id,
                 view3d_object_data_drop_tooltip);
  win_dropbox_add(list,
                 "VIEW3D_OT_drop_world",
                 view3d_world_drop_poll,
                 view3d_id_drop_copy,
                 win_drag_free_imported_drag_id,
                 NULL);
}

static void view3d_widgets(void)
{
  WinGizmoMapType *gzmap_type = win_gizmomaptype_ensure(
      &(const struct WinGizmoMapType_Params){SPACE_VIEW3D, RGN_TYPE_WINDOW});

  win_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_xform_gizmo_cxt);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_spot);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_area);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_target);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_force_field);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_camera);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_camera_view);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_empty_image);
  /* TODO: Not working well enough, disable for now. */
#if 0
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_armature_spline);
#endif

  WM_gizmogrouptype_append(VIEW3D_GGT_xform_gizmo);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_cage);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_shear);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_extrude);
  WM_gizmogrouptype_append(VIEW3D_GGT_mesh_presel_elem);
  WM_gizmogrouptype_append(VIEW3D_GGT_mesh_presel_edgering);
  WM_gizmogrouptype_append(VIEW3D_GGT_tool_generic_handle_normal);
  WM_gizmogrouptype_append(VIEW3D_GGT_tool_generic_handle_free);

  WM_gizmogrouptype_append(VIEW3D_GGT_ruler);
  WM_gizmotype_append(VIEW3D_GT_ruler_item);

  WM_gizmogrouptype_append(VIEW3D_GGT_placement);

  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_nav);
  WM_gizmotype_append(VIEW3D_GT_nav_rotate);
}

/* type cb, not rgn itself */
static void view3d_main_rgn_free(ARgn *rgn)
{
  RgnView3D *rv3d = rgn->rgndata;

  if (rv3d) {
    if (rv3d->localvd) {
      mem_free(rv3d->localvd);
    }
    if (rv3d->clipbb) {
      mem_free(rv3d->clipbb);
    }

    if (rv3d->render_engine) {
      render_engine_free(rv3d->render_engine);
    }

    if (rv3d->sms) {
      mem_free(rv3d->sms);
    }

    mem_free(rv3d);
    rgn->rgndata = NULL;
  }
}

/* copy rgndata */
static void *view3d_main_rgn_duplicate(void *poin)
{
  if (poin) {
    RgnView3D *rv3d = poin, *new;

    new = mem_dupalloc(rv3d);
    if (rv3d->localvd) {
      new->localvd = mem_dupalloc(rv3d->localvd);
    }
    if (rv3d->clipbb) {
      new->clipbb = mem_dupalloc(rv3d->clipbb);
    }

    new->render_engine = NULL;
    new->sms = NULL;
    new->smooth_timer = NULL;

    return new;
  }
  return NULL;
}

static void view3d_main_rgn_listener(const wmRegionListenerParams *params)
{
  Win *win = params->win;
  ScrArea *area = params->area;
  ARgn *rgn = params->rgn;
  WinNotifier *winn = params->notifier;
  const Scene *scene = params->scene;
  View3D *v3d = area->spacedata.first;
  RgnView3D *rv3d = rgn->rgndata;
  WinGizmoMap *gzmap = rgn->gizmo_map;

  /* czt changes */
  switch (winn->category) {
    case NC_WM:
      if (ELEM(winn->data, ND_UNDO)) {
        win_gizmomap_tag_refresh(gzmap);
      }
      else if (ELEM(winn->data, ND_XR_DATA_CHANGED)) {
        /* Only cause a redraw if this a VR session mirror. Should more features be added that
         * require redraws, we could pass something to winn->ref, e.g. the flag value. */
        if (v3d->flag & V3D_XR_SESSION_MIRROR) {
          ed_rgn_tag_redraw(rgn);
        }
      }
      break;
    case NC_ANIM:
      switch (winn->data) {
        case ND_KEYFRAME_PROP:
        case ND_NLA_ACTCHANGE:
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_NLA:
        case ND_KEYFRAME:
          if (ELEM(winn->action, NA_EDITED, NA_ADDED, NA_REMOVED)) {
            ed_rgn_tag_redraw(rgn);
          }
          break;
        case ND_ANIMCHAN:
          if (ELEM(winn->action, NA_EDITED, NA_ADDED, NA_REMOVED, NA_SELECTED)) {
            ED_rhn_tag_redraw(rgn);
          }
          break;
      }
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_SCENEBROWSE:
        case ND_LAYER_CONTENT:
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
        case ND_LAYER:
          if (winn->ref) {
            dune_screen_view3d_sync(v3d, winn->ref);
          }
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
        case ND_OB_ACTIVE:
        case ND_OB_SELECT:
          ATTR_FALLTHROUGH;
        case ND_FRAME:
        case ND_TRANSFORM:
        case ND_OB_VISIBLE:
        case ND_RENDER_OPTIONS:
        case ND_MARKERS:
        case ND_MODE:
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
        case ND_WORLD:
          /* handled by space_view3d_listener() for v3d access */
          break;
        case ND_DRAW_RENDER_VIEWPORT: {
          if (v3d->camera && (scene == winn->ref)) {
            if (rv3d->persp == RV3D_CAMOB) {
              ed_rgn_tag_redraw(rgn);
            }
          }
          break;
        }
      }
      if (winn->action == NA_EDITED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_OBJECT:
      switch (winn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SELECT:
        case ND_TRANSFORM:
        case ND_POSE:
        case ND_DRAW:
        case ND_MOD:
        case ND_SHADERFX:
        case ND_CONSTRAINT:
        case ND_KEYS:
        case ND_PARTICLE:
        case ND_POINTCACHE:
        case ND_LOD:
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
        case ND_DRAW_ANIMVIZ:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      switch (win->action) {
        case NA_ADDED:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_GEOM:
      switch (winn->data) {
        case ND_SELECT: {
          win_gizmomap_tag_refresh(gzmap);
          ATTR_FALLTHROUGH;
        }
        case ND_DATA:
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
        case ND_VERTEX_GROUP:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      switch (winn->action) {
        case NA_EDITED:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_CAMERA:
      switch (winn->data) {
        case ND_DRAW_RENDER_VIEWPORT: {
          if (v3d->camera && (v3d->camera->data == winn->ref)) {
            if (rv3d->persp == RV3D_CAMOB) {
              ed_rgn_tag_redraw(rgn);
            }
          }
          break;
        }
      }
      break;
    case NC_GROUP:
      /* all group ops for now */
      ed_rgn_tag_redraw(rgn);
      break;
    case NC_BRUSH:
      switch (winn->action) {
        case NA_EDITED:
          ed_rgn_tag_redraw_cursor(rgn);
          break;
        case NA_SELECTED:
          /* used on brush changes needed bc 3d cursor
           * has to be drawn if clone brush is sel */
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_MATERIAL:
      switch (winn->data) {
        case ND_SHADING:
        case ND_NODES:
          /* TODO: This is a bit too much updates, but needed to
           * have proper material drivers update in the viewport.
           * How to solve?  */
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_SHADING_DRAW:
        case ND_SHADING_LINKS:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_WORLD:
      switch (winn->data) {
        case ND_WORLD_DRAW:
          /* handled by space_view3d_listener() for v3d access */
          break;
        case ND_WORLD:
          /* Needed for updating world materials */
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_LAMP:
      switch (winn->data) {
        case ND_LIGHTING:
          /* TODO: Too much but needed to
           * handle updates from new graph.  */
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_LIGHTING_DRAW:
          ed_rgn_tag_redraw(rgn);
          win_gizmomap_tag_refresh(gzmap);
          break;
      }
      break;
    case NC_LIGHTPROBE:
      ed_area_tag_refresh(area);
      break;
    case NC_IMAGE:
      /* this could be more fine grained checks if we had
       * more cxt than just the rgn */
      ed_rgn_tag_redraw(rgn);
      break;
    case NC_TEXTURE:
      /* same as above */
      ed_rgn_tag_redraw(rgn);
      break;
    case NC_MOVIECLIP:
      if (winn->data == ND_DISPLAY || wmn->action == NA_EDITED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_VIEW3D) {
        if (winn->subtype == NS_VIEW3D_GPU) {
          rv3d->rflag |= RV3D_GPULIGHT_UPDATE;
        }
        else if (winn->subtype == NS_VIEW3D_SHADING) {
#ifdef WITH_XR_OPENXR
          ed_view3d_xr_shading_update(G_MAIN->win.first, v3d, scene);
#endif

          ViewLayer *view_layer = win_get_active_view_layer(win);
          Graph *graph = dune_scene_get_graph(scene, view_layer);
          if (graph) {
            ed_render_view3d_update(graph, win, area, true);
          }
        }
        ed_rgn_tag_redraw(rgn);
        win_gizmomap_tag_refresh(gzmap);
      }
      break;
    case NC_ID:
      if (ELEM(winn->action, NA_RENAME, NA_EDITED, NA_ADDED, NA_REMOVED)) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_SCREEN:
      switch (winn->data) {
        case ND_ANIMPLAY:
        case ND_SKETCH:
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_LAYOUTBROWSE:
        case ND_LAYOUTDELETE:
        case ND_LAYOUTSET:
          win_gizmomap_tag_refresh(gzmap);
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_LAYER:
          ed_rgn_tag_redraw(rgn);
          break;
      }

      break;
    case NC_PEN:
      if (winn->data == ND_DATA || ELEM(winn->action, NA_EDITED, NA_SELECTED)) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
  }
}

static void view3d_main_rgn_msg_sub(const WinRgnMsgSubParams *params)
{
  struct WinMsgBus *mbus = params->msg_bus;
  const Cxt *C = params->context;
  ScrArea *area = params->area;
  ARgn *rgn = params->rgn;

  /* Are many props that impact 3Dview drawing,
   * instead of subbing to individual props, sub to types
   * accepting some redundant redraws.
   * For other space types we might try avoid this, keep the 3Dview as an exceptional case! */
  WinMsgParamsApi msg_key_params = {{0}};

  /* Only subscribe to types. */
  ApiStruct *type_array[] = {
      &ApiWin,

      /* These object have props that impact drawing */
      &ApiAreaLight,
      &ApiCamera,
      &ApiLight,
      &ApiSpeaker,
      &ApiSunLight,

      /* General types the 3Dview deps on */
      &ApiObj,
      &ApiUnitSettings, /* grid-floor */

      &ApiView3DCursor,
      &ApiView3DOverlay,
      &ApiView3DShading,
      &ApiWorld,
  };

  WinMsgSubVal msg_sub_val_rgn_tag_redraw = {
      .owner = rgn,
      .user_data = rgn,
      .notify = ed_rgn_do_msg_notify_tag_redraw,
  };

  for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
    msg_key_params.ptr.type = type_array[i];
    win_msg_subscribe_api_params(mbus, &msg_key_params, &msg_sub_val_rgn_tag_redraw, __func__);
  }

  /* Sub to a handful of other props */
  RgnView3D *rv3d = rgn->rgndata;

  win_msg_sub_api_anon_prop(mbus, RenderSettings, engine, &msg_sub_value_rgn_tag_redraw);
  win_msg_sub_api_anon_prop(
      mbus, RenderSettings, resolution_x, &msg_sub_value_rgn_tag_redraw);
  win_msg_sub_rna_anon_prop(
      mbus, RenderSettings, resolution_y, &msg_sub_value_rgn_tag_redraw);
  win_msg_sub_api_anon_prop(
      mbus, RenderSettings, pixel_aspect_x, &msg_sub_val_rgn_tag_redraw);
  win_msg_sub_api_anon_prop(
      mbus, RenderSettings, pixel_aspect_y, &msg_sub_val_rgn_tag_redraw);
  if (rv3d->persp == RV3D_CAMOB) {
    win_msg_sub_api_anon_prop(
        mbus, RenderSettings, use_border, &msg_sub_val_rgn_tag_redraw);
  }

  win_msg_sub_api_anon_type(mbus, SceneEEVEE, &msg_sub_val_rgn_tag_redraw);
  win_msg_sub_api_anon_type(mbus, SceneDisplay, &msg_sub_val_rgn_tag_redraw);
  win_msg_sub_api_anon_type(mbus, ObjDisplay, &msg_sub_val_rgn_tag_redraw);

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *obact = OBACT(view_layer);
  if (obact != NULL) {
    switch (obact->mode) {
      case OB_MODE_PARTICLE_EDIT:
        win_msg_sub_api_anon_type(mbus, ParticleEdit, &msg_sub_value_rgn_tag_redraw);
        break;
      default:
        break;
    }
  }

  {
    WinMsgSubVal msg_sub_value_rgn_tag_refresh = {
        .owner = rgn,
        .user_data = area,
        .notify = win_toolsystem_do_msg_notify_tag_refresh,
    };
    WM_msg_subscribe_rna_anon_prop(mbus, Object, mode, &msg_sub_value_rgn_tag_refresh);
    WM_msg_subscribe_rna_anon_prop(mbus, LayerObjects, active, &msg_sub_value_rgn_tag_refresh);
  }
}

/* concept is to retrieve cursor type context-less */
static void view3d_main_rgn_cursor(Win *win, ScrArea *area, ARgn *rgn)
{
  if (win_cursor_set_from_tool(win, area, rgn)) {
    return;
  }

  ViewLayer *view_layer = win_get_active_view_layer(win);
  Object *obedit = OBEDIT_FROM_VIEW_LAYER(view_layer);
  if (obedit) {
    win_cursor_set(win, WIN_CURSOR_EDIT);
  }
  else {
    win_cursor_set(win, WIN_CURSOR_DEFAULT);
  }
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void view3d_header_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap = win_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);

  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  ed_rgn_header_init(rgn);
}

static void view3d_header_rgn_draw(const Cxt *C, ARgn *rgn)
{
  ED_region_header(C, region);
}

static void view3d_header_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_SCENE:
      switch (winn->data) {
        case ND_FRAME:
        case ND_OB_ACTIVE:
        case ND_OB_SEL:
        case ND_OB_VISIBLE:
        case ND_MODE:
        case ND_LAYER:
        case ND_TOOLSETTINGS:
        case ND_LAYER_CONTENT:
        case ND_RENDER_OPTIONS:
          es_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_VIEW3D) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_PEN:
      if (winn->data & ND_PEN_EDITMODE) {
        ee_rgn_tag_redraw(rgn);
      }
      else if (winn->action == NA_EDITED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_BRUSH:
      ed_rgn_tag_redraw(rgn);
      break;
  }

    /* From topbar, which ones are needed? split per header? */
    /* Disable for now, re-enable if needed, or remove - campbell. */
#if 0
  /* cxt changes */
  switch (winn->category) {
    case NC_WM:
      if (winn->data == ND_HISTORY) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_SCENE:
      if (winn->data == ND_MODE) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_VIEW3D) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_PEN:
      if (winn->data == ND_DATA) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
  }
#endif
}

static void view3d_header_rgn_msg_sub(const WinRgnMsgSubParams *params)
{
  struct WinMsgBus *mbus = params->msg_bus;
  ARgn *rgn = params->rgn;

  WinMsgParamsApi msg_key_params = {{0}};

  /* Only sub to types. */
  StructAPI *type_array[] = {
      &ApiView3DShading,
  };

  WinMsgSubVal msg_sub_val_rgn_tag_redraw = {
      .owner = rgn,
      .user_data = rgn,
      .notify = ed_rgn_do_msg_notify_tag_redraw,
  };

  for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
    msg_key_params.ptr.type = type_array[i];
    win_msg_sub_api_params(mbus, &msg_key_params, &msg_sub_val_rgn_tag_redraw, __func__);
  }
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void view3d_btn_rgn_init(WinMngr *wm, ARgn *rgn)
{
  wmKeyMap *keymap;

  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

void ed_view3d_btns_rgn_layout_ex(const Cxt *C,
                                        ARgn *rgn,
                                        const char *category_override)
{
  const enum eCxtObjMode mode = cxt_data_mode_enum(C);

  const char *cxts_base[4] = {NULL};
  cxts_base[0] = cxt_data_mode_string(C);

  const char **cxts = &cxts_base[1];

  switch (mode) {
    case CXT_MODE_EDIT_MESH:
      ARRAY_SET_ITEMS(cxts, ".mesh_edit");
      break;
    case CXT_MODE_EDIT_CURVE:
      ARRAY_SET_ITEMS(cxts, ".curve_edit");
      break;
    case CXT_MODE_EDIT_CURVES:
      ARRAY_SET_ITEMS(cxts, ".curves_edit");
      break;
    case CXT_MODE_EDIT_SURFACE:
      ARRAY_SET_ITEMS(cxts, ".curve_edit");
      break;
    case CXT_MODE_EDIT_TXT:
      ARRAY_SET_ITEMS(cxts, ".text_edit");
      break;
    case CXT_MODE_EDIT_ARMATURE:
      ARRAY_SET_ITEMS(cxts, ".armature_edit");
      break;
    case CXT_MODE_EDIT_METABALL:
      ARRAY_SET_ITEMS(cxts, ".mball_edit");
      break;
    case CXT_MODE_EDIT_LATTICE:
      ARRAY_SET_ITEMS(cxts, ".lattice_edit");
      break;
    case CXT_MODE_POSE:
      ARRAY_SET_ITEMS(cxts, ".posemode");
      break;
    case CXT_MODE_SCULPT:
      ARRAY_SET_ITEMS(cxts, ".paint_common", ".sculpt_mode");
      break;
    case CXT_MODE_PAINT_WEIGHT:
      ARRAY_SET_ITEMS(cxts, ".paint_common", ".weightpaint");
      break;
    case CXT_MODE_PAINT_VERTEX:
      ARRAY_SET_ITEMS(cxts, ".paint_common", ".vertexpaint");
      break;
    case CXT_MODE_PAINT_TEXTURE:
      ARRAY_SET_ITEMS(cxts, ".paint_common", ".imagepaint");
      break;
    case CXT_MODE_PARTICLE:
      ARRAY_SET_ITEMS(cxts, ".paint_common", ".particlemode");
      break;
    case CXT_MODE_OBJ:
      ARRAY_SET_ITEMS(cxts, ".objmode");
      break;
    case CXT_MODE_PAINT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_paint");
      break;
    case CXT_MODE_SCULPT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_sculpt");
      break;
    case CXT_MODE_WEIGHT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_weight");
      break;
    case CXT_MODE_VERTEX_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_vertex");
      break;
    case CXT_MODE_SCULPT_CURVES:
      ARRAY_SET_ITEMS(cxts, ".curves_sculpt");
      break;
    default:
      break;
  }

  switch (mode) {
    case CXT_MODE_PAINT_PEN:
      ARRAY_SET_ITEMS(contexts, ".pen_paint");
      break;
    case CXT_MODE_SCULPT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_sculpt");
      break;
    case CXT_MODE_WEIGHT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_weight");
      break;
    case CXT_MODE_EDIT_PEN:
      ARRAY_SET_ITEMS(cxts, ".pen_edit");
      break;
    case CXT_MODE_VERTEX_PEN:
      ARRAY_SET_ITEMS(cxts, "pen_vertex");
      break;
    default:
      break;
  }

  List *pnltypes = &rgn->type->pnltypes;

  /* Allow drawing 3D view toolbar from non 3D view space type. */
  if (category_override != NULL) {
    SpaceType *st = dune_spacetype_from_id(SPACE_VIEW3D);
    ARgnType *art = dune_rgntype_from_id(st, RGN_TYPE_UI);
    pnltypes = &art->pnltypes;
  }

  ed_rgn_pnls_layout_ex(C, rgn, pnltypes, cxts_base, category_override);
}

static void view3d_btns_rgn_layout(const Cxt *C, ARgn *rgn)
{
  ed_view3d_btns_rgn_layout_ex(C, region, NULL);
}

static void view3d_btns_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_ANIM:
      switch (wmn->data) {
        case ND_KEYFRAME_PROP:
        case ND_NLA_ACTCHANGE:
          ed_rgn_tag_redraw(rgn);
          break;
        case ND_NLA:
        case ND_KEYFRAME:
          if (ELEM(winn->action, NA_EDITED, NA_ADDED, NA_REMOVED)) {
            ed_rgn_tag_redraw(rgn);
          }
          break;
      }
      break;
    case NC_SCENE:
      switch (win->data) {
        case ND_FRAME:
        case ND_OB_ACTIVE:
        case ND_OB_SEL:
        case ND_OB_VISIBLE:
        case ND_MODE:
        case ND_LAYER:
        case ND_LAYER_CONTENT:
        case ND_TOOLSETTINGS:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      switch (winn->action) {
        case NA_EDITED:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_OBJECT:
      switch (winn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SELECT:
        case ND_TRANSFORM:
        case ND_POSE:
        case ND_DRAW:
        case ND_KEYS:
        case ND_MOD:
        case ND_SHADERFX:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      break;
    case NC_GEOM:
      switch (winn->data) {
        case ND_DATA:
        case ND_VERTEX_GROUP:
        case ND_SEL:
          ed_rgn_tag_redraw(rgn);
          break;
      }
      if (winn->action == NA_EDITED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_TEXTURE:
    case NC_MATERIAL:
      /* for brush textures */
      es_rgn_tag_redraw(rgn);
      break;
    case NC_BRUSH:
      /* NA_SEL is used on brush changes */
      if (ELEM(winn->action, NA_EDITED, NA_SELECTED)) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_VIEW3D) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_ID:
      if (winn->action == NA_RENAME) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_PEN:
      if ((winn->data & (ND_DATA | ND_PEN_EDITMODE)) || (wmn->action == NA_EDITED)) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_IMAGE:
      /* Update for the image layers in texture paint. */
      if (winn->action == NA_EDITED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
    case NC_WIN:
      if (winn->data == ND_XR_DATA_CHANGED) {
        ed_rgn_tag_redraw(rgn);
      }
      break;
  }
}

/* add handlers, stuff you only do once or on area/region changes */
static void view3d_tools_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void view3d_tools_rgn_draw(const Cxt *C, ARgn *rgn)
{
  ed_rgn_pnls_ex(C, rgn, (const char *[]){cxt_data_mode_string(C), NULL});
}

/* area (not rgn) level listener */
static void space_view3d_listener(const WinSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  WinNotifier *winn = params->notifier;
  View3D *v3d = area->spacedata.first;

  /* context changes */
  switch (winn->category) {
    case NC_SCENE:
      switch (winn->data) {
        case ND_WORLD: {
          const bool use_scene_world = V3D_USES_SCENE_WORLD(v3d);
          if (v3d->flag2 & V3D_HIDE_OVERLAYS || use_scene_world) {
            ed_area_tag_redraw_rgntype(area, RGN_TYPE_WIN);
          }
          break;
        }
      }
      break;
    case NC_WORLD:
      switch (win->data) {
        case ND_WORLD_DRAW:
        case ND_WORLD:
          if (v3d->shading.background_type == V3D_SHADING_BACKGROUND_WORLD) {
            ed_area_tag_redraw_rgntype(area, RGN_TYPE_WIN);
          }
          break;
      }
      break;
    case NC_MATERIAL:
      switch (winn->data) {
        case ND_NODES:
          if (v3d->shading.type == OB_TEXTURE) {
            ed_area_tag_redraw_rgntype(area, RGN_TYPE_WIN);
          }
          break;
      }
      break;
  }
}

static void space_view3d_refresh(const Cxt *C, ScrArea *area)
{
  Scene *scene = cxt_data_scene(
  View3D *v3d = (View3D *)area->spacedata.first;
  MEM_SAFE_FREE(v3d->runtime.local_stats);
}

const char *view3d_cxt_dir[] = {
    "active_obj",
    "sel_ids",
    NULL,
};

static int view3d_cxt(const Cxt *C, const char *member, CxtDataResult *result)
{
  /* fallback to the scene layer,
   * allows duplicate and other obj ops to run outside the 3d view */
  if (cxt_data_dir(member)) {
    cxt_data_dir_set(result, view3d_cxt_dir);
    return CTX_RESULT_OK;
  }
  if (cxt_data_equals(member, "active_object")) {
    /* In most cases the active object is the `view_layer->basact->object`.
     * For the 3D view however it can be NULL when hidden.
     *
     * This is ignored in the case the object is in any mode (besides object-mode),
     * since the object's mode impacts the current tool, cursor, gizmos etc.
     * If we didn't have this exception, changing visibility would need to perform
     * many of the same updates as changing the objs mode.
     *
     * Further, there are multiple ways to hide objs: collection, objtype, etc.
     * it's simplest if all these methods behave consistently - respecting the objmode
     * wo showing the object.
     *
     * See T85532 for alternatives that were considered. */
    ViewLayer *view_layer = CTX_data_view_layer(C);
    if (view_layer->basact) {
      Obj *ob = view_layer->basact->object;
      /* if hidden but in edit mode, we still display, can happen with animation */
      if ((view_layer->basact->flag & BASE_VISIBLE_GRAPH) != 0 ||
          (ob->mode != OB_MODE_OBJ)) {
        cxt_data_id_ptr_set(result, &ob->id);
      }
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "sel_ids")) {
    List sel_objs;
    cxt_data_sel_objs(C, &sel_objs);
    LIST_FOREACH (CollectionPtrLink *, obj_ptr_link, &sel_objs) {
      Id *sel_id = obj_ptr_link->ptr.owner_id;
      cxt_data_id_list_add(result, sel_id);
    }
    lib_freelist(&sel_objs);
    cxt_data_type_set(result, CXT_DATA_TYPE_COLLECTION);
    return CXT_RESULT_OK;
  }

  return CXT_RESULT_MEMBER_NOT_FOUND;
}

static void view3d_id_remap_v3d_ob_centers(View3D *v3d, const struct IdRemapper *mappings)
{
  if (dune_id_remapper_apply(mappings, (Id **)&v3d->ob_center, ID_REMAP_APPLY_DEFAULT) ==
      ID_REMAP_RESULT_SOURCE_UNASSIGNED) {
    /* Otherwise, bonename may remain valid...
     * We could be smart and check this, too? */
    v3d->ob_center_bone[0] = '\0';
  }
}

static void view3d_id_remap_v3d(ScrArea *area,
                                SpaceLink *slink,
                                View3D *v3d,
                                const struct IdRemapper *mappings,
                                const bool is_local)
{
  ARgn *rgn;
  if (dune_id_remapper_apply(mappings, (Id **)&v3d->camera, ID_REMAP_APPLY_DEFAULT) ==
      ID_REMAP_RESULT_SOURCE_UNASSIGNED) {
    /* 3D view might be inactive, in that case needs to use slink->rgnbase */
    List *rgnbase = (slink == area->spacedata.first) ? &area->rgnbase :
                                                       &slink->rgnbase;
    for (rgn = rgnbase->first; rgn; rgn = rgn->next) {
      if (rgn->rgntype == RGN_TYPE_WIN) {
        RgnView3D *rv3d = is_local ? ((RgnView3D *)rgn->rgndata)->localvd :
                                        rgn->rgndata;
        if (rv3d && (rv3d->persp == RV3D_CAMOB)) {
          rv3d->persp = RV3D_PERSP;
        }
      }
    }
  }
}

static void view3d_id_remap(ScrArea *area, SpaceLink *slink, const struct IdRemapper *mappings)
{

  if (!dune_id_remapper_has_mapping_for(
          mappings, FILTER_ID_OB | FILTER_ID_MA | FILTER_ID_IM | FILTER_ID_MC)) {
    return;
  }

  View3D *view3d = (View3D *)slink;
  view3d_id_remap_v3d(area, slink, view3d, mappings, false);
  view3d_id_remap_v3d_ob_centers(view3d, mappings);
  if (view3d->localvd != NULL) {
    /* Obj centers in localview aren't used, see: T52663 */
    view3d_id_remap_v3d(area, slink, view3d->localvd, mappings, true);
  }
}

void ed_spacetype_view3d(void)
{
  SpaceType *st = mem_calloc(sizeof(SpaceType), "spacetype view3d");
  ARgnType *art;

  st->spaceid = SPACE_VIEW3D;
  strncpy(st->name, "View3D", DUNE_ST_MAXNAME);

  st->create = view3d_create;
  st->free = view3d_free;
  st->init = view3d_init;
  st->exit = view3d_exit;
  st->listener = space_view3d_listener;
  st->refresh = space_view3d_refresh;
  st->duplicate = view3d_duplicate;
  st->otypes = view3d_optypes;
  st->keymap = view3d_keymap;
  st->dropboxes = view3d_dropboxes;
  st->gizmos = view3d_widgets;
  st->cxt = view3d_cxt;
  st->id_remap = view3d_id_remap;

  /* rgns: main win */
  art = mem_calloc(sizeof(ARgnType), "spacetype view3d main rgn");
  art->rgnid = RGN_TYPE_WIN;
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_TOOL | ED_KEYMAP_PEN;
  art->draw = view3d_main_rgn_draw;
  art->init = view3d_main_rgn_init;
  art->exit = view3d_main_rgn_exit;
  art->free = view3d_main_rgn_free;
  art->duplicate = view3d_main_region_duplicate;
  art->listener = view3d_main_region_listener;
  art->message_subscribe = view3d_main_region_message_subscribe;
  art->cursor = view3d_main_region_cursor;
  art->lock = 1; /* can become flag, see BKE_spacedata_draw_locks */
  LIB_addhead(&st->regiontypes, art);

  /* regions: listview/buttons */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d buttons region");
  art->regionid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PANEL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = view3d_buttons_region_listener;
  art->message_subscribe = ED_area_do_mgs_subscribe_for_tool_ui;
  art->init = view3d_buttons_region_init;
  art->layout = view3d_buttons_region_layout;
  art->draw = ED_region_panels_draw;
  LIB_addhead(&st->regiontypes, art);

  view3d_buttons_register(art);

  /* regions: tool(bar) */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d tools region");
  art->regionid = RGN_TYPE_TOOLS;
  art->prefsizex = 58; /* XXX */
  art->prefsizey = 50; /* XXX */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = view3d_buttons_region_listener;
  art->message_subscribe = ED_region_generic_tools_region_message_subscribe;
  art->snap_size = ED_region_generic_tools_region_snap_size;
  art->init = view3d_tools_region_init;
  art->draw = view3d_tools_region_draw;
  LIB_addhead(&st->regiontypes, art);

  /* regions: tool header */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d tool header region");
  art->regionid = RGN_TYPE_TOOL_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->listener = view3d_header_region_listener;
  art->message_subscribe = ED_area_do_mgs_subscribe_for_tool_header;
  art->init = view3d_header_region_init;
  art->draw = view3d_header_region_draw;
  LIB_addhead(&st->regiontypes, art);

  /* regions: header */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->listener = view3d_header_region_listener;
  art->message_subscribe = view3d_header_region_message_subscribe;
  art->init = view3d_header_region_init;
  art->draw = view3d_header_region_draw;
  LIB_addhead(&st->regiontypes, art);

  /* regions: hud */
  art = ED_area_type_hud(st->spaceid);
  LIB_addhead(&st->regiontypes, art);

  /* regions: xr */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d xr region");
  art->regionid = RGN_TYPE_XR;
  LIB_addhead(&st->regiontypes, art);

  DUNE_spacetype_register(st);
}
