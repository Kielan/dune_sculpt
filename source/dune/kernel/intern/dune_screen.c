/* Allow using deprecated functionality for .blend file I/O. */
#define TYPES_DEPRECATED_ALLOW

#ifdef WIN32
#  include "lib_winstuff.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_collection.h"
#include "types_defaults.h"
#include "types_pen.h"
#include "types_mask.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_text.h"
#include "types_view3d.h"
#include "types_workspace.h"

#include "lib_ghash.h"
#include "lib_list.h"
#include "lib_math_vector.h"
#include "lib_mempool.h"
#include "lib_rect.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_pen.h"
#include "dune_icons.h"
#include "dune_idprop.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_node.h"
#include "dune_screen.h"
#include "dune_workspace.h"

#include "loader_read_write.h"

static void screen_free_data(Id *id)
{
  Screen *screen = (Screen *)id;

  /* No animation-data here. */

  LIST_FOREACH (ARegion *, region, &screen->regionbase) {
    dune_area_region_free(NULL, region);
  }

  lib_freelistn(&screen->regionbase);

  dune_screen_area_map_free(AREAMAP_FROM_SCREEN(screen));

  dune_previewimg_free(&screen->preview);

  /* Region and timer are freed by the window manager. */
  MEM_SAFE_FREE(screen->tool_tip);
}

static void screen_foreach_id_dopesheet(LibForeachIdData *data, DopeSheet *ads)
{
  if (ads != NULL) {
    DUNE_LIB_FOREACHID_PROCESS_ID(data, ads->source, IDWALK_CB_NOP);
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, ads->filter_grp, IDWALK_CB_NOP);
  }
}

void dune_screen_foreach_id_screen_area(LibForeachIdData *data, ScrArea *area)
{
  DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, area->full, IDWALK_CB_NOP);

  /* TODO: this should be moved to a callback in `SpaceType`, defined in each editor's own code.
   * Will be for a later round of cleanup though... */
  LIST_FOREACH (SpaceLink *, sl, &area->spacedata) {
    switch (sl->spacetype) {
      case SPACE_VIEW3D: {
        View3D *v3d = (View3D *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, v3d->camera, IDWALK_CB_NOP);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, v3d->ob_center, IDWALK_CB_NOP);
        if (v3d->localvd) {
          DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, v3d->localvd->camera, IDWALK_CB_NOP);
        }
        break;
      }
      case SPACE_GRAPH: {
        SpaceGraph *sipo = (SpaceGraph *)sl;
        DUNE_LIB_FOREACHID_PROCESS_FN_CALL(data,
                                           screen_foreach_id_dopesheet(data, sipo->ads));
        break;
      }
      case SPACE_PROPS: {
        SpaceProps *sbtns = (SpaceProps *)sl;
        DUNE_LIB_FOREACHID_PROCESS_ID(data, sbuts->pinid, IDWALK_CB_NOP);
        break;
      }
      case SPACE_FILE:
        break;
      case SPACE_ACTION: {
        SpaceAction *saction = (SpaceAction *)sl;
        screen_foreach_id_dopesheet(data, &saction->ads);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, saction->action, IDWALK_CB_NOP);
        break;
      }
      case SPACE_IMAGE: {
        SpaceImage *sima = (SpaceImage *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sima->image, IDWALK_CB_USER_ONE);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sima->mask_info.mask, IDWALK_CB_USER_ONE);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sima->gpd, IDWALK_CB_USER);
        break;
      }
      case SPACE_SEQ: {
        SpaceSeq *sseq = (SpaceSeq *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sseq->gpd, IDWALK_CB_USER);
        break;
      }
      case SPACE_NLA: {
        SpaceNla *snla = (SpaceNla *)sl;
        DUNE_LIB_FOREACHID_PROCESS_FN_CALL(data,
                                           screen_foreach_id_dopesheet(data, snla->ads));
        break;
      }
      case SPACE_TEXT: {
        SpaceText *st = (SpaceText *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, st->text, IDWALK_CB_NOP);
        break;
      }
      case SPACE_SCRIPT: {
        SpaceScript *scpt = (SpaceScript *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, scpt->script, IDWALK_CB_NOP);
        break;
      }
      case SPACE_OUTLINER: {
        SpaceOutliner *space_outliner = (SpaceOutliner *)sl;
        DUNE_LIB_FOREACHID_PROCESS_ID(data, space_outliner->search_tse.id, IDWALK_CB_NOP);
        if (space_outliner->treestore != NULL) {
          TreeStoreElem *tselem;
          lib_mempool_iter iter;

          lib_mempool_iternew(space_outliner->treestore, &iter);
          while ((tselem = LIB_mempool_iterstep(&iter))) {
            DUNE_LIB_FOREACHID_PROCESS_ID(data, tselem->id, IDWALK_CB_NOP);
          }
        }
        break;
      }
      case SPACE_NODE: {
        SpaceNode *snode = (SpaceNode *)sl;
        const bool is_private_nodetree = snode->id != NULL &&
                                         ntreeFromId(snode->id) == snode->nodetree;

        DUNE_LIB_FOREACHID_PROCESS_ID(data, snode->id, IDWALK_CB_NOP);
        DUNE_LIB_FOREACHID_PROCESS_ID(data, snode->from, IDWALK_CB_NOP);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(
            data, snode->nodetree, is_private_nodetree ? IDWALK_CB_EMBEDDED : IDWALK_CB_USER_ONE);

        LIST_FOREACH (NodeTreePath *, path, &snode->treepath) {
          if (path == snode->treepath.first) {
            /* first nodetree in path is same as snode->nodetree */
            DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data,
                                              path->nodetree,
                                              is_private_nodetree ? IDWALK_CB_EMBEDDED :
                                                                    IDWALK_CB_USER_ONE);
          }
          else {
            DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, path->nodetree, IDWALK_CB_USER_ONE);
          }

          if (path->nodetree == NULL) {
            break;
          }
        }

        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, snode->edittree, IDWALK_CB_NOP);
        break;
      }
      case SPACE_CLIP: {
        SpaceClip *sclip = (SpaceClip *)sl;
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sclip->clip, IDWALK_CB_USER_ONE);
        DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, sclip->mask_info.mask, IDWALK_CB_USER_ONE);
        break;
      }
      case SPACE_SPREADSHEET: {
        SpaceSpreadsheet *sspreadsheet = (SpaceSpreadsheet *)sl;
        LIST_FOREACH (SpreadsheetContext *, context, &sspreadsheet->context_path) {
          if (context->type == SPREADSHEET_CONTEXT_OBJECT) {
            KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(
                data, ((SpreadsheetContextObject *)context)->object, IDWALK_CB_NOP);
          }
        }
        break;
      }
      default:
        break;
    }
  }
}

static void screen_foreach_id(ID *id, LibraryForeachIDData *data)
{
  if ((KERNEL_lib_query_foreachid_process_flags_get(data) & IDWALK_INCLUDE_UI) == 0) {
    return;
  }
  duneScreen *screen = (duneScreen *)id;

  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    KERNEL_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, BKE_screen_foreach_id_screen_area(data, area));
  }
}

static void screen_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  duneScreen *screen = (duneScreen *)id;

  /* write LibData */
  /* in 2.50+ files, the file identifier for screens is patched, forward compatibility */
  LOADER_write_struct_at_address_with_filecode(writer, ID_SCRN, bScreen, id_address, screen);
  KERNEL_id_dune_write(writer, &screen->id);

  KERNEL_previewimg_dune_write(writer, screen->preview);

  /* direct data */
  KERNEL_screen_area_map_blend_write(writer, AREAMAP_FROM_SCREEN(screen));
}

bool KERNEL_screen_dune_read_data(DuneDataReader *reader, bScreen *screen)
{
  bool success = true;

  screen->regionbase.first = screen->regionbase.last = NULL;
  screen->context = NULL;
  screen->active_region = NULL;

  LOADER_read_data_address(reader, &screen->preview);
  KERNEL_previewimg_dune_read(reader, screen->preview);

  if (!KERNEL_screen_area_map_dune_read_data(reader, AREAMAP_FROM_SCREEN(screen))) {
    printf("Error reading Screen %s... removing it.\n", screen->id.name + 2);
    success = false;
  }

  return success;
}

/* NOTE: file read without screens option G_FILE_NO_UI;
 * check lib pointers in call below */
static void screen_dune_read_lib(DuneLibReader *reader, ID *id)
{
  duneScreen *screen = (duneScreen *)id;
  /* deprecated, but needed for versioning (will be NULL'ed then) */
  LOADER_read_id_address(reader, screen->id.lib, &screen->scene);

  screen->animtimer = NULL; /* saved in rare cases */
  screen->tool_tip = NULL;
  screen->scrubbing = false;

  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    KERNEL_screen_area_dune_read_lib(reader, &screen->id, area);
  }
}

IDTypeInfo IDType_ID_SCR = {
    .id_code = ID_SCR,
    .id_filter = 0,
    .main_listbase_index = INDEX_ID_SCR,
    .struct_size = sizeof(bScreen),
    .name = "Screen",
    .name_plural = "screens",
    .translation_context = BLT_I18NCONTEXT_ID_SCREEN,
    .flags = IDTYPE_FLAGS_NO_COPY | IDTYPE_FLAGS_ONLY_APPEND | IDTYPE_FLAGS_NO_ANIMDATA,
    .asset_type_info = NULL,

    .init_data = NULL,
    .copy_data = NULL,
    .free_data = screen_free_data,
    .make_local = NULL,
    .foreach_id = screen_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .dune_write = screen_dune_write,
    /* Cannot be used yet, because direct_link_screen has a return value. */
    .dune_read_data = NULL,
    .dune_read_lib = screen_dune_read_lib,
    .dune_read_expand = NULL,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* ************ Spacetype/regiontype handling ************** */

/* keep global; this has to be accessible outside of windowmanager */
static ListBase spacetypes = {NULL, NULL};

/* not SpaceType itself */
static void spacetype_free(SpaceType *st)
{
  LISTBASE_FOREACH (ARegionType *, art, &st->regiontypes) {
#ifdef WITH_PYTHON
    BPY_callback_screen_free(art);
#endif
    LIB_freelistN(&art->drawcalls);

    LISTBASE_FOREACH (PanelType *, pt, &art->paneltypes) {
      if (pt->rna_ext.free) {
        pt->rna_ext.free(pt->rna_ext.data);
      }

      LIB_freelistN(&pt->children);
    }

    LISTBASE_FOREACH (HeaderType *, ht, &art->headertypes) {
      if (ht->rna_ext.free) {
        ht->rna_ext.free(ht->rna_ext.data);
      }
    }

    LIB_freelistN(&art->paneltypes);
    LIB_freelistN(&art->headertypes);
  }

  LIB_freelistN(&st->regiontypes);
}

void KERNEL_spacetypes_free(void)
{
  LISTBASE_FOREACH (SpaceType *, st, &spacetypes) {
    spacetype_free(st);
  }

  LIB_freelistN(&spacetypes);
}

SpaceType *KERNEL_spacetype_from_id(int spaceid)
{
  LISTBASE_FOREACH (SpaceType *, st, &spacetypes) {
    if (st->spaceid == spaceid) {
      return st;
    }
  }
  return NULL;
}

ARegionType *KERNEL_regiontype_from_id_or_first(const SpaceType *st, int regionid)
{
  LISTBASE_FOREACH (ARegionType *, art, &st->regiontypes) {
    if (art->regionid == regionid) {
      return art;
    }
  }

  printf(
      "Error, region type %d missing in - name:\"%s\", id:%d\n", regionid, st->name, st->spaceid);
  return st->regiontypes.first;
}

ARegionType *KERNEL_regiontype_from_id(const SpaceType *st, int regionid)
{
  LISTBASE_FOREACH (ARegionType *, art, &st->regiontypes) {
    if (art->regionid == regionid) {
      return art;
    }
  }
  return NULL;
}

const ListBase *KERNEL_spacetypes_list(void)
{
  return &spacetypes;
}

void KERNEL_spacetype_register(SpaceType *st)
{
  /* sanity check */
  SpaceType *stype = BKE_spacetype_from_id(st->spaceid);
  if (stype) {
    printf("error: redefinition of spacetype %s\n", stype->name);
    spacetype_free(stype);
    MEM_freeN(stype);
  }

  LIB_addtail(&spacetypes, st);
}

bool KERNEL_spacetype_exists(int spaceid)
{
  return KERNEL_spacetype_from_id(spaceid) != NULL;
}

/* ***************** Space handling ********************** */

void KERNEL_spacedata_freelist(ListBase *lb)
{
  LISTBASE_FOREACH (SpaceLink *, sl, lb) {
    SpaceType *st = KERNEL_spacetype_from_id(sl->spacetype);

    /* free regions for pushed spaces */
    LISTBASE_FOREACH (ARegion *, region, &sl->regionbase) {
      KERNEL_area_region_free(st, region);
    }

    LIB_freelistN(&sl->regionbase);

    if (st && st->free) {
      st->free(sl);
    }
  }

  LIB_freelistN(lb);
}

static void panel_list_copy(ListBase *newlb, const ListBase *lb)
{
  LIB_listbase_clear(newlb);
  LIB_duplicatelist(newlb, lb);

  /* copy panel pointers */
  Panel *new_panel = newlb->first;
  Panel *panel = lb->first;
  for (; new_panel; new_panel = new_panel->next, panel = panel->next) {
    new_panel->activedata = NULL;
    memset(&new_panel->runtime, 0x0, sizeof(new_panel->runtime));
    panel_list_copy(&new_panel->children, &panel->children);
  }
}

ARegion *KERNEL_area_region_copy(const SpaceType *st, const ARegion *region)
{
  ARegion *newar = MEM_dupallocN(region);

  memset(&newar->runtime, 0x0, sizeof(newar->runtime));

  newar->prev = newar->next = NULL;
  LIB_listbase_clear(&newar->handlers);
  LIB_listbase_clear(&newar->uiblocks);
  LIB_listbase_clear(&newar->panels_category);
  LIB_listbase_clear(&newar->panels_category_active);
  LIB_listbase_clear(&newar->ui_lists);
  newar->visible = 0;
  newar->gizmo_map = NULL;
  newar->regiontimer = NULL;
  newar->headerstr = NULL;
  newar->draw_buffer = NULL;

  /* use optional regiondata callback */
  if (region->regiondata) {
    ARegionType *art = KERNEL_regiontype_from_id(st, region->regiontype);

    if (art && art->duplicate) {
      newar->regiondata = art->duplicate(region->regiondata);
    }
    else if (region->flag & RGN_FLAG_TEMP_REGIONDATA) {
      newar->regiondata = NULL;
    }
    else {
      newar->regiondata = MEM_dupallocN(region->regiondata);
    }
  }

  panel_list_copy(&newar->panels, &region->panels);

  LIB_listbase_clear(&newar->ui_previews);
  LIB_duplicatelist(&newar->ui_previews, &region->ui_previews);

  return newar;
}

/* from lb_src to lb_dst, lb_dst is supposed to be freed */
static void region_copylist(SpaceType *st, ListBase *lb_dst, ListBase *lb_src)
{
  /* to be sure */
  LIB_listbase_clear(lb_dst);

  LISTBASE_FOREACH (ARegion *, region, lb_src) {
    ARegion *region_new = KERNEL_area_region_copy(st, region);
    LIB_addtail(lb_dst, region_new);
  }
}

void KERNEL_spacedata_copylist(ListBase *lb_dst, ListBase *lb_src)
{
  LIB_listbase_clear(lb_dst); /* to be sure */

  LISTBASE_FOREACH (SpaceLink *, sl, lb_src) {
    SpaceType *st = KERNEL_spacetype_from_id(sl->spacetype);

    if (st && st->duplicate) {
      SpaceLink *slnew = st->duplicate(sl);

      LIB_addtail(lb_dst, slnew);

      region_copylist(st, &slnew->regionbase, &sl->regionbase);
    }
  }
}

void KERNEL_spacedata_draw_locks(bool set)
{
  LISTBASE_FOREACH (SpaceType *, st, &spacetypes) {
    LISTBASE_FOREACH (ARegionType *, art, &st->regiontypes) {
      if (set) {
        art->do_lock = art->lock;
      }
      else {
        art->do_lock = false;
      }
    }
  }
}

ARegion *KERNEL_spacedata_find_region_type(const SpaceLink *slink,
                                        const ScrArea *area,
                                        int region_type)
{
  const bool is_slink_active = slink == area->spacedata.first;
  const ListBase *regionbase = (is_slink_active) ? &area->regionbase : &slink->regionbase;
  ARegion *region = NULL;

  LIB_assert(LIB_findindex(&area->spacedata, slink) != -1);

  LISTBASE_FOREACH (ARegion *, region_iter, regionbase) {
    if (region_iter->regiontype == region_type) {
      region = region_iter;
      break;
    }
  }

  /* Should really unit test this instead. */
  LIB_assert(!is_slink_active || region == BKE_area_find_region_type(area, region_type));

  return region;
}

static void (*spacedata_id_remap_cb)(struct ScrArea *area,
                                     struct SpaceLink *sl,
                                     ID *old_id,
                                     ID *new_id) = NULL;

void KERNEL_spacedata_callback_id_remap_set(void (*func)(ScrArea *area, SpaceLink *sl, ID *, ID *))
{
  spacedata_id_remap_cb = func;
}

void KERNEL_spacedata_id_unref(struct ScrArea *area, struct SpaceLink *sl, struct ID *id)
{
  if (spacedata_id_remap_cb) {
    spacedata_id_remap_cb(area, sl, id, NULL);
  }
}

/** Avoid bad-level calls to #WM_gizmomap_tag_refresh. **/
static void (*region_refresh_tag_gizmomap_callback)(struct wmGizmoMap *) = NULL;

void KERNEL_region_callback_refresh_tag_gizmomap_set(void (*callback)(struct wmGizmoMap *))
{
  region_refresh_tag_gizmomap_callback = callback;
}

void KERNEL_screen_gizmo_tag_refresh(struct bScreen *screen)
{
  if (region_refresh_tag_gizmomap_callback == NULL) {
    return;
  }

  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      if (region->gizmo_map != NULL) {
        region_refresh_tag_gizmomap_callback(region->gizmo_map);
      }
    }
  }
}

/** Avoid bad-level calls to WM_gizmomap_delete **/
static void (*region_free_gizmomap_callback)(struct wmGizmoMap *) = NULL;

void KERNEL_region_callback_free_gizmomap_set(void (*callback)(struct wmGizmoMap *))
{
  region_free_gizmomap_callback = callback;
}

static void area_region_panels_free_recursive(Panel *panel)
{
  MEM_SAFE_FREE(panel->activedata);

  LISTBASE_FOREACH_MUTABLE (Panel *, child_panel, &panel->children) {
    area_region_panels_free_recursive(child_panel);
  }

  MEM_freeN(panel);
}

void KERNEL_area_region_panels_free(ListBase *panels)
{
  LISTBASE_FOREACH_MUTABLE (Panel *, panel, panels) {
    /* Free custom data just for parent panels to avoid a double free. */
    MEM_SAFE_FREE(panel->runtime.custom_data_ptr);
    area_region_panels_free_recursive(panel);
  }
  LIB_listbase_clear(panels);
}

void KERNEL_area_region_free(SpaceType *st, ARegion *region)
{
  if (st) {
    ARegionType *art = BKE_regiontype_from_id(st, region->regiontype);

    if (art && art->free) {
      art->free(region);
    }

    if (region->regiondata) {
      printf("regiondata free error\n");
    }
  }
  else if (region->type && region->type->free) {
    region->type->free(region);
  }

  KERNEL_area_region_panels_free(&region->panels);

  LISTBASE_FOREACH (uiList *, uilst, &region->ui_lists) {
    if (uilst->dyn_data && uilst->dyn_data->free_runtime_data_fn) {
      uilst->dyn_data->free_runtime_data_fn(uilst);
    }
    if (uilst->properties) {
      IDP_FreeProperty(uilst->properties);
    }
    MEM_SAFE_FREE(uilst->dyn_data);
  }

  if (region->gizmo_map != NULL) {
    region_free_gizmomap_callback(region->gizmo_map);
  }

  if (region->runtime.block_name_map != NULL) {
    LIB_ghash_free(region->runtime.block_name_map, NULL, NULL);
    region->runtime.block_name_map = NULL;
  }

  LIB_freelistN(&region->ui_lists);
  LIB_freelistN(&region->ui_previews);
  LIB_freelistN(&region->panels_category);
  LIB_freelistN(&region->panels_category_active);
}

void KERNEL_screen_area_free(ScrArea *area)
{
  SpaceType *st = KERNEL_spacetype_from_id(area->spacetype);

  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    KERNEL_area_region_free(st, region);
  }

  MEM_SAFE_FREE(area->global);
  LIB_freelistN(&area->regionbase);

  KERNEL_spacedata_freelist(&area->spacedata);

  LIB_freelistN(&area->actionzones);
}

void KERNEL_screen_area_map_free(ScrAreaMap *area_map)
{
  LISTBASE_FOREACH_MUTABLE (ScrArea *, area, &area_map->areabase) {
    KERNEL_screen_area_free(area);
  }

  LIB_freelistN(&area_map->vertbase);
  LIB_freelistN(&area_map->edgebase);
  LIB_freelistN(&area_map->areabase);
}

void KERNEL_screen_free_data(bScreen *screen)
{
  screen_free_data(&screen->id);
}

/* ***************** Screen edges & verts ***************** */

ScrEdge *KERNEL_screen_find_edge(const duneScreen *screen, ScrVert *v1, ScrVert *v2)
{
  KERNEL_screen_sort_scrvert(&v1, &v2);
  LISTBASE_FOREACH (ScrEdge *, se, &screen->edgebase) {
    if (se->v1 == v1 && se->v2 == v2) {
      return se;
    }
  }

  return NULL;
}

void KERNEL_screen_sort_scrvert(ScrVert **v1, ScrVert **v2)
{
  if (*v1 > *v2) {
    ScrVert *tmp = *v1;
    *v1 = *v2;
    *v2 = tmp;
  }
}

void KERNEL_screen_remove_double_scrverts(bScreen *screen)
{
  LISTBASE_FOREACH (ScrVert *, verg, &screen->vertbase) {
    if (verg->newv == NULL) { /* !!! */
      ScrVert *v1 = verg->next;
      while (v1) {
        if (v1->newv == NULL) { /* !?! */
          if (v1->vec.x == verg->vec.x && v1->vec.y == verg->vec.y) {
            // printf("doublevert\n");
            v1->newv = verg;
          }
        }
        v1 = v1->next;
      }
    }
  }

  /* replace pointers in edges and faces */
  LISTBASE_FOREACH (ScrEdge *, se, &screen->edgebase) {
    if (se->v1->newv) {
      se->v1 = se->v1->newv;
    }
    if (se->v2->newv) {
      se->v2 = se->v2->newv;
    }
    /* edges changed: so.... */
    KERNEL_screen_sort_scrvert(&(se->v1), &(se->v2));
  }
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    if (area->v1->newv) {
      area->v1 = area->v1->newv;
    }
    if (area->v2->newv) {
      area->v2 = area->v2->newv;
    }
    if (area->v3->newv) {
      area->v3 = area->v3->newv;
    }
    if (area->v4->newv) {
      area->v4 = area->v4->newv;
    }
  }

  /* remove */
  LISTBASE_FOREACH_MUTABLE (ScrVert *, verg, &screen->vertbase) {
    if (verg->newv) {
      LIB_remlink(&screen->vertbase, verg);
      MEM_freeN(verg);
    }
  }
}

void KERNEL_screen_remove_double_scredges(bScreen *screen)
{
  /* compare */
  LISTBASE_FOREACH (ScrEdge *, verg, &screen->edgebase) {
    ScrEdge *se = verg->next;
    while (se) {
      ScrEdge *sn = se->next;
      if (verg->v1 == se->v1 && verg->v2 == se->v2) {
        LIB_remlink(&screen->edgebase, se);
        MEM_freeN(se);
      }
      se = sn;
    }
  }
}

void KERNEL_screen_remove_unused_scredges(bScreen *screen)
{
  /* sets flags when edge is used in area */
  int a = 0;
  LISTBASE_FOREACH_INDEX (ScrArea *, area, &screen->areabase, a) {
    ScrEdge *se = KERNEL_screen_find_edge(screen, area->v1, area->v2);
    if (se == NULL) {
      printf("error: area %d edge 1 doesn't exist\n", a);
    }
    else {
      se->flag = 1;
    }
    se = KERNEL_screen_find_edge(screen, area->v2, area->v3);
    if (se == NULL) {
      printf("error: area %d edge 2 doesn't exist\n", a);
    }
    else {
      se->flag = 1;
    }
    se = KERNEL_screen_find_edge(screen, area->v3, area->v4);
    if (se == NULL) {
      printf("error: area %d edge 3 doesn't exist\n", a);
    }
    else {
      se->flag = 1;
    }
    se = KERNEL_screen_find_edge(screen, area->v4, area->v1);
    if (se == NULL) {
      printf("error: area %d edge 4 doesn't exist\n", a);
    }
    else {
      se->flag = 1;
    }
  }
  LISTBASE_FOREACH_MUTABLE (ScrEdge *, se, &screen->edgebase) {
    if (se->flag == 0) {
      LIB_remlink(&screen->edgebase, se);
      MEM_freeN(se);
    }
    else {
      se->flag = 0;
    }
  }
}

void KERNEL_screen_remove_unused_scrverts(duneScreen *screen)
{
  /* we assume edges are ok */
  LISTBASE_FOREACH (ScrEdge *, se, &screen->edgebase) {
    se->v1->flag = 1;
    se->v2->flag = 1;
  }

  LISTBASE_FOREACH_MUTABLE (ScrVert *, sv, &screen->vertbase) {
    if (sv->flag == 0) {
      LIB_remlink(&screen->vertbase, sv);
      MEM_freeN(sv);
    }
    else {
      sv->flag = 0;
    }
  }
}

/* ***************** Utilities ********************** */

ARegion *KERNEL_area_find_region_type(const ScrArea *area, int region_type)
{
  if (area) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      if (region->regiontype == region_type) {
        return region;
      }
    }
  }

  return NULL;
}

ARegion *KERNEL_area_find_region_active_win(ScrArea *area)
{
  if (area == NULL) {
    return NULL;
  }

  ARegion *region = LIB_findlink(&area->regionbase, area->region_active_win);
  if (region && (region->regiontype == RGN_TYPE_WINDOW)) {
    return region;
  }

  /* fallback to any */
  return KERNEL_area_find_region_type(area, RGN_TYPE_WINDOW);
}

ARegion *KERNEL_area_find_region_xy(ScrArea *area, const int regiontype, const int xy[2])
{
  if (area == NULL) {
    return NULL;
  }

  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    if (ELEM(regiontype, RGN_TYPE_ANY, region->regiontype)) {
      if (BLI_rcti_isect_pt_v(&region->winrct, xy)) {
        return region;
      }
    }
  }
  return NULL;
}

ARegion *KERNEL_screen_find_region_xy(bScreen *screen, const int regiontype, const int xy[2])
{
  LISTBASE_FOREACH (ARegion *, region, &screen->regionbase) {
    if (ELEM(regiontype, RGN_TYPE_ANY, region->regiontype)) {
      if (LIB_rcti_isect_pt_v(&region->winrct, xy)) {
        return region;
      }
    }
  }
  return NULL;
}

ScrArea *KERNEL_screen_find_area_from_space(struct bScreen *screen, SpaceLink *sl)
{
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    if (LIB_findindex(&area->spacedata, sl) != -1) {
      return area;
    }
  }

  return NULL;
}

ScrArea *KERNEL_screen_find_big_area(funeScreen *screen, const int spacetype, const short min)
{
  ScrArea *big = NULL;
  int maxsize = 0;

  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    if (ELEM(spacetype, SPACE_TYPE_ANY, area->spacetype)) {
      if (min <= area->winx && min <= area->winy) {
        int size = area->winx * area->winy;
        if (size > maxsize) {
          maxsize = size;
          big = area;
        }
      }
    }
  }

  return big;
}

ScrArea *KERNEL_screen_area_map_find_area_xy(const ScrAreaMap *areamap,
                                          const int spacetype,
                                          const int xy[2])
{
  LISTBASE_FOREACH (ScrArea *, area, &areamap->areabase) {
    if (LIB_rcti_isect_pt_v(&area->totrct, xy)) {
      if (ELEM(spacetype, SPACE_TYPE_ANY, area->spacetype)) {
        return area;
      }
      break;
    }
  }
  return NULL;
}
ScrArea *KERNEL_screen_find_area_xy(bScreen *screen, const int spacetype, const int xy[2])
{
  return KERNEL_screen_area_map_find_area_xy(AREAMAP_FROM_SCREEN(screen), spacetype, xy);
}

void KERNEL_screen_view3d_sync(View3D *v3d, struct Scene *scene)
{
  if (v3d->scenelock && v3d->localvd == NULL) {
    v3d->camera = scene->camera;

    if (v3d->camera == NULL) {
      LISTBASE_FOREACH (ARegion *, region, &v3d->regionbase) {
        if (region->regiontype == RGN_TYPE_WINDOW) {
          RegionView3D *rv3d = region->regiondata;
          if (rv3d->persp == RV3D_CAMOB) {
            rv3d->persp = RV3D_PERSP;
          }
        }
      }
    }
  }
}

void KERNEL_screen_view3d_scene_sync(duneScreen *screen, Scene *scene)
{
  /* are there cameras in the views that are not in the scene? */
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
      if (sl->spacetype == SPACE_VIEW3D) {
        View3D *v3d = (View3D *)sl;
        KERNEL_screen_view3d_sync(v3d, scene);
      }
    }
  }
}

void KERNEL_screen_view3d_shading_init(View3DShading *shading)
{
  const View3DShading *shading_default = DNA_struct_default_get(View3DShading);
  memcpy(shading, shading_default, sizeof(*shading));
}

ARegion *KERNEL_screen_find_main_region_at_xy(bScreen *screen, const int space_type, const int xy[2])
{
  ScrArea *area = KERNEL_screen_find_area_xy(screen, space_type, xy);
  if (!area) {
    return NULL;
  }
  return KERNEL_area_find_region_xy(area, RGN_TYPE_WINDOW, xy);
}

/* Magic zoom calculation, no idea what it signifies, if you find out, tell me! -zr
 *
 * Simple, its magic dude! Well, to be honest,
 * this gives a natural feeling zooming with multiple keypad presses (ton). */

float KERNEL_screen_view3d_zoom_to_fac(float camzoom)
{
  return powf(((float)M_SQRT2 + camzoom / 50.0f), 2.0f) / 4.0f;
}

float KERNEL_screen_view3d_zoom_from_fac(float zoomfac)
{
  return ((sqrtf(4.0f * zoomfac) - (float)M_SQRT2) * 50.0f);
}

bool KERNEL_screen_is_fullscreen_area(const duneScreen *screen)
{
  return ELEM(screen->state, SCREENMAXIMIZED, SCREENFULL);
}

bool KERNEL_screen_is_used(const duneScreen *screen)
{
  return (screen->winid != 0);
}

void KERNEL_screen_header_alignment_reset(duneScreen *screen)
{
  int alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      if (ELEM(region->regiontype, RGN_TYPE_HEADER, RGN_TYPE_TOOL_HEADER)) {
        if (ELEM(area->spacetype, SPACE_FILE, SPACE_USERPREF, SPACE_OUTLINER, SPACE_PROPERTIES)) {
          region->alignment = RGN_ALIGN_TOP;
          continue;
        }
        region->alignment = alignment;
      }
      if (region->regiontype == RGN_TYPE_FOOTER) {
        if (ELEM(area->spacetype, SPACE_FILE, SPACE_USERPREF, SPACE_OUTLINER, SPACE_PROPERTIES)) {
          region->alignment = RGN_ALIGN_BOTTOM;
          continue;
        }
        region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_TOP : RGN_ALIGN_BOTTOM;
      }
    }
  }
  screen->do_refresh = true;
}

void KERNEL_screen_view3d_shading_dune_write(BlendWriter *writer, View3DShading *shading)
{
  if (shading->prop) {
    IDP_DuneWrite(writer, shading->prop);
  }
}

void KERNEL_screen_view3d_shading_dune_read_data(DuneDataReader *reader, View3DShading *shading)
{
  if (shading->prop) {
    LOADER_read_data_address(reader, &shading->prop);
    IDP_DuneDataRead(reader, &shading->prop);
  }
}

static void write_region(DuneWriter *writer, ARegion *region, int spacetype)
{
  LOADER_write_struct(writer, ARegion, region);

  if (region->regiondata) {
    if (region->flag & RGN_FLAG_TEMP_REGIONDATA) {
      return;
    }

    switch (spacetype) {
      case SPACE_VIEW3D:
        if (region->regiontype == RGN_TYPE_WINDOW) {
          RegionView3D *rv3d = region->regiondata;
          LOADER_write_struct(writer, RegionView3D, rv3d);

          if (rv3d->localvd) {
            LOADER_write_struct(writer, RegionView3D, rv3d->localvd);
          }
          if (rv3d->clipbb) {
            LOADER_write_struct(writer, BoundBox, rv3d->clipbb);
          }
        }
        else {
          printf("regiondata write missing!\n");
        }
        break;
      default:
        printf("regiondata write missing!\n");
    }
  }
}

static void write_uilist(DuneWriter *writer, uiList *ui_list)
{
  LOADER_write_struct(writer, uiList, ui_list);

  if (ui_list->properties) {
    IDP_DuneWrite(writer, ui_list->properties);
  }
}

static void write_space_outliner(DuneWriter *writer, SpaceOutliner *space_outliner)
{
  LIB_mempool *ts = space_outliner->treestore;

  if (ts) {
    SpaceOutliner space_outliner_flat = *space_outliner;

    int elems = LIB_mempool_len(ts);
    /* linearize mempool to array */
    TreeStoreElem *data = elems ? LIB_mempool_as_arrayN(ts, "TreeStoreElem") : NULL;

    if (data) {
      /* In this block we use the memory location of the treestore
       * but _not_ its data, the addresses in this case are UUID's,
       * since we can't rely on malloc giving us different values each time.
       */
      TreeStore ts_flat = {0};

      /* we know the treestore is at least as big as a pointer,
       * so offsetting works to give us a UUID. */
      void *data_addr = (void *)POINTER_OFFSET(ts, sizeof(void *));

      ts_flat.usedelem = elems;
      ts_flat.totelem = elems;
      ts_flat.data = data_addr;

      LOADER_write_struct(writer, SpaceOutliner, space_outliner);

      LOADER_write_struct_at_address(writer, TreeStore, ts, &ts_flat);
      LOADER_write_struct_array_at_address(writer, TreeStoreElem, elems, data_addr, data);

      MEM_freeN(data);
    }
    else {
      space_outliner_flat.treestore = NULL;
      LOADER_write_struct_at_address(writer, SpaceOutliner, space_outliner, &space_outliner_flat);
    }
  }
  else {
    LOADER_write_struct(writer, SpaceOutliner, space_outliner);
  }
}

static void write_panel_list(DuneWriter *writer, ListBase *lb)
{
  LISTBASE_FOREACH (Panel *, panel, lb) {
    LOADER_write_struct(writer, Panel, panel);
    write_panel_list(writer, &panel->children);
  }
}

static void write_area(BlendWriter *writer, ScrArea *area)
{
  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    write_region(writer, region, area->spacetype);
    write_panel_list(writer, &region->panels);

    LISTBASE_FOREACH (PanelCategoryStack *, pc_act, &region->panels_category_active) {
      LOADER_write_struct(writer, PanelCategoryStack, pc_act);
    }

    LISTBASE_FOREACH (uiList *, ui_list, &region->ui_lists) {
      write_uilist(writer, ui_list);
    }

    LISTBASE_FOREACH (uiPreview *, ui_preview, &region->ui_previews) {
      LOADER_write_struct(writer, uiPreview, ui_preview);
    }
  }

  LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
    LISTBASE_FOREACH (ARegion *, region, &sl->regionbase) {
      write_region(writer, region, sl->spacetype);
    }

    if (sl->spacetype == SPACE_VIEW3D) {
      View3D *v3d = (View3D *)sl;
      LOADER_write_struct(writer, View3D, v3d);

      if (v3d->localvd) {
        LOADER_write_struct(writer, View3D, v3d->localvd);
      }

      KERNEL_screen_view3d_shading_dune_write(writer, &v3d->shading);
    }
    else if (sl->spacetype == SPACE_GRAPH) {
      SpaceGraph *sipo = (SpaceGraph *)sl;
      ListBase tmpGhosts = sipo->runtime.ghost_curves;

      /* temporarily disable ghost curves when saving */
      LIB_listbase_clear(&sipo->runtime.ghost_curves);

      LOADER_write_struct(writer, SpaceGraph, sl);
      if (sipo->ads) {
        LOADER_write_struct(writer, bDopeSheet, sipo->ads);
      }

      /* reenable ghost curves */
      sipo->runtime.ghost_curves = tmpGhosts;
    }
    else if (sl->spacetype == SPACE_PROPERTIES) {
      LOADER_write_struct(writer, SpaceProperties, sl);
    }
    else if (sl->spacetype == SPACE_FILE) {
      SpaceFile *sfile = (SpaceFile *)sl;

      LOADER_write_struct(writer, SpaceFile, sl);
      if (sfile->params) {
        LOADER_write_struct(writer, FileSelectParams, sfile->params);
      }
      if (sfile->asset_params) {
        LOADER_write_struct(writer, FileAssetSelectParams, sfile->asset_params);
      }
    }
    else if (sl->spacetype == SPACE_SEQ) {
      LOADER_write_struct(writer, SpaceSeq, sl);
    }
    else if (sl->spacetype == SPACE_OUTLINER) {
      SpaceOutliner *space_outliner = (SpaceOutliner *)sl;
      write_space_outliner(writer, space_outliner);
    }
    else if (sl->spacetype == SPACE_IMAGE) {
      LOADER_write_struct(writer, SpaceImage, sl);
    }
    else if (sl->spacetype == SPACE_TEXT) {
      LOADER_write_struct(writer, SpaceText, sl);
    }
    else if (sl->spacetype == SPACE_SCRIPT) {
      SpaceScript *scr = (SpaceScript *)sl;
      scr->but_refs = NULL;
      LOADER_write_struct(writer, SpaceScript, sl);
    }
    else if (sl->spacetype == SPACE_ACTION) {
      LOADER_write_struct(writer, SpaceAction, sl);
    }
    else if (sl->spacetype == SPACE_NLA) {
      SpaceNla *snla = (SpaceNla *)sl;

      LOADER_write_struct(writer, SpaceNla, snla);
      if (snla->ads) {
        LOADER_write_struct(writer, bDopeSheet, snla->ads);
      }
    }
    else if (sl->spacetype == SPACE_NODE) {
      SpaceNode *snode = (SpaceNode *)sl;
      LOADER_write_struct(writer, SpaceNode, snode);

      LISTBASE_FOREACH (bNodeTreePath *, path, &snode->treepath) {
        LOADER_write_struct(writer, bNodeTreePath, path);
      }
    }
    else if (sl->spacetype == SPACE_CONSOLE) {
      SpaceConsole *con = (SpaceConsole *)sl;

      LISTBASE_FOREACH (ConsoleLine *, cl, &con->history) {
        /* 'len_alloc' is invalid on write, set from 'len' on read */
        LOADER_write_struct(writer, ConsoleLine, cl);
        LOADER_write_raw(writer, (size_t)cl->len + 1, cl->line);
      }
      LOADER_write_struct(writer, SpaceConsole, sl);
    }
    else if (sl->spacetype == SPACE_TOPBAR) {
      LOADER_write_struct(writer, SpaceTopBar, sl);
    }
    else if (sl->spacetype == SPACE_STATUSBAR) {
      LOADER_write_struct(writer, SpaceStatusBar, sl);
    }
    else if (sl->spacetype == SPACE_USERPREF) {
      LOADER_write_struct(writer, SpaceUserPref, sl);
    }
    else if (sl->spacetype == SPACE_CLIP) {
      LOADER_write_struct(writer, SpaceClip, sl);
    }
    else if (sl->spacetype == SPACE_INFO) {
      LOADER_write_struct(writer, SpaceInfo, sl);
    }
    else if (sl->spacetype == SPACE_SPREADSHEET) {
      LOADER_write_struct(writer, SpaceSpreadsheet, sl);
      SpaceSpreadsheet *sspreadsheet = (SpaceSpreadsheet *)sl;

      LISTBASE_FOREACH (SpreadsheetRowFilter *, row_filter, &sspreadsheet->row_filters) {
        LOADER_write_struct(writer, SpreadsheetRowFilter, row_filter);
        LOADER_write_string(writer, row_filter->value_string);
      }

      LISTBASE_FOREACH (SpreadsheetColumn *, column, &sspreadsheet->columns) {
        LOADER_write_struct(writer, SpreadsheetColumn, column);
        LOADER_write_struct(writer, SpreadsheetColumnID, column->id);
        LOADER_write_string(writer, column->id->name);
        /* While the display name is technically runtime data, we write it here, otherwise the row
         * filters might not now their type if their region draws before the main region.
         * This would ideally be cleared here. */
        LOADER_write_string(writer, column->display_name);
      }
      LISTBASE_FOREACH (SpreadsheetContext *, context, &sspreadsheet->context_path) {
        switch (context->type) {
          case SPREADSHEET_CONTEXT_OBJECT: {
            SpreadsheetContextObject *object_context = (SpreadsheetContextObject *)context;
            LOADER_write_struct(writer, SpreadsheetContextObject, object_context);
            break;
          }
          case SPREADSHEET_CONTEXT_MODIFIER: {
            SpreadsheetContextModifier *modifier_context = (SpreadsheetContextModifier *)context;
            LOADER_write_struct(writer, SpreadsheetContextModifier, modifier_context);
            LOADER_write_string(writer, modifier_context->modifier_name);
            break;
          }
          case SPREADSHEET_CONTEXT_NODE: {
            SpreadsheetContextNode *node_context = (SpreadsheetContextNode *)context;
            LOADER_write_struct(writer, SpreadsheetContextNode, node_context);
            LOADER_write_string(writer, node_context->node_name);
            break;
          }
        }
      }
    }
  }
}

void KERNEL_screen_area_map_dune_write(DuneWriter *writer, ScrAreaMap *area_map)
{
  LOADER_write_struct_list(writer, ScrVert, &area_map->vertbase);
  LOADER_write_struct_list(writer, ScrEdge, &area_map->edgebase);
  LISTBASE_FOREACH (ScrArea *, area, &area_map->areabase) {
    area->butspacetype = area->spacetype; /* Just for compatibility, will be reset below. */

    LOADER_write_struct(writer, ScrArea, area);

    LOADER_write_struct(writer, ScrGlobalAreaData, area->global);

    write_area(writer, area);

    area->butspacetype = SPACE_EMPTY; /* Unset again, was changed above. */
  }
}

static void direct_link_panel_list(DuneDataReader *reader, ListBase *lb)
{
  LOADER_read_list(reader, lb);

  LISTBASE_FOREACH (Panel *, panel, lb) {
    panel->runtime_flag = 0;
    panel->activedata = NULL;
    panel->type = NULL;
    panel->runtime.custom_data_ptr = NULL;
    direct_link_panel_list(reader, &panel->children);
  }
}

static void direct_link_region(DuneDataReader *reader, ARegion *region, int spacetype)
{
  memset(&region->runtime, 0x0, sizeof(region->runtime));

  direct_link_panel_list(reader, &region->panels);

  LOADER_read_list(reader, &region->panels_category_active);

  LOADER_read_list(reader, &region->ui_lists);

  /* The area's search filter is runtime only, so we need to clear the active flag on read. */
  region->flag &= ~RGN_FLAG_SEARCH_FILTER_ACTIVE;

  LISTBASE_FOREACH (uiList *, ui_list, &region->ui_lists) {
    ui_list->type = NULL;
    ui_list->dyn_data = NULL;
    LOADER_read_data_address(reader, &ui_list->properties);
    IDP_DuneDataRead(reader, &ui_list->properties);
  }

  LOADER_read_list(reader, &region->ui_previews);

  if (spacetype == SPACE_EMPTY) {
    /* unknown space type, don't leak regiondata */
    region->regiondata = NULL;
  }
  else if (region->flag & RGN_FLAG_TEMP_REGIONDATA) {
    /* Runtime data, don't use. */
    region->regiondata = NULL;
  }
  else {
    LOADER_read_data_address(reader, &region->regiondata);
    if (region->regiondata) {
      if (spacetype == SPACE_VIEW3D) {
        RegionView3D *rv3d = region->regiondata;

        LOADER_read_data_address(reader, &rv3d->localvd);
        LOADER_read_data_address(reader, &rv3d->clipbb);

        rv3d->render_engine = NULL;
        rv3d->sms = NULL;
        rv3d->smooth_timer = NULL;

        rv3d->rflag &= ~(RV3D_NAVIGATING | RV3D_PAINTING);
        rv3d->runtime_viewlock = 0;
      }
    }
  }

  region->v2d.sms = NULL;
  region->v2d.alpha_hor = region->v2d.alpha_vert = 255; /* visible by default */
  LIB_listbase_clear(&region->panels_category);
  LIB_listbase_clear(&region->handlers);
  LIB_listbase_clear(&region->uiblocks);
  region->headerstr = NULL;
  region->visible = 0;
  region->type = NULL;
  region->do_draw = 0;
  region->gizmo_map = NULL;
  region->regiontimer = NULL;
  region->draw_buffer = NULL;
  memset(&region->drawrct, 0, sizeof(region->drawrct));
}

/* for the saved 2.50 files without regiondata */
void KERNEL_screen_view3d_do_versions_250(View3D *v3d, ListBase *regions)
{
  LISTBASE_FOREACH (ARegion *, region, regions) {
    if (region->regiontype == RGN_TYPE_WINDOW && region->regiondata == NULL) {
      RegionView3D *rv3d;

      rv3d = region->regiondata = MEM_callocN(sizeof(RegionView3D), "region v3d patch");
      rv3d->persp = (char)v3d->persp;
      rv3d->view = (char)v3d->view;
      rv3d->dist = v3d->dist;
      copy_v3_v3(rv3d->ofs, v3d->ofs);
      copy_qt_qt(rv3d->viewquat, v3d->viewquat);
    }
  }

  /* this was not initialized correct always */
  if (v3d->gridsubdiv == 0) {
    v3d->gridsubdiv = 10;
  }
}

static void direct_link_area(DuneDataReader *reader, ScrArea *area)
{
  LOADER_read_list(reader, &(area->spacedata));
  LOADER_read_list(reader, &(area->regionbase));

  LIB_listbase_clear(&area->handlers);
  area->type = NULL; /* spacetype callbacks */

  /* Should always be unset so that rna_Area_type_get works correctly. */
  area->butspacetype = SPACE_EMPTY;

  area->region_active_win = -1;

  area->flag &= ~AREA_FLAG_ACTIVE_TOOL_UPDATE;

  LOADER_read_data_address(reader, &area->global);

  /* if we do not have the spacetype registered we cannot
   * free it, so don't allocate any new memory for such spacetypes. */
  if (!KERNEL_spacetype_exists(area->spacetype)) {
    /* Hint for versioning code to replace deprecated space types. */
    area->butspacetype = area->spacetype;

    area->spacetype = SPACE_EMPTY;
  }

  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    direct_link_region(reader, region, area->spacetype);
  }

  /* accident can happen when read/save new file with older version */
  /* 2.50: we now always add spacedata for info */
  if (area->spacedata.first == NULL) {
    SpaceInfo *sinfo = MEM_callocN(sizeof(SpaceInfo), "spaceinfo");
    area->spacetype = sinfo->spacetype = SPACE_INFO;
    LIB_addtail(&area->spacedata, sinfo);
  }
  /* add local view3d too */
  else if (area->spacetype == SPACE_VIEW3D) {
    KERNEL_screen_view3d_do_versions_250(area->spacedata.first, &area->regionbase);
  }

  LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
    LOADER_read_list(reader, &(sl->regionbase));

    /* if we do not have the spacetype registered we cannot
     * free it, so don't allocate any new memory for such spacetypes. */
    if (!KERNEL_spacetype_exists(sl->spacetype)) {
      sl->spacetype = SPACE_EMPTY;
    }

    LISTBASE_FOREACH (ARegion *, region, &sl->regionbase) {
      direct_link_region(reader, region, sl->spacetype);
    }

    if (sl->spacetype == SPACE_VIEW3D) {
      View3D *v3d = (View3D *)sl;

      memset(&v3d->runtime, 0x0, sizeof(v3d->runtime));

      if (v3d->gpd) {
        LOADER_read_data_address(reader, &v3d->gpd);
        KERNEL_gpencil_dune_read_data(reader, v3d->gpd);
      }
      LOADER_read_data_address(reader, &v3d->localvd);

      /* render can be quite heavy, set to solid on load */
      if (v3d->shading.type == OB_RENDER) {
        v3d->shading.type = OB_SOLID;
      }
      v3d->shading.prev_type = OB_SOLID;

      KERNEL_screen_view3d_shading_dune_read_data(reader, &v3d->shading);

      KERNEL_screen_view3d_do_versions_250(v3d, &sl->regionbase);
    }
    else if (sl->spacetype == SPACE_GRAPH) {
      SpaceGraph *sipo = (SpaceGraph *)sl;

      LOADER_read_data_address(reader, &sipo->ads);
      memset(&sipo->runtime, 0x0, sizeof(sipo->runtime));
    }
    else if (sl->spacetype == SPACE_NLA) {
      SpaceNla *snla = (SpaceNla *)sl;

      LOADER_read_data_address(reader, &snla->ads);
    }
    else if (sl->spacetype == SPACE_OUTLINER) {
      SpaceOutliner *space_outliner = (SpaceOutliner *)sl;

      /* use LOADER_read_get_new_data_address_no_us and do not free old memory avoiding double
       * frees and use of freed memory. this could happen because of a
       * bug fixed in revision 58959 where the treestore memory address
       * was not unique */
      TreeStore *ts = LOADER_read_get_new_data_address_no_us(reader, space_outliner->treestore);
      space_outliner->treestore = NULL;
      if (ts) {
        TreeStoreElem *elems = BLO_read_get_new_data_address_no_us(reader, ts->data);

        space_outliner->treestore = BLI_mempool_create(
            sizeof(TreeStoreElem), ts->usedelem, 512, LIB_MEMPOOL_ALLOW_ITER);
        if (ts->usedelem && elems) {
          for (int i = 0; i < ts->usedelem; i++) {
            TreeStoreElem *new_elem = BLI_mempool_alloc(space_outliner->treestore);
            *new_elem = elems[i];
          }
        }
        /* we only saved what was used */
        space_outliner->storeflag |= SO_TREESTORE_CLEANUP; /* at first draw */
      }
      space_outliner->tree.first = space_outliner->tree.last = NULL;
      space_outliner->runtime = NULL;
    }
    else if (sl->spacetype == SPACE_IMAGE) {
      SpaceImage *sima = (SpaceImage *)sl;

      sima->iuser.scene = NULL;
      sima->scopes.waveform_1 = NULL;
      sima->scopes.waveform_2 = NULL;
      sima->scopes.waveform_3 = NULL;
      sima->scopes.vecscope = NULL;
      sima->scopes.ok = 0;

      /* WARNING: gpencil data is no longer stored directly in sima after 2.5
       * so sacrifice a few old files for now to avoid crashes with new files!
       * committed: r28002 */
#if 0
      sima->gpd = newdataadr(fd, sima->gpd);
      if (sima->gpd) {
        KERNEL_gpencil_dune_read_data(fd, sima->gpd);
      }
#endif
    }
    else if (sl->spacetype == SPACE_NODE) {
      SpaceNode *snode = (SpaceNode *)sl;

      if (snode->gpd) {
        LOADER_read_data_address(reader, &snode->gpd);
        KERNEL_gpencil_dune_read_data(reader, snode->gpd);
      }

      LOADER_read_list(reader, &snode->treepath);
      snode->edittree = NULL;
      snode->runtime = NULL;
    }
    else if (sl->spacetype == SPACE_TEXT) {
      SpaceText *st = (SpaceText *)sl;
      memset(&st->runtime, 0x0, sizeof(st->runtime));
    }
    else if (sl->spacetype == SPACE_SEQ) {
      SpaceSeq *sseq = (SpaceSeq *)sl;

      /* grease pencil data is not a direct data and can't be linked from direct_link*
       * functions, it should be linked from lib_link* functions instead
       *
       * otherwise it'll lead to lost grease data on open because it'll likely be
       * read from file after all other users of grease pencil and newdataadr would
       * simple return NULL here (sergey)
       */
#if 0
      if (sseq->gpd) {
        sseq->gpd = newdataadr(fd, sseq->gpd);
        KERNEL_gpencil_dune_read_data(fd, sseq->gpd);
      }
#endif
      sseq->scopes.reference_ibuf = NULL;
      sseq->scopes.zebra_ibuf = NULL;
      sseq->scopes.waveform_ibuf = NULL;
      sseq->scopes.sep_waveform_ibuf = NULL;
      sseq->scopes.vector_ibuf = NULL;
      sseq->scopes.histogram_ibuf = NULL;
      memset(&sseq->runtime, 0x0, sizeof(sseq->runtime));
    }
    else if (sl->spacetype == SPACE_PROPERTIES) {
      SpaceProperties *sbuts = (SpaceProperties *)sl;

      sbuts->path = NULL;
      sbuts->texuser = NULL;
      sbuts->mainbo = sbuts->mainb;
      sbuts->mainbuser = sbuts->mainb;
      sbuts->runtime = NULL;
    }
    else if (sl->spacetype == SPACE_CONSOLE) {
      SpaceConsole *sconsole = (SpaceConsole *)sl;

      LOADER_read_list(reader, &sconsole->scrollback);
      LOADER_read_list(reader, &sconsole->history);

      /* Comma expressions, (e.g. expr1, expr2, expr3) evaluate each expression,
       * from left to right.  the right-most expression sets the result of the comma
       * expression as a whole. */
      LISTBASE_FOREACH_MUTABLE (ConsoleLine *, cl, &sconsole->history) {
        LOADER_read_data_address(reader, &cl->line);
        if (cl->line) {
          /* the allocted length is not written, so reset here */
          cl->len_alloc = cl->len + 1;
        }
        else {
          LIB_remlink(&sconsole->history, cl);
          MEM_freeN(cl);
        }
      }
    }
    else if (sl->spacetype == SPACE_FILE) {
      SpaceFile *sfile = (SpaceFile *)sl;

      /* this sort of info is probably irrelevant for reloading...
       * plus, it isn't saved to files yet!
       */
      sfile->folders_prev = sfile->folders_next = NULL;
      LIB_listbase_clear(&sfile->folder_histories);
      sfile->files = NULL;
      sfile->layout = NULL;
      sfile->op = NULL;
      sfile->previews_timer = NULL;
      sfile->tags = 0;
      sfile->runtime = NULL;
      LOADER_read_data_address(reader, &sfile->params);
      LOADER_read_data_address(reader, &sfile->asset_params);
      if (sfile->params) {
        sfile->params->rename_id = NULL;
      }
      if (sfile->asset_params) {
        sfile->asset_params->base_params.rename_id = NULL;
      }
    }
    else if (sl->spacetype == SPACE_ACTION) {
      SpaceAction *saction = (SpaceAction *)sl;

      memset(&saction->runtime, 0x0, sizeof(saction->runtime));
    }
    else if (sl->spacetype == SPACE_CLIP) {
      SpaceClip *sclip = (SpaceClip *)sl;

      sclip->scopes.track_search = NULL;
      sclip->scopes.track_preview = NULL;
      sclip->scopes.ok = 0;
    }
    else if (sl->spacetype == SPACE_SPREADSHEET) {
      SpaceSpreadsheet *sspreadsheet = (SpaceSpreadsheet *)sl;

      sspreadsheet->runtime = NULL;
      BLO_read_list(reader, &sspreadsheet->row_filters);
      LISTBASE_FOREACH (SpreadsheetRowFilter *, row_filter, &sspreadsheet->row_filters) {
        BLO_read_data_address(reader, &row_filter->value_string);
      }
      BLO_read_list(reader, &sspreadsheet->columns);
      LISTBASE_FOREACH (SpreadsheetColumn *, column, &sspreadsheet->columns) {
        BLO_read_data_address(reader, &column->id);
        BLO_read_data_address(reader, &column->id->name);
        /* While the display name is technically runtime data, it is loaded here, otherwise the row
         * filters might not now their type if their region draws before the main region.
         * This would ideally be cleared here. */
        BLO_read_data_address(reader, &column->display_name);
      }

      BLO_read_list(reader, &sspreadsheet->context_path);
      LISTBASE_FOREACH (SpreadsheetContext *, context, &sspreadsheet->context_path) {
        switch (context->type) {
          case SPREADSHEET_CONTEXT_NODE: {
            SpreadsheetContextNode *node_context = (SpreadsheetContextNode *)context;
            BLO_read_data_address(reader, &node_context->node_name);
            break;
          }
          case SPREADSHEET_CONTEXT_MODIFIER: {
            SpreadsheetContextModifier *modifier_context = (SpreadsheetContextModifier *)context;
            BLO_read_data_address(reader, &modifier_context->modifier_name);
            break;
          }
          case SPREADSHEET_CONTEXT_OBJECT: {
            break;
          }
        }
      }
    }
  }

  BLI_listbase_clear(&area->actionzones);

  BLO_read_data_address(reader, &area->v1);
  BLO_read_data_address(reader, &area->v2);
  BLO_read_data_address(reader, &area->v3);
  BLO_read_data_address(reader, &area->v4);
}

bool KERNEL_screen_area_map_dune_read_data(DuneDataReader *reader, ScrAreaMap *area_map)
{
  LOADER_read_list(reader, &area_map->vertbase);
  LOADER_read_list(reader, &area_map->edgebase);
  LOADER_read_list(reader, &area_map->areabase);
  LISTBASE_FOREACH (ScrArea *, area, &area_map->areabase) {
    direct_link_area(reader, area);
  }

  /* edges */
  LISTBASE_FOREACH (ScrEdge *, se, &area_map->edgebase) {
    LOADER_read_data_address(reader, &se->v1);
    LOADER_read_data_address(reader, &se->v2);
    KERNEL_screen_sort_scrvert(&se->v1, &se->v2);

    if (se->v1 == NULL) {
      LIB_remlink(&area_map->edgebase, se);

      return false;
    }
  }

  return true;
}

void KERNEL_screen_area_dune_read_lib(DuneLibReader *reader, ID *parent_id, ScrArea *area)
{
  LOADER_read_id_address(reader, parent_id->lib, &area->full);

  memset(&area->runtime, 0x0, sizeof(area->runtime));

  LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
    switch (sl->spacetype) {
      case SPACE_VIEW3D: {
        View3D *v3d = (View3D *)sl;

        LOADER_read_id_address(reader, parent_id->lib, &v3d->camera);
        LOADER_read_id_address(reader, parent_id->lib, &v3d->ob_center);

        if (v3d->localvd) {
          LOADER_read_id_address(reader, parent_id->lib, &v3d->localvd->camera);
        }
        break;
      }
      case SPACE_GRAPH: {
        SpaceGraph *sipo = (SpaceGraph *)sl;
        bDopeSheet *ads = sipo->ads;

        if (ads) {
          LOADER_read_id_address(reader, parent_id->lib, &ads->source);
          LOADER_read_id_address(reader, parent_id->lib, &ads->filter_grp);
        }
        break;
      }
      case SPACE_PROPERTIES: {
        SpaceProperties *sbuts = (SpaceProperties *)sl;
        LOADER_read_id_address(reader, parent_id->lib, &sbuts->pinid);
        if (sbuts->pinid == NULL) {
          sbuts->flag &= ~SB_PIN_CONTEXT;
        }
        break;
      }
      case SPACE_FILE: {
        SpaceFile *sfile = (SpaceFile *)sl;
        sfile->tags |= FILE_TAG_REBUILD_MAIN_FILES;
        break;
      }
      case SPACE_ACTION: {
        SpaceAction *saction = (SpaceAction *)sl;
        bDopeSheet *ads = &saction->ads;

        if (ads) {
          LOADER_read_id_address(reader, parent_id->lib, &ads->source);
          LOADER_read_id_address(reader, parent_id->lib, &ads->filter_grp);
        }

        LOADER_read_id_address(reader, parent_id->lib, &saction->action);
        break;
      }
      case SPACE_IMAGE: {
        SpaceImage *sima = (SpaceImage *)sl;

        LOADER_read_id_address(reader, parent_id->lib, &sima->image);
        LOADER_read_id_address(reader, parent_id->lib, &sima->mask_info.mask);

        /* NOTE: pre-2.5, this was local data not lib data, but now we need this as lib data
         * so fingers crossed this works fine!
         */
        LOADER_read_id_address(reader, parent_id->lib, &sima->gpd);
        break;
      }
      case SPACE_SEQ: {
        SpaceSeq *sseq = (SpaceSeq *)sl;

        /* NOTE: pre-2.5, this was local data not lib data, but now we need this as lib data
         * so fingers crossed this works fine!
         */
        LOADER_read_id_address(reader, parent_id->lib, &sseq->gpd);
        break;
      }
      case SPACE_NLA: {
        SpaceNla *snla = (SpaceNla *)sl;
        bDopeSheet *ads = snla->ads;

        if (ads) {
          LOADER_read_id_address(reader, parent_id->lib, &ads->source);
          LOADER_read_id_address(reader, parent_id->lib, &ads->filter_grp);
        }
        break;
      }
      case SPACE_TEXT: {
        SpaceText *st = (SpaceText *)sl;

        LOADER_read_id_address(reader, parent_id->lib, &st->text);
        break;
      }
      case SPACE_SCRIPT: {
        SpaceScript *scpt = (SpaceScript *)sl;
        /*scpt->script = NULL; - 2.45 set to null, better re-run the script */
        if (scpt->script) {
          LOADER_read_id_address(reader, parent_id->lib, &scpt->script);
          if (scpt->script) {
            SCRIPT_SET_NULL(scpt->script);
          }
        }
        break;
      }
      case SPACE_OUTLINER: {
        SpaceOutliner *space_outliner = (SpaceOutliner *)sl;
        LOADER_read_id_address(reader, NULL, &space_outliner->search_tse.id);

        if (space_outliner->treestore) {
          TreeStoreElem *tselem;
          LIB_mempool_iter iter;

          LIB_mempool_iternew(space_outliner->treestore, &iter);
          while ((tselem = LIB_mempool_iterstep(&iter))) {
            LOADER_read_id_address(reader, NULL, &tselem->id);
          }
          /* rebuild hash table, because it depends on ids too */
          space_outliner->storeflag |= SO_TREESTORE_REBUILD;
        }
        break;
      }
      case SPACE_NODE: {
        SpaceNode *snode = (SpaceNode *)sl;

        /* node tree can be stored locally in id too, link this first */
        LOADER_read_id_address(reader, parent_id->lib, &snode->id);
        LOADER_read_id_address(reader, parent_id->lib, &snode->from);

        bNodeTree *ntree = snode->id ? ntreeFromID(snode->id) : NULL;
        if (ntree) {
          snode->nodetree = ntree;
        }
        else {
          LOADER_read_id_address(reader, parent_id->lib, &snode->nodetree);
        }

        bNodeTreePath *path;
        for (path = snode->treepath.first; path; path = path->next) {
          if (path == snode->treepath.first) {
            /* first nodetree in path is same as snode->nodetree */
            path->nodetree = snode->nodetree;
          }
          else {
            LOADER_read_id_address(reader, parent_id->lib, &path->nodetree);
          }

          if (!path->nodetree) {
            break;
          }
        }

        /* remaining path entries are invalid, remove */
        bNodeTreePath *path_next;
        for (; path; path = path_next) {
          path_next = path->next;

          LIB_remlink(&snode->treepath, path);
          MEM_freeN(path);
        }

        /* edittree is just the last in the path,
         * set this directly since the path may have been shortened above */
        if (snode->treepath.last) {
          path = snode->treepath.last;
          snode->edittree = path->nodetree;
        }
        else {
          snode->edittree = NULL;
        }
        break;
      }
      case SPACE_CLIP: {
        SpaceClip *sclip = (SpaceClip *)sl;
        LOADER_read_id_address(reader, parent_id->lib, &sclip->clip);
        LOADER_read_id_address(reader, parent_id->lib, &sclip->mask_info.mask);
        break;
      }
      case SPACE_SPREADSHEET: {
        SpaceSpreadsheet *sspreadsheet = (SpaceSpreadsheet *)sl;
        LISTBASE_FOREACH (SpreadsheetContext *, context, &sspreadsheet->context_path) {
          if (context->type == SPREADSHEET_CONTEXT_OBJECT) {
            LOADER_read_id_address(
                reader, parent_id->lib, &((SpreadsheetContextObject *)context)->object);
          }
        }
        break;
      }
      default:
        break;
    }
  }
}
