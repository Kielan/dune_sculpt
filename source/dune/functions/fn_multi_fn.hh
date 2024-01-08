#pragma once
/* `MultiFn` encapsulates a fn that is optimized for throughput (instead of latency).
 * Throughput is optimized by always proc'ing many elems at once instead of each elem
 * separately. Ideal for fns that are eval often (e.g. for every particle).
 *
 * By proc'ing a lot of data at once, individual fns become easier to optimize for humans
 * and for the compiler. Performance profiles become easier to understand and show
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

#include "fn_multi_fn_cxt.hh"
#include "fn_multi_fn_params.hh"

namespace dune::fn {

class MultiFn {
 private:
  const MFSignature *signature_ref_ = nullptr;

 public:
  virtual ~MultiFn()
  {
  }

  /* Result: matches call directly but this method has additional features.
   * - Automatic multi-threading when possible and appropriate.
   * - Automatic index mask offsetting to avoid large tmp intermediate arrays that are mostly
   *   unused. */
  void call_auto(IndexMask mask, MFParams params, MFCxt cxt) const;
  virtual void call(IndexMask mask, MFParams params, MFCxt cxt) const = 0;

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

  bool depends_on_cxt() const
  {
    return signature_ref_->depends_on_cxt;
  }

  const MFSignature &signature() const
  {
    lib_assert(signature_ref_ != nullptr);
    return *signature_ref_;
  }

  /* Info about how multi-fn behaves that help a caller to ex it efficiently. */
  struct ExHints {
    /* Suggested min workload under which multi-threading does not rly help.
     * This should be lowered when the multi-fn is doing something computationally expensive. */
    int64_t min_grain_size = 10000;
    /* Indicates that multi-fn will alloc an array large enough to hold all indices
     * passed in as mask. 
     * Tell caller its pref to pass in smaller indices.
     * Full mask should be split up into smaller segments to decrease peak
     * mem usage. */
    bool allocs_array = false;
    /* Tells caller that every ex takes about the same time. 
     * To make a more educated guess about a good grain size.  */
    bool uniform_ex_time = true;
  };

  ExHints ex_hints() const;

 protected:
  /* Make the fn use the given signature. Should be called once in the constructor of
   * child classes. No copy of the signature is made so caller must ensure that the
   * signature lives as long as the multi fn. Ok to embed the signature into the child
   * class. */
  void set_signature(const MFSignature *signature)
  {
    /* Take a ptr as arg, so that it is more obvious that no copy is created. */
    lib_assert(signature != nullptr);
    signature_ref_ = signature;
  }

  virtual ExHints get_ex_hints() const;
};

inline MFParamsBuilder::MFParamsBuilder(const MultiFn &fn, int64_t mask_size)
    : MFParamsBuilder(fn.signature(), IndexMask(mask_size))
{
}

inline MFParamsBuilder::MFParamsBuilder(const MultiFn &fn, const IndexMask *mask)
    : MFParamsBuilder(fn.signature(), *mask)
{
}

namespace multi_fn_types {
using fn::MFCxt;
using fn::MFCxtBuilder;
using fn::MFDataType;
using fn::MFParams;
using fn::MFParamsBuilder;
using fn::MFParamType;
using fn::MultiFn;
}  // namespace multi_fn_types

}  // namespace dune::fn
