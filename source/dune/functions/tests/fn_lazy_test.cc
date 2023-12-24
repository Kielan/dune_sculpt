#include "testing/testing.h"

#include "fn_lazy_ex.hh"
#include "fn_lazy_graph.hh"
#include "fn_lazy_graph_executor.hh"

#include "lib_task.h"
#include "lib_timeit.hh"

namespace dune::fn::lazy_fn::tests {

class AddLazyFn : public LazyFn {
 public:
  AddLazyFn()
  {
    debug_name_ = "Add";
    inputs_.append({"A", CPPType::get<int>()});
    inputs_.append({"B", CPPType::get<int>()});
    outputs_.append({"Result", CPPType::get<int>()});
  }

  void ex_impl(Params &params, const Cxt & /*cxt*/) const override
  {
    const int a = params.get_input<int>(0);
    const int b = params.get_input<int>(1);
    params.set_output(0, a + b);
  }
};

class StoreValFn : public LazyFn {
 private:
  int *dst1_;
  int *dst2_;

 public:
  StoreValFn(int *dst1, int *dst2) : dst1_(dst1), dst2_(dst2)
  {
    debug_name_ = "Store Val";
    inputs_.append({"A", CPPType::get<int>()});
    inputs_.append({"B", CPPType::get<int>(), ValUsage::Maybe});
  }

  void ex_impl(Params &params, const Cxt & /*cxt*/) const override
  {
    *dst1_ = params.get_input<int>(0);
    if (int *val = params.try_get_input_data_ptr_or_request<int>(1)) {
      *dst2_ = *val;
    }
  }
};

class SimpleSideEffectProvider : public GraphEx::SideEffectProvider {
 private:
  Vector<const FnNode *> side_effect_nodes_;

 public:
  SimpleSideEffectProvider(Span<const FnNode *> side_effect_nodes)
      : side_effect_nodes_(side_effect_nodes)
  {
  }

  Vector<const FnNode *> get_nodes_with_side_effects(
      const Cxt & /*cxt*/) const override
  {
    return side_effect_nodes_;
  }
};

TEST(lazy_fn, SimpleAdd)
{
  const AddLazyFn add_fn;
  int result = 0;
  ex_lazy_fn_eagerly(
      add_fn, nullptr, nullptr, std::make_tuple(30, 5), std::make_tuple(&result));
  EXPECT_EQ(result, 35);
}

TEST(lazy_fn, SideEffects)
{
  lib_task_scheduler_init();
  int dst1 = 0;
  int dst2 = 0;

  const AddLazyFn add_fn;
  const StoreValFn store_fn{&dst1, &dst2};

  Graph graph;
  FnNode &add_node_1 = graph.add_fn(add_fn);
  FnNode &add_node_2 = graph.add_fn(add_fn);
  FnNode &store_node = graph.add_fn(store_fn);
  GraphInputSocket &graph_input = graph.add_input(CPPType::get<int>());

  graph.add_link(graph_input, add_node_1.input(0));
  graph.add_link(graph_input, add_node_2.input(0));
  graph.add_link(add_node_1.output(0), store_node.input(0));
  graph.add_link(add_node_2.output(0), store_node.input(1));

  const int val_10 = 10;
  const int val_100 = 100;
  add_node_1.input(1).set_default_value(&value_10);
  add_node_2.input(1).set_default_value(&value_100);

  graph.update_node_indices();

  SimpleSideEffectProvider side_effect_provider{{&store_node}};

  GraphExecutor executor_fn{graph, {&graph_input}, {}, nullptr, &side_effect_provider, nullptr};
  ex_lazy_fn_eagerly(
      executor_fn, nullptr, nullptr, std::make_tuple(5), std::make_tuple());

  EXPECT_EQ(dst1, 15);
  EXPECT_EQ(dst2, 105);
}

class PartialEvalTestFn : public LazyFn {
 public:
  PartialEvalTestFn()
  {
    debug_name_ = "Partial Eval";
    allow_missing_requested_inputs_ = true;

    inputs_.append_as("A", CPPType::get<int>(), ValUsage::Used);
    inputs_.append_as("B", CPPType::get<int>(), ValUsage::Used);

    outputs_.append_as("A*2", CPPType::get<int>());
    outputs_.append_as("B*5", CPPType::get<int>());
  }

  void ex_impl(Params &params, const Cxt & /*cxt*/) const override
  {
    if (!params.output_was_set(0)) {
      if (int *a = params.try_get_input_data_ptr<int>(0)) {
        params.set_output(0, *a * 2);
      }
    }
    if (!params.output_was_set(1)) {
      if (int *b = params.try_get_input_data_ptr<int>(1)) {
        params.set_output(1, *b * 5);
      }
    }
  }

  void possible_output_dependencies(const int output_index,
                                    FnRef<void(Span<int>)> fn) const override
  {
    /* Each output only depends on the input w the same index. */
    const int input_index = output_index;
    fn({input_index});
  }
};

TEST(lazy_fn, GraphWithCycle)
{
  const PartialEvalTestFn fn;

  Graph graph;
  FnNode &fn_node = graph.add_fn(fn);

  GraphInputSocket &input_socket = graph.add_input(CPPType::get<int>());
  GraphOutputSocket &output_socket = graph.add_output(CPPType::get<int>());

  graph.add_link(input_socket, fn_node.input(0));
  /* Create a cycle in graph. Should still be possible to eval it,
   * bc theres no actual data dep in the cycle. */
  graph.add_link(fn_node.output(0), fn_node.input(1));
  graph.add_link(fn_node.output(1), output_socket);

  graph.update_node_indices();

  GraphExecutor executor_fn{graph, {&input_socket}, {&output_socket}, nullptr, nullptr, nullptr};
  int result = 0;
  ex_lazy_fn_eagerly(
      executor_fn, nullptr, nullptr, std::make_tuple(10), std::make_tuple(&result));

  EXPECT_EQ(result, 10 * 2 * 5);
}

}  // namespace dune::fn::lazy_fn::tests
