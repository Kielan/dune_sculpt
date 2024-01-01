/* Helper fns and implementations of standard data types for #GHash
 * (not its implementation). */

#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_ghash.h" /* own include */
#include "lib_hash_mm2a.h"
#include "lib_utildefines.h"

/* keep last */
#include "lib_strict_flags.h"

/* Generic Key Hash & Comparison Fns */
#if 0
/* works but slower */
uint lib_ghashutil_ptrhash(const void *key)
{
  return (uint)(intptr_t)key;
}
#else
uint lib_ghashutil_ptrhash(const void *key)
{
  /* Based Python3.7's ptr hashing fn. */
  size_t y = (size_t)key;
  /* bottom 3 or 4 bits are likely to be 0; rotate y by 4 to avoid
   * excessive hash collisions for dictionaries and sets */

  /* Unlike Python `sizeof(uint)` is used instead of `sizeof(void *)`,
   * Otherwise casting to 'uint' ignores the upper bits on 64bit platforms. */
  return (uint)(y >> 4) | ((uint)y << (sizeof(uint[8]) - 4));
}
#endif
bool lib_ghashutil_ptrcmp(const void *a, const void *b)
{
  return (a != b);
}

uint lib_ghashutil_uinthash_v4(const uint key[4])
{
  uint hash;
  hash = key[0];
  hash *= 37;
  hash += key[1];
  hash *= 37;
  hash += key[2];
  hash *= 37;
  hash += key[3];
  return hash;
}

uint lib_ghashutil_uinthash_v4_murmur(const uint key[4])
{
  return lib_hash_mm2((const uchar *)key, sizeof(int[4]) /* sizeof(key) */, 0);
}

bool lib_ghashutil_uinthash_v4_cmp(const void *a, const void *b)
{
  return (memcmp(a, b, sizeof(uint[4])) != 0);
}

uint lib_ghashutil_uinthash(uint key)
{
  key += ~(key << 16);
  key ^= (key >> 5);
  key += (key << 3);
  key ^= (key >> 13);
  key += ~(key << 9);
  key ^= (key >> 17);

  return key;
}

uint lib_ghashutil_inthash_p(const void *ptr)
{
  uintptr_t key = (uintptr_t)ptr;

  key += ~(key << 16);
  key ^= (key >> 5);
  key += (key << 3);
  key ^= (key >> 13);
  key += ~(key << 9);
  key ^= (key >> 17);

  return (uint)(key & 0xffffffff);
}

uint lib_ghashutil_inthash_p_murmur(const void *ptr)
{
  uintptr_t key = (uintptr_t)ptr;

  return lib_hash_mm2((const uchar *)&key, sizeof(key), 0);
}

uint lib_ghashutil_inthash_p_simple(const void *ptr)
{
  return PTR_AS_UINT(ptr);
}

bool lib_ghashutil_intcmp(const void *a, const void *b)
{
  return (a != b);
}

size_t lib_ghashutil_combine_hash(size_t hash_a, size_t hash_b)
{
  return hash_a ^ (hash_b + 0x9e3779b9 + (hash_a << 6) + (hash_a >> 2));
}

uint lib_ghashutil_strhash_n(const char *key, size_t n)
{
  const signed char *p;
  uint h = 5381;

  for (p = (const signed char *)key; n-- && *p != '\0'; p++) {
    h = (uint)((h << 5) + h) + (uint)*p;
  }

  return h;
}
uint lib_ghashutil_strhash_p(const void *ptr)
{
  const signed char *p;
  uint h = 5381;

  for (p = ptr; *p != '\0'; p++) {
    h = (uint)((h << 5) + h) + (uint)*p;
  }

  return h;
}
uint lib_ghashutil_strhash_p_murmur(const void *ptr)
{
  const uchar *key = ptr;

  return lib_hash_mm2(key, strlen((const char *)key) + 1, 0);
}
bool lib_ghashutil_strcmp(const void *a, const void *b)
{
  return (a == b) ? false : !STREQ(a, b);
}

GHashPair *lib_ghashutil_pairalloc(const void *first, const void *second)
{
  GHashPair *pair = mem_malloc(sizeof(GHashPair), "GHashPair");
  pair->first = first;
  pair->second = second;
  return pair;
}

uint lib_ghashutil_pairhash(const void *ptr)
{
  const GHashPair *pair = ptr;
  uint hash = lib_ghashutil_ptrhash(pair->first);
  return hash ^ lib_ghashutil_ptrhash(pair->second);
}

bool lib_ghashutil_paircmp(const void *a, const void *b)
{
  const GHashPair *A = a;
  const GHashPair *B = b;

  return ((A->first != B->first) || (A->second != B->second));
}

void lib_ghashutil_pairfree(void *ptr)
{
  mem_free(ptr);
}

/* Convenience GHash Creation Fns */
GHash *lib_ghash_ptr_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_ghash_new_ex(lib_ghashutil_ptrhash, lib_ghashutil_ptrcmp, info, nentries_reserve);
}
GHash *lib_ghash_ptr_new(const char *info)
{
  return lib_ghash_ptr_new_ex(info, 0);
}

GHash *lib_ghash_str_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_ghash_new_ex(lib_ghashutil_strhash_p, BLI_ghashutil_strcmp, info, nentries_reserve);
}
GHash *lib_ghash_str_new(const char *info)
{
  return lib_ghash_str_new_ex(info, 0);
}

GHash *lib_ghash_int_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_ghash_new_ex(BLI_ghashutil_inthash_p, BLI_ghashutil_intcmp, info, nentries_reserve);
}
GHash *lib_ghash_int_new(const char *info)
{
  return lib_ghash_int_new_ex(info, 0);
}

GHash *lib_ghash_pair_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_ghash_new_ex(lib_ghashutil_pairhash, BLI_ghashutil_paircmp, info, nentries_reserve);
}
GHash *lib_ghash_pair_new(const char *info)
{
  return lib_ghash_pair_new_ex(info, 0);
}

/* Convenience GSet Creation Fns */
GSet *lib_gset_ptr_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_gset_new_ex(lib_ghashutil_ptrhash, lib_ghashutil_ptrcmp, info, nentries_reserve);
}
GSet *lib_gset_ptr_new(const char *info)
{
  return lib_gset_ptr_new_ex(info, 0);
}

GSet *lib_gset_str_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_gset_new_ex(lib_ghashutil_strhash_p, BLI_ghashutil_strcmp, info, nentries_reserve);
}
GSet *lib_gset_str_new(const char *info)
{
  return lib_gset_str_new_ex(info, 0);
}

GSet *lib_gset_pair_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_gset_new_ex(lib_ghashutil_pairhash, BLI_ghashutil_paircmp, info, nentries_reserve);
}
GSet *lib_gset_pair_new(const char *info)
{
  return lib_gset_pair_new_ex(info, 0);
}

GSet *lib_gset_int_new_ex(const char *info, const uint nentries_reserve)
{
  return lib_gset_new_ex(lib_ghashutil_inthash_p, BLI_ghashutil_intcmp, info, nentries_reserve);
}
GSet *lib_gset_int_new(const char *info)
{
  return lib_gset_int_new_ex(info, 0);
}
