#pragma once

/** GHash is a hash-map implementation (unordered key, value pairs).
 * This is also used to implement a 'set' (see GSet below). */

#include "lib_compiler_attrs.h"
#include "lib_compiler_compat.h"
#include "lib_sys_types.h" /* for bool */

#define _GHASH_INTERNAL_ATTR
#ifndef GHASH_INTERNAL_API
#  ifdef __GNUC__
#    undef _GHASH_INTERNAL_ATTR
#    define _GHASH_INTERNAL_ATTR __attribute__((deprecated)) /* not deprecated, just private. */
#  endif
#endif

typedef unsigned int (*GHashHashFP)(const void *key);
/** returns false when equal */
typedef bool (*GHashCmpFP)(const void *a, const void *b);
typedef void (*GHashKeyFreeFP)(void *key);
typedef void (*GHashValFreeFP)(void *val);
typedef void *(*GHashKeyCopyFP)(const void *key);
typedef void *(*GHashValCopyFP)(const void *val);

typedef struct GHash GHash;

typedef struct GHashIterator {
  GHash *gh;
  struct Entry *curEntry;
  unsigned int curBucket;
} GHashIterator;

typedef struct GHashIterState {
  unsigned int curr_bucket _GHASH_INTERNAL_ATTR;
} GHashIterState;

enum {
  GHASH_FLAG_ALLOW_DUPES = (1 << 0),  /* Only checked for in debug mode */
  GHASH_FLAG_ALLOW_SHRINK = (1 << 1), /* Allow to shrink buckets' size. */

#ifdef GHASH_INTERNAL_API
  /* Internal usage only */
  /* Whether the GHash is actually used as GSet (no value storage). */
  GHASH_FLAG_IS_GSET = (1 << 16),
#endif
};
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
GHash *lib_ghash_new_ex(GHashHashFP hashfp,
                        GHashCmpFP cmpfp,
                        const char *info,
                        unsigned int nentries_reserve) ATTR_MALLOC ATTR_WARN_UNUSED_RESULT;
/** Wraps Lib_ghash_new_ex with zero entries reserved. */
GHash *lib_ghash_new(GHashHashFP hashfp,
                     GHashCmpFP cmpfp,
                     const char *info) ATTR_MALLOC ATTR_WARN_UNUSED_RESULT;
/** Copy given GHash. Keys and values are also copied if relevant callback is provided,
 * else pointers remain the same. */
GHash *lib_ghash_copy(const GHash *gh,
                      GHashKeyCopyFP keycopyfp,
                      GHashValCopyFP valcopyfp) ATTR_MALLOC ATTR_WARN_UNUSED_RESULT;
/**
 * Frees the GHash and its members.
 *
 * param gh: The GHash to free.
 * param keyfreefp: Optional callback to free the key.
 * param valfreefp: Optional callback to free the value.
 */
void lib_ghash_free(GHash *gh, GHashKeyFreeFP keyfreefp, GHashValFreeFP valfreefp);
/** Reserve given amount of entries (resize gh accordingly if needed). */
void lib_ghash_reserve(GHash *gh, unsigned int nentries_reserve);
/**
 * Insert a key/value pair into the \a gh.
 *
 * note Duplicates are not checked,
 * the caller is expected to ensure elements are unique unless
 * GHASH_FLAG_ALLOW_DUPES flag is set.
 */
void lib_ghash_insert(GHash *gh, void *key, void *val);
/**
 * Inserts a new value to a key that may already be in ghash.
 *
 * Avoids lib_ghash_remove, lib_ghash_insert calls (double lookups)
 *
 * returns true if a new key has been added.
 */
bool lib_ghash_reinsert(
    GHash *gh, void *key, void *val, GHashKeyFreeFP keyfreefp, GHashValFreeFP valfreefp);
/**
 * Replaces the key of an item in the gh.
 *
 * Use when a key is re-allocated or its memory location is changed.
 *
 * returns The previous key or NULL if not found, the caller may free if it's needed.
 */
void *lib_ghash_replace_key(GHash *gh, void *key);
/**
 * Lookup the value of key in gh.
 *
 * param key: The key to lookup.
 * returns the value for \a key or NULL.
 *
 * note When NULL is a valid value, use #LIB_ghash_lookup_p to differentiate a missing key
 * from a key with a NULL value. (Avoids calling #LIB_ghash_haskey before #LIB_ghash_lookup)
 */
void *lib_ghash_lookup(const GHash *gh, const void *key) ATTR_WARN_UNUSED_RESULT;
/** A version of lib_ghash_lookup which accepts a fallback argument. */
void *lib_ghash_lookup_default(const GHash *gh,
                               const void *key,
                               void *val_default) ATTR_WARN_UNUSED_RESULT;
/**
 * Lookup a pointer to the value of key in gh.
 *
 * param key: The key to lookup.
 * returns the pointer to value for \a key or NULL.
 *
 * note This has 2 main benefits over lib_ghash_lookup.
 * - A NULL return always means that key isn't in gh.
 * - The value can be modified in-place without further function calls (faster).
 */
void **lib_ghash_lookup_p(GHash *gh, const void *key) ATTR_WARN_UNUSED_RESULT;
/**
 * Ensure key is exists in gh.
 *
 * This handles the common situation where the caller needs ensure a key is added to \a gh,
 * constructing a new value in the case the key isn't found.
 * Otherwise use the existing value.
 *
 * Such situations typically incur multiple lookups, however this function
 * avoids them by ensuring the key is added,
 * returning a pointer to the value so it can be used or initialized by the caller.
 *
 * returns true when the value didn't need to be added.
 * (when false, the caller _must_ initialize the value).
 */
bool lib_ghash_ensure_p(GHash *gh, void *key, void ***r_val) ATTR_WARN_UNUSED_RESULT;
/**
 * A version of lib_ghash_ensure_p that allows caller to re-assign the key.
 * Typically used when the key is to be duplicated.
 *
 * warning Caller _must_ write to \a r_key when returning false.
 */
bool lib_ghash_ensure_p_ex(GHash *gh, const void *key, void ***r_key, void ***r_val)
    ATTR_WARN_UNUSED_RESULT;
/**
 * Remove key from gh, or return false if the key wasn't found.
 *
 * param key: The key to remove.
 * param keyfreefp: Optional callback to free the key.
 * param valfreefp: Optional callback to free the value.
 * return true if \a key was removed from \a gh.
 */
bool lib_ghash_remove(GHash *gh,
                      const void *key,
                      GHashKeyFreeFP keyfreefp,
                      GHashValFreeFP valfreefp);
/** Wraps lib_ghash_clear_ex with zero entries reserved. */
void lib_ghash_clear(GHash *gh, GHashKeyFreeFP keyfreefp, GHashValFreeFP valfreefp);
/**
 * Reset gh clearing all entries.
 *
 * param keyfreefp: Optional callback to free the key.
 * param valfreefp: Optional callback to free the value.
 * param nentries_reserve: Optionally reserve the number of members that the hash will hold.
 */
void lib_ghash_clear_ex(GHash *gh,
                        GHashKeyFreeFP keyfreefp,
                        GHashValFreeFP valfreefp,
                        unsigned int nentries_reserve);
/**
 * Remove key from gh, returning the value or NULL if the key wasn't found.
 *
 * param key: The key to remove.
 * param keyfreefp: Optional callback to free the key.
 * return the value of key int gh or NULL.
 */
void *lib_ghash_popkey(GHash *gh,
                       const void *key,
                       GHashKeyFreeFP keyfreefp) ATTR_WARN_UNUSED_RESULT;
/**
 * return true if the key is in gh.
 */
bool lib_ghash_haskey(const GHash *gh, const void *key) ATTR_WARN_UNUSED_RESULT;
/**
 * Remove a random entry from \a gh, returning true
 * if a key/value pair could be removed, false otherwise.
 *
 * param r_key: The removed key.
 * param r_val: The removed value.
 * param state: Used for efficient removal.
 * return true if there was something to pop, false if ghash was already empty.
 */
bool lib_ghash_pop(GHash *gh, GHashIterState *state, void **r_key, void **r_val)
    ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** return size of the GHash. */
unsigned int lib_ghash_len(const GHash *gh) ATTR_WARN_UNUSED_RESULT;
/** Sets a GHash flag. */
void lib_ghash_flag_set(GHash *gh, unsigned int flag);
/**
 * Clear a GHash flag.
 */
void lib_ghash_flag_clear(GHash *gh, unsigned int flag);
/** GHash Iterator */

/**
 * Create a new GHashIterator. The hash table must not be mutated
 * while the iterator is in use, and the iterator will step exactly
 * lib_ghash_len(gh) times before becoming done.
 *
 * param gh: The GHash to iterate over.
 * return Pointer to a new iterator.
 */
GHashIterator *lib_ghashIterator_new(GHash *gh) ATTR_MALLOC ATTR_WARN_UNUSED_RESULT;

/**
 * Init an already allocated GHashIterator. The hash table must not
 * be mutated while the iterator is in use, and the iterator will
 * step exactly lib_ghash_len(gh) times before becoming done.
 *
 * param ghi: The GHashIterator to initialize.
 * param gh: The GHash to iterate over.
 */
void lib_ghashIterator_init(GHashIterator *ghi, GHash *gh);
/**
 * Free a GHashIterator.
 *
 * param ghi: The iterator to free.
 */
void lib_ghashIterator_free(GHashIterator *ghi);
/**
 * Steps the iterator to the next index.
 *
 * param ghi: The iterator.
 */
void LIB_ghashIterator_step(GHashIterator *ghi);

LIB_INLINE void *LIB_ghashIterator_getKey(GHashIterator *ghi) ATTR_WARN_UNUSED_RESULT;
LIB_INLINE void *LIB_ghashIterator_getValue(GHashIterator *ghi) ATTR_WARN_UNUSED_RESULT;
LIB_INLINE void **LIB_ghashIterator_getValue_p(GHashIterator *ghi) ATTR_WARN_UNUSED_RESULT;
LIB_INLINE bool LIB_ghashIterator_done(const GHashIterator *ghi) ATTR_WARN_UNUSED_RESULT;

struct _gh_Entry {
  void *next, *key, *val;
};
LIB_INLINE void *LIB_ghashIterator_getKey(GHashIterator *ghi)
{
  return ((struct _gh_Entry *)ghi->curEntry)->key;
}
LIB_INLINE void *LIB_ghashIterator_getValue(GHashIterator *ghi)
{
  return ((struct _gh_Entry *)ghi->curEntry)->val;
}
LIB_INLINE void **LIB_ghashIterator_getValue_p(GHashIterator *ghi)
{
  return &((struct _gh_Entry *)ghi->curEntry)->val;
}
LIB_INLINE bool LIB_ghashIterator_done(const GHashIterator *ghi)
{
  return !ghi->curEntry;
}
/* disallow further access */
#ifdef __GNUC__
#  pragma GCC poison _gh_Entry
#else
#  define _gh_Entry void
#endif

#define GHASH_ITER(gh_iter_, ghash_) \
  for (LIB_ghashIterator_init(&gh_iter_, ghash_); LIB_ghashIterator_done(&gh_iter_) == false; \
       LIB_ghashIterator_step(&gh_iter_))

#define GHASH_ITER_INDEX(gh_iter_, ghash_, i_) \
  for (LIB_ghashIterator_init(&gh_iter_, ghash_), i_ = 0; \
       LIB_ghashIterator_done(&gh_iter_) == false; \
       LIB_ghashIterator_step(&gh_iter_), i_++)
