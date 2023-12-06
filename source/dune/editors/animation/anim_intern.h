#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* KeyingSets/Keyframing Interface ------------- */

/** List of builtin KeyingSets (defined in `keyingsets.cc`). */
extern ListBase builtin_keyingsets;

/* Operator Define Prototypes ------------------- */

/* -------------------------------------------------------------------- */
/** \name Main Keyframe Management operators
 *
 * These handle keyframes management from various spaces.
 * They only make use of Keying Sets.
 * \{ */

void ANIM_OT_keyframe_insert(struct wmOperatorType *ot);
void ANIM_OT_keyframe_delete(struct wmOperatorType *ot);
void ANIM_OT_keyframe_insert_by_name(struct wmOperatorType *ot);
void ANIM_OT_keyframe_delete_by_name(struct wmOperatorType *ot);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Main Keyframe Management operators
 *
 * These handle keyframes management from various spaces.
 * They will handle the menus required for each space.
 * \{ */

void ANIM_OT_keyframe_insert_menu(struct wmOperatorType *ot);

void ANIM_OT_keyframe_delete_v3d(struct wmOperatorType *ot);
void ANIM_OT_keyframe_clear_v3d(struct wmOperatorType *ot);

/* -------------------------------------------------------------------- */
/* Keyframe management ops for UI btns (RMB menu) */

void ANIM_OT_keyframe_insert_btn(struct wmOpType *ot);
void ANIM_OT_keyframe_delete_btn(struct wmOpType *ot);
void ANIM_OT_keyframe_clear_btn(struct wmOpType *ot);

/* KeyingSet mgmt ops for UI btns (RMB menu) */
void ANIM_OT_keyingset_btn_add(struct WinOpType *ot);
void ANIM_OT_keyingset_btn_remove(struct WinOpType *ot);

/* KeyingSet mgmt ops for api collections/UI btns */
void ANIM_OT_keying_set_add(struct WinOpType *ot);
void ANIM_OT_keying_set_remove(struct WinOpType *ot);
void ANIM_OT_keying_set_path_add(struct WinOpType *ot);
void ANIM_OT_keying_set_path_remove(struct WinOpType *ot);

/* KeyingSet general ops */
void ANIM_OT_keying_set_active_set(struct WinOpType *ot);

/* Driver management ops for UI btns (RMB menu) */
void ANIM_OT_driver_button_add(struct wmOperatorType *ot);
void ANIM_OT_driver_button_remove(struct wmOperatorType *ot);
void ANIM_OT_driver_button_edit(struct wmOperatorType *ot);
void ANIM_OT_copy_driver_button(struct wmOperatorType *ot);
void ANIM_OT_paste_driver_button(struct wmOperatorType *ot);

/** \} */

#ifdef __cplusplus
}
#endif
