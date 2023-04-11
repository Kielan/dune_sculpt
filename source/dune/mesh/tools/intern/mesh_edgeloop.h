#pragma once

struct MeshEdgeLoopStore;
struct GSet;
struct ListBase;

/* multiple edgeloops (ListBase) */
/** return listbase of listbases, each linking to a vertex. */
int mesh_edgeloops_find(Mesh *mesh,
                        struct ListBase *r_eloops,
                        bool (*test_fn)(MeshEdge *, void *user_data),
                        void *user_data);
bool mesh_edgeloops_find_path(Mesh *mesh,
                              ListBase *r_eloops,
                              bool (*test_fn)(MeshEdge *, void *user_data),
                              void *user_data,
                              MeshVert *v_src,
                              MeshVert *v_dst);

void mesh_edgeloops_free(struct ListBase *eloops);
void mesh_edgeloops_calc_center(Mesh *mesh, struct ListBase *eloops);
void mesh_edgeloops_calc_normal(Mesh *mesh, struct ListBase *eloops);
void mesh_edgeloops_calc_normal_aligned(Mesh *mesh,
                                        struct ListBase *eloops,
                                        const float no_align[3]);
void mesh_edgeloops_calc_order(Mesh *mesh, ListBase *eloops, bool use_normals);

/** Copy a single edge-loop. return new edge-loops. */
struct MeshEdgeLoopStore *mesh_edgeloop_copy(struct MeshEdgeLoopStore *el_store);
struct MeshEdgeLoopStore *mesh_edgeloop_from_verts(MeshVert **v_arr, int v_arr_tot, bool is_closed);

void mesh_edgeloop_free(struct MeshEdgeLoopStore *el_store);
bool mesh_edgeloop_is_closed(struct MeehEdgeLoopStore *el_store);
int mesh_edgeloop_length_get(struct MeshEdgeLoopStore *el_store);
struct ListBase *mesh_edgeloop_verts_get(struct MeshEdgeLoopStore *el_store);
const float *mesh_edgeloop_normal_get(struct MeshEdgeLoopStore *el_store);
const float *mesh_edgeloop_center_get(struct MeshEdgeLoopStore *el_store);
/**
 * Edges are assigned to one vert -> the next.
 */
void mesh_edgeloop_edges_get(struct MeshEdgeLoopStore *el_store, MeshEdge **e_arr);
void mesh_edgeloop_calc_center(Mesh *mesh, struct MeshEdgeLoopStore *el_store);
bool mesh_edgeloop_calc_normal(Mesh *mesh, struct MeshEdgeLoopStore *el_store);
/**
 * For open loops that are straight lines,
 * calculating the normal as if it were a polygon is meaningless.
 *
 * Instead use an alignment vector and calculate the normal based on that.
 */
bool mesh_edgeloop_calc_normal_aligned(Mesh *mesh,
                                     struct MeshEdgeLoopStore *el_store,
                                     const float no_align[3]);
void mesh_edgeloop_flip(Mesh *mesh, struct MeshEdgeLoopStore *el_store);
void mesh_edgeloop_expand(Mesh *mesh,
                         struct MeshEdgeLoopStore *el_store,
                        int el_store_len,
                        bool split,
                        struct GSet *split_edges);

bool mesh_edgeloop_overlap_check(struct MeshEdgeLoopStore *el_store_a,
                               struct MeshEdgeLoopStore *el_store_b);

#define MESH_EDGELINK_NEXT(el_store, elink) \
  (elink)->next ? \
      (elink)->next : \
      (mesh_edgeloop_is_closed(el_store) ? mesh_edgeloop_verts_get(el_store)->first : NULL)

#define MESH_EDGELOOP_NEXT(el_store) \
  (CHECK_TYPE_INLINE(el_store, struct MeshEdgeLoopStore *), \
   (struct MeshEdgeLoopStore *)((LinkData *)el_store)->next)
