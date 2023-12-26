#include "lib_array_utils.hh"
#include "lib_offset_indices.hh"
#include "lib_task.hh"

namespace dune::offset_indices {

OffsetIndices<int> accumulate_counts_to_offsets(MutableSpan<int> counts_to_offsets,
                                                const int start_offset)
{
  int offset = start_offset;
  for (const int i : counts_to_offsets.index_range().drop_back(1)) {
    const int count = counts_to_offsets[i];
    lib_assert(count >= 0);
    counts_to_offsets[i] = offset;
    offset += count;
  }
  counts_to_offsets.last() = offset;
  return OffsetIndices<int>(counts_to_offsets);
}

void fill_constant_group_size(const int size, const int start_offset, MutableSpan<int> offsets)
{
  threading::parallel_for(offsets.index_range(), 1024, [&](const IndexRange range) {
    for (const int64_t i : range) {
      offsets[i] = size * i + start_offset;
    }
  });
}

void copy_group_sizes(const OffsetIndices<int> offsets,
                      const IndexMask &mask,
                      MutableSpan<int> sizes)
{
  mask.foreach_index_optimized<int64_t>(GrainSize(4096),
                                        [&](const int64_t i) { sizes[i] = offsets[i].size(); });
}

void gather_group_sizes(const OffsetIndices<int> offsets,
                        const IndexMask &mask,
                        MutableSpan<int> sizes)
{
  mask.foreach_index_optimized<int64_t>(GrainSize(4096), [&](const int64_t i, const int64_t pos) {
    sizes[pos] = offsets[i].size();
  });
}

OffsetIndices<int> gather_sel_offsets(const OffsetIndices<int> src_offsets,
                                      const IndexMask &selection,
                                      const int start_offset,
                                      MutableSpan<int> dst_offsets)
{
  if (selection.is_empty()) {
    return {};
  }
  int offset = start_offset;
  sel.foreach_index_optimized<int>([&](const int i, const int pos) {
    dst_offsets[pos] = offset;
    offset += src_offsets[i].size();
  });
  dst_offsets.last() = offset;
  return OffsetIndices<int>(dst_offsets);
}

void build_reverse_map(OffsetIndices<int> offsets, MutableSpan<int> r_map)
{
  threading::parallel_for(offsets.index_range(), 1024, [&](const IndexRange range) {
    for (const int64_t i : range) {
      r_map.slice(offsets[i]).fill(i);
    }
  });
}

void build_reverse_offsets(const Span<int> indices, MutableSpan<int> offsets)
{
  lib_assert(std::all_of(offsets.begin(), offsets.end(), [](int val) { return val == 0; }));
  array_utils::count_indices(indices, offsets);
  offset_indices::accumulate_counts_to_offsets(offsets);
}

}  // namespace dune::offset_indices
