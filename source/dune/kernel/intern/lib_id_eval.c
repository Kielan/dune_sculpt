/**
 * Contains management of ID's and libraries
 * allocate and free of all library data
 */

#include "structs_ID.h"
#include "structs_mesh_types.h"

#include "LIB_utildefines.h"

#include "KERNEL_lib_id.h"
#include "KERNEL_mesh.h"

void KERNEL_id_eval_properties_copy(ID *id_cow, ID *id)
{
  const ID_Type id_type = GS(id->name);
  LIB_assert((id_cow->tag & LIB_TAG_COPIED_ON_WRITE) && !(id->tag & LIB_TAG_COPIED_ON_WRITE));
  LIB_assert(ID_TYPE_SUPPORTS_PARAMS_WITHOUT_COW(id_type));
  if (id_type == ID_ME) {
    KERNEL_mesh_copy_parameters((Mesh *)id_cow, (const Mesh *)id);
  }
  else {
    LIB_assert_unreachable();
  }
}
