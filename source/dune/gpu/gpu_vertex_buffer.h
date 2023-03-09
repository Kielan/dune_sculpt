/** GPU vertex buffer **/

#pragma once

#include "lib_utildefines.h"

#include "gpu_vertex_format.h"

typedef enum {
  /** Initial state. */
  GPU_VERTBUF_INVALID = 0,
  /** Was init with a vertex format. */
  GPU_VERTBUF_INIT = (1 << 0),
  /** Data has been touched and need to be re-uploaded. */
  GPU_VERTBUF_DATA_DIRTY = (1 << 1),
  /** The buffer has been created inside GPU memory. */
  GPU_VERTBUF_DATA_UPLOADED = (1 << 2),
} GpuVertBufStatus;

ENUM_OPERATORS(GpuVertBufStatus, GPU_VERTBUF_DATA_UPLOADED)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How to create a #GPUVertBuf:
 * 1) verts = GPU_vertbuf_calloc()
 * 2) GPU_vertformat_attr_add(verts->format, ...)
 * 3) GPU_vertbuf_data_alloc(verts, vertex_len) <-- finalizes/packs vertex format
 * 4) GPU_vertbuf_attr_fill(verts, pos, application_pos_buffer)
 */

typedef enum {
  /* can be extended to support more types */
  GPU_USAGE_STREAM,
  GPU_USAGE_STATIC, /* do not keep data in memory */
  GPU_USAGE_DYNAMIC,
  GPU_USAGE_DEVICE_ONLY, /* Do not do host->device data transfers. */
} GpuUsageType;

/** Opaque type hiding dune::gpu::VertBuf. */
typedef struct GpuVertBuf GpuVertBuf;

GpuVertBuf *gpu_vertbuf_calloc(void);
GpuVertBuf *gpu_vertbuf_create_with_format_ex(const GpuVertFormat *, GpuUsageType);

#define gpu_vertbuf_create_with_format(format) \
  gpu_vertbuf_create_with_format_ex(format, GPU_USAGE_STATIC)

/**
 * (Download and) return a pointer containing the data of a vertex buffer.
 *
 * Note that the returned pointer is still owned by the driver. To get an
 * local copy, use `gpu_vertbuf_unmap` after calling `GPU_vertbuf_read`.
 */
const void *gpu_vertbuf_read(GpuVertBuf *verts);
void *gpu_vertbuf_unmap(const GpuVertBuf *verts, const void *mapped_data);
/** Same as discard but does not free. */
void gpu_vertbuf_clear(GpuVertBuf *verts);
void gpu_vertbuf_discard(GpuVertBuf *);

/**
 * Avoid GpuVertBuf data-block being free but not its data.
 */
void gpu_vertbuf_handle_ref_add(GpuVertBuf *verts);
void gpu_vertbuf_handle_ref_remove(GpuVertBuf *verts);

void gpu_vertbuf_init_with_format_ex(GpuVertBuf *, const GpuVertFormat *, GpuUsageType);

void gpu_vertbuf_init_build_on_device(GpuVertBuf *verts, GpuVertFormat *format, uint v_len);

#define GPU_vertbuf_init_with_format(verts, format) \
  gpu_vertbuf_init_with_format_ex(verts, format, GPU_USAGE_STATIC)

GpuVertBuf *gpu_vertbuf_duplicate(GpuVertBuf *verts);

/**
 * Create a new allocation, discarding any existing data.
 */
void gpu_vertbuf_data_alloc(GpuVertBuf *, uint v_len);
/**
 * Resize buffer keeping existing data.
 */
void gpu_vertbuf_data_resize(GpuVertBuf *, uint v_len);
/**
 * Set vertex count but does not change allocation.
 * Only this many verts will be uploaded to the GPU and rendered.
 * This is useful for streaming data.
 */
void gpu_vertbuf_data_len_set(GpuVertBuf *, uint v_len);

/**
 * The most important #set_attr variant is the untyped one. Get it right first.
 * It takes a void* so the app developer is responsible for matching their app data types
 * to the vertex attribute's type and component count. They're in control of both, so this
 * should not be a problem.
 */
void gpu_vertbuf_attr_set(GpuVertBuf *, uint a_idx, uint v_idx, const void *data);

/** Fills a whole vertex (all attributes). Data must match packed layout. */
void gpu_vertbuf_vert_set(GpuVertBuf *verts, uint v_idx, const void *data);

/**
 * Tightly packed, non interleaved input data.
 */
void gpu_vertbuf_attr_fill(GpuVertBuf *, uint a_idx, const void *data);

void gpu_vertbuf_attr_fill_stride(GpuVertBuf *, uint a_idx, uint stride, const void *data);

/**
 * For low level access only.
 */
typedef struct GpuVertBufRaw {
  uint size;
  uint stride;
  unsigned char *data;
  unsigned char *data_init;
#ifdef DEBUG
  /* Only for overflow check */
  unsigned char *_data_end;
#endif
} GpuVertBufRaw;

GPU_INLINE void *gpu_vertbuf_raw_step(GPUVertBufRaw *a)
{
  unsigned char *data = a->data;
  a->data += a->stride;
  lib_assert(data < a->_data_end);
  return (void *)data;
}

GPU_INLINE uint gpu_vertbuf_raw_used(GpuVertBufRaw *a)
{
  return ((a->data - a->data_init) / a->stride);
}

void Gpu_vertbuf_attr_get_raw_data(GPUVertBuf *, uint a_idx, GPUVertBufRaw *access);

/**
 * Returns the data buffer and set it to null internally to avoid freeing.
 * Be careful when using this. The data needs to match the expected format.
 */
void *gpu_vertbuf_steal_data(GpuVertBuf *verts);

/** Be careful when using this. The data needs to match the expected format. **/
void *gpu_vertbuf_get_data(const GpuVertBuf *verts);
const GpuVertFormat *gpu_vertbuf_get_format(const GpuVertBuf *verts);
uint gpu_vertbuf_get_vertex_alloc(const GpuVertBuf *verts);
uint gpu_vertbuf_get_vertex_len(const GpuVertBuf *verts);
GpuVertBufStatus gpu_vertbuf_get_status(const GpuVertBuf *verts);
void gpu_vertbuf_tag_dirty(GpuVertBuf *verts);

/**
 * Should be rename to #GPU_vertbuf_data_upload.
 */
void gpu_vertbuf_use(GpuVertBuf *);
void gpu_vertbuf_bind_as_ssbo(struct GpuVertBuf *verts, int binding);

void gpu_vertbuf_wrap_handle(GpuVertBuf *verts, uint64_t handle);

/**
 * XXX: do not use!
 * This is just a wrapper for the use of the Hair refine workaround.
 * To be used with #GPU_vertbuf_use().
 */
void gpu_vertbuf_update_sub(GPUVertBuf *verts, uint start, uint len, const void *data);

/* Metrics */
uint gpu_vertbuf_get_memory_usage(void);

/* Macros */
#define GPU_VERTBUF_DISCARD_SAFE(verts) \
  do { \
    if (verts != NULL) { \
      GPU_vertbuf_discard(verts); \
      verts = NULL; \
    } \
  } while (0)

#ifdef __cplusplus
}
#endif
