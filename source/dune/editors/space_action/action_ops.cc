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
  win_optype_append(ACTION_OT_clickselect);
  win_optype_append(ACTION_OT_select_all);
  win_optype_append(ACTION_OT_select_box);
  win_optype_append(ACTION_OT_select_lasso);
  win_optype_append(ACTION_OT_select_circle);
  win_optype_append(ACTION_OT_select_column);
  win_optype_append(ACTION_OT_select_linked);
  win_optype_append(ACTION_OT_select_more);
  win_optype_append(ACTION_OT_select_less);
  win_optype_append(ACTION_OT_select_leftright);

  /* editing */
  win_optype_append(ACTION_OT_snap);
  win_optype_append(ACTION_OT_mirror);
  win_optype_append(ACTION_OT_frame_jump);
  win_optype_append(ACTION_OT_handle_type);
  win_optype_append(ACTION_OT_interpolation_type);
  win_optype_append(ACTION_OT_extrapolation_type);
  win_optype_append(ACTION_OT_easing_type);
  win_optype_append(ACTION_OT_keyframe_type);
  win_optype_append(ACTION_OT_bake_keys);
  win_optype_append(ACTION_OT_clean);
  win_optype_append(ACTION_OT_delete);
  win_optype_append(ACTION_OT_dup);
  win_optype_append(ACTION_OT_keyframe_insert);
  win_optype_append(ACTION_OT_copy);
  win_optype_append(ACTION_OT_paste);

  win_optype_append(ACTION_OT_new);
  win_optype_append(ACTION_OT_unlink);

  win_optype_append(ACTION_OT_push_down);
  win_optype_append(ACTION_OT_stash);
  win_optype_append(ACTION_OT_stash_and_create);

  win_optype_append(ACTION_OT_layer_next);
  win_optype_append(ACTION_OT_layer_prev);

  win_optype_append(ACTION_OT_previewrange_set);
  win_optype_append(ACTION_OT_view_all);
  win_optype_append(ACTION_OT_view_selected);
  win_optype_append(ACTION_OT_view_frame);

  win_optype_append(ACTION_OT_markers_make_local);
}

void ed_opmacros_action()
{
  WinOpType *ot;
  WinOpTypeMacro *otmacro;

  ot = win_optype_append_macro("ACTION_OT_dup_move",
                               "Duplicate",
                               "Make a copy of all sel keyframes and move them",
                               OPTYPE_UNDO | OPTYPE_REGISTER);
  win_optype_macro_define(ot, "ACTION_OT_dup");
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
