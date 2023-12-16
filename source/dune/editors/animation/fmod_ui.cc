/* UI for F-Mods
 * This file defines templates and some editing cbs needed by the interface for
 * F-Mods, as used by F-Curves in the Graph Editor and NLA-Strips in the NLA Editor. */
#include <cstring>

#include "types_anim.h"
#include "types_scene.h"
#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lang.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "ui.hh"
#include "ui_resources.hh"

#include "ed_anim_api.hh"
#include "ed_undo.hh"

#include "graph.hh"

using PnlDrwFn = void (*)(const Cxt *, Pnl *);
static void fmod_pnl_header(const Cxt *C, Pnl *pnl);

/* Pnl Registering and Pnl Cbs */
/* Get the list of FMods from the cxt (either the NLA or graph editor). */
static List *fmod_list_space_specific(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);

  if (area->spacetype == SPACE_GRAPH) {
    FCurve *fcu = anim_graph_cxt_fcurve(C);
    return &fcu->mods;
  }

  if (area->spacetype == SPACE_NLA) {
    NlaStrip *strip = anim_nla_cxt_strip(C);
    return &strip->mods;
  }

  /* This should not be called in any other space. */
  lib_assert(false);
  return nullptr;
}

/* Get a ptr to the pnl's FMod, and also its owner Id if \a r_owner_id is not nullptr.
 * Also in the graph editor, gray out the pnl if the FMod's FCurve has mods turned off. */
static ApiPtr *fmod_get_ptrs(const Cxt *C, const Pnl *pnl, Id **r_owner_id)
{
  ApiPtr *ptr = ui_pnl_custom_data_get(panel);

  if (r_owner_id != nullptr) {
    *r_owner_id = ptr->owner_id;
  }

  if (C != nullptr && cxt_win_space_graph(C)) {
    FCurve *fcu = anim_graph_cxt_fcurve(C);
    uiLayoutSetActive(pnl->layout, !(fcu->flag & FCURVE_MOD_OFF));
  }

  return ptr;
}

/* Move an FMod to the index it's moved to after a drag and drop. */
static void fmod_reorder(Cxt *C, Pnl *pnl, int new_index)
{
  Id *owner_id;
  ApiPtr *ptr = fmod_get_ptrs(nullptr, pnl, &owner_id);
  FMod *fcm = static_cast<FMod *>(ptr->data);
  const FModTypeInfo *fmi = get_fmod_typeinfo(fcm->type);

  /* Cycles mod has to be the first, so make sure it's kept that way. */
  if (fmi->requires_flag & FMI_REQUIRES_ORIGINAL_DATA) {
    win_report(RPT_ERROR, "Mod requires original data");
    return;
  }

  List *mods = fmod_list_space_specific(C);

  /* Again, make sure we don't move a mod before a cycles mod. */
  FMod *fcm_first = static_cast<FMod *>(mods->first);
  const FModTypeInfo *fmi_first = get_fmod_typeinfo(fcm_first->type);
  if (fmi_first->requires_flag & FMI_REQUIRES_ORIGINAL_DATA && new_index == 0) {
    win_report(RPT_ERROR, "Mod requires original data");
    return;
  }

  int current_index = lib_findindex(mods, fcm);
  lib_assert(current_index >= 0);
  lib_assert(new_index >= 0);

  /* Don't do anything if the drag didn't change the index. */
  if (current_index == new_index) {
    return;
  }

  /* Move the FMod in the list. */
  lib_list_link_move(mods, fcm, new_index - current_index);

  ed_undo_push(C, "Reorder F-Curve Mod");

  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);
  graph_id_tag_update(owner_id, ID_RECALC_ANIM);
}

static short get_fmod_expand_flag(const Cxt * /*C*/, Pnl *pnl)
{
  ApiPtr *ptr = fmod_get_ptrs(nullptr, pnl, nullptr);
  FMod *fcm = (FMod *)ptr->data;

  return fcm->ui_expand_flag;
}

static void set_fmod_expand_flag(const Cxt * /*C*/, Pnl *pnl, short expand_flag)
{
  ApiPtr *ptr = fmod_get_ptrs(nullptr, pnl, nullptr);
  FMod *fcm = (FMod *)ptr->data;

  fcm->ui_expand_flag = expand_flag;
}

static PnlType *fmod_pnl_register(ARgnType *rgn_type,
                                  eFModTypes type,
                                  PnlDrwFn drw,
                                  PnlTypePollFn poll,
                                  const char *id_prefix)
{
  PnlType *pnl_type = static_cast<PnlType *>(mem_calloc(sizeof(PnlType), __func__));

  /* Intentionally leave the label field blank. The header is filled with btns. */
  const FModTypeInfo *fmi = get_fmod_typeinfo(type);
  SNPRINTF(pnl_type->idname, "%s_PT_%s", id_prefix, fmi->name);
  STRNCPY(pnl_type->category, "Mods");
  STRNCPY(pnl_type->lang_cxt, LANG_CXT_DEFAULT_BPYAPI);

  pnl_type->drw_header = fmod_pnl_header;
  pnl_type->drw = drw;
  pnl_type->poll = poll;

  /* Give the pnl the special flag that says it was built here and corresponds to a
   * modifier rather than a PnlType. */
  pnl_type->flag = PNL_TYPE_HEADER_EXPAND | PNL_TYPE_INSTANCED;
  pnl_type->reorder = fmod_reorder;
  pnl_type->get_list_data_expand_flag = get_fmod_expand_flag;
  pnl_type->set_list_data_expand_flag = set_fmod_expand_flag;

  lib_addtail(&rgn_type->pnltypes, pnl_type);

  return pnl_type;
}

/* Add a child pnl to the parent.
 * To create the pnl type's idname, appends the name arg to parent's
 * idname. */
static PnlType *fmod_subpnl_register(ARgnType *rgn_type,
                                     const char *name,
                                     const char *label,
                                     PnlDrwFn drw_header,
                                     PnlDrwFn drw,
                                     PnlTypePollFn poll,
                                     PnlType *parent)
{
  PnlType *pnl_type = static_cast<PnlType *>(mem_calloc(sizeof(PnlType), __func__));

  SNPRINTF(pnl_type->idname, "%s_%s", parent->idname, name);
  STRNCPY(pnl_type->label, label);
  STRNCPY(pnl_type->category, "Mods");
  STRNCPY(pnl_type->lang_cxt, LANG_CXT_DEFAULT_BPYAPI);

  pnl_type->drw_header = drw_header;
  pnl_type->drw = drw;
  pnl_type->poll = poll;
  pnl_type->flag = PNL_TYPE_DEFAULT_CLOSED;

  lib_assert(parent != nullptr);
  STRNCPY(panel_type->parent_id, parent->idname);
  pnl_type->parent = parent;
  lib_addtail(&parent->children, lib_genericNode(pnl_type));
  lib_addtail(&rgn_type->pnltypes, pnl_type);

  return pnl_type;
}

/* General UI Cbs and Drwing */

#define B_REDR 1
#define B_FMOD_REDRW 20

/* Cb to remove the given mod. */
struct FModDelCxt {
  Id *owner_id;
  List *mods;
};

static void del_fmod_cb(Cxt *C, void *cxt_v, void *fcm_v)
{
  FModDelCxt *cxt = (FModDelCxt *)cxt_v;
  List *mods = cxt->mods;
  FMod *fcm = (FMod *)fcm_v;

  /* remove the given F-Mod from the active mod-stack */
  remove_fmod(mods, fcm);

  ed_undo_push(C, "Del F-Curve Mod");

  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);
  graph_id_tag_update(ctx->owner_id, ID_RECALC_ANIM);
}

static void fmod_influence_drw(uiLayout *layout, ApiPtr *ptr)
{
  FMod *fcm = (FMod *)ptr->data;
  uiItemS(layout);

  uiLayout *row = uiLayoutRowWithHeading(layout, true, IFACE_("Influence"));
  uiItemR(row, ptr, "use_influence", UI_ITEM_NONE, "", ICON_NONE);
  uiLayout *sub = uiLayoutRow(row, true);

  uiLayoutSetActive(sub, fcm->flag & FMOD_FLAG_USEINFLUENCE);
  uiItemR(sub, ptr, "influence", UI_ITEM_NONE, "", ICON_NONE);
}

static void fmod_frame_range_header_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *layout = pnl->layout;

  ApiPtr *ptr = fmod_get_ptrs(C, pnl, nullptr);

  uiItemR(layout, ptr, "use_restricted_range", UI_ITEM_NONE, nullptr, ICON_NONE);
}

static void fmod_frame_range_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *col;
  uiLayout *layout = pnl->layout;

  ApiPtr *ptr = fmod_get_ptrs(C, pnl, nullptr);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  FMod *fcm = (FMod *)ptr->data;
  uiLayoutSetActive(layout, fcm->flag & FMOD_FLAG_RANGERESTRICT);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "frame_start", UI_ITEM_NONE, IFACE_("Start"), ICON_NONE);
  uiItemR(col, ptr, "frame_end", UI_ITEM_NONE, IFACE_("End"), ICON_NONE);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "blend_in", UI_ITEM_NONE, IFACE_("Blend In"), ICON_NONE);
  uiItemR(col, ptr, "blend_out", UI_ITEM_NONE, IFACE_("Out"), ICON_NONE);
}

static void fmod_pnl_header(const Cxt *C, Pnl *pnl)
{
  uiLayout *layout = pnl->layout;

  Id *owner_id;
  ApiPtr *ptr = fmod_get_ptrs(C, pnl, &owner_id);
  FMod *fcm = (FMod *)ptr->data;
  const FModTypeInfo *fmi = fmod_get_typeinfo(fcm);

  uiBlock *block = uiLayoutGetBlock(layout);

  uiLayout *sub = uiLayoutRow(layout, true);

  /* Checkbox for 'active' status (for now). */
  uiItemR(sub, ptr, "active", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);

  /* Name. */
  if (fmi) {
    uiItemR(sub, ptr, "name", UI_ITEM_NONE, "", ICON_NONE);
  }
  else {
    uiItemL(sub, IFACE_("<Unknown Mod>"), ICON_NONE);
  }
  /* Right align. */
  sub = uiLayoutRow(layout, true);
  uiLayoutSetAlignment(sub, UI_LAYOUT_ALIGN_RIGHT);
  uiLayoutSetEmboss(sub, UI_EMBOSS_NONE);

  /* 'Mute' btn. */
  uiItemR(sub, ptr, "mute", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);

  /* Del btn. */
  uiBtn *btn = uiDefIconBtn(block,
                            UI_BTYPE_BTN,
                            B_REDR,
                            ICON_X,
                            0,
                            0,
                            UI_UNIT_X,
                            UI_UNIT_Y,
                            nullptr,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                            TIP_("Del Mod"));
  FModDelCxt *ctx = static_cast<FModDelCxt *>(
      mem_malloc(sizeof(FModDelCxt), __func__));
  ctx->owner_id = owner_id;
  ctx->mods = fmod_list_space_specific(C);
  lib_assert(cxt->mods != nullptr);

  ui_btn_fn_set(btn, del_fmod_cb, cxt, fcm);

  uiItemS(layout);
}

/* Generator Mod */
static void generator_pnl_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *layout = pnl->layout;

  Id *owner_id;
  ApiPtr *ptr = fmod_get_ptrs(C, pnl, &owner_id);
  FMod *fcm = (FMod *)ptr->data;
  FMod_Generator *data = (FModGenerator *)fcm->data;

  uiItemR(layout, ptr, "mode", UI_ITEM_NONE, "", ICON_NONE);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiItemR(layout, ptr, "use_additive", UI_ITEM_NONE, nullptr, ICON_NONE);

  uiItemR(layout, ptr, "poly_order", UI_ITEM_NONE, IFACE_("Order"), ICON_NONE);

  ApiProp *prop = api_struct_find_prop(ptr, "coefficients");
  uiLayout *col = uiLayoutColumn(layout, true);
  switch (data->mode) {
    case FCM_GENERATOR_POLYNOMIAL: /* Polynomial expression. */
    {

      char xval[32];

      /* The first val gets a "Coefficient" label. */
      STRNCPY(xval, N_("Coefficient"));

      for (int i = 0; i < data->arraysize; i++) {
        uiItemFullR(col, ptr, prop, i, 0, UI_ITEM_NONE, IFACE_(xval), ICON_NONE);
        SNPRINTF(xval, "x^%d", i + 1);
      }
      break;
    }
    case FCM_GENERATOR_POLYNOMIAL_FACTORISED: /* Factorized polynomial expression */
    {
      {
        /* Add column labels above the buttons to prevent confusion.
         * Fake the prop split layout, otherwise the labels use the full row. */
        uiLayout *split = uiLayoutSplit(col, 0.4f, false);
        uiLayoutColumn(split, false);
        uiLayout *title_col = uiLayoutColumn(split, false);
        uiLayout *title_row = uiLayoutRow(title_col, true);
        uiItemL(title_row, CXT_IFACE_(LANG_CXT_ID_ACTION, "A"), ICON_NONE);
        uiItemL(title_row, CXT_IFACE_(LANG_CXT_ID_ACTION, "B"), ICON_NONE);
      }

      uiLayout *first_row = uiLayoutRow(col, true);
      uiItemFullR(first_row, ptr, prop, 0, 0, UI_ITEM_NONE, IFACE_("y = (Ax + B)"), ICON_NONE);
      uiItemFullR(first_row, ptr, prop, 1, 0, UI_ITEM_NONE, "", ICON_NONE);
      for (int i = 2; i < data->arraysize - 1; i += 2) {
        /* \u2715 is the multiplication symbol. */
        uiLayout *row = uiLayoutRow(col, true);
        uiItemFullR(row, ptr, prop, i, 0, UI_ITEM_NONE, IFACE_("\u2715 (Ax + B)"), ICON_NONE);
        uiItemFullR(row, ptr, prop, i + 1, 0, UI_ITEM_NONE, "", ICON_NONE);
      }
      break;
    }
  }

  fmod_influence_drw(layout, ptr);
}

static void pnl_register_generator(ARgnType *rgn_type,
                                   const char *id_prefix,
                                   PnlTypePollFn poll_fn)
{
  PnlType *pnl_type = fmod_pnl_register(
      rgn_type, FMOD_TYPE_GENERATOR, generator_pnl_dre, poll_fn, id_prefix);
  fmod_subpnl_register(rgn_type,
                      "frame_range",
                      "",
                      fmod_frame_range_header_drw,
                      fmod_frame_range_drw,
                      poll_fn,
                      pnl_type);
}

/* Fn Generator Mod */

static void fn_generator_pnl_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *col;
  uiLayout *layout = panel->layout;

  PointerRNA *ptr = fmodifier_get_pointers(C, panel, nullptr);

  uiItemR(layout, ptr, "function_type", UI_ITEM_NONE, "", ICON_NONE);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "use_additive", UI_ITEM_NONE, nullptr, ICON_NONE);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "amplitude", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "phase_multiplier", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "phase_offset", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "value_offset", UI_ITEM_NONE, nullptr, ICON_NONE);

  fmod_influence_drw(layout, ptr);
}

static void pnl_register_fn_generator(ARgnType *rgn_type,
                                      const char *id_prefix,
                                      PnlTypePollFn poll_fn)
{
  PnlType *pnl_type = fmod_pnl_register(
      rgn_type, FMOD_TYPE_FN_GENERATOR, fn_generator_pnl_drw, poll_fn, id_prefix);
  fmod_subpbl_register(rgn_type,
                       "frame_range",
                       "",
                       fmod_frame_range_header_drw,
                       fmod_frame_range_drw,
                       poll_fn,
                       pnl_type);
}

/* Cycles Mod */
static void cycles_pnl_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *col;
  uiLayout *layout = pnl->layout;

  ApiPtr *ptr = fmod_get_ptrs(C, pnl, nullptr);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  /* Before. */
  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "mode_before", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "cycles_before", UI_ITEM_NONE, IFACE_("Count"), ICON_NONE);

  /* After. */
  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "mode_after", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "cycles_after", UI_ITEM_NONE, IFACE_("Count"), ICON_NONE);

  fmod_influence_drw(layout, ptr);
}

static void pnl_register_cycles(ARgnType *rgn_type,
                                const char *id_prefix,
                                PnlTypePollFn poll_fn)
{
  PnlType *pnl_type = fmod_pnl_register(
      rgn_type, FMOD_TYPE_CYCLES, cycles_pnl_drw, poll_fn, id_prefix);
  fmod_subpnl_register(rgn_type,
                       "frame_range",
                        "",
                        fmod_frame_range_header_drw,
                        fmod_frame_range_drw,
                        poll_fn,
                        pnl_type);
}

/* Noise Mod */
static void noise_pnl_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *col;
  uiLayout *layout = pnl->layout;

  ApiPtr *ptr = fmod_get_ptrs(C, pnl, nullptr);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiItemR(layout, ptr, "blend_type", UI_ITEM_NONE, nullptr, ICON_NONE);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "scale", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "strength", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "offset", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "phase", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "depth", UI_ITEM_NONE, nullptr, ICON_NONE);

  fmod_influence_drw(layout, ptr);
}

static void pno_register_noise(ARgnType *rgn_type,
                               const char *id_prefix,
                               PnlTypePollFn poll_fn)
{
  PnlType *pnl_type = fmod_pnl_register(
      rgn_type, FMOD_TYPE_NOISE, noise_pnl_drw, poll_fn, id_prefix);
  fmod_subpnl_register(rgn_type,
                              "frame_range",
                              "",
                              fmod_frame_range_header_drw,
                              fmod_frame_range_drw,
                              poll_fn,
                              pnl_type);
}

/* Envelope Mod */
static void fmod_envelope_addpoint_cb(bContext *C, void *fcm_dv, void * /*arg*/)
{
  Scene *scene = cxt_data_scene(C);
  FModEnvelope *env = (FModEnvelope *)fcm_dv;
  FCMEnvelopeData *fedn;
  FCMEnvelopeData fed;

  /* init template data */
  fed.min = -1.0f;
  fed.max = 1.0f;
  fed.time = float(scene->r.cfra); /* make this int for ease of use? */
  fed.f1 = fed.f2 = 0;

  /* check that no data exists for the current frame... */
  if (env->data) {
    bool exists;
    int i = dune_fcm_envelope_find_index(env->data, float(scene->r.cfra), env->totvert, &exists);

    /* binarysearch_...() will set exists by default to 0,
     * so if it is non-zero, that means that the point exists already */
    if (exists) {
      return;
    }

    /* add new */
    fedn = static_cast<FCMEnvelopeData *>(
        mem_calloc((env->totvert + 1) * sizeof(FCMEnvelopeData), "FCMEnvelopeData"));

    /* add the points that should occur before the point to be pasted */
    if (i > 0) {
      memcpy(fedn, env->data, i * sizeof(FCMEnvelopeData));
    }

    /* add point to paste at index i */
    *(fedn + i) = fed;

    /* add the points that occur after the point to be pasted */
    if (i < env->totvert) {
      memcpy(fedn + i + 1, env->data + i, (env->totvert - i) * sizeof(FCM_EnvelopeData));
    }

    /* replace (+ free) old with new */
    mem_free(env->data);
    env->data = fedn;

    env->totvert++;
  }
  else {
    env->data = static_cast<FCMEnvelopeData *>(
        mem_calloc(sizeof(FCMEnvelopeData), "FCMEnvelopeData"));
    *(env->data) = fed;

    env->totvert = 1;
  }
}

/* cb to remove envelope data point */
/* TODO: should we have a separate file for things like this? */
static void fmod_envelope_delpoint_cb(Cxt * /*C*/, void *fcm_dv, void *ind_v)
{
  FModEnvelope *env = (FModEnvelope *)fcm_dv;
  FCMEnvelopeData *fedn;
  int index = PTR_AS_INT(ind_v);

  /* check that no data exists for the current frame... */
  if (env->totvert > 1) {
    /* allocate a new smaller array */
    fedn = static_cast<FCMEnvelopeData *>(
        mem_calloc(sizeof(FCMEnvelopeData) * (env->totvert - 1), "FCMEnvelopeData"));

    memcpy(fedn, env->data, sizeof(FCMEnvelopeData) * (index));
    memcpy(fedn + index,
           env->data + (index + 1),
           sizeof(FCMEnvelopeData) * ((env->totvert - index) - 1));

    /* free old array, and set the new */
    mem_free(env->data);
    env->data = fedn;
    env->totvert--;
  }
  else {
    /* just free arr, since the only vert was deleted */
    MEM_SAFE_FREE(env->data);
    env->totvert = 0;
  }
}

/* drw settings for envelope mod */
static void envelope_pnl_drw(const Cxt *C, Pnl *pnl)
{
  uiLayout *row, *col;
  uiLayout *layout = pnl->layout;

  Id *owner_id;
  ApiPtr *ptr = fmod_get_ptrs(C, pnl, &owner_id);
  FMod *fcm = (FMod *)ptr->data;
  FModEnvelope *env = (FModEnvelope *)fcm->data;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  /* General settings. */
  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "ref_val", UI_ITEM_NONE, IFACE_("Ref"), ICON_NONE);
  uiItemR(col, ptr, "default_min", UI_ITEM_NONE, IFACE_("Min"), ICON_NONE);
  uiItemR(col, ptr, "default_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);

  /* Ctrl points list. */
  row = uiLayoutRow(layout, false);
  uiBlock *block = uiLayoutGetBlock(row);

  Btn *btn = Btn(block,
                 UI_BTYPE_BTN,
                 B_FMOD_REDRW,
                 IFACE_("Add Ctrl Point"),
                        0,
                        0,
                        7.5 * UI_UNIT_X,
                        UI_UNIT_Y,
                        nullptr,
                        0,
                        0,
                        0,
                        0,
                        TIP_("Add a new control-point to the envelope on the current frame"));
  ui_btn_fn_set(btn, fmod_envelope_addpoint_cb, env, nullptr);

  col = uiLayoutColumn(layout, false);
  uiLayoutSetPropSep(col, false);

  FCMEnvelopeData *fed = env->data;
  for (int i = 0; i < env->totvert; i++, fed++) {
    ApiPtr ctrl_ptr = api_ptr_create(owner_id, &ApiFModEnvelopeCtrlPoint, fed);

    /* get a new row to op on */
    row = uiLayoutRow(col, true);
    block = uiLayoutGetBlock(row);

    uiItemR(row, &ctrl_ptr, "frame", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(row, &ctrl_ptr, "min", UI_ITEM_NONE, IFACE_("Min"), ICON_NONE);
    uiItemR(row, &ctrl_ptr, "max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);

    btn = uiDefIconBtn(block,
                       UI_BTYPE_BTN,
                       B_FMOD_REDRW,
                       ICON_X,
                       0,
                       0,
                       0.9 * UI_UNIT_X,
                       UI_UNIT_Y,
                       nullptr,
                       0.0,
                       0.0,
                       0.0,
                       0.0,
                       TIP_("Delete envelope control point"));
    ui_btn_fn_set(btn, fmod_envelope_deletepoint_cb, env, POINTER_FROM_INT(i));
    ui_block_align_begin(block);
  }

  fmodifier_influence_draw(layout, ptr);
}

static void panel_register_envelope(ARegionType *region_type,
                                    const char *id_prefix,
                                    PanelTypePollFn poll_fn)
{
  PanelType *panel_type = fmodifier_panel_register(
      region_type, FMODIFIER_TYPE_ENVELOPE, envelope_panel_draw, poll_fn, id_prefix);
  fmodifier_subpanel_register(region_type,
                              "frame_range",
                              "",
                              fmodifier_frame_range_header_draw,
                              fmodifier_frame_range_draw,
                              poll_fn,
                              panel_type);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Limits Modifier
 * \{ */

static void limits_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *col, *row, *sub;
  uiLayout *layout = panel->layout;

  PointerRNA *ptr = fmodifier_get_pointers(C, panel, nullptr);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  /* Minimums. */
  col = uiLayoutColumn(layout, false);
  row = uiLayoutRowWithHeading(col, true, IFACE_("Minimum X"));
  uiItemR(row, ptr, "use_min_x", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_min_x"));
  uiItemR(sub, ptr, "min_x", UI_ITEM_NONE, "", ICON_NONE);

  row = uiLayoutRowWithHeading(col, true, IFACE_("Y"));
  uiItemR(row, ptr, "use_min_y", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_min_y"));
  uiItemR(sub, ptr, "min_y", UI_ITEM_NONE, "", ICON_NONE);

  /* Maximums. */
  col = uiLayoutColumn(layout, false);
  row = uiLayoutRowWithHeading(col, true, IFACE_("Maximum X"));
  uiItemR(row, ptr, "use_max_x", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_max_x"));
  uiItemR(sub, ptr, "max_x", UI_ITEM_NONE, "", ICON_NONE);

  row = uiLayoutRowWithHeading(col, true, IFACE_("Y"));
  uiItemR(row, ptr, "use_max_y", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_max_y"));
  uiItemR(sub, ptr, "max_y", UI_ITEM_NONE, "", ICON_NONE);

  fmodifier_influence_draw(layout, ptr);
}

static void pnl_register_limits(ARgnType *rgn_type,
                                  const char *id_prefix,
                                  PnlTypePollFn poll_fn)
{
  PanelType *panel_type = fmod_pnl_register(
      region_type, FMOD_TYPE_LIMITS, limits_panel_draw, poll_fn, id_prefix);
  fmodifier_subpnl_register(region_type,
                              "frame_range",
                              "",
                              fmodifier_frame_range_header_draw,
                              fmodifier_frame_range_draw,
                              poll_fn,
                              panel_type);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Stepped Interpolation Modifier
 * \{ */

static void stepped_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *col, *sub, *row;
  uiLayout *layout = panel->layout;

  PointerRNA *ptr = fmodifier_get_pointers(C, panel, nullptr);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  /* Stepping Settings. */
  col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "frame_step", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "frame_offset", UI_ITEM_NONE, nullptr, ICON_NONE);

  /* Start range settings. */
  row = uiLayoutRowWithHeading(layout, true, IFACE_("Start Frame"));
  uiItemR(row, ptr, "use_frame_start", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_frame_start"));
  uiItemR(sub, ptr, "frame_start", UI_ITEM_NONE, "", ICON_NONE);

  /* End range settings. */
  row = uiLayoutRowWithHeading(layout, true, IFACE_("End Frame"));
  uiItemR(row, ptr, "use_frame_end", UI_ITEM_NONE, "", ICON_NONE);
  sub = uiLayoutColumn(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_frame_end"));
  uiItemR(sub, ptr, "frame_end", UI_ITEM_NONE, "", ICON_NONE);

  fmodifier_influence_draw(layout, ptr);
}

static void panel_register_stepped(ARegionType *region_type,
                                   const char *id_prefix,
                                   PanelTypePollFn poll_fn)
{
  PanelType *panel_type = fmodifier_panel_register(
      region_type, FMODIFIER_TYPE_STEPPED, stepped_panel_draw, poll_fn, id_prefix);
  fmodifier_subpanel_register(region_type,
                              "frame_range",
                              "",
                              fmodifier_frame_range_header_draw,
                              fmodifier_frame_range_draw,
                              poll_fn,
                              panel_type);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Panel Creation
 * \{ */

void ANIM_fmodifier_panels(const bContext *C,
                           ID *owner_id,
                           ListBase *fmodifiers,
                           uiListPanelIDFromDataFunc panel_id_fn)
{
  ARegion *region = CTX_wm_region(C);

  bool panels_match = UI_panel_list_matches_data(region, fmodifiers, panel_id_fn);

  if (!panels_match) {
    UI_panels_free_instanced(C, region);
    LISTBASE_FOREACH (FModifier *, fcm, fmodifiers) {
      char panel_idname[MAX_NAME];
      panel_id_fn(fcm, panel_idname);

      PointerRNA *fcm_ptr = static_cast<PointerRNA *>(
          MEM_mallocN(sizeof(PointerRNA), "panel customdata"));
      *fcm_ptr = RNA_pointer_create(owner_id, &RNA_FModifier, fcm);

      UI_panel_add_instanced(C, region, &region->panels, panel_idname, fcm_ptr);
    }
  }
  else {
    /* Assuming there's only one group of instanced panels, update the custom data pointers. */
    Panel *panel = static_cast<Panel *>(region->panels.first);
    LISTBASE_FOREACH (FModifier *, fcm, fmodifiers) {

      /* Move to the next instanced panel corresponding to the next modifier. */
      while ((panel->type == nullptr) || !(panel->type->flag & PANEL_TYPE_INSTANCED)) {
        panel = panel->next;
        BLI_assert(panel !=
                   nullptr); /* There shouldn't be fewer panels than modifiers with UIs. */
      }

      PointerRNA *fcm_ptr = static_cast<PointerRNA *>(
          MEM_mallocN(sizeof(PointerRNA), "panel customdata"));
      *fcm_ptr = RNA_pointer_create(owner_id, &RNA_FModifier, fcm);
      UI_panel_custom_data_set(panel, fcm_ptr);

      panel = panel->next;
    }
  }
}

void ANIM_modifier_panels_register_graph_and_NLA(ARegionType *region_type,
                                                 const char *modifier_panel_prefix,
                                                 PanelTypePollFn poll_function)
{
  panel_register_generator(region_type, modifier_panel_prefix, poll_function);
  panel_register_fn_generator(region_type, modifier_panel_prefix, poll_function);
  panel_register_noise(region_type, modifier_panel_prefix, poll_function);
  panel_register_envelope(region_type, modifier_panel_prefix, poll_function);
  panel_register_limits(region_type, modifier_panel_prefix, poll_function);
  panel_register_stepped(region_type, modifier_panel_prefix, poll_function);
}

void ANIM_modifier_panels_register_graph_only(ARegionType *region_type,
                                              const char *modifier_panel_prefix,
                                              PanelTypePollFn poll_function)
{
  panel_register_cycles(region_type, modifier_panel_prefix, poll_function);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Copy / Paste Buffer Code
 *
 * For now, this is also defined in this file so that it can be shared between the graph editor
 * and the NLA editor.
 * \{ */

/* Copy/Paste Buffer itself (list of FModifier 's) */
static ListBase fmodifier_copypaste_buf = {nullptr, nullptr};

/* ---------- */

void ANIM_fmodifiers_copybuf_free()
{
  /* just free the whole buffer */
  free_fmodifiers(&fmodifier_copypaste_buf);
}

bool ANIM_fmodifiers_copy_to_buf(ListBase *modifiers, bool active)
{
  bool ok = true;

  /* sanity checks */
  if (ELEM(nullptr, modifiers, modifiers->first)) {
    return false;
  }

  /* copy the whole list, or just the active one? */
  if (active) {
    FModifier *fcm = find_active_fmodifier(modifiers);

    if (fcm) {
      FModifier *fcmN = copy_fmodifier(fcm);
      BLI_addtail(&fmodifier_copypaste_buf, fcmN);
    }
    else {
      ok = false;
    }
  }
  else {
    copy_fmodifiers(&fmodifier_copypaste_buf, modifiers);
  }

  /* did we succeed? */
  return ok;
}

bool anim_fmods_paste_from_buf(List *mods, bool replace, FCurve *curve)
{
  bool ok = false;

  /* sanity checks */
  if (mods == nullptr) {
    return false;
  }

  bool was_cyclic = curve && dune_fcurve_is_cyclic(curve);

  /* if replacing the list, free the existing mods */
  if (replace) {
    free_fmods(mods);
  }

  /* now copy over all the mods in the buf to the end of the list */
  LIST_FOREACH (FMod *, fcm, &fmod_copypaste_buf) {
    /* make a copy of it */
    FMod *fcmN = copy_fmodifier(fcm);

    fcmN->curve = curve;

    /* make sure the new one isn't active, otherwise the list may get several actives */
    fcmN->flag &= ~FMODIFIER_FLAG_ACTIVE;

    /* now add it to the end of the list */
    BLI_addtail(modifiers, fcmN);
    ok = true;
  }

  /* adding or removing the Cycles modifier requires an update to handles */
  if (curve && BKE_fcurve_is_cyclic(curve) != was_cyclic) {
    BKE_fcurve_handles_recalc(curve);
  }

  /* did we succeed? */
  return ok;
}

/** \} */
