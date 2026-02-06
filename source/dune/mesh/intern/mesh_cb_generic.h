#pragma once

/* mesh */
bool mesh_elem_cb_check_hflag_enabled(MElem *, void *user_data);
bool mesh_elem_cb_check_hflag_disabled(MElem *, void *user_data);
bool mesh_elem_cb_check_hflag_ex(MElem *, void *user_data);
bool mesh_elem_cb_check_elem_not_equal(MElem *ele, void *user_data);

#define mesh_elem_cb_check_hflag_ex_simple(type, hflag_p, hflag_n) \
  (bool (*)(type, void *)) mesh_elem_cb_check_hflag_ex, \
      PTR_FROM_UINT(((hflag_p) | (hflag_n << 8)))

#define mesh_elem_cb_check_hflag_enabled_simple(type, hflag_p) \
  (bool (*)(type, void *)) mesh_elem_cb_check_hflag_enabled, PTR_FROM_UINT((hflag_p))

#define mesh_elem_cb_check_hflag_disabled_simple(type, hflag_n) \
  (bool (*)(type, void *)) mesh_elem_cb_check_hflag_disabled, PTR_FROM_UINT(hflag_n)
