#include "fn_multi.hh"

namespace dune::fn::multi_fn::tests {

class AddPrefixFn : public MultiFn {
 public:
  AddPrefixFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Add Prefix", signature};
      builder.single_input<std::string>("Prefix");
      builder.single_mutable<std::string>("Strings");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    const VArray<std::string> &prefixes = params.readonly_single_input<std::string>(0, "Prefix");
    MutableSpan<std::string> strings = params.single_mutable<std::string>(1, "Strings");

    mask.foreach_index([&](const int64_t i) { strings[i] = prefixes[i] + strings[i]; });
  }
};

class CreateRangeFn : public MultiFn {
 public:
  CreateRangeFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Create Range", signature};
      builder.single_input<int>("Size");
      builder.vector_output<int>("Range");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    const VArray<int> &sizes = params.readonly_single_input<int>(0, "Size");
    GVectorArray &ranges = params.vector_output(1, "Range");

    mask.foreach_index([&](const int64_t i) {
      int size = sizes[i];
      for (int j : IndexRange(size)) {
        ranges.append(i, &j);
      }
    });
  }
};

class GenericAppendFn : public MultiFn {
 private:
  Signature signature_;

 public:
  GenericAppendFn(const CPPType &type)
  {
    SignatureBuilder builder{"Append", signature_};
    builder.vector_mutable("Vector", type);
    builder.single_input("Val", type);
    this->set_signature(&signature_);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    GVectorArray &vectors = params.vector_mutable(0, "Vector");
    const GVArray &vals = params.readonly_single_input(1, "Val");

    mask.foreach_index([&](const int64_t i) {
      BUF_FOR_CPP_TYPE_VAL(vals.type(), buf);
      vals.get(i, buf);
      vectors.append(i, buf);
      vals.type().destruct(buf);
    });
  }
};

class ConcatVectorsFn : public MultiFn {
 public:
  ConcatVectorsFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Concat Vectors", signature};
      builder.vector_mutable<int>("A");
      builder.vector_input<int>("B");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    GVectorArray &a = params.vector_mutable(0);
    const GVVectorArray &b = params.readonly_vector_input(1);
    a.extend(mask, b);
  }
};

class AppendFn : public MultiFn {
 public:
  AppendFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Append", signature};
      builder.vector_mutable<int>("Vector");
      builder.single_input<int>("Val");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    GVectorArray_TypedMutableRef<int> vectors = params.vector_mutable<int>(0);
    const VArray<int> &vals = params.readonly_single_input<int>(1);

    mask.foreach_index([&](const int64_t i) { vectors.append(i, vals[i]); });
  }
};

class SumVectorFn : public MultiFn {
 public:
  SumVectorFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Sum Vectors", signature};
      builder.vector_input<int>("Vector");
      builder.single_output<int>("Sum");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    const VVectorArray<int> &vectors = params.readonly_vector_input<int>(0);
    MutableSpan<int> sums = params.uninitialized_single_output<int>(1);

    mask.foreach_index([&](const int64_t i) {
      int sum = 0;
      for (int j : IndexRange(vectors.get_vector_size(i))) {
        sum += vectors.get_vector_element(i, j);
      }
      sums[i] = sum;
    });
  }
};

class OptionalOutputsFn : public MultiFn {
 public:
  OptionalOutputsFn()
  {
    static const Signature signature = []() {
      Signature signature;
      SignatureBuilder builder{"Optional Outputs", signature};
      builder.single_output<int>("Out 1");
      builder.single_output<std::string>("Out 2");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, Params params, Cxt /*cxt*/) const override
  {
    if (params.single_output_is_required(0, "Out 1")) {
      MutableSpan<int> vals = params.uninitialized_single_output<int>(0, "Out 1");
      index_mask::masked_fill(vals, 5, mask);
    }
    MutableSpan<std::string> vals = params.uninitialized_single_output<std::string>(1, "Out 2");
    mask.foreach_index(
        [&](const int i) { new (&vals[i]) std::string("hello, this is a long string"); });
  }
};

}  // namespace dune::fn::multi_fn::tests
