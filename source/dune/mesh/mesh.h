#pragma once

/* Mesh is a non-manifold boundary representation
 * designed to support advanced editing operations.
 * m_structure The Structure
 *
 * Mesh stores topology in four main element structures:
 *
 * - Faces - MeshFace
 * - Loops - MeshLoop, (stores per-face-vertex data, UV's, vertex-colors, etc)
 * - Edges - MeshEdge
 * - Verts - MeshVert
 * subsection m_header_flags Header Flags
 * Each element (vertex/edge/face/loop)
 * in a mesh has an associated bit-field called "header flags".
 *
 * MeshHeader flags should **never** be read or written to by bmesh operators (see Operators below).
 *
 * Access to header flags is done with `mesh_elem_flag_*()` functions.
 * subsection mesh_faces Faces
 *
 * Faces in Mesh are stored as a circular linked list of loops. Loops store per-face-vertex data
 * (among other things outlined later in this document), and define the face boundary.
 * subsection mesh_loop The Loop
 *
 * Loops can be thought of as a *face-corner*, since faces don't reference verts or edges directly.
 * Each loop connects the face to one of its corner vertices,
 * and also references an edge which connects this loop's vertex to the next loop's vertex.
 *
 * Loops allow faces to access their verts and edges,
 * while edges and faces store their loops, allowing access in the opposite direction too.
 *
 * Loop pointers:
 *
 * - MeshLoop#v - pointer to the vertex associated with this loop.
 * - MeshLoop#e - pointer to the edge associated with this loop,
 *   between verts `(loop->v, loop->next->v)`
 * - MeshLoop#f - pointer to the face associated with this loop.
 * subsection m_two_side_face 2-Sided Faces
 *
 * There are some situations where you need 2-sided faces (e.g. a face of two vertices).
 * This is supported by Mesh, but note that such faces should only be used as intermediary steps,
 * and should not end up in the final mesh.
 * mesh_edges_and_verts Edges and Vertices
 *
 * Edges and Verts in Mesh are primitive structures.
 *
 * There can be more than one edge between two vertices in Mesh,
 * though the rest of Dune (i.e. DNA and evaluated Mesh) does not support this.
 * So it should only occur temporarily during editing operations.
 * m_queries Queries
 *
 * The following topological queries are available:
 *
 * - Edges/Faces/Loops around a vertex.
 * - Faces around an edge.
 * - Loops around an edge.
 *
 * These are accessible through the iterator api, which is covered later in this document
 *
 * See source/dune/mesh/mesh_query.h for more misc. queries.
 * The Mesh API
 *
 * One of the goals of the Mesh API is to make it easy
 * and natural to produce highly maintainable code.
 * Code duplication, etc are avoided where possible.
 * m_iter_api Iterator API
 *
 * Most topological queries in Mesh go through an iterator API (see Queries above).
 * These are defined in mesh_iterators.h.
 * If you can, please use the MESH_ITER_MESH, MESH_ITER_ELEM macros in mesh_iters.h
 * subsectionm_walker_api Walker API
 *
 * Topological queries that require a stack (e.g. recursive queries) go through the Walker API,
 * which is defined in bmesh_walkers.h. Currently the "walkers" are hard-coded into the API,
 * though a mechanism for plugging in new walkers needs to be added at some point.
 *
 * Most topological queries should go through these two APIs;
 * there are additional fns you can use for topological iter,
 * but their meant for internal mesh code.
 *
 * The walker API supports delimiter flags,
 * to allow the caller to flag elems not to walk past.
 * subsection m_ops Ops
 *
 * Ops are an integral part of Mesh. Unlike regular dune ops,
 *Mesh ops **mo's** are designed to be nested (e.g. call other ops).
 *
 * Each op has a number of input/output "slots"
 * which are used to pass settings & data into/out of the op
 * (and allows for chaining ops together).
 *
 * These slots are identified by name, using strings.
 *
 * Access to slots is done with `mesh_op_slot_***()` fns.
 * m_tool_flags Tool Flags
 *
 * The Mesh API provides a set of flags for faces, edges and vertices,
 * which are private to an operator.
 * These flags may be used by the client operator code as needed
 * (a common example is flagging elements for use in another operator).
 * Each call to an operator allocates its own set of tool flags when it's executed,
 * avoiding flag conflicts between operators.
 *
 * These flags should not be confused with header flags, which are used to store persistent flags
 * (e.g. selection, hide status, etc).
 *
 * Access to tool flags is done with `mop_elem_flag_***()` functions.
 *
 * Operators are **never** allowed to read or write to header flags.
 * They act entirely on the data inside their input slots.
 * For example an operator should not check the selected state of an element,
 * there are some exceptions to this - some operators check of a face is smooth.
 * m_slot_types Slot Types
 *
 * The following slot types are available:
 *
 * - integer - MESH_OP_SLOT_INT
 * - boolean - MESH_OP_SLOT_BOOL
 * - float   - MESH_OP_SLOT_FLT
 * - pointer - MESH_OP_SLOT_PTR
 * - matrix  - MESH_OP_SLOT_MAT
 * - vector  - MESH_OP_SLOT_VEC
 * - buffer  - MESH_OP_SLOT_ELEMENT_BUF - a list of verts/edges/faces.
 * - map     - MEEH_OP_SLOT_MAPPING - simple hash map.
 * mesh_slot_iter Slot Iterators
 *
 * Access to element buffers or maps must go through the slot iterator api,
 * defined in bmesh_operators.h.
 * Use MESH_OP_ITER where ever possible.
 * mesh_elem_buf Element Buffers
 *
 * The element buffer slot type is used to feed elements (verts/edges/faces) to operators.
 * Internally they are stored as pointer arrays (which happily has not caused any problems so far).
 * Many operators take in a buffer of elements, process it,
 * then spit out a new one; this allows operators to be chained together.
 *
 * Element buffers may have elements of different types within the same buffer
 * (this is supported by the API.
 * mesh_fname Function Naming Conventions
 *
 * These conventions should be used throughout the mesh module.
 *
 * - `mesh_kernel_*()` - Low level API, for primitive functions that others are built ontop of.
 * - `mesh_***()` - Low level API function.
 * - `mmesh_static_***()` -     'static' functions, not a part of the API at all,
 *   but use prefix since they operate on Mesh data.
 * - `mesh_***()` -     High level Mesh API function for use anywhere.
 * - `meshop_***()` -    High level operator API function for use anywhere.
 * - `meshop_***()` -    Low level / internal operator API functions.
 * - `_dmesh_***()` -    Functions which are called via macros only.
 *
 * mesh_todo Mesh TODO's
 *
 * There may be a better place for this section, but adding here for now.
 *
 * mesh_todo_optimize Optimizations
 *
 * - Skip normal calc when its not needed
 *   (when calling chain of operators & for modifiers, flag as dirty)
 * - Skip MO flag allocation, its not needed in many cases,
 *   this is fairly redundant to calc by default.
 * - Ability to call MO's with option not to create return data (will save some time)
 * - Binary diff UNDO, currently this uses huge amount of ram
 *   when all shapes are stored for each undo step for eg.
 * - Use two diff iterator types for BMO map/buffer types. */

#include "types_customdata.h" /* BMesh struct in bmesh_class.h uses */
#include "types_listBase.h"   /* selection history uses */

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mesh_class.h"

/* include the rest of the API */
#include "intern/mesh_error.h"
#include "intern/mesh_op_api.h"

#include "intern/mesh_cb_generic.h"
#include "intern/mesh_construct.h"
#include "intern/mesh_core.h"
#include "intern/mesh_delete.h"
#include "intern/mesh_edgeloop.h"
#include "intern/mesh_interp.h"
#include "intern/mesh_iterators.h"
#include "intern/mesh_log.h"
#include "intern/mesh_marking.h"
#include "intern/mesh_mesh.h"
#include "intern/mesh_mesh_convert.h"
#include "intern/mesh_mesh_debug.h"
#include "intern/mesh_mesh_duplicate.h"
#include "intern/mesh_mesh_normals.h"
#include "intern/mesh_mesh_partial_update.h"
#include "intern/mesh_mesh_tessellate.h"
#include "intern/mesh_mesh_validate.h"
#include "intern/mesh_mods.h"
#include "intern/mesh_operators.h"
#include "intern/mesh_polygon.h"
#include "intern/mesh_polygon_edgenet.h"
#include "intern/mesh_query.h"
#include "intern/mesh_query_uv.h"
#include "intern/mesh_walkers.h"

#include "intern/mesh_inline.h"

#ifdef __cplusplus
}
#endif
