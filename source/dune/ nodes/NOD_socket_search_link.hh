#pragma once

#include <functional>

#include "lib_string_ref.hh"
#include "lib_vector.hh"

#include "types_node.h" /* Necessary for eNodeSocketInOut. */

#include "NOD_node_declaration.hh"

struct Cxt;
struct SpaceNode;

namespace dune::nodes {

/* Params for the op of adding a node after the link drag search menu closes. */
class LinkSearchOpParams {
 private:
  /* Keep track of the nodes added by the callback, so they can be selected or moved afterwards.  */
  Vector<Node *> &added_nodes_;

 public:
  const Cxt &C;
  NodeTree &node_tree;
  /**
   * The node that contains the #socket.
   */
  bNode &node;
  /**
   * The existing socket to connect any added nodes to. Might be an input or output socket. */
  NodeSocket &socket;

  LinkSearchOpParams(const Cxt &C,
                     NodeTree &node_tree,
                     Node &node,
                     NodeSocket &socket,
                     Vector<bNode *> &added_nodes)
      : added_nodes_(added_nodes), C(C), node_tree(node_tree), node(node), socket(socket)
  {
  }

  Node &add_node(StringRef idname);
  Node &add_node(const NodeType &type);
  /* Find a socket with the given name (correctly checks for inputs and outputs)
   * and connect it to the socket the link drag started from (socket) */
  void connect_available_socket(bNode &new_node, StringRef socket_name);
  /* Like connect_available_socket, but also calls the node's update fn. */
  void update_and_connect_available_socket(Node &new_node, StringRef socket_name);
};

struct SocketLinkOp {
  using LinkSocketFn = pop std::function<void(LinkSearchOpParams &link_params)>;

  std::string name;
  LinkSocketFn fn;
  int weight = 0;
};

class GatherLinkSearchOpParams {
  /* The current node type. */
  const NodeType &node_type_;

  const SpaceNode &snode_;
  const NodeTree &node_tree_;

  const NodeSocket &other_socket_;

  /* The ops currently being built. Owned by the caller. */
  Vector<SocketLinkOperation> &items_;

 public:
  GatherLinkSearchOpParams(const bNodeType &node_type,
                           const SpaceNode &snode,
                           const bNodeTree &node_tree,
                           const bNodeSocket &other_socket,
                           Vector<SocketLinkOperation> &items)
      : node_type_(node_type),
        snode_(snode),
        node_tree_(node_tree),
        other_socket_(other_socket),
        items_(items)
  {
  }

  /* The node on the other side of the dragged link */
  const bNodeSocket &other_socket() const;

  /* The currently active node editor   */
  const SpaceNode &space_node() const;

  /* The node tree the user is editing when the search menu is created. */
  const NodeTree &node_tree() const;

  /* The type of the node in the current callback.q */
  const NodeType &node_type() const;

  /* Whether to list the input or output sockets of the node.qq   */
  eNodeSocketInOut in_out() const;

  /* param weight: Used to customize the order when multiple search items match.
   *
   * warning When creating lambdas for the fn arg, be careful not to capture this class
   * itself, since it is tmp. That is why we tend to use the same varq name for this
   * class (`params`) that we do for the arg to `LinkSocketFn`.   */
  void add_item(std::string socket_name, SocketLinkOpa::LinkSocketFn fn, int weight = 0);
};

/* This cb can be used for a node type when a few things are true about its inputs.
 * To avoid creating more boilerplate, it is the default cb for node types.
 * - Either all declared sockets are visible in the default state of the node, *OR* the node's
 *   type's declaration has been extended with make_available fans for those sockets.
 *
 * If a node type does not meet these criteria, the function will do nothing in a release build.
 * In a debug build, an assert will most likely be hit.
 *
 * \note For nodes with the deprecated #bNodeSocketTemplate instead of a declaration,
 * these criteria do not apply and the function just tries its best without asserting.
 */
void search_link_ops_for_basic_node(GatherLinkSearchOpParams &params);

void search_link_ops_for_declarations(GatherLinkSearchOpParams &params,
                                      Span<SocketDeclaration *> declarations);

}  // namespace blender::nodes
