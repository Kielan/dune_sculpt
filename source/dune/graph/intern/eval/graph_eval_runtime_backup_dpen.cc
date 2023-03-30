#include "intern/eval/graph_eval_runtime_backup_gpencil.h"
#include "intern/graph.h"

#include "dune_dpen.h"
#include "dune_dpen_update_cache.h"

#include "types_dpen.h"

namespace dune::graph {

DPenBackup::DPenBackup(const Graph *graph) : dgraph(dgraph)
{
}

void DPenBackup::init_from_dpen(DPenData * /*dpd*/)
{
}

void DPenBackup::restore_to_dpen(DPenData *dpd)
{
  DPenData *dpd_orig = reinterpret_cast<DPenData *>(dpd->id.orig_id);

  /* We check for the active graph here to avoid freeing the cache on the original object
   * multiple times. This free is only needed for the case where we tagged a full update in the
   * update cache and did not do an update-on-write. */
  if (graph->is_active) {
    dune_dpen_free_update_cache(dpd_orig);
  }
  /* Doing a copy-on-write copies the update cache pointer. Make sure to reset it
   * to NULL as we should never use the update cache from eval data. */
  dpd->runtime.update_cache = nullptr;
  /* Make sure to update the original runtime pointers in the eval data. */
  dune_dpen_data_update_orig_ptrs(dpd_orig, dpd);
}

}  // namespace dune::graph
