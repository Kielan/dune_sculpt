#include "lib_dot_export.hh"

#include "fn_lazy_graph.hh"

#include <sstream>

namespace dune::fn::lazy_fn {

Graph::Graph()
{
  graph_input_node_ = allocator_.construct<InterfaceNode>().release();
  graph_output_node_ = allocator_.construct<InterfaceNode>().release();
  nodes_.append(graph_input_node_);
  nodes_.append(graph_output_node_);
}

Graph::~Graph()
{
  for (FnNode *node : this->n_nodes()) {
    for (InputSocket *socket : node->inputs_) {
      std::destroy_at(socket);
    }
    for (OutputSocket *socket : node->outputs_) {
      std::destroy_at(socket);
    }
    std::destroy_at(node);
  }
  for (const InterfaceNode *node : {graph_input_node_, graph_output_node_}) {
    for (InputSocket *socket : node->inputs_) {
      std::destroy_at(socket);
    }
    for (OutputSocket *socket : node->outputs_) {
      std::destroy_at(socket);
    }
    std::destroy_at(node);
  }
}

FnNode &Graph::add_fn(const LazyFn &fn)
{
  const Span<Input> inputs = fn.inputs();
  const Span<Output> outputs = fn.outputs();

  FnNode &node = *allocator_.construct<FunctionNode>().release();
  node.fn_ = &fn;
  node.inputs_ = allocator_.construct_elements_and_ptr_array<InputSocket>(inputs.size());
  node.outputs_ = allocator_.construct_elements_and_ptr_array<OutputSocket>(outputs.size());

  for (const int i : inputs.index_range()) {
    InputSocket &socket = *node.inputs_[i];
    socket.index_in_node_ = i;
    socket.is_input_ = true;
    socket.node_ = &node;
    socket.type_ = inputs[i].type;
  }
  for (const int i : outputs.index_range()) {
    OutputSocket &socket = *node.outputs_[i];
    socket.index_in_node_ = i;
    socket.is_input_ = false;
    socket.node_ = &node;
    socket.type_ = outputs[i].type;
  }

  nodes_.append(&node);
  return node;
}

GraphInputSocket &Graph::add_input(const CPPType &type, std::string name)
{
  GraphInputSocket &socket = *allocator_.construct<GraphInputSocket>().release();
  socket.is_input_ = false;
  socket.node_ = graph_input_node_;
  socket.type_ = &type;
  socket.index_in_node_ = graph_inputs_.append_and_get_index(&socket);
  graph_input_node_->outputs_ = graph_inputs_;

  graph_input_node_->socket_names_.append(std::move(name));
  return socket;
}

GraphOutputSocket &Graph::add_output(const CPPType &type, std::string name)
{
  GraphOutputSocket &socket = *allocator_.construct<GraphOutputSocket>().release();
  socket.is_input_ = true;
  socket.node_ = graph_output_node_;
  socket.type_ = &type;
  socket.index_in_node_ = graph_outputs_.append_and_get_index(&socket);
  graph_output_node_->inputs_ = graph_outputs_;

  graph_output_node_->socket_names_.append(std::move(name));
  return socket;
}

void Graph::add_link(OutputSocket &from, InputSocket &to)
{
  lib_assert(to.origin_ == nullptr);
  lib_assert(from.type_ == to.type_);
  to.origin_ = &from;
  from.targets_.append(&to);
}

void Graph::clear_origin(InputSocket &socket)
{
  if (socket.origin_ != nullptr) {
    socket.origin_->targets_.remove_first_occurrence_and_reorder(&socket);
    socket.origin_ = nullptr;
  }
}

void Graph::update_node_indices()
{
  for (const int i : nodes_.index_range()) {
    nodes_[i]->index_in_graph_ = i;
  }
}

void Graph::update_socket_indices()
{
  int socket_counter = 0;
  for (const int i : nodes_.index_range()) {
    for (InputSocket *socket : nodes_[i]->inputs()) {
      socket->index_in_graph_ = socket_counter++;
    }
    for (OutputSocket *socket : nodes_[i]->outputs()) {
      socket->index_in_graph_ = socket_counter++;
    }
  }
  socket_num_ = socket_counter;
}

bool Graph::node_indices_are_valid() const
{
  for (const int i : nodes_.index_range()) {
    if (nodes_[i]->index_in_graph_ != i) {
      return false;
    }
  }
  return true;
}

std::string Socket::name() const
{
  if (node_->is_fn()) {
    const FnNode &fn_node = static_cast<const FunctionNode &>(*node_);
    const LazyFn &fn = fn_node.function();
    if (is_input_) {
      return fn.input_name(index_in_node_);
    }
    return fn.output_name(index_in_node_);
  }
  const InterfaceNode &interface_node = *static_cast<const InterfaceNode *>(node_);
  return interface_node.socket_names_[index_in_node_];
}

std::string Socket::detailed_name() const
{
  std::stringstream ss;
  ss << node_->name() << ":" << (is_input_ ? "IN" : "OUT") << ":" << index_in_node_ << ":"
     << this->name();
  return ss.str();
}

std::string Node::name() const
{
  if (this->is_fn()) {
    return fn_->name();
  }
  return "Interface";
}

std::string Graph::ToDotOptions::socket_name(const Socket &socket) const
{
  return socket.name();
}

std::optional<std::string> Graph::ToDotOptions::socket_font_color(const Socket & /*socket*/) const
{
  return std::nullopt;
}

void Graph::ToDotOptions::add_edge_attributes(const OutputSocket & /*from*/,
                                              const InputSocket & /*to*/,
                                              dot::DirectedEdge & /*dot_edge*/) const
{
}

std::string Graph::to_dot(const ToDotOptions &options) const
{
  dot::DirectedGraph digraph;
  digraph.set_rankdir(dot::Attr_rankdir::LeftToRight);

  Map<const Node *, dot::NodeWithSocketsRef> dot_nodes;

  for (const Node *node : nodes_) {
    dot::Node &dot_node = digraph.new_node("");
    if (node->is_interface()) {
      dot_node.set_background_color("lightblue");
    }
    else {
      dot_node.set_background_color("white");
    }

    dot::NodeWithSockets dot_node_with_sockets;
    dot_node_with_sockets.node_name = node->name();
    for (const InputSocket *socket : node->inputs()) {
      dot::NodeWithSockets::Input &dot_input = dot_node_with_sockets.add_input(
          options.socket_name(*socket));
      dot_input.fontcolor = options.socket_font_color(*socket);
    }
    for (const OutputSocket *socket : node->outputs()) {
      dot::NodeWithSockets::Output &dot_output = dot_node_w_sockets.add_output(
          options.socket_name(*socket));
      dot_output.fontcolor = options.socket_font_color(*socket);
    }

    dot_nodes.add_new(node, dot::NodeWithSocketsRef(dot_node, dot_node_w_sockets));
  }

  for (const Node *node : nodes_) {
    for (const InputSocket *socket : node->inputs()) {
      const dot::NodeWithSocketsRef &to_dot_node = dot_nodes.lookup(&socket->node());
      const dot::NodePort to_dot_port = to_dot_node.input(socket->index());

      if (const OutputSocket *origin = socket->origin()) {
        dot::NodeWithSocketsRef &from_dot_node = dot_nodes.lookup(&origin->node());
        dot::DirectedEdge &dot_edge = digraph.new_edge(from_dot_node.output(origin->index()),
                                                       to_dot_port);
        options.add_edge_attributes(*origin, *socket, dot_edge);
      }
      else if (const void *default_val = socket->default_val()) {
        const CPPType &type = socket->type();
        std::string val_string;
        if (type.is_printable()) {
          val_string = type.to_string(default_val);
        }
        else {
          val_string = type.name();
        }
        dot::Node &default_val_dot_node = digraph.new_node(val_string);
        default_val_dot_node.set_shape(dot::Attr_shape::Ellipse);
        default_val_dot_node.attributes.set("color", "#00000055");
        digraph.new_edge(default_val_dot_node, to_dot_port);
      }
    }
  }

  return digraph.to_dot_string();
}

}  // namespace blender::fn::lazy_function
