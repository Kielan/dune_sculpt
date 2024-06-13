#pragma once

/* For eval, geo node groups are converted to a lazy-fn graph. The generated graph
 * is cached per node group, so it only has to be generated once after a change.
 *
 * Node groups are *not* inlined into the lazy-fn graph. This could be added in the future as
 * it might improve performance in some cases, but generally does not seem necessary. Inlining node
 * groups also has disadvantages like making per-node-group caches less useful, resulting in more
 * overhead.
 *
 * Instead, group nodes are just like all other nodes in the lazy-fn graph. What makes them
 * special is that they ref the lazy-fn graph of the group they ref.
 *
 * During lazy-fn graph generation, a mapping between the NodeTree and
 * lazy_fn::Graph is build that can be used when eval'ing the graph (e.g. for logging). */

#include <variant>

#include "fn_lazy_fn_graph.hh"
#include "fn_lazy_fn_graph_executor.hh"

#include "node_geo_nodes_log.hh"
#include "node_multi_fn.hh"

#include "lib_compute_cxt.hh"

#include "dune_bake_items.hh"
#include "dune_node_tree_zones.hh"

struct Ob;
struct Graph;
struct Scene;

namespace dune::nodes {

using lf::LazyFn;
using mf::MultiFn;

/* The structs in here describe the diff possible behaviors of a simulation input node. */
namespace sim_input {

/* Only pass data through the node. Data that is incompatible with simulations (like
 * anonymous attrs), is removed though. */
struct PassThrough {};

/* The input is not evald, instead the vals provided here are output by the node. */
struct OutputCopy {
  float delta_time;
  dune::bake::BakeStateRef state;
};

/* Same as above, but the values can be output by move, instead of copy. This can reduce the amount
 * of unnecessary copies, when the old simulation state is not needed anymore. */
struct OutputMove {
  float delta_time;
  dune::bake::BakeState state;
};

using Behavior = std::variant<PassThrough, OutputCopy, OutputMove>;

}  // namespace sim_input

/* The structs in here describe the diff possible behaviors of a sim output node. */
namespace sim_output {

/* Output the data that comes from the corresponding sim input node, ignoring the nodes in
 * the zone. */
struct PassThrough {};

/* Computes the sim step and calls the given fn to cache the new sim state.
 * The new sim state is the output of the node. */
struct StoreNewState {
  std::fn<void(dune::bake::BakeState state)> store_fn;
};

/* The inputs are not evaluated, instead the given cached items are output directly. */
struct ReadSingle {
  dune::bake::BakeStateRef state;
};

/* The inputs are not evaluated, instead of a mix of the two given states is output. */
struct ReadInterpolated {
  /* Factor between 0 and 1 that determines the influence of the two simulation states. */
  float mix_factor;
  dune::bake::BakeStateRef prev_state;
  dune::bake::BakeStateRef next_state;
};

/* Used when there was some issue loading the baked data from disk. */
struct ReadErr {
  std::string msg;
};

using Behavior = std::variant<PassThrough, StoreNewState, ReadSingle, ReadInterpolated, ReadError>;

}  // namespace sim_output

/* Ctrls the behavior of one sim zone. */
struct SimZoneBehavior {
  sim_input::Behavior input;
  sim_output::Behavior output;
};

class GeoNodesSimParams {
 public:
  /* Get the expected behavior for the sim zone with the given id (see NestedNodeRef).
   * It's possible that this method called multiple times for the same id. In this case, the same
   * ptr should be returned in each call. */
  virtual SimulationZoneBehavior *get(const int zone_id) const = 0;
};

/* The set of possible behaviors are the same for both of these nodes currently. */
using BakeNodeBehavior = sim_output::Behavior;

class GeoNodesBakeParams {
 public:
  virtual BakeNodeBehavior *get(const int id) const = 0;
};

struct GeoNodesSideEffectNodes {
  MultiValMap<ComputeCxtHash, const lf::FnNode *> nodes_by_cxt;
  /* The repeat zone is id by the compute cxt of the parent and the id of the
   * repeat output node. */
  MultiValMap<std::pair<ComputeCxtHash, int32_t>, int> iters_by_repeat_zone;
};

/* Data that is passed into geo nodes eval from the mod. */
struct GeoNodesModData {
  /* Ob that is currently evaluated. */
  const Ob *self_ob = nullptr;
  /* Graph that is eval'ing the mod. */
  Graph *graph = nullptr;
};

struct GeoNodesOpData {
  eObMode mode;
  /* The ob currently effected by the op. */
  const Object *self_ob = nullptr;
  /* Current eval graph. */
  Graph *graph = nullptr;
  Scene *scene = nullptr;
};

struct GeoNodesCallData {
  /* Top-level node tree of the current eval. */
  const NodeTree *root_ntree = nullptr;
  /* Optional logger that keeps track of data generated during eval to allow for better
   * debugging afterwards. */
  geo_eval_log::GeoModLog *eval_log = nullptr;
  /* Optional injected behavior for simulations. */
  GeoNodesSimParams *sim_params = nullptr;
  /* Optional injected behavior for bake nodes. */
  GeoNodesBakeParams *bake_params = nullptr;
  /* Some nodes should be ex'd even when their output is not used (e.g. active viewer nodes and
   * the node groups they are contained in). */
  const GeoNodesSideEffectNodes *side_effect_nodes = nullptr;
  /* Controls in which compute cxts we want to log socket vals. Logging them in all cxts
   * can result in slowdowns. In the majority of cases, the logged socket values are freed without
   * being looked at anyway.
   *
   * If this is null, all socket vals will be logged. */
  const Set<ComputeCxtHash> *socket_log_cxts = nullptr;

  /* Data from the mod that is being eval'd. */
  GeoNodesModData *mod_data = nullptr;
  /* Data from ex as op in 3D viewport. */
  GeoNodesOpData *op_data = nullptr;

  /* Self ob has slight diff semantics depending on how geo nodes is called.
   * Thus it is not stored directly in the global data. */
  const Ob *self_ob() const;
};

/* Custom user data that is passed to every geo nodes related lazy-fn eval. */
struct GeoNodesLFUserData : public lf::UserData {
  /* Data provided by the root caller of geo nodes. */
  const GeoNodesCallData *call_data = nullptr;
  /* Current compute cxt. This is diff depending in the (nested) node group that is being
   * evaluated. */
  const ComputeCxt *compute_cxt = nullptr;
  /* Log socket vals in the current compute cxt. Child cxts might use logging again. */
  bool log_socket_vals = true;

  destruct_ptr<lf::LocalUserData> get_local(LinearAllocator<> &allocator) override;
};

struct GeoNodesLFLocalUserData : public lf::LocalUserData {
 private:
  /* Thread-local logger for the current node tree in the current compute cxt. It is only
   * instantiated when it is actually used and then cached for the current thread. */
  mutable std::optional<geo_eval_log::GeoTreeLogger *> tree_logger_;

 public:
  GeoNodesLFLocalUserData(GeoNodesLFUserData & /*user_data*/) {}

  /* Get the current tree logger. This method is not thread-safe, each thread is supposed to have
   * a separate logger. */
  geo_eval_log::GeoTreeLogger *try_get_tree_logger(const GeoNodesLFUserData &user_data) const
  {
    if (!tree_logger_.has_val()) {
      this->ensure_tree_logger(user_data);
    }
    return *tree_logger_;
  }

 private:
  void ensure_tree_logger(const GeoNodesLFUserData &user_data) const;
};

/* Generally this is DynamicSocket. Meaning that to determine if a node group will
 * use a particular input, it has to be partially ex'd.
 *
 * In other cases, it's not necessary to look into the node group to determine
 * if an input is necessary. */
enum class InputUsageHintType {
  /* The input socket is never used. */
  Never,
  /* The input socket is used when a subset of the outputs is used. */
  DependsOnOutput,
  /* Can't determine statically if the input is used, check the corresponding output socket. */
  DynamicSocket,
};

struct InputUsageHint {
  InputUsageHintType type = InputUsageHintType::DependsOnOutput;
  /* Used in depends-on-output mode. */
  Vector<int> output_deps;
};

/* Contains the mapping between the NodeTree and the corresponding lazy-fn graph.
 * This is *not* a one-to-one mapping. */
struct GeoNodeLazyFnGraphMapping {
  /* Optimization to avoid partially evaling a node group to assess which
   * inputs are needed. */
  Vector<InputUsageHint> group_input_usage_hints;
  /* A mapping used for logging intermediate vals. */
  MultiValMap<const lf::Socket *, const NodeSocket *> sockets_by_lf_socket_map;
  /* Mappings for some special node types. Generally, this mapping does not exist for all node
   * types, so better have more specialized mappings for now. */
  Map<const Node *, const lf::FnNode *> group_node_map;
  Map<const Node *, const lf::FnNode *> possible_side_effect_node_map;
  Map<const dune::NodeTreeZone *, const lf::FnNode *> zone_node_map;

  /* Indexed by NodeSocket::index_in_all_outputs. */
  Array<int> lf_input_index_for_output_socket_usage;
  /* Indexed by NodeSocket::index_in_all_outputs. */
  Array<int> lf_input_index_for_attr_prop_to_output;
  /* Indexed by NodeSocket::index_in_tree. */
  Array<int> lf_index_by_bsocket;
};

/* Contains the info that is necessary to ex a geo node tree. */
struct GeoNodesGroupFn {
  /* The lazy-fn that does what the node group does. Its inputs and outputs are described
   * below. */
  const LazyFn *fn = nullptr;

  struct {
    /* Main input values that come out of the Group Input node. */
    IndexRange main;
    /* A bool for every group output that indicates whether that output is needed. It's ok if
     * those are set to true even when an output is not used, but the other way around will lead to
     * bugs. The node group uses those vals to compute the lifetimes of anonymous attrs. */
    IndexRange output_usages;
    /* Some node groups can prop attrs from a geo input to a geo output. In
     * those cases, the caller of the node group has to decide which anonymous attrs must
     * be kept alive on the geo bc the caller requires them. */
    struct {
      IndexRange range;
      Vector<int> geo_outputs;
    } attrs_to_prop;
  } inputs;

  struct {
    /* Main output vals that are passed into the Group Output node. */
    IndexRange main;
    /* A bool for every group input that indicates whether this input will be used. Oftentimes
     * this can be determined wo actually computing much. This is used to compute anonymous
     * attribute lifetimes. */
    IndexRange input_usages;
  } outputs;
};

/* Data that is cached for every NodeTree. */
struct GeoNodesLazyFnGraphInfo {
  /* Contains resources that need to be freed when the graph is not needed anymore. */
  ResourceScope scope;
  GeoNodesGroupFn fn;
  /* The actual lazy-fn graph. */
  lf::Graph graph;
  /* Mappings between the lazy-fn graph and the NodeTree. */
  GeoNodeLazyFnGraphMapping mapping;
  /* Approx number of nodes in the graph if all sub-graphs were inlined.
   * This can be used as a simple heuristic for the complexity of the node group. */
  int num_inline_nodes_approximate = 0;
};

std::unique_ptr<LazyFn> get_sim_output_lazy_fn(
    const Node &node, GeoNodesLazyFnGraphInfo &own_lf_graph_info);
std::unique_ptr<LazyFn> get_sim_input_lazy_fn(
    const NodeTree &node_tree,
    const Node &node,
    GeoNodesLazyFnGraphInfo &own_lf_graph_info);
std::unique_ptr<LazyFn> get_switch_node_lazy_fn(const Node &node);
std::unique_ptr<LazyFn> get_index_switch_node_lazy_fn(
    const Node &node, GeoNodesLazyFnGraphInfo &lf_graph_info);
std::unique_ptr<LazyFn> get_bake_lazy_fn(
    const Node &node, GeoNodesLazyFnGraphInfo &own_lf_graph_info);

/* Outputs the default val of each output socket that has not been output yet. This needs the
 * Node bc otherwise the default vals for the outputs are not known. The lazy-fn
 * params do not differentiate between e.g. float and vector sockets. The SocketValVariant
 * type is used for both. */
void set_default_remaining_node_outputs(lf::Params &params, const bNode &node);

struct FoundNestedNodeId {
  int id;
  bool is_in_simulation = false;
  bool is_in_loop = false;
};

std::optional<FoundNestedNodeId> find_nested_node_id(const GeoNodesLFUserData &user_data,
                                                     const int node_id);

/* An anonymous attr created by a node */
class NodeAnonymousAttrId : public dune::AnonymousAttrId {
  std::string long_name_;
  std::string socket_name_;

 public:
  NodeAnonymousAttrId(const Ob &ob,
                           const ComputeCxt &compute_cxt,
                           const Node &node,
                           const StringRef id,
                           const StringRef name);

  std::string user_name() const override;
};

/* Main fn that converts a NodeTree into a lazy-fn graph. If the graph has been
 * generated alrdy, nothing is done. Under some circumstances a valid graph cannot be created. In
 * those cases null is returned. */
const GeoNodesLazyFnGraphInfo *ensure_geo_nodes_lazy_fn_graph(
    const NodeTree &tree);

}  // namespace dune::nodes
