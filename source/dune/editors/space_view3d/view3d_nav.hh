#pragma once

#include "lib_utildefines.h"

/* Size of the sphere being dragged for trackball rotation within the view bounds.
 * also affects speed (smaller is faster) */
#define V3D_OP_TRACKBALLSIZE (1.1f)

struct ARgn;
struct Graph;
struct Dial;
struct RgnView3D;
struct Scene;
struct ScrArea;
struct View3D;
struct Cxt;
struct Ob;
struct ApiPtr;
struct rcti;
struct WinEv;
struct WinKeyConfig;
struct WinKeyMap;
struct WinOp;
struct WinOpType;
struct WinTimer;
struct Win;
struct WinMngr;

enum eV3D_OpPropFlag {
  V3D_OP_PROP_MOUSE_CO = (1 << 0),
  V3D_OP_PROP_DELTA = (1 << 1),
  V3D_OP_PROP_USE_ALL_RGNS = (1 << 2),
  V3D_OP_PROP_USE_MOUSE_INIT = (1 << 3),
};
ENUM_OPS(eV3D_OpPropFlag, V3D_OP_PROP_USE_MOUSE_INIT);

enum eV3D_OpEv {
  VIEW_PASS = 0,
  VIEW_APPLY,
  VIEW_CONFIRM,
  /* Only supported by some viewport ops */
  VIEW_CANCEL,
};

/* NOTE: these defines are saved in keymap files, do not change values but just add new ones */
enum {
  VIEW_MODAL_CANCEL = 0,  /* used for all view ops */
  VIEW_MODAL_CONFIRM = 1, /* used for all view ops */
  VIEWROT_MODAL_AXIS_SNAP_ENABLE = 2,
  VIEWROT_MODAL_AXIS_SNAP_DISABLE = 3,
  VIEWROT_MODAL_SWITCH_ZOOM = 4,
  VIEWROT_MODAL_SWITCH_MOVE = 5,
  VIEWROT_MODAL_SWITCH_ROTATE = 6,
};

enum eViewOpsFlag {
  VIEWOPS_FLAG_NONE = 0,
  /** When enabled, rotate around the sel */
  VIEWOPS_FLAG_ORBIT_SEL = (1 << 0),
  /* When enabled, use the depth under the cursor for nav */
  VIEWOPS_FLAG_DEPTH_NAV = (1 << 1),
  /* When enabled run ed_view3d_persp_ensure this may switch out of camera view
   * when orbiting or switch from orthographic to perspective when auto-perspective is enabled.
   * Some ops don't require this (view zoom/pan or NDOF where subtle rotation is common
   * so we don't want it to trigger auto-perspective). */
  VIEWOPS_FLAG_PERSP_ENSURE = (1 << 2),

  VIEWOPS_FLAG_ZOOM_TO_MOUSE = (1 << 3),

  VIEWOPS_FLAG_INIT_ZFAC = (1 << 4),
};
ENUM_OPS(eViewOpsFlag, VIEWOPS_FLAG_INIT_ZFAC);

struct ViewOpsType {
  eViewOpsFlag flag;
  const char *idname;
  bool (*poll_fn)(Cxt *C);
  int (*init_fn)(Cxt *C, ViewOpsData *vod, const WinEv *ev, ApiPtr *ptr);
  int (*apply_fn)(Cxt *C, ViewOpsData *vod, const eV3D_OpEv ev_code, const int xy[2]);
};

/* Generic View Op Custom-Data */
struct ViewOpsData {
  /* Cxt ptrs (assigned by viewops_data_create). */
  Scene *scene;
  ScrArea *area;
  ARgn *rgn;
  View3D *v3d;
  RgnView3D *rv3d;
  Graph *graph;

  /* Needed for continuous zoom. */
  wmTimer *timer;

  /* Viewport state on init, don't change afterwards. */
  struct {

    /* These vars reflect the same in RgnView3D. */
    float ofs[3];        /* DOLLY, MOVE, ROTATE and ZOOM. */
    float ofs_lock[2];   /* MOVE. */
    float camdx, camdy;  /* MOVE and ZOOM. */
    float camzoom;       /* ZOOM. */
    float dist;          /* ROTATE and ZOOM. */
    float quat[4];       /* ROLL and ROTATE. */
    char persp;          /* ROTATE. */
    char view;           /* ROTATE. */
    char view_axis_roll; /* ROTATE. */

    /* RgnView3D.persp set after auto-perspective is applied.
     * If we want the value before running the operator, add a separate member. */
    char persp_with_auto_persp_applied;

    /* The ones below are unrelated to the state of the 3D view. */
    /* WinEv.xy. */
    int ev_xy[2];
    /* Offset used when "use_cursor_init" is false to simulate pressing in the middle of the
     * rgn. */
    int event_xy_offset[2];
    /* WinEv.type that triggered the op. */
    int ev_type;

    /* Initial distance to 'ofs'. */
    float zfac;

    /* Trackball rotation only. */
    float trackvec[3];
    /* Dolly only. */
    float mousevec[3];

    /* Used for roll */
    Dial *dial;
  } init;

  /* Previous state (previous modal ev handled). */
  struct {
    int event_xy[2];
    /* For ops that use time-steps (continuous zoom). */
    double time;
  } prev;

  /* Current state. */
  struct {
    /* Working copy of RgnView3D.viewquat, needed for rotation calc
     * so we can apply snap to the 3D Viewport while keeping the unsnapped rotation
     * here to use when snap is disabled and for continued calc. */
    float viewquat[4];
  } curr;

  const ViewOpsType *nav_type;
  eViewOpsFlag viewops_flag;

  float reverse;
  bool axis_snap; /* view rotate only */

  /* Use for orbit selection and auto-dist. */
  float dyn_ofs[3];
  bool use_dyn_ofs;

  /* In orthographic views, a dynamic offset should not cause RgnView3D::ofs to end up
   * at a location that has no relation to the content where `ofs` originated or to `dyn_ofs`.
   * Failing to do so can cause the orthographic views `ofs` to be far away from the content
   * to the point it gets clipped out of the view.
   * See view3d_orbit_apply_dyn_ofs code-comments for an example, also see: #104385. */
  bool use_dyn_ofs_ortho_correction;

  void init_cxt(Cxt *C);
  void state_backup();
  void state_restore();
  void init_nav(Cxt *C,
                       const WinEv *ev,
                       const ViewOpsType *nav_type,
                       const float dyn_ofs_override[3] = nullptr,
                       const bool use_cursor_init = false);
  void end_nav(Cxt *C);

#ifdef WITH_CXX_GUARDEDALLOC
  MEM_CXX_CLASS_ALLOC_FNS("ViewOpsData")
#endif
};

/* view3d_nav.cc */
bool view3d_location_poll(Cxt *C);
bool view3d_rotation_poll(Cxt *C);
bool view3d_zoom_or_dolly_poll(Cxt *C);

int view3d_nav_invoke_impl(Cxt *C,
                           WinOp *op,
                           const WinEv *ev,
                           const ViewOpsType *nav_type);
int view3d_nav_modal_fn(Cxt *C, WinOp *op, const WinEv *ev);
void view3d_nav_cancel_fn(Cxt *C, WinOp *op);

void calctrackballvec(const rcti *rect, const int ev_xy[2], float r_dir[3]);
void viewmove_apply(ViewOpsData *vod, int x, int y);
void view3d_orbit_apply_dyn_ofs(float r_ofs[3],
                                const float ofs_old[3],
                                const float viewquat_old[4],
                                const float viewquat_new[4],
                                const float dyn_ofs[3]);
void viewrotate_apply_dyn_ofs(ViewOpsData *vod, const float viewquat_new[4]);
bool view3d_orbit_calc_center(Cxt *C, float r_dyn_ofs[3]);

void view3d_op_props_common(WinOpType *ot, const eV3D_OpPropFlag flag);

/* Allocate and fill in context ptrs for ViewOpsData */
void viewops_data_free(bContext *C, ViewOpsData *vod);

/* Allocate, fill in cxt ptrs and calc the vals for ViewOpsData */
ViewOpsData *viewops_data_create(bContext *C,
                                 const wmEvent *event,
                                 const ViewOpsType *nav_type,
                                 const bool use_cursor_init);
void axis_set_view(bContext *C,
                   View3D *v3d,
                   ARegion *region,
                   const float quat_[4],
                   char view,
                   char view_axis_roll,
                   int perspo,
                   const float *align_to_quat,
                   const int smooth_viewtx);

/* view3d_navigate_dolly.cc */
void viewdolly_modal_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_dolly(wmOperatorType *ot);

extern ViewOpsType ViewOpsType_dolly;

/* view3d_navigate_fly.cc */
void fly_modal_keymap(wmKeyConfig *keyconf);
void view3d_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_fly(wmOperatorType *ot);

/* view3d_navigate_move.cc */
void viewmove_modal_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_move(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_move;

/* view3d_navigate_ndof.cc */
#ifdef WITH_INPUT_NDOF
struct wmNDOFMotionData;
/* Called from both fly mode and walk mode, */
void view3d_ndof_fly(const wmNDOFMotionData *ndof,
                     View3D *v3d,
                     RegionView3D *rv3d,
                     bool use_precision,
                     short protectflag,
                     bool *r_has_translate,
                     bool *r_has_rotate);
void VIEW3D_OT_ndof_orbit(wmOperatorType *ot);
void VIEW3D_OT_ndof_orbit_zoom(wmOperatorType *ot);
void VIEW3D_OT_ndof_pan(wmOperatorType *ot);
void VIEW3D_OT_ndof_all(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_ndof_orbit;
extern const ViewOpsType ViewOpsType_ndof_orbit_zoom;
extern const ViewOpsType ViewOpsType_ndof_pan;
extern const ViewOpsType ViewOpsType_ndof_all;
#endif /* WITH_INPUT_NDOF */

/* view3d_navigate_roll.cc */
void VIEW3D_OT_view_roll(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_roll;

/* view3d_navigate_rotate.cc */
void viewrotate_modal_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_rotate(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_rotate;

/* view3d_nav_smoothview.cc */

/* Parameters for setting the new 3D Viewport state.
 *
 * Each of the struct members may be NULL to signify they aren't to be adjusted */
struct V3D_SmoothParams {
  Object *camera_old, *camera;
  const float *ofs, *quat, *dist, *lens;

  /* Alternate rotation center, when set `ofs` must be NULL. */
  const float *dyn_ofs;

  /* When non-NULL, perform undo pushes when transforming the camera. */
  const char *undo_str;
  /* When true use grouped undo pushes, use for incremental viewport manipulation
   * which are likely to be activated by holding a key or from the mouse-wheel. */
  bool undo_grouped;
};

/* The arguments are the desired situation */
void ED_view3d_smooth_view_ex(const Depsgraph *depsgraph,
                              wmWindowManager *wm,
                              wmWindow *win,
                              ScrArea *area,
                              View3D *v3d,
                              ARegion *region,
                              int smooth_viewtx,
                              const V3D_SmoothParams *sview);

void ED_view3d_smooth_view(
    bContext *C, View3D *v3d, ARegion *region, int smooth_viewtx, const V3D_SmoothParams *sview);

/* Call before multiple smooth-view operations begin to properly handle undo.
 *
 * \note Only use explicit undo calls when multiple calls to smooth-view are necessary
 * or when calling #ED_view3d_smooth_view_ex.
 * Otherwise pass in #V3D_SmoothParams.undo_str so an undo step is pushed as needed */
void ED_view3d_smooth_view_undo_begin(bContext *C, const ScrArea *area);
/* Run after multiple smooth-view operations have run to push undo as needed. */
void ED_view3d_smooth_view_undo_end(bContext *C,
                                    const ScrArea *area,
                                    const char *undo_str,
                                    bool undo_grouped);

/* Apply the smooth-view immediately, use when we need to start a new view operation.
 * (so we don't end up half-applying a view operation when pressing keys quickly). */
void ED_view3d_smooth_view_force_finish(bContext *C, View3D *v3d, ARegion *region);

void VIEW3D_OT_smoothview(wmOperatorType *ot);

/* view3d_navigate_view_all.cc */
void VIEW3D_OT_view_all(wmOperatorType *ot);
void VIEW3D_OT_view_selected(wmOperatorType *ot);

/* view3d_navigate_view_axis.cc */
void VIEW3D_OT_view_axis(wmOperatorType *ot);

/* view3d_navigate_view_camera.cc */
void VIEW3D_OT_view_camera(wmOperatorType *ot);

/* view3d_navigate_view_center_cursor.cc */
void VIEW3D_OT_view_center_cursor(wmOperatorType *ot);

/* view3d_navigate_view_center_pick.cc */
void VIEW3D_OT_view_center_pick(wmOperatorType *ot);

/* view3d_navigate_view_orbit.cc */
void VIEW3D_OT_view_orbit(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_orbit;

/* view3d_navigate_view_pan.cc */
void VIEW3D_OT_view_pan(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_pan;

/* view3d_navigate_walk.cc */
void walk_modal_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_walk(wmOperatorType *ot);

/* view3d_navigate_zoom.cc */
void viewzoom_modal_keymap(wmKeyConfig *keyconf);
void VIEW3D_OT_zoom(wmOperatorType *ot);

extern const ViewOpsType ViewOpsType_zoom;

/* view3d_navigate_zoom_border.cc */

void VIEW3D_OT_zoom_border(wmOperatorType *ot);
