/**
 * Methods for constructing dgraph.
 */

#include "MEM_guardedalloc.h"

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

#include "dgraph.h"
#include "dgraph_build.h"
#include "dgraph_debug.h"

#include "builder/dgraph_builder_relations.h"
#include "builder/pipeline_all_objects.h"
#include "builder/pipeline_compositor.h"
#include "builder/pipeline_from_ids.h"
#include "builder/pipeline_render.h"
#include "builder/pipeline_view_layer.h"

#include "intern/debug/dgraph_debug.h"

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_operation.h"

#include "intern/dgraph_registry.h"
#include "intern/dgraph_relation.h"
#include "intern/draph_tag.h"
#include "intern/dgraph_type.h"

/* ****************** */
/* External Build API */

namespace dgraph = dune::dgraph;

static dgraph::NodeType dgraph_build_scene_component_type(eDGraphSceneComponentType component)
{
  switch (component) {
    case DGRAPH_SCENE_COMP_PARAMS:
      return dgraph::NodeType::PARAMS;
    case DGRAPH_SCENE_COMP_ANIMATION:
      return dgraph::NodeType::ANIMATION;
    case DGRAPH_SCENE_COMP_SEQUENCER:
      return dgraph::NodeType::SEQUENCER;
  }
  return dgraph::NodeType::UNDEFINED;
}

static dgraph::DNodeHandle *get_node_handle(DNodeHandle *node_handle)
{
  return reinterpret_cast<deg::DNodeHandle *>(node_handle);
}

void dgraph_add_scene_relation(DNodeHandle *node_handle,
                            Scene *scene,
                            eDGraphSceneComponentType component,
                            const char *description)
{
  dgraph::NodeType type = deg_build_scene_component_type(component);
  dgraph::ComponentKey comp_key(&scene->id, type);
  dgraph::DNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(comp_key, dgraph_node_handle, description);
}

void dgraph_add_object_relation(DNodeHandle *node_handle,
                             Object *object,
                             eDGraphObjectComponentType component,
                             const char *description)
{
  dgraph::NodeType type = dgraph::nodeTypeFromObjectComponent(component);
  dgraph::ComponentKey comp_key(&object->id, type);
  dgraph::DNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(comp_key, dgraph_node_handle, description);
}

bool dgraph_object_has_geometry_component(Object *object)
{
  return dgraph::geometry_tag_to_component(&object->id) != dgraph::NodeType::UNDEFINED;
}

void dgraph_add_collection_geometry_relation(DNodeHandle *node_handle,
                                             Collection *collection,
                                             const char *description)
{
  dgraph::OpKey op_key{
      &collection->id, dgraph::NodeType::GEOMETRY, dgraph::OpCode::GEOMETRY_EVAL_DONE};
  dgraph::DNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(op_key, dgraph_node_handle, description);
}

void dgraph_add_collection_geometry_customdata_mask(DNodeHandle *node_handle,
                                                 Collection *collection,
                                                 const CustomData_MeshMasks *masks)
{
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, ob) {
    dgraph_add_customdata_mask(node_handle, ob, masks);
    if (ob->type == OB_EMPTY && ob->instance_collection != nullptr) {
      dgraph_add_collection_geometry_customdata_mask(node_handle, ob->instance_collection, masks);
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

void dgraph_add_simulation_relation(DNodeHandle *node_handle,
                                    Simulation *simulation,
                                    const char *description)
{
  dgraph::OpKey op_key(
      &simulation->id, dgraph::NodeType::SIMULATION, dgraph::OpCode::SIMULATION_EVAL);
  dgraph::DNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(op_key, dgraph_node_handle, description);
}

void dgraph_add_node_tree_output_relation(DNodeHandle *node_handle,
                                          DNodeTree *node_tree,
                                          const char *description)
{
  dgraph::OpKey ntree_output_key(
      &node_tree->id, dgraph::NodeType::NTREE_OUTPUT, dgraph::OpCode::NTREE_OUTPUT);
  dgraph::DNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(
      ntree_output_key, dgraph_node_handle, description);
}

void dgraph_add_object_cache_relation(DNodeHandle *node_handle,
                                   CacheFile *cache_file,
                                   eDGraphObjectComponentType component,
                                   const char *description)
{
  dgraph::NodeType type = deg::nodeTypeFromObjectComponent(component);
  dgraph::ComponentKey comp_key(&cache_file->id, type);
  dgraph::DepsNodeHandle *deg_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(comp_key, deg_node_handle, description);
}

void dgraph_add_bone_relation(DGraphNodeHandle *node_handle,
                           Object *object,
                           const char *bone_name,
                           eDGraphObjectComponentType component,
                           const char *description)
{
  dgraph::NodeType type = dgraph::nodeTypeFromObjectComponent(component);
  dgraph::ComponentKey comp_key(&object->id, type, bone_name);
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(comp_key, dgraph_node_handle, description);
}

void dgraph_add_object_pointcache_relation(struct DGraphNodeHandle *node_handle,
                                        struct Object *object,
                                        eDGraphObjectComponentType component,
                                        const char *description)
{
  dgraph::NodeType type = dgraph::nodeTypeFromObjectComponent(component);
  dgraph::ComponentKey comp_key(&object->id, type);
  dgraph::DGraphNodeHandle *deg_node_handle = get_node_handle(node_handle);
  dgraph::DGraphRelationBuilder *relation_builder = dgraph_node_handle->builder;
  /* Add relation from source to the node handle. */
  relation_builder->add_node_handle_relation(comp_key, deg_node_handle, description);
  /* Node deduct point cache component and connect source to it. */
  Id *id = dgraph_get_id_from_handle(node_handle);
  dgraph::ComponentKey point_cache_key(id, dgraph::NodeType::POINT_CACHE);
  dgraph::Relation *rel = relation_builder->add_relation(comp_key, point_cache_key, "Point Cache");
  if (rel != nullptr) {
    rel->flag |= dgraph::RELATION_FLAG_FLUSH_USER_EDIT_ONLY;
  }
  else {
    fprintf(stderr, "Error in point cache relation from %s to ^%s.\n", object->id.name, id->name);
  }
}

void dgraph_add_generic_id_relation(struct DGraphNodeHandle *node_handle,
                                 struct Id *id,
                                 const char *description)
{
  dgraph::OpKey op_key(
      id, dgraph::NodeType::GENERIC_DATABLOCK, dgraph::OpCode::GENERIC_DATABLOCK_UPDATE);
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_node_handle_relation(op_key, dgraph_node_handle, description);
}

void dgraph_add_modifier_to_transform_relation(struct DGraphNodeHandle *node_handle,
                                               const char *description)
{
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_modifier_to_transform_relation(dgraph_node_handle, description);
}

void dgraph_add_special_eval_flag(struct DGraphNodeHandle *node_handle, Id *id, uint32_t flag)
{
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_special_eval_flag(id, flag);
}

void dgraph_add_customdata_mask(struct DGraphNodeHandle *node_handle,
                             struct Object *object,
                             const CustomData_MeshMasks *masks)
{
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph_node_handle->builder->add_customdata_mask(object, dgraph::DGraphCustomDataMeshMasks(masks));
}

struct Id *dgraph_get_id_from_handle(struct DGraphNodeHandle *node_handle)
{
  dgraph::DGraphNodeHandle *dgraph_handle = get_node_handle(node_handle);
  return dgraph_handle->node->owner->owner->id_orig;
}

struct DGraph *dgraph_get_graph_from_handle(struct DGraphNodeHandle *node_handle)
{
  dgraph::DGraphNodeHandle *dgraph_node_handle = get_node_handle(node_handle);
  dgraph::DGraphRelationBuilder *relation_builder = dgraph_node_handle->builder;
  return reinterpret_cast<DGraph *>(relation_builder->getGraph());
}

/* ******************** */
/* Graph Building API's */

void dgraph_build_from_view_layer(DGraph *graph)
{
  dgraph::ViewLayerBuilderPipeline builder(graph);
  builder.build();
}

void dgraph_build_for_all_objects(struct DGraph *graph)
{
  dgraph::AllObjectsBuilderPipeline builder(graph);
  builder.build();
}

void deg_graph_build_for_render_pipeline(Depsgraph *graph)
{
  deg::RenderBuilderPipeline builder(graph);
  builder.build();
}

void deg_graph_build_for_compositor_preview(Depsgraph *graph, bNodeTree *nodetree)
{
  deg::CompositorBuilderPipeline builder(graph, nodetree);
  builder.build();
}

void deg_graph_build_from_ids(Depsgraph *graph, ID **ids, const int num_ids)
{
  deg::FromIDsBuilderPipeline builder(graph, blender::Span(ids, num_ids));
  builder.build();
}

void deg_graph_tag_relations_update(Depsgraph *graph)
{
  DEG_DEBUG_PRINTF(graph, TAG, "%s: Tagging relations for update.\n", __func__);
  deg::Depsgraph *deg_graph = reinterpret_cast<deg::Depsgraph *>(graph);
  deg_graph->need_update = true;
  /* NOTE: When relations are updated, it's quite possible that
   * we've got new bases in the scene. This means, we need to
   * re-create flat array of bases in view layer.
   *
   * TODO(sergey): Try to make it so we don't flush updates
   * to the whole depsgraph. */
  deg::IDNode *id_node = deg_graph->find_id_node(&deg_graph->scene->id);
  if (id_node != nullptr) {
    id_node->tag_update(deg_graph, deg::DEG_UPDATE_SOURCE_RELATIONS);
  }
}

void dep_graph_relations_update(Depsgraph *graph)
{
  deg::Depsgraph *deg_graph = (deg::Depsgraph *)graph;
  if (!deg_graph->need_update) {
    /* Graph is up to date, nothing to do. */
    return;
  }
  deg_graph_build_from_view_layer(graph);
}

void deg_relations_tag_update(Main *bmain)
{
  DEG_GLOBAL_DEBUG_PRINTF(TAG, "%s: Tagging relations for update.\n", __func__);
  for (deg::Depsgraph *depsgraph : deg::get_all_registered_graphs(bmain)) {
    deg_graph_tag_relations_update(reinterpret_cast<Depsgraph *>(depsgraph));
  }
}
