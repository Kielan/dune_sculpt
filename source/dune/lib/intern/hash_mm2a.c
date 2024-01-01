/* Fns to compute Murmur2A hash key.
 * Very fast hash gen int32 result w few collisions and good repartition.
 *
 * See also:
 * ref implement:
 * - https://smhasher.googlecode.com/svn-history/r130/trunk/MurmurHash2.cpp
 * - http://programmers.stackexchange.com/questions/49550
 *
 * warning Dont store that hash in files or ect, its not endian-agnostic,
 * should only use for tmp data. */

#include "lib_compiler_attrs.h"

#include "lib_hash_mm2a.h" /* own include */

/* Helpers. */
#define MM2A_M 0x5bd1e995

#define MM2A_MIX(h, k) \
  { \
    (k) *= MM2A_M; \
    (k) ^= (k) >> 24; \
    (k) *= MM2A_M; \
    (h) = ((h)*MM2A_M) ^ (k); \
  } \
  (void)0

#define MM2A_MIX_FINALIZE(h) \
  { \
    (h) ^= (h) >> 13; \
    (h) *= MM2A_M; \
    (h) ^= (h) >> 15; \
  } \
  (void)0

static void mm2a_mix_tail(LibHashMurmur2A *mm2, const uchar **data, size_t *len)
{
  while (*len && ((*len < 4) || mm2->count)) {
    mm2->tail |= (uint32_t)(**data) << (mm2->count * 8);

    mm2->count++;
    (*len)--;
    (*data)++;

    if (mm2->count == 4) {
      MM2A_MIX(mm2->hash, mm2->tail);
      mm2->tail = 0;
      mm2->count = 0;
    }
  }
}

void lib_hash_mm2a_init(LibHashMurmur2A *mm2, uint32_t seed)
{
  mm2->hash = seed;
  mm2->tail = 0;
  mm2->count = 0;
  mm2->size = 0;
}

void lib_hash_mm2a_add(LibHashMurmur2A *mm2, const uchar *data, size_t len)
{
  mm2->size += (uint32_t)len;

  mm2a_mix_tail(mm2, &data, &len);

  for (; len >= 4; data += 4, len -= 4) {
    uint32_t k = *(const uint32_t *)data;

    MM2A_MIX(mm2->hash, k);
  }

  mm2a_mix_tail(mm2, &data, &len);
}

void lib_hash_mm2a_add_int(LibHashMurmur2A *mm2, int data)
{
  lib_hash_mm2a_add(mm2, (const uchar *)&data, sizeof(data));
}

uint32_t lib_hash_mm2a_end(LibHashMurmur2A *mm2)
{
  MM2A_MIX(mm2->hash, mm2->tail);
  MM2A_MIX(mm2->hash, mm2->size);

  MM2A_MIX_FINALIZE(mm2->hash);

  return mm2->hash;
}

uint32_t lib_hash_mm2(const uchar *data, size_t len, uint32_t seed)
{
  /* Init the hash to a 'random' val */
  uint32_t h = seed ^ len;

  /* Mix 4 bytes at a time into the hash */
  for (; len >= 4; data += 4, len -= 4) {
    uint32_t k = *(uint32_t *)data;

    MM2A_MIX(h, k);
  }

  /* Handle the last few bytes of the input array */
  switch (len) {
    case 3:
      h ^= data[2] << 16;
      ATTR_FALLTHROUGH;
    case 2:
      h ^= data[1] << 8;
      ATTR_FALLTHROUGH;
    case 1:
      h ^= data[0];
      h *= MM2A_M;
  }

  /* Do a few final mixes of the hash to ensure the last few bytes are well-incorporated. */
  MM2A_MIX_FINALIZE(h);

  return h;
}
