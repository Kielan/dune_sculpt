#pragma once

/* Struct members on own line. */
/* clang-format off */

#define _TYPES_DEFAULT_ArmatureModData \
  { \
    .deformflag = ARM_DEF_VGROUP, \
    .multi = 0.0f, \
    .ob = NULL, \
    .defgrp_name = "", \
  }

/* Default to 2 duplicates distributed along the x-axis by an offset of 1 object width. */
#define _TYPES_DEFAULT_ArrayModData \
  { \
    .start_cap = NULL, \
    .end_cap = NULL, \
    .curve_ob = NULL, \
    .offset_ob = NULL, \
    .offset = {1.0f, 0.0f, 0.0f}, \
    .scale = {1.0f, 0.0f, 0.0f}, \
    .length = 0.0f, \
    .merge_dist = 0.01f, \
    .fit_type = MOD_ARR_FIXEDCOUNT, \
    .offset_type = MOD_ARR_OFF_RELATIVE, \
    .flags = 0, \
    .count = 2, \
    .uv_offset = {0.0f, 0.0f}, \
  }

#define _TYPES_DEFAULT_BevelModData \
  { \
    .value = 0.1f, \
    .res = 1, \
    .flags = 0, \
    .val_flags = MOD_BEVEL_AMT_OFFSET, \
    .profile_type = MOD_BEVEL_PROFILE_SUPERELLIPSE, \
    .lim_flags = MOD_BEVEL_ANGLE, \
    .e_flags = 0, \
    .mat = -1, \
    .edge_flags = 0, \
    .face_str_mode = MOD_BEVEL_FACE_STRENGTH_NONE, \
    .miter_inner = MOD_BEVEL_MITER_SHARP, \
    .miter_outer = MOD_BEVEL_MITER_SHARP, \
    .affect_type = MOD_BEVEL_AFFECT_EDGES, \
    .profile = 0.5f, \
    .bevel_angle = DEG2RADF(30.0f), \
    .spread = 0.1f, \
    .defgrp_name = "", \
  }

#define _TYPES_DEFAULT_BoolModData \
  { \
    .object = NULL, \
    .collection = NULL, \
    .double_threshold = 1e-6f, \
    .op = eBoolModOp_Diff, \
    .solver = eBoolModSolver_Exact, \
    .flag = eBoolModFlag_Ob, \
    .mesh_flag = 0, \
  }

#define _TYPES_DEFAULT_BuildModData \
  { \
    .start = 1.0f, \
    .length = 100.0f, \
    .flag = 0, \
    .randomize = 0, \
    .seed = 0, \
  }

#define _TYPES_DEFAULT_CastModData \
  { \
    .object = NULL, \
    .fac = 0.5f, \
    .radius = 0.0f, \
    .size = 0.0f, \
    .defgrp_name = "", \
    .flag = MOD_CAST_X | MOD_CAST_Y | MOD_CAST_Z | MOD_CAST_SIZE_FROM_RADIUS, \
    .type = MOD_CAST_TYPE_SPHERE, \
  }

#define _TYPES_DEFAULT_ClothSimSettings \
  { \
    .cache = NULL, \
    .mingoal = 0.0f, \
    .Cvi = 1.0f, \
    .gravity = {0.0f, 0.0f, -9.81f}, \
    .dt = 0.0f, \
    .mass = 0.3f, \
    .shear = 5.0f, \
    .bending = 0.5f, \
    .max_bend = 0.5f, \
    .max_shear = 5.0f, \
    .max_sewing = 0.0f, \
    .avg_spring_len = 0.0f, \
    .timescale = 1.0f, \
    .time_scale = 1.0f, \
    .maxgoal = 1.0f, \
    .eff_force_scale = 1000.0f, \
    .eff_wind_scale = 250.0f, \
    .sim_time_old = 0.0f, \
    .defgoal = 0.0f, \
    .goalspring = 1.0f, \
    .goalfrict = 0.0f, \
    .velocity_smooth = 0.0f, \
    .density_target = 0.0f, \
    .density_strength = 0.0f, \
    .collider_friction = 0.0f, \
    .shrink_min = 0.0f, \
    .shrink_max = 0.0f, \
    .uniform_pressure_force = 0.0f, \
    .target_volume = 0.0f, \
    .pressure_factor = 1.0f, \
    .fluid_density = 0.0f, \
    .vgroup_pressure = 0, \
    .bending_damping = 0.5f, \
    .voxel_cell_size = 0.1f, \
    .stepsPerFrame = 5, \
    .flags = CLOTH_SIMSETTINGS_FLAG_INTERNAL_SPRINGS_NORMAL, \
    .maxspringlen = 10, \
    .solver_type = 0, \
    .vgroup_bend = 0, \
    .vgroup_mass = 0, \
    .vgroup_struct = 0, \
    .vgroup_shrink = 0, \
    .shapekey_rest = 0, \
    .presets = 2, \
    .reset = 0, \
    .effector_weights = NULL, \
    .bending_model = CLOTH_BENDING_ANGULAR, \
    .vgroup_shear = 0, \
    .tension = 15.0f , \
    .compression = 15.0f, \
    .max_tension =  15.0f, \
    .max_compression = 15.0f, \
    .tension_damp = 5.0f, \
    .compression_damp = 5.0f, \
    .shear_damp = 5.0f, \
    .internal_spring_max_length = 0.0f, \
    .internal_spring_max_diversion = M_PI_4, \
    .vgroup_intern = 0, \
    .internal_tension = 15.0f, \
    .internal_compression = 15.0f, \
    .max_internal_tension = 15.0f, \
    .max_internal_compression = 15.0f, \
  }

#define _TYPES_DEFAULT_ClothCollSettings \
  { \
    .collision_list = NULL, \
    .epsilon = 0.015f, \
    .self_friction = 5.0f, \
    .friction = 5.0f, \
    .damping = 0.0f, \
    .selfepsilon = 0.015f, \
    .flags = CLOTH_COLLSETTINGS_FLAG_ENABLED, \
    .loop_count = 2, \
    .group = NULL, \
    .vgroup_selfcol = 0, \
    .vgroup_objcol = 0, \
    .clamp = 0.0f, \
    .self_clamp = 0.0f, \
  }

#define _TYPES_DEFAULT_ClothModData \
  { \
    .clothOb = NULL, \
    .sim_parms = NULL, \
    .coll_parms = NULL, \
    .point_cache = NULL, \
    .ptcaches = {NULL, NULL}, \
    .hairdata = NULL, \
    .hair_grid_min = {0.0f, 0.0f, 0.0f}, \
    .hair_grid_max = {0.0f, 0.0f, 0.0f}, \
    .hair_grid_res = {0, 0, 0}, \
    .hair_grid_cellsize = 0.0f, \
    .solver_result = NULL, \
  }

#define _TYPES_DEFAULT_CollisionModData \
  { \
    .x = NULL, \
    .xnew = NULL, \
    .xold = NULL, \
    .current_xnew = NULL, \
    .current_x = NULL, \
    .current_v = NULL, \
    .tri = NULL, \
    .mvert_num = 0, \
    .tri_num = 0, \
    .time_x = -1000.0f, \
    .time_xnew = -1000.0f, \
    .is_static = false, \
    .bvhtree = NULL, \
  }

#define _TYPES_DEFAULT_CorrectiveSmoothModData \
  { \
    .bind_coords = NULL, \
    .bind_coords_num = 0, \
    .lambda = 0.5f, \
    .scale = 1.0f, \
    .repeat = 5, \
    .flag = 0, \
    .smooth_type = MOD_CORRECTIVESMOOTH_SMOOTH_SIMPLE, \
    .defgrp_name = "", \
  }

#define _TYPES_DEFAULT_CurveModData \
  { \
    .ob = NULL, \
    .name = "", \
    .defaxis = MOD_CURVE_POSX, \
    .flag = 0, \
  }

/* Defines are scattered across too many files, they need to be moved to DNA. */
#if 0
#define _TYPES_DEFAULT_DataTransferModData \
  { \
    .ob_src = NULL, \
    .data_types = 0, \
    .vmap_mode = MREMAP_MODE_VERT_NEAREST, \
    .emap_mode = MREMAP_MODE_EDGE_NEAREST, \
    .lmap_mode = MREMAP_MODE_LOOP_NEAREST_POLYNOR, \
    .pmap_mode = MREMAP_MODE_POLY_NEAREST, \
    .map_max_distance = 1.0f, \
    .map_ray_radius = 0.0f, \
    .islands_precision = 0.0f, \
    .layers_sel_src = {DT_LAYERS_ALL_SRC, DT_LAYERS_ALL_SRC, DT_LAYERS_ALL_SRC, DT_LAYERS_ALL_SRC}, \
    .layers_sel_dst = {DT_LAYERS_NAME_DST, DT_LAYERS_NAME_DST, DT_LAYERS_NAME_DST, DT_LAYERS_NAME_DST}, \
    .mix_mod = CDT_MIX_TRANSFER, \
    .mix_factor = 1.0f, \
    .defgrp_name = "", \
    .flags = MOD_DATATRANSFER_OBSRC_TRANSFORM, \
  }
#endif

#define _TYPES_DEFAULT_DecimateModData \
  { \
    .percent = 1.0f, \
    .iter = 0, \
    .delimit = 0, \
    .symmetry_axis = 0, \
    .angle = DEG2RADF(5.0f), \
    .defgrp_name = "", \
    .defgrp_factor = 1.0f, \
    .flag = 0, \
    .mode = 0, \
    .face_count = 0, \
  }

#define _TYPES_DEFAULT_DisplaceModData \
  { \
    .texture = NULL, \
    .map_object = NULL, \
    .map_bone = "", \
    .uvlayer_name = "", \
    .uvlayer_tmp = 0, \
    .texmapping = 0, \
    .strength = 1.0f, \
    .direction = MOD_DISP_DIR_NOR, \
    .defgrp_name = "", \
    .midlevel = 0.5f, \
    .space = MOD_DISP_SPACE_LOCAL, \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_DynamicPaintModData \
  { \
    .canvas = NULL, \
    .brush = NULL, \
    .type = MOD_DYNAMICPAINT_TYPE_CANVAS, \
  }

/* Default to 30-degree split angle, sharpness from both angle & flag. */
#define _TYPES_DEFAULT_EdgeSplitModData \
  { \
    .split_angle = DEG2RADF(30.0f), \
    .flags = MOD_EDGESPLIT_FROMANGLE | MOD_EDGESPLIT_FROMFLAG, \
  }

#define _TYPES_DEFAULT_ExplodeModData \
  { \
    .facepa = NULL, \
    .flag = eExplodeFlag_Unborn | eExplodeFlag_Alive | eExplodeFlag_Dead, \
    .vgroup = 0, \
    .protect = 0.0f, \
    .uvname = "", \
  }

/* Fluid modifier settings skipped for now. */
#define _TYPES_DEFAULT_HookModData \
  { \
    .subtarget = "", \
    .flag = 0, \
    .falloff_type = eHook_Falloff_Smooth, \
    .parentinv = _TYPES_DEFAULT_UNIT_M4, \
    .cent = {0.0f, 0.0f, 0.0f}, \
    .falloff = 0.0f, \
    .curfalloff = NULL, \
    .indexar = NULL, \
    .totindex = 0, \
    .force = 1.0f, \
    .name = "", \
  }

#define _TYPES_DEFAULT_LaplacianDeformModData \
  { \
    .anchor_grp_name = "", \
    .total_verts = 0, \
    .repeat = 1, \
    .vertexco = NULL, \
    .cache_system = NULL, \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_LaplacianSmoothModData \
  { \
    .lambda = 0.01f, \
    .lambda_border = 0.01f, \
    .defgrp_name = "", \
    .flag = MOD_LAPLACIANSMOOTH_X | MOD_LAPLACIANSMOOTH_Y | MOD_LAPLACIANSMOOTH_Z | \
            MOD_LAPLACIANSMOOTH_PRESERVE_VOLUME | MOD_LAPLACIANSMOOTH_NORMALIZED, \
    .repeat = 1, \
  }

#define _TYPES_DEFAULT_LatticeModData \
  { \
    .ob = NULL, \
    .name = "", \
    .strength = 1.0f, \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_MaskModData \
  { \
    .ob_arm = NULL, \
    .vgroup = "", \
    .mode = 0, \
    .flag = 0, \
    .threshold = 0.0f, \
  }

/* Y and Z forward and up axes, Dune default. */
#define _TYPES_DEFAULT_MeshCacheModData \
  { \
    .flag = 0, \
    .type = MOD_MESHCACHE_TYPE_MDD, \
    .time_mode = 0, \
    .play_mode = 0, \
    .forward_axis = 1, \
    .up_axis = 2, \
    .flip_axis = 0, \
    .interp = MOD_MESHCACHE_INTERP_LINEAR, \
    .factor = 1.0f, \
    .deform_mode = 0.0f, \
    .frame_start = 0.0f, \
    .frame_scale = 1.0f, \
    .eval_frame = 0.0f, \
    .eval_time = 0.0f, \
    .eval_factor = 0.0f, \
    .filepath = "", \
  }

#define TYPES_DEFAULT_MeshDeformModData \
  { \
    .ob = 0, \
    .defgrp_name = "", \
    .gridsize = 5, \
    .flag = 0, \
    .bindinfluences = NULL, \
    .bindoffsets = NULL, \
    .bindcagecos = NULL, \
    .totvert = 0, \
    .totcagevert = 0, \
    .dyngrid = NULL, \
    .dyninfluences = NULL, \
    .dynverts = NULL, \
    .dyngridsize = 0, \
    .totinfluence = 0, \
    .dyncellmin = {0.0f, 0.0f, 0.0f}, \
    .dyncellwidth = 0.0f, \
    .bindmat = _DNA_DEFAULT_UNIT_M4, \
    .bindweights = NULL, \
    .bindcos = NULL, \
    .bindfunc = NULL, \
  }

#define _TYPES_DEFAULT_MeshSeqCacheModData \
  { \
    .cache_file = NULL, \
    .ob_path = "", \
    .read_flag = MOD_MESHSEQ_READ_VERT | MOD_MESHSEQ_READ_POLY | MOD_MESHSEQ_READ_UV | \
                 MOD_MESHSEQ_READ_COLOR | MOD_MESHSEQ_INTERPOLATE_VERTICES, \
    .velocity_scale = 1.0f, \
    .reader = NULL, \
    .reader_object_path = "", \
  }

#define _TYPES_DEFAULT_MirrorModData \
  { \
    .flag = MOD_MIR_AXIS_X | MOD_MIR_VGROUP, \
    .tolerance = 0.001f, \
    .bisect_threshold = 0.001f, \
    .uv_offset = {0.0f, 0.0f}, \
    .uv_offset_copy = {0.0f, 0.0f}, \
    .mirror_ob = NULL, \
    .use_correct_order_on_merge = true, \
  }

#define _TYPES_DEFAULT_MultiresModData \
  { \
    .lvl = 0, \
    .sculptlvl = 0, \
    .renderlvl = 0, \
    .totlvl = 0, \
    .flags = eMultiresModFlag_UseCrease | eMultiresModFlag_ControlEdges, \
    .uv_smooth = SUBSURF_UV_SMOOTH_PRESERVE_BOUNDARIES, \
    .quality = 4, \
    .boundary_smooth = SUBSURF_BOUNDARY_SMOOTH_ALL, \
  }

#define _TYPES_DEFAULT_NormalEditModData \
  { \
    .defgrp_name = "", \
    .target = NULL, \
    .mode = MOD_NORMALEDIT_MODE_RADIAL, \
    .flag = 0, \
    .mix_mode = MOD_NORMALEDIT_MIX_COPY, \
    .mix_factor = 1.0f, \
    .mix_limit = M_PI, \
    .offset = {0.0f, 0.0f, 0.0f}, \
  }

/* Some fields are initialized in initData. */
#define _TYPES_DEFAULT_OceanModData \
  { \
    .ocean = NULL, \
    .oceancache = NULL, \
    .resolution = 7, \
    .viewport_resolution = 7, \
    .spatial_size = 50, \
    .wind_velocity = 30.0f, \
    .damp = 0.5f, \
    .smallest_wave = 0.01f, \
    .depth = 200.0f, \
    .wave_alignment = 0.0f, \
    .wave_direction = 0.0f, \
    .wave_scale = 1.0f, \
    .chop_amount = 1.0f, \
    .foam_coverage = 0.0f, \
    .time = 1.0f, \
    .spectrum = MOD_OCEAN_SPECTRUM_PHILLIPS, \
    .fetch_jonswap = 120.0f, \
    .sharpen_peak_jonswap = 0.0f, \
    .bakestart = 1, \
    .bakeend = 250, \
    .cachepath = "", \
    .foamlayername = "", \
    .spraylayername = "", \
    .cached = 0, \
    .geometry_mode = 0, \
    .flag = 0, \
    .repeat_x = 1, \
    .repeat_y = 1, \
    .seed = 0, \
    .size = 1.0f, \
    .foam_fade = 0.98f, \
  }

#define _TYPES_DEFAULT_ParticleInstanceModData \
  { \
    .psys = 1, \
    .flag = eParticleInstanceFlag_Parents | eParticleInstanceFlag_Unborn | \
            eParticleInstanceFlag_Alive | eParticleInstanceFlag_Dead, \
    .axis = 2, \
    .space = eParticleInstanceSpace_World, \
    .position = 1.0f, \
    .random_position = 0.0f, \
    .rotation = 0.0f, \
    .random_rotation = 0.0f, \
    .particle_offset = 0.0f, \
    .particle_amount = 1.0f, \
    .index_layer_name = "", \
    .val_layer_name = "", \
  }

#define _TYPES_DEFAULT_ParticleSystemModData \
  { \
    .psys = NULL, \
    .mesh_final = NULL, \
    .mesh_original = NULL, \
    .totdmvert = 0, \
    .totdmedge = 0, \
    .totdmface = 0, \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_RemeshModData \
  { \
    .threshold = 1.0f, \
    .scale = 0.9f, \
    .hermite_num = 1.0f, \
    .depth = 4, \
    .flag = MOD_REMESH_FLOOD_FILL, \
    .mode = MOD_REMESH_VOXEL, \
    .voxel_size = 0.1f, \
    .adaptivity = 0.0f, \
  }

#define _TYPES_DEFAULT_ScrewModData \
  { \
    .ob_axis = NULL, \
    .steps = 16, \
    .render_steps = 16, \
    .iter = 1, \
    .screw_ofs = 0.0f, \
    .angle = 2.0f * M_PI, \
    .merge_dist = 0.01f, \
    .flag = MOD_SCREW_SMOOTH_SHADING, \
    .axis = 2, \
  }

/* Shape key modifier has no items. */
#define _TYPES_DEFAULT_ShrinkwrapModData \
  { \
    .target = NULL, \
    .auxTarget = NULL, \
    .vgroup_name = "", \
    .keepDist = 0.0f, \
    .shrinkType = MOD_SHRINKWRAP_NEAREST_SURFACE, \
    .shrinkOpts = MOD_SHRINKWRAP_PROJECT_ALLOW_POS_DIR, \
    .shrinkMode = 0, \
    .projLimit = 0.0f, \
    .projAxis = 0, \
    .subsurfLevels = 0, \
  }

#define _TYPES_DEFAULT_SimpleDeformModData \
  { \
    .origin = NULL, \
    .vgroup_name = "", \
    .factor = DEG2RADF(45.0f), \
    .limit = {0.0f, 1.0f}, \
    .mode = MOD_SIMPLEDEFORM_MODE_TWIST, \
    .axis = 0, \
    .deform_axis = 0, \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_NodesModData \
  { 0 }

#define _TYPES_DEFAULT_SkinModData \
  { \
    .branch_smoothing = 0.0f, \
    .flag = 0, \
    .symmetry_axes = MOD_SKIN_SYMM_X, \
  }

#define _TYPES_DEFAULT_SmoothModData \
  { \
    .fac = 0.5f, \
    .repeat = 1, \
    .defgrp_name = "", \
    .flag = MOD_SMOOTH_X | MOD_SMOOTH_Y | MOD_SMOOTH_Z, \
  }

/* Softbody modifier skipped for now. */
#define _TYPES_DEFAULT_SolidifyModData \
  { \
    .defgrp_name = "", \
    .shell_defgrp_name = "", \
    .rim_defgrp_name = "", \
    .offset = 0.01f, \
    .offset_fac = -1.0f, \
    .offset_fac_vg = 0.0f, \
    .offset_clamp = 0.0f, \
    .mode = MOD_SOLIDIFY_MODE_EXTRUDE, \
    .nonmanifold_offset_mode = MOD_SOLIDIFY_NONMANIFOLD_OFFSET_MODE_CONSTRAINTS, \
    .nonmanifold_boundary_mode = MOD_SOLIDIFY_NONMANIFOLD_BOUNDARY_MODE_NONE, \
    .crease_inner = 0.0f, \
    .crease_outer = 0.0f, \
    .crease_rim = 0.0f, \
    .flag = MOD_SOLIDIFY_RIM, \
    .mat_ofs = 0, \
    .mat_ofs_rim = 0, \
    .merge_tolerance = 0.0001f, \
    .bevel_convex = 0.0f, \
  }

#define _TYPES_DEFAULT_SubsurfModData \
  { \
    .subdivType = 0, \
    .levels = 1, \
    .renderLevels = 2, \
    .flags = eSubsurfModFlag_UseCrease | eSubsurfModFlag_ControlEdges, \
    .uv_smooth = SUBSURF_UV_SMOOTH_PRESERVE_BOUNDARIES, \
    .quality = 3, \
    .boundary_smooth = SUBSURF_BOUNDARY_SMOOTH_ALL, \
    .emCache = NULL, \
    .mCache = NULL, \
  }

#define _TYPES_DEFAULT_SurfaceModData \
  { \
    .x = NULL, \
    .v = NULL, \
    .mesh = NULL, \
    .bvhtree = NULL, \
    .cfra = 0, \
    .numverts = 0, \
  }

#define _TYPES_DEFAULT_SurfaceDeformModData \
  { \
    .graph = NULL, \
    .target = NULL, \
    .verts = NULL, \
    .falloff = 4.0f, \
    .num_mesh_verts = 0, \
    .num_bind_verts = 0, \
    .numpoly = 0, \
    .flags = 0, \
    .mat = _TYPES_DEFAULT_UNIT_M4, \
    .strength = 1.0f, \
    .defgrp_name = "", \
  }

#define _TYPES_DEFAULT_TriangulateModData \
  { \
    .flag = 0, \
    .quad_method = MOD_TRIANGULATE_QUAD_SHORTEDGE, \
    .ngon_method = MOD_TRIANGULATE_NGON_BEAUTY, \
    .min_vertices = 4, \
  }

#define _TYPES_DEFAULT_UVProjectModData \
  { \
    .projectors = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}, \
    .num_projectors = 1, \
    .aspectx = 1.0f, \
    .aspecty = 1.0f, \
    .scalex = 1.0f, \
    .scaley = 1.0f, \
    .uvlayer_name = "", \
    .uvlayer_tmp = 0, \
  }

#define _TYPES_DEFAULT_UVWarpModData \
  { \
    .axis_u = 0, \
    .axis_v = 1, \
    .flag = 0, \
    .center = {0.5f, 0.5f}, \
    .offset = {0.0f, 0.0f}, \
    .scale = {1.0f, 1.0f}, \
    .rotation = 0.0f, \
    .ob_src = NULL, \
    .bone_src = "", \
    .ob_dst = NULL, \
    .bone_dst = "", \
    .vgroup_name = "", \
    .uvlayer_name = "", \
  }

#define _TYPES_DEFAULT_WarpModData \
  { \
    .texture = NULL, \
    .map_ob = NULL, \
    .map_bone = "", \
    .uvlayer_name = "", \
    .uvlayer_tmp = 0, \
    .texmapping = 0, \
    .ob_from = NULL, \
    .ob_to = NULL, \
    .bone_from = "", \
    .bone_to = "", \
    .curfalloff = NULL, \
    .defgrp_name = "", \
    .strength = 1.0f, \
    .falloff_radius = 1.0f, \
    .flag = 0, \
    .falloff_type = eWarp_Falloff_Smooth, \
  }

#define _TYPES_DEFAULT_WaveModData \
  { \
    .texture = NULL, \
    .map_ob = NULL, \
    .map_bone = "", \
    .uvlayer_name = "", \
    .uvlayer_tmp = 0, \
    .texmapping = MOD_DISP_MAP_LOCAL, \
    .objectcenter = NULL, \
    .defgrp_name = "", \
    .flag = MOD_WAVE_X | MOD_WAVE_Y | MOD_WAVE_CYCL | MOD_WAVE_NORM_X | MOD_WAVE_NORM_Y | MOD_WAVE_NORM_Z, \
    .startx = 0.0f, \
    .starty = 0.0f, \
    .height = 0.5f, \
    .width = 1.5f, \
    .narrow = 1.5f, \
    .speed = 0.25f, \
    .damp = 10.0f, \
    .falloff = 0.0f, \
    .timeoffs = 0.0f, \
    .lifetime = 0.0f, \
  }

#define _TYPES_DEFAULT_WeightedNormalModData \
  { \
    .defgrp_name = "", \
    .mode = MOD_WEIGHTEDNORMAL_MODE_FACE, \
    .flag = 0, \
    .weight = 50, \
    .thresh = 0.01f, \
  }

#define _TYPES_DEFAULT_WeightVGEditModData \
  { \
    .defgrp_name = "", \
    .edit_flags = 0, \
    .falloff_type = MOD_WVG_MAPPING_NONE, \
    .default_weight = 0.0f, \
    .cmap_curve = NULL, \
    .add_threshold = 0.01f, \
    .rem_threshold = 0.01f, \
    .mask_constant =  1.0f, \
    .mask_defgrp_name = "", \
    .mask_tex_use_channel = MOD_WVG_MASK_TEX_USE_INT, \
    .mask_texture = NULL, \
    .mask_tex_map_ob = NULL, \
    .mask_tex_map_bone = "", \
    .mask_tex_mapping = MOD_DISP_MAP_LOCAL, \
    .mask_tex_uvlayer_name = "", \
  }

#define _TYPES_DEFAULT_WeightVGMixModData \
  { \
    .defgrp_name_a = "", \
    .defgrp_name_b = "", \
    .default_weight_a = 0.0f, \
    .default_weight_b = 0.0f, \
    .mix_mode = MOD_WVG_MIX_SET, \
    .mix_set = MOD_WVG_SET_AND, \
    .mask_constant = 1.0f, \
    .mask_defgrp_name = "", \
    .mask_tex_use_channel = MOD_WVG_MASK_TEX_USE_INT, \
    .mask_texture = NULL, \
    .mask_tex_map_obj = NULL, \
    .mask_tex_map_bone = "", \
    .mask_tex_mapping = MOD_DISP_MAP_LOCAL, \
    .mask_tex_uvlayer_name = "", \
    .flag = 0, \
  }

#define _TYPES_DEFAULT_WeightVGProximityModData \
  { \
    .defgrp_name = "", \
    .proximity_mode = MOD_WVG_PROXIMITY_OBJECT, \
    .proximity_flags = MOD_WVG_PROXIMITY_GEOM_VERTS, \
    .proximity_ob_target = NULL, \
    .mask_constant = 1.0f, \
    .mask_defgrp_name = "", \
    .mask_tex_use_channel = MOD_WVG_MASK_TEX_USE_INT, \
    .mask_texture = NULL, \
    .mask_tex_map_ob = NULL, \
    .mask_tex_map_bone = "", \
    .mask_tex_mapping = MOD_DISP_MAP_LOCAL, \
    .mask_tex_uvlayer_name = "", \
    .min_dist = 0.0f, \
    .max_dist = 1.0f, \
    .falloff_type = MOD_WVG_MAPPING_NONE, \
  }

#define _TYPES_DEFAULT_WeldModData \
  { \
    .merge_dist = 0.001f, \
    .mode = MOD_WELD_MODE_ALL, \
    .defgrp_name = "", \
  }

#define _TYPES_DEFAULT_WireframeModData \
  { \
    .defgrp_name = "", \
    .offset = 0.02f, \
    .offset_fac = 0.0f, \
    .offset_fac_vg = 0.0f, \
    .crease_weight = 1.0f, \
    .flag = MOD_WIREFRAME_REPLACE | MOD_WIREFRAME_OFS_EVEN, \
    .mat_ofs = 0, \
  }

/* clang-format off */
