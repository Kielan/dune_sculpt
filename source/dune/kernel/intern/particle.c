
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2007 by Janne Karhu. All rights reserved. */

/** \file
 * \ingroup bke
 */

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_defaults.h"

#include "DNA_cloth_types.h"
#include "DNA_collection_types.h"
#include "DNA_curve_types.h"
#include "DNA_dynamicpaint_types.h"
#include "DNA_fluid_types.h"
#include "DNA_key_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_force_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"

#include "BLI_blenlib.h"
#include "BLI_kdopbvh.h"
#include "BLI_kdtree.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_anim_path.h"
#include "BKE_boids.h"
#include "BKE_cloth.h"
#include "BKE_collection.h"
#include "BKE_colortools.h"
#include "BKE_deform.h"
#include "BKE_displist.h"
#include "BKE_effect.h"
#include "BKE_idtype.h"
#include "BKE_key.h"
#include "BKE_lattice.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_scene.h"
#include "BKE_texture.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "RE_texture.h"

#include "BLO_read_write.h"

#include "particle_private.h"

static void fluid_free_settings(SPHFluidSettings *fluid);

static void particle_settings_init(ID *id)
{
  ParticleSettings *particle_settings = (ParticleSettings *)id;
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(particle_settings, id));

  MEMCPY_STRUCT_AFTER(particle_settings, DNA_struct_default_get(ParticleSettings), id);

  particle_settings->effector_weights = BKE_effector_add_weights(NULL);
  particle_settings->pd = BKE_partdeflect_new(PFIELD_NULL);
  particle_settings->pd2 = BKE_partdeflect_new(PFIELD_NULL);
}

static void particle_settings_copy_data(Main *UNUSED(bmain),
                                        ID *id_dst,
                                        const ID *id_src,
                                        const int UNUSED(flag))
{
  ParticleSettings *particle_settings_dst = (ParticleSettings *)id_dst;
  const ParticleSettings *partticle_settings_src = (const ParticleSettings *)id_src;

  particle_settings_dst->pd = BKE_partdeflect_copy(partticle_settings_src->pd);
  particle_settings_dst->pd2 = BKE_partdeflect_copy(partticle_settings_src->pd2);
  particle_settings_dst->effector_weights = MEM_dupallocN(
      partticle_settings_src->effector_weights);
  particle_settings_dst->fluid = MEM_dupallocN(partticle_settings_src->fluid);

  if (partticle_settings_src->clumpcurve) {
    particle_settings_dst->clumpcurve = BKE_curvemapping_copy(partticle_settings_src->clumpcurve);
  }
  if (partticle_settings_src->roughcurve) {
    particle_settings_dst->roughcurve = BKE_curvemapping_copy(partticle_settings_src->roughcurve);
  }
  if (partticle_settings_src->twistcurve) {
    particle_settings_dst->twistcurve = BKE_curvemapping_copy(partticle_settings_src->twistcurve);
  }

  particle_settings_dst->boids = boid_copy_settings(partticle_settings_src->boids);

  for (int a = 0; a < MAX_MTEX; a++) {
    if (partticle_settings_src->mtex[a]) {
      particle_settings_dst->mtex[a] = MEM_dupallocN(partticle_settings_src->mtex[a]);
    }
  }

  BLI_duplicatelist(&particle_settings_dst->instance_weights,
                    &partticle_settings_src->instance_weights);
}

static void particle_settings_free_data(ID *id)
{
  ParticleSettings *particle_settings = (ParticleSettings *)id;

  for (int a = 0; a < MAX_MTEX; a++) {
    MEM_SAFE_FREE(particle_settings->mtex[a]);
  }

  if (particle_settings->clumpcurve) {
    BKE_curvemapping_free(particle_settings->clumpcurve);
  }
  if (particle_settings->roughcurve) {
    BKE_curvemapping_free(particle_settings->roughcurve);
  }
  if (particle_settings->twistcurve) {
    BKE_curvemapping_free(particle_settings->twistcurve);
  }

  BKE_partdeflect_free(particle_settings->pd);
  BKE_partdeflect_free(particle_settings->pd2);

  MEM_SAFE_FREE(particle_settings->effector_weights);

  BLI_freelistN(&particle_settings->instance_weights);

  boid_free_settings(particle_settings->boids);
  fluid_free_settings(particle_settings->fluid);
}

static void particle_settings_foreach_id(ID *id, LibraryForeachIDData *data)
{
  ParticleSettings *psett = (ParticleSettings *)id;
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->instance_collection, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->instance_object, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->bb_ob, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->collision_group, IDWALK_CB_NOP);

  for (int i = 0; i < MAX_MTEX; i++) {
    if (psett->mtex[i]) {
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data,
                                              BKE_texture_mtex_foreach_id(data, psett->mtex[i]));
    }
  }

  if (psett->effector_weights) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->effector_weights->group, IDWALK_CB_NOP);
  }

  if (psett->pd) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->pd->tex, IDWALK_CB_USER);
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->pd->f_source, IDWALK_CB_NOP);
  }
  if (psett->pd2) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->pd2->tex, IDWALK_CB_USER);
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, psett->pd2->f_source, IDWALK_CB_NOP);
  }

  if (psett->boids) {
    LISTBASE_FOREACH (BoidState *, state, &psett->boids->states) {
      LISTBASE_FOREACH (BoidRule *, rule, &state->rules) {
        if (rule->type == eBoidRuleType_Avoid) {
          BoidRuleGoalAvoid *gabr = (BoidRuleGoalAvoid *)rule;
          BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, gabr->ob, IDWALK_CB_NOP);
        }
        else if (rule->type == eBoidRuleType_FollowLeader) {
          BoidRuleFollowLeader *flbr = (BoidRuleFollowLeader *)rule;
          BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, flbr->ob, IDWALK_CB_NOP);
        }
      }
    }
  }

  LISTBASE_FOREACH (ParticleDupliWeight *, dw, &psett->instance_weights) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, dw->ob, IDWALK_CB_NOP);
  }
}

static void write_boid_state(BlendWriter *writer, BoidState *state)
{
  BLO_write_struct(writer, BoidState, state);

  LISTBASE_FOREACH (BoidRule *, rule, &state->rules) {
    switch (rule->type) {
      case eBoidRuleType_Goal:
      case eBoidRuleType_Avoid:
        BLO_write_struct(writer, BoidRuleGoalAvoid, rule);
        break;
      case eBoidRuleType_AvoidCollision:
        BLO_write_struct(writer, BoidRuleAvoidCollision, rule);
        break;
      case eBoidRuleType_FollowLeader:
        BLO_write_struct(writer, BoidRuleFollowLeader, rule);
        break;
      case eBoidRuleType_AverageSpeed:
        BLO_write_struct(writer, BoidRuleAverageSpeed, rule);
        break;
      case eBoidRuleType_Fight:
        BLO_write_struct(writer, BoidRuleFight, rule);
        break;
      default:
        BLO_write_struct(writer, BoidRule, rule);
        break;
    }
  }
#if 0
  BoidCondition *cond = state->conditions.first;
  for (; cond; cond = cond->next) {
    BLO_write_struct(writer, BoidCondition, cond);
  }
#endif
}

static void particle_settings_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  ParticleSettings *part = (ParticleSettings *)id;

  /* write LibData */
  BLO_write_id_struct(writer, ParticleSettings, id_address, &part->id);
  BKE_id_blend_write(writer, &part->id);

  if (part->adt) {
    BKE_animdata_blend_write(writer, part->adt);
  }
  BLO_write_struct(writer, PartDeflect, part->pd);
  BLO_write_struct(writer, PartDeflect, part->pd2);
  BLO_write_struct(writer, EffectorWeights, part->effector_weights);

  if (part->clumpcurve) {
    BKE_curvemapping_blend_write(writer, part->clumpcurve);
  }
  if (part->roughcurve) {
    BKE_curvemapping_blend_write(writer, part->roughcurve);
  }
  if (part->twistcurve) {
    BKE_curvemapping_blend_write(writer, part->twistcurve);
  }

  LISTBASE_FOREACH (ParticleDupliWeight *, dw, &part->instance_weights) {
    /* update indices, but only if dw->ob is set (can be NULL after loading e.g.) */
    if (dw->ob != NULL) {
      dw->index = 0;
      if (part->instance_collection) { /* can be NULL if lining fails or set to None */
        FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (part->instance_collection, object) {
          if (object == dw->ob) {
            break;
          }
          dw->index++;
        }
        FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
      }
    }
    BLO_write_struct(writer, ParticleDupliWeight, dw);
  }

  if (part->boids && part->phystype == PART_PHYS_BOIDS) {
    BLO_write_struct(writer, BoidSettings, part->boids);

    LISTBASE_FOREACH (BoidState *, state, &part->boids->states) {
      write_boid_state(writer, state);
    }
  }
  if (part->fluid && part->phystype == PART_PHYS_FLUID) {
    BLO_write_struct(writer, SPHFluidSettings, part->fluid);
  }

  for (int a = 0; a < MAX_MTEX; a++) {
    if (part->mtex[a]) {
      BLO_write_struct(writer, MTex, part->mtex[a]);
    }
  }
}

void BKE_particle_partdeflect_blend_read_data(BlendDataReader *UNUSED(reader), PartDeflect *pd)
{
  if (pd) {
    pd->rng = NULL;
  }
}

static void particle_settings_blend_read_data(BlendDataReader *reader, ID *id)
{
  ParticleSettings *part = (ParticleSettings *)id;
  BLO_read_data_address(reader, &part->adt);
  BLO_read_data_address(reader, &part->pd);
  BLO_read_data_address(reader, &part->pd2);

  BKE_animdata_blend_read_data(reader, part->adt);
  BKE_particle_partdeflect_blend_read_data(reader, part->pd);
  BKE_particle_partdeflect_blend_read_data(reader, part->pd2);

  BLO_read_data_address(reader, &part->clumpcurve);
  if (part->clumpcurve) {
    BKE_curvemapping_blend_read(reader, part->clumpcurve);
  }
  BLO_read_data_address(reader, &part->roughcurve);
  if (part->roughcurve) {
    BKE_curvemapping_blend_read(reader, part->roughcurve);
  }
  BLO_read_data_address(reader, &part->twistcurve);
  if (part->twistcurve) {
    BKE_curvemapping_blend_read(reader, part->twistcurve);
  }

  BLO_read_data_address(reader, &part->effector_weights);
  if (!part->effector_weights) {
    part->effector_weights = BKE_effector_add_weights(part->force_group);
  }

  BLO_read_list(reader, &part->instance_weights);

  BLO_read_data_address(reader, &part->boids);
  BLO_read_data_address(reader, &part->fluid);

  if (part->boids) {
    BLO_read_list(reader, &part->boids->states);

    LISTBASE_FOREACH (BoidState *, state, &part->boids->states) {
      BLO_read_list(reader, &state->rules);
      BLO_read_list(reader, &state->conditions);
      BLO_read_list(reader, &state->actions);
    }
  }
  for (int a = 0; a < MAX_MTEX; a++) {
    BLO_read_data_address(reader, &part->mtex[a]);
  }

  /* Protect against integer overflow vulnerability. */
  CLAMP(part->trail_count, 1, 100000);
}

void BKE_particle_partdeflect_blend_read_lib(BlendLibReader *reader, ID *id, PartDeflect *pd)
{
  if (pd && pd->tex) {
    BLO_read_id_address(reader, id->lib, &pd->tex);
  }
  if (pd && pd->f_source) {
    BLO_read_id_address(reader, id->lib, &pd->f_source);
  }
}

static void particle_settings_blend_read_lib(BlendLibReader *reader, ID *id)
{
  ParticleSettings *part = (ParticleSettings *)id;

  /* XXX: deprecated - old animation system. */
  BLO_read_id_address(reader, part->id.lib, &part->ipo);

  BLO_read_id_address(reader, part->id.lib, &part->instance_object);
  BLO_read_id_address(reader, part->id.lib, &part->instance_collection);
  BLO_read_id_address(reader, part->id.lib, &part->force_group);
  BLO_read_id_address(reader, part->id.lib, &part->bb_ob);
  BLO_read_id_address(reader, part->id.lib, &part->collision_group);

  BKE_particle_partdeflect_blend_read_lib(reader, &part->id, part->pd);
  BKE_particle_partdeflect_blend_read_lib(reader, &part->id, part->pd2);

  if (part->effector_weights) {
    BLO_read_id_address(reader, part->id.lib, &part->effector_weights->group);
  }
  else {
    part->effector_weights = BKE_effector_add_weights(part->force_group);
  }

  if (part->instance_weights.first && part->instance_collection) {
    LISTBASE_FOREACH (ParticleDupliWeight *, dw, &part->instance_weights) {
      BLO_read_id_address(reader, part->id.lib, &dw->ob);
    }
  }
  else {
    BLI_listbase_clear(&part->instance_weights);
  }

  if (part->boids) {
    LISTBASE_FOREACH (BoidState *, state, &part->boids->states) {
      LISTBASE_FOREACH (BoidRule *, rule, &state->rules) {
        switch (rule->type) {
          case eBoidRuleType_Goal:
          case eBoidRuleType_Avoid: {
            BoidRuleGoalAvoid *brga = (BoidRuleGoalAvoid *)rule;
            BLO_read_id_address(reader, part->id.lib, &brga->ob);
            break;
          }
          case eBoidRuleType_FollowLeader: {
            BoidRuleFollowLeader *brfl = (BoidRuleFollowLeader *)rule;
            BLO_read_id_address(reader, part->id.lib, &brfl->ob);
            break;
          }
        }
      }
    }
  }

  for (int a = 0; a < MAX_MTEX; a++) {
    MTex *mtex = part->mtex[a];
    if (mtex) {
      BLO_read_id_address(reader, part->id.lib, &mtex->tex);
      BLO_read_id_address(reader, part->id.lib, &mtex->object);
    }
  }
}

static void particle_settings_blend_read_expand(BlendExpander *expander, ID *id)
{
  ParticleSettings *part = (ParticleSettings *)id;
  BLO_expand(expander, part->instance_object);
  BLO_expand(expander, part->instance_collection);
  BLO_expand(expander, part->force_group);
  BLO_expand(expander, part->bb_ob);
  BLO_expand(expander, part->collision_group);

  for (int a = 0; a < MAX_MTEX; a++) {
    if (part->mtex[a]) {
      BLO_expand(expander, part->mtex[a]->tex);
      BLO_expand(expander, part->mtex[a]->object);
    }
  }

  if (part->effector_weights) {
    BLO_expand(expander, part->effector_weights->group);
  }

  if (part->pd) {
    BLO_expand(expander, part->pd->tex);
    BLO_expand(expander, part->pd->f_source);
  }
  if (part->pd2) {
    BLO_expand(expander, part->pd2->tex);
    BLO_expand(expander, part->pd2->f_source);
  }

  if (part->boids) {
    LISTBASE_FOREACH (BoidState *, state, &part->boids->states) {
      LISTBASE_FOREACH (BoidRule *, rule, &state->rules) {
        if (rule->type == eBoidRuleType_Avoid) {
          BoidRuleGoalAvoid *gabr = (BoidRuleGoalAvoid *)rule;
          BLO_expand(expander, gabr->ob);
        }
        else if (rule->type == eBoidRuleType_FollowLeader) {
          BoidRuleFollowLeader *flbr = (BoidRuleFollowLeader *)rule;
          BLO_expand(expander, flbr->ob);
        }
      }
    }
  }

  LISTBASE_FOREACH (ParticleDupliWeight *, dw, &part->instance_weights) {
    BLO_expand(expander, dw->ob);
  }
}

IDTypeInfo IDType_ID_PA = {
    .id_code = ID_PA,
    .id_filter = FILTER_ID_PA,
    .main_listbase_index = INDEX_ID_PA,
    .struct_size = sizeof(ParticleSettings),
    .name = "ParticleSettings",
    .name_plural = "particles",
    .translation_context = BLT_I18NCONTEXT_ID_PARTICLESETTINGS,
    .flags = 0,
    .asset_type_info = NULL,

    .init_data = particle_settings_init,
    .copy_data = particle_settings_copy_data,
    .free_data = particle_settings_free_data,
    .make_local = NULL,
    .foreach_id = particle_settings_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .blend_write = particle_settings_blend_write,
    .blend_read_data = particle_settings_blend_read_data,
    .blend_read_lib = particle_settings_blend_read_lib,
    .blend_read_expand = particle_settings_blend_read_expand,

    .blend_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

unsigned int PSYS_FRAND_SEED_OFFSET[PSYS_FRAND_COUNT];
unsigned int PSYS_FRAND_SEED_MULTIPLIER[PSYS_FRAND_COUNT];
float PSYS_FRAND_BASE[PSYS_FRAND_COUNT];

void BKE_particle_init_rng(void)
{
  RNG *rng = BLI_rng_new_srandom(5831); /* arbitrary */
  for (int i = 0; i < PSYS_FRAND_COUNT; i++) {
    PSYS_FRAND_BASE[i] = BLI_rng_get_float(rng);
    PSYS_FRAND_SEED_OFFSET[i] = (unsigned int)BLI_rng_get_int(rng);
    PSYS_FRAND_SEED_MULTIPLIER[i] = (unsigned int)BLI_rng_get_int(rng);
  }
  BLI_rng_free(rng);
}

static void get_child_modifier_parameters(ParticleSettings *part,
                                          ParticleThreadContext *ctx,
                                          ChildParticle *cpa,
                                          short cpa_from,
                                          int cpa_num,
                                          float *cpa_fuv,
                                          float *orco,
                                          ParticleTexture *ptex);
static void get_cpa_texture(Mesh *mesh,
                            ParticleSystem *psys,
                            ParticleSettings *part,
                            ParticleData *par,
                            int child_index,
                            int face_index,
                            const float fw[4],
                            float *orco,
                            ParticleTexture *ptex,
                            int event,
                            float cfra);

int count_particles(ParticleSystem *psys)
{
  ParticleSettings *part = psys->part;
  PARTICLE_P;
  int tot = 0;

  LOOP_SHOWN_PARTICLES
  {
    if (pa->alive == PARS_UNBORN && (part->flag & PART_UNBORN) == 0) {
    }
    else if (pa->alive == PARS_DEAD && (part->flag & PART_DIED) == 0) {
    }
    else {
      tot++;
    }
  }
  return tot;
}
int count_particles_mod(ParticleSystem *psys, int totgr, int cur)
{
  ParticleSettings *part = psys->part;
  PARTICLE_P;
  int tot = 0;

  LOOP_SHOWN_PARTICLES
  {
    if (pa->alive == PARS_UNBORN && (part->flag & PART_UNBORN) == 0) {
    }
    else if (pa->alive == PARS_DEAD && (part->flag & PART_DIED) == 0) {
    }
    else if (p % totgr == cur) {
      tot++;
    }
  }
  return tot;
}
/* We allocate path cache memory in chunks instead of a big contiguous
 * chunk, windows' memory allocator fails to find big blocks of memory often. */

#define PATH_CACHE_BUF_SIZE 1024

static ParticleCacheKey *pcache_key_segment_endpoint_safe(ParticleCacheKey *key)
{
  return (key->segments > 0) ? (key + (key->segments - 1)) : key;
}

static ParticleCacheKey **psys_alloc_path_cache_buffers(ListBase *bufs, int tot, int totkeys)
{
  LinkData *buf;
  ParticleCacheKey **cache;
  int i, totkey, totbufkey;

  tot = MAX2(tot, 1);
  totkey = 0;
  cache = MEM_callocN(tot * sizeof(void *), "PathCacheArray");

  while (totkey < tot) {
    totbufkey = MIN2(tot - totkey, PATH_CACHE_BUF_SIZE);
    buf = MEM_callocN(sizeof(LinkData), "PathCacheLinkData");
    buf->data = MEM_callocN(sizeof(ParticleCacheKey) * totbufkey * totkeys, "ParticleCacheKey");

    for (i = 0; i < totbufkey; i++) {
      cache[totkey + i] = ((ParticleCacheKey *)buf->data) + i * totkeys;
    }

    totkey += totbufkey;
    BLI_addtail(bufs, buf);
  }

  return cache;
}

static void psys_free_path_cache_buffers(ParticleCacheKey **cache, ListBase *bufs)
{
  LinkData *buf;

  if (cache) {
    MEM_freeN(cache);
  }

  for (buf = bufs->first; buf; buf = buf->next) {
    MEM_freeN(buf->data);
  }
  BLI_freelistN(bufs);
}

/************************************************/
/*          Getting stuff                       */
/************************************************/

ParticleSystem *psys_get_current(Object *ob)
{
  ParticleSystem *psys;
  if (ob == NULL) {
    return NULL;
  }

  for (psys = ob->particlesystem.first; psys; psys = psys->next) {
    if (psys->flag & PSYS_CURRENT) {
      return psys;
    }
  }

  return NULL;
}
short psys_get_current_num(Object *ob)
{
  ParticleSystem *psys;
  short i;

  if (ob == NULL) {
    return 0;
  }

  for (psys = ob->particlesystem.first, i = 0; psys; psys = psys->next, i++) {
    if (psys->flag & PSYS_CURRENT) {
      return i;
    }
  }

  return i;
}
void psys_set_current_num(Object *ob, int index)
{
  ParticleSystem *psys;
  short i;

  if (ob == NULL) {
    return;
  }

  for (psys = ob->particlesystem.first, i = 0; psys; psys = psys->next, i++) {
    if (i == index) {
      psys->flag |= PSYS_CURRENT;
    }
    else {
      psys->flag &= ~PSYS_CURRENT;
    }
  }
}

struct LatticeDeformData *psys_create_lattice_deform_data(ParticleSimulationData *sim)
{
  struct LatticeDeformData *lattice_deform_data = NULL;

  if (psys_in_edit_mode(sim->depsgraph, sim->psys) == 0) {
    Object *lattice = NULL;
    ModifierData *md = (ModifierData *)psys_get_modifier(sim->ob, sim->psys);
    bool for_render = DEG_get_mode(sim->depsgraph) == DAG_EVAL_RENDER;
    int mode = for_render ? eModifierMode_Render : eModifierMode_Realtime;

    for (; md; md = md->next) {
      if (md->type == eModifierType_Lattice) {
        if (md->mode & mode) {
          LatticeModifierData *lmd = (LatticeModifierData *)md;
          lattice = lmd->object;
          sim->psys->lattice_strength = lmd->strength;
        }

        break;
      }
    }
    if (lattice) {
      lattice_deform_data = BKE_lattice_deform_data_create(lattice, NULL);
    }
  }

  return lattice_deform_data;
}
void psys_disable_all(Object *ob)
{
  ParticleSystem *psys = ob->particlesystem.first;

  for (; psys; psys = psys->next) {
    psys->flag |= PSYS_DISABLED;
  }
}
void psys_enable_all(Object *ob)
{
  ParticleSystem *psys = ob->particlesystem.first;

  for (; psys; psys = psys->next) {
    psys->flag &= ~PSYS_DISABLED;
  }
}

ParticleSystem *psys_orig_get(ParticleSystem *psys)
{
  if (psys->orig_psys == NULL) {
    return psys;
  }
  return psys->orig_psys;
}

struct ParticleSystem *psys_eval_get(Depsgraph *depsgraph, Object *object, ParticleSystem *psys)
{
  Object *object_eval = DEG_get_evaluated_object(depsgraph, object);
  if (object_eval == object) {
    return psys;
  }
  ParticleSystem *psys_eval = object_eval->particlesystem.first;
  while (psys_eval != NULL) {
    if (psys_eval->orig_psys == psys) {
      return psys_eval;
    }
    psys_eval = psys_eval->next;
  }
  return psys_eval;
}

static PTCacheEdit *psys_orig_edit_get(ParticleSystem *psys)
{
  if (psys->orig_psys == NULL) {
    return psys->edit;
  }
  return psys->orig_psys->edit;
}

bool psys_in_edit_mode(Depsgraph *depsgraph, const ParticleSystem *psys)
{
  const ViewLayer *view_layer = DEG_get_input_view_layer(depsgraph);
  if (view_layer->basact == NULL) {
    /* TODO(sergey): Needs double-check with multi-object edit. */
    return false;
  }
  const bool use_render_params = (DEG_get_mode(depsgraph) == DAG_EVAL_RENDER);
  const Object *object = view_layer->basact->object;
  if (object->mode != OB_MODE_PARTICLE_EDIT) {
    return false;
  }
  const ParticleSystem *psys_orig = psys_orig_get((ParticleSystem *)psys);
  return (psys_orig->edit || psys->pointcache->edit) && (use_render_params == false);
}

bool psys_check_enabled(Object *ob, ParticleSystem *psys, const bool use_render_params)
{
  ParticleSystemModifierData *psmd;

  if (psys->flag & PSYS_DISABLED || psys->flag & PSYS_DELETE || !psys->part) {
    return 0;
  }

  psmd = psys_get_modifier(ob, psys);

  if (!psmd) {
    return 0;
  }

  if (use_render_params) {
    if (!(psmd->modifier.mode & eModifierMode_Render)) {
      return 0;
    }
  }
  else if (!(psmd->modifier.mode & eModifierMode_Realtime)) {
    return 0;
  }

  return 1;
}

bool psys_check_edited(ParticleSystem *psys)
{
  if (psys->part && psys->part->type == PART_HAIR) {
    return (psys->flag & PSYS_EDITED || (psys->edit && psys->edit->edited));
  }

  return (psys->pointcache->edit && psys->pointcache->edit->edited);
}

void psys_find_group_weights(ParticleSettings *part)
{
  /* Find object pointers based on index. If the collection is linked from
   * another library linking may not have the object pointers available on
   * file load, so we have to retrieve them later. See T49273. */
  ListBase instance_collection_objects = {NULL, NULL};

  if (part->instance_collection) {
    instance_collection_objects = BKE_collection_object_cache_get(part->instance_collection);
  }

  LISTBASE_FOREACH (ParticleDupliWeight *, dw, &part->instance_weights) {
    if (dw->ob == NULL) {
      Base *base = BLI_findlink(&instance_collection_objects, dw->index);
      if (base != NULL) {
        dw->ob = base->object;
      }
    }
  }
}

void psys_check_group_weights(ParticleSettings *part)
{
  ParticleDupliWeight *dw, *tdw;

  if (part->ren_as != PART_DRAW_GR || !part->instance_collection) {
    BLI_freelistN(&part->instance_weights);
    return;
  }

  /* Find object pointers. */
  psys_find_group_weights(part);

  /* Remove NULL objects, that were removed from the collection. */
  dw = part->instance_weights.first;
  while (dw) {
    if (dw->ob == NULL ||
        !BKE_collection_has_object_recursive(part->instance_collection, dw->ob)) {
      tdw = dw->next;
      BLI_freelinkN(&part->instance_weights, dw);
      dw = tdw;
    }
    else {
      dw = dw->next;
    }
  }

  /* Add new objects in the collection. */
  int index = 0;
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (part->instance_collection, object) {
    dw = part->instance_weights.first;
    while (dw && dw->ob != object) {
      dw = dw->next;
    }

    if (!dw) {
      dw = MEM_callocN(sizeof(ParticleDupliWeight), "ParticleDupliWeight");
      dw->ob = object;
      dw->count = 1;
      BLI_addtail(&part->instance_weights, dw);
    }

    dw->index = index++;
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

  /* Ensure there is an element marked as current. */
  int current = 0;
  for (dw = part->instance_weights.first; dw; dw = dw->next) {
    if (dw->flag & PART_DUPLIW_CURRENT) {
      current = 1;
      break;
    }
  }

  if (!current) {
    dw = part->instance_weights.first;
    if (dw) {
      dw->flag |= PART_DUPLIW_CURRENT;
    }
  }
}

int psys_uses_gravity(ParticleSimulationData *sim)
{
  return sim->scene->physics_settings.flag & PHYS_GLOBAL_GRAVITY && sim->psys->part &&
         sim->psys->part->effector_weights->global_gravity != 0.0f;
}

/************************************************/
/*          Freeing stuff                       */
/************************************************/

static void fluid_free_settings(SPHFluidSettings *fluid)
{
  if (fluid) {
    MEM_freeN(fluid);
  }
}

void free_hair(Object *object, ParticleSystem *psys, int dynamics)
{
  PARTICLE_P;

  LOOP_PARTICLES
  {
    MEM_SAFE_FREE(pa->hair);
    pa->totkey = 0;
  }

  psys->flag &= ~PSYS_HAIR_DONE;

  if (psys->clmd) {
    if (dynamics) {
      BKE_modifier_free((ModifierData *)psys->clmd);
      psys->clmd = NULL;
      PTCacheID pid;
      BKE_ptcache_id_from_particles(&pid, object, psys);
      BKE_ptcache_id_clear(&pid, PTCACHE_CLEAR_ALL, 0);
    }
    else {
      cloth_free_modifier(psys->clmd);
    }
  }

  if (psys->hair_in_mesh) {
    BKE_id_free(NULL, psys->hair_in_mesh);
  }
  psys->hair_in_mesh = NULL;

  if (psys->hair_out_mesh) {
    BKE_id_free(NULL, psys->hair_out_mesh);
  }
  psys->hair_out_mesh = NULL;
}
void free_keyed_keys(ParticleSystem *psys)
{
  PARTICLE_P;

  if (psys->part->type == PART_HAIR) {
    return;
  }

  if (psys->particles && psys->particles->keys) {
    MEM_freeN(psys->particles->keys);

    LOOP_PARTICLES
    {
      if (pa->keys) {
        pa->keys = NULL;
        pa->totkey = 0;
      }
    }
  }
}
static void free_child_path_cache(ParticleSystem *psys)
{
  psys_free_path_cache_buffers(psys->childcache, &psys->childcachebufs);
  psys->childcache = NULL;
  psys->totchildcache = 0;
}
void psys_free_path_cache(ParticleSystem *psys, PTCacheEdit *edit)
{
  if (edit) {
    psys_free_path_cache_buffers(edit->pathcache, &edit->pathcachebufs);
    edit->pathcache = NULL;
    edit->totcached = 0;
  }
  if (psys) {
    psys_free_path_cache_buffers(psys->pathcache, &psys->pathcachebufs);
    psys->pathcache = NULL;
    psys->totcached = 0;

    free_child_path_cache(psys);
  }
}
void psys_free_children(ParticleSystem *psys)
{
  if (psys->child) {
    MEM_freeN(psys->child);
    psys->child = NULL;
    psys->totchild = 0;
  }

  free_child_path_cache(psys);
}
void psys_free_particles(ParticleSystem *psys)
{
  PARTICLE_P;

  if (psys->particles) {
    /* Even though psys->part should never be NULL,
     * this can happen as an exception during deletion.
     * See ID_REMAP_SKIP/FORCE/FLAG_NEVER_NULL_USAGE in BKE_library_remap. */
    if (psys->part && psys->part->type == PART_HAIR) {
      LOOP_PARTICLES
      {
        if (pa->hair) {
          MEM_freeN(pa->hair);
        }
      }
    }

    if (psys->particles->keys) {
      MEM_freeN(psys->particles->keys);
    }

    if (psys->particles->boid) {
      MEM_freeN(psys->particles->boid);
    }

    MEM_freeN(psys->particles);
    psys->particles = NULL;
    psys->totpart = 0;
  }
}
void psys_free_pdd(ParticleSystem *psys)
{
  if (psys->pdd) {
    MEM_SAFE_FREE(psys->pdd->cdata);

    MEM_SAFE_FREE(psys->pdd->vdata);

    MEM_SAFE_FREE(psys->pdd->ndata);

    MEM_SAFE_FREE(psys->pdd->vedata);

    psys->pdd->totpoint = 0;
    psys->pdd->totpart = 0;
    psys->pdd->partsize = 0;
  }
}
void psys_free(Object *ob, ParticleSystem *psys)
{
  if (psys) {
    int nr = 0;
    ParticleSystem *tpsys;

    psys_free_path_cache(psys, NULL);

    /* NOTE: We pass dynamics=0 to free_hair() to prevent it from doing an
     * unneeded clear of the cache. But for historical reason that code path
     * was only clearing cloth part of modifier data.
     *
     * Part of the story there is that particle evaluation is trying to not
     * re-allocate thew ModifierData itself, and limits all allocations to
     * the cloth part of it.
     *
     * Why evaluation is relying on hair_free() and in some specific code
     * paths there is beyond me.
     */
    free_hair(ob, psys, 0);
    if (psys->clmd != NULL) {
      BKE_modifier_free((ModifierData *)psys->clmd);
    }

    psys_free_particles(psys);

    if (psys->edit && psys->free_edit) {
      psys->free_edit(psys->edit);
    }

    if (psys->child) {
      MEM_freeN(psys->child);
      psys->child = NULL;
      psys->totchild = 0;
    }

    /* check if we are last non-visible particle system */
    for (tpsys = ob->particlesystem.first; tpsys; tpsys = tpsys->next) {
      if (tpsys->part) {
        if (ELEM(tpsys->part->ren_as, PART_DRAW_OB, PART_DRAW_GR)) {
          nr++;
          break;
        }
      }
    }
    /* clear do-not-draw-flag */
    if (!nr) {
      ob->transflag &= ~OB_DUPLIPARTS;
    }

    psys->part = NULL;

    if ((psys->flag & PSYS_SHARED_CACHES) == 0) {
      BKE_ptcache_free_list(&psys->ptcaches);
    }
    psys->pointcache = NULL;

    BLI_freelistN(&psys->targets);

    BLI_bvhtree_free(psys->bvhtree);
    BLI_kdtree_3d_free(psys->tree);

    if (psys->fluid_springs) {
      MEM_freeN(psys->fluid_springs);
    }

    BKE_effectors_free(psys->effectors);

    if (psys->pdd) {
      psys_free_pdd(psys);
      MEM_freeN(psys->pdd);
    }

    BKE_particle_batch_cache_free(psys);

    MEM_freeN(psys);
  }
}

void psys_copy_particles(ParticleSystem *psys_dst, ParticleSystem *psys_src)
{
  /* Free existing particles. */
  if (psys_dst->particles != psys_src->particles) {
    psys_free_particles(psys_dst);
  }
  if (psys_dst->child != psys_src->child) {
    psys_free_children(psys_dst);
  }
  /* Restore counters. */
  psys_dst->totpart = psys_src->totpart;
  psys_dst->totchild = psys_src->totchild;
  /* Copy particles and children. */
  psys_dst->particles = MEM_dupallocN(psys_src->particles);
  psys_dst->child = MEM_dupallocN(psys_src->child);

  /* Ideally this should only be performed if `(psys_dst->part->type == PART_HAIR)`.
   *
   * But #ParticleData (`psys_dst`) is some sub-data of the #Object ID, while #ParticleSettings
   * (`psys_dst->part`) is another ID. In case the particle settings is a linked ID that gets
   * missing, it will be replaced (in readfile code) by a place-holder, which defaults to a
   * `PART_EMITTER` type of particle settings.
   *
   * This leads to a situation where each particle of `psys_dst` still has a valid allocated `hair`
   * data, which should still be preserved in case the missing particle settings ID becomes valid
   * again.
   *
   * Furthermore, #free_hair() always frees `pa->hair` if it's not NULL, regardless of the
   * particle type. So *not* copying here would cause a double free (or more), e.g. freeing the
   * copy-on-write copy and the original data will crash Blender.
   * In any case, sharing pointers between `psys_src` and `psys_dst` should be forbidden.
   *
   * So while we could in theory 'sanitize' the situation by setting `pa->hair` to NULL in the new
   * copy (in case of non-`PART_HAIR` type), it is probably safer for now to systematically
   * duplicate the `hair` data if available. */
  {
    ParticleData *pa;
    int p;
    for (p = 0, pa = psys_dst->particles; p < psys_dst->totpart; p++, pa++) {
      pa->hair = MEM_dupallocN(pa->hair);
    }
  }
  if (psys_dst->particles && (psys_dst->particles->keys || psys_dst->particles->boid)) {
    ParticleKey *key = psys_dst->particles->keys;
    BoidParticle *boid = psys_dst->particles->boid;
    ParticleData *pa;
    int p;
    if (key != NULL) {
      key = MEM_dupallocN(key);
    }
    if (boid != NULL) {
      boid = MEM_dupallocN(boid);
    }
    for (p = 0, pa = psys_dst->particles; p < psys_dst->totpart; p++, pa++) {
      if (boid != NULL) {
        pa->boid = boid++;
      }
      if (key != NULL) {
        pa->keys = key;
        key += pa->totkey;
      }
    }
  }
}

/************************************************/
/*          Interpolation                       */
/************************************************/

static float interpolate_particle_value(
    float v1, float v2, float v3, float v4, const float w[4], int four)
{
  float value;

  value = w[0] * v1 + w[1] * v2 + w[2] * v3;
  if (four) {
    value += w[3] * v4;
  }

  CLAMP(value, 0.0f, 1.0f);

  return value;
}

void psys_interpolate_particle(
    short type, ParticleKey keys[4], float dt, ParticleKey *result, bool velocity)
{
  float t[4];

  if (type < 0) {
    interp_cubic_v3(result->co, result->vel, keys[1].co, keys[1].vel, keys[2].co, keys[2].vel, dt);
  }
  else {
    key_curve_position_weights(dt, t, type);

    interp_v3_v3v3v3v3(result->co, keys[0].co, keys[1].co, keys[2].co, keys[3].co, t);

    if (velocity) {
      float temp[3];

      if (dt > 0.999f) {
        key_curve_position_weights(dt - 0.001f, t, type);
        interp_v3_v3v3v3v3(temp, keys[0].co, keys[1].co, keys[2].co, keys[3].co, t);
        sub_v3_v3v3(result->vel, result->co, temp);
      }
      else {
        key_curve_position_weights(dt + 0.001f, t, type);
        interp_v3_v3v3v3v3(temp, keys[0].co, keys[1].co, keys[2].co, keys[3].co, t);
        sub_v3_v3v3(result->vel, temp, result->co);
      }
    }
  }
}

typedef struct ParticleInterpolationData {
  HairKey *hkey[2];

  Mesh *mesh;
  MVert *mvert[2];

  int keyed;
  ParticleKey *kkey[2];

  PointCache *cache;
  PTCacheMem *pm;

  PTCacheEditPoint *epoint;
  PTCacheEditKey *ekey[2];

  float birthtime, dietime;
  int bspline;
} ParticleInterpolationData;
/**
 * Assumes pointcache->mem_cache exists, so for disk cached particles
 * call #psys_make_temp_pointcache() before use.
 * It uses #ParticleInterpolationData.pm to store the current memory cache frame
 * so it's thread safe.
 */
static void get_pointcache_keys_for_time(Object *UNUSED(ob),
                                         PointCache *cache,
                                         PTCacheMem **cur,
                                         int index,
                                         float t,
                                         ParticleKey *key1,
                                         ParticleKey *key2)
{
  static PTCacheMem *pm = NULL;
  int index1, index2;

  if (index < 0) { /* initialize */
    *cur = cache->mem_cache.first;

    if (*cur) {
      *cur = (*cur)->next;
    }
  }
  else {
    if (*cur) {
      while (*cur && (*cur)->next && (float)(*cur)->frame < t) {
        *cur = (*cur)->next;
      }

      pm = *cur;

      index2 = BKE_ptcache_mem_index_find(pm, index);
      index1 = BKE_ptcache_mem_index_find(pm->prev, index);
      if (index2 < 0) {
        return;
      }

      BKE_ptcache_make_particle_key(key2, index2, pm->data, (float)pm->frame);
      if (index1 < 0) {
        copy_particle_key(key1, key2, 1);
      }
      else {
        BKE_ptcache_make_particle_key(key1, index1, pm->prev->data, (float)pm->prev->frame);
      }
    }
    else if (cache->mem_cache.first) {
      pm = cache->mem_cache.first;
      index2 = BKE_ptcache_mem_index_find(pm, index);
      if (index2 < 0) {
        return;
      }
      BKE_ptcache_make_particle_key(key2, index2, pm->data, (float)pm->frame);
      copy_particle_key(key1, key2, 1);
    }
  }
}
static int get_pointcache_times_for_particle(PointCache *cache,
                                             int index,
                                             float *start,
                                             float *end)
{
  PTCacheMem *pm;
  int ret = 0;

  for (pm = cache->mem_cache.first; pm; pm = pm->next) {
    if (BKE_ptcache_mem_index_find(pm, index) >= 0) {
      *start = pm->frame;
      ret++;
      break;
    }
  }

  for (pm = cache->mem_cache.last; pm; pm = pm->prev) {
    if (BKE_ptcache_mem_index_find(pm, index) >= 0) {
      *end = pm->frame;
      ret++;
      break;
    }
  }

  return ret == 2;
}

float psys_get_dietime_from_cache(PointCache *cache, int index)
{
  PTCacheMem *pm;
  int dietime = 10000000; /* some max value so that we can default to pa->time+lifetime */

  for (pm = cache->mem_cache.last; pm; pm = pm->prev) {
    if (BKE_ptcache_mem_index_find(pm, index) >= 0) {
      return (float)pm->frame;
    }
  }

  return (float)dietime;
}

static void init_particle_interpolation(Object *ob,
                                        ParticleSystem *psys,
                                        ParticleData *pa,
                                        ParticleInterpolationData *pind)
{

  if (pind->epoint) {
    PTCacheEditPoint *point = pind->epoint;

    pind->ekey[0] = point->keys;
    pind->ekey[1] = point->totkey > 1 ? point->keys + 1 : NULL;

    pind->birthtime = *(point->keys->time);
    pind->dietime = *((point->keys + point->totkey - 1)->time);
  }
  else if (pind->keyed) {
    ParticleKey *key = pa->keys;
    pind->kkey[0] = key;
    pind->kkey[1] = pa->totkey > 1 ? key + 1 : NULL;

    pind->birthtime = key->time;
    pind->dietime = (key + pa->totkey - 1)->time;
  }
  else if (pind->cache) {
    float start = 0.0f, end = 0.0f;
    get_pointcache_keys_for_time(ob, pind->cache, &pind->pm, -1, 0.0f, NULL, NULL);
    pind->birthtime = pa ? pa->time : pind->cache->startframe;
    pind->dietime = pa ? pa->dietime : pind->cache->endframe;

    if (get_pointcache_times_for_particle(pind->cache, pa - psys->particles, &start, &end)) {
      pind->birthtime = MAX2(pind->birthtime, start);
      pind->dietime = MIN2(pind->dietime, end);
    }
  }
  else {
    HairKey *key = pa->hair;
    pind->hkey[0] = key;
    pind->hkey[1] = key + 1;

    pind->birthtime = key->time;
    pind->dietime = (key + pa->totkey - 1)->time;

    if (pind->mesh) {
      pind->mvert[0] = &pind->mesh->mvert[pa->hair_index];
      pind->mvert[1] = pind->mvert[0] + 1;
    }
  }
}
static void edit_to_particle(ParticleKey *key, PTCacheEditKey *ekey)
{
  copy_v3_v3(key->co, ekey->co);
  if (ekey->vel) {
    copy_v3_v3(key->vel, ekey->vel);
  }
  key->time = *(ekey->time);
}
static void hair_to_particle(ParticleKey *key, HairKey *hkey)
{
  copy_v3_v3(key->co, hkey->co);
  key->time = hkey->time;
}

static void mvert_to_particle(ParticleKey *key, MVert *mvert, HairKey *hkey)
{
  copy_v3_v3(key->co, mvert->co);
  key->time = hkey->time;
}

static void do_particle_interpolation(ParticleSystem *psys,
                                      int p,
                                      ParticleData *pa,
                                      float t,
                                      ParticleInterpolationData *pind,
                                      ParticleKey *result)
{
  PTCacheEditPoint *point = pind->epoint;
  ParticleKey keys[4];
  int point_vel = (point && point->keys->vel);
  float real_t, dfra, keytime, invdt = 1.0f;

  /* billboards won't fill in all of these, so start cleared */
  memset(keys, 0, sizeof(keys));

  /* interpret timing and find keys */
  if (point) {
    if (result->time < 0.0f) {
      real_t = -result->time;
    }
    else {
      real_t = *(pind->ekey[0]->time) +
               t * (*(pind->ekey[0][point->totkey - 1].time) - *(pind->ekey[0]->time));
    }

    while (*(pind->ekey[1]->time) < real_t) {
      pind->ekey[1]++;
    }

    pind->ekey[0] = pind->ekey[1] - 1;
  }
  else if (pind->keyed) {
    /* we have only one key, so let's use that */
    if (pind->kkey[1] == NULL) {
      copy_particle_key(result, pind->kkey[0], 1);
      return;
    }

    if (result->time < 0.0f) {
      real_t = -result->time;
    }
    else {
      real_t = pind->kkey[0]->time +
               t * (pind->kkey[0][pa->totkey - 1].time - pind->kkey[0]->time);
    }

    if (psys->part->phystype == PART_PHYS_KEYED && psys->flag & PSYS_KEYED_TIMING) {
      ParticleTarget *pt = psys->targets.first;

      pt = pt->next;

      while (pt && pa->time + pt->time < real_t) {
        pt = pt->next;
      }

      if (pt) {
        pt = pt->prev;

        if (pa->time + pt->time + pt->duration > real_t) {
          real_t = pa->time + pt->time;
        }
      }
      else {
        real_t = pa->time + ((ParticleTarget *)psys->targets.last)->time;
      }
    }

    CLAMP(real_t, pa->time, pa->dietime);

    while (pind->kkey[1]->time < real_t) {
      pind->kkey[1]++;
    }

    pind->kkey[0] = pind->kkey[1] - 1;
  }
  else if (pind->cache) {
    if (result->time < 0.0f) { /* flag for time in frames */
      real_t = -result->time;
    }
    else {
      real_t = pa->time + t * (pa->dietime - pa->time);
    }
  }
  else {
    if (result->time < 0.0f) {
      real_t = -result->time;
    }
    else {
      real_t = pind->hkey[0]->time +
               t * (pind->hkey[0][pa->totkey - 1].time - pind->hkey[0]->time);
    }

    while (pind->hkey[1]->time < real_t) {
      pind->hkey[1]++;
      pind->mvert[1]++;
    }

    pind->hkey[0] = pind->hkey[1] - 1;
  }

  /* set actual interpolation keys */
  if (point) {
    edit_to_particle(keys + 1, pind->ekey[0]);
    edit_to_particle(keys + 2, pind->ekey[1]);
  }
  else if (pind->mesh) {
    pind->mvert[0] = pind->mvert[1] - 1;
    mvert_to_particle(keys + 1, pind->mvert[0], pind->hkey[0]);
    mvert_to_particle(keys + 2, pind->mvert[1], pind->hkey[1]);
  }
  else if (pind->keyed) {
    memcpy(keys + 1, pind->kkey[0], sizeof(ParticleKey));
    memcpy(keys + 2, pind->kkey[1], sizeof(ParticleKey));
  }
  else if (pind->cache) {
    get_pointcache_keys_for_time(NULL, pind->cache, &pind->pm, p, real_t, keys + 1, keys + 2);
  }
  else {
    hair_to_particle(keys + 1, pind->hkey[0]);
    hair_to_particle(keys + 2, pind->hkey[1]);
  }

  /* set secondary interpolation keys for hair */
  if (!pind->keyed && !pind->cache && !point_vel) {
    if (point) {
      if (pind->ekey[0] != point->keys) {
        edit_to_particle(keys, pind->ekey[0] - 1);
      }
      else {
        edit_to_particle(keys, pind->ekey[0]);
      }
    }
    else if (pind->mesh) {
      if (pind->hkey[0] != pa->hair) {
        mvert_to_particle(keys, pind->mvert[0] - 1, pind->hkey[0] - 1);
      }
      else {
        mvert_to_particle(keys, pind->mvert[0], pind->hkey[0]);
      }
    }
    else {
      if (pind->hkey[0] != pa->hair) {
        hair_to_particle(keys, pind->hkey[0] - 1);
      }
      else {
        hair_to_particle(keys, pind->hkey[0]);
      }
    }

    if (point) {
      if (pind->ekey[1] != point->keys + point->totkey - 1) {
        edit_to_particle(keys + 3, pind->ekey[1] + 1);
      }
      else {
        edit_to_particle(keys + 3, pind->ekey[1]);
      }
    }
    else if (pind->mesh) {
      if (pind->hkey[1] != pa->hair + pa->totkey - 1) {
        mvert_to_particle(keys + 3, pind->mvert[1] + 1, pind->hkey[1] + 1);
      }
      else {
        mvert_to_particle(keys + 3, pind->mvert[1], pind->hkey[1]);
      }
    }
    else {
      if (pind->hkey[1] != pa->hair + pa->totkey - 1) {
        hair_to_particle(keys + 3, pind->hkey[1] + 1);
      }
      else {
        hair_to_particle(keys + 3, pind->hkey[1]);
      }
    }
  }

  dfra = keys[2].time - keys[1].time;
  keytime = (real_t - keys[1].time) / dfra;

  /* Convert velocity to time-step size. */
  if (pind->keyed || pind->cache || point_vel) {
    invdt = dfra * 0.04f * (psys ? psys->part->timetweak : 1.0f);
    mul_v3_fl(keys[1].vel, invdt);
    mul_v3_fl(keys[2].vel, invdt);
    interp_qt_qtqt(result->rot, keys[1].rot, keys[2].rot, keytime);
  }

  /* Now we should have in chronological order k1<=k2<=t<=k3<=k4 with key-time between
   * [0, 1]->[k2, k3] (k1 & k4 used for cardinal & b-spline interpolation). */
  psys_interpolate_particle((pind->keyed || pind->cache || point_vel) ?
                                -1 /* signal for cubic interpolation */
                                :
                                (pind->bspline ? KEY_BSPLINE : KEY_CARDINAL),
                            keys,
                            keytime,
                            result,
                            1);

  /* the velocity needs to be converted back from cubic interpolation */
  if (pind->keyed || pind->cache || point_vel) {
    mul_v3_fl(result->vel, 1.0f / invdt);
  }
}

static void interpolate_pathcache(ParticleCacheKey *first, float t, ParticleCacheKey *result)
{
  int i = 0;
  ParticleCacheKey *cur = first;

  /* scale the requested time to fit the entire path even if the path is cut early */
  t *= (first + first->segments)->time;

  while (i < first->segments && cur->time < t) {
    cur++;
  }

  if (cur->time == t) {
    *result = *cur;
  }
  else {
    float dt = (t - (cur - 1)->time) / (cur->time - (cur - 1)->time);
    interp_v3_v3v3(result->co, (cur - 1)->co, cur->co, dt);
    interp_v3_v3v3(result->vel, (cur - 1)->vel, cur->vel, dt);
    interp_qt_qtqt(result->rot, (cur - 1)->rot, cur->rot, dt);
    result->time = t;
  }

  /* first is actual base rotation, others are incremental from first */
  if (cur == first || cur - 1 == first) {
    copy_qt_qt(result->rot, first->rot);
  }
  else {
    mul_qt_qtqt(result->rot, first->rot, result->rot);
  }
}

/************************************************/
/*          Particles on a dm                   */
/************************************************/

void psys_interpolate_face(Mesh *mesh,
                           MVert *mvert,
                           const float (*vert_normals)[3],
                           MFace *mface,
                           MTFace *tface,
                           float (*orcodata)[3],
                           float w[4],
                           float vec[3],
                           float nor[3],
                           float utan[3],
                           float vtan[3],
                           float orco[3])
{
  float *v1 = 0, *v2 = 0, *v3 = 0, *v4 = 0;
  float e1[3], e2[3], s1, s2, t1, t2;
  float *uv1, *uv2, *uv3, *uv4;
  float n1[3], n2[3], n3[3], n4[3];
  float tuv[4][2];
  float *o1, *o2, *o3, *o4;

  v1 = mvert[mface->v1].co;
  v2 = mvert[mface->v2].co;
  v3 = mvert[mface->v3].co;

  copy_v3_v3(n1, vert_normals[mface->v1]);
  copy_v3_v3(n2, vert_normals[mface->v2]);
  copy_v3_v3(n3, vert_normals[mface->v3]);

  if (mface->v4) {
    v4 = mvert[mface->v4].co;
    copy_v3_v3(n4, vert_normals[mface->v4]);

    interp_v3_v3v3v3v3(vec, v1, v2, v3, v4, w);

    if (nor) {
      if (mface->flag & ME_SMOOTH) {
        interp_v3_v3v3v3v3(nor, n1, n2, n3, n4, w);
      }
      else {
        normal_quad_v3(nor, v1, v2, v3, v4);
      }
    }
  }
  else {
    interp_v3_v3v3v3(vec, v1, v2, v3, w);

    if (nor) {
      if (mface->flag & ME_SMOOTH) {
        interp_v3_v3v3v3(nor, n1, n2, n3, w);
      }
      else {
        normal_tri_v3(nor, v1, v2, v3);
      }
    }
  }

  /* calculate tangent vectors */
  if (utan && vtan) {
    if (tface) {
      uv1 = tface->uv[0];
      uv2 = tface->uv[1];
      uv3 = tface->uv[2];
      uv4 = tface->uv[3];
    }
    else {
      uv1 = tuv[0];
      uv2 = tuv[1];
      uv3 = tuv[2];
      uv4 = tuv[3];
      map_to_sphere(uv1, uv1 + 1, v1[0], v1[1], v1[2]);
      map_to_sphere(uv2, uv2 + 1, v2[0], v2[1], v2[2]);
      map_to_sphere(uv3, uv3 + 1, v3[0], v3[1], v3[2]);
      if (v4) {
        map_to_sphere(uv4, uv4 + 1, v4[0], v4[1], v4[2]);
      }
    }

    if (v4) {
      s1 = uv3[0] - uv1[0];
      s2 = uv4[0] - uv1[0];

      t1 = uv3[1] - uv1[1];
      t2 = uv4[1] - uv1[1];

      sub_v3_v3v3(e1, v3, v1);
      sub_v3_v3v3(e2, v4, v1);
    }
    else {
      s1 = uv2[0] - uv1[0];
      s2 = uv3[0] - uv1[0];

      t1 = uv2[1] - uv1[1];
      t2 = uv3[1] - uv1[1];

      sub_v3_v3v3(e1, v2, v1);
      sub_v3_v3v3(e2, v3, v1);
    }

    vtan[0] = (s1 * e2[0] - s2 * e1[0]);
    vtan[1] = (s1 * e2[1] - s2 * e1[1]);
    vtan[2] = (s1 * e2[2] - s2 * e1[2]);

    utan[0] = (t1 * e2[0] - t2 * e1[0]);
    utan[1] = (t1 * e2[1] - t2 * e1[1]);
    utan[2] = (t1 * e2[2] - t2 * e1[2]);
  }

  if (orco) {
    if (orcodata) {
      o1 = orcodata[mface->v1];
      o2 = orcodata[mface->v2];
      o3 = orcodata[mface->v3];

      if (mface->v4) {
        o4 = orcodata[mface->v4];

        interp_v3_v3v3v3v3(orco, o1, o2, o3, o4, w);
      }
      else {
        interp_v3_v3v3v3(orco, o1, o2, o3, w);
      }
      BKE_mesh_orco_verts_transform(mesh, (float(*)[3])orco, 1, true);
    }
    else {
      copy_v3_v3(orco, vec);
    }
  }
}
void psys_interpolate_uvs(const MTFace *tface, int quad, const float w[4], float uvco[2])
{
  float v10 = tface->uv[0][0];
  float v11 = tface->uv[0][1];
  float v20 = tface->uv[1][0];
  float v21 = tface->uv[1][1];
  float v30 = tface->uv[2][0];
  float v31 = tface->uv[2][1];
  float v40, v41;

  if (quad) {
    v40 = tface->uv[3][0];
    v41 = tface->uv[3][1];

    uvco[0] = w[0] * v10 + w[1] * v20 + w[2] * v30 + w[3] * v40;
    uvco[1] = w[0] * v11 + w[1] * v21 + w[2] * v31 + w[3] * v41;
  }
  else {
    uvco[0] = w[0] * v10 + w[1] * v20 + w[2] * v30;
    uvco[1] = w[0] * v11 + w[1] * v21 + w[2] * v31;
  }
}

void psys_interpolate_mcol(const MCol *mcol, int quad, const float w[4], MCol *mc)
{
  const char *cp1, *cp2, *cp3, *cp4;
  char *cp;

  cp = (char *)mc;
  cp1 = (const char *)&mcol[0];
  cp2 = (const char *)&mcol[1];
  cp3 = (const char *)&mcol[2];

  if (quad) {
    cp4 = (char *)&mcol[3];

    cp[0] = (int)(w[0] * cp1[0] + w[1] * cp2[0] + w[2] * cp3[0] + w[3] * cp4[0]);
    cp[1] = (int)(w[0] * cp1[1] + w[1] * cp2[1] + w[2] * cp3[1] + w[3] * cp4[1]);
    cp[2] = (int)(w[0] * cp1[2] + w[1] * cp2[2] + w[2] * cp3[2] + w[3] * cp4[2]);
    cp[3] = (int)(w[0] * cp1[3] + w[1] * cp2[3] + w[2] * cp3[3] + w[3] * cp4[3]);
  }
  else {
    cp[0] = (int)(w[0] * cp1[0] + w[1] * cp2[0] + w[2] * cp3[0]);
    cp[1] = (int)(w[0] * cp1[1] + w[1] * cp2[1] + w[2] * cp3[1]);
    cp[2] = (int)(w[0] * cp1[2] + w[1] * cp2[2] + w[2] * cp3[2]);
    cp[3] = (int)(w[0] * cp1[3] + w[1] * cp2[3] + w[2] * cp3[3]);
  }
}

static float psys_interpolate_value_from_verts(
    Mesh *mesh, short from, int index, const float fw[4], const float *values)
{
  if (values == 0 || index == -1) {
    return 0.0;
  }

  switch (from) {
    case PART_FROM_VERT:
      return values[index];
    case PART_FROM_FACE:
    case PART_FROM_VOLUME: {
      MFace *mf = &mesh->mface[index];
      return interpolate_particle_value(
          values[mf->v1], values[mf->v2], values[mf->v3], values[mf->v4], fw, mf->v4);
    }
  }
  return 0.0f;
}

/* conversion of pa->fw to origspace layer coordinates */
static void psys_w_to_origspace(const float w[4], float uv[2])
{
  uv[0] = w[1] + w[2];
  uv[1] = w[2] + w[3];
}

/* conversion of pa->fw to weights in face from origspace */
static void psys_origspace_to_w(OrigSpaceFace *osface, int quad, const float w[4], float neww[4])
{
  float v[4][3], co[3];

  v[0][0] = osface->uv[0][0];
  v[0][1] = osface->uv[0][1];
  v[0][2] = 0.0f;
  v[1][0] = osface->uv[1][0];
  v[1][1] = osface->uv[1][1];
  v[1][2] = 0.0f;
  v[2][0] = osface->uv[2][0];
  v[2][1] = osface->uv[2][1];
  v[2][2] = 0.0f;

  psys_w_to_origspace(w, co);
  co[2] = 0.0f;

  if (quad) {
    v[3][0] = osface->uv[3][0];
    v[3][1] = osface->uv[3][1];
    v[3][2] = 0.0f;
    interp_weights_poly_v3(neww, v, 4, co);
  }
  else {
    interp_weights_poly_v3(neww, v, 3, co);
    neww[3] = 0.0f;
  }
}

int psys_particle_dm_face_lookup(Mesh *mesh_final,
                                 Mesh *mesh_original,
                                 int findex_orig,
                                 const float fw[4],
                                 struct LinkNode **poly_nodes)
{
  MFace *mtessface_final;
  OrigSpaceFace *osface_final;
  int pindex_orig;
  float uv[2], (*faceuv)[2];

  const int *index_mf_to_mpoly_deformed = NULL;
  const int *index_mf_to_mpoly = NULL;
  const int *index_mp_to_orig = NULL;

  const int totface_final = mesh_final->totface;
  const int totface_deformed = mesh_original ? mesh_original->totface : totface_final;

  if (ELEM(0, totface_final, totface_deformed)) {
    return DMCACHE_NOTFOUND;
  }

  index_mf_to_mpoly = CustomData_get_layer(&mesh_final->fdata, CD_ORIGINDEX);
  index_mp_to_orig = CustomData_get_layer(&mesh_final->pdata, CD_ORIGINDEX);
  BLI_assert(index_mf_to_mpoly);

  if (mesh_original) {
    index_mf_to_mpoly_deformed = CustomData_get_layer(&mesh_original->fdata, CD_ORIGINDEX);
  }
  else {
    BLI_assert(mesh_final->runtime.deformed_only);
    index_mf_to_mpoly_deformed = index_mf_to_mpoly;
  }
  BLI_assert(index_mf_to_mpoly_deformed);

  pindex_orig = index_mf_to_mpoly_deformed[findex_orig];

  if (mesh_original == NULL) {
    mesh_original = mesh_final;
  }

  index_mf_to_mpoly_deformed = NULL;

  mtessface_final = mesh_final->mface;
  osface_final = CustomData_get_layer(&mesh_final->fdata, CD_ORIGSPACE);

  if (osface_final == NULL) {
    /* Assume we don't need osface_final data, and we get a direct 1-1 mapping... */
    if (findex_orig < totface_final) {
      // printf("\tNO CD_ORIGSPACE, assuming not needed\n");
      return findex_orig;
    }

    printf("\tNO CD_ORIGSPACE, error out of range\n");
    return DMCACHE_NOTFOUND;
  }
  if (findex_orig >= mesh_original->totface) {
    return DMCACHE_NOTFOUND; /* index not in the original mesh */
  }

  psys_w_to_origspace(fw, uv);

  if (poly_nodes) {
    /* we can have a restricted linked list of faces to check, faster! */
    LinkNode *tessface_node = poly_nodes[pindex_orig];

    for (; tessface_node; tessface_node = tessface_node->next) {
      int findex_dst = POINTER_AS_INT(tessface_node->link);
      faceuv = osface_final[findex_dst].uv;

      /* check that this intersects - Its possible this misses :/ -
       * could also check its not between */
      if (mtessface_final[findex_dst].v4) {
        if (isect_point_quad_v2(uv, faceuv[0], faceuv[1], faceuv[2], faceuv[3])) {
          return findex_dst;
        }
      }
      else if (isect_point_tri_v2(uv, faceuv[0], faceuv[1], faceuv[2])) {
        return findex_dst;
      }
    }
  }
  else { /* if we have no node, try every face */
    for (int findex_dst = 0; findex_dst < totface_final; findex_dst++) {
      /* If current tessface from 'final' DM and orig tessface (given by index)
       * map to the same orig poly. */
      if (BKE_mesh_origindex_mface_mpoly(index_mf_to_mpoly, index_mp_to_orig, findex_dst) ==
          pindex_orig) {
        faceuv = osface_final[findex_dst].uv;

        /* check that this intersects - Its possible this misses :/ -
         * could also check its not between */
        if (mtessface_final[findex_dst].v4) {
          if (isect_point_quad_v2(uv, faceuv[0], faceuv[1], faceuv[2], faceuv[3])) {
            return findex_dst;
          }
        }
        else if (isect_point_tri_v2(uv, faceuv[0], faceuv[1], faceuv[2])) {
          return findex_dst;
        }
      }
    }
  }

  return DMCACHE_NOTFOUND;
}

static int psys_map_index_on_dm(Mesh *mesh,
                                int from,
                                int index,
                                int index_dmcache,
                                const float fw[4],
                                float UNUSED(foffset),
                                int *mapindex,
                                float mapfw[4])
{
  if (index < 0) {
    return 0;
  }

  if (mesh->runtime.deformed_only || index_dmcache == DMCACHE_ISCHILD) {
    /* for meshes that are either only deformed or for child particles, the
     * index and fw do not require any mapping, so we can directly use it */
    if (from == PART_FROM_VERT) {
      if (index >= mesh->totvert) {
        return 0;
      }

      *mapindex = index;
    }
    else { /* FROM_FACE/FROM_VOLUME */
      if (index >= mesh->totface) {
        return 0;
      }

      *mapindex = index;
      copy_v4_v4(mapfw, fw);
    }
  }
  else {
    /* for other meshes that have been modified, we try to map the particle
     * to their new location, which means a different index, and for faces
     * also a new face interpolation weights */
    if (from == PART_FROM_VERT) {
      if (index_dmcache == DMCACHE_NOTFOUND || index_dmcache >= mesh->totvert) {
        return 0;
      }

      *mapindex = index_dmcache;
    }
    else { /* FROM_FACE/FROM_VOLUME */
           /* find a face on the derived mesh that uses this face */
      MFace *mface;
      OrigSpaceFace *osface;
      int i;

      i = index_dmcache;

      if (i == DMCACHE_NOTFOUND || i >= mesh->totface) {
        return 0;
      }

      *mapindex = i;

      /* modify the original weights to become
       * weights for the derived mesh face */
      osface = CustomData_get_layer(&mesh->fdata, CD_ORIGSPACE);
      mface = &mesh->mface[i];

      if (osface == NULL) {
        mapfw[0] = mapfw[1] = mapfw[2] = mapfw[3] = 0.0f;
      }
      else {
        psys_origspace_to_w(&osface[i], mface->v4, fw, mapfw);
      }
    }
  }

  return 1;
}

void psys_particle_on_dm(Mesh *mesh_final,
                         int from,
                         int index,
                         int index_dmcache,
                         const float fw[4],
                         float foffset,
                         float vec[3],
                         float nor[3],
                         float utan[3],
                         float vtan[3],
                         float orco[3])
{
  float tmpnor[3], mapfw[4];
  float(*orcodata)[3];
  int mapindex;

  if (!psys_map_index_on_dm(
          mesh_final, from, index, index_dmcache, fw, foffset, &mapindex, mapfw)) {
    if (vec) {
      vec[0] = vec[1] = vec[2] = 0.0;
    }
    if (nor) {
      nor[0] = nor[1] = 0.0;
      nor[2] = 1.0;
    }
    if (orco) {
      orco[0] = orco[1] = orco[2] = 0.0;
    }
    if (utan) {
      utan[0] = utan[1] = utan[2] = 0.0;
    }
    if (vtan) {
      vtan[0] = vtan[1] = vtan[2] = 0.0;
    }

    return;
  }

  orcodata = CustomData_get_layer(&mesh_final->vdata, CD_ORCO);
  const float(*vert_normals)[3] = BKE_mesh_vertex_normals_ensure(mesh_final);

  if (from == PART_FROM_VERT) {
    copy_v3_v3(vec, mesh_final->mvert[mapindex].co);

    if (nor) {
      copy_v3_v3(nor, vert_normals[mapindex]);
    }

    if (orco) {
      if (orcodata) {
        copy_v3_v3(orco, orcodata[mapindex]);
        BKE_mesh_orco_verts_transform(mesh_final, (float(*)[3])orco, 1, true);
      }
      else {
        copy_v3_v3(orco, vec);
      }
    }

    if (utan && vtan) {
      utan[0] = utan[1] = utan[2] = 0.0f;
      vtan[0] = vtan[1] = vtan[2] = 0.0f;
    }
  }
  else { /* PART_FROM_FACE / PART_FROM_VOLUME */
    MFace *mface;
    MTFace *mtface;
    MVert *mvert;

    mface = &mesh_final->mface[mapindex];
    mvert = mesh_final->mvert;
    mtface = mesh_final->mtface;

    if (mtface) {
      mtface += mapindex;
    }

    if (from == PART_FROM_VOLUME) {
      psys_interpolate_face(mesh_final,
                            mvert,
                            vert_normals,
                            mface,
                            mtface,
                            orcodata,
                            mapfw,
                            vec,
                            tmpnor,
                            utan,
                            vtan,
                            orco);
      if (nor) {
        copy_v3_v3(nor, tmpnor);
      }

      /* XXX Why not normalize tmpnor before copying it into nor??? -- mont29 */
      normalize_v3(tmpnor);

      mul_v3_fl(tmpnor, -foffset);
      add_v3_v3(vec, tmpnor);
    }
    else {
      psys_interpolate_face(mesh_final,
                            mvert,
                            vert_normals,
                            mface,
                            mtface,
                            orcodata,
                            mapfw,
                            vec,
                            nor,
                            utan,
                            vtan,
                            orco);
    }
  }
}

float psys_particle_value_from_verts(Mesh *mesh, short from, ParticleData *pa, float *values)
{
  float mapfw[4];
  int mapindex;

  if (!psys_map_index_on_dm(
          mesh, from, pa->num, pa->num_dmcache, pa->fuv, pa->foffset, &mapindex, mapfw)) {
    return 0.0f;
  }

  return psys_interpolate_value_from_verts(mesh, from, mapindex, mapfw, values);
}

ParticleSystemModifierData *psys_get_modifier(Object *ob, ParticleSystem *psys)
{
  ModifierData *md;
  ParticleSystemModifierData *psmd;

  for (md = ob->modifiers.first; md; md = md->next) {
    if (md->type == eModifierType_ParticleSystem) {
      psmd = (ParticleSystemModifierData *)md;
      if (psmd->psys == psys) {
        return psmd;
      }
    }
  }
  return NULL;
}

/************************************************/
/*          Particles on a shape                */
/************************************************/

/* ready for future use */
static void psys_particle_on_shape(int UNUSED(distr),
                                   int UNUSED(index),
                                   float *UNUSED(fuv),
                                   float vec[3],
                                   float nor[3],
                                   float utan[3],
                                   float vtan[3],
                                   float orco[3])
{
  /* TODO */
  const float zerovec[3] = {0.0f, 0.0f, 0.0f};
  if (vec) {
    copy_v3_v3(vec, zerovec);
  }
  if (nor) {
    copy_v3_v3(nor, zerovec);
  }
  if (utan) {
    copy_v3_v3(utan, zerovec);
  }
  if (vtan) {
    copy_v3_v3(vtan, zerovec);
  }
  if (orco) {
    copy_v3_v3(orco, zerovec);
  }
}

/************************************************/
/*          Particles on emitter                */
/************************************************/

void psys_emitter_customdata_mask(ParticleSystem *psys, CustomData_MeshMasks *r_cddata_masks)
{
  MTex *mtex;
  int i;

  if (!psys->part) {
    return;
  }

  for (i = 0; i < MAX_MTEX; i++) {
    mtex = psys->part->mtex[i];
    if (mtex && mtex->mapto && (mtex->texco & TEXCO_UV)) {
      r_cddata_masks->fmask |= CD_MASK_MTFACE;
    }
  }

  if (psys->part->tanfac != 0.0f) {
    r_cddata_masks->fmask |= CD_MASK_MTFACE;
  }

  /* ask for vertexgroups if we need them */
  for (i = 0; i < PSYS_TOT_VG; i++) {
    if (psys->vgroup[i]) {
      r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
      break;
    }
  }

  /* particles only need this if they are after a non deform modifier, and
   * the modifier stack will only create them in that case. */
  r_cddata_masks->lmask |= CD_MASK_ORIGSPACE_MLOOP;
  /* XXX Check we do need all those? */
  r_cddata_masks->vmask |= CD_MASK_ORIGINDEX;
  r_cddata_masks->emask |= CD_MASK_ORIGINDEX;
  r_cddata_masks->pmask |= CD_MASK_ORIGINDEX;

  r_cddata_masks->vmask |= CD_MASK_ORCO;
}

void psys_particle_on_emitter(ParticleSystemModifierData *psmd,
                              int from,
                              int index,
                              int index_dmcache,
                              float fuv[4],
                              float foffset,
                              float vec[3],
                              float nor[3],
                              float utan[3],
                              float vtan[3],
                              float orco[3])
{
  if (psmd && psmd->mesh_final) {
    if (psmd->psys->part->distr == PART_DISTR_GRID && psmd->psys->part->from != PART_FROM_VERT) {
      if (vec) {
        copy_v3_v3(vec, fuv);
      }

      if (orco) {
        copy_v3_v3(orco, fuv);
      }
      return;
    }
    /* we can't use the num_dmcache */
    psys_particle_on_dm(
        psmd->mesh_final, from, index, index_dmcache, fuv, foffset, vec, nor, utan, vtan, orco);
  }
  else {
    psys_particle_on_shape(from, index, fuv, vec, nor, utan, vtan, orco);
  }
}

/************************************************/
/*          Path Cache                          */
/************************************************/

void precalc_guides(ParticleSimulationData *sim, ListBase *effectors)
{
  EffectedPoint point;
  ParticleKey state;
  EffectorData efd;
  EffectorCache *eff;
  ParticleSystem *psys = sim->psys;
  EffectorWeights *weights = sim->psys->part->effector_weights;
  GuideEffectorData *data;
  PARTICLE_P;

  if (!effectors) {
    return;
  }

  LOOP_PARTICLES
  {
    psys_particle_on_emitter(sim->psmd,
                             sim->psys->part->from,
                             pa->num,
                             pa->num_dmcache,
                             pa->fuv,
                             pa->foffset,
                             state.co,
                             0,
                             0,
                             0,
                             0);

    mul_m4_v3(sim->ob->obmat, state.co);
    mul_mat3_m4_v3(sim->ob->obmat, state.vel);

    pd_point_from_particle(sim, pa, &state, &point);

    for (eff = effectors->first; eff; eff = eff->next) {
      if (eff->pd->forcefield != PFIELD_GUIDE) {
        continue;
      }

      if (!eff->guide_data) {
        eff->guide_data = MEM_callocN(sizeof(GuideEffectorData) * psys->totpart,
                                      "GuideEffectorData");
      }

      data = eff->guide_data + p;

      sub_v3_v3v3(efd.vec_to_point, state.co, eff->guide_loc);
      copy_v3_v3(efd.nor, eff->guide_dir);
      efd.distance = len_v3(efd.vec_to_point);

      copy_v3_v3(data->vec_to_point, efd.vec_to_point);
      data->strength = effector_falloff(eff, &efd, &point, weights);
    }
  }
}

int do_guides(Depsgraph *depsgraph,
              ParticleSettings *part,
              ListBase *effectors,
              ParticleKey *state,
              int index,
              float time)
{
  CurveMapping *clumpcurve = (part->child_flag & PART_CHILD_USE_CLUMP_CURVE) ? part->clumpcurve :
                                                                               NULL;
  CurveMapping *roughcurve = (part->child_flag & PART_CHILD_USE_ROUGH_CURVE) ? part->roughcurve :
                                                                               NULL;
  EffectorCache *eff;
  PartDeflect *pd;
  Curve *cu;
  GuideEffectorData *data;

  float effect[3] = {0.0f, 0.0f, 0.0f}, veffect[3] = {0.0f, 0.0f, 0.0f};
  float guidevec[4], guidedir[3], rot2[4], temp[3];
  float guidetime, radius, weight, angle, totstrength = 0.0f;
  float vec_to_point[3];

  if (effectors) {
    for (eff = effectors->first; eff; eff = eff->next) {
      pd = eff->pd;

      if (pd->forcefield != PFIELD_GUIDE) {
        continue;
      }

      data = eff->guide_data + index;

      if (data->strength <= 0.0f) {
        continue;
      }

      guidetime = time / (1.0f - pd->free_end);

      if (guidetime > 1.0f) {
        continue;
      }

      cu = (Curve *)eff->ob->data;

      if (pd->flag & PFIELD_GUIDE_PATH_ADD) {
        if (BKE_where_on_path(
                eff->ob, data->strength * guidetime, guidevec, guidedir, NULL, &radius, &weight) ==
            0) {
          return 0;
        }
      }
      else {
        if (BKE_where_on_path(eff->ob, guidetime, guidevec, guidedir, NULL, &radius, &weight) ==
            0) {
          return 0;
        }
      }

      mul_m4_v3(eff->ob->obmat, guidevec);
      mul_mat3_m4_v3(eff->ob->obmat, guidedir);

      normalize_v3(guidedir);

      copy_v3_v3(vec_to_point, data->vec_to_point);

      if (guidetime != 0.0f) {
        /* curve direction */
        cross_v3_v3v3(temp, eff->guide_dir, guidedir);
        angle = dot_v3v3(eff->guide_dir, guidedir) / (len_v3(eff->guide_dir));
        angle = saacos(angle);
        axis_angle_to_quat(rot2, temp, angle);
        mul_qt_v3(rot2, vec_to_point);

        /* curve tilt */
        axis_angle_to_quat(rot2, guidedir, guidevec[3] - eff->guide_loc[3]);
        mul_qt_v3(rot2, vec_to_point);
      }

      /* curve taper */
      if (cu->taperobj) {
        mul_v3_fl(vec_to_point,
                  BKE_displist_calc_taper(depsgraph,
                                          eff->scene,
                                          cu->taperobj,
                                          (int)(data->strength * guidetime * 100.0f),
                                          100));
      }
      else { /* Curve size. */
        if (cu->flag & CU_PATH_RADIUS) {
          mul_v3_fl(vec_to_point, radius);
        }
      }

      if (clumpcurve) {
        BKE_curvemapping_changed_all(clumpcurve);
      }
      if (roughcurve) {
        BKE_curvemapping_changed_all(roughcurve);
      }

      {
        ParticleKey key;
        const float par_co[3] = {0.0f, 0.0f, 0.0f};
        const float par_vel[3] = {0.0f, 0.0f, 0.0f};
        const float par_rot[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        const float orco_offset[3] = {0.0f, 0.0f, 0.0f};

        copy_v3_v3(key.co, vec_to_point);
        do_kink(&key,
                par_co,
                par_vel,
                par_rot,
                guidetime,
                pd->kink_freq,
                pd->kink_shape,
                pd->kink_amp,
                0.0f,
                pd->kink,
                pd->kink_axis,
                0,
                0);
        do_clump(&key,
                 par_co,
                 guidetime,
                 orco_offset,
                 pd->clump_fac,
                 pd->clump_pow,
                 1.0f,
                 part->child_flag & PART_CHILD_USE_CLUMP_NOISE,
                 part->clump_noise_size,
                 clumpcurve);
        copy_v3_v3(vec_to_point, key.co);
      }

      add_v3_v3(vec_to_point, guidevec);

      // sub_v3_v3v3(pa_loc, pa_loc, pa_zero);
      madd_v3_v3fl(effect, vec_to_point, data->strength);
      madd_v3_v3fl(veffect, guidedir, data->strength);
      totstrength += data->strength;

      if (pd->flag & PFIELD_GUIDE_PATH_WEIGHT) {
        totstrength *= weight;
      }
    }
  }

  if (totstrength != 0.0f) {
    if (totstrength > 1.0f) {
      mul_v3_fl(effect, 1.0f / totstrength);
    }
    CLAMP(totstrength, 0.0f, 1.0f);
    // add_v3_v3(effect, pa_zero);
    interp_v3_v3v3(state->co, state->co, effect, totstrength);

    normalize_v3(veffect);
    mul_v3_fl(veffect, len_v3(state->vel));
    copy_v3_v3(state->vel, veffect);
    return 1;
  }
  return 0;
}

static void do_path_effectors(ParticleSimulationData *sim,
                              int i,
                              ParticleCacheKey *ca,
                              int k,
                              int steps,
                              float *UNUSED(rootco),
                              float effector,
                              float UNUSED(dfra),
                              float UNUSED(cfra),
                              float *length,
                              float *vec)
{
  float force[3] = {0.0f, 0.0f, 0.0f};
  ParticleKey eff_key;
  EffectedPoint epoint;

  /* Don't apply effectors for dynamic hair, otherwise the effectors don't get applied twice. */
  if (sim->psys->flag & PSYS_HAIR_DYNAMICS) {
    return;
  }

  copy_v3_v3(eff_key.co, (ca - 1)->co);
  copy_v3_v3(eff_key.vel, (ca - 1)->vel);
  copy_qt_qt(eff_key.rot, (ca - 1)->rot);

  pd_point_from_particle(sim, sim->psys->particles + i, &eff_key, &epoint);
  BKE_effectors_apply(sim->psys->effectors,
                      sim->colliders,
                      sim->psys->part->effector_weights,
                      &epoint,
                      force,
                      NULL,
                      NULL);

  mul_v3_fl(force,
            effector * powf((float)k / (float)steps, 100.0f * sim->psys->part->eff_hair) /
                (float)steps);

  add_v3_v3(force, vec);

  normalize_v3(force);

  if (k < steps) {
    sub_v3_v3v3(vec, (ca + 1)->co, ca->co);
  }

  madd_v3_v3v3fl(ca->co, (ca - 1)->co, force, *length);

  if (k < steps) {
    *length = len_v3(vec);
  }
}
static void offset_child(ChildParticle *cpa,
                         ParticleKey *par,
                         float *par_rot,
                         ParticleKey *child,
                         float flat,
                         float radius)
{
  copy_v3_v3(child->co, cpa->fuv);
  mul_v3_fl(child->co, radius);

  child->co[0] *= flat;

  copy_v3_v3(child->vel, par->vel);

  if (par_rot) {
    mul_qt_v3(par_rot, child->co);
    copy_qt_qt(child->rot, par_rot);
  }
  else {
    unit_qt(child->rot);
  }

  add_v3_v3(child->co, par->co);
}
float *psys_cache_vgroup(Mesh *mesh, ParticleSystem *psys, int vgroup)
{
  float *vg = 0;

  if (vgroup < 0) {
    /* hair dynamics pinning vgroup */
  }
  else if (psys->vgroup[vgroup]) {
    MDeformVert *dvert = mesh->dvert;
    if (dvert) {
      int totvert = mesh->totvert, i;
      vg = MEM_callocN(sizeof(float) * totvert, "vg_cache");
      if (psys->vg_neg & (1 << vgroup)) {
        for (i = 0; i < totvert; i++) {
          vg[i] = 1.0f - BKE_defvert_find_weight(&dvert[i], psys->vgroup[vgroup] - 1);
        }
      }
      else {
        for (i = 0; i < totvert; i++) {
          vg[i] = BKE_defvert_find_weight(&dvert[i], psys->vgroup[vgroup] - 1);
        }
      }
    }
  }
  return vg;
}
void psys_find_parents(ParticleSimulationData *sim, const bool use_render_params)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = sim->psys->part;
  KDTree_3d *tree;
  ChildParticle *cpa;
  ParticleTexture ptex;
  int p, totparent, totchild = sim->psys->totchild;
  float co[3], orco[3];
  int from = PART_FROM_FACE;
  totparent = (int)(totchild * part->parents * 0.3f);

  if (use_render_params && part->child_nbr && part->ren_child_nbr) {
    totparent *= (float)part->child_nbr / (float)part->ren_child_nbr;
  }

  /* hard limit, workaround for it being ignored above */
  if (sim->psys->totpart < totparent) {
    totparent = sim->psys->totpart;
  }

  tree = BLI_kdtree_3d_new(totparent);

  for (p = 0, cpa = sim->psys->child; p < totparent; p++, cpa++) {
    psys_particle_on_emitter(
        sim->psmd, from, cpa->num, DMCACHE_ISCHILD, cpa->fuv, cpa->foffset, co, 0, 0, 0, orco);

    /* Check if particle doesn't exist because of texture influence.
     * Insert only existing particles into kdtree. */
    get_cpa_texture(sim->psmd->mesh_final,
                    psys,
                    part,
                    psys->particles + cpa->pa[0],
                    p,
                    cpa->num,
                    cpa->fuv,
                    orco,
                    &ptex,
                    PAMAP_DENS | PAMAP_CHILD,
                    psys->cfra);

    if (ptex.exist >= psys_frand(psys, p + 24)) {
      BLI_kdtree_3d_insert(tree, p, orco);
    }
  }

  BLI_kdtree_3d_balance(tree);

  for (; p < totchild; p++, cpa++) {
    psys_particle_on_emitter(
        sim->psmd, from, cpa->num, DMCACHE_ISCHILD, cpa->fuv, cpa->foffset, co, 0, 0, 0, orco);
    cpa->parent = BLI_kdtree_3d_find_nearest(tree, orco, NULL);
  }

  BLI_kdtree_3d_free(tree);
}

static bool psys_thread_context_init_path(ParticleThreadContext *ctx,
                                          ParticleSimulationData *sim,
                                          Scene *scene,
                                          float cfra,
                                          const bool editupdate,
                                          const bool use_render_params)
{
  ParticleSystem *psys = sim->psys;
  ParticleSettings *part = psys->part;
  int totparent = 0, between = 0;
  int segments = 1 << part->draw_step;
  int totchild = psys->totchild;

  psys_thread_context_init(ctx, sim);

  /*---start figuring out what is actually wanted---*/
  if (psys_in_edit_mode(sim->depsgraph, psys)) {
    ParticleEditSettings *pset = &scene->toolsettings->particle;

    if ((use_render_params == 0) &&
        (psys_orig_edit_get(psys) == NULL || pset->flag & PE_DRAW_PART) == 0) {
      totchild = 0;
    }

    segments = 1 << pset->draw_step;
  }

  if (totchild && part->childtype == PART_CHILD_FACES) {
    totparent = (int)(totchild * part->parents * 0.3f);

    if (use_render_params && part->child_nbr && part->ren_child_nbr) {
      totparent *= (float)part->child_nbr / (float)part->ren_child_nbr;
    }

    /* part->parents could still be 0 so we can't test with totparent */
    between = 1;
  }

  if (use_render_params) {
    segments = 1 << part->ren_step;
  }
  else {
    totchild = (int)((float)totchild * (float)part->disp / 100.0f);
  }

  totparent = MIN2(totparent, totchild);

  if (totchild == 0) {
    return false;
  }

  /* fill context values */
  ctx->between = between;
  ctx->segments = segments;
  if (ELEM(part->kink, PART_KINK_SPIRAL)) {
    ctx->extra_segments = max_ii(part->kink_extra_steps, 1);
  }
  else {
    ctx->extra_segments = 0;
  }
  ctx->totchild = totchild;
  ctx->totparent = totparent;
  ctx->parent_pass = 0;
  ctx->cfra = cfra;
  ctx->editupdate = editupdate;

  psys->lattice_deform_data = psys_create_lattice_deform_data(&ctx->sim);

  /* cache all relevant vertex groups if they exist */
  ctx->vg_length = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_LENGTH);
  ctx->vg_clump = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_CLUMP);
  ctx->vg_kink = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_KINK);
  ctx->vg_rough1 = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_ROUGH1);
  ctx->vg_rough2 = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_ROUGH2);
  ctx->vg_roughe = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_ROUGHE);
  ctx->vg_twist = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_TWIST);
  if (psys->part->flag & PART_CHILD_EFFECT) {
    ctx->vg_effector = psys_cache_vgroup(ctx->mesh, psys, PSYS_VG_EFFECTOR);
  }

  /* prepare curvemapping tables */
  if ((part->child_flag & PART_CHILD_USE_CLUMP_CURVE) && part->clumpcurve) {
    ctx->clumpcurve = BKE_curvemapping_copy(part->clumpcurve);
    BKE_curvemapping_changed_all(ctx->clumpcurve);
  }
  else {
    ctx->clumpcurve = NULL;
  }
  if ((part->child_flag & PART_CHILD_USE_ROUGH_CURVE) && part->roughcurve) {
    ctx->roughcurve = BKE_curvemapping_copy(part->roughcurve);
    BKE_curvemapping_changed_all(ctx->roughcurve);
  }
  else {
    ctx->roughcurve = NULL;
  }
  if ((part->child_flag & PART_CHILD_USE_TWIST_CURVE) && part->twistcurve) {
    ctx->twistcurve = BKE_curvemapping_copy(part->twistcurve);
    BKE_curvemapping_changed_all(ctx->twistcurve);
  }
  else {
    ctx->twistcurve = NULL;
  }

  return true;
}

static void psys_task_init_path(ParticleTask *task, ParticleSimulationData *sim)
{
  /* init random number generator */
  int seed = 31415926 + sim->psys->seed;

  task->rng_path = BLI_rng_new(seed);
}

/* NOTE: this function must be thread safe, except for branching! */
static void psys_thread_create_path(ParticleTask *task,
                                    struct ChildParticle *cpa,
                                    ParticleCacheKey *child_keys,
                                    int i)
{
  ParticleThreadContext *ctx = task->ctx;
  Object *ob = ctx->sim.ob;
  ParticleSystem *psys = ctx->sim.psys;
  ParticleSettings *part = psys->part;
  ParticleCacheKey **cache = psys->childcache;
  PTCacheEdit *edit = psys_orig_edit_get(psys);
  ParticleCacheKey **pcache = psys_in_edit_mode(ctx->sim.depsgraph, psys) && edit ?
                                  edit->pathcache :
                                  psys->pathcache;
  ParticleCacheKey *child, *key[4];
  ParticleTexture ptex;
  float *cpa_fuv = 0, *par_rot = 0, rot[4];
  float orco[3], hairmat[4][4], dvec[3], off1[4][3], off2[4][3];
  float eff_length, eff_vec[3], weight[4];
  int k, cpa_num;
  short cpa_from;

  if (!pcache) {
    return;
  }

  if (ctx->between) {
    ParticleData *pa = psys->particles + cpa->pa[0];
    int w, needupdate;
    float foffset, wsum = 0.0f;
    float co[3];
    float p_min = part->parting_min;
    float p_max = part->parting_max;
    /* Virtual parents don't work nicely with parting. */
    float p_fac = part->parents > 0.0f ? 0.0f : part->parting_fac;

    if (ctx->editupdate) {
      needupdate = 0;
      w = 0;
      while (w < 4 && cpa->pa[w] >= 0) {
        if (edit->points[cpa->pa[w]].flag & PEP_EDIT_RECALC) {
          needupdate = 1;
          break;
        }
        w++;
      }

      if (!needupdate) {
        return;
      }

      memset(child_keys, 0, sizeof(*child_keys) * (ctx->segments + 1));
    }

    /* get parent paths */
    for (w = 0; w < 4; w++) {
      if (cpa->pa[w] >= 0) {
        key[w] = pcache[cpa->pa[w]];
        weight[w] = cpa->w[w];
      }
      else {
        key[w] = pcache[0];
        weight[w] = 0.0f;
      }
    }

    /* modify weights to create parting */
    if (p_fac > 0.0f) {
      const ParticleCacheKey *key_0_last = pcache_key_segment_endpoint_safe(key[0]);
      for (w = 0; w < 4; w++) {
        if (w && (weight[w] > 0.0f)) {
          const ParticleCacheKey *key_w_last = pcache_key_segment_endpoint_safe(key[w]);
          float d;
          if (part->flag & PART_CHILD_LONG_HAIR) {
            /* For long hair use tip distance/root distance as parting
             * factor instead of root to tip angle. */
            float d1 = len_v3v3(key[0]->co, key[w]->co);
            float d2 = len_v3v3(key_0_last->co, key_w_last->co);

            d = d1 > 0.0f ? d2 / d1 - 1.0f : 10000.0f;
          }
          else {
            float v1[3], v2[3];
            sub_v3_v3v3(v1, key_0_last->co, key[0]->co);
            sub_v3_v3v3(v2, key_w_last->co, key[w]->co);
            normalize_v3(v1);
            normalize_v3(v2);

            d = RAD2DEGF(saacos(dot_v3v3(v1, v2)));
          }

          if (p_max > p_min) {
            d = (d - p_min) / (p_max - p_min);
          }
          else {
            d = (d - p_min) <= 0.0f ? 0.0f : 1.0f;
          }

          CLAMP(d, 0.0f, 1.0f);

          if (d > 0.0f) {
            weight[w] *= (1.0f - d);
          }
        }
        wsum += weight[w];
      }
      for (w = 0; w < 4; w++) {
        weight[w] /= wsum;
      }

      interp_v4_v4v4(weight, cpa->w, weight, p_fac);
    }

    /* get the original coordinates (orco) for texture usage */
    cpa_num = cpa->num;

    foffset = cpa->foffset;
    cpa_fuv = cpa->fuv;
    cpa_from = PART_FROM_FACE;

    psys_particle_on_emitter(
        ctx->sim.psmd, cpa_from, cpa_num, DMCACHE_ISCHILD, cpa->fuv, foffset, co, 0, 0, 0, orco);

    mul_m4_v3(ob->obmat, co);

    for (w = 0; w < 4; w++) {
      sub_v3_v3v3(off1[w], co, key[w]->co);
    }

    psys_mat_hair_to_global(ob, ctx->sim.psmd->mesh_final, psys->part->from, pa, hairmat);
  }
  else {
    ParticleData *pa = psys->particles + cpa->parent;
    float co[3];
    if (ctx->editupdate) {
      if (!(edit->points[cpa->parent].flag & PEP_EDIT_RECALC)) {
        return;
      }

      memset(child_keys, 0, sizeof(*child_keys) * (ctx->segments + 1));
    }

    /* get the parent path */
    key[0] = pcache[cpa->parent];

    /* get the original coordinates (orco) for texture usage */
    cpa_from = part->from;

    /*
     * NOTE: Should in theory be the same as:
     * cpa_num = psys_particle_dm_face_lookup(
     *        ctx->sim.psmd->dm_final,
     *        ctx->sim.psmd->dm_deformed,
     *        pa->num, pa->fuv,
     *        NULL);
     */
    cpa_num = (ELEM(pa->num_dmcache, DMCACHE_ISCHILD, DMCACHE_NOTFOUND)) ? pa->num :
                                                                           pa->num_dmcache;

    /* XXX hack to avoid messed up particle num and subsequent crash (T40733) */
    if (cpa_num > ctx->sim.psmd->mesh_final->totface) {
      cpa_num = 0;
    }
    cpa_fuv = pa->fuv;

    psys_particle_on_emitter(ctx->sim.psmd,
                             cpa_from,
                             cpa_num,
                             DMCACHE_ISCHILD,
                             cpa_fuv,
                             pa->foffset,
                             co,
                             0,
                             0,
                             0,
                             orco);

    psys_mat_hair_to_global(ob, ctx->sim.psmd->mesh_final, psys->part->from, pa, hairmat);
  }

  child_keys->segments = ctx->segments;

  /* get different child parameters from textures & vgroups */
  get_child_modifier_parameters(part, ctx, cpa, cpa_from, cpa_num, cpa_fuv, orco, &ptex);

  if (ptex.exist < psys_frand(psys, i + 24)) {
    child_keys->segments = -1;
    return;
  }

  /* create the child path */
  for (k = 0, child = child_keys; k <= ctx->segments; k++, child++) {
    if (ctx->between) {
      int w = 0;

      zero_v3(child->co);
      zero_v3(child->vel);
      unit_qt(child->rot);

      for (w = 0; w < 4; w++) {
        copy_v3_v3(off2[w], off1[w]);

        if (part->flag & PART_CHILD_LONG_HAIR) {
          /* Use parent rotation (in addition to emission location) to determine child offset. */
          if (k) {
            mul_qt_v3((key[w] + k)->rot, off2[w]);
          }

          /* Fade the effect of rotation for even lengths in the end */
          project_v3_v3v3(dvec, off2[w], (key[w] + k)->vel);
          madd_v3_v3fl(off2[w], dvec, -(float)k / (float)ctx->segments);
        }

        add_v3_v3(off2[w], (key[w] + k)->co);
      }

      /* child position is the weighted sum of parent positions */
      interp_v3_v3v3v3v3(child->co, off2[0], off2[1], off2[2], off2[3], weight);
      interp_v3_v3v3v3v3(child->vel,
                         (key[0] + k)->vel,
                         (key[1] + k)->vel,
                         (key[2] + k)->vel,
                         (key[3] + k)->vel,
                         weight);

      copy_qt_qt(child->rot, (key[0] + k)->rot);
    }
    else {
      if (k) {
        mul_qt_qtqt(rot, (key[0] + k)->rot, key[0]->rot);
        par_rot = rot;
      }
      else {
        par_rot = key[0]->rot;
      }
      /* offset the child from the parent position */
      offset_child(cpa,
                   (ParticleKey *)(key[0] + k),
                   par_rot,
                   (ParticleKey *)child,
                   part->childflat,
                   part->childrad);
    }

    child->time = (float)k / (float)ctx->segments;
  }

  /* apply effectors */
  if (part->flag & PART_CHILD_EFFECT) {
    for (k = 0, child = child_keys; k <= ctx->segments; k++, child++) {
      if (k) {
        do_path_effectors(&ctx->sim,
                          cpa->pa[0],
                          child,
                          k,
                          ctx->segments,
                          child_keys->co,
                          ptex.effector,
                          0.0f,
                          ctx->cfra,
                          &eff_length,
                          eff_vec);
      }
      else {
        sub_v3_v3v3(eff_vec, (child + 1)->co, child->co);
        eff_length = len_v3(eff_vec);
      }
    }
  }

  {
    ParticleData *pa = NULL;
    ParticleCacheKey *par = NULL;
    float par_co[3];
    float par_orco[3];

    if (ctx->totparent) {
      if (i >= ctx->totparent) {
        pa = &psys->particles[cpa->parent];
        /* this is now threadsafe, virtual parents are calculated before rest of children */
        BLI_assert(cpa->parent < psys->totchildcache);
        par = cache[cpa->parent];
      }
    }
    else if (cpa->parent >= 0) {
      pa = &psys->particles[cpa->parent];
      par = pcache[cpa->parent];

      /* If particle is non-existing, try to pick a viable parent from particles
       * used for interpolation. */
      for (k = 0; k < 4 && pa && (pa->flag & PARS_UNEXIST); k++) {
        if (cpa->pa[k] >= 0) {
          pa = &psys->particles[cpa->pa[k]];
          par = pcache[cpa->pa[k]];
        }
      }

      if (pa->flag & PARS_UNEXIST) {
        pa = NULL;
      }
    }

    if (pa) {
      ListBase modifiers;
      BLI_listbase_clear(&modifiers);

      psys_particle_on_emitter(ctx->sim.psmd,
                               part->from,
                               pa->num,
                               pa->num_dmcache,
                               pa->fuv,
                               pa->foffset,
                               par_co,
                               NULL,
                               NULL,
                               NULL,
                               par_orco);

      psys_apply_child_modifiers(
          ctx, &modifiers, cpa, &ptex, orco, hairmat, child_keys, par, par_orco);
    }
    else {
      zero_v3(par_orco);
    }
  }

  /* Hide virtual parents */
  if (i < ctx->totparent) {
    child_keys->segments = -1;
  }
}

static void exec_child_path_cache(TaskPool *__restrict UNUSED(pool), void *taskdata)
{
  ParticleTask *task = taskdata;
  ParticleThreadContext *ctx = task->ctx;
  ParticleSystem *psys = ctx->sim.psys;
  ParticleCacheKey **cache = psys->childcache;
  ChildParticle *cpa;
  int i;

  cpa = psys->child + task->begin;
  for (i = task->begin; i < task->end; i++, cpa++) {
    BLI_assert(i < psys->totchildcache);
    psys_thread_create_path(task, cpa, cache[i], i);
  }
}

void psys_cache_child_paths(ParticleSimulationData *sim,
                            float cfra,
                            const bool editupdate,
                            const bool use_render_params)
{
  TaskPool *task_pool;
  ParticleThreadContext ctx;
  ParticleTask *tasks_parent, *tasks_child;
  int numtasks_parent, numtasks_child;
  int i, totchild, totparent;

  if (sim->psys->flag & PSYS_GLOBAL_HAIR) {
    return;
  }

  /* create a task pool for child path tasks */
  if (!psys_thread_context_init_path(&ctx, sim, sim->scene, cfra, editupdate, use_render_params)) {
    return;
  }

  task_pool = BLI_task_pool_create(&ctx, TASK_PRIORITY_LOW);
  totchild = ctx.totchild;
  totparent = ctx.totparent;

  if (editupdate && sim->psys->childcache && totchild == sim->psys->totchildcache) {
    /* just overwrite the existing cache */
  }
  else {
    /* clear out old and create new empty path cache */
    free_child_path_cache(sim->psys);

    sim->psys->childcache = psys_alloc_path_cache_buffers(
        &sim->psys->childcachebufs, totchild, ctx.segments + ctx.extra_segments + 1);
    sim->psys->totchildcache = totchild;
  }

  /* cache parent paths */
  ctx.parent_pass = 1;
  psys_tasks_create(&ctx, 0, totparent, &tasks_parent, &numtasks_parent);
  for (i = 0; i < numtasks_parent; i++) {
    ParticleTask *task = &tasks_parent[i];

    psys_task_init_path(task, sim);
    BLI_task_pool_push(task_pool, exec_child_path_cache, task, false, NULL);
  }
  BLI_task_pool_work_and_wait(task_pool);

  /* cache child paths */
  ctx.parent_pass = 0;
  psys_tasks_create(&ctx, totparent, totchild, &tasks_child, &numtasks_child);
  for (i = 0; i < numtasks_child; i++) {
    ParticleTask *task = &tasks_child[i];

    psys_task_init_path(task, sim);
    BLI_task_pool_push(task_pool, exec_child_path_cache, task, false, NULL);
  }
  BLI_task_pool_work_and_wait(task_pool);

  BLI_task_pool_free(task_pool);

  psys_tasks_free(tasks_parent, numtasks_parent);
  psys_tasks_free(tasks_child, numtasks_child);

  psys_thread_context_free(&ctx);
}

/* figure out incremental rotations along path starting from unit quat */
static void cache_key_incremental_rotation(ParticleCacheKey *key0,
                                           ParticleCacheKey *key1,
                                           ParticleCacheKey *key2,
                                           float *prev_tangent,
                                           int i)
{
  float cosangle, angle, tangent[3], normal[3], q[4];

  switch (i) {
    case 0:
      /* start from second key */
      break;
    case 1:
      /* calculate initial tangent for incremental rotations */
      sub_v3_v3v3(prev_tangent, key0->co, key1->co);
      normalize_v3(prev_tangent);
      unit_qt(key1->rot);
      break;
    default:
      sub_v3_v3v3(tangent, key0->co, key1->co);
      normalize_v3(tangent);

      cosangle = dot_v3v3(tangent, prev_tangent);

      /* note we do the comparison on cosangle instead of
       * angle, since floating point accuracy makes it give
       * different results across platforms */
      if (cosangle > 0.999999f) {
        copy_v4_v4(key1->rot, key2->rot);
      }
      else {
        angle = saacos(cosangle);
        cross_v3_v3v3(normal, prev_tangent, tangent);
        axis_angle_to_quat(q, normal, angle);
        mul_qt_qtqt(key1->rot, q, key2->rot);
      }

      copy_v3_v3(prev_tangent, tangent);
  }

