/* Manage init resources and correctly shutting down. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* only called once, for startup */
void win_init(Cxt *C, int argc, const char **argv)
{

  if (!G.background) {
    win_ghost_init(C); /* note: it assigns C to ghost! */
    win_init_cursor_data();
    //dune_sound_jack_sync_cb_set(sound_jack_sync_cb);
  }
  
  GHOST_CreateSystemPaths();
  
  dune_addon_pref_type_init();
  dune_keyconfig_pref_type_init();
  
  win_optype_init();
  win_optypes_register();
  
  win_pnltype_init(); /* Lookup table only. */
  win_menutype_init();
  win_uilisttype_init();
  win_gizmotype_init();
  win_gizmogrouptype_init();
  

  ed_undosys_type_init();

  dune_lib_cb_free_notifier_ref_set(
      win_main_remove_notifier_ref);                    /* lib_id.c */
  dune_rgn_cb_free_gizmomap_set(win_gizmomap_remove); /* screen.c */
  dune_rgn_cb_refresh_tag_gizmomap_set(win_gizmomap_tag_refresh);
  dune_lib_cb_remap_editor_id_ref_set(
      win_main_remap_editor_id_ref);                     /* lib_id.c */
  dune_spacedata_cb_id_remap_set(ed_spacedata_id_remap); /* screen.c */

}
