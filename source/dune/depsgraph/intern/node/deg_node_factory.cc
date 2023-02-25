#include "intern/node/deg_node_factory.h"

namespace dune::deg {

/* Global type registry */
static DepsNodeFactory *node_typeinfo_registry[static_cast<int>(NodeType::NUM_TYPES)] = {nullptr};

void register_node_typeinfo(DepsNodeFactory *factory)
{
  lib_assert(factory != nullptr);
  const int type_as_int = static_cast<int>(factory->type());
  node_typeinfo_registry[type_as_int] = factory;
}

DepsNodeFactory *type_get_factory(const NodeType type)
{
  /* Look up type - at worst, it doesn't exist in table yet, and we fail. */
  const int type_as_int = static_cast<int>(type);
  return node_typeinfo_registry[type_as_int];
}

}  // namespace dune::deg
