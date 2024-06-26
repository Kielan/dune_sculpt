#include "LIB_span.hh"
#include "LIB_virtual_array.hh"

#include "KERNEL_spline.hh"

using dune::float3;
using dune::GVArray;
using dune::MutableSpan;
using dune::Span;

void PolySpline::copy_settings(Spline &UNUSED(dst)) const
{
  /* Poly splines have no settings not covered by the base class. */
}

void PolySpline::copy_data(Spline &dst) const
{
  PolySpline &poly = static_cast<PolySpline &>(dst);
  poly.positions_ = positions_;
  poly.radii_ = radii_;
  poly.tilts_ = tilts_;
}

int PolySpline::size() const
{
  const int size = positions_.size();
  LIB_assert(size == radii_.size());
  LIB_assert(size == tilts_.size());
  return size;
}

void PolySpline::resize(const int size)
{
  positions_.resize(size);
  radii_.resize(size);
  tilts_.resize(size);
  this->mark_cache_invalid();
  attributes.reallocate(size);
}

MutableSpan<float3> PolySpline::positions()
{
  return positions_;
}
Span<float3> PolySpline::positions() const
{
  return positions_;
}
MutableSpan<float> PolySpline::radii()
{
  return radii_;
}
Span<float> PolySpline::radii() const
{
  return radii_;
}
MutableSpan<float> PolySpline::tilts()
{
  return tilts_;
}
Span<float> PolySpline::tilts() const
{
  return tilts_;
}

void PolySpline::reverse_impl()
{
}

void PolySpline::mark_cache_invalid()
{
  tangent_cache_dirty_ = true;
  normal_cache_dirty_ = true;
  length_cache_dirty_ = true;
}

int PolySpline::evaluated_points_size() const
{
  return this->size();
}

void PolySpline::correct_end_tangents() const
{
}

Span<float3> PolySpline::evaluated_positions() const
{
  return this->positions();
}

GVArray PolySpline::interpolate_to_evaluated(const GVArray &src) const
{
  LIB_assert(src.size() == this->size());
  return src;
}
