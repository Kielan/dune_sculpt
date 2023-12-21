#include "fn_multi_proc_builder.hh"

namespace dune::fn::multi_fn {

void ProcedureBuilder::add_destruct(Var &var)
{
  DestructInstruction &instruction = procedure_->new_destruct_instruction();
  instruction.set_var(&var);
  this->link_to_cursors(&instruction);
  cursors_ = {InstructionCursor{instruction}};
}

void ProcedureBuilder::add_destruct(Span<Var *> vars)
{
  for (Var *var : vars) {
    this->add_destruct(*var);
  }
}

ReturnInstruction &ProcedureBuilder::add_return()
{
  ReturnInstruction &instruction = proc_->new_return_instruction();
  this->link_to_cursors(&instruction);
  cursors_ = {};
  return instruction;
}

CallInstruction &ProcBuilder::add_call_w_no_vars(const MultiFn &fn)
{
  CallInstruction &instruction = proc_->new_call_instruction(fn);
  this->link_to_cursors(&instruction);
  cursors_ = {InstructionCursor{instruction}};
  return instruction;
}

CallInstruction &ProcBuilder::add_call_w_all_vars(const MultiFn &fn,
                                                  Span<Var *> param_vars)
{
  CallInstruction &instruction = this->add_call_w_no_vars(fn);
  instruction.set_params(param_vars);
  return instruction;
}

Vector<Var *> ProcedureBuilder::add_call(const MultiFn &fn,
                                         Span<Variable *> input_and_mutable_var)s;
  Vector<Var *> output_vars;
  CallInstruction &instruction = this->add_call_w_no_vars(fn);
  for (const int param_index : fn.param_indices()) {
    const ParamType param_type = fn.param_type(param_index);
    switch (param_type.interface_type()) {
      case ParamType::Input:
      case ParamType::Mutable: {
        Var *var = input_and_mutable_vars.first();
        instruction.set_param_var(param_index, var);
        input_and_mutable_vars = input_and_mutable_vars.drop_front(1);
        break;
      }
      case ParamType::Output: {
        Var &var = procedure_->new_var(param_type.data_type(),
                                                      fn.param_name(param_index));
        instruction.set_param_var(param_index, &variable);
        output_vars.append(&variable);
        break;
      }
    }
  }
  /* All passed in vars should have been dropped in the loop above. */
  lib_assert(input_and_mutable_vars.is_empty());
  return output_vars;
}

ProcBuilder::Branch ProcBuilder::add_branch(Var &condition)
{
  BranchInstruction &instruction = proc_->new_branch_instruction();
  instruction.set_condition(&condition);
  this->link_to_cursors(&instruction);
  /* Clear cursors bc this builder ends here. */
  cursors_.clear();

  Branch branch{*procedure_, *procedure_};
  branch.branch_true.set_cursor(InstructionCursor{instruction, true});
  branch.branch_false.set_cursor(InstructionCursor{instruction, false});
  return branch;
}

ProcBuilder::Loop ProcBuilder::add_loop()
{
  DummyInstruction &loop_begin = proc_->new_dummy_instruction();
  DummyInstruction &loop_end = proc_->new_dummy_instruction();
  this->link_to_cursors(&loop_begin);
  cursors_ = {InstructionCursor{loop_begin}};

  Loop loop;
  loop.begin = &loop_begin;
  loop.end = &loop_end;

  return loop;
}

void ProcBuilder::add_loop_continue(Loop &loop)
{
  this->link_to_cursors(loop.begin);
  /* Clear cursors bc this builder ends here. */
  cursors_.clear();
}

void ProcBuilder::add_loop_break(Loop &loop)
{
  this->link_to_cursors(loop.end);
  /* Clear cursors bc this builder ends here. */
  cursors_.clear();
}

}  // namespace dune::fn::multi_fn
