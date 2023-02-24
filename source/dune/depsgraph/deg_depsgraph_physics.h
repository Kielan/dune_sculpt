/**
 * Physics utilities for effectors and collision.
 */

#pragma once

#include "DEG_depsgraph.h"

struct DepsNodeHandle;
struct Depsgraph;
struct EffectorWeights;
struct ListBase;
struct Object;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ePhysRelationType {
  DEG_PHYS_EFFECTOR = 0,
  DEG_PHYS_COLLISION = 1,
  DEG_PHYS_SMOKE_COLLISION = 2,
  DEG_PHYS_DYNAMIC_BRUSH = 3,
  DEG_PHYS_RELATIONS_NUM = 4,
} ePhysRelationType;

/* Get collision/effector relations from collection or entire scene. These
 * created during depsgraph relations building and should only be accessed
 * during evaluation. */
struct ListBase *deg_get_effector_relations(const struct Depsgraph *depsgraph,
                                            struct Collection *collection);
struct ListBase *deg_get_collision_relations(const struct Depsgraph *depsgraph,
                                             struct Collection *collection,
                                             unsigned int modifier_type);

/* Build collision/effector relations for depsgraph. */
typedef bool (*deg_CollobjFilterFunction)(struct Object *obj, struct ModifierData *md);

void deg_add_collision_relations(struct DepsNodeHandle *handle,
                                 struct Object *object,
                                 struct Collection *collection,
                                 unsigned int modifier_type,
                                 deg_CollobjFilterFunction filter_function,
                                 const char *name);
void deg_add_forcefield_relations(struct DepsNodeHandle *handle,
                                  struct Object *object,
                                  struct EffectorWeights *eff,
                                  bool add_absorption,
                                  int skip_forcefield,
                                  const char *name);

#ifdef __cplusplus
} /* extern "C" */
#endif
