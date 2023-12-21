#pragma once

/* Struct members on own line. */
/* clang-format off */

/* CacheFile Struct */
#define _TYPES_DEFAULT_CacheFile \
  { \
    .filepath[0] = '\0', \
    .override_frame = false, \
    .frame = 0.0f, \
    .is_seq = false, \
    .scale = 1.0f, \
    .ob_paths ={NULL, NULL}, \
 \
    .type = 0, \
    .handle = NULL, \
    .handle_filepath[0] = '\0', \
    .handle_readers = NULL, \
    .use_prefetch = 1, \
    .prefetch_cache_size = 4096, \
  }

/* clang-format on */
