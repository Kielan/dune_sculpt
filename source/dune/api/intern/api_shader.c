#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "types_simulation_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#else

static void api_def_simulation(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Simulation", "ID");
  api_def_struct_ui_text(sapi, "Simulation", "Simulation data-block");
  api_def_struct_ui_icon(sapi, ICON_PHYSICS); /* TODO: Use correct icon. */

  prop = api_def_prop(sapi, "node_tree", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "nodetree");
  api_def_prop_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Node Tree", "Node tree defining the simulation");

  /* common */
  api_def_animdata_common(sapi);
}

void api_def_simulation(DuneApi *dapi)
{
  api_def_simulation(dapi);
}

#endif
