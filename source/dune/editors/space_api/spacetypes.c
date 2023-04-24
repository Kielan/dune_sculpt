#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "types_scene.h"
#include "types_windowmanager.h"

#include "dune_context.h"
#include "dune_screen.h"

#include "gpu_state.h"

#include "ui_interface.h"
#include "ui_view2d.h"

#include "ed_anim_api.h"
#include "ed_armature.h"
#include "ed_asset.h"
#include "ed_clip.h"
#include "ed_curve.h"
#include "ed_curves.h"
#include "ed_curves_sculpt.h"
#include "ed_fileselect.h"
#include "ed_geometry.h"
#include "ed_gizmo_lib.h"
#include "ed_pen.h"
#include "ed_lattice.h"
#include "ed_markers.h"
#include "ed_mask.h"
#include "ed_mball.h"
#include "ed_mesh.h"
#include "ed_node.h"
#include "ed_object.h"
#include "ed_paint.h"
#include "ed_physics.h"
#include "ed_render.h"
#include "ed_scene.h"
#include "ed_screen.h"
#include "ed_sculpt.h"
#include "ed_sequencer.h"
#include "ed_sound.h"
#include "ed_space_api.h"
#include "ed_transform.h"
#include "ed_userpref.h"
#include "ed_util.h"
#include "ed_uvedit.h"

#include "io_ops.h"

void ed_spacetypes_init(void)
{
  /* UI unit is a variable, may be used in some space type initialization. */
  U.widget_unit = 20;

  /* Create space types. */
  ed_spacetype_outliner();
  ed_spacetype_view3d();
  ed_spacetype_ipo();
  ed_spacetype_image();
  ed_spacetype_node();
  ed_spacetype_buttons();
  ed_spacetype_info();
  ed_spacetype_file();
  ed_spacetype_action();
  ed_spacetype_nla();
  ed_spacetype_script();
  ed_spacetype_text();
  ed_spacetype_sequencer();
  ed_spacetype_console();
  ed_spacetype_userpref();
  ed_spacetype_clip();
  ed_spacetype_statusbar();
  ed_spacetype_topbar();
  ed_spacetype_spreadsheet();

  /* Register operator types for screen and all spaces. */
  ed_operatortypes_userpref();
  ed_operatortypes_workspace();
  ed_operatypes_scene();
  ed_optypes_screen();
  ee_optypes_anim();
  ed_animchannels();
  ED_optypes_asset();
  ED_optypes_gpencil();
  ED_optypes_object();
  ED_optypes_lattice();
  ED_operatortypes_mesh();
  ED_operatortypes_geometry();
  ED_operatortypes_sculpt();
  ED_operatortypes_sculpt_curves();
  ED_operatortypes_uvedit();
  ED_operatortypes_paint();
  ED_operatortypes_physics();
  ED_operatortypes_curve();
  ED_operatortypes_curves();
  ED_operatortypes_armature();
  ED_operatortypes_marker();
  ED_operatortypes_metaball();
  ED_operatortypes_sound();
  ED_operatortypes_render();
  ED_operatortypes_mask();
  ED_operatortypes_io();
  ed_optypes_edutils();

  ed_optypes_view2d();
  ed_optypes_ui();

  ed_screen_user_menu_register();

  ed_uilisttypes_ui();

  /* Gizmo types. */
  ED_gizmotypes_button_2d();
  ED_gizmotypes_dial_3d();
  ED_gizmotypes_move_3d();
  ED_gizmotypes_arrow_3d();
  ED_gizmotypes_preselect_3d();
  ED_gizmotypes_primitive_3d();
  ED_gizmotypes_blank_3d();
  ED_gizmotypes_cage_2d();
  ED_gizmotypes_cage_3d();
  ED_gizmotypes_snap_3d();

  /* Register types for operators and gizmos. */
  const ListBase *spacetypes = dune_spacetypes_list();
  LISTBASE_FOREACH (const SpaceType *, type, spacetypes) {
    /* Initialize gizmo types first, operator types need them. */
    if (type->gizmos) {
      type->gizmos();
    }
    if (type->operatortypes) {
      type->operatortypes();
    }
  }
}

void ED_spacemacros_init(void)
{
  /* Macros must go last since they reference other operators.
   * They need to be registered after python operators too. */
  ED_opmacros_armature();
  ED_opmacros_mesh();
  ED_opmacros_uvedit();
  ED_opmacros_metaball();
  ED_opmacros_node();
  ED_opmacros_object();
  ED_opmacros_file();
  ED_opnacros_graph();
  ED_opmacros_action();
  ED_opmacros_clip();
  ED_opmacros_curve();
  ED_opmacros_mask();
  ED_operatormacros_sequencer();
  ED_operatormacros_paint();
  ED_operatormacros_gpencil();

  /* Register dropboxes (can use macros). */
  ED_dropboxes_ui();
  const ListBase *spacetypes = dune_spacetypes_list();
  LISTBASE_FOREACH (const SpaceType *, type, spacetypes) {
    if (type->dropboxes) {
      type->dropboxes();
    }
  }
}

void ED_spacetypes_keymap(wmKeyConfig *keyconf)
{
  ED_keymap_screen(keyconf);
  ED_keymap_anim(keyconf);
  ED_keymap_animchannels(keyconf);
  ED_keymap_gpencil(keyconf);
  ED_keymap_object(keyconf);
  ED_keymap_lattice(keyconf);
  ED_keymap_mesh(keyconf);
  ED_keymap_uvedit(keyconf);
  ED_keymap_curve(keyconf);
  ED_keymap_armature(keyconf);
  ED_keymap_physics(keyconf);
  ED_keymap_metaball(keyconf);
  ED_keymap_paint(keyconf);
  ED_keymap_mask(keyconf);
  ED_keymap_marker(keyconf);

  ED_keymap_view2d(keyconf);
  ED_keymap_ui(keyconf);

  ED_keymap_transform(keyconf);

  const ListBase *spacetypes = dune_spacetypes_list();
  LISTBASE_FOREACH (const SpaceType *, type, spacetypes) {
    if (type->keymap) {
      type->keymap(keyconf);
    }
    LISTBASE_FOREACH (ARegionType *, region_type, &type->regiontypes) {
      if (region_type->keymap) {
        region_type->keymap(keyconf);
      }
    }
  }
}

/* ********************** Custom Draw Call API ***************** */

typedef struct RegionDrawCB {
  struct RegionDrawCB *next, *prev;

  void (*draw)(const struct dContext *, struct ARegion *, void *);
  void *customdata;

  int type;

} RegionDrawCB;

void *ed_region_draw_cb_activate(ARegionType *art,
                                 void (*draw)(const struct dContext *, struct ARegion *, void *),
                                 void *customdata,
                                 int type)
{
  RegionDrawCB *rdc = MEM_callocN(sizeof(RegionDrawCB), "RegionDrawCB");

  lib_addtail(&art->drawcalls, rdc);
  rdc->draw = draw;
  rdc->customdata = customdata;
  rdc->type = type;

  return rdc;
}

bool ED_region_draw_cb_exit(ARegionType *art, void *handle)
{
  LISTBASE_FOREACH (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc == (RegionDrawCB *)handle) {
      lib_remlink(&art->drawcalls, rdc);
      MEM_freeN(rdc);
      return true;
    }
  }
  return false;
}

static void ed_region_draw_cb_draw(const dContext *C, ARegion *region, ARegionType *art, int type)
{
  LISTBASE_FOREACH_MUTABLE (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc->type == type) {
      rdc->draw(C, region, rdc->customdata);

      /* This is needed until we get rid of BGL which can change the states we are tracking. */
      GPU_bgl_end();
    }
  }
}

void ED_region_draw_cb_draw(const dContext *C, ARegion *region, int type)
{
  ed_region_draw_cb_draw(C, region, region->type, type);
}

void ED_region_surface_draw_cb_draw(ARegionType *art, int type)
{
  ed_region_draw_cb_draw(NULL, NULL, art, type);
}

void ED_region_draw_cb_remove_by_type(ARegionType *art, void *draw_fn, void (*free)(void *))
{
  LISTBASE_FOREACH_MUTABLE (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc->draw == draw_fn) {
      if (free) {
        free(rdc->customdata);
      }
      lib_remlink(&art->drawcalls, rdc);
      MEM_freeN(rdc);
    }
  }
}

/* ********************* space template *********************** */
/* forward declare */
void ED_spacetype_xxx(void);

/* allocate and init some vars */
static SpaceLink *xxx_create(const ScrArea *UNUSED(area), const Scene *UNUSED(scene))
{
  return NULL;
}

/* not spacelink itself */
static void xxx_free(SpaceLink *UNUSED(sl))
{
}

/* spacetype; init callback for usage, should be re-doable. */
static void xxx_init(wmWindowManager *UNUSED(wm), ScrArea *UNUSED(area))
{

  /* link area to SpaceXXX struct */

  /* define how many regions, the order and types */

  /* add types to regions */
}

static SpaceLink *xxx_duplicate(SpaceLink *UNUSED(sl))
{

  return NULL;
}

static void xxx_operatortypes(void)
{
  /* register operator types for this space */
}

static void xxx_keymap(wmKeyConfig *UNUSED(keyconf))
{
  /* add default items to keymap */
}

/* only called once, from screen/spacetypes.c */
void ED_spacetype_xxx(void)
{
  static SpaceType st;

  st.spaceid = SPACE_VIEW3D;

  st.create = xxx_create;
  st.free = xxx_free;
  st.init = xxx_init;
  st.duplicate = xxx_duplicate;
  st.operatortypes = xxx_operatortypes;
  st.keymap = xxx_keymap;

  dune_spacetype_register(&st);
}
