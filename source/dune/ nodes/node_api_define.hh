#pragma once

#include <optional>

#include "lib_fn_ref.hh"
#include "api_define.hh"
#include "win_types.hh" /* For notifier defines */

void api_Node_update(Main *main, Scene *scene, ApiPtr *ptr);
void api_Node_socket_update(Main *main, Scene *scene, ApiPtr *ptr);

namespace dune::nodes {

struct EnumApiAccessors {
  EnumPropGetFn getter;
  EnumPropSetFn setter;

  EnumApiAccessors(EnumPropGetFn getter, EnumPropSetFn setter)
      : getter(getter), setter(setter)
  {
  }
};

 /* Generates accessor methods for a prop stored directly in the `bNode`, typically
 * `Node->custom1` or similar. */
#define node_inline_enum_accessors(member) \
  EnumApiAccessors( \
      [](ApiPtr *ptr, ApiProp * /*prop*/) -> int { \
        const Node &node = *static_cast<const Node *>(ptr->data); \
        return node.member; \
      }, \
      [](ApiPtr *ptr, ApiProp * /*prop*/, const int val) { \
        Node &node = *static_cast<Node *>(ptr->data); \
        node.member = val; \
      })

/* Generates accessor methods for a prop stored in `Node->storage`. This is expected to be
 * used in a node file that uses NODE_STORAGE_FNS. */
#define NOD_storage_enum_accessors(member) \
  EnumApiAccessors( \
      [](ApiPtr *ptr, ApiProp * /*prop*/) -> int { \
        const Node &node = *static_cast<const Node *>(ptr->data); \
        return node_storage(node).member; \
      }, \
      [](ApiPtr *ptr, ApiProp * /*prop*/, const int val) { \
        Node &node = *static_cast<Node *>(ptr->data); \
        node_storage(node).member = val; \
      })

const EnumPropItem *enum_items_filter(const EnumPropItem *original_item_arr,
                                      FnRef<bool(const EnumPropItem &item)> fn);

ApiProp *api_def_node_enum(ApiStruct *sapi,
                               const char *id,
                               const char *ui_name,
                               const char *ui_description,
                               const EnumPropItem *static_items,
                               const EnumApiAccessors accessors,
                               std::optional<int> default_val = std::nullopt,
                               const EnumPropItemFn item_fn = nullptr);

}  // namespace dune::nodes
