#pragma once

/** General operations for brushes. **/

#include "structs_color_types.h"
#include "structs_object_enums.h"

struct Brush;
struct ImBuf;
struct ImagePool;
struct Object;
struct Main;
struct MTex;
struct Scene;
struct ToolSettings;
struct UnifiedPaintSettings;

// enum eCurveMappingPreset;

/* Globals for brush execution. */

void KERNEL_brush_system_init(void);
void KERNEL_brush_system_exit(void);

/* Data-block functions. */

/**
 * note Resulting brush will have two users: one as a fake user,
 * another is assumed to be used by the caller.
 */
struct Brush *KERNEL_brush_add(struct Main *dunemain, const char *name, eObjectMode ob_mode);
/**
 * Add a new gp-brush.
 */
struct Brush *KERNEL_brush_add_gpencil(struct Main *dunemain,
                                    struct ToolSettings *ts,
                                    const char *name,
                                    eObjectMode mode);
/**
 * Delete a Brush.
 */
bool KERNEL_brush_delete(struct Main *dunemain, struct Brush *brush);
/**
 * Add grease pencil settings.
 */
void KERNEL_brush_init_gpencil_settings(struct Brush *brush);

void KERNEL_brush_init_curves_sculpt_settings(struct Brush *brush);

struct Brush *KERNEL_brush_first_search(struct Main *dunemain, eObjectMode ob_mode);

void KERNEL_brush_sculpt_reset(struct Brush *brush);

/**
 * Create a set of grease pencil Drawing presets.
 */
void KERNEL_brush_gpencil_paint_presets(struct Main *dunemain, struct ToolSettings *ts, bool reset);
/**
 * Create a set of grease pencil Vertex Paint presets.
 */
void KERNEL_brush_gpencil_vertex_presets(struct Main *dunemain, struct ToolSettings *ts, bool reset);
/**
 * Create a set of grease pencil Sculpt Paint presets.
 */
void KERNEL_brush_gpencil_sculpt_presets(struct Main *dunemain, struct ToolSettings *ts, bool reset);
/**
 * Create a set of grease pencil Weight Paint presets.
 */
void KERNEL_brush_gpencil_weight_presets(struct Main *dunemain, struct ToolSettings *ts, bool reset);
void KERNEL_gpencil_brush_preset_set(struct Main *dunemain, struct Brush *brush, short type);

void KERNEL_brush_jitter_pos(const struct Scene *scene,
                          struct Brush *brush,
                          const float pos[2],
                          float jitterpos[2]);
void KERNEL_brush_randomize_texture_coords(struct UnifiedPaintSettings *ups, bool mask);

/* Brush curve. */

/**
 * Library Operations
 */
void KERNEL_brush_curve_preset(struct Brush *b, enum eCurveMappingPreset preset);
/**
 * Uses the brush curve control to find a strength value between 0 and 1.
 */
float KERNEL_brush_curve_strength_clamped(const struct Brush *br, float p, float len);
/**
 * Uses the brush curve control to find a strength value.
 */
float KERNEL_brush_curve_strength(const struct Brush *br, float p, float len);

/* Sampling. */

/**
 * Generic texture sampler for 3D painting systems.
 * point has to be either in region space mouse coordinates,
 * or 3d world coordinates for 3D mapping.
 *
 * RGBA outputs straight alpha.
 */
float KERNEL_brush_sample_tex_3d(const struct Scene *scene,
                              const struct Brush *br,
                              const struct MTex *mtex,
                              const float point[3],
                              float rgba[4],
                              int thread,
                              struct ImagePool *pool);
float KERNEL_brush_sample_masktex(const struct Scene *scene,
                               struct Brush *br,
                               const float point[2],
                               int thread,
                               struct ImagePool *pool);

/**
 * Get the mask texture for this given object mode.
 *
 * This is preferred above using mtex/mask_mtex attributes directly as due to legacy these
 * attributes got switched in sculpt mode.
 */
const struct MTex *KERNEL_brush_mask_texture_get(const struct Brush *brush,
                                              const eObjectMode object_mode);

/**
 * Get the color texture for this given object mode.
 *
 * This is preferred above using mtex/mask_mtex attributes directly as due to legacy these
 * attributes got switched in sculpt mode.
 */
const struct MTex *KERNEL_brush_color_texture_get(const struct Brush *brush,
                                               const eObjectMode object_mode);

/**
 * Radial control.
 */
struct ImBuf *KERNEL_brush_gen_radial_control_imbuf(struct Brush *br,
                                                 bool secondary,
                                                 bool display_gradient);

/* Unified strength size and color. */

const float *KERNEL_brush_color_get(const struct Scene *scene, const struct Brush *brush);
const float *KERNEL_brush_secondary_color_get(const struct Scene *scene, const struct Brush *brush);
void KERNEL_brush_color_set(struct Scene *scene, struct Brush *brush, const float color[3]);

int KERNEL_brush_size_get(const struct Scene *scene, const struct Brush *brush);
void KERNEL_brush_size_set(struct Scene *scene, struct Brush *brush, int size);

float KERNEL_brush_unprojected_radius_get(const struct Scene *scene, const struct Brush *brush);
void KERNEL_brush_unprojected_radius_set(struct Scene *scene,
                                      struct Brush *brush,
                                      float unprojected_radius);

float KERNEL_brush_alpha_get(const struct Scene *scene, const struct Brush *brush);
void KERNEL_brush_alpha_set(struct Scene *scene, struct Brush *brush, float alpha);
float KERNEL_brush_weight_get(const struct Scene *scene, const struct Brush *brush);
void KERNEL_brush_weight_set(const struct Scene *scene, struct Brush *brush, float value);

bool KERNEL_brush_use_locked_size(const struct Scene *scene, const struct Brush *brush);
bool KERNEL_brush_use_alpha_pressure(const struct Brush *brush);
bool KERNEL_brush_use_size_pressure(const struct Brush *brush);

bool KERNEL_brush_sculpt_has_secondary_color(const struct Brush *brush);

/**
 * Scale unprojected radius to reflect a change in the brush's 2D size.
 */
void KERNEL_brush_scale_unprojected_radius(float *unprojected_radius,
                                        int new_brush_size,
                                        int old_brush_size);

/**
 * Scale brush size to reflect a change in the brush's unprojected radius.
 */
void KERNEL_brush_scale_size(int *r_brush_size,
                          float new_unprojected_radius,
                          float old_unprojected_radius);

/* Accessors */
#define KERNEL_brush_tool_get(brush, p) \
  (CHECK_TYPE_ANY(brush, struct Brush *, const struct Brush *), \
   *(const char *)POINTER_OFFSET(brush, (p)->runtime.tool_offset))
#define KE_brush_tool_set(brush, p, tool) \
  { \
    CHECK_TYPE_ANY(brush, struct Brush *); \
    *(char *)POINTER_OFFSET(brush, (p)->runtime.tool_offset) = tool; \
  } \
  ((void)0)

/* debugging only */
void KERNEL_brush_debug_print_state(struct Brush *br);
