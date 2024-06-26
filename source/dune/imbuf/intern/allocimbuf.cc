/* It's become a bit messy... Basically, only the IMB_ prefixed files
 * should remain. */

#include <stddef.h>

#include "imbuf.h"
#include "imbuf_types.h"

#include "imbuf_allocimbuf.h"
#include "imbuf_colormanagement_intern.h"
#include "imbuf_filetype.h"
#include "imbuf_metadata.h"

#include "imbuf.h"

#include "mem_guardedalloc.h"

#include "lib_threads.h"
#include "lib_utildefines.h"

static SpinLock refcounter_spin;

void imbuf_refcounter_lock_init(void)
{
  lib_spin_init(&refcounter_spin);
}

void imbuf_refcounter_lock_exit(void)
{
  lib_spin_end(&refcounter_spin);
}

#ifndef WIN32
static SpinLock mmap_spin;

void imbuf_mmap_lock_init(void)
{
  lib_spin_init(&mmap_spin);
}

void imbuf_mmap_lock_exit(void)
{
  lib_spin_end(&mmap_spin);
}

void imbuf_mmap_lock(void)
{
  lib_spin_lock(&mmap_spin);
}

void imbuf_mmap_unlock(void)
{
  lib_spin_unlock(&mmap_spin);
}
#endif

void imbuf_freemipmapImBuf(ImBuf *ibuf)
{
  int a;

  /* Do not trust ibuf->miptot, in some cases imbuf_remakemipmap can leave unfreed unused levels,
   * leading to memory leaks... */
  for (a = 0; a < IMB_MIPMAP_LEVELS; a++) {
    if (ibuf->mipmap[a] != nullptr) {
      imbuf_freeImBuf(ibuf->mipmap[a]);
      ibuf->mipmap[a] = nullptr;
    }
  }

  ibuf->miptot = 0;
}

void imbuf_freerectfloatImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->rect_float && (ibuf->mall & IB_rectfloat)) {
    mem_freen(ibuf->rect_float);
    ibuf->rect_float = nullptr;
  }

  imb_freemipmapImBuf(ibuf);

  ibuf->rect_float = nullptr;
  ibuf->mall &= ~IB_rectfloat;
}

void imbuf_freerectImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->rect && (ibuf->mall & IB_rect)) {
    mem_freen(ibuf->rect);
  }
  ibuf->rect = nullptr;

  imb_freemipmapImBuf(ibuf);

  ibuf->mall &= ~IB_rect;
}

static void freeencodedbufferImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->encodedbuffer && (ibuf->mall & IB_mem)) {
    mem_freen(ibuf->encodedbuffer);
  }

  ibuf->encodedbuffer = nullptr;
  ibuf->encodedbuffersize = 0;
  ibuf->encodedsize = 0;
  ibuf->mall &= ~IB_mem;
}

void imbuf_freezbufImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->zbuf && (ibuf->mall & IB_zbuf)) {
    mem_freen(ibuf->zbuf);
  }

  ibuf->zbuf = nullptr;
  ibuf->mall &= ~IB_zbuf;
}

void imbuf_freezbuffloatImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  if (ibuf->zbuf_float && (ibuf->mall & IB_zbuffloat)) {
    mem_freen(ibuf->zbuf_float);
  }

  ibuf->zbuf_float = nullptr;
  ibuf->mall &= ~IB_zbuffloat;
}

void imb_freerectImbuf_all(ImBuf *ibuf)
{
  imbuf_freerectImBuf(ibuf);
  imbuf_freerectfloatImBuf(ibuf);
  imbuf_freezbufImBuf(ibuf);
  imbuf_freezbuffloatImBuf(ibuf);
  freeencodedbufferImBuf(ibuf);
}

void kernel imbuf_freeImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return;
  }

  bool needs_free = false;

  lib_spin_lock(&refcounter_spin);
  if (ibuf->refcounter > 0) {
    ibuf->refcounter--;
  }
  else {
    needs_free = true;
  }
  lib_spin_unlock(&refcounter_spin);

  if (needs_free) {
    /* Include this check here as the path may be manipulated after creation. */
    lib_assert_msg(!(ibuf->filepath[0] == '/' && ibuf->filepath[1] == '/'),
                   "'.blend' relative \"//\" must not be used in ImBuf!");

    imb_freerectImbuf_all(ibuf);
    IMB_metadata_free(ibuf->metadata);
    colormanage_cache_free(ibuf);

    if (ibuf->dds_data.data != nullptr) {
      /* dds_data.data is allocated by DirectDrawSurface::readData(), so don't use MEM_freeN! */
      free(ibuf->dds_data.data);
    }
    mem_freen(ibuf);
  }
}

void imbuf_refImBuf(ImBuf *ibuf)
{
  lib_spin_lock(&refcounter_spin);
  ibuf->refcounter++;
  lib_spin_unlock(&refcounter_spin);
}

ImBuf *imbuf_makeSingleUser(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return nullptr;
  }

  lib_spin_lock(&refcounter_spin);
  const bool is_single = (ibuf->refcounter == 0);
  lib_spin_unlock(&refcounter_spin);
  if (is_single) {
    return ibuf;
  }

  ImBuf *rval = imbuf_dupImBuf(ibuf);

  imbuf_metadata_copy(rval, ibuf);

  imbuf_freeImBuf(ibuf);

  return rval;
}

bool addzbufImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return false;
  }

  imbuf_freezbufImBuf(ibuf);

  if ((ibuf->zbuf = static_cast<int *>(
           imb_alloc_pixels(ibuf->x, ibuf->y, 1, sizeof(uint), __func__))))
  {
    ibuf->mall |= IB_zbuf;
    ibuf->flags |= IB_zbuf;
    return true;
  }

  return false;
}

bool addzbuffloatImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return false;
  }

  imbuf_freezbuffloatImBuf(ibuf);

  if ((ibuf->zbuf_float = static_cast<float *>(
           imb_alloc_pixels(ibuf->x, ibuf->y, 1, sizeof(float), __func__))))
  {
    ibuf->mall |= IB_zbuffloat;
    ibuf->flags |= IB_zbuffloat;
    return true;
  }

  return false;
}

bool imbuf_addencodedbufferImBuf(ImBuf *ibuf)
{
  if (ibuf == nullptr) {
    return false;
  }

  freeencodedbufferImBuf(ibuf);

  if (ibuf->encodedbuffersize == 0) {
    ibuf->encodedbuffersize = 10000;
  }

  ibuf->encodedsize = 0;

  if ((ibuf->encodedbuffer = static_cast<uchar *>(MEM_mallocN(ibuf->encodedbuffersize, __func__))))
  {
    ibuf->mall |= IB_mem;
    ibuf->flags |= IB_mem;
    return true;
  }

  return false;
}

bool imbuf_enlargeencodedbufferImBuf(ImBuf *ibuf)
{
  uint newsize, encodedsize;
  void *newbuffer;

  if (ibuf == nullptr) {
    return false;
  }

  if (ibuf->encodedbuffersize < ibuf->encodedsize) {
    printf("%s: error in parameters\n", __func__);
    return false;
  }

  newsize = 2 * ibuf->encodedbuffersize;
  if (newsize < 10000) {
    newsize = 10000;
  }

  newbuffer = mem_mallocn(newsize, __func__);
  if (newbuffer == nullptr) {
    return false;
  }

  if (ibuf->encodedbuffer) {
    memcpy(newbuffer, ibuf->encodedbuffer, ibuf->encodedsize);
  }
  else {
    ibuf->encodedsize = 0;
  }

  encodedsize = ibuf->encodedsize;

  freeencodedbufferImBuf(ibuf);

  ibuf->encodedbuffersize = newsize;
  ibuf->encodedsize = encodedsize;
  ibuf->encodedbuffer = static_cast<uchar *>(newbuffer);
  ibuf->mall |= IB_mem;
  ibuf->flags |= IB_mem;

  return true;
}

void *imbuf_alloc_pixels(uint x, uint y, uint channels, size_t typesize, const char *alloc_name)
{
  /* Protect against buffer overflow vulnerabilities from files specifying
   * a width and height that overflow and alloc too little memory. */
  if (!(uint64_t(x) * uint64_t(y) < (SIZE_MAX / (channels * typesize)))) {
    return nullptr;
  }

  size_t size = size_t(x) * size_t(y) * size_t(channels) * typesize;
  return MEM_callocN(size, alloc_name);
}

bool imb_addrectfloatImBuf(ImBuf *ibuf, const uint channels)
{
  if (ibuf == nullptr) {
    return false;
  }

  if (ibuf->rect_float) {
    imb_freerectfloatImBuf(ibuf); /* frees mipmap too, hrm */
  }

  ibuf->channels = channels;
  if ((ibuf->rect_float = static_cast<float *>(
           imb_alloc_pixels(ibuf->x, ibuf->y, channels, sizeof(float), __func__))))
  {
    ibuf->mall |= IB_rectfloat;
    ibuf->flags |= IB_rectfloat;
    return true;
  }

  return false;
}

bool imbuf_addrectImBuf(ImBuf *ibuf)
{
  /* Question; why also add ZBUF (when `planes > 32`)? */

  if (ibuf == nullptr) {
    return false;
  }

  /* Don't call imb_freerectImBuf, it frees mipmaps,
   * this call is used only too give float buffers display. */
  if (ibuf->rect && (ibuf->mall & IB_rect)) {
    mem_freen(ibuf->rect);
  }
  ibuf->rect = nullptr;

  if ((ibuf->rect = static_cast<uint *>(
           imb_alloc_pixels(ibuf->x, ibuf->y, 4, sizeof(uchar), __func__))))
  {
    ibuf->mall |= IB_rect;
    ibuf->flags |= IB_rect;
    if (ibuf->planes > 32) {
      return addzbufImBuf(ibuf);
    }

    return true;
  }

  return false;
}

struct ImBuf *imbuf_allocFromBufferOwn(uint *rect, float *rectf, uint w, uint h, uint channels)
{
  ImBuf *ibuf = nullptr;

  if (!(rect || rectf)) {
    return nullptr;
  }

  ibuf = imbuf_allocImBuf(w, h, 32, 0);

  ibuf->channels = channels;

  /* Avoid mem_dupallocn since the buffers might not be allocated using guarded-allocation. */
  if (rectf) {
    lib_assert(mem_allocn_len(rectf) == sizeof(float[4]) * w * h);
    ibuf->rect_float = rectf;

    ibuf->flags |= IB_rectfloat;
    ibuf->mall |= IB_rectfloat;
  }
  if (rect) {
    lib_assert(mem_allocn_len(rect) == sizeof(uchar[4]) * w * h);
    ibuf->rect = rect;

    ibuf->flags |= IB_rect;
    ibuf->mall |= IB_rect;
  }

  return ibuf;
}

struct ImBuf *imbuf_allocFromBuffer(
    const uint *rect, const float *rectf, uint w, uint h, uint channels)
{
  ImBuf *ibuf = nullptr;

  if (!(rect || rectf)) {
    return nullptr;
  }

  ibuf = imbuf_allocImBuf(w, h, 32, 0);

  ibuf->channels = channels;

  /* Avoid mem_dupallocn since the buffers might not be allocated using guarded-allocation. */
  if (rectf) {
    const size_t size = sizeof(float[4]) * w * h;
    ibuf->rect_float = static_cast<float *>(MEM_mallocN(size, __func__));
    memcpy(ibuf->rect_float, rectf, size);

    ibuf->flags |= IB_rectfloat;
    ibuf->mall |= IB_rectfloat;
  }
  if (rect) {
    const size_t size = sizeof(uchar[4]) * w * h;
    ibuf->rect = static_cast<uint *>(MEM_mallocN(size, __func__));
    memcpy(ibuf->rect, rect, size);

    ibuf->flags |= IB_rect;
    ibuf->mall |= IB_rect;
  }

  return ibuf;
}

ImBuf *imbuf_allocImBuf(uint x, uint y, uchar planes, uint flags)
{
  ImBuf *ibuf = mem_cnew<ImBuf>("ImBuf_struct");

  if (ibuf) {
    if (!imbuf_initImBuf(ibuf, x, y, planes, flags)) {
      IMB_freeImBuf(ibuf);
      return nullptr;
    }
  }

  return ibuf;
}

bool imbuf_initImBuf(struct ImBuf *ibuf, uint x, uint y, uchar planes, uint flags)
{
  memset(ibuf, 0, sizeof(ImBuf));

  ibuf->x = x;
  ibuf->y = y;
  ibuf->planes = planes;
  ibuf->ftype = IMB_FTYPE_PNG;
  /* The '15' means, set compression to low ratio but not time consuming. */
  ibuf->foptions.quality = 15;
  /* float option, is set to other values when buffers get assigned. */
  ibuf->channels = 4;
  /* IMB_DPI_DEFAULT -> pixels-per-meter. */
  ibuf->ppm[0] = ibuf->ppm[1] = IMB_DPI_DEFAULT / 0.0254f;

  if (flags & IB_rect) {
    if (imb_addrectImBuf(ibuf) == false) {
      return false;
    }
  }

  if (flags & IB_rectfloat) {
    if (imb_addrectfloatImBuf(ibuf, ibuf->channels) == false) {
      return false;
    }
  }

  if (flags & IB_zbuf) {
    if (addzbufImBuf(ibuf) == false) {
      return false;
    }
  }

  if (flags & IB_zbuffloat) {
    if (addzbuffloatImBuf(ibuf) == false) {
      return false;
    }
  }

  /* assign default spaces */
  colormanage_imbuf_set_default_spaces(ibuf);

  return true;
}

ImBuf *imbuf_dupImBuf(const ImBuf *ibuf1)
{
  ImBuf *ibuf2, tbuf;
  int flags = 0;
  int a, x, y;

  if (ibuf1 == nullptr) {
    return nullptr;
  }

  if (ibuf1->rect) {
    flags |= IB_rect;
  }
  if (ibuf1->rect_float) {
    flags |= IB_rectfloat;
  }
  if (ibuf1->zbuf) {
    flags |= IB_zbuf;
  }
  if (ibuf1->zbuf_float) {
    flags |= IB_zbuffloat;
  }

  x = ibuf1->x;
  y = ibuf1->y;

  ibuf2 = imbuf_allocImBuf(x, y, ibuf1->planes, flags);
  if (ibuf2 == nullptr) {
    return nullptr;
  }

  if (flags & IB_rect) {
    memcpy(ibuf2->rect, ibuf1->rect, size_t(x) * y * sizeof(int));
  }

  if (flags & IB_rectfloat) {
    memcpy(ibuf2->rect_float, ibuf1->rect_float, size_t(ibuf1->channels) * x * y * sizeof(float));
  }

  if (flags & IB_zbuf) {
    memcpy(ibuf2->zbuf, ibuf1->zbuf, size_t(x) * y * sizeof(int));
  }

  if (flags & IB_zbuffloat) {
    memcpy(ibuf2->zbuf_float, ibuf1->zbuf_float, size_t(x) * y * sizeof(float));
  }

  if (ibuf1->encodedbuffer) {
    ibuf2->encodedbuffersize = ibuf1->encodedbuffersize;
    if (imb_addencodedbufferImBuf(ibuf2) == false) {
      imbuf_freeImBuf(ibuf2);
      return nullptr;
    }

    memcpy(ibuf2->encodedbuffer, ibuf1->encodedbuffer, ibuf1->encodedsize);
  }

  /* silly trick to copy the entire contents of ibuf1 struct over to ibuf */
  tbuf = *ibuf1;

  /* fix pointers */
  tbuf.rect = ibuf2->rect;
  tbuf.rect_float = ibuf2->rect_float;
  tbuf.encodedbuffer = ibuf2->encodedbuffer;
  tbuf.zbuf = ibuf2->zbuf;
  tbuf.zbuf_float = ibuf2->zbuf_float;
  for (a = 0; a < IMB_MIPMAP_LEVELS; a++) {
    tbuf.mipmap[a] = nullptr;
  }
  tbuf.dds_data.data = nullptr;

  /* set malloc flag */
  tbuf.mall = ibuf2->mall;
  tbuf.refcounter = 0;

  /* for now don't duplicate metadata */
  tbuf.metadata = nullptr;

  tbuf.display_buffer_flags = nullptr;
  tbuf.colormanage_cache = nullptr;

  *ibuf2 = tbuf;

  return ibuf2;
}

size_t imbuf_get_rect_len(const ImBuf *ibuf)
{
  return size_t(ibuf->x) * size_t(ibuf->y);
}

size_t imbuf_get_size_in_memory(ImBuf *ibuf)
{
  int a;
  size_t size = 0, channel_size = 0;

  size += sizeof(ImBuf);

  if (ibuf->rect) {
    channel_size += sizeof(char);
  }

  if (ibuf->rect_float) {
    channel_size += sizeof(float);
  }

  size += channel_size * ibuf->x * ibuf->y * ibuf->channels;

  if (ibuf->miptot) {
    for (a = 0; a < ibuf->miptot; a++) {
      if (ibuf->mipmap[a]) {
        size += imbuf_get_size_in_memory(ibuf->mipmap[a]);
      }
    }
  }

  return size;
}
