#pragma once

#include "intern/node/dgraph_node_factory.h"

struct Id;

namespace dune {
namespace dgraph {

template<class ModeObjectType> NodeType DGraphNodeFactoryImpl<ModeObjectType>::type() const
{
  return ModeObjectType::typeinfo.type;
}

template<class ModeObjectType> const char *DGraphNodeFactoryImpl<ModeObjectType>::type_name() const
{
  return ModeObjectType::typeinfo.type_name;
}

template<class ModeObjectType> int DGraphNodeFactoryImpl<ModeObjectType>::id_recalc_tag() const
{
  return ModeObjectType::typeinfo.id_recalc_tag;
}

template<class ModeObjectType>
Node *DGraphNodeFactoryImpl<ModeObjectType>::create_node(const Id *id,
                                                       const char *subdata,
                                                       const char *name) const
{
  Node *node = new ModeObjectType();
  /* Populate base node settings. */
  node->type = type();
  /* Set name if provided, or use default type name. */
  if (name[0] != '\0') {
    node->name = name;
  }
  else {
    node->name = type_name();
  }
  node->init(id, subdata);
  return node;
}

}  // namespace dgraph
}  // namespace dune
