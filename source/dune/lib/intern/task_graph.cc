#include "mem_guardedalloc.h"

#include "lib_task.h"

#include <memory>
#include <vector>

#ifdef WITH_TBB
#  include <tbb/flow_graph.h>
#endif

/* Task Graph */
struct TaskGraph {
#ifdef WITH_TBB
  tbb::flow::graph tbb_graph;
#endif
  std::vector<std::unique_ptr<TaskNode>> nodes;

#ifdef WITH_CXX_GUARDEDALLOC
  MEM_CXX_CLASS_ALLOC_FNS("task_graph:TaskGraph")
#endif
};

/* TaskNode - a node in the task graph. */
struct TaskNode {
  /* TBB Node. */
#ifdef WITH_TBB
  tbb::flow::continue_node<tbb::flow::continue_msg> tbb_node;
#endif
  /* Successors to ex after this task, for serial ex fallback. */
  std::vector<TaskNode *> successors;

  /* User fn to be ex w given task data. */
  TaskGraphNodeRunFn run_fn;
  void *task_data;
  /* Optional cb to free task data along with the graph. If task data
   * is shared between nodes, only a single task node should free the data. */
  TaskGraphNodeFreeFn free_fn;

  TaskNode(TaskGraph *task_graph,
           TaskGraphNodeRunFn run_fn,
           void *task_data,
           TaskGraphNodeFreeFn free_fn)
      :
#ifdef WITH_TBB
        tbb_node(task_graph->tbb_graph,
                 tbb::flow::unlimited,
                 [&](const tbb::flow::continue_msg input) { run(input); }),
#endif
        run_fn(run_fn),
        task_data(task_data),
        free_fn(free_fn)
  {
#ifndef WITH_TBB
    UNUSED_VARS(task_graph);
#endif
  }

  TaskNode(const TaskNode &other) = delete;
  TaskNode &operator=(const TaskNode &other) = delete;

  ~TaskNode()
  {
    if (task_data && free_fn) {
      free_fn(task_data);
    }
  }

#ifdef WITH_TBB
  tbb::flow::continue_msg run(const tbb::flow::continue_msg /*input*/)
  {
    run_fn(task_data);
    return tbb::flow::continue_msg();
  }
#endif

  void run_serial()
  {
    run_fn(task_data);
    for (TaskNode *successor : successors) {
      successor->run_serial();
    }
  }

#ifdef WITH_CXX_GUARDEDALLOC
  MEM_CXX_CLASS_ALLOC_FNS("task_graph:TaskNode")
#endif
};

TaskGraph *lib_task_graph_create()
{
  return new TaskGraph();
}

void lib_task_graph_free(TaskGraph *task_graph)
{
  delete task_graph;
}

void lib_task_graph_work_and_wait(TaskGraph *task_graph)
{
#ifdef WITH_TBB
  task_graph->tbb_graph.wait_for_all();
#else
  UNUSED_VARS(task_graph);
#endif
}

TaskNode *lib_task_graph_node_create(TaskGraph *task_graph,
                                     TaskGraphNodeRunFunction run,
                                     void *user_data,
                                     TaskGraphNodeFreeFunction free_func)
{
  TaskNode *task_node = new TaskNode(task_graph, run, user_data, free_func);
  task_graph->nodes.push_back(std::unique_ptr<TaskNode>(task_node));
  return task_node;
}

bool lib_task_graph_node_push_work(TaskNode *task_node)
{
#ifdef WITH_TBB
  if (lib_task_scheduler_num_threads() > 1) {
    return task_node->tbb_node.try_put(tbb::flow::continue_msg());
  }
#endif

  task_node->run_serial();
  return true;
}

void lib_task_graph_edge_create(TaskNode *from_node, TaskNode *to_node)
{
#ifdef WITH_TBB
  if (lib_task_scheduler_num_threads() > 1) {
    tbb::flow::make_edge(from_node->tbb_node, to_node->tbb_node);
    return;
  }
#endif

  from_node->successors.push_back(to_node);
}
