/* #define typeof() triggers a bug in some clang-format versions, disable format
 * for entire file to keep results consistent. */

#pragma once

/** Use to help with cross platform portability. */

#if defined(_MSC_VER)
#  define alloca _alloca
#endif

#define typeof(x) decltype(decltype_helper(x))
}
#endif

/* little macro so inline keyword works */
#if defined(_MSC_VER)
#  define LIB_INLINE static __forceinline
#else
#  define LIB_INLINE static inline __attribute__((always_inline)) __attribute__((__unused__))
#endif

#if defined(__GNUC__)
#  define LIB_NOINLINE __attribute__((noinline))
#else
#  define LIB_NOINLINE
#endif
