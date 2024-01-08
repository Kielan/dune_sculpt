#pragma once

#include "fn_multi_fn_proc.hh"

namespace dune::fn {

/* Util class to build a MFProc. */
class MFProcBuilder {
 private:
  /* Procedure that is being build. */
  MFProc *proc_ = nullptr;
  /* Cursors where the next Instruct should be inserted. */
  Vector<MFInstructCursor> cursors_;

 public:
  struct Branch;
  struct Loop;

  MFProcBuilder(MFProc &proc,
                     MFInstructionCursor initial_cursor = MFInstructionCursor::ForEntry());

  MFProcBuilder(Span<MFProcBuilder *> builders);

  MFProcBuilder(Branch &branch);

  void set_cursor(const MFInstructCursor &cursor);
  void set_cursor(Span<MFInstructCursor> cursors);
  void set_cursor(Span<MFProcBuilder *> builders);
  void set_cursor_after_branch(Branch &branch);
  void set_cursor_after_loop(Loop &loop);

  void add_destruct(MFVar &var);
  void add_destruct(Span<MFVar *> var);

  MFReturnInstruct &add_return();

  Branch add_branch(MFVar &condition);

  Loop add_loop();
  void add_loop_continue(Loop &loop);
  void add_loop_break(Loop &loop);

  MFCallInstruct &add_call_with_no_vars(const MultiFn &fn);
  MFCallInstruct &add_call_with_all_vars(const MultiFn &fn,
                                         Span<MFVar *> param_vars);

  Vector<MFVariable *> add_call(const MultiFn &fn,
                                Span<MFVariable *> input_and_mutable_variables = {});

  template<int OutputN>
  std::array<MFVariable *, OutputN> add_call(const MultiFn &fn,
                                             Span<MFVariable *> input_and_mutable_variables = {});

  void add_param(MFParamType::InterfaceType interface_type, MFVariable &variable);
  MFVariable &add_param(MFParamType param_type, std::string name = "");

  MFVariable &add_input_param(MFDataType data_type, std::string name = "");
  template<typename T> MFVariable &add_single_input_param(std::string name = "");
  template<typename T> MFVariable &add_single_mutable_param(std::string name = "");

  void add_output_param(MFVariable &variable);

 private:
  void link_to_cursors(MFInstruction *instruction);
};

struct MFProcBuilder::Branch {
  MFProcBuilder branch_true;
  MFProcBuilder branch_false;
};

struct MFProcBuilder::Loop {
  MFInstruction *begin = nullptr;
  MFDummyInstruction *end = nullptr;
};

/* MFProcBuilder inline methods. */
inline MFProcBuilder::MFProcBuilder(Branch &branch)
    : MFProcBuilder(*branch.branch_true.proc_)
{
  this->set_cursor_after_branch(branch);
}

inline MFProcBuilder::MFProcBuilder(MFProc &proc,
                                    MFInstructionCursor initial_cursor)
    : procedure_(&procedure), cursors_({initial_cursor})
{
}

inline MFProcBuilder::MFProcBuilder(Span<MFProcBuilder *> builders)
    : MFProcBuilder(*builders[0]->proc_)
{
  this->set_cursor(builders);
}

inline void MFProcBuilder::set_cursor(const MFInstructionCursor &cursor)
{
  cursors_ = {cursor};
}

inline void MFProcBuilder::set_cursor(Span<MFInstructionCursor> cursors)
{
  cursors_ = cursors;
}

inline void MFProcBuilder::set_cursor_after_branch(Branch &branch)
{
  this->set_cursor({&branch.branch_false, &branch.branch_true});
}

inline void MFProcBuilder::set_cursor_after_loop(Loop &loop)
{
  this->set_cursor(MFInstructionCursor{*loop.end});
}

inline void MFProcBuilder::set_cursor(Span<MFProcedureBuilder *> builders)
{
  cursors_.clear();
  for (MFProcBuilder *builder : builders) {
    cursors_.extend(builder->cursors_);
  }
}

template<int OutputN>
inline std::array<MFVariable *, OutputN> MFProcBuilder::add_call(
    const MultiFn &fn, Span<MFVariable *> input_and_mutable_variables)
{
  Vector<MFVariable *> output_variables = this->add_call(fn, input_and_mutable_variables);
  lib_assert(output_variables.size() == OutputN);

  std::array<MFVariable *, OutputN> output_array;
  initialized_copy_n(output_variables.data(), OutputN, output_array.data());
  return output_array;
}

inline void MFProcBuilder::add_param(MFParamType::InterfaceType interface_type,
                                     MFVariable &variable)
{
  procedure_->add_param(interface_type, variable);
}

inline MFVariable &MFProcBuilder::add_param(MFParamType param_type, std::string name)
{
  MFVariable &variable = procedure_->new_variable(param_type.data_type(), std::move(name));
  this->add_param(param_type.interface_type(), variable);
  return variable;
}

inline MFVariable &MFProcBuilder::add_input_param(MFDataType data_type, std::string name)
{
  return this->add_param(MFParamType(MFParamType::Input, data_type), std::move(name));
}

template<typename T>
inline MFVariable &MFProcBuilder::add_single_input_param(std::string name)
{
  return this->add_param(MFParamType::ForSingleInput(CPPType::get<T>()), std::move(name));
}

template<typename T>
inline MFVariable &MFProcBuilder::add_single_mutable_param(std::string name)
{
  return this->add_param(MFParamType::ForMutableSingle(CPPType::get<T>()), std::move(name));
}

inline void MFProcBuilder::add_output_param(MFVariable &variable)
{
  this->add_param(MFParamType::Output, variable);
}

inline void MFProcBuilder::link_to_cursors(MFInstruction *instruction)
{
  for (MFInstructionCursor &cursor : cursors_) {
    cursor.set_next(*proc_, instruction);
  }
}

}  // namespace dune::fn
