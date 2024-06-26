#include "lib_bit_span.hh"
#include "lib_bit_span_ops.hh"

#include <ostream>

namespace dune::bits {

void MutableBitSpan::set_all()
{
  if (bit_range_.is_empty()) {
    return;
  }
  const AlignedIndexRanges ranges = split_index_range_by_alignment(bit_range_, BitsPerInt);
  {
    BitInt &first_int = *int_containing_bit(data_, bit_range_.start());
    const BitInt first_int_mask = mask_range_bits(ranges.prefix.start() & BitIndexMask,
                                                  ranges.prefix.size());
    first_int |= first_int_mask;
  }
  {
    BitInt *start = int_containing_bit(data_, ranges.aligned.start());
    const int64_t ints_to_fill = ranges.aligned.size() / BitsPerInt;
    constexpr BitInt fill_val = BitInt(-1);
    init_fill_n(start, ints_to_fill, fill_val);
  }
  {
    BitInt &last_int = *int_containing_bit(data_, bit_range_.one_after_last() - 1);
    const BitInt last_int_mask = mask_first_n_bits(ranges.suffix.size());
    last_int |= last_int_mask;
  }
}

void MutableBitSpan::reset_all()
{
  if (bit_range_.is_empty()) {
    return;
  }
  const AlignedIndexRanges ranges = split_index_range_by_alignment(bit_range_, BitsPerInt);
  {
    BitInt &first_int = *int_containing_bit(data_, bit_range_.start());
    const BitInt first_int_mask = mask_range_bits(ranges.prefix.start() & BitIndexMask,
                                                  ranges.prefix.size());
    first_int &= ~first_int_mask;
  }
  {
    BitInt *start = int_containing_bit(data_, ranges.aligned.start());
    const int64_t ints_to_fill = ranges.aligned.size() / BitsPerInt;
    constexpr BitInt fill_val = 0;
    initialized_fill_n(start, ints_to_fill, fill_val);
  }
  {
    BitInt &last_int = *int_containing_bit(data_, bit_range_.one_after_last() - 1);
    const BitInt last_int_mask = mask_first_n_bits(ranges.suffix.size());
    last_int &= ~last_int_mask;
  }
}

void MutableBitSpan::copy_from(const BitSpan other)
{
  lib_assert(this->size() == other.size());
  copy_from_or(*this, other);
}

void MutableBitSpan::copy_from(const BoundedBitSpan other)
{
  lib_assert(this->size() == other.size());
  copy_from_or(*this, other);
}

void MutableBoundedBitSpan::copy_from(const BitSpan other)
{
  lib_assert(this->size() == other.size());
  copy_from_or(*this, other);
}

void MutableBoundedBitSpan::copy_from(const BoundedBitSpan other)
{
  lib_assert(this->size() == other.size());
  copy_from_or(*this, other);
}

std::ostream &operator<<(std::ostream &stream, const BitSpan &span)
{
  stream << "(Size: " << span.size() << ", ";
  for (const BitRef bit : span) {
    stream << bit;
  }
  stream << ")";
  return stream;
}

std::ostream &operator<<(std::ostream &stream, const MutableBitSpan &span)
{
  return stream << BitSpan(span);
}

}  // namespace dune::bits
