#include "fn_multi_builder.hh"

#include "lob_hash.hh"

namespace dune::fn::multi_fn {

CustomMFGenericConst::CustomMFGenericConst(const CPPType &type,
                                           const void *val,
                                           bool make_val_copy)
    : type_(type), owns_val_(make_val_copy)
{
  if (make_val_copy) {
    void *copied_val = mem_malloc_aligned(type.size(), type.alignment(), __func__);
    type.copy_construct(val, copied_val);
    val = copied_val;
  }
  value_ = val;

  SignatureBuilder builder{"Constant", signature_};
  builder.single_output("Value", type);
  this->set_signature(&signature_);
}

CustomMFGenericConst::~CustomMFGenericConst()
{
  if (owns_val_) {
    signature_.params[0].type.data_type().single_type().destruct(const_cast<void *>(value_));
    mem_free(const_cast<void *>(value_));
  }
}

void CustomMFGenericConstant::call(const IndexMask &mask,
                                    Params params,
                                    Context /*context*/) const
{
  GMutableSpan output = params.uninitialized_single_output(0);
  type_.fill_construct_indices(value_, output.data(), mask);
}

uint64_t CustomMFGenericConstant::hash() const
{
  return type_.hash_or_fallback(value_, uintptr_t(this));
}

bool CustomMFGenericConstant::equals(const MultiFunction &other) const
{
  const CustomMFGenericConstant *_other = dynamic_cast<const CustomMF_GenericConstant *>(&other);
  if (_other == nullptr) {
    return false;
  }
  if (type_ != _other->type_) {
    return false;
  }
  return type_.is_equal(value_, _other->value_);
}

CustomMFGenericConstantArray::CustomMF_GenericConstantArray(GSpan array) : array_(array)
{
  const CPPType &type = array.type();
  SignatureBuilder builder{"Constant Vector", signature_};
  builder.vector_output("Value", type);
  this->set_signature(&signature_);
}

void CustomMF_GenericConstantArray::call(const IndexMask &mask,
                                         Params params,
                                         Context /*context*/) const
{
  GVectorArray &vectors = params.vector_output(0);
  mask.foreach_index([&](const int64_t i) { vectors.extend(i, array_); });
}

CustomMFDefaultOutput::CustomMF_DefaultOutput(Span<DataType> input_types,
                                               Span<DataType> output_types)
    : output_amount_(output_types.size())
{
  SignatureBuilder builder{"Default Output", signature_};
  for (DataType data_type : input_types) {
    builder.input("Input", data_type);
  }
  for (DataType data_type : output_types) {
    builder.output("Output", data_type);
  }
  this->set_signature(&signature_);
}
void CustomMFDefaultOutput::call(const IndexMask &mask, Params params, Context /*context*/) const
{
  for (int param_index : this->param_indices()) {
    ParamType param_type = this->param_type(param_index);
    if (!param_type.is_output()) {
      continue;
    }

    if (param_type.data_type().is_single()) {
      GMutableSpan span = params.uninitialized_single_output(param_index);
      const CPPType &type = span.type();
      type.fill_construct_indices(type.default_value(), span.data(), mask);
    }
  }
}

CustomMFGenericCopy::CustomMF_GenericCopy(DataType data_type)
{
  SignatureBuilder builder{"Copy", signature_};
  builder.input("Input", data_type);
  builder.output("Output", data_type);
  this->set_signature(&signature_);
}

void CustomMFGenericCopy::call(const IndexMask &mask, Params params, Context /*context*/) const
{
  const DataType data_type = this->param_type(0).data_type();
  switch (data_type.category()) {
    case DataType::Single: {
      const GVArray &inputs = params.readonly_single_input(0, "Input");
      GMutableSpan outputs = params.uninitialized_single_output(1, "Output");
      inputs.materialize_to_uninitialized(mask, outputs.data());
      break;
    }
    case DataType::Vector: {
      const GVVectorArray &inputs = params.readonly_vector_input(0, "Input");
      GVectorArray &outputs = params.vector_output(1, "Output");
      outputs.extend(mask, inputs);
      break;
    }
  }
}

}  // namespace blender::fn::multi_function
