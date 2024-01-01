#include "lib_length_parameterize.hh"
#include "lib_task.hh"

namespace dune::length_parameterize {

void sample_uniform(const Span<float> lengths,
                    const bool include_last_point,
                    MutableSpan<int> r_segment_indices,
                    MutableSpan<float> r_factors)
{
  const int count = r_segment_indices.size();
  lib_assert(count > 0);
  lib_assert(lengths.size() >= 1);
  lib_assert(std::is_sorted(lengths.begin(), lengths.end()));

  if (count == 1) {
    r_segment_indices[0] = 0;
    r_factors[0] = 0.0f;
    return;
  }
  const float total_length = lengths.last();
  const float step_length = total_length / (count - include_last_point);
  threading::parallel_for(IndexRange(count), 512, [&](const IndexRange range) {
    SampleSegmentHint hint;
    for (const int i : range) {
      /* Use min to avoid issues w floating point accuracy. */
      const float sample_length = std::min(total_length, i * step_length);
      sample_at_length(lengths, sample_length, r_segment_indices[i], r_factors[i], &hint);
    }
  });
}

void sample_at_lengths(const Span<float> accumulated_segment_lengths,
                       const Span<float> sample_lengths,
                       MutableSpan<int> r_segment_indices,
                       MutableSpan<float> r_factors)
{
  lib_assert(
      std::is_sorted(accumulated_segment_lengths.begin(), accumulated_segment_lengths.end()));
  lib_assert(std::is_sorted(sample_lengths.begin(), sample_lengths.end()));

  const int count = sample_lengths.size();
  lib_assert(count == r_segment_indices.size());
  lib_assert(count == r_factors.size());

  threading::parallel_for(IndexRange(count), 512, [&](const IndexRange range) {
    SampleSegmentHint hint;
    for (const int i : range) {
      const float sample_length = sample_lengths[i];
      sample_at_length(
          accumulated_segment_lengths, sample_length, r_segment_indices[i], r_factors[i], &hint);
    }
  });
}

}  // namespace dune::length_parameterize
