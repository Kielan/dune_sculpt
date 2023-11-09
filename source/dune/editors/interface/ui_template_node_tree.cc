#include "dune_cxt.h"
#include "dune_node_tree.hh"
#include "dune_node_tree_update.h"

#include "lib_color.hh"
#include "lib_string.h"

#include "lang.h"

#include "types_node_tree_interface.h"

#include "ed_node.hh"
#include "ed_undo.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_tree_view.hh"

#include "win_api.hh"

namespace node_ui = dune::node_ui;

namespace dune::ui::nodes {

struct WinDragNodeTreeUI {
  NodeTreeUIItem *item;
};

namespace {

class NodePnlViewItem;
class NodeSocketViewItem;
class NodeTreeUIView;

class NodeTreeUIDragController : public AbstractViewItemDragController {
 private:
  NodeTreeUIItem &item_;

 public:
  explicit NodeTreeUIDragController(NodeTreeUIView &view,
                                    NodeTreeUIItem &item);
  virtual ~NodeTreeUIDragController() = default;

  eWinDragDataType get_drag_type() const;

  void *create_drag_data() const;
};

class NodeSocketDropTarget : public TreeViewItemDropTarget {
 private:
  NodeTreeUISocket &socket_;

 public:
  explicit NodeSocketDropTarget(NodeSocketViewItem &item, NodeTreeUISocket &socket);

  bool can_drop(const WinDrag &drag, const char **r_disabled_hint) const override;
  std::string drop_tooltip(const DragInfo &drag_info) const override;
  bool on_drop(Cxt * /*C*/, const DragInfo &drag_info) const override;

 protected:
  WinDragNodeTreeUI *get_drag_node_tree_declaration(const WinDrag &drag) const;
};

class NodePnlDropTarget : public TreeViewItemDropTarget {
 private:
  NodeTreeUIPnl &pnl_;

 public:
  explicit NodePnlDropTarget(NodePnlViewItem &item, NodeTreeUIPnl &pnl);

  bool can_drop(const WinDrag &drag, const char **r_disabled_hint) const override;
  std::string drop_tooltip(const DragInfo &drag_info) const override;
  bool on_drop(Cxt *C, const DragInfo &drag_info) const override;

 protected:
  WinDragNodeTreeUI *get_drag_node_tree_declaration(const WinDrag &drag) const;
};

class NodeSocketViewItem : public BasicTreeViewItem {
 private:
  NodeTree &nodetree_;
  NodeTreeInterfaceSocket &socket_;

 public:
  NodeSocketViewItem(NodeTree &nodetree,
                     NodeTreeUI &ui,
                     NodeTreeUISocket &socket)
      : BasicTreeViewItem(socket.name, ICON_NONE), nodetree_(nodetree), socket_(socket)
  {
    set_is_active_fn([ui, &socket]() { return ui.active_item() == &socket.item; });
    set_on_activate_fn([&ui](Cxt & /*C*/, BasicTreeViewItem &new_active) {
      NodeSocketViewItem &self = static_cast<NodeSocketViewItem &>(new_active);
      ui.active_item_set(&self.socket_.item);
    });
  }

  void build_row(uiLayout &row) override
  {
    uiLayoutSetPropDecorate(&row, false);

    uiLayout *input_socket_layout = uiLayoutRow(&row, true);
    if (socket_.flag & NODE_UI_SOCKET_INPUT) {
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
      /* Socket template only draws in embossed layouts */
      uiLayoutSetEmboss(output_socket_layout, UI_EMBOSS);
      /* Cxt is not used by the template function. */
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
    nodetree_.tree_ui.tag_items_changed();
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
  NodeTreeInterfacePnl &pnl_;

 public:
  NodePnlViewItem(NodeTree &nodetree,
                  NodeTreeUI &ui,
                  NodeTreeUIPnl &pnl)
      : BasicTreeViewItem(pnl.name, ICON_NONE), nodetree_(nodetree), pnl_(pnl)
  {
    set_is_active_fn([ui, &pnl]() { return ui.active_item() == &pnl.item; });
    set_on_activate_fn([&ui](Cxt & /*C*/, BasicTreeViewItem &new_active) {
      NodePnlViewItem &self = static_cast<NodePnlViewItem &>(new_active);
      ui.active_item_set(&self.pnl_.item);
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

    return &pnl_ == &other_item->panel_;
  }

  bool supports_renaming() const override
  {
    return true;
  }
  bool rename(const Cxt &C, StringRefNull new_name) override
  {
    MEM_SAFE_FREE(pnl_.name);

    pnl_.name = lib_strdup(new_name.c_str());
    nodetree_.tree_ui.tag_items_changed();
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

class NodeTreeUIView : public AbstractTreeView {
 private:
  NodeTree &nodetree_;
  NodeTreeUI &ui_;

 public:
  explicit NodeTreeUIView(NodeTree &nodetree, NodeTreeUI &ui)
      : nodetree_(nodetree), ui_(ui)
  {
  }

  NodeTree &nodetree()
  {
    return nodetree_;
  }

  NodeTreeUI &ui()
  {
    return ui_;
  }

  void build_tree() override
  {
    /* Draw root items */
    this->add_items_for_pnl_recursive(ui_.root_pnl, *this);
  }

 protected:
  void add_items_for_pnl_recursive(NodeTreeUIfacePnl &parent,
                                   ui::TreeViewOrItem &parent_item)
  {
    for (NodeTreeUIItem *item : parent.items()) {
      switch (item->item_type) {
        case NODE_UI_SOCKET: {
          NodeTreeUISocket *socket = node_ui::get_item_as<NodeTreeUISocket>(
              item);
          NodeSocketViewItem &socket_item = parent_item.add_tree_item<NodeSocketViewItem>(
              nodetree_, ui_, *socket);
          socket_item.set_collapsed(false);
          break;
        }
        case NODE_UI_PNL: {
          NodeTreeUIPnl *pnl = node_ui::get_item_as<NodeTreeUIPnl>(
              item);
          NodePnlViewItem &pnl_item = parent_item.add_tree_item<NodePnlViewItem>(
              nodetree_, ui_, *pnl);
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
      static_cast<NodeTreeUIView &>(this->get_tree_view()), socket_.item);
}

std::unique_ptr<TreeViewItemDropTarget> NodeSocketViewItem::create_drop_target()
{
  return std::make_unique<NodeSocketDropTarget>(*this, socket_);
}

std::unique_ptr<AbstractViewItemDragController> NodePnlViewItem::create_drag_controller() const
{
  return std::make_unique<NodeTreeUIDragController>(
      static_cast<NodeTreeUIView &>(this->get_tree_view()), pnl_.item);
}

std::unique_ptr<TreeViewItemDropTarget> NodePnlViewItem::create_drop_target()
{
  return std::make_unique<NodePnlDropTarget>(*this, pnl_);
}

NodeTreeUIDragController::NodeTreeUIDragController(NodeTreeUIView &view,
                                                   NodeTreeUIItem &item)
    : AbstractViewItemDragController(view), item_(item)
{
}

eWinDragDataType NodeTreeUIDragController::get_drag_type() const
{
  return WIN_DRAG_NODE_TREE_UI;
}

void *NodeTreeUIDragController::create_drag_data() const
{
  WinDragNodeTreeUI *drag_data = mem_cnew<WinDragNodeTreeUI>(__func__);
  drag_data->item = &item_;
  return drag_data;
}

NodeSocketDropTarget::NodeSocketDropTarget(NodeSocketViewItem &item,
                                           NodeTreeUISocket &socket)
    : TreeViewItemDropTarget(item, DropBehavior::Reorder), socket_(socket)
{
}

bool NodeSocketDropTarget::can_drop(const WinDrag &drag, const char ** /*r_disabled_hint*/) const
{
  if (drag.type != WIN_DRAG_NODE_TREE_UI) {
    return false;
  }
  WinDragNodeTreeUI *drag_data = get_drag_node_tree_declaration(drag);

  /* Can't drop an item onto its children. */
  if (const NodeTreeUIfacePnl *pnl = node_interface::get_item_as<NodeTreeUIPnl>(
          drag_data->item))
  {
    if (pnl->contains(socket_.item)) {
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
  WinDragNodeTreeUI *drag_data = get_drag_node_tree_declaration(drag_info.drag_data);
  lib_assert(drag_data != nullptr);
  NodeTreeInterfaceItem *drag_item = drag_data->item;
  lib_assert(drag_item != nullptr);

  NodeTree &nodetree = this->get_view<NodeTreeUIView>().nodetree();
  NodeTreeUI &ui = this->get_view<NodeTreeUIView>().ui();

  NodeTreeUIPnl *parent = ui.find_item_parent(socket_.item, true);
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

  ui.move_item_to_parent(*drag_item, parent, index);

  /* General update */
  ed_node_tree_propagate_change(C, cxt_data_main(C), &nodetree);
  ed_undo_push(C, "Insert node group item");
  return true;
}

WinDragNodeTreeUI *NodeSocketDropTarget::get_drag_node_tree_declaration(
    const WinDrag &drag) const
{
  lib_assert(drag.type == WIN_DRAG_NODE_TREE_UI);
  return static_cast<WinDragNodeTreeUI *>(drag.ptr);
}

NodePnlDropTarget::NodePnlDropTarget(NodePnlViewItem &item, NodeTreeUIPnl &pnl)
    : TreeViewItemDropTarget(item, DropBehavior::ReorderAndInsert), pnl_(pnl)
{
}

bool NodePnlDropTarget::can_drop(const WinDrag &drag, const char ** /*r_disabled_hint*/) const
{
  if (drag.type != WIN_DRAG_NODE_TREE_UI) {
    return false;
  }
  WinDragNodeTreeUI *drag_data = get_drag_node_tree_declaration(drag);

  /* Can't drop an item onto its children. */
  if (const NodeTreeUIPnl *pnl = node_ui::get_item_as<NodeTreeUIPnl>(
          drag_data->item))
  {
    if (pnl->contains(pnl_.item)) {
      return false;
    }
  }

  return true;
}

std::string NodePnlDropTarget::drop_tooltip(const DragInfo &drag_info) const
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

bool NodePnlDropTarget::on_drop(Cxt *C, const DragInfo &drag_info) const
{
  WinDragNodeTreeUI *drag_data = get_drag_node_tree_declaration(drag_info.drag_data);
  lib_assert(drag_data != nullptr);
  NodeTreeUIItem *drag_item = drag_data->item;
  lib_assert(drag_item != nullptr);

  NodeTree &nodetree = get_view<NodeTreeUIView>().nodetree();
  NodeTreeUI &ui = get_view<NodeTreeUIView>().ui();

  NodeTreeUIPnl *parent = nullptr;
  int index = -1;
  switch (drag_info.drop_location) {
    case DropLocation::Into: {
      /* Insert into target */
      prnt = &pnl_;
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

  ui.move_item_to_parent(*drag_item, parent, index);

  /* General update */
  ed_node_tree_propagate_change(C, cxt_data_main(C), &nodetree);
  ed_undo_push(C, "Insert node group item");
  return true;
}

WinDragNodeTreeUI *NodePnlDropTarget::get_drag_node_tree_declaration(
    const WinDrag &drag) const
{
  lib_assert(drag.type == WIN_DRAG_NODE_TREE_UI);
  return static_cast<WinDragNodeTreeUI *>(drag.ptr);
}

}  // namespace

}  // namespace dune::ui::nodes

void uiTemplateNodeTree(uiLayout *layout, ApiPtr *ptr)
{
  if (!ptr->data) {
    return;
  }
  if (!api_struct_is_a(ptr->type, &ApiNodeTreeUI)) {
    return;
  }
  NodeTree &nodetree = *reinterpret_cast<NodeTree *>(ptr->owner_id);
  NodeTreeUI &ui = *static_cast<NodeTreeUI *>(ptr->data);

  uiBlock *block = uiLayoutGetBlock(layout);

  dune::ui::AbstractTreeView *tree_view = ui_block_add_view(
      *block,
      "Node Tree Declaration Tree View",
      std::make_unique<dune::ui::nodes::NodeTreeUIView>(nodetree, ui));
  tree_view->set_min_rows(3);

  dune::ui::TreeViewBuilder::build_tree_view(*tree_view, *layout);
}
