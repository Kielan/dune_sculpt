#include "lib_color.hh"
#include "lib_cpp_type_make.hh"
#include "lib_cpp_types_make.hh"
#include "lib_math_matrix_types.hh"
#include "lib_math_quaternion_types.hh"
#include "lib_math_vector_types.hh"

namespace dune {

static auto &get_vector_from_self_map()
{
  static Map<const CPPType *, const VectorCPPType *> map;
  return map;
}

static auto &get_vector_from_val_map()
{
  static Map<const CPPType *, const VectorCPPType *> map;
  return map;
}

void VectorCPPType::register_self()
{
  get_vector_from_self_map().add_new(&this->self, this);
  get_vector_from_val_map().add_new(&this->value, this);
}

const VectorCPPType *VectorCPPType::get_from_self(const CPPType &self)
{
  const VectorCPPType *type = get_vector_from_self_map().lookup_default(&self, nullptr);
  lib_assert(type == nullptr || type->self == self);
  return type;
}

const VectorCPPType *VectorCPPType::get_from_val(const CPPType &val)
{
  const VectorCPPType *type = get_vector_from_val_map().lookup_default(&val, nullptr);
  lib_assert(type == nullptr || type->val == val);
  return type;
}

}  // namespace dune
LIB_CPP_TYPE_MAKE(bool, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(float, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(dune::float2, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(dune::float3, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(dune::float4x4, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(int8_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(int16_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(int32_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(dune::int2, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(int64_t, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(uint8_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(uint16_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(uint32_t, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(uint64_t, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(dune::ColorGeometry4f, CPPTypeFlags::BasicType)
LIB_CPP_TYPE_MAKE(dune::ColorGeometry4b, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(dune::math::Quaternion, CPPTypeFlags::BasicType)

LIB_CPP_TYPE_MAKE(std::string, CPPTypeFlags::BasicType)

LIB_VECTOR_CPP_TYPE_MAKE(std::string)

namespace dune {

void register_cpp_types()
{
  LIB_CPP_TYPE_REGISTER(bool);

  LIB_CPP_TYPE_REGISTER(float);
  LIB_CPP_TYPE_REGISTER(dune::float2);
  LIB_CPP_TYPE_REGISTER(dune::float3);
  LIB_CPP_TYPE_REGISTER(dune::float4x4);

  LIB_CPP_TYPE_REGISTER(int8_t);
  LIB_CPP_TYPE_REGISTER(int16_t);
  LIB_CPP_TYPE_REGISTER(int32_t);
  LIB_CPP_TYPE_REGISTER(dune::int2);
  LIB_CPP_TYPE_REGISTER(int64_t);

  LIB_CPP_TYPE_REGISTER(uint8_t);
  LIB_CPP_TYPE_REGISTER(uint16_t);
  LIB_CPP_TYPE_REGISTER(uint32_t);
  LIB_CPP_TYPE_REGISTER(uint64_t);

  LIB_CPP_TYPE_REGISTER(dune::ColorGeometry4f);
  LIB_CPP_TYPE_REGISTER(dune::ColorGeometry4b);

  LIB_CPP_TYPE_REGISTER(math::Quaternion);

  LIB_CPP_TYPE_REGISTER(std::string);

  LIB_VECTOR_CPP_TYPE_REGISTER(std::string);
}

}  // namespace dune
