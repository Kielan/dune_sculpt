/** Manage initializing resources and correctly shutting down. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* only called once, for startup */
void wm_init(DContext *C, int argc, const char **argv)
{

  if (!G.background) {
    wm_ghost_init(C); /* note: it assigns C to ghost! */
    wm_init_cursor_data();
    //dune_sound_jack_sync_cb_set(sound_jack_sync_callback);
  }
  
  GHOST_CreateSystemPaths();
  
  dune_addon_pref_type_init();
  dune_keyconfig_pref_type_init();
  
  wm_optype_init();
  wm_optypes_register();
  
  wm_paneltype_init(); /* Lookup table only. */
  wm_menutype_init();
  wm_uilisttype_init();
  wm_gizmotype_init();
  wm_gizmogrouptype_init();
  

  ed_undosys_type_init();

  dune_library_callback_free_notifier_reference_set(
      wm_main_remove_notifier_reference);                    /* lib_id.c */
  dune_region_callback_free_gizmomap_set(wm_gizmomap_remove); /* screen.c */
  dune_region_callback_refresh_tag_gizmomap_set(WM_gizmomap_tag_refresh);
  dune_library_callback_remap_editor_id_reference_set(
      wm_main_remap_editor_id_reference);                     /* lib_id.c */
  dune_spacedata_callback_id_remap_set(ED_spacedata_id_remap); /* screen.c */

}
