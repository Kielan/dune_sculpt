#pragma once

/* A Field represents a fn that outputs a val based on an arbitrary num of inputs. The
 * inputs for a specific field eval are provided by a FieldCx.
 *
 * A typical example is a field that computes a displacement vector for every ver on a mesh
 * based on its position.
 *
 * Fields; use as build, composed, and eval at run-time
 * Fields are stored in a directed tree
 * graph data structure, each node is a FieldNode and edges are deps.
 * A FieldNode has an arbitrary num of inputs
 * and at least one output and a Field refs a specific
 * output of a FieldNode. The inputs of a FieldNode are other fields.
 *
 * There are 2 diff types of field nodes:
 *  - FieldInput: Has no input and exactly 1 output. It represents an input to the entire field
 *    when it is eval. During eval the val of this input is based on a FieldCxt.
 *  - FieldOp: Has an arbitrary num of field inputs and at least one output. Its main
 *    use is to compose multiple existing fields into new fields.
 *
 * When fields are eval, they are converted into a multi-fn proc which allows
 * efficient computation. In the future we might support diff field eval mechanisms for
 * e.g. the following scenarios:
 *  - Latency of a single eval is more important than throughput.
 *  - Eval should happen on other hardware like GPUs.
 *
 * Whenever possible, multiple fields should be eval'd together to avoid dup work when
 * they share common sub-fields and a common cxt. */

#include "lib_fn_ref.hh"
#include "lib_generic_virtual_arr.hh"
#include "lib_string_ref.hh"
#include "lib_vector.hh"
#include "lib_vector_set.hh"

#include "fn_multi_builder.hh"

namespace dune::fn {

class FieldInput;
struct FieldInputs;

/* Have a fixed set of base node types bc all code that works w field nodes has to
 * understand those. */
enum class FieldNodeType {
  Input,
  Op,
  Constant,
};

/* A node in a field-tree. It has at least 1 output that can be refd by fields. */
class FieldNode {
 private:
  FieldNodeType node_type_;

 protected:
  /* Keeps track of the inputs that this node depends on. This avoids recomputing it every time the
   * data is required. It is a shared ptr, bc very often multiple nodes depend on the same
   * inputs.
   * Might contain null. */
  std::shared_ptr<const FieldInputs> field_inputs_;

 public:
  FieldNode(FieldNodeType node_type);
  virtual ~FieldNode();

  virtual const CPPType &output_cpp_type(int output_index) const = 0;

  FieldNodeType node_type() const;
  bool depends_on_input() const;

  const std::shared_ptr<const FieldInputs> &field_inputs() const;

  virtual uint64_t hash() const;
  virtual bool is_equal_to(const FieldNode &other) const;
};

/* Common base class for fields to avoid declaring the same methods for GField and GFieldRef */
template<typename NodePtr> class GFieldBase {
 protected:
  NodePtr node_ = nullptr;
  int node_output_index_ = 0;

  GFieldBase(NodePtr node, const int node_output_index)
      : node_(std::move(node)), node_output_index_(node_output_index)
  {
  }

 public:
  GFieldBase() = default;

  operator bool() const
  {
    return node_ != nullptr;
  }

  friend bool operator==(const GFieldBase &a, const GFieldBase &b)
  {
    /* 2 nodes can compare equal even when their ptr is not the same.
     * For example: 2 "Position" nodes are the same. */
    return *a.node_ == *b.node_ && a.node_output_index_ == b.node_output_index_;
  }

  uint64_t hash() const
  {
    return get_default_hash_2(*node_, node_output_index_);
  }

  const CPPType &cpp_type() const
  {
    return node_->output_cpp_type(node_output_index_);
  }

  const FieldNode &node() const
  {
    return *node_;
  }

  int node_output_index() const
  {
    return node_output_index_;
  }
};

/* A field whose output type is only known at run-time. */
class GField : public GFieldBase<std::shared_ptr<FieldNode>> {
 public:
  GField() = default;

  GField(std::shared_ptr<FieldNode> node, const int node_output_index = 0)
      : GFieldBase<std::shared_ptr<FieldNode>>(std::move(node), node_output_index)
  {
  }
};

/* Same as GField but is cheaper to copy/move around,
 * bc it does not contain a std::shared_ptr. */
class GFieldRef : public GFieldBase<const FieldNode *> {
 public:
  GFieldRef() = default;

  GFieldRef(const GField &field)
      : GFieldBase<const FieldNode *>(&field.node(), field.node_output_index())
  {
  }

  GFieldRef(const FieldNode &node, const int node_output_index = 0)
      : GFieldBase<const FieldNode *>(&node, node_output_index)
  {
  }
};

namespace detail {
/* Utility class to make #is_field_v work. */
struct TypedFieldBase {
};
}  // namespace detail

/* A typed version of GField. It has the same mem layout as GField. */
template<typename T> class Field : public GField, detail::TypedFieldBase {
 public:
  using base_type = T;

  Field() = default;

  Field(GField field) : GField(std::move(field))
  {
    lib_assert(this->cpp_type().template is<T>());
  }

  Field(std::shared_ptr<FieldNode> node, const int node_output_index = 0)
      : Field(GField(std::move(node), node_output_index))
  {
  }
};

/* True when T is any Field<...> type. */
template<typename T>
static constexpr bool is_field_v = std::is_base_of_v<detail::TypedFieldBase, T> &&
                                   !std::is_same_v<detail::TypedFieldBase, T>;

/* A FieldNode that allows composing existing fields into new fields. */
class FieldOp : public FieldNode {
  /* The multi-fn used by this node. It is optionally owned.
   * Multi-fns with mutable or vector params are not supported currently. */
  std::shared_ptr<const MultiFn> owned_fn_;
  const MultiFn *fn_;

  /* Inputs to the op. */
  dune::Vector<GField> inputs_;

 public:
  FieldOp(std::shared_ptr<const MultiFn> fn, Vector<GField> inputs = {});
  FieldOp(const MultiFn &fn, Vector<GField> inputs = {});
  ~FieldOp();

  Span<GField> inputs() const;
  const MultiFn &multi_fn() const;

  const CPPType &output_cpp_type(int output_index) const override;
};

class FieldCxt;

/* A FieldNode that represents an input to the entire field-tree. */
class FieldInput : public FieldNode {
 public:
  /* The order is also used for sorting in socket inspection. */
  enum class Category {
    NamedAttr = 0,
    Generated = 1,
    AnonymousAttribute = 2,
    Unknown,
  };

 protected:
  const CPPType *type_;
  std::string debug_name_;
  Category category_ = Category::Unknown;

 public:
  FieldInput(const CPPType &type, std::string debug_name = "");
  ~FieldInput();

  /* Get the val of this specific input based on the given cxt.
   * The returned virtual array,
   * should live at least as long as the passed in scope. May return null. */
  virtual GVArray get_varr_for_cx(const FieldCx &cx,
                                         IndexMask mask,
                                         ResourceScope &scope) const = 0;

  virtual std::string socket_inspection_name() const;
  dune::StringRef debug_name() const;
  const CPPType &cpp_type() const;
  Category category() const;

  const CPPType &output_cpp_type(int output_index) const override;
};

class FieldConstant : public FieldNode {
 private:
  const CPPType &type_;
  void *val_;

 public:
  FieldConstant(const CPPType &type, const void *val);
  ~FieldConstant();

  const CPPType &output_cpp_type(int output_index) const override;
  const CPPType &type() const;
  GPtr value() const;
};

/* Keeps track of the inputs of a field. */
struct FieldInputs {
  /* All FieldInput nodes that a field (possibly indirectly) depends on. */
  VectorSet<const FieldInput *> nodes;
  /* Same as above but the inputs are dedup'd. For example, when there are 2 separate index
   * input nodes, only 1 will show up in this list. */
  VectorSet<std::reference_wrapper<const FieldInput>> deduplicated_nodes;
};

/* Provides inputs for a specific field eval */
class FieldCx {
 public:
  virtual ~FieldCx() = default;

  virtual GVArray get_varray_for_input(const FieldInput &field_input,
                                       IndexMask mask,
                                       ResourceScope &scope) const;
};

/* Util class that makes it easier to eval fields. */
class FieldEvaluator : NonMovable, NonCopyable {
 private:
  struct OutputPtrInfo {
    void *dst = nullptr;
    /* When a destination virtual array is provided for an input, this is
     * unnecessary, otherwise this is used to construct the required virtual array. */
    void (*set)(void *dst, const GVArr &varr, ResourceScope &scope) = nullptr;
  };

  ResourceScope scope_;
  const FieldCx &cx_;
  const IndexMask mask_;
  Vector<GField> fields_to_eval_;
  Vector<GVMutableArr> dst_varrs_;
  Vector<GVArr> evaluated_varrs_;
  Vector<OutputPtrInfo> output_ptr_infos_;
  bool is_evaluated_ = false;

  Field<bool> selection_field_;
  IndexMask selection_mask_;

 public:
  /* Takes mask by ptr bc the mask has to live longer than the evaluator. */
  FieldEvaluator(const FieldCx &cx, const IndexMask *mask)
      : cx_(cx), mask_(*mask)
  {
  }

  /* Construct a field evaluator for all indices less than size. */
  FieldEvaluator(const FieldCx &cx, const int64_t size) : cx_(cx), mask_(size)
  {
  }

  ~FieldEvaluator()
  {
    /* This assert isn't strictly necessary,
     * could be replaced w a warning,
     * it will catch cases where someone forgets to call evaluate(). */
    lib_assert(is_evaluated_);
  }

  /* The selection field is evald 1st to determine which indices of the other fields should
   * be evaluated. Calling this method multiple times will replace the prev set
   * selection field. Only the elements selected by both this selection and the selection provided
   * in the constructor are calcd. If no selection field is set, it is assumed that all
   * indices passed to the constructor are selected. */
  void set_selection(Field<bool> selection)
  {
    selection_field_ = std::move(selection);
  }

  /* param field: Field to add to the evaluator.
   * param dst: Mutable virtual array that the evaluated result for this field is be written into */
  int add_w_destination(GField field, GVMutableArr dst);

  /* Same as add_w_destination but typed. */
  template<typename T> int add_w_destination(Field<T> field, VMutableArr<T> dst)
  {
    return this->add_w_destination(GField(std::move(field)), GVMutableArr(std::move(dst)));
  }

  /* param field: Field to add to the evaluator.
   * param dst: Mutable span that the evaluated result for this field is be written into.
   * note: When the output may only be used as a single val, the version of this function with
   * a virtual array result array should be used  */
  int add_w_destination(GField field, GMutableSpan dst);

  /* param field: Field to add to the evaluator.
   * param dst: Mutable span that the evaluated result for this field is be written into.
   * note: When the output may only be used as a single val, the v of this fn w
   * a virtual array result array should be used. */
  template<typename T> int add_w_destination(Field<T> field, MutableSpan<T> dst)
  {
    return this->add_w_destination(std::move(field), VMutableArr<T>::ForSpan(dst));
  }

  int add(GField field, GVArr *varr_ptr);

  /* param field: Field to add to the evaluator.
   * param varray_ptr: Once evaluate is called, the resulting virtual array will be will be
   * assigned to the given position.
   * return Index of the field in the evaluator which can be used in the get_evaluated methods */
  template<typename T> int add(Field<T> field, VArr<T> *varr_ptr)
  {
    const int field_index = fields_to_eval_.append_and_get_index(std::move(field));
    dst_varrs_.append({});
    output_ptr_infos_.append(OutputPtrInfo{
        varray_ptr, [](void *dst, const GVArr &varr, ResourceScope &UNUSED(scope)) {
          *(VArr<T> *)dst = varr.typed<T>();
        }});
    return field_index;
  }

  /* return Index of the field in the evaluator which can be used in the get_evaluated methods. */
  int add(GField field);

  /* Eval all fields on the evaluator. This can only be called once */
  void evaluate();

  const GVArray &get_evaluated(const int field_idx) const
  {
    lib_assert(is_evaluated_);
    return evaluated_varrs_[field_idx];
  }

  template<typename T> VArray<T> get_evaluated(const int field_idx)
  {
    return this->get_evaluated(field_idx).typed<T>();
  }

  IndexMask get_evaluated_selection_as_mask();

  /* Retrieve the output of an evaluated bool field and convert it to a mask, which can be used
   * to avoid calcs for unnecessary elements later on. The evaluator will own the indices in
   * some cases, so it must live at least as long as the returned mask. */
  IndexMask get_evaluated_as_mask(int field_idx);
};

/* Evaluate fields in the given cxt. If possible, multiple fields should be evaluated together,
 * bc that can be more efficient when they share common sub-fields.
 *
 * param scope: The resource scope that owns data that makes up the output virtual arrays. Make
 *   sure the scope is not destructed when the output virtual arrays are still used.
 * param fields_to_evaluate: The fields that should be evaluated together.
 * param mask: Determines which indices are computed. The mask may be referenced by the returned
 *   virtual arrays. So the underlying indices (if applicable) should live longer then #scope.
 * param cxt: The context that the field is evaluated in. Used to retrieve data from each
 *   FieldInput in the field network.
 * param dst_varrays: If provided, the computed data will be written into those virtual arrays
 *   instead of into newly created ones. That allows making the computed data live longer than
 *   #scope and is more efficient when the data will be written into those virtual arrays
 *   later anyway.
 * return The computed virtual arrays for each provided field. If dst_varrays is passed, the
 *   provided virtual arrays are returned. */
Vector<GVArr> evaluate_fields(ResourceScope &scope,
                                Span<GFieldRef> fields_to_eval,
                                IndexMask mask,
                                const FieldCx &cx,
                                Span<GVMutableArr> dst_varrs = {});

/* Util fns for simple field creation and evaluation */
void evaluate_constant_field(const GField &field, void *r_val);

template<typename T> T evaluate_constant_field(const Field<T> &field)
{
  T val;
  val.~T();
  evaluate_constant_field(field, &val);
  return val;
}

GField make_constant_field(const CPPType &type, const void *val);

template<typename T> Field<T> make_constant_field(T val)
{
  return make_constant_field(CPPType::get<T>(), &val);
}

/* If the field depends on some input, the same field is returned.
 * Else the field is evaluated and a new field is created that computes this const.
 *
 * Making the field constant has two benefits:
 * - The field-tree becomes a single node, its more efficient when the field is evaluated many
 *   times.
 * - Mem of the input fields may be freed. */
GField make_field_constant_if_possible(GField field);

class IndexFieldInput final : public FieldInput {
 public:
  IndexFieldInput();

  static GVArr get_index_varr(IndexMask mask);

  GVArray get_varr_for_cx(const FieldCx &cx,
                          IndexMask mask,
                          ResourceScope &scope) const final;

  uint64_t hash() const override;
  bool is_equal_to(const fn::FieldNode &other) const override;
};

/* Val or Field Class
 * Util class that wraps a single val and a field, to simplify accessing both of the types. */
template<typename T> struct ValOrField {
  /* Val that is used when the field is empty. */
  T val{};
  Field<T> field;

  ValOrField() = default;

  ValOrField(T value) : val(std::move(val))
  {
  }

  ValOrField(Field<T> field) : field(std::move(field))
  {
  }

  bool is_field() const
  {
    return (bool)this->field;
  }

  Field<T> as_field() const
  {
    if (this->field) {
      return this->field;
    }
    return make_const_field(this->val);
  }

  T as_val() const
  {
    if (this->field) {
      /* This returns a default val when the field is not const. */
      return evaluate_constant_field(this->field);
    }
    return this->value;
  }
};

/* FieldNode Inline Methods */

inline FieldNode::FieldNode(const FieldNodeType node_type) : node_type_(node_type)
{
}

inline FieldNodeType FieldNode::node_type() const
{
  return node_type_;
}

inline bool FieldNode::depends_on_input() const
{
  return field_inputs_ && !field_inputs_->nodes.is_empty();
}

inline const std::shared_ptr<const FieldInputs> &FieldNode::field_inputs() const
{
  return field_inputs_;
}

inline uint64_t FieldNode::hash() const
{
  return get_default_hash(this);
}

inline bool FieldNode::is_equal_to(const FieldNode &other) const
{
  return this == &other;
}

inline bool operator==(const FieldNode &a, const FieldNode &b)
{
  return a.is_equal_to(b);
}

inline bool operator!=(const FieldNode &a, const FieldNode &b)
{
  return !(a == b);
}

/* FieldOp Inline Methods */

inline Span<GField> FieldOp::inputs() const
{
  return inputs_;
}

inline const MultiFn &FieldOp::multi_fn() const
{
  return *fn_;
}

inline const CPPType &FieldOp::output_cpp_type(int output_idx) const
{
  int output_counter = 0;
  for (const int param_idx : fn_->param_indices()) {
    MFParamType param_type = fn_->param_type(param_idx);
    if (param_type.is_output()) {
      if (output_counter == output_idx) {
        return param_type.data_type().single_type();
      }
      output_counter++;
    }
  }
  lib_assert_unreachable();
  return CPPType::get<float>();
}


/* FieldInput Inline Methods */
inline std::string FieldInput::socket_inspection_name() const
{
  return debug_name_;
}

inline StringRef FieldInput::debug_name() const
{
  return debug_name_;
}

inline const CPPType &FieldInput::cpp_type() const
{
  return *type_;
}

inline FieldInput::Category FieldInput::category() const
{
  return category_;
}

inline const CPPType &FieldInput::output_cpp_type(int output_idx) const
{
  lib_assert(output_idx == 0);
  UNUSED_VARS_NDEBUG(output_idx);
  return *type_;
}

}  // namespace dune::fn
