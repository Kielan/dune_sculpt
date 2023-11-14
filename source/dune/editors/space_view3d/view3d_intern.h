#pragma once

#include "ed_view3d.h"

/* internal exports only */
struct ARgn;
struct ARgnType;
struct BoundBox;
struct Graph;
struct Obj;
struct Scene;
struct ViewCxt;
struct ViewLayer;
struct Cxt;
struct WinGizmoGroupType;
struct WinGizmoType;
struct WinKeyConfig;
struct WinOpType;
struct WinMngr;

/* view3d_header.c */
void VIEW3D_OT_toggle_matcap_flip(struct WinOpType *ot);

/* view3d_ops.c */
void view3d_optypes(void);

/* view3d_edit.c */
void VIEW3D_OT_zoom_camera_1_to_1(struct WinOpType *ot);
void VIEW3D_OT_view_lock_clear(struct WinOpType *ot);
void VIEW3D_OT_view_lock_to_active(struct WinOpType *ot);
void VIEW3D_OT_view_center_camera(struct WinOpType *ot);
void VIEW3D_OT_view_center_lock(struct WinOpType *ot);
void VIEW3D_OT_view_persportho(struct WinOpType *ot);
void VIEW3D_OT_nav(struct WinOpType *ot);
void VIEW3D_OT_background_image_add(struct WinOpType *ot);
void VIEW3D_OT_background_image_remove(struct WinOpType *ot);
void VIEW3D_OT_drop_world(struct WinOpType *ot);
void VIEW3D_OT_clip_border(struct WinOpType *ot);
void VIEW3D_OT_cursor3d(struct WinOpType *ot);
void VIEW3D_OT_render_border(struct WinOpType *ot);
void VIEW3D_OT_clear_render_border(struct WinOpType *ot);
void VIEW3D_OT_toggle_shading(struct WinOpType *ot);
void VIEW3D_OT_toggle_xray(struct WinOpType *ot);

/* view3d_draw.c */
void view3d_main_rgn_draw(const struct Cxt *C, struct ARgn *rgn);
/* Info drawn on top of the solid plates and composed data. */
void view3d_draw_rgn_info(const struct Cxt *C, struct ARgn *rgn);

/* view3d_draw_legacy.c */
void ed_view3d_draw_sel_loop(struct Graph *graph,
                                struct ViewCxt *vc,
                                struct Scene *scene,
                                struct ViewLayer *view_layer,
                                struct View3D *v3d,
                                struct ARgn *rgn,
                                bool use_obedit_skip,
                                bool use_nearest);

void ed_view3d_draw_depth_loop(struct Graph *graph,
                               struct Scene *scene,
                               struct ARgn *rgn,
                               View3D *v3d);

void view3d_depths_rect_create(struct ARgn *rgn, struct rcti *rect, struct ViewDepths *r_d);
/* Util fun to find the closest Z value, use for auto-depth. */
float view3d_depth_near(struct ViewDepths *d);

/* view3d_sel.c */
void VIEW3D_OT_sel(struct WinOpType *ot);
void VIEW3D_OT_sel_circle(struct WinOpType *ot);
void VIEW3D_OT_sel_box(struct WinOpType *ot);
void VIEW3D_OT_sel_lasso(struct WinOpType *ot);
void VIEW3D_OT_sel_menu(struct WinOpType *ot);
void VIEW3D_OT_bone_sel_menu(struct WinOpType *ot);

/* view3d_utils.c */
/** For home, center etc. */
void view3d_boxview_copy(struct ScrArea *area, struct ARgn *rgn);
/* Sync center/zoom view of rgn to others, for view transforms. */
void view3d_boxview_sync(struct ScrArea *area, struct ARgn *rgn);

bool ed_view3d_boundbox_clip_ex(const RgnView3D *rv3d,
                                const struct BoundBox *bb,
                                float obmat[4][4]);
bool ed_view3d_boundbox_clip(RgnView3D *rv3d, const struct BoundBox *bb);

/* view3d_view.c */
void VIEW3D_OT_camera_to_view(struct WinOpType *ot);
void VIEW3D_OT_camera_to_view_selected(struct WinOpType *ot);
void VIEW3D_OT_object_as_camera(struct WinOpType *ot);
void VIEW3D_OT_localview(struct WinOpType *ot);
void VIEW3D_OT_localview_remove_from(struct WinOpType *ot);

/* param rect: optional for picking (can be NULL). */
void view3d_winmatrix_set(struct Graph *graph,
                          struct ARgn *rgn,
                          const View3D *v3d,
                          const rcti *rect);
/* Sets RgnView3D.viewmat
 * param graph: Graph.
 * param scene: Scene for camera and cursor location.
 * param v3d: View 3D space data.
 * param rv3d: 3D rgn which stores the final matrices.
 * param rect_scale: Optional 2D scale argument,
 * Use when displaying a sub-region, eg: when #view3d_winmatrix_set takes a 'rect' argument.
 *
 * note don't set windows active in here, is used by renderwin too. */
void view3d_viewmatrix_set(struct Graph *graph,
                           const struct Scene *scene,
                           const View3D *v3d,
                           RgnView3D *rv3d,
                           const float rect_scale[2]);

/* Called in transform_ops.c, on each regeneration of key-maps. */
/* view3d_placement.c */
void viewplace_modal_keymap(struct WinKeyConfig *keyconf);

/* view3d_btns.c */
void VIEW3D_OT_obj_mode_pie_or_toggle(struct WinOpType *ot);
void view3d_btns_register(struct ARgnType *art);

/* view3d_camera_control.c */
/* Creates a View3DCameraControl handle and sets up
 * the view for first-person style nav. */
struct View3DCameraControl *ed_view3d_cameracontrol_acquire(struct Graph *graph,
                                                            struct Scene *scene,
                                                            View3D *v3d,
                                                            RgnView3D *rv3d);
/* Updates cameras from the `rv3d` values, optionally auto-keyframing. */
void ed_view3d_cameracontrol_update(struct View3DCameraControl *vctrl,
                                    bool use_autokey,
                                    struct Cxt *C,
                                    bool do_rotate,
                                    bool do_translate);
/* Release view control.
 * param restore: Sets the view state to the values that were set
 * before ed_view3d_control_acquire was called */
void ed_view3d_cameracontrol_release(struct View3DCameraControl *vctrl, bool restore);
/* Returns the obj which is being manipulated or NULL. */
struct Obj *ed_view3d_cameracontrol_obj_get(struct View3DCameraControl *vctrl);

/* view3d_snap.c */

/* Calculates the bounding box corners (min and max) for obedit.
 * The returned values are in global space. */
bool ed_view3d_minmax_verts(struct Obj *obedit, float min[3], float max[3]);

void VIEW3D_OT_snap_selected_to_grid(struct WinOpType *ot);
void VIEW3D_OT_snap_selected_to_cursor(struct WinOpType *ot);
void VIEW3D_OT_snap_selected_to_active(struct WinOpType *ot);
void VIEW3D_OT_snap_cursor_to_grid(struct WinOpType *ot);
void VIEW3D_OT_snap_cursor_to_center(struct WinOpType *ot);
void VIEW3D_OT_snap_cursor_to_selected(struct WinOpType *ot);
void VIEW3D_OT_snap_cursor_to_active(struct WinOpType *ot);

/* view3d_placement.c */
void VIEW3D_OT_interactive_add(struct WinOpType *ot);

/* space_view3d.c */
extern const char *view3d_cxt_dir[]; /* doc access */

/* view3d_widgets.c */
void VIEW3D_GGT_light_spot(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_light_area(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_light_target(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_camera(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_camera_view(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_force_field(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_empty_img(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_armature_spline(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_nav(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_mesh_preselect_elem(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_mesh_preselect_edgering(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_tool_generic_handle_normal(struct WinGizmoGroupType *gzgt);
void VIEW3D_GGT_tool_generic_handle_free(struct WinGizmoGroupType *gzgt);

void VIEW3D_GGT_ruler(struct WinGizmoGroupType *gzgt);
void VIEW3D_GT_ruler_item(struct WinGizmoType *gzt);
void VIEW3D_OT_ruler_add(struct WinOpType *ot);
void VIEW3D_OT_ruler_remove(struct WinOpType *ot);

void VIEW3D_GT_nav_rotate(struct WinGizmoType *gzt);

void VIEW3D_GGT_placement(struct WinGizmoGroupType *gzgt);

/* workaround for trivial but noticeable camera bug caused by imprecision
 * between view border calculation in 2D/3D space, workaround for bug T28037.
 * without this define we get the old behavior which is to try and align them
 * both which _mostly_ works fine, but when the camera moves beyond ~1000 in
 * any direction it starts to fail */
#define VIEW3D_CAMERA_BORDER_HACK
#ifdef VIEW3D_CAMERA_BORDER_HACK
extern uchar view3d_camera_border_hack_col[3];
extern bool view3d_camera_border_hack_test;
#endif
