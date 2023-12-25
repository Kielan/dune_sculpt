#include "lib_cpp_type.hh"

#include <sstream>

namespace dune {

std::string CPPType::to_string(const void *val) const
{
  std::stringstream ss;
  this->print(val, ss);
  return ss.str();
}

void CPPType::print_or_default(const void *val,
                               std::stringstream &ss,
                               StringRef default_val) const
{
  if (this->is_printable()) {
    this->print(val, ss);
  }
  else {
    ss << default_val;
  }
}

}  // namespace dune
