#include "lib_bit_ref.hh"

#include <ostream>

namespace dune::bits {

std::ostream &operator<<(std::ostream &stream, const BitRef &bit)
{
  return stream << (bit ? '1' : '0');
}

std::ostream &operator<<(std::ostream &stream, const MutableBitRef &bit)
{
  return stream << BitRef(bit);
}

}  // namespace dune::bits
