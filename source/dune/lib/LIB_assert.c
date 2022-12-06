/**
 * Helper functions for LIB_assert.h header.
 */

#include "LIB_assert.h" /* Own include. */
#include "LIB_system.h"

#include <stdio.h>
#include <stdlib.h>

void _LIB_assert_print_pos(const char *file, const int line, const char *function, const char *id)
{
  fprintf(stderr, "LIB_assert failed: %s:%d, %s(), at \'%s\'\n", file, line, function, id);
}

void _LIB_assert_print_extra(const char *str)
{
  fprintf(stderr, "  %s\n", str);
}

void _LIB_assert_unreachable_print(const char *file, const int line, const char *function)
{
  fprintf(stderr, "Code marked as unreachable has been executed. Please report this as a bug.\n");
  fprintf(stderr, "Error found at %s:%d in %s.\n", file, line, function);
}

void _LIB_assert_print_backtrace(void)
{
#ifndef NDEBUG
  LIB_system_backtrace(stderr);
#endif
}

void _LIB_assert_abort(void)
{
  /* Wrap to remove 'noreturn' attribute since this suppresses missing return statements,
   * allowing changes to debug builds to accidentally to break release builds.
   *
   * For example `LIB_assert(0);` at the end of a function that returns a value,
   * will hide that it's missing a return. */

  abort();
}
