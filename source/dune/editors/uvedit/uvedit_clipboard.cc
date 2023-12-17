/* Attempt to find a graph isomorphism between the topology of two different UV islands.
 * On terminology, for the purposes of this file:
 * * An iso_graph is a "Graph" in Graph Theory.
 *  * An iso_graph has an unordered set of iso_verts.
 *  * An iso_graph has an unordered set of iso_edges.
 * * An iso_vert is a "Vertex" in Graph Theory
 *   * Each iso_vert has a label.
 * * An iso_edge is an "Edge" in Graph Theory
 *   * Each iso_edge connects two iso_verts.
 *   * An iso_edge is undirected. */

#include "dune_cxt.hh"
#include "dune_customdata.hh"
#include "dune_meshedit.hh"
#include "dune_layer.h"
#include "dune_mesh_mapping.hh" /* UvElementMap */
#include "dune_report.h"

#include "graph.hh"

#include "ed_mesh.hh"
#include "ed_screen.hh"
#include "ed_uvedit.hh" /* Own include. */

#include "win_api.hh"

#include "uvedit_clipboard_graph_iso.hh"
#include "uvedit_intern.h" /* linker, extern "C" */

void uv_clipboard_free();

class UVClipboardBuf {
 public:
  ~UVClipboardBuf();

  void append(UvElementMap *element_map, const int cd_loop_uv_offset);
  /* return True when found. */
  bool find_isomorphism(UvElementMap *dest_element_map,
                        int island_index,
                        int cd_loop_uv_offset,
                        dune::Vector<int> &r_label,
                        bool *r_search_abandoned);

  void write_uvs(UvElementMap *element_map,
                 int island_index,
                 const int cd_loop_uv_offset,
                 const dune::Vector<int> &label);

 private:
  dune::Vector<GraphISO *> graph;
  dune::Vector<int> offset;
  dune::Vector<std::pair<float, float>> uv;
};

static UVClipboardBuf *uv_clipboard = nullptr;

UVClipboardBuf::~UVClipboardBuf()
{
  for (const int64_t index : graph.index_range()) {
    delete graph[index];
  }
  graph.clear();
  offset.clear();
  uv.clear();
}

/* Given a `MeshLoop`, possibly belonging to an island in a `UvElementMap`,
 * return the `iso_index` corresponding to it's representation
 * in the `iso_graph`.
 *
 * If the `MeshLoop` is not part of the `iso_graph`, return -1. */
static int iso_index_for_loop(const MeshLoop *loop,
                              UvElementMap *element_map,
                              const int island_index)
{
  UvElement *element = mesh_uv_element_get(element_map, loop);
  if (!element) {
    return -1; /* Either unselected, or a different island. */
  }
  const int index = mesh_uv_element_get_unique_index(element_map, element);
  const int base_index = mesh_uv_element_get_unique_index(
      element_map, element_map->storage + element_map->island_indices[island_index]);
  return index - base_index;
}

/* Add an `iso_edge` to an `iso_graph` between two MeshLoops. */
static void add_iso_edge(
    GraphISO *graph, MeshLoop *loop_v, MeshLoop *loop_w, UvElementMap *element_map, int island_index)
{
  lib_assert(loop_v->f == loop_w->f); /* Ensure on the same face. */
  const int index_v = iso_index_for_loop(loop_v, element_map, island_index);
  const int index_w = iso_index_for_loop(loop_w, element_map, island_index);
  lib_assert(index_v != index_w);
  if (index_v == -1 || index_w == -1) {
    return; /* Unselected. */
  }

  lib_assert(0 <= index_v && index_v < graph->n);
  lib_assert(0 <= index_w && index_w < graph->n);

  graph->add_edge(index_v, index_w);
}

/* Build an `iso_graph` representation of an island of a `UvElementMap`.
 */
static GraphISO *build_iso_graph(UvElementMap *element_map,
                                 const int island_index,
                                 int /*cd_loop_uv_offset*/)
{
  GraphISO *g = new GraphISO(element_map->island_total_unique_uvs[island_index]);
  for (int i = 0; i < g->n; i++) {
    g->label[i] = i;
  }

  const int i0 = element_map->island_indices[island_index];
  const int i1 = i0 + element_map->island_total_uvs[island_index];

  /* Add iso_edges. */
  for (int i = i0; i < i1; i++) {
    const UvElement *element = element_map->storage + i;
    /* Look forward around the current face. */
    add_iso_edge(g, element->l, element->l->next, element_map, island_index);

    /* Look backward around the current face.
     * (Required for certain vertex selection cases.)
     */
    add_iso_edge(g, element->l->prev, element->l, element_map, island_index);
  }

  /* TODO: call g->sort_vertices_by_degree() */

  return g;
}

/* Convert each island inside an `element_map` into an `iso_graph`, and append them to the
 * clipboard buf. */
void UV_ClipboardBuf::append(UvElementMap *element_map, const int cd_loop_uv_offset)
{
  for (int island_index = 0; island_index < element_map->total_islands; island_index++) {
    offset.append(uv.size());
    graph.append(build_iso_graph(element_map, island_index, cd_loop_uv_offset));

    /* TODO: Consider iterating over `mesh_uv_element_map_ensure_unique_index` instead. */
    for (int j = 0; j < element_map->island_total_uvs[island_index]; j++) {
      UvElement *element = element_map->storage + element_map->island_indices[island_index] + j;
      if (!element->separate) {
        continue;
      }
      float *luv = MESH_ELEM_CD_GET_FLOAT_P(element->l, cd_loop_uv_offset);
      uv.append(std::make_pair(luv[0], luv[1]));
    }
  }
}

/* Write UVs back to an island. */
void UVClipboardBuf::write_uvs(UvElementMap *element_map,
                               int island_index,
                               const int cd_loop_uv_offset,
                               const dune::Vector<int> &label)
{
  lib_assert(label.size() == element_map->island_total_unique_uvs[island_index]);

  /* TODO: Consider iterating over `BM_uv_element_map_ensure_unique_index` instead. */
  int unique_uv = 0;
  for (int j = 0; j < element_map->island_total_uvs[island_index]; j++) {
    int k = element_map->island_indices[island_index] + j;
    UvElement *element = element_map->storage + k;
    if (!element->separate) {
      continue;
    }
    lob_assert(0 <= unique_uv);
    lib_assert(unique_uv < label.size());
    const std::pair<float, float> &src_uv = uv_clipboard->uv[label[unique_uv]];
    while (element) {
      float *luv = MESH_ELEM_CD_GET_FLOAT_P(element->l, cd_loop_uv_offset);
      luv[0] = src_uv.first;
      luv[1] = src_uv.second;
      element = element->next;
      if (!element || element->separate) {
        break;
      }
    }
    unique_uv++;
  }
  lib_assert(unique_uv == label.size());
}

/* Call the external isomorphism solver.
 * return True when found. */
static bool find_isomorphism(UvElementMap *dest,
                             const int dest_island_index,
                             GraphISO *graph_src,
                             const int cd_loop_uv_offset,
                             dune::Vector<int> &r_label,
                             bool *r_search_abandoned)
{

  const int island_total_unique_uvs = dest->island_total_unique_uvs[dest_island_index];
  if (island_total_unique_uvs != graph_source->n) {
    return false; /* Isomorphisms can't differ in |iso_vert|. */
  }
  r_label.resize(island_total_unique_uvs);

  GraphISO *graph_dest = build_iso_graph(dest, dest_island_index, cd_loop_uv_offset);

  int(*solution)[2] = (int(*)[2])mem_malloc(graph_source->n * sizeof(*solution), __func__);
  int solution_length = 0;
  const bool found = ed_uvedit_clipboard_max_common_subgraph(
      graph_src, graph_dest, solution, &solution_length, r_search_abandoned);

  /* Todo: Implement "Best Effort" / "Nearest Match" paste functionality here. */

  if (found) {
    BLI_assert(solution_length == dest->island_total_unique_uvs[dest_island_index]);
    for (int i = 0; i < solution_length; i++) {
      int index_s = solution[i][0];
      int index_t = solution[i][1];
      lib_assert(0 <= index_s && index_s < solution_length);
      lib_assert(0 <= index_t && index_t < solution_length);
      r_label[index_t] = index_s;
    }
  }

  MEM_SAFE_FREE(solution);
  delete graph_dest;
  return found;
}

bool UVClipboardBuf::find_isomorphism(UvElementMap *dest_element_map,
                                          const int dest_island_index,
                                          const int cd_loop_uv_offset,
                                          dune::Vector<int> &r_label,
                                          bool *r_search_abandoned)
{
  for (const int64_t source_island_index : graph.index_range()) {
    if (::find_isomorphism(dest_element_map,
                           dest_island_index,
                           graph[src_island_index],
                           cd_loop_uv_offset,
                           r_label,
                           r_search_abandoned))
    {
      const int island_total_unique_uvs =
          dest_element_map->island_total_unique_uvs[dest_island_index];
      const int island_offset = offset[src_island_index];
      lib_assert(island_total_unique_uvs == r_label.size());
      for (int i = 0; i < island_total_unique_uvs; i++) {
        r_label[i] += island_offset; /* TODO: (minor optimization) Defer offset. */
      }

      /* TODO: There may be more than one match. How to choose between them? */
      return true;
    }
  }

  return false;
}

static int uv_copy_ex(Cxt *C, WinOp * /*op*/)
{
  uv_clipboard_free();
  uv_clipboard = new UVClipboardBuf();

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Scene *scene = cxt_data_scene(C);

  uint obs_len = 0;
  Ob **obs = dune_view_layer_arr_from_obs_in_edit_mode_unique_data_w_uvs(
      scene, view_layer, ((View3D *)nullptr), &obs_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Ob *ob = obs[ob_index];
    MeshEdit *me = dune_meshedit_from_ob(ob);

    const bool use_seams = false;
    UvElementMap *element_map = mesh_uv_element_map_create(
        em->bm, scene, true, false, use_seams, true);
    if (element_map) {
      const int cd_loop_uv_offset = CustomData_get_offset(&me->mesh->ldata, CD_PROP_FLOAT2);
      uv_clipboard->append(element_map, cd_loop_uv_offset);
    }
    mesh_uv_element_map_free(element_map);
  }

  mem_free(obs);

  /* TODO: Serialize `UvClipboard` to system clipboard. */
  return OP_FINISHED;
}

static int uv_paste_exec(Cxt *C, WinOp *op)
{
  /* TODO: Restore `UvClipboard` from sys clipboard. */
  if (!uv_clipboard) {
    return OPERATOR_FINISHED; /* Nothing to do. */
  }
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Scene *scene = cxt_data_scene(C);

  uint obs_len = 0;
  Ob **ob = dune_view_layer_arr_from_obs_in_edit_mode_unique_data_with_uvs(
      scene, view_layer, ((View3D *)nullptr), &objects_len);

  bool changed_multi = false;
  int complicated_search = 0;
  int total_search = 0;
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Ob *ob = obs[ob_index];
    MeshEdit *em = dune_meshedit_from_ob(ob);

    const bool use_seams = false;
    const int cd_loop_uv_offset = CustomData_get_offset(&em->bm->ldata, CD_PROP_FLOAT2);

    UvElementMap *dest_element_map = mesh_uv_element_map_create(
        em->mesh, scene, true, false, use_seams, true);

    if (!dest_element_map) {
      continue;
    }

    bool changed = false;

    for (int i = 0; i < dest_element_map->total_islands; i++) {
      total_search++;
      dune::Vector<int> label;
      bool search_abandoned = false;
      const bool found = uv_clipboard->find_isomorphism(
          dest_element_map, i, cd_loop_uv_offset, label, &search_abandoned);
      if (!found) {
        if (search_abandoned) {
          complicated_search++;
        }
        continue; /* No source UVs can be found that is isomorphic to this island. */
      }

      uv_clipboard->write_uvs(dest_element_map, i, cd_loop_uv_offset, label);
      changed = true; /* UVs were moved. */
    }

    mesh_uv_element_map_free(dest_element_map);

    if (changed) {
      changed_multi = true;

      graph_id_tag_update(static_cast<Id *>(ob->data), 0);
      win_ev_add_notifier(C, NC_GEOM | ND_DATA, ob->data);
    }
  }

  if (complicated_search) {
    dune_reportf(op->reports,
                RPT_WARNING,
                "Skipped %d of %d island(s), geometry was too complicated to detect a match",
                complicated_search,
                total_search);
  }

  mem_free(obs);

  return changed_multi ? OP_FINISHED : OPERATOR_CANCELLED;
}

void uv_ot_copy(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy UVs";
  ot->description = "Copy sel UV vertices";
  ot->idname = "UV_OT_copy";
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* api cbs */
  ot->ex = uv_copy_ex;
  ot->poll = ed_op_uvedit;
}

void uv_ot_paste(WinOpType *ot)
{
  /* ids */
  ot->name = "Paste UVs";
  ot->description = "Paste selected UV vertices";
  ot->idname = "UV_OT_paste";
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* api cbs */
  ot->ex = uv_paste_ex;
  ot->poll = ed_op_uvedit;
}

void UV_clipboard_free()
{
  delete uv_clipboard;
  uv_clipboard = nullptr;
}
