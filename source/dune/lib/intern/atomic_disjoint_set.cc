#include "lib_atomic_disjoint_set.hh"
#include "lib_enumerable_thread_specific.hh"
#include "lib_map.hh"
#include "lib_sort.hh"
#include "lib_task.hh"

namespace dune {

AtomicDisjointSet::AtomicDisjointSet(const int size) : items_(size)
{
  threading::parallel_for(IndexRange(size), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      items_[i].store(Item{i, 0}, relaxed);
    }
  });
}

static void update_first_occurrence(Map<int, int> &map, const int root, const int index)
{
  map.add_or_modify(
      root,
      [&](int *first_occurrence) { *first_occurrence = index; },
      [&](int *first_occurrence) {
        if (index < *first_occurrence) {
          *first_occurrence = index;
        }
      });
}

void AtomicDisjointSet::calc_reduced_ids(MutableSpan<int> result) const
{
  lib_assert(result.size() == items_.size());

  const int size = result.size();

  /* Find the root for elem. W multi-threading, this root is not deterministic. So
   * some postprocessing has to be done to make it deterministic. */
  threading::EnumerableThreadSpecific<Map<int, int>> first_occurrence_by_root_per_thread;
  threading::parallel_for(IndexRange(size), 1024, [&](const IndexRange range) {
    Map<int, int> &first_occurrence_by_root = first_occurrence_by_root_per_thread.local();
    for (const int i : range) {
      const int root = this->find_root(i);
      result[i] = root;
      update_first_occurrence(first_occurrence_by_root, root, i);
    }
  });

  /* Build a map containing the 1st elem index that has a certain root. */
  Map<int, int> &combined_map = first_occurrence_by_root_per_thread.local();
  for (const Map<int, int> &other_map : first_occurrence_by_root_per_thread) {
    if (&combined_map == &other_map) {
      continue;
    }
    for (const auto item : other_map.items()) {
      update_first_occurrence(combined_map, item.key, item.val);
    }
  }

  struct RootOccurence {
    int root;
    int first_occurrence;
  };

  /* Sort roots by 1at occurrence. This removes the non-determinism above. */
  Vector<RootOccurence, 16> root_occurrences;
  root_occurrences.reserve(combined_map.size());
  for (const auto item : combined_map.items()) {
    root_occurrences.append({item.key, item.value});
  }
  parallel_sort(root_occurrences.begin(),
                root_occurrences.end(),
                [](const RootOccurence &a, const RootOccurence &b) {
                  return a.first_occurrence < b.first_occurrence;
                });

  /* Remap original root vals w deterministic values. */
  Map<int, int> id_by_root;
  id_by_root.reserve(root_occurrences.size());
  for (const int i : root_occurrences.index_range()) {
    id_by_root.add_new(root_occurrences[i].root, i);
  }
  threading::parallel_for(IndexRange(size), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      result[i] = id_by_root.lookup(result[i]);
    }
  });
}

int AtomicDisjointSet::count_sets() const
{
  return threading::parallel_reduce<int>(
      items_.index_range(),
      1024,
      0,
      [&](const IndexRange range, int count) {
        for (const int i : range) {
          if (this->is_root(i)) {
            count++;
          }
        }
        return count;
      },
      [](const int a, const int b) { return a + b; });
}
}  // namespace dune
