/* Eyedropper (ID data-blocks)
 * Defines:
 * UI_OT_eyedropper_id */

#include "mem_guardedalloc.h"

#include "types_object.h"
#include "types_screen.h"
#include "types_space.h"

#include "lib_math_vector.h"
#include "lib_string.h"

#include "lang.h"

#include "dune_cxt.h"
#include "dune_idtype.h"
#include "dune_report.h"
#include "dune_screen.hh"

#include "api_access.hh"

#include "ui.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_outliner.hh"
#include "ed_screen.hh"
#include "ed_space_api.hh"
#include "ed_view3d.hh"

#include "eyedropper_intern.hh"
#include "interface_intern.hh"

/* DataDropper is only internal name to avoid confusion with other kinds of eye-droppers. */
struct DataDropper {
  ApiPtr ptr;
  ApiProp *prop;
  short idcode;
  const char *idcode_name;
  bool is_undo;

  Id *init_id; /* for resetting on cancel */

  ScrArea *cursor_area; /* Area under the cursor */
  ARgnType *art;
  void *draw_handle_pixel;
  int name_pos[2];
  char name[200];
};

static void datadropper_draw_cb(const Cxt * /*C*/, ARgn * /*rhn*/, void *arg)
{
  DataDropper *ddr = static_cast<DataDropper *>(arg);
  eyedropper_draw_cursor_text_rgn(ddr->name_pos, ddr->name);
}

static int datadropper_init(Cxt *C, WinOp *op)
{
  int index_dummy;
  ApiStruct *type;

  SpaceType *st;
  ARgnType *art;

  st = dune_spacetype_from_id(SPACE_VIEW3D);
  art = dune_regiontype_from_id(st, RGN_TYPE_WIN);

  DataDropper *ddr = mem_cnew<DataDropper>(__func__);

  Btn *btn = ui_cxt_active_btn_prop_get(C, &ddr->ptr, &ddr->prop, &index_dummy);

  if ((ddr->ptr.data == nullptr) || (ddr->prop == nullptr) ||
      (api_prop_editable(&ddr->ptr, ddr->prop) == false) ||
      (api_prop_type(ddr->prop) != PROP_PTR))
  {
    mem_free(ddr);
    return false;
  }
  op->customdata = ddr;

  ddr->is_undo = btn_flag_is_set(btn, BTN_UNDO);

  ddr->cursor_area = cxt_win_area(C);
  ddr->art = art;
  ddr->draw_handle_pixel = ed_rgn_draw_cb_activate(
      art, datadropper_draw_cb, ddr, RGN_DRAW_POST_PIXEL);

  type = api_prop_ptr_type(&ddr->ptr, ddr->prop);
  ddr->idcode = api_type_to_id_code(type);
  lib_assert(ddr->idcode != 0);
  /* Note we can translate here (instead of on draw time),
   * because this struct has very short lifetime. */
  ddr->idcode_name = TIP_(dune_idtype_idcode_to_name(ddr->idcode));

  const ApiPtr ptr = api_prop_ptr_get(&ddr->ptr, ddr->prop);
  ddr->init_id = ptr.owner_id;

  return true;
}

static void datadropper_exit(Cxt *C, WinOp *op)
{
  Win *win = cxt_win(C);

  win_cursor_modal_restore(win);

  if (op->customdata) {
    DataDropper *ddr = (DataDropper *)op->customdata;

    if (ddr->art) {
      ed_rgn_draw_cb_exit(ddr->art, ddr->draw_handle_pixel);
    }

    mem_free(op->customdata);

    op->customdata = nullptr;
  }

  win_evt_add_mousemove(win);
}

/* datadropper id helper fns */
/* get the Id from the 3D view or outliner. */
static void datadropper_id_sample_pt(
    Cxt *C, Win *win, ScrArea *area, DataDropper *ddr, const int evt_xy[2], Id **r_id)
{
  Win *win_prev = cxt_win(C);
  ScrArea *area_prev = cxt_win_area(C);
  ARgn *region_prev = cxt_win_region(C);

  ddr->name[0] = '\0';

  if (area) {
    if (ELEM(area->spacetype, SPACE_VIEW3D, SPACE_OUTLINER)) {
      ARgn *rgn = dune_area_find_region_xy(area, RGN_TYPE_WIN, event_xy);
      if (rgn) {
        const int mval[2] = {evt_xy[0] - rgn->winrct.xmin, evt_xy[1] - rgn->winrct.ymin};
        Base *base;

        cxt_win_set(C, win);
        cxt_win_area_set(C, area);
        cxt_win_rgn_set(C, region);

        /* Unfortunately it's necessary to always draw else we leave stale text. */
        ed_rgn_tag_redraw(rgn);

        if (area->spacetype == SPACE_VIEW3D) {
          base = ed_view3d_give_base_under_cursor(C, mval);
        }
        else {
          base = ed_outliner_give_base_under_cursor(C, mval);
        }

        if (base) {
          Object *ob = base->object;
          Id *id = nullptr;
          if (ddr->idcode == ID_OB) {
            id = (Id *)ob;
          }
          else if (ob->data) {
            if (GS(((Id *)ob->data)->name) == ddr->idcode) {
              id = (Id *)ob->data;
            }
            else {
              SNPRINTF(ddr->name, "Incompatible, expected a %s", ddr->idcode_name);
            }
          }

          ApiPtr idptr = api_id_ptr_create(id);

          if (id && api_prop_ptr_poll(&ddr->ptr, ddr->prop, &idptr)) {
            SNPRINTF(ddr->name, "%s: %s", ddr->idcode_name, id->name + 2);
            *r_id = id;
          }

          copy_v2_v2_int(ddr->name_pos, mval);
        }
      }
    }
  }

  cxt_win_set(C, win_prev);
  cxt_win_area_set(C, area_prev);
  cxt_win_rgn_set(C, rgn_prev);
}

/* sets the Id, returns success */
static bool datadropper_id_set(Cxt *C, DataDropper *ddr, Id *id)
{
  ApiPtr ptr_value = api_id_ptr_create(id);

  api_prop_ptr_set(&ddr->ptr, ddr->prop, ptr_value, nullptr);

  api_prop_update(C, &ddr->ptr, ddr->prop);

  ptr_value = api_prop_ptr_get(&ddr->ptr, ddr->prop);

  return (ptr_value.owner_id == id);
}

/* single point sample & set */
static bool datadropper_id_sample(Cxt *C, DataDropper *ddr, const int evt_xy[2])
{
  Id *id = nullptr;

  int evt_xy_win[2];
  Win *win;
  ScrArea *area;
  datadropper_win_area_find(C, event_xy, event_xy_win, &win, &area);

  datadropper_id_sample_pt(C, win, area, ddr, event_xy_win, &id);
  return datadropper_id_set(C, ddr, id);
}

static void datadropper_cancel(Cxt *C, WinOp *op)
{
  DataDropper *ddr = static_cast<DataDropper *>(op->customdata);
  datadropper_id_set(C, ddr, ddr->init_id);
  datadropper_exit(C, op);
}

/* To switch the draw cb when rgn under mouse evt changes */
static void datadropper_set_draw_cb_rgn(ScrArea *area, DataDropper *ddr)
{
  if (area) {
    /* If spacetype changed */
    if (area->spacetype != ddr->cursor_area->spacetype) {
      /* Remove old callback */
      ed_rgn_draw_cb_exit(ddr->art, ddr->draw_handle_pixel);

      /* Redraw old area */
      ARegion *region = dune_area_find_region_type(ddr->cursor_area, RGN_TYPE_WIN);
      ed_region_tag_redraw(region);

      /* Set draw callback in new region */
      ARegionType *art = dune_regiontype_from_id(area->type, RGN_TYPE_WIN);

      ddr->cursor_area = area;
      ddr->art = art;
      ddr->draw_handle_pixel = ed_region_draw_cb_activate(
          art, datadropper_draw_cb, ddr, RGN_DRAW_POST_PIXEL);
    }
  }
}

/* main modal status check */
static int datadropper_modal(Cxt *C, WinOp *op, const WinEvent *event)
{
  DataDropper *ddr = (DataDropper *)op->customdata;

  /* handle modal keymap */
  if (event->type == EVT_MODAL_MAP) {
    switch (event->val) {
      case EYE_MODAL_CANCEL:
        datadropper_cancel(C, op);
        return OP_CANCELLED;
      case EYE_MODAL_SAMPLE_CONFIRM: {
        const bool is_undo = ddr->is_undo;
        const bool success = datadropper_id_sample(C, ddr, event->xy);
        datadropper_exit(C, op);
        if (success) {
          /* Could support finished & undo-skip. */
          return is_undo ? OP_FINISHED : OP_CANCELLED;
        }
        dune_report(op->reports, RPT_WARNING, "Failed to set value");
        return OP_CANCELLED;
      }
    }
  }
  else if (event->type == MOUSEMOVE) {
    Id *id = nullptr;

    int event_xy_win[2];
    Win *win;
    ScrArea *area;
    datadropper_win_area_find(C, event->xy, event_xy_win, &win, &area);

    /* Set the region for eyedropper cursor text drawing */
    datadropper_set_draw_cb_region(area, ddr);

    datadropper_id_sample_pt(C, win, area, ddr, event_xy_win, &id);
  }

  return OP_RUNNING_MODAL;
}

/* Modal Op init */
static int datadropper_invoke(Cxt *C, WinOp *op, const WinEvent * /*event*/)
{
  /* init */
  if (datadropper_init(C, op)) {
    Win *win = cxt_win(C);
    /* Workaround for de-activating the btn clearing the cursor, see #76794 */
    ui_cxt_active_btn_clear(C, win, cxt_win_region(C));
    win_cursor_modal_set(win, WIN_CURSOR_EYEDROPPER);

    /* add temp handler */
    win_event_add_modal_handler(C, op);

    return OP_RUNNING_MODAL;
  }
  return OP_CANCELLED;
}

/* Repeat op */
static int datadropper_exec(Cxt *C, WinOp *op)
{
  /* init */
  if (datadropper_init(C, op)) {
    /* cleanup */
    datadropper_exit(C, op);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

static bool datadropper_poll(Cxt *C)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index_dummy;
  Btn *btn;

  /* data dropper only supports object data */
  if ((cxt_win(C) != nullptr) &&
      (btn = ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index_dummy)) &&
      (btn->type == UI_BTYPE_SEARCH_MENU) && (btn->flag & UI_BTN_VALUE_CLEAR))
  {
    if (prop && api_prop_type(prop) == PROP_PTR) {
      ApiStruct *type = api_prop_ptr_type(&ptr, prop);
      const short idcode = api_type_to_id_code(type);
      if ((idcode == ID_OB) || OB_DATA_SUPPORT_ID(idcode)) {
        return true;
      }
    }
  }

  return false;
}

void UI_OT_eyedropper_id(WinOpType *ot)
{
  /* identifiers */
  ot->name = "Eyedropper Data-Block";
  ot->idname = "UI_OT_eyedropper_id";
  ot->description = "Sample a data-block from the 3D View to store in a property";

  /* api callbacks */
  ot->invoke = datadropper_invoke;
  ot->modal = datadropper_modal;
  ot->cancel = datadropper_cancel;
  ot->exec = datadropper_exec;
  ot->poll = datadropper_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* properties */
}
