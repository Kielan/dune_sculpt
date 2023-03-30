/**
 * Core routines for how the Depsgraph works.
 */

#include "intern/graph_tag.h"

#include <cstdio>
#include <cstring> /* required for memset */
#include <queue>

#include "lib_math_bits.h"
#include "lib_task.h"
#include "lib_utildefines.h"

#include "types_anim.h"
#include "types_curve.h"
#include "types_key.h"
#include "types_lattice.h"
#include "types_mesh.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_screen.h"
#include "types_windowmanager.h"

#include "dune_anim_data.h"
#include "dune_global.h"
#include "dune_idtype.h"
#include "dune_node.h"
#include "dune_scene.h"
#include "dune_screen.h"
#include "dune_workspace.h"

#include "graph.h"
#include "graph_debug.h"
#include "graph_query.h"

#include "intern/builder/graph_builder.h"
#include "intern/graph.h"
#include "intern/graph_registry.h"
#include "intern/graph_update.h"
#include "intern/eval/graph_eval_copy_on_write.h"
#include "intern/eval/graph_eval_flush.h"
#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_factory.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_op.h"
#include "intern/node/graph_node_time.h"

namespace dune = dune::graph;

/* *********************** */
/* Update Tagging/Flushing */

namespace dune::graph {

namespace {

void graph_geometry_tag_to_component(const Id *id, NodeType *component_type)
{
  const NodeType result = geometry_tag_to_component(id);
  if (result != NodeType::UNDEFINED) {
    *component_type = result;
  }
}

bool is_selectable_data_id_type(const IdType id_type)
{
  return ELEM(id_type, ID_ME, ID_CU_LEGACY, ID_MB, ID_LT, ID_GD, ID_CV, ID_PT, ID_VO);
}

void graph_select_tag_to_component_opcode(const Id *id,
                                          NodeType *component_type,
                                          OpCode *op_code)
{
  const IdType id_type = GS(id->name);
  if (id_type == ID_SCE) {
    /* We need to flush base flags to all objects in a scene since we
     * don't know which ones changed. However, we don't want to update
     * the whole scene, so pick up some operation which will do as less
     * as possible.
     *
     * TODO: We can introduce explicit exit operation which
     * does nothing and which is only used to cascade flush down the
     * road. */
    *component_type = NodeType::LAYER_COLLECTIONS;
    *op_code = OpCode::VIEW_LAYER_EVAL;
  }
  else if (id_type == ID_OB) {
    *component_type = NodeType::OBJECT_FROM_LAYER;
    *op_code = OpCode::OBJECT_FROM_LAYER_ENTRY;
  }
  else if (id_type == ID_MC) {
    *component_type = NodeType::BATCH_CACHE;
    *op_code = OpCode::MOVIECLIP_SELECT_UPDATE;
  }
  else if (is_selectable_data_id_type(id_type)) {
    *component_type = NodeType::BATCH_CACHE;
    *op_code = OpCode::GEOMETRY_SELECT_UPDATE;
  }
  else {
    *component_type = NodeType::COPY_ON_WRITE;
    *op_code = OpCode::COPY_ON_WRITE;
  }
}

void graph_base_flags_tag_to_component_opcode(const Id *id,
                                               NodeType *component_type,
                                               OpCode *operation_code)
{
  const IdType id_type = GS(id->name);
  if (id_type == ID_SCE) {
    *component_type = NodeType::LAYER_COLLECTIONS;
    *op_code = OpCode::VIEW_LAYER_EVAL;
  }
  else if (id_type == ID_OB) {
    *component_type = NodeType::OBJECT_FROM_LAYER;
    *op_code = OpCode::OBJECT_BASE_FLAGS;
  }
}

OpCode psysTagToOpCode(IdRecalcFlag tag)
{
  if (tag == ID_RECALC_PSYS_RESET) {
    return OpCode::PARTICLE_SETTINGS_RESET;
  }
  return OpCode::OPERATION;
}

void graph_tag_to_component_opcode(const Id *id,
                                   IdRecalcFlag tag,
                                   NodeType *component_type,
                                   OpCode *op_code)
{
  const IdType id_type = GS(id->name);
  *component_type = NodeType::UNDEFINED;
  *operation_code = OpCode::OPERATION;
  /* Special case for now, in the future we should get rid of this. */
  if (tag == 0) {
    *component_type = NodeType::ID_REF;
    *op_code = OpCode::OPERATION;
    return;
  }
  switch (tag) {
    case ID_RECALC_TRANSFORM:
      *component_type = NodeType::TRANSFORM;
      break;
    case ID_RECALC_GEOMETRY:
      dgraph_geometry_tag_to_component(id, component_type);
      break;
    case ID_RECALC_ANIMATION:
      *component_type = NodeType::ANIMATION;
      break;
    case ID_RECALC_PSYS_REDO:
    case ID_RECALC_PSYS_RESET:
    case ID_RECALC_PSYS_CHILD:
    case ID_RECALC_PSYS_PHYS:
      if (id_type == ID_PA) {
        /* NOTES:
         * - For particle settings node we need to use different
         *   component. Will be nice to get this unified with object,
         *   but we can survive for now with single exception here.
         *   Particles needs reconsideration anyway, */
        *component_type = NodeType::PARTICLE_SETTINGS;
        *op_code = psysTagToOpCode(tag);
      }
      else {
        *component_type = NodeType::PARTICLE_SYSTEM;
      }
      break;
    case ID_RECALC_COPY_ON_WRITE:
      *component_type = NodeType::COPY_ON_WRITE;
      break;
    case ID_RECALC_SHADING:
      *component_type = NodeType::SHADING;
      break;
    case ID_RECALC_SELECT:
      graph_select_tag_to_component_opcode(id, component_type, op_code);
      break;
    case ID_RECALC_BASE_FLAGS:
      dgraph_base_flags_tag_to_component_opcode(id, component_type, op_code);
      break;
    case ID_RECALC_POINT_CACHE:
      *component_type = NodeType::POINT_CACHE;
      break;
    case ID_RECALC_EDITORS:
      /* There is no such node in depsgraph, this tag is to be handled
       * separately. */
      break;
    case ID_RECALC_SEQUENCER_STRIPS:
      *component_type = NodeType::SEQUENCER;
      break;
    case ID_RECALC_FRAME_CHANGE:
    case ID_RECALC_AUDIO_FPS:
    case ID_RECALC_AUDIO_VOLUME:
    case ID_RECALC_AUDIO_MUTE:
    case ID_RECALC_AUDIO_LISTENER:
    case ID_RECALC_AUDIO:
      *component_type = NodeType::AUDIO;
      break;
    case ID_RECALC_PARAMS:
      *component_type = NodeType::PARAMS;
      break;
    case ID_RECALC_SOURCE:
      *component_type = NodeType::PARAMS;
      break;
    case ID_RECALC_GEOMETRY_ALL_MODES:
    case ID_RECALC_ALL:
    case ID_RECALC_PSYS_ALL:
      lib_assert_msg(0, "Should not happen");
      break;
    case ID_RECALC_TAG_FOR_UNDO:
      break; /* Must be ignored by depsgraph. */
    case ID_RECALC_NTREE_OUTPUT:
      *component_type = NodeType::NTREE_OUTPUT;
      *op_code = OpCode::NTREE_OUTPUT;
      break;
  }
}

void id_tag_update_ntree_special(
    Main *dmain, Graph *graph, Id *id, int flag, eUpdateSource update_source)
{
  NodeTree *ntree = ntreeFromId(id);
  if (ntree == nullptr) {
    return;
  }
  graph_id_tag_update(dmain, graph, &ntree->id, flag, update_source);
}

void graph_update_editors_tag(Main *dmain, Graph *graph, Id *id)
{
  /* NOTE: We handle this immediately, without delaying anything, to be
   * sure we don't cause threading issues with OpenGL. */
  /* TODO: Make sure this works for CoW-ed data-blocks as well. */
  GraphEditorUpdateContext update_ctx = {nullptr};
  update_ctx.dmain = dmain;
  update_ctx.dgraph = (::Graph *)graph;
  update_ctx.scene = graph->scene;
  update_ctx.view_layer = graph->view_layer;
  graph_editors_id_update(&update_ctx, id);
}

void graph_id_tag_copy_on_write(Graph *graph, IdNode *id_node, eUpdateSource update_source)
{
  ComponentNode *cow_comp = id_node->find_component(NodeType::COPY_ON_WRITE);
  if (cow_comp == nullptr) {
    lib_assert(!dgraph_copy_on_write_is_needed(GS(id_node->id_orig->name)));
    return;
  }
  cow_comp->tag_update(graph, update_source);
}

void graph_tag_component(Graph *graph,
                         IdNode *id_node,
                         NodeType component_type,
                         OpCode op_code,
                         eUpdateSource update_source)
{
  ComponentNode *component_node = id_node->find_component(component_type);
  /* NOTE: Animation component might not be existing yet (which happens when adding new driver or
   * adding a new keyframe), so the required copy-on-write tag needs to be taken care explicitly
   * here. */
  if (component_node == nullptr) {
    if (component_type == NodeType::ANIMATION) {
      id_node->is_cow_explicitly_tagged = true;
      graph_id_tag_copy_on_write(graph, id_node, update_source);
    }
    return;
  }
  if (op_code == OpCode::OPERATION) {
    component_node->tag_update(graph, update_source);
  }
  else {
    OpNode *op_node = component_node->find_op(op_code);
    if (op_node != nullptr) {
      op_node->tag_update(graph, update_source);
    }
  }
  /* If component depends on copy-on-write, tag it as well. */
  if (component_node->need_tag_cow_before_update()) {
    graph_id_tag_copy_on_write(graph, id_node, update_source);
  }
  if (component_type == NodeType::COPY_ON_WRITE) {
    id_node->is_cow_explicitly_tagged = true;
  }
}

/* This is a tag compatibility with legacy code.
 *
 * Mainly, old code was tagging object with ID_RECALC_GEOMETRY tag to inform
 * that object's data data-block changed. Now API expects that ID is given
 * explicitly, but not all areas are aware of this yet. */
void graph_id_tag_legacy_compat(
    Main *dmain, Graph *graph, Id *id, IdRecalcFlag tag, eUpdateSource update_source)
{
  if (ELEM(tag, ID_RECALC_GEOMETRY, 0)) {
    switch (GS(id->name)) {
      case ID_OB: {
        Object *object = (Object *)id;
        Id *data_id = (Id *)object->data;
        if (data_id != nullptr) {
          graph_id_tag_update(dmain, dgraph, data_id, 0, update_source);
        }
        break;
      }
      /* TODO: Shape keys are annoying, maybe we should find a
       * way to chain geometry evaluation to them, so we don't need extra
       * tagging here. */
      case ID_ME: {
        Mesh *mesh = (Mesh *)id;
        if (mesh->key != nullptr) {
          Id *key_id = &mesh->key->id;
          if (key_id != nullptr) {
            graph_id_tag_update(dmain, graph, key_id, 0, update_source);
          }
        }
        break;
      }
      case ID_LT: {
        Lattice *lattice = (Lattice *)id;
        if (lattice->key != nullptr) {
          Id *key_id = &lattice->key->id;
          if (key_id != nullptr) {
            graph_id_tag_update(dmain, graph, key_id, 0, update_source);
          }
        }
        break;
      }
      case ID_CU_LEGACY: {
        Curve *curve = (Curve *)id;
        if (curve->key != nullptr) {
          Id *key_id = &curve->key->id;
          if (key_id != nullptr) {
            graph_id_tag_update(dmain, graph, key_id, 0, update_source);
          }
        }
        break;
      }
      default:
        break;
    }
  }
}

void graph_id_tag_update_single_flag(Main *dmain,
                                     Graph *graph,
                                     Id *id,
                                     IdNode *id_node,
                                     IdRecalcFlag tag,
                                     eUpdateSource update_source)
{
  if (tag == ID_RECALC_EDITORS) {
    if (graph != nullptr && graph->is_active) {
      graph_update_editors_tag(dmain, graph, id);
    }
    return;
  }
  /* Get description of what is to be tagged. */
  NodeType component_type;
  OpCode op_code;
  graph_tag_to_component_opcode(id, tag, &component_type, &op_code);
  /* Check whether we've got something to tag. */
  if (component_type == NodeType::UNDEFINED) {
    /* Given id does not support tag. */
    /* TODO: Shall we raise some panic here? */
    return;
  }
  /* Some sanity checks before moving forward. */
  if (id_node == nullptr) {
    /* Happens when object is tagged for update and not yet in the
     * dependency graph (but will be after relations update). */
    return;
  }
  /* Tag id recalc flag. */
  GraphNodeFactory *factory = type_get_factory(component_type);
  lib_assert(factory != nullptr);
  id_node->id_cow->recalc |= factory->id_recalc_tag();
  /* Tag corresponding dependency graph operation for update. */
  if (component_type == NodeType::ID_REF) {
    id_node->tag_update(graph, update_source);
  }
  else {
    graph_tag_component(graph, id_node, component_type, op_code, update_source);
  }
  /* TODO: Get rid of this once all areas are using proper data ID
   * for tagging. */
  graph_id_tag_legacy_compat(dmain, graph, id, tag, update_source);
}

string stringify_append_bit(const string &str, IdRecalcFlag tag)
{
  const char *tag_name = Graph_update_tag_as_string(tag);
  if (tag_name == nullptr) {
    return str;
  }
  string result = str;
  if (!result.empty()) {
    result += ", ";
  }
  result += tag_name;
  return result;
}

string stringify_update_bitfield(int flag)
{
  if (flag == 0) {
    return "LEGACY_0";
  }
  string result;
  int current_flag = flag;
  /* Special cases to avoid ALL flags form being split into
   * individual bits. */
  if ((current_flag & ID_RECALC_PSYS_ALL) == ID_RECALC_PSYS_ALL) {
    result = stringify_append_bit(result, ID_RECALC_PSYS_ALL);
  }
  /* Handle all the rest of the flags. */
  while (current_flag != 0) {
    IdRecalcFlag tag = (IdRecalcFlag)(1 << bitscan_forward_clear_i(&current_flag));
    result = stringify_append_bit(result, tag);
  }
  return result;
}

const char *update_source_as_string(eUpdateSource source)
{
  switch (source) {
    case GRAPH_UPDATE_SOURCE_TIME:
      return "TIME";
    case GRAPH_UPDATE_SOURCE_USER_EDIT:
      return "USER_EDIT";
    case GRAPH_UPDATE_SOURCE_RELATIONS:
      return "RELATIONS";
    case GRAPH_UPDATE_SOURCE_VISIBILITY:
      return "VISIBILITY";
  }
  lib_assert_msg(0, "Should never happen.");
  return "UNKNOWN";
}

int dgraph_recalc_flags_for_legacy_zero()
{
  return ID_RECALC_ALL &
         ~(ID_RECALC_PSYS_ALL | ID_RECALC_ANIMATION | ID_RECALC_SOURCE | ID_RECALC_EDITORS);
}

int dgraph_recalc_flags_effective(DGraph *graph, int flags)
{
  if (graph != nullptr) {
    if (!graph->is_active) {
      return 0;
    }
  }
  if (flags == 0) {
    return dgraph_recalc_flags_for_legacy_zero();
  }
  return flags;
}

/* Special tag function which tags all components which needs to be tagged
 * for update flag=0.
 *
 * TODO: This is something to be avoid in the future, make it more
 * explicit and granular for users to tag what they really need. */
void dgraph_node_tag_zero(Main *dmain,
                          Graph *graph,
                          IdNode *id_node,
                          eUpdateSource update_source)
{
  if (id_node == nullptr) {
    return;
  }
  Id *id = id_node->id_orig;
  /* TODO(sergey): Which recalc flags to set here? */
  id_node->id_cow->recalc |= graph_recalc_flags_for_legacy_zero();

  for (ComponentNode *comp_node : id_node->components.values()) {
    if (comp_node->type == NodeType::ANIMATION) {
      continue;
    }
    if (comp_node->type == NodeType::COPY_ON_WRITE) {
      id_node->is_cow_explicitly_tagged = true;
    }

    comp_node->tag_update(graph, update_source);
  }
  graph_id_tag_legacy_compat(dmain, graph, id, (IdRecalcFlag)0, update_source);
}

void graph_tag_on_visible_update(Graph *graph, const bool do_time)
{
  graph->need_visibility_update = true;
  graph->need_visibility_time_update |= do_time;
}

} /* namespace */

void graph_tag_ids_for_visible_update(Graph *graph)
{
  if (!graph->need_visibility_update) {
    return;
  }

  const bool do_time = graph->need_visibility_time_update;
  Main *dmain = graph->dmain;

  /* NOTE: It is possible to have this function called with `do_time=false` first and later (prior
   * to evaluation though) with `do_time=true`. This means early output checks should be aware of
   * this. */
  for (graph::IdNode *id_node : graph->id_nodes) {
    const IdType id_type = GS(id_node->id_orig->name);

    if (!id_node->visible_components_mask) {
      /* id has no components which affects anything visible.
       * No need bother with it to tag or anything. */
      continue;
    }
    int flag = 0;
    if (!graph::graph_copy_on_write_is_expanded(id_node->id_cow)) {
      flag |= ID_RECALC_COPY_ON_WRITE;
      if (do_time) {
        if (dune_animdata_from_id(id_node->id_orig) != nullptr) {
          flag |= ID_RECALC_ANIMATION;
        }
      }
    }
    else {
      if (id_node->visible_components_mask == id_node->previously_visible_components_mask) {
        /* The id was already visible and evaluated, all the subsequent
         * updates and tags are to be done explicitly. */
        continue;
      }
    }
    /* We only tag components which needs an update. Tagging everything is
     * not a good idea because that might reset particles cache (or any
     * other type of cache).
     *
     * TODO: Need to generalize this somehow. */
    if (id_type == ID_OB) {
      flag |= ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY;
    }
    graph_id_tag_update(dmain, graph, id_node->id_orig, flag, GRAPH_UPDATE_SOURCE_VISIBILITY);
    if (id_type == ID_SCE) {
      /* Make sure collection properties are up to date. */
      id_node->tag_update(graph, GRAPH_UPDATE_SOURCE_VISIBILITY);
    }
    /* Now when ID is updated to the new visibility state, prevent it from
     * being re-tagged again. Simplest way to do so is to pretend that it
     * was already updated by the "previous" dependency graph.
     *
     * NOTE: Even if the on_visible_update() is called from the state when
     * dependency graph is tagged for relations update, it will be fine:
     * since dependency graph builder re-schedules entry tags, all the
     * tags we request from here will be applied in the updated state of
     * dependency graph. */
    id_node->previously_visible_components_mask = id_node->visible_components_mask;
  }

  graph->need_visibility_update = false;
  graph->need_visibility_time_update = false;
}

NodeType geometry_tag_to_component(const ID *id)
{
  const ID_Type id_type = GS(id->name);
  switch (id_type) {
    case ID_OB: {
      const Object *object = (Object *)id;
      switch (object->type) {
        case OB_MESH:
        case OB_CURVES_LEGACY:
        case OB_SURF:
        case OB_FONT:
        case OB_LATTICE:
        case OB_MBALL:
        case OB_GPENCIL:
        case OB_CURVES:
        case OB_POINTCLOUD:
        case OB_VOLUME:
          return NodeType::GEOMETRY;
        case OB_ARMATURE:
          return NodeType::EVAL_POSE;
          /* TODO: More cases here? */
      }
      break;
    }
    case ID_ME:
    case ID_CU_LEGACY:
    case ID_LT:
    case ID_MB:
    case ID_CV:
    case ID_PT:
    case ID_VO:
    case ID_GR:
      return NodeType::GEOMETRY;
    case ID_PA: /* Particles */
      return NodeType::UNDEFINED;
    case ID_LP:
      return NodeType::PARAMS;
    case ID_GD:
      return NodeType::GEOMETRY;
    case ID_PAL: /* Palettes */
      return NodeType::PARAMS;
    case ID_MSK:
      return NodeType::PARAMS;
    default:
      break;
  }
  return NodeType::UNDEFINED;
}

void id_tag_update(Main *dmain, Id *id, int flag, eUpdateSource update_source)
{
  graph_id_tag_update(dmain, nullptr, id, flag, update_source);
  for (dgraph::DGraph *dgraph : dgraph::get_all_registered_graphs(dmain)) {
    graph_id_tag_update(bmain, dgraph, id, flag, update_source);
  }

  /* Accumulate all tags for an id between two undo steps, so they can be
   * replayed for undo. */
  id->recalc_after_undo_push |= dgraph_recalc_flags_effective(nullptr, flag);
}

void graph_id_tag_update(
    Main *dmain, DGraph *graph, Id *id, int flag, eUpdateSource update_source)
{
  const int debug_flags = (graph != nullptr) ? DEG_debug_flags_get((::Depsgraph *)graph) : G.debug;
  if (graph != nullptr && graph->is_evaluating) {
    if (debug_flags & G_DEBUG_DEPSGRAPH_TAG) {
      printf("ID tagged for update during dependency graph evaluation.\n");
    }
    return;
  }
  if (debug_flags & G_DEBUG_DEPSGRAPH_TAG) {
    printf("%s: id=%s flags=%s source=%s\n",
           __func__,
           id->name,
           stringify_update_bitfield(flag).c_str(),
           update_source_as_string(update_source));
  }
  IdNode *id_node = (graph != nullptr) ? graph->find_id_node(id) : nullptr;
  if (graph != nullptr) {
    dgraph_id_type_tag(reinterpret_cast<::DGraph *>(graph), GS(id->name));
  }
  if (flag == 0) {
    dgraph_node_tag_zero(dmain, graph, id_node, update_source);
  }
  /* Store original flag in the id.
   * Allows to have more granularity than a node-factory based flags. */
  if (id_node != nullptr) {
    id_node->id_cow->recalc |= flag;
  }
  /* When id is tagged for update based on an user edits store the recalc flags in the original ID.
   * This way ids in the undo steps will have this flag preserved, making it possible to restore
   * all needed tags when new dependency graph is created on redo.
   * This is the only way to ensure modifications to animation data (such as keyframes i.e.)
   * properly triggers animation update for the newly constructed dependency graph on redo (while
   * usually newly created dependency graph skips animation update to avoid loss of unkeyed
   * changes). */
  if (update_source == DGRAPH_UPDATE_SOURCE_USER_EDIT) {
    id->recalc |= dgraph_recalc_flags_effective(graph, flag);
  }
  int current_flag = flag;
  while (current_flag != 0) {
    IdRecalcFlag tag = (IdRecalcFlag)(1 << bitscan_forward_clear_i(&current_flag));
    graph_id_tag_update_single_flag(bmain, graph, id, id_node, tag, update_source);
  }
  /* Special case for nested node tree data-blocks. */
  id_tag_update_ntree_special(bmain, graph, id, flag, update_source);
  /* Direct update tags means that something outside of simulated/cached
   * physics did change and that cache is to be invalidated.
   * This is only needed if data changes. If it's just a drawing, we keep the
   * point cache. */
  if (update_source == DGRAPH_UPDATE_SOURCE_USER_EDIT && flag != ID_RECALC_SHADING) {
    graph_id_tag_update_single_flag(
        bmain, graph, id, id_node, ID_RECALC_POINT_CACHE, update_source);
  }
}

}  // namespace dune::dgraph

const char *dgraph_update_tag_as_string(IdRecalcFlag flag)
{
  switch (flag) {
    case ID_RECALC_TRANSFORM:
      return "TRANSFORM";
    case ID_RECALC_GEOMETRY:
      return "GEOMETRY";
    case ID_RECALC_GEOMETRY_ALL_MODES:
      return "GEOMETRY_ALL_MODES";
    case ID_RECALC_ANIMATION:
      return "ANIMATION";
    case ID_RECALC_PSYS_REDO:
      return "PSYS_REDO";
    case ID_RECALC_PSYS_RESET:
      return "PSYS_RESET";
    case ID_RECALC_PSYS_CHILD:
      return "PSYS_CHILD";
    case ID_RECALC_PSYS_PHYS:
      return "PSYS_PHYS";
    case ID_RECALC_PSYS_ALL:
      return "PSYS_ALL";
    case ID_RECALC_COPY_ON_WRITE:
      return "COPY_ON_WRITE";
    case ID_RECALC_SHADING:
      return "SHADING";
    case ID_RECALC_SELECT:
      return "SELECT";
    case ID_RECALC_BASE_FLAGS:
      return "BASE_FLAGS";
    case ID_RECALC_POINT_CACHE:
      return "POINT_CACHE";
    case ID_RECALC_EDITORS:
      return "EDITORS";
    case ID_RECALC_SEQUENCER_STRIPS:
      return "SEQUENCER_STRIPS";
    case ID_RECALC_FRAME_CHANGE:
      return "FRAME_CHANGE";
    case ID_RECALC_AUDIO_FPS:
      return "AUDIO_FPS";
    case ID_RECALC_AUDIO_VOLUME:
      return "AUDIO_VOLUME";
    case ID_RECALC_AUDIO_MUTE:
      return "AUDIO_MUTE";
    case ID_RECALC_AUDIO_LISTENER:
      return "AUDIO_LISTENER";
    case ID_RECALC_AUDIO:
      return "AUDIO";
    case ID_RECALC_PARAMS:
      return "PARAMETERS";
    case ID_RECALC_SOURCE:
      return "SOURCE";
    case ID_RECALC_ALL:
      return "ALL";
    case ID_RECALC_TAG_FOR_UNDO:
      return "TAG_FOR_UNDO";
    case ID_RECALC_NTREE_OUTPUT:
      return "ID_RECALC_NTREE_OUTPUT";
  }
  return nullptr;
}

/* Data-Based Tagging. */

void dgraph_id_tag_update(Id *id, int flag)
{
  dgraph_id_tag_update_ex(G.main, id, flag);
}

void DGraph_id_tag_update_ex(Main *dmain, Id *id, int flag)
{
  if (id == nullptr) {
    /* Ideally should not happen, but old dgraph allowed this. */
    return;
  }
  dgraph::id_tag_update(dmain, id, flag, dgraph::DGRAPH_UPDATE_SOURCE_USER_EDIT);
}

void dgraph_id_tag_update(struct Main *dmain,
                             struct DGraph *dgraph,
                             struct Id *id,
                             int flag)
{
  dgraph::DGraph *graph = (dgraph::DGraph *)dgraph;
  dgraph::graph_id_tag_update(dmain, graph, id, flag, dgrap::DGRAPH_UPDATE_SOURCE_USER_EDIT);
}

void dgraph_time_tag_update(struct Main *dmain)
{
  for (dgraph::DGraph *dgraph : dgraph::get_all_registered_graphs(dmain)) {
    DGraph_time_tag_update(reinterpret_cast<::DGraph *>(dgraph));
  }
}

void DGraph_time_tag_update(struct DGraph *dgraph)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->tag_time_source();
}

void DGraph_id_type_tag(DGraph *dgraph, short id_type)
{
  if (id_type == ID_NT) {
    /* Stupid workaround so parent data-blocks of nested node-tree get looped
     * over when we loop over tagged data-block types. */
    DGraph_id_type_tag(dgraph, ID_MA);
    DGraph_id_type_tag(dgraph, ID_TE);
    DGraph_id_type_tag(dgraph, ID_LA);
    DGraph_id_type_tag(dgraph, ID_WO);
    DGraph_id_type_tag(dgraph, ID_SCE);
    DGraph_id_type_tag(dgraph, ID_SIM);
  }
  const int id_type_index = dune_idtype_idcode_to_index(id_type);
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->id_type_updated[id_type_index] = 1;
}

void dgraph_id_type_tag(Main *dmain, short id_type)
{
  for (dgraph::DGraph *dgraph : dgraph::get_all_registered_graphs(dmain)) {
    dgraph_id_type_tag(reinterpret_cast<::DGraph *>(dgraph), id_type);
  }
}

void dgraph_tag_on_visible_update(DGraph *dgraph, const bool do_time)
{
  dgraph::DGraph *graph = (dgraph::DGraph *)dgraph;
  dgraph::graph_tag_on_visible_update(graph, do_time);
}

void dgraph_tag_on_visible_update(Main *dmain, const bool do_time)
{
  for (dgraph::DGraph *dgraph : dgraph::get_all_registered_graphs(dmain)) {
    dgraph::graph_tag_on_visible_update(depsgraph, do_time);
  }
}

void dgraph_enable_editors_update(DGraph *dgraph)
{
  dgraph::DGraph *graph = (dgraph::DGraph *)dgraph;
  graph->use_editors_update = true;
}

void DGraph_editors_update(DGraph *dgraph, bool time)
{
  dgraph::DGraph *graph = (dgraph::DGraph *)dgraph;
  if (!graph->use_editors_update) {
    return;
  }

  Scene *scene = dgraph_get_input_scene(dgraph);
  ViewLayer *view_layer = dgraph_get_input_view_layer(dgraph);
  Main *dmain = dgraph_get_dmain(dgraph);
  bool updated = time || dgraph_id_type_any_updated(dgraph);

  DGraphEditorUpdateContext update_ctx = {nullptr};
  update_ctx.dmain = dmain;
  update_ctx.dgraph = dgraph;
  update_ctx.scene = scene;
  update_ctx.view_layer = view_layer;
  dgraph::dgraph_editors_scene_update(&update_ctx, updated);
}

static void dgraph_clear_id_recalc_flags(Id *id)
{
  id->recalc &= ~ID_RECALC_ALL;
  DNodeTree *ntree = ntreeFromId(id);
  /* Clear embedded node trees too. */
  if (ntree) {
    ntree->id.recalc &= ~ID_RECALC_ALL;
  }
  /* XXX And what about scene's master collection here? */
}

void dgraph_ids_clear_recalc(DGraph *dgraph, const bool backup)
{
  dgraph::DGraph *dgraph = reinterpret_cast<deg::DGraph *>(dgraph);
  /* TODO: Re-implement POST_UPDATE_HANDLER_WORKAROUND using entry_tags
   * and id_tags storage from the new dependency graph. */
  if (!dgraph_id_type_any_updated(depsgraph)) {
    return;
  }
  /* Go over all ID nodes, clearing tags. */
  for (dgraph::IdNode *id_node : dgraph->id_nodes) {
    if (backup) {
      id_node->id_cow_recalc_backup |= id_node->id_cow->recalc;
    }
    /* TODO: we clear original ID recalc flags here, but this may not work
     * correctly when there are multiple depsgraph with others still using
     * the recalc flag. */
    id_node->is_user_modified = false;
    id_node->is_cow_explicitly_tagged = false;
    dgraph_clear_id_recalc_flags(id_node->id_cow);
    if (dgraph->is_active) {
      dgraph_clear_id_recalc_flags(id_node->id_orig);
    }
  }
  memset(dgraph->id_type_updated, 0, sizeof(dgraph->id_type_updated));
}

void DGraph_ids_restore_recalc(DGraph *dgraph)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);

  for (dgraph::IdNode *id_node : dgraph_graph->id_nodes) {
    id_node->id_cow->recalc |= id_node->id_cow_recalc_backup;
    id_node->id_cow_recalc_backup = 0;
  }
}
