/**
 * STRUCTS
 *
 * Group generic defines for all structs headers may use in this file.
 */

#pragma once

/* structs ignores */
#ifdef STRUCTS_DEPRECATED_ALLOW
/* allow use of deprecated items */
#  define STRUCTS_DEPRECATED
#else
#  ifndef STRUCTS_DEPRECATED
#    ifdef __GNUC__
#      define STRUCTS_DEPRECATED __attribute__((deprecated))
#    else
/* TODO: MSVC & others. */
#      define STRUCTS_DEPRECATED
#    endif
#  endif
#endif

#ifdef __GNUC__
#  define STRUCTS_PRIVATE_ATTR __attribute__((deprecated))
#else
#  define STRUCTS_PRIVATE_ATTR
#endif

/* poison pragma */
#ifdef STRUCTS_DEPRECATED_ALLOW
#  define STRUCTS_DEPRECATED_GCC_POISON 0
#else
/* enable the pragma if we can */
#  ifdef __GNUC__
#    define STRUCTS_DEPRECATED_GCC_POISON 1
#  else
#    define STRUCTS_DEPRECATED_GCC_POISON 0
#  endif
#endif

/* hrmf, we need a better include then this */
#include "../dunelib/LIB_sys_types.h" /* needed for int64_t only! */

/* non-id name variables should use this length */
#define MAX_NAME 64
