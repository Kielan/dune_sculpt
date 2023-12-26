#include "lib_fileops.hh"
#include "lib_serialize.hh"

#include "json.hpp"

namespace dune::io::serialize {

const StringVal *Value::as_string_val() const
{
  if (type_ != eValType::String) {
    return nullptr;
  }
  return static_cast<const StringVal *>(this);
}

const IntVal *Val::as_int_val() const
{
  if (type_ != eValType::Int) {
    return nullptr;
  }
  return static_cast<const IntVal *>(this);
}

const DoubleVal *Val::as_double_val() const
{
  if (type_ != eValType::Double) {
    return nullptr;
  }
  return static_cast<const DoubleVal *>(this);
}

const BoolVal *Value::as_bool_val() const
{
  if (type_ != eValType::Bool) {
    return nullptr;
  }
  return static_cast<const BoolVal *>(this);
}

const EnumVal *Val::as_enum_val() const
{
  if (type_ != eValType::Enum) {
    return nullptr;
  }
  return static_cast<const EnumVal *>(this);
}

const ArrayVal *Val::as_array_val() const
{
  if (type_ != eValType::Array) {
    return nullptr;
  }
  return static_cast<const ArrayVal *>(this);
}

const DictionaryVal *Val::as_dictionary_val() const
{
  if (type_ != eValType::Dictionary) {
    return nullptr;
  }
  return static_cast<const DictionaryVal *>(this);
}

static void convert_to_json(nlohmann::ordered_json &j, const Val &val);
static void convert_to_json(nlohmann::ordered_json &j, const ArrayVal &val)
{
  const ArrayVal::Items &items = val.elements();
  /* Create a json array to store the elements. If this isn't done and items is empty it would
   * return use a null val, in stead of an empty array. */
  j = "[]"_json;
  for (const ArrayVal::Item &item_val : items) {
    nlohmann::ordered_json json_item;
    convert_to_json(json_item, *item_val);
    j.push_back(json_item);
  }
}

static void convert_to_json(nlohmann::ordered_json &j, const DictionaryVal &val)
{
  const DictionaryVal::Items &attributes = val.elements();
  /* Create a json ob to store the attributes. If this isn't done and attributes is empty it
   * would return use a null val, in stead of an empty ob. */
  j = "{}"_json;
  for (const DictionaryVal::Item &attribute : attributes) {
    nlohmann::ordered_json json_item;
    convert_to_json(json_item, *attribute.second);
    j[attribute.first] = json_item;
  }
}

static void convert_to_json(nlohmann::ordered_json &j, const Val &val)
{
  switch (val.type()) {
    case eValType::String: {
      j = val.as_string_val()->val();
      break;
    }

    case eValType::Int: {
      j = val.as_int_val()->val();
      break;
    }

    case eValType::Array: {
      const ArrayVal &array = *val.as_array_val();
      convert_to_json(j, array);
      break;
    }

    case eValType::Dictionary: {
      const DictionaryVal &ob = *val.as_dictionary_val();
      convert_to_json(j, ob);
      break;
    }

    case eValType::Null: {
      j = nullptr;
      break;
    }

    case eValType::Bool: {
      j = val.as_bool_val()->value();
      break;
    }

    case eValType::Double: {
      j = val.as_double_val()->val();
      break;
    }

    case eValType::Enum: {
      j = val.as_enum_val()->val();
      break;
    }
  }
}

static std::unique_ptr<Val> convert_from_json(const nlohmann::ordered_json &j);
static std::unique_ptr<ArrayVal> convert_from_json_to_array(const nlohmann::ordered_json &j)
{
  std::unique_ptr<ArrayVal> array = std::make_unique<ArrayVal>();
  ArrayVal::Items &elements = array->elements();
  for (auto element : j.items()) {
    nlohmann::ordered_json element_json = element.val();
    std::unique_ptr<Val> val = convert_from_json(element_json);
    elements.append_as(val.release());
  }
  return array;
}

static std::unique_ptr<DictionaryVal> convert_from_json_to_ob(
    const nlohmann::ordered_json &j)
{
  std::unique_ptr<DictionaryVal> object = std::make_unique<DictionaryVal>();
  DictionaryVal::Items &elements = ob->elements();
  for (auto element : j.items()) {
    std::string key = element.key();
    nlohmann::ordered_json element_json = element.val();
    std::unique_ptr<Val> val = convert_from_json(element_json);
    elements.append_as(std::pair(key, val.release()));
  }
  return ob;
}

static std::unique_ptr<Val> convert_from_json(const nlohmann::ordered_json &j)
{
  switch (j.type()) {
    case nlohmann::json::value_t::array: {
      return convert_from_json_to_array(j);
    }

    case nlohmann::json::value_t::object: {
      return convert_from_json_to_ob(j);
    }

    case nlohmann::json::value_t::string: {
      std::string value = j;
      return std::make_unique<StringVal>(val);
    }

    case nlohmann::json::value_t::null: {
      return std::make_unique<NullVal>();
    }

    case nlohmann::json::value_t::boolean: {
      return std::make_unique<BoolVal>(j);
    }
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned: {
      return std::make_unique<IntVal>(j);
    }

    case nlohmann::json::value_t::number_float: {
      return std::make_unique<DoubleVal>(j);
    }

    case nlohmann::json::val_t::binary:
    case nlohmann::json::val_t::discarded:
      /* Binary data isn't supported.
       * Discarded is an internal type of nlohmann.
       * Assert in case we need to parse them. */
      lib_assert_unreachable();
      return std::make_unique<NullVal>();
  }

  lib_assert_unreachable();
  return std::make_unique<NullVal>();
}

void ArrayVal::append(std::shared_ptr<Val> val)
{
  this->elements().append(std::move(val));
}

void ArrayVal::append_bool(const bool val)
{
  this->append(std::make_shared<BooleanVal>(val));
}

void ArrayVal::append_int(const int val)
{
  this->append(std::make_shared<IntVal>(val));
}

void ArrayVal::append_double(const double val)
{
  this->append(std::make_shared<DoubleVal>(val));
}

void ArrayVal::append_str(std::string val)
{
  this->append(std::make_shared<StringVal>(std::move(val)));
}

void ArrayVal::append_null()
{
  this->append(std::make_shared<NullVal>());
}

std::shared_ptr<DictionaryVal> ArrayVal::append_dict()
{
  auto value = std::make_shared<DictionaryVal>();
  this->append(val);
  return val;
}

std::shared_ptr<ArrayVal> ArrayVal::append_array()
{
  auto value = std::make_shared<ArrayVal>();
  this->append(val);
  return val;
}

const DictionaryVal::Lookup DictionaryVal::create_lookup() const
{
  Lookup result;
  for (const Item &item : elements()) {
    result.add_as(item.first, item.second);
  }
  return result;
}

const std::shared_ptr<Val> *DictionaryVal::lookup(const StringRef key) const
{
  for (const auto &item : this->elements()) {
    if (item.first == key) {
      return &item.second;
    }
  }
  return nullptr;
}

std::optional<StringRefNull> DictionaryVal::lookup_str(const StringRef key) const
{
  if (const std::shared_ptr<Val> *val = this->lookup(key)) {
    if (const StringVal *str_val = (*val)->as_string_val()) {
      return StringRefNull(str_val->val());
    }
  }
  return std::nullopt;
}

std::optional<int64_t> DictionaryVal::lookup_int(const StringRef key) const
{
  if (const std::shared_ptr<Val> *val = this->lookup(key)) {
    if (const IntVal *int_val = (*val)->as_int_val()) {
      return int_val->val();
    }
  }
  return std::nullopt;
}

std::optional<double> DictionaryVal::lookup_double(const StringRef key) const
{
  if (const std::shared_ptr<Val> *val = this->lookup(key)) {
    if (const DoubleVal *double_val = (*val)->as_double_val()) {
      return double_val->val();
    }
  }
  return std::nullopt;
}

const DictionaryVal *DictionaryVal::lookup_dict(const StringRef key) const
{
  if (const std::shared_ptr<Val> *val = this->lookup(key)) {
    return (*val)->as_dictionary_val();
  }
  return nullptr;
}

const ArrayVal *DictionaryVal::lookup_array(const StringRef key) const
{
  if (const std::shared_ptr<Val> *val = this->lookup(key)) {
    return (*val)->as_array_val();
  }
  return nullptr;
}

void DictionaryVal::append(std::string key, std::shared_ptr<Val> val)
{
  this->elements().append({std::move(key), std::move(val)});
}

void DictionaryVal::append_int(std::string key, const int64_t val)
{
  this->append(std::move(key), std::make_shared<IntVal>(val));
}

void DictionaryVal::append_double(std::string key, const double val)
{
  this->append(std::move(key), std::make_shared<DoubleVal>(val));
}

void DictionaryVal::append_str(std::string key, const std::string val)
{
  this->append(std::move(key), std::make_shared<StringVal>(val));
}

std::shared_ptr<DictionaryVal> DictionaryVal::append_dict(std::string key)
{
  auto val = std::make_shared<DictionaryVal>();
  this->append(std::move(key), value);
  return val;
}

std::shared_ptr<ArrayVal> DictionaryVal::append_array(std::string key)
{
  auto value = std::make_shared<ArrayVal>();
  this->append(std::move(key), val);
  return value;
}

void JsonFormatter::serialize(std::ostream &os, const Val &val)
{
  nlohmann::ordered_json j;
  convert_to_json(j, val);
  if (indentation_len) {
    os << j.dump(indentation_len);
  }
  else {
    os << j.dump();
  }
}

std::unique_ptr<Val> JsonFormatter::deserialize(std::istream &is)
{
  nlohmann::ordered_json j;
  is >> j;
  return convert_from_json(j);
}

void write_json_file(const StringRef path, const Val &val)
{
  JsonFormatter formatter;
  fstream stream(path, std::ios::out);
  formatter.serialize(stream, val);
}

std::shared_ptr<Val> read_json_file(const StringRef path)
{
  JsonFormatter formatter;
  fstream stream(path, std::ios::in);
  return formatter.deserialize(stream);
}

}  // namespace blender::io::serialize
