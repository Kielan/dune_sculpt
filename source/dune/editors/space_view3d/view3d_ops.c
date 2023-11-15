#include <math.h>
#include <stdlib.h>

#include "types_ob.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_view3d.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_appdir.h"
#include "dune_copybuffer.h"
#include "dune_cxt.h"
#include "dune_main.h"
#include "dune_report.h"

#include "api_access.h"
#include "api_define.h"

#include "win_api.h"
#include "win_types.h"

#include "ed_outliner.h"
#include "ed_screen.h"
#include "ed_transform.h"

#include "view3d_intern.h"
#include "view3d_nav.h"

#ifdef WIN32
#  include "lib_math_base.h" /* M_PI */
#endif

/* copy paste */
static int view3d_copybuffer_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  char str[FILE_MAX];
  int num_copied = 0;

  dune_copybuffer_copy_begin(main);

  /* cxt, sel, could be generalized */
  CXT_DATA_BEGIN (C, Ob *, ob, sel_objs) {
    if ((ob->id.tag & LIB_TAG_DOIT) == 0) {
      dune_copybuffer_copy_tag_ID(&ob->id);
      num_copied++;
    }
  }
  CXT_DATA_END;

  lib_join_dirfile(str, sizeof(str), dune_tmpdir_base(), "copybuffer.blend");
  dune_copybuffer_copy_end(main, str, op->reports);

  dune_reportf(op->reports, RPT_INFO, "Copied %d sel obj(s)", num_copied);

  return OP_FINISHED;
}

static void VIEW3D_OT_copybuffer(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Objs";
  ot->idname = "VIEW3D_OT_copybuffer";
  ot->description = "Sel objs are copied to the clipboard";

  /* api cbs */
  ot->ex = view3d_copybuffer_ex;
  ot->poll = ed_op_scene;
}

static int view3d_pastebuffer_ex(Cxt *C, WinOp *op)
{
  char str[FILE_MAX];
  short flag = 0;

  if (api_bool_get(op->ptr, "autosel")) {
    flag |= FILE_AUTOSEL;
  }
  if (api_bool_get(op->ptr, "active_collection")) {
    flag |= FILE_ACTIVE_COLLECTION;
  }

  lib_join_dirfile(str, sizeof(str), dune_tmpdir_base(), "copybuffer.dune");

  const int num_pasted = dune_copybuffer_paste(C, str, flag, op->reports, FILTER_ID_OB);
  if (num_pasted == 0) {
    dune_report(op->reports, RPT_INFO, "No objs to paste");
    return OP_CANCELLED;
  }

  win_ev_add_notifier(C, NC_WIN, NULL);
  ed_outliner_sel_sync_from_ob_tag(C);

  dune_reportf(op->reports, RPT_INFO, "%d obj(s) pasted", num_pasted);

  return OP_FINISHED;
}

static void VIEW3D_OT_pastebuffer(WinOpType *ot)
{

  /* ids */
  ot->name = "Paste Objs";
  ot->idname = "VIEW3D_OT_pastebuffer";
  ot->description = "Objs from the clipboard are pasted";

  /* api cbs */
  ot->ex = view3d_pastebuffer_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi, "autosel", true, "Select", "Select pasted objects");
  api_def_bool(ot->sapi,
               "active_collection",
               true,
               "Active Collection",
               "Put pasted objs in the active collection");
}

/* registration */
void view3d_optypes(void)
{
  win_optype_append(VIEW3D_OT_rotate);
  win_optype_append(VIEW3D_OT_move);
  win_optype_append(VIEW3D_OT_zoom);
  win_optype_append(VIEW3D_OT_zoom_camera_1_to_1);
  win_optype_append(VIEW3D_OT_dolly);
#ifdef WITH_INPUT_NDOF
  win_optype_append(VIEW3D_OT_ndof_orbit_zoom);
  win_optype_append(VIEW3D_OT_ndof_orbit);
  win_optype_append(VIEW3D_OT_ndof_pan);
  win_optype_append(VIEW3D_OT_ndof_all);
#endif /* WITH_INPUT_NDOF */
  win_optype_append(VIEW3D_OT_view_all);
  win_optype_append(VIEW3D_OT_view_axis);
  win_optype_append(VIEW3D_OT_view_camera);
  win_optype_append(VIEW3D_OT_view_orbit);
  win_optype_append(VIEW3D_OT_view_roll);
  win_optype_append(VIEW3D_OT_view_pan);
  win_optype_append(VIEW3D_OT_view_persportho);
  win_optype_append(VIEW3D_OT_background_img_add);
  win_optype_append(VIEW3D_OT_background_img_remove);
  win_optype_append(VIEW3D_OT_drop_world);
  win_optype_append(VIEW3D_OT_view_sel);
  win_optype_append(VIEW3D_OT_view_lock_clear);
  win_optype_append(VIEW3D_OT_view_lock_to_active);
  win_optype_append(VIEW3D_OT_view_center_cursor);
  win_optype_append(VIEW3D_OT_view_center_pick);
  win_optype_append(VIEW3D_OT_view_center_camera);
  win_optype_append(VIEW3D_OT_view_center_lock);
  win_optype_append(VIEW3D_OT_sel);
  win_optype_append(VIEW3D_OT_sel_box);
  win_optype_append(VIEW3D_OT_clip_border);
  win_optype_append(VIEW3D_OT_sel_circle);
  win_optype_append(VIEW3D_OT_smoothview);
  win_optype_append(VIEW3D_OT_render_border);
  win_optype_append(VIEW3D_OT_clear_render_border);
  win_optype_append(VIEW3D_OT_zoom_border);
  win_optype_append(VIEW3D_OT_cursor3d);
  win_optype_append(VIEW3D_OT_sel_lasso);
  win_optype_append(VIEW3D_OT_sel_menu);
  win_optype_append(VIEW3D_OT_bone_sel_menu);
  win_optype_append(VIEW3D_OT_camera_to_view);
  win_optype_append(VIEW3D_OT_camera_to_view_sel);
  win_optype_append(VIEW3D_OT_ob_as_camera);
  win_optype_append(VIEW3D_OT_localview);
  win_optype_append(VIEW3D_OT_localview_remove_from);
  win_optype_append(VIEW3D_OT_fly);
  win_optype_append(VIEW3D_OT_walk);
  win_optype_append(VIEW3D_OT_nav);
  win_optype_append(VIEW3D_OT_copybuffer);
  win_optype_append(VIEW3D_OT_pastebuffer);

  win_optype_append(VIEW3D_OT_ob_mode_pie_or_toggle);

  win_optype_append(VIEW3D_OT_snap_sel_to_grid);
  win_optype_append(VIEW3D_OT_snap_sel_to_cursor);
  win_optype_append(VIEW3D_OT_snap_sel_to_active);
  win_optype_append(VIEW3D_OT_snap_cursor_to_grid);
  win_optype_append(VIEW3D_OT_snap_cursor_to_center);
  win_optype_append(VIEW3D_OT_snap_cursor_to_sel);
  win_optype_append(VIEW3D_OT_snap_cursor_to_active);

  win_optype_append(VIEW3D_OT_interactive_add);

  win_optype_append(VIEW3D_OT_toggle_shading);
  win_optype_append(VIEW3D_OT_toggle_xray);
  win_optype_append(VIEW3D_OT_toggle_matcap_flip);

  win_optype_append(VIEW3D_OT_ruler_add);
  win_optype_append(VIEW3D_OT_ruler_remove);

  transform_optypes();
}

void view3d_keymap(WinKeyConfig *keyconf)
{
  win_keymap_ensure(keyconf, "3D View Generic", SPACE_VIEW3D, 0);

  /* only for region 3D win */
  win_keymap_ensure(keyconf, "3D View", SPACE_VIEW3D, 0);

  fly_modal_keymap(keyconf);
  walk_modal_keymap(keyconf);
  viewrotate_modal_keymap(keyconf);
  viewmove_modal_keymap(keyconf);
  viewzoom_modal_keymap(keyconf);
  viewdolly_modal_keymap(keyconf);
  viewplace_modal_keymap(keyconf);
}
