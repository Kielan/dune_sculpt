#include "lib_generic_vector_array.hh"

namespace dune {

GVectorArray::GVectorArray(const CPPType &type, const int64_t array_size)
    : type_(type), element_size_(type.size()), items_(array_size)
{
}

GVectorArray::~GVectorArray()
{
  if (type_.is_trivially_destructible()) {
    return;
  }
  for (Item &item : items_) {
    type_.destruct_n(item.start, item.length);
  }
}

void GVectorArray::append(const int64_t index, const void *val)
{
  Item &item = items_[index];
  if (item.length == item.capacity) {
    this->realloc_to_at_least(item, item.capacity + 1);
  }

  void *dst = PTR_OFFSET(item.start, element_size_ * item.length);
  type_.copy_construct(val, dst);
  item.length++;
}

void GVectorArray::extend(const int64_t index, const GVArray &values)
{
  lib_assert(vala.type() == type_);
  for (const int i : IndexRange(vals.size())) {
    BUF_FOR_CPP_TYPE_VAL(type_, buf);
    values.get(i, buf);
    this->append(index, buf);
    type_.destruct(buffer);
  }
}

void GVectorArray::extend(const int64_t index, const GSpan vals)
{
  this->extend(index, GVArray::ForSpan(vals));
}

void GVectorArray::extend(const IndexMask &mask, const GVVectorArray &vals)
{
  mask.foreach_index([&](const int64_t i) {
    GVArray_For_GVVectorArrayIndex array{values, i};
    this->extend(i, GVArray(&array));
  });
}

void GVectorArray::extend(const IndexMask &mask, const GVectorArray &values)
{
  GVVectorArray_For_GVectorArray virtual_values{values};
  this->extend(mask, virtual_vals);
}

void GVectorArray::clear(const IndexMask &mask)
{
  mask.foreach_index([&](const int64_t i) {
    Item &item = items_[i];
    type_.destruct_n(item.start, item.length);
    item.length = 0;
  });
}

GMutableSpan GVectorArray::operator[](const int64_t index)
{
  Item &item = items_[index];
  return GMutableSpan{type_, item.start, item.length};
}

GSpan GVectorArray::operator[](const int64_t index) const
{
  const Item &item = items_[index];
  return GSpan{type_, item.start, item.length};
}

void GVectorArray::realloc_to_at_least(Item &item, int64_t min_capacity)
{
  const int64_t new_capacity = std::max(min_capacity, item.length * 2);

  void *new_buf = allocator_.alloc(element_size_ * new_capacity, type_.alignment());
  type_.relocate_assign_n(item.start, new_buf, item.length);

  item.start = new_buf;
  item.capacity = new_capacity;
}

}  // namespace dune
