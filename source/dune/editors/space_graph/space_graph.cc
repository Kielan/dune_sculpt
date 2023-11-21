#include <cstdio>
#include <cstring>

#include "types_anim.h"
#include "types_collection.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_color.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_screen.hh"

#include "ed_anim_api.hh"
#include "ed_markers.hh"
#include "ed_screen.hh"
#include "ed_space_api.hh"
#include "ed_time_scrub_ui.hh"

#include "gpu_framebuf.h"
#include "gpu_immediate.h"
#include "gpu_state.h"

#include "win_api.hh"
#include "win_msg.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "loader_read_write.hh"

#include "graph_intern.h" /* own include */

/* default cbs for ipo space */
static SpaceLink *graph_create(const ScrArea * /*area*/, const Scene *scene)
{
  ARgn *rgn;
  SpaceGraph *sipo;

  /* Graph Editor - general stuff */
  sipo = static_cast<SpaceGraph *>(mem_calloc(sizeof(SpaceGraph), "init graphedit"));
  sipo->spacetype = SPACE_GRAPH;

  /* allocate DopeSheet data for Graph Editor */
  sipo->ads = static_cast<DopeSheet *>(mem_calloc(sizeof(DopeSheet), "GraphEdit DopeSheet"));
  sipo->ads->source = (Id *)scene;

  /* settings for making it easier by default to just see what you're interested in tweaking */
  sipo->ads->filterflag |= ADS_FILTER_ONLYSEL;
  sipo->flag |= SIPO_SHOW_MARKERS;

  /* header */
  region = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "header for graphedit"));

  lib_addtail(&sipo->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_HEADER;
  rgn->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* channels */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "channels rgn for graphedit"));

  lib_addtail(&sipo->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_CHANNELS;
  rgn->alignment = RGN_ALIGN_LEFT;

  rgn->v2d.scroll = (V2D_SCROLL_RIGHT | V2D_SCROLL_BOTTOM);

  /* ui btns */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "btns rgn for graphedit"));

  lib_addtail(&sipo->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_UI;
  rgn->alignment = RGN_ALIGN_RIGHT;

  /* main rgn */
  rgn = static_cast<ARgn *>(mem_calloc(sizeof(ARgn), "main rgn for graphedit"));

  lib_addtail(&sipo->rgnbase, rgn);
  rgn->rgntype = RGN_TYPE_WIN;

  rgn->v2d.tot.xmin = 0.0f;
  rgn->v2d.tot.ymin = float(scene->r.sfra) - 10.0f;
  rgn->v2d.tot.xmax = float(scene->r.efra);
  rgn->v2d.tot.ymax = 10.0f;

  rgn->v2d.cur = rgn->v2d.tot;

  rgn->v2d.min[0] = FLT_MIN;
  rgn->v2d.min[1] = FLT_MIN;

  rgn->v2d.max[0] = MAXFRAMEF;
  rgn->v2d.max[1] = FLT_MAX;

  rgn->v2d.scroll = (V2D_SCROLL_BOTTOM | V2D_SCROLL_HORIZONTAL_HANDLES);
  rgn->v2d.scroll |= (V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HANDLES);

  region->v2d.keeptot = 0;

  return (SpaceLink *)sipo;
}

/* Doesn't free the space-link itself. */
static void graph_free(SpaceLink *sl)
{
  SpaceGraph *si = (SpaceGraph *)sl;

  if (si->ads) {
    lib_freelist(&si->ads->chanbase);
    mem_free(si->ads);
  }

  if (si->runtime.ghost_curves.first) {
    dune_fcurves_free(&si->runtime.ghost_curves);
  }
}

/* spacetype; init cb */
static void graph_init(WinMngr *wm, ScrArea *area)
{
  SpaceGraph *sipo = (SpaceGraph *)area->spacedata.first;

  /* init dopesheet data if non-existent (i.e. for old files) */
  if (sipo->ads == nullptr) {
    Win *win = win_find_by_area(wm, area);
    sipo->ads = static_cast<DopeSheet *>(mem_calloc(sizeof(DopeSheet), "GraphEdit DopeSheet"));
    sipo->ads->source = win ? (Id *)win_get_active_scene(win) : nullptr;
  }

  /* force immediate init of any invalid F-Curve colors */
  /* but, don't do SIPO_TMP_NEEDCHANSYNC (i.e. channel sel state sync)
   * as this is run on each rgn resize; setting this here will cause sel
   * state to be lost on area/rgn resizing. #35744. */
  ed_area_tag_refresh(area);
}

static SpaceLink *graph_dup(SpaceLink *sl)
{
  SpaceGraph *sipon = static_cast<SpaceGraph *>(mem_dupalloc(sl));

  memset(&sipon->runtime, 0x0, sizeof(sipon->runtime));

  /* clear or remove stuff from old */
  lib_duplist(&sipon->runtime.ghost_curves, &((SpaceGraph *)sl)->runtime.ghost_curves);
  sipon->ads = static_cast<DopeSheet *>(mem_dupalloc(sipon->ads));

  return (SpaceLink *)sipon;
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void graph_main_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  ui_view2d_rgn_reinit(&rgn->v2d, V2D_COMMONVIEW_CUSTOM, rgn->winx, rgn->winy);

  /* own keymap */
  keymap = win_keymap_ensure(wm->defaultconf, "Graph Editor", SPACE_GRAPH, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);
  keymap = win_keymap_ensure(wm->defaultconf, "Graph Editor Generic", SPACE_GRAPH, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

/* Drw a darker area above 1 and below -1. */
static void drw_normalization_borders(Scene *scene, View2D *v2d)
{
  gpu_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformThemeColorShadeAlpha(TH_BACK, -25, -180);

  if (v2d->cur.ymax >= 1) {
    immRectf(pos, scene->r.sfra, 1, scene->r.efra, v2d->cur.ymax);
  }
  if (v2d->cur.ymin <= -1) {
    immRectf(pos, scene->r.sfra, v2d->cur.ymin, scene->r.efra, -1);
  }

  gpu_blend(GPU_BLEND_NONE);
  immUnbindProgram();
}

static void graph_main_rgn_drw(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  SpaceGraph *sipo = cxt_win_space_graph(C);
  Scene *scene = cxt_data_scene(C);
  AnimCxt ac;
  View2D *v2d = &rgn->v2d;

  /* clear and setup matrix */
  ui_ThemeClearColor(TH_BACK);

  ui_view2d_view_ortho(v2d);

  /* grid */
  bool display_seconds = (sipo->mode == SIPO_MODE_ANIM) && (sipo->flag & SIPO_DRAWTIME);
  ui_view2d_drw_lines_x_frames_or_seconds(v2d, scene, display_seconds);
  ui_view2d_drw_lines_y_vals(v2d);

  ed_rgn_drw_cb_drw(C, rgn, RGN_DRW_PRE_VIEW);

  /* start and end frame (in F-Curve mode only) */
  if (sipo->mode != SIPO_MODE_DRIVERS) {
    anim_drw_framerange(scene, v2d);
  }

  if (sipo->mode == SIPO_MODE_ANIM && (sipo->flag & SIPO_NORMALIZE)) {
    drw_normalization_borders(scene, v2d);
  }

  /* drw data */
  if (anim_animdata_get_cxt(C, &ac)) {
    /* drw ghost curves */
    graph_drw_ghost_curves(&ac, sipo, rgn);

    /* drw curves twice: unsel, then sel, so that the are fewer occlusion problems */
    graph_drw_curves(&ac, sipo, rgn, 0);
    graph_drw_curves(&ac, sipo, rgn, 1);

    /* the slow way to set tot rect... but for nice sliders needed. */
    get_graph_keyframe_extents(
        &ac, &v2d->tot.xmin, &v2d->tot.xmax, &v2d->tot.ymin, &v2d->tot.ymax, false, true);
    /* extra offset so that these items are visible */
    v2d->tot.xmin -= 10.0f;
    v2d->tot.xmax += 10.0f;
  }

  if ((sipo->flag & SIPO_NODRAWCURSOR) == 0) {
    uint pos = gpu_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

    /* horizontal component of value-cursor (value line before the current frame line) */
    float y = sipo->cursorVal;

    /* Draw a line to indicate the cursor value. */
    immUniformThemeColorShadeAlpha(TH_CFRAME, -10, -50);
    gpu_blend(GPU_BLEND_ALPHA);
    gpu_line_width(2.0);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex2f(pos, v2d->cur.xmin, y);
    immVertex2f(pos, v2d->cur.xmax, y);
    immEnd();

    gpu_blend(GPU_BLEND_NONE);

    /* Vertical component of the cursor. */
    if (sipo->mode == SIPO_MODE_DRIVERS) {
      /* cursor x-val */
      float x = sipo->cursorTime;

      /* to help differentiate this from the current frame,
       * drw slightly darker like the horizontal one */
      immUniformThemeColorShadeAlpha(TH_CFRAME, -40, -50);
      gpu_blend(GPU_BLEND_ALPHA);
      gpu_line_width(2.0);

      immBegin(GPU_PRIM_LINES, 2);
      immVertex2f(pos, x, v2d->cur.ymin);
      immVertex2f(pos, x, v2d->cur.ymax);
      immEnd();

      gpu_blend(GPU_BLEND_NONE);
    }

    immUnbindProgram();
  }

  /* markers */
  if (sipo->mode != SIPO_MODE_DRIVERS) {
    ui_view2d_view_orthoSpecial(rgn, v2d, true);
    int marker_drw_flag = DRW_MARKERS_MARGIN;
    if (sipo->flag & SIPO_SHOW_MARKERS) {
      ed_markers_drw(C, marker_drw_flag);
    }
  }

  /* preview range */
  if (sipo->mode != SIPO_MODE_DRIVERS) {
    ui_view2d_view_ortho(v2d);
    anim_drw_previewrange(C, v2d, 0);
  }

  /* cb */
  ui_view2d_view_ortho(v2d);
  ed_rgn_drw_cb_drw(C, rgn, RGN_DRW_POST_VIEW);

  /* reset view matrix */
  ui_view2d_view_restore(C);

  /* time-scrubbing */
  ed_time_scrub_drw(rgn, scene, display_seconds, false);
}

static void graph_main_rgn_drw_overlay(const Cxt *C, ARgn *rgn)
{
  /* drw entirely, view changes should be handled here */
  const SpaceGraph *sipo = cxt_win_space_graph(C);

  const Scene *scene = cxt_data_scene(C);
  View2D *v2d = &rgn->v2d;

  /* Driver Editor's X axis is not time. */
  if (sipo->mode != SIPO_MODE_DRIVERS) {
    /* scrubbing rgn */
    ed_time_scrub_drw_current_frame(rgn, scene, sipo->flag & SIPO_DRWTIME);
  }

  /* scrollers */
  /* FIXME: args for scrollers depend on the type of data being shown. */
  ui_view2d_scrollers_drw(v2d, nullptr);

  /* scale numbers */
  {
    rcti rect;
    lib_rcti_init(
        &rect, 0, 15 * UI_SCALE_FAC, 15 * UI_SCALE_FAC, rgn->winy - UI_TIME_SCRUB_MARGIN_Y);
    ui_view2d_drw_scale_y_vals(rgn, v2d, &rect, TH_SCROLL_TEXT);
  }
}

static void graph_channel_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  /* make sure we keep the hide flags */
  rgn->v2d.scroll |= V2D_SCROLL_RIGHT;

  /* prevent any noise of past */
  rgn->v2d.scroll &= ~(V2D_SCROLL_LEFT | V2D_SCROLL_TOP | V2D_SCROLL_BOTTOM);

  rgn->v2d.scroll |= V2D_SCROLL_HORIZONTAL_HIDE;
  rgn->v2d.scroll |= V2D_SCROLL_VERTICAL_HIDE;

  ui_view2d_rgn_reinit(&rgn->v2d, V2D_COMMONVIEW_LIST, rgn->winx, rgn->winy);

  /* own keymap */
  keymap = win_keymap_ensure(win->defaultconf, "Anim Channels", SPACE_EMPTY, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);
  keymap = win_keymap_ensure(win->defaultconf, "Graph Editor Generic", SPACE_GRAPH, RGN_TYPE_WIN);
  win_ev_add_keymap_handler(&rgn->handlers, keymap);
}

static void graph_channel_rgn_drw(const Cxt *C, ARgn *rgn)
{
  AnimCxt ac;
  View2D *v2d = &rgn->v2d;

  /* clear and setup matrix */
  ui_ThemeClearColor(TH_BACK);

  ui_view2d_view_ortho(v2d);

  /* drw channels */
  if (anim_animdata_get_cxt(C, &ac)) {
    graph_drw_channel_names((Cxt *)C, &ac, rgn);
  }

  /* channel filter next to scrubbing area */
  ed_time_scrub_channel_search_drw(C, rgn, ac.ads);

  /* reset view matrix */
  ui_view2d_view_restore(C);

  /* scrollers */
  ui_view2d_scrollers_drw(v2d, nullptr);
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void graph_header_rgn_init(WinMngr * /*wm*/, ARgn *rgn)
{
  ed_rgn_header_init(region);
}

static void graph_header_rgn_drw(const Cxt *C, ARgn *rgn)
{
  ed_rgn_header(C, rgn);
}

/* add handlers, stuff you only do once or on area/rgn changes */
static void graph_btns_rgn_init(WinMngr *wm, ARgn *rgn)
{
  WinKeyMap *keymap;

  ed_rgn_pnls_init(wm, rgn);

  keymap = win_keymap_ensure(wm->defaultconf, "Graph Editor Generic", SPACE_GRAPH, RGN_TYPE_WIN);
  win_ev_add_keymap_handler_v2d_mask(&rgn->handlers, keymap);
}

static void graph_btns_rgn_drw(const Cxt *C, ARgn *rgn)
{
  ed_rgn_pnls(C, rgn);
}

static void graph_rgn_listener(const WinRgnListenerParams *params)
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
        case ND_RENDER_OPTIONS:
        case ND_OB_ACTIVE:
        case ND_FRAME:
        case ND_FRAME_RANGE:
        case ND_MARKERS:
          ws_rgn_tag_redrw(rgn);
          break;
        case ND_SEQ:
          if (winn->action == NA_SEL) {
            ed_rgn_tag_redrw(rgn);
          }
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
    case NC_NODE:
      switch (winn->action) {
        case NA_EDITED:
        case NA_SEL:
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

static void graph_rgn_msg_sub(const WinRgnMsgSubParams *params)
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
        use_preview ? &api_scene_frame_preview_start : &api_Scene_frame_start,
        use_preview ? &api_scene_frame_preview_end : &api_Scene_frame_end,
        &api_Scene_use_preview_range,
        &api_Scene_frame_current,
    };

    ApiPtr idptr = api_id_ptr_create(&scene->id);

    for (int i = 0; i < ARRAY_SIZE(props); i++) {
      win_msg_sub_api(mbus, &idptr, props[i], &msg_sub_val_rgn_tag_redrw, __func__);
    }
  }

  /* All dopesheet filter settings, etc. affect the drwing of this editor,
   * also same applies for all anim-related datatypes that may appear here,
   * so just whitelist the entire structs for updates */
  {
    WinMsgParamsApi msg_key_params = {{nullptr}};
    ApiStruct *type_array[] = {
        &ApiDopeSheet, /* dopesheet filters */

        &ApiActionGroup, /* channel groups */
        &ApiFCurve,      /* F-Curve */
        &ApiKeyframe,
        &ApiFCurveSample,

        &ApiFMod, /* F-Mods (Why can't we just do all subclasses too?) */
        &ApiFModCycles,
        &ApiFModEnvelope,
        &ApiFModEnvelopeCtrlPoint,
        &ApiFModFnGenerator,
        &ApiFModGenerator,
        &ApiFModLimits,
        &ApiFModNoise,
        &ApiFModStepped,
    };

    for (int i = 0; i < ARRAY_SIZE(type_array); i++) {
      msg_key_params.ptr.type = type_array[i];
      win_msg_sub_api_params(
          mbus, &msg_key_params, &msg_sub_val_rgn_tag_redrw, __func__);
    }
  }
}

/* editor level listener */
static void graph_listener(const WinSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  const WinNotifier *winn = params->notifier;
  SpaceGraph *sipo = (SpaceGraph *)area->spacedata.first;

  /* cxt changes */
  switch (winn->category) {
    case NC_ANIM:
      /* For sel changes of anim data, we can just redrw...
       * otherwise auto-color might need to be done again. */
      if (ELEM(winn->data, ND_KEYFRAME, ND_ANIMCHAN) && (winn->action == NA_SEL)) {
        ed_area_tag_redrw(area);
      }
      else {
        ed_area_tag_refresh(area);
      }
      break;
    case NC_SCENE:
      switch (winn->data) {
        case ND_OB_ACTIVE: /* Sel changed, so force refresh to flush
                            * (needs flag set to do syncing). */
        case ND_OB_SEL:
          sipo->runtime.flag |= SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
          break;

        default: /* just redrwing the view will do */
          ed_area_tag_redrw(area);
          break;
      }
      break;
    case NC_OB:
      switch (winn->data) {
        case ND_BONE_SEL: /* Sel changed, so force refresh to flush
                           * (needs flag set to do syncing). */
        case ND_BONE_ACTIVE:
          sipo->runtime.flag |= SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC;
          ed_area_tag_refresh(area);
          break;
        case ND_TRANSFORM:
          break; /* Do nothing. */

        default: /* just redrwing the view will do */
          ed_area_tag_redrw(area);
          break;
      }
      break;
    case NC_NODE:
      if (winn->action == NA_SEL) {
        /* sel changed, so force refresh to flush (needs flag set to do syncing) */
        sipo->runtime.flag |= SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC;
        ed_area_tag_refresh(area);
      }
      break;
    case NC_SPACE:
      if (winn->data == ND_SPACE_GRAPH) {
        ed_area_tag_redrw(area);
      }
      break;
    case NC_WIN:
      if (sipo->runtime.flag &
          (SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC | SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC_COLOR))
      {
        /* force redrw/refresh after undo/redo - prevents "black curve" problem */
        ed_area_tag_refresh(area);
      }
      break;

#if 0 /* restore the case below if not enough updates occur... */
    default: {
      if (winn->data == ND_KEYS) {
        ed_area_tag_redrw(area);
      }
    }
#endif
  }
}

/* Update F-Curve colors */
static void graph_refresh_fcurve_colors(const Cxt *C)
{
  AnimCxt ac;

  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  size_t items;
  int filter;
  int i;

  if (anim_animdata_get_cxt(C, &ac) == false) {
    return;
  }

  ui_SetTheme(SPACE_GRAPH, RGN_TYPE_WIN);

  /* build list of F-Curves which will be visible as channels in channel-rgn
   * we don't include ANIMFILTER_CURVEVISIBLE filter, that results in a
   * mismatch between channel-colors and the drawn curves */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_NODUPLIS | ANIMFILTER_FCURVESONLY);
  items = anim_animdata_filter(
      &ac, &anim_data, eAnimFilter_Flags(filter), ac.data, eAnimCont_Types(ac.datatype));

  /* loop over F-Curves, assigning colors */
  for (ale = static_cast<bAnimListElem *>(anim_data.first), i = 0; ale; ale = ale->next, i++) {
    FCurve *fcu = (FCurve *)ale->data;

    /* set color of curve here */
    switch (fcu->color_mode) {
      case FCURVE_COLOR_CUSTOM: {
        /* User has defined a custom color for this curve already
         * (we assume it's not going to cause clashes with text colors),
         * which should be left alone... Nothing needs to be done here.
         */
        break;
      }
      case FCURVE_COLOR_AUTO_RGB: {
        /* F-Curve's array index is automatically mapped to RGB values.
         * This works best of 3-value vectors.
         * TODO: find a way to module the hue so that not all curves have same color...
         */
        float *col = fcu->color;

        switch (fcu->array_index) {
          case 0:
            ui_GetThemeColor3fv(TH_AXIS_X, col);
            break;
          case 1:
            ui_GetThemeColor3fv(TH_AXIS_Y, col);
            break;
          case 2:
            ui_GetThemeColor3fv(TH_AXIS_Z, col);
            break;
          default:
            /* 'unknown' color - bluish so as to not conflict with handles */
            col[0] = 0.3f;
            col[1] = 0.8f;
            col[2] = 1.0f;
            break;
        }
        break;
      }
      case FCURVE_COLOR_AUTO_YRGB: {
        /* Like FCURVE_COLOR_AUTO_RGB, except this is for quaternions... */
        float *col = fcu->color;

        switch (fcu->array_index) {
          case 1:
            ui_GetThemeColor3fv(TH_AXIS_X, col);
            break;
          case 2:
            ui_GetThemeColor3fv(TH_AXIS_Y, col);
            break;
          case 3:
            ui_GetThemeColor3fv(TH_AXIS_Z, col);
            break;

          case 0: {
            /* Special Case: "W" channel should be yellowish, so blend X and Y channel colors... */
            float c1[3], c2[3];
            float h1[3], h2[3];
            float hresult[3];

            /* get colors (rgb) */
            ui_GetThemeColor3fv(TH_AXIS_X, c1);
            ui_GetThemeColor3fv(TH_AXIS_Y, c2);

            /* perform blending in HSV space (to keep brightness similar) */
            rgb_to_hsv_v(c1, h1);
            rgb_to_hsv_v(c2, h2);

            interp_v3_v3v3(hresult, h1, h2, 0.5f);

            /* convert back to RGB for display */
            hsv_to_rgb_v(hresult, col);
            break;
          }

          default:
            /* 'unknown' color: bluish to not conflict w handles */
            col[0] = 0.3f;
            col[1] = 0.8f;
            col[2] = 1.0f;
            break;
        }
        break;
      }
      case FCURVE_COLOR_AUTO_RAINBOW:
      default: {
        /* determine color 'automatically' using 'magic fn' which uses the given args
         * of current item index + total items to determine some RGB color */
        getcolor_fcurve_rainbow(i, items, fcu->color);
        break;
      }
    }
  }

  /* free tmp list */
  anim_animdata_freelist(&anim_data);
}

static void graph_refresh(const Cxt *C, ScrArea *area)
{
  SpaceGraph *sipo = (SpaceGraph *)area->spacedata.first;

  /* updates to data needed depends on Graph Editor mode... */
  switch (sipo->mode) {
    case SIPO_MODE_ANIM: /* all animation */
    {
      break;
    }

    case SIPO_MODE_DRIVERS: /* Drivers only. */
    {
      break;
    }
  }

  /* rgn updates? */
  /* re-sizing y-extents of tot should go here? */
  /* Update the state of the animchannels in response to changes from the data they represent
   * The tmp flag is used to indicate when this needs to be done,
   * and will be cleared once handled. */
  if (sipo->runtime.flag & SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC) {
    anim_sync_animchannels_to_data(C);
    sipo->runtime.flag &= ~SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC;
    ed_area_tag_redrw(area);
  }

  /* We could check 'SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC_COLOR', but color is recalculated anyway. */
  if (sipo->runtime.flag & SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC_COLOR) {
    sipo->runtime.flag &= ~SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC_COLOR;
#if 0 /* Done below. */
    graph_refresh_fcurve_colors(C);
#endif
    ed_area_tag_redrw(area);
  }

  sipo->runtime.flag &= ~(SIPO_RUNTIME_FLAG_TWEAK_HANDLES_LEFT |
                          SIPO_RUNTIME_FLAG_TWEAK_HANDLES_RIGHT);

  /* init/adjust F-Curve colors */
  graph_refresh_fcurve_colors(C);
}

static void graph_id_remap(ScrArea * /*area*/, SpaceLink *slink, const IdRemapper *mappings)
{
  SpaceGraph *sgraph = (SpaceGraph *)slink;
  if (!sgraph->ads) {
    return;
  }

  dune_id_remapper_apply(mappings, (Id **)&sgraph->ads->filter_grp, ID_REMAP_APPLY_DEFAULT);
  dune_id_remapper_apply(mappings, (Id **)&sgraph->ads->source, ID_REMAP_APPLY_DEFAULT);
}

static void graph_foreach_id(SpaceLink *space_link, LibraryForeachIDData *data)
{
  SpaceGraph *sgraph = reinterpret_cast<SpaceGraph *>(space_link);
  const int data_flags = dune_lib_query_foreachid_process_flags_get(data);
  const bool is_readonly = (data_flags & IDWALK_READONLY) != 0;

  /* Could be dedup'd w the DopeSheet handling of SpaceAction and SpaceNla. */
  if (sgraph->ads == nullptr) {
    return;
  }

  DUNE_LIB_FOREACHID_PROCESS_ID(data, sgraph->ads->source, IDWALK_CB_NOP);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sgraph->ads->filter_grp, IDWALK_CB_NOP);

  if (!is_readonly) {
    /* Force recalc of list of channels (i.e. including calculating F-Curve colors) to
     * prevent the "black curves" problem post-undo. */
    sgraph->runtime.flag |= SIPO_RUNTIME_FLAG_NEED_CHAN_SYNC_COLOR;
  }
}

static int graph_space_subtype_get(ScrArea *area)
{
  SpaceGraph *sgraph = static_cast<SpaceGraph *>(area->spacedata.first);
  return sgraph->mode;
}

static void graph_space_subtype_set(ScrArea *area, int val)
{
  SpaceGraph *sgraph = static_cast<SpaceGraph *>(area->spacedata.first);
  sgraph->mode = val;
}

static void graph_space_subtype_item_extend(Cxt * /*C*/,
                                            EnumPropItem **item,
                                            int *totitem)
{
  api_enum_items_add(item, totitem, api_enum_space_graph_mode_items);
}

static void graph_space_blend_read_data(DataReader *reader, SpaceLink *sl)
{
  SpaceGraph *sipo = (SpaceGraph *)sl;

  loader_read_data_address(reader, &sipo->ads);
  memset(&sipo->runtime, 0x0, sizeof(sipo->runtime));
}

static void graph_space_dune_write(Writer *writer, SpaceLink *sl)
{
  SpaceGraph *sipo = (SpaceGraph *)sl;
  List tmpGhosts = sipo->runtime.ghost_curves;

  /* tmp disable ghost curves when saving */
  lib_list_clear(&sipo->runtime.ghost_curves);

  loader_write_struct(writer, SpaceGraph, sl);
  if (sipo->ads) {
    loader_write_struct(writer, DopeSheet, sipo->ads);
  }

  /* Re-enable ghost curves. */
  sipo->runtime.ghost_curves = tmpGhosts;
}

void ed_spacetype_ipo()
{
  SpaceType *st = static_cast<SpaceType *>(mem_calloc(sizeof(SpaceType), "spacetype ipo"));
  ARgnType *art;

  st->spaceid = SPACE_GRAPH;
  STRNCPY(st->name, "Graph");

  st->create = graph_create;
  st->free = graph_free;
  st->init = graph_init;
  st->dup = graph_dup;
  st->optypes = graphedit_optypes;
  st->keymap = graphedit_keymap;
  st->listener = graph_listener;
  st->refresh = graph_refresh;
  st->id_remap = graph_id_remap;
  st->foreach_id = graph_foreach_id;
  st->space_subtype_item_extend = graph_space_subtype_item_extend;
  st->space_subtype_get = graph_space_subtype_get;
  st->space_subtype_set = graph_space_subtype_set;
  st->dune_read_data = graph_space_dune_read_data;
  st->dune_read_after_liblink = nullptr;
  st->dune_write = graph_space_dune_write;

  /* rgns: main win */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype graphedit rgn"));
  art->rgnid = RGN_TYPE_WIN;
  art->init = graph_main_rgn_init;
  art->drw = graph_main_rgn_drw;
  art->drw_overlay = graph_main_rgn_drw_overlay;
  art->listener = graph_rgn_listener;
  art->msg_sub = graph_rgn_msg_sub;
  art->keymapflag = ED_KEYMAP_VIEW2D | ED_KEYMAP_ANIM | ED_KEYMAP_FRAMES;

  lib_addhead(&st->rgntypes, art);

  /* regions: header */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype graphedit rgn"));
  art->rgnid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->listener = graph_rgn_listener;
  art->init = graph_header_rgn_init;
  art->drw = graph_header_rgn_drw;

  lib_addhead(&st->rgntypes, art);

  /* rgns: channels */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype graphedit rgn"));
  art->rgnid = RGN_TYPE_CHANNELS;
  /* 200 is the 'standard', but due to scrollers, we want a bit more to fit the lock icons in */
  art->prefsizex = 200 + V2D_SCROLL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES;
  art->listener = graph_rgn_listener;
  art->msg_sub = graph_rgn_msg_sub;
  art->init = graph_channel_rgn_init;
  art->drw = graph_channel_rgn_drw;

  lib_addhead(&st->rgntypes, art);

  /* rgns: UI btns */
  art = static_cast<ARgnType *>(mem_calloc(sizeof(ARgnType), "spacetype graphedit rgn"));
  art->rgnid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PNL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = graph_rgn_listener;
  art->init = graph_btns_rgn_init;
  art->drw = graph_btns_rgn_drw;

  lib_addhead(&st->rgntypes, art);

  graph_btns_register(art);

  art = ed_area_type_hud(st->spaceid);
  lib_addhead(&st->rgntypes, art);

  dune_spacetype_register(st);
}
