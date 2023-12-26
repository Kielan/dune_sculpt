#include "lib_math_basis_types.hh"

#include <ostream>

namespace dune::math {

std::ostream &operator<<(std::ostream &stream, const Axis axis)
{
  switch (axis.axis_) {
    default:
      lib_assert_unreachable();
      return stream << "Invalid Axis";
    case Axis::Val::X:
      return stream << 'X';
    case Axis::Val::Y:
      return stream << 'Y';
    case Axis::Val::Z:
      return stream << 'Z';
  }
}
std::ostream &operator<<(std::ostream &stream, const AxisSigned axis)
{
  switch (axis.axis_) {
    default:
      lib_assert_unreachable();
      return stream << "Invalid AxisSigned";
    case AxisSigned::Val::X_POS:
    case AxisSigned::Val::Y_POS:
    case AxisSigned::Val::Z_POS:
    case AxisSigned::Val::X_NEG:
    case AxisSigned::Val::Y_NEG:
    case AxisSigned::Val::Z_NEG:
      return stream << axis.axis() << (axis.sign() == -1 ? '-' : '+');
  }
}
std::ostream &operator<<(std::ostream &stream, const CartesianBasis &rot)
{
  return stream << "CartesianBasis" << rot.axes;
}

}  // namespace dune::math
