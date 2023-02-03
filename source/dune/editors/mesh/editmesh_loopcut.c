#include "DNA_object_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_string.h"

#include "BLT_translation.h"

#include "DNA_mesh_types.h"

#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_layer.h"
#include "BKE_modifier.h"
#include "BKE_report.h"
#include "BKE_unit.h"

#include "UI_interface.h"

#include "ED_mesh.h"
#include "ED_numinput.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_view3d.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "mesh_intern.h" /* own include */

#define SUBD_SMOOTH_MAX 4.0f
#define SUBD_CUTS_MAX 500

/* ringsel operator */

struct MeshCoordsCache {
  bool is_init, is_alloc;
  const float (*coords)[3];
};

/* struct for properties used while drawing */
typedef struct RingSelOpData {
  ARegion *region;   /* region that ringsel was activated in */
  void *draw_handle; /* for drawing preview loop */

  struct EditMesh_PreSelEdgeRing *presel_edgering;

  ViewContext vc;

  Depsgraph *depsgraph;

  Base **bases;
  uint bases_len;

  struct MeshCoordsCache *geom_cache;

  /* These values switch objects based on the object under the cursor. */
  uint base_index;
  Object *ob;
  BMEditMesh *em;
  BMEdge *eed;

  NumInput num;

  bool extend;
  bool do_cut;

  float cuts; /* cuts as float so smooth mouse pan works in small increments */
  float smoothness;
} RingSelOpData;

/* modal loop selection drawing callback */
static void ringsel_draw(const bContext *UNUSED(C), ARegion *UNUSED(region), void *arg)
{
  RingSelOpData *lcd = arg;
  EDBM_preselect_edgering_draw(lcd->presel_edgering, lcd->ob->obmat);
}

static void edgering_select(RingSelOpData *lcd)
{
  if (!lcd->eed) {
    return;
  }

  if (!lcd->extend) {
    for (uint base_index = 0; base_index < lcd->bases_len; base_index++) {
      Object *ob_iter = lcd->bases[base_index]->object;
      BMEditMesh *em = BKE_editmesh_from_object(ob_iter);
      EDBM_flag_disable_all(em, BM_ELEM_SELECT);
      DEG_id_tag_update(ob_iter->data, ID_RECALC_SELECT);
      WM_main_add_notifier(NC_GEOM | ND_SELECT, ob_iter->data);
    }
  }

  BMEditMesh *em = lcd->em;
  BMEdge *eed_start = lcd->eed;
  BMWalker walker;
  BMEdge *eed;
  BMW_init(&walker,
           em->bm,
           BMW_EDGERING,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_FLAG_TEST_HIDDEN,
           BMW_NIL_LAY);

  for (eed = BMW_begin(&walker, eed_start); eed; eed = BMW_step(&walker)) {
    BM_edge_select_set(em->bm, eed, true);
  }
  BMW_end(&walker);
}

static void ringsel_find_edge(RingSelOpData *lcd, const int previewlines)
{
  if (lcd->eed) {
    struct MeshCoordsCache *gcache = &lcd->geom_cache[lcd->base_index];
    if (gcache->is_init == false) {
      Scene *scene_eval = (Scene *)DEG_get_evaluated_id(lcd->vc.depsgraph, &lcd->vc.scene->id);
      Object *ob_eval = DEG_get_evaluated_object(lcd->vc.depsgraph, lcd->ob);
      BMEditMesh *em_eval = BKE_editmesh_from_object(ob_eval);
      gcache->coords = BKE_editmesh_vert_coords_when_deformed(
          lcd->vc.depsgraph, em_eval, scene_eval, ob_eval, NULL, &gcache->is_alloc);
      gcache->is_init = true;
    }

    EDBM_preselect_edgering_update_from_edge(
        lcd->presel_edgering, lcd->em->bm, lcd->eed, previewlines, gcache->coords);
  }
  else {
    EDBM_preselect_edgering_clear(lcd->presel_edgering);
  }
}

static void ringsel_finish(bContext *C, wmOperator *op)
{
  RingSelOpData *lcd = op->customdata;
  const int cuts = RNA_int_get(op->ptr, "number_cuts");
  const float smoothness = RNA_float_get(op->ptr, "smoothness");
  const int smooth_falloff = RNA_enum_get(op->ptr, "falloff");
#ifdef BMW_EDGERING_NGON
  const bool use_only_quads = false;
#else
  const bool use_only_quads = false;
#endif

  if (lcd->eed) {
    BMEditMesh *em = lcd->em;
    BMVert *v_eed_orig[2] = {lcd->eed->v1, lcd->eed->v2};

    edgering_select(lcd);

    if (lcd->do_cut) {
      const bool is_macro = (op->opm != NULL);
      /* a single edge (rare, but better support) */
      const bool is_edge_wire = BM_edge_is_wire(lcd->eed);
      const bool is_single = is_edge_wire || !BM_edge_is_any_face_len_test(lcd->eed, 4);
      const int seltype = is_edge_wire ? SUBDIV_SELECT_INNER :
                          is_single    ? SUBDIV_SELECT_NONE :
                                         SUBDIV_SELECT_LOOPCUT;

      /* Enable grid-fill, so that intersecting loop-cut works as one would expect.
       * Note though that it will break edge-slide in this specific case.
       * See T31939. */
      BM_mesh_esubdivide(em->bm,
                         BM_ELEM_SELECT,
                         smoothness,
                         smooth_falloff,
                         true,
                         0.0f,
                         0.0f,
                         cuts,
                         seltype,
                         SUBD_CORNER_PATH,
                         0,
                         true,
                         use_only_quads,
                         0);

      /* when used in a macro the tessfaces will be recalculated anyway,
       * this is needed here because modifiers depend on updated tessellation, see T45920 */
      EDBM_update(lcd->ob->data,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = true,
                  });

      if (is_single) {
        /* de-select endpoints */
        BM_vert_select_set(em->bm, v_eed_orig[0], false);
        BM_vert_select_set(em->bm, v_eed_orig[1], false);

        EDBM_selectmode_flush_ex(lcd->em, SCE_SELECT_VERTEX);
      }
      /* we can't slide multiple edges in vertex select mode */
      else if (is_macro && (cuts > 1) && (em->selectmode & SCE_SELECT_VERTEX)) {
        EDBM_selectmode_disable(lcd->vc.scene, em, SCE_SELECT_VERTEX, SCE_SELECT_EDGE);
      }
      /* Force edge slide to edge select mode in face select mode. */
      else if (EDBM_selectmode_disable(lcd->vc.scene, em, SCE_SELECT_FACE, SCE_SELECT_EDGE)) {
        /* pass, the change will flush selection */
      }
      else {
        /* else flush explicitly */
        EDBM_selectmode_flush(lcd->em);
      }
    }
    else {
      /* XXX Is this piece of code ever used now? Simple loop select is now
       *     in editmesh_select.c (around line 1000)... */
      /* sets as active, useful for other tools */
      if (em->selectmode & SCE_SELECT_VERTEX) {
        /* low priority TODO: get vertrex close to mouse. */
        BM_select_history_store(em->bm, lcd->eed->v1);
      }
      if (em->selectmode & SCE_SELECT_EDGE) {
        BM_select_history_store(em->bm, lcd->eed);
      }

      EDBM_selectmode_flush(lcd->em);
      DEG_id_tag_update(lcd->ob->data, ID_RECALC_SELECT);
      WM_event_add_notifier(C, NC_GEOM | ND_SELECT, lcd->ob->data);
    }
  }
}
