#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_fileops.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "i18n_translation.h"

#include "dune_context.h"
#include "dune_main.h"
#include "dune_report.h"
#include "dune_screen.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_screen.h"
#include "ed_undo.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "buttons_intern.h" /* own include */

/* -------------------------------------------------------------------- */
/** Start / Clear Search Filter Operators
 *
 *  Almost a duplicate of the file browser operator #FILE_OT_start_filter.
 **/

static int btns_start_filter_exec(Ctx *C, wmOp *UNUSED(op))
{
  SpaceProps *space = ctx_wm_space_props(C);
  ScrArea *area = ctx_wm_area(C);
  ARegion *region = dune_area_find_region_type(area, RGN_TYPE_HEADER);

  ARegion *region_ctx = ctx_wm_region(C);
  ctx_wm_region_set(C, region);
  ui_textbtn_activate_api(C, region, space, "search_filter");
  CTX_wm_region_set(C, region_ctx);

  return OPERATOR_FINISHED;
}

void BUTTONS_OT_start_filter(struct wmOperatorType *ot)
{
  /* Identifiers. */
  ot->name = "Filter";
  ot->description = "Start entering filter text";
  ot->idname = "BUTTONS_OT_start_filter";

  /* Callbacks. */
  ot->exec = buttons_start_filter_exec;
  ot->poll = ED_operator_buttons_active;
}

static int buttons_clear_filter_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceProperties *space = CTX_wm_space_properties(C);

  space->runtime->search_string[0] = '\0';

  ScrArea *area = ctx_wm_area(C);
  ED_region_search_filter_update(area, ctx_wm_region(C));
  ED_area_tag_redraw(area);

  return OPERATOR_FINISHED;
}

void btns_ot_clear_filter(struct wmOpType *ot)
{
  /* Identifiers. */
  ot->name = "Clear Filter";
  ot->description = "Clear the search filter";
  ot->idname = "btns_ot_clear_filter";

  /* Callbacks. */
  ot->exec = btns_clear_filter_ex;
  ot->poll = ed_op_btns_active;
}

/* -------------------------------------------------------------------- */
/** \name Pin ID Operator **/

static int toggle_pin_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceProperties *sbuts = CTX_wm_space_properties(C);

  sbuts->flag ^= SB_PIN_CONTEXT;

  /* Create the properties space pointer. */
  ApiPtr sbuts_ptr;
  bScreen *screen = ctx_wm_screen(C);
  api_pointer_create(&screen->id, &ApiSpaceProps, sbuts, &sbuts_ptr);

  /* Create the new id pointer and set the pin id with api
   * so we can use the property's RNA update functionality. */
  Id *new_id = (sbuts->flag & SB_PIN_CONTEXT) ? btns_ctx_id_path(C) : NULL;
  ApiPtr new_id_ptr;
  api_id_ptr_create(new_id, &new_id_ptr);
  api_ptr_set(&sbuts_ptr, "pin_id", new_id_ptr);

  ed_area_tag_redraw(ctx_wm_area(C));

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

/* -------------------------------------------------------------------- */
/** Context Menu Operator **/

static int context_menu_invoke(Ctx *C, wmOpType *ot) {
  uiPopupMenu *pup = UI_popup_menu_begin(C, IFACE_("Context Menu"), ICON_NONE);
  uiLayout *layout = UI_popup_menu_layout(pup);

  uiItemM(layout, "INFO_MT_area", NULL, ICON_NONE);
  UI_popup_menu_end(C, pup);

  return OPERATOR_INTERFACE;
}

void BUTTONS_OT_context_menu(wmOperatorType *ot)
{
  /* Identifiers. */
  ot->name = "Context Menu";
  ot->description = "Display properties editor context_menu";
  ot->idname = "BUTTONS_OT_context_menu";

  /* Callbacks. */
  ot->invoke = context_menu_invoke;
  ot->poll = ED_operator_buttons_active;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name File Browse Operator
 * \{ */

typedef struct FileBrowseOp {
  PointerRNA ptr;
  PropertyRNA *prop;
  bool is_undo;
  bool is_userdef;
} FileBrowseOp;

static int file_browse_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  FileBrowseOp *fbo = op->customdata;
  ID *id;
  char *str;
  int str_len;
  const char *path_prop = RNA_struct_find_property(op->ptr, "directory") ? "directory" :
                                                                           "filepath";

  if (RNA_struct_property_is_set(op->ptr, path_prop) == 0 || fbo == NULL) {
    return OPERATOR_CANCELLED;
  }

  str = RNA_string_get_alloc(op->ptr, path_prop, NULL, 0, &str_len);

  /* Add slash for directories, important for some properties. */
  if (RNA_property_subtype(fbo->prop) == PROP_DIRPATH) {
    char path[FILE_MAX];
    const bool is_relative = RNA_boolean_get(op->ptr, "relative_path");
    id = fbo->ptr.owner_id;

    BLI_strncpy(path, str, FILE_MAX);
    BLI_path_abs(path, id ? ID_BLEND_PATH(bmain, id) : BKE_main_blendfile_path(bmain));

    if (BLI_is_dir(path)) {
      /* Do this first so '//' isn't converted to '//\' on windows. */
      BLI_path_slash_ensure(path);
      if (is_relative) {
        const int path_len = BLI_strncpy_rlen(path, str, FILE_MAX);
        BLI_path_rel(path, BKE_main_blendfile_path(bmain));
        str = MEM_reallocN(str, path_len + 2);
        BLI_strncpy(str, path, FILE_MAX);
      }
      else {
        str = MEM_reallocN(str, str_len + 2);
      }
    }
    else {
      char *const lslash = (char *)BLI_path_slash_rfind(str);
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
    wmOperator *redo_op = WM_operator_last_redo(C);
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

static void file_browse_cancel(Ctx *UNUSED(C), wmOp *op)
{
  mem_freen(op->customdata);
  op->customdata = NULL;
}

static int file_browse_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ApiPtr ptr;
  ApiProp *prop;
  bool is_undo;
  bool is_userdef;
  FileBrowseOp *fbo;
  char *str;

  if (ctx_wm_space_file(C)) {
    dune_report(op->reports, RPT_ERROR, "Cannot activate a file selector, one already open");
    return OP_CANCELLED;
  }

  UI_context_active_but_prop_get_filebrowser(C, &ptr, &prop, &is_undo, &is_userdef);

  if (!prop) {
    return OPERATOR_CANCELLED;
  }

  str = RNA_property_string_get_alloc(&ptr, prop, NULL, 0, NULL);

  /* Useful yet irritating feature, Shift+Click to open the file
   * Alt+Click to browse a folder in the OS's browser. */
  if (event->modifier & (KM_SHIFT | KM_ALT)) {
    wmOperatorType *ot = WM_operatortype_find("WM_OT_path_open", true);
    PointerRNA props_ptr;

    if (event->modifier & KM_ALT) {
      char *lslash = (char *)BLI_path_slash_rfind(str);
      if (lslash) {
        *lslash = '\0';
      }
    }

    WM_operator_properties_create_ptr(&props_ptr, ot);
    RNA_string_set(&props_ptr, "filepath", str);
    WM_operator_name_call_ptr(C, ot, WM_OP_EXEC_DEFAULT, &props_ptr, NULL);
    WM_operator_properties_free(&props_ptr);

    MEM_freeN(str);
    return OPERATOR_CANCELLED;
  }

  PropertyRNA *prop_relpath;
  const char *path_prop = RNA_struct_find_property(op->ptr, "directory") ? "directory" :
                                                                           "filepath";
  fbo = MEM_callocN(sizeof(FileBrowseOp), "FileBrowseOp");
  fbo->ptr = ptr;
  fbo->prop = prop;
  fbo->is_undo = is_undo;
  fbo->is_userdef = is_userdef;
  op->customdata = fbo;

  /* Normally ED_fileselect_get_params would handle this but we need to because of stupid
   * user-prefs exception. - campbell */
  if ((prop_relpath = RNA_struct_find_property(op->ptr, "relative_path"))) {
    if (!RNA_property_is_set(op->ptr, prop_relpath)) {
      bool is_relative = (U.flag & USER_RELPATHS) != 0;

      /* While we want to follow the defaults,
       * we better not switch existing paths relative/absolute state. */
      if (str[0]) {
        is_relative = BLI_path_is_rel(str);
      }

      if (UNLIKELY(ptr.data == &U || is_userdef)) {
        is_relative = false;
      }

      /* Annoying exception!, if we're dealing with the user prefs, default relative to be off. */
      RNA_property_boolean_set(op->ptr, prop_relpath, is_relative);
    }
  }

  RNA_string_set(op->ptr, path_prop, str);
  MEM_freeN(str);

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

void BUTTONS_OT_file_browse(wmOperatorType *ot)
{
  /* Identifiers. */
  ot->name = "Accept";
  ot->description =
      "Open a file browser, hold Shift to open the file, Alt to browse containing directory";
  ot->idname = "BUTTONS_OT_file_browse";

  /* Callbacks. */
  ot->invoke = file_browse_invoke;
  ot->exec = file_browse_exec;
  ot->cancel = file_browse_cancel;

  /* Conditional undo based on button flag. */
  ot->flag = 0;

  /* Properties. */
  WM_operator_properties_filesel(ot,
                                 0,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
}

void BUTTONS_OT_directory_browse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Accept";
  ot->description =
      "Open a directory browser, hold Shift to open the file, Alt to browse containing directory";
  ot->idname = "BUTTONS_OT_directory_browse";

  /* api callbacks */
  ot->invoke = file_browse_invoke;
  ot->exec = file_browse_exec;
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
