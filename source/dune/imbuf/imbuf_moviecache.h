#pragma once

#include "lib_ghash.h"
#include "lib_utildefines.h"

/* Cache system for movie data - now supports storing ImBufs only
 * Supposed to provide unified cache system for movie clips, sequencer and
 * other movie-related areas */

#ifdef __cplusplus
extern "C" {
#endif

struct ImBuf;
struct MovieCache;

typedef void (*MovieCacheGetKeyDataFP)(void *userkey, int *framenr, int *proxy, int *render_flags);

typedef void *(*MovieCacheGetPriorityDataFP)(void *userkey);
typedef int (*MovieCacheGetItemPriorityFP)(void *last_userkey, void *priority_data);
typedef void (*MovieCachePriorityDeleterFP)(void *priority_data);

void imbuf_moviecache_init(void);
void imbuf_moviecache_destruct(void);

struct MovieCache *imbuf_moviecache_create(const char *name,
                                         int keysize,
                                         GHashHashFP hashfp,
                                         GHashCmpFP cmpfp);
void imbuf_moviecache_set_getdata_cb(struct MovieCache *cache,
                                     MovieCacheGetKeyDataFP getdatafp);
void imbuf_moviecache_set_priority_cb(struct MovieCache *cache,
                                      MovieCacheGetPriorityDataFP getprioritydatafp,
                                      MovieCacheGetItemPriorityFP getitempriorityfp,
                                      MovieCachePriorityDeleterFP prioritydeleterfp);

void imbuf_moviecache_put(struct MovieCache *cache, void *userkey, struct ImBuf *ibuf);
bool imbuf_moviecache_put_if_possible(struct MovieCache *cache, void *userkey, struct ImBuf *ibuf);
struct ImBuf *imbuf_moviecache_get(struct MovieCache *cache, void *userkey, bool *r_is_cached_empty);
void imbuf_moviecache_remove(struct MovieCache *cache, void *userkey);
bool imbuf_moviecache_has_frame(struct MovieCache *cache, void *userkey);
void imbuf_moviecache_free(struct MovieCache *cache);

void imbuf_moviecache_cleanup(struct MovieCache *cache,
                            bool(cleanup_check_cb)(struct ImBuf *ibuf,
                                                   void *userkey,
                                                   void *userdata),
                            void *userdata);

/** Get segments of cached frames. Useful for debugging cache policies. */
void imbuf_moviecache_get_cache_segments(
    struct MovieCache *cache, int proxy, int render_flags, int *r_totseg, int **r_points);

struct MovieCacheIter;
struct MovieCacheIter *imbuf_moviecacheIter_new(struct MovieCache *cache);
void imbuf_moviecacheIter_free(struct MovieCacheIter *iter);
bool imbuf_moviecacheIter_done(struct MovieCacheIter *iter);
void imbuf_moviecacheIter_step(struct MovieCacheIter *iter);
struct ImBuf *imbuf_moviecacheIter_getImBuf(struct MovieCacheIter *iter);
void *imbuf_moviecacheIter_getUserKey(struct MovieCacheIter *iter);

#ifdef __cplusplus
}
#endif
