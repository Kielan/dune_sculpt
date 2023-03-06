#pragma once

struct DPenData;

namespace dune::dgraph {

struct DGraph;

/* Backup of volume datablocks runtime data. */
class DPenBackup {
 public:
  DPenBackup(const DGraph *dgraph);

  void init_from_dpen(DPenData *dpd);
  void restore_to_dpen(DPenData *dpd);

  const DGraph *dgraph;
};

}  // namespace dune::dgraph
