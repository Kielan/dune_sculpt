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
  PointerRNA op_ptr;

  Scene *scene = CTX_data_scene(C);

  if (!BKE_scene_uses_blender_eevee(scene)) {
    /* Only do auto bake if eevee is the active engine */
    return;
  }

  wmOperatorType *ot = WM_operatortype_find("SCENE_OT_light_cache_bake", true);
  WM_operator_properties_create_ptr(&op_ptr, ot);
  RNA_int_set(&op_ptr, "delay", 200);
  RNA_enum_set_identifier(C, &op_ptr, "subset", "DIRTY");

  WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &op_ptr, NULL);

  WM_operator_properties_free(&op_ptr);
}

/* region dropbox definition */
static void view3d_dropboxes(void)
{
  ListBase *lb = WM_dropboxmap_find("View3D", SPACE_VIEW3D, RGN_TYPE_WINDOW);

  struct wmDropBox *drop;
  drop = WM_dropbox_add(lb,
                        "OBJECT_OT_add_named",
                        view3d_ob_drop_poll_local_id,
                        view3d_ob_drop_copy_local_id,
                        WM_drag_free_imported_drag_ID,
                        NULL);

  drop->draw = WM_drag_draw_item_name_fn;
  drop->draw_activate = view3d_ob_drop_draw_activate;
  drop->draw_deactivate = view3d_ob_drop_draw_deactivate;

  drop = WM_dropbox_add(lb,
                        "OBJECT_OT_transform_to_mouse",
                        view3d_ob_drop_poll_external_asset,
                        view3d_ob_drop_copy_external_asset,
                        WM_drag_free_imported_drag_ID,
                        NULL);

  drop->draw = WM_drag_draw_item_name_fn;
  drop->draw_activate = view3d_ob_drop_draw_activate;
  drop->draw_deactivate = view3d_ob_drop_draw_deactivate;

  WM_dropbox_add(lb,
                 "OBJECT_OT_drop_named_material",
                 view3d_mat_drop_poll,
                 view3d_id_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 view3d_mat_drop_tooltip);
  WM_dropbox_add(lb,
                 "VIEW3D_OT_background_image_add",
                 view3d_ima_bg_drop_poll,
                 view3d_id_path_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 NULL);
  WM_dropbox_add(lb,
                 "OBJECT_OT_drop_named_image",
                 view3d_ima_empty_drop_poll,
                 view3d_id_path_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 NULL);
  WM_dropbox_add(lb,
                 "OBJECT_OT_volume_import",
                 view3d_volume_drop_poll,
                 view3d_id_path_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 NULL);
  WM_dropbox_add(lb,
                 "OBJECT_OT_collection_instance_add",
                 view3d_collection_drop_poll,
                 view3d_collection_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 NULL);
  WM_dropbox_add(lb,
                 "OBJECT_OT_data_instance_add",
                 view3d_object_data_drop_poll,
                 view3d_id_drop_copy_with_type,
                 WM_drag_free_imported_drag_ID,
                 view3d_object_data_drop_tooltip);
  WM_dropbox_add(lb,
                 "VIEW3D_OT_drop_world",
                 view3d_world_drop_poll,
                 view3d_id_drop_copy,
                 WM_drag_free_imported_drag_ID,
                 NULL);
}

static void view3d_widgets(void)
{
  wmGizmoMapType *gzmap_type = WM_gizmomaptype_ensure(
      &(const struct wmGizmoMapType_Params){SPACE_VIEW3D, RGN_TYPE_WINDOW});

  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_xform_gizmo_context);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_spot);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_area);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_light_target);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_force_field);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_camera);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_camera_view);
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_empty_image);
  /* TODO(campbell): Not working well enough, disable for now. */
#if 0
  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_armature_spline);
#endif

  WM_gizmogrouptype_append(VIEW3D_GGT_xform_gizmo);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_cage);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_shear);
  WM_gizmogrouptype_append(VIEW3D_GGT_xform_extrude);
  WM_gizmogrouptype_append(VIEW3D_GGT_mesh_preselect_elem);
  WM_gizmogrouptype_append(VIEW3D_GGT_mesh_preselect_edgering);
  WM_gizmogrouptype_append(VIEW3D_GGT_tool_generic_handle_normal);
  WM_gizmogrouptype_append(VIEW3D_GGT_tool_generic_handle_free);

  WM_gizmogrouptype_append(VIEW3D_GGT_ruler);
  WM_gizmotype_append(VIEW3D_GT_ruler_item);

  WM_gizmogrouptype_append(VIEW3D_GGT_placement);

  WM_gizmogrouptype_append_and_link(gzmap_type, VIEW3D_GGT_navigate);
  WM_gizmotype_append(VIEW3D_GT_navigate_rotate);
}

/* type callback, not region itself */
static void view3d_main_region_free(ARegion *region)
{
  RegionView3D *rv3d = region->regiondata;

  if (rv3d) {
    if (rv3d->localvd) {
      MEM_freeN(rv3d->localvd);
    }
    if (rv3d->clipbb) {
      MEM_freeN(rv3d->clipbb);
    }

    if (rv3d->render_engine) {
      RE_engine_free(rv3d->render_engine);
    }

    if (rv3d->sms) {
      MEM_freeN(rv3d->sms);
    }

    MEM_freeN(rv3d);
    region->regiondata = NULL;
  }
}

/* copy regiondata */
static void *view3d_main_region_duplicate(void *poin)
{
  if (poin) {
    RegionView3D *rv3d = poin, *new;

    new = MEM_dupallocN(rv3d);
    if (rv3d->localvd) {
      new->localvd = MEM_dupallocN(rv3d->localvd);
    }
    if (rv3d->clipbb) {
      new->clipbb = MEM_dupallocN(rv3d->clipbb);
    }

    new->render_engine = NULL;
    new->sms = NULL;
    new->smooth_timer = NULL;

    return new;
  }
  return NULL;
}

static void view3d_main_region_listener(const wmRegionListenerParams *params)
{
  wmWindow *window = params->window;
  ScrArea *area = params->area;
  ARegion *region = params->region;
  wmNotifier *wmn = params->notifier;
  const Scene *scene = params->scene;
  View3D *v3d = area->spacedata.first;
  RegionView3D *rv3d = region->regiondata;
  wmGizmoMap *gzmap = region->gizmo_map;

  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (ELEM(wmn->data, ND_UNDO)) {
        WM_gizmomap_tag_refresh(gzmap);
      }
      else if (ELEM(wmn->data, ND_XR_DATA_CHANGED)) {
        /* Only cause a redraw if this a VR session mirror. Should more features be added that
         * require redraws, we could pass something to wmn->reference, e.g. the flag value. */
        if (v3d->flag & V3D_XR_SESSION_MIRROR) {
          ED_region_tag_redraw(region);
        }
      }
      break;
    case NC_ANIMATION:
      switch (wmn->data) {
        case ND_KEYFRAME_PROP:
        case ND_NLA_ACTCHANGE:
          ED_region_tag_redraw(region);
          break;
        case ND_NLA:
        case ND_KEYFRAME:
          if (ELEM(wmn->action, NA_EDITED, NA_ADDED, NA_REMOVED)) {
            ED_region_tag_redraw(region);
          }
          break;
        case ND_ANIMCHAN:
          if (ELEM(wmn->action, NA_EDITED, NA_ADDED, NA_REMOVED, NA_SELECTED)) {
            ED_region_tag_redraw(region);
          }
          break;
      }
      break;
    case NC_SCENE:
      switch (wmn->data) {
        case ND_SCENEBROWSE:
        case ND_LAYER_CONTENT:
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
          break;
        case ND_LAYER:
          if (wmn->reference) {
            BKE_screen_view3d_sync(v3d, wmn->reference);
          }
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
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
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
          break;
        case ND_WORLD:
          /* handled by space_view3d_listener() for v3d access */
          break;
        case ND_DRAW_RENDER_VIEWPORT: {
          if (v3d->camera && (scene == wmn->reference)) {
            if (rv3d->persp == RV3D_CAMOB) {
              ED_region_tag_redraw(region);
            }
          }
          break;
        }
      }
      if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_OBJECT:
      switch (wmn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SELECT:
        case ND_TRANSFORM:
        case ND_POSE:
        case ND_DRAW:
        case ND_MODIFIER:
        case ND_SHADERFX:
        case ND_CONSTRAINT:
        case ND_KEYS:
        case ND_PARTICLE:
        case ND_POINTCACHE:
        case ND_LOD:
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
          break;
        case ND_DRAW_ANIMVIZ:
          ED_region_tag_redraw(region);
          break;
      }
      switch (wmn->action) {
        case NA_ADDED:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_GEOM:
      switch (wmn->data) {
        case ND_SELECT: {
          WM_gizmomap_tag_refresh(gzmap);
          ATTR_FALLTHROUGH;
        }
        case ND_DATA:
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
          break;
        case ND_VERTEX_GROUP:
          ED_region_tag_redraw(region);
          break;
      }
      switch (wmn->action) {
        case NA_EDITED:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_CAMERA:
      switch (wmn->data) {
        case ND_DRAW_RENDER_VIEWPORT: {
          if (v3d->camera && (v3d->camera->data == wmn->reference)) {
            if (rv3d->persp == RV3D_CAMOB) {
              ED_region_tag_redraw(region);
            }
          }
          break;
        }
      }
      break;
    case NC_GROUP:
      /* all group ops for now */
      ED_region_tag_redraw(region);
      break;
    case NC_BRUSH:
      switch (wmn->action) {
        case NA_EDITED:
          ED_region_tag_redraw_cursor(region);
          break;
        case NA_SELECTED:
          /* used on brush changes - needed because 3d cursor
           * has to be drawn if clone brush is selected */
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_MATERIAL:
      switch (wmn->data) {
        case ND_SHADING:
        case ND_NODES:
          /* TODO(sergey): This is a bit too much updates, but needed to
           * have proper material drivers update in the viewport.
           *
           * How to solve?
           */
          ED_region_tag_redraw(region);
          break;
        case ND_SHADING_DRAW:
        case ND_SHADING_LINKS:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_WORLD:
      switch (wmn->data) {
        case ND_WORLD_DRAW:
          /* handled by space_view3d_listener() for v3d access */
          break;
        case ND_WORLD:
          /* Needed for updating world materials */
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_LAMP:
      switch (wmn->data) {
        case ND_LIGHTING:
          /* TODO(sergey): This is a bit too much, but needed to
           * handle updates from new depsgraph.
           */
          ED_region_tag_redraw(region);
          break;
        case ND_LIGHTING_DRAW:
          ED_region_tag_redraw(region);
          WM_gizmomap_tag_refresh(gzmap);
          break;
      }
      break;
    case NC_LIGHTPROBE:
      ED_area_tag_refresh(area);
      break;
    case NC_IMAGE:
      /* this could be more fine grained checks if we had
       * more context than just the region */
      ED_region_tag_redraw(region);
      break;
    case NC_TEXTURE:
      /* same as above */
      ED_region_tag_redraw(region);
      break;
    case NC_MOVIECLIP:
      if (wmn->data == ND_DISPLAY || wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        if (wmn->subtype == NS_VIEW3D_GPU) {
          rv3d->rflag |= RV3D_GPULIGHT_UPDATE;
        }
        else if (wmn->subtype == NS_VIEW3D_SHADING) {
#ifdef WITH_XR_OPENXR
          ED_view3d_xr_shading_update(G_MAIN->wm.first, v3d, scene);
#endif

          ViewLayer *view_layer = WM_window_get_active_view_layer(window);
          Depsgraph *depsgraph = BKE_scene_get_depsgraph(scene, view_layer);
          if (depsgraph) {
            ED_render_view3d_update(depsgraph, window, area, true);
          }
        }
        ED_region_tag_redraw(region);
        WM_gizmomap_tag_refresh(gzmap);
      }
      break;
    case NC_ID:
      if (ELEM(wmn->action, NA_RENAME, NA_EDITED, NA_ADDED, NA_REMOVED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCREEN:
      switch (wmn->data) {
        case ND_ANIMPLAY:
        case ND_SKETCH:
          ED_region_tag_redraw(region);
          break;
        case ND_LAYOUTBROWSE:
        case ND_LAYOUTDELETE:
        case ND_LAYOUTSET:
          WM_gizmomap_tag_refresh(gzmap);
          ED_region_tag_redraw(region);
          break;
        case ND_LAYER:
          ED_region_tag_redraw(region);
          break;
      }

      break;
    case NC_GPENCIL:
      if (wmn->data == ND_DATA || ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void view3d_main_region_message_subscribe(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  const bContext *C = params->context;
  ScrArea *area = params->area;
  ARegion *region = params->region;

  /* Developer NOTE: there are many properties that impact 3D view drawing,
   * so instead of subscribing to individual properties, just subscribe to types
   * accepting some redundant redraws.
   *
   * For other space types we might try avoid this, keep the 3D view as an exceptional case! */
  wmMsgParams_RNA msg_key_params = {{0}};

  /* Only subscribe to types. */
  StructRNA *type_array[] = {
      &RNA_Window,

      /* These object have properties that impact drawing. */
      &RNA_AreaLight,
      &RNA_Camera,
      &RNA_Light,
      &RNA_Speaker,
      &RNA_SunLight,

      /* General types the 3D view depends on. */
      &RNA_Object,
      &RNA_UnitSettings, /* grid-floor */

      &RNA_View3DCursor,
      &RNA_View3DOverlay,
      &RNA_View3DShading,
      &RNA_World,
  };

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ED_region_do_msg_notify_tag_redraw,
  };

  for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
    msg_key_params.ptr.type = type_array[i];
    WM_msg_subscribe_rna_params(mbus, &msg_key_params, &msg_sub_value_region_tag_redraw, __func__);
  }

  /* Subscribe to a handful of other properties. */
  RegionView3D *rv3d = region->regiondata;

  WM_msg_subscribe_rna_anon_prop(mbus, RenderSettings, engine, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_prop(
      mbus, RenderSettings, resolution_x, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_prop(
      mbus, RenderSettings, resolution_y, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_prop(
      mbus, RenderSettings, pixel_aspect_x, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_prop(
      mbus, RenderSettings, pixel_aspect_y, &msg_sub_value_region_tag_redraw);
  if (rv3d->persp == RV3D_CAMOB) {
    WM_msg_subscribe_rna_anon_prop(
        mbus, RenderSettings, use_border, &msg_sub_value_region_tag_redraw);
  }

  WM_msg_subscribe_rna_anon_type(mbus, SceneEEVEE, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_type(mbus, SceneDisplay, &msg_sub_value_region_tag_redraw);
  WM_msg_subscribe_rna_anon_type(mbus, ObjectDisplay, &msg_sub_value_region_tag_redraw);

  ViewLayer *view_layer = CTX_data_view_layer(C);
  Object *obact = OBACT(view_layer);
  if (obact != NULL) {
    switch (obact->mode) {
      case OB_MODE_PARTICLE_EDIT:
        WM_msg_subscribe_rna_anon_type(mbus, ParticleEdit, &msg_sub_value_region_tag_redraw);
        break;
      default:
        break;
    }
  }

  {
    wmMsgSubscribeValue msg_sub_value_region_tag_refresh = {
        .owner = region,
        .user_data = area,
        .notify = WM_toolsystem_do_msg_notify_tag_refresh,
    };
    WM_msg_subscribe_rna_anon_prop(mbus, Object, mode, &msg_sub_value_region_tag_refresh);
    WM_msg_subscribe_rna_anon_prop(mbus, LayerObjects, active, &msg_sub_value_region_tag_refresh);
  }
}

/* concept is to retrieve cursor type context-less */
static void view3d_main_region_cursor(wmWindow *win, ScrArea *area, ARegion *region)
{
  if (WM_cursor_set_from_tool(win, area, region)) {
    return;
  }

  ViewLayer *view_layer = WM_window_get_active_view_layer(win);
  Object *obedit = OBEDIT_FROM_VIEW_LAYER(view_layer);
  if (obedit) {
    WM_cursor_set(win, WM_CURSOR_EDIT);
  }
  else {
    WM_cursor_set(win, WM_CURSOR_DEFAULT);
  }
}

/* add handlers, stuff you only do once or on area/region changes */
static void view3d_header_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap = WM_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);

  WM_event_add_keymap_handler(&region->handlers, keymap);

  ED_region_header_init(region);
}

static void view3d_header_region_draw(const duneContext *C, ARegion *region)
{
  ED_region_header(C, region);
}

static void view3d_header_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_SCENE:
      switch (wmn->data) {
        case ND_FRAME:
        case ND_OB_ACTIVE:
        case ND_OB_SELECT:
        case ND_OB_VISIBLE:
        case ND_MODE:
        case ND_LAYER:
        case ND_TOOLSETTINGS:
        case ND_LAYER_CONTENT:
        case ND_RENDER_OPTIONS:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_GPENCIL:
      if (wmn->data & ND_GPENCIL_EDITMODE) {
        ED_region_tag_redraw(region);
      }
      else if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_BRUSH:
      ED_region_tag_redraw(region);
      break;
  }

    /* From topbar, which ones are needed? split per header? */
    /* Disable for now, re-enable if needed, or remove - campbell. */
#if 0
  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (wmn->data == ND_HISTORY) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_MODE) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_GPENCIL:
      if (wmn->data == ND_DATA) {
        ED_region_tag_redraw(region);
      }
      break;
  }
#endif
}

static void view3d_header_region_message_subscribe(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  ARegion *region = params->region;

  wmMsgParams_API msg_key_params = {{0}};

  /* Only subscribe to types. */
  StructAPI *type_array[] = {
      &API_View3DShading,
  };

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ED_region_do_msg_notify_tag_redraw,
  };

  for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
    msg_key_params.ptr.type = type_array[i];
    WM_msg_subscribe_rna_params(mbus, &msg_key_params, &msg_sub_value_region_tag_redraw, __func__);
  }
}

/* add handlers, stuff you only do once or on area/region changes */
static void view3d_buttons_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  ED_region_panels_init(wm, region);

  keymap = WM_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);
  WM_event_add_keymap_handler(&region->handlers, keymap);
}

void ED_view3d_buttons_region_layout_ex(const duneContext *C,
                                        ARegion *region,
                                        const char *category_override)
{
  const enum eContextObjectMode mode = CTX_data_mode_enum(C);

  const char *contexts_base[4] = {NULL};
  contexts_base[0] = CTX_data_mode_string(C);

  const char **contexts = &contexts_base[1];

  switch (mode) {
    case CTX_MODE_EDIT_MESH:
      ARRAY_SET_ITEMS(contexts, ".mesh_edit");
      break;
    case CTX_MODE_EDIT_CURVE:
      ARRAY_SET_ITEMS(contexts, ".curve_edit");
      break;
    case CTX_MODE_EDIT_CURVES:
      ARRAY_SET_ITEMS(contexts, ".curves_edit");
      break;
    case CTX_MODE_EDIT_SURFACE:
      ARRAY_SET_ITEMS(contexts, ".curve_edit");
      break;
    case CTX_MODE_EDIT_TEXT:
      ARRAY_SET_ITEMS(contexts, ".text_edit");
      break;
    case CTX_MODE_EDIT_ARMATURE:
      ARRAY_SET_ITEMS(contexts, ".armature_edit");
      break;
    case CTX_MODE_EDIT_METABALL:
      ARRAY_SET_ITEMS(contexts, ".mball_edit");
      break;
    case CTX_MODE_EDIT_LATTICE:
      ARRAY_SET_ITEMS(contexts, ".lattice_edit");
      break;
    case CTX_MODE_POSE:
      ARRAY_SET_ITEMS(contexts, ".posemode");
      break;
    case CTX_MODE_SCULPT:
      ARRAY_SET_ITEMS(contexts, ".paint_common", ".sculpt_mode");
      break;
    case CTX_MODE_PAINT_WEIGHT:
      ARRAY_SET_ITEMS(contexts, ".paint_common", ".weightpaint");
      break;
    case CTX_MODE_PAINT_VERTEX:
      ARRAY_SET_ITEMS(contexts, ".paint_common", ".vertexpaint");
      break;
    case CTX_MODE_PAINT_TEXTURE:
      ARRAY_SET_ITEMS(contexts, ".paint_common", ".imagepaint");
      break;
    case CTX_MODE_PARTICLE:
      ARRAY_SET_ITEMS(contexts, ".paint_common", ".particlemode");
      break;
    case CTX_MODE_OBJECT:
      ARRAY_SET_ITEMS(contexts, ".objectmode");
      break;
    case CTX_MODE_PAINT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_paint");
      break;
    case CTX_MODE_SCULPT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_sculpt");
      break;
    case CTX_MODE_WEIGHT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_weight");
      break;
    case CTX_MODE_VERTEX_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_vertex");
      break;
    case CTX_MODE_SCULPT_CURVES:
      ARRAY_SET_ITEMS(contexts, ".curves_sculpt");
      break;
    default:
      break;
  }

  switch (mode) {
    case CTX_MODE_PAINT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_paint");
      break;
    case CTX_MODE_SCULPT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_sculpt");
      break;
    case CTX_MODE_WEIGHT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_weight");
      break;
    case CTX_MODE_EDIT_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_edit");
      break;
    case CTX_MODE_VERTEX_GPENCIL:
      ARRAY_SET_ITEMS(contexts, ".greasepencil_vertex");
      break;
    default:
      break;
  }

  ListBase *paneltypes = &region->type->paneltypes;

  /* Allow drawing 3D view toolbar from non 3D view space type. */
  if (category_override != NULL) {
    SpaceType *st = DUNE_spacetype_from_id(SPACE_VIEW3D);
    ARegionType *art = DUNE_regiontype_from_id(st, RGN_TYPE_UI);
    paneltypes = &art->paneltypes;
  }

  ED_region_panels_layout_ex(C, region, paneltypes, contexts_base, category_override);
}

static void view3d_buttons_region_layout(const duneContext *C, ARegion *region)
{
  ED_view3d_buttons_region_layout_ex(C, region, NULL);
}

static void view3d_buttons_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_ANIMATION:
      switch (wmn->data) {
        case ND_KEYFRAME_PROP:
        case ND_NLA_ACTCHANGE:
          ED_region_tag_redraw(region);
          break;
        case ND_NLA:
        case ND_KEYFRAME:
          if (ELEM(wmn->action, NA_EDITED, NA_ADDED, NA_REMOVED)) {
            ED_region_tag_redraw(region);
          }
          break;
      }
      break;
    case NC_SCENE:
      switch (wmn->data) {
        case ND_FRAME:
        case ND_OB_ACTIVE:
        case ND_OB_SELECT:
        case ND_OB_VISIBLE:
        case ND_MODE:
        case ND_LAYER:
        case ND_LAYER_CONTENT:
        case ND_TOOLSETTINGS:
          ED_region_tag_redraw(region);
          break;
      }
      switch (wmn->action) {
        case NA_EDITED:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_OBJECT:
      switch (wmn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SELECT:
        case ND_TRANSFORM:
        case ND_POSE:
        case ND_DRAW:
        case ND_KEYS:
        case ND_MODIFIER:
        case ND_SHADERFX:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_GEOM:
      switch (wmn->data) {
        case ND_DATA:
        case ND_VERTEX_GROUP:
        case ND_SELECT:
          ED_region_tag_redraw(region);
          break;
      }
      if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_TEXTURE:
    case NC_MATERIAL:
      /* for brush textures */
      ED_region_tag_redraw(region);
      break;
    case NC_BRUSH:
      /* NA_SELECTED is used on brush changes */
      if (ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_ID:
      if (wmn->action == NA_RENAME) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_GPENCIL:
      if ((wmn->data & (ND_DATA | ND_GPENCIL_EDITMODE)) || (wmn->action == NA_EDITED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_IMAGE:
      /* Update for the image layers in texture paint. */
      if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_WM:
      if (wmn->data == ND_XR_DATA_CHANGED) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

/* add handlers, stuff you only do once or on area/region changes */
static void view3d_tools_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  ED_region_panels_init(wm, region);

  keymap = WM_keymap_ensure(wm->defaultconf, "3D View Generic", SPACE_VIEW3D, 0);
  WM_event_add_keymap_handler(&region->handlers, keymap);
}

static void view3d_tools_region_draw(const duneContext *C, ARegion *region)
{
  ED_region_panels_ex(C, region, (const char *[]){CTX_data_mode_string(C), NULL});
}

/* area (not region) level listener */
static void space_view3d_listener(const wmSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  wmNotifier *wmn = params->notifier;
  View3D *v3d = area->spacedata.first;

  /* context changes */
  switch (wmn->category) {
    case NC_SCENE:
      switch (wmn->data) {
        case ND_WORLD: {
          const bool use_scene_world = V3D_USES_SCENE_WORLD(v3d);
          if (v3d->flag2 & V3D_HIDE_OVERLAYS || use_scene_world) {
            ED_area_tag_redraw_regiontype(area, RGN_TYPE_WINDOW);
          }
          break;
        }
      }
      break;
    case NC_WORLD:
      switch (wmn->data) {
        case ND_WORLD_DRAW:
        case ND_WORLD:
          if (v3d->shading.background_type == V3D_SHADING_BACKGROUND_WORLD) {
            ED_area_tag_redraw_regiontype(area, RGN_TYPE_WINDOW);
          }
          break;
      }
      break;
    case NC_MATERIAL:
      switch (wmn->data) {
        case ND_NODES:
          if (v3d->shading.type == OB_TEXTURE) {
            ED_area_tag_redraw_regiontype(area, RGN_TYPE_WINDOW);
          }
          break;
      }
      break;
  }
}

static void space_view3d_refresh(const duneContext *C, ScrArea *area)
{
  Scene *scene = CTX_data_scene(C);
  LightCache *lcache = scene->eevee.light_cache_data;

  if (lcache && (lcache->flag & LIGHTCACHE_UPDATE_AUTO) != 0) {
    lcache->flag &= ~LIGHTCACHE_UPDATE_AUTO;
    view3d_lightcache_update((bContext *)C);
  }

  View3D *v3d = (View3D *)area->spacedata.first;
  MEM_SAFE_FREE(v3d->runtime.local_stats);
}

const char *view3d_context_dir[] = {
    "active_object",
    "selected_ids",
    NULL,
};

static int view3d_context(const bContext *C, const char *member, bContextDataResult *result)
{
  /* fallback to the scene layer,
   * allows duplicate and other object operators to run outside the 3d view */

  if (CTX_data_dir(member)) {
    CTX_data_dir_set(result, view3d_context_dir);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "active_object")) {
    /* In most cases the active object is the `view_layer->basact->object`.
     * For the 3D view however it can be NULL when hidden.
     *
     * This is ignored in the case the object is in any mode (besides object-mode),
     * since the object's mode impacts the current tool, cursor, gizmos etc.
     * If we didn't have this exception, changing visibility would need to perform
     * many of the same updates as changing the objects mode.
     *
     * Further, there are multiple ways to hide objects - by collection, by object type, etc.
     * it's simplest if all these methods behave consistently - respecting the object-mode
     * without showing the object.
     *
     * See T85532 for alternatives that were considered. */
    ViewLayer *view_layer = CTX_data_view_layer(C);
    if (view_layer->basact) {
      Object *ob = view_layer->basact->object;
      /* if hidden but in edit mode, we still display, can happen with animation */
      if ((view_layer->basact->flag & BASE_VISIBLE_DEPSGRAPH) != 0 ||
          (ob->mode != OB_MODE_OBJECT)) {
        CTX_data_id_pointer_set(result, &ob->id);
      }
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "selected_ids")) {
    ListBase selected_objects;
    CTX_data_selected_objects(C, &selected_objects);
    LISTBASE_FOREACH (CollectionPointerLink *, object_ptr_link, &selected_objects) {
      ID *selected_id = object_ptr_link->ptr.owner_id;
      CTX_data_id_list_add(result, selected_id);
    }
    LIB_freelistN(&selected_objects);
    CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
    return CTX_RESULT_OK;
  }

  return CTX_RESULT_MEMBER_NOT_FOUND;
}

static void view3d_id_remap_v3d_ob_centers(View3D *v3d, const struct IDRemapper *mappings)
{
  if (DUNE_id_remapper_apply(mappings, (ID **)&v3d->ob_center, ID_REMAP_APPLY_DEFAULT) ==
      ID_REMAP_RESULT_SOURCE_UNASSIGNED) {
    /* Otherwise, bonename may remain valid...
     * We could be smart and check this, too? */
    v3d->ob_center_bone[0] = '\0';
  }
}

static void view3d_id_remap_v3d(ScrArea *area,
                                SpaceLink *slink,
                                View3D *v3d,
                                const struct IDRemapper *mappings,
                                const bool is_local)
{
  ARegion *region;
  if (DUNE_id_remapper_apply(mappings, (ID **)&v3d->camera, ID_REMAP_APPLY_DEFAULT) ==
      ID_REMAP_RESULT_SOURCE_UNASSIGNED) {
    /* 3D view might be inactive, in that case needs to use slink->regionbase */
    ListBase *regionbase = (slink == area->spacedata.first) ? &area->regionbase :
                                                              &slink->regionbase;
    for (region = regionbase->first; region; region = region->next) {
      if (region->regiontype == RGN_TYPE_WINDOW) {
        RegionView3D *rv3d = is_local ? ((RegionView3D *)region->regiondata)->localvd :
                                        region->regiondata;
        if (rv3d && (rv3d->persp == RV3D_CAMOB)) {
          rv3d->persp = RV3D_PERSP;
        }
      }
    }
  }
}

static void view3d_id_remap(ScrArea *area, SpaceLink *slink, const struct IDRemapper *mappings)
{

  if (!DUNE_id_remapper_has_mapping_for(
          mappings, FILTER_ID_OB | FILTER_ID_MA | FILTER_ID_IM | FILTER_ID_MC)) {
    return;
  }

  View3D *view3d = (View3D *)slink;
  view3d_id_remap_v3d(area, slink, view3d, mappings, false);
  view3d_id_remap_v3d_ob_centers(view3d, mappings);
  if (view3d->localvd != NULL) {
    /* Object centers in local-view aren't used, see: T52663 */
    view3d_id_remap_v3d(area, slink, view3d->localvd, mappings, true);
  }
}

void ED_spacetype_view3d(void)
{
  SpaceType *st = MEM_callocN(sizeof(SpaceType), "spacetype view3d");
  ARegionType *art;

  st->spaceid = SPACE_VIEW3D;
  strncpy(st->name, "View3D", BKE_ST_MAXNAME);

  st->create = view3d_create;
  st->free = view3d_free;
  st->init = view3d_init;
  st->exit = view3d_exit;
  st->listener = space_view3d_listener;
  st->refresh = space_view3d_refresh;
  st->duplicate = view3d_duplicate;
  st->operatortypes = view3d_operatortypes;
  st->keymap = view3d_keymap;
  st->dropboxes = view3d_dropboxes;
  st->gizmos = view3d_widgets;
  st->context = view3d_context;
  st->id_remap = view3d_id_remap;

  /* regions: main window */
  art = MEM_callocN(sizeof(ARegionType), "spacetype view3d main region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_TOOL | ED_KEYMAP_GPENCIL;
  art->draw = view3d_main_region_draw;
  art->init = view3d_main_region_init;
  art->exit = view3d_main_region_exit;
  art->free = view3d_main_region_free;
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
