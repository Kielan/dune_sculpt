#include <fmt/format.h>

#include "node_extra_info.hh"
#include "node_api_define.hh"
#include "node_zone_socket_items.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "lib_string.h"

#include "dune_bake_geo_nodes_mod.hh"
#include "dune_bake_items_socket.hh"
#include "dune_cxt.hh"

#include "ed_node.hh"

#include "types_mod.h"

#include "api_access.hh"
#include "api_prototypes.h"

#include "mod_nodes.hh"

#include "win_api.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_bake_cc {

namespace bake = dune::bake;

NODE_STORAGE_FNS(NodeGeoBake)

static void node_decl(NodeDeclBuilder &b)
{
  const Node *node = b.node_or_null();
  if (!node) {
    return;
  }
  const NodeGeoBake &storage = node_storage(*node);

  for (const int i : IndexRange(storage.items_num)) {
    const NodeGeoBakeItem &item = storage.items[i];
    const eNodeSocketDatatype socket_type = eNodeSocketDatatype(item.socket_type);
    const StringRef name = item.name;
    const std::string id = BakeItemsAccessor::socket_id_for_item(item);
    auto &input_decl = b.add_input(socket_type, name, id);
    auto &output_decl = b.add_output(socket_type, name, id);
    if (socket_type_supports_fields(socket_type)) {
      input_decl.supports_field();
      if (item.flag & GEO_NODE_BAKE_ITEM_IS_ATTRIBUTE) {
        output_decl.field_source();
      }
      else {
        output_decl.dependent_field({input_decl.input_index()});
      }
    }
  }
  b.add_input<decl::Extend>("", "__extend__");
  b.add_output<decl::Extend>("", "__extend__");
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoBake *data = mem_cnew<NodeGeoBake>(__func__);

  data->items = mem_cnew_array<NodeGeoBakeItem>(1, __func__);
  data->items_num = 1;

  NodeGeometryBakeItem &item = data->items[0];
  item.name = lib_strdup("Geo");
  item.id = data->next_id++;
  item.attr_domain = int16_t(AttrDomain::Point);
  item.socket_type = SOCK_GEO;

  node->storage = data;
}

static void node_free_storage(Node *node)
{
  socket_items::destruct_array<BakeItemsAccessor>(*node);
  mem_freen(node->storage);
}

static void node_copy_storage(NodeTree * /*tree*/, Node *dst_node, const bNode *src_node)
{
  const NodeGeoBake &src_storage = node_storage(*src_node);
  auto *dst_storage = mem_new<NodeGeoBake>(__func__, src_storage);
  dst_node->storage = dst_storage;

  socket_items::copy_array<BakeItemsAccessor>(*src_node, *dst_node);
}

static bool node_insert_link(NodeTree *ntree, Node *node, NodeLink *link)
{
  return socket_items::try_add_item_via_any_extend_socket<BakeItemsAccessor>(
      *ntree, *node, *node, *link);
}

static const CPPType &get_item_cpp_type(const eNodeSocketDatatype socket_type)
{
  const char *socket_idname = nodeStaticSocketType(socket_type, 0);
  const NodeSocketType *typeinfo = nodeSocketTypeFind(socket_idname);
  lib_assert(typeinfo);
  lib_assert(typeinfo->geo_nodes_cpp_type);
  return *typeinfo->geo_nodes_cpp_type;
}

static bake::BakeSocketConfig make_bake_socket_config(const Span<NodeGeoBakeItem> bake_items)
{
  bake::BakeSocketConfig config;
  const int items_num = bake_items.size();
  config.domains.resize(items_num);
  config.types.resize(items_num);
  config.geo_by_attr.resize(items_num);

  int last_geo_index = -1;
  for (const int item_i : bake_items.index_range()) {
    const NodeGeoBakeItem &item = bake_items[item_i];
    config.types[item_i] = eNodeSocketDatatype(item.socket_type);
    config.domains[item_i] = AttrDomain(item.attr_domain);
    if (item.socket_type == SOCK_GEO) {
      last_geo_index = item_i;
    }
    else if (last_geo_index != -1) {
      config.geo_by_attr[item_i].append(last_geo_index);
    }
  }
  return config;
}

class LazyFnForBakeNode final : public LazyFn {
  const Node &node_;
  Span<NodeGeoBakeItem> bake_items_;
  bake::BakeSocketConfig bake_socket_config_;

 public:
  LazyFnForBakeNode(const Node &node, GeoNodesLazyFnGraphInfo &lf_graph_info)
      : node_(node)
  {
    debug_name_ = "Bake";
    const NodeGeoBake &storage = node_storage(node);
    bake_items_ = {storage.items, storage.items_num};

    MutableSpan<int> lf_index_by_bsocket = lf_graph_info.mapping.lf_index_by_bsocket;

    for (const int i : bake_items_.index_range()) {
      const NodeGeoBakeItem &item = bake_items_[i];
      const NodeSocket &input_bsocket = node.input_socket(i);
      const NodeSocket &output_bsocket = node.output_socket(i);
      const CPPType &type = get_item_cpp_type(eNodeSocketDatatype(item.socket_type));
      lf_index_by_bsocket[input_bsocket.index_in_tree()] = inputs_.append_and_get_index_as(
          item.name, type, lf::ValUsage::Maybe);
      lf_index_by_bsocket[output_bsocket.index_in_tree()] = outputs_.append_and_get_index_as(
          item.name, type);
    }

    bake_socket_config_ = make_bake_socket_config(bake_items_);
  }

  void ex_impl(lf::Params &params, const lf::Cxt &cxt) const final
  {
    GeoNodesLFUserData &user_data = *static_cast<GeoNodesLFUserData *>(context.user_data);
    GeoNodesLFLocalUserData &local_user_data = *static_cast<GeoNodesLFLocalUserData *>(
        cxt.local_user_data);
    if (!user_data.call_data->self_object()) {
      /* The self object is currently required for generating anonymous attribute names. */
      this->set_default_outputs(params);
      return;
    }
    if (!user_data.call_data->bake_params) {
      this->set_default_outputs(params);
      return;
    }
    std::optional<FoundNestedNodeId> found_id = find_nested_node_id(user_data, node_.identifier);
    if (!found_id) {
      this->set_default_outputs(params);
      return;
    }
    if (found_id->is_in_loop) {
      this->set_default_outputs(params);
      return;
    }
    BakeNodeBehavior *behavior = user_data.call_data->bake_params->get(found_id->id);
    if (!behavior) {
      this->set_default_outputs(params);
      return;
    }
    if (auto *info = std::get_if<sim_output::ReadSingle>(behavior)) {
      this->output_cached_state(params, user_data, info->state);
    }
    else if (auto *info = std::get_if<sim_output::ReadInterpolated>(behavior)) {
      this->output_mixed_cached_state(params,
                                      *user_data.call_data->self_object(),
                                      *user_data.compute_cxt,
                                      info->prev_state,
                                      info->next_state,
                                      info->mix_factor);
    }
    else if (std::get_if<sim_output::PassThrough>(behavior)) {
      this->pass_through(params, user_data);
    }
    else if (auto *info = std::get_if<sim_output::StoreNewState>(behavior)) {
      this->store(params, user_data, *info);
    }
    else if (auto *info = std::get_if<sim_output::ReadError>(behavior)) {
      if (geo_eval_log::GeoTreeLogger *tree_logger = local_user_data.try_get_tree_logger(
              user_data))
      {
        tree_logger->node_warnings.append(
            {node_.id, {NodeWarningType::Error, info->msg}});
      }
      this->set_default_outputs(params);
    }
    else {
      lib_assert_unreachable();
    }
  }

  void set_default_outputs(lf::Params &params) const
  {
    set_default_remaining_node_outputs(params, node_);
  }

  void pass_through(lf::Params &params, GeoNodesLFUserData &user_data) const
  {
    std::optional<bake::BakeState> bake_state = this->get_bake_state_from_inputs(params);
    if (!bake_state) {
      /* Wait for inputs to be computed. */
      return;
    }
    Array<void *> output_vals(bake_items_.size());
    for (const int i : bake_items_.index_range()) {
      output_values[i] = params.get_output_data_ptr(i);
    }
    this->move_bake_state_to_vals(std::move(*bake_state),
                                    *user_data.call_data->self_object(),
                                    *user_data.compute_cxt,
                                    output_vals);
    for (const int i : bake_items_.index_range()) {
      params.output_set(i);
    }
  }

  void store(lf::Params &params,
             GeoNodesLFUserData &user_data,
             const sim_output::StoreNewState &info) const
  {
    std::optional<bake::BakeState> bake_state = this->get_bake_state_from_inputs(params);
    if (!bake_state) {
      /* Wait for inputs to be computed. */
      return;
    }
    this->output_cached_state(params, user_data, *bake_state);
    info.store_fn(std::move(*bake_state));
  }

  void output_cached_state(lf::Params &params,
                           GeoNodesLFUserData &user_data,
                           const bake::BakeStateRef &bake_state) const
  {
    Array<void *> output_vals(bake_items_.size());
    for (const int i : bake_items_.index_range()) {
      output_vals[i] = params.get_output_data_ptr(i);
    }
    this->copy_bake_state_to_vals(bake_state,
                                    *user_data.call_data->self_object(),
                                    *user_data.compute_context,
                                    output_vals);
    for (const int i : bake_items_.index_range()) {
      params.output_set(i);
    }
  }

  void output_mixed_cached_state(lf::Params &params,
                                 const Object &self_object,
                                 const ComputeCxt &compute_cxt,
                                 const bake::BakeStateRef &prev_state,
                                 const bake::BakeStateRef &next_state,
                                 const float mix_factor) const
  {
    Array<void *> output_vals(bake_items_.size());
    for (const int i : bake_items_.index_range()) {
      output_vals[i] = params.get_output_data_ptr(i);
    }
    this->copy_bake_state_to_vals(prev_state, self_object, compute_cxt, output_vals);

    Array<void *> next_vals(bake_items_.size());
    LinearAllocator<> allocator;
    for (const int i : bake_items_.index_range()) {
      const CPPType &type = *outputs_[i].type;
      next_vals[i] = allocator.allocate(type.size(), type.alignment());
    }
    this->copy_bake_state_to_values(next_state, self_object, compute_context, next_values);

    for (const int i : bake_items_.index_range()) {
      mix_baked_data_item(eNodeSocketDatatype(bake_items_[i].socket_type),
                          output_vals[i],
                          next_vals[i],
                          mix_factor);
    }

    for (const int i : bake_items_.index_range()) {
      const CPPType &type = *outputs_[i].type;
      type.destruct(next_vals[i]);
    }

    for (const int i : bake_items_.index_range()) {
      params.output_set(i);
    }
  }

  std::optional<bake::BakeState> get_bake_state_from_inputs(lf::Params &params) const
  {
    Array<void *> input_values(bake_items_.size());
    for (const int i : bake_items_.index_range()) {
      input_values[i] = params.try_get_input_data_ptr_or_request(i);
    }
    if (input_vals.as_span().contains(nullptr)) {
      /* Wait for inputs to be computed. */
      return std::nullopt;
    }

    Array<std::unique_ptr<bake::BakeItem>> bake_items = bake::move_socket_vals_to_bake_items(
        input_vals, bake_socket_config_);

    bake::BakeState bake_state;
    for (const int i : bake_items_.index_range()) {
      const NodeGeoBakeItem &item = bake_items_[i];
      std::unique_ptr<bake::BakeItem> &bake_item = bake_items[i];
      if (bake_item) {
        bake_state.items_by_id.add_new(item.id, std::move(bake_item));
      }
    }
    return bake_state;
  }

  void move_bake_state_to_vals(bake::BakeState bake_state,
                                 const Object &self_object,
                                 const ComputeCxt &compute_cxt,
                                 Span<void *> r_output_vals) const
  {
    Vector<bake::BakeItem *> bake_items;
    for (const NodeGeoBakeItem &item : bake_items_) {
      std::unique_ptr<bake::BakeItem> *bake_item = bake_state.items_by_id.lookup_ptr(
          item.id);
      bake_items.append(bake_item ? bake_item->get() : nullptr);
    }
    bake::move_bake_items_to_socket_values(
        bake_items,
        bake_socket_config_,
        [&](const int i, const CPPType &type) {
          return this->make_attr_field(self_object, compute_context, bake_items_[i], type);
        },
        r_output_vals);
  }

  void copy_bake_state_to_vals(const bake::BakeStateRef &bake_state,
                               const Object &self_object,
                               const ComputeCxt &compute_cxt,
                               Span<void *> r_output_vals) const
  {
    Vector<const bake::BakeItem *> bake_items;
    for (const NodeGeoBakeItem &item : bake_items_) {
      const bake::BakeItem *const *bake_item = bake_state.items_by_id.lookup_ptr(item.identifier);
      bake_items.append(bake_item ? *bake_item : nullptr);
    }
    bake::copy_bake_items_to_socket_vals(
        bake_items,
        bake_socket_config_,
        [&](const int i, const CPPType &type) {
          return this->make_attr_field(self_object, compute_cxt, bake_items_[i], type);
        },
        r_output_vals);
  }

  std::shared_ptr<AnonymousAttrFieldInput> make_attr_field(
      const Object &self_object,
      const ComputeCxt &compute_cxt,
      const NodeGeoBakeItem &item,
      const CPPType &type) const
  {
    AnonymousAttrIdPtr attribute_id = AnonymousAttributeIDPtr(
        mem_new<NodeAnonymousAttributeID>(__func__,
                                          self_object,
                                          compute_context,
                                          node_,
                                          std::to_string(item.identifier),
                                          item.name));
    return std::make_shared<AnonymousAttributeFieldInput>(
        attr_id, type, node_.label_or_name());
  }
};

struct BakeDrawCxt {
  const bNode *node;
  SpaceNode *snode;
  const Object *object;
  const NodesModifierData *nmd;
  const NodesModifierBake *bake;
  PointerRNA bake_rna;
  std::optional<IndexRange> baked_range;
  bool bake_still;
  bool is_baked;
};

[[nodiscard]] static bool get_bake_draw_context(const bContext *C,
                                                const bNode &node,
                                                BakeDrawContext &r_ctx)
{
  r_ctx.node = &node;
  r_ctx.snode = CTX_wm_space_node(C);
  if (!r_ctx.snode) {
    return false;
  }
  std::optional<ed::space_node::ObjectAndModifier> object_and_modifier =
      ed::space_node::get_modifier_for_node_editor(*r_ctx.snode);
  if (!object_and_mod) {
    return false;
  }
  r_ctx.object = object_and_mod->object;
  r_ctx.nmd = object_and_mod->nmd;
  const std::optional<int32_t> bake_id = ed::space_node::find_nested_node_id_in_root(*r_ctx.snode,
                                                                                     *r_ctx.node);
  if (!bake_id) {
    return false;
  }
  r_cxt.bake = nullptr;
  for (const NodesModBake &iter_bake : Span(r_cxt.nmd->bakes, r_cxt.nmd->bakes_num)) {
    if (iter_bake.id == *bake_id) {
      r_cxt.bake = &iter_bake;
      break;
    }
  }
  if (!r_cxt.bake) {
    return false;
  }

  r_cxt.bake_api = api_ptr_create(
      const_cast<Id *>(&r_cxt.object->id), &RNA_NodesModifierBake, (void *)r_ctx.bake);
  if (r_ctx.nmd->runtime->cache) {
    const bake::ModCache &cache = *r_ctx.nmd->runtime->cache;
    std::lock_guard lock{cache.mutex};
    if (const std::unique_ptr<bake::BakeNodeCache> *node_cache_ptr =
            cache.bake_cache_by_id.lookup_ptr(*bake_id))
    {
      const bake::BakeNodeCache &node_cache = **node_cache_ptr;
      if (!node_cache.bake.frames.is_empty()) {
        const int first_frame = node_cache.bake.frames.first()->frame.frame();
        const int last_frame = node_cache.bake.frames.last()->frame.frame();
        r_ctx.baked_range = IndexRange(first_frame, last_frame - first_frame + 1);
      }
    }
  }

  r_ctx.bake_still = r_cxt.bake->bake_mode == NODES_MOD_BAKE_MODE_STILL;
  r_ctx.is_baked = r_cxt.baked_range.has_val();

  return true;
}

static std::string get_baked_string(const BakeDrawCxt &cxt)
{
  if (cxt.bake_still && cxt.baked_range->size() == 1) {
    return fmt::format(RPT_("Baked Frame {}"), cxt.baked_range->first());
  }
  return fmt::format(RPT_("Baked {} - {}"), cxt.baked_range->first(), cxt.baked_range->last());
}

static void draw_bake_btn(uiLayout *layout, const BakeDrawCxt &cxt)
{
  uiLayout *col = uiLayoutColumn(layout, true);
  uiLayout *row = uiLayoutRow(col, true);
  {
    ApiPtr ptr;
    uiItemFullO(row,
                "OBJECT_OT_geo_node_bake_single",
                IFACE_("Bake"),
                ICON_NONE,
                nullptr,
                WIN_OP_INVOKE_DEFAULT,
                UI_ITEM_NONE,
                &ptr);
    win_op_props_id_lookup_set_from_id(&ptr, &cxt.object->id);
    api_string_set(&ptr, "mod_name", cxt.nmd->mod.name);
    api_int_set(&ptr, "bake_id", cxt.bake->id);
  }
  {
    uiLayout *subrow = uiLayoutRow(row, true);
    uiLayoutSetActive(subrow, cxt.is_baked);
    ApiPtr ptr;
    uiItemFullO(subrow,
                "OBJECT_OT_geo_node_bake_delete_single",
                "",
                ICON_TRASH,
                nullptr,
                WIN_OP_INVOKE_DEFAULT,
                UI_ITEM_NONE,
                &ptr);
    WM_operator_properties_id_lookup_set_from_id(&ptr, &ctx.object->id);
    RNA_string_set(&ptr, "modifier_name", ctx.nmd->modifier.name);
    RNA_int_set(&ptr, "bake_id", ctx.bake->id);
  }
}

static void node_extra_info(NodeExtraInfoParams &params)
{
  BakeDrawContext ctx;
  if (!get_bake_draw_context(&params.C, params.node, ctx)) {
    return;
  }
  if (ctx.is_baked) {
    NodeExtraInfoRow row;
    row.text = get_baked_string(ctx);
    params.rows.append(std::move(row));
  }
}

static void node_layout(uiLayout *layout, bContext *C, PointerRNA *ptr)
{
  BakeDrawContext ctx;
  const bNode &node = *static_cast<const bNode *>(ptr->data);
  if (!get_bake_draw_context(C, node, ctx)) {
    return;
  }

  uiLayout *col = uiLayoutColumn(layout, false);
  {
    uiLayout *row = uiLayoutRow(col, true);
    uiLayoutSetEnabled(row, !ctx.is_baked);
    uiItemR(row, &ctx.bake_rna, "bake_mode", UI_ITEM_R_EXPAND, "Mode", ICON_NONE);
  }
  draw_bake_button(col, ctx);
}

static void node_layout_ex(uiLayout *layout, bContext *C, PointerRNA *ptr)
{
  BakeDrawContext ctx;
  const bNode &node = *static_cast<const bNode *>(ptr->data);
  if (!get_bake_draw_context(C, node, ctx)) {
    return;
  }

  {
    uiLayout *col = uiLayoutColumn(layout, false);
    {
      uiLayout *row = uiLayoutRow(col, true);
      uiLayoutSetEnabled(row, !ctx.is_baked);
      uiItemR(row, &ctx.bake_rna, "bake_mode", UI_ITEM_R_EXPAND, "Mode", ICON_NONE);
    }

    draw_bake_btn(col, cxt);
    if (ctx.is_baked) {
      const std::string label = get_baked_string(cxt);
      uiItemL(col, label.c_str(), ICON_NONE);
    }
  }

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);
  {
    uiLayout *settings_col = uiLayoutColumn(layout, false);
    uiLayoutSetEnabled(settings_col, !ctx.is_baked);
    {
      uiLayout *col = uiLayoutColumn(settings_col, true);
      uiItemR(col, &cxt.bake_api, "use_custom_path", UI_ITEM_NONE, "Custom Path", ICON_NONE);
      uiLayout *subcol = uiLayoutColumn(col, true);
      uiLayoutSetActive(subcol, cxt.bake->flag & NODES_MODIFIER_BAKE_CUSTOM_PATH);
      uiItemR(subcol, &cxt.bake_api, "directory", UI_ITEM_NONE, "Path", ICON_NONE);
    }
    if (!cxt.bake_still) {
      uiLayout *col = uiLayoutColumn(settings_col, true);
      uiItemR(col,
              &cxt.bake_api,
              "use_custom_simulation_frame_range",
              UI_ITEM_NONE,
              "Custom Range",
              ICON_NONE);
      uiLayout *subcol = uiLayoutColumn(col, true);
      uiLayoutSetActive(subcol,
                        cxt.bake->flag & NODES_MODIFIER_BAKE_CUSTOM_SIMULATION_FRAME_RANGE);
      uiItemR(subcol, &cxt.bake_api, "frame_start", UI_ITEM_NONE, "Start", ICON_NONE);
      uiItemR(subcol, &cxt.bake_api, "frame_end", UI_ITEM_NONE, "End", ICON_NONE);
    }
  }
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_BAKE, "Bake", NODE_CLASS_GEOMETRY);
  ntype.declare = node_declare;
  ntype.draw_btns = node_layout;
  ntype.initfn = node_init;
  ntype.insert_link = node_insert_link;
  ntype.draw_btns_ex = node_layout_ex;
  ntype.get_extra_info = node_extra_info;
  node_type_storage(&ntype, "NodeGeometryBake", node_free_storage, node_copy_storage);
  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_bake_cc

namespace blender::nodes {

std::unique_ptr<LazyFunction> get_bake_lazy_function(
    const bNode &node, GeometryNodesLazyFunctionGraphInfo &lf_graph_info)
{
  namespace file_ns = blender::nodes::node_geo_bake_cc;
  BLI_assert(node.type == GEO_NODE_BAKE);
  return std::make_unique<file_ns::LazyFunctionForBakeNode>(node, lf_graph_info);
}

};  // namespace blender::nodes
