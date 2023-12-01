#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Ob;
struct ApiStruct;
struct Cxt;
struct WinOp;
struct WinOpType;

struct ModData;

/* add hook menu */
enum eObHookAddMode {
  OB_ADDHOOK_NEWOB = 1,
  OB_ADDHOOK_SELOB,
  OB_ADDHOOK_SELOB_BONE,
};

/* internal exports only */
/* ob_transform.cc */
void OB_OT_location_clear(struct WinOpType *ot);
void OB_OT_rotation_clear(struct WinOpType *ot);
void OB_OT_scale_clear(struct WinOpType *ot);
void OB_OT_origin_clear(struct WinOpType *ot);
void OB_OT_visual_transform_apply(struct WinOpType *ot);
void OB_OT_transform_apply(struct WinOpType *ot);
void OB_OT_parent_inverse_apply(WinOpType *ot);
void OB_OT_transform_axis_target(struct WinOpType *ot);
void OB_OT_origin_set(struct WinOpType *ot);

/* `ob_relations.cc` */
void OB_OT_parent_set(struct WinOpType *ot);
void OB_OT_parent_no_inverse_set(struct WinOpType *ot);
void OB_OT_parent_clear(struct WinOpType *ot);
void OB_OT_vertex_parent_set(struct WinOpType *ot);
void OB_OT_track_set(struct WinOpType *ot);
void OB_OT_track_clear(struct WinOpType *ot);
void OB_OT_make_local(struct WinOpType *ot);
void OB_OT_make_single_user(struct WinOpType *ot);
void OB_OT_make_links_scene(struct WinOpType *ot);
void OB_OT_make_links_data(struct WinOpType *ot);

void OB_OT_make_override_lib(struct WinOpType *ot);
void OB_OT_reset_override_lib(struct WinOpType *ot);
void OB_OT_clear_override_lib(struct WinOpType *ot);

/* Used for drop-box.
 * Assigns to ob under cursor, only first material slot. */
void O_OT_drop_named_material(struct WinOpType *ot);
/* Used for drop-box.
 * Assigns to ob under cursor, creates a new geometry nodes mod */
void OB_OT_drop_geometry_nodes(struct WinOpType *ot);
/* Op is only for empty img obs */
void OB_OT_unlink_data(struct WinOpType *ot);

/* ob_edit.cc */
void OB_OT_hide_view_set(struct WinOpType *ot);
void OB_OT_hide_view_clear(struct WinOpType *ot);
void OB_OT_hide_collection(struct WinOpType *ot);
void OB_OT_mode_set(struct WinOpType *ot);
void OB_OT_mode_set_with_submode(struct WinOpType *ot);
void OB_OT_editmode_toggle(struct WinOpType *ot);
void OB_OT_posemode_toggle(struct WinOpType *ot);
void OB_OT_shade_smooth(struct WinOpType *ot);
void OB_OT_shade_smooth_by_angle(struct WinOpType *ot);
void OB_OT_shade_flat(struct WinOpType *ot);
void OB_OT_paths_calc(struct WinOpType *ot);
void OB_OT_paths_update(struct WinOpType *ot);
void OB_OT_paths_clear(struct WinOpType *ot);
void OB_OT_paths_update_visible(struct WinOpType *ot);
void OB_OT_forcefield_toggle(struct WinOpType *ot);

void OB_OT_move_to_collection(struct WinOpType *ot);
void OB_OT_link_to_collection(struct WinOpType *ot);

void OB_OT_transfer_mode(struct WinOpType *ot);

/* `ob_sel.cc` */
void OB_OT_sel_all(struct WinOpType *ot);
void OB_OT_sel_random(struct WinOpType *ot);
void OB_OT_sel_by_type(struct WinOpType *ot);
void OB_OT_sel_linked(struct WinOpType *ot);
void OB_OT_sel_grouped(struct WinOpType *ot);
void OB_OT_sel_mirror(struct WinOpType *ot);
void OB_OT_sel_more(struct WinOpType *ot);
void OB_OT_sel_less(struct WinOpType *ot);
void OB_OT_sel_same_collection(struct WinOpType *ot);

/* ob_add.cc */
void OB_OT_add(struct WinOpType *ot);
void OB_OT_add_named(struct WinOpType *ot);
void OB_OT_transform_to_mouse(struct WinOpType *ot);
void OB_OT_metaball_add(struct WinOpType *ot);
void OB_OT_txt_add(struct WinOpType *ot);
void OB_OT_armature_add(struct WinOpType *ot);
void OB_OT_empty_add(struct WinOpType *ot);
void OB_OT_lightprobe_add(struct WinOpType *ot);
void OB_OT_drop_named_img(struct WinOpType *ot);
void OB_OT_pen_add(struct WinOpType *ot);
void OB_OT_pen_add(struct WinOpType *ot);
void OB_OT_light_add(struct WinOpType *ot);
void OB_OT_effector_add(struct WinOpType *ot);
void OB_OT_camera_add(struct WinOpType *ot);
void OB_OT_speaker_add(struct WinOpType *ot);
void OB_OT_curves_random_add(struct WinOpType *ot);
void OB_OT_curves_empty_hair_add(struct WinOpType *ot);
void OB_OT_pointcloud_add(struct WinOpType *ot);
/* Only used as menu. */
void OB_OT_collection_instance_add(struct WinOpType *ot);
void OB_OT_collection_external_asset_drop(struct WinOpType *ot);
void OB_OT_data_instance_add(struct WinOpType *ot);

void OB_OT_dups_make_real(struct WinOpType *ot);
void OB_OT_dup(struct WinOpType *ot);
void OB_OT_delete(struct WinOpType *ot);
void OB_OT_join(struct WinOpType *ot);
void OB_OT_join_shapes(struct WinOpType *ot);
void OB_OT_convert(struct WinOpType *ot);

/* `ob_volume.cc` */
void OB_OT_volume_add(struct WinOpType *ot);
/* Called by other space types too */
void OB_OT_volume_import(struct WinOpType *ot);

/* `ob_hook.cc` */
void OB_OT_hook_add_selob(struct WinOpType *ot);
void OB_OT_hook_add_newob(struct WinOpType *ot);
void OB_OT_hook_remove(struct WinOpType *ot);
void OB_OT_hook_sel(struct WinOpType *ot);
void OB_OT_hook_assign(struct WinOpType *ot);
void OB_OT_hook_reset(struct WinOpType *ot);
void OB_OT_hook_recenter(struct WinOpType *ot);

/* `ob_collection.cc` */
void COLLECTION_OT_create(struct WinOpType *ot);
void COLLECTION_OT_obs_remove_all(struct WinOpType *ot);
void COLLECTION_OT_obs_remove(struct WinOpType *ot);
void COLLECTION_OT_obs_add_active(struct WinOpType *ot);
void COLLECTION_OT_obs_remove_active(struct WinOpType *ot);

/* ob_light_linking_ops.cc */
void OB_OT_light_linking_receiver_collection_new(struct WinOpType *ot);
void OB_OT_light_linking_receivers_sel(struct WinOpType *ot);
void OB_OT_light_linking_receivers_link(struct WinOpType *ot);

void OB_OT_light_linking_blocker_collection_new(struct WinOpType *ot);
void OB_OT_light_linking_blockers_sel(struct WinOpType *ot);
void OB_OT_light_linking_blockers_link(struct WinOpType *ot);

void OB_OT_light_linking_unlink_from_collection(struct WinOpType *ot);

/* `ob_mod.cc` */
bool edit_mod_poll_generic(struct Cxt *C,
                           struct ApiStruct *api_type,
                           int obtype_flag,
                           bool is_editmode_allowed,
                           bool is_liboverride_allowed);
void edit_mod_props(struct WinOpType *ot);
bool edit_mod_invoke_props(struct Cxt *C, struct WinOp *op);

struct ModData *edit_mod_prop_get(struct WinOp *op,
                                  struct Ob *ob,
                                  int type);

void OB_OT_mod_add(struct WinOpType *ot);
void OB_OT_mod_remove(struct WinOpType *ot);
void OB_OT_mod_move_up(struct WinOpType *ot);
void OB_OT_mod_move_down(struct WinOpType *ot);
void OB_OT_mod_move_to_index(struct WinOpType *ot);
void OB_OT_mod_apply(struct WinOpType *ot);
void OB_OT_mod_apply_as_shapekey(WinOpType *ot);
void OB_OT_mod_convert(struct WinOpType *ot);
void OB_OT_mod_copy(struct WinOpType *ot);
void OB_OT_mod_copy_to_sel(struct WinOpType *ot);
void OB_OT_mod_set_active(struct WinOpType *ot);
void OB_OT_multires_subdivide(struct WinOpType *ot);
void OB_OT_multires_reshape(struct WinOpType *ot);
void OB_OT_multires_higher_levels_delete(struct WinOpType *ot);
void OB_OT_multires_base_apply(struct WinOpType *ot);
void OB_OT_multires_unsubdivide(struct WinOpType *ot);
void OB_OT_multires_rebuild_subdiv(struct WinOpType *ot);
void OB_OT_multires_external_save(struct WinOpType *ot);
void OB_OT_multires_external_pack(struct WinOpType *ot);
void OB_OT_correctivesmooth_bind(struct WinOpType *ot);
void OB_OT_meshdeform_bind(struct WinOpType *ot);
void OB_OT_explode_refresh(struct WinOpType *ot);
void OB_OT_ocean_bake(struct WinOpType *ot);
void OB_OT_skin_root_mark(struct WinOpType *ot);
void OB_OT_skin_loose_mark_clear(struct WinOpType *ot);
void OB_OT_skin_radii_equalize(struct WinOpType *ot);
void OB_OT_skin_armature_create(struct WinOpType *ot);
void OB_OT_laplaciandeform_bind(struct WinOpType *ot);
void OB_OT_surfacedeform_bind(struct WinOpType *ot);
void OB_OT_geometry_nodes_input_attribute_toggle(struct WinOpType *ot);
void OB_OT_geometry_node_tree_copy_assign(struct WinOpType *ot);

/* ob_pen_mods.c */
void OB_OT_pen_mod_add(struct WinOpType *ot);
void OB_OT_pen_mod_remove(struct WinOpType *ot);
void OB_OT_pen_mod_move_up(struct WinOpType *ot);
void OB_OT_pen_mod_move_down(struct WinOpType *ot);
void OB_OT_pen_mod_move_to_index(struct WinOpType *ot);
void OB_OT_pen_mod_apply(struct WinOpType *ot);
void OB_OT_pen_mod_copy(struct WinOpType *ot);
void OB_OT_pen_mod_copy_to_sel(struct WinOpType *ot);

void PEN_OT_segment_add(struct WinOpType *ot);
void PEN_OT_segment_remove(struct WinOpType *ot);
void PEN_OT_segment_move(struct WinOpType *ot);

void PEN_OT_time_segment_add(struct WinOpType *ot);
void PEN_OT_time_segment_remove(struct WinOpType *ot);
void PEN_OT_time_segment_move(struct WinOpType *ot);

/* `ob_shader_fx.cc` */
void OB_OT_shaderfx_add(struct WinOpType *ot);
void OB_OT_shaderfx_copy(struct WinOpType *ot);
void OB_OT_shaderfx_remove(struct WinOpType *ot);
void OB_OT_shaderfx_move_up(struct WinOpType *ot);
void OB_OT_shaderfx_move_down(struct WinOpType *ot);
void OB_OT_shaderfx_move_to_index(struct WinOpType *ot);

/* `ob_constraint.cc` */
void OB_OT_constraint_add(struct WinOpType *ot);
void OB_OT_constraint_add_with_targets(struct WinOpType *ot);
void POSE_OT_constraint_add(struct WinOpType *ot);
void POSE_OT_constraint_add_with_targets(struct WinOpType *ot);

void OB_OT_constraints_copy(struct WinOpType *ot);
void POSE_OT_constraints_copy(struct WinOpType *ot);

void OB_OT_constraints_clear(struct WinOpType *ot);
void POSE_OT_constraints_clear(struct WinOpType *ot);

void POSE_OT_ik_add(struct WinOpType *ot);
void POSE_OT_ik_clear(struct WinOpType *ot);

void CONSTRAINT_OT_delete(struct WinOpType *ot);
void CONSTRAINT_OT_apply(struct WinOpType *ot);
void CONSTRAINT_OT_copy(struct WinOpType *ot);
void CONSTRAINT_OT_copy_to_sel(struct WinOpType *ot);

void CONSTRAINT_OT_move_up(struct WinOpType *ot);
void CONSTRAINT_OT_move_to_index(struct WinOpType *ot);
void CONSTRAINT_OT_move_down(struct WinOpType *ot);

void CONSTRAINT_OT_stretchto_reset(struct WinOpType *ot);
void CONSTRAINT_OT_limitdistance_reset(struct WinOpType *ot);
void CONSTRAINT_OT_childof_set_inverse(struct WinOpType *ot);
void CONSTRAINT_OT_childof_clear_inverse(struct WinOpType *ot);
void CONSTRAINT_OT_obsolver_set_inverse(struct WinOpType *ot);
void CONSTRAINT_OT_obsolver_clear_inverse(struct WinOpType *ot);
void CONSTRAINT_OT_followpath_path_anim(struct WinOpType *ot);

/* ob_vgroup.cc */
void OB_OT_vertex_group_add(struct WinOpType *ot);
void OB_OT_vertex_group_remove(struct WinOpType *ot);
void OB_OT_vertex_group_assign(struct WinOpType *ot);
void OB_OT_vertex_group_assign_new(struct WinOpType *ot);
void OB_OT_vertex_group_remove_from(struct WinOpType *ot);
void OB_OT_vertex_group_sel(struct WinOpType *ot);
void OB_OT_vertex_group_desel(struct WinOpType *ot);
void OB_OT_vertex_group_copy_to_sel(struct WinOpType *ot);
void OB_OT_vertex_group_copy(struct WinOpType *ot);
void OB_OT_vertex_group_normalize(struct WinOpType *ot);
void OB_OT_vertex_group_normalize_all(struct WinOpType *ot);
void OB_OT_vertex_group_levels(struct WinOpType *ot);
void OB_OT_vertex_group_lock(struct WinOpType *ot);
void OB_OT_vertex_group_invert(struct WinOpType *ot);
void OB_OT_vertex_group_smooth(struct WinOpType *ot);
void OB_OT_vertex_group_clean(struct WinOpType *ot);
void OB_OT_vertex_group_quantize(struct WinOpType *ot);
void OB_OT_vertex_group_limit_total(struct WinOpType *ot);
void OB_OT_vertex_group_mirror(struct WinOpType *ot);
void OB_OT_vertex_group_set_active(struct WinOpType *ot);
void OB_OT_vertex_group_sort(struct WinOpType *ot);
void OB_OT_vertex_group_move(struct WinOpType *ot);
void OB_OT_vertex_weight_paste(struct WinOpType *ot);
void OB_OT_vertex_weight_delete(struct WinOpType *ot);
void OB_OT_vertex_weight_set_active(struct WinOpType *ot);
void OB_OT_vertex_weight_normalize_active_vertex(struct WinOpType *ot);
void OB_OT_vertex_weight_copy(struct WinOpType *ot);

/* `ob_warp.cc` */
void TRANSFORM_OT_vertex_warp(struct WinOpType *ot);

/* `ob_shapekey.cc` */
void OB_OT_shape_key_add(struct WinOpType *ot);
void OB_OT_shape_key_remove(struct WinOpType *ot);
void OB_OT_shape_key_clear(struct WinOpType *ot);
void OB_OT_shape_key_retime(struct WinOpType *ot);
void OB_OT_shape_key_mirror(struct WinOpType *ot);
void OB_OT_shape_key_move(struct WinOpType *ot);

/* `ob_collection.cc` */
void OB_OT_collection_add(struct WjnOpType *ot);
void OB_OT_collection_link(struct WinOpType *ot);
void OB_OT_collection_remove(struct WinOpType *ot);
void OB_OT_collection_unlink(struct WinOpType *ot);
void OB_OT_collection_obs_sel(struct WinOpType *ot);

/* `ob_bake.cc` */
void OB_OT_bake_img(WinOpType *ot);
void OB_OT_bake(WinOpType *ot);

/* ob_bake_sim.cc */
void OB_OT_sim_nodes_cache_calc_to_frame(WinOpType *ot);
void OB_OT_sim_nodes_cache_bake(WinOpType *ot);
void OB_OT_sim_nodes_cache_delete(WinOpType *ot);
void OB_OT_sim_nodes_cache_bake_single(WinOpType *ot);
void OB_OT_sim_nodes_cache_delete_single(WinOpType *ot);

/* `ob_random.cc` */
void TRANSFORM_OT_vertex_random(struct WinOpType *ot);

/* ob_remesh.cc */
void OB_OT_voxel_remesh(struct WinOpType *ot);
void OB_OT_voxel_size_edit(struct WinOpType *ot);
void OB_OT_quadriflow_remesh(struct WinOpType *ot);

/* ob_transfer_data.c */
/* Transfer mesh data from active to sel obs */
void OB_OT_data_transfer(struct WinOpType *ot);
void OB_OT_datalayout_transfer(struct WinOpType *ot);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace blender::ed::object {

void object_modifier_add_asset_register();

}

#endif
