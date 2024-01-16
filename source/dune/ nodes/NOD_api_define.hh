#pragma once

#include <optional>

#include "lib_fn_ref.hh"
#include "api_define.hh"
#include "win_types.hh" /* For notifier defines */

void api_Node_update(Main *main, Scene *scene, ApiPtr *ptr);
void aou_Node_socket_update(Main *main, Scene *scene, ApiPtr *ptr);

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
 * `bNode->custom1` or similar. */
#define NOD_inline_enum_accessors(member) \
  EnumRNAAccessors( \
      [](ApiPtr *ptr, ApiProp * /*prop*/) -> int { \
        const Node &node = *static_cast<const Node *>(ptr->data); \
        return node.member; \
      }, \
      [](ApiPtr *ptr, ApiProp * /*prop*/, const int val) { \
        Node &node = *static_cast<bNode *>(ptr->data); \
        node.member = val; \
      })

/* Generates accessor methods for a prop stored in `Node->storage`. This is expected to be
 * used in a node file that uses NODE_STORAGE_FNS. */
#define NOD_storage_enum_accessors(member) \
  EnumApiAccessors( \
      [](ApiPtr *ptr, ApiProp * /*prop*/) -> int { \
        const Node &node = *static_cast<const bNode *>(ptr->data); \
        return node_storage(node).member; \
      }, \
      [](ApiPtr *ptr, ApiProp * /*prop*/, const int val) { \
        Node &node = *static_cast<Node *>(ptr->data); \
        node_storage(node).member = val; \
      })

const EnumPropertyItem *enum_items_filter(const EnumPropertyItem *original_item_array,
                                          FunctionRef<bool(const EnumPropertyItem &item)> fn);

PropertyRNA *RNA_def_node_enum(StructRNA *srna,
                               const char *identifier,
                               const char *ui_name,
                               const char *ui_description,
                               const EnumPropertyItem *static_items,
                               const EnumRNAAccessors accessors,
                               std::optional<int> default_value = std::nullopt,
                               const EnumPropertyItemFunc item_func = nullptr);

}  // namespace blender::nodes
