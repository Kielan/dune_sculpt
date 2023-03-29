#pragma once

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_operation.h"

struct Id;
struct ApiPtr;
struct ApiProp;

namespace dune::dgraph {

struct DGraph;
struct Node;
class ApiNodeQueryIdData;
class DGraphBuilder;

/* For queries which gives operation node or key defines whether we are
 * interested in a result of the given property or whether we are linking some
 * dependency to that property. */
enum class ApiPtrSource {
  /* Query will return pointer to an entry operation of component which is
   * responsible for evaluation of the given property. */
  ENTRY,
  /* Query will return pointer to an exit operation of component which is
   * responsible for evaluation of the given property.
   * More precisely, it will return operation at which the property is known
   * to be evaluated. */
  EXIT,
};

/* A helper structure which wraps all fields needed to find a node inside of
 * the dependency graph. */
class ApiNodeId {
 public:
  ApiNodeId();

  /* Check whether this identifier is valid and usable. */
  bool is_valid() const;

  Id *id;
  NodeType type;
  const char *component_name;
  OpCode op_code;
  const char *op_name;
  int op_name_tag;
};

/* Helper class which performs optimized lookups of a node within a given
 * dependency graph which satisfies given Api pointer or RAN path. */
class ApiNodeQuery {
 public:
  ApiNodeQuery(DGraph *dgraph, DGraphBuilder *builder);
  ~ApiNodeQuery();

  Node *find_node(const ApiPtr *ptr, const ApiProp *prop, ApiPtrSource source);

 protected:
  DGraph *dgraph_;
  DGraphBuilder *builder_;

  /* Indexed by an Id, returns ApiNodeQueryIdData associated with that Id. */
  Map<const Id *, unique_ptr<ApiNodeQueryIdData>> id_data_map_;

  /* Construct identifier of the node which corresponds given configuration
   * of Api property. */
  ApiNodeId construct_node_id(const ApiPtr *ptr,
                              const ApiProp *prop,
                              ApiPtrSource source);

  /* Make sure Id data exists for the given Id, and returns it. */
  ApiNodeQueryIdData *ensure_id_data(const Id *id);

  /* Check whether prop_id contains api_path_component.
   *
   * This checks more than a sub-string:
   *
   * prop_id.                  contains(prop_id, "location")
   * ------------------------  -------------------------------------
   * location                  true
   * ["test_location"]         false
   * pose["bone"].location     true
   * pose["bone"].location.x   true
   */
  static bool contains(const char *prop_id, const char *api_path_component);
};

bool api_prop_affects_params_node(const ApiPtr *ptr, const ApiProp *prop);

}  // namespace dune::dgraph
