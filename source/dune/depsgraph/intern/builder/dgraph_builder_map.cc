#include "intern/builder/dgraph_builder_map.h"

#include "types_id.h"

namespace dune::deg {

bool BuilderMap::checkIsBuilt(Id *id, int tag) const
{
  return (getIdTag(id) & tag) == tag;
}

void BuilderMap::tagBuild(Id *id, int tag)
{
  id_tags_.lookup_or_add(id, 0) |= tag;
}

bool BuilderMap::checkIsBuiltAndTag(Id *id, int tag)
{
  int &id_tag = id_tags_.lookup_or_add(id, 0);
  const bool result = (id_tag & tag) == tag;
  id_tag |= tag;
  return result;
}

int BuilderMap::getIdTag(Id *id) const
{
  return id_tags_.lookup_default(id, 0);
}

}  // namespace dune::deg
