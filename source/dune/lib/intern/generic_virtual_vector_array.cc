#include "lib_generic_virtual_vector_array.hh"

namespace dune {

void GVArray_For_GVVectorArrayIndex::get(const int64_t index_in_vector, void *r_val) const
{
  vector_array_.get_vector_element(index_, index_in_vector, r_val);
}

void GVArray_For_GVVectorArrayIndex::get_to_uninitialized(const int64_t index_in_vector,
                                                          void *r_val) const
{
  type_->default_construct(r_val);
  vector_array_.get_vector_element(index_, index_in_vector, r_val);
}

int64_t GVVectorArray_For_SingleGVArray::get_vector_size_impl(const int64_t /*index*/) const
{
  return varray_.size();
}

void GVVectorArray_For_SingleGVArray::get_vector_element_impl(const int64_t /*index*/,
                                                              const int64_t index_in_vector,
                                                              void *r_val) const
{
  varray_.get(index_in_vector, r_val);
}

bool GVVectorArray_For_SingleGVArray::is_single_vector_impl() const
{
  return true;
}

int64_t GVVectorArray_For_SingleGSpan::get_vector_size_impl(const int64_t /*index*/) const
{
  return span_.size();
}

void GVVectorArray_For_SingleGSpan::get_vector_element_impl(const int64_t /*index*/,
                                                            const int64_t index_in_vector,
                                                            void *r_val) const
{
  type_->copy_assign(span_[index_in_vector], r_val);
}

bool GVVectorArray_For_SingleGSpan::is_single_vector_impl() const
{
  return true;
}

}  // namespace dune
