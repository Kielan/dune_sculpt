#include <stddef.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_anim_types.h"
#include "DNA_boid_types.h"
#include "DNA_cloth_types.h"
#include "DNA_curve_types.h"
#include "DNA_listBase.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BLI_blenlib.h"
#include "BLI_edgehash.h"
#include "BLI_kdopbvh.h"
#include "BLI_kdtree.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_string_utils.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BKE_animsys.h"
#include "BKE_boids.h"
#include "BKE_collision.h"
#include "BKE_colortools.h"
#include "BKE_effect.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_particle.h"

#include "BKE_bvhutils.h"
#include "BKE_cloth.h"
#include "BKE_collection.h"
#include "BKE_lattice.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_pointcache.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_physics.h"
#include "DEG_depsgraph_query.h"

#include "PIL_time.h"

#include "RE_texture.h"

/* FLUID sim particle import */
#ifdef WITH_FLUID
#  include "DNA_fluid_types.h"
#  include "manta_fluid_API.h"
#endif  // WITH_FLUID

static ThreadRWMutex psys_bvhtree_rwlock = BLI_RWLOCK_INITIALIZER;

/************************************************/
/*          Reacting to system events           */
/************************************************/

static int particles_are_dynamic(ParticleSystem *psys)
{
  if (psys->pointcache->flag & PTCACHE_BAKED) {
    return 0;
  }

  if (psys->part->type == PART_HAIR) {
    return psys->flag & PSYS_HAIR_DYNAMICS;
  }

  return ELEM(psys->part->phystype, PART_PHYS_NEWTON, PART_PHYS_BOIDS, PART_PHYS_FLUID);
}

float psys_get_current_display_percentage(ParticleSystem *psys, const bool use_render_params)
{
  ParticleSettings *part = psys->part;

  if ((use_render_params &&
       !particles_are_dynamic(psys)) ||          /* non-dynamic particles can be rendered fully */
      (part->child_nbr && part->childtype) ||    /* display percentage applies to children */
      (psys->pointcache->flag & PTCACHE_BAKING)) /* baking is always done with full amount */
  {
    return 1.0f;
  }

  return psys->part->disp / 100.0f;
}

static int tot_particles(ParticleSystem *psys, PTCacheID *pid)
{
  if (pid && psys->pointcache->flag & PTCACHE_EXTERNAL) {
    return pid->cache->totpoint;
  }
  if (psys->part->distr == PART_DISTR_GRID && psys->part->from != PART_FROM_VERT) {
    return psys->part->grid_res * psys->part->grid_res * psys->part->grid_res - psys->totunexist;
  }

  return psys->part->totpart - psys->totunexist;
}

void psys_reset(ParticleSystem *psys, int mode)
{
  PARTICLE_P;

  if (ELEM(mode, PSYS_RESET_ALL, PSYS_RESET_DEPSGRAPH)) {
    if (mode == PSYS_RESET_ALL || !(psys->flag & PSYS_EDITED)) {
      /* don't free if not absolutely necessary */
      if (psys->totpart != tot_particles(psys, NULL)) {
        psys_free_particles(psys);
        psys->totpart = 0;
      }

      psys->totkeyed = 0;
      psys->flag &= ~(PSYS_HAIR_DONE | PSYS_KEYED);

      if (psys->edit && psys->free_edit) {
        psys->free_edit(psys->edit);
        psys->edit = NULL;
        psys->free_edit = NULL;
      }
    }
  }
  else if (mode == PSYS_RESET_CACHE_MISS) {
    /* set all particles to be skipped */
    LOOP_PARTICLES
    {
      pa->flag |= PARS_NO_DISP;
    }
  }

  /* reset children */
  MEM_SAFE_FREE(psys->child);

  psys->totchild = 0;

  /* reset path cache */
  psys_free_path_cache(psys, psys->edit);

  /* reset point cache */
  BKE_ptcache_invalidate(psys->pointcache);

  MEM_SAFE_FREE(psys->fluid_springs);

  psys->tot_fluidsprings = psys->alloc_fluidsprings = 0;
}

void psys_unique_name(Object *object, ParticleSystem *psys, const char *defname)
{
  BLI_uniquename(&object->particlesystem,
                 psys,
                 defname,
                 '.',
                 offsetof(ParticleSystem, name),
                 sizeof(psys->name));
}

static void realloc_particles(ParticleSimulationData *sim, int new_totpart)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = psys->part;
  ParticleData *newpars = NULL;
  BoidParticle *newboids = NULL;
  PARTICLE_P;
  int totpart, totsaved = 0;

  if (new_totpart < 0) {
    if ((part->distr == PART_DISTR_GRID) && (part->from != PART_FROM_VERT)) {
      totpart = part->grid_res;
      totpart *= totpart * totpart;
    }
    else {
      totpart = part->totpart;
    }
  }
  else {
    totpart = new_totpart;
  }

  if (totpart != psys->totpart) {
    if (psys->edit && psys->free_edit) {
      psys->free_edit(psys->edit);
      psys->edit = NULL;
      psys->free_edit = NULL;
    }

    if (totpart) {
      newpars = MEM_callocN(totpart * sizeof(ParticleData), "particles");
      if (newpars == NULL) {
        return;
      }

      if (psys->part->phystype == PART_PHYS_BOIDS) {
        newboids = MEM_callocN(totpart * sizeof(BoidParticle), "boid particles");

        if (newboids == NULL) {
          /* allocation error! */
          if (newpars) {
            MEM_freeN(newpars);
          }
          return;
        }
      }
    }

    if (psys->particles) {
      totsaved = MIN2(psys->totpart, totpart);
      /* Save old pars. */
      if (totsaved) {
        memcpy(newpars, psys->particles, totsaved * sizeof(ParticleData));

        if (psys->particles->boid) {
          memcpy(newboids, psys->particles->boid, totsaved * sizeof(BoidParticle));
        }
      }

      if (psys->particles->keys) {
        MEM_freeN(psys->particles->keys);
      }

      if (psys->particles->boid) {
        MEM_freeN(psys->particles->boid);
      }

      for (p = 0, pa = newpars; p < totsaved; p++, pa++) {
        if (pa->keys) {
          pa->keys = NULL;
          pa->totkey = 0;
        }
      }

      for (p = totsaved, pa = psys->particles + totsaved; p < psys->totpart; p++, pa++) {
        if (pa->hair) {
          MEM_freeN(pa->hair);
        }
      }

      MEM_freeN(psys->particles);
      psys_free_pdd(psys);
    }

    psys->particles = newpars;
    psys->totpart = totpart;

    if (newboids) {
      LOOP_PARTICLES
      {
        pa->boid = newboids++;
      }
    }
  }

  if (psys->child) {
    MEM_freeN(psys->child);
    psys->child = NULL;
    psys->totchild = 0;
  }
}

int psys_get_child_number(Scene *scene, ParticleSystem *psys, const bool use_render_params)
{
  int nbr;

  if (!psys->part->childtype) {
    return 0;
  }

  if (use_render_params) {
    nbr = psys->part->ren_child_nbr;
  }
  else {
    nbr = psys->part->child_nbr;
  }

  return get_render_child_particle_number(&scene->r, nbr, use_render_params);
}

int psys_get_tot_child(Scene *scene, ParticleSystem *psys, const bool use_render_params)
{
  return psys->totpart * psys_get_child_number(scene, psys, use_render_params);
}

/************************************************/
/*          Distribution                        */
/************************************************/

void psys_calc_dmcache(Object *ob, Mesh *mesh_final, Mesh *mesh_original, ParticleSystem *psys)
{
  /* use for building derived mesh mapping info:
   *
   * node: the allocated links - total derived mesh element count
   * nodearray: the array of nodes aligned with the base mesh's elements, so
   *            each original elements can reference its derived elements
   */
  Mesh *me = (Mesh *)ob->data;
  bool use_modifier_stack = psys->part->use_modifier_stack;
  PARTICLE_P;

  /* CACHE LOCATIONS */
  if (!mesh_final->runtime.deformed_only) {
    /* Will use later to speed up subsurf/evaluated mesh. */
    LinkNode *node, *nodedmelem, **nodearray;
    int totdmelem, totelem, i, *origindex, *origindex_poly = NULL;

    if (psys->part->from == PART_FROM_VERT) {
      totdmelem = mesh_final->totvert;

      if (use_modifier_stack) {
        totelem = totdmelem;
        origindex = NULL;
      }
      else {
        totelem = me->totvert;
        origindex = CustomData_get_layer(&mesh_final->vdata, CD_ORIGINDEX);
      }
    }
    else { /* FROM_FACE/FROM_VOLUME */
      totdmelem = mesh_final->totface;

      if (use_modifier_stack) {
        totelem = totdmelem;
        origindex = NULL;
        origindex_poly = NULL;
      }
      else {
        totelem = mesh_original->totface;
        origindex = CustomData_get_layer(&mesh_final->fdata, CD_ORIGINDEX);

        /* for face lookups we need the poly origindex too */
        origindex_poly = CustomData_get_layer(&mesh_final->pdata, CD_ORIGINDEX);
        if (origindex_poly == NULL) {
          origindex = NULL;
        }
      }
    }

    nodedmelem = MEM_callocN(sizeof(LinkNode) * totdmelem, "psys node elems");
    nodearray = MEM_callocN(sizeof(LinkNode *) * totelem, "psys node array");

    for (i = 0, node = nodedmelem; i < totdmelem; i++, node++) {
      int origindex_final;
      node->link = POINTER_FROM_INT(i);

      /* may be vertex or face origindex */
      if (use_modifier_stack) {
        origindex_final = i;
      }
      else {
        origindex_final = origindex ? origindex[i] : ORIGINDEX_NONE;

        /* if we have a poly source, do an index lookup */
        if (origindex_poly && origindex_final != ORIGINDEX_NONE) {
          origindex_final = origindex_poly[origindex_final];
        }
      }

      if (origindex_final != ORIGINDEX_NONE && origindex_final < totelem) {
        if (nodearray[origindex_final]) {
          /* prepend */
          node->next = nodearray[origindex_final];
          nodearray[origindex_final] = node;
        }
        else {
          nodearray[origindex_final] = node;
        }
      }
    }

    /* cache the verts/faces! */
    LOOP_PARTICLES
    {
      if (pa->num < 0) {
        pa->num_dmcache = DMCACHE_NOTFOUND;
        continue;
      }

      if (use_modifier_stack) {
        if (pa->num < totelem) {
          pa->num_dmcache = DMCACHE_ISCHILD;
        }
        else {
          pa->num_dmcache = DMCACHE_NOTFOUND;
        }
      }
      else {
        if (psys->part->from == PART_FROM_VERT) {
          if (pa->num < totelem && nodearray[pa->num]) {
            pa->num_dmcache = POINTER_AS_INT(nodearray[pa->num]->link);
          }
          else {
            pa->num_dmcache = DMCACHE_NOTFOUND;
          }
        }
        else { /* FROM_FACE/FROM_VOLUME */
          pa->num_dmcache = psys_particle_dm_face_lookup(
              mesh_final, mesh_original, pa->num, pa->fuv, nodearray);
        }
      }
    }

    MEM_freeN(nodearray);
    MEM_freeN(nodedmelem);
  }
  else {
    /* TODO_PARTICLE: make the following line unnecessary, each function
     * should know to use the num or num_dmcache, set the num_dmcache to
     * an invalid value, just in case. */

    LOOP_PARTICLES
    {
      pa->num_dmcache = DMCACHE_NOTFOUND;
    }
  }
}

void psys_thread_context_init(ParticleThreadContext *ctx, ParticleSimulationData *sim)
{
  memset(ctx, 0, sizeof(ParticleThreadContext));
  ctx->sim = *sim;
  ctx->mesh = ctx->sim.psmd->mesh_final;
  ctx->ma = BKE_object_material_get(sim->ob, sim->psys->part->omat);
}

void psys_tasks_create(ParticleThreadContext *ctx,
                       int startpart,
                       int endpart,
                       ParticleTask **r_tasks,
                       int *r_numtasks)
{
  ParticleTask *tasks;
  int numtasks = min_ii(BLI_system_thread_count() * 4, endpart - startpart);
  int particles_per_task = numtasks > 0 ? (endpart - startpart) / numtasks : 0;
  int remainder = numtasks > 0 ? (endpart - startpart) - particles_per_task * numtasks : 0;

  tasks = MEM_callocN(sizeof(ParticleTask) * numtasks, "ParticleThread");
  *r_numtasks = numtasks;
  *r_tasks = tasks;

  int p = startpart;
  for (int i = 0; i < numtasks; i++) {
    tasks[i].ctx = ctx;
    tasks[i].begin = p;
    p = p + particles_per_task + (i < remainder ? 1 : 0);
    tasks[i].end = p;
  }

  /* Verify that all particles are accounted for. */
  if (numtasks > 0) {
    BLI_assert(tasks[numtasks - 1].end == endpart);
  }
}

void psys_tasks_free(ParticleTask *tasks, int numtasks)
{
  int i;

  /* threads */
  for (i = 0; i < numtasks; i++) {
    if (tasks[i].rng) {
      BLI_rng_free(tasks[i].rng);
    }
    if (tasks[i].rng_path) {
      BLI_rng_free(tasks[i].rng_path);
    }
  }

  MEM_freeN(tasks);
}

void psys_thread_context_free(ParticleThreadContext *ctx)
{
  /* path caching */
  if (ctx->vg_length) {
    MEM_freeN(ctx->vg_length);
  }
  if (ctx->vg_clump) {
    MEM_freeN(ctx->vg_clump);
  }
  if (ctx->vg_kink) {
    MEM_freeN(ctx->vg_kink);
  }
  if (ctx->vg_rough1) {
    MEM_freeN(ctx->vg_rough1);
  }
  if (ctx->vg_rough2) {
    MEM_freeN(ctx->vg_rough2);
  }
  if (ctx->vg_roughe) {
    MEM_freeN(ctx->vg_roughe);
  }
  if (ctx->vg_twist) {
    MEM_freeN(ctx->vg_twist);
  }

  if (ctx->sim.psys->lattice_deform_data) {
    BKE_lattice_deform_data_destroy(ctx->sim.psys->lattice_deform_data);
    ctx->sim.psys->lattice_deform_data = NULL;
  }

  /* distribution */
  if (ctx->jit) {
    MEM_freeN(ctx->jit);
  }
  if (ctx->jitoff) {
    MEM_freeN(ctx->jitoff);
  }
  if (ctx->weight) {
    MEM_freeN(ctx->weight);
  }
  if (ctx->index) {
    MEM_freeN(ctx->index);
  }
  if (ctx->seams) {
    MEM_freeN(ctx->seams);
  }
  // if (ctx->vertpart) MEM_freeN(ctx->vertpart);
  BLI_kdtree_3d_free(ctx->tree);

  if (ctx->clumpcurve != NULL) {
    BKE_curvemapping_free(ctx->clumpcurve);
  }
  if (ctx->roughcurve != NULL) {
    BKE_curvemapping_free(ctx->roughcurve);
  }
  if (ctx->twistcurve != NULL) {
    BKE_curvemapping_free(ctx->twistcurve);
  }
}

static void init_particle_texture(ParticleSimulationData *sim, ParticleData *pa, int p)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = psys->part;
  ParticleTexture ptex;

  psys_get_texture(sim, pa, &ptex, PAMAP_INIT, 0.0f);

  switch (part->type) {
    case PART_EMITTER:
      if (ptex.exist < psys_frand(psys, p + 125)) {
        pa->flag |= PARS_UNEXIST;
      }
      pa->time = part->sta + (part->end - part->sta) * ptex.time;
      break;
    case PART_HAIR:
      if (ptex.exist < psys_frand(psys, p + 125)) {
        pa->flag |= PARS_UNEXIST;
      }
      pa->time = 0.0f;
      break;
  }
}

void init_particle(ParticleSimulationData *sim, ParticleData *pa)
{
  ParticleSettings *part = sim->psys->part;
  float birth_time = (float)(pa - sim->psys->particles) / (float)sim->psys->totpart;

  pa->flag &= ~PARS_UNEXIST;
  pa->time = part->sta + (part->end - part->sta) * birth_time;

  pa->hair_index = 0;
  /* We can't reset to -1 anymore since we've figured out correct index in #distribute_particles
   * usage other than straight after distribute has to handle this index by itself - jahka. */
  // pa->num_dmcache = DMCACHE_NOTFOUND; /* assume we don't have a derived mesh face */
}

static void initialize_all_particles(ParticleSimulationData *sim)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = psys->part;
  /* Grid distributionsets UNEXIST flag, need to take care of
   * it here because later this flag is being reset.
   *
   * We can't do it for any distribution, because it'll then
   * conflict with texture influence, which does not free
   * unexisting particles and only sets flag.
   *
   * It's not so bad, because only grid distribution sets
   * UNEXIST flag.
   */
  const bool emit_from_volume_grid = (part->distr == PART_DISTR_GRID) &&
                                     (!ELEM(part->from, PART_FROM_VERT, PART_FROM_CHILD));
  PARTICLE_P;
  LOOP_PARTICLES
  {
    if (!(emit_from_volume_grid && (pa->flag & PARS_UNEXIST) != 0)) {
      init_particle(sim, pa);
    }
  }
}

static void free_unexisting_particles(ParticleSimulationData *sim)
{
  ParticleSystem *psys = sim->psys;
  PARTICLE_P;

  psys->totunexist = 0;

  LOOP_PARTICLES
  {
    if (pa->flag & PARS_UNEXIST) {
      psys->totunexist++;
    }
  }

  if (psys->totpart && psys->totunexist == psys->totpart) {
    if (psys->particles->boid) {
      MEM_freeN(psys->particles->boid);
    }

    MEM_freeN(psys->particles);
    psys->particles = NULL;
    psys->totpart = psys->totunexist = 0;
  }

  if (psys->totunexist) {
    int newtotpart = psys->totpart - psys->totunexist;
    ParticleData *npa, *newpars;

    npa = newpars = MEM_callocN(newtotpart * sizeof(ParticleData), "particles");

    for (p = 0, pa = psys->particles; p < newtotpart; p++, pa++, npa++) {
      while (pa->flag & PARS_UNEXIST) {
        pa++;
      }

      memcpy(npa, pa, sizeof(ParticleData));
    }

    if (psys->particles->boid) {
      MEM_freeN(psys->particles->boid);
    }
    MEM_freeN(psys->particles);
    psys->particles = newpars;
    psys->totpart -= psys->totunexist;

    if (psys->particles->boid) {
      BoidParticle *newboids = MEM_callocN(psys->totpart * sizeof(BoidParticle), "boid particles");

      LOOP_PARTICLES
      {
        pa->boid = newboids++;
      }
    }
  }
}

static void get_angular_velocity_vector(short avemode, ParticleKey *state, float vec[3])
{
  switch (avemode) {
    case PART_AVE_VELOCITY:
      copy_v3_v3(vec, state->vel);
      break;
    case PART_AVE_HORIZONTAL: {
      float zvec[3];
      zvec[0] = zvec[1] = 0;
      zvec[2] = 1.0f;
      cross_v3_v3v3(vec, state->vel, zvec);
      break;
    }
    case PART_AVE_VERTICAL: {
      float zvec[3], temp[3];
      zvec[0] = zvec[1] = 0;
      zvec[2] = 1.0f;
      cross_v3_v3v3(temp, state->vel, zvec);
      cross_v3_v3v3(vec, temp, state->vel);
      break;
    }
    case PART_AVE_GLOBAL_X:
      vec[0] = 1.0f;
      vec[1] = vec[2] = 0;
      break;
    case PART_AVE_GLOBAL_Y:
      vec[1] = 1.0f;
      vec[0] = vec[2] = 0;
      break;
    case PART_AVE_GLOBAL_Z:
      vec[2] = 1.0f;
      vec[0] = vec[1] = 0;
      break;
  }
}

void psys_get_birth_coords(
    ParticleSimulationData *sim, ParticleData *pa, ParticleKey *state, float dtime, float cfra)
{
  Object *ob = sim->ob;
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = psys->part;
  ParticleTexture ptex;
  float fac, phasefac, nor[3] = {0, 0, 0}, loc[3], vel[3] = {0.0, 0.0, 0.0}, rot[4], q2[4];
  float r_vel[3], r_ave[3], r_rot[4], vec[3], p_vel[3] = {0.0, 0.0, 0.0};
  float x_vec[3] = {1.0, 0.0, 0.0}, utan[3] = {0.0, 1.0, 0.0}, vtan[3] = {0.0, 0.0, 1.0},
        rot_vec[3] = {0.0, 0.0, 0.0};
  float q_phase[4];

  const bool use_boids = ((part->phystype == PART_PHYS_BOIDS) && (pa->boid != NULL));
  const bool use_tangents = ((use_boids == false) &&
                             ((part->tanfac != 0.0f) || (part->rotmode == PART_ROT_NOR_TAN)));

  int p = pa - psys->particles;

  /* get birth location from object       */
  if (use_tangents) {
    psys_particle_on_emitter(sim->psmd,
                             part->from,
                             pa->num,
                             pa->num_dmcache,
                             pa->fuv,
                             pa->foffset,
                             loc,
                             nor,
                             utan,
                             vtan,
                             0);
  }
  else {
    psys_particle_on_emitter(
        sim->psmd, part->from, pa->num, pa->num_dmcache, pa->fuv, pa->foffset, loc, nor, 0, 0, 0);
  }

  /* get possible textural influence */
  psys_get_texture(sim, pa, &ptex, PAMAP_IVEL, cfra);

  /* particles live in global space so    */
  /* let's convert:                       */
  /* -location                            */
  mul_m4_v3(ob->obmat, loc);

  /* -normal                              */
  mul_mat3_m4_v3(ob->obmat, nor);
  normalize_v3(nor);

  /* -tangent                             */
  if (use_tangents) {
#if 0
    float phase = vg_rot ?
                      2.0f *
                          (psys_particle_value_from_verts(sim->psmd->dm, part->from, pa, vg_rot) -
                           0.5f) :
                      0.0f;
#else
    float phase = 0.0f;
#endif
    mul_v3_fl(vtan, -cosf((float)M_PI * (part->tanphase + phase)));
    fac = -sinf((float)M_PI * (part->tanphase + phase));
    madd_v3_v3fl(vtan, utan, fac);

    mul_mat3_m4_v3(ob->obmat, vtan);

    copy_v3_v3(utan, nor);
    mul_v3_fl(utan, dot_v3v3(vtan, nor));
    sub_v3_v3(vtan, utan);

    normalize_v3(vtan);
  }

  /* -velocity (boids need this even if there's no random velocity) */
  if (part->randfac != 0.0f || (part->phystype == PART_PHYS_BOIDS && pa->boid)) {
    r_vel[0] = 2.0f * (psys_frand(psys, p + 10) - 0.5f);
    r_vel[1] = 2.0f * (psys_frand(psys, p + 11) - 0.5f);
    r_vel[2] = 2.0f * (psys_frand(psys, p + 12) - 0.5f);

    mul_mat3_m4_v3(ob->obmat, r_vel);
    normalize_v3(r_vel);
  }

  /* -angular velocity                    */
  if (part->avemode == PART_AVE_RAND) {
    r_ave[0] = 2.0f * (psys_frand(psys, p + 13) - 0.5f);
    r_ave[1] = 2.0f * (psys_frand(psys, p + 14) - 0.5f);
    r_ave[2] = 2.0f * (psys_frand(psys, p + 15) - 0.5f);

    mul_mat3_m4_v3(ob->obmat, r_ave);
    normalize_v3(r_ave);
  }

  /* -rotation                            */
  if (part->randrotfac != 0.0f) {
    r_rot[0] = 2.0f * (psys_frand(psys, p + 16) - 0.5f);
    r_rot[1] = 2.0f * (psys_frand(psys, p + 17) - 0.5f);
    r_rot[2] = 2.0f * (psys_frand(psys, p + 18) - 0.5f);
    r_rot[3] = 2.0f * (psys_frand(psys, p + 19) - 0.5f);
    normalize_qt(r_rot);

    mat4_to_quat(rot, ob->obmat);
    mul_qt_qtqt(r_rot, r_rot, rot);
  }

  if (use_boids) {
    float dvec[3], q[4], mat[3][3];

    copy_v3_v3(state->co, loc);

    /* boids don't get any initial velocity. */
    zero_v3(state->vel);

    /* boids store direction in ave */
    if (fabsf(nor[2]) == 1.0f) {
      sub_v3_v3v3(state->ave, loc, ob->obmat[3]);
      normalize_v3(state->ave);
    }
    else {
      copy_v3_v3(state->ave, nor);
    }

    /* calculate rotation matrix */
    project_v3_v3v3(dvec, r_vel, state->ave);
    sub_v3_v3v3(mat[0], state->ave, dvec);
    normalize_v3(mat[0]);
    negate_v3_v3(mat[2], r_vel);
    normalize_v3(mat[2]);
    cross_v3_v3v3(mat[1], mat[2], mat[0]);

    /* apply rotation */
    mat3_to_quat_is_ok(q, mat);
    copy_qt_qt(state->rot, q);
  }
  else {
    /* conversion done so now we apply new: */
    /* -velocity from:                      */

    /*      *reactions                      */
    if (dtime > 0.0f) {
      sub_v3_v3v3(vel, pa->state.vel, pa->prev_state.vel);
    }

    /*      *emitter velocity               */
    if (dtime != 0.0f && part->obfac != 0.0f) {
      sub_v3_v3v3(vel, loc, state->co);
      mul_v3_fl(vel, part->obfac / dtime);
    }

    /*      *emitter normal                 */
    if (part->normfac != 0.0f) {
      madd_v3_v3fl(vel, nor, part->normfac);
    }

    /*      *emitter tangent                */
    if (sim->psmd && part->tanfac != 0.0f) {
      madd_v3_v3fl(vel, vtan, part->tanfac);
    }

    /*      *emitter object orientation     */
    if (part->ob_vel[0] != 0.0f) {
      normalize_v3_v3(vec, ob->obmat[0]);
      madd_v3_v3fl(vel, vec, part->ob_vel[0]);
    }
    if (part->ob_vel[1] != 0.0f) {
      normalize_v3_v3(vec, ob->obmat[1]);
      madd_v3_v3fl(vel, vec, part->ob_vel[1]);
    }
    if (part->ob_vel[2] != 0.0f) {
      normalize_v3_v3(vec, ob->obmat[2]);
      madd_v3_v3fl(vel, vec, part->ob_vel[2]);
    }

    /*      *texture                        */
    /* TODO */

    /*      *random                         */
    if (part->randfac != 0.0f) {
      madd_v3_v3fl(vel, r_vel, part->randfac);
    }

    /*      *particle                       */
    if (part->partfac != 0.0f) {
      madd_v3_v3fl(vel, p_vel, part->partfac);
    }

    mul_v3_v3fl(state->vel, vel, ptex.ivel);

    /* -location from emitter               */
    copy_v3_v3(state->co, loc);

    /* -rotation                            */
    unit_qt(state->rot);

    if (part->rotmode) {
      bool use_global_space;

      /* create vector into which rotation is aligned */
      switch (part->rotmode) {
        case PART_ROT_NOR:
        case PART_ROT_NOR_TAN:
          copy_v3_v3(rot_vec, nor);
          use_global_space = false;
          break;
        case PART_ROT_VEL:
          copy_v3_v3(rot_vec, vel);
          use_global_space = true;
          break;
        case PART_ROT_GLOB_X:
        case PART_ROT_GLOB_Y:
        case PART_ROT_GLOB_Z:
          rot_vec[part->rotmode - PART_ROT_GLOB_X] = 1.0f;
          use_global_space = true;
          break;
        case PART_ROT_OB_X:
        case PART_ROT_OB_Y:
        case PART_ROT_OB_Z:
          copy_v3_v3(rot_vec, ob->obmat[part->rotmode - PART_ROT_OB_X]);
          use_global_space = false;
          break;
        default:
          use_global_space = true;
          break;
      }

      /* create rotation quat */

      if (use_global_space) {
        negate_v3(rot_vec);
        vec_to_quat(q2, rot_vec, OB_POSX, OB_POSZ);

        /* randomize rotation quat */
        if (part->randrotfac != 0.0f) {
          interp_qt_qtqt(rot, q2, r_rot, part->randrotfac);
        }
        else {
          copy_qt_qt(rot, q2);
        }
      }
      else {
        /* calculate rotation in local-space */
        float q_obmat[4];
        float q_imat[4];

        mat4_to_quat(q_obmat, ob->obmat);
        invert_qt_qt_normalized(q_imat, q_obmat);

        if (part->rotmode != PART_ROT_NOR_TAN) {
          float rot_vec_local[3];

          /* rot_vec */
          negate_v3(rot_vec);
          copy_v3_v3(rot_vec_local, rot_vec);
          mul_qt_v3(q_imat, rot_vec_local);
          normalize_v3(rot_vec_local);

          vec_to_quat(q2, rot_vec_local, OB_POSX, OB_POSZ);
        }
        else {
          /* (part->rotmode == PART_ROT_NOR_TAN) */
          float tmat[3][3];

          /* NOTE: utan_local is not taken from 'utan', we calculate from rot_vec/vtan. */
          /* NOTE(campbell): it looks like rotation phase may be applied twice
           * (once with vtan, again below) however this isn't the case. */
          float *rot_vec_local = tmat[0];
          float *vtan_local = tmat[1];
          float *utan_local = tmat[2];

          /* use tangents */
          BLI_assert(use_tangents == true);

          /* rot_vec */
          copy_v3_v3(rot_vec_local, rot_vec);
          mul_qt_v3(q_imat, rot_vec_local);

          /* vtan_local */
          copy_v3_v3(vtan_local, vtan); /* flips, can't use */
          mul_qt_v3(q_imat, vtan_local);

          /* ensure orthogonal matrix (rot_vec aligned) */
          cross_v3_v3v3(utan_local, vtan_local, rot_vec_local);
          cross_v3_v3v3(vtan_local, utan_local, rot_vec_local);

          /* NOTE: no need to normalize. */
          mat3_to_quat(q2, tmat);
        }

        /* randomize rotation quat */
        if (part->randrotfac != 0.0f) {
          mul_qt_qtqt(r_rot, r_rot, q_imat);
          interp_qt_qtqt(rot, q2, r_rot, part->randrotfac);
        }
        else {
          copy_qt_qt(rot, q2);
        }

        mul_qt_qtqt(rot, q_obmat, rot);
      }

      /* rotation phase */
      phasefac = part->phasefac;
      if (part->randphasefac != 0.0f) {
        phasefac += part->randphasefac * psys_frand(psys, p + 20);
      }
      axis_angle_to_quat(q_phase, x_vec, phasefac * (float)M_PI);

      /* combine base rotation & phase */
      mul_qt_qtqt(state->rot, rot, q_phase);
    }

    /* -angular velocity                    */

    zero_v3(state->ave);

    if (part->avemode) {
      if (part->avemode == PART_AVE_RAND) {
        copy_v3_v3(state->ave, r_ave);
      }
      else {
        get_angular_velocity_vector(part->avemode, state, state->ave);
      }

      normalize_v3(state->ave);
      mul_v3_fl(state->ave, part->avefac);
    }
  }
}

/* recursively evaluate emitter parent anim at cfra */
static void evaluate_emitter_anim(struct Depsgraph *depsgraph,
                                  Scene *scene,
                                  Object *ob,
                                  float cfra)
{
  if (ob->parent) {
    evaluate_emitter_anim(depsgraph, scene, ob->parent, cfra);
  }

  BKE_object_where_is_calc_time(depsgraph, scene, ob, cfra);
}

void reset_particle(ParticleSimulationData *sim, ParticleData *pa, float dtime, float cfra)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part;
  ParticleTexture ptex;
  int p = pa - psys->particles;
  part = psys->part;

  /* get precise emitter matrix if particle is born */
  if (part->type != PART_HAIR && dtime > 0.0f && pa->time < cfra && pa->time >= sim->psys->cfra) {
    evaluate_emitter_anim(sim->depsgraph, sim->scene, sim->ob, pa->time);

    psys->flag |= PSYS_OB_ANIM_RESTORE;
  }

  psys_get_birth_coords(sim, pa, &pa->state, dtime, cfra);

  /* Initialize particle settings which depends on texture.
   *
   * We could only do it now because we'll need to know coordinate
   * before sampling the texture.
   */
  init_particle_texture(sim, pa, p);

  if (part->phystype == PART_PHYS_BOIDS && pa->boid) {
    BoidParticle *bpa = pa->boid;

    /* and gravity in r_ve */
    bpa->gravity[0] = bpa->gravity[1] = 0.0f;
    bpa->gravity[2] = -1.0f;
    if ((sim->scene->physics_settings.flag & PHYS_GLOBAL_GRAVITY) &&
        (sim->scene->physics_settings.gravity[2] != 0.0f)) {
      bpa->gravity[2] = sim->scene->physics_settings.gravity[2];
    }

    bpa->data.health = part->boids->health;
    bpa->data.mode = eBoidMode_InAir;
    bpa->data.state_id = ((BoidState *)part->boids->states.first)->id;
    bpa->data.acc[0] = bpa->data.acc[1] = bpa->data.acc[2] = 0.0f;
  }

  if (part->type == PART_HAIR) {
    pa->lifetime = 100.0f;
  }
  else {
    /* initialize the lifetime, in case the texture coordinates
     * are from Particles/Strands, which would cause undefined values
     */
    pa->lifetime = part->lifetime * (1.0f - part->randlife * psys_frand(psys, p + 21));
    pa->dietime = pa->time + pa->lifetime;

    /* get possible textural influence */
    psys_get_texture(sim, pa, &ptex, PAMAP_LIFE, cfra);

    pa->lifetime = part->lifetime * ptex.life;

    if (part->randlife != 0.0f) {
      pa->lifetime *= 1.0f - part->randlife * psys_frand(psys, p + 21);
    }
  }

  pa->dietime = pa->time + pa->lifetime;

  if ((sim->psys->pointcache) && (sim->psys->pointcache->flag & PTCACHE_BAKED) &&
      (sim->psys->pointcache->mem_cache.first)) {
    float dietime = psys_get_dietime_from_cache(sim->psys->pointcache, p);
    pa->dietime = MIN2(pa->dietime, dietime);
  }

  if (pa->time > cfra) {
    pa->alive = PARS_UNBORN;
  }
  else if (pa->dietime <= cfra) {
    pa->alive = PARS_DEAD;
  }
  else {
    pa->alive = PARS_ALIVE;
  }

  pa->state.time = cfra;
}
static void reset_all_particles(ParticleSimulationData *sim, float dtime, float cfra, int from)
{
  ParticleData *pa;
  int p, totpart = sim->psys->totpart;

  for (p = from, pa = sim->psys->particles + from; p < totpart; p++, pa++) {
    reset_particle(sim, pa, dtime, cfra);
  }
}

/************************************************/
/*          Particle targets                    */
/************************************************/
