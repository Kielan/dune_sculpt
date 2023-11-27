#include "dune_curves.hh"

#include "types_ob.h"

#include "ed_curves.hh"
#include "ed_transverts.hh"

namespace dune::ed::curves {

void transverts_from_curves_positions_create(dune::CurvesGeometry &curves, TransVertStore *tvs)
{
  IndexMaskMemory memory;
  IndexMask sel = retrieve_sel_points(curves, memory);
  MutableSpan<float3> positions = curves.positions_for_write();

  tvs->transverts = static_cast<TransVert *>(
      mem_calloc_array(sel.size(), sizeof(TransVert), __func__));
  tvs->transverts_tot = sel.size();

  sel.foreach_index(GrainSize(1024), [&](const int64_t i, const int64_t pos) {
    TransVert &tv = tvs->transverts[pos];
    tv.loc = positions[i];
    tv.flag = SEL;
    copy_v3_v3(tv.oldloc, tv.loc);
  });
}

}  // namespace dune::ed::curves

float (*ed_curves_point_normals_array_create(const Curves *curves_id))[3]
{
  using namespace dune;
  const dune::CurvesGeometry &curves = curves_id->geometry.wrap();
  const int size = curves.points_num();
  float3 *data = static_cast<float3 *>(mem_malloc_array(size, sizeof(float3), __func__));
  dune::curves_normals_point_domain_calc(curves, {data, size});
  return reinterpret_cast<float(*)[3]>(data);
}
