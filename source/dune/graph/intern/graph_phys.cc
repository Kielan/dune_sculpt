/**  Physics utilities for effectors and collision. **/

#include "intern/graph_phys.h"

#include "mem_guardedalloc.h"

#include "lib_compiler_compat.h"
#include "lib_listbase.h"

#include "dune_collision.h"
#include "dune_effect.h"
#include "dune_modifier.h"

#include "types_collection.h"
#include "types_object_force.h"
#include "types_object.h"

#include "graph_build.h"
#include "graph_phys.h"
#include "graph_query.h"

#include "graph.h"

namespace graph = dune::graph;

/*************************** Evaluation Query API *****************************/

static ePhysRelationType modifier_to_relation_type(unsigned int modifier_type)
{
  switch (modifier_type) {
    case eModifierType_Collision:
      return GRAPH_PHYS_COLLISION;
    case eModifierType_Fluid:
      return GRAPH_PHYS_SMOKE_COLLISION;
    case eModifierType_DynamicPaint:
      return GRAPH_PHYS_DYNAMIC_BRUSH;
  }

  lib_assert_msg(0, "Unknown collision modifier type");
  return GRAPH_PHYS_RELATIONS_NUM;
}
/* Get id from an id type object, in a safe manner. This means that object can be nullptr,
 * in which case the function returns nullptr.
 */
template<class T> static Id *object_id_safe(T *object)
{
  if (object == nullptr) {
    return nullptr;
  }
  return &object->id;
}

ListBase *graph_get_effector_relations(const Graph *graph, Collection *collection)
{
  const dgraph::DGraph *dgraph = reinterpret_cast<const graph::Graph *>(graph);
  dune::Map<const Id *, ListBase *> *hash = graph->physics_relations[GRAPH_PHYS_EFFECTOR];
  if (hash == nullptr) {
    return nullptr;
  }
  /* NOTE: nullptr is a valid lookup key here as it means that the relation is not bound to a
   * specific collection. */
  Id *collection_orig = graph_get_original_id(object_id_safe(collection));
  return hash->lookup_default(collection_orig, nullptr);
}

ListBase *graph_get_collision_relations(const Graph *graph,
                                        Collection *collection,
                                        unsigned int modifier_type)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  const ePhysRelationType type = modifier_to_relation_type(modifier_type);
  dune::Map<const Id *, ListBase *> *hash = graph->phys_relations[type];
  if (hash == nullptr) {
    return nullptr;
  }
  /* NOTE: nullptr is a valid lookup key here as it means that the relation is not bound to a
   * specific collection. */
  Id *collection_orig = graph_get_original_id(object_id_safe(collection));
  return hash->lookup_default(collection_orig, nullptr);
}

/********************** DGraph Building API ************************/

void graph_add_collision_relations(NodeHandle *handle,
                                   Object *object,
                                   Collection *collection,
                                   unsigned int modifier_type,
                                   Graph_CollobjFilterFn filter_fn,
                                   const char *name)
{
  Graph *graph = graph_get_graph_from_handle(handle);
  graph::Graph *graph_graph = (graph::Graph *)graph;
  ListBase *relations = build_collision_relations(graph, collection, modifier_type);
  LISTBASE_FOREACH (CollisionRelation *, relation, relations) {
    Object *ob1 = relation->ob;
    if (ob1 == object) {
      continue;
    }
    if (filter_fn == nullptr ||
        filter_fn(ob1, dune_modifiers_findby_type(ob1, (ModifierType)modifier_type))) {
      graph_add_object_pointcache_relation(handle, ob1, GRAPH_OB_COMP_TRANSFORM, name);
      graph_add_object_pointcache_relation(handle, ob1, GRAPH_OB_COMP_GEOMETRY, name);
    }
  }
}

void graph_add_forcefield_relations(NodeHandle *handle,
                                    Object *object,
                                    EffectorWeights *effector_weights,
                                    bool add_absorption,
                                    int skip_forcefield,
                                    const char *name)
{
  Graph *graph = graph_get_graph_from_handle(handle);
  graph::Graph *graph = (graph::Graph *)graph;
  ListBase *relations = build_effector_relations(graph, effector_weights->group);
  LISTBASE_FOREACH (EffectorRelation *, relation, relations) {
    if (relation->ob == object) {
      continue;
    }
    if (relation->pd->forcefield == skip_forcefield) {
      continue;
    }

    /* Relation to forcefield object, optionally including geometry.
     * Use special point cache relations for automatic cache clearing. */
    graph_add_object_pointcache_relation(handle, relation->ob, GRAPH_OB_COMP_TRANSFORM, name);

    if (relation->psys || ELEM(relation->pd->shape, PFIELD_SHAPE_SURFACE, PFIELD_SHAPE_POINTS) ||
        relation->pd->forcefield == PFIELD_GUIDE) {
      /* TODO: Consider going more granular with more dedicated
       * particle system operation. */
      graph_add_object_pointcache_relation(handle, relation->ob, GRAPH_OB_COMP_GEOMETRY, name);
    }

    /* Smoke flow relations. */
    if (relation->pd->forcefield == PFIELD_FLUIDFLOW && relation->pd->f_source != nullptr) {
      graph_add_object_pointcache_relation(
          handle, relation->pd->f_source, GRAPH_OB_COMP_TRANSFORM, "Fluid Force Domain");
      graph_add_object_pointcache_relation(
          handle, relation->pd->f_source, GRAPH_OB_COMP_GEOMETRY, "Fluid Force Domain");
    }

    /* Absorption forces need collision relation. */
    if (add_absorption && (relation->pd->flag & PFIELD_VISIBILITY)) {
      graph_add_collision_relations(
          handle, object, nullptr, eModifierType_Collision, nullptr, "Force Absorption");
    }
  }
}

/******************************** Internal API ********************************/

namespace dune::dgraph {

ListBase *build_effector_relations(Graph *graph, Collection *collection)
{
  Map<const Id *, ListBase *> *hash = graph->phys_relations[GRAPH_PHYS_EFFECTOR];
  if (hash == nullptr) {
    graph->phys_relations[GRAPH_PHYS_EFFECTOR] = new Map<const Id *, ListBase *>();
    hash = graph->phys_relations[GRAPH_PHYS_EFFECTOR];
  }
  /* If collection is nullptr still use it as a key.
   * In this case the dune_effector_relations_create() will create relates for all bases in the
   * view layer.
   */
  Id *collection_id = object_id_safe(collection);
  return hash->lookup_or_add_cb(collection_id, [&]() {
    ::DGraph *dgraph = reinterpret_cast<::DGraph *>(graph);
    return dune_effector_relations_create(dgraph, graph->view_layer, collection);
  });
}

ListBase *build_collision_relations(DGraph *graph,
                                    Collection *collection,
                                    unsigned int modifier_type)
{
  const ePhysicsRelationType type = modifier_to_relation_type(modifier_type);
  Map<const Id *, ListBase *> *hash = graph->physics_relations[type];
  if (hash == nullptr) {
    graph->physics_relations[type] = new Map<const Id *, ListBase *>();
    hash = graph->physics_relations[type];
  }
  /* If collection is nullptr still use it as a key.
   * In this case the dune_collision_relations_create() will create relates for all bases in the
   * view layer.
   */
  Id *collection_id = object_id_safe(collection);
  return hash->lookup_or_add_cb(collection_id, [&]() {
    ::DGraph *dgraph = reinterpret_cast<::DGraph *>(graph);
    return dune_collision_relations_create(dgraph, collection, modifier_type);
  });
}

void clear_physics_relations(DGraph *graph)
{
  for (int i = 0; i < DGRAPH_PHYSICS_RELATIONS_NUM; i++) {
    Map<const Id *, ListBase *> *hash = graph->physics_relations[i];
    if (hash) {
      const ePhysicsRelationType type = (ePhysicsRelationType)i;

      switch (type) {
        case DGRAPH_PHYS_EFFECTOR:
          for (ListBase *list : hash->values()) {
            dune_effector_relations_free(list);
          }
          break;
        case DGRAPH_PHYS_COLLISION:
        case DGRAPH_PHYS_SMOKE_COLLISION:
        case DGRAPH_PHYS_DYNAMIC_BRUSH:
          for (ListBase *list : hash->values()) {
            dune_collision_relations_free(list);
          }
          break;
        case DGRAPH_PHYS_RELATIONS_NUM:
          break;
      }
      delete hash;
      graph->physics_relations[i] = nullptr;
    }
  }
}

}  // namespace dune::dgraph
