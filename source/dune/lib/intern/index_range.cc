#include "lib_index_range.hh"

#include <ostream>

namespace dune {

std::ostream &operator<<(std::ostream &stream, IndexRange range)
{
  stream << '[' << range.start() << ", " << range.one_after_last() << ')';
  return stream;
}

}  // namespace dune
