#include "types_defaults.h"
#include "types_pen_legacy.h"
#include "types_img.h"
#include "types_mask.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_threads.h"

#include "dune_colortools.h"
#include "dune_cxt.hh"
#include "dune_img.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_screen.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "graph.hh"

#include "imbuf_types.h"

#include "ed_img.hh"
#include "ed_mask.hh"
#include "ed_node.hh"
#include "ed_render.hh"
#include "ed_screen.hh"
#include "ed_space_api.hh"
#include "ed_transform.hh"
#include "ed_util.hh"
#include "ed_uvedit.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "loader_read_write.hh"

#include "drw_engine.h"

#include "img_intern.h"

/* common state */
static void img_scopes_tag_refresh(ScrArea *area)
{
  SpaceImg *simg = (SpaceImg *)area->spacedata.first;

  /* only while histogram is visible */
  LIST_FOREACH (ARgn *, rgn, &area->rgnbase) {
    if (rgn->rgntype == RGN_TYPE_TOOL_PROPS && region->flag & RGN_FLAG_HIDDEN) {
      return;
    }
  }

  simg->scopes.ok = 0;
}

static void img_user_refresh_scene(const Cxt *C, SpaceImg *simg)
{
  /* Update scene img user for acquiring render results. */
  simg->iuser.scene = cxt_data_scene(C);

  if (simg->img && simg->img->type == IMG_TYPE_R_RESULT) {
    /* While rendering, prefer scene that is being rendered. */
    Scene *render_scene = ed_render_job_get_current_scene(C);
    if (render_scene) {
      simg->iuser.scene = render_scene;
    }
  }

  /* Auto switch img to show in UV editor when sel changes. */
  ed_space_img_auto_set(C, simg);
}

/* default cbs for img space */
static SpaceLink *img_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARgn *rgn;
  SpaceImg *simg;

  simg = static_cast<SpaceImg *>(mem_calloc(sizeof(SpaceImg), "initimg"));
  simg->spacetype = SPACE_IMG;
  simg->zoom = 1.0f;
  simg->lock = true;
  simg->flag = SI_SHOW_PEN | SI_USE_ALPHA | SI_COORDFLOATS;
  simg->uv_opacity = 1.0f;
  simg->overlay.flag = SI_OVERLAY_SHOW_OVERLAYS | SI_OVERLAY_SHOW_GRID_BACKGROUND;

  dune_imguser_default(&simg->iuser);
  simg->iuser.flag = IMA_SHOW_STEREO | IMA_ANIM_ALWAYS;

  dune_scopes_new(&simg->scopes);
  simg->sample_line_hist.height = 100;

  simg->tile_grid_shape[0] = 0;
  simg->tile_grid_shape[1] = 1;

  simg->custom_grid_subdiv[0] = 10;
  simg->custom_grid_subdiv[1] = 10;

  simg->mask_info = *types_struct_default_get(MaskSpaceInfo);

  /* header */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "header for img"));

  lib_addtail(&simg->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_HEADER;
  rgn->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* tool header */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "tool header for img"));

  lib_addtail(&simg->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_TOOL_HEADER;
  rgn->flag = RGN_FLAG_HIDDEN | RGN_FLAG_HIDDEN_BY_USER;

  /* btns/list view */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "btns for img"));

  lib_addtail(&simg->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_UI;
  rgn->alignment = RGN_ALIGN_RIGHT;
  rgn->flag = RGN_FLAG_HIDDEN;

  /* scopes/uv sculpt/paint */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "btn for img"));

  lib_addtail(&simg->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_TOOLS;
  rgn->alignment = RGN_ALIGN_LEFT;
  rgn->flag = RGN_FLAG_HIDDEN;

  /* main area */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "main area for img"));

  lib_addtail(&simg->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_WIN;

  return (SpaceLink *)simg;
}

/* Doesn't free the space-link itself. */
static void img_free(SpaceLink *sl)
{
  SpaceImg *simg = (SpaceImg *)sl;

  dune_scopes_free(&sime->scopes);
}

/* spacetype; init cb, add handlers */
static void img_init(WinMngr * /*wm*/, ScrArea *area)
{
  List *list = win_dropboxmap_find("Imag", SPACE_IMG, RGN_TYPE_WIN);

  /* add drop boxes */
  win_ev_add_dropbox_handler(&area->handlers, list);
}

static SpaceLink *img_dup(SpaceLink *sl)
{
  SpaceImg *simg = static_cast<SpaceImg *>(mem_dupalloc(sl));

  /* clear or remove stuff from old */
  dune_scopes_new(&simg->scopes);

  return (SpaceLink *)simg;
}

static void img_optypes()
{
  win_optype_append(IMG_OT_view_all);
  win_optype_append(IMG_OT_view_pan);
  win_optype_append(IMG_OT_view_selected);
  win_optype_append(IMG_OT_view_center_cursor);
  win_optype_append(IMG_OT_view_cursor_center);
  win_optype_append(IMG_OT_view_zoom);
  win_optype_append(IMG_OT_view_zoom_in);
  win_optype_append(IMG_OT_view_zoom_out);
  win_optype_append(IMG_OT_view_zoom_ratio);
  win_optype_append(IMG_OT_view_zoom_border);
#ifdef WITH_INPUT_NDOF
  win_optype_append(IMG_OT_view_ndof);
#endif

  win_optype_append(IMG_OT_new);
  win_optype_append(IMG_OT_open);
  win_optype_append(IMG_OT_file_browse);
  win_optype_append(IMG_OT_match_movie_length);
  win_optype_append(IMG_OT_replace);
  win_optype_append(IMG_OT_reload);
  win_optype_append(IMG_OT_save);
  win_optype_append(IMG_OT_save_as);
  win_optype_append(IMG_OT_save_sequence);
  win_optype_append(IMG_OT_save_all_modified);
  win_optype_append(IMG_OT_pack);
  win_optype_append(IMG_OT_unpack);
  win_optype_append(IMG_OT_clipboard_copy);
  win_optype_append(IMG_OT_clipboard_paste);

  win_optype_append(IMG_OT_flip);
  win_optype_append(IMG_OT_invert);
  win_optype_append(IMG_OT_resize);

  win_optype_append(IMG_OT_cycle_render_slot);
  win_optype_append(IMG_OT_clear_render_slot);
  win_optype_append(IMG_OT_add_render_slot);
  win_optype_append(IMG_OT_remove_render_slot);

  win_optype_append(IMG_OT_sample);
  win_optype_append(IMG_OT_sample_line);
  win_optype_append(IMG_OT_curves_point_set);

  win_optype_append(IMG_OT_change_frame);

  win_optype_append(IMG_OT_read_viewlayers);
  win_optype_append(IMG_OT_render_border);
  win_optype_append(IMG_OT_clear_render_border);

  win_optype_append(IMG_OT_tile_add);
  win_optype_append(IMG_OT_tile_remove);
  win_optype_append(IMG_OT_tile_fill);
}

static void img_keymap(WinKeyConfig *keyconf)
{
  win_keymap_ensure(keyconf, "Img Generic", SPACE_IMG, RGN_TYPE_WIN);
  win_keymap_ensure(keyconf, "Img", SPACE_IMG, RGN_TYPE_WIN);
}

/* dropboxes */
static bool img_drop_poll(Cxt *C, WinDrag *drag, const WinEv *ev)
{
  ScrArea *area = ed_win_area(C);
  if (ed_rgn_overlap_isect_any_xy(area, ev->xy)) {
    return false;
  }
  if (drag->type == WIN_DRAG_PATH) {
    const eFileSelFileTypes file_type = eFileSelFileTypes(win_drag_get_path_file_type(drag));
    if (ELEM(file_type, 0, FILE_TYPE_IMG, FILE_TYPE_MOVIE)) {
      return true;
    }
  }
  return false;
}

static void img_drop_copy(Cxt * /*C*/, WinDrag *drag, WinDropBox *drop)
{
  /* copy drag path to props */
  api_string_set(drop->ptr, "filepath", win_drag_get_path(drag));
}

/* area+rgn dropbox definition */
static void img_dropboxes()
{
  List *list = win_dropboxmap_find("Img", SPACE_IMG, RGN_TYPE_WIN);

  win_dropbox_add(lb, "IMG_OT_open", img_drop_poll, img_drop_copy, nullptr, nullptr);
}

/* take care not to get into feedback loop here,
 * calling composite job causes viewer to refresh. */
static void img_refresh(const Cxt *C, ScrArea *area)
{
  Scene *scene = cxt_data_scene(C);
  SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
  Img *img;

  img = ed_space_img(simg);
  dune_img_user_frame_calc(img, &simg->iuser, scene->r.cfra);

  /* Check if we have to set the img from the edit-mesh. */
  if (img && (img->src == IMG_SRC_VIEWER && simg->mode == SI_MODE_MASK)) {
    if (scene->nodetree) {
      Mask *mask = ed_space_img_get_mask(simg);
      if (mask) {
        ed_node_composite_job(C, scene->nodetree, scene);
      }
    }
  }
}

static void img_listener(const WinSpaceTypeListenerParams *params)
{
  Win *win = params->win;
  ScrArea *area = params->area;
  const WinNotifier *winn = params->notifier;
  SpaceImg *sim = (SpaceImg *)area->spacedata.first;

  /* cxt changes */
  switch (winn->category) {
    case NC_WIN:
      /* notifier comes from editing color space */
      img_scopes_tag_refresh(area);
      ed_area_tag_redrw(area);
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_FRAME:
          img_scopes_tag_refresh(area);
          ed_area_tag_refresh(area);
          ed_area_tag_redrw(area);
          break;
        case ND_MODE:
          ed_paint_cursor_start(&params->scene->toolsettings->imapaint.paint,
                                ed_img_tools_paint_poll);

          if (winn->subtype == NS_EDITMODE_MESH) {
            ed_area_tag_refresh(area);
          }
          ed_area_tag_redrw(area);
          break;
        case ND_RENDER_RESULT:
        case ND_RENDER_OPTIONS:
        case ND_COMPO_RESULT:
          if (ed_space_img_show_render(simg)) {
            img_scopes_tag_refresh(area);
            dune_img_partial_update_mark_full_update(simg->img);
          }
          ed_area_tag_redrw(area);
          break;
      }
      break;
    case NC_IMG:
      if (winn->ref == simg->img || !winn->ref) {
        if (winn->action != NA_PAINTING) {
          img_scopes_tag_refresh(area);
          ed_area_tag_refresh(area);
          ed_area_tag_redrw(area);
        }
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_IMG) {
        img_scopes_tag_refresh(area);
        ed_area_tag_redrw(area);
      }
      break;
    case NC_MASK: {
      Scene *scene = win_get_active_scene(win);
      ViewLayer *view_layer = win_get_active_view_layer(win);
      dune_view_layer_synced_ensure(scene, view_layer);
      Ob *obedit = dune_view_layer_edit_ob_get(view_layer);
      if (ed_space_img_check_show_maskedit(simg, obedit)) {
        switch (winn->data) {
          case ND_SEL:
            ed_area_tag_redrw(area);
            break;
          case ND_DATA:
          case ND_DRW:
            /* causes node-recalc */
            ed_area_tag_redrw(area);
            ed_area_tag_refresh(area);
            break;
        }
        switch (winn->action) {
          case NA_SEL:
            ed_area_tag_redrw(area);
            break;
          case NA_EDITED:
            /* causes node-recalc */
            ed_area_tag_redrw(area);
            ed_area_tag_refresh(area);
            break;
        }
      }
      break;
    }
    case NC_GEOM: {
      switch (winn->data) {
        case ND_DATA:
        case ND_SEL:
          img_scopes_tag_refresh(area);
          ed_area_tag_refresh(area);
          ed_area_tag_redrw(area);
          break;
      }
      break;
    }
    case NC_OB: {
      switch (win->data) {
        case ND_TRANSFORM:
        case ND_MOD: {
          const Scene *scene = win_get_active_scene(win);
          ViewLayer *view_layer = win_get_active_view_layer(win);
          
          dune_view_layer_synced_ensure(scene, view_layer);
          Ob *ob = dun_view_layer_active_ob_get(view_layer);
          /* W a geometry nodes mod, the UVs on `ob` can change in response to
           * any change on `winn->ref`. If we could track the upstream dependencies,
           * unnecessary redrws could be reduced. Until then, just redrw. See #98594. */
          if (ob && (ob->mode & OB_MODE_EDIT)) {
            if (sim->lock && (sim->flag & SI_DRAWSHADOW)) {
              ed_area_tag_refresh(area);
              ed_area_tag_redrw(area);
            }
          }
          break;
        }
      }

      break;
    }
    case NC_ID: {
      if (winn->action == NA_RENAME) {
        ed_area_tag_redrw(area);
      }
      break;
    }
    case NC_WIN:
      if (winn->data == ND_UNDO) {
        ed_area_tag_redrw(area);
        ed_area_tag_refresh(area);
      }
      break;
  }
}

const char *img_cxt_dir[] = {"edit_img", "edit_mask", nullptr};

static int /*eCxtResult*/ img_cxt(const Cxt *C,
                                  const char *member,
                                  CxtDataResult *result)
{
  SpaceImg *simg = cxt_win_space_img(C);

  if (_data_dir(member)) {
    cxt_data_dir_set(result, img_cxt_dir);
    // return CXT_RESULT_OK; /* TODO(@sybren). */
  }
  else if (cxy_data_equals(member, "edit_img")) {
    cxt_data_id_ptr_set(result, (Id *)ed_space_img(simg));
    return CXT_RESULT_OK;
  }
  else if (cxt_data_equals(member, "edit_mask")) {
    Mask *mask = ed_space_img_get_mask(simg);
    if (mask) {
      cxt_data_id_ptr_set(result, &mask->id);
    }
    return CXT_RESULT_OK;
  }
  return CXT_RESULT_MEMBER_NOT_FOUND;
}

static void IMG_GGT_gizmo2d(WinGizmoGroupType *gzgt)
{
  gzgt->name = "UV Transform Gizmo";
  gzgt->idname = "IMG_GGT_gizmo2d";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_DRW_MODAL_EXCLUDE | WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
                 WIN_GIZMOGROUPTYPE_DELAY_REFRESH_FOR_TWEAK);

  gzgt->gzmap_params.spaceid = SPACE_IMG;
  gzgt->gzmap_params.rgnid = RGN_TYPE_WIN;

  ed_widgetgroup_gizmo2d_xform_cbs_set(gzgt);
}

static void IMG_GGT_gizmo2d_translate(WinGizmoGroupType *gzgt)
{
  gzgt->name = "UV Translate Gizmo";
  gzgt->idname = "IMG_GGT_gizmo2d_translate";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_DRW_MODAL_EXCLUDE | WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
                 WIN_GIZMOGROUPTYPE_DELAY_REFRESH_FOR_TWEAK);

  gzgt->gzmap_params.spaceid = SPACE_IMG;
  gzgt->gzmap_params.rgnid = RGN_TYPE_WI;

  ed_widgetgroup_gizmo2d_xform_no_cage_cbs_set(gzgt);
}

static void IMG_GGT_gizmo2d_resize(WinGizmoGroupType *gzgt)
{
  gzgt->name = "UV Transform Gizmo Resize";
  gzgt->idname = "IMG_GGT_gizmo2d_resize";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_DRW_MODAL_EXCLUDE | WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
                 WIN_GIZMOGROUPTYPE_DELAY_REFRESH_FOR_TWEAK);

  gzgt->gzmap_params.spaceid = SPACE_IMG;
  gzgt->gzmap_params.rgnid = RGN_TYPE_WIN;

  ed_widgetgroup_gizmo2d_resize_cbs_set(gzgt);
}

static void IMG_GGT_gizmo2d_rotate(WinGizmoGroupType *gzgt)
{
  gzgt->name = "UV Transform Gizmo Resize";
  gzgt->idname = "IMG_GGT_gizmo2d_rotate";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_DRA_MODAL_EXCLUDE | WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
                 WIN_GIZMOGROUPTYPE_DELAY_REFRESH_FOR_TWEAK);

  gzgt->gzmap_params.spaceid = SPACE_IMG;
  gzgt->gzmap_params.regionid = RGN_TYPE_WIN;

  ed_widgetgroup_gizmo2d_rotate_cbs_set(gzgt);
}

static void IMG_GGT_nav(WinGizmoGroupType *gzgt)
{
  VIEW2D_GGT_nav_impl(gzgt, "IMG_GGT_nav");
}

static void img_widgets()
{
  const WinGizmoMapType_Params params{SPACE_IMAGE, RGN_TYPE_WINDOW};
  WinGizmoMapType *gzmap_type = WM_gizmomaptype_ensure(&params);

  win_gizmogrouptype_append(IMG_GGT_gizmo2d);
  win_gizmogrouptype_append(IMG_GGT_gizmo2d_translate);
  win_gizmogrouptype_append(IMG_GGT_gizmo2d_resize);
  win_gizmogrouptype_append(IMG_GGT_gizmo2d_rotate);

  win_gizmogrouptype_append_and_link(gzmap_type, IMAGE_GGT_navigate);
}

/* main rgn */
/* sets up the fields of the View2D from zoom and offset */
static void img_main_rgn_set_view2d(SpaceImg *simg, ARgn *rgn)
{
  Img *img = ed_space_img(simg);

  int width, height;
  ed_space_img_get_size(simg, &width, &height);

  float w = width;
  float h = height;

  if (ima) {
    h *= img->aspy / img->aspx;
  }

  int winx = lib_rcti_size_x(&rgn->winrct) + 1;
  int winy = lib_rcti_size_y(&rgn->winrct) + 1;

  /* For rgn overlap, move center so img doesn't overlap header. */
  const rcti *visible_rect = ed_rgn_visible_rect(rgn);
  const int visible_winy = lib_rcti_size_y(visible_rect) + 1;
  int visible_centerx = 0;
  int visible_centery = visible_rect->ymin + (visible_winy - winy) / 2;

  rgn->v2d.tot.xmin = 0;
  rgn->v2d.tot.ymin = 0;
  rgn->v2d.tot.xmax = w;
  rgn->v2d.tot.ymax = h;

  rgn->v2d.mask.xmin = rgn->v2d.mask.ymin = 0;
  rgn->v2d.mask.xmax = winx;
  rgn->v2d.mask.ymax = winy;

  /* which part of the img space do we see? */
  float x1 = rgn->winrct.xmin + visible_centerx + (winx - simg->zoom * w) / 2.0f;
  float y1 = rgn->winrct.ymin + visible_centery + (winy - simg->zoom * h) / 2.0f;

  x1 -= simg->zoom * simg->xof;
  y1 -= simg->zoom * simg->yof;

  /* relative display right */
  rgn->v2d.cur.xmin = ((rgn->winrct.xmin - float(x1)) / simg->zoom);
  rgn->v2d.cur.xmax = rgn->v2d.cur.xmin + (float(winx) / simg->zoom);

  /* relative display left */
  rgn->v2d.cur.ymin = ((rgn->winrct.ymin - float(y1)) / simg->zoom);
  rgn->v2d.cur.ymax = rgn->v2d.cur.ymin + (float(winy) / simg->zoom);

  /* normalize 0.0..1.0 */
  rgn->v2d.cur.xmin /= w;
  rgn->v2d.cur.xmax /= w;
  rgn->v2d.cur.ymin /= h;
  rgn->v2d.cur.ymax /= h;
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void img_main_rgn_init(WiwMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  /* Don't use `ui_view2d_re_reinit(&rgn->v2d, ...)`
   * since the space clip manages own v2d in img_main_rgn_set_view2d */
  /* mask polls mode */
  keymap = win_keymap_ensure(win->defaultconf, "Mask Editing", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);

  /* img paint polls for mode */
  keymap = win_keymap_ensure(win->defaultconf, "Curve", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Paint Curve", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Img Paint", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "UV Editor", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);

  /* own keymaps */
  keymap = win_keymap_ensure(wm->defaultconf, "Img Generic", SPACE_IMG, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
  keymap = win_keymap_ensure(wm->defaultconf, "Img", SPACE_IMG, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);
}

static void img_main_rgn_drw(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  SpaceImg *simg = cxt_win_space_img(C);
  Ob *obedit = cxt_data_edit_ob(C);
  Graph *graph = cxt_data_expect_eval_graph(C);
  Mask *mask = nullptr;
  Scene *scene = cxt_data_scene(C);
  View2D *v2d = &rgn->v2d;
  Img *img = ed_space_img(simf);
  const bool show_viewer = (img && img->source == IMG_SRC_VIEWER);

  /* not supported yet, disabling for now */
  scene->r.scemode &= ~R_COMP_CROP;

  img_user_refresh_scene(C, simg);

  /* we set view2d from own zoom and offset each time */
  img_main_rgn_set_view2d(simg, rgn);

  /* check for mask (delay drw) */
  if (!ed_space_img_show_uvedit(simg, obedit) && sima->mode == SI_MODE_MASK) {
    mask = ed_space_img_get_mask(simg);
  }

  if (show_viewer) {
    lib_thread_lock(LOCK_DRW_IMG);
  }
  dune_view(C);
  if (show_viewer) {
    lib_thread_unlock(LOCK_DRW_IMG);
  }

  drw_img_main_helpers(C, rgn);

  /* Drw Meta data of the img isn't added to the DrawMngr as it is
   * used in other areas as well. */
  if (simg->overlay.flag & SI_OVERLAY_SHOW_OVERLAYS && simg->flag & SI_DRW_METADATA) {
    void *lock;
    /* `ed_space_img_get_zoom` tmp locks the img, so this needs to be done before
     * the img is locked when calling `ed_space_img_acquire_buffer`. */
    float zoomx, zoomy;
    ed_space_img_get_zoom(simg, rgn, &zoomx, &zoomy);
    ImBuf *ibuf = ed_space_img_acquire_buffer(simg, &lock, 0);
    if (ibuf) {
      int x, y;
      rctf frame;
      lib_rctf_init(&frame, 0.0f, ibuf->x, 0.0f, ibuf->y);
      ui_view2d_view_to_rgn(&rgn->v2d, 0.0f, 0.0f, &x, &y);
      ed_rgn_img_metadata_drw(x, y, ibuf, &frame, zoomx, zoomy);
    }
    ed_space_img_release_buffer(simg, ibuf, lock);
  }

  /* sample line */
  ui_view2d_view_ortho(v2d);
  drw_img_sample_line(simg);
  ui_view2d_view_restore(C);

  if (mask) {
    int width, height;
    float aspx, aspy;

    if (show_viewer) {
      /* ed_space_img_get* will acquire img buffer which requires
       * lock here by the same reason why lock is needed in draw_image_main */
      lib_thread_lock(LOCK_DRW_IMG);
    }

    ed_space_img_get_size(simg, &width, &height);
    ed_space_img_get_aspect(simg, &aspx, &aspy);

    if (show_viewer) {
      lib_thread_unlock(LOCK_DRW_IMG);
    }

    ed_mask_drw_rgn(graph,
                    mask,
                    rgn, /* Mask overlay is drwn by img/overlay engine. */
                    simg->mask_info.drw_flag & ~MASK_DRWFLAG_OVERLAY,
                    simg->mask_info.drw_type,
                    eMaskOverlayMode(simg->mask_info.overlay_mode),
                        simg->mask_info.blend_factor,
                        width,
                        height,
                        aspx,
                        aspy,
                        true,
                        false,
                        nullptr,
                        C);
  }
  if ((simg->gizmo_flag & SI_GIZMO_HIDE) == 0) {
    win_gizmomap_drw(rgn->gizmo_map, C, WIN_GIZMOMAP_DRWSTEP_2D);
  }
  drw_img_cache(C, rgn);
}

static void img_main_rgn_listener(const WinRgnListenerParams *params)
{
  ScrArea *area = params->area;
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_GEOM:
      if (ELEM(winn->data, ND_DATA, ND_SEL)) {
        win_gizmomap_tag_refresh(rgn->gizmo_map);
      }
      break;
    case NC_PEN:
      if (ELEM(winn->action, NA_EDITED, NA_SEL)) {
        ed_rgn_tag_redrw(rgn)
      }
      else if (winn->data & ND_PEN_EDITMODE) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_IMG:
      if (winn->action == NA_PAINTING) {
        ed_rgn_tag_redrw(rgn);
      }
      win_gizmomap_tag_refresh(rgn->gizmo_map);
      break;
    case NC_MATERIAL:
      if (win->data == ND_SHADING_LINKS) {
        SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);

        if (simg->iuser.scene && (simg->iuser.scene->toolsettings->uv_flag & UV_SHOW_SAME_IMG)) {
          ed_rgn_tag_redrw(rgn);
        }
      }
      break;
    case NC_SCREEN:
      if (ELEM(winn->data, ND_LAYER)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

/* btns rgn */
/* add handlers, stuff you only do once or on area/rgn changes */
static void img_btns_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  rgn->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "Img Generic", SPACE_IMG, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void img_btns_rgn_layout(const Cxt *C, ARgn *rgn)
{
  const enum eCxtObMode mode = cxt_data_mode_enum(C);
  const char *cxts_base[3] = {nullptr};

  const char **cxts = cxts_base;

  SpaceImg *simg = cxt_win_space_img(C);
  switch (simg->mode) {
    case SI_MODE_VIEW:
      break;
    case SI_MODE_PAINT:
      ARRAY_SET_ITEMS(cxts, ".paint_common_2d", ".imgpaint_2d");
      break;
    case SI_MODE_MASK:
      break;
    case SI_MODE_UV:
      if (mode == CXT_MODE_EDIT_MESH) {
        ARRAY_SET_ITEMS(cxts, ".uv_sculpt");
      }
      break;
  }

  ed_rgn_panels_layout_ex(C, rgn, &rgn->type->pnltypes, cxts_base, nullptr);
}

static void img_btns_rgn_drw(const Cxt *C, ARgn *rgn)
{
  SpaceImg *simg = cxt_win_space_img(C);
  Scene *scene = cxt_data_scene(C);
  void *lock;
  /* Support tiles in scopes? */
  ImBuf *ibuf = ed_space_img_acquire_buffer(simg, &lock, 0);
  /* performance regression if name of scopes category changes! */
  PnlCategoryStack *category = ui_pnl_category_active_find(rgn, "Scopes");

  /* only update scopes if scope category is active */
  if (category) {
    if (ibuf) {
      if (!simg->scopes.ok) {
        dune_histogram_update_sample_line(
            &simg->sample_line_hist, ibuf, &scene->view_settings, &scene->display_settings);
      }
      if (simg->img->flag & IMG_VIEW_AS_RENDER) {
        ed_space_img_scopes_update(C, simg, ibuf, true);
      }
      else {
        ed_space_img_scopes_update(C, simg, ibuf, false);
      }
    }
  }
  ed_space_img_release_buffer(simg, ibuf, lock);

  /* Layout handles details. */
  ed_rgn_pnls_drw(C, rgn);
}

static void img_btns_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_TEXTURE:
    case NC_MATERIAL:
      /* sending by texture render job and needed to properly update displaying
       * brush texture icon */
      ed_rgn_tag_redrw(rgn);
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_MODE:
        case ND_RENDER_RESULT:
        case ND_COMPO_RESULT:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_IMG:
      if (winn->action != NA_PAINTING) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_NODE:
      ed_rgn_tag_redrw(rgn);
      break;
    case NC_PEN:
      if (ELEM(winn->action, NA_EDITED, NA_SEL)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_BRUSH:
      if (winn->action == NA_EDITED) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

/* scopes rgn */
/* add handlers, stuff you only do once or on area/rgn changes */
static void img_tools_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  rgn->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "Img Generic", SPACE_IMG, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void img_tools_rgn_drw(const Cxt *C, ARgn *rgn)
{
  ed_rgn_pnls(C, rgn);
}

static void img_tools_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_PEN:
      if (winn->data == ND_DATA || ELEM(winn->action, NA_EDITED, NA_SEL)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_BRUSH:
      /* NA_SEL is used on brush changes */
      if (ELEM(winn->action, NA_EDITED, NA_SEL)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_MODE:
        case ND_RENDER_RESULT:
        case ND_COMPO_RESULT:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_IMG:
      if (winn->action != NA_PAINTING) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_NODE:
      ed_rgn_tag_redrw(rgn);
      break;
  }
}

/* Tool header rgn */
static void img_tools_header_rgn_drw(const Cxt *C, ARgn *rgm)
{
  ScrArea *area = cxt_win_area(C);
  SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);

  img_user_refresh_scene(C, simg);

  ed_rgn_header_with_btn_sections(
      C,
      rgn,
      (RGN_ALIGN_ENUM_FROM_MASK(rgn->alignment) == RGN_ALIGN_TOP) ?
          BtnSectionsAlign::Top :
          BtnSectionsAlign::Bottom);
}

/* header rgn */
/* add handlers, stuff you only do once or on area/rgn changes */
static void img_header_rgn_init(WinMngr * /*wm*/, ARgn *rgn)
{
  ed_rg_header_init(rgn);
}

static void img_header_rgn_drw(const Cxt *C, ARgn *rgn)
{
  ScrArea *area = cxt_win_area(C);
  SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);

  img_user_refresh_scene(C, simg);

  ed_rgn_header(C, rgn);
}

static void img_header_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_SCENE:
      switch (winn->data) {
        case ND_MODE:
        case ND_TOOLSETTINGS:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_GEOM:
      switch (winn->data) {
        case ND_DATA:
        case ND_SEL:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_BRUSH:
      if (winn->action == NA_EDITED) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

static void img_id_remap(ScrArea * /*area*/, SpaceLink *slink, const IdRemapper *mappings)
{
  SpaceImg *simg = (SpaceImg *)slink;

  if (!dune_id_remapper_has_mapping_for(mappings,
                                        FILTER_ID_IM | FILTER_ID_GD_LEGACY | FILTER_ID_MSK)) {
    return;
  }

  dune_id_remapper_apply(mappings, (Id **)&simg->img, ID_REMAP_APPLY_ENSURE_REAL);
  dune_id_remapper_apply(mappings, (Id **)&simg->gpd, ID_REMAP_APPLY_UPDATE_REFCOUNT);
  dune_id_remapper_apply(mappings, (Id **)&simg->mask_info.mask, ID_REMAP_APPLY_ENSURE_REAL);
}

static void img_foreach_id(SpaceLink *space_link, LibForeachIdData *data)
{
  SpaceImg *simg = reinterpret_cast<SpaceImg *>(space_link);
  const int data_flags = dune_lib_query_foreachid_process_flags_get(data);
  const bool is_readonly = (data_flags & IDWALK_READONLY) != 0;

  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->img, IDWALK_CB_USER_ONE);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->iuser.scene, IDWALK_CB_NOP);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->mask_info.mask, IDWALK_CB_USER_ONE);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->pendata, IDWALK_CB_USER);
  if (!is_readonly) {
    simg->scopes.ok = 0;
  }
}

/* Used for splitting out a subset of modes is more involved,
 * The previous non-uv-edit mode is stored so switching back to the
 * img doesn't always reset the sub-mode */
static int img_space_subtype_get(ScrArea *area)
{
  SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
  return simg->mode == SI_MODE_UV ? SI_MODE_UV : SI_MODE_VIEW;
}

static void img_space_subtype_set(ScrArea *area, int val)
{
  SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
  if (val == SI_MODE_UV) {
    if (simg->mode != SI_MODE_UV) {
      simg->mode_prev = simg->mode;
    }
    simg->mode = val;
  }
  else {
    simg->mode = simg->mode_prev;
  }
}

static void img_space_subtype_item_extend(Cxt * /*C*/,
                                          EnumPropItem **item,
                                          int *totitem)
{
  api_enum_items_add(item, totitem, api_enum_space_img_mode_items);
}

static void img_space_dune_read_data(DataReader * /*reader*/, SpaceLink *sl)
{
  SpaceImg *simg = (SpaceImg *)sl;

  simg->iuser.scene = nullptr;
  simg->scopes.waveform_1 = nullptr;
  simg->scopes.waveform_2 = nullptr;
  simg->scopes.waveform_3 = nullptr;
  simg->scopes.vecscope = nullptr;
  simg->scopes.ok = 0;

/* WARNING: pendata is no longer stored directly in sima after 2.5
 * so sacrifice a few old files for now to avoid crashes with new files!
 * committed: r28002 */
#if 0
  simg->pdata = newdataadr(fd, simg->pdata);
  if (simg->pdata) {
    dune_pen_dune_read_data(fd, simg->pdata);
  }
#endif
}

static void img_space_dune_write(Writer *writer, SpaceLink *sl)
{
  loader_write_struct(writer, SpaceImg, sl);
}

/* spacetype */
void ed_spacetype_img()
{
  SpaceType *st = static_cast<SpaceType *>(mem_calloc(sizeof(SpaceType), "spacetype img"));
  ARgnType *art;

  st->spaceid = SPACE_IMG;
  STRNCPY(st->name, "Img");

  st->create = img_create;
  st->free = img_free;
  st->init = img_init;
  st->dup = img_dup;
  st->optypes = img_optypes;
  st->keymap = img_keymap;
  st->dropboxes = img_dropboxes;
  st->refresh = img_refresh;
  st->listener = img_listener;
  st->cxt = img_cxt;
  st->gizmos = img_widgets;
  st->id_remap = img_id_remap;
  st->foreach_id = img_foreach_id;
  st->space_subtype_item_extend = img_space_subtype_item_extend;
  st->space_subtype_get = img_space_subtype_get;
  st->space_subtype_set = img_space_subtype_set;
  st->dune_read_data = img_space_dune_read_data;
  st->dune_read_after_liblink = nullptr;
  st->dune_write = img_space_dune_write;

  /* rgns: main win */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype img rgn"));
  art->rgnid = RGN_TYPE_WIN;
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_TOOL | ED_KEYMAP_FRAMES | ED_KEYMAP_PEN;
  art->init = img_main_rgn_init;
  art->drw = img_main_rgn_drw;
  art->listener = img_main_rgn_listener;
  lib_addhead(&st->rgntypes, art);

  /* rgns: list-view/btns/scopes */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype iag rgn");
  art->rgnid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PNL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = img_btns_rgn_listener;
  art->msg_sub = ed_area_do_msg_sub_for_tool_ui;
  art->init = img_btns_rgn_init;
  art->layout = img_btns_rgn_layout;
  art->drw = imf_btns_rgn_drw;
  lib_addhead(&st->rgntypes, art);

  ed_uvedit_btns_register(art);
  img_btns_register(art);

  /* rgns: tool(bar) */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype img rgn"));
  art->rgnid = RGN_TYPE_TOOLS;
  art->prefsizex = int(UI_TOOLBAR_WIDTH);
  art->prefsizey = 50;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = img_tools_rgn_listener;
  art->msg_sub = ed_rgn_generic_tools_rgn_msg_sub;
  art->snap_size = ed_rgn_generic_tools_rgn_snap_size;
  art->init = img_tools_rgn_init;
  art->dre = img_tools_rgn_drw;
  lib_addhead(&st->rgntypes, art);

  /* rgns: tool header */
  art = static_cast<ARgnType *>(
      mem_calloc(sizeof(ARgnType), "spacetype img tool header rgn"));
  art->rgnid = RGN_TYPE_TOOL_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ed_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->listener = img_header_rgn_listener;
  art->init = img_header_rgn_init;
  art->drw = img_tools_header_rgn_drw;
  art->msg_sub = ed_area_do_msg_sub_for_tool_header;
  lib_addhead(&st->rgntypes, art);

  /* rgns: header */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype img rgn"));
  art->rgnid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->listener = img_header_rgn_listener;
  art->init = img_header_rgn_init;
  art->drw = img_header_rgn_drw;

  lib_addhead(&st->rgntypes, art);

  /* regions: hud */
  art = ed_area_type_hud(st->spaceid);
  lib_addhead(&st->rgntypes, art);

  dune_spacetype_register(st);
}
