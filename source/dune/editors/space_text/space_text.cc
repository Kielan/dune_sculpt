#include <cstring>

#include "types_text.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"

#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_screen.h"

#include "ed_screen.hh"
#include "ed_space_api.hh"

#include "wm_api.hh"
#include "wm_types.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "loader_read_write.hh"

#include "api_access.hh"
#include "api_path.hh"

#include "text_format.hh"
#include "text_intern.hh" /* own include */

/* default cbs for text space */

static SpaceLink *text_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARegion *region;
  SpaceText *stext;

  stext = static_cast<SpaceText *>(mem_callocn(sizeof(SpaceText), "inittext"));
  stext->spacetype = SPACE_TEXT;

  stext->lheight = 12;
  stext->tabnumber = 4;
  stext->margin_column = 80;
  stext->showsyntax = true;
  stext->showlinenrs = true;

  /* header */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "header for text"));

  lib_addtail(&stext->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* footer */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "footer for text"));
  lib_addtail(&stext->regionbase, region);
  region->regiontype = RGN_TYPE_FOOTER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_TOP : RGN_ALIGN_BOTTOM;

  /* properties region */
  region = static_cast<ARegion *>(mem_callocn(sizeof(ARegion), "properties region for text"));

  lib_addtail(&stext->regionbase, region);
  region->regiontype = RGN_TYPE_UI;
  region->alignment = RGN_ALIGN_RIGHT;
  region->flag = RGN_FLAG_HIDDEN;

  /* main region */
  region = static_cast<ARegion *>(mem_callocn(sizeof(ARegion), "main region for text"));

  lib_addtail(&stext->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return (SpaceLink *)stext;
}

/* Doesn't free the space-link itself. */
static void text_free(SpaceLink *sl)
{
  SpaceText *stext = (SpaceText *)sl;

  stext->text = nullptr;
  text_free_caches(stext);
}

/* spacetype; init callback */
static void text_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *text_duplicate(SpaceLink *sl)
{
  SpaceText *stextn = static_cast<SpaceText *>(MEM_dupallocN(sl));

  /* clear or remove stuff from old */

  stextn->runtime.drawcache = nullptr; /* space need its own cache */

  return (SpaceLink *)stextn;
}

static void text_listener(const wmSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  const wmNotifier *wmn = params->notifier;
  SpaceText *st = static_cast<SpaceText *>(area->spacedata.first);

  /* context changes */
  switch (wmn->category) {
    case NC_TEXT:
      /* check if active text was changed, no need to redraw if text isn't active
       * (reference == nullptr) means text was unlinked, should update anyway for this
       * case -- no way to know was text active before unlinking or not */
      if (wmn->reference && wmn->reference != st->text) {
        break;
      }

      switch (wmn->data) {
        case ND_DISPLAY:
        case ND_CURSOR:
          ED_area_tag_redraw(area);
          break;
      }

      switch (wmn->action) {
        case NA_EDITED:
          if (st->text) {
            text_drawcache_tag_update(st, true);
            text_update_edited(st->text);
          }

          ed_area_tag_redraw(area);
          ATTR_FALLTHROUGH; /* fall down to tag redraw */
        case NA_ADDED:
        case NA_REMOVED:
        case NA_SELECTED:
          ed_area_tag_redraw(area);
          break;
      }

      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_TEXT) {
        ed_area_tag_redraw(area);
      }
      break;
  }
}

static void text_optypes()
{
  wm_optype_append(TEXT_OT_new);
  wm_optype_append(TEXT_OT_open);
  wm_optype_append(TEXT_OT_reload);
  wm_optype_append(TEXT_OT_unlink);
  wm_optype_append(TEXT_OT_save);
  wm_optype_append(TEXT_OT_save_as);
  wm_otype_append(TEXT_OT_make_internal);
  wm_optype_append(TEXT_OT_run_script);
  wm_optype_append(TEXT_OT_refresh_pyconstraints);

  wm_optype_append(TEXT_OT_paste);
  WM_optype_append(TEXT_OT_copy);
  WM_optype_append(TEXT_OT_cut);
  WM_optype_append(TEXT_OT_duplicate_line);

  WM_optype_append(TEXT_OT_convert_whitespace);
  WM_optype_append(TEXT_OT_comment_toggle);
  WM_optype_append(TEXT_OT_unindent);
  WM_optype_append(TEXT_OT_indent);
  WM_optype_append(TEXT_OT_indent_or_autocomplete);

  WM_optype_append(TEXT_OT_select_line);
  WM_optype_append(TEXT_OT_select_all);
  WM_optype_append(TEXT_OT_select_word);

  WM_optype_append(TEXT_OT_move_lines);

  wm_optype_append(TEXT_OT_jump);
  wm_optype_append(TEXT_OT_move);
  wm_optype_append(TEXT_OT_move_select);
  wm_optype_append(TEXT_OT_delete);
  wm_optype_append(TEXT_OT_overwrite_toggle);

  wm_optype_append(TEXT_OT_selection_set);
  wm_optype_append(TEXT_OT_cursor_set);
  WM_optype_append(TEXT_OT_scroll);
  WM_optype_append(TEXT_OT_scroll_bar);
  WM_optype_append(TEXT_OT_line_number);

  WM_optype_append(TEXT_OT_line_break);
  WM_optype_append(TEXT_OT_insert);

  WM_optype_append(TEXT_OT_find);
  WM_optype_append(TEXT_OT_find_set_selected);
  WM_optype_append(TEXT_OT_replace);
  WM_optype_append(TEXT_OT_replace_set_selected);

  WM_optype_append(TEXT_OT_start_find);
  WM_optype_append(TEXT_OT_jump_to_file_at_point_internal);

  WM_optype_append(TEXT_OT_to_3d_object);

  WM_optype_append(TEXT_OT_resolve_conflict);

  WM_optype_append(TEXT_OT_autocomplete);
}

static void text_keymap(wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "Text Generic", SPACE_TEXT, 0);
  WM_keymap_ensure(keyconf, "Text", SPACE_TEXT, 0);
}

const char *text_context_dir[] = {"edit_text", nullptr};

static int /*eContextResult*/ text_cxt(const Cxt *C,
                                       const char *member,
                                       CxtDataResult *result)
{
  SpaceText *st = cxt_wm_space_text(C);

  if (cxt_data_dir(member)) {
    cxt_data_dir_set(result, text_context_dir);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "edit_text")) {
    if (st->text != nullptr) {
      cxt_data_id_ptr_set(result, &st->text->id);
    }
    return CXT_RESULT_OK;
  }

  return CXT_RESULT_MEMBER_NOT_FOUND;
}

/* main region */

/* add handlers, stuff you only do once or on area/region changes */
static void text_main_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;
  List *lb;

  ui_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_STANDARD, region->winx, region->winy);

  /* own keymap */
  keymap = wm_keymap_ensure(wm->defaultconf, "Text Generic", SPACE_TEXT, 0);
  wm_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);
  keymap = wm_keymap_ensure(wm->defaultconf, "Text", SPACE_TEXT, 0);
  wm_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);

  /* add drop boxes */
  lb = wm_dropboxmap_find("Text", SPACE_TEXT, RGN_TYPE_WINDOW);

  wm_event_add_dropbox_handler(&region->handlers, lb);
}

static void text_main_region_draw(const Cxt *C, ARegion *region)
{
  /* draw entirely, view changes should be handled here */
  SpaceText *st = cxt_wm_space_text(C);
  // View2D *v2d = &region->v2d;

  /* clear and setup matrix */
  UI_ThemeClearColor(TH_BACK);

  // UI_view2d_view_ortho(v2d);

  /* data... */
  draw_text_main(st, region);

  /* reset view matrix */
  // UI_view2d_view_restore(C);

  /* scrollers? */
}

static void text_cursor(wmWindow *win, ScrArea *area, ARegion *region)
{
  SpaceText *st = static_cast<SpaceText *>(area->spacedata.first);
  int wmcursor = WM_CURSOR_TEXT_EDIT;

  if (st->text && lib_rcti_isect_pt(&st->runtime.scroll_region_handle,
                                    win->eventstate->xy[0] - region->winrct.xmin,
                                    st->runtime.scroll_region_handle.ymin))
  {
    wmcursor = WM_CURSOR_DEFAULT;
  }

  wm_cursor_set(win, wmcursor);
}

/* dropboxes */

static bool text_drop_poll(Cxt * /*C*/, wmDrag *drag, const wmEvent * /*event*/)
{
  if (drag->type == WM_DRAG_PATH) {
    const eFileSel_File_Types file_type = eFileSel_File_Types(wm_drag_get_path_file_type(drag));
    if (ELEM(file_type, 0, FILE_TYPE_PYSCRIPT, FILE_TYPE_TEXT)) {
      return true;
    }
  }
  return false;
}

static void text_drop_copy(Cxt * /*C*/, wmDrag *drag, wmDropBox *drop)
{
  /* copy drag path to properties */
  api_string_set(drop->ptr, "filepath", wm_drag_get_path(drag));
}

static bool text_drop_paste_poll(Cxt * /*C*/, wmDrag *drag, const wmEvent * /*event*/)
{
  return (drag->type == WM_DRAG_ID);
}

static void text_drop_paste(Cxt * /*C*/, wmDrag *drag, wmDropBox *drop)
{
  char *text;
  Id *id = wm_drag_get_local_id(drag, 0);

  /* copy drag path to properties */
  text = api_path_full_id_py(id);
  api_string_set(drop->ptr, "text", text);
  MEM_freeN(text);
}

/* this region dropbox definition */
static void text_dropboxes()
{
  List *lb = wm_dropboxmap_find("Text", SPACE_TEXT, RGN_TYPE_WINDOW);

  wm_dropbox_add(lb, "TEXT_OT_open", text_drop_poll, text_drop_copy, nullptr, nullptr);
  wm_dropbox_add(lb, "TEXT_OT_insert", text_drop_paste_poll, text_drop_paste, nullptr, nullptr);
}

/* end drop */

/* header region */

/* add handlers, stuff you only do once or on area/region changes */
static void text_header_region_init(Window * /*wm*/, ARegion *region)
{
  ed_region_header_init(region);
}

static void text_header_region_draw(const Cxt *C, ARegion *region)
{
  ed_region_header(C, region);
}

/* props region */

/* add handlers, stuff you only do once or on area/region changes */
static void text_props_region_init(Window *wm, ARegion *region)
{
  wmKeyMap *keymap;

  region->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
  ed_region_panels_init(wm, region);

  /* own keymaps */
  keymap = wm_keymap_ensure(wm->defaultconf, "Text Generic", SPACE_TEXT, 0);
  wm_event_add_keymap_handler_v2d_mask(&region->handlers, keymap);
}

static void text_props_region_draw(const Cxt *C, ARegion *region)
{
  SpaceText *st = cxt_wm_space_text(C);

  ed_region_panels(C, region);

  /* this flag trick is make sure buttons have been added already */
  if (st->flags & ST_FIND_ACTIVATE) {
    if (ui_textbtn_activate_rna(C, region, st, "find_text")) {
      /* if the panel was already open we need to do another redraw */
      ScrArea *area = cxt_wm_area(C);
      wm_event_add_notifier(C, NC_SPACE | ND_SPACE_TEXT, area);
    }
    st->flags &= ~ST_FIND_ACTIVATE;
  }
}

static void text_id_remap(ScrArea * /*area*/, SpaceLink *slink, const IdRemapper *mappings)
{
  SpaceText *stext = (SpaceText *)slink;
  dune_id_remapper_apply(mappings, (Id **)&stext->text, ID_REMAP_APPLY_ENSURE_REAL);
}

static void text_foreach_id(SpaceLink *space_link, LibForeachIdData *data)
{
  SpaceText *st = reinterpret_cast<SpaceText *>(space_link);
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, st->text, IDWALK_CB_USER_ONE);
}

static void text_space_dune_read_data(DuneDataReader * /*reader*/, SpaceLink *sl)
{
  SpaceText *st = (SpaceText *)sl;
  memset(&st->runtime, 0x0, sizeof(st->runtime));
}

static void text_space_dune_write(DuneWriter *writer, SpaceLink *sl)
{
  loader_write_struct(writer, SpaceText, sl);
}

/* registration */
void ed_spacetype_text()
{
  SpaceType *st = static_cast<SpaceType *>(MEM_callocN(sizeof(SpaceType), "spacetype text"));
  ARegionType *art;

  st->spaceid = SPACE_TEXT;
  STRNCPY(st->name, "Text");

  st->create = text_create;
  st->free = text_free;
  st->init = text_init;
  st->duplicate = text_duplicate;
  st->optypes = text_optypes;
  st->keymap = text_keymap;
  st->listener = text_listener;
  st->cxt = text_cxt;
  st->dropboxes = text_dropboxes;
  st->id_remap = text_id_remap;
  st->foreach_id = text_foreach_id;
  st->dune_read_data = text_space_dune_read_data;
  st->dune_read_after_liblink = nullptr;
  st->dune_write = text_space_dune_write;

  /* regions: main window */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype text region"));
  art->regionid = RGN_TYPE_WINDOW;
  art->init = text_main_region_init;
  art->draw = text_main_region_draw;
  art->cursor = text_cursor;
  art->event_cursor = true;

  lib_addhead(&st->regiontypes, art);

  /* regions: properties */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype text region"));
  art->regionid = RGN_TYPE_UI;
  art->prefsizex = UI_COMPACT_PANEL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI;

  art->init = text_properties_region_init;
  art->draw = text_properties_region_draw;
  lib_addhead(&st->regiontypes, art);

  /* regions: header */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype text region"));
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;

  art->init = text_header_region_init;
  art->draw = text_header_region_draw;
  lib_addhead(&st->regiontypes, art);

  /* regions: footer */
  art = static_cast<ARegionType *>(MEM_callocN(sizeof(ARegionType), "spacetype text region"));
  art->regionid = RGN_TYPE_FOOTER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FOOTER;
  art->init = text_header_region_init;
  art->draw = text_header_region_draw;
  lib_addhead(&st->regiontypes, art);

  dune_spacetype_register(st);

  /* register formatters */
  ED_text_format_register_py();
  ED_text_format_register_osl();
  ED_text_format_register_pov();
  ED_text_format_register_pov_ini();
}
