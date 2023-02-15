#include "wm_api.h"

#include "ed_geometry.h"

#include "geometry_intern.hh"

/**************************** registration **********************************/

void ed_optypes_geometry(void)
{
  using namespace dune::ed::geometry;

  wm_optype_append(GEOMETRY_OT_attribute_add);
  wm_optype_append(GEOMETRY_OT_attribute_remove);
  wm_optype_append(GEOMETRY_OT_attribute_convert);
}
