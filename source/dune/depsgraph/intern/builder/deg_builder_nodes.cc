 /* Methods for constructing depsgraph's nodes */

#include "intern/builder/deg_builder_nodes.h"

#include <cstdio>
#include <cstdlib>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_action_types.h"
#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_camera_types.h"
#include "DNA_collection_types.h"
#include "DNA_constraint_types.h"
#include "DNA_curve_types.h"
#include "DNA_effect_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_key_types.h"
#include "DNA_light_types.h"
#include "DNA_lightprobe_types.h"
#include "DNA_linestyle_types.h"
#include "DNA_mask_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meta_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_simulation_types.h"
#include "DNA_sound_types.h"
#include "DNA_speaker_types.h"
#include "DNA_texture_types.h"
#include "DNA_vfont_types.h"
#include "DNA_world_types.h"

#include "BKE_action.h"
#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_armature.h"
#include "BKE_cachefile.h"
#include "BKE_collection.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_effect.h"
#include "BKE_fcurve_driver.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_image.h"
#include "BKE_key.h"
#include "BKE_lattice.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_light.h"
#include "BKE_mask.h"
#include "BKE_material.h"
#include "BKE_mball.h"
#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_movieclip.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_shader_fx.h"
#include "BKE_simulation.h"
#include "BKE_sound.h"
#include "BKE_tracking.h"
#include "BKE_volume.h"
#include "BKE_world.h"

#include "RNA_access.h"
#include "RNA_prototypes.h"
#include "RNA_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "SEQ_iterator.h"
#include "SEQ_sequencer.h"

#include "intern/builder/deg_builder.h"
#include "intern/builder/deg_builder_rna.h"
#include "intern/depsgraph.h"
#include "intern/depsgraph_tag.h"
#include "intern/depsgraph_type.h"
#include "intern/eval/deg_eval_copy_on_write.h"
#include "intern/node/deg_node.h"
#include "intern/node/deg_node_component.h"
#include "intern/node/deg_node_id.h"
#include "intern/node/deg_node_operation.h"

namespace blender::deg {

/* ************ */
/* Node Builder */

/* **** General purpose functions **** */

DepsgraphNodeBuilder::DepsgraphNodeBuilder(Main *bmain,
                                           Depsgraph *graph,
                                           DepsgraphBuilderCache *cache)
    : DepsgraphBuilder(bmain, graph, cache),
      scene_(nullptr),
      view_layer_(nullptr),
      view_layer_index_(-1),
      collection_(nullptr),
      is_parent_collection_visible_(true)
{
}

DepsgraphNodeBuilder::~DepsgraphNodeBuilder()
{
  for (IDInfo *id_info : id_info_hash_.values()) {
    if (id_info->id_cow != nullptr) {
      deg_free_copy_on_write_datablock(id_info->id_cow);
      MEM_freeN(id_info->id_cow);
    }
    MEM_freeN(id_info);
  }
}

IDNode *DepsgraphNodeBuilder::add_id_node(ID *id)
{
  BLI_assert(id->session_uuid != MAIN_ID_SESSION_UUID_UNSET);

  const ID_Type id_type = GS(id->name);
  IDNode *id_node = nullptr;
  ID *id_cow = nullptr;
  IDComponentsMask previously_visible_components_mask = 0;
  uint32_t previous_eval_flags = 0;
  DEGCustomDataMeshMasks previous_customdata_masks;
  IDInfo *id_info = id_info_hash_.lookup_default(id->session_uuid, nullptr);
  if (id_info != nullptr) {
    id_cow = id_info->id_cow;
    previously_visible_components_mask = id_info->previously_visible_components_mask;
    previous_eval_flags = id_info->previous_eval_flags;
    previous_customdata_masks = id_info->previous_customdata_masks;
    /* Tag ID info to not free the CoW ID pointer. */
    id_info->id_cow = nullptr;
  }
  id_node = graph_->add_id_node(id, id_cow);
  id_node->previously_visible_components_mask = previously_visible_components_mask;
  id_node->previous_eval_flags = previous_eval_flags;
  id_node->previous_customdata_masks = previous_customdata_masks;

  /* NOTE: Zero number of components indicates that ID node was just created. */
  const bool is_newly_created = id_node->components.is_empty();

  if (is_newly_created) {
    if (deg_copy_on_write_is_needed(id_type)) {
      ComponentNode *comp_cow = id_node->add_component(NodeType::COPY_ON_WRITE);
      OperationNode *op_cow = comp_cow->add_operation(
          [id_node](::Depsgraph *depsgraph) { deg_evaluate_copy_on_write(depsgraph, id_node); },
          OperationCode::COPY_ON_WRITE,
          "",
          -1);
      graph_->operations.append(op_cow);
    }

    ComponentNode *visibility_component = id_node->add_component(NodeType::VISIBILITY);
    OperationNode *visibility_operation = visibility_component->add_operation(
        nullptr, OperationCode::OPERATION, "", -1);
    /* Pin the node so that it and its relations are preserved by the unused nodes/relations
     * deletion. This is mainly to make it easier to debug visibility. */
    visibility_operation->flag |= OperationFlag::DEPSOP_FLAG_PINNED;
    graph_->operations.append(visibility_operation);
  }
  return id_node;
}

IDNode *DepsgraphNodeBuilder::find_id_node(ID *id)
{
  return graph_->find_id_node(id);
}

TimeSourceNode *DepsgraphNodeBuilder::add_time_source()
{
  return graph_->add_time_source();
}

ComponentNode *DepsgraphNodeBuilder::add_component_node(ID *id,
                                                        NodeType comp_type,
                                                        const char *comp_name)
{
  IDNode *id_node = add_id_node(id);
  ComponentNode *comp_node = id_node->add_component(comp_type, comp_name);
  comp_node->owner = id_node;
  return comp_node;
}

OperationNode *DepsgraphNodeBuilder::add_operation_node(ComponentNode *comp_node,
                                                        OperationCode opcode,
                                                        const DepsEvalOperationCb &op,
                                                        const char *name,
                                                        int name_tag)
{
  OperationNode *op_node = comp_node->find_operation(opcode, name, name_tag);
  if (op_node == nullptr) {
    op_node = comp_node->add_operation(op, opcode, name, name_tag);
    graph_->operations.append(op_node);
  }
  else {
    fprintf(stderr,
            "add_operation: Operation already exists - %s has %s at %p\n",
            comp_node->identifier().c_str(),
            op_node->identifier().c_str(),
            op_node);
    BLI_assert_msg(0, "Should not happen!");
  }
  return op_node;
}

OperationNode *DepsgraphNodeBuilder::add_operation_node(ID *id,
                                                        NodeType comp_type,
                                                        const char *comp_name,
                                                        OperationCode opcode,
                                                        const DepsEvalOperationCb &op,
                                                        const char *name,
                                                        int name_tag)
{
  ComponentNode *comp_node = add_component_node(id, comp_type, comp_name);
  return add_operation_node(comp_node, opcode, op, name, name_tag);
}

OperationNode *DepsgraphNodeBuilder::add_operation_node(ID *id,
                                                        NodeType comp_type,
                                                        OperationCode opcode,
                                                        const DepsEvalOperationCb &op,
                                                        const char *name,
                                                        int name_tag)
{
  return add_operation_node(id, comp_type, "", opcode, op, name, name_tag);
}

OperationNode *DepsgraphNodeBuilder::ensure_operation_node(ID *id,
                                                           NodeType comp_type,
                                                           const char *comp_name,
                                                           OperationCode opcode,
                                                           const DepsEvalOperationCb &op,
                                                           const char *name,
                                                           int name_tag)
{
  OperationNode *operation = find_operation_node(id, comp_type, comp_name, opcode, name, name_tag);
  if (operation != nullptr) {
    return operation;
  }
  return add_operation_node(id, comp_type, comp_name, opcode, op, name, name_tag);
}

OperationNode *DepsgraphNodeBuilder::ensure_operation_node(ID *id,
                                                           NodeType comp_type,
                                                           OperationCode opcode,
                                                           const DepsEvalOperationCb &op,
                                                           const char *name,
                                                           int name_tag)
{
  OperationNode *operation = find_operation_node(id, comp_type, opcode, name, name_tag);
  if (operation != nullptr) {
    return operation;
  }
  return add_operation_node(id, comp_type, opcode, op, name, name_tag);
}

bool DepsgraphNodeBuilder::has_operation_node(ID *id,
                                              NodeType comp_type,
                                              const char *comp_name,
                                              OperationCode opcode,
                                              const char *name,
                                              int name_tag)
{
  return find_operation_node(id, comp_type, comp_name, opcode, name, name_tag) != nullptr;
}

OperationNode *DepsgraphNodeBuilder::find_operation_node(ID *id,
                                                         NodeType comp_type,
                                                         const char *comp_name,
                                                         OperationCode opcode,
                                                         const char *name,
                                                         int name_tag)
{
  ComponentNode *comp_node = add_component_node(id, comp_type, comp_name);
  return comp_node->find_operation(opcode, name, name_tag);
}

OperationNode *DepsgraphNodeBuilder::find_operation_node(
    ID *id, NodeType comp_type, OperationCode opcode, const char *name, int name_tag)
{
  return find_operation_node(id, comp_type, "", opcode, name, name_tag);
}

ID *DepsgraphNodeBuilder::get_cow_id(const ID *id_orig) const
{
  return graph_->get_cow_id(id_orig);
}

ID *DepsgraphNodeBuilder::ensure_cow_id(ID *id_orig)
{
  if (id_orig->tag & LIB_TAG_COPIED_ON_WRITE) {
    /* ID is already remapped to copy-on-write. */
    return id_orig;
  }
  IDNode *id_node = add_id_node(id_orig);
  return id_node->id_cow;
}

/* **** Build functions for entity nodes **** */

void DepsgraphNodeBuilder::begin_build()
{
  /* Store existing copy-on-write versions of datablock, so we can re-use
   * them for new ID nodes. */
  for (IDNode *id_node : graph_->id_nodes) {
    /* It is possible that the ID does not need to have CoW version in which case id_cow is the
     * same as id_orig. Additionally, such ID might have been removed, which makes the check
     * for whether id_cow is expanded to access freed memory. In order to deal with this we
     * check whether CoW is needed based on a scalar value which does not lead to access of
     * possibly deleted memory. */
    IDInfo *id_info = (IDInfo *)MEM_mallocN(sizeof(IDInfo), "depsgraph id info");
    if (deg_copy_on_write_is_needed(id_node->id_type) &&
        deg_copy_on_write_is_expanded(id_node->id_cow) && id_node->id_orig != id_node->id_cow) {
      id_info->id_cow = id_node->id_cow;
    }
    else {
      id_info->id_cow = nullptr;
    }
    id_info->previously_visible_components_mask = id_node->visible_components_mask;
    id_info->previous_eval_flags = id_node->eval_flags;
    id_info->previous_customdata_masks = id_node->customdata_masks;
    BLI_assert(!id_info_hash_.contains(id_node->id_orig_session_uuid));
    id_info_hash_.add_new(id_node->id_orig_session_uuid, id_info);
    id_node->id_cow = nullptr;
  }

  for (OperationNode *op_node : graph_->entry_tags) {
    ComponentNode *comp_node = op_node->owner;
    IDNode *id_node = comp_node->owner;

    SavedEntryTag entry_tag;
    entry_tag.id_orig = id_node->id_orig;
    entry_tag.component_type = comp_node->type;
    entry_tag.opcode = op_node->opcode;
    entry_tag.name = op_node->name;
    entry_tag.name_tag = op_node->name_tag;
    saved_entry_tags_.append(entry_tag);
  }

  /* Make sure graph has no nodes left from previous state. */
  graph_->clear_all_nodes();
  graph_->operations.clear();
  graph_->entry_tags.clear();
}

/* Util callbacks for `BKE_library_foreach_ID_link`, used to detect when a COW ID is using ID
 * pointers that are either:
 *  - COW ID pointers that do not exist anymore in current depsgraph.
 *  - Orig ID pointers that do have now a COW version in current depsgraph.
 * In both cases, it means the COW ID user needs to be flushed, to ensure its pointers are properly
 * remapped.
 *
 * NOTE: This is split in two, a static function and a public method of the node builder, to allow
 * the code to access the builder's data more easily. */

int DepsgraphNodeBuilder::foreach_id_cow_detect_need_for_update_callback(ID *id_cow_self,
                                                                         ID *id_pointer)
{
  if (id_pointer->orig_id == nullptr) {
    /* `id_cow_self` uses a non-cow ID, if that ID has a COW copy in current depsgraph its owner
     * needs to be remapped, i.e. COW-flushed. */
    IDNode *id_node = find_id_node(id_pointer);
    if (id_node != nullptr && id_node->id_cow != nullptr) {
      graph_id_tag_update(bmain_,
                          graph_,
                          id_cow_self->orig_id,
                          ID_RECALC_COPY_ON_WRITE,
                          DEG_UPDATE_SOURCE_RELATIONS);
      return IDWALK_RET_STOP_ITER;
    }
  }
  else {
    /* `id_cow_self` uses a COW ID, if that COW copy is removed from current depsgraph its owner
     * needs to be remapped, i.e. COW-flushed. */
    /* NOTE: at that stage, old existing COW copies that are to be removed from current state of
     * evaluated depsgraph are still valid pointers, they are freed later (typically during
     * destruction of the builder itself). */
    IDNode *id_node = find_id_node(id_pointer->orig_id);
    if (id_node == nullptr) {
      graph_id_tag_update(bmain_,
                          graph_,
                          id_cow_self->orig_id,
                          ID_RECALC_COPY_ON_WRITE,
                          DEG_UPDATE_SOURCE_RELATIONS);
      return IDWALK_RET_STOP_ITER;
    }
  }
  return IDWALK_RET_NOP;
}

static int foreach_id_cow_detect_need_for_update_callback(LibraryIDLinkCallbackData *cb_data)
{
  ID *id = *cb_data->id_pointer;
  if (id == nullptr) {
    return IDWALK_RET_NOP;
  }

  DepsgraphNodeBuilder *builder = static_cast<DepsgraphNodeBuilder *>(cb_data->user_data);
  ID *id_cow_self = cb_data->id_self;

  return builder->foreach_id_cow_detect_need_for_update_callback(id_cow_self, id);
}

void DepsgraphNodeBuilder::update_invalid_cow_pointers()
{
  /* NOTE: Currently the only ID types that depsgraph may decide to not evaluate/generate COW
   * copies for, even though they are referenced by other data-blocks, are Collections and Objects
   * (through their various visibility flags, and the ones from #LayerCollections too). However,
   * this code is kept generic as it makes it more future-proof, and optimization here would give
   * negligible performance improvements in typical cases.
   *
   * NOTE: This mechanism may also 'fix' some missing update tagging from non-depsgraph code in
   * some cases. This is slightly unfortunate (as it may hide issues in other parts of Blender
   * code), but cannot really be avoided currently. */

  for (const IDNode *id_node : graph_->id_nodes) {
    if (id_node->previously_visible_components_mask == 0) {
      /* Newly added node/ID, no need to check it. */
      continue;
    }
    if (ELEM(id_node->id_cow, id_node->id_orig, nullptr)) {
      /* Node/ID with no COW data, no need to check it. */
      continue;
    }
    if ((id_node->id_cow->recalc & ID_RECALC_COPY_ON_WRITE) != 0) {
      /* Node/ID already tagged for COW flush, no need to check it. */
      continue;
    }
    if ((id_node->id_cow->flag & LIB_EMBEDDED_DATA) != 0) {
      /* For now, we assume embedded data are managed by their owner IDs and do not need to be
       * checked here.
       *
       * NOTE: This exception somewhat weak, and ideally should not be needed. Currently however,
       * embedded data are handled as full local (private) data of their owner IDs in part of
       * Blender (like read/write code, including undo/redo), while depsgraph generally treat them
       * as regular independent IDs. This leads to inconsistencies that can lead to bad level
       * memory accesses.
       *
       * E.g. when undoing creation/deletion of a collection directly child of a scene's master
       * collection, the scene itself is re-read in place, but its master collection becomes a
       * completely new different pointer, and the existing COW of the old master collection in the
       * matching deg node is therefore pointing to fully invalid (freed) memory. */
      continue;
    }
    BKE_library_foreach_ID_link(nullptr,
                                id_node->id_cow,
                                deg::foreach_id_cow_detect_need_for_update_callback,
                                this,
                                IDWALK_IGNORE_EMBEDDED_ID | IDWALK_READONLY);
  }
}

void DepsgraphNodeBuilder::tag_previously_tagged_nodes()
{
  for (const SavedEntryTag &entry_tag : saved_entry_tags_) {
    IDNode *id_node = find_id_node(entry_tag.id_orig);
    if (id_node == nullptr) {
      continue;
    }
    ComponentNode *comp_node = id_node->find_component(entry_tag.component_type);
    if (comp_node == nullptr) {
      continue;
    }
    OperationNode *op_node = comp_node->find_operation(
        entry_tag.opcode, entry_tag.name.c_str(), entry_tag.name_tag);
    if (op_node == nullptr) {
      continue;
    }
    /* Since the tag is coming from a saved copy of entry tags, this means
     * that originally node was explicitly tagged for user update. */
    op_node->tag_update(graph_, DEG_UPDATE_SOURCE_USER_EDIT);
  }
}

void DepsgraphNodeBuilder::end_build()
{
  tag_previously_tagged_nodes();
  update_invalid_cow_pointers();
}

void DepsgraphNodeBuilder::build_id(ID *id)
{
  if (id == nullptr) {
    return;
  }

  const ID_Type id_type = GS(id->name);
  switch (id_type) {
    case ID_AC:
      build_action((bAction *)id);
      break;
    case ID_AR:
      build_armature((bArmature *)id);
      break;
    case ID_CA:
      build_camera((Camera *)id);
      break;
    case ID_GR:
      build_collection(nullptr, (Collection *)id);
      break;
    case ID_OB:
      /* TODO(sergey): Get visibility from a "parent" somehow.
       *
       * NOTE: Using `false` visibility here should be fine, since if this
       * driver affects on something invisible we don't really care if the
       * driver gets evaluated (and even don't want this to force object
       * to become visible).
       *
       * If this happened to be affecting visible object, then it is up to
       * deg_graph_build_flush_visibility() to ensure visibility of the
       * object is true. */
      build_object(-1, (Object *)id, DEG_ID_LINKED_INDIRECTLY, false);
      break;
    case ID_KE:
      build_shapekeys((Key *)id);
      break;
    case ID_LA:
      build_light((Light *)id);
      break;
    case ID_LP:
      build_lightprobe((LightProbe *)id);
      break;
    case ID_NT:
      build_nodetree((bNodeTree *)id);
      break;
    case ID_MA:
      build_material((Material *)id);
      break;
    case ID_TE:
      build_texture((Tex *)id);
      break;
    case ID_IM:
      build_image((Image *)id);
      break;
    case ID_WO:
      build_world((World *)id);
      break;
    case ID_MSK:
      build_mask((Mask *)id);
      break;
    case ID_LS:
      build_freestyle_linestyle((FreestyleLineStyle *)id);
      break;
    case ID_MC:
      build_movieclip((MovieClip *)id);
      break;
    case ID_ME:
    case ID_MB:
    case ID_CU_LEGACY:
    case ID_LT:
    case ID_GD:
    case ID_CV:
    case ID_PT:
    case ID_VO:
      build_object_data_geometry_datablock(id);
      break;
    case ID_SPK:
      build_speaker((Speaker *)id);
      break;
    case ID_SO:
      build_sound((bSound *)id);
      break;
    case ID_TXT:
      /* Not a part of dependency graph. */
      break;
    case ID_CF:
      build_cachefile((CacheFile *)id);
      break;
    case ID_SCE:
      build_scene_parameters((Scene *)id);
      break;
    case ID_SIM:
      build_simulation((Simulation *)id);
      break;
    case ID_PA:
      build_particle_settings((ParticleSettings *)id);
      break;

    case ID_LI:
    case ID_IP:
    case ID_SCR:
    case ID_VF:
    case ID_BR:
    case ID_WM:
    case ID_PAL:
    case ID_PC:
    case ID_WS:
      BLI_assert(!deg_copy_on_write_is_needed(id_type));
      build_generic_id(id);
      break;
  }
}

void DepsgraphNodeBuilder::build_generic_id(ID *id)
{
  if (built_map_.checkIsBuiltAndTag(id)) {
    return;
  }

  build_idproperties(id->properties);
  build_animdata(id);
  build_parameters(id);
}

static void build_idproperties_callback(IDProperty *id_property, void *user_data)
{
  DepsgraphNodeBuilder *builder = reinterpret_cast<DepsgraphNodeBuilder *>(user_data);
  BLI_assert(id_property->type == IDP_ID);
  builder->build_id(reinterpret_cast<ID *>(id_property->data.pointer));
}

void DepsgraphNodeBuilder::build_idproperties(IDProperty *id_property)
{
  IDP_foreach_property(id_property, IDP_TYPE_FILTER_ID, build_idproperties_callback, this);
}

void DepsgraphNodeBuilder::build_collection(LayerCollection *from_layer_collection,
                                            Collection *collection)
{
  const int visibility_flag = (graph_->mode == DAG_EVAL_VIEWPORT) ? COLLECTION_HIDE_VIEWPORT :
                                                                    COLLECTION_HIDE_RENDER;
  const bool is_collection_restricted = (collection->flag & visibility_flag);
  const bool is_collection_visible = !is_collection_restricted && is_parent_collection_visible_;
  IDNode *id_node;
  if (built_map_.checkIsBuiltAndTag(collection)) {
    id_node = find_id_node(&collection->id);
    if (is_collection_visible && id_node->is_directly_visible == false &&
        id_node->is_collection_fully_expanded == true) {
      /* Collection became visible, make sure nested collections and
       * objects are poked with the new visibility flag, since they
       * might become visible too. */
    }
    else if (from_layer_collection == nullptr && !id_node->is_collection_fully_expanded) {
      /* Initially collection was built from layer now, and was requested
       * to not recurse into object. But now it's asked to recurse into all objects. */
    }
    else {
      return;
    }
  }
  else {
    /* Collection itself. */
    id_node = add_id_node(&collection->id);
    id_node->is_directly_visible = is_collection_visible;

    build_idproperties(collection->id.properties);
    add_operation_node(&collection->id, NodeType::GEOMETRY, OperationCode::GEOMETRY_EVAL_DONE);
  }
  if (from_layer_collection != nullptr) {
    /* If we came from layer collection we don't go deeper, view layer
     * builder takes care of going deeper. */
    return;
  }
  /* Backup state. */
  Collection *current_state_collection = collection_;
  const bool is_current_parent_collection_visible = is_parent_collection_visible_;
  /* Modify state as we've entered new collection/ */
  collection_ = collection;
  is_parent_collection_visible_ = is_collection_visible;
  /* Build collection objects. */
  LISTBASE_FOREACH (CollectionObject *, cob, &collection->gobject) {
    build_object(-1, cob->ob, DEG_ID_LINKED_INDIRECTLY, is_collection_visible);
  }
  /* Build child collections. */
  LISTBASE_FOREACH (CollectionChild *, child, &collection->children) {
    build_collection(nullptr, child->collection);
  }
  /* Restore state. */
  collection_ = current_state_collection;
  is_parent_collection_visible_ = is_current_parent_collection_visible;
  id_node->is_collection_fully_expanded = true;
}

void DepsgraphNodeBuilder::build_object(int base_index,
                                        Object *object,
                                        eDepsNode_LinkedState_Type linked_state,
                                        bool is_visible)
{
  const bool has_object = built_map_.checkIsBuiltAndTag(object);

  /* When there is already object in the dependency graph accumulate visibility an linked state
   * flags. Only do it on the object itself (apart from very special cases) and leave dealing with
   * visibility of dependencies to the visibility flush step which happens at the end of the build
   * process. */
  if (has_object) {
    IDNode *id_node = find_id_node(&object->id);
    if (id_node->linked_state == DEG_ID_LINKED_INDIRECTLY) {
      build_object_flags(base_index, object, linked_state);
    }
    id_node->linked_state = max(id_node->linked_state, linked_state);
    id_node->is_directly_visible |= is_visible;
    id_node->has_base |= (base_index != -1);

    /* There is no relation path which will connect current object with all the ones which come
     * via the instanced collection, so build the collection again. Note that it will do check
     * whether visibility update is needed on its own. */
    build_object_instance_collection(object, is_visible);

    return;
  }

  /* Create ID node for object and begin init. */
  IDNode *id_node = add_id_node(&object->id);
  Object *object_cow = get_cow_datablock(object);
  id_node->linked_state = linked_state;
  /* NOTE: Scene is nullptr when building dependency graph for render pipeline.
   * Probably need to assign that to something non-nullptr, but then the logic here will still be
   * somewhat weird. */
  if (scene_ != nullptr && object == scene_->camera) {
    id_node->is_directly_visible = true;
  }
  else {
    id_node->is_directly_visible = is_visible;
  }
  id_node->has_base |= (base_index != -1);
  /* Various flags, flushing from bases/collections. */
  build_object_from_layer(base_index, object, linked_state);
  /* Transform. */
  build_object_transform(object);
  /* Parent. */
  if (object->parent != nullptr) {
    build_object(-1, object->parent, DEG_ID_LINKED_INDIRECTLY, is_visible);
  }
  /* Modifiers. */
  if (object->modifiers.first != nullptr) {
    BuilderWalkUserData data;
    data.builder = this;
    BKE_modifiers_foreach_ID_link(object, modifier_walk, &data);
  }
  /* Grease Pencil Modifiers. */
  if (object->greasepencil_modifiers.first != nullptr) {
    BuilderWalkUserData data;
    data.builder = this;
    BKE_gpencil_modifiers_foreach_ID_link(object, modifier_walk, &data);
  }
  /* Shader FX. */
  if (object->shader_fx.first != nullptr) {
    BuilderWalkUserData data;
    data.builder = this;
    BKE_shaderfx_foreach_ID_link(object, modifier_walk, &data);
  }
  /* Constraints. */
  if (object->constraints.first != nullptr) {
    BuilderWalkUserData data;
    data.builder = this;
    BKE_constraints_id_loop(&object->constraints, constraint_walk, &data);
  }
  /* Object data. */
  build_object_data(object);
  /* Parameters, used by both drivers/animation and also to inform dependency
   * from object's data. */
  build_parameters(&object->id);
  build_idproperties(object->id.properties);
  /* Build animation data,
   *
   * Do it now because it's possible object data will affect
   * on object's level animation, for example in case of rebuilding
   * pose for proxy. */
  build_animdata(&object->id);
  /* Particle systems. */
  if (object->particlesystem.first != nullptr) {
    build_particle_systems(object, is_visible);
  }
  /* Force field Texture. */
  if ((object->pd != nullptr) && (object->pd->forcefield == PFIELD_TEXTURE) &&
      (object->pd->tex != nullptr)) {
    build_texture(object->pd->tex);
  }
  /* Object dupligroup. */
  if (object->instance_collection != nullptr) {
    build_object_instance_collection(object, is_visible);
    OperationNode *op_node = add_operation_node(
        &object->id, NodeType::DUPLI, OperationCode::DUPLI);
    op_node->flag |= OperationFlag::DEPSOP_FLAG_PINNED;
  }
  /* Synchronization back to original object. */
  add_operation_node(&object->id,
                     NodeType::SYNCHRONIZATION,
                     OperationCode::SYNCHRONIZE_TO_ORIGINAL,
                     [object_cow](::Depsgraph *depsgraph) {
                       BKE_object_sync_to_original(depsgraph, object_cow);
                     });
}
