#pragma once

#include "fn_multi_fn_proc.hh"

namespace dune::fn {

/* Multi-fn that ex a proc internally. */
class MFProcExecutor : public MultiFn {
 private:
  MFSignature signature_;
  const MFProc &proc_;

 public:
  MFProcExecutor(const MFProc &proc);

  void call(IndexMask mask, MFParams params, MFCxt cxt) const override;

 private:
  ExHints get_ex_hints() const override;
};

}  // namespace dune::fn
