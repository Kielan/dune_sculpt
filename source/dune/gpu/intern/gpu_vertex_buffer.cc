/* GPU vertex buffer */

#include "mem_guardedalloc.h"

#include "gpu_backend.hh"
#include "gpu_vertex_format_private.h"

#include "gl_vertex_buffer.hh"    /* TODO: remove. */
#include "gpu_context_private.hh" /* TODO: remove. */

#include "gpu_vertex_buffer_private.hh"

#include <cstring>

/** VertBuf **/
namespace dune::gpu {

size_t VertBuf::memory_usage = 0;

VertBuf::VertBuf()
{
  /* Needed by some code check. */
  format.attr_len = 0;
}

VertBuf::~VertBuf()
{
  /* Should already have been cleared. */
  lib_assert(flag == GPU_VERTBUF_INVALID);
}

void VertBuf::init(const GPUVertFormat *format, GPUUsageType usage)
{
  usage_ = usage;
  flag = GPU_VERTBUF_DATA_DIRTY;
  gpu_vertformat_copy(&this->format, format);
  if (!format->packed) {
    VertexFormat_pack(&this->format);
  }
  flag |= GPU_VERTBUF_INIT;
}

void VertBuf::clear()
{
  this->release_data();
  flag = GPU_VERTBUF_INVALID;
}

VertBuf *VertBuf::duplicate()
{
  VertBuf *dst = GPUBackend::get()->vertbuf_alloc();
  /* Full copy. */
  *dst = *this;
  /* Almost full copy... */
  dst->handle_refcount_ = 1;
  /* Duplicate all needed implementation specifics data. */
  this->duplicate_data(dst);
  return dst;
}

void VertBuf::allocate(uint vert_len)
{
  lib_assert(format.packed);
  /* Catch any unnecessary usage. */
  lib_assert(vertex_alloc != vert_len || data == nullptr);
  vertex_len = vertex_alloc = vert_len;

  this->acquire_data();

  flag |= GPU_VERTBUF_DATA_DIRTY;
}

void VertBuf::resize(uint vert_len)
{
  /* Catch any unnecessary usage. */
  lib_assert(vertex_alloc != vert_len);
  vertex_len = vertex_alloc = vert_len;

  this->resize_data();

  flag |= GPU_VERTBUF_DATA_DIRTY;
}

void VertBuf::upload()
{
  this->upload_data();
}

}  // namespace dune::gpu

/* C-API */
using namespace dune;
using namespace dune::gpu;

/* Creation & deletion */

GPUVertBuf *gpu_vertbuf_calloc()
{
  return wrap(GPUBackend::get()->vertbuf_alloc());
}

GPUVertBuf *gpu_vertbuf_create_with_format_ex(const GPUVertFormat *format, GPUUsageType usage)
{
  GPUVertBuf *verts = gpu_vertbuf_calloc();
  unwrap(verts)->init(format, usage);
  return verts;
}

void gpu_vertbuf_init_with_format_ex(GPUVertBuf *verts_,
                                     const GPUVertFormat *format,
                                     GPUUsageType usage)
{
  unwrap(verts_)->init(format, usage);
}

void gpu_vertbuf_init_build_on_device(GPUVertBuf *verts, GPUVertFormat *format, uint v_len)
{
  gpu_vertbuf_init_with_format_ex(verts, format, GPU_USAGE_DEVICE_ONLY);
  gpu_vertbuf_data_alloc(verts, v_len);
}

GPUVertBuf *gpu_vertbuf_duplicate(GPUVertBuf *verts_)
{
  return wrap(unwrap(verts_)->duplicate());
}

const void *gpu_vertbuf_read(GPUVertBuf *verts)
{
  return unwrap(verts)->read();
}

void *gpu_vertbuf_unmap(const GPUVertBuf *verts, const void *mapped_data)
{
  return unwrap(verts)->unmap(mapped_data);
}

void gpu_vertbuf_clear(GPUVertBuf *verts)
{
  unwrap(verts)->clear();
}

void gpu_vertbuf_discard(GPUVertBuf *verts)
{
  unwrap(verts)->clear();
  unwrap(verts)->ref_remove();
}

void gpu_vertbuf_handle_ref_add(GPUVertBuf *verts)
{
  unwrap(verts)->ref_add();
}

void gpu_vertbuf_handle_ref_remove(GPUVertBuf *verts)
{
  unwrap(verts)->refuse_remove();
}

/* -------- Data update -------- */

void gpu_vertbuf_data_alloc(GPUVertBuf *verts, uint v_len)
{
  unwrap(verts)->allocate(v_len);
}

void gpu_vertbuf_data_resize(GPUVertBuf *verts, uint v_len)
{
  unwrap(verts)->resize(v_len);
}

void gpu_vertbuf_data_len_set(GPUVertBuf *verts_, uint v_len)
{
  VertBuf *verts = unwrap(verts_);
  lib_assert(verts->data != nullptr); /* Only for dynamic data. */
  lib_assert(v_len <= verts->vertex_alloc);
  verts->vertex_len = v_len;
}

void gpu_vertbuf_attr_set(GPUVertBuf *verts_, uint a_idx, uint v_idx, const void *data)
{
  VertBuf *verts = unwrap(verts_);
  const GPUVertFormat *format = &verts->format;
  const GPUVertAttr *a = &format->attrs[a_idx];
  lib_assert(v_idx < verts->vertex_alloc);
  lib_assert(a_idx < format->attr_len);
  lib_assert(verts->data != nullptr);
  verts->flag |= GPU_VERTBUF_DATA_DIRTY;
  memcpy(verts->data + a->offset + v_idx * format->stride, data, a->sz);
}

void gpu_vertbuf_attr_fill(GPUVertBuf *verts_, uint a_idx, const void *data)
{
  VertBuf *verts = unwrap(verts_);
  const GPUVertFormat *format = &verts->format;
  lib_assert(a_idx < format->attr_len);
  const GPUVertAttr *a = &format->attrs[a_idx];
  const uint stride = a->sz; /* tightly packed input data */
  verts->flag |= GPU_VERTBUF_DATA_DIRTY;
  gpu_vertbuf_attr_fill_stride(verts_, a_idx, stride, data);
}

void gpu_vertbuf_vert_set(GPUVertBuf *verts_, uint v_idx, const void *data)
{
  VertBuf *verts = unwrap(verts_);
  const GPUVertFormat *format = &verts->format;
  lib_assert(v_idx < verts->vertex_alloc);
  lib_assert(verts->data != nullptr);
  verts->flag |= GPU_VERTBUF_DATA_DIRTY;
  memcpy(verts->data + v_idx * format->stride, data, format->stride);
}

void gpu_vertbuf_attr_fill_stride(GPUVertBuf *verts_, uint a_idx, uint stride, const void *data)
{
  VertBuf *verts = unwrap(verts_);
  const GPUVertFormat *format = &verts->format;
  const GPUVertAttr *a = &format->attrs[a_idx];
  lib_assert(a_idx < format->attr_len);
  lib_assert(verts->data != nullptr);
  verts->flag |= GPU_VERTBUF_DATA_DIRTY;
  const uint vertex_len = verts->vertex_len;

  if (format->attr_len == 1 && stride == format->stride) {
    /* we can copy it all at once */
    memcpy(verts->data, data, vertex_len * a->sz);
  }
  else {
    /* we must copy it per vertex */
    for (uint v = 0; v < vertex_len; v++) {
      memcpy(
          verts->data + a->offset + v * format->stride, (const uchar *)data + v * stride, a->sz);
    }
  }
}

void gpu_vertbuf_attr_get_raw_data(GPUVertBuf *verts_, uint a_idx, GPUVertBufRaw *access)
{
  VertBuf *verts = unwrap(verts_);
  const GPUVertFormat *format = &verts->format;
  const GPUVertAttr *a = &format->attrs[a_idx];
  lib_assert(a_idx < format->attr_len);
  lib_assert(verts->data != nullptr);

  verts->flag |= GPU_VERTBUF_DATA_DIRTY;
  verts->flag &= ~GPU_VERTBUF_DATA_UPLOADED;
  access->size = a->sz;
  access->stride = format->stride;
  access->data = (uchar *)verts->data + a->offset;
  access->data_init = access->data;
#ifdef DEBUG
  access->_data_end = access->data_init + (size_t)(verts->vertex_alloc * format->stride);
#endif
}

/* Getters */
void *gpu_vertbuf_get_data(const GPUVertBuf *verts)
{
  /* TODO: Assert that the format has no padding. */
  return unwrap(verts)->data;
}

void *gpu_vertbuf_steal_data(GPUVertBuf *verts_)
{
  VertBuf *verts = unwrap(verts_);
  /* TODO: Assert that the format has no padding. */
  lib_assert(verts->data);
  void *data = verts->data;
  verts->data = nullptr;
  return data;
}

const GPUVertFormat *gpu_vertbuf_get_format(const GPUVertBuf *verts)
{
  return &unwrap(verts)->format;
}

uint gpu_vertbuf_get_vertex_alloc(const GPUVertBuf *verts)
{
  return unwrap(verts)->vertex_alloc;
}

uint gpu_vertbuf_get_vertex_len(const GPUVertBuf *verts)
{
  return unwrap(verts)->vertex_len;
}

GPUVertBufStatus gpu_vertbuf_get_status(const GPUVertBuf *verts)
{
  return unwrap(verts)->flag;
}

void gpu_vertbuf_tag_dirty(GPUVertBuf *verts)
{
  unwrap(verts)->flag |= GPU_VERTBUF_DATA_DIRTY;
}

uint gpu_vertbuf_get_memory_usage()
{
  return VertBuf::memory_usage;
}

void gpu_vertbuf_use(GPUVertBuf *verts)
{
  unwrap(verts)->upload();
}

void gpu_vertbuf_wrap_handle(GPUVertBuf *verts, uint64_t handle)
{
  unwrap(verts)->wrap_handle(handle);
}

void gpu_vertbuf_bind_as_ssbo(struct GPUVertBuf *verts, int binding)
{
  unwrap(verts)->bind_as_ssbo(binding);
}

void gpu_vertbuf_update_sub(GPUVertBuf *verts, uint start, uint len, const void *data)
{
  unwrap(verts)->update_sub(start, len, data);
}
