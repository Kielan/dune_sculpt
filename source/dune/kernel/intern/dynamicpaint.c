
/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include <math.h>
#include <stdio.h>

#include "BLI_blenlib.h"
#include "BLI_kdtree.h"
#include "BLI_math.h"
#include "BLI_string_utils.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_collection_types.h"
#include "DNA_constraint_types.h"
#include "DNA_dynamicpaint_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BKE_armature.h"
#include "BKE_bvhutils.h" /* bvh tree */
#include "BKE_collection.h"
#include "BKE_collision.h"
#include "BKE_colorband.h"
#include "BKE_constraint.h"
#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_dynamicpaint.h"
#include "BKE_effect.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_runtime.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

/* for image output */
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "RE_texture.h"

#include "atomic_ops.h"

#include "CLG_log.h"

/* could enable at some point but for now there are far too many conversions */
#ifdef __GNUC__
//#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif

static CLG_LogRef LOG = {"bke.dynamicpaint"};

/* precalculated gaussian factors for 5x super sampling */
static const float gaussianFactors[5] = {
    0.996849f,
    0.596145f,
    0.596145f,
    0.596145f,
    0.524141f,
};
static const float gaussianTotal = 3.309425f;

/* UV Image neighboring pixel table x and y list */
static int neighX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static int neighY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

/* Neighbor x/y list that prioritizes grid directions over diagonals */
static int neighStraightX[8] = {1, 0, -1, 0, 1, -1, -1, 1};
static int neighStraightY[8] = {0, 1, 0, -1, 1, 1, -1, -1};

/* subframe_updateObject() flags */
#define SUBFRAME_RECURSION 5
/* surface_getBrushFlags() return vals */
#define BRUSH_USES_VELOCITY (1 << 0)
/* Brush mesh ray-cast status. */
#define HIT_VOLUME 1
#define HIT_PROXIMITY 2
/* dynamicPaint_findNeighborPixel() return codes */
#define NOT_FOUND -1
#define ON_MESH_EDGE -2
#define OUT_OF_TEXTURE -3
/* paint effect default movement per frame in global units */
#define EFF_MOVEMENT_PER_FRAME 0.05f
/* initial wave time factor */
#define WAVE_TIME_FAC (1.0f / 24.0f)
#define CANVAS_REL_SIZE 5.0f
/* drying limits */
#define MIN_WETNESS 0.001f
#define MAX_WETNESS 5.0f

/* dissolve inline function */
BLI_INLINE void value_dissolve(float *r_value,
                               const float time,
                               const float scale,
                               const bool is_log)
{
  *r_value = (is_log) ? (*r_value) * (powf(MIN_WETNESS, 1.0f / (1.2f * time / scale))) :
                        (*r_value) - 1.0f / time * scale;
}

/***************************** Internal Structs ***************************/

typedef struct Bounds2D {
  float min[2], max[2];
} Bounds2D;

typedef struct Bounds3D {
  float min[3], max[3];
  bool valid;
} Bounds3D;

typedef struct VolumeGrid {
  int dim[3];
  /** whole grid bounds */
  Bounds3D grid_bounds;

  /** (x*y*z) precalculated grid cell bounds */
  Bounds3D *bounds;
  /** (x*y*z) t_index begin id */
  int *s_pos;
  /** (x*y*z) number of t_index points */
  int *s_num;
  /** actual surface point index, access: (s_pos + s_num) */
  int *t_index;

  int *temp_t_index;
} VolumeGrid;

typedef struct Vec3f {
  float v[3];
} Vec3f;

typedef struct BakeAdjPoint {
  /** vector pointing towards this neighbor */
  float dir[3];
  /** distance to */
  float dist;
} BakeAdjPoint;

/** Surface data used while processing a frame */
typedef struct PaintBakeNormal {
  /** current pixel world-space inverted normal */
  float invNorm[3];
  /** normal directional scale for displace mapping */
  float normal_scale;
} PaintBakeNormal;

/** Temp surface data used to process a frame */
typedef struct PaintBakeData {
  /* point space data */
  PaintBakeNormal *bNormal;
  /** index to start reading point sample realCoord */
  int *s_pos;
  /** num of realCoord samples */
  int *s_num;
  /** current pixel center world-space coordinates for each sample ordered as (s_pos + s_num) */
  Vec3f *realCoord;
  Bounds3D mesh_bounds;
  float dim[3];

  /* adjacency info */
  /** current global neighbor distances and directions, if required */
  BakeAdjPoint *bNeighs;
  double average_dist;
  /* space partitioning */
  /** space partitioning grid to optimize brush checks */
  VolumeGrid *grid;

  /* velocity and movement */
  /** speed vector in global space movement per frame, if required */
  Vec3f *velocity;
  Vec3f *prev_velocity;
  /** special temp data for post-p velocity based brushes like smudge
   * 3 float dir vec + 1 float str */
  float *brush_velocity;
  /** copy of previous frame vertices. used to observe surface movement. */
  MVert *prev_verts;
  /** Previous frame object matrix. */
  float prev_obmat[4][4];
  /** flag to check if surface was cleared/reset -> have to redo velocity etc. */
  int clear;
} PaintBakeData;

/** UV Image sequence format point */
typedef struct PaintUVPoint {
  /* Pixel / mesh data */
  /** tri index on domain derived mesh */
  unsigned int tri_index;
  unsigned int pixel_index;
  /* vertex indexes */
  unsigned int v1, v2, v3;

  /** If this pixel isn't uv mapped to any face, but its neighboring pixel is. */
  unsigned int neighbor_pixel;
} PaintUVPoint;

typedef struct ImgSeqFormatData {
  PaintUVPoint *uv_p;
  Vec3f *barycentricWeights; /* b-weights for all pixel samples */
} ImgSeqFormatData;

/* adjacency data flags */
#define ADJ_ON_MESH_EDGE (1 << 0)
#define ADJ_BORDER_PIXEL (1 << 1)

typedef struct PaintAdjData {
  /** Array of neighboring point indexes, for single sample use (n_index + neigh_num). */
  int *n_target;
  /** Index to start reading n_target for each point. */
  int *n_index;
  /** Number of neighbors for each point. */
  int *n_num;
  /** Vertex adjacency flags. */
  int *flags;
  /** Size of n_target. */
  int total_targets;
  /** Indices of border pixels (only for texture paint). */
  int *border;
  /** Size of border. */
  int total_border;
} PaintAdjData;

/************************* Runtime evaluation store ***************************/

void dynamicPaint_Modifier_free_runtime(DynamicPaintRuntime *runtime_data)
{
  if (runtime_data == NULL) {
    return;
  }
  if (runtime_data->canvas_mesh) {
    BKE_id_free(NULL, runtime_data->canvas_mesh);
  }
  if (runtime_data->brush_mesh) {
    BKE_id_free(NULL, runtime_data->brush_mesh);
  }
  MEM_freeN(runtime_data);
}

static DynamicPaintRuntime *dynamicPaint_Modifier_runtime_ensure(DynamicPaintModifierData *pmd)
{
  if (pmd->modifier.runtime == NULL) {
    pmd->modifier.runtime = MEM_callocN(sizeof(DynamicPaintRuntime), "dynamic paint runtime");
  }
  return (DynamicPaintRuntime *)pmd->modifier.runtime;
}

static Mesh *dynamicPaint_canvas_mesh_get(DynamicPaintCanvasSettings *canvas)
{
  if (canvas->pmd->modifier.runtime == NULL) {
    return NULL;
  }
  DynamicPaintRuntime *runtime_data = (DynamicPaintRuntime *)canvas->pmd->modifier.runtime;
  return runtime_data->canvas_mesh;
}

static Mesh *dynamicPaint_brush_mesh_get(DynamicPaintBrushSettings *brush)
{
  if (brush->pmd->modifier.runtime == NULL) {
    return NULL;
  }
  DynamicPaintRuntime *runtime_data = (DynamicPaintRuntime *)brush->pmd->modifier.runtime;
  return runtime_data->brush_mesh;
}

/***************************** General Utils ******************************/

/* Set canvas error string to display at the bake report */
static bool setError(DynamicPaintCanvasSettings *canvas, const char *string)
{
  /* Add error to canvas ui info label */
  BLI_strncpy(canvas->error, string, sizeof(canvas->error));
  CLOG_STR_ERROR(&LOG, string);
  return false;
}

/* Get number of surface points for cached types */
static int dynamicPaint_surfaceNumOfPoints(DynamicPaintSurface *surface)
{
  if (surface->format == MOD_DPAINT_SURFACE_F_PTEX) {
    return 0; /* Not supported at the moment. */
  }
  if (surface->format == MOD_DPAINT_SURFACE_F_VERTEX) {
    const Mesh *canvas_mesh = dynamicPaint_canvas_mesh_get(surface->canvas);
    return (canvas_mesh) ? canvas_mesh->totvert : 0;
  }

  return 0;
}

DynamicPaintSurface *get_activeSurface(DynamicPaintCanvasSettings *canvas)
{
  return BLI_findlink(&canvas->surfaces, canvas->active_sur);
}

bool dynamicPaint_outputLayerExists(struct DynamicPaintSurface *surface, Object *ob, int output)
{
  const char *name;

  if (output == 0) {
    name = surface->output_name;
  }
  else if (output == 1) {
    name = surface->output_name2;
  }
  else {
    return false;
  }

  if (surface->format == MOD_DPAINT_SURFACE_F_VERTEX) {
    if (surface->type == MOD_DPAINT_SURFACE_T_PAINT) {
      Mesh *me = ob->data;
      return (CustomData_get_named_layer_index(&me->ldata, CD_MLOOPCOL, name) != -1);
    }
    if (surface->type == MOD_DPAINT_SURFACE_T_WEIGHT) {
      return (BKE_object_defgroup_name_index(ob, name) != -1);
    }
  }

  return false;
}

static bool surface_duplicateOutputExists(void *arg, const char *name)
{
  DynamicPaintSurface *t_surface = arg;
  DynamicPaintSurface *surface = t_surface->canvas->surfaces.first;

  for (; surface; surface = surface->next) {
    if (surface != t_surface && surface->type == t_surface->type &&
        surface->format == t_surface->format) {
      if ((surface->output_name[0] != '\0' && !BLI_path_cmp(name, surface->output_name)) ||
          (surface->output_name2[0] != '\0' && !BLI_path_cmp(name, surface->output_name2))) {
        return true;
      }
    }
  }
  return false;
}

static void surface_setUniqueOutputName(DynamicPaintSurface *surface, char *basename, int output)
{
  char name[64];
  BLI_strncpy(name, basename, sizeof(name)); /* in case basename is surface->name use a copy */
  if (output == 0) {
    BLI_uniquename_cb(surface_duplicateOutputExists,
                      surface,
                      name,
                      '.',
                      surface->output_name,
                      sizeof(surface->output_name));
  }
  else if (output == 1) {
    BLI_uniquename_cb(surface_duplicateOutputExists,
                      surface,
                      name,
                      '.',
                      surface->output_name2,
                      sizeof(surface->output_name2));
  }
}

static bool surface_duplicateNameExists(void *arg, const char *name)
{
  DynamicPaintSurface *t_surface = arg;
  DynamicPaintSurface *surface = t_surface->canvas->surfaces.first;

  for (; surface; surface = surface->next) {
    if (surface != t_surface && STREQ(name, surface->name)) {
      return true;
    }
  }
  return false;
}

void dynamicPaintSurface_setUniqueName(DynamicPaintSurface *surface, const char *basename)
{
  char name[64];
  BLI_strncpy(name, basename, sizeof(name)); /* in case basename is surface->name use a copy */
  BLI_uniquename_cb(
      surface_duplicateNameExists, surface, name, '.', surface->name, sizeof(surface->name));
}

void dynamicPaintSurface_updateType(struct DynamicPaintSurface *surface)
{
  if (surface->format == MOD_DPAINT_SURFACE_F_IMAGESEQ) {
    surface->output_name[0] = '\0';
    surface->output_name2[0] = '\0';
    surface->flags |= MOD_DPAINT_ANTIALIAS;
    surface->depth_clamp = 1.0f;
  }
  else {
    strcpy(surface->output_name, "dp_");
    BLI_strncpy(surface->output_name2, surface->output_name, sizeof(surface->output_name2));
    surface->flags &= ~MOD_DPAINT_ANTIALIAS;
    surface->depth_clamp = 0.0f;
  }

  if (surface->type == MOD_DPAINT_SURFACE_T_PAINT) {
    strcat(surface->output_name, "paintmap");
    strcat(surface->output_name2, "wetmap");
    surface_setUniqueOutputName(surface, surface->output_name2, 1);
  }
  else if (surface->type == MOD_DPAINT_SURFACE_T_DISPLACE) {
    strcat(surface->output_name, "displace");
  }
  else if (surface->type == MOD_DPAINT_SURFACE_T_WEIGHT) {
    strcat(surface->output_name, "weight");
  }
  else if (surface->type == MOD_DPAINT_SURFACE_T_WAVE) {
    strcat(surface->output_name, "wave");
  }

  surface_setUniqueOutputName(surface, surface->output_name, 0);
}

static int surface_totalSamples(DynamicPaintSurface *surface)
{
  if (surface->format == MOD_DPAINT_SURFACE_F_IMAGESEQ && surface->flags & MOD_DPAINT_ANTIALIAS) {
    return (surface->data->total_points * 5);
  }
  if (surface->format == MOD_DPAINT_SURFACE_F_VERTEX && surface->flags & MOD_DPAINT_ANTIALIAS &&
      surface->data->adj_data) {
    return (surface->data->total_points + surface->data->adj_data->total_targets);
  }

  return surface->data->total_points;
}

static void blendColors(const float t_color[3],
                        const float t_alpha,
                        const float s_color[3],
                        const float s_alpha,
                        float result[4])
{
  /* Same thing as BLI's blend_color_mix_float(), but for non-premultiplied alpha. */
  float i_alpha = 1.0f - s_alpha;
  float f_alpha = t_alpha * i_alpha + s_alpha;

  /* blend colors */
  if (f_alpha) {
    for (int i = 0; i < 3; i++) {
      result[i] = (t_color[i] * t_alpha * i_alpha + s_color[i] * s_alpha) / f_alpha;
    }
  }
  else {
    copy_v3_v3(result, t_color);
  }
  /* return final alpha */
  result[3] = f_alpha;
}

/* Mix two alpha weighed colors by a defined ratio. output is saved at a_color */
static float mixColors(
    float a_color[3], float a_weight, const float b_color[3], float b_weight, float ratio)
{
  float weight_ratio, factor;
  if (b_weight) {
    /* if first value has no weight just use b_color */
    if (!a_weight) {
      copy_v3_v3(a_color, b_color);
      return b_weight * ratio;
    }
    weight_ratio = b_weight / (a_weight + b_weight);
  }
  else {
    return a_weight * (1.0f - ratio);
  }

  /* calculate final interpolation factor */
  if (ratio <= 0.5f) {
    factor = weight_ratio * (ratio * 2.0f);
  }
  else {
    ratio = (ratio * 2.0f - 1.0f);
    factor = weight_ratio * (1.0f - ratio) + ratio;
  }
  /* mix final color */
  interp_v3_v3v3(a_color, a_color, b_color, factor);
  return (1.0f - factor) * a_weight + factor * b_weight;
}

static void scene_setSubframe(Scene *scene, float subframe)
{
  /* dynamic paint subframes must be done on previous frame */
  scene->r.cfra -= 1;
  scene->r.subframe = subframe;
}

static int surface_getBrushFlags(DynamicPaintSurface *surface, Depsgraph *depsgraph)
{
  unsigned int numobjects;
  Object **objects = BKE_collision_objects_create(
      depsgraph, NULL, surface->brush_group, &numobjects, eModifierType_DynamicPaint);

  int flags = 0;

  for (int i = 0; i < numobjects; i++) {
    Object *brushObj = objects[i];

    ModifierData *md = BKE_modifiers_findby_type(brushObj, eModifierType_DynamicPaint);
    if (md && md->mode & (eModifierMode_Realtime | eModifierMode_Render)) {
      DynamicPaintModifierData *pmd2 = (DynamicPaintModifierData *)md;

      if (pmd2->brush) {
        DynamicPaintBrushSettings *brush = pmd2->brush;

        if (brush->flags & MOD_DPAINT_USES_VELOCITY) {
          flags |= BRUSH_USES_VELOCITY;
        }
      }
    }
  }

  BKE_collision_objects_free(objects);

  return flags;
}

/* check whether two bounds intersect */
static bool boundsIntersect(Bounds3D *b1, Bounds3D *b2)
{
  if (!b1->valid || !b2->valid) {
    return false;
  }
  for (int i = 2; i--;) {
    if (!(b1->min[i] <= b2->max[i] && b1->max[i] >= b2->min[i])) {
      return false;
    }
  }
  return true;
}

/* check whether two bounds intersect inside defined proximity */
static bool boundsIntersectDist(Bounds3D *b1, Bounds3D *b2, const float dist)
{
  if (!b1->valid || !b2->valid) {
    return false;
  }
  for (int i = 2; i--;) {
    if (!(b1->min[i] <= (b2->max[i] + dist) && b1->max[i] >= (b2->min[i] - dist))) {
      return false;
    }
  }
  return true;
}

/* check whether bounds intersects a point with given radius */
static bool boundIntersectPoint(Bounds3D *b, const float point[3], const float radius)
{
  if (!b->valid) {
    return false;
  }
  for (int i = 2; i--;) {
    if (!(b->min[i] <= (point[i] + radius) && b->max[i] >= (point[i] - radius))) {
      return false;
    }
  }
  return true;
}

/* expand bounds by a new point */
static void boundInsert(Bounds3D *b, const float point[3])
{
  if (!b->valid) {
    copy_v3_v3(b->min, point);
    copy_v3_v3(b->max, point);
    b->valid = true;
    return;
  }

  minmax_v3v3_v3(b->min, b->max, point);
}

static float getSurfaceDimension(PaintSurfaceData *sData)
{
  Bounds3D *mb = &sData->bData->mesh_bounds;
  return max_fff((mb->max[0] - mb->min[0]), (mb->max[1] - mb->min[1]), (mb->max[2] - mb->min[2]));
}

static void freeGrid(PaintSurfaceData *data)
{
  PaintBakeData *bData = data->bData;
  VolumeGrid *grid = bData->grid;

  if (grid->bounds) {
    MEM_freeN(grid->bounds);
  }
  if (grid->s_pos) {
    MEM_freeN(grid->s_pos);
  }
  if (grid->s_num) {
    MEM_freeN(grid->s_num);
  }
  if (grid->t_index) {
    MEM_freeN(grid->t_index);
  }

  MEM_freeN(bData->grid);
  bData->grid = NULL;
}

static void grid_bound_insert_cb_ex(void *__restrict userdata,
                                    const int i,
                                    const TaskParallelTLS *__restrict tls)
{
  PaintBakeData *bData = userdata;

  Bounds3D *grid_bound = tls->userdata_chunk;

  boundInsert(grid_bound, bData->realCoord[bData->s_pos[i]].v);
}

static void grid_bound_insert_reduce(const void *__restrict UNUSED(userdata),
                                     void *__restrict chunk_join,
                                     void *__restrict chunk)
{
  Bounds3D *join = chunk_join;
  Bounds3D *grid_bound = chunk;

  boundInsert(join, grid_bound->min);
  boundInsert(join, grid_bound->max);
}

static void grid_cell_points_cb_ex(void *__restrict userdata,
                                   const int i,
                                   const TaskParallelTLS *__restrict tls)
{
  PaintBakeData *bData = userdata;
  VolumeGrid *grid = bData->grid;
  int *temp_t_index = grid->temp_t_index;
  int *s_num = tls->userdata_chunk;

  int co[3];

  for (int j = 3; j--;) {
    co[j] = (int)floorf((bData->realCoord[bData->s_pos[i]].v[j] - grid->grid_bounds.min[j]) /
                        bData->dim[j] * grid->dim[j]);
    CLAMP(co[j], 0, grid->dim[j] - 1);
  }

  temp_t_index[i] = co[0] + co[1] * grid->dim[0] + co[2] * grid->dim[0] * grid->dim[1];
  s_num[temp_t_index[i]]++;
}

static void grid_cell_points_reduce(const void *__restrict userdata,
                                    void *__restrict chunk_join,
                                    void *__restrict chunk)
{
  const PaintBakeData *bData = userdata;
  const VolumeGrid *grid = bData->grid;
  const int grid_cells = grid->dim[0] * grid->dim[1] * grid->dim[2];

  int *join_s_num = chunk_join;
  int *s_num = chunk;

  /* calculate grid indexes */
  for (int i = 0; i < grid_cells; i++) {
    join_s_num[i] += s_num[i];
  }
}

static void grid_cell_bounds_cb(void *__restrict userdata,
                                const int x,
                                const TaskParallelTLS *__restrict UNUSED(tls))
{
  PaintBakeData *bData = userdata;
  VolumeGrid *grid = bData->grid;
  float *dim = bData->dim;
  int *grid_dim = grid->dim;

  for (int y = 0; y < grid_dim[1]; y++) {
    for (int z = 0; z < grid_dim[2]; z++) {
      const int b_index = x + y * grid_dim[0] + z * grid_dim[0] * grid_dim[1];
      /* set bounds */
      for (int j = 3; j--;) {
        const int s = (j == 0) ? x : ((j == 1) ? y : z);
        grid->bounds[b_index].min[j] = grid->grid_bounds.min[j] + dim[j] / grid_dim[j] * s;
        grid->bounds[b_index].max[j] = grid->grid_bounds.min[j] + dim[j] / grid_dim[j] * (s + 1);
      }
      grid->bounds[b_index].valid = true;
    }
  }
}

static void surfaceGenerateGrid(struct DynamicPaintSurface *surface)
{
  PaintSurfaceData *sData = surface->data;
  PaintBakeData *bData = sData->bData;
  VolumeGrid *grid;
  int grid_cells, axis = 3;
  int *temp_t_index = NULL;
  int *temp_s_num = NULL;

  if (bData->grid) {
    freeGrid(sData);
  }

  bData->grid = MEM_callocN(sizeof(VolumeGrid), "Surface Grid");
  grid = bData->grid;

  {
    int i, error = 0;
    float dim_factor, volume, dim[3];
    float td[3];
    float min_dim;

    /* calculate canvas dimensions */
    /* Important to init correctly our ref grid_bound... */
    boundInsert(&grid->grid_bounds, bData->realCoord[bData->s_pos[0]].v);
    {
      TaskParallelSettings settings;
      BLI_parallel_range_settings_defaults(&settings);
      settings.use_threading = (sData->total_points > 1000);
      settings.userdata_chunk = &grid->grid_bounds;
      settings.userdata_chunk_size = sizeof(grid->grid_bounds);
      settings.func_reduce = grid_bound_insert_reduce;
      BLI_task_parallel_range(0, sData->total_points, bData, grid_bound_insert_cb_ex, &settings);
    }
    /* get dimensions */
    sub_v3_v3v3(dim, grid->grid_bounds.max, grid->grid_bounds.min);
    copy_v3_v3(td, dim);
    copy_v3_v3(bData->dim, dim);
    min_dim = max_fff(td[0], td[1], td[2]) / 1000.0f;

    /* deactivate zero axes */
    for (i = 0; i < 3; i++) {
      if (td[i] < min_dim) {
        td[i] = 1.0f;
        axis--;
      }
    }

    if (axis == 0 || max_fff(td[0], td[1], td[2]) < 0.0001f) {
      MEM_freeN(bData->grid);
      bData->grid = NULL;
      return;
    }

    /* now calculate grid volume/area/width depending on num of active axis */
    volume = td[0] * td[1] * td[2];

    /* determine final grid size by trying to fit average 10.000 points per grid cell */
    dim_factor = (float)pow((double)volume / ((double)sData->total_points / 10000.0),
                            1.0 / (double)axis);

    /* define final grid size using dim_factor, use min 3 for active axes */
    for (i = 0; i < 3; i++) {
      grid->dim[i] = (int)floor(td[i] / dim_factor);
      CLAMP(grid->dim[i], (dim[i] >= min_dim) ? 3 : 1, 100);
    }
    grid_cells = grid->dim[0] * grid->dim[1] * grid->dim[2];

    /* allocate memory for grids */
    grid->bounds = MEM_callocN(sizeof(Bounds3D) * grid_cells, "Surface Grid Bounds");
    grid->s_pos = MEM_callocN(sizeof(int) * grid_cells, "Surface Grid Position");

    grid->s_num = MEM_callocN(sizeof(int) * grid_cells, "Surface Grid Points");
    temp_s_num = MEM_callocN(sizeof(int) * grid_cells, "Temp Surface Grid Points");
    grid->t_index = MEM_callocN(sizeof(int) * sData->total_points, "Surface Grid Target Ids");
    grid->temp_t_index = temp_t_index = MEM_callocN(sizeof(int) * sData->total_points,
                                                    "Temp Surface Grid Target Ids");

    /* in case of an allocation failure abort here */
    if (!grid->bounds || !grid->s_pos || !grid->s_num || !grid->t_index || !temp_s_num ||
        !temp_t_index) {
      error = 1;
    }

    if (!error) {
      /* calculate number of points within each cell */
      {
        TaskParallelSettings settings;
        BLI_parallel_range_settings_defaults(&settings);
        settings.use_threading = (sData->total_points > 1000);
        settings.userdata_chunk = grid->s_num;
        settings.userdata_chunk_size = sizeof(*grid->s_num) * grid_cells;
        settings.func_reduce = grid_cell_points_reduce;
        BLI_task_parallel_range(0, sData->total_points, bData, grid_cell_points_cb_ex, &settings);
      }

      /* calculate grid indexes (not needed for first cell, which is zero). */
      for (i = 1; i < grid_cells; i++) {
        grid->s_pos[i] = grid->s_pos[i - 1] + grid->s_num[i - 1];
      }

      /* save point indexes to final array */
      for (i = 0; i < sData->total_points; i++) {
        int pos = grid->s_pos[temp_t_index[i]] + temp_s_num[temp_t_index[i]];
        grid->t_index[pos] = i;

        temp_s_num[temp_t_index[i]]++;
      }

      /* calculate cell bounds */
      {
        TaskParallelSettings settings;
        BLI_parallel_range_settings_defaults(&settings);
        settings.use_threading = (grid_cells > 1000);
        BLI_task_parallel_range(0, grid->dim[0], bData, grid_cell_bounds_cb, &settings);
      }
    }

    if (temp_s_num) {
      MEM_freeN(temp_s_num);
    }
    MEM_SAFE_FREE(temp_t_index);

    if (error || !grid->s_num) {
      setError(surface->canvas, N_("Not enough free memory"));
      freeGrid(sData);
    }
  }
}

/***************************** Freeing data ******************************/

void dynamicPaint_freeBrush(struct DynamicPaintModifierData *pmd)
{
  if (pmd->brush) {
    if (pmd->brush->paint_ramp) {
      MEM_freeN(pmd->brush->paint_ramp);
    }
    if (pmd->brush->vel_ramp) {
      MEM_freeN(pmd->brush->vel_ramp);
    }

    MEM_freeN(pmd->brush);
    pmd->brush = NULL;
  }
}

static void dynamicPaint_freeAdjData(PaintSurfaceData *data)
{
  if (data->adj_data) {
    if (data->adj_data->n_index) {
      MEM_freeN(data->adj_data->n_index);
    }
    if (data->adj_data->n_num) {
      MEM_freeN(data->adj_data->n_num);
    }
    if (data->adj_data->n_target) {
      MEM_freeN(data->adj_data->n_target);
    }
    if (data->adj_data->flags) {
      MEM_freeN(data->adj_data->flags);
    }
    if (data->adj_data->border) {
      MEM_freeN(data->adj_data->border);
    }
    MEM_freeN(data->adj_data);
    data->adj_data = NULL;
  }
}

static void free_bakeData(PaintSurfaceData *data)
{
  PaintBakeData *bData = data->bData;
  if (bData) {
    if (bData->bNormal) {
      MEM_freeN(bData->bNormal);
    }
    if (bData->s_pos) {
      MEM_freeN(bData->s_pos);
    }
    if (bData->s_num) {
      MEM_freeN(bData->s_num);
    }
    if (bData->realCoord) {
      MEM_freeN(bData->realCoord);
    }
    if (bData->bNeighs) {
      MEM_freeN(bData->bNeighs);
    }
    if (bData->grid) {
      freeGrid(data);
    }
    if (bData->prev_verts) {
      MEM_freeN(bData->prev_verts);
    }
    if (bData->velocity) {
      MEM_freeN(bData->velocity);
    }
    if (bData->prev_velocity) {
      MEM_freeN(bData->prev_velocity);
    }

    MEM_freeN(data->bData);
    data->bData = NULL;
  }
}

/* free surface data if it's not used anymore */
static void surface_freeUnusedData(DynamicPaintSurface *surface)
{
  if (!surface->data) {
    return;
  }

  /* free bakedata if not active or surface is baked */
  if (!(surface->flags & MOD_DPAINT_ACTIVE) ||
      (surface->pointcache && surface->pointcache->flag & PTCACHE_BAKED)) {
    free_bakeData(surface->data);
  }
}

void dynamicPaint_freeSurfaceData(DynamicPaintSurface *surface)
{
  PaintSurfaceData *data = surface->data;
  if (!data) {
    return;
  }

  if (data->format_data) {
    /* format specific free */
    if (surface->format == MOD_DPAINT_SURFACE_F_IMAGESEQ) {
      ImgSeqFormatData *format_data = (ImgSeqFormatData *)data->format_data;
      if (format_data->uv_p) {
        MEM_freeN(format_data->uv_p);
      }
      if (format_data->barycentricWeights) {
        MEM_freeN(format_data->barycentricWeights);
      }
    }
    MEM_freeN(data->format_data);
  }
  /* type data */
  if (data->type_data) {
    MEM_freeN(data->type_data);
  }
  dynamicPaint_freeAdjData(data);
  /* bake data */
  free_bakeData(data);

  MEM_freeN(surface->data);
  surface->data = NULL;
}
