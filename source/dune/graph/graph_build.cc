/** Methods for constructing dgraph. **/

#include "mem_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "PIL_time.h"
#include "PIL_time_utildefines.h"

#include "types_cachefile.h"
#include "types_collection.h"
#include "types_node.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_simulation.h"

#include "dune_collection.h"
#include "dune_main.h"
#include "dune_scene.h"

#include "graph.h"
#include "graph_build.h"
#include "graph_debug.h"

#include "builder/graph_builder_relations.h"
#include "builder/pipeline_all_objects.h"
#include "builder/pipeline_compositor.h"
#include "builder/pipeline_from_ids.h"
#include "builder/pipeline_render.h"
#include "builder/pipeline_view_layer.h"

#include "intern/debug/graph_debug.h"

#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_operation.h"

#include "intern/graph_registry.h"
#include "intern/graph_relation.h"
#include "intern/graph_tag.h"
#include "intern/graph_type.h"

/* ****************** */
/* External Build API */

namespace graph = dune::graph;

static graph::NodeType graph_build_scene_component_type(eGraphSceneComponentType component)
{
  switch (component) {
    case GRAPH_SCENE_COMP_PARAMS:
      return graph::NodeType::PARAMS;
    case GRAPH_SCENE_COMP_ANIMATION:
      return graph::NodeType::ANIMATION;
    case GRAPH_SCENE_COMP_SEQUENCER:
      return graph::NodeType::SEQUENCER;
  }
  return graph::NodeType::UNDEFINED;
}

static graph::NodeHandle *get_node_handle(NodeHandle *node_handle)
{
  return reinterpret_cast<deg::NodeHandle *>(node_handle);
}

void graph_add_scene_relation(NodeHandle *node_handle,
                              Scene *scene,
                              eGraphSceneComponentType component,
                              const char *description)
{
  graph::NodeType type = graph_build_scene_component_type(component);
  graph::ComponentKey comp_key(&scene->id, type);
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(comp_key, graph_node_handle, description);
}

void graph_add_object_relation(NodeHandle *node_handle,
                                Object *object,
                                eGraphObjectComponentType component,
                                const char *description)
{
  graph::NodeType type = graph::nodeTypeFromObjectComponent(component);
  graph::ComponentKey comp_key(&object->id, type);
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(comp_key, graph_node_handle, description);
}

bool graph_object_has_geometry_component(Object *object)
{
  return graph::geometry_tag_to_component(&object->id) != graph::NodeType::UNDEFINED;
}

void ggraph_add_collection_geometry_relation(NodeHandle *node_handle,
                                             Collection *collection,
                                             const char *description)
{
  graph::OpKey op_key{
      &collection->id, graph::NodeType::GEOMETRY, graph::OpCode::GEOMETRY_EVAL_DONE};
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(op_key, graph_node_handle, description);
}

void graph_add_collection_geometry_customdata_mask(NodeHandle *node_handle,
                                                    Collection *collection,
                                                    const CustomData_MeshMasks *masks)
{
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, ob) {
    graph_add_customdata_mask(node_handle, ob, masks);
    if (ob->type == OB_EMPTY && ob->instance_collection != nullptr) {
      graph_add_collection_geometry_customdata_mask(node_handle, ob->instance_collection, masks);
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

void graph_add_simulation_relation(NodeHandle *node_handle,
                                   Simulation *simulation,
                                   const char *description)
{
  graph::OpKey op_key(
      &simulation->id, graph::NodeType::SIMULATION, graph::OpCode::SIMULATION_EVAL);
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(op_key, graph_node_handle, description);
}

void graph_add_node_tree_output_relation(NodeHandle *node_handle,
                                         NodeTree *node_tree,
                                         const char *description)
{
  graph::OpKey ntree_output_key(
      &node_tree->id, graph::NodeType::NTREE_OUTPUT, graph::OpCode::NTREE_OUTPUT);
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(
      ntree_output_key, graph_node_handle, description);
}

void graph_add_object_cache_relation(NodeHandle *node_handle,
                                     CacheFile *cache_file,
                                     eGraphObjectComponentType component,
                                     const char *description)
{
  graph::NodeType type = graph::nodeTypeFromObjectComponent(component);
  graph::ComponentKey comp_key(&cache_file->id, type);
  graph::NodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(comp_key, deg_node_handle, description);
}

void graph_add_bone_relation(GraphNodeHandle *node_handle,
                             Object *object,
                             const char *bone_name,
                             eGraphObjectComponentType component,
                             const char *description)
{
  graph::NodeType type = graph::nodeTypeFromObjectComponent(component);
  graph::ComponentKey comp_key(&object->id, type, bone_name);
  graph::GraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(comp_key, graph_node_handle, description);
}

void graph_add_object_pointcache_relation(struct GraphNodeHandle *node_handle,
                                          struct Object *object,
                                          eGraphObjectComponentType component,
                                          const char *description)
{
  graph::NodeType type = graph::nodeTypeFromObjectComponent(component);
  graph::ComponentKey comp_key(&object->id, type);
  graph::GraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph::GraphRelationBuilder *relation_builder = graph_node_handle->builder;
  /* Add relation from source to the node handle. */
  relation_builder->add_node_handle_relation(comp_key, deg_node_handle, description);
  /* Node deduct point cache component and connect source to it. */
  Id *id = graph_get_id_from_handle(node_handle);
  graph::ComponentKey point_cache_key(id, graph::NodeType::POINT_CACHE);
  graph::Relation *rel = relation_builder->add_relation(comp_key, point_cache_key, "Point Cache");
  if (rel != nullptr) {
    rel->flag |= graph::RELATION_FLAG_FLUSH_USER_EDIT_ONLY;
  }
  else {
    fprintf(stderr, "Error in point cache relation from %s to ^%s.\n", object->id.name, id->name);
  }
}

void graph_add_generic_id_relation(struct GraphNodeHandle *node_handle,
                                   struct Id *id,
                                   const char *description)
{
  graph::OpKey op_key(
      id, graph::NodeType::GENERIC_DATABLOCK, graph::OpCode::GENERIC_DATABLOCK_UPDATE);
  graph::DGraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_node_handle_relation(op_key, graph_node_handle, description);
}

void graph_add_modifier_to_transform_relation(struct GraphNodeHandle *node_handle,
                                               const char *description)
{
  graph::DGraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_modifier_to_transform_relation(graph_node_handle, description);
}

void graph_add_special_eval_flag(struct GraphNodeHandle *node_handle, Id *id, uint32_t flag)
{
  graph::GraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_special_eval_flag(id, flag);
}

void graph_add_customdata_mask(struct GraphNodeHandle *node_handle,
                               struct Object *object,
                               const CustomData_MeshMasks *masks)
{
  graph::GraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph_node_handle->builder->add_customdata_mask(object, graph::GraphCustomDataMeshMasks(masks));
}

struct Id *graph_get_id_from_handle(struct GraphNodeHandle *node_handle)
{
  graph::GraphNodeHandle *graph_handle = get_node_handle(node_handle);
  return graph_handle->node->owner->owner->id_orig;
}

struct Graph *graph_get_graph_from_handle(struct GraphNodeHandle *node_handle)
{
  graph::GraphNodeHandle *graph_node_handle = get_node_handle(node_handle);
  graph::GraphRelationBuilder *relation_builder = graph_node_handle->builder;
  return reinterpret_cast<Graph *>(relation_builder->getGraph());
}

/* ******************** */
/* Graph Building API's */

void graph_build_from_view_layer(Graph *graph)
{
  graph::ViewLayerBuilderPipeline builder(graph);
  builder.build();
}

void graph_build_for_all_objects(struct Graph *graph)
{
  graph::AllObjectsBuilderPipeline builder(graph);
  builder.build();
}

void graph_build_for_render_pipeline(Graph *graph)
{
  graph::RenderBuilderPipeline builder(graph);
  builder.build();
}

void graph_build_for_compositor_preview(Graph *graph, NodeTree *nodetree)
{
  graph::CompositorBuilderPipeline builder(graph, nodetree);
  builder.build();
}

void graph_build_from_ids(Graph *graph, Id **ids, const int num_ids)
{
  graph::FromIdsBuilderPipeline builder(graph, dune::Span(ids, num_ids));
  builder.build();
}

void graph_tag_relations_update(Graph *graph)
{
  GRAPH_DEBUG_PRINTF(graph, TAG, "%s: Tagging relations for update.\n", __func__);
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  graph->need_update = true;
  /* NOTE: When relations are updated, it's quite possible that
   * we've got new bases in the scene. This means, we need to
   * re-create flat array of bases in view layer.
   *
   * TODO: Try to make it so we don't flush updates
   * to the whole d dependency graph. */
  graph::IdNode *id_node = dgraph->find_id_node(&graph->scene->id);
  if (id_node != nullptr) {
    id_node->tag_update(graph, graph::GRAPH_UPDATE_SOURCE_RELATIONS);
  }
}

void graph_relations_update(Graph *graph)
{
  graph::Graph *graph = (graph::Graph *)graph;
  if (!graph->need_update) {
    /* Graph is up to date, nothing to do. */
    return;
  }
  graph_build_from_view_layer(graph);
}

void graph_relations_tag_update(Main *dmain)
{
  GRAPH_GLOBAL_DEBUG_PRINTF(TAG, "%s: Tagging relations for update.\n", __func__);
  for (graph::Graph *graph : graph::get_all_registered_graphs(dmain)) {
    graph_graph_tag_relations_update(reinterpret_cast<DGraph *>(graph));
  }
}
