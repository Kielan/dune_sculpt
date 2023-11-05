/* Eyedropper (Animation Driver Targets).
 * Defines:
 * - UI_OT_eyedropper_driver */

#include "mem_guardedalloc.h"

#include "types_anim.h"
#include "types_object.h"
#include "types_screen.h"

#include "dune_animsys.h"
#include "dune_cxt.h"

#include "graph.hh"
#include "graph_build.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_path.hh"

#include "ui.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_keyframing.hh"

#include "eyedropper_intern.hh"
#include "ui_intern.hh"

struct DriverDropper {
  /* Destination prop (i.e. where we'll add a driver) */
  ApiPtr ptr;
  ApiProp *prop;
  int index;
  bool is_undo;

  /* TODO: new target? */
};

static bool driverdropper_init(Cxt *C, WinOp *aop)
{
  DriverDropper *ddr = mem_cnew<DriverDropper>(__func__);

  Btn *btn = ui_cxt_active_btn_prop_get(C, &ddr->ptr, &ddr->prop, &ddr->index);

  if ((ddr->ptr.data == nullptr) || (ddr->prop == nullptr) ||
      (api_prop_editable(&ddr->ptr, ddr->prop) == false) ||
      (api_prop_animateable(&ddr->ptr, ddr->prop) == false) || (btn->flag & BTN_DRIVEN))
  {
    MEM_free(ddr);
    return false;
  }
  op->customdata = ddr;

  ddr->is_undo = btn_flag_is_set(btn, BTN_UNDO);

  return true;
}

static void driverdropper_exit(Cxt *C, WinOp *op)
{
  win_cursor_modal_restore(cxt_win(C));

  MEM_SAFE_FREE(op->customdata);
}

static void driverdropper_sample(Cxt *C, WinOp *op, const WinEvt *evt)
{
  DriverDropper *ddr = static_cast<DriverDropper *>(op->customdata);
  Btn *btn = eyedropper_get_prop_btn_under_mouse(C, event);

  const short mapping_type = api_enum_get(op->ptr, "mapping_type");
  const short flag = 0;

  /* we can only add a driver if we know what RNA property it corresponds to */
  if (btn == nullptr) {
    return;
  }
  /* Get paths for the source. */
  ApiPtr *target_ptr = &btn->apipoint;
  ApiProp *target_prop = btn->apiprop;
  const int target_index = btn->apiindex;

  char *target_path = api_path_from_id_to_prop(target_ptr, target_prop);

  /* Get paths for the destination. */
  char *dst_path = api_path_from_if_to_prop(&ddr->ptr, ddr->prop);

  /* Now create driver(s) */
  if (target_path && dst_path) {
    int success = anim_add_driver_with_target(op->reports,
                                              ddr->ptr.owner_id,
                                              dst_path,
                                              ddr->index,
                                              target_ptr->owner_id,
                                              target_path,
                                              target_index,
                                              flag,
                                              DRIVER_TYPE_PYTHON,
                                              mapping_type);

    if (success) {
      /* send updates */
      ui_cxt_update_anim_flag(C);
      graph_relations_tag_update(cxt_data_main(C));
      graph_id_tag_update(ddr->ptr.owner_id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
      win_evt_add_notifier(C, NC_ANIM | ND_FCURVES_ORDER, nullptr); /* XXX */
    }
  }

  /* cleanup */
  if (target_path) {
    mem_free(target_path);
  }
  if (dst_path) {
    mem_free(dst_path);
  }
}

static void driverdropper_cancel(Cxt *C, WinOp *op)
{
  driverdropper_exit(C, op);
}

/* main modal status check */
static int driverdropper_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  DriverDropper *ddr = static_cast<DriverDropper *>(op->customdata);

  /* handle modal keymap */
  if (ev->type == EV_MODAL_MAP) {
    switch (ev->val) {
      case EYE_MODAL_CANCEL: {
        driverdropper_cancel(C, op);
        return OP_CANCELLED;
      }
      case EYE_MODAL_SAMPLE_CONFIRM: {
        const bool is_undo = ddr->is_undo;
        driverdropper_sample(C, op, event);
        driverdropper_exit(C, op);
        /* Could support finished & undo-skip. */
        return is_undo ? OP_FINISHED : OP_CANCELLED;
      }
    }
  }

  return OP_RUNNING_MODAL;
}

/* Modal Op init */
static int driverdropper_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  /* init */
  if (driverdropper_init(C, op)) {
   Win *win = cxt_win(C);
    /* Workaround for de-activating the bgn clearing the cursor, see #76794 */
    ui_cxt_active_btn_clear(C, win, cxt_win_rgn(C));
    win_cursor_modal_set(win, WIN_CURSOR_EYEDROPPER);

    /* add temp handler */
    win_event_add_modal_handler(C, op);

    return OP_RUNNING_MODAL;
  }
  return OP_CANCELLED;
}

/* Repeat op */
static int driverdropper_ex(Cxt *C, WinOp *op)
{
  /* init */
  if (driverdropper_init(C, op)) {
    /* cleanup */
    driverdropper_exit(C, op);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

static bool driverdropper_poll(Cxt *C)
{
  if (!cxt_win(C)) {
    return false;
  }
  return true;
}

void UI_OT_eyedropper_driver(WinOpType *ot)
{
  /* ids */
  ot->name = "Eyedropper Driver";
  ot->idname = "UI_OT_eyedropper_driver";
  ot->description = "Pick a prop to use as a driver target";

  /* api cbs */
  ot->invoke = driverdropper_invoke;
  ot->modal = driverdropper_modal;
  ot->cancel = driverdropper_cancel;
  ot->ex = driverdropper_ex;
  ot->poll = driverdropper_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* props */
  api_def_enum(ot->sapi,
               "mapping_type",
               prop_driver_create_mapping_types,
               0,
               "Mapping Type",
               "Method used to match target and driven props");
}
