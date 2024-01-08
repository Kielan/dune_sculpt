#pragma once
/* This file contains a graph data struct that allows composing multiple lazy-fns into a
 * combined lazy-fn.
 * Are 2 types of nodes in the graph:
 * - FnNode: Corresponds to a LazyFn. Inputs/outputs of the fn become
 *   input and output sockets of the node.
 * - InterfaceNode: Indicates inputs/outputs of the entire graph.
 *   Can have an arbitrary num of sockets. */
#include "lib_linear_allocator.hh"

#include "fn_lazy_fn.hh"

namespace dune::dot {
class DirectedEdge;
}

namespace dune::fn::lazy_fn {

class Socket;
class InputSocket;
class OutputSocket;
class Node;
class Graph;

/* A Socket is the interface of a Node. Every Socket is either an InputSocket or OutputSocket.
 * Links can be created from output sockets to input sockets */
class Socket : NonCopyable, NonMovable {
 protected:
  /* The node the socket belongs to. */
  Node *node_;
  /* Data type of the socket. Only sockets w the same type can be linked. */
  const CPPType *type_;
  /* Indicates whether this is an InputSocket or OutputSocket. */
  bool is_input_;
  /* Index of the socket. E.g. 0 for the first input and the first output socket. */
  int index_in_node_;
  /**
   * Index of the socket in the entire graph. Every socket has a different index.
   */
  int index_in_graph_;

  friend Graph;

 public:
  bool is_input() const;
  bool is_output() const;

  int index() const;
  int index_in_graph() const;

  InputSocket &as_input();
  OutputSocket &as_output();
  const InputSocket &as_input() const;
  const OutputSocket &as_output() const;

  const Node &node() const;
  Node &node();

  const CPPType &type() const;

  std::string name() const;
  std::string detailed_name() const;
};

class InputSocket : public Socket {
 private:
  /* An input can have at most one link connected to it. The linked socket is the "origin" bc
   * it's where the data is coming from. The type of the origin must be the same as the type of
   * this socket. */
  OutputSocket *origin_;
  /* Can be null or a non-owning ptt to a val of the type of the socket. This val will be
   * used when the input is used but not linked.
   *
   * This is technically not needed, bc one could just create a separate node that just
   * outputs the val, but that would have more overhead. Especially bc it's commonly the
   * case that most inputs are unlinked. */
  const void *default_value_ = nullptr;

  friend Graph;

 public:
  OutputSocket *origin();
  const OutputSocket *origin() const;

  const void *default_val() const;
  void set_default_val(const void *val);
};

class OutputSocket : public Socket {
 private:
  /* An output can be linked to an arbitrary num of inputs of the same type. */
  Vector<InputSocket *> targets_;

  friend Graph;

 public:
  Span<InputSocket *> targets();
  Span<const InputSocket *> targets() const;
};

/* A Node has input and output sockets. Every node is either a FnNode or an InterfaceNode. */
class Node : NonCopyable, NonMovable {
 protected:
  /* The fn this node corresponds to. If this is null the node is an InterfaceNode.
   * The fn is not owned by this Node nor by the Graph. */
  const LazyFn *fn_ = nullptr;
  /* Input sockets of the node. */
  Span<InputSocket *> inputs_;
  /* Output sockets of the node. */
  Span<OutputSocket *> outputs_;
  /* An index that is set when calling Graph::update_node_indices. This can be used to create
   * efficient mappings from nodes to other data using just an array instead of a hash map.
   * This has better performance than always using hash maps.  */
  int index_in_graph_ = -1;

  friend Graph;

 public:
  bool is_interface() const;
  bool is_function() const;
  int index_in_graph() const;

  Span<const InputSocket *> inputs() const;
  Span<const OutputSocket *> outputs() const;
  Span<InputSocket *> inputs();
  Span<OutputSocket *> outputs();

  const InputSocket &input(int index) const;
  const OutputSocket &output(int index) const;
  InputSocket &input(int index);
  OutputSocket &output(int index);

  std::string name() const;
};

/* A Node corresponds to a specific LazyFn. */
class FnNode final : public Node {
 public:
  const LazyFn &fn() const;
};

/* A Node that does *not* correspond to a LazyFn. Instead it can be used to indicate inputs
 * and outputs of the entire graph. It can have an arbitrary num of inputs and outputs. */
class InterfaceNode final : public Node {
 private:
  friend Node;
  friend Socket;
  friend Graph;

  Vector<std::string> socket_names_;
};

/* Interface input sockets are output sockets on the input node.
 * here renaming for code clarity. */
using GraphInputSocket = OutputSocket;
using GraphOutputSocket = InputSocket;

/* A container for an arbitrary num of nodes and links between their sockets. */
class Graph : NonCopyable, NonMovable {
 private:
  /* Used to alloc nodes and sockets in the graph. */
  LinearAllocator<> allocator_;
  /* Contains all nodes in the graph so that it is efficient to iter over them.
   * The first two nodes are the interface input and output nodes */
  Vector<Node *> nodes_;

  InterfaceNode *graph_input_node_ = nullptr;
  InterfaceNode *graph_output_node_ = nullptr;

  Vector<GraphInputSocket *> graph_inputs_;
  Vector<GraphOutputSocket *> graph_outputs_;

  /* Num of sockets in the graph. Can be used as array size when indexing using
   * `Socket::index_in_graph`.  */
  int socket_num_ = 0;

 public:
  Graph();
  ~Graph();

  /* Get all nodes in the graph. The index in the span corresponds to Node::index_in_graph. */
  Span<const Node *> nodes() const;
  Span<Node *> nodes();

  Span<const FnNode *> fn_nodes() const;
  Span<FnNode *> fn_nodes();

  Span<GraphInputSocket *> graph_inputs();
  Span<GraphOutputSocket *> graph_outputs();

  Span<const GraphInputSocket *> graph_inputs() const;
  Span<const GraphOutputSocket *> graph_outputs() const;

  /* Add a new fn node with sockets that match the passed in LazyFn */
  FnNode &add_fn(const LazyFn &fn);

  /* Add inputs and outputs to the graph */
  GraphInputSocket &add_input(const CPPType &type, std::string name = "");
  GraphOutputSocket &add_output(const CPPType &type, std::string name = "");

  /* Add a link between the two given sockets.
   * This has undefined behavior when the input is linked to something else alrdy. */
  void add_link(OutputSocket &from, InputSocket &to);

  /* If the socket is linked, remove the link. */
  void clear_origin(InputSocket &socket);

  /* Ensuree Node::index_in_graph is up to date */
  void update_node_indices();
  /* Ensures Socket::index_in_graph is up to date.  */
  void update_socket_indices();

  /* Num of sockets in the graph.  */
  int socket_num() const;

  /* Can be used to assert that #update_node_indices has been called. */
  bool node_indices_are_valid() const;

  /* Optional config options for the dot graph generation. Allows creating
   * visualizations for specific purposes. */
  class ToDotOptions {
   public:
    virtual std::string socket_name(const Socket &socket) const;
    virtual std::optional<std::string> socket_font_color(const Socket &socket) const;
    virtual void add_edge_attributes(const OutputSocket &from,
                                     const InputSocket &to,
                                     dot::DirectedEdge &dot_edge) const;
  };

  /* Util to generate a dot graph string for the graph. This can be used for debugging */
  std::string to_dot(const ToDotOptions &options = {}) const;
};

/* Socket Inline Methods */
inline bool Socket::is_input() const
{
  return is_input_;
}

inline bool Socket::is_output() const
{
  return !is_input_;
}

inline int Socket::index() const
{
  return index_in_node_;
}

inline int Socket::index_in_graph() const
{
  return index_in_graph_;
}

inline InputSocket &Socket::as_input()
{
  lib_assert(this->is_input());
  return *static_cast<InputSocket *>(this);
}

inline OutputSocket &Socket::as_output()
{
  lib_assert(this->is_output());
  return *static_cast<OutputSocket *>(this);
}

inline const InputSocket &Socket::as_input() const
{
  lib_assert(this->is_input());
  return *static_cast<const InputSocket *>(this);
}

inline const OutputSocket &Socket::as_output() const
{
  lib_assert(this->is_output());
  return *static_cast<const OutputSocket *>(this);
}

inline const Node &Socket::node() const
{
  return *node_;
}

inline Node &Socket::node()
{
  return *node_;
}

inline const CPPType &Socket::type() const
{
  return *type_;
}

/* InputSocket Inline Methods */
inline const OutputSocket *InputSocket::origin() const
{
  return origin_;
}

inline OutputSocket *InputSocket::origin()
{
  return origin_;
}

inline const void *InputSocket::default_val() const
{
  return default_val_;
}

inline void InputSocket::set_default_val(const void *val)
{
  default_val_ = val;
}

/* OutputSocket Inline Methods */
inline Span<const InputSocket *> OutputSocket::targets() const
{
  return targets_;
}

inline Span<InputSocket *> OutputSocket::targets()
{
  return targets_;
}

/* Node Inline Method */
inline bool Node::is_interface() const
{
  return fn_ == nullptr;
}

inline bool Node::is_function() const
{
  return fn_ != nullptr;
}

inline int Node::index_in_graph() const
{
  return index_in_graph_;
}

inline Span<const InputSocket *> Node::inputs() const
{
  return inputs_;
}

inline Span<const OutputSocket *> Node::outputs() const
{
  return outputs_;
}

inline Span<InputSocket *> Node::inputs()
{
  return inputs_;
}

inline Span<OutputSocket *> Node::outputs()
{
  return outputs_;
}

inline const InputSocket &Node::input(const int index) const
{
  return *inputs_[index];
}

inline const OutputSocket &Node::output(const int index) const
{
  return *outputs_[index];
}

inline InputSocket &Node::input(const int index)
{
  return *inputs_[index];
}

inline OutputSocket &Node::output(const int index)
{
  return *outputs_[index];
}

/* FnNode Inline Methods */
inline const LazyFn &FnNode::fn() const
{
  lib_assert(fn_ != nullptr);
  return *fn_;
}

/* Graph Inline Methods */
inline Span<const Node *> Graph::nodes() const
{
  return nodes_;
}

inline Span<Node *> Graph::nodes()
{
  return nodes_;
}

inline Span<const FnNode *> Graph::fn_nodes() const
{
  return nodes_.as_span().drop_front(2).cast<const FnNode *>();
}

inline Span<FnNode *> Graph::fn_nodes()
{
  return nodes_.as_span().drop_front(2).cast<FnNode *>();
}

inline Span<GraphInputSocket *> Graph::graph_inputs()
{
  return graph_inputs_;
}

inline Span<GraphOutputSocket *> Graph::graph_outputs()
{
  return graph_outputs_;
}

inline Span<const GraphInputSocket *> Graph::graph_inputs() const
{
  return graph_inputs_;
}

inline Span<const GraphOutputSocket *> Graph::graph_outputs() const
{
  return graph_outputs_;
}

inline int Graph::socket_num() const
{
  return socket_num_;
}

}  // namespace dune::fn::lazy_fn
