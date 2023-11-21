#include <cstdio>
#include <cstring>

#include "types_node.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_cxt.hh"
#include "dune_img.h"
#include "dune_img_format.h"
#include "dune_node.h"
#include "dune_scene.h"
#include "dune_screen.hh"

#include "render_pipeline.h"

#include "imbuf_colormanagement.h"
#include "imbuf.h"
#include "imbuf_types.h"

#include "ed_pen_legacy.hh"
#include "ed_img.hh"
#include "ed_screen.hh"

#include "api_access.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "img_intern.h"

#define B_NOP -1
#define MAX_IMG_INFO_LEN 128

ImageUser *ntree_get_active_iuser(NodeTree *ntree)
{
  if (ntree) {
    LIST_FOREACH (Node *, node, &ntree->nodes) {
      if (ELEM(node->type, CMP_NODE_VIEWER, CMP_NODE_SPLITVIEWER)) {
        if (node->flag & NODE_DO_OUTPUT) {
          return static_cast<ImgUser *>(node->storage);
        }
      }
    }
  }
  return nullptr;
}

/* cbs for standard img btns */
static void ui_imguser_slot_menu(Cxt * /*C*/, uiLayout *layout, void *image_p)
{
  uiBlock *block = uiLayoutGetBlock(layout);
  Img *img = static_cast<Img *>(image_p);

  int slot_id;
  LIST_FOREACH_INDEX (RenderSlot *, slot, &image->renderslots, slot_id) {
    char str[64];
    if (slot->name[0] != '\0') {
      STRNCPY(str, slot->name);
    }
    else {
      SNPRINTF(str, IFACE_("Slot %d"), slot_id + 1);
    }
    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         str,
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &img->render_slot,
         float(slot_id),
         0.0,
         0,
        -1,
        "");
  }

  uiItemS(layout);
  Btn(block,
      BTYPE_LABEL,
      0,
      IFACE_("Slot"),
      0,
      0,
      UNIT_X * 5,
      UNIT_Y,
      nullptr,
      0.0,
      0.0,
      0,
      0,
      "");
}

static bool ui_imguser_slot_menu_step(Cxt *C, int direction, void *img_p)
{
  Img *img = static_cast<Img *>(img_p);

  if (ed_img_slot_cycle(img, direction)) {
    win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);
    return true;
  }
  return true;
}

static const char *ui_imguser_layer_fake_name(RenderResult *rr)
{
  RenderView *rv = rener_RenderViewGetById(rr, 0);
  ImBuf *ibuf = rv->ibuf;
  if (!ibuf) {
    return nullptr;
  }
  if (ibuf->float_buffer.data) {
    return IFACE_("Composite");
  }
  if (ibuf->byte_buffer.data) {
    return IFACE_("Seq");
  }
  return nullptr;
}

/* workaround for passing many args */
struct ImgUIData {
  Img *img;
  ImgUser *iuser;
  int rpass_index;
};

static ImgUIData *ui_imguser_data_copy(const ImgUIData *rnd_pt_src)
{
  ImgUIData *rnd_pt_dst = static_cast<ImgUIData *>(
  mem_malloc(sizeof(*rnd_pt_src), __func__));
  memcpy(rnd_pt_dst, rnd_pt_src, sizeof(*rnd_pt_src));
  return rnd_pt_dst;
}

static void ui_imguser_layer_menu(Cxt * /*C*/, uiLayout *layout, void *rnd_pt)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  uiBlock *block = uiLayoutGetBlock(layout);
  Img *img = rnd_data->img;
  ImgUser *iuser = rnd_data->iuser;
  Scene *scene = iuser->scene;

  /* May have been freed since drwing. */
  RenderResult *rr = dune_img_acquire_renderresult(scene, img);
  if (UNLIKELY(rr == nullptr)) {
    return;
  }

  ui_block_layout_set_current(block, layout);
  uiLayoutColumn(layout, false);

  const char *fake_name = ui_imguser_layer_fake_name(rr);
  if (fake_name) {
    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         fake_name,
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &iuser->layer,
         0.0,
         0.0,
         0,
         -1,
         "");
  }

  int nr = fake_name ? 1 : 0;
  for (RenderLayer *rl = static_cast<RenderLayer *>(rr->layers.first); rl; rl = rl->next, nr++) {
    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         rl->name,
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &iuser->layer,
         float(nr),
         0.0,
         0,
         -1,
         "");
  }

  uiItemS(layout);
  Btn(block,
      BTYPE_LABEL,
      0,
      IFACE_("Layer"),
      0,
      0,
      UNIT_X * 5,
      UNIT_Y,
      nullptr,
      0.0,
      0.0,
      0,
      0,
      "");

  dune_img_release_renderresult(scene, img);
}

static void ui_imguser_pass_menu(Cxt * /*C*/, uiLayout *layout, void *rnd_pt)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  uiBlock *block = uiLayoutGetBlock(layout);
  Img *img = rnd_data->img;
  ImgUser *iuser = rnd_data->iuser;
  /* (rpass_index == -1) means composite result */
  const int rpass_index = rnd_data->rpass_index;
  Scene *scene = iuser->scene;
  RenderResult *rr;
  RenderLayer *rl;
  RenderPass *rpass;
  int nr;

  /* may have been freed since drwing */
  rr = dune_img_acquire_renderresult(scene, img);
  if (UNLIKELY(rr == nullptr)) {
    return;
  }

  rl = static_cast<RenderLayer *>(lib_findlink(&rr->layers, rpass_index));

  ui_block_layout_set_current(block, layout);
  uiLayoutColumn(layout, false);

  nr = (rl == nullptr) ? 1 : 0;

  List added_passes;
  lib_list_clear(&added_passes);

  /* rendered results don't have a Combined pass */
  /* multiview: the ordering must be ascending, so the left-most pass is always the one picked */
  for (rpass = static_cast<RenderPass *>(rl ? rl->passes.first : nullptr); rpass;
       rpass = rpass->next, nr++)
  {
    /* just show one pass of each kind */
    if (lib_findstring_ptr(&added_passes, rpass->name, offsetof(LinkData, data))) {
      continue;
    }
    lib_addtail(&added_passes, lib_genericNode(rpass->name));

    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         IFACE_(rpass->name),
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &iuser->pass,
         float(nr),
         0.0,
         0,
         -1,
         "");
  }

  uiItemS(layout);
  Btn(block,
      BTYPE_LABEL,
      0,
      IFACE_("Pass"),
      0,
      0,
      UNIT_X * 5,
      UNIT_Y,
      nullptr,
      0.0,
      0.0,
      0,
      0,
      "");

  lib_freelist(&added_passes);

  dune_img_release_renderresult(scene, img);
}

/* view menus */
static void ui_imguser_view_menu_rr(Cxt * /*C*/, uiLayout *layout, void *rnd_pt)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  uiBlock *block = uiLayoutGetBlock(layout);
  Img *img = rnd_data->image;
  ImgUser *iuser = rnd_data->iuser;
  RenderResult *rr;
  RenderView *rview;
  int nr;
  Scene *scene = iuser->scene;

  /* may have been freed since drwing */
  rr = dune_img_acquire_renderresult(scene, img);
  if (UNLIKELY(rr == nullptr)) {
    return;
  }

  ui_block_layout_set_current(block, layout);
  uiLayoutColumn(layout, false);

  Btn(block,
      BTYPE_LABEL,
      0,
      IFACE_("View"),
      0,
      0,
      UNIT_X * 5,
      UNIT_Y,
      nullptr,
      0.0,
      0.0,
      0,
      0,
      "");

  uiItemS(layout);

  nr = (rr ? lib_list_count(&rr->views) : 0) - 1;
  for (rview = static_cast<RenderView *>(rr ? rr->views.last : nullptr); rview;
       rview = rview->prev, nr--)
  {
    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         IFACE_(rview->name),
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &iuser->view,
         float(nr),
         0.0,
         0,
         -1,
         "");
  }

  dune_img_release_renderresult(scene, img);
}

static void ui_imguser_view_menu_multiview(Cxt * /*C*/, uiLayout *layout, void *rnd_pt)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  uiBlock *block = uiLayoutGetBlock(layout);
  Img *img = rnd_data->img;
  ImgUser *iuser = rnd_data->iuser;
  int nr;
  ImgView *iv;
  
  ui_block_layout_set_current(block, layout);
  uiLayoutColumn(layout, false);

  Btn(block,
      BTYPE_LABEL,
      0,
      IFACE_("View"),
      0,
      0,
      UNIT_X * 5,
      UNIT_Y,
      nullptr,
      0.0,
      0.0,
      0,
      0,
      "");

  uiItemS(layout);

  nr = lib_list_count(&img->views) - 1;
  for (iv = static_cast<ImgView *>(img->views.last); iv; iv = iv->prev, nr--) {
    BtnS(block,
         BTYPE_BTN_MENU,
         B_NOP,
         IFACE_(iv->name),
         0,
         0,
         UNIT_X * 5,
         UNIT_X,
         &iuser->view,
         float(nr),
         0.0,
         0,
         -1,
         "");
  }
}

/* 5 layer btn cbs... */
static void img_multi_cb(Cxt *C, void *rnd_pt, void *rr_v)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  ImgUser *iuser = rnd_data->iuser;
  dune_img_multilayer_index(static_cast<RenderResult *>(rr_v), iuser);
  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);
}

static bool ui_imguser_layer_menu_step(Cxt *C, int direction, void *rnd_pt)
{
  Scene *scene = cxt_data_scene(C);
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  Img *img = rnd_data->image;
  ImgUser *iuser = rnd_data->iuser;
  RenderResult *rr;
  bool changed = false;

  rr = dune_img_acquire_renderresult(scene, img);
  if (UNLIKELY(rr == nullptr)) {
    return false;
  }

  if (direction == -1) {
    if (iuser->layer > 0) {
      iuser->layer--;
      changed = true;
    }
  }
  else if (direction == 1) {
    int tot = lib_list_count(&rr->layers);

    if (render_HasCombinedLayer(rr)) {
      tot++; /* fake compo/seq layer */
    }

    if (iuser->layer < tot - 1) {
      iuser->layer++;
      changed = true;
    }
  }
  else {
    lib_assert(0);
  }

  dune_img_release_renderresult(scene, img);

  if (changed) {
    dune_img_multilayer_index(rr, iuser);
    win_ev_add_notifier(C, NC_IMG| ND_DRW, nullptr);
  }

  return changed;
}

static bool ui_imguser_pass_menu_step(Cxt *C, int direction, void *rnd_pt)
{
  Scene *scene = cxt_data_scene(C);
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  Img *img = rnd_data->img;
  ImgUser *iuser = rnd_data->iuser;
  RenderResult *rr;
  bool changed = false;
  int layer = iuser->layer;
  RenderLayer *rl;
  RenderPass *rpass;

  rr = dune_img_acquire_renderresult(scene, img);
  if (UNLIKELY(rr == nullptr)) {
    dune_img_release_renderresult(scene, img);
    return false;
  }

  if (render_HasCombinedLayer(rr)) {
    layer -= 1;
  }

  rl = static_cast<RenderLayer *>(lib_findlink(&rr->layers, layer));
  if (rl == nullptr) {
    dune_img_release_renderresult(scene, img);
    return false;
  }

  rpass = static_cast<RenderPass *>(lib_findlink(&rl->passes, iuser->pass));
  if (rpass == nullptr) {
    dune_img_release_renderresult(scene, img);
    return false;
  }

  /* This looks reversed, but matches menu direction. */
  if (direction == -1) {
    RenderPass *rp;
    int rp_index = iuser->pass + 1;

    for (rp = rpass->next; rp; rp = rp->next, rp_index++) {
      if (!STREQ(rp->name, rpass->name)) {
        iuser->pass = rp_index;
        changed = true;
        break;
      }
    }
  }
  else if (direction == 1) {
    RenderPass *rp;
    int rp_index = 0;

    if (iuser->pass == 0) {
      dune_img_release_renderresult(scene, img);
      return false;
    }

    for (rp = static_cast<RenderPass *>(rl->passes.first); rp; rp = rp->next, rp_index++) {
      if (STREQ(rp->name, rpass->name)) {
        iuser->pass = rp_index - 1;
        changed = true;
        break;
      }
    }
  }
  else {
    lib_assert(0);
  }

  dune_img_release_renderresult(scene, img);

  if (changed) {
    dune_img_multilayer_index(rr, iuser);
    win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);
  }

  return changed;
}

/* 5 view btn cbs... */
static void im_multiview_cb(Cxt *C, void *rnd_pt, void * /*arg_v*/)
{
  ImgUIData *rnd_data = static_cast<ImgUIData *>(rnd_pt);
  Img *img = rnd_data->img;
  ImgUser *iuser = rnd_data->iuser;

  dune_img_multiview_index(img, iuser);
  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);
}

static void uiblock_layer_pass_btns(uiLayout *layout,
                                    Img *img,
                                    RenderResult *rr,
                                    ImgUser *iuser,
                                    int w,
                                    const short *render_slot)
{
  ImgUIData rnd_pt_local, *rnd_pt = nullptr;
  uiBlock *block = uiLayoutGetBlock(layout);
  uiBtn *btm;
  RenderLayer *rl = nullptr;
  int wmenu1, wmenu2, wmenu3, wmenu4;
  const char *fake_name;
  const char *display_name = "";
  const bool show_stereo = (iuser->flag & IMA_SHOW_STEREO) != 0;

  if (iuser->scene == nullptr) {
    return;
  }

  uiLayoutRow(layout, true);

  /* layer menu is 1/3 larger than pass */
  wmenu1 = (2 * w) / 5;
  wmenu2 = (3 * w) / 5;
  wmenu3 = (3 * w) / 6;
  wmenu4 = (3 * w) / 6;

  rnd_pt_local.img = img;
  rnd_pt_local.iuser = iuser;
  rnd_pt_local.rpass_index = 0;

  /* menu btns */
  if (render_slot) {
    char str[64];
    RenderSlot *slot = dune_img_get_renderslot(img, *render_slot);
    if (slot && slot->name[0] != '\0') {
      STRNCPY(str, slot->name);
    }
    else {
      SNPRINTF(str, IFACE_("Slot %d"), *render_slot + 1);
    }

    rnd_pt = ui_imguser_data_copy(&rnd_pt_local);
    btn = uiDefMenuBtn(
        block, ui_imguser_slot_menu, img, str, 0, 0, wmenu1, UNIT_Y, TIP_("Sel Slot"));
    btn_fn_menu_step_set(btn, ui_imguser_slot_menu_step);
    btn_fn_set(but, imb_multi_cb, rnd_pt, rr);
    btn_type_set_menu_from_pulldown(but);
    rnd_pt = nullptr;
  }

  if (rr) {
    RenderPass *rpass;
    RenderView *rview;
    int rpass_index;

    /* layer */
    fake_name = ui_imguser_layer_fake_name(rr);
    rpass_index = iuser->layer - (fake_name ? 1 : 0);
    rl = static_cast<RenderLayer *>(lib_findlink(&rr->layers, rpass_index));
    rnd_pt_local.rpass_index = rpass_index;

    if (render_layers_have_name(rr)) {
      display_name = rl ? rl->name : (fake_name ? fake_name : "");
      rnd_pt = ui_imguser_data_copy(&rnd_pt_local);
      but = uiDefMenuBtn(block,
                         ui_imuser_layer_menu,
                         rnd_pt,
                         display_name,
                         0,
                         0,
                         wmenu2,
                         UNIT_Y,
                         TIP_("Sel Layer"));
      btn_fn_menu_step_set(btn, ui_imguser_layer_menu_step);
      btn_fn_set(but, img_multi_cb, rnd_pt, rr);
      btn_type_set_menu_from_pulldown(but);
      rnd_pt = nullptr;
    }

    /* pass */
    rpass = static_cast<RenderPass *>(rl ? lib_findlink(&rl->passes, iuser->pass) : nullptr);

    if (rl && render_passes_have_name(rl)) {
      display_name = rpass ? rpass->name : "";
      rnd_pt = ui_imguser_data_copy(&rnd_pt_local);
      btn = MenuBtn(block,
                    ui_imguser_pass_menu,
                    rnd_pt,
                    IFACE_(display_name),
                    0,
                    0,
                    wmenu3,
                    UI_UNIT_Y,
                    TIP_("Sel Pass"));
      btn_fn_menu_step_set(btn, ui_imguser_pass_menu_step);
      btn_fn_set(btn, img_multi_cb, rnd_pt, rr);
      btn_type_set_menu_from_pulldown(but);
      rnd_pt = nullptr;
    }

    /* view */
    if (lib_list_count_at_most(&rr->views, 2) > 1 &&
        ((!show_stereo) || !render_RenderResult_is_stereo(rr)))
    {
      rview = static_cast<RenderView *>(lib_findlink(&rr->views, iuser->view));
      display_name = rview ? rview->name : "";

      rnd_pt = ui_imguser_data_copy(&rnd_pt_local);
      btn = Btn(block,
                 ui_imguser_view_menu_rr,
                 rnd_pt,
                 display_name,
                 0,
                 0,
                 wmenu4,
                 UNIT_Y,
                 TIP_("Sel View"));
      btn_fn_set(btn, img_multi_cb, rnd_pt, rr);
      btn_type_set_menu_from_pulldown(but);
      rnd_pt = nullptr;
    }
  }

  /* stereo imb */
  else if ((dune_img_is_stereo(img) && (!show_stereo)) ||
           (dune_img_is_multiview(img) && !dune_img_is_stereo(img)))
  {
    int nr = 0;

    LIST_FOREACH (ImgView *, iv, &img->views) {
      if (nr++ == iuser->view) {
        display_name = iv->name;
        break;
      }
    }

    rnd_pt = ui_imguser_data_copy(&rnd_pt_local);
    btn = MenuBtn(block,
                  ui_imguser_view_menu_multiview,
                  rnd_pt,
                  display_name,
                  0,
                  0,
                  wmenu1,
                  UNIT_Y,
                  TIP_("Sel View"));
    btn_fn_set(btn, img_multiview_cb, rnd_pt, nullptr);
    btn_type_set_menu_from_pulldown(but);
    rnd_pt = nullptr;
  }
}

struct ApiUpdateCb {
  ApiPtr ptr;
  ApiProp *prop;
  ImgUser *iuser;
};

static void api_update_cb(Cxt *C, void *arg_cb, void * /*arg*/)
{
  ApiUpdateCb *cb = (ApiUpdateCb *)arg_cb;

  /* we call update here on the ptr prop, this way the
   * owner of the img ptr can still define its own update
   * and notifier */
  api_prop_update(C, &cb->ptr, cb->prop);
}

void uiTemplateImg(uiLayout *layout,
                   Cxt *C,
                   ApiPtr *ptr,
                   const char *propname,
                   ApiPtr *userptr,
                   bool compact,
                   bool multiview)
{
  if (!ptr->data) {
    return;
  }

  ApiProp *prop = api_struct_find_prop(ptr, propname);
  if (!prop) {
    printf(
        "%s: prop not found: %s.%s\n", __func__, api_struct_id(ptr->type), propname);
    return;
  }

  if (api_prop_type(prop) != PROP_PTR) {
    printf("%s: expected ptr prop for %s.%s\n",
           __func__,
           api_struct_id(ptr->type),
           propname);
    return;
  }

  uiBlock *block = uiLayoutGetBlock(layout);

  ApiPtr imaptr = api_prop_ptr_get(ptr, prop);
  Img *img = static_cast<Img *>(imgptr.data);
  ImgUser *iuser = static_cast<ImgUser *>(userptr->data);

  Scene *scene = cxt_data_scene(C);
  dune_img_user_frame_calc(img, iuser, int(scene->r.cfra));

  uiLayoutSetCxtPtr(layout, "edit_img", &imaptr);
  uiLayoutSetCxtPtr(layout, "edit_img_user", userptr);

  SpaceImg *space_img = cxt_win_space_img(C);
  if (!compact && (space_im == nullptr || iuser != &space_img->iuser)) {
    uiTemplateId(layout,
                 C,
                 ptr,
                 propname,
                 ima ? nullptr : "IMG_OT_new",
                 "IMG_OT_open",
                 nullptr,
                 UI_TEMPLATE_ID_FILTER_ALL,
                 false,
                 nullptr);

    if (img != nullptr) {
      uiItemS(layout);
    }
  }

  if (img == nullptr) {
    return;
  }

  if (img->source == IMA_SRC_VIEWER) {
    /* Viewer imgs. */
    uiTemplateImgInfo(layout, C, img, iuser);

    if (img->type == IMG_TYPE_COMPOSITE) {
    }
    else if (img->type == IMG_TYPE_R_RESULT) {
      /* browse layer/passes */
      RenderResult *rr;
      const float dpi_fac = UI_SCALE_FAC;
      const int menus_width = 230 * dpi_fac;

      /* Use dune_img_acquire_renderresult so we get the correct slot in the menu. */
      rr = dune_img_acquire_renderresult(scene, img);
      uiblock_layer_pass_btns(layout, img, rr, iuser, menus_width, &ima->render_slot);
      dune_img_release_renderresult(scene, img);
    }

    return;
  }

  /* Set custom cb for prop updates. */
  ApiUpdateCb *cb = static_cast<ApiUpdateCb *>(mem_calloc(sizeof(ApiUpdateCb), "ApiUpdateCb"));
  cb->ptr = *ptr;
  cb->prop = prop;
  cb->iuser = iuser;
  ui_block_fn_set(block, api_update_cb, cb, nullptr);

  /* Disable editing if img was mod, to avoid losing changes. */
  const bool is_dirty = dune_img_is_dirty(img);
  if (is_dirty) {
    uiLayout *row = uiLayoutRow(layout, true);
    uiItemO(row, IFACE_("Save"), ICON_NONE, "img.save");
    uiItemO(row, IFACE_("Discard"), ICON_NONE, "img.reload");
    uiItemS(layout);
  }

  layout = uiLayoutColumn(layout, false);
  uiLayoutSetEnabled(layout, !is_dirty);
  uiLayoutSetPropDecorate(layout, false);

  /* Img src */
  {
    uiLayout *col = uiLayoutColumn(layout, false);
    uiLayoutSetPropSep(col, true);
    uiItemR(col, &imaptr, "source", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  /* Filepath */
  const bool is_packed = dune_img_has_packedfile(img);
  const bool no_filepath = is_packed && !dune_img_has_filepath(img);

  if ((img->source != IMG_SRC_GENERATED) && !no_filepath) {
    uiItemS(layout);

    uiLayout *row = uiLayoutRow(layout, true);
    if (is_packed) {
      uiItemO(row, "", ICON_PACKAGE, "img.unpack");
    }
    else {
      uiItemO(row, "", ICON_UGLYPACKAGE, "img.pack");
    }

    row = uiLayoutRow(row, true);
    uiLayoutSetEnabled(row, is_packed == false);

    prop = api_struct_find_prop(&imgptr, "filepath");
    uiDefAutoBtnR(block, &imgptr, prop, -1, "", ICON_NONE, 0, 0, 200, UI_UNIT_Y);
    uiItemO(row, "", ICON_FILEBROWSER, "img.file_browse");
    uiItemO(row, "", ICON_FILE_REFRESH, "img.reload");
  }

  /* Img layers and Info */
  if (img->src == IMG_SRC_GENERATED) {
    uiItemS(layout);

    /* Generated */
    uiLayout *col = uiLayoutColumn(layout, false);
    uiLayoutSetPropSep(col, true);

    uiLayout *sub = uiLayoutColumn(col, true);
    uiItemR(sub, &imgptr, "generated_width", UI_ITEM_NONE, "X", ICON_NONE);
    uiItemR(sub, &imgptr, "generated_height", UI_ITEM_NONE, "Y", ICON_NONE);

    uiItemR(col, &imgptr, "use_generated_float", UI_ITEM_NONE, nullptr, ICON_NONE);

    uiItemS(col);

    uiItemR(col, &imaptr, "generated_type", UI_ITEM_R_EXPAND, IFACE_("Type"), ICON_NONE);
    ImgTile *base_tile = dune_img_get_tile(img, 0);
    if (base_tile->gen_type == IMG_GENTYPE_BLANK) {
      uiItemR(col, &imaptr, "generated_color", UI_ITEM_NONE, nullptr, ICON_NONE);
    }
  }
  else if (compact == 0) {
    uiTemplateImgInfo(layout, C, ima, iuser);
  }
  if (ima->type == IMG_TYPE_MULTILAYER && ima->rr) {
    uiItemS(layout);

    const float dpi_fac = UI_SCALE_FAC;
    uiblock_layer_pass_btns(layout, ima, ima->rr, iuser, 230 * dpi_fac, nullptr);
  }

  if (dune_img_is_anim(ima)) {
    /* Anim */
    uiItemS(layout);

    uiLayout *col = uiLayoutColumn(layout, true);
    uiLayoutSetPropSep(col, true);

    uiLayout *sub = uiLayoutColumn(col, true);
    uiLayout *row = uiLayoutRow(sub, true);
    uiItemR(row, userptr, "frame_duration", UI_ITEM_NONE, IFACE_("Frames"), ICON_NONE);
    uiItemO(row, "", ICON_FILE_REFRESH, "IMG_OT_match_movie_length");

    uiItemR(sub, userptr, "frame_start", UI_ITEM_NONE, IFACE_("Start"), ICON_NONE);
    uiItemR(sub, userptr, "frame_offset", UI_ITEM_NONE, nullptr, ICON_NONE);

    uiItemR(col, userptr, "use_cyclic", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, userptr, "use_auto_refresh", UI_ITEM_NONE, nullptr, ICON_NONE);

    if (img->src == IMG_SRC_MOVIE && compact == 0) {
      uiItemR(col, &imgptr, "use_deinterlace", UI_ITEM_NONE, IFACE_("Deinterlace"), ICON_NONE);
    }
  }

  /* Multiview */
  if (multiview && compact == 0) {
    if ((scene->r.scemode & R_MULTIVIEW) != 0) {
      uiItemS(layout);

      uiLayout *col = uiLayoutColumn(layout, false);
      uiLayoutSetPropSep(col, true);
      uiItemR(col, &imhptr, "use_multiview", UI_ITEM_NONE, nullptr, ICON_NONE);

      if (api_bool_get(&imgptr, "use_multiview")) {
        uiTemplateImgViews(layout, &imaptr);
      }
    }
  }

  /* Color-space and alpha. */
  {
    uiItemS(layout);

    uiLayout *col = uiLayoutColumn(layout, false);
    uiLayoutSetPropSep(col, true);
    uiTemplateColorspaceSettings(col, &imaptr, "colorspace_settings");

    if (compact == 0) {
      if (img->source != IMG_SRC_GENERATED) {
        if (dune_img_has_alpha(ima)) {
          uiLayout *sub = Ra(col, false);
          uiItemR(sub, &imaptr, "alpha_mode", UI_ITEM_NONE, IFACE_("Alpha"), ICON_NONE);

          bool is_data = imbuf_colormanagement_space_name_is_data(img->colorspace_settings.name);
          uiLayoutSetActive(sub, !is_data);
        }

        if (ima && iuser) {
          void *lock;
          ImBuf *ibuf = dune_img_acquire_ibuf(img, iuser, &lock);

          if (ibuf && ibuf->float_buffer.data && (ibuf->flags & IB_halffloat) == 0) {
            uiItemR(col, &imaptr, "use_half_precision", UI_ITEM_NONE, nullptr, ICON_NONE);
          }
          dune_img_release_ibuf(img, ibuf, lock);
        }
      }

      uiItemR(col, &imgptr, "use_view_as_render", UI_ITEM_NONE, nullptr, ICON_NONE);
      uiItemR(col, &imgptr, "seam_margin", UI_ITEM_NONE, nullptr, ICON_NONE);
    }
  }

  ui_block_fn_set(block, nullptr, nullptr, nullptr);
}

void uiTemplateImgSettings(uiLayout *layout, ApiPtr *imfptr, bool color_management)
{
  ImgFormatData *imf = static_cast<ImgFormatData *>(imfptr->data);
  Id *id = imfptr->owner_id;
  const int depth_ok = dune_imtype_valid_depths(imf->imtype);
  /* some settings depend on this being a scene that's rendered */
  const bool is_render_out = (id && GS(id->name) == ID_SCE);

  uiLayout *col;

  col = uiLayoutColumn(layout, false);

  uiLayoutSetPropSep(col, true);
  uiLayoutSetPropDecorate(col, false);

  uiItemR(col, imfptr, "file_format", UI_ITEM_NONE, nullptr, ICON_NONE);

  /* Multi-layer always saves raw unmodified channels. */
  if (imf->imtype != R_IMF_IMTYPE_MULTILAYER) {
    uiItemR(uiLayoutRow(col, true),
            imfptr,
            "color_mode",
            UI_ITEM_R_EXPAND,
            IFACE_("Color"),
            ICON_NONE);
  }

  /* only display depth setting if multiple depths can be used */
  if (ELEM(depth_ok,
           R_IMF_CHAN_DEPTH_1,
           R_IMF_CHAN_DEPTH_8,
           R_IMF_CHAN_DEPTH_10,
           R_IMF_CHAN_DEPTH_12,
           R_IMF_CHAN_DEPTH_16,
           R_IMF_CHAN_DEPTH_24,
           R_IMF_CHAN_DEPTH_32) == 0)
  {
    uiItemR(uiLayoutRow(col, true), imfptr, "color_depth", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
  }

  if (dune_imtype_supports_quality(imf->imtype)) {
    uiItemR(col, imfptr, "quality", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (dune_imtype_supports_compress(imf->imtype)) {
    uiItemR(col, imfptr, "compression", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (ELEM(imf->imtype, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER)) {
    uiItemR(col, imfptr, "exr_codec", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (is_render_out && ELEM(imf->imtype, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER)) {
    uiItemR(col, imfptr, "use_preview", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (imf->imtype == R_IMF_IMTYPE_JP2) {
    uiItemR(col, imfptr, "jpeg2k_codec", UI_ITEM_NONE, nullptr, ICON_NONE);

    uiItemR(col, imfptr, "use_jpeg2k_cinema_preset", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, imfptr, "use_jpeg2k_cinema_48", UI_ITEM_NONE, nullptr, ICON_NONE);

    uiItemR(col, imfptr, "use_jpeg2k_ycc", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (imf->imtype == R_IMF_IMTYPE_DPX) {
    uiItemR(col, imfptr, "use_cineon_log", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  if (imf->imtype == R_IMF_IMTYPE_CINEON) {
#if 1
    uiItemL(col, TIP_("Hard coded Non-Linear, Gamma:1.7"), ICON_NONE);
#else
    uiItemR(col, imfptr, "use_cineon_log", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, imfptr, "cineon_black", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, imfptr, "cineon_white", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, imfptr, "cineon_gamma", UI_ITEM_NONE, nullptr, ICON_NONE);
#endif
  }

  if (imf->imtype == R_IMF_IMTYPE_TIFF) {
    uiItemR(col, imfptr, "tiff_codec", UI_ITEM_NONE, nullptr, ICON_NONE);
  }

  /* Override color mgmt */
  if (color_management) {
    uiItemS(col);
    uiItemR(col, imfptr, "color_management", UI_ITEM_NONE, nullptr, ICON_NONE);

    if (imf->color_management == R_IMF_COLOR_MANAGEMENT_OVERRIDE) {
      if (dune_imtype_requires_linear_float(imf->imtype)) {
        ApiPtr linear_settings_ptr = api_ptr_get(imfptr, "linear_colorspace_settings");
        uiItemR(col, &linear_settings_ptr, "name", UI_ITEM_NONE, IFACE_("Color Space"), ICON_NONE);
      }
      else {
        ApiPtr display_settings_ptr = api_ptr_get(imfptr, "display_settings");
        uiItemR(col, &display_settings_ptr, "display_device", UI_ITEM_NONE, nullptr, ICON_NONE);
        uiTemplateColormanagedViewSettings(col, nullptr, imfptr, "view_settings");
      }
    }
  }
}

void uiTemplateImgStereo3d(uiLayout *layout, ApiPtr *stereo3d_format_ptr)
{
  Stereo3dFormat *stereo3d_format = static_cast<Stereo3dFormat *>(stereo3d_format_ptr->data);
  uiLayout *col;

  col = uiLayoutColumn(layout, false);
  uiItemR(col, stereo3d_format_ptr, "display_mode", UI_ITEM_NONE, nullptr, ICON_NONE);

  switch (stereo3d_format->display_mode) {
    case S3D_DISPLAY_ANAGLYPH: {
      uiItemR(col, stereo3d_format_ptr, "anaglyph_type", UI_ITEM_NONE, nullptr, ICON_NONE);
      break;
    }
    case S3D_DISPLAY_INTERLACE: {
      uiItemR(col, stereo3d_format_ptr, "interlace_type", UI_ITEM_NONE, nullptr, ICON_NONE);
      uiItemR(col, stereo3d_format_ptr, "use_interlace_swap", UI_ITEM_NONE, nullptr, ICON_NONE);
      break;
    }
    case S3D_DISPLAY_SIDEBYSIDE: {
      uiItemR(
          col, stereo3d_format_ptr, "use_sidebyside_crosseyed", UI_ITEM_NONE, nullptr, ICON_NONE);
      ATTR_FALLTHROUGH;
    }
    case S3D_DISPLAY_TOPBOTTOM: {
      uiItemR(col, stereo3d_format_ptr, "use_squeezed_frame", UI_ITEM_NONE, nullptr, ICON_NONE);
      break;
    }
  }
}

static void uiTemplateViewsFormat(uiLayout *layout,
                                  ApiPtr *ptr,
                                  ApiPtr *stereo3d_format_ptr)
{
  uiLayout *col;

  col = uiLayoutColumn(layout, false);

  uiLayoutSetPropSep(col, true);
  uiLayoutSetPropDecorate(col, false);

  uiItemR(col, ptr, "views_format", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);

  if (stereo3d_format_ptr && api_enum_get(ptr, "views_format") == R_IMF_VIEWS_STEREO_3D) {
    uiTemplateImgStereo3d(col, stereo3d_format_ptr);
  }
}

void uiTemplateImgViews(uiLayout *layout, ApiPtr *imgptr)
{
  Img *img = static_cast<Img *>(imgptr->data);

  if (ima->type != IMG_TYPE_MULTILAYER) {
    ApiProp *prop;
    ApiPtr stereo3d_format_ptr;

    prop = api_struct_find_prop(imgptr, "stereo_3d_format");
    stereo3d_format_ptr = api_prop_ptr_get(imgptr, prop);

    uiTemplateViewsFormat(layout, imgptr, &stereo3d_format_ptr);
  }
  else {
    uiTemplateViewsFormat(layout, imgptr, nullptr);
  }
}

void uiTemplateImgFormatViews(uiLayout *layout, ApiPtr *imfptr, ApiPtr *ptr)
{
  ImgFormatData *imf = static_cast<ImgFormatData *>(imfptr->data);

  if (ptr != nullptr) {
    uiItemR(layout, ptr, "use_multiview", UI_ITEM_NONE, nullptr, ICON_NONE);
    if (!api_bool_get(ptr, "use_multiview")) {
      return;
    }
  }

  if (imf->imtype != R_IMF_IMTYPE_MULTILAYER) {
    ApiProp *prop;
    ApiPtr stereo3d_format_ptr;

    prop = api_struct_find_prop(imfptr, "stereo_3d_format");
    stereo3d_format_ptr = api_prop_ptr_get(imfptr, prop);

    uiTemplateViewsFormat(layout, imfptr, &stereo3d_format_ptr);
  }
  else {
    uiTemplateViewsFormat(layout, imfptr, nullptr);
  }
}

void uiTemplateImgLayers(uiLayout *layout, Cxt *C, Img *ima, ImgUser *iuser)
{
  Scene *scene = cxt_data_scene(C);

  /* render layers and passes */
  if (ima && iuser) {
    RenderResult *rr;
    const float dpi_fac = UI_SCALE_FAC;
    const int menus_width = 160 * dpi_fac;
    const bool is_render_result = (ima->type == IMG_TYPE_R_RESULT);

    /* Use dune_im_acquire_renderresult so we get the correct slot in the menu. */
    rr = dune_img_acquire_renderresult(scene, ima);
    uiblock_layer_pass_btns(
        layout, img, rr, iuser, menus_width, is_render_result ? &img->render_slot : nullptr);
    dune_img_release_renderresult(scene, ima);
  }
}

void uiTemplateImgInfo(uiLayout *layout, Cxt *C, Img *img, ImgUser *iuser)
{
  if (img == nullptr || iuser == nullptr) {
    return;
  }

  /* Acquire image buffer. */
  void *lock;
  ImBuf *ibuf = dune_img_acquire_ibuf(img, iuser, &lock);

  uiLayout *col = uiLayoutColumn(layout, true);
  uiLayoutSetAlignment(col, UI_LAYOUT_ALIGN_RIGHT);

  if (ibuf == nullptr) {
    uiItemL(col, TIP_("Can't Load Img"), ICON_NONE);
  }
  else {
    char str[MAX_IMAGE_INFO_LEN] = {0};
    const int len = MAX_IMG_INFO_LEN;
    int ofs = 0;

    ofs += lib_snprintf_rlen(str + ofs, len - ofs, TIP_("%d \u00D7 %d, "), ibuf->x, ibuf->y);

    if (ibuf->float_buffer.data) {
      if (ibuf->channels != 4) {
        ofs += lib_snprintf_rlen(
            str + ofs, len - ofs, TIP_("%d float channel(s)"), ibuf->channels);
      }
      else if (ibuf->planes == R_IMF_PLANES_RGBA) {
        ofs += lib_strncpy_rlen(str + ofs, TIP_(" RGBA float"), len - ofs);
      }
      else {
        ofs += lib_strncpy_rlen(str + ofs, TIP_(" RGB float"), len - ofs);
      }
    }
    else {
      if (ibuf->planes == R_IMF_PLANES_RGBA) {
        ofs += lib_strncpy_rlen(str + ofs, TIP_(" RGBA byte"), len - ofs);
      }
      else {
        ofs += lib_strncpy_rlen(str + ofs, TIP_(" RGB byte"), len - ofs);
      }
    }

    eGPUTextureFormat texture_format = imbuf_gpu_get_texture_format(
        ibuf, img->flag & IMG_HIGH_BITDEPTH, ibuf->planes >= 8);
    const char *texture_format_description = gpu_texture_format_name(texture_format);
    ofs += lib_snprintf_rlen(str + ofs, len - ofs, TIP_(",  %s"), texture_format_description);

    uiItemL(col, str, ICON_NONE);
  }

  /* Frame number, even if we can't load the img. */
  if (ELEM(ima->source, IMG_SRC_SEQ, IMG_SRC_MOVIE)) {
    /* don't use iuser->framenr directly because it may not be updated if auto-refresh is off */
    Scene *scene = cxt_data_scene(C);
    const int framenr = dune_img_user_frame_get(iuser, scene->r.cfra, nullptr);
    char str[MAX_IMG_INFO_LEN];
    int duration = 0;

    if (img->src == IMA_SRC_MOVIE && dune_img_has_anim(img)) {
      anim *anim = ((ImgAnim *)ima->anims.first)->anim;
      if (anim) {
        duration = imbuf_anim_get_duration(anim, IMB_TC_RECORD_RUN);
      }
    }

    if (duration > 0) {
      /* Movie duration */
      SNPRINTF(str, TIP_("Frame %d / %d"), framenr, duration);
    }
    else if (img->src == IMG_SRC_SEQ && ibuf) {
      /* Img seq frame number + filename */
      const char *filename = lib_path_basename(ibuf->filepath);
      SNPRINTF(str, TIP_("Frame %d: %s"), framenr, filename);
    }
    else {
      /* Frame number */
      SNPRINTF(str, TIP_("Frame %d"), framenr);
    }

    uiItemL(col, str, ICON_NONE);
  }

  dune_img_release_ibuf(ima, ibuf, lock);
}

#undef MAX_IMG_INFO_LEN

static bool metadata_pnl_cxt_poll(const Cxt *C, PnlType * /*pt*/)
{
  SpaceImg *space_img = cxt_win_space_img(C);
  return space_img != nullptr && space_img->img != nullptr;
}

static void metadata_pnl_cxt_drw(const Cxt *C, Pnl *pnl)
{
  void *lock;
  SpaceImg *space_img = cxt_win_space_img(C);
  Img *img = space_img->img;
  ImBuf *ibuf = dune_img_acquire_ibuf(img, &space_img->iuser, &lock);
  if (ibuf != nullptr) {
    ed_rgn_img_metadata_pnl_drw(ibuf, pnl->layout);
  }
  dune_img_release_ibuf(img, ibuf, lock);
}

void img_btns_register(ARgnType *art)
{
  PnlType *pt;

  pt = static_cast<PnlType *>(mem_calloc(sizeof(PnlType), "spacetype img pnl metadata"));
  STRNCPY(pt->idname, "IMG_PT_metadata");
  STRNCPY(pt->label, N_("Metadata"));
  STRNCPY(pt->category, "Img");
  STRNCPY(pt->lang_cxt, LANG_CXT_DEFAULT);
  pt->order = 10;
  pt->poll = metadata_pnl_cxt_poll;
  pt->drw = metadata_pnl_cxt_drw;
  pt->flag |= PNL_TYPE_DEFAULT_CLOSED;
  lib_addtail(&art->pnltypes, pt);
}
