#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "STRUCTS_curve_types.h"
#include "STRUCTS_key_types.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_mesh_types.h"
#include "STRUCTS_meta_types.h"
#include "STRUCTS_object_types.h"
#include "STRUCTS_pointcloud_types.h"
#include "STRUCTS_scene_types.h"

#include "LI_edgehash.h"
#include "LI_index_range.hh"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_string.h"
#include "LI_utildefines.h"

#include "KE_DerivedMesh.h"
#include "KE_deform.h"
#include "KE_displist.h"
#include "KE_editmesh.h"
#include "KE_geometry_set.hh"
#include "KE_key.h"
#include "KE_lib_id.h"
#include "KE_lib_query.h"
#include "KE_main.h"
#include "KE_material.h"
#include "KE_mball.h"
#include "KE_mesh.h"
#include "KE_mesh_runtime.h"
#include "KE_mesh_wrapper.h"
#include "KE_modifier.h"
#include "KE_spline.hh"
/* these 2 are only used by conversion functions */
#include "KE_curve.h"
/* -- */
#include "KE_object.h"
/* -- */
#include "KE_pointcloud.h"

#include "KE_curve_to_mesh.hh"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"
