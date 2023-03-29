/**
 * Physics utilities for effectors and collision.
 */

#pragma once

#include "dgraph.h"

struct DGraphNodeHandle;
struct DGraph;
struct EffectorWeights;
struct ListBase;
struct Object;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ePhysRelationType {
  DGRAPH_PHYS_EFFECTOR = 0,
  DGRAPH_PHYS_COLLISION = 1,
  DGRAPH_PHYS_SMOKE_COLLISION = 2,
  DGRAPH_PHYS_DYNAMIC_BRUSH = 3,
  DGRAPH_PHYS_RELATIONS_NUM = 4,
} ePhysRelationType;

/* Get collision/effector relations from collection or entire scene. These
 * created during dgraph relations building and should only be accessed
 * during evaluation. */
struct ListBase *dgraph_get_effector_relations(const struct DGraph *dgraph,
                                            struct Collection *collection);
struct ListBase *dgraph_get_collision_relations(const struct DGraph *dgraph,
                                             struct Collection *collection,
                                             unsigned int modifier_type);

/* Build collision/effector relations for dgraph. */
typedef bool (*dgraph_CollobjFilterFn)(struct Object *obj, struct ModifierData *md);

void dgraph_add_collision_relations(struct DGraphNodeHandle *handle,
                                    struct Object *object,
                                    struct Collection *collection,
                                    unsigned int modifier_type,
                                    dgraph_CollobjFilterFn filter_function,
                                    const char *name);
void dgraph_add_forcefield_relations(struct DGraphNodeHandle *handle,
                                  struct Object *object,
                                  struct EffectorWeights *eff,
                                  bool add_absorption,
                                  int skip_forcefield,
                                  const char *name);

#ifdef __cplusplus
} /* extern "C" */
#endif
