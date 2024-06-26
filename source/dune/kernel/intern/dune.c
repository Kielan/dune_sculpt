/**
 * Application level startup/shutdown functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_listbase.h"
#include "LIB_string.h"
#include "LIB_utildefines.h"

#include "IMB_imbuf.h"
#include "IMB_moviecache.h"

#include "KERNEL_addon.h"
#include "KERNEL_dune.h" /* own include */
#include "KERNEL_dune_user_menu.h"
#include "KERNEL_dune_version.h" /* own include */
#include "KERNEL_dunefile.h"
#include "KERNEL_brush.h"
#include "KERNEL_cachefile.h"
#include "KERNEL_callbacks.h"
#include "KERNEL_global.h"
#include "KERNEL_idprop.h"
#include "KERNEL_image.h"
#include "KERNEL_layer.h"
#include "KERNEL_main.h"
#include "KERNEL_node.h"
#include "KERNEL_report.h"
#include "KERNEL_scene.h"
#include "KERNEL_screen.h"
#include "KERNEL_studiolight.h"

#include "DEG_depsgraph.h"

#include "RE_pipeline.h"
#include "RE_texture.h"

#include "SEQ_sequencer.h"

#include "BLF_api.h"

Global G;
UserDef U;

/* -------------------------------------------------------------------- */
/* Dune Free on Exit */

void KERNEL_dune_free(void)
{
  /* samples are in a global list..., also sets G_MAIN->sound->sample NULL */

  /* Needs to run before main free as wm is still referenced for icons preview jobs. */
  KERNEL_studiolight_free();

  KERNEL_main_free(G_MAIN);
  G_MAIN = NULL;

  if (G.log.file != NULL) {
    fclose(G.log.file);
  }

  KERNEL_spacetypes_free(); /* after free main, it uses space callbacks */

  IMB_exit();
  KERNEL_cachefiles_exit();
  DEG_free_node_types();

  KERNEL_brush_system_exit();
  RE_texture_rng_exit();

  KERNEL_callback_global_finalize();

  IMB_moviecache_destruct();

  KERNEL_node_system_exit();
}

/* -------------------------------------------------------------------- */
/** Dune Version Access */

static char dune_version_string[48] = "";

static void dune_version_init(void)
{
  const char *version_cycle = "";
  if (STREQ(STRINGIFY(DUNR_VERSION_CYCLE), "alpha")) {
    version_cycle = " Alpha";
  }
  else if (STREQ(STRINGIFY(DUNE_VERSION_CYCLE), "beta")) {
    version_cycle = " Beta";
  }
  else if (STREQ(STRINGIFY(DUNE_VERSION_CYCLE), "rc")) {
    version_cycle = " Release Candidate";
  }
  else if (STREQ(STRINGIFY(DUNE_VERSION_CYCLE), "release")) {
    version_cycle = "";
  }
  else {
    LIB_assert_msg(0, "Invalid Dune version cycle");
  }

  LIB_snprintf(dune_version_string,
               ARRAY_SIZE(dune_version_string),
               "%d.%01d.%d%s",
               DUNE_VERSION / 100,
               DUNE_VERSION % 100,
               DUNE_VERSION_PATCH,
               version_cycle);
}

const char *KERNEL_dune_version_string(void)
{
  return dune_version_string;
}

bool KERNEL_dune_version_is_alpha(void)
{
  bool is_alpha = STREQ(STRINGIFY(DUNE_VERSION_CYCLE), "alpha");
  return is_alpha;
}

/* -------------------------------------------------------------------- */
/** Dune #Global Initialize/Clear */

void KERNEL_dune_globals_init(void)
{
  dune_version_init();

  memset(&G, 0, sizeof(Global));

  U.savetime = 1;

  G_MAIN = KERNEL_main_new();

  strcpy(G.ima, "//");

#ifndef WITH_PYTHON_SECURITY /* default */
  G.f |= G_FLAG_SCRIPT_AUTOEXEC;
#else
  G.f &= ~G_FLAG_SCRIPT_AUTOEXEC;
#endif

  G.log.level = 1;
}

void KERNEL_dune_globals_clear(void)
{
  KERNEL_main_free(G_MAIN); /* free all lib data */

  G_MAIN = NULL;
}

/* -------------------------------------------------------------------- */
/**  Dune Preferences */

static void keymap_item_free(wmKeyMapItem *kmi)
{
  if (kmi->properties) {
    IDP_FreeProperty(kmi->properties);
  }
  if (kmi->ptr) {
    MEM_freeN(kmi->ptr);
  }
}

void KERNEL_dune_userdef_data_swap(UserDef *userdef_a, UserDef *userdef_b)
{
  SWAP(UserDef, *userdef_a, *userdef_b);
}

void KERNEL_dune_userdef_data_set(UserDef *userdef)
{
  KERNEL_dune_userdef_data_swap(&U, userdef);
  KERNEL_dune_userdef_data_free(userdef, true);
}

void KERNEL_dune_userdef_data_set_and_free(UserDef *userdef)
{
  KERNEL_dune_userdef_data_set(userdef);
  MEM_freeN(userdef);
}

static void userdef_free_keymaps(UserDef *userdef)
{
  for (wmKeyMap *km = userdef->user_keymaps.first, *km_next; km; km = km_next) {
    km_next = km->next;
    LISTBASE_FOREACH (wmKeyMapDiffItem *, kmdi, &km->diff_items) {
      if (kmdi->add_item) {
        keymap_item_free(kmdi->add_item);
        MEM_freeN(kmdi->add_item);
      }
      if (kmdi->remove_item) {
        keymap_item_free(kmdi->remove_item);
        MEM_freeN(kmdi->remove_item);
      }
    }

    LISTBASE_FOREACH (wmKeyMapItem *, kmi, &km->items) {
      keymap_item_free(kmi);
    }

    LIB_freelistN(&km->diff_items);
    LIB_freelistN(&km->items);

    MEM_freeN(km);
  }
  LIB_listbase_clear(&userdef->user_keymaps);
}

static void userdef_free_keyconfig_prefs(UserDef *userdef)
{
  for (wmKeyConfigPref *kpt = userdef->user_keyconfig_prefs.first, *kpt_next; kpt;
       kpt = kpt_next) {
    kpt_next = kpt->next;
    IDP_FreeProperty(kpt->prop);
    MEM_freeN(kpt);
  }
  LIB_listbase_clear(&userdef->user_keyconfig_prefs);
}

static void userdef_free_user_menus(UserDef *userdef)
{
  for (bUserMenu *um = userdef->user_menus.first, *um_next; um; um = um_next) {
    um_next = um->next;
    KERNEL_dune_user_menu_item_free_list(&um->items);
    MEM_freeN(um);
  }
}

static void userdef_free_addons(UserDef *userdef)
{
  for (bAddon *addon = userdef->addons.first, *addon_next; addon; addon = addon_next) {
    addon_next = addon->next;
    KERNEL_addon_free(addon);
  }
  LIB_listbase_clear(&userdef->addons);
}

void KERNEL_dune_userdef_data_free(UserDef *userdef, bool clear_fonts)
{
#define U LIB_STATIC_ASSERT(false, "Global 'U' not allowed, only use arguments passed in!")
#ifdef U /* quiet warning */
#endif

  userdef_free_keymaps(userdef);
  userdef_free_keyconfig_prefs(userdef);
  userdef_free_user_menus(userdef);
  userdef_free_addons(userdef);

  if (clear_fonts) {
    LISTBASE_FOREACH (uiFont *, font, &userdef->uifonts) {
      BLF_unload_id(font->blf_id);
    }
    BLF_default_set(-1);
  }

  LIB_freelistN(&userdef->autoexec_paths);
  LIB_freelistN(&userdef->asset_libraries);

  LIB_freelistN(&userdef->uistyles);
  LIB_freelistN(&userdef->uifonts);
  LIB_freelistN(&userdef->themes);

#undef U
}

/* -------------------------------------------------------------------- */
/** DUNE Preferences (Application Templates) */

void KERNEL_dune_userdef_app_template_data_swap(UserDef *userdef_a, UserDef *userdef_b)
{
  /* TODO:
   * - various minor settings (add as needed).
   */

#define DATA_SWAP(id) \
  { \
    UserDef userdef_tmp; \
    memcpy(&(userdef_tmp.id), &(userdef_a->id), sizeof(userdef_tmp.id)); \
    memcpy(&(userdef_a->id), &(userdef_b->id), sizeof(userdef_tmp.id)); \
    memcpy(&(userdef_b->id), &(userdef_tmp.id), sizeof(userdef_tmp.id)); \
  } \
  ((void)0)

#define LIST_SWAP(id) \
  { \
    SWAP(ListBase, userdef_a->id, userdef_b->id); \
  } \
  ((void)0)

#define FLAG_SWAP(id, ty, flags) \
  { \
    CHECK_TYPE(&(userdef_a->id), ty *); \
    const ty f = flags; \
    const ty a = userdef_a->id; \
    const ty b = userdef_b->id; \
    userdef_a->id = (userdef_a->id & ~f) | (b & f); \
    userdef_b->id = (userdef_b->id & ~f) | (a & f); \
  } \
  ((void)0)

  LIST_SWAP(uistyles);
  LIST_SWAP(uifonts);
  LIST_SWAP(themes);
  LIST_SWAP(addons);
  LIST_SWAP(user_keymaps);
  LIST_SWAP(user_keyconfig_prefs);

  DATA_SWAP(font_path_ui);
  DATA_SWAP(font_path_ui_mono);
  DATA_SWAP(keyconfigstr);

  DATA_SWAP(gizmo_flag);
  DATA_SWAP(app_flag);

  /* We could add others. */
  FLAG_SWAP(uiflag, int, USER_SAVE_PROMPT | USER_SPLASH_DISABLE | USER_SHOW_GIZMO_NAVIGATE);

  DATA_SWAP(ui_scale);

#undef SWAP_TYPELESS
#undef DATA_SWAP
#undef LIST_SWAP
#undef FLAG_SWAP
}

void KERNEL_dune_userdef_app_template_data_set(UserDef *userdef)
{
  KERNEL_dune_userdef_app_template_data_swap(&U, userdef);
  KERNEL_dune_userdef_data_free(userdef, true);
}

void KERNEL_blender_userdef_app_template_data_set_and_free(UserDef *userdef)
{
  KERNEL_dune_userdef_app_template_data_set(userdef);
  MEM_freeN(userdef);
}

/* -------------------------------------------------------------------- */
/** DUNE's AtExit
 *
 * Don't use MEM_mallocN so functions can be registered at any time.
 */

static struct AtExitData {
  struct AtExitData *next;

  void (*func)(void *user_data);
  void *user_data;
} *g_atexit = NULL;

void KERNEL_dune_atexit_register(void (*func)(void *user_data), void *user_data)
{
  struct AtExitData *ae = malloc(sizeof(*ae));
  ae->next = g_atexit;
  ae->func = func;
  ae->user_data = user_data;
  g_atexit = ae;
}

void KERNEL_dune_atexit_unregister(void (*func)(void *user_data), const void *user_data)
{
  struct AtExitData *ae = g_atexit;
  struct AtExitData **ae_p = &g_atexit;

  while (ae) {
    if ((ae->func == func) && (ae->user_data == user_data)) {
      *ae_p = ae->next;
      free(ae);
      return;
    }
    ae_p = &ae->next;
    ae = ae->next;
  }
}

void KERNEL_dune_atexit(void)
{
  struct AtExitData *ae = g_atexit, *ae_next;
  while (ae) {
    ae_next = ae->next;

    ae->func(ae->user_data);

    free(ae);
    ae = ae_next;
  }
  g_atexit = NULL;
}
