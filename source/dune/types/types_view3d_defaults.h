#pragma once

/* Struct members on own line. */
/* clang-format off */

/* Viewport Struct */
#define _TYPES_DEFAULT_View3DShading \
  { \
    .type = OB_SOLID, \
    .prev_type = OB_SOLID, \
    .flag = V3D_SHADING_SPECULAR_HIGHLIGHT | V3D_SHADING_XRAY_WIREFRAME | \
            V3D_SHADING_SCENE_LIGHTS_RENDER | V3D_SHADING_SCENE_WORLD_RENDER, \
    .light = V3D_LIGHTING_STUDIO, \
    .shadow_intensity = 0.5f, \
    .xray_alpha = 0.5f, \
    .xray_alpha_wire = 0.5f, \
    .cavity_valley_factor = 1.0f, \
    .cavity_ridge_factor = 1.0f, \
    .cavity_type = V3D_SHADING_CAVITY_CURVATURE, \
    .curvature_ridge_factor = 1.0f, \
    .curvature_valley_factor = 1.0f, \
    .single_color = {0.8f, 0.8f, 0.8f}, \
    .background_color = {0.05f, 0.05f, 0.05f}, \
    .studiolight_intensity = 1.0f, \
    .rndr_pass = SCE_PASS_COMBINED, \
  }

#define _TYPES_DEFAULT_View3DOverlay \
  { \
    .wireframe_threshold = 1.0f, \
    .wireframe_opacity = 1.0f, \
    .xray_alpha_bone = 0.5f, \
    .bone_wire_alpha = 1.0f, \
    .fade_alpha = 0.40f, \
    .texture_paint_mode_opacity = 1.0f, \
    .weight_paint_mode_opacity = 1.0f, \
    .vert_paint_mode_opacity = 1.0f, \
    /* Intentionally different to vertex/paint mode, \
     * we typically want to see shading too. */ \
    .sculpt_mode_mask_opacity = 0.75f, \
    .sculpt_mode_face_sets_opacity = 1.0f, \
 \
    .edit_flag = V3D_OVERLAY_EDIT_FACES | V3D_OVERLAY_EDIT_SEAMS | \
                             V3D_OVERLAY_EDIT_SHARP | V3D_OVERLAY_EDIT_FREESTYLE_EDGE | \
                             V3D_OVERLAY_EDIT_FREESTYLE_FACE | V3D_OVERLAY_EDIT_EDGES | \
                             V3D_OVERLAY_EDIT_CREASES | V3D_OVERLAY_EDIT_BWEIGHTS, \
    .handle_display = CURVE_HANDLE_SEL, \
 \
    .pen_paper_opacity = 0.5f, \
    .pen_grid_opacity = 0.9f, \
    .pen_vertex_paint_opacity = 1.0f, \
    .normals_constant_screen_size = 7.0f, \
  }

#define _TYPES_DEFAULT_View3DCursor \
  { \
    .rotation_mode = ROT_MODE_XYZ, \
    .rotation_quaternion = {1, 0, 0, 0}, \
    .rotation_axis = {0, 1, 0}, \
  }

#define _TYPES_DEFAULT_View3D \
  { \
    .spacetype = SPACE_VIEW3D, \
    .scenelock = true, \
    .grid = 1.0f, \
    .gridlines = 16, \
    .gridsubdiv = 10, \
    .shading = _TYPES_DEFAULT_View3DShading, \
    .overlay = _TYPES_DEFAULT_View3DOverlay, \
 \
    .gridflag = V3D_SHOW_X | V3D_SHOW_Y | V3D_SHOW_FLOOR | V3D_SHOW_ORTHO_GRID, \
 \
    .flag = V3D_SEL_OUTLINE, \
    .flag2 = V3D_SHOW_RECONSTRUCTION | V3D_SHOW_ANNOTATION, \
 \
    .lens = 50.0f, \
    .clip_start = 0.01f, \
    .clip_end = 1000.0f, \
 \
    .bundle_size = 0.2f, \
    .bundle_drwtype = OB_PLAINAXES, \
 \
    /* stereo */ \
    .stereo3d_camera = STEREO_3D_ID, \
    .stereo3d_flag = V3D_S3D_DISPPLANE, \
    .stereo3d_convergence_alpha = 0.15f, \
    .stereo3d_volume_alpha = 0.05f, \
 \
    /* Pen settings. */ \
    .very_opacity = 1.0f, \
    .pen_flag = V3D_PEN_SHOW_EDIT_LINES, \
  }
