#include "fn_multi_proc.hh"

#include "lib_dot_export.hh"
#include "lib_stack.hh"

#include <sstream>

namespace dune::fn::multi_fn {

void InstructionCursor::set_next(Proc &proc, Instruction *new_instruction) const
{
  switch (type_) {
    case Type::None: {
      break;
    }
    case Type::Entry: {
      proc.set_entry(*new_instruction);
      break;
    }
    case Type::Call: {
      static_cast<CallInstruction *>(instruction_)->set_next(new_instruction);
      break;
    }
    case Type::Branch: {
      BranchInstruction &branch_instruction = *static_cast<BranchInstruction *>(instruction_);
      if (branch_output_) {
        branch_instruction.set_branch_true(new_instruction);
      }
      else {
        branch_instruction.set_branch_false(new_instruction);
      }
      break;
    }
    case Type::Destruct: {
      static_cast<DestructInstruction *>(instruction_)->set_next(new_instruction);
      break;
    }
    case Type::Dummy: {
      static_cast<DummyInstruction *>(instruction_)->set_next(new_instruction);
      break;
    }
  }
}

Instruction *InstructionCursor::next(Procedure &procedure) const
{
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Entry:
      return proc.entry();
    case Type::Call:
      return static_cast<CallInstruction *>(instruction_)->next();
    case Type::Branch: {
      BranchInstruction &branch_instruction = *static_cast<BranchInstruction *>(instruction_);
      if (branch_output_) {
        return branch_instruction.branch_true();
      }
      return branch_instruction.branch_false();
    }
    case Type::Destruct:
      return static_cast<DestructInstruction *>(instruction_)->next();
    case Type::Dummy:
      return static_cast<DummyInstruction *>(instruction_)->next();
  }
  return nullptr;
}

void Var::set_name(std::string name)
{
  name_ = std::move(name);
}

void CallInstruction::set_next(Instruction *instruction)
{
  if (next_ != nullptr) {
    next_->prev_.remove_first_occurrence_and_reorder(*this);
  }
  if (instruction != nullptr) {
    instruction->prev_.append(*this);
  }
  next_ = instruction;
}

void CallInstruction::set_param_var(int param_index, Var *var)
{
  if (params_[param_index] != nullptr) {
    params_[param_index]->users_.remove_first_occurrence_and_reorder(this);
  }
  if (var != nullptr) {
    lib_assert(fn_->param_type(param_index).data_type() == var->data_type());
    variable->users_.append(this);
  }
  params_[param_index] = var;
}

void CallInstruction::set_params(Span<Var *> vars)
{
  lib_assert(vars.size() == params_.size());
  for (const int i : vars.index_range()) {
    this->set_param_var(i, vars[i]);
  }
}

void BranchInstruction::set_condition(Var *var)
{
  if (condition_ != nullptr) {
    condition_->users_.remove_first_occurrence_and_reorder(this);
  }
  if (var != nullptr) {
    var->users_.append(this);
  }
  condition_ = var;
}

void BranchInstruction::set_branch_true(Instruction *instruction)
{
  if (branch_true_ != nullptr) {
    branch_true_->prev_.remove_first_occurrence_and_reorder({*this, true});
  }
  if (instruction != nullptr) {
    instruction->prev_.append({*this, true});
  }
  branch_true_ = instruction;
}

void BranchInstruction::set_branch_false(Instruction *instruction)
{
  if (branch_false_ != nullptr) {
    branch_false_->prev_.remove_first_occurrence_and_reorder({*this, false});
  }
  if (instruction != nullptr) {
    instruction->prev_.append({*this, false});
  }
  branch_false_ = instruction;
}

void DestructInstruction::set_var(Var *var)
{
  if (var_ != nullptr) {
    var_->users_.remove_first_occurrence_and_reorder(this);
  }
  if (var != nullptr) {
    var->users_.append(this);
  }
  var_ = var;
}

void DestructInstruction::set_next(Instruction *instruction)
{
  if (next_ != nullptr) {
    next_->prev_.remove_first_occurrence_and_reorder(*this);
  }
  if (instruction != nullptr) {
    instruction->prev_.append(*this);
  }
  next_ = instruction;
}

void DummyInstruction::set_next(Instruction *instruction)
{
  if (next_ != nullptr) {
    next_->prev_.remove_first_occurrence_and_reorder(*this);
  }
  if (instruction != nullptr) {
    instruction->prev_.append(*this);
  }
  next_ = instruction;
}

Var &Proc::new_var(DataType data_type, std::string name)
{
  Var &var = *allocator_.construct<Var>().release();
  var.name_ = std::move(name);
  var.data_type_ = data_type;
  var.index_in_graph_ = vars_.size();
  var_.append(&var);
  return var;
}

CallInstruction &Proc::new_call_instruction(const MultiFn &fn)
{
  CallInstruction &instruction = *allocator_.construct<CallInstruction>().release();
  instruction.type_ = InstructionType::Call;
  instruction.fn_ = &fn;
  instruction.params_ = allocator_.alloc_array<Var *>(fn.param_amount());
  instruction.params_.fill(nullptr);
  call_instructions_.append(&instruction);
  return instruction;
}

BranchInstruction &Proc::new_branch_instruction()
{
  BranchInstruction &instruction = *allocator_.construct<BranchInstruction>().release();
  instruction.type_ = InstructionType::Branch;
  branch_instructions_.append(&instruction);
  return instruction;
}

DestructInstruction &Proc::new_destruct_instruction()
{
  DestructInstruction &instruction = *allocator_.construct<DestructInstruction>().release();
  instruction.type_ = InstructionType::Destruct;
  destruct_instructions_.append(&instruction);
  return instruction;
}

DummyInstruction &Proc::new_dummy_instruction()
{
  DummyInstruction &instruction = *allocator_.construct<DummyInstruction>().release();
  instruction.type_ = InstructionType::Dummy;
  dummy_instructions_.append(&instruction);
  return instruction;
}

ReturnInstruction &Proc::new_return_instruction()
{
  ReturnInstruction &instruction = *allocator_.construct<ReturnInstruction>().release();
  instruction.type_ = InstructionType::Return;
  return_instructions_.append(&instruction);
  return instruction;
}

void Proc::add_param(ParamType::InterfaceType interface_type, Var &var)
{
  params_.append({interface_type, &variable});
}

void Proc::set_entry(Instruction &entry)
{
  if (entry_ != nullptr) {
    entry_->prev_.remove_first_occurrence_and_reorder(InstructionCursor::ForEntry());
  }
  entry_ = &entry;
  entry_->prev_.append(InstructionCursor::ForEntry());
}

Proc::~Proc()
{
  for (CallInstruction *instruction : call_instructions_) {
    instruction->~CallInstruction();
  }
  for (BranchInstruction *instruction : branch_instructions_) {
    instruction->~BranchInstruction();
  }
  for (DestructInstruction *instruction : destruct_instructions_) {
    instruction->~DestructInstruction();
  }
  for (DummyInstruction *instruction : dummy_instructions_) {
    instruction->~DummyInstruction();
  }
  for (ReturnInstruction *instruction : return_instructions_) {
    instruction->~ReturnInstruction();
  }
  for (Var *var : vars_) {
    var->~Var();
  }
}

bool Proc::validate() const
{
  if (entry_ == nullptr) {
    return false;
  }
  if (!this->validate_all_instruction_ptrs_set()) {
    return false;
  }
  if (!this->validate_all_params_provided()) {
    return false;
  }
  if (!this->validate_same_vars_in_one_call()) {
    return false;
  }
  if (!this->validate_params()) {
    return false;
  }
  if (!this->validate_init()) {
    return false;
  }
  return true;
}

bool Proc::validate_all_instruction_ptrs_set() const
{
  for (const CallInstruction *instruction : call_instructions_) {
    if (instruction->next_ == nullptr) {
      return false;
    }
  }
  for (const DestructInstruction *instruction : destruct_instructions_) {
    if (instruction->next_ == nullptr) {
      return false;
    }
  }
  for (const BranchInstruction *instruction : branch_instructions_) {
    if (instruction->branch_true_ == nullptr) {
      return false;
    }
    if (instruction->branch_false_ == nullptr) {
      return false;
    }
  }
  for (const DummyInstruction *instruction : dummy_instructions_) {
    if (instruction->next_ == nullptr) {
      return false;
    }
  }
  return true;
}

bool Proc::validate_all_params_provided() const
{
  for (const CallInstruction *instruction : call_instructions_) {
    const MultiFn &fn = instruction->fn();
    for (const int param_index : fn.param_indices()) {
      const ParamType param_type = fn.param_type(param_index);
      if (param_type.category() == ParamCategory::SingleOutput) {
        /* Single outputs are optional. */
        continue;
      }
      const Var *var = instruction->params_[param_index];
      if (var == nullptr) {
        return false;
      }
    }
  }
  for (const BranchInstruction *instruction : branch_instructions_) {
    if (instruction->condition_ == nullptr) {
      return false;
    }
  }
  for (const DestructInstruction *instruction : destruct_instructions_) {
    if (instruction->var_ == nullptr) {
      return false;
    }
  }
  return true;
}

bool Proc::validate_same_vars_in_one_call() const
{
  for (const CallInstruction *instruction : call_instructions_) {
    const MultiFn &fn = *instruction->fn_;
    for (const int param_index : fn.param_indices()) {
      const ParamType param_type = fn.param_type(param_index);
      const Var *var = instruction->params_[param_index];
      if (variable == nullptr) {
        continue;
      }
      for (const int other_param_index : fn.param_indices()) {
        if (other_param_index == param_index) {
          continue;
        }
        const Var *other_var = instruction->params_[other_param_index];
        if (other_var != var) {
          continue;
        }
        if (ELEM(param_type.interface_type(), ParamType::Mutable, ParamType::Output)) {
          /* When a var is used as mutable or output param, it can only be used once. */
          return false;
        }
        const ParamType other_param_type = fn.param_type(other_param_index);
        /* A var is allowed to be used as input more than once. */
        if (other_param_type.interface_type() != ParamType::Input) {
          return false;
        }
      }
    }
  }
  return true;
}

bool Proc::validate_params() const
{
  Set<const Var *> vars;
  for (const Param &param : params_) {
    /* One var cannot be used as multiple params. */
    if (!vars.add(param.vars)) {
      return false;
    }
  }
  return true;
}

bool Proc::validate_init() const
{
  /* TODO: Issue warning when it maybe wrongly initialized. */
  for (const DestructInstruction *instruction : destruct_instructions_) {
    const Var &var = *instruction->var_;
    const InitState state = this->find_init_state_before_instruction(*instruction,
                                                                     var);
    if (!state.can_be_init) {
      return false;
    }
  }
  for (const BranchInstruction *instruction : branch_instructions_) {
    const Var &var = *instruction->condition_;
    const InitState state = this->find_init_state_before_instruction(*instruction,
                                                                     var);
    if (!state.can_be_init) {
      return false;
    }
  }
  for (const CallInstruction *instruction : call_instructions_) {
    const MultiFn &fn = *instruction->fn_;
    for (const int param_index : fn.param_indices()) {
      const ParamType param_type = fn.param_type(param_index);
      /* If the param was an unneeded output, it could be null. */
      if (!instruction->params_[param_index]) {
        continue;
      }
      const Var &var = *instruction->params_[param_index];
      const InitState state = this->find_init_state_before_instruction(*instruction,
                                                                       var);
      switch (param_type.interface_type()) {
        case ParamType::Input:
        case ParamType::Mutable: {
          if (!state.can_be_init) {
            return false;
          }
          break;
        }
        case ParamType::Output: {
          if (!state.can_be_uninit) {
            return false;
          }
          break;
        }
      }
    }
  }
  Set<const Var *> vars_that_should_be_init_on_return;
  for (const Param &param : params_) {
    if (ELEM(param.type, ParamType::Mutable, ParamType::Output)) {
      vars_that_should_be_init_on_return.add_new(param.var);
    }
  }
  for (const ReturnInstruction *instruction : return_instructions_) {
    for (const Var *var : vars_) {
      const InitState init_state = this->find_init_state_before_instruction(*instruction,
                                                                            *var);
      if (vars_that_should_be_init_on_return.contains(var)) {
        if (!init_state.can_be_init) {
          return false;
        }
      }
      else {
        if (!init_state.can_be_uninit) {
          return false;
        }
      }
    }
  }
  return true;
}

Proc::InitState Proc::find_init_state_before_instruction(
    const Instruction &target_instruction, const Var &target_var) const
{
  InitState state;

  auto check_entry_instruction = [&]() {
    bool caller_init_var = false;
    for (const Param &param : params_) {
      if (param.var == &target_var) {
        if (ELEM(param.type, ParamType::Input, ParamType::Mutable)) {
          caller_init_var = true;
          break;
        }
      }
    }
    if (caller_init_var) {
      state.can_be_init = true;
    }
    else {
      state.can_be_uninit = true;
    }
  };

  if (&target_instruction == entry_) {
    check_entry_instruction();
  }

  Set<const Instruction *> checked_instructions;
  Stack<const Instruction *> instructions_to_check;
  for (const InstructionCursor &cursor : target_instruction.prev_) {
    if (cursor.instruction() != nullptr) {
      instructions_to_check.push(cursor.instruction());
    }
  }

  while (!instructions_to_check.is_empty()) {
    const Instruction &instruction = *instructions_to_check.pop();
    if (!checked_instructions.add(&instruction)) {
      /* Skip if the instruction has been checked alrdy. */
      continue;
    }
    bool state_modified = false;
    switch (instruction.type_) {
      case InstructionType::Call: {
        const CallInstruction &call_instruction = static_cast<const CallInstruction &>(
            instruction);
        const MultiFn &fn = *call_instruction.fn_;
        for (const int param_index : fn.param_indices()) {
          if (call_instruction.params_[param_index] == &target_var) {
            const ParamType param_type = fn.param_type(param_index);
            if (param_type.interface_type() == ParamType::Output) {
              state.can_be_init = true;
              state_modified = true;
              break;
            }
          }
        }
        break;
      }
      case InstructionType::Destruct: {
        const DestructInstruction &destruct_instruction = static_cast<const DestructInstruction &>(
            instruction);
        if (destruct_instruction.var_ == &target_var) {
          state.can_be_uninit = true;
          state_modified = true;
        }
        break;
      }
      case InstructionType::Branch:
      case InstructionType::Dummy:
      case InstructionType::Return: {
        /* These instruction types don't change the init state of vars. */
        break;
      }
    }

    if (!state_modified) {
      if (&instruction == entry_) {
        check_entry_instruction();
      }
      for (const InstructionCursor &cursor : instruction.prev_) {
        if (cursor.instruction() != nullptr) {
          instructions_to_check.push(cursor.instruction());
        }
      }
    }
  }

  return state;
}

class ProcDotExport {
 private:
  const Proc &proc_;
  dot::DirectedGraph digraph_;
  Map<const Instruction *, dot::Node *> dot_nodes_by_begin_;
  Map<const Instruction *, dot::Node *> dot_nodes_by_end_;

 public:
  ProcDotExport(const Proc &proc) : proc_(proc) {}

  std::string generate()
  {
    this->create_nodes();
    this->create_edges();
    return digraph_.to_dot_string();
  }

  void create_nodes()
  {
    Vector<const Instruction *> all_instructions;
    auto add_instructions = [&](auto instructions) {
      all_instructions.extend(instructions.begin(), instructions.end());
    };
    add_instructions(proc_.call_instructions_);
    add_instructions(proc_.branch_instructions_);
    add_instructions(proc_.destruct_instructions_);
    add_instructions(proc_.dummy_instructions_);
    add_instructions(proc_.return_instructions_);

    Set<const Instruction *> handled_instructions;

    for (const Instruction *representative : all_instructions) {
      if (handled_instructions.contains(representative)) {
        continue;
      }
      Vector<const Instruction *> block_instructions = this->get_instructions_in_block(
          *representative);
      std::stringstream ss;
      ss << "<";

      for (const Instruction *current : block_instructions) {
        handled_instructions.add_new(current);
        switch (current->type()) {
          case InstructionType::Call: {
            this->instruction_to_string(*static_cast<const CallInstruction *>(current), ss);
            break;
          }
          case InstructionType::Destruct: {
            this->instruction_to_string(*static_cast<const DestructInstruction *>(current), ss);
            break;
          }
          case InstructionType::Dummy: {
            this->instruction_to_string(*static_cast<const DummyInstruction *>(current), ss);
            break;
          }
          case InstructionType::Return: {
            this->instruction_to_string(*static_cast<const ReturnInstruction *>(current), ss);
            break;
          }
          case InstructionType::Branch: {
            this->instruction_to_string(*static_cast<const BranchInstruction *>(current), ss);
            break;
          }
        }
        ss << R"(<br align="left" />)";
      }
      ss << ">";

      dot::Node &dot_node = digraph_.new_node(ss.str());
      dot_node.set_shape(dot::Attr_shape::Rectangle);
      dot_nodes_by_begin_.add_new(block_instructions.first(), &dot_node);
      dot_nodes_by_end_.add_new(block_instructions.last(), &dot_node);
    }
  }

  void create_edges()
  {
    auto create_edge = [&](dot::Node &from_node,
                           const Instruction *to_instruction) -> dot::DirectedEdge & {
      if (to_instruction == nullptr) {
        dot::Node &to_node = digraph_.new_node("missing");
        to_node.set_shape(dot::Attr_shape::Diamond);
        return digraph_.new_edge(from_node, to_node);
      }
      dot::Node &to_node = *dot_nodes_by_begin_.lookup(to_instruction);
      return digraph_.new_edge(from_node, to_node);
    };

    for (auto item : dot_nodes_by_end_.items()) {
      const Instruction &from_instruction = *item.key;
      dot::Node &from_node = *item.val;
      switch (from_instruction.type()) {
        case InstructionType::Call: {
          const Instruction *to_instruction =
              static_cast<const CallInstruction &>(from_instruction).next();
          create_edge(from_node, to_instruction);
          break;
        }
        case InstructionType::Destruct: {
          const Instruction *to_instruction =
              static_cast<const DestructInstruction &>(from_instruction).next();
          create_edge(from_node, to_instruction);
          break;
        }
        case InstructionType::Dummy: {
          const Instruction *to_instruction =
              static_cast<const DummyInstruction &>(from_instruction).next();
          create_edge(from_node, to_instruction);
          break;
        }
        case InstructionType::Return: {
          break;
        }
        case InstructionType::Branch: {
          const BranchInstruction &branch_instruction = static_cast<const BranchInstruction &>(
              from_instruction);
          const Instruction *to_true_instruction = branch_instruction.branch_true();
          const Instruction *to_false_instruction = branch_instruction.branch_false();
          create_edge(from_node, to_true_instruction).attributes.set("color", "#118811");
          create_edge(from_node, to_false_instruction).attributes.set("color", "#881111");
          break;
        }
      }
    }

    dot::Node &entry_node = this->create_entry_node();
    create_edge(entry_node, procedure_.entry());
  }

  bool has_to_be_block_begin(const Instruction &instruction)
  {
    if (instruction.prev().size() != 1) {
      return true;
    }
    if (ELEM(instruction.prev()[0].type(),
             InstructionCursor::Type::Branch,
             InstructionCursor::Type::Entry))
    {
      return true;
    }
    return false;
  }

  const Instruction &get_first_instruction_in_block(const Instruction &representative)
  {
    const Instruction *current = &representative;
    while (!this->has_to_be_block_begin(*current)) {
      current = current->prev()[0].instruction();
      if (current == &representative) {
        /* There is a loop wo entry or exit, just break it up here. */
        break;
      }
    }
    return *current;
  }

  const Instruction *get_next_instruction_in_block(const Instruction &instruction,
                                                   const Instruction &block_begin)
  {
    const Instruction *next = nullptr;
    switch (instruction.type()) {
      case InstructionType::Call: {
        next = static_cast<const CallInstruction &>(instruction).next();
        break;
      }
      case InstructionType::Destruct: {
        next = static_cast<const DestructInstruction &>(instruction).next();
        break;
      }
      case InstructionType::Dummy: {
        next = static_cast<const DummyInstruction &>(instruction).next();
        break;
      }
      case InstructionType::Return:
      case InstructionType::Branch: {
        break;
      }
    }
    if (next == nullptr) {
      return nullptr;
    }
    if (next == &block_begin) {
      return nullptr;
    }
    if (this->has_to_be_block_begin(*next)) {
      return nullptr;
    }
    return next;
  }

  Vector<const Instruction *> get_instructions_in_block(const Instruction &representative)
  {
    Vector<const Instruction *> instructions;
    const Instruction &begin = this->get_first_instruction_in_block(representative);
    for (const Instruction *current = &begin; current != nullptr;
         current = this->get_next_instruction_in_block(*current, begin))
    {
      instructions.append(current);
    }
    return instructions;
  }

  void var_to_string(const Var *var, std::stringstream &ss)
  {
    if (var == nullptr) {
      ss << "null";
    }
    else {
      ss << "$" << var->index_in_proc();
      if (!var->name().is_empty()) {
        ss << "(" << var->name() << ")";
      }
    }
  }

  void instruction_name_format(StringRef name, std::stringstream &ss)
  {
    ss << name;
  }

  void instruction_to_string(const CallInstruction &instruction, std::stringstream &ss)
  {
    const MultiFn &fn = instruction.fn();
    this->instruction_name_format(fn.debug_name() + ": ", ss);
    for (const int param_index : fn.param_indices()) {
      const ParamType param_type = fn.param_type(param_index);
      const Var *var = instruction.params()[param_index];
      ss << R"(<font color="grey30">)";
      switch (param_type.interface_type()) {
        case ParamType::Input: {
          ss << "in";
          break;
        }
        case ParamType::Mutable: {
          ss << "mut";
          break;
        }
        case ParamType::Output: {
          ss << "out";
          break;
        }
      }
      ss << " </font> ";
      var_to_string(var, ss);
      if (param_index < fn.param_amount() - 1) {
        ss << ", ";
      }
    }
  }

  void instruction_to_string(const DestructInstruction &instruction, std::stringstream &ss)
  {
    instruction_name_format("Destruct ", ss);
    var_to_string(instruction.var(), ss);
  }

  void instruction_to_string(const DummyInstruction & /*instruction*/, std::stringstream &ss)
  {
    instruction_name_format("Dummy ", ss);
  }

  void instruction_to_string(const ReturnInstruction & /*instruction*/, std::stringstream &ss)
  {
    instruction_name_format("Return ", ss);

    Vector<ConstParam> outgoing_params;
    for (const ConstParam &param : proc_.params()) {
      if (ELEM(param.type, ParamType::Mutable, ParamType::Output)) {
        outgoing_params.append(param);
      }
    }
    for (const int param_index : outgoing_params.index_range()) {
      const ConstParam &param = outgoing_params[param_index];
      var_to_string(param.var, ss);
      if (param_index < outgoing_params.size() - 1) {
        ss << ", ";
      }
    }
  }

  void instruction_to_string(const BranchInstruction &instruction, std::stringstream &ss)
  {
    instruction_name_format("Branch ", ss);
    var_to_string(instruction.condition(), ss);
  }

  dot::Node &create_entry_node()
  {
    std::stringstream ss;
    ss << "Entry: ";
    Vector<ConstParam> incoming_params;
    for (const ConstParam &param : proc_.params()) {
      if (ELEM(param.type, ParamType::Input, ParamType::Mutable)) {
        incoming_params.append(param);
      }
    }
    for (const int param_index : incoming_params.index_range()) {
      const ConstParam &param = incoming_params[param_index];
      var_to_string(param.var, ss);
      if (param_index < incoming_params.size() - 1) {
        ss << ", ";
      }
    }

    dot::Node &node = digraph_.new_node(ss.str());
    node.set_shape(dot::Attr_shape::Ellipse);
    return node;
  }
};

std::string Proc::to_dot() const
{
  ProcDotExport dot_export{*this};
  return dot_export.generate();
}

}  // namespace dune::fn::multi_fn
