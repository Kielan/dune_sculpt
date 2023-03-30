/**
 *
 * Datatypes for internal use in the Depsgraph
 *
 * All of these datatypes are only really used within the "core" depsgraph.
 * In particular, node types declared here form the structure of operations
 * in the graph.
 */

#pragma once

#include <functional>

/* TODO: Ideally we'll just use char* and statically allocated strings
 * to avoid any possible overhead caused by string (re)allocation/formatting. */
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "lib_map.hh"
#include "lib_set.hh"
#include "lib_string_ref.hh"
#include "lib_vector.hh"
#include "lib_vector_set.hh"

struct Graph;

struct CustomData_MeshMasks;

namespace dune {
namespace graph {

/* Commonly used types. */
using std::deque;
using std::optional;
using std::pair;
using std::string;
using std::unique_ptr;

/* Commonly used functions. */
using std::make_pair;
using std::max;
using std::to_string;

/* Function bindings. */
using std::function;
using namespace std::placeholders;
#define function_bind std::bind

/* Source of the dependency graph node update tag.
 *
 * NOTE: This is a bit mask, so accumulation of sources is possible.
 *
 * TODO: Find a better place for this. */
enum eUpdateSource {
  /* Update is caused by a time change. */
  GRAPH_UPDATE_SOURCE_TIME = (1 << 0),
  /* Update caused by user directly or indirectly influencing the node. */
  GRAPH_UPDATE_SOURCE_USER_EDIT = (1 << 1),
  /* Update is happening as a special response for the relations update. */
  GRAPH_UPDATE_SOURCE_RELATIONS = (1 << 2),
  /* Update is happening due to visibility change. */
  GRAPH_UPDATE_SOURCE_VISIBILITY = (1 << 3),
};

/* C++ wrapper around DNA's CustomData_MeshMasks struct. */
struct GraphCustomDataMeshMasks {
  uint64_t vert_mask;
  uint64_t edge_mask;
  uint64_t face_mask;
  uint64_t loop_mask;
  uint64_t poly_mask;

  GraphCustomDataMeshMasks() : vert_mask(0), edge_mask(0), face_mask(0), loop_mask(0), poly_mask(0)
  {
  }

  explicit GraphCustomDataMeshMasks(const CustomDataMeshMasks *other);

  GraphCustomDataMeshMasks &operator|=(const GraphCustomDataMeshMasks &other)
  {
    this->vert_mask |= other.vert_mask;
    this->edge_mask |= other.edge_mask;
    this->face_mask |= other.face_mask;
    this->loop_mask |= other.loop_mask;
    this->poly_mask |= other.poly_mask;
    return *this;
  }

  GraphCustomDataMeshMasks operator|(const GraphCustomDataMeshMasks &other) const
  {
    GraphCustomDataMeshMasks result;
    result.vert_mask = this->vert_mask | other.vert_mask;
    result.edge_mask = this->edge_mask | other.edge_mask;
    result.face_mask = this->face_mask | other.face_mask;
    result.loop_mask = this->loop_mask | other.loop_mask;
    result.poly_mask = this->poly_mask | other.poly_mask;
    return result;
  }

  bool operator==(const GraphCustomDataMeshMasks &other) const
  {
    return (this->vert_mask == other.vert_mask && this->edge_mask == other.edge_mask &&
            this->face_mask == other.face_mask && this->loop_mask == other.loop_mask &&
            this->poly_mask == other.poly_mask);
  }

  bool operator!=(const GraphCustomDataMeshMasks &other) const
  {
    return !(*this == other);
  }

  static GraphCustomDataMeshMasks MaskVert(const uint64_t vert_mask)
  {
    GraphCustomDataMeshMasks result;
    result.vert_mask = vert_mask;
    return result;
  }

  static GraphCustomDataMeshMasks MaskEdge(const uint64_t edge_mask)
  {
    GraphCustomDataMeshMasks result;
    result.edge_mask = edge_mask;
    return result;
  }

  static GraphCustomDataMeshMasks MaskFace(const uint64_t face_mask)
  {
    GraphCustomDataMeshMasks result;
    result.face_mask = face_mask;
    return result;
  }

  static GraphCustomDataMeshMasks MaskLoop(const uint64_t loop_mask)
  {
    GraphCustomDataMeshMasks result;
    result.loop_mask = loop_mask;
    return result;
  }

  static GraphCustomDataMeshMasks MaskPoly(const uint64_t poly_mask)
  {
    GraphCustomDataMeshMasks result;
    result.poly_mask = poly_mask;
    return result;
  }
};

}  // namespace dgraph
}  // namespace dune
