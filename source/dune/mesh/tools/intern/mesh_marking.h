#pragma once

typedef struct MeshEditSelection {
  struct MeshEditSelection *next, *prev;
  MeshElem *ele;
  char htype;
} MeshEditSelection;

typedef enum eMeshSelectionFlushFLags {
  M_SELECT_LEN_FLUSH_RECALC_NOTHING = 0,
  M_SELECT_LEN_FLUSH_RECALC_VERT = (1 << 0),
  M_SELECT_LEN_FLUSH_RECALC_EDGE = (1 << 1),
  M_SELECT_LEN_FLUSH_RECALC_FACE = (1 << 2),
  M_SELECT_LEN_FLUSH_RECALC_ALL = (BM_SELECT_LEN_FLUSH_RECALC_VERT |
                                    BM_SELECT_LEN_FLUSH_RECALC_EDGE |
                                    BM_SELECT_LEN_FLUSH_RECALC_FACE),
} eMeshSelectionFlushFLags;

/* Geometry hiding code. */

#define mesh_elem_hide_set(mesh, ele, hide) _bm_elem_hide_set(bm, &(ele)->head, hide)
void _mesh_elem_hide_set(Mesh *mesh, MeshHeader *head, bool hide);
void mesh_vert_hide_set(MeshVert *v, bool hide);
void mesh_edge_hide_set(MeshEdge *e, bool hide);
void mesh_face_hide_set(MeshFace *f, bool hide);

/* Selection code. */

/**
 * note use mesh_elem_flag_test(ele, MESH_ELEM_SELECT) to test selection
 * note by design, this will not touch the editselection history stuff
 */
void mesh_elem_select_set(Mesh *mesh, MeshElem *ele, bool select);

void mesh_elem_hflag_enable_test(
    Mesh *mesh, char htype, char hflag, bool respecthide, bool overwrite, char hflag_test);
void mesh_elem_hflag_disable_test(
    Mesh *mesh, char htype, char hflag, bool respecthide, bool overwrite, char hflag_test);

void mesh_elem_hflag_enable_all(Mesh *mesh, char htype, char hflag, bool respecthide);
void mesh_elem_hflag_disable_all(Mesh *mesh, char htype, char hflag, bool respecthide);

/* Individual element select functions, mesh_elem_select_set is a shortcut for these
 * that automatically detects which one to use. */

/**
 * Select Vert
 *
 * Changes selection state of a single vertex
 * in a mesh
 */
void mesh_vert_select_set(Mesh *mesh, MeshVert *v, bool select);
/**
 * Select Edge
 *
 * Changes selection state of a single edge in a mesh.
 */
void mesh_edge_select_set(Mesh *mesh, MeshEdge *e, bool select);
/**
 * Select Face
 *
 * Changes selection state of a single
 * face in a mesh.
 */
void mesh_face_select_set(Mesh *mesh, MeshFace *f, bool select);

/* Lower level functions which don't do flushing. */

void mesh_edge_select_set_noflush(Mesh *mesh, MeshEdge *e, bool select);
void mesh_face_select_set_noflush(Mesh *mesh, MeshFace *f, bool select);

/**
 * Select Mode Clean
 *
 * Remove isolated selected elements when in a mode doesn't support them.
 * eg: in edge-mode a selected vertex must be connected to a selected edge.
 *
 * this could be made a part of mesh_select_mode_flush_ex
 */
void mesh_select_mode_clean_ex(Mesh *mesh, short selectmode);
void mesh_select_mode_clean(Mesh *mesh);

/**
 * Select Mode Set
 *
 * Sets the selection mode for the bmesh,
 * updating the selection state.
 */
void BM_mesh_select_mode_set(BMesh *bm, int selectmode);
/**
 * \brief Select Mode Flush
 *
 * Makes sure to flush selections 'upwards'
 * (ie: all verts of an edge selects the edge and so on).
 * This should only be called by system and not tool authors.
 */
void BM_mesh_select_mode_flush_ex(BMesh *bm, short selectmode, eBMSelectionFlushFLags flags);
void BM_mesh_select_mode_flush(BMesh *bm);

/**
 * Mode independent de-selection flush (up/down).
 */
void BM_mesh_deselect_flush(BMesh *bm);
/**
 * Mode independent selection flush (up/down).
 */
void BM_mesh_select_flush(BMesh *bm);

int BM_mesh_elem_hflag_count_enabled(BMesh *bm, char htype, char hflag, bool respecthide);
int BM_mesh_elem_hflag_count_disabled(BMesh *bm, char htype, char hflag, bool respecthide);

/* Edit selection stuff. */

void BM_mesh_active_face_set(BMesh *bm, BMFace *f);
BMFace *BM_mesh_active_face_get(BMesh *bm, bool is_sloppy, bool is_selected);
BMEdge *BM_mesh_active_edge_get(BMesh *bm);
BMVert *BM_mesh_active_vert_get(BMesh *bm);
BMElem *BM_mesh_active_elem_get(BMesh *bm);

/**
 * Generic way to get data from an #BMEditSelection type
 * These functions were written to be used by the Modifier widget
 * when in Rotate about active mode, but can be used anywhere.
 *
 * - #BM_editselection_center
 * - #BM_editselection_normal
 * - #BM_editselection_plane
 */
void BM_editselection_center(BMEditSelection *ese, float r_center[3]);
void BM_editselection_normal(BMEditSelection *ese, float r_normal[3]);
/**
 * Calculate a plane that is right angles to the edge/vert/faces normal
 * also make the plane run along an axis that is related to the geometry,
 * because this is used for the gizmos Y axis.
 */
void BM_editselection_plane(BMEditSelection *ese, float r_plane[3]);

#define BM_select_history_check(bm, ele) _bm_select_history_check(bm, &(ele)->head)
#define BM_select_history_remove(bm, ele) _bm_select_history_remove(bm, &(ele)->head)
#define BM_select_history_store_notest(bm, ele) _bm_select_history_store_notest(bm, &(ele)->head)
#define BM_select_history_store(bm, ele) _bm_select_history_store(bm, &(ele)->head)
#define BM_select_history_store_head_notest(bm, ele) \
  _bm_select_history_store_head_notest(bm, &(ele)->head)
#define BM_select_history_store_head(bm, ele) _bm_select_history_store_head(bm, &(ele)->head)
#define BM_select_history_store_after_notest(bm, ese_ref, ele) \
  _bm_select_history_store_after_notest(bm, ese_ref, &(ele)->head)
#define BM_select_history_store_after(bm, ese, ese_ref) \
  _bm_select_history_store_after(bm, ese_ref, &(ele)->head)

bool _bm_select_history_check(BMesh *bm, const BMHeader *ele);
bool _bm_select_history_remove(BMesh *bm, BMHeader *ele);
void _bm_select_history_store_notest(BMesh *bm, BMHeader *ele);
void _bm_select_history_store(BMesh *bm, BMHeader *ele);
void _bm_select_history_store_head_notest(BMesh *bm, BMHeader *ele);
void _bm_select_history_store_head(BMesh *bm, BMHeader *ele);
void _bm_select_history_store_after(BMesh *bm, BMEditSelection *ese_ref, BMHeader *ele);
void _bm_select_history_store_after_notest(BMesh *bm, BMEditSelection *ese_ref, BMHeader *ele);

void BM_select_history_validate(BMesh *bm);
void BM_select_history_clear(BMesh *bm);
/**
 * Get the active mesh element (with active-face fallback).
 */
bool BM_select_history_active_get(BMesh *bm, struct BMEditSelection *ese);
/**
 * Return a map from #BMVert/#BMEdge/#BMFace -> #BMEditSelection.
 */
struct GHash *BM_select_history_map_create(BMesh *bm);

/**
 * Map arguments may all be the same pointer.
 */
void BM_select_history_merge_from_targetmap(
    BMesh *bm, GHash *vert_map, GHash *edge_map, GHash *face_map, bool use_chain);

#define BM_SELECT_HISTORY_BACKUP(bm) \
  { \
    ListBase _bm_prev_selected = (bm)->selected; \
    BLI_listbase_clear(&(bm)->selected)

#define BM_SELECT_HISTORY_RESTORE(bm) \
  (bm)->selected = _bm_prev_selected; \
  } \
  (void)0
