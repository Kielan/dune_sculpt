/* Util fns for var size bit-masks. */
#include <limits.h>
#include <string.h>

#include "lib_bitmap.h"
#include "lib_math_bits.h"
#include "lib_utildefines.h"

void lib_bitmap_set_all(LibBitmap *bitmap, bool set, size_t bits)
{
  memset(bitmap, set ? UCHAR_MAX : 0, LIB_BITMAP_SIZE(bits));
}

void lib_bitmap_flip_all(LibBitmap *bitmap, size_t bits)
{
  size_t blocks_num = _BITMAP_NUM_BLOCKS(bits);
  for (size_t i = 0; i < blocks_num; i++) {
    bitmap[i] ^= ~(BLI_bitmap)0;
  }
}

void lib_bitmap_copy_all(LibBitmap *dst, const LibBitmap *src, size_t bits)
{
  memcpy(dst, src, LIB_BITMAP_SIZE(bits));
}

void lib_bitmap_and_all(LibBitmap *dst, const LibBitmap *src, size_t bits)
{
  size_t blocks_num = _BITMAP_NUM_BLOCKS(bits);
  for (size_t i = 0; i < blocks_num; i++) {
    dst[i] &= src[i];
  }
}

void lib_bitmap_or_all(LibBitmap *dst, const LibBitmap *src, size_t bits)
{
  size_t blocks_num = _BITMAP_NUM_BLOCKS(bits);
  for (size_t i = 0; i < blocks_num; i++) {
    dst[i] |= src[i];
  }
}

int lib_bitmap_find_first_unset(const LibBitmap *bitmap, const size_t bits)
{
  const size_t blocks_num = _BITMAP_NUM_BLOCKS(bits);
  int result = -1;
  /* Skip over completely set blocks. */
  int index = 0;
  while (index < blocks_num && bitmap[index] == ~0u) {
    index++;
  }
  if (index < blocks_num) {
    /* Found a partially used block: find the lowest unused bit. */
    const uint m = ~bitmap[index];
    lib_assert(m != 0);
    const uint bit_index = bitscan_forward_uint(m);
    result = bit_index + (index << _BITMAP_POWER);
  }
  return result;
}
