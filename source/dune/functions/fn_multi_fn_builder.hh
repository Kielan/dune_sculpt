#pragma once

/* Utils to create multi-fns w less redundant code. */
#include <functional>

#include "fn_multi_fn.hh"

namespace dune::fn {

/* Gen a multi-fn w the following params:
 * 1. single input (SI) of type In1
 * 2. single output (SO) of type Out1
 *
 * Example: create a fn that adds 10 to the incoming vals:
 * CustomMF_SI_SO<int, int> fn("add 10", [](int val) { return val + 10; }); */
template<typename In1, typename Out1> class CustomMF_SI_SO : public MultiFn {
 private:
  using FnT = std::fn<void(IndexMask, const VArray<In1> &, MutableSpan<Out1>)>;
  FnT fn_;
  MFSignature signature_;

 public:
  CustomMF_SI_SO(const char *name, FnT fn) : fn_(std::move(fn))
  {
    MFSignatureBuilder signature{name};
    signature.single_input<In1>("In1");
    signature.single_output<Out1>("Out1");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  template<typename ElemFnT>
  CustomMF_SI_SO(const char *name, ElemFnT elem_fn)
      : CustomMF_SI_SO(name, CustomMF_SI_SO::create_fn(element_fn))
  {
  }

  template<typename ElementFnT> static FnT create_fn(ElemFnT elem_fn)
  {
    return [=](IndexMask mask, const VArray<In1> &in1, MutableSpan<Out1> out1) {
      /* Devirtualization results in a 2-3x speedup for some simple fns. */
      devirtualize_varray(in1, [&](const auto &in1) {
        mask.foreach_index(
            [&](int i) { new (static_cast<void *>(&out1[i])) Out1(element_fn(in1[i])); });
      });
    };
  }

  void call(IndexMask mask, MFParams params, MFCxt UNUSED(cxt)) const override
  {
    const VArray<In1> &in1 = params.readonly_single_input<In1>(0);
    MutableSpan<Out1> out1 = params.uninitialized_single_output<Out1>(1);
    fn_(mask, in1, out1);
  }
};

/* Gen a multi-fn w the following params:
 * 1. single input (SI) of type In1
 * 2. single input (SI) of type In2
 * 3. single output (SO) of type Out */
template<typename In1, typename In2, typename Out1>
class CustomMF_SI_SI_SO : public MultiFn {
 private:
  using FnT =
      std::fn<void(IndexMask, const VArray<In1> &, const VArray<In2> &, MutableSpan<Out1>)>;
  FnT fn_;
  MFSignature signature_;

 public:
  CustomMF_SI_SI_SO(const char *name, FnT fn) : fn_(std::move(fn))
  {
    MFSignatureBuilder signature{name};
    signature.single_input<In1>("In1");
    signature.single_input<In2>("In2");
    signature.single_output<Out1>("Out1");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  template<typename ElemFnT>
  CustomMF_SI_SI_SO(const char *name, ElemFnT elem_fn)
      : CustomMF_SI_SI_SO(name, CustomMF_SI_SI_SO::create_fn(elem_fn))
  {
  }

  template<typename ElemFnT> static FnT create_fn(ElemFnT elem_fn)
  {
    return [=](IndexMask mask,
               const VArray<In1> &in1,
               const VArray<In2> &in2,
               MutableSpan<Out1> out1) {
      /* Devirtualization results in a 2-3x speedup for some simple functions. */
      devirtualize_varray2(in1, in2, [&](const auto &in1, const auto &in2) {
        mask.foreach_index(
            [&](int i) { new (static_cast<void *>(&out1[i])) Out1(element_fn(in1[i], in2[i])); });
      });
    };
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    const VArray<In1> &in1 = params.readonly_single_input<In1>(0);
    const VArray<In2> &in2 = params.readonly_single_input<In2>(1);
    MutableSpan<Out1> out1 = params.uninitialized_single_output<Out1>(2);
    function_(mask, in1, in2, out1);
  }
};

/* Gen a multi-fn wi the following params:
 * 1. single input (SI) of type In1
 * 2. single input (SI) of type In2
 * 3. single input (SI) of type In3
 * 4. single output (SO) of type Out1 */
template<typename In1, typename In2, typename In3, typename Out1>
class CustomMF_SI_SI_SI_SO : public MultiFn {
 private:
  using FonT = std::function<void(IndexMask,
                                       const VArray<In1> &,
                                       const VArray<In2> &,
                                       const VArray<In3> &,
                                       MutableSpan<Out1>)>;
  FnT fn_;
  MFSignature signature_;

 public:
  CustomMF_SI_SI_SI_SO(const char *name, FnT fn) : fn_(std::move(fn))
  {
    MFSignatureBuilder signature{name};
    signature.single_input<In1>("In1");
    signature.single_input<In2>("In2");
    signature.single_input<In3>("In3");
    signature.single_output<Out1>("Out1");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  template<typename ElemFnT>
  CustomMF_SI_SI_SI_SO(const char *name, ElemFnT elem_fn)
      : CustomMF_SI_SI_SI_SO(name, CustomMF_SI_SI_SI_SO::create_fn(elem_fn))
  {
  }

  template<typename ElemFnT> static FnT create_fn(ElemFn elem_fn)
  {
    return [=](IndexMask mask,
               const VArray<In1> &in1,
               const VArray<In2> &in2,
               const VArray<In3> &in3,
               MutableSpan<Out1> out1) {
      mask.foreach_index([&](int i) {
        new (static_cast<void *>(&out1[i])) Out1(element_fn(in1[i], in2[i], in3[i]));
      });
    };
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    const VArray<In1> &in1 = params.readonly_single_input<In1>(0);
    const VArray<In2> &in2 = params.readonly_single_input<In2>(1);
    const VArray<In3> &in3 = params.readonly_single_input<In3>(2);
    MutableSpan<Out1> out1 = params.uninitialized_single_output<Out1>(3);
    function_(mask, in1, in2, in3, out1);
  }
};

/**
 * Generates a multi-function with the following parameters:
 * 1. single input (SI) of type In1
 * 2. single input (SI) of type In2
 * 3. single input (SI) of type In3
 * 4. single input (SI) of type In4
 * 5. single output (SO) of type Out1
 */
template<typename In1, typename In2, typename In3, typename In4, typename Out1>
class CustomMF_SI_SI_SI_SI_SO : public MultiFunction {
 private:
  using FunctionT = std::function<void(IndexMask,
                                       const VArray<In1> &,
                                       const VArray<In2> &,
                                       const VArray<In3> &,
                                       const VArray<In4> &,
                                       MutableSpan<Out1>)>;
  FunctionT function_;
  MFSignature signature_;

 public:
  CustomMF_SI_SI_SI_SI_SO(const char *name, FunctionT function) : function_(std::move(function))
  {
    MFSignatureBuilder signature{name};
    signature.single_input<In1>("In1");
    signature.single_input<In2>("In2");
    signature.single_input<In3>("In3");
    signature.single_input<In4>("In4");
    signature.single_output<Out1>("Out1");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  template<typename ElementFuncT>
  CustomMF_SI_SI_SI_SI_SO(const char *name, ElementFuncT element_fn)
      : CustomMF_SI_SI_SI_SI_SO(name, CustomMF_SI_SI_SI_SI_SO::create_function(element_fn))
  {
  }

  template<typename ElementFuncT> static FunctionT create_function(ElementFuncT element_fn)
  {
    return [=](IndexMask mask,
               const VArray<In1> &in1,
               const VArray<In2> &in2,
               const VArray<In3> &in3,
               const VArray<In4> &in4,
               MutableSpan<Out1> out1) {
      mask.foreach_index([&](int i) {
        new (static_cast<void *>(&out1[i])) Out1(element_fn(in1[i], in2[i], in3[i], in4[i]));
      });
    };
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    const VArray<In1> &in1 = params.readonly_single_input<In1>(0);
    const VArray<In2> &in2 = params.readonly_single_input<In2>(1);
    const VArray<In3> &in3 = params.readonly_single_input<In3>(2);
    const VArray<In4> &in4 = params.readonly_single_input<In4>(3);
    MutableSpan<Out1> out1 = params.uninitialized_single_output<Out1>(4);
    function_(mask, in1, in2, in3, in4, out1);
  }
};

/**
 * Generates a multi-function with the following parameters:
 * 1. single mutable (SM) of type Mut1
 */
template<typename Mut1> class CustomMF_SM : public MultiFunction {
 private:
  using FunctionT = std::function<void(IndexMask, MutableSpan<Mut1>)>;
  FunctionT function_;
  MFSignature signature_;

 public:
  CustomMF_SM(const char *name, FunctionT function) : function_(std::move(function))
  {
    MFSignatureBuilder signature{name};
    signature.single_mutable<Mut1>("Mut1");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  template<typename ElementFuncT>
  CustomMF_SM(const char *name, ElementFuncT element_fn)
      : CustomMF_SM(name, CustomMF_SM::create_function(element_fn))
  {
  }

  template<typename ElementFuncT> static FunctionT create_function(ElementFuncT element_fn)
  {
    return [=](IndexMask mask, MutableSpan<Mut1> mut1) {
      mask.foreach_index([&](int i) { element_fn(mut1[i]); });
    };
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    MutableSpan<Mut1> mut1 = params.single_mutable<Mut1>(0);
    function_(mask, mut1);
  }
};

/**
 * Generates a multi-function that converts between two types.
 */
template<typename From, typename To> class CustomMF_Convert : public MultiFunction {
 public:
  CustomMF_Convert()
  {
    static MFSignature signature = create_signature();
    this->set_signature(&signature);
  }

  static MFSignature create_signature()
  {
    static std::string name = CPPType::get<From>().name() + " to " + CPPType::get<To>().name();
    MFSignatureBuilder signature{name.c_str()};
    signature.single_input<From>("Input");
    signature.single_output<To>("Output");
    return signature.build();
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    const VArray<From> &inputs = params.readonly_single_input<From>(0);
    MutableSpan<To> outputs = params.uninitialized_single_output<To>(1);

    for (int64_t i : mask) {
      new (static_cast<void *>(&outputs[i])) To(inputs[i]);
    }
  }
};

/**
 * A multi-function that outputs the same value every time. The value is not owned by an instance
 * of this function. If #make_value_copy is false, the caller is responsible for destructing and
 * freeing the value.
 */
class CustomMF_GenericConstant : public MultiFunction {
 private:
  const CPPType &type_;
  const void *value_;
  MFSignature signature_;
  bool owns_value_;

  template<typename T> friend class CustomMF_Constant;

 public:
  CustomMF_GenericConstant(const CPPType &type, const void *value, bool make_value_copy);
  ~CustomMF_GenericConstant();
  void call(IndexMask mask, MFParams params, MFContext context) const override;
  uint64_t hash() const override;
  bool equals(const MultiFunction &other) const override;
};

/**
 * A multi-function that outputs the same array every time. The array is not owned by in instance
 * of this function. The caller is responsible for destructing and freeing the values.
 */
class CustomMF_GenericConstantArray : public MultiFunction {
 private:
  GSpan array_;
  MFSignature signature_;

 public:
  CustomMF_GenericConstantArray(GSpan array);
  void call(IndexMask mask, MFParams params, MFContext context) const override;
};

/**
 * Generates a multi-function that outputs a constant value.
 */
template<typename T> class CustomMF_Constant : public MultiFunction {
 private:
  T value_;
  MFSignature signature_;

 public:
  template<typename U> CustomMF_Constant(U &&value) : value_(std::forward<U>(value))
  {
    MFSignatureBuilder signature{"Constant"};
    signature.single_output<T>("Value");
    signature_ = signature.build();
    this->set_signature(&signature_);
  }

  void call(IndexMask mask, MFParams params, MFContext UNUSED(context)) const override
  {
    MutableSpan<T> output = params.uninitialized_single_output<T>(0);
    mask.foreach_index([&](int i) { new (&output[i]) T(value_); });
  }

  uint64_t hash() const override
  {
    return get_default_hash(value_);
  }

  bool equals(const MultiFunction &other) const override
  {
    const CustomMF_Constant *other1 = dynamic_cast<const CustomMF_Constant *>(&other);
    if (other1 != nullptr) {
      return value_ == other1->value_;
    }
    const CustomMF_GenericConstant *other2 = dynamic_cast<const CustomMF_GenericConstant *>(
        &other);
    if (other2 != nullptr) {
      const CPPType &type = CPPType::get<T>();
      if (type == other2->type_) {
        return type.is_equal_or_false(static_cast<const void *>(&value_), other2->value_);
      }
    }
    return false;
  }
};

class CustomMF_DefaultOutput : public MultiFunction {
 private:
  int output_amount_;
  MFSignature signature_;

 public:
  CustomMF_DefaultOutput(Span<MFDataType> input_types, Span<MFDataType> output_types);
  void call(IndexMask mask, MFParams params, MFContext context) const override;
};

class CustomMF_GenericCopy : public MultiFunction {
 private:
  MFSignature signature_;

 public:
  CustomMF_GenericCopy(MFDataType data_type);
  void call(IndexMask mask, MFParams params, MFContext context) const override;
};

}  // namespace dune::fn
