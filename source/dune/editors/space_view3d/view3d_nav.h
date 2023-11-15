#pragma once

/* Size of the sphere being dragged for trackball rotation within the view bounds.
 * also affects speed (smaller is faster). */
#define V3D_OP_TRACKBALLSIZE (1.1f)

struct ARgn;
struct Graph;
struct Dial;
struct Main;
struct RgnView3D;
struct Scene;
struct ScrArea;
struct View3D;
struct Cxt;
struct rcti;
struct WinEv;
struct WinOp;

enum eV3D_OpPropFlag {
  V3D_OP_PROP_MOUSE_CO = (1 << 0),
  V3D_OP_PROP_DELTA = (1 << 1),
  V3D_OP_PROP_USE_ALL_RGNS = (1 << 2),
  V3D_OP_PROP_USE_MOUSE_INIT = (1 << 3),
};

enum {
  VIEW_PASS = 0,
  VIEW_APPLY,
  VIEW_CONFIRM,
};

/* These defs are saved in keymap files, do not change vals but just add new ones */
enum {
  VIEW_MODAL_CONFIRM = 1, /* used for all view ops */
  VIEWROT_MODAL_AXIS_SNAP_ENABLE = 2,
  VIEWROT_MODAL_AXIS_SNAP_DISABLE = 3,
  VIEWROT_MODAL_SWITCH_ZOOM = 4,
  VIEWROT_MODAL_SWITCH_MOVE = 5,
  VIEWROT_MODAL_SWITCH_ROTATE = 6,
};

enum eViewOpsFlag {
  /* When enabled, rotate around the selection. */
  VIEWOPS_FLAG_ORBIT_SEL = (1 << 0),
  /* When enabled, use the depth under the cursor for nav. */
  VIEWOPS_FLAG_DEPTH_NAV = (1 << 1),
  /* When enabled run ed_view3d_persp_ensure this may switch out of camera view
   * when orbiting or switch from orthographic to perspective when auto-perspective is enabled.
   * Some ops don't require this (view zoom/pan or NDOF where subtle rotation is common
   * so we don't want it to trigger auto-perspective). */
  VIEWOPS_FLAG_PERSP_ENSURE = (1 << 2),
  /* When set, ignore any options that depend on initial cursor location. */
  VIEWOPS_FLAG_USE_MOUSE_INIT = (1 << 3),
};

/* Generic View Op Custom-Data */
typedef struct ViewOpsData {
  /* Cxt ptrs (assigned by viewops_data_create). */
  struct Main *main;
  struct Scene *scene;
  struct ScrArea *area;
  struct ARgn *rgn;
  struct View3D *v3d;
  struct RgnView3D *rv3d;
  struct Graph *graph;

  /* Needed for continuous zoom. */
  struct WinTimer *timer;

  /* Viewport state on init, don't change afterwards. */
  struct {
    float dist;
    float camzoom;
    float quat[4];
    /* WinEv.xy. */
    int ev_xy[2];
    /* Offset to use when VIEWOPS_FLAG_USE_MOUSE_INIT is not set.
     * so we can sim pressing in the middle of the screen. */
    int ev_xy_offset[2];
    /* WinEv.type that triggered the op */
    int ev_type;
    float ofs[3];
    /* Init distance to 'ofs' */
    float zfac;

    /* Trackball rotation only. */
    float trackvec[3];
    /* Dolly only. */
    float mousevec[3];

    /* RgnView3D.persp set after auto-perspective is applied.
     * If we want the value before running the operator, add a separate member. */
    char persp;

    /* Used for roll */
    struct Dial *dial;
  } init;

  /* Previous state (previous modal ev handled) */
  struct {
    int ev_xy[2];
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

  float reverse;
  bool axis_snap; /* view rotate only */

  /* Use for orbit sel and auto-dist. */
  float dyn_ofs[3];
  bool use_dyn_ofs;
} ViewOpsData;

/* view3d_nav.c */
bool view3d_location_poll(struct Cxt *C);
bool view3d_rotation_poll(struct Cxt *C);
bool view3d_zoom_or_dolly_poll(struct Cxt *C);

enum eViewOpsFlag viewops_flag_from_prefs(void);
void calctrackballvec(const struct rcti *rect, const int event_xy[2], float r_dir[3]);
void viewmove_apply(ViewOpsData *vod, int x, int y);
void view3d_orbit_apply_dyn_ofs(float r_ofs[3],
                                const float ofs_old[3],
                                const float viewquat_old[4],
                                const float viewquat_new[4],
                                const float dyn_ofs[3]);
void viewrotate_apply_dyn_ofs(ViewOpsData *vod, const float viewquat_new[4]);
bool view3d_orbit_calc_center(struct Cxt *C, float r_dyn_ofs[3]);

void view3d_op_props_common(struct WinOpType *ot, const enum eV3D_OpPropFlag flag);

/* Allocate and fill in cxt ptrs for ViewOpsData */
void viewops_data_free(struct Cxt *C, ViewOpsData *vod);

/* Allocate, fill in cxt ptrs and calculate the vals for ViewOpsData */
ViewOpsData *viewops_data_create(struct Cxt *C,
                                 const struct WinEv *ev,
                                 enum eViewOpsFlag viewops_flag);

void VIEW3D_OT_view_all(struct WinOpType *ot);
void VIEW3D_OT_view_selected(struct WinOpType *ot);
void VIEW3D_OT_view_center_cursor(struct WinOpType *ot);
void VIEW3D_OT_view_center_pick(struct WinOpType *ot);
void VIEW3D_OT_view_axis(struct WinOpType *ot);
void VIEW3D_OT_view_camera(struct WinOpType *ot);
void VIEW3D_OT_view_orbit(struct WinOpType *ot);
void VIEW3D_OT_view_pan(struct WinOpType *ot);

/* view3d_nav_dolly.c */
void viewdolly_modal_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_dolly(struct WinOpType *ot);

/* view3d_nav_fly.c */
void fly_modal_keymap(struct WinKeyConfig *keyconf);
void view3d_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_fly(struct WinOpType *ot);

/* view3d_nav_move.c */
void viewmove_modal_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_move(struct WinOpType *ot);

/* view3d_nav_ndof.c */
#ifdef WITH_INPUT_NDOF
struct WinNDOFMotionData;

/* Called from both fly mode and walk mode */
void view3d_ndof_fly(const struct WinNDOFMotionData *ndof,
                     struct View3D *v3d,
                     struct RgnView3D *rv3d,
                     bool use_precision,
                     short protectflag,
                     bool *r_has_translate,
                     bool *r_has_rotate);
void VIEW3D_OT_ndof_orbit(struct WinOpType *ot);
void VIEW3D_OT_ndof_orbit_zoom(struct WinOpType *ot);
void VIEW3D_OT_ndof_pan(struct WinOpType *ot);
void VIEW3D_OT_ndof_all(struct WinOpType *ot);
#endif /* WITH_INPUT_NDOF */

/* view3d_nav_roll.c */
void VIEW3D_OT_view_roll(struct WinOpType *ot);

/* view3d_nav_rotate.c */
void viewrotate_modal_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_rotate(struct WinOpType *ot);

/* view3d_nav_smoothview.c */

/* Params for setting the new 3D Viewport state.
 * Each of the struct members may be NULL to signify they aren't to be adjusted. */
typedef struct V3D_SmoothParams {
  struct Ob *camera_old, *camera;
  const float *ofs, *quat, *dist, *lens;

  /* Alternate rotation center, when set `ofs` must be NULL. */
  const float *dyn_ofs;
} V3D_SmoothParams;

/* The args are the desired situation. */
void ed_view3d_smooth_view_ex(const struct Graph *graph,
                              struct WinMngr *wm,
                              struct Win *win,
                              struct ScrArea *area,
                              struct View3D *v3d,
                              struct ARgn *rgn,
                              int smooth_viewtx,
                              const V3D_SmoothParams *sview);

void ed_view3d_smooth_view(struct Cxt *C,
                           struct View3D *v3d,
                           struct ARgn *rgn,
                           int smooth_viewtx,
                           const V3D_SmoothParams *sview);

/* Apply the smooth-view immediately, use when we need to start a new view op.
 * (so we don't end up half-applying a view op when pressing keys quickly). */
void ed_view3d_smooth_view_force_finish(struct Cxt *C,
                                        struct View3D *v3d,
                                        struct ARgn *rgn);

void VIEW3D_OT_smoothview(struct WinOpType *ot);

/* view3d_nav_walk.c */
void walk_modal_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_walk(struct WinOpType *ot);

/* view3d_nav_zoom.c */
void viewzoom_modal_keymap(struct WinKeyConfig *keyconf);
void VIEW3D_OT_zoom(struct WinOpType *ot);

/* view3d_nav_zoom_border.c */
void VIEW3D_OT_zoom_border(struct WinOpType *ot);
