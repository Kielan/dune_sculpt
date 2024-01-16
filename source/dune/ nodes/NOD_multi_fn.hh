#pragma once

#include "fn_multi_fn.hh"

#include "types_node.h"
namespace dune::nodes {

class NodeMultiFns;

/* Util class to help nodes build a multi-fn for themselves. */
class NodeMultiFnBuilder : NonCopyable, NonMovable {
 private:
  const Node &node_;
  const NodeTree &tree_;
  std::shared_ptr<mf::MultiFn> owned_built_fn_;
  const mf::MultiFn *built_fn_ = nullptr;

  friend NodeMultiFns;

 public:
  NodeMultiFunctionBuilder(const bNode &node, const bNodeTree &tree);

  /* Assign a multi-fn for the current node. The input and output parameters of the function
   * have to match the available sockets in the node. */
  void set_matching_fn(const mf::MultiFn *fn);
  void set_matching_fn(const mf::MultiFn &fn);

  /* Util method for creating and assigning a multi-function when it can't have a static
   * lifetime. */
  template<typename T, typename... Args> void construct_and_set_matching_fn(Args &&...args);

  const Node &node();
  const NodeTree &tree();
};

/* Gives access to multi-fns for all nodes in a node tree that support them. */
class NodeMultiFns {
 public:
  struct Item {
    const mf::MultiFn *fn = nullptr;
    std::shared_ptr<mf::MultiFn> owned_fn;
  };

 private:
  Map<const Node *, Item> map_;

 public:
  NodeMultiFns(const NodeTree &tree);

  const Item &try_get(const Node &node) const;
};

/* NodeMultiFnBuilder Inline Methods */

inline NodeMultiFnBuilder::NodeMultiFnBuilder(const bNode &node, const bNodeTree &tree)
    : node_(node), tree_(tree)
{
}

inline const Node &NodeMultiFnBuilder::node()
{
  return node_;
}

inline const NodeTree &NodeMultiFnBuilder::tree()
{
  return tree_;
}

inline void NodeMultiFunctionBuilder::set_matching_fn(const mf::MultiFunction *fn)
{
  built_fn_ = fn;
}

inline void NodeMultiFunctionBuilder::set_matching_fn(const mf::MultiFunction &fn)
{
  built_fn_ = &fn;
}

template<typename T, typename... Args>
inline void NodeMultiFunctionBuilder::construct_and_set_matching_fn(Args &&...args)
{
  owned_built_fn_ = std::make_shared<T>(std::forward<Args>(args)...);
  built_fn_ = &*owned_built_fn_;
}

/* NodeMultiFns Inline Methods */
inline const NodeMultiFns::Item &NodeMultiFns::try_get(const Node &node) const
{
  static Item empty_item;
  const Item *item = map_.lookup_ptr(&node);
  if (item == nullptr) {
    return empty_item;
  }
  return *item;
}

/** \} */

}  // namespace blender::nodes
