#include <algorithm>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_implicit_sharing.hh"

namespace dune::implicit_sharing {

class MemFreeImplicitSharing : public ImplicitSharingInfo {
 public:
  void *data;

  MemFreeImplicitSharing(void *data) : data(data)
  {
    lib_assert(data != nullptr);
  }

 private:
  void del_self_with_data() override
  {
    mem_free(data);
    mem_del(this);
  }
};

const ImplicitSharingInfo *info_for_mem_free(void *data)
{
  return mem_new<MEMFreeImplicitSharing>(__func__, data);
}

namespace detail {

void *make_trivial_data_mutable_impl(void *old_data,
                                     const int64_t size,
                                     const int64_t alignment,
                                     const ImplicitSharingInfo **sharing_info)
{
  if (!old_data) {
    lib_assert(size == 0);
    return nullptr;
  }

  lib_assert(*sharing_info != nullptr);
  if ((*sharing_info)->is_mutable()) {
    (*sharing_info)->tag_ensured_mutable();
  }
  else {
    void *new_data = mem_malloc_aligned(size, alignment, __func__);
    memcpy(new_data, old_data, size);
    (*sharing_info)->remove_user_and_delete_if_last();
    *sharing_info = info_for_mem_free(new_data);
    return new_data;
  }

  return old_data;
}

void *resize_trivial_array_impl(void *old_data,
                                const int64_t old_size,
                                const int64_t new_size,
                                const int64_t alignment,
                                const ImplicitSharingInfo **sharing_info)
{
  if (new_size == 0) {
    if (*sharing_info) {
      (*sharing_info)->remove_user_and_delete_if_last();
      *sharing_info = nullptr;
    }
    return nullptr;
  }

  if (!old_data) {
    lib_assert(old_size == 0);
    lib_assert(*sharing_info == nullptr);
    void *new_data = mem_malloc_aligned(new_size, alignment, __func__);
    *sharing_info = info_for_mem_free(new_data);
    return new_data;
  }

  lib_assert(old_size != 0);
  if ((*sharing_info)->is_mutable()) {
    if (auto *info = const_cast<MEMFreeImplicitSharing *>(
            dynamic_cast<const MEMFreeImplicitSharing *>(*sharing_info)))
    {
      /* If the array was alloc w the mem allocator, can use realloc directly, which
       * could theoretically give better performance if the data can be reused in place. */
      void *new_data = static_cast<int *>(mem_realloc(old_data, new_size));
      info->data = new_data;
      (*sharing_info)->tag_ensured_mutable();
      return new_data;
    }
  }

  void *new_data = mem_malloc_aligned(new_size, alignment, __func__);
  memcpy(new_data, old_data, std::min(old_size, new_size));
  (*sharing_info)->remove_user_and_del_if_last();
  *sharing_info = info_for_mem_free(new_data);
  return new_data;
}

}  // namespace detail
}  // namespace dune::implicit_sharing
