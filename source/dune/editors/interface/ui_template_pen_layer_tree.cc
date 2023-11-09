#include "dune_cxt.h"
#include "dune_pen.hh"

#include "lang.h"

#include "graph.hh"

#include "ui.hh"
#include "ui_tree_view.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "ed_undo.hh"

#include "win_api.hh"

#include <fmt/format.h>

namespace dune::ui::pen {

using namespace dune::pen;

class LayerTreeView : public AbstractTreeView {
 public:
  explicit LayerTreeView(Pen &pen) : pen_(pen) {}

  void build_tree() override;

 private:
  void build_tree_node_recursive(TreeViewOrItem &parent, TreeNode &node);
  Pen &pen_;
};

class LayerNodeDropTarget : public TreeViewItemDropTarget {
  TreeNode &drop_tree_node_;

 public:
  LayerNodeDropTarget(AbstractTreeViewItem &item, TreeNode &drop_tree_node, DropBehavior behavior)
      : TreeViewItemDropTarget(item, behavior), drop_tree_node_(drop_tree_node)
  {
  }

  bool can_drop(const WinDrag &drag, const char ** /*r_disabled_hint*/) const override
  {
    return drag.type == WIN_DRAG_PEN_LAYER;
  }

  std::string drop_tooltip(const DragInfo &drag_info) const override
  {
    const WinDragPenLayer *drag_pen =
        static_cast<const WinDragPenLayer *>(drag_info.drag_data.ptr);
    Layer &drag_layer = drag_grease_pen->layer->wrap();

    std::string_view drag_name = drag_layer.name();
    std::string_view drop_name = drop_tree_node_.name();

    switch (drag_info.drop_location) {
      case DropLocation::Into:
        return fmt::format(TIP_("Move layer {} into {}"), drag_name, drop_name);
      case DropLocation::Before:
        return fmt::format(TIP_("Move layer {} above {}"), drag_name, drop_name);
      case DropLocation::After:
        return fmt::format(TIP_("Move layer {} below {}"), drag_name, drop_name);
      default:
        lib_assert_unreachable();
        break;
    }

    return "";
  }

  bool on_drop(Cxt *C, const DragInfo &drag_info) const override
  {
    const WinDragPenLayer *drag_pen =
        static_cast<const WinDragGreasePenLayer *>(drag_info.drag_data.poin);
    Pen &pen = *drag_pen->pen;
    Layer &drag_layer = drag_pen->layer->wrap();

    if (!drop_tree_node_.parent_group()) {
      /* Root node is not added to the tree view, so there should never be a drop target for this.
       */
      lib_assert_unreachable();
      return false;
    }

    if (&drop_tree_node_ == &drag_layer.as_node()) {
      return false;
    }

    switch (drag_info.drop_location) {
      case DropLocation::Into: {
        lib_assert_msg(drop_tree_node_.is_group(),
                       "Inserting should not be possible for layers, only for groups, because "
                       "only groups use DropBehavior::Reorder_and_Insert");
        LayerGroup &drop_group = drop_tree_node_.as_group();
        pen.move_node_into(drag_layer.as_node(), drop_group);
        break;
      }
      case DropLocation::Before: {
        /* Draw order is inverted, so inserting before (above) means inserting the node after. */
        pen.move_node_after(drag_layer.as_node(), drop_tree_node_);
        break;
      }
      case DropLocation::After: {
        /* Draw order is inverted, so inserting after (below) means inserting the node before. */
        pen.move_node_before_drag_layer.as_node(), drop_tree_node_);
        break;
      }
      default: {
        lib_assert_unreachable();
        return false;
      }
    }

    graph_id_tag_update(&pen.id, ID_RECALC_GEOMETRY);
    win_ev_add_notifier(C, NC_PEN | NA_EDITED, nullptr);
    return true;
  }
};

class LayerViewItemDragController : public AbstractViewItemDragController {
  Pen &pen_;
  Layer &dragged_layer_;

 public:
  LayerViewItemDragController(LayerTreeView &tree_view, Pen &pen, Layer &layer)
      : AbstractViewItemDragController(tree_view),
        pen_(pen),
        dragged_layer_(layer)
  {
  }

  eWinDragDataType get_drag_type() const override
  {
    return WIN_DRAG_PEN_LAYER;
  }

  void *create_drag_data() const override
  {
    WinDragPenLayer *drag_data = mem_new<WinDragPenLayer>(__func__);
    drag_data->layer = &dragged_layer_;
    drag_data->pen = &pen_;
    return drag_data;
  }

  void on_drag_start() override
  {
    pen_.set_active_layer(&dragged_layer_);
  }
};

class LayerViewItem : public AbstractTreeViewItem {
 public:
  LayerViewItem(Pen &pen, Layer &layer)
      : pen_(pen), layer_(layer)
  {
    this->label_ = layer.name();
  }

  void build_row(uiLayout &row) override
  {
    build_layer_name(row);

    uiLayout *sub = uiLayoutRow(&row, true);
    uiLayoutSetPropDecorate(sub, false);

    build_layer_btns(*sub);
  }

  bool supports_collapsing() const override
  {
    return false;
  }

  std::optional<bool> should_be_active() const override
  {
    if (this->pen_.has_active_layer()) {
      return reinterpret_cast<PenLayer *>(&layer_) == this->pen_.active_layer;
    }
    return {};
  }

  void on_activate(Cxt &C) override
  {
    ApiPtr pen_ptr = api_ptr_create(
        &pen_.id, &ApiPenv3Layers, nullptr);
    ApiPtr value_ptr = api_ptr_create(&pen_.id, &ApiPenLayer, &layer_);

    ApiProp *prop = api_struct_find_prop(&pen_ptr, "active");

    api_prop_ptr_set(&pen_ptr, prop, value_ptr, nullptr);
    api_prop_update(&C, &pen_ptr, prop);

    ed_undo_push(&C, "Active Pen Layer");
  }

  bool supports_renaming() const override
  {
    return true;
  }

  bool rename(const Cxt &C, StringRefNull new_name) override
  {
    ApiPtr layer_ptr = api_ptr_create(&pen_.id, &ApiPenLayer, &layer_);
    ApiProp *prop = api_struct_find_prop(&layer_ptr, "name");

    api_prop_string_set(&layer_ptr, prop, new_name.c_str());
    api_prop_update(&const_cast<Cxt &>(C), &layer_ptr, prop);

    ed_undo_push(&const_cast<Cxt &>(C), "Rename Pen Layer");
    return true;
  }

  StringRef get_rename_string() const override
  {
    return layer_.name();
  }

  std::unique_ptr<AbstractViewItemDragController> create_drag_controller() const override
  {
    return std::make_unique<LayerViewItemDragController>(
        static_cast<LayerTreeView &>(get_tree_view()), pen_, layer_);
  }

  std::unique_ptr<TreeViewItemDropTarget> create_drop_target() override
  {
    return std::make_unique<LayerNodeDropTarget>(*this, layer_.as_node(), DropBehavior::Reorder);
  }

 private:
  Pen &pen_;
  Layer &layer_;

  void build_layer_name(uiLayout &row)
  {
    Btn *btn = uiItemL_ex(
        &row, layer_.name().c_str(), ICON_OUTLINER_DATA_PEN_LAYER, false, false);
    if (layer_.is_locked() || !layer_.parent_group().is_visible()) {
      btn_disable(btn, "Layer is locked or not visible");
    }
  }

  void build_layer_btns(uiLayout &row)
  {
    Btn *btn;
    ApiPtr layer_ptr = api_ptr_create(&pen_.id, &ApiPenLayer, &layer_);

    uiBlock *block = uiLayoutGetBlock(&row);
    btn = BtnIconR(block,
                   BTYPE_ICON_TOGGLE,
                   0,
                   ICON_NONE,
                   0,
                   0,
                   UNIT_X,
                   UNIT_Y,
                   &layer_ptr,
                   "hide",
                   0,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   nullptr);
    if (!layer_.parent_group().is_visible()) {
      btn_flag_enable(btn, BTN_INACTIVE);
    }

    btn = BtnIconR(block,
                   BTYPE_ICON_TOGGLE,
                   0,
                   ICON_NONE,
                   0,
                   0,
                   UNIT_X,
                   UNIT_Y,
                   &layer_ptr,
                   "lock",
                   0,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   nullptr);
    if (layer_.parent_group().is_locked()) {
      btn_flag_enable(btn, BTN_INACTIVE);
    }
  }
};

class LayerGroupViewItem : public AbstractTreeViewItem {
 public:
  LayerGroupViewItem(Pen &pen, LayerGroup &group)
      : pen_(pen), group_(group)
  {
    this->disable_activatable();
    this->label_ = group_.name();
  }

  void build_row(uiLayout &row) override
  {
    build_layer_group_name(row);

    uiLayout *sub = uiLayoutRow(&row, true);
    uiLayoutSetPropDecorate(sub, false);

    build_layer_group_btns(*sub);
  }

  bool supports_renaming() const override
  {
    return true;
  }

  bool rename(const Cxt &C, StringRefNull new_name) override
  {
    ApiPtr group_ptr = api_ptr_create(
        &pen_.id, &ApiPenLayerGroup, &group_);
    ApiProp *prop = api_struct_find_prop(&group_ptr, "name");

    api_prop_string_set(&group_ptr, prop, new_name.c_str());
    api_prop_update(&const_cast<Cxt &>(C), &group_ptr, prop);

    ed_undo_push(&const_cast<Cxt &>(C), "Rename Pen Layer Group");
    return true;
  }

  StringRef get_rename_string() const override
  {
    return group_.name();
  }

  std::unique_ptr<TreeViewItemDropTarget> create_drop_target() override
  {
    return std::make_unique<LayerNodeDropTarget>(
        *this, group_.as_node(), DropBehavior::ReorderAndInsert);
  }

 private:
  Pen &pen_;
  LayerGroup &group_;

  void build_layer_group_name(uiLayout &row)
  {
    uiItemS_ex(&row, 0.8f);
    Btn *btn = uiItemL_ex(&row, group_.name().c_str(), ICON_FILE_FOLDER, false, false);
    if (group_.is_locked()) {
      ui_btn_disable(but, "Layer Group is locked");
    }
  }

  void build_layer_group_btns(uiLayout &row)
  {
    ApiPtr group_ptr = api_ptr_create(
        &pen_.id, &ApiPenLayerGroup, &group_);

    uiItemR(&row, &group_ptr, "hide", UI_ITEM_R_ICON_ONLY, nullptr, ICON_NONE);
    uiItemR(&row, &group_ptr, "lock", UI_ITEM_R_ICON_ONLY, nullptr, ICON_NONE);
  }
};

void LayerTreeView::build_tree_node_recursive(TreeViewOrItem &parent, TreeNode &node)
{
  using namespace dune::pen;
  if (node.is_layer()) {
    LayerViewItem &item = parent.add_tree_item<LayerViewItem>(this->pen_,
                                                              node.as_layer());
    item.set_collapsed(false);
  }
  else if (node.is_group()) {
    LayerGroupViewItem &group_item = parent.add_tree_item<LayerGroupViewItem>(this->pen_,
                                                                              node.as_group());
    group_item.set_collapsed(false);
    LIST_FOREACH_BACKWARD (PenLayerTreeNode *, node_, &node.as_group().children) {
      build_tree_node_recursive(group_item, node_->wrap());
    }
  }
}

void LayerTreeView::build_tree()
{
  using namespace dune::pen;
  LIST_FOREACH_BACKWARD (
      PenLayerTreeNode *, node, &this->pen_.root_group_ptr->children)
  {
    this->build_tree_node_recursive(*this, node->wrap());
  }
}

}  // namespace dune::ui::pen

void uiTemplatePenLayerTree(uiLayout *layout, Cxt *C)
{
  using namespace dune;

  Object *object = cxt_data_active_object(C);
  if (!object || object->type != OB_PEN) {
    return;
  }
  Pen &pen = *static_cast<Pen *>(object->data);

  uiBlock *block = uiLayoutGetBlock(layout);

  ui::AbstractTreeView *tree_view = ui_block_add_view(
      *block,
      "Pen Layer Tree View",
      std::make_unique<dune::ui::pen::LayerTreeView>(pen));
  tree_view->set_min_rows(3);

  ui::TreeViewBuilder::build_tree_view(*tree_view, *layout);
}
