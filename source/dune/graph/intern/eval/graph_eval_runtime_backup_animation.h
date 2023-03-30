#pragma once

#include "dune_modifier.h"

#include "intern/graph_type.h"

namespace dune::graph {

struct Graph;

class AnimationValueBackup {
 public:
  AnimationValueBackup() = default;
  AnimationValueBackup(const string &api_path, int array_index, float value);

  AnimationValueBackup(const AnimationValueBackup &other) = default;
  AnimationValueBackup(AnimationValueBackup &&other) noexcept = default;

  AnimationValueBackup &op=(const AnimationValueBackup &other) = default;
  AnimationValueBackup &op=(AnimationValueBackup &&other) = default;

  string api_path;
  int array_index;
  float value;
};

/* Backup of animated properties values. */
class AnimationBackup {
 public:
  AnimationBackup(const Graph *graph);

  void reset();

  void init_from_id(Id *id);
  void restore_to_id(Id *id);

  bool meed_value_backup;
  Vector<AnimationValueBackup> values_backup;
};

}  // namespace dune::graph
