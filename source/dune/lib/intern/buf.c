/* Primitive generic buf lib.
 *
 * - Automatically grow as needed.
 *   (currently never shrinks).
 * - Can be passed between fns.
 * - Supports using stack mem by default,
 *   falling back to heap as needed.
 *
 * Usage examples:
 * \code{.c}
 * lib_buf_declare_static(int, my_int_array, LIB_BUF_NOP, 32);
 *
 * lib_buf_append(my_int_array, int, 42);
 * lib_assert(my_int_array.count == 1);
 * lib_assert(lib_buf_at(my_int_array, int, 0) == 42);
 *
 * lib_buf_free(&my_int_array);
 * \endcode
 *
 * this more or less fills same purpose as lib_array,
 * but supports resizing the array outside of the fn
 * it was declared in. */

#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_buf.h"
#include "lib_utildefines.h"

#include "lib_strict_flags.h"

static void *buf_alloc(LibBuf *buf, const size_t len)
{
  return mem_malloc(buf->elem_size * len, "lib_buf.data");
}

static void *buf_realloc(LibBuf *buf, const size_t len)
{
  return mem_realloc_id(buf->data, buf->elem_size * len, "lib_buf.data");
}

void lib_buf_resize(LibBuf *buf, const size_t new_count)
{
  if (UNLIKELY(new_count > buf->alloc_count)) {
    if (buf->flag & LIB_BUF_USE_STATIC) {
      void *orig = buf->data;

      buf->data = buf_alloc(buf, new_count);
      memcpy(buf->data, orig, buf->elem_size * buf->count);
      buf->alloc_count = new_count;
      buf->flag &= ~LIB_BUF_USE_STATIC;
    }
    else {
      if (buf->alloc_count && (new_count < buf->alloc_count * 2)) {
        buf->alloc_count *= 2;
      }
      else {
        buf->alloc_count = new_count;
      }

      buf->data = buf_realloc(buf, buf->alloc_count);
    }
  }

  buf->count = new_count;
}

void lib_buf_reinit(LibBuf *buf, const size_t new_count)
{
  if (UNLIKELY(new_count > buf->alloc_count)) {
    if ((buf->flag & LIB_BUF_USE_STATIC) == 0) {
      if (buf->data) {
        mem_free(buf->data);
      }
    }

    if (buf->alloc_count && (new_count < buf->alloc_count * 2)) {
      buf->alloc_count *= 2;
    }
    else {
      buf->alloc_count = new_count;
    }

    buf->flag &= ~LIB_BUF_USE_STATIC;
    buf->data = buf_alloc(buf, buf->alloc_count);
  }

  buffer->count = new_count;
}

void _bli_buf_append_array(LibBuf *buf, void *new_data, size_t count)
{
  size_t size = buf->count;
  lib_buf_resize(buf, size + count);

  uint8_t *bytes = (uint8_t *)buf->data;
  memcpy(bytes + size * buf->elem_size, new_data, count * buf->elem_size);
}

void _bli_buf_free(LibBuf *buf)
{
  if ((buf->flag & LIB_BUF_USE_STATIC) == 0) {
    if (buf->data) {
      mem_free(buf->data);
    }
  }
  memset(buf, 0, sizeof(*buf));
}
