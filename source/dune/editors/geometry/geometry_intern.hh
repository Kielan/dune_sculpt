#pragma once

struct wmOperatorType;

namespace dune::ed::geometry {

/* *** geometry_attributes.cc *** */
void GEOMETRY_OT_attribute_add(struct wmOperatorType *ot);
void GEOMETRY_OT_attribute_remove(struct wmOperatorType *ot);
void GEOMETRY_OT_attribute_convert(struct wmOperatorType *ot);

}  // namespace dune::ed::geometry
