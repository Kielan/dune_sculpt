#pragma once

struct MeshFace;
struct MeshVert;
struct Mesh;
struct RangeTreeUInt;

typedef struct MeshLog MeshLog;
typedef struct MeshLogEntry MeshLogEntry;

/* Allocate and initialize a new MeshLog */
/* Allocate, initialize, and assign a new MeshLog */
MeshLog *mesh_log_create(Mesh *mesh);

/* Allocate and initialize a new MeshLog using existing MeshLogEntries */
/* Allocate and initialize a new MeshLog using existing MeshLogEntries
 *
 * The 'entry' should be the last entry in the BMLog. Its prev pointer
 * will be followed back to find the first entry.
 *
 * The unused IDs field of the log will be initialized by taking all
 * keys from all GHashes in the log entry.
 */
MeshLog *mesh_log_from_existing_entries_create(Mesh *mesh, MeshLogEntry *entry);

/* Free all the data in a MeshLog including the log itself */
/* Free all the data in a MeshLog including the log itself */
void mesh_log_free(MeshLog *log);

/* Get the number of log entries */
/* Get the number of log entries */
int mesh_log_length(const MeshLog *log);

/* Apply a consistent ordering to Mesh vertices and faces */
/* Apply a consistent ordering to Mesh vertices */
void mesh_log_mesh_elems_reorder(Mesh *meeh, MeshLog *log);

/* Start a new log entry and update the log entry list */
/* Start a new log entry and update the log entry list
 *
 * If the log entry list is empty, or if the current log entry is the
 * last entry, the new entry is simply appended to the end.
 *
 * Otherwise, the new entry is added after the current entry and all
 * following entries are deleted.
 *
 * In either case, the new entry is set as the current log entry.
 */
BMLogEntry *BM_log_entry_add(BMLog *log);

/* Mark all used ids as unused for this node */
void BM_log_cleanup_entry(BMLogEntry *entry);

/* Remove an entry from the log */
/* Remove an entry from the log
 *
 * Uses entry->log as the log. If the log is NULL, the entry will be
 * free'd but not removed from any list, nor shall its IDs be
 * released.
 *
 * This operation is only valid on the first and last entries in the
 * log. Deleting from the middle will assert.
 */
void mesh_log_entry_drop(MeshLogEntry *entry);

/* Undo one MeshLogEntry */
/* Undo one MeshLogEntry
 *
 * Has no effect if there's nothing left to undo */
void mesh_log_undo(Mesh *mesh, MeshLog *log);

/* Redo one MeshLogEntry */
/* Redo one MeshLogEntry
 *
 * Has no effect if there's nothing left to redo */
void mesh_log_redo(Mesh *mesh, MeshLog *log);

/* Log a vertex before it is modified */
/* Log a vertex before it is modified
 *
 * Before modifying vertex coordinates, masks, or hflags, call this
 * function to log its current values. This is better than logging
 * after the coordinates have been modified, because only those
 * vertices that are modified need to have their original values
 * stored.
 *
 * Handles two separate cases:
 *
 * If the vertex was added in the current log entry, update the
 * vertex in the map of added vertices.
 *
 * If the vertex already existed prior to the current log entry, a
 * separate key/value map of modified vertices is used (using the
 * vertex's ID as the key). The values stored in that case are
 * the vertex's original state so that an undo can restore the
 * previous state.
 *
 * On undo, the current vertex state will be swapped with the stored
 * state so that a subsequent redo operation will restore the newer
 * vertex state.
 */
void mesh_log_vert_before_modified(MeshLog *log, struct MeshVert *v, int cd_vert_mask_offset);

/* Log a new vertex as added to the Mesh */
/* Log a new vertex as added to the Mesh
 *
 * The new vertex gets a unique ID assigned. It is then added to a map
 * of added vertices, with the key being its ID and the value
 * containing everything needed to reconstruct that vertex.
 */
void mesh_log_vert_added(BMLog *log, struct BMVert *v, int cd_vert_mask_offset);

/* Log a face before it is modified */
/* Log a face before it is modified
 *
 * This is intended to handle only header flags and we always
 * assume face has been added before
 */
void mesh_log_face_modified(BMLog *log, struct BMFace *f);

/* Log a new face as added to the BMesh */
/* Log a new face as added to the BMesh
 *
 * The new face gets a unique ID assigned. It is then added to a map
 * of added faces, with the key being its ID and the value containing
 * everything needed to reconstruct that face.
 */
void mesh_log_face_added(MeshLog *log, struct MeshFace *f);

/* Log a vertex as removed from the Mesh */
/* Log a vertex as removed from the Mesh
 *
 * A couple things can happen here:
 *
 * If the vertex was added as part of the current log entry, then it's
 * deleted and forgotten about entirely. Its unique ID is returned to
 * the unused pool.
 *
 * If the vertex was already part of the BMesh before the current log
 * entry, it is added to a map of deleted vertices, with the key being
 * its ID and the value containing everything needed to reconstruct
 * that vertex.
 *
 * If there's a move record for the vertex, that's used as the
 * vertices original location, then the move record is deleted.
 */
void mesh_log_vert_removed(MeshLog *log, struct MeshVert *v, int cd_vert_mask_offset);

/* Log a face as removed from the Mesh */
/* Log a face as removed from the Mesh
 *
 * A couple things can happen here:
 *
 * If the face was added as part of the current log entry, then it's
 * deleted and forgotten about entirely. Its unique id is returned to
 * the unused pool.
 *
 * If the face was already part of the Mesh before the current log
 * entry, it is added to a map of deleted faces, with the key being
 * its id and the value containing everything needed to reconstruct
 * that face.
 */
void mesh_log_face_removed(MeshLog *log, struct MeshFace *f);

/* Log all vertices/faces in the Mesh as added */
/* Log all vertices/faces in the Mesh as added */
void mesh_log_all_added(Mesh *mesh, MeshLog *log);

/* Log all vertices/faces in the Mesh as removed */
/* Log all vertices/faces in the Mesh as removed */
void mesh_log_before_all_removed(Mesh *mesh, MeshLog *log);

/* Get the logged coordinates of a vertex */
/* Get the logged coordinates of a vertex
 *
 * Does not modify the log or the vertex */
const float *mesh_log_original_vert_co(MeshLog *log, MeshVert *v);

/* Get the logged normal of a vertex
 *
 * Does not modify the log or the vertex */
const float *mesh_og_original_vert_no(MeshLog *log, MeshVert *v);

/* Get the logged mask of a vertex */
/* Get the logged mask of a vertex
 *
 * Does not modify the log or the vertex */
float mesh_log_original_mask(MeshLog *log, MeshVert *v);

/* Get the logged data of a vertex (avoid multiple lookups) */
void mesh_log_original_vert_data(MeshLog *log, MeshVert *v, const float **r_co, const float **r_no);

/* For internal use only (unit testing) */
/* For internal use only (unit testing) */
MeshLogEntry *mesh_log_current_entry(MeshLog *log);
/* For internal use only (unit testing) */
struct RangeTreeUInt *mesh_log_unused_ids(MeshLog *log);
