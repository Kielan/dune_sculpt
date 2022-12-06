#pragma once

/**
 * Defines:
 * - #LIB_assert
 * - #LIB_STATIC_ASSERT
 * - #LIB_STATIC_ASSERT_ALIGN
 */

/* Utility functions. */

void _LIB_assert_print_pos(const char *file, int line, const char *function, const char *id);
void _LIB_assert_print_extra(const char *str);
void _LIB_assert_print_backtrace(void);
void _LIB_assert_abort(void);
void _LIB_assert_unreachable_print(const char *file, int line, const char *function);

#ifndef NDEBUG
/* _LIB_ASSERT_PRINT_POS */
#  if defined(__GNUC__)
#    define _LIB_ASSERT_PRINT_POS(a) _LIB_assert_print_pos(__FILE__, __LINE__, __func__, #    a)
#  elif defined(_MSC_VER)
#    define _LIB_ASSERT_PRINT_POS(a) _LIB_assert_print_pos(__FILE__, __LINE__, __func__, #    a)
#  else
#    define _LIB_ASSERT_PRINT_POS(a) _LIB_assert_print_pos(__FILE__, __LINE__, "<?>", #    a)
#  endif
/* _LIB_ASSERT_ABORT */
#  ifdef WITH_ASSERT_ABORT
#    define _LIB_ASSERT_ABORT _LIB_assert_abort
#  else
#    define _LIB_ASSERT_ABORT() (void)0
#  endif
/* LIB_assert */
#  define LIB_assert(a) \
    (void)((!(a)) ? ((_LIB_assert_print_backtrace(), \
                      _LIB_ASSERT_PRINT_POS(a), \
                      _LIB_ASSERT_ABORT(), \
                      NULL)) : \
                    NULL)
/** A version of #LIB_assert() to pass an additional message to be printed on failure */
#else
#  define LIB_assert(a) ((void)0)
#  define LIB_assert_msg(a, msg) ((void)0)
#endif

#if defined(_MSC_VER)
/* Visual Studio */
#  if (_MSC_VER > 1910) && !defined(__clang__)
#    define LIB_STATIC_ASSERT(a, msg) static_assert(a, msg);
#  else
#    define LIB_STATIC_ASSERT(a, msg) _STATIC_ASSERT(a);
#  endif
#elif defined(__COVERITY__)
/* Workaround error with coverity */
#  define LIB_STATIC_ASSERT(a, msg)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
/* C11 */
#  define LIB_STATIC_ASSERT(a, msg) _Static_assert(a, msg);
#else
/* Old unsupported compiler */
#  define LIB_STATIC_ASSERT(a, msg)
#endif

#define LIB_STATIC_ASSERT_ALIGN(st, align) \
  LIB_STATIC_ASSERT((sizeof(st) % (align) == 0), "Structure must be strictly aligned")

/**
 * Indicates that this line of code should never be executed. If it is reached, it will abort in
 * debug builds and print an error in release builds.
 */
#define LIB_assert_unreachable() \
  { \
    _LIB_assert_unreachable_print(__FILE__, __LINE__, __func__); \
    LIB_assert_msg(0, "This line of code is marked to be unreachable."); \
  } \
  ((void)0)
