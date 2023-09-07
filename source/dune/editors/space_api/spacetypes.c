#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "types_scene.h"
#include "types_wm.h"

#include "dune_cxt.h"
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
#include "ed_phys.h"
#include "ed_render.h"
#include "ed_scene.h"
#include "ed_screen.h"
#include "ed_sculpt.h"
#include "ed_seq.h"
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
  ed_spacetype_btns();
  ed_spacetype_info();
  ed_spacetype_file();
  ed_spacetype_action();
  ed_spacetype_nla();
  ed_spacetype_script();
  ed_spacetype_text();
  ed_spacetype_seq();
  ed_spacetype_console();
  ed_spacetype_userpref();
  ed_spacetype_clip();
  ed_spacetype_statusbar();
  ed_spacetype_topbar();
  ed_spacetype_spreadsheet();

  /* Register operator types for screen and all spaces. */
  ed_optypes_userpref();
  ed_optypes_workspace();
  ed_optypes_scene();
  ed_optypes_screen();
  ee_optypes_anim();
  ed_optypes_animchannels();
  ed_optypes_asset();
  ed_optypes_pen();
  ed_optypes_object();
  ed_optypes_lattice();
  ed_optypes_mesh();
  ed_optypes_geometry();
  ed_optypes_sculpt();
  ed_optypes_sculpt_curves();
  ed_optypes_uvedit();
  ed_optypes_paint();
  ed_optypes_phys();
  ed_optypes_curve();
  ed_optypes_curves();
  ed_optypes_armature();
  ed_optypes_marker();
  ed_optypes_metaball();
  ed_optypes_sound();
  ed_optypes_render();
  ed_optypes_mask();
  ed_optypes_io();
  ed_optypes_edutils();

  ed_optypes_view2d();
  ed_optypes_ui();

  ed_screen_user_menu_register();

  ed_uilisttypes_ui();

  /* Gizmo types. */
  ed_gizmotypes_btn_2d();
  ed_gizmotypes_dial_3d();
  ed_gizmotypes_move_3d();
  ed_gizmotypes_arrow_3d();
  ed_gizmotypes_preselect_3d();
  ed_gizmotypes_primitive_3d();
  ed_gizmotypes_blank_3d();
  ed_gizmotypes_cage_2d();
  ed_gizmotypes_cage_3d();
  ed_gizmotypes_snap_3d();

  /* Register types for operators and gizmos. */
  const List *spacetypes = dune_spacetypes_list();
  LIST_FOREACH (const SpaceType *, type, spacetypes) {
    /* Initialize gizmo types first, operator types need them. */
    if (type->gizmos) {
      type->gizmos();
    }
    if (type->optypes) {
      type->optypes();
    }
  }
}

void ed_spacemacros_init(void)
{
  /* Macros must go last since they reference other operators.
   * They need to be registered after python operators too. */
  ed_opmacros_armature();
  ed_opmacros_mesh();
  ed_opmacros_uvedit();
  ed_opmacros_metaball();
  ed_opmacros_node();
  ed_opmacros_object();
  ed_opmacros_file();
  ed_opnacros_graph();
  ed_opmacros_action();
  ed_opmacros_clip();
  ed_opmacros_curve();
  ed_opmacros_mask();
  ed_opmacros_seq();
  ed_opmacros_paint();
  ed_opmacros_pen();

  /* Register dropboxes (can use macros). */
  ed_dropboxes_ui();
  const List *spacetypes = dune_spacetypes_list();
  LIST_FOREACH (const SpaceType *, type, spacetypes) {
    if (type->dropboxes) {
      type->dropboxes();
    }
  }
}

void ed_spacetypes_keymap(wmKeyConfig *keyconf)
{
  ed_keymap_screen(keyconf);
  ed_keymap_anim(keyconf);
  ed_keymap_animchannels(keyconf);
  ed_keymap_pen(keyconf);
  ed_keymap_object(keyconf);
  ed_keymap_lattice(keyconf);
  ed_keymap_mesh(keyconf);
  ed_keymap_uvedit(keyconf);
  ed_keymap_curve(keyconf);
  ed_keymap_armature(keyconf);
  ed_keymap_phys(keyconf);
  ed_keymap_metaball(keyconf);
  ed_keymap_paint(keyconf);
  ed_keymap_mask(keyconf);
  ed_keymap_marker(keyconf);

  ed_keymap_view2d(keyconf);
  ed_keymap_ui(keyconf);

  ed_keymap_transform(keyconf);

  const List *spacetypes = dune_spacetypes_list();
  LIST_FOREACH (const SpaceType *, type, spacetypes) {
    if (type->keymap) {
      type->keymap(keyconf);
    }
    LIST_FOREACH (ARegionType *, region_type, &type->regiontypes) {
      if (region_type->keymap) {
        region_type->keymap(keyconf);
      }
    }
  }
}

/* Custom Draw Call API ***************** */

typedef struct RegionDrawCB {
  struct RegionDrawCB *next, *prev;

  void (*draw)(const struct Cxt *, struct ARegion *, void *);
  void *customdata;

  int type;

} RegionDrawCB;

void *ed_region_draw_cb_activate(ARegionType *art,
                                 void (*draw)(const struct Cxt *, struct ARegion *, void *),
                                 void *customdata,
                                 int type)
{
  RegionDrawCB *rdc = mem_callocn(sizeof(RegionDrawCB), "RegionDrawCB");

  lib_addtail(&art->drawcalls, rdc);
  rdc->draw = draw;
  rdc->customdata = customdata;
  rdc->type = type;

  return rdc;
}

bool rd_region_draw_cb_exit(ARegionType *art, void *handle)
{
  LIST_FOREACH (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc == (RegionDrawCB *)handle) {
      lib_remlink(&art->drawcalls, rdc);
      mem_freem(rdc);
      return true;
    }
  }
  return false;
}

static void ed_region_draw_cb_draw(const Cxt *C, ARegion *region, ARegionType *art, int type)
{
  LIST_FOREACH_MUTABLE (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc->type == type) {
      rdc->draw(C, region, rdc->customdata);

      /* This is needed until we get rid of BGL which can change the states we are tracking. */
      gpu_bgl_end();
    }
  }
}

void ed_region_draw_cb_draw(const Ctx *C, ARegion *region, int type)
{
  ed_region_draw_cb_draw(C, region, region->type, type);
}

void ed_region_surface_draw_cb_draw(ARegionType *art, int type)
{
  ed_region_draw_cb_draw(NULL, NULL, art, type);
}

void ed_region_draw_cb_remove_by_type(ARegionType *art, void *draw_fn, void (*free)(void *))
{
  LIST_FOREACH_MUTABLE (RegionDrawCB *, rdc, &art->drawcalls) {
    if (rdc->draw == draw_fn) {
      if (free) {
        free(rdc->customdata);
      }
      lib_remlink(&art->drawcalls, rdc);
      mem_freen(rdc);
    }
  }
}

/* space template *********************** */
/* forward declare */
void ed_spacetype_xxx(void);

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

static void xxx_optypes(void)
{
  /* register operator types for this space */
}

static void xxx_keymap(wmKeyConfig *UNUSED(keyconf))
{
  /* add default items to keymap */
}

/* only called once, from screen/spacetypes.c */
void ed_spacetype_xxx(void)
{
  static SpaceType st;

  st.spaceid = SPACE_VIEW3D;

  st.create = xxx_create;
  st.free = xxx_free;
  st.init = xxx_init;
  st.duplicate = xxx_duplicate;
  st.operatortypes = xxx_optypes;
  st.keymap = xxx_keymap;

  dune_spacetype_register(&st);
}
