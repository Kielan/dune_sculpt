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
