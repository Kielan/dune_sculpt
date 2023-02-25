#pragma once

struct DPenData;

namespace dune::deg {

struct Depsgraph;

/* Backup of volume datablocks runtime data. */
class DPenBackup {
 public:
  GPencilBackup(const Depsgraph *depsgraph);

  void init_from_gpencil(DPenData *gpd);
  void restore_to_gpencil(bGPdata *gpd);

  const Depsgraph *depsgraph;
};

}  // namespace dune::deg
