/* file
 * ingroup lib
 *
 * A general (pointer -> pointer) chaining hash table
 * for 'Abstract Data Types' (known as an ADT Hash Table).
 */

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct Entry {
  struct Entry *next;

  void *key;
} Entry;

struct GHash {
  GHashHashFP hashfp;
  GHashCmpFP cmpfp;

  Entry **buckets;
  struct LIB_mempool *entrypool;
  uint nbuckets;
  uint limit_grow, limit_shrink;
#ifdef GHASH_USE_MODULO_BUCKETS
  uint cursize, size_min;
#else
  uint bucket_mask, bucket_bit, bucket_bit_min;
#endif

  uint nentries;
  uint flag;
};

/**
 * Expand buckets to the next size up or down.
 */
static void ghash_buckets_resize(GHash *gh, const uint nbuckets)
{
  Entry **buckets_old = gh->buckets;
  Entry **buckets_new;
  const uint nbuckets_old = gh->nbuckets;
  uint i;

  BLI_assert((gh->nbuckets != nbuckets) || !gh->buckets);
  //  printf("%s: %d -> %d\n", __func__, nbuckets_old, nbuckets);

  gh->nbuckets = nbuckets;
#ifdef GHASH_USE_MODULO_BUCKETS
#else
  gh->bucket_mask = nbuckets - 1;
#endif

  buckets_new = (Entry **)MEM_callocN(sizeof(*gh->buckets) * gh->nbuckets, __func__);

  if (buckets_old) {
    if (nbuckets > nbuckets_old) {
      for (i = 0; i < nbuckets_old; i++) {
        for (Entry *e = buckets_old[i], *e_next; e; e = e_next) {
          const uint hash = ghash_entryhash(gh, e);
          const uint bucket_index = ghash_bucket_index(gh, hash);
          e_next = e->next;
          e->next = buckets_new[bucket_index];
          buckets_new[bucket_index] = e;
        }
      }
    }
    else {
      for (i = 0; i < nbuckets_old; i++) {
#ifdef GHASH_USE_MODULO_BUCKETS
        for (Entry *e = buckets_old[i], *e_next; e; e = e_next) {
          const uint hash = ghash_entryhash(gh, e);
          const uint bucket_index = ghash_bucket_index(gh, hash);
          e_next = e->next;
          e->next = buckets_new[bucket_index];
          buckets_new[bucket_index] = e;
        }
#else
        /* No need to recompute hashes in this case, since our mask is just smaller,
         * all items in old bucket 'i' will go in same new bucket (i & new_mask)! */
        const uint bucket_index = ghash_bucket_index(gh, i);
        LIB_assert(!buckets_old[i] ||
                   (bucket_index == ghash_bucket_index(gh, ghash_entryhash(gh, buckets_old[i]))));
        Entry *e;
        for (e = buckets_old[i]; e && e->next; e = e->next) {
          /* pass */
        }
        if (e) {
          e->next = buckets_new[bucket_index];
          buckets_new[bucket_index] = buckets_old[i];
        }
#endif
      }
    }
  }

  gh->buckets = buckets_new;
  if (buckets_old) {
    MEM_freeN(buckets_old);
  }
}


/**
 * Check if the number of items in the GHash is large enough to require more buckets,
 * or small enough to require less buckets, and resize \a gh accordingly.
 */
static void ghash_buckets_expand(GHash *gh, const uint nentries, const bool user_defined)
{
  uint new_nbuckets;

  if (LIKELY(gh->buckets && (nentries < gh->limit_grow))) {
    return;
  }

  new_nbuckets = gh->nbuckets;

#ifdef GHASH_USE_MODULO_BUCKETS
  while ((nentries > gh->limit_grow) && (gh->cursize < GHASH_MAX_SIZE - 1)) {
    new_nbuckets = hashsizes[++gh->cursize];
    gh->limit_grow = GHASH_LIMIT_GROW(new_nbuckets);
  }
#else
  while ((nentries > gh->limit_grow) && (gh->bucket_bit < GHASH_BUCKET_BIT_MAX)) {
    new_nbuckets = 1u << ++gh->bucket_bit;
    gh->limit_grow = GHASH_LIMIT_GROW(new_nbuckets);
  }
#endif

  if (user_defined) {
#ifdef GHASH_USE_MODULO_BUCKETS
    gh->size_min = gh->cursize;
#else
    gh->bucket_bit_min = gh->bucket_bit;
#endif
  }

  if ((new_nbuckets == gh->nbuckets) && gh->buckets) {
    return;
  }

  gh->limit_grow = GHASH_LIMIT_GROW(new_nbuckets);
  gh->limit_shrink = GHASH_LIMIT_SHRINK(new_nbuckets);
  ghash_buckets_resize(gh, new_nbuckets);
}

/**
 * Clear and reset a gh buckets, reserve again buckets for given number of entries.
 */
LIB_INLINE void ghash_buckets_reset(GHash *gh, const uint nentries)
{
  MEM_SAFE_FREE(gh->buckets);

#ifdef GHASH_USE_MODULO_BUCKETS
  gh->cursize = 0;
  gh->size_min = 0;
  gh->nbuckets = hashsizes[gh->cursize];
#else
  gh->bucket_bit = GHASH_BUCKET_BIT_MIN;
  gh->bucket_bit_min = GHASH_BUCKET_BIT_MIN;
  gh->nbuckets = 1u << gh->bucket_bit;
  gh->bucket_mask = gh->nbuckets - 1;
#endif

  gh->limit_grow = GHASH_LIMIT_GROW(gh->nbuckets);
  gh->limit_shrink = GHASH_LIMIT_SHRINK(gh->nbuckets);

  gh->nentries = 0;

  ghash_buckets_expand(gh, nentries, (nentries != 0));
}

static GHash *ghash_new(GHashHashFP hashfp,
                        GHashCmpFP cmpfp,
                        const char *info,
                        const uint nentries_reserve,
                        const uint flag)
{
  GHash *gh = MEM_mallocN(sizeof(*gh), info);

  gh->hashfp = hashfp;
  gh->cmpfp = cmpfp;

  gh->buckets = NULL;
  gh->flag = flag;

  ghash_buckets_reset(gh, nentries_reserve);
  gh->entrypool = LIB_mempool_create(
      GHASH_ENTRY_SIZE(flag & GHASH_FLAG_IS_GSET), 64, 64, LIB_MEMPOOL_NOP);

  return gh;
}

/* Public API */

/**
 * Creates a new, empty GHash.
 *
 * param hashfp: Hash callback.
 * param cmpfp: Comparison callback.
 * param info: Identifier string for the GHash.
 * param nentries_reserve: Optionally reserve the number of members that the hash will hold.
 * Use this to avoid resizing buckets if the size is known or can be closely approximated.
 * return  An empty GHash.
 */
GHash *LIB_ghash_new_ex(GHashHashFP hashfp,
                        GHashCmpFP cmpfp,
                        const char *info,
                        const uint nentries_reserve)
{
  return ghash_new(hashfp, cmpfp, info, nentries_reserve, 0);
}
