#pragma once

/* This file provides means to create a LazyFn from Graph (which could then e.g. be used in
 * another Graph again). */
#include "lib_vector.hh"
#include "lib_vector_set.hh"
#include "fn_lazy_fn_graph.hh"

namespace dune::fn::lazy_fn {

/* Can be implemented to log values produced during graph eval. */
class GraphExLogger {
 public:
  virtual ~GraphExLogger() = default;

  virtual void log_socket_value(const Socket &socket,
                                GPointer value,
                                const Context &context) const;

  virtual void log_before_node_execute(const FunctionNode &node,
                                       const Params &params,
                                       const Context &context) const;

  virtual void log_after_node_execute(const FunctionNode &node,
                                      const Params &params,
                                      const Context &context) const;

  virtual void dump_when_outputs_are_missing(const FunctionNode &node,
                                             Span<const OutputSocket *> missing_sockets,
                                             const Context &context) const;
  virtual void dump_when_input_is_set_twice(const InputSocket &target_socket,
                                            const OutputSocket &from_socket,
                                            const Context &context) const;
};

/* Has to be implemented when some of the nodes in the graph may have side effects. The
 * GraphEx has to know about that to make sure that these nodes will be executed even though
 * their outputs are not needed. */
class GraphExSideEffectProvider {
 public:
  virtual ~GraphExSideEffectProvider() = default;
  virtual Vector<const FunctionNode *> get_nodes_with_side_effects(const Context &context) const;
};

/* Can be used to pass extra context into the execution of a function. The main alternative to this
 * is to create a wrapper `LazyFunction` for the `FunctionNode`s. Using this light weight wrapper
 * is preferable if possible. */
class GraphExNodeExWrapper {
 public:
  virtual ~GraphExNodeExWrapper() = default;

  /* Is expected to run `node.function().execute(params, context)` but might do some extra work,
   * like adjusting the cxt. */
  virtual void execute_node(const FnNode &node,
                            Params &params,
                            const Cxt &cxt) const = 0;
};

class GraphEx : public LazyFunction {
 public:
  using Logger = GraphExLogger;
  using SideEffectProvider = GraphExSideEffectProvider;
  using NodeExWrapper = GraphExNodeExWrapper;

 private:
  /**
   * The graph that is evaluated.
   */
  const Graph &graph_;
  /**
   * Input and output sockets of the entire graph.  */
  Vector<const GraphInputSocket *> graph_inputs_;
  Vector<const GraphOutputSocket *> graph_outputs_;
  Array<int> graph_input_index_by_socket_index_;
  Array<int> graph_output_index_by_socket_index_;
  /**
   * Optional logger for events that happen during execution.
   */
  const Logger *logger_;
  /**
   * Optional side effect provider. It knows which nodes have side effects based on the context
   * during evaluation.
   */
  const SideEffectProvider *side_effect_provider_;
  /**
   * Optional wrapper for node execution functions. */
  const NodeExWrapper *node_execute_wrapper_;

  /**
   * When a graph is executed, various things have to be allocated (e.g. the state of all nodes).
   * Instead of doing many small allocations, a single bigger allocation is done. This struct
   * contains the preprocessed offsets into that bigger buffer.
   */
  struct {
    int node_states_array_offset;
    int loaded_inputs_array_offset;
    Array<int> node_states_offsets;
    int total_size;
  } init_buffer_info_;

  friend class Ex;

 public:
  GraphEx(const Graph &graph,
                Vector<const GraphInputSocket *> graph_inputs,
                Vector<const GraphOutputSocket *> graph_outputs,
                const Logger *logger,
                const SideEffectProvider *side_effect_provider,
                const NodeExecuteWrapper *node_execute_wrapper);

  void *init_storage(LinearAllocator<> &allocator) const override;
  void destruct_storage(void *storage) const override;

  std::string input_name(int index) const override;
  std::string output_name(int index) const override;

 private:
  void execute_impl(Params &params, const Context &context) const override;
};

}  // namespace blender::fn::lazy_function
