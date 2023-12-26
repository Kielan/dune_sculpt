#include "lib_ordered_edge.hh"

namespace dune {

std::ostream &operator<<(std::ostream &stream, const OrderedEdge &e)
{
  return stream << "OrderedEdge(" << e.v_low << ", " << e.v_high << ")";
}

}  // namespace
