#pragma once

struct ARgn;
struct Cxt;
struct Screen;
struct RgnView3D;
struct Scene;
struct ScrArea;
struct TransformBounds;
struct TransInfo;
struct WinGizmoGroup;
struct WinGizmoGroupType;
struct WinMsgBus;

/* Gizmo */
/* `transform_gizmo_3d.cc` */
#define GIZMO_AXIS_LINE_WIDTH 2.0f

void gizmo_prepare_mat(const Cxt *C, RgnView3D *rv3d, const TransformBounds *tbounds);
void gizmo_xform_msg_sub(WinGizmoGroup *gzgroup,
                                   WinMsgBus *mbus,
                                   Scene *scene,
                                   Screen *screen,
                                   ScrArea *area,
                                   ARgn *rgn,
                                   void (*type_fn)(WinGizmoGroupType *));

/* Set the T_NO_GIZMO flag.
 * This maintains the conventional behavior of not displaying the gizmo if the op has
 * been triggered by shortcuts */
void transform_gizmo_3d_model_from_constraint_and_mode_init(TransInfo *t);

/* Change the gizmo and its orientation to match the transform state.
 * This used while the modal op is running so changes to the constraint or mode show
 * the gizmo assoc. w that state, as if it had been the initial gizmo dragged. */
void transform_gizmo_3d_model_from_constraint_and_mode_set(TransInfo *t);

/* Restores the non-modal state of the gizmo. */
void transform_gizmo_3d_model_from_constraint_and_mode_restore(TransInfo *t);

