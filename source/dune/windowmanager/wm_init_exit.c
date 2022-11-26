/*
 * Manage initializing resources and correctly shutting down.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* only called once, for startup */
void WM_init(bContext *C, int argc, const char **argv)
{

  if (!G.background) {
    wm_ghost_init(C); /* note: it assigns C to ghost! */
    wm_init_cursor_data();
    //KERNEL_sound_jack_sync_callback_set(sound_jack_sync_callback);
  }
  
  GHOST_CreateSystemPaths();
  
  KERNEL_addon_pref_type_init();
  KERNEL_keyconfig_pref_type_init();
  
  wm_operatortype_init();
  wm_operatortypes_register();
  
  WM_paneltype_init(); /* Lookup table only. */
  WM_menutype_init();
  WM_uilisttype_init();
  wm_gizmotype_init();
  wm_gizmogrouptype_init();
  

  ED_undosys_type_init();

  KERNEL_library_callback_free_notifier_reference_set(
      WM_main_remove_notifier_reference);                    /* lib_id.c */
  KERNEL_region_callback_free_gizmomap_set(wm_gizmomap_remove); /* screen.c */
  KERNEL_region_callback_refresh_tag_gizmomap_set(WM_gizmomap_tag_refresh);
  KERNEL_library_callback_remap_editor_id_reference_set(
      WM_main_remap_editor_id_reference);                     /* lib_id.c */
  KERNEL_spacedata_callback_id_remap_set(ED_spacedata_id_remap); /* screen.c */

}
