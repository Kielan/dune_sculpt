/**
 * The MeshLog is an interface for storing undo/redo steps as a BMesh is
 * modified. It only stores changes to the BMesh, not full copies.
 *
 * Currently it supports the following types of changes:
 *
 * - Adding and removing vertices
 * - Adding and removing faces
 * - Moving vertices
 * - Setting vertex paint-mask values
 * - Setting vertex hflags
 */

#include "mem_guardedalloc.h"

#include "lib_ghash.h"
#include "lib_listbase.h"
#include "lib_math.h"
#include "lib_mempool.h"
#include "lib_utildefines.h"

#include "dune_customdata.h"

#include "mesh.h"
#include "mesh_log.h"
#include "range_tree.h"

#include "lib_strict_flags.h"

struct MeshLogEntry {
  struct MeshLogEntry *next, *prev;

  /* The following GHashes map from an element ID to one of the log
   * types above */

  /* Elements that were in the previous entry, but have been
   * deleted */
  GHash *deleted_verts;
  GHash *deleted_faces;
  /* Elements that were not in the previous entry, but are in the
   * result of this entry */
  GHash *added_verts;
  GHash *added_faces;

  /* Vertices whose coordinates, mask value, or hflag have changed */
  GHash *modified_verts;
  GHash *modified_faces;

  lib_mempool *pool_verts;
  lib_mempool *pool_faces;

  /* This is only needed for dropping MeshLogEntries while still in
   * dynamic-topology mode, as that should release vert/face IDs
   * back to the MeshLog but no MeshLog pointer is available at that
   * time.
   *
   * This field is not guaranteed to be valid, any use of it should
   * check for NULL. */
  MeshLog *log;
};

struct MeshLog {
  /* Tree of free Ids */
  struct RangeTreeUInt *unused_ids;

  /* Mapping from unique Ids to vertices and faces
   *
   * Each vertex and face in the log gets a unique uinteger
   * assigned. That ID is taken from the set managed by the
   * unused_ids range tree.
   *
   * The Id is needed because element pointers will change as they
   * are created and deleted.
   */
  GHash *id_to_elem;
  GHash *elem_to_id;

  /* All MeshLogEntrys, ordered from earliest to most recent */
  ListBase entries;

  /* The current log entry from entries list
   *
   * If null, then the original mesh from before any of the log
   * entries is current (i.e. there is nothing left to undo.)
   *
   * If equal to the last entry in the entries list, then all log
   * entries have been applied (i.e. there is nothing left to redo.)
   */
  MeshLogEntry *current_entry;
};

typedef struct {
  float co[3];
  float no[3];
  char hflag;
  float mask;
} MeshLogVert;

typedef struct {
  uint v_ids[3];
  char hflag;
} MeshLogFace;

/************************* Get/set element IDs ************************/

/* bypass actual hashing, the keys don't overlap */
#define logkey_hash lib_ghashutil_inthash_p_simple
#define logkey_cmp lib_ghashutil_intcmp

/* Get the vertex's unique id from the log */
static uint mesh_log_vert_id_get(MeshLog *log, MeshVert *v)
{
  lib_assert(lib_ghash_haskey(log->elem_to_id, v));
  return PTR_AS_UINT(lib_ghash_lookup(log->elem_to_id, v));
}

/* Set the vertex's unique id in the log */
static void mesh_log_vert_id_set(MeshLog *log, MeshVert *v, uint id)
{
  void *vid = PTR_FROM_UINT(id);

  lib_ghash_reinsert(log->id_to_elem, vid, v, NULL, NULL);
  lib_ghash_reinsert(log->elem_to_id, v, vid, NULL, NULL);
}

/* Get a vertex from its unique id */
static MeshVert *mesh_log_vert_from_id(MeshLog *log, uint id)
{
  void *key = PTR_FROM_UINT(id);
  lib_assert(lib_ghash_haskey(log->id_to_elem, key));
  return lib_ghash_lookup(log->id_to_elem, key);
}

/* Get the face's unique id from the log */
static uint mesh_log_face_id_get(MeshLog *log, MeshFace *f)
{
  lib_assert(lib_ghash_haskey(log->elem_to_id, f));
  return PTR_AS_UINT(lib_ghash_lookup(log->elem_to_id, f));
}

/* Set the face's unique id in the log */
static void mesh_log_face_id_set(MeshLog *log, BMFace *f, uint id)
{
  void *fid = PTR_FROM_UINT(id);

  lib_ghash_reinsert(log->id_to_elem, fid, f, NULL, NULL);
  lib_ghash_reinsert(log->elem_to_id, f, fid, NULL, NULL);
}

/* Get a face from its unique id */
static MeshFace *mesh_log_face_from_id(MeshLog *log, uint id)
{
  void *key = PTR_FROM_UINT(id);
  lib_assert(lib_ghash_haskey(log->id_to_elem, key));
  return lib_ghash_lookup(log->id_to_elem, key);
}

/************************ MeshLogVert / MeshLogFace ***********************/

/* Get a vertex's paint-mask value
 *
 * Returns zero if no paint-mask layer is present */
static float vert_mask_get(MeshVert *v, const int cd_vert_mask_offset)
{
  if (cd_vert_mask_offset != -1) {
    return MESH_ELEM_CD_GET_FLOAT(v, cd_vert_mask_offset);
  }
  return 0.0f;
}

/* Set a vertex's paint-mask value
 *
 * Has no effect is no paint-mask layer is present */
static void vert_mask_set(MeshVert *v, const float new_mask, const int cd_vert_mask_offset)
{
  if (cd_vert_mask_offset != -1) {
    MESH_ELEM_CD_SET_FLOAT(v, cd_vert_mask_offset, new_mask);
  }
}

/* Update a MeshLogVert with data from a MeshVert */
static void mesh_log_vert_meshvert_copy(MeshLogVert *lv, MeshVert *v, const int cd_vert_mask_offset)
{
  copy_v3_v3(lv->co, v->co);
  copy_v3_v3(lv->no, v->no);
  lv->mask = vert_mask_get(v, cd_vert_mask_offset);
  lv->hflag = v->head.hflag;
}

/* Allocate and initialize a MeshLogVert */
static MeshLogVert *mesh_log_vert_alloc(MeshLog *log, MeshVert *v, const int cd_vert_mask_offset)
{
  MeshLogEntry *entry = log->current_entry;
  MeshLogVert *lv = lib_mempool_alloc(entry->pool_verts);

  mesh_log_vert_copy(lv, v, cd_vert_mask_offset);

  return lv;
}

/* Allocate and initialize a MeshLogFace */
static MeshLogFace *mesh_log_face_alloc(MeshLog *log, MeshFace *f)
{
  MeshLogEntry *entry = log->current_entry;
  MeshLogFace *lf = lib_mempool_alloc(entry->pool_faces);
  MeshVert *v[3];

  lib_assert(f->len == 3);

  // mesh_iter_as_array(NULL, MESH_VERTS_OF_FACE, f, (void **)v, 3);
  mesh_face_as_array_vert_tri(f, v);

  lf->v_ids[0] = mesh_log_vert_id_get(log, v[0]);
  lf->v_ids[1] = mesh_log_vert_id_get(log, v[1]);
  lf->v_ids[2] = mesh_log_vert_id_get(log, v[2]);

  lf->hflag = f->head.hflag;
  return lf;
}

/************************ Helpers for undo/redo ***********************/

static void mesh_log_verts_unmake(Mesh *mesh, MeshLog *log, GHash *verts)
{
  const int cd_vert_mask_offset = CustomData_get_offset(&bm->vdata, CD_PAINT_MASK);

  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, verts) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    MeshLogVert *lv = lib_ghashIterator_getValue(&gh_iter);
    uint id = PTR_AS_UINT(key);
    MeehVert *v = mesh_log_vert_from_id(log, id);

    /* Ensure the log has the final values of the vertex before
     * deleting it */
    mesh_log_vert_meshvert_copy(lv, v, cd_vert_mask_offset);

    mesh_vert_kill(mesh, v);
  }
}

static void mesh_log_faces_unmake(Mesh *mesh, MeshLog *log, GHash *faces)
{
  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, faces) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    uint id = PTR_AS_UINT(key);
    MeshFace *f = mesh_log_face_from_id(log, id);
    MeshEdge *e_tri[3];
    MeshLoop *l_iter;
    int i;

    l_iter = MESH_FACE_FIRST_LOOP(f);
    for (i = 0; i < 3; i++, l_iter = l_iter->next) {
      e_tri[i] = l_iter->e;
    }

    /* Remove any unused edges */
    mesh_face_kill(mesh, f);
    for (i = 0; i < 3; i++) {
      if (mesh_edge_is_wire(e_tri[i])) {
        mesh_edge_kill(mesh, e_tri[i]);
      }
    }
  }
}

static void mesh_log_verts_restore(Mesh *mesh, MeshLog *log, GHash *verts)
{
  const int cd_vert_mask_offset = CustomData_get_offset(&mesh->vdata, CD_PAINT_MASK);

  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, verts) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    MeshLogVert *lv = lib_ghashIterator_getValue(&gh_iter);
    MeshVert *v = mesh_vert_create(mesh, lv->co, NULL, MESH_CREATE_NOP);
    vert_mask_set(v, lv->mask, cd_vert_mask_offset);
    v->head.hflag = lv->hflag;
    copy_v3_v3(v->no, lv->no);
    mesh_log_vert_id_set(log, v, PTR_AS_UINT(key));
  }
}

static void mesh_log_faces_restore(Mesh *mesh, MeshLog *log, GHash *faces)
{
  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, faces) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    MeshLogFace *lf = lib_ghashIterator_getValue(&gh_iter);
    MeshVert *v[3] = {
        mesh_log_vert_from_id(log, lf->v_ids[0]),
        mesh_log_vert_from_id(log, lf->v_ids[1]),
        mesh_log_vert_from_id(log, lf->v_ids[2]),
    };
    MeshFace *f;

    f = mesh_face_create_verts(mesh, v, 3, NULL, MESH_CREATE_NOP, true);
    f->head.hflag = lf->hflag;
    mesh_log_face_id_set(log, f, PTR_AS_UINT(key));
  }
}

static void mesh_log_vert_values_swap(Mesh *mesh, MeshLog *log, GHash *verts)
{
  const int cd_vert_mask_offset = CustomData_get_offset(&mesh->vdata, CD_PAINT_MASK);

  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, verts) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    MeshLogVert *lv = lib_ghashIterator_getValue(&gh_iter);
    uint id = PTR_AS_UINT(key);
    MeshVert *v = mesh_log_vert_from_id(log, id);
    float mask;

    swap_v3_v3(v->co, lv->co);
    swap_v3_v3(v->no, lv->no);
    SWAP(char, v->head.hflag, lv->hflag);
    mask = lv->mask;
    lv->mask = vert_mask_get(v, cd_vert_mask_offset);
    vert_mask_set(v, mask, cd_vert_mask_offset);
  }
}

static void mesh_log_face_values_swap(MeshLog *log, GHash *faces)
{
  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, faces) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    MeshLogFace *lf = lib_ghashIterator_getValue(&gh_iter);
    uint id = PTR_AS_UINT(key);
    MeshFace *f = mesh_log_face_from_id(log, id);

    SWAP(char, f->head.hflag, lf->hflag);
  }
}

/**********************************************************************/

/* Assign unique IDs to all vertices and faces already in the Mesh */
static void mesh_log_assign_ids(Mesh *mesh, MeshLog *log)
{
  MeshIter iter;
  MeshVert *v;
  MeshFace *f;

  /* Generate vertex IDs */
  MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {
    uint id = range_tree_uint_take_any(log->unused_ids);
    mesh_log_vert_id_set(log, v, id);
  }

  /* Generate face IDs */
  MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
    uint id = range_tree_uint_take_any(log->unused_ids);
    mesh_log_face_id_set(log, f, id);
  }
}

/* Allocate an empty log entry */
static MeshLogEntry *mesh_log_entry_create(void)
{
  MeshLogEntry *entry = mem_callocn(sizeof(MeshLogEntry), __func__);

  entry->deleted_verts = lib_ghash_new(logkey_hash, logkey_cmp, __func__);
  entry->deleted_faces = lib_ghash_new(logkey_hash, logkey_cmp, __func__);
  entry->added_verts = lib_ghash_new(logkey_hash, logkey_cmp, __func__);
  entry->added_faces = lib_ghash_new(logkey_hash, logkey_cmp, __func__);
  entry->modified_verts = lib_ghash_new(logkey_hash, logkey_cmp, __func__);
  entry->modified_faces = lib_ghash_new(logkey_hash, logkey_cmp, __func__);

  entry->pool_verts = lib_mempool_create(sizeof(MeshLogVert), 0, 64, LIB_MEMPOOL_NOP);
  entry->pool_faces = lib_mempool_create(sizeof(MeshLogFace), 0, 64, LIB_MEMPOOL_NOP);

  return entry;
}

/* Free the data in a log entry
 *
 * NOTE: does not free the log entry itself. */
static void mesh_log_entry_free(MeshLogEntry *entry)
{
  lib_ghash_free(entry->deleted_verts, NULL, NULL);
  lib_ghash_free(entry->deleted_faces, NULL, NULL);
  lib_ghash_free(entry->added_verts, NULL, NULL);
  lib_ghash_free(entry->added_faces, NULL, NULL);
  lib_ghash_free(entry->modified_verts, NULL, NULL);
  lib_ghash_free(entry->modified_faces, NULL, NULL);

  lib_mempool_destroy(entry->pool_verts);
  lib_mempool_destroy(entry->pool_faces);
}

static void mesh_log_id_ghash_retake(RangeTreeUInt *unused_ids, GHash *id_ghash)
{
  GHashIterator gh_iter;

  GHASH_ITER (gh_iter, id_ghash) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    uint id = PTR_AS_UINT(key);

    range_tree_uint_retake(unused_ids, id);
  }
}

static int uint_compare(const void *a_v, const void *b_v)
{
  const uint *a = a_v;
  const uint *b = b_v;
  return (*a) < (*b);
}

/* Remap IDs to contiguous indices
 *
 * E.g. if the vertex IDs are (4, 1, 10, 3), the mapping will be:
 *    4 -> 2
 *    1 -> 0
 *   10 -> 3
 *    3 -> 1
 */
static GHash *mesh_log_compress_ids_to_indices(uint *ids, uint totid)
{
  GHash *map = lib_ghash_int_new_ex(__func__, totid);
  uint i;

  qsort(ids, totid, sizeof(*ids), uint_compare);

  for (i = 0; i < totid; i++) {
    void *key = PTR_FROM_UINT(ids[i]);
    void *val = PTR_FROM_UINT(i);
    lib_ghash_insert(map, key, val);
  }

  return map;
}

/* Release all ID keys in id_ghash */
static void mesh_log_id_ghash_release(MeshLog *log, GHash *id_ghash)
{
  GHashIterator gh_iter;

  GHASH_ITER (gh_iter, id_ghash) {
    void *key = lib_ghashIterator_getKey(&gh_iter);
    uint id = PTR_AS_UINT(key);
    range_tree_uint_release(log->unused_ids, id);
  }
}

/***************************** Public API *****************************/

MeshLog *mesh_log_create(Mesh *mesh)
{
  MeshLog *log = mem_callocn(sizeof(*log), __func__);
  const uint reserve_num = (uint)(mesh->totvert + mesh->totface);

  log->unused_ids = range_tree_uint_alloc(0, (uint)-1);
  log->id_to_elem = lib_ghash_new_ex(logkey_hash, logkey_cmp, __func__, reserve_num);
  log->elem_to_id = lib_ghash_ptr_new_ex(__func__, reserve_num);

  /* Assign IDs to all existing vertices and faces */
  mesh_log_assign_ids(mesh, log);

  return log;
}

void mesh_log_cleanup_entry(MeshLogEntry *entry)
{
  MeshLog *log = entry->log;

  if (log) {
    /* Take all used IDs */
    mesh_log_id_ghash_retake(log->unused_ids, entry->deleted_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->deleted_faces);
    mesh_log_id_ghash_retake(log->unused_ids, entry->added_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->added_faces);
    mesh_log_id_ghash_retake(log->unused_ids, entry->modified_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->modified_faces);

    /* delete entries to avoid releasing ids in node cleanup */
    lib_ghash_clear(entry->deleted_verts, NULL, NULL);
    lib_ghash_clear(entry->deleted_faces, NULL, NULL);
    lib_ghash_clear(entry->added_verts, NULL, NULL);
    lib_ghash_clear(entry->added_faces, NULL, NULL);
    lib_ghash_clear(entry->modified_verts, NULL, NULL);
  }
}

MeshLog *mesh_log_from_existing_entries_create(BMesh *bm, BMLogEntry *entry)
{
  MeshLog *log = mesh_log_create(mesh);

  if (entry->prev) {
    log->current_entry = entry;
  }
  else {
    log->current_entry = NULL;
  }

  /* MeshLog manage the entry list again */
  log->entries.first = log->entries.last = entry;

  {
    while (entry->prev) {
      entry = entry->prev;
      log->entries.first = entry;
    }
    entry = log->entries.last;
    while (entry->next) {
      entry = entry->next;
      log->entries.last = entry;
    }
  }

  for (entry = log->entries.first; entry; entry = entry->next) {
    entry->log = log;

    /* Take all used IDs */
    mesh_log_id_ghash_retake(log->unused_ids, entry->deleted_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->deleted_faces);
    mesh_log_id_ghash_retake(log->unused_ids, entry->added_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->added_faces);
    mesh_log_id_ghash_retake(log->unused_ids, entry->modified_verts);
    mesh_log_id_ghash_retake(log->unused_ids, entry->modified_faces);
  }

  return log;
}

void mesh_log_free(MeshLog *log)
{
 MeshLogEntry *entry;

  if (log->unused_ids) {
    range_tree_uint_free(log->unused_ids);
  }

  if (log->id_to_elem) {
    lib_ghash_free(log->id_to_elem, NULL, NULL);
  }

  if (log->elem_to_id) {
    lib_ghash_free(log->elem_to_id, NULL, NULL);
  }

  /* Clear the MeshLog references within each entry, but do not free
   * the entries themselves */
  for (entry = log->entries.first; entry; entry = entry->next) {
    entry->log = NULL;
  }

  mem_freen(log);
}

int mesh_log_length(const MeshLog *log)
{
  return lib_listbase_count(&log->entries);
}

void mesh_log_mesh_elems_reorder(Mesh *mesh, MeshLog *log)
{
  uint *varr;
  uint *farr;

  GHash *id_to_idx;

  MeshIter mesh_iter;
  MeshVert *v;
  MeshFace *f;

  uint i;

  /* Put all vertex IDs into an array */
  varr = mem_mallocn(sizeof(int) * (size_t)mesh->totvert, __func__);
  MESH_INDEX_ITER (v, &mesh_iter, mesh, MESH_VERTS_OF_MESH, i) {
    varr[i] = mesh_log_vert_id_get(log, v);
  }

  /* Put all face IDs into an array */
  farr = mem_mallocn(sizeof(int) * (size_t)mesh->totface, __func__);
  MESH_INDEX_ITER (f, &mesh_iter, mesh, MESH_FACES_OF_MESH, i) {
    farr[i] = mesh_log_face_id_get(log, f);
  }

  /* Create MeshVert index remap array */
  id_to_idx = mesh_log_compress_ids_to_indices(varr, (uint)bm->totvert);
  MESH_INDEX_ITER_ (v, &mesh_iter, mesh, MESH_VERTS_OF_MESH, i) {
    const uint id = mesh_log_vert_id_get(log, v);
    const void *key = PTR_FROM_UINT(id);
    const void *val = lib_ghash_lookup(id_to_idx, key);
    varr[i] = PTR_AS_UINT(val);
  }
  lib_ghash_free(id_to_idx, NULL, NULL);

  /* Create MeshFace index remap array */
  id_to_idx = mesh_log_compress_ids_to_indices(farr, (uint)mesh->totface);
  MESH_INDEX_ITER (f, &mesh_iter, mesh, MESH_FACES_OF_MESH, i) {
    const uint id = mesh_log_face_id_get(log, f);
    const void *key = PTR_FROM_UINT(id);
    const void *val = lib_ghash_lookup(id_to_idx, key);
    farr[i] = PTR_AS_UINT(val);
  }
  lib_ghash_free(id_to_idx, NULL, NULL);

  mesh_remap(mesh, varr, NULL, farr);

  mem_freen(varr);
  mem_freen(farr);
}

MeshLogEntry *mesh_log_entry_add(MeshLog *log)
{
  /* WARNING: this is now handled by the UndoSystem: BKE_UNDOSYS_TYPE_SCULPT
   * freeing here causes unnecessary complications. */
  MeshLogEntry *entry;
#if 0
  /* Delete any entries after the current one */
  entry = log->current_entry;
  if (entry) {
    MeshLogEntry *next;
    for (entry = entry->next; entry; entry = next) {
      next = entry->next;
      mesh_log_entry_free(entry);
      lib_freelinkb(&log->entries, entry);
    }
  }
#endif

  /* Create and append the new entry */
  entry = mesh_log_entry_create();
  lib_addtail(&log->entries, entry);
  entry->log = log;
  log->current_entry = entry;

  return entry;
}

void mesh_log_entry_drop(MeshLogEntry *entry)
{
  MeshLog *log = entry->log;

  if (!log) {
    /* Unlink */
    lib_assert(!(entry->prev && entry->next));
    if (entry->prev) {
      entry->prev->next = NULL;
    }
    else if (entry->next) {
      entry->next->prev = NULL;
    }

    mesh_log_entry_free(entry);
    mem_freen(entry);
    return;
  }

  if (!entry->prev) {
    /* Release IDs of elements that are deleted by this
     * entry. Since the entry is at the beginning of the undo
     * stack, and it's being deleted, those elements can never be
     * restored. Their IDs can go back into the pool. */

    /* This would never happen usually since first entry of log is
     * usually dyntopo enable, which, when reverted will free the log
     * completely. However, it is possible have a stroke instead of
     * dyntopo enable as first entry if nodes have been cleaned up
     * after sculpting on a different object than A, B.
     *
     * The steps are:
     * A dyntopo enable - sculpt
     * B dyntopo enable - sculpt - undo (A objects operators get cleaned up)
     * A sculpt (now A's log has a sculpt operator as first entry)
     *
     * Causing a cleanup at this point will call the code below, however
     * this will invalidate the state of the log since the deleted vertices
     * have been reclaimed already on step 2 (see BM_log_cleanup_entry)
     *
     * Also, design wise, a first entry should not have any deleted vertices since it
     * should not have anything to delete them -from-
     */
    // mesh_log_id_ghash_release(log, entry->deleted_faces);
    // mesh_log_id_ghash_release(log, entry->deleted_verts);
  }
  else if (!entry->next) {
    /* Release IDs of elements that are added by this entry. Since
     * the entry is at the end of the undo stack, and it's being
     * deleted, those elements can never be restored. Their IDs
     * can go back into the pool. */
    mesh_log_id_ghash_release(log, entry->added_faces);
    mesh_log_id_ghash_release(log, entry->added_verts);
  }
  else {
    lib_assert_msg(0, "Cannot drop MeshLogEntry from middle");
  }

  if (log->current_entry == entry) {
    log->current_entry = entry->prev;
  }

  mesh_log_entry_free(entry);
  lib_freelinkn(&log->entries, entry);
}

void mesh_log_undo(Mesh *mesh, MeshLog *log)
{
  MeshLogEntry *entry = log->current_entry;

  if (entry) {
    log->current_entry = entry->prev;

    /* Delete added faces and verts */
    mesh_log_faces_unmake(mesh, log, entry->added_faces);
    mesh_log_verts_unmake(mesh, log, entry->added_verts);

    /* Restore deleted verts and faces */
    mesh_log_verts_restore(mesh, log, entry->deleted_verts);
    mesh_log_faces_restore(mesh, log, entry->deleted_faces);

    /* Restore vertex coordinates, mask, and hflag */
    mesh_log_vert_values_swap(mesh, log, entry->modified_verts);
    mesh_log_face_values_swap(log, entry->modified_faces);
  }
}

void mesh_log_redo(Mesh *mesh, MeshLog *log)
{
  MeshLogEntry *entry = log->current_entry;

  if (!entry) {
    /* Currently at the beginning of the undo stack, move to first entry */
    entry = log->entries.first;
  }
  else if (entry->next) {
    /* Move to next undo entry */
    entry = entry->next;
  }
  else {
    /* Currently at the end of the undo stack, nothing left to redo */
    return;
  }

  log->current_entry = entry;

  if (entry) {
    /* Re-delete previously deleted faces and verts */
    mesh_log_faces_unmake(mesh, log, entry->deleted_faces);
    mesh_log_verts_unmake(mesh, log, entry->deleted_verts);

    /* Restore previously added verts and faces */
    mesh_log_verts_restore(mesh, log, entry->added_verts);
    mesh_log_faces_restore(mesh, log, entry->added_faces);

    /* Restore vertex coordinates, mask, and hflag */
    mesh_log_vert_values_swap(mesh, log, entry->modified_verts);
    mesh_log_face_values_swap(log, entry->modified_faces);
  }
}

void mesh_log_vert_before_modified(MeshLog *log, MeshVert *v, const int cd_vert_mask_offset)
{
  MeshLogEntry *entry = log->current_entry;
  MeshLogVert *lv;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);
  void **val_p;

  /* Find or create the MeshLogVert entry */
  if ((lv = lib_ghash_lookup(entry->added_verts, key))) {
    mesh_log_vert_meshvert_copy(lv, v, cd_vert_mask_offset);
  }
  else if (!lib_ghash_ensure_p(entry->modified_verts, key, &val_p)) {
    lv = mesh_log_vert_alloc(log, v, cd_vert_mask_offset);
    *val_p = lv;
  }
}

void mesh_log_vert_added(MeshLog *log, MeshVert *v, const int cd_vert_mask_offset)
{
  MeshLogVert *lv;
  uint v_id = range_tree_uint_take_any(log->unused_ids);
  void *key = PTR_FROM_UINT(v_id);

  mesh_log_vert_id_set(log, v, v_id);
  lv = mesh_log_vert_alloc(log, v, cd_vert_mask_offset);
  lib_ghash_insert(log->current_entry->added_verts, key, lv);
}

void mesh_log_face_modified(MeshLog *log, MeshFace *f)
{
  MeshLogFace *lf;
  uint f_id = mesh_log_face_id_get(log, f);
  void *key = PTR_FROM_UINT(f_id);

  lf = mesh_log_face_alloc(log, f);
  lib_ghash_insert(log->current_entry->modified_faces, key, lf);
}
void mesh_log_vert_removed(MeshLog *log, MeshVert *v, const int cd_vert_mask_offset)
{
  MeshLogEntry *entry = log->current_entry;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);

  /* if it has a key, it shouldn't be NULL */
  lib_assert(!!lib_ghash_lookup(entry->added_verts, key) ==
             !!lib_ghash_haskey(entry->added_verts, key));

  if (lib_ghash_remove(entry->added_verts, key, NULL, NULL)) {
    range_tree_uint_release(log->unused_ids, v_id);
  }
  else {
    MeshLogVert *lv, *lv_mod;

    lv = mesh_log_vert_alloc(log, v, cd_vert_mask_offset);
    lib_ghash_insert(entry->deleted_verts, key, lv);

    /* If the vertex was modified before deletion, ensure that the
     * original vertex values are stored */
    if ((lv_mod = lib_ghash_lookup(entry->modified_verts, key))) {
      (*lv) = (*lv_mod);
      lib_ghash_remove(entry->modified_verts, key, NULL, NULL);
    }
  }
}

void mesh_log_face_removed(MeshLog *log, MeshFace *f)
{
  MeshLogEntry *entry = log->current_entry;
  uint f_id = mesh_log_face_id_get(log, f);
  void *key = PTR_FROM_UINT(f_id);

  /* if it has a key, it shouldn't be NULL */
  lib_assert(!!lib_ghash_lookup(entry->added_faces, key) ==
             !!lib_ghash_haskey(entry->added_faces, key));

  if (lib_ghash_remove(entry->added_faces, key, NULL, NULL)) {
    range_tree_uint_release(log->unused_ids, f_id);
  }
  else {
    MeshLogFace *lf;

    lf = mesh_log_face_alloc(log, f);
    lib_ghash_insert(entry->deleted_faces, key, lf);
  }
}

void mesh_log_all_added(Mesh *mesh, MeshLog *log)
{
  const int cd_vert_mask_offset = CustomData_get_offset(&mesh->vdata, CD_PAINT_MASK);
  MeshIter bm_iter;
  MeshVert *v;
  MeshFace *f;

  /* avoid unnecessary resizing on initialization */
  if (lib_ghash_len(log->current_entry->added_verts) == 0) {
    lib_ghash_reserve(log->current_entry->added_verts, (uint)mesh->totvert);
  }

  if (lib_ghash_len(log->current_entry->added_faces) == 0) {
    lib_ghash_reserve(log->current_entry->added_faces, (uint)mesh->totface);
  }

  /* Log all vertices as newly created */
  MESH_ITER (v, &mesh_iter, mesh, MESH_VERTS_OF_MESH) {
    mesh_log_vert_added(log, v, cd_vert_mask_offset);
  }

  /* Log all faces as newly created */
  MESH_ITER (f, &mesh_iter, mesh, MESH_FACES_OF_MESH) {
    mesh_log_face_added(log, f);
  }
}

void mesh_log_before_all_removed(Mesh *mesh, MeshLog *log)
{
  const int cd_vert_mask_offset = CustomData_get_offset(&bm->vdata, CD_PAINT_MASK);
  MeshIter mesh_iter;
  MeshVert *v;
  MeshFace *f;

  /* Log deletion of all faces */
  MESH_ITER (f, &mesh_iter, mesh, MESH_FACES_OF_MESH) {
    mesh_log_face_removed(log, f);
  }

  /* Log deletion of all vertices */
  MESH_ITER (v, &mesh_iter, mesh, MESH_VERTS_OF_MESH) {
    mesh_log_vert_removed(log, v, cd_vert_mask_offset);
  }
}

const float *mesh_log_original_vert_co(MeshLog *log, MeshVert *v)
{
  MeshLogEntry *entry = log->current_entry;
  const MeshLogVert *lv;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);

  lib_assert(entry);

  lib_assert(lib_ghash_haskey(entry->modified_verts, key));

  lv = lib_ghash_lookup(entry->modified_verts, key);
  return lv->co;
}

const float *mesh_log_original_vert_no(MeshLog *log, MeshVert *v)
{
  MeshLogEntry *entry = log->current_entry;
  const MeshLogVert *lv;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);

  lib_assert(entry);

  lib_assert(lib_ghash_haskey(entry->modified_verts, key));

  lv = lib_ghash_lookup(entry->modified_verts, key);
  return lv->no;
}

float mesh_log_original_mask(MeshLog *log, MeshVert *v)
{
  MeshLogEntry *entry = log->current_entry;
  const MeshLogVert *lv;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);

  lib_assert(entry);

  lib_assert(lib_ghash_haskey(entry->modified_verts, key));

  lv = lib_ghash_lookup(entry->modified_verts, key);
  return lv->mask;
}

void mesh_log_original_vert_data(MeshLog *log, MeshVert *v, const float **r_co, const float **r_no)
{
  MeshLogEntry *entry = log->current_entry;
  const MeshLogVert *lv;
  uint v_id = mesh_log_vert_id_get(log, v);
  void *key = PTR_FROM_UINT(v_id);

  lib_assert(entry);

  lib_assert(lib_ghash_haskey(entry->modified_verts, key));

  lv = lib_ghash_lookup(entry->modified_verts, key);
  *r_co = lv->co;
  *r_no = lv->no;
}

/************************ Debugging and Testing ***********************/

MeshLogEntry *mesh_log_current_entry(MeshLog *log)
{
  return log->current_entry;
}

RangeTreeUInt *mesh_log_unused_ids(MeshLog *log)
{
  return log->unused_ids;
}

#if 0
/* Print the list of entries, marking the current one
 *
 * Keep around for debugging */
void mesh_log_print(const MeshLog *log, const char *description)
{
  const MeshLogEntry *entry;
  const char *current = " <-- current";
  int i;

  printf("%s:\n", description);
  printf("    % 2d: [ initial ]%s\n", 0, (!log->current_entry) ? current : "");
  for (entry = log->entries.first, i = 1; entry; entry = entry->next, i++) {
    printf("    % 2d: [%p]%s\n", i, entry, (entry == log->current_entry) ? current : "");
  }
}
#endif
