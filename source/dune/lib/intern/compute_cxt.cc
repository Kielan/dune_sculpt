#include "lib_compute_cxt.hh"
#include "lib_hash_md5.h"
#include <sstream>

namespace dune {

void ComputeCxtHash::mix_in(const void *data, int64_t len)
{
  DynamicStackBuf<> buf_owner(HashSizeInBytes + len, 8);
  char *buf = static_cast<char *>(buf_owner.buf());
  memcpy(buf, this, HashSizeInBytes);
  memcpy(buf + HashSizeInBytes, data, len);

  lib_hash_md5_buf(buf, HashSizeInBytes + len, this);
}

std::ostream &operator<<(std::ostream &stream, const ComputeCxtHash &hash)
{
  std::stringstream ss;
  ss << "0x" << std::hex << hash.v1 << hash.v2;
  stream << ss.str();
  return stream;
}

void ComputeCxt::print_stack(std::ostream &stream, StringRef name) const
{
  Stack<const ComputeCxt *> stack;
  for (const ComputeCxt *current = this; current; current = current->parent_) {
    stack.push(current);
  }
  stream << "Cxt Stack: " << name << "\n";
  while (!stack.is_empty()) {
    const ComputeCxt *current = stack.pop();
    stream << "-> ";
    current->print_current_in_line(stream);
    const ComputeCxtHash &current_hash = current->hash_;
    stream << " \t(hash: " << current_hash << ")\n";
  }
}

std::ostream &operator<<(std::ostream &stream, const ComputeContext &compute_context)
{
  compute_context.print_stack(stream, "");
  return stream;
}

}  // namespace blender
