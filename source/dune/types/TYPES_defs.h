/**
 * TYPES
 *
 * Group generic defines for all structs headers may use in this file.
 */

#pragma once

/* structs ignores */
#ifdef TYPES_DEPRECATED_ALLOW
/* allow use of deprecated items */
#  define TYPES_DEPRECATED
#else
#  ifndef TYPES_DEPRECATED
#    ifdef __GNUC__
#      define TYPES_DEPRECATED __attribute__((deprecated))
#    else
/* TODO: MSVC & others. */
#      define TYPES_DEPRECATED
#    endif
#  endif
#endif

#ifdef __GNUC__
#  define TYPES_PRIVATE_ATTR __attribute__((deprecated))
#else
#  define TYPES_PRIVATE_ATTR
#endif

/* poison pragma */
#ifdef TYPES_DEPRECATED_ALLOW
#  define TYPES_DEPRECATED_GCC_POISON 0
#else
/* enable the pragma if we can */
#  ifdef __GNUC__
#    define TYPES_DEPRECATED_GCC_POISON 1
#  else
#    define TYPES_DEPRECATED_GCC_POISON 0
#  endif
#endif

/* hrmf, we need a better include then this */
#include "../dunelib/LIB_sys_types.h" /* needed for int64_t only! */

/* non-id name variables should use this length */
#define MAX_NAME 64
