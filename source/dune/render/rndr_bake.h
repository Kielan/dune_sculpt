
#pragma once

struct Graph;
struct ImBuf;
struct MeshLoopUV;
struct Mesh;
struct Rndr;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BakeImg {
  struct Img *img;
  int width;
  int height;
  size_t offset;
} BakeImg;

typedef struct BakeTargets {
  /* All imgs of the ob. */
  BakeImg *imgs;
  int num_imgs;

  /* Lookup table from Material number to BakeImg. */
  int *material_to_img;
  int num_materials;

  /* Pixel buf to bake to. */
  float *result;
  int num_pixels;
  int num_channels;

  /* Baking to non-color data image. */
  bool is_noncolor;
} BakeTargets;

typedef struct BakePixel {
  int primitive_id, ob_id;
  int seed;
  float uv[2];
  float du_dx, du_dy;
  float dv_dx, dv_dy;
} BakePixel;

typedef struct BakeHighPolyData {
  struct Ob *ob;
  struct Ob *ob_eval;
  struct Mesh *me;
  bool is_flip_ob;

  float obmat[4][4];
  float imat[4][4];
} BakeHighPolyData;

/* external_engine.c */
bool rndr_bake_has_engine(const struct Rndr *re);

bool rndr_bake_engine(struct Render *re,
                    struct Graph *graph,
                    struct Ob *ob,
                    int ob_id,
                    const BakePixel pixel_array[],
                    const BakeTargets *targets,
                    eScenePassType pass_type,
                    int pass_filter,
                    float result[]);

/* bake.c */
int rndr_pass_depth(eScenePassType pass_type);

bool rndr_bake_pixels_populate_from_obs(struct Mesh *me_low,
                                        BakePixel pixel_arr_from[],
                                        BakePixel pixel_arr_to[],
                                        BakeHighPolyData highpoly[],
                                        int tot_highpoly,
                                        size_t num_pixels,
                                        bool is_custom_cage,
                                        float cage_extrusion,
                                        float max_ray_distance,
                                        float mat_low[4][4],
                                        float mat_cage[4][4],
                                        struct Mesh *me_cage);

void rndr_bake_pixels_populate(struct Mesh *me,
                             struct BakePixel *pixel_array,
                             size_t num_pixels,
                             const struct BakeTargets *targets,
                             const char *uv_layer);

void rndr_bake_mask_fill(const BakePixel pixel_array[], size_t num_pixels, char *mask);

void rndr_bake_margin(struct ImBuf *ibuf,
                    char *mask,
                    int margin,
                    char margin_type,
                    struct Mesh const *me,
                    char const *uv_layer);

void rndr_bake_normal_world_to_ob(const BakePixel pixel_array[],
                                  size_t num_pixels,
                                  int depth,
                                  float result[],
                                  struct Ob *ob,
                                  const eBakeNormalSwizzle normal_swizzle[3]);
/* This fn converts an ob space normal map
 * to a tangent space normal map for a given low poly mesh. */
void rndr_bake_normal_world_to_tangent(const BakePixel pixel_array[],
                                     size_t num_pixels,
                                     int depth,
                                     float result[],
                                     struct Mesh *me,
                                     const eBakeNormalSwizzle normal_swizzle[3],
                                     float mat[4][4]);
void rndr_bake_normal_world_to_world(const BakePixel pixel_array[],
                                   size_t num_pixels,
                                   int depth,
                                   float result[],
                                   const eBakeNormalSwizzle normal_swizzle[3]);

void rndr_bake_ibuf_clear(struct Img *img, bool is_tangent);

#ifdef __cplusplus
}
#endif
