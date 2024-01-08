#pragma once

/* Mfn has an arbitrary amnt of params.
 * Params belongs to 1 of 3 interface types:
 * - Input: An input param is readonly inside the fn.
 *     Vals must be provided by the caller.
 * - Output: An output param must be initd by the fn.
 *     Caller provides the mem where the data is to be constructed.
 * - Mutable: A mutable param can be considered to be an input and output
 *     Caller is to init the data. The fn is allowed to modify it.
 *
 * MFNParam has a MFNDataType; describes the kind of data is being passed
 * around. */
#include "fn_multi_data_type.hh"

namespace dune::fn {

class MFParamType {
 public:
  enum InterfaceType {
    Input,
    Output,
    Mutable,
  };

  enum Category {
    SingleInput,
    VectorInput,
    SingleOutput,
    VectorOutput,
    SingleMutable,
    VectorMutable,
  };

 private:
  InterfaceType interface_type_;
  MFDataType data_type_;

 public:
  MFParamType(InterfaceType interface_type, MFDataType data_type)
      : interface_type_(interface_type), data_type_(data_type)
  {
  }

  static MFParamType ForSingleInput(const CPPType &type)
  {
    return MFParamType(InterfaceType::Input, MFDataType::ForSingle(type));
  }

  static MFParamType ForVectorInput(const CPPType &base_type)
  {
    return MFParamType(InterfaceType::Input, MFDataType::ForVector(base_type));
  }

  static MFParamType ForSingleOutput(const CPPType &type)
  {
    return MFParamType(InterfaceType::Output, MFDataType::ForSingle(type));
  }

  static MFParamType ForVectorOutput(const CPPType &base_type)
  {
    return MFParamType(InterfaceType::Output, MFDataType::ForVector(base_type));
  }

  static MFParamType ForMutableSingle(const CPPType &type)
  {
    return MFParamType(InterfaceType::Mutable, MFDataType::ForSingle(type));
  }

  static MFParamType ForMutableVector(const CPPType &base_type)
  {
    return MFParamType(InterfaceType::Mutable, MFDataType::ForVector(base_type));
  }

  MFDataType data_type() const
  {
    return data_type_;
  }

  InterfaceType interface_type() const
  {
    return interface_type_;
  }

  Category category() const
  {
    switch (data_type_.category()) {
      case MFDataType::Single: {
        switch (interface_type_) {
          case Input:
            return SingleInput;
          case Output:
            return SingleOutput;
          case Mutable:
            return SingleMutable;
        }
        break;
      }
      case MFDataType::Vector: {
        switch (interface_type_) {
          case Input:
            return VectorInput;
          case Output:
            return VectorOutput;
          case Mutable:
            return VectorMutable;
        }
        break;
      }
    }
    lib_assert(false);
    return SingleInput;
  }

  bool is_input_or_mutable() const
  {
    return elem(interface_type_, Input, Mutable);
  }

  bool is_output_or_mutable() const
  {
    return elem(interface_type_, Output, Mutable);
  }

  bool is_output() const
  {
    return interface_type_ == Output;
  }

  friend bool operator==(const MFParamType &a, const MFParamType &b);
  friend bool operator!=(const MFParamType &a, const MFParamType &b);
};

inline bool operator==(const MFParamType &a, const MFParamType &b)
{
  return a.interface_type_ == b.interface_type_ && a.data_type_ == b.data_type_;
}

inline bool operator!=(const MFParamType &a, const MFParamType &b)
{
  return !(a == b);
}

}  // namespace dune::fn
