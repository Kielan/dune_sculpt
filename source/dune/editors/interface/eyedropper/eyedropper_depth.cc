/** \file
 * \ingroup edinterface
 *
 * This file defines an eyedropper for picking 3D depth value (primary use is depth-of-field).
 *
 * Defines:
 * - #UI_OT_eyedropper_depth
 */

#include "MEM_guardedalloc.h"

#include "DNA_camera_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BLI_math_vector.h"
#include "BLI_string.h"

#include "BKE_context.h"
#include "BKE_lib_id.h"
#include "BKE_screen.hh"
#include "BKE_unit.h"

#include "RNA_access.hh"
#include "RNA_prototypes.h"

#include "UI_interface.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "ED_screen.hh"
#include "ED_space_api.hh"
#include "ED_view3d.hh"

#include "eyedropper_intern.hh"
#include "interface_intern.hh"

/**
 * \note #DepthDropper is only internal name to avoid confusion with other kinds of eye-droppers.
 */
struct DepthDropper {
  PointerRNA ptr;
  PropertyRNA *prop;
  bool is_undo;

  bool is_set;
  float init_depth; /* For resetting on cancel. */

  bool accum_start; /* Has mouse been pressed. */
  float accum_depth;
  int accum_tot;

  ARegionType *art;
  void *draw_handle_pixel;
  int name_pos[2];
  char name[200];
};

static void depthdropper_draw_cb(const bContext * /*C*/, ARegion * /*region*/, void *arg)
{
  DepthDropper *ddr = static_cast<DepthDropper *>(arg);
  eyedropper_draw_cursor_text_region(ddr->name_pos, ddr->name);
}

static int depthdropper_init(bContext *C, wmOperator *op)
{
  int index_dummy;

  SpaceType *st;
  ARegionType *art;

  st = BKE_spacetype_from_id(SPACE_VIEW3D);
  art = BKE_regiontype_from_id(st, RGN_TYPE_WINDOW);

  DepthDropper *ddr = MEM_cnew<DepthDropper>(__func__);

  uiBut *but = UI_context_active_but_prop_get(C, &ddr->ptr, &ddr->prop, &index_dummy);

  /* fallback to the active camera's dof */
  if (ddr->prop == nullptr) {
    RegionView3D *rv3d = CTX_wm_region_view3d(C);
    if (rv3d && rv3d->persp == RV3D_CAMOB) {
      View3D *v3d = CTX_wm_view3d(C);
      if (v3d->camera && v3d->camera->data &&
          BKE_id_is_editable(CTX_data_main(C), static_cast<const ID *>(v3d->camera->data)))
      {
        Camera *camera = (Camera *)v3d->camera->data;
        ddr->ptr = RNA_pointer_create(&camera->id, &RNA_CameraDOFSettings, &camera->dof);
        ddr->prop = RNA_struct_find_property(&ddr->ptr, "focus_distance");
        ddr->is_undo = true;
      }
    }
  }
  else {
    ddr->is_undo = UI_but_flag_is_set(but, UI_BUT_UNDO);
  }

  if ((ddr->ptr.data == nullptr) || (ddr->prop == nullptr) ||
      (RNA_property_editable(&ddr->ptr, ddr->prop) == false) ||
      (RNA_property_type(ddr->prop) != PROP_FLOAT))
  {
    MEM_freeN(ddr);
    return false;
  }
  op->customdata = ddr;

  ddr->art = art;
  ddr->draw_handle_pixel = ED_region_draw_cb_activate(
      art, depthdropper_draw_cb, ddr, REGION_DRAW_POST_PIXEL);
  ddr->init_depth = RNA_property_float_get(&ddr->ptr, ddr->prop);

  return true;
}

static void depthdropper_exit(bContext *C, wmOperator *op)
{
  WM_cursor_modal_restore(CTX_wm_window(C));

  if (op->customdata) {
    DepthDropper *ddr = (DepthDropper *)op->customdata;

    if (ddr->art) {
      ED_rgn_draw_cb_exit(ddr->art, ddr->draw_handle_pixel);
    }

    mem_free(op->customdata);

    op->customdata = nullptr;
  }
}

/*  depthdropper id helper fns */
/* get the Id from the screen. */
static void depthdropper_depth_sample_pt(Cxt *C,
                                         DepthDropper *ddr,
                                         const int m_xy[2],
                                         float *r_depth)
{
  /* we could use some clever */
  Screen *screen = cxt_win_screen(C);
  ScrArea *area = dune_screen_find_area_xy(screen, SPACE_TYPE_ANY, m_xy);
  Scene *scene = cxt_data_scene(C);

  ScrArea *area_prev = cxt_win_area(C);
  ARgn *rgn_prev = cxt_win_rgn(C);

  ddr->name[0] = '\0';

  if (area) {
    if (area->spacetype == SPACE_VIEW3D) {
      ARgn *rgn = dune_area_find_region_xy(area, RGN_TYPE_WIN, m_xy);
      if (region) {
        Graph *graph = cxt_data_graph_ptr(C);
        View3D *v3d = static_cast<View3D *>(area->spacedata.first);
        RgnView3D *rv3d = static_cast<RgnView3D *>(rgn->rgndata);
        /* weak, we could pass in some reference point */
        const float *view_co = v3d->camera ? v3d->camera->object_to_world[3] : rv3d->viewinv[3];
        const int mval[2] = {m_xy[0] - rgn->winrct.xmin, m_xy[1] - rgn->winrct.ymin};
        copy_v2_v2_int(ddr->name_pos, mval);

        float co[3];

        cxt_win_area_set(C, area);
        cxt_win_rgn_set(C, region);

        /* Unfortunately it's necessary to always draw otherwise we leave stale text. */
        ed_rgn_tag_redraw(region);

        view3d_op_needs_opengl(C);

        if (ed_view3d_autodist(graph, region, v3d, mval, co, true, nullptr)) {
          const float mval_center_fl[2] = {float(rgn->winx) / 2, float(rgn->winy) / 2};
          float co_align[3];

          /* quick way to get view-center aligned point */
          ed_view3d_win_to_3d(v3d, rgn, co, mval_center_fl, co_align);

          *r_depth = len_v3v3(view_co, co_align);

          dune_unit_value_as_string(ddr->name,
                                   sizeof(ddr->name),
                                   double(*r_depth),
                                   4,
                                   B_UNIT_LENGTH,
                                   &scene->unit,
                                   false);
        }
        else {
          STRNCPY(ddr->name, "Nothing under cursor");
        }
      }
    }
  }

  cxt_win_area_set(C, area_prev);
  cxt_win_rgn_set(C, rgn_prev);
}

/* sets the sample depth RGB, maintaining A */
static void depthdropper_depth_set(Cxt *C, DepthDropper *ddr, const float depth)
{
  api_prop_float_set(&ddr->ptr, ddr->prop, depth);
  ddr->is_set = true;
  api_prop_update(C, &ddr->ptr, ddr->prop);
}

/* set sample from accumulated values */
static void depthdropper_depth_set_accum(Cxt *C, DepthDropper *ddr)
{
  float depth = ddr->accum_depth;
  if (ddr->accum_tot) {
    depth /= float(ddr->accum_tot);
  }
  depthdropper_depth_set(C, ddr, depth);
}

/* single point sample & set */
static void depthdropper_depth_sample(Cxt *C, DepthDropper *ddr, const int m_xy[2])
{
  float depth = -1.0f;
  if (depth != -1.0f) {
    depthdropper_depth_sample_pt(C, ddr, m_xy, &depth);
    depthdropper_depth_set(C, ddr, depth);
  }
}

static void depthdropper_depth_sample_accum(Cxt *C, DepthDropper *ddr, const int m_xy[2])
{
  float depth = -1.0f;
  depthdropper_depth_sample_pt(C, ddr, m_xy, &depth);
  if (depth != -1.0f) {
    ddr->accum_depth += depth;
    ddr->accum_tot++;
  }
}

static void depthdropper_cancel(Cxt *C, WinOp *op)
{
  DepthDropper *ddr = static_cast<DepthDropper *>(op->customdata);
  if (ddr->is_set) {
    depthdropper_depth_set(C, ddr, ddr->init_depth);
  }
  depthdropper_exit(C, op);
}

/* main modal status check */
static int depthdropper_modal(Cxt *C, WinOp *op, const WinEvt *evt)
{
  DepthDropper *ddr = static_cast<DepthDropper *>(op->customdata);

  /* handle modal keymap */
  if (evt->type == EVT_MODAL_MAP) {
    switch (evt->val) {
      case EYE_MODAL_CANCEL:
        depthdropper_cancel(C, op);
        return OP_CANCELLED;
      case EYE_MODAL_SAMPLE_CONFIRM: {
        const bool is_undo = ddr->is_undo;
        if (ddr->accum_tot == 0) {
          depthdropper_depth_sample(C, ddr, event->xy);
        }
        else {
          depthdropper_depth_set_accum(C, ddr);
        }
        depthdropper_exit(C, op);
        /* Could support finished & undo-skip. */
        return is_undo ? OP_FINISHED : OP_CANCELLED;
      }
      case EYE_MODAL_SAMPLE_BEGIN:
        /* enable accum and make first sample */
        ddr->accum_start = true;
        depthdropper_depth_sample_accum(C, ddr, evt->xy);
        break;
      case EYE_MODAL_SAMPLE_RESET:
        ddr->accum_tot = 0;
        ddr->accum_depth = 0.0f;
        depthdropper_depth_sample_accum(C, ddr, evt->xy);
        depthdropper_depth_set_accum(C, ddr);
        break;
    }
  }
  else if (evt->type == MOUSEMOVE) {
    if (ddr->accum_start) {
      /* button is pressed so keep sampling */
      depthdropper_depth_sample_accum(C, ddr, evt->xy);
      depthdropper_depth_set_accum(C, ddr);
    }
  }

  return OP_RUNNING_MODAL;
}

/* Modal Operator init */
static int depthdropper_invoke(Cxt *C, WinOp *op, const WinEvt * /*event*/)
{
  /* init */
  if (depthdropper_init(C, op)) {
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

/* Repeat operator */
static int depthdropper_exec(Cxt *C, WinOp *op)
{
  /* init */
  if (depthdropper_init(C, op)) {
    /* cleanup */
    depthdropper_exit(C, op);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

static bool depthdropper_poll(Cxt *C)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index_dummy;
  Btn *btn;

  /* check if there's an active btn taking depth value */
  if ((cxt_win(C) != nullptr) &&
      (btn = ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index_dummy)) &&
      (btn->type == BTYPE_NUM) && (prop != nullptr))
  {
    if ((api_prop_type(prop) == PROP_FLOAT) &&
        (api_prop_subtype(prop) & PROP_UNIT_LENGTH) &&
        (api_prop_array_check(prop) == false))
    {
      return true;
    }
  }
  else {
    RgnView3D *rv3d = cxt_win_rgn_view3d(C);
    if (rv3d && rv3d->persp == RV3D_CAMOB) {
      View3D *v3d = cxt_win_view3d(C);
      if (v3d->camera && v3d->camera->data &&
          dune_id_is_editable(cxt_data_main(C), static_cast<const Id *>(v3d->camera->data)))
      {
        return true;
      }
    }
  }

  return false;
}

void UI_OT_eyedropper_depth(WinOpType *ot)
{
  /* ids */
  ot->name = "Eyedropper Depth";
  ot->idname = "UI_OT_eyedropper_depth";
  ot->description = "Sample depth from the 3D view";

  /* api callbacks */
  ot->invoke = depthdropper_invoke;
  ot->modal = depthdropper_modal;
  ot->cancel = depthdropper_cancel;
  ot->exec = depthdropper_exec;
  ot->poll = depthdropper_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* properties */
}
