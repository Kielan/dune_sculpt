/** \file
 * \ingroup edmesh
 */

/* Internal for editmesh_xxxx.c functions */

#pragma once

struct MEditMesh;
struct MElem;
struct MOp;
struct EnumPropItem;
struct LinkNode;
struct Cx;
struct wmKeyConfig;
struct wmKeyMap;
struct wmOp;
struct wmOpType;

/* editmesh_utils.c */
/* ok: the edm module is for editmode mesh stuff. In contrast, the
 * MEdit module is for code shared with dune core (kernel) that concerns
 * the MEditMesh struct. */

/* Calls a mesh op, reporting errs to the user, etc. */
bool edm_op_callf(struct MEditMesh *em, struct wmOp *op, const char *fmt, ...);
bool edm_op_call_and_self(struct MEditMesh *em,
                          struct wmOp *op,
                          const char *sel_slot,
                          bool sel_replace,
                          const char *fmt,
                          ...);
/* Same as above, but doesn't report errors. */
bool edm_op_call_silentf(struct DuneMeshEdit *em, const char *fmt, ...);

/* These next two fns are the split version of edm_op_callf, so you can
 * do stuff with a mesh op, after initializing it but before executing it.
 *
 * execute the op w M_Exec_Op */
bool edm_op_init(
    struct DuneMeshEdit *em, struct MOp *mop, struct wmOp *op, const char *fmt, ...);

/* Cleans up after a mesh op.
 *
 * The return value:
 * - False on error (the mesh must not be changed).
 * - True on success, executes and finishes a DuneMesh op. */
bool edm_op_finish(struct DuneMeshEdit *em,
                    struct DuneMeshOp *mop,
                    struct wmOp *op,
                    bool do_report);

void edm_stats_update(struct DuneMeshEdit *em);

/* Poll call for mesh operators requiring a view3d context. */
bool edm_view3d_poll(struct Cx *C);

struct DuneMeshElem *edm_elem_from_selmode(struct DuneMeshEdit *em,
                                         struct DuneMeshVert *eve,
                                         struct DuneMeshEdge *eed,
                                         struct DuneMeshFace *efa);

/* Used when we want to store a single index for any vert/edge/face.
 *
 * Intended for use with ops. */
int edm_elem_to_idx_any(struct DuneMeshEdit *em, struct DuneMeshElem *ele);
struct DuneMeshElem *edm_elem_from_idx_any(struct DuneMeshEdit *em, uint idx);

int edm_elem_to_idx_any_multi(struct ViewLayer *view_layer,
                                 struct DuneMeshEdit *em,
                                 struct DuneMeshElem *ele,
                                 int *r_object_index);
struct DuneMeshElem *edm_elem_from_idx_any_multi(struct ViewLayer *view_layer,
                                              uint object_idx,
                                              uint elem_idx,
                                              struct Object **r_obedit);

/* Extrudes individual edges. */
bool edm_extrude_edges_indiv(struct DuneMeshEdit *duneMeshEdit,
                            struct wmOp *op,
                            char hflag,
                            bool use_normal_flip);

/* editmesh_add.c */
void mesh_ot_primitive_plane_add(struct wmOpType *ot);
void mesh_ot_primitive_cube_add(struct wmOpType *ot);
void mesh_ot_primitive_circle_add(struct wmOpType *ot);
void mesh_ot_primitive_cylinder_add(struct wmOpType *ot);
void mesh_ot_primitive_cone_add(struct wmOpType *ot);
void mesh_ot_primitive_grid_add(struct wmOpType *ot);
void mesh_ot_primitive_monkey_add(struct wmOpType *ot);
void mesh_ot_primitive_uv_sphere_add(struct wmOpType *ot);
void mesh_ot_primitive_ico_sphere_add(struct wmOpType *ot);

/* editmesh_add_gizmo.c */
void mesh_ot_primitive_cube_add_gizmo(struct wmOpType *ot);

/* editmesh_bevel.c */
void mesh_ot_bevel(struct wmOpType *ot);
struct wmKeyMap *bevel_modal_keymap(struct wmKeyConfig *keyconf);

/* editmesh_bisect.c */
void mesh_ot_bisect(struct wmOpType *ot);

/* editmesh_extrude.c */
void mesh_ot_extrude_repeat(struct wmOpType *ot);
void mesh_ot_extrude_rgn(struct wmOpType *ot);
void mesh_ot_extrude_cx(struct wmOpType *ot);
void mesh_ot_extrude_verts_indiv(struct wmOpType *ot);
void mesh_ot_extrude_edges_indiv(struct wmOpType *ot);
void mesh_ot_extrude_faces_indiv(struct wmOpType *ot);
void mesh_ot_dupli_extrude_cursor(struct wmOpType *ot);

/* editmesh_extrude_screw.c *** */
void mesh_ot_screw(struct wmOpType *ot);

/* editmesh_extrude_spin.c *** */
void mesh_ot_spin(struct wmOpType *ot);

/* editmesh_extrude_spin_gizmo.c *** */

void mesh_ggt_spin(struct wmGizmoGroupType *gzgt);
void mesh_ggt_spin_redo(struct wmGizmoGroupType *gzgt);

/* editmesh_polybuild.c *** */
void mesh_ot_polybuild_face_at_cursor(struct wmOpType *ot);
void mesh_ot_polybuild_split_at_cursor(struct wmOpType *ot);
void mesh_ot_polybuild_dissolve_at_cursor(struct wmOpType *ot);
void mesh_ot_polybuild_transform_at_cursor(struct wmOpType *ot);
void mesh_ot_polybuild_delete_at_cursor(struct wmOpType *ot);

/* editmesh_inset.c */
void MESH_OT_inset(struct wmOpType *ot);

/* *** editmesh_intersect */
void MESH_OT_intersect(struct wmOpType *ot);
void MESH_OT_intersect_boolean(struct wmOpType *ot);
void MESH_OT_face_split_by_edges(struct wmOpType *ot);

/* editmesh_knife.c *** */
void mesh_ot_knife_tool(struct wmOpType *ot);
void mesh_ot_knife_project(struct wmOpType *ot);
/* param use_tag: When set, tag all faces inside the polylines. */
void edm_mesh_knife(struct Cx *C,
                     struct ViewCx *vc,
                     struct LinkNode *polys,
                     bool use_tag,
                     bool cut_through);

struct wmKeyMap *knifetool_modal_keymap(struct wmKeyConfig *keyconf);

/* editmesh_loopcut.c */
void mesh_ot_loopcut(struct wmOpType *ot);

/* editmesh_rip.c */
void mesh_ot_rip(struct wmOpType *ot);
void mesh_ot_rip_edge(struct wmOpType *ot);

/* editmesh_select.c *** */
void mesh_ot_sel_similar(struct wmOpType *ot);
void mesh_ot_sel_similar_rgn(struct wmOpType *ot);
void mesh_ot_sel_mode(struct wmOpType *ot);
void mesh_ot_loop_multi_sel(struct wmOpType *ot);
void mesh_ot_loop_sel(struct wmOpType *ot);
void mesh_ot_edgering_sel(struct wmOpType *ot);
void mesh_ot_select_all(struct wmOpType *ot);
void mesh_ot_select_interior_faces(struct wmOpType *ot);
void mesh_ot_shortest_path_pick(struct wmOpType *ot);
void mesh_ot_sel_linked(struct wmOpType *ot);
void mesh_ot_sel_linked_pick(struct wmOpType *ot);
void mesh_ot_sel_face_by_sides(struct wmOpType *ot);
void mesh_ot_sel_loose(struct wmOpType *ot);
void mesh_ot_sel_mirror(struct wmOpType *ot);
void mesh_ot_sel_more(struct wmOpType *ot);
void mesh_ot_sel_less(struct wmOpType *ot);
void mesh_ot_sel_nth(struct wmOpType *ot);
void mesh_ot_edges_sel_sharp(struct wmOpType *ot);
void mesh_ot_faces_sel_linked_flat(struct wmOpType *ot);
void mesh_ot_sel_non_manifold(struct wmOpType *ot);
void mesh_ot_sel_random(struct wmOpType *ot);
void mesh_ot_sel_ungrouped(struct wmOpType *ot);
void mesh_ot_sel_axis(struct wmOpType *ot);
void mesh_ot_rgn_to_loop(struct wmOpType *ot);
void mesh_OT_loop_to_rgn(struct wmOpType *ot);
void mesh_ot_shortest_path_sel(struct wmOpType *ot);

extern struct EnumPropItem *corner_type_items;

/* editmesh_tools.c *** */
void mesh_ot_subdivide(struct wmOpType *ot);
void mesh_ot_subdivide_edgering(struct wmOpType *ot);
void mesh_ot_unsubdivide(struct wmOpType *ot);
void mesh_ot_normals_make_consistent(struct wmOpType *ot);
void mesh_ot_verts_smooth(struct wmOpType *ot);
void mesh_OT_verts_smooth_laplacian(struct wmOpType *ot);
void mesh_OT_vert_connect(struct wmOpType *ot);
void mesh_OT_vert_connect_path(struct wmOpType *ot);
void mesh_OT_vert_connect_concave(struct wmOpType *ot);
void mesh_OT_vert_connect_nonplanar(struct wmOpType *ot);
void mesh_OT_face_make_planar(struct wmOpType *ot);
void mesh_OT_edge_split(struct wmOpType *ot);
void mesh_OT_bridge_edge_loops(struct wmOpType *ot);
void mesh_OT_offset_edge_loops(struct wmOpType *ot);
void mesh_OT_wireframe(struct wmOpType *ot);
void mesh_OT_convex_hull(struct wmOpType *ot);
void mesh_OT_symmetrize(struct wmOpType *ot);
void mesh_OT_symmetry_snap(struct wmOpType *ot);
void mesh_OT_shape_propagate_to_all(struct wmOpType *ot);
void mesh_OT_blend_from_shape(struct wmOpType *ot);
void mesh_OT_sort_elements(struct wmOpType *ot);
void mesh_OT_uvs_rotate(struct wmOpType *ot);
void mesh_OT_uvs_reverse(struct wmOpType *ot);
void mesh_OT_colors_rotate(struct wmOpType *ot);
void MESH_OT_colors_reverse(struct wmOpType *ot);
void MESH_OT_delete(struct wmOpType *ot);
void MESH_OT_delete_loose(struct wmOpType *ot);
void MESH_OT_edge_collapse(struct wmOpeType *ot);
void MESH_OT_faces_shade_smooth(struct wmOpType *ot);
void MESH_OT_faces_shade_flat(struct wmOpType *ot);
void MESH_OT_split(struct wmOpType *ot);
void MESH_OT_edge_rotate(struct wmOpType *ot);
void MESH_OT_hide(struct wmOpType *ot);
void MESH_OT_reveal(struct wmOpType *ot);
void MESH_OT_mark_seam(struct wmOpType *ot);
void MESH_OT_mark_sharp(struct wmOpType *ot);
void MESH_OT_flip_normals(struct wmOpType *ot);
void MESH_OT_solidify(struct wmOpType *ot);
void MESH_OT_knife_cut(struct wmOpType *ot);
void MESH_OT_separate(struct wmOpType *ot);
void MESH_OT_fill(struct wmOpType *ot);
void MESH_OT_fill_grid(struct wmOpType *ot);
void MESH_OT_fill_holes(struct wmOpType *ot);
void MESH_OT_beautify_fill(struct wmOpType *ot);
void MESH_OT_quads_convert_to_tris(struct wmOpType *ot);
void MESH_OT_tris_convert_to_quads(struct wmOpType *ot);
void MESH_OT_decimate(struct wmOpType *ot);
void MESH_OT_dissolve_verts(struct wmOpType *ot);
void MESH_OT_dissolve_edges(struct wmOpType *ot);
void MESH_OT_dissolve_faces(struct wmOpType *ot);
void MESH_OT_dissolve_mode(struct wmOpType *ot);
void MESH_OT_dissolve_limited(struct wmOpType *ot);
void MESH_OT_dissolve_degenerate(struct wmOpType *ot);
void MESH_OT_delete_edgeloop(struct wmOpType *ot);
void MESH_OT_edge_face_add(struct wmOpType *ot);
void MESH_OT_duplicate(struct wmOpType *ot);
void MESH_OT_merge(struct wmOpType *ot);
void MESH_OT_remove_doubles(struct wmOpType *ot);
void MESH_OT_poke(struct wmOpType *ot);
void MESH_OT_point_normals(struct wmOpType *ot);
void MESH_OT_merge_normals(struct wmOpType *ot);
void MESH_OT_split_normals(struct wmOpType *ot);
void mesh_OT_normals_tools(struct wmOpType *ot);
void mesh_OT_set_normals_from_faces(struct wmOpType *ot);
void mesh_OT_average_normals(struct wmOpType *ot);
void mesh_OT_smooth_normals(struct wmOpType *ot);
void mesh_OT_mod_weighted_strength(struct wmOpType *ot);

/* *** editmesh_mask_extract.c */
void MESH_OT_paint_mask_extract(struct wmOpType *ot);
void MESH_OT_face_set_extract(struct wmOpType *ot);
void MESH_OT_paint_mask_slice(struct wmOpType *ot);

/** Called in transform_ops.c, on each regeneration of key-maps. */
struct wmKeyMap *point_normals_modal_keymap(wmKeyConfig *keyconf);

#if defined(WITH_FREESTYLE)
void MESH_OT_mark_freestyle_edge(struct wmOpType *ot);
void MESH_OT_mark_freestyle_face(struct wmOpType *ot);
#endif

/* *** mesh_data.c *** */
void MESH_OT_uv_texture_add(struct wmOperatorType *ot);
void MESH_OT_uv_texture_remove(struct wmOperatorType *ot);
void MESH_OT_vertex_color_add(struct wmOperatorType *ot);
void MESH_OT_vertex_color_remove(struct wmOperatorType *ot);
void MESH_OT_sculpt_vertex_color_add(struct wmOperatorType *ot);
void MESH_OT_sculpt_vertex_color_remove(struct wmOperatorType *ot);
void MESH_OT_customdata_mask_clear(struct wmOperatorType *ot);
void MESH_OT_customdata_skin_add(struct wmOperatorType *ot);
void MESH_OT_customdata_skin_clear(struct wmOperatorType *ot);
void MESH_OT_customdata_custom_splitnormals_add(struct wmOperatorType *ot);
void MESH_OT_customdata_custom_splitnormals_clear(struct wmOperatorType *ot);
