#pragma once

#include "fn_multi_fn_proc.hh"

namespace dune::fn {

/* A multi-fn that ex a procedure internally. */
class MFProcExecutor : public MultiFn {
 private:
  MFSignature signature_;
  const MFProcedure &procedure_;

 public:
  MFProcedureExecutor(const MFProc &proc);

  void call(IndexMask mask, MFParams params, MFCtx ctx) const override;

 private:
  ExecutionHints get_execution_hints() const override;
};

}  // namespace dune::fn
