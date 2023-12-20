
#pragma once

#include "lib_cpp_type_make.hh"
#include "fn_field.hh"

namespace dune::fn {

template<typename T> struct FieldCPPTypeParam {
};

class FieldCPPType : public CPPType {
 private:
  const CPPType &base_type_;

 public:
  template<typename T>
  FieldCPPType(FieldCPPTypeParam<Field<T>> /* unused */, StringRef debug_name)
      : CPPType(CPPTypeParam<Field<T>, CPPTypeFlags::None>(), debug_name),
        base_type_(CPPType::get<T>())
  {
  }

  const CPPType &base_type() const
  {
    return base_type_;
  }

  /* Ensure that GField and Field<T> have the same layout, to enable casting between the two. */
  static_assert(sizeof(Field<int>) == sizeof(GField));
  static_assert(sizeof(Field<int>) == sizeof(Field<std::string>));

  const GField &get_gfield(const void *field) const
  {
    return *(const GField *)field;
  }

  void construct_from_gfield(void *r_value, const GField &gfield) const
  {
    new (r_val) GField(gfield);
  }
};

class ValueOrFieldCPPType : public CPPType {
 private:
  const CPPType &base_type_;
  void (*construct_from_val_)(void *dst, const void *val);
  void (*construct_from_field_)(void *dst, GField field);
  const void *(*get_val_ptr_)(const void *val_or_field);
  const GField *(*get_field_ptr_)(const void *val_or_field);
  bool (*is_field_)(const void *val_or_field);
  GField (*as_field_)(const void *val_or_field);

 public:
  template<typename T>
  ValOrFieldCPPType(FieldCPPTypeParam<ValOrField<T>> /* unused */, StringRef debug_name)
      : CPPType(CPPTypeParam<ValOrField<T>, CPPTypeFlags::None>(), debug_name),
        base_type_(CPPType::get<T>())
  {
    construct_from_val_ = [](void *dst, const void *val_or_field) {
      new (dst) ValOrField<T>(*(const T *)val_or_field);
    };
    construct_from_field_ = [](void *dst, GField field) {
      new (dst) ValOrField<T>(Field<T>(std::move(field)));
    };
    get_val_ptr_ = [](const void *val_or_field) {
      return (const void *)&((ValOrField<T> *)value_or_field)->value;
    };
    get_field_ptr_ = [](const void *val_or_field) -> const GField * {
      return &((ValOrField<T> *)val_or_field)->field;
    };
    is_field_ = [](const void *val_or_field) {
      return ((ValOrField<T> *)val_or_field)->is_field();
    };
    as_field_ = [](const void *val_or_field) -> GField {
      return ((ValOrField<T> *)val_or_field)->as_field();
    };
  }

  const CPPType &base_type() const
  {
    return base_type_;
  }

  void construct_from_val(void *dst, const void *val) const
  {
    construct_from_val_(dst, val);
  }

  void construct_from_field(void *dst, GField field) const
  {
    construct_from_field_(dst, field);
  }

  const void *get_va_ptr(const void *value_or_field) const
  {
    return get_val_ptr_(val_or_field);
  }

  void *get_val_ptr(void *val_or_field) const
  {
    /* Use `const_cast` to avoid dup the cb for the non-const case. */
    return const_cast<void *>(get_val_ptr_(val_or_field));
  }

  const GField *get_field_ptr(const void *val_or_field) const
  {
    return get_field_ptr_(val_or_field);
  }

  bool is_field(const void *value_or_field) const
  {
    return is_field_(val_or_field);
  }

  GField as_field(const void *val_or_field) const
  {
    return as_field_(val_or_field);
  }
};

}  // namespace dune::fn

#define MAKE_FIELD_CPP_TYPE(DEBUG_NAME, FIELD_TYPE) \
  template<> const dune::CPPType &dune::CPPType::get_impl<dune::fn::Field<FIELD_TYPE>>() \
  { \
    static dune::fn::FieldCPPType cpp_type{ \
        dune::fn::FieldCPPTypeParam<dune::fn::Field<FIELD_TYPE>>(), STRINGIFY(DEBUG_NAME)}; \
    return cpp_type; \
  } \
  template<> \
  const dune::CPPType &dune::CPPType::get_impl<dune::fn::ValOrField<FIELD_TYPE>>() \
  { \
    static dune::fn::ValOrFieldCPPType cpp_type{ \
        dune::fn::FieldCPPTypeParam<dune::fn::ValOrField<FIELD_TYPE>>(), \
        STRINGIFY(DEBUG_NAME##OrValue)}; \
    return cpp_type; \
  }
