#pragma once

#include "types_node.h"

#include "lib_math_base_safe.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.hh"
#include "lib_string_ref.hh"

#include "fn_multi_builder.hh"

namespace dune::nodes {

struct FloatMathOpInfo {
  StringRefNull title_case_name;
  StringRefNull shader_name;

  FloatMathOpInfo() = delete;
  FloatMathOpInfo(StringRefNull title_case_name, StringRefNull shader_name)
      : title_case_name(title_case_name), shader_name(shader_name)
  {
  }
};

const FloatMathOpInfo *get_float_math_op_info(int op);
const FloatMathOpInfo *get_float3_math_op_info(int op);
const FloatMathOpInfo *get_float_compare_op_info(int op);

/* This calls the `cb` with two args:
 * 1. The math fn that takes a float as input and outputs a new float.
 * 2. A FloatMathOpInfo struct ref.
 * Returns true when the cb has been called, otherwise false.
 *
 * The math fn that is passed to the cb is actually a lambda fn that is diff
 * for every op. Therefore, if the cb is templated on the math fn, it will get
 * instantiated for every op separately. This has two benefits:
 * - The compiler can optimize the cb for every op separately.
 * - A static var declared in the cb will be generated for every op separately.
 *
 * If separate instantiations are not desired, the cb can also take a fn ptr w
 * the following signature as input instead: float (*math_function)(float a). */
template<typename Cb>
inline bool try_dispatch_float_math_fl_to_fl(const int op, Cb &&cb)
{
  const FloatMathOpInfo *info = get_float_math_op_info(op);
  if (info == nullptr) {
    return false;
  }

  static auto ex_preset_fast = mf::build::ex_presets::AllSpanOrSingle();
  static auto ex_preset_slow = mf::build::ex_presets::Materialized();

  /* This is just an util fn to keep the individual cases smaller. */
  auto dispatch = [&](auto ex_preset, auto math_fn) -> bool {
    cb(ex_preset, math_fn, *info);
    return true;
  };

  switch (operation) {
    case NODE_MATH_EXPONENT:
      return dispatch(ex_preset_slow, [](float a) { return expf(a); });
    case NODE_MATH_SQRT:
      return dispatch(exec_preset_fast, [](float a) { return safe_sqrtf(a); });
    case NODE_MATH_INV_SQRT:
      return dispatch(exec_preset_fast, [](float a) { return safe_inverse_sqrtf(a); });
    case NODE_MATH_ABSOLUTE:
      return dispatch(exec_preset_fast, [](float a) { return fabs(a); });
    case NODE_MATH_RADIANS:
      return dispatch(exec_preset_fast, [](float a) { return (float)DEG2RAD(a); });
    case NODE_MATH_DEGREES:
      return dispatch(exec_preset_fast, [](float a) { return (float)RAD2DEG(a); });
    case NODE_MATH_SIGN:
      return dispatch(exec_preset_fast, [](float a) { return compatible_signf(a); });
    case NODE_MATH_ROUND:
      return dispatch(exec_preset_fast, [](float a) { return floorf(a + 0.5f); });
    case NODE_MATH_FLOOR:
      return dispatch(exec_preset_fast, [](float a) { return floorf(a); });
    case NODE_MATH_CEIL:
      return dispatch(exec_preset_fast, [](float a) { return ceilf(a); });
    case NODE_MATH_FRACTION:
      return dispatch(exec_preset_fast, [](float a) { return a - floorf(a); });
    case NODE_MATH_TRUNC:
      return dispatch(exec_preset_fast, [](float a) { return a >= 0.0f ? floorf(a) : ceilf(a); });
    case NODE_MATH_SINE:
      return dispatch(exec_preset_slow, [](float a) { return sinf(a); });
    case NODE_MATH_COSINE:
      return dispatch(exec_preset_slow, [](float a) { return cosf(a); });
    case NODE_MATH_TANGENT:
      return dispatch(exec_preset_slow, [](float a) { return tanf(a); });
    case NODE_MATH_SINH:
      return dispatch(exec_preset_slow, [](float a) { return sinhf(a); });
    case NODE_MATH_COSH:
      return dispatch(exec_preset_slow, [](float a) { return coshf(a); });
    case NODE_MATH_TANH:
      return dispatch(exec_preset_slow, [](float a) { return tanhf(a); });
    case NODE_MATH_ARCSINE:
      return dispatch(exec_preset_slow, [](float a) { return safe_asinf(a); });
    case NODE_MATH_ARCCOSINE:
      return dispatch(exec_preset_slow, [](float a) { return safe_acosf(a); });
    case NODE_MATH_ARCTANGENT:
      return dispatch(exec_preset_slow, [](float a) { return atanf(a); });
  }
  return false;
}

/* This is similar to try_dispatch_float_math_fl_to_fl, just with a diff cb signature. */
template<typename Cb>
inline bool try_dispatch_float_math_fl_fl_to_fl(const int op, Callback &&cb)
{
  const FloatMathOpInfo *info = get_float_math_op_info(op);
  if (info == nullptr) {
    return false;
  }

  static auto ex_preset_fast = mf::build::ex_presets::AllSpanOrSingle();
  static auto ex_preset_slow = mf::build::ex_presets::Materialized();

  /* This is just an util fn to keep the individual cases smaller. */
  auto dispatch = [&](auto ex_preset, auto math_fn) -> bool {
    cb(ex_preset, math_fn, *info);
    return true;
  };

  switch (op) {
    case NODE_MATH_ADD:
      return dispatch(ex_preset_fast, [](float a, float b) { return a + b; });
    case NODE_MATH_SUBTRACT:
      return dispatch(ex_preset_fast, [](float a, float b) { return a - b; });
    case NODE_MATH_MULTIPLY:
      return dispatch(ex_preset_fast, [](float a, float b) { return a * b; });
    case NODE_MATH_DIVIDE:
      return dispatch(ex_preset_fast, [](float a, float b) { return safe_divide(a, b); });
    case NODE_MATH_POWER:
      return dispatch(ex_preset_slow, [](float a, float b) { return safe_powf(a, b); });
    case NODE_MATH_LOGARITHM:
      return dispatch(ex_preset_slow, [](float a, float b) { return safe_logf(a, b); });
    case NODE_MATH_MINIMUM:
      return dispatch(ex_preset_fast, [](float a, float b) { return std::min(a, b); });
    case NODE_MATH_MAXIMUM:
      return dispatch(ex_preset_fast, [](float a, float b) { return std::max(a, b); });
    case NODE_MATH_LESS_THAN:
      return dispatch(ex_preset_fast, [](float a, float b) { return (float)(a < b); });
    case NODE_MATH_GREATER_THAN:
      return dispatch(ex_preset_fast, [](float a, float b) { return (float)(a > b); });
    case NODE_MATH_MODULO:
      return dispatch(ex_preset_fast, [](float a, float b) { return safe_modf(a, b); });
    case NODE_MATH_FLOORED_MODULO:
      return dispatch(ex_preset_fast, [](float a, float b) { return safe_floored_modf(a, b); });
    case NODE_MATH_SNAP:
      return dispatch(ex_preset_fast,
                      [](float a, float b) { return floorf(safe_divide(a, b)) * b; });
    case NODE_MATH_ARCTAN2:
      return dispatch(ex_preset_slow, [](float a, float b) { return atan2f(a, b); });
    case NODE_MATH_PINGPONG:
      return dispatch(ex_preset_fast, [](float a, float b) { return pingpongf(a, b); });
  }
  return false;
}

/* This is similar to try_dispatch_float_math_fl_to_fl, just with a diff cb signature. */
template<typename Cb>
inline bool try_dispatch_float_math_fl_fl_fl_to_fl(const int op, Cb &&cb)
{
  const FloatMathOpInfo *info = get_float_math_op_info(op);
  if (info == nullptr) {
    return false;
  }

  /* This is just an util fn to keep the individual cases smaller. */
  auto dispatch = [&](auto ex_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_MATH_MULTIPLY_ADD:
      return dispatch(mf::build::exec_presets::AllSpanOrSingle(),
                      [](float a, float b, float c) { return a * b + c; });
    case NODE_MATH_COMPARE:
      return dispatch(mf::build::exec_presets::SomeSpanOrSingle<0, 1>(),
                      [](float a, float b, float c) -> float {
                        return ((a == b) || (fabsf(a - b) <= fmaxf(c, FLT_EPSILON))) ? 1.0f : 0.0f;
                      });
    case NODE_MATH_SMOOTH_MIN:
      return dispatch(mf::build::exec_presets::SomeSpanOrSingle<0, 1>(),
                      [](float a, float b, float c) { return smoothminf(a, b, c); });
    case NODE_MATH_SMOOTH_MAX:
      return dispatch(mf::build::exec_presets::SomeSpanOrSingle<0, 1>(),
                      [](float a, float b, float c) { return -smoothminf(-a, -b, c); });
    case NODE_MATH_WRAP:
      return dispatch(mf::build::exec_presets::SomeSpanOrSingle<0>(),
                      [](float a, float b, float c) { return wrapf(a, b, c); });
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_fl3_to_fl3(const NodeVectorMathOperation operation,
                                                   Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_fast = mf::build::exec_presets::AllSpanOrSingle();
  static auto exec_preset_slow = mf::build::exec_presets::Materialized();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_ADD:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return a + b; });
    case NODE_VECTOR_MATH_SUBTRACT:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return a - b; });
    case NODE_VECTOR_MATH_MULTIPLY:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return a * b; });
    case NODE_VECTOR_MATH_DIVIDE:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return safe_divide(a, b); });
    case NODE_VECTOR_MATH_CROSS_PRODUCT:
      return dispatch(exec_preset_fast,
                      [](float3 a, float3 b) { return cross_high_precision(a, b); });
    case NODE_VECTOR_MATH_PROJECT:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return project(a, b); });
    case NODE_VECTOR_MATH_REFLECT:
      return dispatch(exec_preset_fast,
                      [](float3 a, float3 b) { return reflect(a, normalize(b)); });
    case NODE_VECTOR_MATH_SNAP:
      return dispatch(exec_preset_fast,
                      [](float3 a, float3 b) { return floor(safe_divide(a, b)) * b; });
    case NODE_VECTOR_MATH_MODULO:
      return dispatch(exec_preset_slow, [](float3 a, float3 b) { return mod(a, b); });
    case NODE_VECTOR_MATH_MINIMUM:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return min(a, b); });
    case NODE_VECTOR_MATH_MAXIMUM:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return max(a, b); });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_fl3_to_fl(const NodeVectorMathOperation operation,
                                                  Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_fast = mf::build::exec_presets::AllSpanOrSingle();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_DOT_PRODUCT:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return dot(a, b); });
    case NODE_VECTOR_MATH_DISTANCE:
      return dispatch(exec_preset_fast, [](float3 a, float3 b) { return distance(a, b); });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_fl3_fl3_to_fl3(const NodeVectorMathOperation operation,
                                                       Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_fast = mf::build::exec_presets::AllSpanOrSingle();
  static auto exec_preset_slow = mf::build::exec_presets::Materialized();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_MULTIPLY_ADD:
      return dispatch(exec_preset_fast, [](float3 a, float3 b, float3 c) { return a * b + c; });
    case NODE_VECTOR_MATH_WRAP:
      return dispatch(exec_preset_slow, [](float3 a, float3 b, float3 c) {
        return float3(wrapf(a.x, b.x, c.x), wrapf(a.y, b.y, c.y), wrapf(a.z, b.z, c.z));
      });
    case NODE_VECTOR_MATH_FACEFORWARD:
      return dispatch(exec_preset_fast,
                      [](float3 a, float3 b, float3 c) { return faceforward(a, b, c); });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_fl3_fl_to_fl3(const NodeVectorMathOperation operation,
                                                      Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_slow = mf::build::exec_presets::Materialized();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_REFRACT:
      return dispatch(exec_preset_slow,
                      [](float3 a, float3 b, float c) { return refract(a, normalize(b), c); });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_to_fl(const NodeVectorMathOperation operation,
                                              Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_fast = mf::build::exec_presets::AllSpanOrSingle();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_LENGTH:
      return dispatch(exec_preset_fast, [](float3 in) { return length(in); });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_fl_to_fl3(const NodeVectorMathOperation operation,
                                                  Callback &&callback)
{
  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto exec_preset_fast = mf::build::exec_presets::AllSpanOrSingle();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(ex_preset, math_function, *info);
    return true;
  };

  switch (op) {
    case NODE_VECTOR_MATH_SCALE:
      return dispatch(ex_preset_fast, [](float3 a, float b) { return a * b; });
    default:
      return false;
  }
  return false;
}

/**
 * This is similar to try_dispatch_float_math_fl_to_fl, just with a different callback signature.
 */
template<typename Callback>
inline bool try_dispatch_float_math_fl3_to_fl3(const NodeVectorMathOperation operation,
                                               Callback &&callback)
{
  using namespace blender::math;

  const FloatMathOperationInfo *info = get_float3_math_operation_info(operation);
  if (info == nullptr) {
    return false;
  }

  static auto ex_preset_fast = mf::build::exec_presets::AllSpanOrSingle();
  static auto ex_preset_slow = mf::build::exec_presets::Materialized();

  /* This is just a utility function to keep the individual cases smaller. */
  auto dispatch = [&](auto exec_preset, auto math_function) -> bool {
    callback(exec_preset, math_function, *info);
    return true;
  };

  switch (operation) {
    case NODE_VECTOR_MATH_NORMALIZE:
      /* Should be safe. */
      return dispatch(ex_preset_fast, [](float3 in) { return normalize(in); });
    case NODE_VECTOR_MATH_FLOOR:
      return dispatch(ex_preset_fast, [](float3 in) { return floor(in); });
    case NODE_VECTOR_MATH_CEIL:
      return dispatch(ex_preset_fast, [](float3 in) { return ceil(in); });
    case NODE_VECTOR_MATH_FRACTION:
      return dispatch(ex_preset_fast, [](float3 in) { return fract(in); });
    case NODE_VECTOR_MATH_ABSOLUTE:
      return dispatch(ex_preset_fast, [](float3 in) { return abs(in); });
    case NODE_VECTOR_MATH_SINE:
      return dispatch(ex_preset_slow,
                      [](float3 in) { return float3(sinf(in.x), sinf(in.y), sinf(in.z)); });
    case NODE_VECTOR_MATH_COSINE:
      return dispatch(ex_preset_slow,
                      [](float3 in) { return float3(cosf(in.x), cosf(in.y), cosf(in.z)); });
    case NODE_VECTOR_MATH_TANGENT:
      return dispatch(ex_preset_slow,
                      [](float3 in) { return float3(tanf(in.x), tanf(in.y), tanf(in.z)); });
    default:
      return false;
  }
  return false;
}

}  // namespace blender::nodes
