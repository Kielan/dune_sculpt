#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_fileops.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_cxt.h"
#include "dune_main.h"
#include "dune_report.h"
#include "dune_screen.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_screen.h"
#include "ed_undo.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "btns_intern.h" /* own include */

/* Start / Clear Search Filter Operators
 *  Almost a duplicate of the file browser op file_ot_start_filter **/

static int btns_start_filter_ex(Cxt *C, wmOp *UNUSED(op))
{
  SpaceProps *space = cxt_wm_space_props(C);
  ScrArea *area = cxt_wm_area(C);
  ARegion *region = dune_area_find_region_type(area, RGN_TYPE_HEADER);

  ARegion *region_cxt = cxt_wm_region(C);
  cxt_wm_region_set(C, region);
  ui_textbtn_activate_api(C, region, space, "search_filter");
  cxt_wm_region_set(C, region_ctx);

  return OP_FINISHED;
}

void btns_ot_start_filter(struct wmOpType *ot)
{
  /* Ids. */
  ot->name = "Filter";
  ot->description = "Start entering filter text";
  ot->idname = "btns_ot_start_filter";

  /* Callbacks. */
  ot->exec = btns_start_filter_ex;
  ot->poll = ed_op_btns_active;
}

static int btns_clear_filter_ex(Cxt *C, wmOp *UNUSED(op))
{
  SpaceProps *space = cxt_wm_space_props(C);

  space->runtime->search_string[0] = '\0';

  ScrArea *area = cxt_wm_area(C);
  ed_region_search_filter_update(area, ctx_wm_region(C));
  ed_area_tag_redraw(area);

  return OP_FINISHED;
}

void btns_ot_clear_filter(struct wmOpType *ot)
{
  /* Identifiers. */
  ot->name = "Clear Filter";
  ot->description = "Clear the search filter";
  ot->idname = "btns_ot_clear_filter";

  /* Callbacks. */
  ot->ex = btns_clear_filter_ex;
  ot->poll = ed_op_btns_active;
}

/* Pin id Operator */

static int toggle_pin_ex(Cxt *C, wmOp *UNUSED(op))
{
  SpaceProps *sbtns = cxt_wm_space_props(C);

  sbuts->flag ^= SB_PIN_CXT;

  /* Create the props space pointer. */
  ApiPtr sbtns_ptr;
  Screen *screen = cxt_wm_screen(C);
  api_ptr_create(&screen->id, &ApiSpaceProps, sbtns, &sbtns_ptr);

  /* Create the new id pointer and set the pin id with api
   * so we can use the property's api update functionality. */
  Id *new_id = (sbuts->flag & SB_PIN_CXT) ? btns_cxt_id_path(C) : NULL;
  ApiPtr new_id_ptr;
  api_id_ptr_create(new_id, &new_id_ptr);
  api_ptr_set(&sbtns_ptr, "pin_id", new_id_ptr);

  ed_area_tag_redraw(cxt_wm_area(C));

  return OP_FINISHED;
}

void btns_ot_toggle_pin(wmOpType *ot)
{
  /* Identifiers. */
  ot->name = "Toggle Pin ID";
  ot->description = "Keep the current data-block displayed";
  ot->idname = "btns_ot_toggle_pin";

  /* Callbacks. */
  ot->exec = toggle_pin_ex;
  ot->poll = ed_op_btns_active;
}

/* Context Menu Operator */
static int cxt_menu_invoke(Cxt *C, wmOpType *ot) {
  uiPopupMenu *pup = ui_popup_menu_begin(C, IFACE_("Cxt Menu"), ICON_NONE);
  uiLayout *layout = ui_popup_menu_layout(pup);

  uiItemM(layout, "INFO_MT_area", NULL, ICON_NONE);
  ui_popup_menu_end(C, pup);

  return OP_INTERFACE;
}

void btns_ot_ctx_menu(wmOpType *ot)
{
  /* Identifiers. */
  ot->name = "Context Menu";
  ot->description = "Display props editor cxt_menu";
  ot->idname = "btns_ot_cxt_menu";

  /* Callbacks. */
  ot->invoke = cxt_menu_invoke;
  ot->poll = ed_op_btns_active;
}

/* -------------------------------------------------------------------- */
/* File Browse Operator */

typedef struct FileBrowseOp {
  ApiPtr ptr;
  ApiProp *prop;
  bool is_undo;
  bool is_userdef;
} FileBrowseOp;

static int file_browse_ex(Cxt *C, wmOp *op)
{
  Main *main = ctx_data_main(C);
  FileBrowseOp *fbo = op->customdata;
  Id *id;
  char *str;
  int str_len;
  const char *path_prop = api_struct_find_prop(op->ptr, "directory") ? "directory" :
                                                                           "filepath";

  if (api_struct_prop_is_set(op->ptr, path_prop) == 0 || fbo == NULL) {
    return OP_CANCELLED;
  }

  str = api_string_get_alloc(op->ptr, path_prop, NULL, 0, &str_len);

  /* Add slash for directories, important for some properties. */
  if (api_prop_subtype(fbo->prop) == PROP_DIRPATH) {
    char path[FILE_MAX];
    const bool is_relative = api_bool_get(op->ptr, "relative_path");
    id = fbo->ptr.owner_id;

    lib_strncpy(path, str, FILE_MAX);
    lib_path_abs(path, id ? ID_DUNE_PATH(bmain, id) : dune_main_dunefile_path(main));

    if (lib_is_dir(path)) {
      /* Do this first so '//' isn't converted to '//\' on windows. */
      lib_path_slash_ensure(path);
      if (is_relative) {
        const int path_len = lib_strncpy_rlen(path, str, FILE_MAX);
        lib_path_rel(path, dune_main_blendfile_path(bmain));
        str = mem_reallocn(str, path_len + 2);
        lib_strncpy(str, path, FILE_MAX);
      }
      else {
        str = mem_reallocn(str, str_len + 2);
      }
    }
    else {
      char *const lslash = (char *)lib_path_slash_rfind(str);
      if (lslash) {
        lslash[1] = '\0';
      }
    }
  }

  api_prop_string_set(&fbo->ptr, fbo->prop, str);
  api_prop_update(C, &fbo->ptr, fbo->prop);
  mem_freen(str);

  if (fbo->is_undo) {
    const char *undostr = api_prop_id(fbo->prop);
    ed_undo_push(C, undostr);
  }

  /* Special annoying exception, filesel on redo panel T26618. */
  {
    wmOp *redo_op = wm_op_last_redo(C);
    if (redo_op) {
      if (fbo->ptr.data == redo_op->ptr->data) {
        ed_undo_op_repeat(C, redo_op);
      }
    }
  }

  /* Tag user preferences as dirty. */
  if (fbo->is_userdef) {
    U.runtime.is_dirty = true;
  }

  mem_freen(op->customdata);

  return OP_FINISHED;
}

static void file_browse_cancel(Cxt *UNUSED(C), wmOp *op)
{
  mem_freen(op->customdata);
  op->customdata = NULL;
}

static int file_browse_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  ApiPtr ptr;
  ApiProp *prop;
  bool is_undo;
  bool is_userdef;
  FileBrowseOp *fbo;
  char *str;

  if (cxt_wm_space_file(C)) {
    dune_report(op->reports, RPT_ERROR, "Cannot activate a file selector, one already open");
    return OP_CANCELLED;
  }

  ui_ctx_active_btn_prop_get_filebrowser(C, &ptr, &prop, &is_undo, &is_userdef);

  if (!prop) {
    return OP_CANCELLED;
  }

  str = api_prop_string_get_alloc(&ptr, prop, NULL, 0, NULL);

  /* Useful yet irritating feature, Shift+Click to open the file
   * Alt+Click to browse a folder in the OS's browser. */
  if (event->mod & (KM_SHIFT | KM_ALT)) {
    wmOpType *ot = wm_optype_find("wm_ot_path_open", true);
    ApiPtr props_ptr;

    if (event->mod & KM_ALT) {
      char *lslash = (char *)lib_path_slash_rfind(str);
      if (lslash) {
        *lslash = '\0';
      }
    }

    wm_op_props_create_ptr(&props_ptr, ot);
    api_string_set(&props_ptr, "filepath", str);
    wm_op_name_call_ptr(C, ot, WM_OP_EX_DEFAULT, &props_ptr, NULL);
    wm_op_props_free(&props_ptr);

    mem_freen(str);
    return OP_CANCELLED;
  }

  ApiProp *prop_relpath;
  const char *path_prop = api_struct_find_prop(op->ptr, "directory") ? "directory" :
                                                                           "filepath";
  fbo = mem_callocn(sizeof(FileBrowseOp), "FileBrowseOp");
  fbo->ptr = ptr;
  fbo->prop = prop;
  fbo->is_undo = is_undo;
  fbo->is_userdef = is_userdef;
  op->customdata = fbo;

  /* Normally ed_fileselect_get_params would handle this but we need to because of stupid
   * user-prefs exception. */
  if ((prop_relpath = api_struct_find_prop(op->ptr, "relative_path"))) {
    if (!api_prop_is_set(op->ptr, prop_relpath)) {
      bool is_relative = (U.flag & USER_RELPATHS) != 0;

      /* While we want to follow the defaults,
       * we better not switch existing paths relative/absolute state. */
      if (str[0]) {
        is_relative = lib_path_is_rel(str);
      }

      if (UNLIKELY(ptr.data == &U || is_userdef)) {
        is_relative = false;
      }

      /* Annoying exception!, if we're dealing with the user prefs, default relative to be off. */
      api_prop_bool_set(op->ptr, prop_relpath, is_relative);
    }
  }

  api_string_set(op->ptr, path_prop, str);
  mem_freen(str);

  wm_event_add_fileselect(C, op);

  return OP_RUNNING_MODAL;
}

void btns_ot_file_browse(wmOpType *ot)
{
  /* Identifiers. */
  ot->name = "Accept";
  ot->description =
      "Open a file browser, hold Shift to open the file, Alt to browse containing directory";
  ot->idname = "btns_ot_file_browse";

  /* Callbacks. */
  ot->invoke = file_browse_invoke;
  ot->ex = file_browse_ex;
  ot->cancel = file_browse_cancel;

  /* Conditional undo based on button flag. */
  ot->flag = 0;

  /* Properties. */
  wm_op_props_filesel(ot,
                      0,
                      FILE_SPECIAL,
                      FILE_OPENFILE,
                      WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH,
                      FILE_DEFAULTDISPLAY,
                      FILE_SORT_DEFAULT);
}

void btns_ot_directory_browse(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Accept";
  ot->description =
      "Open a directory browser, hold Shift to open the file, Alt to browse containing directory";
  ot->idname = "btns_it_directory_browse";

  /* api callbacks */
  ot->invoke = file_browse_invoke;
  ot->ex = file_browse_ex;
  ot->cancel = file_browse_cancel;

  /* conditional undo based on button flag */
  ot->flag = 0;

  /* properties */
  wm_op_props_filesel(ot,
                      0,
                      FILE_SPECIAL,
                      FILE_OPENFILE,
                      WM_FILESEL_DIRECTORY | WM_FILESEL_RELPATH,
                      FILE_DEFAULTDISPLAY,
                      FILE_SORT_DEFAULT);
}
