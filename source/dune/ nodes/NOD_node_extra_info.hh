#pragma once

#include "lib_vector.hh"

#include "ui_c.hh"

namespace dune::nodes {

struct NodeExtraInfoRow {
  std::string text;
  int icon = 0;
  const char *tooltip = nullptr;

  uiButToolTipFn tooltip_fn = nullptr;
  void *tooltip_fn_arg = nullptr;
  void (*tooltip_fn_free_arg)(void *) = nullptr;
};

struct NodeExtraInfoParams {
  Vector<NodeExtraInfoRow> &rows;
  const Node &node;
  const Cxt &C;
};

}  // namespace dune::nodes
