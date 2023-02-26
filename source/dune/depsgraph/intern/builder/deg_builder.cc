#include "intern/builder/deg_builder.h"

#include <cstring>

#include "types_id.h"
#include "types_anim.h"
#include "types_armature.h"
#include "types_layer.h"
#include "types_object.h"

#include "lib_stack.h"
#include "lib_utildefines.h"

#include "dune_action.h"

#include "api_prototypes.h"

#include "intern/builder/deg_builder_cache.h"
#include "intern/builder/deg_builder_remove_noop.h"
#include "intern/depsgraph.h"
#include "intern/depsgraph_relation.h"
#include "intern/depsgraph_tag.h"
#include "intern/depsgraph_type.h"
#include "intern/eval/deg_eval_copy_on_write.h"
#include "intern/node/deg_node.h"
#include "intern/node/deg_node_component.h"
#include "intern/node/deg_node_id.h"
#include "intern/node/deg_node_operation.h"

#include "dgraph.h"

namespace dune::deg {

bool deg_check_id_in_dgraph(const DGraph *graph, ID *id_orig)
{
  IdNode *id_node = graph->find_id_node(id_orig);
  return id_node != nullptr;
}

bool deg_check_base_in_dgraph(const Depsgraph *graph, Base *base)
{
  Object *object_orig = base->base_orig->object;
  IdNode *id_node = graph->find_id_node(&object_orig->id);
  if (id_node == nullptr) {
    return false;
  }
  return id_node->has_base;
}

/*******************************************************************************
 * Base class for builders.
 */

DGraphBuilder::DGraphBuilder(Main *dmain, DGraph *graph, DGraphBuilderCache *cache)
    : dmain_(dmain), graph_(graph), cache_(cache)
{
}

bool DGraphBuilder::need_pull_base_into_graph(Base *base)
{
  /* Simple check: enabled bases are always part of dependency graph. */
  const int base_flag = (graph_->mode == DAG_EVAL_VIEWPORT) ? BASE_ENABLED_VIEWPORT :
                                                              BASE_ENABLED_RENDER;
  if (base->flag & base_flag) {
    return true;
  }
  /* More involved check: since we don't support dynamic changes in dependency graph topology and
   * all visible objects are to be part of dependency graph, we pull all objects which has animated
   * visibility. */
  Object *object = base->object;
  AnimatedPropId prop_id;
  if (graph_->mode == DAG_EVAL_VIEWPORT) {
    property_id = AnimatedPropId(&object->id, &api_Object, "hide_viewport");
  }
  else if (graph_->mode == DAG_EVAL_RENDER) {
    prop_id = AnimatedPropId(&object->id, &api_Object, "hide_render");
  }
  else {
    lib_assert_msg(0, "Unknown evaluation mode.");
    return false;
  }
  return cache_->isPropAnimated(&object->id, prop_id);
}

bool DGraphBuilder::check_pchan_has_bbone(Object *object, const bPoseChannel *pchan)
{
  lib_assert(object->type == OB_ARMATURE);
  if (pchan == nullptr || pchan->bone == nullptr) {
    return false;
  }
  /* We don't really care whether segments are higher than 1 due to static user input (as in,
   * rigger entered value like 3 manually), or due to animation. In either way we need to create
   * special evaluation. */
  if (pchan->bone->segments > 1) {
    return true;
  }
  DArmature *armature = static_cast<bArmature *>(object->data);
  AnimatedPropId prop_id(&armature->id, &api_Bone, pchan->bone, "bbone_segments");
  /* Check both Object and Armature animation data, because drivers modifying Armature
   * state could easily be created in the Object AnimData. */
  return cache_->isPropAnimated(&object->id, prop_id) ||
         cache_->isPropAnimated(&armature->id, prop_id);
}

bool DGraphBuilder::check_pchan_has_bbone_segments(Object *object, const DPoseChannel *pchan)
{
  return check_pchan_has_bbone(object, pchan);
}

bool DGraphBuilder::check_pchan_has_bbone_segments(Object *object, const char *bone_name)
{
  const DPoseChannel *pchan = dune_pose_channel_find_name(object->pose, bone_name);
  return check_pchan_has_bbone_segments(object, pchan);
}

/*******************************************************************************
 * Builder finalizer.
 */

namespace {

void dgraph_build_flush_visibility(Depsgraph *graph)
{
  enum {
    DEG_NODE_VISITED = (1 << 0),
  };

  LibStack *stack = lib_stack_new(sizeof(OpNode *), "DEG flush layers stack");
  for (IdNode *id_node : graph->id_nodes) {
    for (ComponentNode *comp_node : id_node->components.values()) {
      comp_node->affects_directly_visible |= id_node->is_directly_visible;
    }
  }
  for (OpNode *op_node : graph->operations) {
    op_node->custom_flags = 0;
    op_node->num_links_pending = 0;
    for (Relation *rel : op_node->outlinks) {
      if ((rel->from->type == NodeType::OPERATION) && (rel->flag & RELATION_FLAG_CYCLIC) == 0) {
        ++op_node->num_links_pending;
      }
    }
    if (op_node->num_links_pending == 0) {
      lib_stack_push(stack, &op_node);
      op_node->custom_flags |= DEG_NODE_VISITED;
    }
  }
  while (!lib_stack_is_empty(stack)) {
    OpNode *op_node;
    lib_stack_pop(stack, &op_node);
    /* Flush layers to parents. */
    for (Relation *rel : op_node->inlinks) {
      if (rel->from->type == NodeType::OPERATION) {
        OpNode *op_from = (OpNode *)rel->from;
        ComponentNode *comp_from = op_from->owner;
        const bool target_directly_visible = op_node->owner->affects_directly_visible;

        /* Visibility component forces all components of the current ID to be considered as
         * affecting directly visible. */
        if (comp_from->type == NodeType::VISIBILITY) {
          if (target_directly_visible) {
            IdNode *id_node_from = comp_from->owner;
            for (ComponentNode *comp_node : id_node_from->components.values()) {
              comp_node->affects_directly_visible |= target_directly_visible;
            }
          }
        }
        else {
          comp_from->affects_directly_visible |= target_directly_visible;
        }
      }
    }
    /* Schedule parent nodes. */
    for (Relation *rel : op_node->inlinks) {
      if (rel->from->type == NodeType::OPERATION) {
        OpNode *op_from = (OpNode *)rel->from;
        if ((rel->flag & RELATION_FLAG_CYCLIC) == 0) {
          lib_assert(op_from->num_links_pending > 0);
          --op_from->num_links_pending;
        }
        if ((op_from->num_links_pending == 0) && (op_from->custom_flags & DEG_NODE_VISITED) == 0) {
          lib_stack_push(stack, &op_from);
          op_from->custom_flags |= DEG_NODE_VISITED;
        }
      }
    }
  }
  lib_stack_free(stack);
}

}  // namespace

void dgraph_build_finalize(Main *dmain, DGraph *graph)
{
  /* Make sure dependencies of visible ID datablocks are visible. */
  dgraph_build_flush_visibility(graph);
  dgraph_remove_unused_noops(graph);

  /* Re-tag IDs for update if it was tagged before the relations
   * update tag. */
  for (IdNode *id_node : graph->id_nodes) {
    Id *id_orig = id_node->id_orig;
    id_node->finalize_build(graph);
    int flag = 0;
    /* Tag rebuild if special evaluation flags changed. */
    if (id_node->eval_flags != id_node->previous_eval_flags) {
      flag |= ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY;
    }
    /* Tag rebuild if the custom data mask changed. */
    if (id_node->customdata_masks != id_node->previous_customdata_masks) {
      flag |= ID_RECALC_GEOMETRY;
    }
    if (!deg_copy_on_write_is_expanded(id_node->id_cow)) {
      flag |= ID_RECALC_COPY_ON_WRITE;
      /* This means ID is being added to the dependency graph first
       * time, which is similar to "ob-visible-change" */
      if (GS(id_orig->name) == ID_OB) {
        flag |= ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY;
      }
    }
    /* Restore recalc flags from original ID, which could possibly contain recalc flags set by
     * an operator and then were carried on by the undo system. */
    flag |= id_orig->recalc;
    if (flag != 0) {
      graph_id_tag_update(dmain, graph, id_node->id_orig, flag, DEG_UPDATE_SOURCE_RELATIONS);
    }
  }
}

}  // namespace dune::deg
