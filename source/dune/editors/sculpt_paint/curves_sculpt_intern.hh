#pragma once

#include "curves_sculpt_intern.h"

#include "lib_math_vector.hh"

namespace dune::ed::sculpt_paint {

struct StrokeExtension {
  bool is_first;
  float2 mouse_position;
};

/**
 * Base class for stroke based operations in curves sculpt mode.
 */
class CurvesSculptStrokeOperation {
 public:
  virtual ~CurvesSculptStrokeOperation() = default;
  virtual void on_stroke_extended(dContext *C, const StrokeExtension &stroke_extension) = 0;
};

std::unique_ptr<CurvesSculptStrokeOperation> new_add_operation();
std::unique_ptr<CurvesSculptStrokeOperation> new_comb_operation();
std::unique_ptr<CurvesSculptStrokeOperation> new_delete_operation();
std::unique_ptr<CurvesSculptStrokeOperation> new_snake_hook_operation();

}  // namespace dune::ed::sculpt_paint
