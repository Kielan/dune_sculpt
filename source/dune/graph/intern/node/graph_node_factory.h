#pragma once

#include "mem_guardedalloc.h"

#include "intern/graph_type.h"
#include "intern/node/graph_node.h"

struct Id;

namespace dune {
namespace dgraph {

struct DGraphNodeFactory {
  virtual NodeType type() const = 0;
  virtual const char *type_name() const = 0;

  virtual int id_recalc_tag() const = 0;

  virtual Node *create_node(const Id *id, const char *subdata, const char *name) const = 0;
};

template<class ModeObjectType> struct DGraphNodeFactoryImpl : public DGraphNodeFactory {
  virtual NodeType type() const override;
  virtual const char *type_name() const override;

  virtual int id_recalc_tag() const override;

  virtual Node *create_node(const Id *id, const char *subdata, const char *name) const override;
};

/* Register typeinfo */
void register_node_typeinfo(DGraphNodeFactory *factory);

/* Get typeinfo for specified type */
DGraphNodeFactory *type_get_factory(NodeType type);

}  // namespace dgraph
}  // namespace dune

#include "intern/node/dgraph_node_factory_impl.h"
