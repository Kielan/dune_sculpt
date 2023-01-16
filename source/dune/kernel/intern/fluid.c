#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"

#include "BLI_fileops.h"
#include "BLI_hash.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "DNA_defaults.h"
#include "DNA_fluid_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_rigidbody_types.h"

#include "BKE_attribute.h"
#include "BKE_effect.h"
#include "BKE_fluid.h"
#include "BKE_global.h"
#include "BKE_lib_id.h"
#include "BKE_modifier.h"
#include "BKE_pointcache.h"

#ifdef WITH_FLUID

#  include <float.h>
#  include <math.h>
#  include <stdio.h>
#  include <string.h> /* memset */

#  include "DNA_customdata_types.h"
#  include "DNA_light_types.h"
#  include "DNA_mesh_types.h"
#  include "DNA_meshdata_types.h"
#  include "DNA_particle_types.h"
#  include "DNA_scene_types.h"

#  include "BLI_kdopbvh.h"
#  include "BLI_kdtree.h"
#  include "BLI_threads.h"
#  include "BLI_voxel.h"

#  include "BKE_bvhutils.h"
#  include "BKE_collision.h"
#  include "BKE_colortools.h"
#  include "BKE_customdata.h"
#  include "BKE_deform.h"
#  include "BKE_mesh.h"
#  include "BKE_mesh_runtime.h"
#  include "BKE_object.h"
#  include "BKE_particle.h"
#  include "BKE_scene.h"
#  include "BKE_texture.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_query.h"

#  include "RE_texture.h"

#  include "CLG_log.h"

#  include "manta_fluid_API.h"

#endif /* WITH_FLUID */

/** Time step default value for nice appearance. */
#define DT_DEFAULT 0.1f

/** Max value for phi initialization */
#define PHI_MAX 9999.0f

static void BKE_fluid_modifier_reset_ex(struct FluidModifierData *fmd, bool need_lock);

#ifdef WITH_FLUID
// #define DEBUG_PRINT

static CLG_LogRef LOG = {"bke.fluid"};

/* -------------------------------------------------------------------- */
/** \name Fluid API
 * \{ */

static ThreadMutex object_update_lock = BLI_MUTEX_INITIALIZER;

struct FluidModifierData;
struct Mesh;
struct Object;
struct Scene;

#  define ADD_IF_LOWER_POS(a, b) (min_ff((a) + (b), max_ff((a), (b))))
#  define ADD_IF_LOWER_NEG(a, b) (max_ff((a) + (b), min_ff((a), (b))))
#  define ADD_IF_LOWER(a, b) (((b) > 0) ? ADD_IF_LOWER_POS((a), (b)) : ADD_IF_LOWER_NEG((a), (b)))

bool BKE_fluid_reallocate_fluid(FluidDomainSettings *fds, int res[3], int free_old)
{
  if (free_old && fds->fluid) {
    manta_free(fds->fluid);
  }
  if (!min_iii(res[0], res[1], res[2])) {
    fds->fluid = NULL;
  }
  else {
    fds->fluid = manta_init(res, fds->fmd);

    fds->res_noise[0] = res[0] * fds->noise_scale;
    fds->res_noise[1] = res[1] * fds->noise_scale;
    fds->res_noise[2] = res[2] * fds->noise_scale;
  }

  return (fds->fluid != NULL);
}

void BKE_fluid_reallocate_copy_fluid(FluidDomainSettings *fds,
                                     int o_res[3],
                                     int n_res[3],
                                     const int o_min[3],
                                     const int n_min[3],
                                     const int o_max[3],
                                     int o_shift[3],
                                     int n_shift[3])
{
  struct MANTA *fluid_old = fds->fluid;
  const int block_size = fds->noise_scale;
  int new_shift[3] = {0};
  sub_v3_v3v3_int(new_shift, n_shift, o_shift);

  /* Allocate new fluid data. */
  BKE_fluid_reallocate_fluid(fds, n_res, 0);

  int o_total_cells = o_res[0] * o_res[1] * o_res[2];
  int n_total_cells = n_res[0] * n_res[1] * n_res[2];

  /* Copy values from old fluid to new fluid object. */
  if (o_total_cells > 1 && n_total_cells > 1) {
    float *o_dens = manta_smoke_get_density(fluid_old);
    float *o_react = manta_smoke_get_react(fluid_old);
    float *o_flame = manta_smoke_get_flame(fluid_old);
    float *o_fuel = manta_smoke_get_fuel(fluid_old);
    float *o_heat = manta_smoke_get_heat(fluid_old);
    float *o_vx = manta_get_velocity_x(fluid_old);
    float *o_vy = manta_get_velocity_y(fluid_old);
    float *o_vz = manta_get_velocity_z(fluid_old);
    float *o_r = manta_smoke_get_color_r(fluid_old);
    float *o_g = manta_smoke_get_color_g(fluid_old);
    float *o_b = manta_smoke_get_color_b(fluid_old);

    float *n_dens = manta_smoke_get_density(fds->fluid);
    float *n_react = manta_smoke_get_react(fds->fluid);
    float *n_flame = manta_smoke_get_flame(fds->fluid);
    float *n_fuel = manta_smoke_get_fuel(fds->fluid);
    float *n_heat = manta_smoke_get_heat(fds->fluid);
    float *n_vx = manta_get_velocity_x(fds->fluid);
    float *n_vy = manta_get_velocity_y(fds->fluid);
    float *n_vz = manta_get_velocity_z(fds->fluid);
    float *n_r = manta_smoke_get_color_r(fds->fluid);
    float *n_g = manta_smoke_get_color_g(fds->fluid);
    float *n_b = manta_smoke_get_color_b(fds->fluid);

    /* Noise smoke fields. */
    float *o_wt_dens = manta_noise_get_density(fluid_old);
    float *o_wt_react = manta_noise_get_react(fluid_old);
    float *o_wt_flame = manta_noise_get_flame(fluid_old);
    float *o_wt_fuel = manta_noise_get_fuel(fluid_old);
    float *o_wt_r = manta_noise_get_color_r(fluid_old);
    float *o_wt_g = manta_noise_get_color_g(fluid_old);
    float *o_wt_b = manta_noise_get_color_b(fluid_old);
    float *o_wt_tcu = manta_noise_get_texture_u(fluid_old);
    float *o_wt_tcv = manta_noise_get_texture_v(fluid_old);
    float *o_wt_tcw = manta_noise_get_texture_w(fluid_old);
    float *o_wt_tcu2 = manta_noise_get_texture_u2(fluid_old);
    float *o_wt_tcv2 = manta_noise_get_texture_v2(fluid_old);
    float *o_wt_tcw2 = manta_noise_get_texture_w2(fluid_old);

    float *n_wt_dens = manta_noise_get_density(fds->fluid);
    float *n_wt_react = manta_noise_get_react(fds->fluid);
    float *n_wt_flame = manta_noise_get_flame(fds->fluid);
    float *n_wt_fuel = manta_noise_get_fuel(fds->fluid);
    float *n_wt_r = manta_noise_get_color_r(fds->fluid);
    float *n_wt_g = manta_noise_get_color_g(fds->fluid);
    float *n_wt_b = manta_noise_get_color_b(fds->fluid);
    float *n_wt_tcu = manta_noise_get_texture_u(fds->fluid);
    float *n_wt_tcv = manta_noise_get_texture_v(fds->fluid);
    float *n_wt_tcw = manta_noise_get_texture_w(fds->fluid);
    float *n_wt_tcu2 = manta_noise_get_texture_u2(fds->fluid);
    float *n_wt_tcv2 = manta_noise_get_texture_v2(fds->fluid);
    float *n_wt_tcw2 = manta_noise_get_texture_w2(fds->fluid);

    int wt_res_old[3];
    manta_noise_get_res(fluid_old, wt_res_old);

    for (int z = o_min[2]; z < o_max[2]; z++) {
      for (int y = o_min[1]; y < o_max[1]; y++) {
        for (int x = o_min[0]; x < o_max[0]; x++) {
          /* old grid index */
          int xo = x - o_min[0];
          int yo = y - o_min[1];
          int zo = z - o_min[2];
          int index_old = manta_get_index(xo, o_res[0], yo, o_res[1], zo);
          /* new grid index */
          int xn = x - n_min[0] - new_shift[0];
          int yn = y - n_min[1] - new_shift[1];
          int zn = z - n_min[2] - new_shift[2];
          int index_new = manta_get_index(xn, n_res[0], yn, n_res[1], zn);

          /* Skip if outside new domain. */
          if (xn < 0 || xn >= n_res[0] || yn < 0 || yn >= n_res[1] || zn < 0 || zn >= n_res[2]) {
            continue;
          }
#  if 0
          /* Note (sebbas):
           * Disabling this "skip section" as not copying borders results in weird cut-off effects.
           * It is possible that this cutting off is the reason for line effects as seen in T74559.
           * Since domain borders will be handled on the simulation side anyways,
           * copying border values should not be an issue. */

          /* boundary cells will be skipped when copying data */
          int bwidth = fds->boundary_width;

          /* Skip if trying to copy from old boundary cell. */
          if (xo < bwidth || yo < bwidth || zo < bwidth || xo >= o_res[0] - bwidth ||
              yo >= o_res[1] - bwidth || zo >= o_res[2] - bwidth) {
            continue;
          }
          /* Skip if trying to copy into new boundary cell. */
          if (xn < bwidth || yn < bwidth || zn < bwidth || xn >= n_res[0] - bwidth ||
              yn >= n_res[1] - bwidth || zn >= n_res[2] - bwidth) {
            continue;
          }
#  endif

          /* copy data */
          if (fds->flags & FLUID_DOMAIN_USE_NOISE) {
            int i, j, k;
            /* old grid index */
            int xx_o = xo * block_size;
            int yy_o = yo * block_size;
            int zz_o = zo * block_size;
            /* new grid index */
            int xx_n = xn * block_size;
            int yy_n = yn * block_size;
            int zz_n = zn * block_size;

            /* insert old texture values into new texture grids */
            n_wt_tcu[index_new] = o_wt_tcu[index_old];
            n_wt_tcv[index_new] = o_wt_tcv[index_old];
            n_wt_tcw[index_new] = o_wt_tcw[index_old];

            n_wt_tcu2[index_new] = o_wt_tcu2[index_old];
            n_wt_tcv2[index_new] = o_wt_tcv2[index_old];
            n_wt_tcw2[index_new] = o_wt_tcw2[index_old];

            for (i = 0; i < block_size; i++) {
              for (j = 0; j < block_size; j++) {
                for (k = 0; k < block_size; k++) {
                  int big_index_old = manta_get_index(
                      xx_o + i, wt_res_old[0], yy_o + j, wt_res_old[1], zz_o + k);
                  int big_index_new = manta_get_index(
                      xx_n + i, fds->res_noise[0], yy_n + j, fds->res_noise[1], zz_n + k);
                  /* copy data */
                  n_wt_dens[big_index_new] = o_wt_dens[big_index_old];
                  if (n_wt_flame && o_wt_flame) {
                    n_wt_flame[big_index_new] = o_wt_flame[big_index_old];
                    n_wt_fuel[big_index_new] = o_wt_fuel[big_index_old];
                    n_wt_react[big_index_new] = o_wt_react[big_index_old];
                  }
                  if (n_wt_r && o_wt_r) {
                    n_wt_r[big_index_new] = o_wt_r[big_index_old];
                    n_wt_g[big_index_new] = o_wt_g[big_index_old];
                    n_wt_b[big_index_new] = o_wt_b[big_index_old];
                  }
                }
              }
            }
          }

          n_dens[index_new] = o_dens[index_old];
          /* heat */
          if (n_heat && o_heat) {
            n_heat[index_new] = o_heat[index_old];
          }
          /* fuel */
          if (n_fuel && o_fuel) {
            n_flame[index_new] = o_flame[index_old];
            n_fuel[index_new] = o_fuel[index_old];
            n_react[index_new] = o_react[index_old];
          }
          /* color */
          if (o_r && n_r) {
            n_r[index_new] = o_r[index_old];
            n_g[index_new] = o_g[index_old];
            n_b[index_new] = o_b[index_old];
          }
          n_vx[index_new] = o_vx[index_old];
          n_vy[index_new] = o_vy[index_old];
          n_vz[index_new] = o_vz[index_old];
        }
      }
    }
  }
  manta_free(fluid_old);
}

void BKE_fluid_cache_free_all(FluidDomainSettings *fds, Object *ob)
{
  int cache_map = (FLUID_DOMAIN_OUTDATED_DATA | FLUID_DOMAIN_OUTDATED_NOISE |
                   FLUID_DOMAIN_OUTDATED_MESH | FLUID_DOMAIN_OUTDATED_PARTICLES |
                   FLUID_DOMAIN_OUTDATED_GUIDE);
  BKE_fluid_cache_free(fds, ob, cache_map);
}

void BKE_fluid_cache_free(FluidDomainSettings *fds, Object *ob, int cache_map)
{
  char temp_dir[FILE_MAX];
  int flags = fds->cache_flag;
  const char *relbase = BKE_modifier_path_relbase_from_global(ob);

  if (cache_map & FLUID_DOMAIN_OUTDATED_DATA) {
    flags &= ~(FLUID_DOMAIN_BAKING_DATA | FLUID_DOMAIN_BAKED_DATA | FLUID_DOMAIN_OUTDATED_DATA);
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_CONFIG, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_DATA, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_SCRIPT, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    fds->cache_frame_pause_data = 0;
  }
  if (cache_map & FLUID_DOMAIN_OUTDATED_NOISE) {
    flags &= ~(FLUID_DOMAIN_BAKING_NOISE | FLUID_DOMAIN_BAKED_NOISE | FLUID_DOMAIN_OUTDATED_NOISE);
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_NOISE, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    fds->cache_frame_pause_noise = 0;
  }
  if (cache_map & FLUID_DOMAIN_OUTDATED_MESH) {
    flags &= ~(FLUID_DOMAIN_BAKING_MESH | FLUID_DOMAIN_BAKED_MESH | FLUID_DOMAIN_OUTDATED_MESH);
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_MESH, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    fds->cache_frame_pause_mesh = 0;
  }
  if (cache_map & FLUID_DOMAIN_OUTDATED_PARTICLES) {
    flags &= ~(FLUID_DOMAIN_BAKING_PARTICLES | FLUID_DOMAIN_BAKED_PARTICLES |
               FLUID_DOMAIN_OUTDATED_PARTICLES);
    BLI_path_join(
        temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_PARTICLES, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    fds->cache_frame_pause_particles = 0;
  }
  if (cache_map & FLUID_DOMAIN_OUTDATED_GUIDE) {
    flags &= ~(FLUID_DOMAIN_BAKING_GUIDE | FLUID_DOMAIN_BAKED_GUIDE | FLUID_DOMAIN_OUTDATED_GUIDE);
    BLI_path_join(temp_dir, sizeof(temp_dir), fds->cache_directory, FLUID_DOMAIN_DIR_GUIDE, NULL);
    BLI_path_abs(temp_dir, relbase);
    if (BLI_exists(temp_dir)) {
      BLI_delete(temp_dir, true, true);
    }
    fds->cache_frame_pause_guide = 0;
  }
  fds->cache_flag = flags;
}

/* convert global position to domain cell space */
static void manta_pos_to_cell(FluidDomainSettings *fds, float pos[3])
{
  mul_m4_v3(fds->imat, pos);
  sub_v3_v3(pos, fds->p0);
  pos[0] *= 1.0f / fds->cell_size[0];
  pos[1] *= 1.0f / fds->cell_size[1];
  pos[2] *= 1.0f / fds->cell_size[2];
}

/* Set domain transformations and base resolution from object mesh. */
static void manta_set_domain_from_mesh(FluidDomainSettings *fds,
                                       Object *ob,
                                       Mesh *me,
                                       bool init_resolution)
{
  size_t i;
  float min[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  float size[3];
  MVert *verts = me->mvert;
  float scale = 0.0;
  int res;

  res = fds->maxres;

  /* Set minimum and maximum coordinates of BB. */
  for (i = 0; i < me->totvert; i++) {
    minmax_v3v3_v3(min, max, verts[i].co);
  }

  /* Set domain bounds. */
  copy_v3_v3(fds->p0, min);
  copy_v3_v3(fds->p1, max);
  fds->dx = 1.0f / res;

  /* Calculate domain dimensions. */
  sub_v3_v3v3(size, max, min);
  if (init_resolution) {
    zero_v3_int(fds->base_res);
    copy_v3_v3(fds->cell_size, size);
  }
  /* Apply object scale. */
  for (i = 0; i < 3; i++) {
    size[i] = fabsf(size[i] * ob->scale[i]);
  }
  copy_v3_v3(fds->global_size, size);
  copy_v3_v3(fds->dp0, min);

  invert_m4_m4(fds->imat, ob->obmat);

  /* Prevent crash when initializing a plane as domain. */
  if (!init_resolution || (size[0] < FLT_EPSILON) || (size[1] < FLT_EPSILON) ||
      (size[2] < FLT_EPSILON)) {
    return;
  }

  /* Define grid resolutions from longest domain side. */
  if (size[0] >= MAX2(size[1], size[2])) {
    scale = res / size[0];
    fds->scale = size[0] / fabsf(ob->scale[0]);
    fds->base_res[0] = res;
    fds->base_res[1] = max_ii((int)(size[1] * scale + 0.5f), 4);
    fds->base_res[2] = max_ii((int)(size[2] * scale + 0.5f), 4);
  }
  else if (size[1] >= MAX2(size[0], size[2])) {
    scale = res / size[1];
    fds->scale = size[1] / fabsf(ob->scale[1]);
    fds->base_res[0] = max_ii((int)(size[0] * scale + 0.5f), 4);
    fds->base_res[1] = res;
    fds->base_res[2] = max_ii((int)(size[2] * scale + 0.5f), 4);
  }
  else {
    scale = res / size[2];
    fds->scale = size[2] / fabsf(ob->scale[2]);
    fds->base_res[0] = max_ii((int)(size[0] * scale + 0.5f), 4);
    fds->base_res[1] = max_ii((int)(size[1] * scale + 0.5f), 4);
    fds->base_res[2] = res;
  }

  /* Set cell size. */
  fds->cell_size[0] /= (float)fds->base_res[0];
  fds->cell_size[1] /= (float)fds->base_res[1];
  fds->cell_size[2] /= (float)fds->base_res[2];
}

static void update_final_gravity(FluidDomainSettings *fds, Scene *scene)
{
  if (scene->physics_settings.flag & PHYS_GLOBAL_GRAVITY) {
    copy_v3_v3(fds->gravity_final, scene->physics_settings.gravity);
  }
  else {
    copy_v3_v3(fds->gravity_final, fds->gravity);
  }
  mul_v3_fl(fds->gravity_final, fds->effector_weights->global_gravity);
}

static bool BKE_fluid_modifier_init(
    FluidModifierData *fmd, Depsgraph *depsgraph, Object *ob, Scene *scene, Mesh *me)
{
  int scene_framenr = (int)DEG_get_ctime(depsgraph);

  if ((fmd->type & MOD_FLUID_TYPE_DOMAIN) && fmd->domain && !fmd->domain->fluid) {
    FluidDomainSettings *fds = fmd->domain;
    int res[3];
    /* Set domain dimensions from mesh. */
    manta_set_domain_from_mesh(fds, ob, me, true);
    /* Set domain gravity, use global gravity if enabled. */
    update_final_gravity(fds, scene);
    /* Reset domain values. */
    zero_v3_int(fds->shift);
    zero_v3(fds->shift_f);
    add_v3_fl(fds->shift_f, 0.5f);
    zero_v3(fds->prev_loc);
    mul_m4_v3(ob->obmat, fds->prev_loc);
    copy_m4_m4(fds->obmat, ob->obmat);

    /* Set resolutions. */
    if (fmd->domain->type == FLUID_DOMAIN_TYPE_GAS &&
        fmd->domain->flags & FLUID_DOMAIN_USE_ADAPTIVE_DOMAIN) {
      res[0] = res[1] = res[2] = 1; /* Use minimum res for adaptive init. */
    }
    else {
      copy_v3_v3_int(res, fds->base_res);
    }
    copy_v3_v3_int(fds->res, res);
    fds->total_cells = fds->res[0] * fds->res[1] * fds->res[2];
    fds->res_min[0] = fds->res_min[1] = fds->res_min[2] = 0;
    copy_v3_v3_int(fds->res_max, res);

    /* Set time, frame length = 0.1 is at 25fps. */
    fds->frame_length = DT_DEFAULT * (25.0f / FPS) * fds->time_scale;
    /* Initially dt is equal to frame length (dt can change with adaptive-time stepping though). */
    fds->dt = fds->frame_length;
    fds->time_per_frame = 0;

    fmd->time = scene_framenr;

    /* Allocate fluid. */
    return BKE_fluid_reallocate_fluid(fds, fds->res, 0);
  }
  if (fmd->type & MOD_FLUID_TYPE_FLOW) {
    if (!fmd->flow) {
      BKE_fluid_modifier_create_type_data(fmd);
    }
    fmd->time = scene_framenr;
    return true;
  }
  if (fmd->type & MOD_FLUID_TYPE_EFFEC) {
    if (!fmd->effector) {
      BKE_fluid_modifier_create_type_data(fmd);
    }
    fmd->time = scene_framenr;
    return true;
  }
  return false;
}

/* Forward declarations. */
static void manta_smoke_calc_transparency(FluidDomainSettings *fds, ViewLayer *view_layer);
static float calc_voxel_transp(
    float *result, const float *input, int res[3], int *pixel, float *t_ray, float correct);
static void update_distances(int index,
                             float *distance_map,
                             BVHTreeFromMesh *tree_data,
                             const float ray_start[3],
                             float surface_thickness,
                             bool use_plane_init);

static int get_light(ViewLayer *view_layer, float *light)
{
  Base *base_tmp = NULL;
  int found_light = 0;

  /* Try to find a lamp, preferably local. */
  for (base_tmp = FIRSTBASE(view_layer); base_tmp; base_tmp = base_tmp->next) {
    if (base_tmp->object->type == OB_LAMP) {
      Light *la = base_tmp->object->data;

      if (la->type == LA_LOCAL) {
        copy_v3_v3(light, base_tmp->object->obmat[3]);
        return 1;
      }
      if (!found_light) {
        copy_v3_v3(light, base_tmp->object->obmat[3]);
        found_light = 1;
      }
    }
  }

  return found_light;
}

static void clamp_bounds_in_domain(FluidDomainSettings *fds,
                                   int min[3],
                                   int max[3],
                                   const float *min_vel,
                                   const float *max_vel,
                                   int margin,
                                   float dt)
{
  for (int i = 0; i < 3; i++) {
    int adapt = (fds->flags & FLUID_DOMAIN_USE_ADAPTIVE_DOMAIN) ? fds->adapt_res : 0;
    /* Add some margin. */
    min[i] -= margin;
    max[i] += margin;

    /* Adapt to velocity. */
    if (min_vel && min_vel[i] < 0.0f) {
      min[i] += (int)floor(min_vel[i] * dt);
    }
    if (max_vel && max_vel[i] > 0.0f) {
      max[i] += (int)ceil(max_vel[i] * dt);
    }

    /* Clamp within domain max size. */
    CLAMP(min[i], -adapt, fds->base_res[i] + adapt);
    CLAMP(max[i], -adapt, fds->base_res[i] + adapt);
  }
}

static bool is_static_object(Object *ob)
{
  /* Check if the object has modifiers that might make the object "dynamic". */
  VirtualModifierData virtualModifierData;
  ModifierData *md = BKE_modifiers_get_virtual_modifierlist(ob, &virtualModifierData);
  for (; md; md = md->next) {
    if (ELEM(md->type,
             eModifierType_Cloth,
             eModifierType_DynamicPaint,
             eModifierType_Explode,
             eModifierType_Ocean,
             eModifierType_ShapeKey,
             eModifierType_Softbody,
             eModifierType_Nodes)) {
      return false;
    }
  }

  /* Active rigid body objects considered to be dynamic fluid objects. */
  if (ob->rigidbody_object && ob->rigidbody_object->type == RBO_TYPE_ACTIVE) {
    return false;
  }

  /* Finally, check if the object has animation data. If so, it is considered dynamic. */
  return !BKE_object_moves_in_time(ob, true);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Bounding Box
 * \{ */

typedef struct FluidObjectBB {
  float *influence;
  float *velocity;
  float *distances;
  float *numobjs;
  int min[3], max[3], res[3];
  int hmin[3], hmax[3], hres[3];
  int total_cells, valid;
} FluidObjectBB;

static void bb_boundInsert(FluidObjectBB *bb, const float point[3])
{
  int i = 0;
  if (!bb->valid) {
    for (; i < 3; i++) {
      bb->min[i] = (int)floor(point[i]);
      bb->max[i] = (int)ceil(point[i]);
    }
    bb->valid = 1;
  }
  else {
    for (; i < 3; i++) {
      if (point[i] < bb->min[i]) {
        bb->min[i] = (int)floor(point[i]);
      }
      if (point[i] > bb->max[i]) {
        bb->max[i] = (int)ceil(point[i]);
      }
    }
  }
}

static void bb_allocateData(FluidObjectBB *bb, bool use_velocity, bool use_influence)
{
  int i, res[3];

  for (i = 0; i < 3; i++) {
    res[i] = bb->max[i] - bb->min[i];
    if (res[i] <= 0) {
      return;
    }
  }
  bb->total_cells = res[0] * res[1] * res[2];
  copy_v3_v3_int(bb->res, res);

  bb->numobjs = MEM_calloc_arrayN(bb->total_cells, sizeof(float), "fluid_bb_numobjs");
  if (use_influence) {
    bb->influence = MEM_calloc_arrayN(bb->total_cells, sizeof(float), "fluid_bb_influence");
  }
  if (use_velocity) {
    bb->velocity = MEM_calloc_arrayN(bb->total_cells, sizeof(float[3]), "fluid_bb_velocity");
  }

  bb->distances = MEM_malloc_arrayN(bb->total_cells, sizeof(float), "fluid_bb_distances");
  copy_vn_fl(bb->distances, bb->total_cells, FLT_MAX);

  bb->valid = true;
}

static void bb_freeData(FluidObjectBB *bb)
{
  if (bb->numobjs) {
    MEM_freeN(bb->numobjs);
  }
  if (bb->influence) {
    MEM_freeN(bb->influence);
  }
  if (bb->velocity) {
    MEM_freeN(bb->velocity);
  }
  if (bb->distances) {
    MEM_freeN(bb->distances);
  }
}

static void bb_combineMaps(FluidObjectBB *output,
                           FluidObjectBB *bb2,
                           int additive,
                           float sample_size)
{
  int i, x, y, z;

  /* Copyfill input 1 struct and clear output for new allocation. */
  FluidObjectBB bb1;
  memcpy(&bb1, output, sizeof(FluidObjectBB));
  memset(output, 0, sizeof(FluidObjectBB));

  for (i = 0; i < 3; i++) {
    if (bb1.valid) {
      output->min[i] = MIN2(bb1.min[i], bb2->min[i]);
      output->max[i] = MAX2(bb1.max[i], bb2->max[i]);
    }
    else {
      output->min[i] = bb2->min[i];
      output->max[i] = bb2->max[i];
    }
  }
  /* Allocate output map. */
  bb_allocateData(output, (bb1.velocity || bb2->velocity), (bb1.influence || bb2->influence));

  /* Low through bounding box */
  for (x = output->min[0]; x < output->max[0]; x++) {
    for (y = output->min[1]; y < output->max[1]; y++) {
      for (z = output->min[2]; z < output->max[2]; z++) {
        int index_out = manta_get_index(x - output->min[0],
                                        output->res[0],
                                        y - output->min[1],
                                        output->res[1],
                                        z - output->min[2]);

        /* Initialize with first input if in range. */
        if (x >= bb1.min[0] && x < bb1.max[0] && y >= bb1.min[1] && y < bb1.max[1] &&
            z >= bb1.min[2] && z < bb1.max[2]) {
          int index_in = manta_get_index(
              x - bb1.min[0], bb1.res[0], y - bb1.min[1], bb1.res[1], z - bb1.min[2]);

          /* Values. */
          output->numobjs[index_out] = bb1.numobjs[index_in];
          if (output->influence && bb1.influence) {
            output->influence[index_out] = bb1.influence[index_in];
          }
          output->distances[index_out] = bb1.distances[index_in];
          if (output->velocity && bb1.velocity) {
            copy_v3_v3(&output->velocity[index_out * 3], &bb1.velocity[index_in * 3]);
          }
        }

        /* Apply second input if in range. */
        if (x >= bb2->min[0] && x < bb2->max[0] && y >= bb2->min[1] && y < bb2->max[1] &&
            z >= bb2->min[2] && z < bb2->max[2]) {
          int index_in = manta_get_index(
              x - bb2->min[0], bb2->res[0], y - bb2->min[1], bb2->res[1], z - bb2->min[2]);

          /* Values. */
          output->numobjs[index_out] = MAX2(bb2->numobjs[index_in], output->numobjs[index_out]);
          if (output->influence && bb2->influence) {
            if (additive) {
              output->influence[index_out] += bb2->influence[index_in] * sample_size;
            }
            else {
              output->influence[index_out] = MAX2(bb2->influence[index_in],
                                                  output->influence[index_out]);
            }
          }
          output->distances[index_out] = MIN2(bb2->distances[index_in],
                                              output->distances[index_out]);
          if (output->velocity && bb2->velocity) {
            /* Last sample replaces the velocity. */
            output->velocity[index_out * 3] = ADD_IF_LOWER(output->velocity[index_out * 3],
                                                           bb2->velocity[index_in * 3]);
            output->velocity[index_out * 3 + 1] = ADD_IF_LOWER(output->velocity[index_out * 3 + 1],
                                                               bb2->velocity[index_in * 3 + 1]);
            output->velocity[index_out * 3 + 2] = ADD_IF_LOWER(output->velocity[index_out * 3 + 2],
                                                               bb2->velocity[index_in * 3 + 2]);
          }
        }
      } /* Low res loop. */
    }
  }

  /* Free original data. */
  bb_freeData(&bb1);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Effectors
 * \{ */

BLI_INLINE void apply_effector_fields(FluidEffectorSettings *UNUSED(fes),
                                      int index,
                                      float src_distance_value,
                                      float *dest_phi_in,
                                      float src_numobjs_value,
                                      float *dest_numobjs,
                                      float const src_vel_value[3],
                                      float *dest_vel_x,
                                      float *dest_vel_y,
                                      float *dest_vel_z)
{
  /* Ensure that distance value is "joined" into the levelset. */
  if (dest_phi_in) {
    dest_phi_in[index] = MIN2(src_distance_value, dest_phi_in[index]);
  }

  /* Accumulate effector object count (important once effector object overlap). */
  if (dest_numobjs && src_numobjs_value > 0) {
    dest_numobjs[index] += 1;
  }

  /* Accumulate effector velocities for each cell. */
  if (dest_vel_x && src_numobjs_value > 0) {
    dest_vel_x[index] += src_vel_value[0];
    dest_vel_y[index] += src_vel_value[1];
    dest_vel_z[index] += src_vel_value[2];
  }
}

static void update_velocities(FluidEffectorSettings *fes,
                              const MVert *mvert,
                              const MLoop *mloop,
                              const MLoopTri *mlooptri,
                              float *velocity_map,
                              int index,
                              BVHTreeFromMesh *tree_data,
                              const float ray_start[3],
                              const float *vert_vel,
                              bool has_velocity)
{
  BVHTreeNearest nearest = {0};
  nearest.index = -1;

  /* Distance between two opposing vertices in a unit cube.
   * I.e. the unit cube diagonal or sqrt(3).
   * This value is our nearest neighbor search distance. */
  const float surface_distance = 1.732;
  nearest.dist_sq = surface_distance * surface_distance; /* find_nearest uses squared distance */

  /* Find the nearest point on the mesh. */
  if (has_velocity &&
      BLI_bvhtree_find_nearest(
          tree_data->tree, ray_start, &nearest, tree_data->nearest_callback, tree_data) != -1) {
    float weights[3];
    int v1, v2, v3, f_index = nearest.index;

    /* Calculate barycentric weights for nearest point. */
    v1 = mloop[mlooptri[f_index].tri[0]].v;
    v2 = mloop[mlooptri[f_index].tri[1]].v;
    v3 = mloop[mlooptri[f_index].tri[2]].v;
    interp_weights_tri_v3(weights, mvert[v1].co, mvert[v2].co, mvert[v3].co, nearest.co);

    /* Apply object velocity. */
    float hit_vel[3];
    interp_v3_v3v3v3(hit_vel, &vert_vel[v1 * 3], &vert_vel[v2 * 3], &vert_vel[v3 * 3], weights);

    /* Guiding has additional velocity multiplier */
    if (fes->type == FLUID_EFFECTOR_TYPE_GUIDE) {
      mul_v3_fl(hit_vel, fes->vel_multi);

      /* Absolute representation of new object velocity. */
      float abs_hit_vel[3];
      copy_v3_v3(abs_hit_vel, hit_vel);
      abs_v3(abs_hit_vel);

      /* Absolute representation of current object velocity. */
      float abs_vel[3];
      copy_v3_v3(abs_vel, &velocity_map[index * 3]);
      abs_v3(abs_vel);

      switch (fes->guide_mode) {
        case FLUID_EFFECTOR_GUIDE_AVERAGED:
          velocity_map[index * 3] = (velocity_map[index * 3] + hit_vel[0]) * 0.5f;
          velocity_map[index * 3 + 1] = (velocity_map[index * 3 + 1] + hit_vel[1]) * 0.5f;
          velocity_map[index * 3 + 2] = (velocity_map[index * 3 + 2] + hit_vel[2]) * 0.5f;
          break;
        case FLUID_EFFECTOR_GUIDE_OVERRIDE:
          velocity_map[index * 3] = hit_vel[0];
          velocity_map[index * 3 + 1] = hit_vel[1];
          velocity_map[index * 3 + 2] = hit_vel[2];
          break;
        case FLUID_EFFECTOR_GUIDE_MIN:
          velocity_map[index * 3] = MIN2(abs_hit_vel[0], abs_vel[0]);
          velocity_map[index * 3 + 1] = MIN2(abs_hit_vel[1], abs_vel[1]);
          velocity_map[index * 3 + 2] = MIN2(abs_hit_vel[2], abs_vel[2]);
          break;
        case FLUID_EFFECTOR_GUIDE_MAX:
        default:
          velocity_map[index * 3] = MAX2(abs_hit_vel[0], abs_vel[0]);
          velocity_map[index * 3 + 1] = MAX2(abs_hit_vel[1], abs_vel[1]);
          velocity_map[index * 3 + 2] = MAX2(abs_hit_vel[2], abs_vel[2]);
          break;
      }
    }
    else if (fes->type == FLUID_EFFECTOR_TYPE_COLLISION) {
      velocity_map[index * 3] = hit_vel[0];
      velocity_map[index * 3 + 1] = hit_vel[1];
      velocity_map[index * 3 + 2] = hit_vel[2];
#  ifdef DEBUG_PRINT
      /* Debugging: Print object velocities. */
      printf("setting effector object vel: [%f, %f, %f]\n", hit_vel[0], hit_vel[1], hit_vel[2]);
#  endif
    }
    else {
      /* Should never reach this block. */
      BLI_assert_unreachable();
    }
  }
  else {
    /* Clear velocities at cells that are not moving. */
    copy_v3_fl(velocity_map, 0.0);
  }
}

typedef struct ObstaclesFromDMData {
  FluidEffectorSettings *fes;

  const MVert *mvert;
  const MLoop *mloop;
  const MLoopTri *mlooptri;

  BVHTreeFromMesh *tree;
  FluidObjectBB *bb;

  bool has_velocity;
  float *vert_vel;
  int *min, *max, *res;
} ObstaclesFromDMData;

static void obstacles_from_mesh_task_cb(void *__restrict userdata,
                                        const int z,
                                        const TaskParallelTLS *__restrict UNUSED(tls))
{
  ObstaclesFromDMData *data = userdata;
  FluidObjectBB *bb = data->bb;

  for (int x = data->min[0]; x < data->max[0]; x++) {
    for (int y = data->min[1]; y < data->max[1]; y++) {
      const int index = manta_get_index(
          x - bb->min[0], bb->res[0], y - bb->min[1], bb->res[1], z - bb->min[2]);
      const float ray_start[3] = {(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f};

      /* Calculate levelset values from meshes. Result in bb->distances. */
      update_distances(index,
                       bb->distances,
                       data->tree,
                       ray_start,
                       data->fes->surface_distance,
                       data->fes->flags & FLUID_EFFECTOR_USE_PLANE_INIT);

      /* Calculate object velocities. Result in bb->velocity. */
      update_velocities(data->fes,
                        data->mvert,
                        data->mloop,
                        data->mlooptri,
                        bb->velocity,
                        index,
                        data->tree,
                        ray_start,
                        data->vert_vel,
                        data->has_velocity);

      /* Increase obstacle count inside of moving obstacles. */
      if (bb->distances[index] < 0) {
        bb->numobjs[index]++;
      }
    }
  }
}

static void obstacles_from_mesh(Object *coll_ob,
                                FluidDomainSettings *fds,
                                FluidEffectorSettings *fes,
                                FluidObjectBB *bb,
                                float dt)
{
  if (fes->mesh) {
    Mesh *me = NULL;
    MVert *mvert = NULL;
    const MLoopTri *looptri;
    const MLoop *mloop;
    BVHTreeFromMesh tree_data = {NULL};
    int numverts, i;

    float *vert_vel = NULL;
    bool has_velocity = false;

    me = BKE_mesh_copy_for_eval(fes->mesh, true);

    int min[3], max[3], res[3];

    /* Duplicate vertices to modify. */
    if (me->mvert) {
      me->mvert = MEM_dupallocN(me->mvert);
      CustomData_set_layer(&me->vdata, CD_MVERT, me->mvert);
    }

    mvert = me->mvert;
    mloop = me->mloop;
    looptri = BKE_mesh_runtime_looptri_ensure(me);
    numverts = me->totvert;

    /* TODO(sebbas): Make initialization of vertex velocities optional? */
    {
      vert_vel = MEM_callocN(sizeof(float[3]) * numverts, "manta_obs_velocity");

      if (fes->numverts != numverts || !fes->verts_old) {
        if (fes->verts_old) {
          MEM_freeN(fes->verts_old);
        }

        fes->verts_old = MEM_callocN(sizeof(float[3]) * numverts, "manta_obs_verts_old");
        fes->numverts = numverts;
      }
      else {
        has_velocity = true;
      }
    }

    /* Transform mesh vertices to domain grid space for fast lookups.
     * This is valid because the mesh is copied above. */
    BKE_mesh_vertex_normals_ensure(me);
    float(*vert_normals)[3] = BKE_mesh_vertex_normals_for_write(me);
    for (i = 0; i < numverts; i++) {
      float co[3];

      /* Vertex position. */
      mul_m4_v3(coll_ob->obmat, mvert[i].co);
      manta_pos_to_cell(fds, mvert[i].co);

      /* Vertex normal. */
      mul_mat3_m4_v3(coll_ob->obmat, vert_normals[i]);
      mul_mat3_m4_v3(fds->imat, vert_normals[i]);
      normalize_v3(vert_normals[i]);

      /* Vertex velocity. */
      add_v3fl_v3fl_v3i(co, mvert[i].co, fds->shift);
      if (has_velocity) {
        sub_v3_v3v3(&vert_vel[i * 3], co, &fes->verts_old[i * 3]);
        mul_v3_fl(&vert_vel[i * 3], 1.0f / dt);
      }
      copy_v3_v3(&fes->verts_old[i * 3], co);

      /* Calculate emission map bounds. */
      bb_boundInsert(bb, mvert[i].co);
    }

    /* Set emission map.
     * Use 3 cell diagonals as margin (3 * 1.732 = 5.196). */
    int bounds_margin = (int)ceil(5.196);
    clamp_bounds_in_domain(fds, bb->min, bb->max, NULL, NULL, bounds_margin, dt);
    bb_allocateData(bb, true, false);

    /* Setup loop bounds. */
    for (i = 0; i < 3; i++) {
      min[i] = bb->min[i];
      max[i] = bb->max[i];
      res[i] = bb->res[i];
    }

    /* Skip effector sampling loop if object has disabled effector. */
    bool use_effector = fes->flags & FLUID_EFFECTOR_USE_EFFEC;
    if (use_effector && BKE_bvhtree_from_mesh_get(&tree_data, me, BVHTREE_FROM_LOOPTRI, 4)) {

      ObstaclesFromDMData data = {
          .fes = fes,
          .mvert = mvert,
          .mloop = mloop,
          .mlooptri = looptri,
          .tree = &tree_data,
          .bb = bb,
          .has_velocity = has_velocity,
          .vert_vel = vert_vel,
          .min = min,
          .max = max,
          .res = res,
      };

      TaskParallelSettings settings;
      BLI_parallel_range_settings_defaults(&settings);
      settings.min_iter_per_thread = 2;
      BLI_task_parallel_range(min[2], max[2], &data, obstacles_from_mesh_task_cb, &settings);
    }
    /* Free bvh tree. */
    free_bvhtree_from_mesh(&tree_data);

    if (vert_vel) {
      MEM_freeN(vert_vel);
    }
    if (me->mvert) {
      MEM_freeN(me->mvert);
    }
    BKE_id_free(NULL, me);
  }
}

static void ensure_obstaclefields(FluidDomainSettings *fds)
{
  if (fds->active_fields & FLUID_DOMAIN_ACTIVE_OBSTACLE) {
    manta_ensure_obstacle(fds->fluid, fds->fmd);
  }
  if (fds->active_fields & FLUID_DOMAIN_ACTIVE_GUIDE) {
    manta_ensure_guiding(fds->fluid, fds->fmd);
  }
  manta_update_pointers(fds->fluid, fds->fmd, false);
}

static void update_obstacleflags(FluidDomainSettings *fds,
                                 Object **coll_ob_array,
                                 int coll_ob_array_len)
{
  int active_fields = fds->active_fields;
  uint coll_index;

  /* First, remove all flags that we want to update. */
  int prev_flags = (FLUID_DOMAIN_ACTIVE_OBSTACLE | FLUID_DOMAIN_ACTIVE_GUIDE);
  active_fields &= ~prev_flags;

  /* Monitor active fields based on flow settings */
  for (coll_index = 0; coll_index < coll_ob_array_len; coll_index++) {
    Object *coll_ob = coll_ob_array[coll_index];
    FluidModifierData *fmd2 = (FluidModifierData *)BKE_modifiers_findby_type(coll_ob,
                                                                             eModifierType_Fluid);

    /* Sanity check. */
    if (!fmd2) {
      continue;
    }

    if ((fmd2->type & MOD_FLUID_TYPE_EFFEC) && fmd2->effector) {
      FluidEffectorSettings *fes = fmd2->effector;
      if (!fes) {
        break;
      }
      if (fes->flags & FLUID_EFFECTOR_NEEDS_UPDATE) {
        fes->flags &= ~FLUID_EFFECTOR_NEEDS_UPDATE;
        fds->cache_flag |= FLUID_DOMAIN_OUTDATED_DATA;
      }
      if (fes->type == FLUID_EFFECTOR_TYPE_COLLISION) {
        active_fields |= FLUID_DOMAIN_ACTIVE_OBSTACLE;
      }
      if (fes->type == FLUID_EFFECTOR_TYPE_GUIDE) {
        active_fields |= FLUID_DOMAIN_ACTIVE_GUIDE;
      }
    }
  }
  fds->active_fields = active_fields;
}

static bool escape_effectorobject(Object *flowobj,
                                  FluidDomainSettings *fds,
                                  FluidEffectorSettings *UNUSED(fes),
                                  int frame)
{
  bool is_static = is_static_object(flowobj);

  bool is_resume = (fds->cache_frame_pause_data == frame);
  bool is_adaptive = (fds->flags & FLUID_DOMAIN_USE_ADAPTIVE_DOMAIN);
  bool is_first_frame = (frame == fds->cache_frame_start);

  /* Cannot use static mode with adaptive domain.
   * The adaptive domain might expand and only later discover the static object. */
  if (is_adaptive) {
    is_static = false;
  }
  /* Skip static effector objects after initial frame. */
  if (is_static && !is_first_frame && !is_resume) {
    return true;
  }
  return false;
}

static void compute_obstaclesemission(Scene *scene,
                                      FluidObjectBB *bb_maps,
                                      struct Depsgraph *depsgraph,
                                      float dt,
                                      Object **effecobjs,
                                      int frame,
                                      float frame_length,
                                      FluidDomainSettings *fds,
                                      uint numeffecobjs,
                                      float time_per_frame)
{
  bool is_first_frame = (frame == fds->cache_frame_start);

  /* Prepare effector maps. */
  for (int effec_index = 0; effec_index < numeffecobjs; effec_index++) {
    Object *effecobj = effecobjs[effec_index];
    FluidModifierData *fmd2 = (FluidModifierData *)BKE_modifiers_findby_type(effecobj,
                                                                             eModifierType_Fluid);

    /* Sanity check. */
    if (!fmd2) {
      continue;
    }

    /* Check for initialized effector object. */
    if ((fmd2->type & MOD_FLUID_TYPE_EFFEC) && fmd2->effector) {
      FluidEffectorSettings *fes = fmd2->effector;
      int subframes = fes->subframes;
      FluidObjectBB *bb = &bb_maps[effec_index];

      /* Optimization: Skip this object under certain conditions. */
      if (escape_effectorobject(effecobj, fds, fes, frame)) {
        continue;
      }

      /* First frame cannot have any subframes because there is (obviously) no previous frame from
       * where subframes could come from. */
      if (is_first_frame) {
        subframes = 0;
      }

      /* More splitting because of emission subframe: If no subframes present, sample_size is 1. */
      float sample_size = 1.0f / (float)(subframes + 1);
      float subframe_dt = dt * sample_size;

      /* Emission loop. When not using subframes this will loop only once. */
      for (int subframe = 0; subframe <= subframes; subframe++) {

        /* Temporary emission map used when subframes are enabled, i.e. at least one subframe. */
        FluidObjectBB bb_temp = {NULL};

        /* Set scene time */
        /* Handle emission subframe */
        if ((subframe < subframes || time_per_frame + dt + FLT_EPSILON < frame_length) &&
            !is_first_frame) {
          scene->r.subframe = (time_per_frame + (subframe + 1.0f) * subframe_dt) / frame_length;
          scene->r.cfra = frame - 1;
        }
        else {
          scene->r.subframe = 0.0f;
          scene->r.cfra = frame;
        }
        /* Sanity check: subframe portion must be between 0 and 1. */
        CLAMP(scene->r.subframe, 0.0f, 1.0f);
#  ifdef DEBUG_PRINT
        /* Debugging: Print subframe information. */
        printf(
            "effector: frame (is first: %d): %d // scene current frame: %d // scene current "
            "subframe: "
            "%f\n",
            is_first_frame,
            frame,
            scene->r.cfra,
            scene->r.subframe);
#  endif
        /* Update frame time, this is considering current subframe fraction
         * BLI_mutex_lock() called in manta_step(), so safe to update subframe here
         * TODO(sebbas): Using BKE_scene_ctime_get(scene) instead of new DEG_get_ctime(depsgraph)
         * as subframes don't work with the latter yet. */
        BKE_object_modifier_update_subframe(
            depsgraph, scene, effecobj, true, 5, BKE_scene_ctime_get(scene), eModifierType_Fluid);

        if (subframes) {
          obstacles_from_mesh(effecobj, fds, fes, &bb_temp, subframe_dt);
        }
        else {
          obstacles_from_mesh(effecobj, fds, fes, bb, subframe_dt);
        }

        /* If this we emitted with temp emission map in this loop (subframe emission), we combine
         * the temp map with the original emission map. */
        if (subframes) {
          /* Combine emission maps. */
          bb_combineMaps(bb, &bb_temp, 0, 0.0f);
          bb_freeData(&bb_temp);
        }
      }
    }
  }
}
