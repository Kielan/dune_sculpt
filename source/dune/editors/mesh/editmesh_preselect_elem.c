#include "MEM_guardedalloc.h"

#include "BLI_math.h"

#include "BKE_editmesh.h"

#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_state.h"

#include "DNA_object_types.h"

#include "ED_mesh.h"
#include "ED_view3d.h"

/* -------------------------------------------------------------------- */
/** Mesh Element Pre-Select
 * Public API:
 *
 * EDBM_preselect_elem_create
 * EDBM_preselect_elem_destroy
 * EDBM_preselect_elem_clear
 * EDBM_preselect_elem_draw
 * EDBM_preselect_elem_update_from_single
 *
 **/
