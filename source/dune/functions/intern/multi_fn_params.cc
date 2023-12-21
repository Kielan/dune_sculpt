#include "fn_multi_params.hh"

namespace dune::fn::multi_fn {

void ParamsBuilder::add_unused_output_for_unsupporting_fn(const CPPType &type)
{
  ResourceScope &scope = this->resource_scope();
  void *buf = scope.linear_allocator().allocate(type.size() * min_array_size_,
                                                type.alignment());
  const GMutableSpan span{type, buf, min_array_size_};
  actual_params_.append_unchecked_as(std::in_place_type<GMutableSpan>, span);
  if (!type.is_trivially_destructible()) {
    scope.add_destruct_call(
        [&type, buf, mask = mask_]() { type.destruct_indices(buf, mask); });
  }
}

}  // namespace dune::fn::multi_fn
