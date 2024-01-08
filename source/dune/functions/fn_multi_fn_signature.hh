#pragma once
/* The signature of a multi-fn contains the fns name and expected params. New
 * signatures should be build using the MFSignatureBuilder class. */
#include "fn_multi_fn_param_type.hh"
#include "lib_vector.hh"

namespace dune::fn {

struct MFSignature {
  /* The name should be statically alloc so that it lives longer than this signature. This is
   * This is used instead of an std::string bc of the overhead when many fns are created.
   * If the name of the fn has to be more dynamic for debugging purposes, override
   * MultiFn::debug_name() instead. Then the dynamic name will only be computed when it is
   * actually needed. */
  const char *fn_name;
  Vector<const char *> param_names;
  Vector<MFParamType> param_types;
  Vector<int> param_data_indices;
  bool depends_on_cxt = false;

  int data_index(int param_index) const
  {
    return param_data_indices[param_index];
  }
};

class MFSignatureBuilder {
 private:
  MFSignature signature_;
  int span_count_ = 0;
  int virtual_array_count_ = 0;
  int virtual_vector_array_count_ = 0;
  int vector_array_count_ = 0;

 public:
  MFSignatureBuilder(const char *fn_name)
  {
    signature_.fn_name = fn_name;
  }

  MFSignature build() const
  {
    return std::move(signature_);
  }

  /* Input Param Types */
  template<typename T> void single_input(const char *name)
  {
    this->single_input(name, CPPType::get<T>());
  }
  void single_input(const char *name, const CPPType &type)
  {
    this->input(name, MFDataType::ForSingle(type));
  }
  template<typename T> void vector_input(const char *name)
  {
    this->vector_input(name, CPPType::get<T>());
  }
  void vector_input(const char *name, const CPPType &base_type)
  {
    this->input(name, MFDataType::ForVector(base_type));
  }
  void input(const char *name, MFDataType data_type)
  {
    signature_.param_names.append(name);
    signature_.param_types.append(MFParamType(MFParamType::Input, data_type));

    switch (data_type.category()) {
      case MFDataType::Single:
        signature_.param_data_indices.append(virtual_array_count_++);
        break;
      case MFDataType::Vector:
        signature_.param_data_indices.append(virtual_vector_array_count_++);
        break;
    }
  }

  /* Output Param Types */
  template<typename T> void single_output(const char *name)
  {
    this->single_output(name, CPPType::get<T>());
  }
  void single_output(const char *name, const CPPType &type)
  {
    this->output(name, MFDataType::ForSingle(type));
  }
  template<typename T> void vector_output(const char *name)
  {
    this->vector_output(name, CPPType::get<T>());
  }
  void vector_output(const char *name, const CPPType &base_type)
  {
    this->output(name, MFDataType::ForVector(base_type));
  }
  void output(const char *name, MFDataType data_type)
  {
    signature_.param_names.append(name);
    signature_.param_types.append(MFParamType(MFParamType::Output, data_type));

    switch (data_type.category()) {
      case MFDataType::Single:
        signature_.param_data_indices.append(span_count_++);
        break;
      case MFDataType::Vector:
        signature_.param_data_indices.append(vector_array_count_++);
        break;
    }
  }

  /* Mutable Param Types */
  template<typename T> void single_mutable(const char *name)
  {
    this->single_mutable(name, CPPType::get<T>());
  }
  void single_mutable(const char *name, const CPPType &type)
  {
    this->mutable_(name, MFDataType::ForSingle(type));
  }
  template<typename T> void vector_mutable(const char *name)
  {
    this->vector_mutable(name, CPPType::get<T>());
  }
  void vector_mutable(const char *name, const CPPType &base_type)
  {
    this->mutable_(name, MFDataType::ForVector(base_type));
  }
  void mutable_(const char *name, MFDataType data_type)
  {
    signature_.param_names.append(name);
    signature_.param_types.append(MFParamType(MFParamType::Mutable, data_type));

    switch (data_type.category()) {
      case MFDataType::Single:
        signature_.param_data_indices.append(span_count_++);
        break;
      case MFDataType::Vector:
        signature_.param_data_indices.append(vector_array_count_++);
        break;
    }
  }

  void add(const char *name, const MFParamType &param_type)
  {
    switch (param_type.interface_type()) {
      case MFParamType::Input:
        this->input(name, param_type.data_type());
        break;
      case MFParamType::Mutable:
        this->mutable_(name, param_type.data_type());
        break;
      case MFParamType::Output:
        this->output(name, param_type.data_type());
        break;
    }
  }

  /* Indicates that fn accesses the cxt. This disables optimizations that
   * depend on the fact that the fn always performs the same op. */
  void depends_on_cxt()
  {
    signature_.depends_on_cxt = true;
  }
};

}  // namespace dune::fn
