#include <cmath>
#include <cstdlib>

#include "types_space.h"

#include "ed_anim_api.hh"
#include "ed_transform.hh"

#include "action_intern.hh"

#include "api_access.hh"

#include "win_api.hh"
#include "win_types.hh"

/* registration - op types */
void action_optypes()
{
  /* keyframes */
  /* sel */
  win_optype_append(act_OT_clicksel);
  win_optype_append(act_OT_sel_all);
  win_optype_append(act_OT_sel_box);
  win_optype_append(act_OT_sel_lasso);
  win_optype_append(act_OT_sel_circle);
  win_optype_append(act_OT_sel_column);
  win_optype_append(act_OT_sel_linked);
  win_optype_append(act_OT_select_more);
  win_optype_append(act_OT_sel_less);
  win_optype_append(act_OT_sel_leftright);

  /* editing */
  win_optype_append(act_OT_snap);
  win_optype_append(act_OT_mirror);
  win_optype_append(act_OT_frame_jump);
  win_optype_append(act_OT_handle_type);
  win_optype_append(act_OT_interpolation_type);
  win_optype_append(act_OT_extrapolation_type);
  win_optype_append(act_OT_easing_type);
  win_optype_append(act_OT_keyframe_type);
  win_optype_append(act_OT_bake_keys);
  win_optype_append(act_OT_clean);
  win_optype_append(act_OT_del);
  win_optype_append(act_OT_dup);
  win_optype_append(act_OT_keyframe_insert);
  win_optype_append(act_OT_copy);
  win_optype_append(act_OT_paste);

  win_optype_append(act_OT_new);
  win_optype_append(act_OT_unlink);

  win_optype_append(act_OT_push_down);
  win_optype_append(act_OT_stash);
  win_optype_append(act_OT_stash_and_create);

  win_optype_append(act_OT_layer_next);
  win_optype_append(act_OT_layer_prev);

  win_optype_append(act_OT_previewrange_set);
  win_optype_append(act_OT_view_all);
  win_optype_append(act_OT_view_sel);
  win_optype_append(act_OT_view_frame);

  win_optype_append(act_OT_markers_make_local);
}

void ed_opmacros_act()
{
  WinOpType *ot;
  WinOpTypeMacro *otmacro;

  ot = win_optype_append_macro("act_OT_dup_move",
                               "Duplicate",
                               "Make a copy of all sel keyframes and move them",
                               OPTYPE_UNDO | OPTYPE_REGISTER);
  win_optype_macro_define(ot, "act_ot_dup");
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_transform");
  api_enum_set(otmacro->ptr, "mode", TFM_TIME_TRANSLATE);
  api_bool_set(otmacro->ptr, "use_dup_keyframes", true);
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);
}

/* registration - keymaps */
void action_keymap(WinKeyConfig *keyconf)
{
  /* keymap for all rgns */
  win_keymap_ensure(keyconf, "Dopesheet Generic", SPACE_ACTION, RGN_TYPE_WIN);

  /* channels */
  /* Channels are not directly handled by the Action Editor module,
   * but are inherited from the Anim module.
   * All the relevant ops, keymaps, drawing, etc.
   * can therefore all be found in that module instead, as these
   * are all used for the Graph-Editor too. */

  /* keyframes */
  win_keymap_ensure(keyconf, "Dopesheet", SPACE_ACTION, RGN_TYPE_WIN);
}
