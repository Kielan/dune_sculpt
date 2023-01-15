#include <stdarg.h>
#include <stddef.h>

#include <math.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_collection_types.h"
#include "DNA_curve_types.h"
#include "DNA_listBase.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_noise.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "PIL_time.h"

#include "BKE_anim_path.h" /* needed for where_on_path */
#include "BKE_bvhutils.h"
#include "BKE_collection.h"
#include "BKE_collision.h"
#include "BKE_curve.h"
#include "BKE_displist.h"
#include "BKE_effect.h"
#include "BKE_fluid.h"
#include "BKE_global.h"
#include "BKE_layer.h"
#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_physics.h"
#include "DEG_depsgraph_query.h"

#include "RE_texture.h"

EffectorWeights *BKE_effector_add_weights(Collection *collection)
{
  EffectorWeights *weights = MEM_callocN(sizeof(EffectorWeights), "EffectorWeights");
  for (int i = 0; i < NUM_PFIELD_TYPES; i++) {
    weights->weight[i] = 1.0f;
  }

  weights->global_gravity = 1.0f;

  weights->group = collection;

  return weights;
}
PartDeflect *BKE_partdeflect_new(int type)
{
  PartDeflect *pd;

  pd = MEM_callocN(sizeof(PartDeflect), "PartDeflect");

  pd->forcefield = type;
  pd->pdef_sbdamp = 0.1f;
  pd->pdef_sbift = 0.2f;
  pd->pdef_sboft = 0.02f;
  pd->pdef_cfrict = 5.0f;
  pd->seed = ((uint)(ceil(PIL_check_seconds_timer())) + 1) % 128;
  pd->f_strength = 1.0f;
  pd->f_damp = 1.0f;

  /* set sensible defaults based on type */
  switch (type) {
    case PFIELD_VORTEX:
      pd->shape = PFIELD_SHAPE_PLANE;
      break;
    case PFIELD_WIND:
      pd->shape = PFIELD_SHAPE_PLANE;
      pd->f_flow = 1.0f;        /* realistic wind behavior */
      pd->f_wind_factor = 1.0f; /* only act perpendicularly to a surface */
      break;
    case PFIELD_TEXTURE:
      pd->f_size = 1.0f;
      break;
    case PFIELD_FLUIDFLOW:
      pd->f_flow = 1.0f;
      break;
  }
  pd->flag = PFIELD_DO_LOCATION | PFIELD_DO_ROTATION | PFIELD_CLOTH_USE_CULLING;

  return pd;
}

/************************ PARTICLES ***************************/

PartDeflect *BKE_partdeflect_copy(const struct PartDeflect *pd_src)
{
  if (pd_src == NULL) {
    return NULL;
  }
  PartDeflect *pd_dst = MEM_dupallocN(pd_src);
  if (pd_dst->rng != NULL) {
    pd_dst->rng = BLI_rng_copy(pd_dst->rng);
  }
  return pd_dst;
}

void BKE_partdeflect_free(PartDeflect *pd)
{
  if (!pd) {
    return;
  }
  if (pd->rng) {
    BLI_rng_free(pd->rng);
  }
  MEM_freeN(pd);
}

/******************** EFFECTOR RELATIONS ***********************/

static void precalculate_effector(struct Depsgraph *depsgraph, EffectorCache *eff)
{
  float ctime = DEG_get_ctime(depsgraph);
  uint cfra = (uint)(ctime >= 0 ? ctime : -ctime);
  if (!eff->pd->rng) {
    eff->pd->rng = BLI_rng_new(eff->pd->seed + cfra);
  }
  else {
    BLI_rng_srandom(eff->pd->rng, eff->pd->seed + cfra);
  }

  if (eff->pd->forcefield == PFIELD_GUIDE && eff->ob->type == OB_CURVES_LEGACY) {
    Curve *cu = eff->ob->data;
    if (cu->flag & CU_PATH) {
      if (eff->ob->runtime.curve_cache == NULL ||
          eff->ob->runtime.curve_cache->anim_path_accum_length == NULL) {
        BKE_displist_make_curveTypes(depsgraph, eff->scene, eff->ob, false);
      }

      if (eff->ob->runtime.curve_cache->anim_path_accum_length) {
        BKE_where_on_path(
            eff->ob, 0.0, eff->guide_loc, eff->guide_dir, NULL, &eff->guide_radius, NULL);
        mul_m4_v3(eff->ob->obmat, eff->guide_loc);
        mul_mat3_m4_v3(eff->ob->obmat, eff->guide_dir);
      }
    }
  }
  else if (eff->pd->shape == PFIELD_SHAPE_SURFACE) {
    eff->surmd = (SurfaceModifierData *)BKE_modifiers_findby_type(eff->ob, eModifierType_Surface);
    if (eff->ob->type == OB_CURVES_LEGACY) {
      eff->flag |= PE_USE_NORMAL_DATA;
    }
  }
  else if (eff->psys) {
    psys_update_particle_tree(eff->psys, ctime);
  }
}

static void add_effector_relation(ListBase *relations,
                                  Object *ob,
                                  ParticleSystem *psys,
                                  PartDeflect *pd)
{
  EffectorRelation *relation = MEM_callocN(sizeof(EffectorRelation), "EffectorRelation");
  relation->ob = ob;
  relation->psys = psys;
  relation->pd = pd;

  BLI_addtail(relations, relation);
}

static void add_effector_evaluation(ListBase **effectors,
                                    Depsgraph *depsgraph,
                                    Scene *scene,
                                    Object *ob,
                                    ParticleSystem *psys,
                                    PartDeflect *pd)
{
  if (*effectors == NULL) {
    *effectors = MEM_callocN(sizeof(ListBase), "effector effectors");
  }

  EffectorCache *eff = MEM_callocN(sizeof(EffectorCache), "EffectorCache");
  eff->depsgraph = depsgraph;
  eff->scene = scene;
  eff->ob = ob;
  eff->psys = psys;
  eff->pd = pd;
  eff->frame = -1;
  BLI_addtail(*effectors, eff);

  precalculate_effector(depsgraph, eff);
}
