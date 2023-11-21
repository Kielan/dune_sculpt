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

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_DRAW_MODAL_EXCLUDE | WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
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
  gzgt->gzmap_params.regionid = RGN_TYPE_WIN;

  ed_widgetgroup_gizmo2d_resize_cbs_set(gzgt);
}

static void IMAGE_GGT_gizmo2d_rotate(wmGizmoGroupType *gzgt)
{
  gzgt->name = "UV Transform Gizmo Resize";
  gzgt->idname = "IMAGE_GGT_gizmo2d_rotate";

  gzgt->flag |= (WM_GIZMOGROUPTYPE_DRAW_MODAL_EXCLUDE | WM_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP |
                 WM_GIZMOGROUPTYPE_DELAY_REFRESH_FOR_TWEAK);

  gzgt->gzmap_params.spaceid = SPACE_IMAGE;
  gzgt->gzmap_params.regionid = RGN_TYPE_WINDOW;

  ED_widgetgroup_gizmo2d_rotate_callbacks_set(gzgt);
}

static void IMAGE_GGT_navigate(wmGizmoGroupType *gzgt)
{
  VIEW2D_GGT_navigate_impl(gzgt, "IMAGE_GGT_navigate");
}

static void image_widgets()
{
  const wmGizmoMapType_Params params{SPACE_IMAGE, RGN_TYPE_WINDOW};
  wmGizmoMapType *gzmap_type = WM_gizmomaptype_ensure(&params);

  WM_gizmogrouptype_append(IMAGE_GGT_gizmo2d);
  WM_gizmogrouptype_append(IMAGE_GGT_gizmo2d_translate);
  WM_gizmogrouptype_append(IMAGE_GGT_gizmo2d_resize);
  WM_gizmogrouptype_append(IMAGE_GGT_gizmo2d_rotate);

  WM_gizmogrouptype_append_and_link(gzmap_type, IMAGE_GGT_navigate);
}

/************************** main region ***************************/

/* sets up the fields of the View2D from zoom and offset */
static void image_main_region_set_view2d(SpaceImage *sima, ARegion *region)
{
  Image *ima = ED_space_image(sima);

  int width, height;
  ED_space_image_get_size(sima, &width, &height);

  float w = width;
  float h = height;

  if (ima) {
    h *= ima->aspy / ima->aspx;
  }

  int winx = BLI_rcti_size_x(&region->winrct) + 1;
  int winy = BLI_rcti_size_y(&region->winrct) + 1;

  /* For region overlap, move center so image doesn't overlap header. */
  const rcti *visible_rect = ED_region_visible_rect(region);
  const int visible_winy = BLI_rcti_size_y(visible_rect) + 1;
  int visible_centerx = 0;
  int visible_centery = visible_rect->ymin + (visible_winy - winy) / 2;

  region->v2d.tot.xmin = 0;
  region->v2d.tot.ymin = 0;
  region->v2d.tot.xmax = w;
  region->v2d.tot.ymax = h;

  region->v2d.mask.xmin = region->v2d.mask.ymin = 0;
  region->v2d.mask.xmax = winx;
  region->v2d.mask.ymax = winy;

  /* which part of the image space do we see? */
  float x1 = region->winrct.xmin + visible_centerx + (winx - sima->zoom * w) / 2.0f;
  float y1 = region->winrct.ymin + visible_centery + (winy - sima->zoom * h) / 2.0f;

  x1 -= sima->zoom * sima->xof;
  y1 -= sima->zoom * sima->yof;

  /* relative display right */
  region->v2d.cur.xmin = ((region->winrct.xmin - float(x1)) / sima->zoom);
  region->v2d.cur.xmax = region->v2d.cur.xmin + (float(winx) / sima->zoom);

  /* relative display left */
  region->v2d.cur.ymin = ((region->winrct.ymin - float(y1)) / sima->zoom);
  region->v2d.cur.ymax = region->v2d.cur.ymin + (float(winy) / sima->zoom);

  /* normalize 0.0..1.0 */
  region->v2d.cur.xmin /= w;
  region->v2d.cur.xmax /= w;
  region->v2d.cur.ymin /= h;
  region->v2d.cur.ymax /= h;
}

/* add handlers, stuff you only do once or on area/region changes */
static void image_main_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  /* NOTE: don't use `UI_view2d_region_reinit(&region->v2d, ...)`
   * since the space clip manages own v2d in #image_main_region_set_view2d */

  /* mask polls mode */
  keymap = WM_keymap_ensure(wm->defaultconf, "Mask Editing", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);

  /* image paint polls for mode */
  keymap = WM_keymap_ensure(wm->defaultconf, "Curve", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);

  keymap = WM_keymap_ensure(wm->defaultconf, "Paint Curve", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->handlers, keymap);

  keymap = WM_keymap_ensure(wm->defaultconf, "Image Paint", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);

  keymap = WM_keymap_ensure(wm->defaultconf, "UV Editor", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->handlers, keymap);

  /* own keymaps */
  keymap = WM_keymap_ensure(wm->defaultconf, "Image Generic", SPACE_IMAGE, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->handlers, keymap);
  keymap = WM_keymap_ensure(wm->defaultconf, "Image", SPACE_IMAGE, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);
}

static void image_main_region_draw(const bContext *C, ARegion *region)
{
  /* draw entirely, view changes should be handled here */
  SpaceImage *sima = CTX_wm_space_image(C);
  Object *obedit = CTX_data_edit_object(C);
  Depsgraph *depsgraph = CTX_data_expect_evaluated_depsgraph(C);
  Mask *mask = nullptr;
  Scene *scene = CTX_data_scene(C);
  View2D *v2d = &region->v2d;
  Image *image = ED_space_image(sima);
  const bool show_viewer = (image && image->source == IMA_SRC_VIEWER);

  /* XXX not supported yet, disabling for now */
  scene->r.scemode &= ~R_COMP_CROP;

  image_user_refresh_scene(C, sima);

  /* we set view2d from own zoom and offset each time */
  image_main_region_set_view2d(sima, region);

  /* check for mask (delay draw) */
  if (!ED_space_image_show_uvedit(sima, obedit) && sima->mode == SI_MODE_MASK) {
    mask = ED_space_image_get_mask(sima);
  }

  if (show_viewer) {
    BLI_thread_lock(LOCK_DRAW_IMAGE);
  }
  DRW_draw_view(C);
  if (show_viewer) {
    BLI_thread_unlock(LOCK_DRAW_IMAGE);
  }

  draw_image_main_helpers(C, region);

  /* Draw Meta data of the image isn't added to the DrawManager as it is
   * used in other areas as well. */
  if (sima->overlay.flag & SI_OVERLAY_SHOW_OVERLAYS && sima->flag & SI_DRAW_METADATA) {
    void *lock;
    /* `ED_space_image_get_zoom` temporarily locks the image, so this needs to be done before
     * the image is locked when calling `ED_space_image_acquire_buffer`. */
    float zoomx, zoomy;
    ED_space_image_get_zoom(sima, region, &zoomx, &zoomy);
    ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &lock, 0);
    if (ibuf) {
      int x, y;
      rctf frame;
      BLI_rctf_init(&frame, 0.0f, ibuf->x, 0.0f, ibuf->y);
      UI_view2d_view_to_region(&region->v2d, 0.0f, 0.0f, &x, &y);
      ED_region_image_metadata_draw(x, y, ibuf, &frame, zoomx, zoomy);
    }
    ED_space_image_release_buffer(sima, ibuf, lock);
  }

  /* sample line */
  UI_view2d_view_ortho(v2d);
  draw_image_sample_line(sima);
  UI_view2d_view_restore(C);

  if (mask) {
    int width, height;
    float aspx, aspy;

    if (show_viewer) {
      /* ED_space_image_get* will acquire image buffer which requires
       * lock here by the same reason why lock is needed in draw_image_main
       */
      BLI_thread_lock(LOCK_DRAW_IMAGE);
    }

    ED_space_image_get_size(sima, &width, &height);
    ED_space_image_get_aspect(sima, &aspx, &aspy);

    if (show_viewer) {
      BLI_thread_unlock(LOCK_DRAW_IMAGE);
    }

    ED_mask_draw_region(depsgraph,
                        mask,
                        region, /* Mask overlay is drawn by image/overlay engine. */
                        sima->mask_info.draw_flag & ~MASK_DRAWFLAG_OVERLAY,
                        sima->mask_info.draw_type,
                        eMaskOverlayMode(sima->mask_info.overlay_mode),
                        sima->mask_info.blend_factor,
                        width,
                        height,
                        aspx,
                        aspy,
                        true,
                        false,
                        nullptr,
                        C);
  }
  if ((sima->gizmo_flag & SI_GIZMO_HIDE) == 0) {
    WM_gizmomap_draw(region->gizmo_map, C, WM_GIZMOMAP_DRAWSTEP_2D);
  }
  draw_image_cache(C, region);
}

static void image_main_region_listener(const wmRegionListenerParams *params)
{
  ScrArea *area = params->area;
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_GEOM:
      if (ELEM(wmn->data, ND_DATA, ND_SELECT)) {
        WM_gizmomap_tag_refresh(region->gizmo_map);
      }
      break;
    case NC_GPENCIL:
      if (ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      else if (wmn->data & ND_GPENCIL_EDITMODE) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_IMAGE:
      if (wmn->action == NA_PAINTING) {
        ED_region_tag_redraw(region);
      }
      WM_gizmomap_tag_refresh(region->gizmo_map);
      break;
    case NC_MATERIAL:
      if (wmn->data == ND_SHADING_LINKS) {
        SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);

        if (sima->iuser.scene && (sima->iuser.scene->toolsettings->uv_flag & UV_SHOW_SAME_IMAGE)) {
          ED_region_tag_redraw(region);
        }
      }
      break;
    case NC_SCREEN:
      if (ELEM(wmn->data, ND_LAYER)) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

/* *********************** buttons region ************************ */

/* add handlers, stuff you only do once or on area/region changes */
static void image_buttons_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  region->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  ED_region_panels_init(wm, region);

  keymap = WM_keymap_ensure(wm->defaultconf, "Image Generic", SPACE_IMAGE, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->handlers, keymap);
}

static void image_buttons_region_layout(const bContext *C, ARegion *region)
{
  const enum eContextObjectMode mode = CTX_data_mode_enum(C);
  const char *contexts_base[3] = {nullptr};

  const char **contexts = contexts_base;

  SpaceImage *sima = CTX_wm_space_image(C);
  switch (sima->mode) {
    case SI_MODE_VIEW:
      break;
    case SI_MODE_PAINT:
      ARRAY_SET_ITEMS(contexts, ".paint_common_2d", ".imagepaint_2d");
      break;
    case SI_MODE_MASK:
      break;
    case SI_MODE_UV:
      if (mode == CTX_MODE_EDIT_MESH) {
        ARRAY_SET_ITEMS(contexts, ".uv_sculpt");
      }
      break;
  }

  ED_region_panels_layout_ex(C, region, &region->type->paneltypes, contexts_base, nullptr);
}

static void image_buttons_region_draw(const bContext *C, ARegion *region)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  Scene *scene = CTX_data_scene(C);
  void *lock;
  /* TODO(lukas): Support tiles in scopes? */
  ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &lock, 0);
  /* XXX performance regression if name of scopes category changes! */
  PanelCategoryStack *category = UI_panel_category_active_find(region, "Scopes");

  /* only update scopes if scope category is active */
  if (category) {
    if (ibuf) {
      if (!sima->scopes.ok) {
        BKE_histogram_update_sample_line(
            &sima->sample_line_hist, ibuf, &scene->view_settings, &scene->display_settings);
      }
      if (sima->image->flag & IMA_VIEW_AS_RENDER) {
        ED_space_image_scopes_update(C, sima, ibuf, true);
      }
      else {
        ED_space_image_scopes_update(C, sima, ibuf, false);
      }
    }
  }
  ED_space_image_release_buffer(sima, ibuf, lock);

  /* Layout handles details. */
  ED_region_panels_draw(C, region);
}

static void image_buttons_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_TEXTURE:
    case NC_MATERIAL:
      /* sending by texture render job and needed to properly update displaying
       * brush texture icon */
      ED_region_tag_redraw(region);
      break;
    case NC_SCENE:
      switch (wmn->data) {
        case ND_MODE:
        case ND_RENDER_RESULT:
        case ND_COMPO_RESULT:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_IMAGE:
      if (wmn->action != NA_PAINTING) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_NODE:
      ED_region_tag_redraw(region);
      break;
    case NC_GPENCIL:
      if (ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_BRUSH:
      if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

/* *********************** scopes region ************************ */

/* add handlers, stuff you only do once or on area/region changes */
static void image_tools_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  region->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  ED_region_panels_init(wm, region);

  keymap = WM_keymap_ensure(wm->defaultconf, "Image Generic", SPACE_IMAGE, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->handlers, keymap);
}

static void image_tools_region_draw(const bContext *C, ARegion *region)
{
  ED_region_panels(C, region);
}

static void image_tools_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_GPENCIL:
      if (wmn->data == ND_DATA || ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_BRUSH:
      /* NA_SELECTED is used on brush changes */
      if (ELEM(wmn->action, NA_EDITED, NA_SELECTED)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      switch (wmn->data) {
        case ND_MODE:
        case ND_RENDER_RESULT:
        case ND_COMPO_RESULT:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_IMAGE:
      if (wmn->action != NA_PAINTING) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_NODE:
      ED_region_tag_redraw(region);
      break;
  }
}

/************************* Tool header region **************************/

static void image_tools_header_region_draw(const bContext *C, ARegion *region)
{
  ScrArea *area = CTX_wm_area(C);
  SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);

  image_user_refresh_scene(C, sima);

  ED_region_header_with_button_sections(
      C,
      region,
      (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) == RGN_ALIGN_TOP) ?
          uiButtonSectionsAlign::Top :
          uiButtonSectionsAlign::Bottom);
}

/************************* header region **************************/

/* add handlers, stuff you only do once or on area/region changes */
static void image_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ED_region_header_init(region);
}

static void image_header_region_draw(const bContext *C, ARegion *region)
{
  ScrArea *area = CTX_wm_area(C);
  SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);

  image_user_refresh_scene(C, sima);

  ED_region_header(C, region);
}

static void image_header_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_SCENE:
      switch (wmn->data) {
        case ND_MODE:
        case ND_TOOLSETTINGS:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_GEOM:
      switch (wmn->data) {
        case ND_DATA:
        case ND_SELECT:
          ED_region_tag_redraw(region);
          break;
      }
      break;
    case NC_BRUSH:
      if (wmn->action == NA_EDITED) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void image_id_remap(ScrArea * /*area*/, SpaceLink *slink, const IDRemapper *mappings)
{
  SpaceImage *simg = (SpaceImage *)slink;

  if (!BKE_id_remapper_has_mapping_for(mappings,
                                       FILTER_ID_IM | FILTER_ID_GD_LEGACY | FILTER_ID_MSK)) {
    return;
  }

  BKE_id_remapper_apply(mappings, (ID **)&simg->image, ID_REMAP_APPLY_ENSURE_REAL);
  BKE_id_remapper_apply(mappings, (ID **)&simg->gpd, ID_REMAP_APPLY_UPDATE_REFCOUNT);
  BKE_id_remapper_apply(mappings, (ID **)&simg->mask_info.mask, ID_REMAP_APPLY_ENSURE_REAL);
}

static void image_foreach_id(SpaceLink *space_link, LibraryForeachIDData *data)
{
  SpaceImage *simg = reinterpret_cast<SpaceImage *>(space_link);
  const int data_flags = BKE_lib_query_foreachid_process_flags_get(data);
  const bool is_readonly = (data_flags & IDWALK_READONLY) != 0;

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->image, IDWALK_CB_USER_ONE);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->iuser.scene, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->mask_info.mask, IDWALK_CB_USER_ONE);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, simg->gpd, IDWALK_CB_USER);
  if (!is_readonly) {
    simg->scopes.ok = 0;
  }
}

/**
 * \note Used for splitting out a subset of modes is more involved,
 * The previous non-uv-edit mode is stored so switching back to the
 * image doesn't always reset the sub-mode.
 */
static int image_space_subtype_get(ScrArea *area)
{
  SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);
  return sima->mode == SI_MODE_UV ? SI_MODE_UV : SI_MODE_VIEW;
}

static void image_space_subtype_set(ScrArea *area, int value)
{
  SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);
  if (value == SI_MODE_UV) {
    if (sima->mode != SI_MODE_UV) {
      sima->mode_prev = sima->mode;
    }
    sima->mode = value;
  }
  else {
    sima->mode = sima->mode_prev;
  }
}

static void image_space_subtype_item_extend(bContext * /*C*/,
                                            EnumPropertyItem **item,
                                            int *totitem)
{
  RNA_enum_items_add(item, totitem, rna_enum_space_image_mode_items);
}

static void image_space_blend_read_data(BlendDataReader * /*reader*/, SpaceLink *sl)
{
  SpaceImage *sima = (SpaceImage *)sl;

  sima->iuser.scene = nullptr;
  sima->scopes.waveform_1 = nullptr;
  sima->scopes.waveform_2 = nullptr;
  sima->scopes.waveform_3 = nullptr;
  sima->scopes.vecscope = nullptr;
  sima->scopes.ok = 0;

/* WARNING: gpencil data is no longer stored directly in sima after 2.5
 * so sacrifice a few old files for now to avoid crashes with new files!
 * committed: r28002 */
#if 0
  sima->gpd = newdataadr(fd, sima->gpd);
  if (sima->gpd) {
    BKE_gpencil_blend_read_data(fd, sima->gpd);
  }
#endif
}

static void image_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  BLO_write_struct(writer, SpaceImage, sl);
}

/**************************** spacetype *****************************/

void ED_spacetype_image()
{
  SpaceType *st = static_cast<SpaceType *>(MEM_callocN(sizeof(SpaceType), "spacetype image"));
  ARegionType *art;

  st->spaceid = SPACE_IMAGE;
  STRNCPY(st->name, "Image");

  st->create = image_create;
  st->free = image_free;
  st->init = image_init;
  st->duplicate = image_duplicate;
  st->operatortypes = image_operatortypes;
  st->keymap = image_keymap;
  st->dropboxes = image_dropboxes;
  st->refresh = image_refresh;
  st->listener = image_listener;
  st->context = image_context;
  st->gizmos = image_widgets;
  st->id_remap = image_id_remap;
  st->foreach_id = image_foreach_id;
  st->space_subtype_item_extend = image_space_subtype_item_extend;
  st->space_subtype_get = image_space_subtype_get;
  st->space_subtype_set = image_space_subtype_set;
  st->blend_read_data = image_space_blend_read_data;
  st->blend_read_after_liblink = nullptr;
  st->blend_write = image_space_blend_write;

  /* regions: main window */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype image region"));
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_TOOL | ED_KEYMAP_FRAMES | ED_KEYMAP_GPENCIL;
  art->init = image_main_region_init;
  art->draw = image_main_region_draw;
  art->listener = image_main_region_listener;
  BLI_addhead(&st->regiontypes, art);

  /* regions: list-view/buttons/scopes */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype image region"));
  art->regionid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PANEL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = image_buttons_region_listener;
  art->message_subscribe = ED_area_do_mgs_subscribe_for_tool_ui;
  art->init = image_buttons_region_init;
  art->layout = image_buttons_region_layout;
  art->draw = image_btns_region_draw;
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

  /* rgs: header */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype image rgn"));
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

  BKE_spacetype_register(st);
}
