#pragma once

struct ARgn;
struct ARgnType;
struct SpaceGraph;
struct AnimCxt;
struct AnimListElem;
struct Cxt;

#ifdef __cplusplus
extern "C" {
#endif

/* internal exports only */
/* `graph_drw.cc` */
/* Left hand part. */
void graph_dre_channel_names(struct Cxt *C, struct AnimCxt *ac, struct ARgn *rgn);

/* This is called twice from `space_graph.cc`, graph_main_rgn_draw()
 * Unsel then sel F-Curves are drwn so that they do not occlude each other */
void graph_drw_curves(struct AnimCt *ac,
                       struct SpaceGraph *sipo,
                       struct ARgn *rgn,
                       short sel);
/* Drw the 'ghost' F-Curves (i.e. snapshots of the curve)
 * unit mapping has alrdy been applied to the vals, so do not try and apply again. */
void graph_drw_ghost_curves(struct AnimCxt *ac,
                            struct SpaceGraph *sipo,
                            struct ARgn *rgn);

/* `graph_sel.cc` */
/* Desels keyframes in the Graph Editor
 * - Called by the desel all op, as well as other ones!
 *
 * - test: check if sel or desel all
 * - sel: how to sel keyframes
 *   0 = desel
 *   1 = sel
 *   2 = invert
 * - do_channels: whether to affect sel status of channels */
void desel_graph_keys(struct AnimCxt *ac, bool test, short sel, bool do_channels);

void GRAPH_OT_sel_all(struct wmOpType *ot);
void GRAPH_OT_sel_box(struct wmOpType *ot);
void GRAPH_OT_sel_lasso(struct wmOpType *ot);
void GRAPH_OT_sel_circle(struct wmOpType *ot);
void GRAPH_OT_sel_column(struct wmOpType *ot);
void GRAPH_OT_sel_linked(struct wmOpType *ot);
void GRAPH_OT_sel_more(struct wmOpType *ot);
void GRAPH_OT_sel_less(struct wmOpType *ot);
void GRAPH_OT_sel_leftright(struct wmOpType *ot);
void GRAPH_OT_sel_key_handles(struct wmOpType *ot);
void GRAPH_OT_clicksel(struct wmOpType *ot);

/* defines for left-right sel tool */
enum eGraphKeysLeftRightSelMode {
  GRAPHKEYS_LRSEL_TEST = 0,
  GRAPHKEYS_LRSEL_LEFT,
  GRAPHKEYS_LRSEL_RIGHT,
};

/* Defines for key/handles sel. */
enum eGraphKeySelKeyHandlesAction {
  GRAPHKEYS_KEYHANDLESSEL_SEL = 0,
  GRAPHKEYS_KEYHANDLESSEL_DESEL,
  /* Leave the sel status as-is. */
  GRAPHKEYS_KEYHANDLESSEL_KEEP,
};

/* defines for column-sel mode */
enum eGraphKeysColumnSelMode {
  GRAPHKEYS_COLUMNSEL_KEYS = 0,
  GRAPHKEYS_COLUMNSEL_CFRA,
  GRAPHKEYS_COLUMNSEL_MARKERS_COLUMN,
  GRAPHKEYS_COLUMNSEL_MARKERS_BETWEEN,
};

/* `graph_edit.cc` */
/* Get the min/max keyframes.
 * it should return total bound-box, filter for sel only can be arg. */
void get_graph_keyframe_extents(struct AnimCxt *ac,
                                float *xmin,
                                float *xmax,
                                float *ymin,
                                float *ymax,
                                bool do_sel_only,
                                bool include_handles);

void GRAPH_OT_previewrange_set(struct wmOpType *ot);
void GRAPH_OT_view_all(struct wmOpType *ot);
void GRAPH_OT_view_selected(struct wmOpType *ot);
void GRAPH_OT_view_frame(struct wmOpType *ot);

void GRAPH_OT_click_insert(struct wmOpType *ot);
void GRAPH_OT_keyframe_insert(struct wmOpType *ot);

void GRAPH_OT_copy(struct wmOpType *ot);
void GRAPH_OT_paste(struct wmOpType *ot);

void GRAPH_OT_dup(struct wmOpType *ot);
void GRAPH_OT_delete(struct wmOpType *ot);
void GRAPH_OT_clean(struct wmOpType *ot);
void GRAPH_OT_blend_to_neighbor(struct wmOpType *ot);
void GRAPH_OT_breakdown(struct wmOpType *ot);
void GRAPH_OT_ease(struct wmOpType *ot);
void GRAPH_OT_blend_offset(struct wmOpType *ot);
void GRAPH_OT_blend_to_ease(struct wmOpType *ot);
void GRAPH_OT_match_slope(struct wmOpType *ot);
void GRAPH_OT_shear(struct wmOpType *ot);
void GRAPH_OT_scale_average(struct wmOpType *ot);
void GRAPH_OT_push_pull(struct wmOpType *ot);
void GRAPH_OT_time_offset(struct wmOpType *ot);
void GRAPH_OT_decimate(struct wmOpType *ot);
void GRAPH_OT_blend_to_default(struct wmOpType *ot);
void GRAPH_OT_butterworth_smooth(struct wmOpType *ot);
void GRAPH_OT_gaussian_smooth(struct wmOpType *ot);
void GRAPH_OT_bake_keys(struct wmOpType *ot);
void GRAPH_OT_keys_to_samples(struct wmOpType *ot);
void GRAPH_OT_samples_to_keys(struct wmOpType *ot);
void GRAPH_OT_sound_to_samples(struct wmOpType *ot);
void GRAPH_OT_smooth(struct wmOpType *ot);
void GRAPH_OT_euler_filter(struct wmOpType *ot);

void GRAPH_OT_handle_type(struct WinOpType *ot);
void GRAPH_OT_interpolation_type(struct WinOpType *ot);
void GRAPH_OT_extrapolation_type(struct WinOpType *ot);
void GRAPH_OT_easing_type(struct WinOpType *ot);

void GRAPH_OT_frame_jump(struct WinOpType *ot);
void GRAPH_OT_keyframe_jump(struct WinOpType *ot);
void GRAPH_OT_snap_cursor_val(struct WinOpType *ot);
void GRAPH_OT_snap(struct WinOpType *ot);
void GRAPH_OT_equalize_handles(struct WinOpType *ot);
void GRAPH_OT_mirror(struct WinOpType *ot);

/* defines for snap keyframes
 * Leep in sync with eEditKeyframesSnap (in ed_keyframes_edit.hh) */
enum eGraphKeysSnapMode {
  GRAPHKEYS_SNAP_CFRA = 1,
  GRAPHKEYS_SNAP_NEAREST_FRAME,
  GRAPHKEYS_SNAP_NEAREST_SECOND,
  GRAPHKEYS_SNAP_NEAREST_MARKER,
  GRAPHKEYS_SNAP_HORIZONTAL,
  GRAPHKEYS_SNAP_VAL,
};

/* Defines for equalize keyframe handles.
 * Keep in sync with eEditKeyframesEqualize (in ed_keyframes_edit.hh).*/
enum eGraphKeysEqualizeMode {
  GRAPHKEYS_EQUALIZE_LEFT = 1,
  GRAPHKEYS_EQUALIZE_RIGHT,
  GRAPHKEYS_EQUALIZE_BOTH,
};

/* defines for mirror keyframes
 * Keep in sync with eEditKeyframesMirror (in ed_keyframes_edit.hh) */
enum eGraphKeysMirrorMode {
  GRAPHKEYS_MIRROR_CFRA = 1,
  GRAPHKEYS_MIRROR_YAXIS,
  GRAPHKEYS_MIRROR_XAXIS,
  GRAPHKEYS_MIRROR_MARKER,
  GRAPHKEYS_MIRROR_VAL,
};

void GRAPH_OT_fmod_add(struct WinOpType *ot);
void GRAPH_OT_fmod_copy(struct WinOpType *ot);
void GRAPH_OT_fmod_paste(struct WinOpType *ot);

void GRAPH_OT_driver_vars_copy(struct WinOpType *ot);
void GRAPH_OT_driver_vars_paste(struct WinOpType *ot);
void GRAPH_OT_driver_del_invalid(struct WinOpType *ot);

void GRAPH_OT_ghost_curves_create(struct WinOpType *ot);
void GRAPH_OT_ghost_curves_clear(struct WinOpType *ot);

/* `graph_btns.cc` */
void graph_btns_register(struct ARgnType *art);
/* `graph_utils.cc` */
/* Find 'active' F-Curve.
 * It must be editable, since that's the purpose of these btns (subject to change).
 * We return the 'wrapper' since it contains valuable cxt info (about hierarchy),
 * which will need to be freed when the caller is done with it.
 *
 * curve-visible flag isn't included,
 * otherwise sel a curve via list to edit is too cumbersome. */
struct AnimListElem *get_active_fcurve_channel(struct AnimCxt *ac);

/* Check if there are any visible keyframes (for sel tools) */
bool graphop_visible_keyframes_poll(struct Cxt *C);
/* Check if there are any visible + editable keyframes (for editing tools) */
bool graphop_editable_keyframes_poll(struct Cxt *C);
/* Has active F-Curve that's editable. */
bool graphop_active_fcurve_poll(struct Cxt *C);
/* Has active F-Curve in the context that's editable. */
bool graphop_active_editable_fcurve_ctx_poll(struct Cxt *C);
/* Has sel F-Curve that's editable */
bool graphop_selected_fcurve_poll(struct bContext *C);

/* ***************************************** */
/* `graph_ops.cc` */

void graphedit_keymap(struct wmKeyConfig *keyconf);
void graphedit_operatortypes(void);

#ifdef __cplusplus
}
#endif
