#include "ui.hh"

#include <cstdio>
#include <memory>

#include <fmt/format.h>

#include "lang.h"

#include "types_collection.h"
#include "types_object.h"

#include "dune_cxt.h"
#include "dune_light_linking.h"

#include "api_access.hh"
#include "api_prototypes.h"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_tree_view.hh"

#include "win_api.hh"

#include "ed_undo.hh"

namespace dune::ui::light_linking {

namespace {

class CollectionDropTarget {
  Collection &collection_;

 public:
  bool can_drop(const WinDrag &drag, const char **r_disabled_hint) const
  {
    if (drag.type != WIN_DRAG_ID) {
      return false;
    }

    const WinDragId *drag_id = static_cast<WinDragId *>(drag.ids.first);
    if (!drag_id) {
      return false;
    }

    /* The dragged Ids are guaranteed to be the same type, so only check the type of the first one. */
    const IdType id_type = GS(drag_id->id->name);
    if (!ELEM(id_type, ID_OB, ID_GR)) {
      *r_disabled_hint = "Can only add objects and collections to the light linking collection";
      return false;
    }

    return true;
  }

  CollectionDropTarget(Collection &collection) : collection_(collection) {}

  Collection &get_collection() const
  {
    return collection_;
  }
};

/* Drop target for the view (when dropping into empty space of the view), not for an item. */
class InsertCollectionDropTarget : public DropTargetInterface {
  CollectionDropTarget collection_target_;

 public:
  InsertCollectionDropTarget(Collection &collection) : collection_target_(collection) {}

  bool can_drop(const WinDrag &drag, const char **r_disabled_hint) const override
  {
    return collection_target_.can_drop(drag, r_disabled_hint);
  }

  std::string drop_tooltip(const DragInfo & /*drag*/) const override
  {
    return TIP_("Add to linking collection");
  }

  bool on_drop(Cxt *C, const DragInfo &drag) const override
  {
    Main *main = cxt_data_main(C);
    Scene *scene = cxt_data_scene(C);

    LIST_FOREACH (WinDragId *, drag_id, &drag.drag_data.ids) {
      dune_light_linking_add_receiver_to_collection(main,
                                                   &collection_target_.get_collection(),
                                                   drag_id->id,
                                                   COLLECTION_LIGHT_LINKING_STATE_INCLUDE);
    }

    /* It is possible that the light linking collection is also used by the view layer.
     * For this case send a notifier so that the UI is updated for the changes in the collection
     * content. */
    win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

    ed_undo_push(C, "Add to linking collection");

    return true;
  }
};

class ReorderCollectionDropTarget : public TreeViewItemDropTarget {
  CollectionDropTarget collection_target_;
  const Id &drop_id_;

 public:
  ReorderCollectionDropTarget(AbstractTreeViewItem &item,
                              Collection &collection,
                              const Id &drop_id)
      : TreeViewItemDropTarget(item, DropBehavior::Reorder),
        collection_target_(collection),
        drop_id_(drop_id)
  {
  }

  bool can_drop(const WinDrag &drag, const char **r_disabled_hint) const override
  {
    return collection_target_.can_drop(drag, r_disabled_hint);
  }

  std::string drop_tooltip(const DragInfo &drag) const override
  {
    const std::string_view drop_name = std::string_view(drop_id_.name + 2);

    switch (drag.drop_location) {
      case DropLocation::Into:
        return "Add to linking collection";
      case DropLocation::Before:
        return fmt::format(TIP_("Add to linking collection before {}"), drop_name);
      case DropLocation::After:
        return fmt::format(TIP_("Add to linking collection after {}"), drop_name);
    }

    return "";
  }

  bool on_drop(Cxt *C, const DragInfo &drag) const override
  {
    Main *main = cxt_data_main(C);
    Scene *scene = cxt_data_scene(C);

    Collection &collection = collection_target_.get_collection();
    const eCollectionLightLinkingState link_state = COLLECTION_LIGHT_LINKING_STATE_INCLUDE;

    LIST_FOREACH (WinDragId *, drag_id, &drag.drag_data.ids) {
      if (drag_id->id == &drop_id_) {
        continue;
      }

      dune_light_linking_unlink_id_from_collection(main, &collection, drag_id->id, nullptr);

      switch (drag.drop_location) {
        case DropLocation::Into:
          dune_light_linking_add_receiver_to_collection(
              main, &collection, drag_id->id, link_state);
          break;
        case DropLocation::Before:
          dune_light_linking_add_receiver_to_collection_before(
              main, &collection, drag_id->id, &drop_id_, link_state);
          break;
        case DropLocation::After:
          dune_light_linking_add_receiver_to_collection_after(
              main, &collection, drag_id->id, &drop_id_, link_state);
          break;
      }
    }

    /* It is possible that the light linking collection is also used by the view layer.
     * For this case send a notifier so that the UI is updated for the changes in the collection
     * content. */
    win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

    ED_undo_push(C, "Add to linking collection");

    return true;
  }
};

class ItemDragController : public AbstractViewItemDragController {
  Id &id_;

 public:
  explicit ItemDragController(AbstractView &view, Id &id)
      : AbstractViewItemDragController(view), id_(id)
  {
  }

  eWinDragDataType get_drag_type() const override
  {
    return WIN_DRAG_ID;
  }

  void *create_drag_data() const override
  {
    return static_cast<void *>(&id_);
  }
};

class CollectionViewItem : public BasicTreeViewItem {
  uiLayout &cxt_layout_;
  Collection &collection_;

  Id &id_;
  CollectionLightLinking &collection_light_linking_;

 public:
  CollectionViewItem(uiLayout &cxt_layout,
                     Collection &collection,
                     Id &id,
                     CollectionLightLinking &collection_light_linking,
                     const BIFIconId icon)
      : BasicTreeViewItem(id.name + 2, icon),
        cxt_layout_(cxt_layout),
        collection_(collection),
        id_(id),
        collection_light_linking_(collection_light_linking)
  {
  }

  void build_row(uiLayout &row) override
  {
    if (is_active()) {
      ApiPtr id_ptr = api_id_ptr_create(&id_);
      ApiPtr collection_ptr = api_id_ptr_create(&collection_.id);

      uiLayoutSetCxtPtr(&cxt_layout_, "id", &id_ptr);
      uiLayoutSetCxtPtr(&cxt_layout_, "collection", &collection_ptr);
    }

    add_label(row);

    uiLayout *sub = uiLayoutRow(&row, true);
    uiLayoutSetPropDecorate(sub, false);

    build_state_btn(*sub);
  }

  std::unique_ptr<AbstractViewItemDragController> create_drag_controller() const override
  {
    return std::make_unique<ItemDragController>(get_tree_view(), id_);
  }

  std::unique_ptr<TreeViewItemDropTarget> create_drop_target() override
  {
    return std::make_unique<ReorderCollectionDropTarget>(*this, collection_, id_);
  }

 private:
  int get_state_icon() const
  {
    switch (collection_light_linking_.link_state) {
      case COLLECTION_LIGHT_LINKING_STATE_INCLUDE:
        return ICON_CHECKBOX_HLT;
      case COLLECTION_LIGHT_LINKING_STATE_EXCLUDE:
        return ICON_CHECKBOX_DEHLT;
    }
    lib_assert_unreachable();
    return ICON_NONE;
  }

  static void link_state_toggle(CollectionLightLinking &collection_light_linking)
  {
    switch (collection_light_linking.link_state) {
      case COLLECTION_LIGHT_LINKING_STATE_INCLUDE:
        collection_light_linking.link_state = COLLECTION_LIGHT_LINKING_STATE_EXCLUDE;
        return;
      case COLLECTION_LIGHT_LINKING_STATE_EXCLUDE:
        collection_light_linking.link_state = COLLECTION_LIGHT_LINKING_STATE_INCLUDE;
        return;
    }

    lib_assert_unreachable();
  }

  void build_state_btn(uiLayout &row)
  {
    uiBlock *block = uiLayoutGetBlock(&row);
    const int icon = get_state_icon();

    ApiPtr collection_light_linking_ptr = api_ptr_create(
        &collection_.id, &ApiCollectionLightLinking, &collection_light_linking_);

    Btn *btn = BtnIconR(block,
                        BTYPE_BTN,
                        0,
                        icon,
                        0,
                        0,
                        UNIT_X,
                        UNIT_Y,
                        &collection_light_linking_ptr,
                        "link_state",
                        0,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        nullptr);

    btn_fn_set(btn, [&collection_light_linking = collection_light_linking_](Cxt &) {
      link_state_toggle(collection_light_linking);
    });
  }
};

class CollectionView : public AbstractTreeView {
  uiLayout &cxt_layout_;
  Collection &collection_;

 public:
  CollectionView(uiLayout &cxt_layout, Collection &collection)
      : cxt_layout_(cxt_layout), collection_(collection)
  {
  }

  void build_tree() override
  {
    LIST_FOREACH (CollectionChild *, collection_child, &collection_.children) {
      Collection *child_collection = collection_child->collection;
      add_tree_item<CollectionViewItem>(cxt_layout_,
                                        collection_,
                                        child_collection->id,
                                        collection_child->light_linking,
                                        ICON_OUTLINER_COLLECTION);
    }

    LIST_FOREACH (CollectionObject *, collection_object, &collection_.gobject) {
      Object *child_object = collection_object->ob;
      add_tree_item<CollectionViewItem>(cxt_layout_,
                                        collection_,
                                        child_object->id,
                                        collection_object->light_linking,
                                        ICON_OBJECT_DATA);
    }
  }

  std::unique_ptr<DropTargetInterface> create_drop_target() override
  {
    return std::make_unique<InsertCollectionDropTarget>(collection_);
  }
};

}  // namespace

}  // namespace dune::ui::light_linking

void uiTemplateLightLinkingCollection(uiLayout *layout,
                                      uiLayout *cxt_layout,
                                      ApiPtr *ptr,
                                      const char *propname)
{
  if (!ptr->data) {
    return;
  }

  ApiProp *prop = api_struct_find_prop(ptr, propname);
  if (!prop) {
    printf(
        "%s: prop not found: %s.%s\n", __func__, api_struct_id(ptr->type), propname);
    return;
  }

  if (api_prop_type(prop) != PROP_PTR) {
    printf("%s: expected ptr prop for %s.%s\n",
           __func__,
           api_struct_id(ptr->type),
           propname);
    return;
  }

  const ApiPtr collection_ptr = api_prop_ptr_get(ptr, prop);
  if (!collection_ptr.data) {
    return;
  }
  if (collection_ptr.type != &ApiCollection) {
    printf("%s: expected collection ptr prop for %s.%s\n",
           __func__,
           api_struct_id(ptr->type),
           propname);
    return;
  }

  Collection *collection = static_cast<Collection *>(collection_ptr.data);

  uiBlock *block = uiLayoutGetBlock(layout);

  dune::ui::AbstractTreeView *tree_view = ui_block_add_view(
      *block,
      "Light Linking Collection Tree View",
      std::make_unique<dune::ui::light_linking::CollectionView>(*cxt_layout, *collection));
  tree_view->set_min_rows(3);

  dune::ui::TreeViewBuilder::build_tree_view(*tree_view, *layout);
}
