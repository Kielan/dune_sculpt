#include <utility>

#include "BKE_attribute_access.hh"
#include "BKE_attribute_math.hh"
#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_geometry_set.hh"
#include "BKE_mesh.h"
#include "BKE_pointcloud.h"
#include "BKE_type_conversions.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_color.hh"
#include "BLI_math_vec_types.hh"
#include "BLI_span.hh"

#include "BLT_translation.h"

#include "CLG_log.h"

#include "attribute_access_intern.hh"

static CLG_LogRef LOG = {"bke.attribute_access"};

using blender::float3;
using blender::GMutableSpan;
using blender::GSpan;
using blender::GVArrayImpl_For_GSpan;
using blender::Set;
using blender::StringRef;
using blender::StringRefNull;
using blender::bke::AttributeIDRef;
using blender::bke::OutputAttribute;

namespace blender::bke {

std::ostream &operator<<(std::ostream &stream, const AttributeIDRef &attribute_id)
{
  if (attribute_id.is_named()) {
    stream << attribute_id.name();
  }
  else if (attribute_id.is_anonymous()) {
    const AnonymousAttributeID &anonymous_id = attribute_id.anonymous_id();
    stream << "<" << BKE_anonymous_attribute_id_debug_name(&anonymous_id) << ">";
  }
  else {
    stream << "<none>";
  }
  return stream;
}

static int attribute_data_type_complexity(const CustomDataType data_type)
{
  switch (data_type) {
    case CD_PROP_BOOL:
      return 0;
    case CD_PROP_INT8:
      return 1;
    case CD_PROP_INT32:
      return 2;
    case CD_PROP_FLOAT:
      return 3;
    case CD_PROP_FLOAT2:
      return 4;
    case CD_PROP_FLOAT3:
      return 5;
    case CD_PROP_COLOR:
      return 6;
#if 0 /* These attribute types are not supported yet. */
    case CD_MLOOPCOL:
      return 3;
    case CD_PROP_STRING:
      return 6;
#endif
    default:
      /* Only accept "generic" custom data types used by the attribute system. */
      BLI_assert_unreachable();
      return 0;
  }
}

CustomDataType attribute_data_type_highest_complexity(Span<CustomDataType> data_types)
{
  int highest_complexity = INT_MIN;
  CustomDataType most_complex_type = CD_PROP_COLOR;

  for (const CustomDataType data_type : data_types) {
    const int complexity = attribute_data_type_complexity(data_type);
    if (complexity > highest_complexity) {
      highest_complexity = complexity;
      most_complex_type = data_type;
    }
  }

  return most_complex_type;
}

/**
 * \note Generally the order should mirror the order of the domains
 * established in each component's ComponentAttributeProviders.
 */
static int attribute_domain_priority(const AttributeDomain domain)
{
  switch (domain) {
    case ATTR_DOMAIN_INSTANCE:
      return 0;
    case ATTR_DOMAIN_CURVE:
      return 1;
    case ATTR_DOMAIN_FACE:
      return 2;
    case ATTR_DOMAIN_EDGE:
      return 3;
    case ATTR_DOMAIN_POINT:
      return 4;
    case ATTR_DOMAIN_CORNER:
      return 5;
    default:
      /* Domain not supported in nodes yet. */
      BLI_assert_unreachable();
      return 0;
  }
}

AttributeDomain attribute_domain_highest_priority(Span<AttributeDomain> domains)
{
  int highest_priority = INT_MIN;
  AttributeDomain highest_priority_domain = ATTR_DOMAIN_CORNER;

  for (const AttributeDomain domain : domains) {
    const int priority = attribute_domain_priority(domain);
    if (priority > highest_priority) {
      highest_priority = priority;
      highest_priority_domain = domain;
    }
  }

  return highest_priority_domain;
}

GMutableSpan OutputAttribute::as_span()
{
  if (!optional_span_varray_) {
    const bool materialize_old_values = !ignore_old_values_;
    optional_span_varray_ = std::make_unique<GVMutableArray_GSpan>(varray_,
                                                                   materialize_old_values);
  }
  GVMutableArray_GSpan &span_varray = *optional_span_varray_;
  return span_varray;
}

void OutputAttribute::save()
{
  save_has_been_called_ = true;
  if (optional_span_varray_) {
    optional_span_varray_->save();
  }
  if (save_) {
    save_(*this);
  }
}

OutputAttribute::~OutputAttribute()
{
  if (!save_has_been_called_) {
    if (varray_) {
      std::cout << "Warning: Call `save()` to make sure that changes persist in all cases.\n";
    }
  }
}

static AttributeIDRef attribute_id_from_custom_data_layer(const CustomDataLayer &layer)
{
  if (layer.anonymous_id != nullptr) {
    return layer.anonymous_id;
  }
  return layer.name;
}

static bool add_builtin_type_custom_data_layer_from_init(CustomData &custom_data,
                                                         const CustomDataType data_type,
                                                         const int domain_size,
                                                         const AttributeInit &initializer)
{
  switch (initializer.type) {
    case AttributeInit::Type::Default: {
      void *data = CustomData_add_layer(&custom_data, data_type, CD_DEFAULT, nullptr, domain_size);
      return data != nullptr;
    }
    case AttributeInit::Type::VArray: {
      void *data = CustomData_add_layer(&custom_data, data_type, CD_DEFAULT, nullptr, domain_size);
      if (data == nullptr) {
        return false;
      }
      const GVArray &varray = static_cast<const AttributeInitVArray &>(initializer).varray;
      varray.materialize_to_uninitialized(varray.index_range(), data);
      return true;
    }
    case AttributeInit::Type::MoveArray: {
      void *source_data = static_cast<const AttributeInitMove &>(initializer).data;
      void *data = CustomData_add_layer(
          &custom_data, data_type, CD_ASSIGN, source_data, domain_size);
      if (data == nullptr) {
        MEM_freeN(source_data);
        return false;
      }
      return true;
    }
  }

  BLI_assert_unreachable();
  return false;
}

static void *add_generic_custom_data_layer(CustomData &custom_data,
                                           const CustomDataType data_type,
                                           const eCDAllocType alloctype,
                                           void *layer_data,
                                           const int domain_size,
                                           const AttributeIDRef &attribute_id)
{
  if (attribute_id.is_named()) {
    char attribute_name_c[MAX_NAME];
    attribute_id.name().copy(attribute_name_c);
    return CustomData_add_layer_named(
        &custom_data, data_type, alloctype, layer_data, domain_size, attribute_name_c);
  }
  const AnonymousAttributeID &anonymous_id = attribute_id.anonymous_id();
  return CustomData_add_layer_anonymous(
      &custom_data, data_type, alloctype, layer_data, domain_size, &anonymous_id);
}

static bool add_custom_data_layer_from_attribute_init(const AttributeIDRef &attribute_id,
                                                      CustomData &custom_data,
                                                      const CustomDataType data_type,
                                                      const int domain_size,
                                                      const AttributeInit &initializer)
{
  switch (initializer.type) {
    case AttributeInit::Type::Default: {
      void *data = add_generic_custom_data_layer(
          custom_data, data_type, CD_DEFAULT, nullptr, domain_size, attribute_id);
      return data != nullptr;
    }
    case AttributeInit::Type::VArray: {
      void *data = add_generic_custom_data_layer(
          custom_data, data_type, CD_DEFAULT, nullptr, domain_size, attribute_id);
      if (data == nullptr) {
        return false;
      }
      const GVArray &varray = static_cast<const AttributeInitVArray &>(initializer).varray;
      varray.materialize_to_uninitialized(varray.index_range(), data);
      return true;
    }
    case AttributeInit::Type::MoveArray: {
      void *source_data = static_cast<const AttributeInitMove &>(initializer).data;
      void *data = add_generic_custom_data_layer(
          custom_data, data_type, CD_ASSIGN, source_data, domain_size, attribute_id);
      if (data == nullptr) {
        MEM_freeN(source_data);
        return false;
      }
      return true;
    }
  }

  BLI_assert_unreachable();
  return false;
}

static bool custom_data_layer_matches_attribute_id(const CustomDataLayer &layer,
                                                   const AttributeIDRef &attribute_id)
{
  if (!attribute_id) {
    return false;
  }
  if (attribute_id.is_anonymous()) {
    return layer.anonymous_id == &attribute_id.anonymous_id();
  }
  return layer.name == attribute_id.name();
}

GVArray BuiltinCustomDataLayerProvider::try_get_for_read(const GeometryComponent &component) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }

  const void *data;
  if (stored_as_named_attribute_) {
    data = CustomData_get_layer_named(custom_data, stored_type_, name_.c_str());
  }
  else {
    data = CustomData_get_layer(custom_data, stored_type_);
  }
  if (data == nullptr) {
    return {};
  }

  const int domain_size = component.attribute_domain_size(domain_);
  return as_read_attribute_(data, domain_size);
}

WriteAttributeLookup BuiltinCustomDataLayerProvider::try_get_for_write(
    GeometryComponent &component) const
{
  if (writable_ != Writable) {
    return {};
  }
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }
  const int domain_size = component.attribute_domain_size(domain_);

  void *data;
  if (stored_as_named_attribute_) {
    data = CustomData_get_layer_named(custom_data, stored_type_, name_.c_str());
  }
  else {
    data = CustomData_get_layer(custom_data, stored_type_);
  }
  if (data == nullptr) {
    return {};
  }

  void *new_data;
  if (stored_as_named_attribute_) {
    new_data = CustomData_duplicate_referenced_layer_named(
        custom_data, stored_type_, name_.c_str(), domain_size);
  }
  else {
    new_data = CustomData_duplicate_referenced_layer(custom_data, stored_type_, domain_size);
  }

  if (data != new_data) {
    if (custom_data_access_.update_custom_data_pointers) {
      custom_data_access_.update_custom_data_pointers(component);
    }
    data = new_data;
  }

  std::function<void()> tag_modified_fn;
  if (update_on_write_ != nullptr) {
    tag_modified_fn = [component = &component, update = update_on_write_]() {
      update(*component);
    };
  }

  return {as_write_attribute_(data, domain_size), domain_, std::move(tag_modified_fn)};
}

bool BuiltinCustomDataLayerProvider::try_delete(GeometryComponent &component) const
{
  if (deletable_ != Deletable) {
    return false;
  }
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }

  const int domain_size = component.attribute_domain_size(domain_);
  int layer_index;
  if (stored_as_named_attribute_) {
    for (const int i : IndexRange(custom_data->totlayer)) {
      if (custom_data_layer_matches_attribute_id(custom_data->layers[i], name_)) {
        layer_index = i;
        break;
      }
    }
  }
  else {
    layer_index = CustomData_get_layer_index(custom_data, stored_type_);
  }

  const bool delete_success = CustomData_free_layer(
      custom_data, stored_type_, domain_size, layer_index);
  if (delete_success) {
    if (custom_data_access_.update_custom_data_pointers) {
      custom_data_access_.update_custom_data_pointers(component);
    }
  }
  return delete_success;
}

bool BuiltinCustomDataLayerProvider::try_create(GeometryComponent &component,
                                                const AttributeInit &initializer) const
{
  if (createable_ != Creatable) {
    return false;
  }
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return false;
  }

  const int domain_size = component.attribute_domain_size(domain_);
  bool success;
  if (stored_as_named_attribute_) {
    if (CustomData_get_layer_named(custom_data, data_type_, name_.c_str())) {
      /* Exists already. */
      return false;
    }
    success = add_custom_data_layer_from_attribute_init(
        name_, *custom_data, stored_type_, domain_size, initializer);
  }
  else {
    if (CustomData_get_layer(custom_data, stored_type_) != nullptr) {
      /* Exists already. */
      return false;
    }
    success = add_builtin_type_custom_data_layer_from_init(
        *custom_data, stored_type_, domain_size, initializer);
  }
  if (success) {
    if (custom_data_access_.update_custom_data_pointers) {
      custom_data_access_.update_custom_data_pointers(component);
    }
  }
  return success;
}

bool BuiltinCustomDataLayerProvider::exists(const GeometryComponent &component) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return false;
  }
  if (stored_as_named_attribute_) {
    return CustomData_get_layer_named(custom_data, stored_type_, name_.c_str()) != nullptr;
  }
  return CustomData_get_layer(custom_data, stored_type_) != nullptr;
}

ReadAttributeLookup CustomDataAttributeProvider::try_get_for_read(
    const GeometryComponent &component, const AttributeIDRef &attribute_id) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }
  const int domain_size = component.attribute_domain_size(domain_);
  for (const CustomDataLayer &layer : Span(custom_data->layers, custom_data->totlayer)) {
    if (!custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      continue;
    }
    const CPPType *type = custom_data_type_to_cpp_type((CustomDataType)layer.type);
    if (type == nullptr) {
      continue;
    }
    GSpan data{*type, layer.data, domain_size};
    return {GVArray::ForSpan(data), domain_};
  }
  return {};
}

WriteAttributeLookup CustomDataAttributeProvider::try_get_for_write(
    GeometryComponent &component, const AttributeIDRef &attribute_id) const
{
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }
  const int domain_size = component.attribute_domain_size(domain_);
  for (CustomDataLayer &layer : MutableSpan(custom_data->layers, custom_data->totlayer)) {
    if (!custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      continue;
    }
    if (attribute_id.is_named()) {
      CustomData_duplicate_referenced_layer_named(
          custom_data, layer.type, layer.name, domain_size);
    }
    else {
      CustomData_duplicate_referenced_layer_anonymous(
          custom_data, layer.type, &attribute_id.anonymous_id(), domain_size);
    }
    const CPPType *type = custom_data_type_to_cpp_type((CustomDataType)layer.type);
    if (type == nullptr) {
      continue;
    }
    GMutableSpan data{*type, layer.data, domain_size};
    return {GVMutableArray::ForSpan(data), domain_};
  }
  return {};
}

bool CustomDataAttributeProvider::try_delete(GeometryComponent &component,
                                             const AttributeIDRef &attribute_id) const
{
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return false;
  }
  const int domain_size = component.attribute_domain_size(domain_);
  for (const int i : IndexRange(custom_data->totlayer)) {
    const CustomDataLayer &layer = custom_data->layers[i];
    if (this->type_is_supported((CustomDataType)layer.type) &&
        custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      CustomData_free_layer(custom_data, layer.type, domain_size, i);
      return true;
    }
  }
  return false;
}

bool CustomDataAttributeProvider::try_create(GeometryComponent &component,
                                             const AttributeIDRef &attribute_id,
                                             const AttributeDomain domain,
                                             const CustomDataType data_type,
                                             const AttributeInit &initializer) const
{
  if (domain_ != domain) {
    return false;
  }
  if (!this->type_is_supported(data_type)) {
    return false;
  }
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return false;
  }
  for (const CustomDataLayer &layer : Span(custom_data->layers, custom_data->totlayer)) {
    if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      return false;
    }
  }
  const int domain_size = component.attribute_domain_size(domain_);
  add_custom_data_layer_from_attribute_init(
      attribute_id, *custom_data, data_type, domain_size, initializer);
  return true;
}

bool CustomDataAttributeProvider::foreach_attribute(const GeometryComponent &component,
                                                    const AttributeForeachCallback callback) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return true;
  }
  for (const CustomDataLayer &layer : Span(custom_data->layers, custom_data->totlayer)) {
    const CustomDataType data_type = (CustomDataType)layer.type;
    if (this->type_is_supported(data_type)) {
      AttributeMetaData meta_data{domain_, data_type};
      const AttributeIDRef attribute_id = attribute_id_from_custom_data_layer(layer);
      if (!callback(attribute_id, meta_data)) {
        return false;
      }
    }
  }
  return true;
}

ReadAttributeLookup NamedLegacyCustomDataProvider::try_get_for_read(
    const GeometryComponent &component, const AttributeIDRef &attribute_id) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }
  for (const CustomDataLayer &layer : Span(custom_data->layers, custom_data->totlayer)) {
    if (layer.type == stored_type_) {
      if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
        const int domain_size = component.attribute_domain_size(domain_);
        return {as_read_attribute_(layer.data, domain_size), domain_};
      }
    }
  }
  return {};
}

WriteAttributeLookup NamedLegacyCustomDataProvider::try_get_for_write(
    GeometryComponent &component, const AttributeIDRef &attribute_id) const
{
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return {};
  }
  for (CustomDataLayer &layer : MutableSpan(custom_data->layers, custom_data->totlayer)) {
    if (layer.type == stored_type_) {
      if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
        const int domain_size = component.attribute_domain_size(domain_);
        void *data_old = layer.data;
        void *data_new = CustomData_duplicate_referenced_layer_named(
            custom_data, stored_type_, layer.name, domain_size);
        if (data_old != data_new) {
          if (custom_data_access_.update_custom_data_pointers) {
            custom_data_access_.update_custom_data_pointers(component);
          }
        }
        return {as_write_attribute_(layer.data, domain_size), domain_};
      }
    }
  }
  return {};
}

bool NamedLegacyCustomDataProvider::try_delete(GeometryComponent &component,
                                               const AttributeIDRef &attribute_id) const
{
  CustomData *custom_data = custom_data_access_.get_custom_data(component);
  if (custom_data == nullptr) {
    return false;
  }
  for (const int i : IndexRange(custom_data->totlayer)) {
    const CustomDataLayer &layer = custom_data->layers[i];
    if (layer.type == stored_type_) {
      if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
        const int domain_size = component.attribute_domain_size(domain_);
        CustomData_free_layer(custom_data, stored_type_, domain_size, i);
        if (custom_data_access_.update_custom_data_pointers) {
          custom_data_access_.update_custom_data_pointers(component);
        }
        return true;
      }
    }
  }
  return false;
}

bool NamedLegacyCustomDataProvider::foreach_attribute(
    const GeometryComponent &component, const AttributeForeachCallback callback) const
{
  const CustomData *custom_data = custom_data_access_.get_const_custom_data(component);
  if (custom_data == nullptr) {
    return true;
  }
  for (const CustomDataLayer &layer : Span(custom_data->layers, custom_data->totlayer)) {
    if (layer.type == stored_type_) {
      AttributeMetaData meta_data{domain_, attribute_type_};
      if (!callback(layer.name, meta_data)) {
        return false;
      }
    }
  }
  return true;
}

void NamedLegacyCustomDataProvider::foreach_domain(
    const FunctionRef<void(AttributeDomain)> callback) const
{
  callback(domain_);
}

CustomDataAttributes::CustomDataAttributes()
{
  CustomData_reset(&data);
  size_ = 0;
}

CustomDataAttributes::~CustomDataAttributes()
{
  CustomData_free(&data, size_);
}

CustomDataAttributes::CustomDataAttributes(const CustomDataAttributes &other)
{
  size_ = other.size_;
  CustomData_copy(&other.data, &data, CD_MASK_ALL, CD_DUPLICATE, size_);
}

CustomDataAttributes::CustomDataAttributes(CustomDataAttributes &&other)
{
  size_ = other.size_;
  data = other.data;
  CustomData_reset(&other.data);
}

CustomDataAttributes &CustomDataAttributes::operator=(const CustomDataAttributes &other)
{
  if (this != &other) {
    CustomData_copy(&other.data, &data, CD_MASK_ALL, CD_DUPLICATE, other.size_);
    size_ = other.size_;
  }

  return *this;
}

std::optional<GSpan> CustomDataAttributes::get_for_read(const AttributeIDRef &attribute_id) const
{
  for (const CustomDataLayer &layer : Span(data.layers, data.totlayer)) {
    if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      const CPPType *cpp_type = custom_data_type_to_cpp_type((CustomDataType)layer.type);
      BLI_assert(cpp_type != nullptr);
      return GSpan(*cpp_type, layer.data, size_);
    }
  }
  return {};
}

GVArray CustomDataAttributes::get_for_read(const AttributeIDRef &attribute_id,
                                           const CustomDataType data_type,
                                           const void *default_value) const
{
  const CPPType *type = blender::bke::custom_data_type_to_cpp_type(data_type);

  std::optional<GSpan> attribute = this->get_for_read(attribute_id);
  if (!attribute) {
    const int domain_size = this->size_;
    return GVArray::ForSingle(
        *type, domain_size, (default_value == nullptr) ? type->default_value() : default_value);
  }

  if (attribute->type() == *type) {
    return GVArray::ForSpan(*attribute);
  }
  const blender::bke::DataTypeConversions &conversions =
      blender::bke::get_implicit_type_conversions();
  return conversions.try_convert(GVArray::ForSpan(*attribute), *type);
}

std::optional<GMutableSpan> CustomDataAttributes::get_for_write(const AttributeIDRef &attribute_id)
{
  for (CustomDataLayer &layer : MutableSpan(data.layers, data.totlayer)) {
    if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      const CPPType *cpp_type = custom_data_type_to_cpp_type((CustomDataType)layer.type);
      BLI_assert(cpp_type != nullptr);
      return GMutableSpan(*cpp_type, layer.data, size_);
    }
  }
  return {};
}

bool CustomDataAttributes::create(const AttributeIDRef &attribute_id,
                                  const CustomDataType data_type)
{
  void *result = add_generic_custom_data_layer(
      data, data_type, CD_DEFAULT, nullptr, size_, attribute_id);
  return result != nullptr;
}

bool CustomDataAttributes::create_by_move(const AttributeIDRef &attribute_id,
                                          const CustomDataType data_type,
                                          void *buffer)
{
  void *result = add_generic_custom_data_layer(
      data, data_type, CD_ASSIGN, buffer, size_, attribute_id);
  return result != nullptr;
}

bool CustomDataAttributes::remove(const AttributeIDRef &attribute_id)
{
  bool result = false;
  for (const int i : IndexRange(data.totlayer)) {
    const CustomDataLayer &layer = data.layers[i];
    if (custom_data_layer_matches_attribute_id(layer, attribute_id)) {
      CustomData_free_layer(&data, layer.type, size_, i);
      result = true;
    }
  }
  return result;
}

void CustomDataAttributes::reallocate(const int size)
{
  size_ = size;
  CustomData_realloc(&data, size);
}

void CustomDataAttributes::clear()
{
  CustomData_free(&data, size_);
  size_ = 0;
}

bool CustomDataAttributes::foreach_attribute(const AttributeForeachCallback callback,
                                             const AttributeDomain domain) const
{
  for (const CustomDataLayer &layer : Span(data.layers, data.totlayer)) {
    AttributeMetaData meta_data{domain, (CustomDataType)layer.type};
    const AttributeIDRef attribute_id = attribute_id_from_custom_data_layer(layer);
    if (!callback(attribute_id, meta_data)) {
      return false;
    }
  }
  return true;
}

void CustomDataAttributes::reorder(Span<AttributeIDRef> new_order)
{
  BLI_assert(new_order.size() == data.totlayer);

  Map<AttributeIDRef, int> old_order;
  old_order.reserve(data.totlayer);
  Array<CustomDataLayer> old_layers(Span(data.layers, data.totlayer));
  for (const int i : old_layers.index_range()) {
    old_order.add_new(attribute_id_from_custom_data_layer(old_layers[i]), i);
  }

  MutableSpan layers(data.layers, data.totlayer);
  for (const int i : layers.index_range()) {
    const int old_index = old_order.lookup(new_order[i]);
    layers[i] = old_layers[old_index];
  }

  CustomData_update_typemap(&data);
}

}  // namespace blender::bke

/* -------------------------------------------------------------------- */
/** \name Geometry Component
 * \{ */

const blender::bke::ComponentAttributeProviders *GeometryComponent::get_attribute_providers() const
{
  return nullptr;
}

bool GeometryComponent::attribute_domain_supported(const AttributeDomain domain) const
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return false;
  }
  return providers->supported_domains().contains(domain);
}

int GeometryComponent::attribute_domain_size(const AttributeDomain UNUSED(domain)) const
{
  return 0;
}

bool GeometryComponent::attribute_is_builtin(const blender::StringRef attribute_name) const
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return false;
  }
  return providers->builtin_attribute_providers().contains_as(attribute_name);
}

bool GeometryComponent::attribute_is_builtin(const AttributeIDRef &attribute_id) const
{
  /* Anonymous attributes cannot be built-in. */
  return attribute_id.is_named() && this->attribute_is_builtin(attribute_id.name());
}

blender::bke::ReadAttributeLookup GeometryComponent::attribute_try_get_for_read(
    const AttributeIDRef &attribute_id) const
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return {};
  }
  if (attribute_id.is_named()) {
    const BuiltinAttributeProvider *builtin_provider =
        providers->builtin_attribute_providers().lookup_default_as(attribute_id.name(), nullptr);
    if (builtin_provider != nullptr) {
      return {builtin_provider->try_get_for_read(*this), builtin_provider->domain()};
    }
  }
  for (const DynamicAttributesProvider *dynamic_provider :
       providers->dynamic_attribute_providers()) {
    ReadAttributeLookup attribute = dynamic_provider->try_get_for_read(*this, attribute_id);
    if (attribute) {
      return attribute;
    }
  }
  return {};
}

blender::GVArray GeometryComponent::attribute_try_adapt_domain_impl(
    const blender::GVArray &varray,
    const AttributeDomain from_domain,
    const AttributeDomain to_domain) const
{
  if (from_domain == to_domain) {
    return varray;
  }
  return {};
}

blender::bke::WriteAttributeLookup GeometryComponent::attribute_try_get_for_write(
    const AttributeIDRef &attribute_id)
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return {};
  }
  if (attribute_id.is_named()) {
    const BuiltinAttributeProvider *builtin_provider =
        providers->builtin_attribute_providers().lookup_default_as(attribute_id.name(), nullptr);
    if (builtin_provider != nullptr) {
      return builtin_provider->try_get_for_write(*this);
    }
  }
  for (const DynamicAttributesProvider *dynamic_provider :
       providers->dynamic_attribute_providers()) {
    WriteAttributeLookup attribute = dynamic_provider->try_get_for_write(*this, attribute_id);
    if (attribute) {
      return attribute;
    }
  }
  return {};
}

bool GeometryComponent::attribute_try_delete(const AttributeIDRef &attribute_id)
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return {};
  }
  if (attribute_id.is_named()) {
    const BuiltinAttributeProvider *builtin_provider =
        providers->builtin_attribute_providers().lookup_default_as(attribute_id.name(), nullptr);
    if (builtin_provider != nullptr) {
      return builtin_provider->try_delete(*this);
    }
  }
  bool success = false;
  for (const DynamicAttributesProvider *dynamic_provider :
       providers->dynamic_attribute_providers()) {
    success = dynamic_provider->try_delete(*this, attribute_id) || success;
  }
  return success;
}

bool GeometryComponent::attribute_try_create(const AttributeIDRef &attribute_id,
                                             const AttributeDomain domain,
                                             const CustomDataType data_type,
                                             const AttributeInit &initializer)
{
  using namespace blender::bke;
  if (!attribute_id) {
    return false;
  }
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return false;
  }
  if (attribute_id.is_named()) {
    const BuiltinAttributeProvider *builtin_provider =
        providers->builtin_attribute_providers().lookup_default_as(attribute_id.name(), nullptr);
    if (builtin_provider != nullptr) {
      if (builtin_provider->domain() != domain) {
        return false;
      }
      if (builtin_provider->data_type() != data_type) {
        return false;
      }
      return builtin_provider->try_create(*this, initializer);
    }
  }
  for (const DynamicAttributesProvider *dynamic_provider :
       providers->dynamic_attribute_providers()) {
    if (dynamic_provider->try_create(*this, attribute_id, domain, data_type, initializer)) {
      return true;
    }
  }
  return false;
}

bool GeometryComponent::attribute_try_create_builtin(const blender::StringRef attribute_name,
                                                     const AttributeInit &initializer)
{
  using namespace blender::bke;
  if (attribute_name.is_empty()) {
    return false;
  }
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return false;
  }
  const BuiltinAttributeProvider *builtin_provider =
      providers->builtin_attribute_providers().lookup_default_as(attribute_name, nullptr);
  if (builtin_provider == nullptr) {
    return false;
  }
  return builtin_provider->try_create(*this, initializer);
}

Set<AttributeIDRef> GeometryComponent::attribute_ids() const
{
  Set<AttributeIDRef> attributes;
  this->attribute_foreach(
      [&](const AttributeIDRef &attribute_id, const AttributeMetaData &UNUSED(meta_data)) {
        attributes.add(attribute_id);
        return true;
      });
  return attributes;
}

bool GeometryComponent::attribute_foreach(const AttributeForeachCallback callback) const
{
  using namespace blender::bke;
  const ComponentAttributeProviders *providers = this->get_attribute_providers();
  if (providers == nullptr) {
    return true;
  }

  /* Keep track handled attribute names to make sure that we do not return the same name twice. */
  Set<std::string> handled_attribute_names;

  for (const BuiltinAttributeProvider *provider :
       providers->builtin_attribute_providers().values()) {
    if (provider->exists(*this)) {
      AttributeMetaData meta_data{provider->domain(), provider->data_type()};
      if (!callback(provider->name(), meta_data)) {
        return false;
      }
      handled_attribute_names.add_new(provider->name());
    }
  }
  for (const DynamicAttributesProvider *provider : providers->dynamic_attribute_providers()) {
    const bool continue_loop = provider->foreach_attribute(
        *this, [&](const AttributeIDRef &attribute_id, const AttributeMetaData &meta_data) {
          if (attribute_id.is_anonymous() || handled_attribute_names.add(attribute_id.name())) {
            return callback(attribute_id, meta_data);
          }
          return true;
        });
    if (!continue_loop) {
      return false;
    }
  }

  return true;
}
