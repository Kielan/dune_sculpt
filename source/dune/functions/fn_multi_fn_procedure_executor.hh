#pragma once

#include "fn_multi_fn_procedure.hh"

namespace dune::fn {

/** A multi-function that executes a procedure internally. */
class MFProcedureExecutor : public MultiFn {
 private:
  MFSignature signature_;
  const MFProcedure &procedure_;

 public:
  MFProcedureExecutor(const MFProcedure &procedure);

  void call(IndexMask mask, MFParams params, MFContext context) const override;

 private:
  ExecutionHints get_execution_hints() const override;
};

}  // namespace dune::fn
