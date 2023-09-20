/* Defines in this header are only used to define dune file storage.
 * This allows us to rename variables & structs without breaking compatibility.
 *
 * - When renaming the member of a struct which has itself been renamed
 *   refer to the newer name, not the original.
 *
 * - Changes here only change generated code for `types.c` and `api.c`
 *   without impacting Dune's run-time, besides allowing us to use the new names.
 *
 * - Renaming something that has already been renamed can be done
 *   by editing the existing rename macro.
 *   All refs to the previous destination name can be removed since they're
 *   never written to disk.
 *
 * - Old names aren't sanity checked (since this file is the only place that knows about them)
 *   typos in the old names will break both backwards & forwards compatibility **TAKE CARE**.
 *
 * - Before editing rename defines run:
 *
 *   `sha1sum $BUILD_DIR/source/dune/types/intern/types.c`
 *
 *   Compare the results before & after to ensure all changes are reversed by renaming
 *   and the types remains unchanged.
 *
 * see versioning_types.c for actual version patching. */

/* No include guard (intentional). */
/* Match Api names where possible. */
/* NOTE: Keep sorted! */
TYPES_STRUCT_RENAME(Lamp, Light)
TYPES_STRUCT_RENAME(Spacetns, SpaceProps)
TYPES_STRUCT_RENAME(SpaceIpo, SpaceGraph)
TYPES_STRUCT_RENAME(SpaceOops, SpaceOutliner)
TYPES_STRUCT_RENAME_ELEM(Point, alfa, tilt)
TYPES_STRUCT_RENAME_ELEM(BezTriple, alfa, tilt)
TYPES_STRUCT_RENAME_ELEM(Bone, curveInX, curve_in_x)
TYPES_STRUCT_RENAME_ELEM(Bone, curveInY, curve_in_z)
TYPES_STRUCT_RENAME_ELEM(Bone, curveOutX, curve_out_x)
TYPES_STRUCT_RENAME_ELEM(Bone, curveOutY, curve_out_z)
TYPES_STRUCT_RENAME_ELEM(Bone, scaleIn, scale_in_x)
TYPES_STRUCT_RENAME_ELEM(Bone, scaleOut, scale_out_x)
TYPES_STRUCT_RENAME_ELEM(Bone, scale_in_y, scale_in_z)
TYPES_STRUCT_RENAME_ELEM(Bone, scale_out_y, scale_out_z)
TYPES_STRUCT_RENAME_ELEM(BrushPenSettings, gradient_f, hardeness)
TYPES_STRUCT_RENAME_ELEM(BrushPenSettings, gradient_s, aspect_ratio)
TYPES_STRUCT_RENAME_ELEM(Camera, YF_dofdist, dof_distance)
TYPES_STRUCT_RENAME_ELEM(Camera, clipend, clip_end)
TYPES_STRUCT_RENAME_ELEM(Camera, clipsta, clip_start)
TYPES_STRUCT_RENAME_ELEM(Collection, dupli_ofs, instance_offset)
TYPES_STRUCT_RENAME_ELEM(Curve, ext1, extrude)
TYPES_STRUCT_RENAME_ELEM(Curve, ext2, bevel_radius)
TYPES_STRUCT_RENAME_ELEM(Curve, len_wchar, len_char32)
TYPES_STRUCT_RENAME_ELEM(Curve, width, offset)
TYPES_STRUCT_RENAME_ELEM(Editing, over_border, overlay_frame_rect)
TYPES_STRUCT_RENAME_ELEM(Editing, over_cfra, overlay_frame_abs)
TYPES_STRUCT_RENAME_ELEM(Editing, over_flag, overlay_frame_flag)
TYPES_STRUCT_RENAME_ELEM(Editing, over_ofs, overlay_frame_ofs)
TYPES_STRUCT_RENAME_ELEM(FileGlobal, filename, filepath)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, cache_frame_pause_guiding, cache_frame_pause_guide)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, guiding_alpha, guide_alpha)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, guiding_beta, guide_beta)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, guiding_parent, guide_parent)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, guiding_source, guide_source)
TYPES_STRUCT_RENAME_ELEM(FluidDomainSettings, guiding_vel_factor, guide_vel_factor)
TYPES_STRUCT_RENAME_ELEM(FluidEffectorSettings, guiding_mode, guide_mode)
TYPES_STRUCT_RENAME_ELEM(Image, name, filepath)
TYPES_STRUCT_RENAME_ELEM(Lib, name, filepath)
TYPES_STRUCT_RENAME_ELEM(LineartPenModData, line_types, edge_types)
TYPES_STRUCT_RENAME_ELEM(LineartPenModData, transparency_flags, mask_switches)
TYPES_STRUCT_RENAME_ELEM(LineartPenModData, transparency_mask, material_mask_bits)
TYPES_STRUCT_RENAME_ELEM(MaskLayer, restrictflag, visibility_flag)
TYPES_STRUCT_RENAME_ELEM(MaterialLineArt, transparency_mask, material_mask_bits)
TYPES_STRUCT_RENAME_ELEM(MovieClip, name, filepath)
TYPES_STRUCT_RENAME_ELEM(Object, col, color)
DNA_STRUCT_RENAME_ELEM(Object, dup_group, instance_collection)
DNA_STRUCT_RENAME_ELEM(Object, dupfacesca, instance_faces_scale)
DNA_STRUCT_RENAME_ELEM(Object, restrictflag, visibility_flag)
DNA_STRUCT_RENAME_ELEM(Object, size, scale)
DNA_STRUCT_RENAME_ELEM(ParticleSettings, dup_group, instance_collection)
DNA_STRUCT_RENAME_ELEM(ParticleSettings, dup_ob, instance_object)
DNA_STRUCT_RENAME_ELEM(ParticleSettings, dupliweights, instance_weights)
DNA_STRUCT_RENAME_ELEM(RigidBodyWorld, steps_per_second, substeps_per_frame)
DNA_STRUCT_RENAME_ELEM(RenderData, bake_filter, bake_margin)
DNA_STRUCT_RENAME_ELEM(SpaceSeq, overlay_type, overlay_frame_type)
TYPES_STRUCT_RENAME_ELEM(SurfaceDeformModData, numverts, num_bind_verts)
TYPES_STRUCT_RENAME_ELEM(Text, name, filepath)
TYPES_STRUCT_RENAME_ELEM(ThemeSpace, scrubbing_background, time_scrub_background)
TYPES_STRUCT_RENAME_ELEM(ThemeSpace, show_back_grad, background_type)
TYPES_STRUCT_RENAME_ELEM(UserDef, gp_manhattendist, gp_manhattandist)
TYPES_STRUCT_RENAME_ELEM(VFont, name, filepath)
TYPES_STRUCT_RENAME_ELEM(View3D, far, clip_end)
TYPES_STRUCT_RENAME_ELEM(View3D, near, clip_start)
TYPES_STRUCT_RENAME_ELEM(View3D, ob_centre, ob_center)
TYPES_STRUCT_RENAME_ELEM(View3D, ob_centre_bone, ob_center_bone)
TYPES_STRUCT_RENAME_ELEM(View3D, ob_centre_cursor, ob_center_cursor)
TYPES_STRUCT_RENAME_ELEM(Penstroke, gradient_f, hardeness)
TYPES_STRUCT_RENAME_ELEM(Penstroke, gradient_s, aspect_ratio)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, curveInX, curve_in_x)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, curveInY, curve_in_z)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, curveOutX, curve_out_x)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, curveOutY, curve_out_z)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, scaleIn, scale_in_x)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, scaleOut, scale_out_x)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, scale_in_y, scale_in_z)
TYPES_STRUCT_RENAME_ELEM(PoseChannel, scale_out_y, scale_out_z)
TYPES_STRUCT_RENAME_ELEM(SameVolumeConstraint, flag, free_axis)
TYPES_STRUCT_RENAME_ELEM(Sound, name, filepath)
TYPES_STRUCT_RENAME_ELEM(Theme, tact, space_action)
TYPES_STRUCT_RENAME_ELEM(Theme, tbuts, space_props)
TYPES_STRUCT_RENAME_ELEM(Theme, tclip, space_clip)
TYPES_STRUCT_RENAME_ELEM(Theme, tconsole, space_console)
TYPES_STRUCT_RENAME_ELEM(Theme, text, space_text)
TYPES_STRUCT_RENAME_ELEM(Theme, tfile, space_file)
TYPES_STRUCT_RENAME_ELEM(Theme, tima, space_image)
TYPES_STRUCT_RENAME_ELEM(Theme, tinfo, space_info)
TYPES_STRUCT_RENAME_ELEM(Theme, tipo, space_graph)
DNA_STRUCT_RENAME_ELEM(Theme, tnla, space_nla)
DNA_STRUCT_RENAME_ELEM(Theme, tnode, space_node)
DNA_STRUCT_RENAME_ELEM(Theme, toops, space_outliner)
DNA_STRUCT_RENAME_ELEM(Theme, tseq, space_seq)
DNA_STRUCT_RENAME_ELEM(Theme, tstatusbar, space_statusbar)
DNA_STRUCT_RENAME_ELEM(Theme, ttopbar, space_topbar)
DNA_STRUCT_RENAME_ELEM(Theme, tuserpref, space_preferences)
TYPES_STRUCT_RENAME_ELEM(Theme, tv3d, space_view3d)
/* Write with a different name, old Blender versions crash loading files with non-NULL
 * global_areas. See D9442. */
TYPEE_STRUCT_RENAME_ELEM(WM, global_area_map, global_areas)

/* NOTE: Keep sorted! */
