#pragma once
/* MFParams and MFParamsBuilder struct.
 * MFParamsBuilder is used by a fn caller to prep params passed into
 * the fn. MFParams is then used inside the called fn to access the params. */

#include <mutex>

#include "lib_generic_ptr.hh"
#include "lib_generic_vector_array.hh"
#include "lib_generic_virtual_vector_array.hh"
#include "lib_resource_scope.hh"

#include "fn_multi_fn_signature.hh"

namespace dune::fn {

class MFParamsBuilder {
 private:
  ResourceScope scope_;
  const MFSignature *signature_;
  IndexMask mask_;
  int64_t min_array_size_;
  Vector<GVArray> virtual_arrays_;
  Vector<GMutableSpan> mutable_spans_;
  Vector<const GVVectorArray *> virtual_vector_arrays_;
  Vector<GVectorArray *> vector_arrays_;

  std::mutex mutex_;
  Vector<std::pair<int, GMutableSpan>> dummy_output_spans_;

  friend class MFParams;

  MFParamsBuilder(const MFSignature &signature, const IndexMask mask)
      : signature_(&signature), mask_(mask), min_array_size_(mask.min_array_size())
  {
  }

 public:
  MFParamsBuilder(const class MultiFn &fn, int64_t size);
  /* The indices refd by the mask has to live longer than the params builder. This is
   * bc the it might have to destruct elems for all masked indices in the end. */
  MFParamsBuilder(const class MultiFn &fn, const IndexMask *mask);

  template<typename T> void add_readonly_single_input_value(T val, StringRef expected_name = "")
  {
    this->add_readonly_single_input(VArray<T>::ForSingle(std::move(value), min_array_size_),
                                    expected_name);
  }
  template<typename T> void add_readonly_single_input(const T *value, StringRef expected_name = "")
  {
    this->add_readonly_single_input(
        GVArray::ForSingleRef(CPPType::get<T>(), min_array_size_, value), expected_name);
  }
  void add_readonly_single_input(const GSpan span, StringRef expected_name = "")
  {
    this->add_readonly_single_input(GVArray::ForSpan(span), expected_name);
  }
  void add_readonly_single_input(GPointer value, StringRef expected_name = "")
  {
    this->add_readonly_single_input(
        GVArray::ForSingleRef(*value.type(), min_array_size_, value.get()), expected_name);
  }
  void add_readonly_single_input(GVArray varray, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForSingleInput(varray.type()), expected_name);
    lib_assert(varray.size() >= min_array_size_);
    virtual_arrays_.append(varray);
  }

  void add_readonly_vector_input(const GVectorArray &vector_array, StringRef expected_name = "")
  {
    this->add_readonly_vector_input(scope_.construct<GVVectorArray_For_GVectorArray>(vector_array),
                                    expected_name);
  }
  void add_readonly_vector_input(const GSpan single_vector, StringRef expected_name = "")
  {
    this->add_readonly_vector_input(
        scope_.construct<GVVectorArray_For_SingleGSpan>(single_vector, min_array_size_),
        expected_name);
  }
  void add_readonly_vector_input(const GVVectorArray &ref, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForVectorInput(ref.type()), expected_name);
    lib_assert(ref.size() >= min_array_size_);
    virtual_vector_arrays_.append(&ref);
  }

  template<typename T> void add_uninitialized_single_output(T *val, StringRef expected_name = "")
  {
    this->add_uninitialized_single_output(GMutableSpan(CPPType::get<T>(), val, 1),
                                          expected_name);
  }
  void add_uninitialized_single_output(GMutableSpan ref, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForSingleOutput(ref.type()), expected_name);
    lib_assert(ref.size() >= min_array_size_);
    mutable_spans_.append(ref);
  }
  void add_ignored_single_output(StringRef expected_name = "")
  {
    this->assert_current_param_name(expected_name);
    const int param_index = this->current_param_index();
    const MFParamType &param_type = signature_->param_types[param_index];
    lib_assert(param_type.category() == MFParamType::SingleOutput);
    const CPPType &type = param_type.data_type().single_type();
    /* An empty span indicates that this is ignored. */
    const GMutableSpan dummy_span{type};
    mutable_spans_.append(dummy_span);
  }

  void add_vector_output(GVectorArray &vector_array, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForVectorOutput(vector_array.type()),
                                    expected_name);
    lib_assert(vector_array.size() >= min_array_size_);
    vector_arrays_.append(&vector_array);
  }

  void add_single_mutable(GMutableSpan ref, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForMutableSingle(ref.type()), expected_name);
    BLI_assert(ref.size() >= min_array_size_);
    mutable_spans_.append(ref);
  }

  void add_vector_mutable(GVectorArray &vector_array, StringRef expected_name = "")
  {
    this->assert_current_param_type(MFParamType::ForMutableVector(vector_array.type()),
                                    expected_name);
    lib_assert(vector_array.size() >= min_array_size_);
    vector_arrays_.append(&vector_array);
  }

  GMutableSpan computed_array(int param_index)
  {
    lib_assert(elem(signature_->param_types[param_index].category(),
                    MFParamType::SingleOutput,
                    MFParamType::SingleMutable));
    int data_index = signature_->data_index(param_index);
    return mutable_spans_[data_index];
  }

  GVectorArray &computed_vector_array(int param_index)
  {
    lib_assert(elem(signature_->param_types[param_index].category(),
                    MFParamType::VectorOutput,
                    MFParamType::VectorMutable));
    int data_index = signature_->data_index(param_index);
    return *vector_arrays_[data_index];
  }

  ResourceScope &resource_scope()
  {
    return scope_;
  }

 private:
  void assert_current_param_type(MFParamType param_type, StringRef expected_name = "")
  {
    UNUSED_VARS_NDEBUG(param_type, expected_name);
#ifdef DEBUG
    int param_index = this->current_param_index();

    if (expected_name != "") {
      StringRef actual_name = signature_->param_names[param_index];
      lib_assert(actual_name == expected_name);
    }

    MFParamType expected_type = signature_->param_types[param_index];
    lin_assert(expected_type == param_type);
#endif
  }

  void assert_current_param_name(StringRef expected_name)
  {
    UNUSED_VARS_NDEBUG(expected_name);
#ifdef DEBUG
    if (expected_name.is_empty()) {
      return;
    }
    const int param_index = this->current_param_index();
    StringRef actual_name = signature_->param_names[param_index];
    lib_assert(actual_name == expected_name);
#endif
  }

  int current_param_index() const
  {
    return virtual_arrays_.size() + mutable_spans_.size() + virtual_vector_arrays_.size() +
           vector_arrays_.size();
  }
};

class MFParams {
 private:
  MFParamsBuilder *builder_;

 public:
  MFParams(MFParamsBuilder &builder) : builder_(&builder)
  {
  }

  template<typename T> VArray<T> readonly_single_input(int param_index, StringRef name = "")
  {
    const GVArray &varray = this->readonly_single_input(param_index, name);
    return varray.typed<T>();
  }
  const GVArray &readonly_single_input(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::SingleInput);
    int data_index = builder_->signature_->data_index(param_index);
    return builder_->virtual_arrays_[data_index];
  }

  /* return True when caller provided a buf for this output param.
   * Allows called multi-fn to skip some computation. Still valid to call
   * uninitialized_single_output when this returns false.
   * In this case a new tmp buf is allocated. */
  bool single_output_is_required(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::SingleOutput);
    int data_index = builder_->signature_->data_index(param_index);
    return !builder_->mutable_spans_[data_index].is_empty();
  }

  template<typename T>
  MutableSpan<T> uninitialized_single_output(int param_index, StringRef name = "")
  {
    return this->uninitialized_single_output(param_index, name).typed<T>();
  }
  GMutableSpan uninitialized_single_output(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::SingleOutput);
    int data_index = builder_->signature_->data_index(param_index);
    GMutableSpan span = builder_->mutable_spans_[data_index];
    if (!span.is_empty()) {
      return span;
    }
    /* The output is ignored by the caller, but the multi-fn does not handle this case.
     * So create a tmp buf that the multi-fn can write to. */
    return this->ensure_dummy_single_output(data_index);
  }

  /* Same as uninitialized_single_output but returns an empty span when the output is not
   * required. */
  template<typename T>
  MutableSpan<T> uninitialized_single_output_if_required(int param_index, StringRef name = "")
  {
    return this->uninitialized_single_output_if_required(param_index, name).typed<T>();
  }
  GMutableSpan uninitialized_single_output_if_required(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::SingleOutput);
    int data_index = builder_->signature_->data_index(param_index);
    return builder_->mutable_spans_[data_index];
  }

  template<typename T>
  const VVectorArray<T> &readonly_vector_input(int param_index, StringRef name = "")
  {
    const GVVectorArray &vector_array = this->readonly_vector_input(param_index, name);
    return builder_->scope_.construct<VVectorArray_For_GVVectorArray<T>>(vector_array);
  }
  const GVVectorArray &readonly_vector_input(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::VectorInput);
    int data_index = builder_->signature_->data_index(param_index);
    return *builder_->virtual_vector_arrays_[data_index];
  }

  template<typename T>
  GVectorArray_TypedMutableRef<T> vector_output(int param_index, StringRef name = "")
  {
    return {this->vector_output(param_index, name)};
  }
  GVectorArray &vector_output(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::VectorOutput);
    int data_index = builder_->signature_->data_index(param_index);
    return *builder_->vector_arrays_[data_index];
  }

  template<typename T> MutableSpan<T> single_mutable(int param_index, StringRef name = "")
  {
    return this->single_mutable(param_index, name).typed<T>();
  }
  GMutableSpan single_mutable(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::SingleMutable);
    int data_index = builder_->signature_->data_index(param_index);
    return builder_->mutable_spans_[data_index];
  }

  template<typename T>
  GVectorArray_TypedMutableRef<T> vector_mutable(int param_index, StringRef name = "")
  {
    return {this->vector_mutable(param_index, name)};
  }
  GVectorArray &vector_mutable(int param_index, StringRef name = "")
  {
    this->assert_correct_param(param_index, name, MFParamType::VectorMutable);
    int data_index = builder_->signature_->data_index(param_index);
    return *builder_->vector_arrays_[data_index];
  }

 private:
  void assert_correct_param(int param_index, StringRef name, MFParamType param_type)
  {
    UNUSED_VARS_NDEBUG(param_index, name, param_type);
#ifdef DEBUG
    lib_assert(builder_->signature_->param_types[param_index] == param_type);
    if (name.size() > 0) {
      lib_assert(builder_->signature_->param_names[param_index] == name);
    }
#endif
  }

  void assert_correct_param(int param_index, StringRef name, MFParamType::Category category)
  {
    UNUSED_VARS_NDEBUG(param_index, name, category);
#ifdef DEBUG
    lib_assert(builder_->signature_->param_types[param_index].category() == category);
    if (name.size() > 0) {
      lib_assert(builder_->signature_->param_names[param_index] == name);
    }
#endif
  }

  GMutableSpan ensure_dummy_single_output(int data_index);
};

}  // namespace dune::fn
