#pragma once

struct DPenData;

namespace dune::graph {

struct Graph;

/* Backup of volume datablocks runtime data. */
class DPenBackup {
 public:
  DPenBackup(const Graph *graph);

  void init_from_dpen(DPenData *dpd);
  void restore_to_dpen(DPenData *dpd);

  const Graph *graph;
};

}  // namespace dune::graph
