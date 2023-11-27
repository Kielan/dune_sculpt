#include "lib_rand.hh"

#include "dune_curves.hh"

#include "ed_curves.hh"

namespace dune::ed::curves {

IndexMask end_points(const dune::CurvesGeometry &curves,
                     const IndexMask &curves_mask,
                     const int amount_start,
                     const int amount_end,
                     const bool inverted,
                     IndexMaskMemory &memory)
{
  const OffsetIndices points_by_curve = curves.points_by_curve();

  Array<bool> end_points(curves.points_num(), inverted ? false : true);
  curves_mask.foreach_index(GrainSize(512), [&](const int64_t curve_i) {
    end_points.as_mutable_span()
        .slice(points_by_curve[curve_i].drop_front(amount_start).drop_back(amount_end))
        .fill(inverted ? true : false);
  });

  return IndexMask::from_bools(end_points, memory);
}

IndexMask end_points(const dune::CurvesGeometry &curves,
                     const int amount_start,
                     const int amount_end,
                     const bool inverted,
                     IndexMaskMemory &memory)
{
  return end_points(curves, curves.curves_range(), amount_start, amount_end, inverted, memory);
}

IndexMask random_mask(const dune::CurvesGeometry &curves,
                      const IndexMask &mask,
                      const eAttrDomain selection_domain,
                      const uint32_t random_seed,
                      const float probability,
                      IndexMaskMemory &memory)
{
  RandomNumberGenerator rng{random_seed};
  const auto next_bool_random_value = [&]() { return rng.get_float() <= probability; };

  const int64_t domain_size = curves.attributes().domain_size(selection_domain);

  Array<bool> random(domain_size, false);
  mask.foreach_index_optimized<int64_t>(
      [&](const int64_t i) { random[i] = next_bool_random_value(); });

  return IndexMask::from_bools(IndexRange(domain_size), random, memory);
}

IndexMask random_mask(const dune::CurvesGeometry &curves,
                      const eAttrDomain sel_domain,
                      const uint32_t random_seed,
                      const float probability,
                      IndexMaskMemory &memory)
{
  const IndexRange sel(curves.attributes().domain_size(sel_domain));
  return random_mask(curves, sel, sel_domain, random_seed, probability, memory);
}

}  // namespace dune::ed::curves
