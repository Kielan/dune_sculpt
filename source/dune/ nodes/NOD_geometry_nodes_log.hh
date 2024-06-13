/* Many geo nodes related UI features need access to data produced during eval. Not only
 * is the final output required but also the intermediate results. Those features include attribute
 * search, node warnings, socket inspection and the viewer node.
 *
 * This file provides the sys for logging data during eval and accessing the data after
 * eval. Geo nodes is ex by a mod, therefore the "root" of logging is
 * GeoModLog which will contain all data generated in a mod.
 *
 * The sys makes a distinction between "loggers" and the "log":
 * - Logger (GeoTreeLogger): Is used during geo nodes eval. Each thread logs data
 *   independently to avoid communication between threads. Logging should generally be fast.
 *   Generally, the logged data is just dumped into simple containers. Any processing of the data
 *   happens later if necessary. This is important for performance, bc in practice, most of
 *   the logged data is never used again. So any processing of the data is likely to be a waste of
 *   resources.
 * - Log (GeoTreeLog, GeoNodeLog): Those are used when accessing logged data in UI code. They
 *   contain and cache preprocessed data produced during logging. The log combines data from all
 *   thread-local loggers to provide simple access. Importantly, the (preprocessed) log is only
 *   created when it is actually used by UI code.
 */

#pragma once

#include <chrono>

#include "lib_compute_cxt.hh"
#include "lib_enumerable_thread_specific.hh"
#include "lib_generic_ptr.hh"
#include "lib_multi_value_map.hh"

#include "dune_geo_set.hh"
#include "dune_node_tree_zones.hh"
#include "dune_viewer_path.hh"

#include "fn_field.hh"

#include "types_node.h"

struct SpaceNode;

namespace dune::nodes::geo_eval_log {

using fn::GField;

enum class NodeWarningType {
  Error,
  Warning,
  Info,
};

struct NodeWarning {
  NodeWarningType type;
  std::string message;
};

enum class NamedAttrUsage {
  None = 0,
  Read = 1 << 0,
  Write = 1 << 1,
  Remove = 1 << 2,
};
ENUM_OPS(NamedAttrUsage, NamedAttrUsage::Remove);

/* Vals of diff types are logged diff. Necessary bc some types are so
 * simple that we can log them entirely (e.g. `int`), while we don't want to log all intermediate
 * geometries in their entirety.
 *
 * ValLog is a base class for the diff ways we log vals. */
class ValLog {
 public:
  virtual ~ValLog() = default;
};

/* Simplest logger. It just stores a copy of the entire val. This is used for most simple types
 * like `int`. */
class GenericValLog : public ValLog {
 public:
  /* This is owning the val, but not the mem. */
  GMutablePtr val;

  GenericValLog(const GMutablePtr val) : val(val) {}

  ~GenericValLog();
};

/* Fields are not logged entirely, bc they might contain arbitrarily large data (e.g.
 * geoms that are sampled). Instead, only the data needed for UI features is logged. */
class FieldInfoLog : public ValLog {
 public:
  const CPPType &type;
  Vector<std::string> input_tooltips;

  FieldInfoLog(const GField &field);
};

struct GeoAttrInfo {
  std::string name;
  /* Can be empty when name does not actually exist on a geo yet. */
  std::optional<dune::AttrDomain> domain;
  std::optional<eCustomDataType> data_type;
};

/* Geos are not logged entirely, bc that would result in a lot of time and memory
 * overhead. Instead, only the data needed for UI features is logged. */
class GeoInfoLog : public ValLog {
 public:
  Vector<GeoAttrInfo> attrs;
  Vector<dune::GeoComponent::Type> component_types;

  struct MeshInfo {
    int verts_num, edges_num, faces_num;
  };
  struct CurveInfo {
    int points_num;
    int splines_num;
  };
  struct PointCloudInfo {
    int points_num;
  };
  struct GreasePencilInfo {
    int layers_num;
  };
  struct InstancesInfo {
    int instances_num;
  };
  struct EditDataInfo {
    bool has_deformed_positions;
    bool has_deform_matrices;
  };

  std::optional<MeshInfo> mesh_info;
  std::optional<CurveInfo> curve_info;
  std::optional<PointCloudInfo> pointcloud_info;
  std::optional<PenInfo> pen_info;
  std::optional<InstancesInfo> instances_info;
  std::optional<EditDataInfo> edit_data_info;

  GeoInfoLog(const dune::GeoSet &geo_set);
};

/* Data logged by a viewer node when it is ex. In this case, we do want to log the entire
 * geometry. */
class ViewerNodeLog {
 public:
  dune::GeoSet geo;
};

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/* Logs all data for a specific geo node tree in a specific cxt. When the same node group
 * is used in multiple times each instantiation will have a separate logger. */
class GeoTreeLogger {
 public:
  std::optional<ComputeCxtHash> parent_hash;
  std::optional<int32_t> group_node_id;
  Vector<ComputeCxtHash> children_hashes;

  LinearAllocator<> *allocator = nullptr;

  struct WarningWithNode {
    int32_t node_id;
    NodeWarning warning;
  };
  struct SocketValLog {
    int32_t node_id;
    int socket_index;
    destruct_ptr<ValLog> val;
  };
  struct NodeExTime {
    int32_t node_id;
    TimePoint start;
    TimePoint end;
  };
  struct ViewerNodeLogWNode {
    int32_t node_id;
    destruct_ptr<ViewerNodeLog> viewer_log;
  };
  struct AttrUsageWNode {
    int32_t node_id;
    StringRefNull attr_name;
    NamedAttrUsage usage;
  };
  struct DebugMsg {
    int32_t node_id;
    StringRefNull msg;
  };

  Vector<WarningWNode> node_warnings;
  Vector<SocketValLog> input_socket_vals;
  Vector<SocketValLog> output_socket_vals;
  Vector<NodeExTime> node_ex_times;
  Vector<ViewerNodeLogWNode, 0> viewer_node_logs;
  Vector<AttrUsageWNode, 0> used_named_attrs;
  Vector<DebugMsg, 0> debug_msgs;

  GeoTreeLogger();
  ~GeoTreeLogger();

  void log_val(const Node &node, const NodeSocket &socket, GPtr val);
  void log_viewer_node(const Node &viewer_node, dune::GeoSet geo);
};

/* Contains data that has been logged for a specific node in a cxt. So when the node is in a
 * node group that is used multiple times, there will be a diff GeoNodeLog for every
 * instance.
 *
 * By default not all of the info below is valid. A GeoTreeLog::ensure_* method has to be called
 * 1st. */
class GeoNodeLog {
 public:
  /* Warnings generated for that node. */
  Vector<NodeWarning> warnings;
  /* Time spent in this node. For node groups this is the sum of the run times of the nodes
   * inside. */
  std::chrono::nanoseconds run_time{0};
  /* Maps from socket indices to their values. */
  Map<int, ValLog *> input_vals_;
  Map<int, ValLog *> output_vals_;
  /* Maps from attr name to their usage flags. */
  Map<StringRefNull, NamedAttrUsage> used_named_attributes;
  /* Messages that are used for debugging purposes during development. */
  Vector<StringRefNull> debug_messages;

  GeoNodeLog();
  ~GeoNodeLog();
};

class GeoModifierLog;

/* Contains data that has been logged for a specific node group in a context. If the same node
 * group is used multiple times, there will be a different #GeoTreeLog for every instance.
 *
 * This contains lazily eval'd data. Call the corresponding `ensure_*` methods before accessing
 * data. */
class GeoTreeLog {
 private:
  GeoModLog *modifier_log_;
  Vector<GeoTreeLogger *> tree_loggers_;
  VectorSet<ComputeCxtHash> children_hashes_;
  bool reduced_node_warnings_ = false;
  bool reduced_node_run_times_ = false;
  bool reduced_socket_vals_ = false;
  bool reduced_viewer_node_logs_ = false;
  bool reduced_existing_attrs_ = false;
  bool reduced_used_named_attrs_ = false;
  bool reduced_debug_msgs_ = false;

 public:
  Map<int32_t, GeoNodeLog> nodes;
  Map<int32_t, ViewerNodeLog *, 0> viewer_node_logs;
  Vector<NodeWarning> all_warnings;
  std::chrono::nanoseconds run_time_sum{0};
  Vector<const GeoAttrInfo *> existing_attrs;
  Map<StringRefNull, NamedAttrUsage> used_named_attrs;

  GeoTreeLog(GeoModLog *mod_log, Vector<GeoTreeLogger *> tree_loggers);
  ~GeoTreeLog();

  void ensure_node_warnings();
  void ensure_node_run_time();
  void ensure_socket_vals();
  void ensure_viewer_node_logs();
  void ensure_existing_attrs();
  void ensure_used_named_attrs();
  void ensure_debug_msgs();

  ValLog *find_socket_val_log(const NodeSocket &query_socket);
};

/* There is 1 GeoModLog for every mod that evals geo nodes. It contains all
 * the loggers that are used during eval as well as the preproc'd logs that are used by UI
 * code. */
class GeoModLog {
 private:
  /* Data that is stored for each thread. */
  struct LocalData {
    /* Each thread has its own allocator. */
    LinearAlloc<> allocator;
    /* Store a separate #GeoTreeLogger for each instance of the corresponding node group (e.g.
     * when the same node group is used multiple times). */
    Map<ComputeCxtHash, destruct_ptr<GeoTreeLogger>> tree_logger_by_cxt;
  };

  /* Container for all thread-local data. */
  threading::EnumerableThreadSpecific<LocalData> data_per_thread_;
  /* A GeoTreeLog for every compute cxt. Created lazily when requested by UI code. */
  Map<ComputeCxtHash, std::unique_ptr<GeoTreeLog>> tree_logs_;

 public:
  GeoModLog();
  ~GeoModLog();

  /* Get a thread-local logger for the current node tree. */
  GeoTreeLogger &get_local_tree_logger(const ComputeCxt &compute_cxt);

  /* Get a log a specific node tree instance. */
  GeoTreeLog &get_tree_log(const ComputeCxtHash &compute_cxt_hash);

  /* Util accessor to logged data. */
  static Map<const dune::NodeTreeZone *, ComputeCxtHash>
  get_cxt_hash_by_zone_for_node_editor(const SpaceNode &snode, StringRefNull modifier_name);

  static Map<const dune::NodeTreeZone *, GeoTreeLog *> get_tree_log_by_zone_for_node_editor(
      const SpaceNode &snode);
  static const ViewerNodeLog *find_viewer_node_log_for_path(const ViewerPath &viewer_path);
};

}  // namespace dune::nodes::geo_eval_log
