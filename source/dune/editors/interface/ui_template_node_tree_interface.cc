#include "BKE_context.h"
#include "BKE_node_tree_interface.hh"
#include "BKE_node_tree_update.h"

#include "BLI_color.hh"
#include "BLI_string.h"

#include "BLT_translation.h"

#include "DNA_node_tree_interface_types.h"

#include "ED_node.hh"
#include "ED_undo.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.h"

#include "UI_interface.hh"
#include "UI_resources.hh"
#include "UI_tree_view.hh"

#include "WM_api.hh"

namespace node_interface = blender::bke::node_interface;

namespace blender::ui::nodes {

struct wmDragNodeTreeInterface {
  bNodeTreeInterfaceItem *item;
};

namespace {

class NodePanelViewItem;
class NodeSocketViewItem;
class NodeTreeInterfaceView;

class NodeTreeInterfaceDragController : public AbstractViewItemDragController {
 private:
  bNodeTreeInterfaceItem &item_;

 public:
  explicit NodeTreeInterfaceDragController(NodeTreeInterfaceView &view,
                                           bNodeTreeInterfaceItem &item);
  virtual ~NodeTreeInterfaceDragController() = default;

  eWM_DragDataType get_drag_type() const;

  void *create_drag_data() const;
};

class NodeSocketDropTarget : public TreeViewItemDropTarget {
 private:
  bNodeTreeInterfaceSocket &socket_;

 public:
  explicit NodeSocketDropTarget(NodeSocketViewItem &item, bNodeTreeInterfaceSocket &socket);

  bool can_drop(const wmDrag &drag, const char **r_disabled_hint) const override;
  std::string drop_tooltip(const DragInfo &drag_info) const override;
  bool on_drop(bContext * /*C*/, const DragInfo &drag_info) const override;

 protected:
  wmDragNodeTreeInterface *get_drag_node_tree_declaration(const wmDrag &drag) const;
};

class NodePanelDropTarget : public TreeViewItemDropTarget {
 private:
  bNodeTreeInterfacePanel &panel_;

 public:
  explicit NodePanelDropTarget(NodePanelViewItem &item, bNodeTreeInterfacePanel &panel);

  bool can_drop(const wmDrag &drag, const char **r_disabled_hint) const override;
  std::string drop_tooltip(const DragInfo &drag_info) const override;
  bool on_drop(Cxt *C, const DragInfo &drag_info) const override;

 protected:
  WinDragNodeTreeInterface *get_drag_node_tree_declaration(const WinDrag &drag) const;
};

class NodeSocketViewItem : public BasicTreeViewItem {
 private:
  bNodeTree &nodetree_;
  bNodeTreeInterfaceSocket &socket_;

 public:
  NodeSocketViewItem(NodeTree &nodetree,
                     NodeTreeInterface &interface,
                     NodeTreeInterfaceSocket &socket)
      : BasicTreeViewItem(socket.name, ICON_NONE), nodetree_(nodetree), socket_(socket)
  {
    set_is_active_fn([interface, &socket]() { return interface.active_item() == &socket.item; });
    set_on_activate_fn([&interface](bContext & /*C*/, BasicTreeViewItem &new_active) {
      NodeSocketViewItem &self = static_cast<NodeSocketViewItem &>(new_active);
      interface.active_item_set(&self.socket_.item);
    });
  }

  void build_row(uiLayout &row) override
  {
    uiLayoutSetPropDecorate(&row, false);

    uiLayout *input_socket_layout = uiLayoutRow(&row, true);
    if (socket_.flag & NODE_INTERFACE_SOCKET_INPUT) {
      /* Socket template only draws in embossed layouts. */
      uiLayoutSetEmboss(input_socket_layout, UI_EMBOSS);
      /* Cxt is not used by the template fn. */
      uiTemplateNodeSocket(input_socket_layout, /*C*/ nullptr, socket_.socket_color());
    }
    else {
      /* Blank item to align output socket labels with inputs. */
      uiItemL(input_socket_layout, "", ICON_BLANK1);
    }

    this->add_label(row);

    uiLayout *output_socket_layout = uiLayoutRow(&row, true);
    if (socket_.flag & NODE_INTERFACE_SOCKET_OUTPUT) {
      /* XXX Socket template only draws in embossed layouts (Julian). */
      uiLayoutSetEmboss(output_socket_layout, UI_EMBOSS);
      /* Context is not used by the template function. */
      uiTemplateNodeSocket(output_socket_layout, /*C*/ nullptr, socket_.socket_color());
    }
    else {
      /* Blank item to align input socket labels with outputs. */
      uiItemL(output_socket_layout, "", ICON_BLANK1);
    }
  }

 protected:
  bool matches(const AbstractViewItem &other) const override
  {
    const NodeSocketViewItem *other_item = dynamic_cast<const NodeSocketViewItem *>(&other);
    if (other_item == nullptr) {
      return false;
    }

    return &socket_ == &other_item->socket_;
  }

  bool supports_renaming() const override
  {
    return true;
  }
  bool rename(const Cxt &C, StringRefNull new_name) override
  {
    MEM_SAFE_FREE(socket_.name);

    socket_.name = lib_strdup(new_name.c_str());
    nodetree_.tree_interface.tag_items_changed();
    ed_node_tree_propagate_change(&C, cxt_data_main(&C), &nodetree_);
    return true;
  }
  StringRef get_rename_string() const override
  {
    return socket_.name;
  }

  std::unique_ptr<AbstractViewItemDragController> create_drag_controller() const override;
  std::unique_ptr<TreeViewItemDropTarget> create_drop_target() override;
};

class NodePnlViewItem : public BasicTreeViewItem {
 private:
  NodeTree &nodetree_;
  NodeTreeInterfacePnl &panel_;

 public:
  NodePnlViewItem(NodeTree &nodetree,
                    NodeTreeInterface &interface,
                    NodeTreeInterfacePnl &pnl)
      : BasicTreeViewItem(panel.name, ICON_NONE), nodetree_(nodetree), pnl_(pnl)
  {
    set_is_active_fn([interface, &pnl]() { return interface.active_item() == &panel.item; });
    set_on_activate_fn([&interface](Cxt & /*C*/, BasicTreeViewItem &new_active) {
      NodePnlViewItem &self = static_cast<NodePnlViewItem &>(new_active);
      interface.active_item_set(&self.pnl_.item);
    });
  }

  void build_row(uiLayout &row) override
  {
    this->add_label(row);

    uiLayout *sub = uiLayoutRow(&row, true);
    uiLayoutSetPropDecorate(sub, false);
  }

 protected:
  bool matches(const AbstractViewItem &other) const override
  {
    const NodePnlViewItem *other_item = dynamic_cast<const NodePnlViewItem *>(&other);
    if (other_item == nullptr) {
      return false;
    }

    return &panel_ == &other_item->panel_;
  }

  bool supports_renaming() const override
  {
    return true;
  }
  bool rename(const Cxt &C, StringRefNull new_name) override
  {
    MEM_SAFE_FREE(pnl_.name);

    pnl_.name = lib_strdup(new_name.c_str());
    nodetree_.tree_interface.tag_items_changed();
    ed_node_tree_propagate_change(&C, cxt_data_main(&C), &nodetree_);
    return true;
  }
  StringRef get_rename_string() const override
  {
    return pnl_.name;
  }

  std::unique_ptr<AbstractViewItemDragController> create_drag_controller() const override;
  std::unique_ptr<TreeViewItemDropTarget> create_drop_target() override;
};

class NodeTreeInterfaceView : public AbstractTreeView {
 private:
  NodeTree &nodetree_;
  NodeTreeInterface &interface_;

 public:
  explicit NodeTreeInterfaceView(NodeTree &nodetree, NodeTreeInterface &interface)
      : nodetree_(nodetree), interface_(interface)
  {
  }

  NodeTree &nodetree()
  {
    return nodetree_;
  }

  NodeTreeInterface &interface()
  {
    return interface_;
  }

  void build_tree() override
  {
    /* Draw root items */
    this->add_items_for_pnl_recursive(interface_.root_panel, *this);
  }

 protected:
  void add_items_for_pnl_recursive(NodeTreeInterfacePnl &parent,
                                     ui::TreeViewOrItem &parent_item)
  {
    for (NodeTreeInterfaceItem *item : parent.items()) {
      switch (item->item_type) {
        case NODE_INTERFACE_SOCKET: {
          NodeTreeInterfaceSocket *socket = node_interface::get_item_as<NodeTreeInterfaceSocket>(
              item);
          NodeSocketViewItem &socket_item = parent_item.add_tree_item<NodeSocketViewItem>(
              nodetree_, interface_, *socket);
          socket_item.set_collapsed(false);
          break;
        }
        case NODE_INTERFACE_PNL: {
          NodeTreeInterfacePnl *pnl = node_interface::get_item_as<NodeTreeInterfacePnl>(
              item);
          NodePnlViewItem &pnl_item = parent_item.add_tree_item<NodePnlViewItem>(
              nodetree_, interface_, *pnl);
          pnl_item.set_collapsed(false);
          add_items_for_pnl_recursive(*pnl, pnl_item);
          break;
        }
      }
    }
  }
};

std::unique_ptr<AbstractViewItemDragController> NodeSocketViewItem::create_drag_controller() const
{
  return std::make_unique<NodeTreeInterfaceDragController>(
      static_cast<NodeTreeInterfaceView &>(this->get_tree_view()), socket_.item);
}

std::unique_ptr<TreeViewItemDropTarget> NodeSocketViewItem::create_drop_target()
{
  return std::make_unique<NodeSocketDropTarget>(*this, socket_);
}

std::unique_ptr<AbstractViewItemDragController> NodePnlViewItem::create_drag_controller() const
{
  return std::make_unique<NodeTreeInterfaceDragController>(
      static_cast<NodeTreeInterfaceView &>(this->get_tree_view()), pnl_.item);
}

std::unique_ptr<TreeViewItemDropTarget> NodePnlViewItem::create_drop_target()
{
  return std::make_unique<NodePnlDropTarget>(*this, pnl_);
}

NodeTreeInterfaceDragController::NodeTreeInterfaceDragController(NodeTreeInterfaceView &view,
                                                                 NodeTreeInterfaceItem &item)
    : AbstractViewItemDragController(view), item_(item)
{
}

eWinDragDataType NodeTreeInterfaceDragController::get_drag_type() const
{
  return WIN_DRAG_NODE_TREE_INTERFACE;
}

void *NodeTreeInterfaceDragController::create_drag_data() const
{
  WinDragNodeTreeInterface *drag_data = mem_cnew<WinDragNodeTreeInterface>(__func__);
  drag_data->item = &item_;
  return drag_data;
}

NodeSocketDropTarget::NodeSocketDropTarget(NodeSocketViewItem &item,
                                           NodeTreeInterfaceSocket &socket)
    : TreeViewItemDropTarget(item, DropBehavior::Reorder), socket_(socket)
{
}

bool NodeSocketDropTarget::can_drop(const WinDrag &drag, const char ** /*r_disabled_hint*/) const
{
  if (drag.type != WIN_DRAG_NODE_TREE_INTERFACE) {
    return false;
  }
  WinDragNodeTreeInterface *drag_data = get_drag_node_tree_declaration(drag);

  /* Can't drop an item onto its children. */
  if (const NodeTreeInterfacePnl *pnl = node_interface::get_item_as<NodeTreeInterfacePnl>(
          drag_data->item))
  {
    if (panel->contains(socket_.item)) {
      return false;
    }
  }
  return true;
}

std::string NodeSocketDropTarget::drop_tooltip(const DragInfo &drag_info) const
{
  switch (drag_info.drop_location) {
    case DropLocation::Into:
      return "";
    case DropLocation::Before:
      return TIP_("Insert before socket");
    case DropLocation::After:
      return TIP_("Insert after socket");
  }
  return "";
}

bool NodeSocketDropTarget::on_drop(Cxt *C, const DragInfo &drag_info) const
{
  WinDragNodeTreeInterface *drag_data = get_drag_node_tree_declaration(drag_info.drag_data);
  lib_assert(drag_data != nullptr);
  NodeTreeInterfaceItem *drag_item = drag_data->item;
  lib_assert(drag_item != nullptr);

  NodeTree &nodetree = this->get_view<NodeTreeInterfaceView>().nodetree();
  NodeTreeInterface &interface = this->get_view<NodeTreeInterfaceView>().interface();

  NodeTreeInterfacePanel *parent = interface.find_item_parent(socket_.item, true);
  int index = -1;

  /* Insert into same panel as the target. */
  lib_assert(parent != nullptr);
  switch (drag_info.drop_location) {
    case DropLocation::Before:
      index = parent->items().as_span().first_index_try(&socket_.item);
      break;
    case DropLocation::After:
      index = parent->items().as_span().first_index_try(&socket_.item) + 1;
      break;
    default:
      /* All valid cases should be handled above. */
      lib_assert_unreachable();
      break;
  }
  if (parent == nullptr || index < 0) {
    return false;
  }

  interface.move_item_to_parent(*drag_item, parent, index);

  /* General update */
  ed_node_tree_propagate_change(C, cxt_data_main(C), &nodetree);
  ed_undo_push(C, "Insert node group item");
  return true;
}

WinDragNodeTreeInterface *NodeSocketDropTarget::get_drag_node_tree_declaration(
    const WinDrag &drag) const
{
  lib_assert(drag.type == WIN_DRAG_NODE_TREE_INTERFACE);
  return static_cast<WinDragNodeTreeInterface *>(drag.poin);
}

NodePnlDropTarget::NodePnlDropTarget(NodePnlViewItem &item, NodeTreeInterfacePnl &pnl)
    : TreeViewItemDropTarget(item, DropBehavior::ReorderAndInsert), pnl_(pnl)
{
}

bool NodePnlDropTarget::can_drop(const WinDrag &drag, const char ** /*r_disabled_hint*/) const
{
  if (drag.type != WIN_DRAG_NODE_TREE_INTERFACE) {
    return false;
  }
  WinDragNodeTreeInterface *drag_data = get_drag_node_tree_declaration(drag);

  /* Can't drop an item onto its children. */
  if (const NodeTreeInterfacePnl *panel = node_interface::get_item_as<NodeTreeInterfacePnl>(
          drag_data->item))
  {
    if (pnl->contains(pnl_.item)) {
      return false;
    }
  }

  return true;
}

std::string NodePanelDropTarget::drop_tooltip(const DragInfo &drag_info) const
{
  switch (drag_info.drop_location) {
    case DropLocation::Into:
      return TIP_("Insert into pnl");
    case DropLocation::Before:
      return TIP_("Insert before pnl");
    case DropLocation::After:
      return TIP_("Insert after pnl");
  }
  return "";
}

bool NodePanelDropTarget::on_drop(Cxt *C, const DragInfo &drag_info) const
{
  WinDragNodeTreeInterface *drag_data = get_drag_node_tree_declaration(drag_info.drag_data);
  lib_assert(drag_data != nullptr);
  NodeTreeInterfaceItem *drag_item = drag_data->item;
  lib_assert(drag_item != nullptr);

  NodeTree &nodetree = get_view<NodeTreeInterfaceView>().nodetree();
  NodeTreeInterface &interface = get_view<NodeTreeInterfaceView>().interface();

  NodeTreeInterfacePnl *parent = nullptr;
  int index = -1;
  switch (drag_info.drop_location) {
    case DropLocation::Into: {
      /* Insert into target */
      parent = &panel_;
      index = 0;
      break;
    }
    case DropLocation::Before: {
      /* Insert into same panel as the target. */
      parent = interface.find_item_parent(pnl_.item, true);
      lib_assert(parent != nullptr);
      index = parent->items().as_span().first_index_try(&pnl_.item);
      break;
    }
    case DropLocation::After: {
      /* Insert into same pnl as the target. */
      parent = interface.find_item_parent(pnl_.item, true);
      lib_assert(parent != nullptr);
      index = parent->items().as_span().first_index_try(&pnl_.item) + 1;
      break;
    }
  }
  if (parent == nullptr || index < 0) {
    return false;
  }

  interface.move_item_to_parent(*drag_item, parent, index);

  /* General update */
  ed_node_tree_propagate_change(C, cxt_data_main(C), &nodetree);
  ed_undo_push(C, "Insert node group item");
  return true;
}

WinDragNodeTreeInterface *NodePanelDropTarget::get_drag_node_tree_declaration(
    const wmDrag &drag) const
{
  lib_assert(drag.type == WIN_DRAG_NODE_TREE_INTERFACE);
  return static_cast<WinDragNodeTreeInterface *>(drag.ptr);
}

}  // namespace

}  // namespace dune::ui::nodes

void uiTemplateNodeTreeInterface(uiLayout *layout, ApiPtr *ptr)
{
  if (!ptr->data) {
    return;
  }
  if (!api_struct_is_a(ptr->type, &ApiNodeTreeInterface)) {
    return;
  }
  NodeTree &nodetree = *reinterpret_cast<NodeTree *>(ptr->owner_id);
  NodeTreeInterface &interface = *static_cast<NodeTreeInterface *>(ptr->data);

  uiBlock *block = uiLayoutGetBlock(layout);

  dune::ui::AbstractTreeView *tree_view = ui_block_add_view(
      *block,
      "Node Tree Declaration Tree View",
      std::make_unique<dune::ui::nodes::NodeTreeInterfaceView>(nodetree, interface));
  tree_view->set_min_rows(3);

  dune::ui::TreeViewBuilder::build_tree_view(*tree_view, *layout);
}
