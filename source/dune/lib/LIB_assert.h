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
