/** \file
 * \ingroup gpu
 *
 * GPU geometric primitives
 */

#pragma once

#include "GPU_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GPU_PRIM_POINTS,
  GPU_PRIM_LINES,
  GPU_PRIM_TRIS,
  GPU_PRIM_LINE_STRIP,
  GPU_PRIM_LINE_LOOP, /* GL has this, Vulkan does not */
  GPU_PRIM_TRI_STRIP,
  GPU_PRIM_TRI_FAN,

  GPU_PRIM_LINES_ADJ,
  GPU_PRIM_TRIS_ADJ,
  GPU_PRIM_LINE_STRIP_ADJ,

  GPU_PRIM_NONE,
} GPUPrimType;

/* what types of primitives does each shader expect? */
typedef enum {
  GPU_PRIM_CLASS_NONE = 0,
  GPU_PRIM_CLASS_POINT = (1 << 0),
  GPU_PRIM_CLASS_LINE = (1 << 1),
  GPU_PRIM_CLASS_SURFACE = (1 << 2),
  GPU_PRIM_CLASS_ANY = GPU_PRIM_CLASS_POINT | GPU_PRIM_CLASS_LINE | GPU_PRIM_CLASS_SURFACE,
} GPUPrimClass;

/**
 * TODO: Improve error checking by validating that the shader is suited for this primitive type.
 * GPUPrimClass GPU_primtype_class(GPUPrimType);
 * bool GPU_primtype_belongs_to_class(GPUPrimType, GPUPrimClass);
 */

#ifdef __cplusplus
}
#endif
