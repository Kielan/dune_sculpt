#include "lib_arr_utils.hh"
#include "lib_map.hh"
#include "lib_multi_val_map.hh"
#include "lib_set.hh"
#include "lib_stack.hh"
#include "lib_vector_set.hh"

#include "fn_field.hh"
#include "fn_multi_fn_builder.hh"
#include "fn_multi_proc.hh"
#include "fn_multi_proc_builder.hh"
#include "fn_multi_proc_executor.hh"
#include "fn_multi_proc_optimization.hh"

namespace dune::fn {

/* Field Eval */
struct FieldTreeInfo {
  /* When fields are built, they only have refs to the fields that they depend on. This map
   * allows traversal of fields in the opposite direction. So for every field it stores the other
   * fields that depend on it directly */
  MultiValMap<GFieldRef, GFieldRef> field_users;
  /* The same field input may exist in the field tree as separate nodes due to the way
   * the tree is constructed. This set contains every different input only once. */
  VectorSet<std::ref_wrapper<const FieldInput>> dedupd_field_inputs;
};

/* Collects some info from the field tree that is required by later steps. */
static FieldTreeInfo preproc_field_tree(Span<GFieldRef> entry_fields)
{
  FieldTreeInfo field_tree_info;

  Stack<GFieldRef> fields_to_check;
  Set<GFieldRef> handled_fields;

  for (GFieldRef field : entry_fields) {
    if (handled_fields.add(field)) {
      fields_to_check.push(field);
    }
  }

  while (!fields_to_check.is_empty()) {
    GFieldRef field = fields_to_check.pop();
    const FieldNode &field_node = field.node();
    switch (field_node.node_type()) {
      case FieldNodeType::Input: {
        const FieldInput &field_input = static_cast<const FieldInput &>(field_node);
        field_tree_info.dedupd_field_inputs.add(field_input);
        break;
      }
      case FieldNodeType::Op: {
        const FieldOp &op = static_cast<const FieldOp &>(field_node);
        for (const GFieldRef op_input : op.inputs()) {
          field_tree_info.field_users.add(op_input, field);
          if (handled_fields.add(op_input)) {
            fields_to_check.push(op_input);
          }
        }
        break;
      }
      case FieldNodeType::Constant: {
        /* Nothing to do. */
        break;
      }
    }
  }
  return field_tree_info;
}

/* Retrieves the data from the cx that is passed as input into the field */
static Vector<GVArr> get_field_cx_inputs(
    ResourceScope &scope,
    const IndexMask &mask,
    const FieldCx &cx,
    const Span<std::ref_wrapper<const FieldInput>> field_inputs)
{
  Vector<GVArr> field_cx_inputs;
  for (const FieldInput &field_input : field_inputs) {
    GVArr varr = cx.get_varr_for_input(field_input, mask, scope);
    if (!varr) {
      const CPPType &type = field_input.cpp_type();
      varr = GVArr::ForSingleDefault(type, mask.min_arr_size());
    }
    field_cx_inputs.append(varr);
  }
  return field_cx_inputs;
}

/* return A set that contains all fields from the field tree that depend on an input that varies
 * for diff indices. */
static Set<GFieldRef> find_varying_fields(const FieldTreeInfo &field_tree_info,
                                          Span<GVArr> field_cx_inputs)
{
  Set<GFieldRef> found_fields;
  Stack<GFieldRef> fields_to_check;

  /* The varying fields are the ones that depend on inputs that are not constant.
   * Start the tree search at the non-constant input fields and traverse through all fields that
   * depend on them. */
  for (const int i : field_cx_inputs.idx_range()) {
    const GVArr &varr = field_cx_inputs[i];
    if (varr.is_single()) {
      continue;
    }
    const FieldInput &field_input = field_tree_info.deduplicated_field_inputs[i];
    const GFieldRef field_input_field{field_input, 0};
    const Span<GFieldRef> users = field_tree_info.field_users.lookup(field_input_field);
    for (const GFieldRef &field : users) {
      if (found_fields.add(field)) {
        fields_to_check.push(field);
      }
    }
  }
  while (!fields_to_check.is_empty()) {
    GFieldRef field = fields_to_check.pop();
    const Span<GFieldRef> users = field_tree_info.field_users.lookup(field);
    for (GFieldRef field : users) {
      if (found_fields.add(field)) {
        fields_to_check.push(field);
      }
    }
  }
  return found_fields;
}

/* Builds the proc so that it computes the fields. */
static void build_multi_fn_proc_for_fields(mf::Proc &proc,
                                           ResourceScope &scope,
                                           const FieldTreeInfo &field_tree_info,
                                           Span<GFieldRef> output_fields)
{
  mf::ProcBuilder builder{proc};
  /* Every input, intermediate and output field corresponds to a var in the proc */
  Map<GFieldRef, mf::Var *> var_by_field;

  /* Start by adding the field inputs as params to the proc. */
  for (const FieldInput &field_input : field_tree_info.deduplicated_field_inputs) {
    mf::Var &var = builder.add_input_param(
        mf::DataType::ForSingle(field_input.cpp_type()), field_input.debug_name());
    var_by_field.add_new({field_input, 0}, &var);
  }

  /* Util struct used to do proper depth 1st search traversal of the tree below. */
  struct FieldWithIndex {
    GFieldRef field;
    int current_input_index = 0;
  };

  for (GFieldRef field : output_fields) {
    /* Start a new stack for each output field to make sure that a field pushed later to the
     * stack does never depend on a field that was pushed before. */
    Stack<FieldWIdx> fields_to_check;
    fields_to_check.push({field, 0});
    while (!fields_to_check.is_empty()) {
      FieldWIdx &field_w_idx = fields_to_check.peek();
      const GFieldRef &field = field_w_idx.field;
      if (var_by_field.contains(field)) {
        /* The field has been handled already. */
        fields_to_check.pop();
        continue;
      }
      const FieldNode &field_node = field.node();
      switch (field_node.node_type()) {
        case FieldNodeType::Input: {
          /* Field inputs should alrdy be handled above. */
          break;
        }
        case FieldNodeType::Op: {
          const FieldOp &op_node = static_cast<const FieldO &>(field.node());
          const Span<GField> op_inputs = op_node.inputs();

          if (field_w_idx.curr_input_idx < op_inputs.size()) {
            /* Not all inputs are handled yet. Push the next input field to the stack and increment
             * the input index. */
            fields_to_check.push({op_inputs[field_w_idx.current_input_idx]});
            field_w_idx.curr_input_idx++;
          }
          else {
            /* All inputs vars are rdy, now gather all vars used by the
             * fn and call it. */
            const mf::MultiFn &multi_fn = op_node.multi_fn();
            Vector<mf::Var *> vars(multi_fn.param_amount());

            int param_input_idx = 0;
            int param_output_idx = 0;
            for (const int param_idx : multi_fn.param_indices()) {
              const mf::ParamType param_type = multi_fn.param_type(param_idx);
              const mf::ParamType::InterfaceType interface_type = param_type.interface_type();
              if (interface_type == mf::ParamType::Input) {
                const GField &input_field = op_inputs[param_input_idx];
                vars[param_idx] = var_by_field.lookup(input_field);
                param_input_idx++;
              }
              else if (interface_type == mf::ParamType::Output) {
                const GFieldRef output_field{op_node, param_output_idx};
                const bool output_is_ignored =
                    field_tree_info.field_users.lookup(output_field).is_empty() &&
                    !output_fields.contains(output_field);
                if (output_is_ignored) {
                  /* Ignored outputs don't need a variable. */
                  vars[param_index] = nullptr;
                }
                else {
                  /* Create a new var for used outputs. */
                  mf::Var &new_var = proc.new_var(param_type.data_type());
                  vars[param_idx] = &new_var;
                  var_by_field.add_new(output_field, &new_var);
                }
                param_output_idx++;
              }
              else {
                lib_assert_unreachable();
              }
            }
            builder.add_call_w_all_vars(multi_fn, vars);
          }
          break;
        }
        case FieldNodeType::Constant: {
          const FieldConstant &constant_node = static_cast<const FieldConstant &>(field_node);
          const mf::MultiFn &fn = proc.construct_fn<mf::CustomMF_GenericConstant>(
              constant_node.type(), constant_node.val().get(), false);
          mf::Var &new_var = *builder.add_call<1>(fn)[0];
          var_by_field.add_new(field, &new_var);
          break;
        }
      }
    }
  }

  /* Add output params to the proc. */
  Set<mf::Var *> already_output_vars;
  for (const GFieldRef &field : output_fields) {
    mf::Var *var = var_by_field.lookup(field);
    if (!already_output_vars.add(var)) {
      /* 1 var can be output at most once.
       * To output same val 2x must make a copy first. */
      const mf::MultiFn &copy_fn = scope.construct<mf::CustomMF_GenericCopy>(
          var->data_type());
      var = builder.add_call<1>(copy_fn, {var})[0];
    }
    builder.add_output_param(*var);
  }

  /* Remove the vars that should not be destructed from the map. */
  for (const GFieldRef &field : output_fields) {
    var_by_field.remove(field);
  }
  /* Add destructor calls for the remaining vars. */
  for (mf::Var *var : var_by_field.vals()) {
    builder.add_destruct(*var);
  }

  mf::ReturnInstruct &return_instruct = builder.add_return();

  mf::proc_optimization::move_destructs_up(proc, return_instr);

  // std::cout << proc.to_dot() << "\n";
  lib_assert(proc.validate());
}

Vector<GVArr> eval_fields(ResourceScope &scope,
                          Span<GFieldRef> fields_to_eval,
                          const IdxMask &mask,
                          const FieldCx &cx,
                          Span<GVMutableArr> dst_varrs)
{
  Vector<GVArr> r_varrs(fields_to_eval.size());
  Array<bool> is_output_written_to_dst(fields_to_eval.size(), false);
  const int arr_size = mask.min_arr_size();

  if (mask.is_empty()) {
    for (const int i : fields_to_eval.idx_range()) {
      const CPPType &type = fields_to_eval[i].cpp_type();
      r_varrs[i] = GVArr::ForEmpty(type);
    }
    return r_varrs;
  }

  /* Destination arrays are optional. Create a small util method to access them. */
  auto get_dst_varr = [&](int idx) -> GVMutableArr {
    if (dst_varrs.is_empty()) {
      return {};
    }
    const GVMutableArr &varr = dst_varrs[index];
    if (!varr) {
      return {};
    }
    lib_assert(varr.size() >= arr_size);
    return varr;
  };

  /* Traverse the field tree and prepare some data that is used in later steps. */
  FieldTreeInfo field_tree_info = preproc_field_tree(fields_to_eval);

  /* Get inputs that will be passed into the field when evald. */
  Vector<GVArr> field_cx_inputs = get_field_cx_inputs(
      scope, mask, cx, field_tree_info.deduplicated_field_inputs);

  /* Finish fields that don't need any proc'ing directly. */
  for (const int out_idx : fields_to_eval.idx_range()) {
    const GFieldRef &field = fields_to_eval[out_idx];
    const FieldNode &field_node = field.node();
    switch (field_node.node_type()) {
      case FieldNodeType::Input: {
        const FieldInput &field_input = static_cast<const FieldInput &>(field.node());
        const int field_input_index = field_tree_info.deduplicated_field_inputs.idx_of(
            field_input);
        const GVArr &varr = field_cx_inputs[field_input_idx];
        r_varrs[out_idx] = varr;
        break;
      }
      case FieldNodeType::Constant: {
        const FieldConstant &field_constant = static_cast<const FieldConstant &>(field.node());
        r_varrs[out_idx] = GVArr::ForSingleRef(
            field_constant.type(), mask.min_arr_size(), field_constant.val().get());
        break;
      }
      case FieldNodeType::Operation: {
        break;
      }
    }
  }

  Set<GFieldRef> varying_fields = find_varying_fields(field_tree_info, field_cx_inputs);

  /* Separate fields into 2 categories. Those that are constant and need to be evald only
   * once, and those that need to be evald for every index. */
  Vector<GFieldRef> varying_fields_to_eval;
  Vector<int> varying_field_indices;
  Vector<GFieldRef> constant_fields_to_eval;
  Vector<int> constant_field_indices;
  for (const int i : fields_to_eval.idx_range()) {
    if (r_varrs[i]) {
      /* Already done. */
      continue;
    }
    GFieldRef field = fields_to_eval[i];
    if (varying_fields.contains(field)) {
      varying_fields_to_eval.append(field);
      varying_field_indices.append(i);
    }
    else {
      constant_fields_to_eval.append(field);
      constant_field_indices.append(i);
    }
  }

  /* Eval varying fields if necessary. */
  if (!varying_fields_to_eval.is_empty()) {
    /* Build the proc for those fields. */
    mf::Proc proc;
    build_multi_fn_proc_for_fields(
        proc, scope, field_tree_info, varying_fields_to_evaluate);
    mf::ProcExecutor proc_executor{procedure};

    mf::ParamsBuilder mf_params{proc_executor, &mask};
    mf::CxtBuilder mf_cxt;

    /* Provide inputs to the proc exec. */
    for (const GVArray &varray : field_context_inputs) {
      mf_params.add_readonly_single_input(varray);
    }

    for (const int i : varying_fields_to_evaluate.index_range()) {
      const GFieldRef &field = varying_fields_to_evaluate[i];
      const CPPType &type = field.cpp_type();
      const int out_index = varying_field_indices[i];

      /* Try to get an existing virtual array that the result should be written into. */
      GVMutableArray dst_varray = get_dst_varray(out_index);
      void *buf;
      if (!dst_varray || !dst_varray.is_span()) {
        /* Allocate a new buf for the computed result. */
        buffer = scope.linear_allocator().alloc(type.size() * array_size, type.alignment());

        if (!type.is_trivially_destructible()) {
          /* Destruct vals in the end. */
          scope.add_destruct_call(
              [buf, mask, &type]() { type.destruct_indices(buffer, mask); });
        }

        r_varrays[out_index] = GVArray::ForSpan({type, buffer, array_size});
      }
      else {
        /* Write the result into the existing span. */
        buffer = dst_varray.get_internal_span().data();

        r_varrays[out_index] = dst_varray;
        is_output_written_to_dst[out_index] = true;
      }

      /* Pass output buf to the proc ex. */
      const GMutableSpan span{type, buf, array_size};
      mf_params.add_uninitialized_single_output(span);
    }

    proc_executor.call_auto(mask, mf_params, mf_cxt);
  }

  /* Eval constant fields if necessary. */
  if (!constant_fields_to_eval.is_empty()) {
    /* Build the proc for those fields. */
    mf::Proc proc;
    build_multi_fn_proc_for_fields(
        proc, scope, field_tree_info, constant_fields_to_eval);
    mf::ProcExecutor proc_executor{proc};
    const IndexMask mask(1);
    mf::ParamsBuilder mf_params{proc_executor, &mask};
    mf::CxtBuilder mf_cxt;

    /* Provide inputs to the proc executor. */
    for (const GVArray &varray : field_cxt_inputs) {
      mf_params.add_readonly_single_input(varray);
    }

    for (const int i : constant_fields_to_eval.index_range()) {
      const GFieldRef &field = constant_fields_to_eval[i];
      const CPPType &type = field.cpp_type();
      /* Alloc mem where the computed val will be stored in. */
      void *buf = scope.linear_allocator().allocate(type.size(), type.alignment());

      if (!type.is_trivially_destructible()) {
        /* Destruct val in the end. */
        scope.add_destruct_call([buffer, &type]() { type.destruct(buf); });
      }

      /* Pass output buf to the proc executor. */
      mf_params.add_uninitialized_single_output({type, buf, 1});

      /* Create virtual array that can be used after the proc has been ex below. */
      const int out_index = constant_field_indices[i];
      r_varrays[out_index] = GVArray::ForSingleRef(type, array_size, buffer);
    }

    proc_executor.call(mask, mf_params, mf_cxt);
  }

  /* Copy data to supplied destination arrays if necessary. In some cases the evaluation above
   * has written the computed data in the right place already. */
  if (!dst_varrs.is_empty()) {
    for (const int out_idx : fields_to_eval.idx_range()) {
      GVMutableArr dst_varr = get_dst_varr(out_index);
      if (!dst_varr) {
        /* Caller did not provide a destination for this output. */
        continue;
      }
      const GVArr &computed_varr = r_varrs[out_idx];
      lib_assert(computed_varr.type() == dst_varr.type());
      if (is_output_written_to_dst[out_index]) {
        /* The result has been written into the destination provided by the caller already. */
        continue;
      }
      /* Still have to copy over the data in the destination provided by the caller. */
      if (dst_varr.is_span()) {
        arr_utils::copy(computed_varr,
                          mask,
                          dst_varr.get_internal_span().take_front(mask.min_arr_size()));
      }
      else {
        /* Slower materialize into a diff structure. */
        const CPPType &type = computed_varr.type();
        threading::parallel_for(mask.idx_range(), 2048, [&](const IdxRange range) {
          BUF_FOR_CPP_TYPE_VAL(type, buf);
          mask.slice(range).foreach_segment([&](auto segment) {
            for (const int i : segment) {
              computed_varr.get_to_uninitialized(i, buffer);
              dst_varr.set_by_relocate(i, buffer);
            }
          });
        });
      }
      r_varrays[out_idx] = dst_varr;
    }
  }
  return r_varrays;
}

void eval_constant_field(const GField &field, void *r_val)
{
  if (field.node().depends_on_input()) {
    const CPPType &type = field.cpp_type();
    type.val_init(r_val);
    return;
  }

  ResourceScope scope;
  FieldCxt cxt;
  Vector<GVArray> varrays = eval_fields(scope, {field}, IndexRange(1), context);
  varrays[0].get_to_uninitialized(0, r_val);
}

GField make_field_constant_if_possible(GField field)
{
  if (field.node().depends_on_input()) {
    return field;
  }
  const CPPType &type = field.cpp_type();
  BUF_FOR_CPP_TYPE_VAL(type, buf);
  eval_constant_field(field, buf);
  GField new_field = make_constant_field(type, buffer);
  type.destruct(buf);
  return new_field;
}

Field<bool> invert_bool_field(const Field<bool> &field)
{
  static auto not_fn = mf::build::SI1_SO<bool, bool>(
      "Not", [](bool a) { return !a; }, mf::build::ex_presets::AllSpanOrSingle());
  auto not_op = FieldOp::Create(not_fn, {field});
  return Field<bool>(not_op);
}

GField make_constant_field(const CPPType &type, const void *val)
{
  auto constant_node = std::make_shared<FieldConstant>(type, val);
  return GField{std::move(constant_node)};
}

GVArray FieldCxt::get_varray_for_input(const FieldInput &field_input,
                                       const IndexMask &mask,
                                       ResourceScope &scope) const
{
  /* By default ask the field input to create the varray. Another field cxt might overwrite
   * the cxt here. */
  return field_input.get_varray_for_cxt(*this, mask, scope);
}

IndexFieldInput::IndexFieldInput() : FieldInput(CPPType::get<int>(), "Index")
{
  category_ = Category::Generated;
}

GVArray IndexFieldInput::get_index_varray(const IndexMask &mask)
{
  auto index_fn = [](int i) { return i; };
  return VArray<int>::ForFn(mask.min_array_size(), index_fn);
}

GVArray IndexFieldInput::get_varray_for_cxt(const fn::FieldCxt & /*cxt*/,
                                            const IndexMask &mask,
                                            ResourceScope & /*scope*/) const
{
  /* TODO: Investigate a similar method to IndexRange::as_span() */
  return get_index_varray(mask);
}

uint64_t IndexFieldInput::hash() const
{
  /* Some random constant hash. */
  return 128736487678;
}

bool IndexFieldInput::is_equal_to(const fn::FieldNode &other) const
{
  return dynamic_cast<const IndexFieldInput *>(&other) != nullptr;
}

/* FieldNode */
/* Avoid generating the destructor in every translation unit. */
FieldNode::~FieldNode() = default;

void FieldNode::for_each_field_input_recursive(FnRef<void(const FieldInput &)> fn) const
{
  if (field_inputs_) {
    for (const FieldInput &field_input : field_inputs_->deduplicated_nodes) {
      fn(field_input);
      if (&field_input != this) {
        field_input.for_each_field_input_recursive(fn);
      }
    }
  }
}

/* FieldOp */
FieldOp::FieldOp(std::shared_ptr<const mf::MultiFn> fn,
                 Vector<GField> inputs)
    : FieldOp(*fn, std::move(inputs))
{
  owned_function_ = std::move(fn);
}

/* Avoid generating the destructor in every translation unit. */
FieldOp::~FieldOp() = default;

/* Return the field inputs used by all the provided fields.
 * This tries to reuse an existing FieldInputs whenever possible to avoid copying it. */
static std::shared_ptr<const FieldInputs> combine_field_inputs(Span<GField> fields)
{
  /* The FieldInputs that we try to reuse if possible. */
  const std::shared_ptr<const FieldInputs> *field_inputs_candidate = nullptr;
  for (const GField &field : fields) {
    const std::shared_ptr<const FieldInputs> &field_inputs = field.node().field_inputs();
    /* Only try to reuse non-empty FieldInputs. */
    if (field_inputs && !field_inputs->nodes.is_empty()) {
      if (field_inputs_candidate == nullptr) {
        field_inputs_candidate = &field_inputs;
      }
      else if ((*field_inputs_candidate)->nodes.size() < field_inputs->nodes.size()) {
        /* Always try to reuse the FieldInputs that has the most nodes alrdy. */
        field_inputs_candidate = &field_inputs;
      }
    }
  }
  if (field_inputs_candidate == nullptr) {
    /* None of the field depends on an input. */
    return {};
  }
  /* Check if all inputs are in the candidate. */
  Vector<const FieldInput *> inputs_not_in_candidate;
  for (const GField &field : fields) {
    const std::shared_ptr<const FieldInputs> &field_inputs = field.node().field_inputs();
    if (!field_inputs) {
      continue;
    }
    if (&field_inputs == field_inputs_candidate) {
      continue;
    }
    for (const FieldInput *field_input : field_inputs->nodes) {
      if (!(*field_inputs_candidate)->nodes.contains(field_input)) {
        inputs_not_in_candidate.append(field_input);
      }
    }
  }
  if (inputs_not_in_candidate.is_empty()) {
    /* The existing FieldInputs can be reused, bc no other field has additional inputs. */
    return *field_inputs_candidate;
  }
  /* Create new FieldInputs that contains all of the inputs that the fields depend on. */
  std::shared_ptr<FieldInputs> new_field_inputs = std::make_shared<FieldInputs>(
      **field_inputs_candidate);
  for (const FieldInput *field_input : inputs_not_in_candidate) {
    new_field_inputs->nodes.add(field_input);
    new_field_inputs->deduplicated_nodes.add(*field_input);
  }
  return new_field_inputs;
}

FieldOp::FieldOp(const mf::MultiFn &fn, Vector<GField> inputs)
    : FieldNode(FieldNodeType::Op), fn_(&fn), inputs_(std::move(inputs))
{
  field_inputs_ = combine_field_inputs(inputs_);
}

/* FieldInput */
FieldInput::FieldInput(const CPPType &type, std::string debug_name)
    : FieldNode(FieldNodeType::Input), type_(&type), debug_name_(std::move(debug_name))
{
  std::shared_ptr<FieldInputs> field_inputs = std::make_shared<FieldInputs>();
  field_inputs->nodes.add_new(this);
  field_inputs->deduplicated_nodes.add_new(*this);
  field_inputs_ = std::move(field_inputs);
}

/* Avoid generating the destructor in every translation unit. */
FieldInput::~FieldInput() = default;

/* FieldConst */
FieldConst::FieldConst(const CPPType &type, const void *val)
    : FieldNode(FieldNodeType::Const), type_(type)
{
  value_ = mem_malloc_aligned(type.size(), type.alignment(), __func__);
  type.copy_construct(val, val_);
}

FieldConstant::~FieldConstant()
{
  type_.destruct(val_);
  mem_free(val_);
}

const CPPType &FieldConstant::output_cpp_type(int output_index) const
{
  lib_assert(output_index == 0);
  UNUSED_VARS_NDEBUG(output_index);
  return type_;
}

const CPPType &FieldConstant::type() const
{
  return type_;
}

GPtr FieldConstant::val() const
{
  return {type_, val_};
}

/* FieldEval */
static IndexMask index_mask_from_selection(const IndexMask full_mask,
                                           const VArray<bool> &sel,
                                           ResourceScope &scope)
{
  return IndexMask::from_bools(full_mask, sel, scope.construct<IndexMaskMem>());
}

int FieldEvaluator::add_with_destination(GField field, GVMutableArray dst)
{
  const int field_index = fields_to_eval_.append_and_get_index(std::move(field));
  dst_varrays_.append(dst);
  output_ptr_infos_.append({});
  return field_index;
}

int FieldEvaluator::add_with_destination(GField field, GMutableSpan dst)
{
  return this->add_with_destination(std::move(field), GVMutableArray::ForSpan(dst));
}

int FieldEvaluator::add(GField field, GVArray *varray_ptr)
{
  const int field_index = fields_to_evaluate_.append_and_get_index(std::move(field));
  dst_varrays_.append(nullptr);
  output_ptr_infos_.append(OutputPtrInfo{
      varray_ptr, [](void *dst, const GVArray &varray, ResourceScope & /*scope*/) {
        *static_cast<GVArray *>(dst) = varray;
      }});
  return field_index;
}

int FieldEvaluator::add(GField field)
{
  const int field_index = fields_to_evaluate_.append_and_get_index(std::move(field));
  dst_varrays_.append(nullptr);
  output_ptr_infos_.append({});
  return field_index;
}

static IndexMask eval_selection(const Field<bool> &selection_field,
                                    const FieldContext &context,
                                    IndexMask full_mask,
                                    ResourceScope &scope)
{
  if (sel_field) {
    VArray<bool> sel =
        eval_fields(scope, {sel_field}, full_mask, context)[0].typed<bool>();
    return index_mask_from_sel(full_mask, selection, scope);
  }
  return full_mask;
}

void FieldEval::eval()
{
  lib_assert_msg(!is_eval_, "Cannot eval fields twice.");

  sel_mask_ = eval_sel(sel_field_, cxt_, mask_, scope_);

  Array<GFieldRef> fields(fields_to_eval_.size());
  for (const int i : fields_to_eval_.index_range()) {
    fields[i] = fields_to_eval_[i];
  }
  eval_varrays_ = eval_fields(scope_, fields, sel_mask_, cxt_, dst_varrays_);
  lib_assert(fields_to_evaluate_.size() == evaluated_varrays_.size());
  for (const int i : fields_to_eval_.index_range()) {
    OutputPointerInfo &info = output_ptr_infos_[i];
    if (info.dst != nullptr) {
      info.set(info.dst, evaluated_varrays_[i], scope_);
    }
  }
  is_eval_ = true;
}

IndexMask FieldEval::get_eval_as_mask(const int field_index)
{
  VArray<bool> varray = this->get_eval(field_index).typed<bool>();

  if (varray.is_single()) {
    if (varray.get_internal_single()) {
      return IndexRange(varray.size());
    }
    return IndexRange(0);
  }
  return index_mask_from_selection(mask_, varray, scope_);
}

IndexMask FieldEvaluator::get_evaluated_selection_as_mask()
{
  BLI_assert(is_evaluated_);
  return selection_mask_;
}

/** \} */

}  // namespace blender::fn
