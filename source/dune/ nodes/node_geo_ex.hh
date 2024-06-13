#pragma once

#include "lib_color.hh"
#include "lib_math_quaternion_types.hh"

#include "fn_field.hh"
#include "fn_lazy_fn.hh"
#include "fn_multi_fn_builder.hh"

#include "dune_attr_math.hh"
#include "dune_geo_fields.hh"
#include "dune_geo_set.hh"
#include "dune_node_socket_val.hh"
#include "dune_volume_grid_fwd.hh"

#include "types_node.h"

#include "node_derived_node_tree.hh"
#include "node_geo_nodes_lazy_fn.hh"

namespace dune::nodes {

using dune::AnonymousAttrFieldInput;
using dune::AnonymousAttrId;
using dune::AnonymousAttrIdPtr;
using dune::AnonymousAttrPropInfo;
using dune::AttrDomain;
using dune::AttrAccessor;
using dune::AttrFieldInput;
using dune::AttrIdRef;
using dune::AttrKind;
using dune::AttrMetaData;
using dune::AttrReader;
using dune::AttrWriter;
using dune::CurveComponent;
using dune::GAttrReader;
using dune::GAttrWriter;
using dune::GeoComponent;
using dune::GeoComponentEditData;
using dune::GeoSet;
using dune::PenComponent;
using dune::GSpanAttrWriter;
using dune::InstancesComponent;
using dune::MeshComponent;
using dune::MutableAttrAccessor;
using dune::PointCloudComponent;
using dune::SocketValVariant;
using dune::SpanAttrWriter;
using dune::VolumeComponent;
using fn::Field;
using fn::FieldCxt;
using fn::FieldEval;
using fn::FieldInput;
using fn::FieldOp;
using fn::GField;
using geo_eval_log::NamedAttrUsage;
using geo_eval_log::NodeWarningType;

class GeoNodeExParams {
 private:
  const Node &node_;
  lf::Params &params_;
  const lf::Cxt &lf_cxt_;
  const Span<int> lf_input_for_output_socket_usage_;
  const Span<int> lf_input_for_attr_prop_to_output_;
  const FnRef<AnonymousAttrIdPtr(int)> get_output_attr_id_;

 public:
  GeoNodeExParams(const Node &node,
                  lf::Params &params,
                  const lf::Cxt &lf_cxt,
                  const Span<int> lf_input_for_output_socket_usage,
                  const Span<int> lf_input_for_attr_prop_to_output,
                  const FnRef<AnonymousAttrIdPtr(int)> get_output_attr_id)
      : node_(node),
        params_(params),
        lf_cxt_(lf_context),
        lf_input_for_output_socket_usage_(lf_input_for_output_socket_usage),
        lf_input_for_attr_prop_to_output_(
            lf_input_for_attr_prop_to_output),
        get_output_attr_id_(get_output_attr_id)
  {
  }

  template<typename T>
  static inline constexpr bool is_field_base_type_v =
      is_same_any_v<T, float, int, bool, ColorGeo4f, float3, std::string, math::Quaternion>;

  template<typename T>
  static inline constexpr bool stored_as_SocketValVariant_v =
      is_field_base_type_v<T> || fn::is_field_v<T> || dune::is_VolumeGrid_v<T> ||
      is_same_any_v<T, GField, dune::GVolumeGrid>;

  /* Get the input val for the input socket w the given id.
   *
   * This method can only be called once for each id. */
  template<typename T> T extract_input(StringRef id)
  {
    if constexpr (stored_as_SocketValVariant_v<T>) {
      SocketValVariant val_variant = this->extract_input<SocketValVariant>(id);
      return val_variant.extract<T>();
    }
    else {
#ifndef NDEBUG
      this->check_input_access(id, &CPPType::get<T>());
#endif
      const int index = this->get_input_index(id);
      T value = params_.extract_input<T>(index);
      if constexpr (std::is_same_v<T, GeoSet>) {
        this->check_input_geo_set(id, val);
      }
      if constexpr (std::is_same_v<T, SocketValVariant>) {
        lib_assert(val.valid_for_socket(
            eNodeSocketDatatype(node_.input_by_id(id).type)));
      }
      return val;
    }
  }

  void check_input_geo_set(StringRef id, const GeoSet &geo_set) const;
  void check_output_geo_set(const GeoSet &geo_set) const;

  /* Get the input val for the input socket w the given id.  */
  template<typename T> T get_input(StringRef id) const
  {
    if constexpr (stored_as_SocketValVariant_v<T>) {
      auto val_variant = this->get_input<SocketValVariant>(id);
      return val_variant.extract<T>();
    }
    else {
#ifndef NDEBUG
      this->check_input_access(id, &CPPType::get<T>());
#endif
      const int index = this->get_input_index(id);
      const T &val = params_.get_input<T>(index);
      if constexpr (std::is_same_v<T, GeoSet>) {
        this->check_input_geo_set(id, val);
      }
      if constexpr (std::is_same_v<T, SocketValVariant>) {
        lib_assert(val.valid_for_socket(
            eNodeSocketDatatype(node_.input_by_id(id).type)));
      }
      return val;
    }
  }

  /* Store the output val for the given socket id. */
  template<typename T> void set_output(StringRef id, T &&val)
  {
    using StoredT = std::decay_t<T>;
    if constexpr (stored_as_SocketValVariant_v<StoredT>) {
      SocketValVariant val_variant(std::forward<T>(val));
      this->set_output(id, std::move(val_variant));
    }
    else {
#ifndef NDEBUG
      const CPPType &type = CPPType::get<StoredT>();
      this->check_output_access(id, type);
      if constexpr (std::is_same_v<StoredT, SocketValVariant>) {
        lib_assert(val.valid_for_socket(
            eNodeSocketDatatype(node_.output_by_id(id).type)));
      }
#endif
      if constexpr (std::is_same_v<StoredT, GeoSet>) {
        this->check_output_geo_set(val);
      }
      const int index = this->get_output_index(id);
      params_.set_output(index, std::forward<T>(val));
    }
  }

  geo_eval_log::GeoTreeLogger *get_local_tree_logger() const
  {
    return this->local_user_data()->try_get_tree_logger(*this->user_data());
  }

  /* Tell the evaluator that a spec input won't be used anymore. */
  void set_input_unused(StringRef id)
  {
    const int index = this->get_input_index(id);
    params_.set_input_unused(index);
  }

  /* Returns true when the output has to be computed. */
  bool output_is_required(StringRef id) const
  {
    const int index = this->get_output_index(id);
    return params_.get_output_usage(index) != lf::ValUsage::Unused;
  }

  /* Get the node that is currently being ex.  */
  const Node &node() const
  {
    return node_;
  }

  const Ob *self_ob() const
  {
    if (const auto *data = this->user_data()) {
      return data->call_data->self_ob();
    }
    return nullptr;
  }

  Graph *graph() const
  {
    if (const auto *data = this->user_data()) {
      if (data->call_data->mod_data) {
        return data->call_data->mod_data->graph;
      }
      if (data->call_data->op_data) {
        return data->call_data->op_data->graph;
      }
    }
    return nullptr;
  }

  GeoNodesLFUserData *user_data() const
  {
    return static_cast<GeoNodesLFUserData *>(lf_cxt_.user_data);
  }

  GeoNodesLFLocalUserData *local_user_data() const
  {
    return static_cast<GeoNodesLFLocalUserData *>(lf_cxt_.local_user_data);
  }

  /* Add an err msg displayed at the top of the node when displaying the node tree,
   * and potentially elsewhere in Dune. */
  void error_msg_add(const NodeWarningType type, StringRef msg) const;

  void set_default_remaining_outputs();

  void used_named_attr(StringRef attr_name, NamedAttrUsage usage);

  /* Return true when the anonymous attr refd by the given output should be created. */
  bool anonymous_attr_output_is_required(const StringRef output_id)
  {
    const int lf_index =
        lf_input_for_output_bsocket_usage_[node_.output_by_identifier(output_identifier)
                                               .index_in_all_outputs()];
    return params_.get_input<bool>(lf_index);
  }

  /* Return a new anonymous attr id for the given output. None is returned if the anonymous
   * attr is not needed. */
  AnonymousAttrIdPtr get_output_anonymous_attr_id_if_needed(
      const StringRef output_id, const bool force_create = false)
  {
    if (!this->anonymous_attr_output_is_required(output_id) && !force_create) {
      return {};
    }
    const NodeSocket &output_socket = node_.output_by_id(output_id);
    return get_output_attr_id_(output_socket.index());
  }

  /* Get info about which anonymous attrs should be propd to the given output. */
  AnonymousAttrPropInfo get_output_prop_info(
      const StringRef output_id) const
  {
    const int lf_index =
        lf_input_for_attr_prop_to_output_[node_.output_by_id(output_id)
                                                          .index_in_all_outputs()];
    const dune::AnonymousAttrSet &set = params_.get_input<dune::AnonymousAttrSet>(
        lf_index);
    AnonymousAttrPropInfo info;
    info.names = set.names;
    info.propagate_all = false;
    return info;
  }

 private:
  /* Utils for detecting common errs at when using this class. */
  void check_input_access(StringRef id, const CPPType *requested_type = nullptr) const;
  void check_output_access(StringRef id, const CPPType &val_type) const;

  /* Find the active socket with the input name (not the id). */
  const NodeSocket *find_available_socket(const StringRef name) const;

  int get_input_index(const StringRef id) const
  {
    int counter = 0;
    for (const NodeSocket *socket : node_.input_sockets()) {
      if (!socket->is_available()) {
        continue;
      }
      if (socket->id == id) {
        return counter;
      }
      counter++;
    }
    lib_assert_unreachable();
    return -1;
  }

  int get_output_index(const StringRef id) const
  {
    int counter = 0;
    for (const NodeSocket *socket : node_.output_sockets()) {
      if (!socket->is_available()) {
        continue;
      }
      if (socket->id == id) {
        return counter;
      }
      counter++;
    }
    lib_assert_unreachable();
    return -1;
  }
};

}  // namespace dune::nodes
