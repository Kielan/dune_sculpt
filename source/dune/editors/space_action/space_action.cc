#include <cstdio>
#include <cstring>

#include "types_act.h"
#include "types_anim.h"
#include "types_collection.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_nla.h"
#include "dune_screen.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "win_api.hh"
#include "win_msg.hh"
#include "win_types.hh"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "ed_anim_api.hh"
#include "ed_markers.hh"
#include "ed_screen.hh"
#include "ed_space_api.hh"
#include "ed_time_scrub_ui.hh"

#include "loader_read_write.hh"

#include "gpu_matrix.h"

#include "act_intern.hh" /* own include */

/* Default Cbs for Action Space */
static SpaceLink *act_create(const ScrArea *area, const Scene *scene)
{
  SpaceAct *sact;
  ARgn *rgn;

  sact = mem_cnew<SpaceAct>("initact");
  sact->spacetype = SPACE_ACT;

  sact->mode = SACTCONT_DOPESHEET;
  sact->mode_prev = SACTCONT_DOPESHEET;
  sact->flag = SACT_SHOW_INTERPOLATION | SACT_SHOW_MARKERS;

  sact->ads.filterflag |= ADS_FILTER_SUMMARY;

  sact->cache_display = TIME_CACHE_DISPLAY | TIME_CACHE_SOFTBODY | TIME_CACHE_PARTICLES |
                           TIME_CACHE_CLOTH | TIME_CACHE_SMOKE | TIME_CACHE_DYNAMICPAINT |
                           TIME_CACHE_RIGIDBODY | TIME_CACHE_SIM_NODES;

  /* header */
  rgn = mem_cnew<ARgn>("header for action");

  lib_addtail(&sact->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_HEADER;
  rgn->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* channel list rgn */
  rgn = mem_cnew<ARgn>("channel rgn for action");
  lib_addtail(&sact->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_CHANNELS;
  rgn->alignment = RGN_ALIGN_LEFT;

  /* only need to set scroll settings, as this will use 'listview' v2d configuration */
  rgn->v2d.scroll = V2D_SCROLL_BOTTOM;
  rgn->v2d.flag = V2D_VIEWSYNC_AREA_VERTICAL;

  /* ui btns */
  rgn = mem_cnew<ARgn>("btns rgn for action");

  lib_addtail(&sact->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_UI;
  rgn->alignment = RGN_ALIGN_RIGHT;

  /* main rgn */
  rgn = mem_cnew<ARgn>("main rgn for action");

  lib_addtail(&sact->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_WIN;

  rgn->v2d.tot.xmin = float(scene->r.sfra - 10);
  rgn->v2d.tot.ymin = float(-area->winy) / 3.0f;
  rgn->v2d.tot.xmax = float(scene->r.efra + 10);
  rgn->v2d.tot.ymax = 0.0f;

  rgn->v2d.cur = rgn->v2d.tot;

  rgn->v2d.min[0] = 0.0f;
  rgn->v2d.min[1] = 0.0f;

  rgn->v2d.max[0] = MAXFRAMEF;
  rgn->v2d.max[1] = FLT_MAX;

  rgn->v2d.minzoom = 0.01f;
  rgn->v2d.maxzoom = 50;
  rgn->v2d.scroll = (V2D_SCROLL_BOTTOM | V2D_SCROLL_HORIZONTAL_HANDLES);
  rgn->v2d.scroll |= V2D_SCROLL_RIGHT;
  rgn->v2d.keepzoom = V2D_LOCKZOOM_Y;
  rgn->v2d.keepofs = V2D_KEEPOFS_Y;
  rgn->v2d.align = V2D_ALIGN_NO_POS_Y;
  rgn->v2d.flag = V2D_VIEWSYNC_AREA_VERTICAL;

  return (SpaceLink *)sact;
}

/* Doesn't free the space-link itself. */
static void act_free(SpaceLink * /*sl*/)
{
  //  SpaceAct *sact = (SpaceAct *) sl;
}

/* spacetype; init cb */
static void act_init(WinMngr * /*wm*/, ScrArea *area)
{
  SpaceAct *sact = static_cast<SpaceAct *>(area->spacedata.first);
  sact->runtime.flag |= SACT_RUNTIME_FLAG_NEED_CHAN_SYNC;
}

static SpaceLink *act_dup(SpaceLink *sl)
{
  SpaceAct *sact = static_cast<SpaceAct *>(mem_dupalloc(sl));

  memset(&sact->runtime, 0x0, sizeof(sact->runtime));

  /* clear or remove stuff from old */
  return (SpaceLink *)sact;
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void act_main_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;
  
  ui_view2d_rgn_reinit(&rgn->v2d, V2D_COMMONVIEW_CUSTOM, rgn->winx, rgn->winy);

  /* own keymap */
  keymap = win_keymap_ensure(wm->defaultconf, "Dopesheet", SPACE_ACT, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);
  keymap = win_keymap_ensure(wm->defaultconf, "Dopesheet Generic", SPACE_ACT, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void act_main_rgn_draw(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  SpaceAct *sact = cxt_win_space_act(C);
  Scene *scene = cxt_data_scene(C);
  AnimCxt ac;
  View2D *v2d = &rgn->v2d;
  short marker_flag = 0;

  ui_view2d_view_ortho(v2d);

  /* clear and setup matrix */
  ui_ThemeClearColor(TH_BACK);

  ui_view2d_view_ortho(v2d);

  /* time grid */
  ui_view2d_drw_lines_x_discrete_frames_or_seconds(
      v2d, scene, sact->flag & SACT_DRWTIME, true);

  ed_rgn_drw_cb_drw(C, rgn, RGN_DRW_PRE_VIEW);

  /* start and end frame */
  ankm_drw_framerange(scene, v2d);

  /* Draw the manually set intended playback frame range highlight in the Act editor. */
  if (elem(sact->mode, SACTCONT_ACT, SACTCONT_SHAPEKEY) && sact->act) {
    AnimData *adt = ed_actedit_animdata_from_cxt(C, nullptr);

    anim_drw_act_framerange(adt, sact->act, v2d, -FLT_MAX, FLT_MAX);
  }

  /* data */
  if (anim_animdata_get_ct(C, &ac)) {
    drw_channel_strips(&ac, sact, rgn);
  }

  /* markers */
  ui_view2d_view_orthoSpecial(rgn, v2d, true);

  marker_flag = ((ac.markers && (ac.markers != &ac.scene->markers)) ? DRAW_MARKERS_LOCAL : 0) |
                DRW_MARKERS_MARGIN;

  if (sact->flag & SACT_SHOW_MARKERS) {
    ed_markers_drw(C, marker_flag);
  }

  /* preview range */
  ui_view2d_view_ortho(v2d);
  anim_drw_previewrange(C, v2d, 0);

  /* cb */
  ui_view2d_view_ortho(v2d);
  ed_rgn_drw_cb_drw(C, rgn, RGN_DRW_POST_VIEW);

  /* reset view matrix */
  ui_view2d_view_restore(C);

  /* gizmos */
  win_gizmomap_drw(rgn->gizmo_map, C, WIN_GIZMOMAP_DRWSTEP_2D);

  /* scrubbing rgn */
  ed_time_scrub_drw(rhn, scene, sact->flag & SACT_DRWTIME, true);
}

static void act_main_rgn_drw_overlay(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  const SpaceAct *sact = cxt_win_space_act(C);
  const Scene *scene = cxt_data_scene(C);
  const Ob *obact = cxt_data_active_ob(C);
  View2D *v2d = &rgn->v2d;

  /* caches */
  if (sact->mode == SACTCONT_TIMELINE) {
    gpu_matrix_push_projection();
    ui_view2d_view_orthoSpecial(rgn, v2d, true);
    timeline_drw_cache(sact, obact, scene);
    gpu_matrix_pop_projection();
  }

  /* scrubbing rgn */
  es_time_scrub_drw_current_frame(rgn, scene, saction->flag & SACTION_DRAWTIME);

  /* scrollers */
  ui_view2d_scrollers_drw(v2d, nullptr);
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void act_channel_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  /* ensure the 2d view sync works - main region has bottom scroller */
  rgn->v2d.scroll = V2D_SCROLL_BOTTOM;

  ui_view2d_rgn_reinit(&rgn->v2d, V2D_COMMONVIEW_LIST, rgn->winx, rgn->winy);

  /* own keymap */
  keymap = win_keymap_ensure(wm->defaultconf, "Anim Channels", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);

  keymap = win_keymap_ensure(wm->defaultconf, "Dopesheet Generic", SPACE_ACT, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void act_channel_rgn_drw(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  AnimCxt ac;
  View2D *v2d = &rgn->v2d;

  /* clear and setup matrix */
  ui_ThemeClearColor(TH_BACK);

  ui_view2d_view_ortho(v2d);

  /* data */
  if (anim_animdata_get_cxt(C, &ac)) {
     drw_channel_names((Cxt *)C, &ac, rgn);
  }

  /* channel filter next to scrubbing area */
  ed_time_scrub_channel_search_drw(C, rgn, ac.ads);

  /* reset view matrix */
  ui_view2d_view_restore(C);

  /* no scrollers here */
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void act_header_rgn_init(WinMngr * /*wm*/, ARgn *rgn)
{
  ed_rgn_header_init(rgn);
}

static void act_header_rgn_drw(const Cxt *C, ARgn *rgn)
{
  /* The anim cxt is not actually used, but this makes sure the action being displayed is up to
   * date. */
  AnimCxt ac;
  anim_animdata_get_cxt(C, &ac);

  ed_rgn_header(C, rgn);
}

static void act_channel_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_ANIM:
      ed_rgn_tag_redrw(rgn);
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_OB_ACTIVE:
        case ND_FRAME:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_OB:
      switch (winn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SEL:
        case ND_KEYS:
          ed_rgn_tag_redrw(rgn);
          break;
        case ND_MOD:
          if (winn->action == NA_RENAME) {
            ed_rgn_tag_redrw(rgn);
          }
          break;
      }
      break;
    case NC_PEN:
      if (elem(winn->act, NA_RENAME, NA_SEL)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_ID:
      if (winn->action == NA_RENAME) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    default:
      if (winn->data == ND_KEYS) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

static void sact_channel_rgn_msg_sub(const WinRgnMsgSubParams *params)
{
  WinMsgBus *mbus = params->msg_bus;
  ARgn *rgn = params->rgm;

  WinMsgSubVal msg_sub_val_rgn_tag_redrw{};
  msg_sub_val_rgn_tag_redrw.owner = rgn;
  msg_sub_val_rgn_tag_redrw.user_data = rgn;
  msg_sub_val_rgn_tag_redrw.notify = ed_rgn_do_msg_notify_tag_redrw;

  /* All dopesheet filter settings, etc. affect the drawing of this editor,
   * also same applies for all animation-related datatypes that may appear here,
   * so just whitelist the entire structs for updates  */
  {
    WinMsgParamsApi msg_key_params = {{nullptr}};
    ApiStruct *type_array[] = {
        &ApiDopeSheet, /* dopesheet filters */

        &ApiActionGroup, /* channel groups */

        &ApiFCurve, /* F-Curve */
        &ApiKeyframe,
        &ApiFCurveSample,

        &ApiPen, /* Pen */
        &ApiPenLayer,
        &ApiPenFrame,
    };

    for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
      msg_key_params.ptr.type = type_array[i];
      win_msg_sub_api_params(
          mbus, &msg_key_params, &msg_sub_val_rgn_tag_redrw, __func__);
    }
  }
}

static void act_main_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  const Notifier *winn = params->notifier;

  /* context changes */
  switch (winn->category) {
    case NC_ANIM:
      ed_rgn_tag_redrw(rgn);
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_RNDR_OPTIONS:
        case ND_OB_ACTIVE:
        case ND_FRAME:
        case ND_FRAME_RANGE:
        case ND_MARKERS:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_OB:
      switch (winn->data) {
        case ND_TRANSFORM:
          /* moving ob shouldn't need to redrw action */
          break;
        case ND_BONE_ACTIVE:
        case ND_BONE_SEL:
        case ND_BONE_COLLECTION:
        case ND_KEYS:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_NODE:
      switch (winn->action) {
        case NA_EDITED:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_ID:
      if (winn->action == NA_RENAME) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_SCREEN:
      if (ELEM(winn->data, ND_LAYER)) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    default:
      if (winn->data == ND_KEYS) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

static void sact_main_rgn_msg_sub(const WinRgnMsgSubParams *params)
{
  WinMsgBus *mbus = params->msg_bus;
  Scene *scene = params->scene;
  ARgn *rgn = params->rgn;

  WinMsgSubVal msg_sub_val_rgn_tag_redrw{};
  msg_sub_val_rgn_tag_redrw.owner = rgn;
  msg_sub_val_rgn_tag_redrw.user_data = rgn;
  msg_sub_val_rgn_tag_redrw.notify = ed_rgn_do_msg_notify_tag_redrw;

  /* Timeline depends on scene props. */
  {
    bool use_preview = (scene->r.flag & SCER_PRV_RANGE);
    const ApiProp *props[] = {
        use_preview ? &api_Scene_frame_preview_start : &api_scene_frame_start,
        use_preview ? &api_Scene_frame_preview_end : &api_scene_frame_end,
        &api_scene_use_preview_range,
        &api_scene_frame_current,
    };

    ApiPtr idptr = api_id_ptr_create(&scene->id);

    for (int i = 0; i < ARRAY_SIZE(props); i++) {
      win_msg_sub_api(mbus, &idptr, props[i], &msg_sub_val_rgn_tag_redrw, __func__);
    }
  }

  /* Now run the general "channels rgn" one - since channels and main should be in sync */
  sact_channel_rgn_msg_sub(params);
}

/* editor level listener */
static void act_listener(const WinSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  const WinNotifier *winn = params->notifier;
  SpaceAct *saction = (SpaceAction *)area->spacedata.first;

  /* cxt changes */
  switch (winn->category) {
    case NC_PEN:
      /* only handle these events for containers in which Pen frames are displayed */
      if (ELEM(saction->mode, SACTCONT_PEN, SACTCONT_DOPESHEET, SACTCONT_TIMELINE)) {
        if (winn->action == NA_EDITED) {
          ed_area_tag_redrw(area);
        }
        else if (wonn->action == NA_SELECTED) {
          saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
        }
      }
      break;
    case NC_ANIM:
      /* For NLA tweak-mode enter/exit, need complete refresh. */
      if (winn->data == ND_NLA_ACTCHANGE) {
        saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
        ed_area_tag_refresh(area);
      }
      /* Auto-color only really needs to change when channels are added/removed,
       * or previously hidden stuff appears
       * (assume for now that if just adding these works, that will be fine) */
      else if (((winn->data == ND_KEYFRAME) && ELEM(winn->action, NA_ADDED, NA_REMOVED)) ||
               ((winn->data == ND_ANIMCHAN) && (winn->action != NA_SELECTED)))
      {
        ed_area_tag_refresh(area);
      }
      /* for simple edits to the curve data though (or just plain sels),
       * a simple redre should work
       * (see #39851 for an example of how this can go wrong)
       */
      else {
        ed_area_tag_redrw(area);
      }
      break;
    case NC_SCENE:
      switch (wonn->data) {
        case ND_SEQ:
          if (winn->action == NA_SELECTED) {
            saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
            ed_area_tag_refresh(area);
          }
          break;
        case ND_OB_ACTIVE:
        case ND_OB_SEL:
          /* Sel changed, so force refresh to flush
           * (needs flag set to do syncing). */
          saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
          break;
        case ND_RENDER_RESULT:
          ed_area_tag_redrw(area);
          break;
        case ND_FRAME_RANGE:
          LIST_FOREACH (ARgn *, rgn, &area->rgnbase) {
            if (rgn->rgntype == RGN_TYPE_WIN) {
              Scene *scene = static_cast<Scene *>(winn->ref);
              rgn->v2d.tot.xmin = float(scene->r.sfra - 4);
              rgn->v2d.tot.xmax = float(scene->r.efra + 4);
              break;
            }
          }
          break;
        default:
          if (saction->mode != SACTCONT_TIMELINE) {
            /* Just redrawing the view will do. */
            ed_area_tag_redrw(area);
          }
          break;
      }
      break;
    case NC_OB:
      switch (winn->data) {
        case ND_BONE_SEL: /* Selection changed, so force refresh to flush
                              * (needs flag set to do syncing). */
        case ND_BONE_ACTIVE:
          saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
          break;
        case ND_TRANSFORM:
          /* moving object shouldn't need to redrw action */
          break;
        case ND_POINTCACHE:
        case ND_MOD:
        case ND_PARTICLE:
          /* only needed in timeline mode */
          if (saction->mode == SACTCONT_TIMELINE) {
            ed_area_tag_refresh(area);
            ed_area_tag_redrw(area);
          }
          break;
        default: /* just redrawing the view will do */
          ed_area_tag_redrw(area);
          break;
      }
      break;
    case NC_MASK:
      if (saction->mode == SACTCONT_MASK) {
        switch (winn->data) {
          case ND_DATA:
            ed_area_tag_refresh(area);
            ed_area_tag_redrw(area);
            break;
          default: /* just redrawing the view will do */
            ed_area_tag_redrw(area);
            break;
        }
      }
      break;
    case NC_NODE:
      if (winn->action == NA_SELECTED) {
        /* sel changed, so force refresh to flush (needs flag set to do syncing) */
        saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
        ed_area_tag_refresh(area);
      }
      break;
    case NC_SPACE:
      switch (winn->data) {
        case ND_SPACE_DOPESHEET:
          ed_area_tag_redrw(area);
          break;
        case ND_SPACE_TIME:
          ed_area_tag_redre(area);
          break;
        case ND_SPACE_CHANGED:
          saction->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
          break;
      }
      break;
    case NC_WIN:
      if (saction->runtime.flag & SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC) {
        /* force redrw/refresh after undo/redo, see: #28962. */
        ed_area_tag_refresh(area);
      }
      break;
    case NC_WM:
      switch (winn->data) {
        case ND_FILEREAD:
          ed_area_tag_refresh(area);
          break;
      }
      break;
  }
}

static void action_header_rgn_listener(const WinRgnListenerParams *params)
{
  ScrArea *area = params->area;
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;
  SpaceAction *saction = (SpaceAction *)area->spacedata.first;

  /* cxt changes */
  switch (winn->category) {
    case NC_SCREEN:
      if (saction->mode == SACTCONT_TIMELINE) {
        if (winn->data == ND_ANIMPLAY) {
          ed_rhn_tag_redrw(rgn);
        }
      }
      break;
    case NC_SCENE:
      if (saction->mode == SACTCONT_TIMELINE) {
        switch (wmn->data) {
          case ND_RENDER_RESULT:
          case ND_OB_SEL:
          case ND_FRAME:
          case ND_FRAME_RANGE:
          case ND_KEYINGSET:
          case ND_RENDER_OPTIONS:
            ed_rgn_tag_redrw(rhn);
            break;
        }
      }
      else {
        switch (winn->data) {
          case ND_OB_ACTIVE:
            ed_rgn_tag_redrw(rgn);
            break;
        }
      }
      break;
    case NC_ID:
      if (winn->action == NA_RENAME) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
    case NC_ANIM:
      switch (winn->data) {
        case ND_ANIMCHAN: /* set of visible animchannels changed */
          /* For now, this should usually just mean that the filters changed
           * It may be better if we had a dedicated flag for that though */
          ed_rgn_tag_redrw(rgn);
          break;

        case ND_KEYFRAME: /* new keyframed added -> active action may have changed */
          // saction->flag |= SACTION_TEMP_NEEDCHANSYNC;
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
  }
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void action_btns_area_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "Dopesheet Generic", SPACE_ACTION, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void action_btns_area_drw(const Cxt *C, ARgn *rgn)
{
  ed_rgn_pnls(C, rgn);
}

static void action_rgn_listener(const WinRgnListenerParams *params)
{
  ARgn *rgn = params->rgn;
  const WinNotifier *winn = params->notifier;

  /* cxt changes */
  switch (winn->category) {
    case NC_ANIM:
      ed_rgn_tag_redrw(rgn);
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_OB_ACTIVE:
        case ND_FRAME:
        case ND_MARKERS:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    case NC_OB:
      switch (winn->data) {
        case ND_BONE_ACTIVE:
        case ND_BONE_SEL:
        case ND_KEYS:
          ed_rgn_tag_redrw(rgn);
          break;
      }
      break;
    default:
      if (winn->data == ND_KEYS) {
        ed_rgn_tag_redrw(rgn);
      }
      break;
  }
}

static void action_refresh(const Cxt *C, ScrArea *area)
{
  SpaceAction *saction = (SpaceAction *)area->spacedata.first;

  /* Update the state of the animchannels in response to changes from the data they represent
   * The tmp flag is used to indicate when this needs to be done,
   * and will be cleared once handled. */
  if (saction->runtime.flag & SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC) {
    /* Perform syncing of channel state incl. sel
     * Active action setting also occurs here
     * (as part of anim channel filtering in `anim_filter.cc`). */
    anim_sync_animchannels_to_data(C);
    saction->runtime.flag &= ~SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;

    /* Tag everything for redre
     * - Rgns (such as header) need to be manually tagged for redrw too
     *   or else they don't update #28962. */
    ed_area_tag_redrw(area);
    LIST_FOREACH (ARgn *, rgn, &area->rgnbase);
      ed_rgn_tag_redrw(rgn);
    }
  }

  /* rgn updates? */
  /* re-sizing y-extents of tot should go here? */
}

static void action_id_remap(ScrArea * /*area*/, SpaceLink *slink, const IdRemapper *mappings)
{
  SpaceAction *sact = (SpaceAction *)slink;

  dune_id_remapper_apply(mappings, (Id **)&sact->action, ID_REMAP_APPLY_DEFAULT);
  dune_id_remapper_apply(mappings, (Id **)&sact->ads.filter_grp, ID_REMAP_APPLY_DEFAULT);
  dune_id_remapper_apply(mappings, &sact->ads.source, ID_REMAP_APPLY_DEFAULT);
}

static void action_foreach_id(SpaceLink *space_link, LibForeachIdData *data)
{
  SpaceAction *sact = reinterpret_cast<SpaceAction *>(space_link);
  const int data_flags = dune_lib_query_foreachid_process_flags_get(data);
  const bool is_readonly = (data_flags & IDWALK_READONLY) != 0;

  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sact->action, IDWALK_CB_NOP);

  /* Could be deduplicated with the DopeSheet handling of SpaceNla and SpaceGraph. */
  DUNE_LIB_FOREACHID_PROCESS_ID(data, sact->ads.source, IDWALK_CB_NOP);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sact->ads.filter_grp, IDWALK_CB_NOP);

  if (!is_readonly) {
    /* Force recalc of list of channels, potentially updating the active action while we're
     * at it (as it can only be updated that way) #28962. */
    sact->runtime.flag |= SACTION_RUNTIME_FLAG_NEED_CHAN_SYNC;
  }
}

/* Used for splitting out a subset of modes is more involved,
 * The previous non-timeline mode is stored so switching back to the
 * dope-sheet doesn't always reset the sub-mode */
static int action_space_subtype_get(ScrArea *area)
{
  SpaceAction *sact = static_cast<SpaceAction *>(area->spacedata.first);
  return sact->mode == SACTCONT_TIMELINE ? SACTCONT_TIMELINE : SACTCONT_DOPESHEET;
}

static void action_space_subtype_set(ScrArea *area, int value)
{
  SpaceAction *sact = static_cast<SpaceAction *>(area->spacedata.first);
  if (value == SACTCONT_TIMELINE) {
    if (sact->mode != SACTCONT_TIMELINE) {
      sact->mode_prev = sact->mode;
    }
    sact->mode = value;
  }
  else {
    sact->mode = sact->mode_prev;
  }
}

static void action_space_subtype_item_extend(Cxt * /*C*/,
                                             EnumPropItem **item,
                                             int *totitem)
{
  api_enum_items_add(item, totitem, api_enum_space_action_mode_items);
}

static void action_space_dune_read_data(DuneDataReader * /*reader*/, SpaceLink *sl)
{
  SpaceAction *saction = (SpaceAction *)sl;
  memset(&saction->runtime, 0x0, sizeof(saction->runtime));
}

static void action_space_dune_write(DuneWriter *writer, SpaceLink *sl)
{
  loader_write_struct(writer, SpaceAction, sl);
}

static void action_main_rgn_view2d_changed(const Cxt * /*C*/, ARgn *rgn)
{
  View2D *v2d = &rgn->v2d;
  ui_view2d_curRect_clamp_y(v2d);
}

void ed_spacetype_action()
{
  SpaceType *st = mem_cnew<SpaceType>("spacetype action");
  ARgnType *art;

  st->spaceid = SPACE_ACTION;
  STRNCPY(st->name, "Action");

  st->create = action_create;
  st->free = action_free;
  st->init = action_init;
  st->dup = action_dup;
  st->optypes = action_optypes;
  st->keymap = action_keymap;
  st->listener = action_listener;
  st->refresh = action_refresh;
  st->id_remap = action_id_remap;
  st->foreach_id = action_foreach_id;
  st->space_subtype_item_extend = action_space_subtype_item_extend;
  st->space_subtype_get = action_space_subtype_get;
  st->space_subtype_set = action_space_subtype_set;
  st->dune_read_data = action_space_dune_read_data;
  st->dune_read_after_liblink = nullptr;
  st->dune_write = action_space_dune_write;

  /* rgns: main win */
  art = mem_cnew<ARgnType>("spacetype action rgn");
  art->rgnid = RGN_TYPE_WIN;
  art->init = action_main_rgn_init;
  art->drw = action_main_rgn_drw;
  art->drw_overlay = action_main_rgn_drw_overlay;
  art->listener = action_main_rgn_listener;
  art->msg_sub = saction_main_rgn_msg_sub;
  art->on_view2d_changed = action_main_rgn_view2d_changed;
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_VIEW2D | ED_KEYMAP_ANIM | ED_KEYMAP_FRAMES;

  lib_addhead(&st->rgntypes, art);

  /* rgns: header */
  art = mem_cnew<ARgnType>("spacetype action rgn");
  art->rgnid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;

  art->init = action_header_rgn_init;
  art->draw = action_header_rgn_draw;
  art->listener = action_header_rgn_listener;

  lib_addhead(&st->rgntypes, art);

  /* rgns: channels */
  art = mem_cnew<ARgnType>("spacetype action rgn");
  art->rgnid = RGN_TYPE_CHANNELS;
  art->prefsizex = 200;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES;

  art->init = action_channel_rgn_init;
  art->draw = action_channel_rgn_draw;
  art->listener = action_channel_rgn_listener;
  art->msg_sub = saction_channel_rgn_msg_sub;

  lib_addhead(&st->rgntypes, art);

  /* regions: UI btns */
  art = mem_cnew<ARgnType>("spacetype action rgn");
  art->rgnid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PNL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI;
  art->listener = action_rgn_listener;
  art->init = action_btns_area_init;
  art->drw = action_btns_area_drw;

  lib_addhead(&st->rgntypes, art);

  action_btns_register(art);

  art = ed_area_type_hud(st->spaceid);
  lib_addhead(&st->rgntypes, art);

  dune_spacetype_register(st);
}
