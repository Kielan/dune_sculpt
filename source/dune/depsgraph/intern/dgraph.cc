/** DGraph core routines. **/

#include "intern/dgraph.h" /* own include */

#include <algorithm>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "lib_console.h"
#include "lib_hash.h"
#include "lib_utildefines.h"

#include "dune_global.h"
#include "dune_idtype.h"
#include "dune_scene.h"

#include "dgraph.h"
#include "dgraph_debug.h"

#include "intern/dgraph_physics.h"
#include "intern/dgraph_registry.h"
#include "intern/dgraph_relation.h"
#include "intern/dgraph_update.h"

#include "intern/eval/dgraph_eval_copy_on_write.h"

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_factory.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_operation.h"
#include "intern/node/dgraph_node_time.h"

namespace dgraph = dune::dgraph;

namespace dune::dgraph {

Depsgraph::DGraph(Main *dmain, Scene *scene, ViewLayer *view_layer, eEvaluationMode mode)
    : time_source(nullptr),
      need_update(true),
      need_visibility_update(true),
      need_visibility_time_update(false),
      dmain(dmain),
      scene(scene),
      view_layer(view_layer),
      mode(mode),
      frame(dune_scene_frame_get(scene)),
      ctime(dune_scene_ctime_get(scene)),
      scene_cow(nullptr),
      is_active(false),
      is_evaluating(false),
      is_render_pipeline_dgraph(false),
      use_editors_update(false)
{
  lib_spin_init(&lock);
  memset(id_type_updated, 0, sizeof(id_type_updated));
  memset(id_type_exist, 0, sizeof(id_type_exist));
  memset(physics_relations, 0, sizeof(physics_relations));

  add_time_source();
}

DGraph::~DGraph()
{
  clear_id_nodes();
  delete time_source;
  lib_spin_end(&lock);
}

/* Node Management ---------------------------- */

TimeSourceNode *DGraph::add_time_source()
{
  if (time_source == nullptr) {
    DNodeFactory *factory = type_get_factory(NodeType::TIMESOURCE);
    time_source = (TimeSourceNode *)factory->create_node(nullptr, "", "Time Source");
  }
  return time_source;
}

TimeSourceNode *DGraph::find_time_source() const
{
  return time_source;
}

void DGraph::tag_time_source()
{
  time_source->tag_update(this, DGRAPH_UPDATE_SOURCE_TIME);
}

IdNode *DGraph::find_id_node(const Id *id) const
{
  return id_hash.lookup_default(id, nullptr);
}

IdNode *DGraph::add_id_node(Id *id, Id *id_cow_hint)
{
  lib_assert((id->tag & LIB_TAG_COPIED_ON_WRITE) == 0);
  IdNode *id_node = find_id_node(id);
  if (!id_node) {
    DNodeFactory *factory = type_get_factory(NodeType::ID_REF);
    id_node = (IdNode *)factory->create_node(id, "", id->name);
    id_node->init_copy_on_write(id_cow_hint);
    /* Register node in Id hash.
     *
     * NOTE: We address Id nodes by the original Id pointer they are
     * referencing to. */
    id_hash.add_new(id, id_node);
    id_nodes.append(id_node);

    id_type_exist[dune_idtype_idcode_to_index(GS(id->name))] = 1;
  }
  return id_node;
}

template<typename FilterFunc>
static void clear_id_nodes_conditional(DGraph::IdDNodes *id_nodes, const FilterFn &filter)
{
  for (IdNode *id_node : *id_nodes) {
    if (id_node->id_cow == nullptr) {
      /* This means builder "stole" ownership of the copy-on-written
       * datablock for her own dirty needs. */
      continue;
    }
    if (id_node->id_cow == id_node->id_orig) {
      /* Copy-on-write version is not needed for this Id type.
       *
       * NOTE: Is important to not de-reference the original datablock here because it might be
       * freed already (happens during main database free when some Ids are freed prior to a
       * scene). */
      continue;
    }
    if (!dgraph_copy_on_write_is_expanded(id_node->id_cow)) {
      continue;
    }
    const ID_Type id_type = GS(id_node->id_cow->name);
    if (filter(id_type)) {
      id_node->destroy();
    }
  }
}

void DGraph::clear_id_nodes()
{
  /* Free memory used by ID nodes. */

  /* Stupid workaround to ensure we free IDs in a proper order. */
  clear_id_nodes_conditional(&id_nodes, [](IdType id_type) { return id_type == ID_SCE; });
  clear_id_nodes_conditional(&id_nodes, [](IdType id_type) { return id_type != ID_PA; });

  for (IdNode *id_node : id_nodes) {
    delete id_node;
  }
  /* Clear containers. */
  id_hash.clear();
  id_nodes.clear();
  /* Clear physics relation caches. */
  clear_physics_relations(this);
}

Relation *DGraph::add_new_relation(Node *from, Node *to, const char *description, int flags)
{
  Relation *rel = nullptr;
  if (flags & RELATION_CHECK_BEFORE_ADD) {
    rel = check_nodes_connected(from, to, description);
  }
  if (rel != nullptr) {
    rel->flag |= flags;
    return rel;
  }

#ifndef NDEBUG
  if (from->type == NodeType::OPERATION && to->type == NodeType::OPERATION) {
    OpNode *op_from = static_cast<OpNode *>(from);
    OpNode *op_to = static_cast<OpNode *>(to);
    lib_assert(op_to->owner->type != NodeType::COPY_ON_WRITE ||
               op_from->owner->type == NodeType::COPY_ON_WRITE);
  }
#endif

  /* Create new relation, and add it to the graph. */
  rel = new Relation(from, to, description);
  rel->flag |= flags;
  return rel;
}

Relation *DGraph::check_nodes_connected(const Node *from,
                                        const Node *to,
                                        const char *description)
{
  for (Relation *rel : from->outlinks) {
    lib_assert(rel->from == from);
    if (rel->to != to) {
      continue;
    }
    if (description != nullptr && !STREQ(rel->name, description)) {
      continue;
    }
    return rel;
  }
  return nullptr;
}

/* Low level tagging -------------------------------------- */

void DGraph::add_entry_tag(OpNode *node)
{
  /* Sanity check. */
  if (node == nullptr) {
    return;
  }
  /* Add to graph-level set of directly modified nodes to start searching
   * from.
   * NOTE: this is necessary since we have several thousand nodes to play
   * with. */
  entry_tags.add(node);
}

void DGraph::clear_all_nodes()
{
  clear_id_nodes();
  delete time_source;
  time_source = nullptr;
}

Id *DGraph::get_cow_id(const Id *id_orig) const
{
  IdNode *id_node = find_id_node(id_orig);
  if (id_node == nullptr) {
    /* This function is used from places where we expect ID to be either
     * already a copy-on-write version or have a corresponding copy-on-write
     * version.
     *
     * We try to enforce that in debug builds, for release we play a bit
     * safer game here. */
    if ((id_orig->tag & LIB_TAG_COPIED_ON_WRITE) == 0) {
      /* TODO: This is nice sanity check to have, but it fails
       * in following situations:
       *
       * - Material has link to texture, which is not needed by new
       *   shading system and hence can be ignored at construction.
       * - Object or mesh has material at a slot which is not used (for
       *   example, object has material slot by materials are set to
       *   object data). */
      // lib_assert_msg(0, "Request for non-existing copy-on-write ID");
    }
    return (Id *)id_orig;
  }
  return id_node->id_cow;
}

}  // namespace dune::dgraph

/* **************** */
/* Public Graph API */

Depsgraph *dgraph_new(Main *dmain, Scene *scene, ViewLayer *view_layer, eEvaluationMode mode)
{
  dgraph::DGraph *dgraph = new dgraph::DGraph(dmain, scene, view_layer, mode);
  dgraph::register_graph(dgraph);
  return reinterpret_cast<DGraph *>(dgraph);
}

void dgraph_replace_owners(struct DGraph *dgraph,
                              Main *dmain,
                              Scene *scene,
                              ViewLayer *view_layer)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);

  const bool do_update_register = dgraph->dmain != bmain;
  if (do_update_register && dgraph->dmain != nullptr) {
    deg::unregister_graph(dgraph);
  }

  dgraph->dmain = dmain;
  dgraph->scene = scene;
  dgraph->view_layer = view_layer;

  if (do_update_register) {
    dgraph::register_graph(dgraph);
  }
}

void dgraph_free(DGraph *graph)
{
  if (graph == nullptr) {
    return;
  }
  using dgraph::DGraph;
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(graph);
  dgraph::unregister_graph(dgraph);
  delete dgraph;
}

bool deg_is_evaluating(const struct DGraph *dgraph)
{
  const deg::Depsgraph *deg_graph = reinterpret_cast<const dgraph::DGraph *>(dgraph);
  return dgraph->is_evaluating;
}

bool dgraph_is_active(const struct DGraph *dgraph)
{
  if (dgraph == nullptr) {
    /* Happens for such cases as work object in what_does_obaction(),
     * and sine render pipeline parts. Shouldn't really be accepting
     * nullptr dgraph, but is quite hard to get proper one in those
     * cases. */
    return false;
  }
  const dgraph::DGraph *dgraph = reinterpret_cast<const dgraph::DGraph *>(dgraph);
  return dgraph->is_active;
}

void dgraph_make_active(struct DGraph *dgraph)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->is_active = true;
  /* TODO: Copy data from evaluated state to original. */
}

void dgraph_make_inactive(struct DGraph *dgraph)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->is_active = false;
}
