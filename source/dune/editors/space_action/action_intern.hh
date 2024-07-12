#pragma once

struct ARgn;
struct ARgnType;
struct Ob;
struct Scene;
struct SpaceAction;
struct AnimCxt;
struct Cxt;
struct WinOpType;

/* internal exports only */

/* **************************************** */
/* `space_action.cc` / `act_btns.cc` */

void act_btns_register(ARgnType *art);

/* ***************************************** */
/* `act_drw.cc` */

/* Left hand part. */
void drw_channel_names(Cxt *C, AnimCxt *ac, ARgn *rgn);
/* Drw keyframes in each channel. */
void drw_channel_strips(AnimCxt *ac, SpaceAction *saction, ARgn *rgn);

void timeline_drw_cache(const SpaceAction *sact, const Ob *ob, const Scene *scene);

/* ***************************************** */
/* `act_sel.cc` */

void act_ot_sel_all(wmOpType *ot);
void act_ot_sel_box(wmOpType *ot);
void act_ot_sel_lasso(wmOpType *ot);
void ACTION_OT_select_circle(wmOpType *ot);
void ACTION_OT_select_column(wmOpType *ot);
void ACTION_OT_select_linked(wmOperatorType *ot);
void ACTION_OT_select_more(wmOpType *ot);
void ACTION_OT_select_less(wmOpType *ot);
void ACTION_OT_select_leftright(wmOpType *ot);
void ACTION_OT_clickselect(wmOpType *ot);

/* defines for left-right sel tool */
enum eActKeys_LeftRightSelMode {
  ACTKEYS_LRSEL_TEST = 0,
  ACTKEYS_LRSEL_LEFT,
  ACTKEYS_LRSEL_RIGHT,
};

/* defines for column-select mode */
enum eActKeys_ColumnSel_Mode {
  ACTKEYS_COLUMNSEL_KEYS = 0,
  ACTKEYS_COLUMNSEL_CFRA,
  ACTKEYS_COLUMNSEL_MARKERS_COLUMN,
  ACTKEYS_COLUMNSEL_MARKERS_BETWEEN,
};

/* ***************************************** */
/* `act_edit.cc` */

void act_ot_previewrange_set(wmOperatorType *ot);
void act_ot_view_all(wmOperatorType *ot);
void act_OT_view_selected(wmOperatorType *ot);
void act_OT_view_frame(wmOperatorType *ot);

void act_OT_copy(wmOperatorType *ot);
void ACTION_OT_paste(wmOperatorType *ot);

void act_OT_keyframe_insert(wmOperatorType *ot);
void act_OT_duplicate(wmOperatorType *ot);
void act_OT_delete(wmOperatorType *ot);
void act_OT_clean(wmOperatorType *ot);
void act_OT_bake_keys(wmOperatorType *ot);

void act_OT_keyframe_type(wmOperatorType *ot);
void act_OT_handle_type(wmOperatorType *ot);
void act_OT_interpolation_type(wmOperatorType *ot);
void act_OT_extrapolation_type(wmOperatorType *ot);
void act_OT_easing_type(wmOperatorType *ot);

void act_OT_frame_jump(wmOperatorType *ot);

void act_OT_snap(wmOperatorType *ot);
void act_OT_mirror(wmOperatorType *ot);

void act_OT_new(wmOperatorType *ot);
void act_OT_unlink(wmOperatorType *ot);

void act_OT_push_down(wmOperatorType *ot);
void act_OT_stash(wmOperatorType *ot);
void act_OT_stash_and_create(wmOperatorType *ot);

void act_OT_layer_next(wmOperatorType *ot);
void act_OT_layer_prev(wmOperatorType *ot);

void act_OT_markers_make_local(wmOperatorType *ot);

/* defines for snap keyframes
 * keep in sync w eEditKeyframes_Snap (in ed_keyframes_edit.hh) */
enum eActKeys_Snap_Mode {
  ACTKEYS_SNAP_CFRA = 1,
  ACTKEYS_SNAP_NEAREST_FRAME,
  ACTKEYS_SNAP_NEAREST_SECOND,
  ACTKEYS_SNAP_NEAREST_MARKER,
};

/* defines for mirror keyframes
 * keep in sync w eEditKeyframes_Mirror (in ed_keyframes_edit.hh) */
enum eActKeys_Mirror_Mode {
  ACTKEYS_MIRROR_CFRA = 1,
  ACTKEYS_MIRROR_YAXIS,
  ACTKEYS_MIRROR_XAXIS,
  ACTKEYS_MIRROR_MARKER,
};

/* ***************************************** */
/* `act_ops.cc` */

void act_optypes();
void act_keymap(WinKeyConfig *keyconf);
