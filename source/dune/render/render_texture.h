/** This include is for non-render pipeline exports (still old cruft here). **/

#pragma once

#include "lib_compiler_attrs.h"

/* called by meshtools */
struct Graph;
struct ImagePool;
struct MeshTex;
struct Tex;

#ifdef __cplusplus
extern "C" {
#endif

/* texture_procedural.c */

/**
 * param pool: Thread pool, may be NULL.
 *
 * return True if the texture has color, otherwise false.
 */
bool render_texture_evaluate(const struct MTex *mtex,
                         const float vec[3],
                         int thread,
                         struct ImagePool *pool,
                         bool skip_load_image,
                         bool texnode_preview,
                         /* Return arguments. */
                         float *r_intensity,
                         float r_rgba[4]) ATTR_NONNULL(1, 2, 7, 8);

/**
 * param in: Destination
 * param tex: Texture.
 * param out: Previous color.
 * param fact: Texture strength.
 * param facg: Button strength value.
 */
void texture_rgb_blend(
    float in[3], const float tex[3], const float out[3], float fact, float facg, int blendtype);
float texture_value_blend(float tex, float out, float fact, float facg, int blendtype);

void render_texture_rng_init(void);
void render_texture_rng_exit(void);

/* texture_image.c */

void ibuf_sample(struct ImBuf *ibuf, float fx, float fy, float dx, float dy, float result[4]);

/* texture_pointdensity.c */

struct PointDensity;

void render_point_density_cache(struct Graph *graph, struct PointDensity *pd);

void render_point_density_minmax(struct Graph *graph,
                             struct PointDensity *pd,
                             float r_min[3],
                             float r_max[3]);

/**
 * note Requires render_point_density_cache() to be called first.
 * note Frees point density structure after sampling.
 */
void render_point_density_sample(struct Graph *graph,
                                 struct PointDensity *pd,
                                 int resolution,
                                 float *values);

void render_point_density_free(struct PointDensity *pd);

void render_point_density_fix_linking(void);

/* texture_procedural.c */

/** Texture evaluation result. */
typedef struct TexResult {
  float tin;
  float trgba[4];
  /* Is actually a boolean: When true -> use alpha, false -> set alpha to 1.0. */
  int talpha;
  float *nor;
} TexResult;

/* This one uses nodes. */

/**
 * warning if the texres's values are not declared zero,
 * check the return value to be sure the color values are set before using the r/g/b values,
 * otherwise you may use uninitialized values - Campbell
 *
 * Use it for stuff which is out of render pipeline.
 */
int multitex_ext(struct Tex *tex,
                 float texvec[3],
                 float dxt[3],
                 float dyt[3],
                 int osatex,
                 struct TexResult *texres,
                 short thread,
                 struct ImagePool *pool,
                 bool scene_color_manage,
                 bool skip_load_image);

/**
 * Nodes disabled.
 * extern-tex doesn't support nodes (ntreeBeginEx() can't be called when rendering is going on).
 *
 * Use it for stuff which is out of render pipeline.
 */
int multitex_ext_safe(struct Tex *tex,
                      const float texvec[3],
                      struct TexResult *texres,
                      struct ImagePool *pool,
                      bool scene_color_manage,
                      bool skip_load_image);

/**
 * Only for internal node usage.
 *
 * this is called from the shader and texture nodes
 * Use it from render pipeline only!
 */
int multitex_nodes(struct Tex *tex,
                   const float texvec[3],
                   float dxt[3],
                   float dyt[3],
                   int osatex,
                   struct TexResult *texres,
                   short thread,
                   short which_output,
                   struct MTex *mtex,
                   struct ImagePool *pool);

#ifdef __cplusplus
}
#endif
