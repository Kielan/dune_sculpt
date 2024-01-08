#pragma once

/* A `MultiFn` encapsulates a fn that is optimized for throughput (instead of latency).
 * The throughput is optimized by always proc'ing many elems at once instead of each elem
 * separately. Ideal for fns that are eval often (e.g. for every particle).
 *
 * By proc'ing a lot of data at once, individual fns become easier to optimize for humans
 * and for the compiler. Furthermore, performance profiles become easier to understand and show
 * better where bottlenecks are.
 *
 * Every multi-fn has a name and an ordered list of params. Params are used for input
 * and output. In fact, there are three kinds of params: inputs, outputs and mutable (which is
 * combination of input and output).
 *
 * To call a multi-fn provide 3 things:
 * - `MFParams`: This refes the input and output arrays that the fn works w. The
 *      arrays are not owned by MFParams.
 * - `IndexMask`: An array of indices indicating which indices in the provided arrays should be
 *      touched/processed.
 * - `MFCxt`: Further information for the called function.
 *
 * A new multi-fn is generally implemented as follows:
 * 1. Create a new subclass of MultiFn.
 * 2. Implement a constructor that init'd
 * the signature of the fn.
 * 3. Override the `call` fn. */
#include "lib_hash.hh"

#include "FN_multi_fn_cxt.hh"
#include "FN_multi_fn_params.hh"

namespace dune::fn {

class MultiFn {
 private:
  const MFSignature *signature_ref_ = nullptr;

 public:
  virtual ~MultiFn()
  {
  }

  /*The result is the same as using call directly but this method has some additional features.
   * - Automatic multi-threading when possible and appropriate.
   * - Automatic index mask offsetting to avoid large temp intermediate arrays that are mostly
   *   unused. */
  void call_auto(IndexMask mask, MFParams params, MFContext context) const;
  virtual void call(IndexMask mask, MFParams params, MFContext context) const = 0;

  virtual uint64_t hash() const
  {
    return get_default_hash(this);
  }

  virtual bool equals(const MultiFn &UNUSED(other)) const
  {
    return false;
  }

  int param_amount() const
  {
    return signature_ref_->param_types.size();
  }

  IndexRange param_indices() const
  {
    return signature_ref_->param_types.index_range();
  }

  MFParamType param_type(int param_index) const
  {
    return signature_ref_->param_types[param_index];
  }

  StringRefNull param_name(int param_index) const
  {
    return signature_ref_->param_names[param_index];
  }

  StringRefNull name() const
  {
    return signature_ref_->function_name;
  }

  virtual std::string debug_name() const;

  bool depends_on_context() const
  {
    return signature_ref_->depends_on_context;
  }

  const MFSignature &signature() const
  {
    BLI_assert(signature_ref_ != nullptr);
    return *signature_ref_;
  }

  /**
   * Information about how the multi-function behaves that help a caller to execute it efficiently.
   */
  struct ExecutionHints {
    /**
     * Suggested minimum workload under which multi-threading does not really help.
     * This should be lowered when the multi-function is doing something computationally expensive.
     */
    int64_t min_grain_size = 10000;
    /**
     * Indicates that the multi-function will allocate an array large enough to hold all indices
     * passed in as mask. This tells the caller that it would be preferable to pass in smaller
     * indices. Also maybe the full mask should be split up into smaller segments to decrease peak
     * memory usage.
     */
    bool allocates_array = false;
    /**
     * Tells the caller that every execution takes about the same time. This helps making a more
     * educated guess about a good grain size.
     */
    bool uniform_execution_time = true;
  };

  ExecutionHints execution_hints() const;

 protected:
  /* Make the function use the given signature. This should be called once in the constructor of
   * child classes. No copy of the signature is made, so the caller has to make sure that the
   * signature lives as long as the multi function. It is ok to embed the signature into the child
   * class. */
  void set_signature(const MFSignature *signature)
  {
    /* Take a pointer as argument, so that it is more obvious that no copy is created. */
    BLI_assert(signature != nullptr);
    signature_ref_ = signature;
  }

  virtual ExecutionHints get_execution_hints() const;
};

inline MFParamsBuilder::MFParamsBuilder(const MultiFunction &fn, int64_t mask_size)
    : MFParamsBuilder(fn.signature(), IndexMask(mask_size))
{
}

inline MFParamsBuilder::MFParamsBuilder(const MultiFunction &fn, const IndexMask *mask)
    : MFParamsBuilder(fn.signature(), *mask)
{
}

namespace multi_function_types {
using fn::MFContext;
using fn::MFContextBuilder;
using fn::MFDataType;
using fn::MFParams;
using fn::MFParamsBuilder;
using fn::MFParamType;
using fn::MultiFunction;
}  // namespace multi_function_types

}  // namespace blender::fn
