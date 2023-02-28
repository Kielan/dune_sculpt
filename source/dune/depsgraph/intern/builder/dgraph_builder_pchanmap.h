#pragma once

#include "intern/dgraph_type.h"

namespace dune::dgraph {

struct RootPChanMap {
  /** Debug contents of map. */
  void print_debug();

  /** Add a mapping. */
  void add_bone(const char *bone, const char *root);

  /** Check if there's a common root bone between two bones. */
  bool has_common_root(const char *bone1, const char *bone2) const;

 protected:
  /**
   * The strings are only referenced by this map. Users of RootPChanMap have to make sure that the
   * life-time of the strings is long enough.
   */
  Map<StringRefNull, Set<StringRefNull>> map_;
};

}  // namespace dune::dgraph
