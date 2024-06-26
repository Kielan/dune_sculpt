/** Mimics old style opengl immediate mode drawing. **/

#pragma once

#include "gpu_batch.h"
#include "gpu_primitive.h"
#include "gpu_shader.h"
#include "gpu_vertex_format.h"

namespace dune::gpu {

class Immediate {
 public:
  /** Pointer to the mapped buffer data for the current vertex. */
  uchar *vertex_data = NULL;
  /** Current vertex index. */
  uint vertex_idx = 0;
  /** Length of the buffer in vertices. */
  uint vertex_len = 0;
  /** Which attributes of current vertex have not been given values? */
  uint16_t unassigned_attr_bits = 0;
  /** Attributes that needs to be set. One bit per attribute. */
  uint16_t enabled_attr_bits = 0;

  /** Current draw call specification. */
  GPUPrimType prim_type = GPU_PRIM_NONE;
  GPUVertFormat vertex_format = {};
  GPUShader *shader = NULL;
  /** Enforce strict vertex count (disabled when using #immBeginAtMost). */
  bool strict_vertex_len = true;

  /** Batch in construction when using #immBeginBatch. */
  GPUBatch *batch = NULL;

  /** Wide Line workaround. */

  /** Previously bound shader to restore after drawing. */
  eGPUBuiltinShader prev_builtin_shader = GPU_SHADER_TEXT;
  /** Builtin shader index. Used to test if the workaround can be done. */
  eGPUBuiltinShader builtin_shader_bound = GPU_SHADER_TEXT;
  /** Uniform color: Kept here to update the wide-line shader just before #immBegin. */
  float uniform_color[4];

 public:
  Immediate(){};
  virtual ~Immediate(){};

  virtual uchar *begin() = 0;
  virtual void end() = 0;
};

}  // namespace dune::gpu

void immActivate();
void immDeactivate();
