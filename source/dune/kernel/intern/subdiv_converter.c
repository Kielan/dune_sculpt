#include "subdiv_converter.h"

#include "LIB_utildefines.h"

#include "opensubdiv_converter_capi.h"

void KERNEL_subdiv_converter_free(struct OpenSubdiv_Converter *converter)
{
  if (converter->freeUserData) {
    converter->freeUserData(converter);
  }
}

int KERNEL_subdiv_converter_vtx_boundary_interpolation_from_settings(const SubdivSettings *settings)
{
  switch (settings->vtx_boundary_interpolation) {
    case SUBDIV_VTX_BOUNDARY_NONE:
      return OSD_VTX_BOUNDARY_NONE;
    case SUBDIV_VTX_BOUNDARY_EDGE_ONLY:
      return OSD_VTX_BOUNDARY_EDGE_ONLY;
    case SUBDIV_VTX_BOUNDARY_EDGE_AND_CORNER:
      return OSD_VTX_BOUNDARY_EDGE_AND_CORNER;
  }
  LIB_assert_msg(0, "Unknown vtx boundary interpolation");
  return OSD_VTX_BOUNDARY_EDGE_ONLY;
}

/*OpenSubdiv_FVarLinearInterpolation*/ int KERNEL_subdiv_converter_fvar_linear_from_settings(
    const SubdivSettings *settings)
{
  switch (settings->fvar_linear_interpolation) {
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_NONE:
      return OSD_FVAR_LINEAR_INTERPOLATION_NONE;
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_ONLY:
      return OSD_FVAR_LINEAR_INTERPOLATION_CORNERS_ONLY;
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_AND_JUNCTIONS:
      return OSD_FVAR_LINEAR_INTERPOLATION_CORNERS_PLUS1;
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_JUNCTIONS_AND_CONCAVE:
      return OSD_FVAR_LINEAR_INTERPOLATION_CORNERS_PLUS2;
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_BOUNDARIES:
      return OSD_FVAR_LINEAR_INTERPOLATION_BOUNDARIES;
    case SUBDIV_FVAR_LINEAR_INTERPOLATION_ALL:
      return OSD_FVAR_LINEAR_INTERPOLATION_ALL;
  }
  LIB_assert_msg(0, "Unknown fvar linear interpolation");
  return OSD_FVAR_LINEAR_INTERPOLATION_NONE;
}
