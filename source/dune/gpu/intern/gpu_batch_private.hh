/* GPU geometry batch
 * Contains VAOs + VBOs + Shader representing a drawable entity. */

#pragma once

#include "gpu_batch.h"
#include "gpu_cxt.h"

#include "gpu_index_buffer_private.hh"
#include "gpu_vertex_buffer_private.hh"

namespace dune {
namespace gpu {

/* Base class which is then specialized for each implementation (GL, VK, ...).
 * note Extends GPUBatch as we still needs to expose some of the internals to the outside C code. */
class Batch : public GPUBatch {
 public:
  virtual ~Batch() = default;

  virtual void draw(int v_first, int v_count, int i_first, int i_count) = 0;

  /* Convenience casts. */
  IndexBuf *elem_() const
  {
    return unwrap(elem);
  }
  VertBuf *verts_(const int index) const
  {
    return unwrap(verts[index]);
  }
  VertBuf *inst_(const int index) const
  {
    return unwrap(inst[index]);
  }
};

}  // namespace gpu
}  // namespace dune
