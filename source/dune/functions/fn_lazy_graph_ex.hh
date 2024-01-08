#pragma once

/* This file provides means to create a LazyFn from Graph (could then be used in
 * another Graph again). */
#include "lib_vector.hh"
#include "lib_vector_set.hh"
#include "fn_lazy_fn_graph.hh"

namespace dune::fn::lazy_fn {

/* Can be implemented to log vals produced during graph eval. */
class GraphExLogger {
 public:
  virtual ~GraphExLogger() = default;

  virtual void log_socket_val(const Socket &socket,
                              GPtr val,
                              const Cxt &cxt) const;

  virtual void log_before_node_ex(const FnNode &node,
                                  const Params &params,
                                  const Cxt &cxt) const;

  virtual void log_after_node_ex(const FnNode &node,
                                 const Params &params,
                                 const Cxt &cxt) const;

  virtual void dump_when_outputs_are_missing(const FnNode &node,
                                             Span<const OutputSocket *> missing_sockets,
                                             const Cxt &cxt) const;
  virtual void dump_when_input_is_set_twice(const InputSocket &target_socket,
                                            const OutputSocket &from_socket,
                                            const Cxt &cxt) const;
};

/* Has to be implemented when some of the nodes in the graph may have side effects. The
 * GraphEx must know effects to ensure tagged nodes are ex
 * though their outputs are not needed. */
class GraphExSideEffectProvider {
 public:
  virtual ~GraphExSideEffectProvider() = default;
  virtual Vector<const FnNode *> get_nodes_with_side_effects(const Context &context) const;
};

/* Used to pass extra cxt into the ex of a fn.
 * The alt to this is to create a wrapper `LazyFn` for the FnNodes.
 * Using th light weight wrapper is preferable if possible. */
class GraphExNodeExWrapper {
 public:
  virtual ~GraphExNodeExWrap() = default;

  /* Is expected to run `node.fn().ex(params, cxt)` but might do some extra work,
   * like adjusting the cxt. */
  virtual void ex_node(const FnNode &node,
                       Params &params,
                       const Cxt &cxt) const = 0;
};

class GraphEx : public LazyFn {
 public:
  using Logger = GraphExLogger;
  using SideEffectProvider = GraphExSideEffectProvider;
  using NodeExWrapper = GraphExNodeExWrapper;

 private:
  /* The graph to eval */
  const Graph &graph_;
  /* Input and output sockets of the entire graph.  */
  Vector<const GraphInputSocket *> graph_inputs_;
  Vector<const GraphOutputSocket *> graph_outputs_;
  Array<int> graph_input_index_by_socket_index_;
  Array<int> graph_output_index_by_socket_index_;
  /* Optional logger for evs that happen during ex. */
  const Logger *logger_;
  /* Optional side effect provider.
   * It knows which nodes have side effects based on the cxt
   * during eval. */
  const SideEffectProvider *side_effect_provider_;
  /* Optional wrapper for node ex fns. */
  const NodeExWrapper *node_ex_wrapper_;

  /* When a graph is ex, various things have to be alloc (the state of all nodes).
   * Instead of many small allocs, a single bigger alloc is done. This struct
   * contains the preproc'd offsets into that bigger buf. */
  struct {
    int node_states_array_offset;
    int loaded_inputs_array_offset;
    Array<int> node_states_offsets;
    int total_size;
  } init_buf_info_;

  friend class Ex;

 public:
  GraphEx(const Graph &graph,
                Vector<const GraphInputSocket *> graph_inputs,
                Vector<const GraphOutputSocket *> graph_outputs,
                const Logger *logger,
                const SideEffectProvider *side_effect_provider,
                const NodeExWrapper *node_exwrapper);

  void *init_storage(LinearAllocator<> &allocator) const override;
  void destruct_storage(void *storage) const override;

  std::string input_name(int index) const override;
  std::string output_name(int index) const override;

 private:
  void ex_impl(Params &params, const Cxt &cxt) const override;
};

}  // namespace dune::fn::lazy_fn
