#include "atomic_ops.h"

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BLI_math_geom.h"
#include "BLI_task.h"
#include "BLI_threads.h"

#include "BKE_bvhutils.h"
#include "BKE_lib_id.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"
#include "BKE_shrinkwrap.h"
#include "BKE_subdiv_ccg.h"
