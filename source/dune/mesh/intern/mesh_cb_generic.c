/* mesh element cb fns. */
#include "lib_utildefines.h"
#include "mesh.h"
#include "intern/mesh_cb_generic.h"

bool mesh_elem_cb_check_hflag_ex(MElem *ele, void *user_data)
{
  const uint hflag_pair = PTR_AS_INT(user_data);
  const char hflag_p = (hflag_pair & 0xff);
  const char hflag_n = (hflag_pair >> 8);

  return ((mesh_elem_flag_test(ele, hflag_p) != 0) && (mesh_elem_flag_test(ele, hflag_n) == 0));
}

bool mesh_elem_cb_check_hflag_enabled(MElem *ele, void *user_data)
{
  const char hflag = PTR_AS_INT(user_data);

  return (mesh_elem_flag_test(ele, hflag) != 0);
}

bool mesh_elem_cb_check_hflag_disabled(MElem *ele, void *user_data)
{
  const char hflag = PTR_AS_INT(user_data);

  return (mesh_elem_flag_test(ele, hflag) == 0);
}

bool mesh_elem_cb_check_elem_not_equal(MElem *ele, void *user_data)
{
  return (ele != user_data);
}
