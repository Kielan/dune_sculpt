/* Defines:
 * UI_OT_eyedropper_color */

#include "mem_guardedalloc.h"

#include "types_screen.h"
#include "types_space.h"

#include "lib_list.h"
#include "lib_math_vector.h"
#include "lib_string.h"

#include "dune_cxt.h"
#include "dune_cryptomatte.h"
#include "dune_image.h"
#include "dune_main.h"
#include "dune_node.h"
#include "dune_screen.h"

#include "node_composite.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "ui.h"

#include "imbuf_colormanagement.h"
#include "imbuf_types.h"

#include "win_api.h"
#include "win_types.h"

#include "api_define.h"

#include "ui_intern.h"

#include "ed_clip.h"
#include "ed_image.h"
#include "ed_node.h"
#include "ed_screen.h"

#include "render_pipeline.h"

#include "render_pipeline.h"

#include "ui_eyedropper_intern.h"

typedef struct Eyedropper {
  struct ColorManagedDisplay *display;

  ApiPtr ptr;
  ApiProp *prop;
  int index;
  bool is_undo;

  bool is_set;
  float init_col[3]; /* for resetting on cancel */

  bool accum_start; /* has mouse been pressed */
  float accum_col[3];
  int accum_tot;

  void *draw_handle_sample_text;
  char sample_text[MAX_NAME];

  Node *crypto_node;
  struct CryptomatteSession *cryptomatte_session;
} Eyedropper;

static void eyedropper_draw_cb(const wWin *win, void *arg)
{
  Eyedropper *eye = arg;
  eyedropper_draw_cursor_text_window(win, eye->sample_text);
}

static bool eyedropper_init(Cxt *C, WinOp *op)
{
  Eyedropper *eye = mem_calloc(sizeof(Eyedropper), __func__);

  Btn *btn = ui_cxt_active_btn_prop_get(C, &eye->ptr, &eye->prop, &eye->index);
  const enum PropSubType prop_subtype = eye->prop ? api_prop_subtype(eye->prop) : 0;

  if ((eye->ptr.data == NULL) || (eye->prop == NULL) ||
      (api_prop_editable(&eye->ptr, eye->prop) == false) ||
      (api_prop_array_length(&eye->ptr, eye->prop) < 3) ||
      (api_prop_type(eye->prop) != PROP_FLOAT) ||
      (ELEM(prop_subtype, PROP_COLOR, PROP_COLOR_GAMMA) == 0)) {
    mem_free(eye);
    return false;
  }
  op->customdata = eye;

  eye->is_undo = btn_flag_is_set(but, UI_BUT_UNDO);

  float col[4];
  api_prop_float_get_array(&eye->ptr, eye->prop, col);
  if (eye->ptr.type == &ApiCompositorNodeCryptomatteV2) {
    eye->crypto_node = (bNode *)eye->ptr.data;
    eye->cryptomatte_session = ntreeCompositCryptomatteSession(cxt_data_scene(C),
                                                               eye->crypto_node);
    eye->draw_handle_sample_text = win_draw_cb_activate(cxt_win(C), eyedropper_draw_cb, eye);
  }

  if (prop_subtype != PROP_COLOR) {
    Scene *scene = cxt_data_scene(C);
    const char *display_device;

    display_device = scene->display_settings.display_device;
    eye->display = imbuf_colormanagement_display_get_named(display_device);

    /* store initial color */
    if (eye->display) {
      imbuf_colormanagement_display_to_scene_linear_v3(col, eye->display);
    }
  }
  copy_v3_v3(eye->init_col, col);

  return true;
}

static void eyedropper_exit(Cxt *C, WinOp *op)
{
  Eyedropper *eye = op->customdata;
  Win *win = cxt_win(C);
  win_cursor_modal_restore(win);

  if (eye->draw_handle_sample_text) {
    win_draw_cb_exit(win, eye->draw_handle_sample_text);
    eye->draw_handle_sample_text = NULL;
  }

  if (eye->cryptomatte_session) {
    dune_cryptomatte_free(eye->cryptomatte_session);
    eye->cryptomatte_session = NULL;
  }

  MEM_SAFE_FREE(op->customdata);
}

/* eyedropper_color_ helper fns */

static bool eyedropper_cryptomatte_sample_renderlayer_fl(RenderLayer *render_layer,
                                                         const char *prefix,
                                                         const float fpos[2],
                                                         float r_col[3])
{
  if (!render_layer) {
    return false;
  }

  const int render_layer_name_len = lib_strnlen(render_layer->name, sizeof(render_layer->name));
  if (strncmp(prefix, render_layer->name, render_layer_name_len) != 0) {
    return false;
  }

  const int prefix_len = strlen(prefix);
  if (prefix_len <= render_layer_name_len + 1) {
    return false;
  }

  /* RenderResult from images can have no render layer name. */
  const char *render_pass_name_prefix = render_layer_name_len ?
                                            prefix + 1 + render_layer_name_len :
                                            prefix;

  LIST_FOREACH (RenderPass *, render_pass, &render_layer->passes) {
    if (STRPREFIX(render_pass->name, render_pass_name_prefix) &&
        !STREQLEN(render_pass->name, render_pass_name_prefix, sizeof(render_pass->name))) {
      lib_assert(render_pass->channels == 4);
      const int x = (int)(fpos[0] * render_pass->rectx);
      const int y = (int)(fpos[1] * render_pass->recty);
      const int offset = 4 * (y * render_pass->rectx + x);
      zero_v3(r_col);
      r_col[0] = render_pass->rect[offset];
      return true;
    }
  }

  return false;
}
static bool eyedropper_cryptomatte_sample_render_fl(const Node *node,
                                                    const char *prefix,
                                                    const float fpos[2],
                                                    float r_col[3])
{
  bool success = false;
  Scene *scene = (Scene *)node->id;
  lib_assert(GS(scene->id.name) == ID_SCE);
  Render *re = render_GetSceneRender(scene);

  if (re) {
    RenderResult *rr = render_AcquireResultRead(re);
    if (rr) {
      LIST_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
        RenderLayer *render_layer = render_GetRenderLayer(rr, view_layer->name);
        success = eyedropper_cryptomatte_sample_renderlayer_fl(render_layer, prefix, fpos, r_col);
        if (success) {
          break;
        }
      }
    }
    render_ReleaseResult(re);
  }
  return success;
}

static bool eyedropper_cryptomatte_sample_image_fl(const Node *node,
                                                   NodeCryptomatte *crypto,
                                                   const char *prefix,
                                                   const float fpos[2],
                                                   float r_col[3])
{
  bool success = false;
  Image *image = (Image *)node->id;
  lib_assert((image == NULL) || (GS(image->id.name) == ID_IM));
  ImageUser *iuser = &crypto->iuser;

  if (image && image->type == IMA_TYPE_MULTILAYER) {
    ImBuf *ibuf = dune_image_acquire_imbuf(image, iuser, NULL);
    if (image->rr) {
      LIST_FOREACH (RenderLayer *, render_layer, &image->rr->layers) {
        success = eyedropper_cryptomatte_sample_renderlayer_fl(render_layer, prefix, fpos, r_col);
        if (success) {
          break;
        }
      }
    }
    dune_image_release_imbuf(image, ibuf, NULL);
  }
  return success;
}

static bool eyedropper_cryptomatte_sample_fl(Cxt *C,
                                             Eyedropper *eye,
                                             const int m_xy[2],
                                             float r_col[3])
{
  Node *node = eye->crypto_node;
  NodeCryptomatte *crypto = node ? ((NodeCryptomatte *)node->storage) : NULL;

  if (!crypto) {
    return false;
  }

  Screen *screen = cxt_win_screen(C);
  ScrArea *area = dune_screen_find_area_xy(screen, SPACE_TYPE_ANY, m_xy);
  if (!area || !ELEM(area->spacetype, SPACE_IMAGE, SPACE_NODE, SPACE_CLIP)) {
    return false;
  }

  ARegion *region = dune_area_find_region_xy(area, RGN_TYPE_WIN, m_xy);
  if (!region) {
    return false;
  }

  int mval[2] = {m_xy[0] - region->winrct.xmin, m_xy[1] - region->winrct.ymin};
  float fpos[2] = {-1.0f, -1.0};
  switch (area->spacetype) {
    case SPACE_IMAGE: {
      SpaceImage *sima = area->spacedata.first;
      ed_space_image_get_position(sima, region, mval, fpos);
      break;
    }
    case SPACE_NODE: {
      Main *main = cxt_data_main(C);
      SpaceNode *snode = area->spacedata.first;
      ed_space_node_get_position(bmain, snode, region, mval, fpos);
      break;
    }
    case SPACE_CLIP: {
      SpaceClip *sc = area->spacedata.first;
      ed_space_clip_get_position(sc, region, mval, fpos);
      break;
    }
    default: {
      break;
    }
  }

  if (fpos[0] < 0.0f || fpos[1] < 0.0f || fpos[0] >= 1.0f || fpos[1] >= 1.0f) {
    return false;
  }

  /* CMP_CRYPTOMATTE_SRC_RENDER and CMP_CRYPTOMATTE_SRC_IMAGE require a refd image/scene to
   * work properly. */
  if (!node->id) {
    return false;
  }

  /* TODO: Migrate this file to cc and use std::string as return param. */
  char prefix[MAX_NAME + 1];
  const Scene *scene = cxt_data_scene(C);
  ntreeCompositCryptomatteLayerPrefix(scene, node, prefix, sizeof(prefix) - 1);
  prefix[MAX_NAME] = '\0';

  if (node->custom1 == CMP_CRYPTOMATTE_SRC_RENDER) {
    return eyedropper_cryptomatte_sample_render_fl(node, prefix, fpos, r_col);
  }
  if (node->custom1 == CMP_CRYPTOMATTE_SRC_IMAGE) {
    return eyedropper_cryptomatte_sample_image_fl(node, crypto, prefix, fpos, r_col);
  }
  return false;
}

void eyedropper_color_sample_fl(Cxt *C, const int m_xy[2], float r_col[3])
{
  /* we could use some clever */
  Main *main = cxt_data_main(C);
  WinManager *wm = cxt_win_manager(C);
  const char *display_device = cxt_data_scene(C)->display_settings.display_device;
  struct ColorManagedDisplay *display = imbuf_colormanagement_display_get_named(display_device);

  int mval[2];
  Win *win;
  ScrArea *area;
  datadropper_win_area_find(C, m_xy, mval, &win, &area);

  if (area) {
    if (area->spacetype == SPACE_IMAGE) {
      ARegion *region = dune_area_find_region_xy(area, RGN_TYPE_WIN, mval);
      if (region) {
        SpaceImage *sima = area->spacedata.first;
        int region_mval[2] = {mval[0] - region->winrct.xmin, mval[1] - region->winrct.ymin};

        if (ed_space_image_color_sample(sima, region, region_mval, r_col, NULL)) {
          return;
        }
      }
    }
    else if (area->spacetype == SPACE_NODE) {
      ARegion *region = dune_area_find_region_xy(area, RGN_TYPE_WIN, mval);
      if (region) {
        SpaceNode *snode = area->spacedata.first;
        int region_mval[2] = {mval[0] - region->winrct.xmin, mval[1] - region->winrct.ymin};

        if (ed_space_node_color_sample(main, snode, region, region_mval, r_col)) {
          return;
        }
      }
    }
    else if (area->spacetype == SPACE_CLIP) {
      ARegion *region = dune_area_find_region_xy(area, RGN_TYPE_WIN, mval);
      if (region) {
        SpaceClip *sc = area->spacedata.first;
        int region_mval[2] = {mval[0] - region->winrct.xmin, mval[1] - region->winrct.ymin};

        if (ed_space_clip_color_sample(sc, region, region_mval, r_col)) {
          return;
        }
      }
    }
  }

  if (win) {
    /* Fallback to simple opengl picker. */
    win_pixel_sample_read(wm, win, mval, r_col);
    imbuf_colormanagement_display_to_scene_linear_v3(r_col, display);
  }
  else {
    zero_v3(r_col);
  }
}

/* sets the sample color RGB, maintaining A */
static void eyedropper_color_set(Cxt *C, Eyedropper *eye, const float col[3])
{
  float col_conv[4];

  /* to maintain alpha */
  api_prop_float_get_array(&eye->ptr, eye->prop, col_conv);

  /* convert from linear rgb space to display space */
  if (eye->display) {
    copy_v3_v3(col_conv, col);
    imbuf_colormanagement_scene_linear_to_display_v3(col_conv, eye->display);
  }
  else {
    copy_v3_v3(col_conv, col);
  }

  api_prop_float_set_array(&eye->ptr, eye->prop, col_conv);
  eye->is_set = true;

  api_prop_update(C, &eye->ptr, eye->prop);
}

static void eyedropper_color_sample(Cxt *C, Eyedropper *eye, const int m_xy[2])
{
  /* Accumulate color. */
  float col[3];
  if (eye->crypto_node) {
    if (!eyedropper_cryptomatte_sample_fl(C, eye, m_xy, col)) {
      return;
    }
  }
  else {
    eyedropper_color_sample_fl(C, m_xy, col);
  }

  if (!eye->crypto_node) {
    add_v3_v3(eye->accum_col, col);
    eye->accum_tot++;
  }
  else {
    copy_v3_v3(eye->accum_col, col);
    eye->accum_tot = 1;
  }

  /* Apply to prop */
  float accum_col[3];
  if (eye->accum_tot > 1) {
    mul_v3_v3fl(accum_col, eye->accum_col, 1.0f / (float)eye->accum_tot);
  }
  else {
    copy_v3_v3(accum_col, eye->accum_col);
  }
  eyedropper_color_set(C, eye, accum_col);
}

static void eyedropper_color_sample_text_update(Cxt *C, Eyedropper *eye, const int m_xy[2])
{
  float col[3];
  eye->sample_text[0] = '\0';

  if (eye->cryptomatte_session) {
    if (eyedropper_cryptomatte_sample_fl(C, eye, m_xy, col)) {
      dune_cryptomatte_find_name(
          eye->cryptomatte_session, col[0], eye->sample_text, sizeof(eye->sample_text));
      eye->sample_text[sizeof(eye->sample_text) - 1] = '\0';
    }
  }
}

static void eyedropper_cancel(Cxt *C, WinOp *op)
{
  Eyedropper *eye = op->customdata;
  if (eye->is_set) {
    eyedropper_color_set(C, eye, eye->init_col);
  }
  eyedropper_exit(C, op);
}

/* main modal status check */
static int eyedropper_modal(Cxt *C, WinOp *op, const WinEvent *event)
{
  Eyedropper *eye = (Eyedropper *)op->customdata;

  /* handle modal keymap */
  if (event->type == EVT_MODAL_MAP) {
    switch (event->val) {
      case EYE_MODAL_CANCEL:
        eyedropper_cancel(C, op);
        return O_CANCELLED;
      case EYE_MODAL_SAMPLE_CONFIRM: {
        const bool is_undo = eye->is_undo;
        if (eye->accum_tot == 0) {
          eyedropper_color_sample(C, eye, event->xy);
        }
        eyedropper_exit(C, op);
        /* Could support finished & undo-skip. */
        return is_undo ? OP_FINISHED : OPERATOR_CANCELLED;
      }
      case EYE_MODAL_SAMPLE_BEGIN:
        /* enable accum and make first sample */
        eye->accum_start = true;
        eyedropper_color_sample(C, eye, event->xy);
        break;
      case EYE_MODAL_SAMPLE_RESET:
        eye->accum_tot = 0;
        zero_v3(eye->accum_col);
        eyedropper_color_sample(C, eye, event->xy);
        break;
    }
  }
  else if (ELEM(event->type, MOUSEMOVE, INBETWEEN_MOUSEMOVE)) {
    if (eye->accum_start) {
      /* button is pressed so keep sampling */
      eyedropper_color_sample(C, eye, event->xy);
    }

    if (eye->draw_handle_sample_text) {
      eyedropper_color_sample_text_update(C, eye, event->xy);
      ed_region_tag_redraw(cxt_win_region(C));
    }
  }

  return OP_RUNNING_MODAL;
}

/* Modal Op init */
static int eyedropper_invoke(Cxt *C, WinOp *op, const WinEvent *UNUSED(event))
{
  /* init */
  if (eyedropper_init(C, op)) {
    Win *win = cxt_win(C);
    /* Workaround for de-activating the btn clearing the cursor, see T76794 */
    ui_cxt_active_btn_clear(C, win, cxt_win_region(C));
    win_cursor_modal_set(win, WIN_CURSOR_EYEDROPPER);

    /* add temp handler */
    win_event_add_modal_handler(C, op);

    return OP_RUNNING_MODAL;
  }
  return OP_PASS_THROUGH;
}

/* Repeat op */
static int eyedropper_ex(Cxt *C, WinOp *op)
{
  /* init */
  if (eyedropper_init(C, op)) {

    /* do something */

    /* cleanup */
    eyedropper_exit(C, op);

    return OP_FINISHED;
  }
  return OP_PASS_THROUGH;
}

static bool eyedropper_poll(Cxt *C)
{
  /* Actual test for active btn happens later, since we don't
   * know which one is active until mouse over. */
  return (cxt_wm_win(C) != NULL);
}

void UI_OT_eyedropper_color(WinOpType *ot)
{
  /* ids */
  ot->name = "Eyedropper";
  ot->idname = "UI_OT_eyedropper_color";
  ot->description = "Sample a color from the Dune win to store in a prop";

  /* api cbs */
  ot->invoke = eyedropper_invoke;
  ot->modal = eyedropper_modal;
  ot->cancel = eyedropper_cancel;
  ot->exec = eyedropper_ex;
  ot->poll = eyedropper_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;
}
