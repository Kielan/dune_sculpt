#pragma once

#include "fn_multi_fn_proc.hh"

namespace dune::fn {

/**
 * Utility class to build a #MFProcedure.
 */
class MFProcBuilder {
 private:
  /** Procedure that is being build. */
  MFProc *proc_ = nullptr;
  /** Cursors where the next instruction should be inserted. */
  Vector<MFInstructionCursor> cursors_;

 public:
  struct Branch;
  struct Loop;

  MFProcBuilder(MFProcedure &procedure,
                     MFInstructionCursor initial_cursor = MFInstructionCursor::ForEntry());

  MFProcBuilder(Span<MFProcedureBuilder *> builders);

  MFProcBuilder(Branch &branch);

  void set_cursor(const MFInstructionCursor &cursor);
  void set_cursor(Span<MFInstructionCursor> cursors);
  void set_cursor(Span<MFProcedureBuilder *> builders);
  void set_cursor_after_branch(Branch &branch);
  void set_cursor_after_loop(Loop &loop);

  void add_destruct(MFVariable &variable);
  void add_destruct(Span<MFVariable *> variables);

  MFReturnInstruction &add_return();

  Branch add_branch(MFVariable &condition);

  Loop add_loop();
  void add_loop_continue(Loop &loop);
  void add_loop_break(Loop &loop);

  MFCallInstruction &add_call_with_no_variables(const MultiFunction &fn);
  MFCallInstruction &add_call_with_all_variables(const MultiFunction &fn,
                                                 Span<MFVariable *> param_variables);

  Vector<MFVariable *> add_call(const MultiFunction &fn,
                                Span<MFVariable *> input_and_mutable_variables = {});

  template<int OutputN>
  std::array<MFVariable *, OutputN> add_call(const MultiFunction &fn,
                                             Span<MFVariable *> input_and_mutable_variables = {});

  void add_parameter(MFParamType::InterfaceType interface_type, MFVariable &variable);
  MFVariable &add_parameter(MFParamType param_type, std::string name = "");

  MFVariable &add_input_parameter(MFDataType data_type, std::string name = "");
  template<typename T> MFVariable &add_single_input_parameter(std::string name = "");
  template<typename T> MFVariable &add_single_mutable_parameter(std::string name = "");

  void add_output_parameter(MFVariable &variable);

 private:
  void link_to_cursors(MFInstruction *instruction);
};

struct MFProcedureBuilder::Branch {
  MFProcedureBuilder branch_true;
  MFProcedureBuilder branch_false;
};

struct MFProcedureBuilder::Loop {
  MFInstruction *begin = nullptr;
  MFDummyInstruction *end = nullptr;
};

/* --------------------------------------------------------------------
 * MFProcedureBuilder inline methods.
 */

inline MFProcedureBuilder::MFProcedureBuilder(Branch &branch)
    : MFProcedureBuilder(*branch.branch_true.procedure_)
{
  this->set_cursor_after_branch(branch);
}

inline MFProcedureBuilder::MFProcedureBuilder(MFProcedure &procedure,
                                              MFInstructionCursor initial_cursor)
    : procedure_(&procedure), cursors_({initial_cursor})
{
}

inline MFProcedureBuilder::MFProcedureBuilder(Span<MFProcedureBuilder *> builders)
    : MFProcedureBuilder(*builders[0]->procedure_)
{
  this->set_cursor(builders);
}

inline void MFProcedureBuilder::set_cursor(const MFInstructionCursor &cursor)
{
  cursors_ = {cursor};
}

inline void MFProcedureBuilder::set_cursor(Span<MFInstructionCursor> cursors)
{
  cursors_ = cursors;
}

inline void MFProcedureBuilder::set_cursor_after_branch(Branch &branch)
{
  this->set_cursor({&branch.branch_false, &branch.branch_true});
}

inline void MFProcedureBuilder::set_cursor_after_loop(Loop &loop)
{
  this->set_cursor(MFInstructionCursor{*loop.end});
}

inline void MFProcedureBuilder::set_cursor(Span<MFProcedureBuilder *> builders)
{
  cursors_.clear();
  for (MFProcedureBuilder *builder : builders) {
    cursors_.extend(builder->cursors_);
  }
}

template<int OutputN>
inline std::array<MFVariable *, OutputN> MFProcedureBuilder::add_call(
    const MultiFunction &fn, Span<MFVariable *> input_and_mutable_variables)
{
  Vector<MFVariable *> output_variables = this->add_call(fn, input_and_mutable_variables);
  BLI_assert(output_variables.size() == OutputN);

  std::array<MFVariable *, OutputN> output_array;
  initialized_copy_n(output_variables.data(), OutputN, output_array.data());
  return output_array;
}

inline void MFProcedureBuilder::add_parameter(MFParamType::InterfaceType interface_type,
                                              MFVariable &variable)
{
  procedure_->add_parameter(interface_type, variable);
}

inline MFVariable &MFProcedureBuilder::add_parameter(MFParamType param_type, std::string name)
{
  MFVariable &variable = procedure_->new_variable(param_type.data_type(), std::move(name));
  this->add_param(param_type.interface_type(), variable);
  return variable;
}

inline MFVariable &MFProcedureBuilder::add_input_parameter(MFDataType data_type, std::string name)
{
  return this->add_param(MFParamType(MFParamType::Input, data_type), std::move(name));
}

template<typename T>
inline MFVariable &MFProcedureBuilder::add_single_input_parameter(std::string name)
{
  return this->add_parameter(MFParamType::ForSingleInput(CPPType::get<T>()), std::move(name));
}

template<typename T>
inline MFVariable &MFProcedureBuilder::add_single_mutable_parameter(std::string name)
{
  return this->add_param(MFParamType::ForMutableSingle(CPPType::get<T>()), std::move(name));
}

inline void MFProcedureBuilder::add_output_parameter(MFVariable &variable)
{
  this->add_param(MFParamType::Output, variable);
}

inline void MFProcedureBuilder::link_to_cursors(MFInstruction *instruction)
{
  for (MFInstructionCursor &cursor : cursors_) {
    cursor.set_next(*procedure_, instruction);
  }
}

}  // namespace dune::fn
