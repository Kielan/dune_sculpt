#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_bitmap.h"
#include "BLI_heap_simple.h"
#include "BLI_kdtree.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_rand.h"

#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_fcurve.h"
#include "BKE_layer.h"
#include "BKE_report.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_curve.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_select_utils.h"
#include "ED_types.h"
#include "ED_view3d.h"

#include "curve_intern.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "DEG_depsgraph.h"

/* -------------------------------------------------------------------- */
/** \name Utilities
 * \{ */

bool select_beztriple(BezTriple *bezt, bool selstatus, uint8_t flag, eVisible_Types hidden)
{
  if ((bezt->hide == 0) || (hidden == HIDDEN)) {
    if (selstatus == SELECT) { /* selects */
      bezt->f1 |= flag;
      bezt->f2 |= flag;
      bezt->f3 |= flag;
      return true;
    }
    /* deselects */
    bezt->f1 &= ~flag;
    bezt->f2 &= ~flag;
    bezt->f3 &= ~flag;
    return true;
  }

  return false;
}

bool select_bpoint(BPoint *bp, bool selstatus, uint8_t flag, bool hidden)
{
  if ((bp->hide == 0) || (hidden == 1)) {
    if (selstatus == SELECT) {
      bp->f1 |= flag;
      return true;
    }
    bp->f1 &= ~flag;
    return true;
  }

  return false;
}

static bool swap_selection_beztriple(BezTriple *bezt)
{
  if (bezt->f2 & SELECT) {
    return select_beztriple(bezt, DESELECT, SELECT, VISIBLE);
  }
  return select_beztriple(bezt, SELECT, SELECT, VISIBLE);
}

static bool swap_selection_bpoint(BPoint *bp)
{
  if (bp->f1 & SELECT) {
    return select_bpoint(bp, DESELECT, SELECT, VISIBLE);
  }
  return select_bpoint(bp, SELECT, SELECT, VISIBLE);
}

bool ED_curve_nurb_select_check(const View3D *v3d, const Nurb *nu)
{
  if (nu->type == CU_BEZIER) {
    const BezTriple *bezt;
    int i;

    for (i = nu->pntsu, bezt = nu->bezt; i--; bezt++) {
      if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
        return true;
      }
    }
  }
  else {
    const BPoint *bp;
    int i;

    for (i = nu->pntsu * nu->pntsv, bp = nu->bp; i--; bp++) {
      if (bp->f1 & SELECT) {
        return true;
      }
    }
  }
  return false;
}

int ED_curve_nurb_select_count(const View3D *v3d, const Nurb *nu)
{
  int sel = 0;

  if (nu->type == CU_BEZIER) {
    const BezTriple *bezt;
    int i;

    for (i = nu->pntsu, bezt = nu->bezt; i--; bezt++) {
      if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
        sel++;
      }
    }
  }
  else {
    const BPoint *bp;
    int i;

    for (i = nu->pntsu * nu->pntsv, bp = nu->bp; i--; bp++) {
      if (bp->f1 & SELECT) {
        sel++;
      }
    }
  }

  return sel;
}

bool ED_curve_nurb_select_all(const Nurb *nu)
{
  bool changed = false;
  int i;
  if (nu->bezt) {
    BezTriple *bezt;
    for (i = nu->pntsu, bezt = nu->bezt; i--; bezt++) {
      if (bezt->hide == 0) {
        if (BEZT_ISSEL_ALL(bezt) == false) {
          BEZT_SEL_ALL(bezt);
          changed = true;
        }
      }
    }
  }
  else if (nu->bp) {
    BPoint *bp;
    for (i = nu->pntsu * nu->pntsv, bp = nu->bp; i--; bp++) {
      if (bp->hide == 0) {
        if ((bp->f1 & SELECT) == 0) {
          bp->f1 |= SELECT;
          changed = true;
        }
      }
    }
  }
  return changed;
}

bool ED_curve_select_all(EditNurb *editnurb)
{
  bool changed = false;
  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    changed |= ED_curve_nurb_select_all(nu);
  }
  return changed;
}

bool ED_curve_nurb_deselect_all(const Nurb *nu)
{
  bool changed = false;
  int i;
  if (nu->bezt) {
    BezTriple *bezt;
    for (i = nu->pntsu, bezt = nu->bezt; i--; bezt++) {
      if (BEZT_ISSEL_ANY(bezt)) {
        BEZT_DESEL_ALL(bezt);
        changed = true;
      }
    }
  }
  else if (nu->bp) {
    BPoint *bp;
    for (i = nu->pntsu * nu->pntsv, bp = nu->bp; i--; bp++) {
      if (bp->f1 & SELECT) {
        bp->f1 &= ~SELECT;
        changed = true;
      }
    }
  }
  return changed;
}

int ED_curve_select_count(const View3D *v3d, const EditNurb *editnurb)
{
  int sel = 0;
  Nurb *nu;

  for (nu = editnurb->nurbs.first; nu; nu = nu->next) {
    sel += ED_curve_nurb_select_count(v3d, nu);
  }

  return sel;
}

bool ED_curve_select_check(const View3D *v3d, const EditNurb *editnurb)
{
  LISTBASE_FOREACH (const Nurb *, nu, &editnurb->nurbs) {
    if (ED_curve_nurb_select_check(v3d, nu)) {
      return true;
    }
  }

  return false;
}

bool ED_curve_deselect_all(EditNurb *editnurb)
{
  bool changed = false;
  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    changed |= ED_curve_nurb_deselect_all(nu);
  }
  return changed;
}

bool ED_curve_deselect_all_multi_ex(Base **bases, int bases_len)
{
  bool changed_multi = false;
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Object *obedit = bases[base_index]->object;
    Curve *cu = obedit->data;
    changed_multi |= ED_curve_deselect_all(cu->editnurb);
    DEG_id_tag_update(&cu->id, ID_RECALC_SELECT);
  }
  return changed_multi;
}

bool ED_curve_deselect_all_multi(struct bContext *C)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewContext vc;
  ED_view3d_viewcontext_init(C, &vc, depsgraph);
  uint bases_len = 0;
  Base **bases = BKE_view_layer_array_from_bases_in_edit_mode_unique_data(
      vc.view_layer, vc.v3d, &bases_len);
  bool changed_multi = ED_curve_deselect_all_multi_ex(bases, bases_len);
  MEM_freeN(bases);
  return changed_multi;
}

bool ED_curve_select_swap(EditNurb *editnurb, bool hide_handles)
{
  BPoint *bp;
  BezTriple *bezt;
  int a;
  bool changed = false;

  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    if (nu->type == CU_BEZIER) {
      bezt = nu->bezt;
      a = nu->pntsu;
      while (a--) {
        if (bezt->hide == 0) {
          bezt->f2 ^= SELECT; /* always do the center point */
          if (!hide_handles) {
            bezt->f1 ^= SELECT;
            bezt->f3 ^= SELECT;
          }
          changed = true;
        }
        bezt++;
      }
    }
    else {
      bp = nu->bp;
      a = nu->pntsu * nu->pntsv;
      while (a--) {
        if (bp->hide == 0) {
          swap_selection_bpoint(bp);
          changed = true;
        }
        bp++;
      }
    }
  }
  return changed;
}

/**
 * \param next: -1/1 for prev/next
 * \param cont: when true select continuously
 * \param selstatus: inverts behavior
 */
static void select_adjacent_cp(ListBase *editnurb,
                               short next,
                               const bool cont,
                               const bool selstatus)
{
  BezTriple *bezt;
  BPoint *bp;
  int a;
  bool lastsel = false;

  if (next == 0) {
    return;
  }

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    lastsel = false;
    if (nu->type == CU_BEZIER) {
      a = nu->pntsu;
      bezt = nu->bezt;
      if (next < 0) {
        bezt = &nu->bezt[a - 1];
      }
      while (a--) {
        if (a - abs(next) < 0) {
          break;
        }
        if ((lastsel == false) && (bezt->hide == 0) &&
            ((bezt->f2 & SELECT) || (selstatus == DESELECT))) {
          bezt += next;
          if (!(bezt->f2 & SELECT) || (selstatus == DESELECT)) {
            bool sel = select_beztriple(bezt, selstatus, SELECT, VISIBLE);
            if (sel && !cont) {
              lastsel = true;
            }
          }
        }
        else {
          bezt += next;
          lastsel = false;
        }
        /* move around in zigzag way so that we go through each */
        bezt -= (next - next / abs(next));
      }
    }
    else {
      a = nu->pntsu * nu->pntsv;
      bp = nu->bp;
      if (next < 0) {
        bp = &nu->bp[a - 1];
      }
      while (a--) {
        if (a - abs(next) < 0) {
          break;
        }
        if ((lastsel == false) && (bp->hide == 0) &&
            ((bp->f1 & SELECT) || (selstatus == DESELECT))) {
          bp += next;
          if (!(bp->f1 & SELECT) || (selstatus == DESELECT)) {
            bool sel = select_bpoint(bp, selstatus, SELECT, VISIBLE);
            if (sel && !cont) {
              lastsel = true;
            }
          }
        }
        else {
          bp += next;
          lastsel = false;
        }
        /* move around in zigzag way so that we go through each */
        bp -= (next - next / abs(next));
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Start/End Operators
 * \{ */

/**
 * (De)selects first or last of visible part of each #Nurb depending on `selfirst`.
 *
 * \param selfirst: defines the end of which to select.
 * \param doswap: defines if selection state of each first/last control point is swapped.
 * \param selstatus: selection status in case `doswap` is false.
 */
static void selectend_nurb(Object *obedit, eEndPoint_Types selfirst, bool doswap, bool selstatus)
{
  ListBase *editnurb = object_editcurve_get(obedit);
  BPoint *bp;
  BezTriple *bezt;
  Curve *cu;
  int a;

  if (obedit == NULL) {
    return;
  }

  cu = (Curve *)obedit->data;
  cu->actvert = CU_ACT_NONE;

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    if (nu->type == CU_BEZIER) {
      a = nu->pntsu;

      /* which point? */
      if (selfirst == LAST) { /* select last */
        bezt = &nu->bezt[a - 1];
      }
      else { /* select first */
        bezt = nu->bezt;
      }

      while (a--) {
        bool sel;
        if (doswap) {
          sel = swap_selection_beztriple(bezt);
        }
        else {
          sel = select_beztriple(bezt, selstatus, SELECT, VISIBLE);
        }

        if (sel == true) {
          break;
        }
      }
    }
    else {
      a = nu->pntsu * nu->pntsv;

      /* which point? */
      if (selfirst == LAST) { /* select last */
        bp = &nu->bp[a - 1];
      }
      else { /* select first */
        bp = nu->bp;
      }

      while (a--) {
        if (bp->hide == 0) {
          bool sel;
          if (doswap) {
            sel = swap_selection_bpoint(bp);
          }
          else {
            sel = select_bpoint(bp, selstatus, SELECT, VISIBLE);
          }

          if (sel == true) {
            break;
          }
        }
      }
    }
  }
}

static int de_select_first_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    selectend_nurb(obedit, FIRST, true, DESELECT);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
    BKE_curve_nurb_vert_active_validate(obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_de_select_first(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select First";
  ot->idname = "CURVE_OT_de_select_first";
  ot->description = "(De)select first of visible part of each NURBS";

  /* api callbacks */
  ot->exec = de_select_first_exec;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int de_select_last_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    selectend_nurb(obedit, LAST, true, DESELECT);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
    BKE_curve_nurb_vert_active_validate(obedit->data);
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_de_select_last(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select Last";
  ot->idname = "CURVE_OT_de_select_last";
  ot->description = "(De)select last of visible part of each NURBS";

  /* api callbacks */
  ot->exec = de_select_last_exec;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select All Operator
 * \{ */

static int de_select_all_exec(bContext *C, wmOperator *op)
{
  int action = RNA_enum_get(op->ptr, "action");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;
    for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
      Object *obedit = objects[ob_index];
      Curve *cu = obedit->data;

      if (ED_curve_select_check(v3d, cu->editnurb)) {
        action = SEL_DESELECT;
        break;
      }
    }
  }

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;
    bool changed = false;

    switch (action) {
      case SEL_SELECT:
        changed = ED_curve_select_all(cu->editnurb);
        break;
      case SEL_DESELECT:
        changed = ED_curve_deselect_all(cu->editnurb);
        break;
      case SEL_INVERT:
        changed = ED_curve_select_swap(
            cu->editnurb, (v3d && (v3d->overlay.handle_display == CURVE_HANDLE_NONE)));
        break;
    }

    if (changed) {
      DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
      WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
      BKE_curve_nurb_vert_active_validate(cu);
    }
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All";
  ot->idname = "CURVE_OT_select_all";
  ot->description = "(De)select all control points";

  /* api callbacks */
  ot->exec = de_select_all_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_all(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Linked Operator
 * \{ */

static int select_linked_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;
    EditNurb *editnurb = cu->editnurb;
    ListBase *nurbs = &editnurb->nurbs;
    bool changed = false;

    LISTBASE_FOREACH (Nurb *, nu, nurbs) {
      if (ED_curve_nurb_select_check(v3d, nu)) {
        changed |= ED_curve_nurb_select_all(nu);
      }
    }

    if (changed) {
      DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
      WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
    }
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

static int select_linked_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  return select_linked_exec(C, op);
}

void CURVE_OT_select_linked(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Linked All";
  ot->idname = "CURVE_OT_select_linked";
  ot->description = "Select all control points linked to the current selection";

  /* api callbacks */
  ot->exec = select_linked_exec;
  ot->invoke = select_linked_invoke;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Linked Pick Operator
 * \{ */

static int select_linked_pick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewContext vc;
  Nurb *nu;
  BezTriple *bezt;
  BPoint *bp;
  int a;
  const bool select = !RNA_boolean_get(op->ptr, "deselect");
  Base *basact = NULL;

  view3d_operator_needs_opengl(C);
  ED_view3d_viewcontext_init(C, &vc, depsgraph);
  copy_v2_v2_int(vc.mval, event->mval);

  if (!ED_curve_pick_vert(&vc, 1, &nu, &bezt, &bp, NULL, &basact)) {
    return OPERATOR_CANCELLED;
  }

  if (bezt) {
    a = nu->pntsu;
    bezt = nu->bezt;
    while (a--) {
      select_beztriple(bezt, select, SELECT, VISIBLE);
      bezt++;
    }
  }
  else if (bp) {
    a = nu->pntsu * nu->pntsv;
    bp = nu->bp;
    while (a--) {
      select_bpoint(bp, select, SELECT, VISIBLE);
      bp++;
    }
  }

  Object *obedit = basact->object;

  DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);

  if (!select) {
    BKE_curve_nurb_vert_active_validate(obedit->data);
  }

  return OPERATOR_FINISHED;
}

void CURVE_OT_select_linked_pick(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Linked";
  ot->idname = "CURVE_OT_select_linked_pick";
  ot->description = "Select all control points linked to already selected ones";

  /* api callbacks */
  ot->invoke = select_linked_pick_invoke;
  ot->poll = ED_operator_editsurfcurve_region_view3d;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna,
                  "deselect",
                  0,
                  "Deselect",
                  "Deselect linked control points rather than selecting them");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Row Operator
 * \{ */

static int select_row_exec(bContext *C, wmOperator *UNUSED(op))
{
  Object *obedit = CTX_data_edit_object(C);
  Curve *cu = obedit->data;
  ListBase *editnurb = object_editcurve_get(obedit);
  static BPoint *last = NULL;
  static int direction = 0;
  Nurb *nu = NULL;
  BPoint *bp = NULL;
  int u = 0, v = 0, a, b;

  if (!BKE_curve_nurb_vert_active_get(cu, &nu, (void *)&bp)) {
    return OPERATOR_CANCELLED;
  }

  if (last == bp) {
    direction = 1 - direction;
    BKE_nurbList_flag_set(editnurb, SELECT, false);
  }
  last = bp;

  u = cu->actvert % nu->pntsu;
  v = cu->actvert / nu->pntsu;
  bp = nu->bp;
  for (a = 0; a < nu->pntsv; a++) {
    for (b = 0; b < nu->pntsu; b++, bp++) {
      if (direction) {
        if (a == v) {
          select_bpoint(bp, SELECT, SELECT, VISIBLE);
        }
      }
      else {
        if (b == u) {
          select_bpoint(bp, SELECT, SELECT, VISIBLE);
        }
      }
    }
  }

  DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);

  return OPERATOR_FINISHED;
}

void CURVE_OT_select_row(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Control Point Row";
  ot->idname = "CURVE_OT_select_row";
  ot->description = "Select a row of control points including active one";

  /* api callbacks */
  ot->exec = select_row_exec;
  ot->poll = ED_operator_editsurf;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Next Operator
 * \{ */

static int select_next_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];

    ListBase *editnurb = object_editcurve_get(obedit);
    select_adjacent_cp(editnurb, 1, 0, SELECT);

    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_next(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Next";
  ot->idname = "CURVE_OT_select_next";
  ot->description = "Select control points following already selected ones along the curves";

  /* api callbacks */
  ot->exec = select_next_exec;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Previous Operator
 * \{ */

static int select_previous_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];

    ListBase *editnurb = object_editcurve_get(obedit);
    select_adjacent_cp(editnurb, -1, 0, SELECT);

    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_previous(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Previous";
  ot->idname = "CURVE_OT_select_previous";
  ot->description = "Select control points preceding already selected ones along the curves";

  /* api callbacks */
  ot->exec = select_previous_exec;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select More Operator
 * \{ */

static void curve_select_more(Object *obedit)
{
  ListBase *editnurb = object_editcurve_get(obedit);
  BPoint *bp, *tempbp;
  int a;
  short sel = 0;

  /* NOTE: NURBS surface is a special case because we mimic
   * the behavior of "select more" of mesh tools.
   * The algorithm is designed to work in planar cases so it
   * may not be optimal always (example: end of NURBS sphere). */
  if (obedit->type == OB_SURF) {
    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      BLI_bitmap *selbpoints;
      a = nu->pntsu * nu->pntsv;
      bp = nu->bp;
      selbpoints = BLI_BITMAP_NEW(a, "selectlist");
      while (a > 0) {
        if ((!BLI_BITMAP_TEST(selbpoints, a)) && (bp->hide == 0) && (bp->f1 & SELECT)) {
          /* upper control point */
          if (a % nu->pntsu != 0) {
            tempbp = bp - 1;
            if (!(tempbp->f1 & SELECT)) {
              select_bpoint(tempbp, SELECT, SELECT, VISIBLE);
            }
          }

          /* left control point. select only if it is not selected already */
          if (a - nu->pntsu > 0) {
            sel = 0;
            tempbp = bp + nu->pntsu;
            if (!(tempbp->f1 & SELECT)) {
              sel = select_bpoint(tempbp, SELECT, SELECT, VISIBLE);
            }
            /* make sure selected bpoint is discarded */
            if (sel == 1) {
              BLI_BITMAP_ENABLE(selbpoints, a - nu->pntsu);
            }
          }

          /* right control point */
          if (a + nu->pntsu < nu->pntsu * nu->pntsv) {
            tempbp = bp - nu->pntsu;
            if (!(tempbp->f1 & SELECT)) {
              select_bpoint(tempbp, SELECT, SELECT, VISIBLE);
            }
          }

          /* lower control point. skip next bp in case selection was made */
          if (a % nu->pntsu != 1) {
            sel = 0;
            tempbp = bp + 1;
            if (!(tempbp->f1 & SELECT)) {
              sel = select_bpoint(tempbp, SELECT, SELECT, VISIBLE);
            }
            if (sel) {
              bp++;
              a--;
            }
          }
        }

        bp++;
        a--;
      }

      MEM_freeN(selbpoints);
    }
  }
  else {
    select_adjacent_cp(editnurb, 1, 0, SELECT);
    select_adjacent_cp(editnurb, -1, 0, SELECT);
  }
}

static int curve_select_more_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    curve_select_more(obedit);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_more(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select More";
  ot->idname = "CURVE_OT_select_more";
  ot->description = "Select control points at the boundary of each selection region";

  /* api callbacks */
  ot->exec = curve_select_more_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Less Operator
 * \{ */

/* basic method: deselect if control point doesn't have all neighbors selected */
static void curve_select_less(Object *obedit)
{
  ListBase *editnurb = object_editcurve_get(obedit);
  BPoint *bp;
  BezTriple *bezt;
  int a;
  int sel = 0;
  bool lastsel = false;

  if (obedit->type == OB_SURF) {
    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      BLI_bitmap *selbpoints;
      a = nu->pntsu * nu->pntsv;
      bp = nu->bp;
      selbpoints = BLI_BITMAP_NEW(a, "selectlist");
      while (a--) {
        if ((bp->hide == 0) && (bp->f1 & SELECT)) {
          sel = 0;

          /* check if neighbors have been selected */
          /* edges of surface are an exception */
          if ((a + 1) % nu->pntsu == 0) {
            sel++;
          }
          else {
            bp--;
            if (BLI_BITMAP_TEST(selbpoints, a + 1) || ((bp->hide == 0) && (bp->f1 & SELECT))) {
              sel++;
            }
            bp++;
          }

          if ((a + 1) % nu->pntsu == 1) {
            sel++;
          }
          else {
            bp++;
            if ((bp->hide == 0) && (bp->f1 & SELECT)) {
              sel++;
            }
            bp--;
          }

          if (a + 1 > nu->pntsu * nu->pntsv - nu->pntsu) {
            sel++;
          }
          else {
            bp -= nu->pntsu;
            if (BLI_BITMAP_TEST(selbpoints, a + nu->pntsu) ||
                ((bp->hide == 0) && (bp->f1 & SELECT))) {
              sel++;
            }
            bp += nu->pntsu;
          }

          if (a < nu->pntsu) {
            sel++;
          }
          else {
            bp += nu->pntsu;
            if ((bp->hide == 0) && (bp->f1 & SELECT)) {
              sel++;
            }
            bp -= nu->pntsu;
          }

          if (sel != 4) {
            select_bpoint(bp, DESELECT, SELECT, VISIBLE);
            BLI_BITMAP_ENABLE(selbpoints, a);
          }
        }
        else {
          lastsel = false;
        }

        bp++;
      }

      MEM_freeN(selbpoints);
    }
  }
  else {
    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      lastsel = false;
      /* check what type of curve/nurb it is */
      if (nu->type == CU_BEZIER) {
        a = nu->pntsu;
        bezt = nu->bezt;
        while (a--) {
          if ((bezt->hide == 0) && (bezt->f2 & SELECT)) {
            sel = (lastsel == 1);

            /* check if neighbors have been selected */
            /* first and last are exceptions */
            if (a == nu->pntsu - 1) {
              sel++;
            }
            else {
              bezt--;
              if ((bezt->hide == 0) && (bezt->f2 & SELECT)) {
                sel++;
              }
              bezt++;
            }

            if (a == 0) {
              sel++;
            }
            else {
              bezt++;
              if ((bezt->hide == 0) && (bezt->f2 & SELECT)) {
                sel++;
              }
              bezt--;
            }

            if (sel != 2) {
              select_beztriple(bezt, DESELECT, SELECT, VISIBLE);
              lastsel = true;
            }
            else {
              lastsel = false;
            }
          }
          else {
            lastsel = false;
          }

          bezt++;
        }
      }
      else {
        a = nu->pntsu * nu->pntsv;
        bp = nu->bp;
        while (a--) {
          if ((lastsel == false) && (bp->hide == 0) && (bp->f1 & SELECT)) {
            sel = 0;

            /* first and last are exceptions */
            if (a == nu->pntsu * nu->pntsv - 1) {
              sel++;
            }
            else {
              bp--;
              if ((bp->hide == 0) && (bp->f1 & SELECT)) {
                sel++;
              }
              bp++;
            }

            if (a == 0) {
              sel++;
            }
            else {
              bp++;
              if ((bp->hide == 0) && (bp->f1 & SELECT)) {
                sel++;
              }
              bp--;
            }

            if (sel != 2) {
              select_bpoint(bp, DESELECT, SELECT, VISIBLE);
              lastsel = true;
            }
            else {
              lastsel = false;
            }
          }
          else {
            lastsel = false;
          }

          bp++;
        }
      }
    }
  }
}

static int curve_select_less_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    curve_select_less(obedit);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_less(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Less";
  ot->idname = "CURVE_OT_select_less";
  ot->description = "Deselect control points at the boundary of each selection region";

  /* api callbacks */
  ot->exec = curve_select_less_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Random Operator
 * \{ */

static int curve_select_random_exec(bContext *C, wmOperator *op)
{
  const bool select = (RNA_enum_get(op->ptr, "action") == SEL_SELECT);
  const float randfac = RNA_float_get(op->ptr, "ratio");
  const int seed = WM_operator_properties_select_random_seed_increment_get(op);

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);
    int seed_iter = seed;

    /* This gives a consistent result regardless of object order. */
    if (ob_index) {
      seed_iter += BLI_ghashutil_strhash_p(obedit->id.name);
    }

    int totvert = 0;
    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->type == CU_BEZIER) {
        int a = nu->pntsu;
        BezTriple *bezt = nu->bezt;
        while (a--) {
          if (!bezt->hide) {
            totvert++;
          }
          bezt++;
        }
      }
      else {
        int a = nu->pntsu * nu->pntsv;
        BPoint *bp = nu->bp;
        while (a--) {
          if (!bp->hide) {
            totvert++;
          }
          bp++;
        }
      }
    }

    BLI_bitmap *verts_selection_mask = BLI_BITMAP_NEW(totvert, __func__);
    const int count_select = totvert * randfac;
    for (int i = 0; i < count_select; i++) {
      BLI_BITMAP_SET(verts_selection_mask, i, true);
    }
    BLI_bitmap_randomize(verts_selection_mask, totvert, seed_iter);

    int bit_index = 0;
    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->type == CU_BEZIER) {
        int a = nu->pntsu;
        BezTriple *bezt = nu->bezt;

        while (a--) {
          if (!bezt->hide) {
            if (BLI_BITMAP_TEST(verts_selection_mask, bit_index)) {
              select_beztriple(bezt, select, SELECT, VISIBLE);
            }
            bit_index++;
          }
          bezt++;
        }
      }
      else {
        int a = nu->pntsu * nu->pntsv;
        BPoint *bp = nu->bp;

        while (a--) {
          if (!bp->hide) {
            if (BLI_BITMAP_TEST(verts_selection_mask, bit_index)) {
              select_bpoint(bp, select, SELECT, VISIBLE);
            }
            bit_index++;
          }
          bp++;
        }
      }
    }

    MEM_freeN(verts_selection_mask);
    BKE_curve_nurb_vert_active_validate(obedit->data);
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_select_random(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Random";
  ot->idname = "CURVE_OT_select_random";
  ot->description = "Randomly select some control points";

  /* api callbacks */
  ot->exec = curve_select_random_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_random(ot);
}
