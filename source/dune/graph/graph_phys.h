/* Phys utils for effectors and collision. */

#pragma once

#include "graph.h"

struct GraphNodeHandle;
struct Graph;
struct EffectorWeights;
struct ListBase;
struct Object;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ePhysRelationType {
  GRAPH_PHYS_EFFECTOR = 0,
  GRAPH_PHYS_COLLISION = 1,
  GRAPH_PHYS_SMOKE_COLLISION = 2,
  GRAPH_PHYS_DYNAMIC_BRUSH = 3,
  GRAPH_PHYS_RELATIONS_NUM = 4,
} ePhysRelationType;

/* Get collision/effector relations from collection or entire scene. These
 * created during dgraph relations building and should only be accessed
 * during evaluation. */
struct List *graph_get_effector_relations(const struct Graph *graph,
                                            struct Collection *collection);
struct List *graph_get_collision_relations(const struct Graph *graph,
                                             struct Collection *collection,
                                             unsigned int modifier_type);

/* Build collision/effector relations for graph. */
typedef bool (*graph_CollobjFilterFn)(struct Object *obj, struct ModifierData *md);

void graph_add_collision_relations(struct GraphNodeHandle *handle,
                                    struct Object *object,
                                    struct Collection *collection,
                                    unsigned int modifier_type,
                                    graph_CollobjFilterFn filter_function,
                                    const char *name);
void graph_add_forcefield_relations(struct GraphNodeHandle *handle,
                                  struct Object *object,
                                  struct EffectorWeights *eff,
                                  bool add_absorption,
                                  int skip_forcefield,
                                  const char *name);

#ifdef __cplusplus
} /* extern "C" */
#endif
