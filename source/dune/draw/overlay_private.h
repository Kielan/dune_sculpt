
#pragma once

#include "draw_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
#  define USE_GEOM_SHADER_WORKAROUND 1
#else
#  define USE_GEOM_SHADER_WORKAROUND 0
#endif

/* Needed for eSpaceImage_UVDT_Stretch and eMaskOverlayMode */
#include "types_mask.h"
#include "types_space.h"
/* Forward declarations */
struct ImBuf;

typedef struct OverlayFramebufferList {
  struct GPUFrameBuffer *overlay_default_fb;
  struct GPUFrameBuffer *overlay_line_fb;
  struct GPUFrameBuffer *overlay_color_only_fb;
  struct GPUFrameBuffer *overlay_in_front_fb;
  struct GPUFrameBuffer *overlay_line_in_front_fb;
  struct GPUFrameBuffer *outlines_prepass_fb;
  struct GPUFrameBuffer *outlines_resolve_fb;
} OverlayFramebufferList;

typedef struct OverlayTextureList {
  struct GPUTexture *temp_depth_tx;
  struct GPUTexture *dummy_depth_tx;
  struct GPUTexture *outlines_id_tx;
  struct GPUTexture *overlay_color_tx;
  struct GPUTexture *overlay_line_tx;
} OverlayTextureList;

#define NOT_IN_FRONT 0
#define IN_FRONT 1

typedef enum OverlayUVLineStyle {
  OVERLAY_UV_LINE_STYLE_OUTLINE = 0,
  OVERLAY_UV_LINE_STYLE_DASH = 1,
  OVERLAY_UV_LINE_STYLE_BLACK = 2,
  OVERLAY_UV_LINE_STYLE_WHITE = 3,
  OVERLAY_UV_LINE_STYLE_SHADOW = 4,
} OVERLAY_UVLineStyle;

typedef struct OverlayPassList {
  DrawPass *antialiasing_ps;
  DrawPass *armature_ps[2];
  DrawPass *armature_bone_select_ps;
  DrawPass *armature_transp_ps[2];
  DrawPass *background_ps;
  DrawPass *clipping_frustum_ps;
  DrawPass *edit_curve_wire_ps[2];
  DrawPass *edit_curve_handle_ps;
  DrawPass *edit_dpen_ps;
  DrawPass *edit_dpen_gizmos_ps;
  DrawPass *edit_dpen_curve_ps;
  DrawPass *edit_lattice_ps;
  DrawPass *edit_mesh_depth_ps[2];
  DrawPass *edit_mesh_verts_ps[2];
  DrawPass *edit_mesh_edges_ps[2];
  DrawPass *edit_mesh_faces_ps[2];
  DrawPass *edit_mesh_faces_cage_ps[2];
  DrawPass *edit_mesh_analysis_ps;
  DrawPass *edit_mesh_normals_ps;
  DrawPass *edit_particle_ps;
  DrawPass *edit_text_overlay_ps;
  DrawPass *edit_text_darken_ps;
  DrawPass *edit_text_wire_ps[2];
  DrawPass *edit_uv_edges_ps;
  DrawPass *edit_uv_verts_ps;
  DrawPass *edit_uv_faces_ps;
  DrawPass *edit_uv_stretching_ps;
  DrawPass *edit_uv_tiled_image_borders_ps;
  DrawPass *edit_uv_stencil_ps;
  DrawPass *edit_uv_mask_ps;
  DrawPass *extra_ps[2];
  DrawPass *extra_blend_ps;
  DrawPass *extra_centers_ps;
  DrawPass *extra_grid_ps;
  DrawPass *pen_canvas_ps;
  DrawPass *facing_ps[2];
  DrawPass *fade_ps[2];
  DrawPass *mode_transfer_ps[2];
  DrawPass *grid_ps;
  DrawPass *image_background_ps;
  DrawPass *image_background_scene_ps;
  DrawPass *image_empties_ps;
  DrawPass *image_empties_back_ps;
  DrawPass *image_empties_blend_ps;
  DrawPass *image_empties_front_ps;
  DrawPass *image_foreground_ps;
  DrawPass *image_foreground_scene_ps;
  DrawPass *metaball_ps[2];
  DrawPass *motion_paths_ps;
  DrawPass *outlines_prepass_ps;
  DrawPass *outlines_detect_ps;
  DrawPass *outlines_resolve_ps;
  DrawPass *paint_color_ps;
  DrawPass *paint_depth_ps;
  DrawPass *paint_overlay_ps;
  DrawPass *particle_ps;
  DrawPass *pointcloud_ps;
  DrawPass *sculpt_mask_ps;
  DrawPass *volume_ps;
  DrawPass *wireframe_ps;
  DrawPass *wireframe_xray_ps;
  DrawPass *xray_fade_ps;
} OverlayPassList;

/* Data used by GLSL shader. To be used as UBO. */
typedef struct OverlayShadingData {
  /** Grid */
  float grid_axes[3], grid_distance;
  float zplane_axes[3], grid_size[3];
  float grid_steps[SI_GRID_STEPS_LEN];
  float inv_viewport_size[2];
  float grid_line_size;
  float zoom_factor; /* Only for UV editor */
  int grid_flag;
  int zpos_flag;
  int zneg_flag;
  /** Wireframe */
  float wire_step_param;
  float wire_opacity;
  /** Edit Curve */
  float edit_curve_normal_length;
  /** Edit Mesh */
  int data_mask[4];
} OverlayShadingData;

typedef struct OverlayExtraCallBuffers {
  DrawCallBuffer *camera_frame;
  DrawCallBuffer *camera_tria[2];
  DrawCallBuffer *camera_distances;
  DrawCallBuffer *camera_volume;
  DrawCallBuffer *camera_volume_frame;

  DrawCallBuffer *center_active;
  DrawCallBuffer *center_selected;
  DrawCallBuffer *center_deselected;
  DrawCallBuffer *center_selected_lib;
  DrawCallBuffer *center_deselected_lib;

  DrawCallBuffer *empty_axes;
  DrawCallBuffer *empty_capsule_body;
  DrawCallBuffer *empty_capsule_cap;
  DrawCallBuffer *empty_circle;
  DrawCallBuffer *empty_cone;
  DrawCallBuffer *empty_cube;
  DrawCallBuffer *empty_cylinder;
  DrawCallBuffer *empty_image_frame;
  DrawCallBuffer *empty_plain_axes;
  DrawCallBuffer *empty_single_arrow;
  DrawCallBuffer *empty_sphere;
  DrawCallBuffer *empty_sphere_solid;

  DrawCallBuffer *extra_dashed_lines;
  DrawCallBuffer *extra_lines;
  DrawCallBuffer *extra_points;

  DrawCallBuffer *field_curve;
  DrawCallBuffer *field_force;
  DrawCallBuffer *field_vortex;
  DrawCallBuffer *field_wind;
  DrawCallBuffer *field_cone_limit;
  DrawCallBuffer *field_sphere_limit;
  DrawCallBuffer *field_tube_limit;

  DrawCallBuffer *groundline;

  DrawCallBuffer *light_point;
  DrawCallBuffer *light_sun;
  DrawCallBuffer *light_spot;
  DrawCallBuffer *light_spot_cone_back;
  DrawCallBuffer *light_spot_cone_front;
  DrawCallBuffer *light_area[2];

  DrawCallBuffer *origin_xform;

  DrawCallBuffer *probe_planar;
  DrawCallBuffer *probe_cube;
  DrawCallBuffer *probe_grid;

  DrawCallBuffer *solid_quad;

  DrawCallBuffer *speaker;

  DrawShadingGroup *extra_wire;
  DrawShadingGroup *extra_loose_points;
} OverlayExtraCallBuffers;

typedef struct OverlayArmatureCallBuffersInner {
  DrawCallBuffer *box_outline;
  DrawCallBuffer *box_fill;

  DrawCallBuffer *dof_lines;
  DrawCallBuffer *dof_sphere;

  DrawCallBuffer *envelope_distance;
  DrawCallBuffer *envelope_outline;
  DrawCallBuffer *envelope_fill;

  DrawCallBuffer *octa_outline;
  DrawCallBuffer *octa_fill;

  DrawCallBuffer *point_outline;
  DrawCallBuffer *point_fill;

  DrawCallBuffer *stick;

  DrawCallBuffer *wire;

  DrawShadingGroup *custom_outline;
  DrawShadingGroup *custom_fill;
  DrawShadingGroup *custom_wire;

  GHash *custom_shapes_ghash;
} OverlayArmatureCallBuffersInner;

typedef struct OverlayArmatureCallBuffers {
  OverlayArmatureCallBuffersInner solid;
  OverlayArmatureCallBuffersInner transp;
} OverlayArmatureCallBuffers;

typedef struct OverlayPrivateData {
  DrawShadingGroup *armature_bone_select_act_grp;
  DrawShadingGroup *armature_bone_select_grp;
  DrawShadingGroup *edit_curve_normal_grp[2];
  DrawShadingGroup *edit_curve_wire_grp[2];
  DrawShadingGroup *edit_curve_handle_grp;
  DrawShadingGroup *edit_curve_points_grp;
  DrawShadingGroup *edit_lattice_points_grp;
  DrawShadingGroup *edit_lattice_wires_grp;
  DrawShadingGroup *edit_dpen_points_grp;
  DrawShadingGroup *edit_dpen_wires_grp;
  DrawShadingGroup *edit_dpen_curve_handle_grp;
  DrawShadingGroup *edit_dpen_curve_points_grp;
  DrawShadingGroup *edit_mesh_depth_grp[2];
  DrawShadingGroup *edit_mesh_faces_grp[2];
  DrawShadingGroup *edit_mesh_faces_cage_grp[2];
  DrawShadingGroup *edit_mesh_verts_grp[2];
  DrawShadingGroup *edit_mesh_edges_grp[2];
  DrawShadingGroup *edit_mesh_facedots_grp[2];
  DRWShadingGroup *edit_mesh_skin_roots_grp[2];
  DRWShadingGroup *edit_mesh_normals_grp;
  DRWShadingGroup *edit_mesh_analysis_grp;
  DRWShadingGroup *edit_particle_strand_grp;
  DRWShadingGroup *edit_particle_point_grp;
  DRWShadingGroup *edit_text_overlay_grp;
  DRWShadingGroup *edit_text_wire_grp[2];
  DRWShadingGroup *edit_uv_verts_grp;
  DRWShadingGroup *edit_uv_edges_grp;
  DRWShadingGroup *edit_uv_shadow_edges_grp;
  DRWShadingGroup *edit_uv_faces_grp;
  DRWShadingGroup *edit_uv_face_dots_grp;
  DRWShadingGroup *edit_uv_stretching_grp;
  DRWShadingGroup *extra_grid_grp;
  DRWShadingGroup *facing_grp[2];
  DRWShadingGroup *fade_grp[2];
  DRWShadingGroup *flash_grp[2];
  DRWShadingGroup *motion_path_lines_grp;
  DRWShadingGroup *motion_path_points_grp;
  DRWShadingGroup *outlines_grp;
  DRWShadingGroup *outlines_ptcloud_grp;
  DRWShadingGroup *outlines_gpencil_grp;
  DRWShadingGroup *paint_depth_grp;
  DRWShadingGroup *paint_surf_grp;
  DRWShadingGroup *paint_wire_grp;
  DRWShadingGroup *paint_wire_selected_grp;
  DRWShadingGroup *paint_point_grp;
  DRWShadingGroup *paint_face_grp;
  DRWShadingGroup *particle_dots_grp;
  DRWShadingGroup *particle_shapes_grp;
  DRWShadingGroup *pointcloud_dots_grp;
  DRWShadingGroup *sculpt_mask_grp;
  DRWShadingGroup *volume_selection_surface_grp;
  DRWShadingGroup *wires_grp[2][2];      /* With and without coloring. */
  DRWShadingGroup *wires_all_grp[2][2];  /* With and without coloring. */
  DRWShadingGroup *wires_hair_grp[2][2]; /* With and without coloring. */
  DRWShadingGroup *wires_sculpt_grp[2];

  DrawView *view_default;
  DrawView *view_wires;
  DrawView *view_edit_faces;
  DrawView *view_edit_faces_cage;
  DrawView *view_edit_edges;
  DrawView *view_edit_verts;
  DrawView *view_edit_text;
  DrawView *view_ref_images;

  /** TODO: get rid of this. */
  ListBase smoke_domains;
  ListBase bg_movie_clips;

  /** Two instances for in_front option and without. */
  OverlayExtraCallBuffers extra_call_buffers[2];

  OverlayArmatureCallBuffers armature_call_buffers[2];

  View3DOverlay overlay;
  enum eCtxObjectMode ctx_mode;
  char space_type;
  bool clear_in_front;
  bool use_in_front;
  bool wireframe_mode;
  bool hide_overlays;
  bool xray_enabled;
  bool xray_enabled_and_not_wire;
  float xray_opacity;
  short v3d_flag;     /* TODO: move to #View3DOverlay. */
  short v3d_gridflag; /* TODO: move to #View3DOverlay. */
  int cfra;
  DrawState clipping_state;
  OverlayShadingData shdata;

  struct {
    bool enabled;
    bool do_depth_copy;
    bool do_depth_infront_copy;
  } antialiasing;
  struct {
    bool show_handles;
    int handle_display;
  } edit_curve;
  struct {
    float overlay_color[4];
  } edit_text;
  struct {
    bool do_zbufclip;
    bool do_faces;
    bool do_edges;
    bool select_vert;
    bool select_face;
    bool select_edge;
    int flag; /** Copy of #v3d->overlay.edit_flag. */
  } edit_mesh;
  struct {
    bool use_weight;
    int select_mode;
  } edit_particle;
  struct {
    bool do_uv_overlay;
    bool do_uv_shadow_overlay;
    bool do_uv_stretching_overlay;
    bool do_tiled_image_overlay;
    bool do_tiled_image_border_overlay;
    bool do_stencil_overlay;
    bool do_mask_overlay;

    bool do_verts;
    bool do_faces;
    bool do_face_dots;

    float uv_opacity;

    int image_size[2];
    float image_aspect[2];

    /* edge drawing */
    OverlayUVLineStyle line_style;
    float dash_length;
    int do_smooth_wire;

    /* stretching overlay */
    float uv_aspect[2];
    eSpaceImage_UVDT_Stretch draw_type;
    ListBase totals;
    float total_area_ratio;
    float total_area_ratio_inv;

    /* stencil overlay */
    struct Image *stencil_image;
    struct ImBuf *stencil_ibuf;
    void *stencil_lock;

    /* mask overlay */
    Mask *mask;
    eMaskOverlayMode mask_overlay_mode;
    GPUTexture *mask_texture;
  } edit_uv;
  struct {
    bool transparent;
    bool show_relations;
    bool do_pose_xray;
    bool do_pose_fade_geom;
  } armature;
  struct {
    bool in_front;
    bool alpha_blending;
  } painting;
  struct {
    DrawCallBuffer *handle[2];
  } mball;
  struct {
    double time;
    bool any_animated;
  } mode_transfer;
} OVERLAY_PrivateData; /* Transient data */

typedef struct OVERLAY_StorageList {
  struct OVERLAY_PrivateData *pd;
} OVERLAY_StorageList;

typedef struct OVERLAY_Data {
  void *engine_type;
  OVERLAY_FramebufferList *fbl;
  OVERLAY_TextureList *txl;
  OVERLAY_PassList *psl;
  OVERLAY_StorageList *stl;
} OVERLAY_Data;

typedef struct OVERLAY_DupliData {
  DRWShadingGroup *wire_shgrp;
  DRWShadingGroup *outline_shgrp;
  DRWShadingGroup *extra_shgrp;
  struct GPUBatch *wire_geom;
  struct GPUBatch *outline_geom;
  struct GPUBatch *extra_geom;
  short base_flag;
} OVERLAY_DupliData;

typedef struct BoneInstanceData {
  /* Keep sync with bone instance vertex format (OVERLAY_InstanceFormats) */
  union {
    float mat[4][4];
    struct {
      float _pad0[3], color_hint_a;
      float _pad1[3], color_hint_b;
      float _pad2[3], color_a;
      float _pad3[3], color_b;
    };
    struct {
      float _pad00[3], amin_a;
      float _pad01[3], amin_b;
      float _pad02[3], amax_a;
      float _pad03[3], amax_b;
    };
  };
} BoneInstanceData;

typedef struct OVERLAY_InstanceFormats {
  struct GPUVertFormat *instance_pos;
  struct GPUVertFormat *instance_extra;
  struct GPUVertFormat *instance_bone;
  struct GPUVertFormat *instance_bone_outline;
  struct GPUVertFormat *instance_bone_envelope;
  struct GPUVertFormat *instance_bone_envelope_distance;
  struct GPUVertFormat *instance_bone_envelope_outline;
  struct GPUVertFormat *instance_bone_stick;
  struct GPUVertFormat *pos;
  struct GPUVertFormat *pos_color;
  struct GPUVertFormat *wire_extra;
  struct GPUVertFormat *point_extra;
} OVERLAY_InstanceFormats;

/* Pack data into the last row of the 4x4 matrix. It will be decoded by the vertex shader. */
BLI_INLINE void pack_data_in_mat4(
    float rmat[4][4], const float mat[4][4], float a, float b, float c, float d)
{
  copy_m4_m4(rmat, mat);
  rmat[0][3] = a;
  rmat[1][3] = b;
  rmat[2][3] = c;
  rmat[3][3] = d;
}

BLI_INLINE void pack_v4_in_mat4(float rmat[4][4], const float mat[4][4], const float v[4])
{
  pack_data_in_mat4(rmat, mat, v[0], v[1], v[2], v[3]);
}

BLI_INLINE void pack_fl_in_mat4(float rmat[4][4], const float mat[4][4], float a)
{
  copy_m4_m4(rmat, mat);
  rmat[3][3] = a;
}

void OVERLAY_antialiasing_init(OVERLAY_Data *vedata);
void OVERLAY_antialiasing_cache_init(OVERLAY_Data *vedata);
void OVERLAY_antialiasing_cache_finish(OVERLAY_Data *vedata);
void OVERLAY_antialiasing_start(OVERLAY_Data *vedata);
void OVERLAY_antialiasing_end(OVERLAY_Data *vedata);
void OVERLAY_xray_fade_draw(OVERLAY_Data *vedata);
void OVERLAY_xray_depth_copy(OVERLAY_Data *vedata);
void OVERLAY_xray_depth_infront_copy(OVERLAY_Data *vedata);

/**
 * Return true if armature should be handled by the pose mode engine.
 */
bool OVERLAY_armature_is_pose_mode(Object *ob, const struct DRWContextState *draw_ctx);
void OVERLAY_armature_cache_init(OVERLAY_Data *vedata);
void OVERLAY_armature_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_armature_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_pose_armature_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_armature_cache_finish(OVERLAY_Data *vedata);
void OVERLAY_armature_draw(OVERLAY_Data *vedata);
void OVERLAY_armature_in_front_draw(OVERLAY_Data *vedata);
void OVERLAY_pose_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_pose_draw(OVERLAY_Data *vedata);

void OVERLAY_background_cache_init(OVERLAY_Data *vedata);
void OVERLAY_background_draw(OVERLAY_Data *vedata);

void OVERLAY_bone_instance_data_set_color_hint(BoneInstanceData *data, const float hint_color[4]);
void OVERLAY_bone_instance_data_set_color(BoneInstanceData *data, const float bone_color[4]);

void OVERLAY_edit_curve_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_curve_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_surf_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_curve_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_gpencil_cache_init(OVERLAY_Data *vedata);
void OVERLAY_gpencil_cache_init(OVERLAY_Data *vedata);
void OVERLAY_gpencil_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_gpencil_draw(OVERLAY_Data *vedata);
void OVERLAY_edit_gpencil_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_lattice_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_lattice_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_lattice_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_lattice_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_text_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_text_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_text_draw(OVERLAY_Data *vedata);

void OVERLAY_volume_cache_init(OVERLAY_Data *vedata);
void OVERLAY_volume_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_volume_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_mesh_init(OVERLAY_Data *vedata);
void OVERLAY_edit_mesh_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_mesh_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_mesh_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_particle_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_particle_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_edit_particle_draw(OVERLAY_Data *vedata);

void OVERLAY_edit_uv_init(OVERLAY_Data *vedata);
void OVERLAY_edit_uv_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_uv_cache_finish(OVERLAY_Data *vedata);
void OVERLAY_edit_uv_draw(OVERLAY_Data *vedata);

void OVERLAY_extra_cache_init(OVERLAY_Data *vedata);
void OVERLAY_extra_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_extra_blend_draw(OVERLAY_Data *vedata);
void OVERLAY_extra_draw(OVERLAY_Data *vedata);
void OVERLAY_extra_in_front_draw(OVERLAY_Data *vedata);
void OVERLAY_extra_centers_draw(OVERLAY_Data *vedata);

void OVERLAY_camera_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_empty_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_light_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_lightprobe_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_speaker_cache_populate(OVERLAY_Data *vedata, Object *ob);

OVERLAY_ExtraCallBuffers *OVERLAY_extra_call_buffer_get(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_extra_point(OVERLAY_ExtraCallBuffers *cb, const float point[3], const float color[4]);
void OVERLAY_extra_line_dashed(OVERLAY_ExtraCallBuffers *cb,
                               const float start[3],
                               const float end[3],
                               const float color[4]);
void OVERLAY_extra_line(OVERLAY_ExtraCallBuffers *cb,
                        const float start[3],
                        const float end[3],
                        int color_id);
void OVERLAY_empty_shape(OVERLAY_ExtraCallBuffers *cb,
                         const float mat[4][4],
                         float draw_size,
                         char draw_type,
                         const float color[4]);
void OVERLAY_extra_loose_points(OVERLAY_ExtraCallBuffers *cb,
                                struct GPUBatch *geom,
                                const float mat[4][4],
                                const float color[4]);
void OVERLAY_extra_wire(OVERLAY_ExtraCallBuffers *cb,
                        struct GPUBatch *geom,
                        const float mat[4][4],
                        const float color[4]);

void OVERLAY_facing_init(OVERLAY_Data *vedata);
void OVERLAY_facing_cache_init(OVERLAY_Data *vedata);
void OVERLAY_facing_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_facing_draw(OVERLAY_Data *vedata);
void OVERLAY_facing_infront_draw(OVERLAY_Data *vedata);

void OVERLAY_fade_init(OVERLAY_Data *vedata);
void OVERLAY_fade_cache_init(OVERLAY_Data *vedata);
void OVERLAY_fade_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_fade_draw(OVERLAY_Data *vedata);
void OVERLAY_fade_infront_draw(OVERLAY_Data *vedata);

void OVERLAY_mode_transfer_cache_init(OVERLAY_Data *vedata);
void OVERLAY_mode_transfer_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_mode_transfer_draw(OVERLAY_Data *vedata);
void OVERLAY_mode_transfer_infront_draw(OVERLAY_Data *vedata);
void OVERLAY_mode_transfer_cache_finish(OVERLAY_Data *vedata);

void OVERLAY_grid_init(OVERLAY_Data *vedata);
void OVERLAY_grid_cache_init(OVERLAY_Data *vedata);
void OVERLAY_grid_draw(OVERLAY_Data *vedata);

void OVERLAY_image_init(OVERLAY_Data *vedata);
void OVERLAY_image_cache_init(OVERLAY_Data *vedata);
void OVERLAY_image_camera_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_image_empty_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_image_cache_finish(OVERLAY_Data *vedata);
void OVERLAY_image_draw(OVERLAY_Data *vedata);
void OVERLAY_image_background_draw(OVERLAY_Data *vedata);
/**
 * This function draws images that needs the view transform applied.
 * It draws these images directly into the scene color buffer.
 */
void OVERLAY_image_scene_background_draw(OVERLAY_Data *vedata);
void OVERLAY_image_in_front_draw(OVERLAY_Data *vedata);

void OVERLAY_metaball_cache_init(OVERLAY_Data *vedata);
void OVERLAY_edit_metaball_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_metaball_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_metaball_draw(OVERLAY_Data *vedata);
void OVERLAY_metaball_in_front_draw(OVERLAY_Data *vedata);

void OVERLAY_motion_path_cache_init(OVERLAY_Data *vedata);
void OVERLAY_motion_path_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_motion_path_draw(OVERLAY_Data *vedata);

void OVERLAY_outline_init(OVERLAY_Data *vedata);
void OVERLAY_outline_cache_init(OVERLAY_Data *vedata);
void OVERLAY_outline_cache_populate(OVERLAY_Data *vedata,
                                    Object *ob,
                                    OVERLAY_DupliData *dupli,
                                    bool init_dupli);
void OVERLAY_outline_draw(OVERLAY_Data *vedata);

void OVERLAY_paint_init(OVERLAY_Data *vedata);
void OVERLAY_paint_cache_init(OVERLAY_Data *vedata);
void OVERLAY_paint_texture_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_paint_vertex_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_paint_weight_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_paint_draw(OVERLAY_Data *vedata);

void OVERLAY_particle_cache_init(OVERLAY_Data *vedata);
void OVERLAY_particle_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_particle_draw(OVERLAY_Data *vedata);

void OVERLAY_sculpt_cache_init(OVERLAY_Data *vedata);
void OVERLAY_sculpt_cache_populate(OVERLAY_Data *vedata, Object *ob);
void OVERLAY_sculpt_draw(OVERLAY_Data *vedata);

void OVERLAY_wireframe_init(OVERLAY_Data *vedata);
void OVERLAY_wireframe_cache_init(OVERLAY_Data *vedata);
void OVERLAY_wireframe_cache_populate(OVERLAY_Data *vedata,
                                      Object *ob,
                                      OVERLAY_DupliData *dupli,
                                      bool init_dupli);
void OVERLAY_wireframe_draw(OVERLAY_Data *vedata);
void OVERLAY_wireframe_in_front_draw(OVERLAY_Data *vedata);

void OVERLAY_shader_library_ensure(void);
GPUShader *OVERLAY_shader_antialiasing(void);
GPUShader *OVERLAY_shader_armature_degrees_of_freedom_wire(void);
GPUShader *OVERLAY_shader_armature_degrees_of_freedom_solid(void);
GPUShader *OVERLAY_shader_armature_envelope(bool use_outline);
GPUShader *OVERLAY_shader_armature_shape(bool use_outline);
GPUShader *OVERLAY_shader_armature_shape_wire(void);
GPUShader *OVERLAY_shader_armature_sphere(bool use_outline);
GPUShader *OVERLAY_shader_armature_stick(void);
GPUShader *OVERLAY_shader_armature_wire(void);
GPUShader *OVERLAY_shader_background(void);
GPUShader *OVERLAY_shader_clipbound(void);
GPUShader *OVERLAY_shader_depth_only(void);
GPUShader *OVERLAY_shader_edit_curve_handle(void);
GPUShader *OVERLAY_shader_edit_curve_point(void);
GPUShader *OVERLAY_shader_edit_curve_wire(void);
GPUShader *OVERLAY_shader_edit_gpencil_guide_point(void);
GPUShader *OVERLAY_shader_edit_gpencil_point(void);
GPUShader *OVERLAY_shader_edit_gpencil_wire(void);
GPUShader *OVERLAY_shader_edit_lattice_point(void);
GPUShader *OVERLAY_shader_edit_lattice_wire(void);
GPUShader *OVERLAY_shader_edit_mesh_analysis(void);
GPUShader *OVERLAY_shader_edit_mesh_edge(bool use_flat_interp);
GPUShader *OVERLAY_shader_edit_mesh_face(void);
GPUShader *OVERLAY_shader_edit_mesh_facedot(void);
GPUShader *OVERLAY_shader_edit_mesh_normal(void);
GPUShader *OVERLAY_shader_edit_mesh_skin_root(void);
GPUShader *OVERLAY_shader_edit_mesh_vert(void);
GPUShader *OVERLAY_shader_edit_particle_strand(void);
GPUShader *OVERLAY_shader_edit_particle_point(void);
GPUShader *OVERLAY_shader_edit_uv_edges_get(void);
GPUShader *OVERLAY_shader_edit_uv_edges_for_edge_select_get(void);
GPUShader *OVERLAY_shader_edit_uv_face_get(void);
GPUShader *OVERLAY_shader_edit_uv_face_dots_get(void);
GPUShader *OVERLAY_shader_edit_uv_verts_get(void);
GPUShader *OVERLAY_shader_edit_uv_stretching_area_get(void);
GPUShader *OVERLAY_shader_edit_uv_stretching_angle_get(void);
GPUShader *OVERLAY_shader_edit_uv_tiled_image_borders_get(void);
GPUShader *OVERLAY_shader_edit_uv_stencil_image(void);
GPUShader *OVERLAY_shader_edit_uv_mask_image(void);
GPUShader *OVERLAY_shader_extra(bool is_select);
GPUShader *OVERLAY_shader_extra_groundline(void);
GPUShader *OVERLAY_shader_extra_wire(bool use_object, bool is_select);
GPUShader *OVERLAY_shader_extra_loose_point(void);
GPUShader *OVERLAY_shader_extra_point(void);
GPUShader *OVERLAY_shader_facing(void);
GPUShader *OVERLAY_shader_gpencil_canvas(void);
GPUShader *OVERLAY_shader_grid(void);
GPUShader *OVERLAY_shader_grid_background(void);
GPUShader *OVERLAY_shader_grid_image(void);
GPUShader *OVERLAY_shader_image(void);
GPUShader *OVERLAY_shader_motion_path_line(void);
GPUShader *OVERLAY_shader_motion_path_vert(void);
GPUShader *OVERLAY_shader_uniform_color(void);
GPUShader *OVERLAY_shader_outline_prepass(bool use_wire);
GPUShader *OVERLAY_shader_outline_prepass_gpencil(void);
GPUShader *OVERLAY_shader_outline_prepass_pointcloud(void);
GPUShader *OVERLAY_shader_extra_grid(void);
GPUShader *OVERLAY_shader_outline_detect(void);
GPUShader *overlay_shader_paint_face(void);
GPUShader *overlay_shader_paint_point(void);
GPUShader *overlay_shader_paint_texture(void);
GPUShader *overlay_shader_paint_vertcol(void);
GPUShader *overlay_shader_paint_weight(bool shading);
GPUShader *overlay_shader_paint_wire(void);
GPUShader *overlay_shader_particle_dot(void);
GPUShader *overlay_shader_particle_shape(void);
GPUShader *overlay_shader_sculpt_mask(void);
GPUShader *overlay_shader_volume_velocity(bool use_needle, bool use_mac);
GPUShader *overlay_shader_volume_gridlines(bool color_with_flags, bool color_range);
GPUShader *overlay_shader_wireframe(bool custom_bias);
GPUShader *overlay_shader_wireframe_select(void);
GPUShader *overlay_shader_xray_fade(void);

OverlayInstanceFormats *overlay_shader_instance_formats_get(void);

void overlay_shader_free(void);

#ifdef __cplusplus
}
#endif
