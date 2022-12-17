#include "MEM_guardedalloc.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define STRUCTS_DEPRECATED_ALLOW

#include "STRUCTS_defaults.h"
#include "STRUCTS_key_types.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_mesh_types.h"
#include "STRUCTS_meshdata_types.h"
#include "STRUCTS_object_types.h"

#include "LI_bitmap.h"
#include "LI_edgehash.h"
#include "LI_endian_switch.h"
#include "LI_ghash.h"
#include "LI_hash.h"
#include "LI_index_range.hh"
#include "LI_linklist.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_math_vector.hh"
#include "LI_memarena.h"
#include "LI_string.h"
#include "LI_task.hh"
#include "LI_utildefines.h"

#include "TRANSLATION_translation.h"

#include "KERNEL_anim_data.h"
#include "KERNEL_bpath.h"
#include "KERNEL_deform.h"
#include "KERNEL_editmesh.h"
#include "KERNEL_global.h"
#include "KERNEL_idtype.h"
#include "KERNEL_key.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_lib_query.h"
#include "KERNEL_main.h"
#include "KERNEL_material.h"
#include "KERNEL_mesh.h"
#include "KERNEL_mesh_runtime.h"
#include "KERNEL_mesh_wrapper.h"
#include "KERNEL_modifier.h"
#include "KERNEL_multires.h"
#include "KERNEL_object.h"

#include "PIL_time.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "LOADER_read_write.h"
