/* Helper fns for lib_assert.h header. */
#include "lib_assert.h" /* Own include. */
#include "lib_system.h"

#include <stdio.h>
#include <stdlib.h>

void _lib_assert_print_pos(const char *file, const int line, const char *fn, const char *id)
{
  fprintf(stderr, "lib_assert failed: %s:%d, %s(), at \'%s\'\n", file, line, fn, id);
}

void _lib_assert_print_extra(const char *str)
{
  fprintf(stderr, "  %s\n", str);
}

void _lib_assert_unreachable_print(const char *file, const int line, const char *fn)
{
  fprintf(stderr, "Code marked as unreachable has been ex. Please report this as a bug.\n");
  fprintf(stderr, "Error found at %s:%d in %s.\n", file, line, fn);
}

void _lib_assert_print_backtrace(void)
{
#ifndef NDEBUG
  lib_system_backtrace(stderr);
#endif
}

void _lib_assert_abort(void)
{
  /* Wrap to remove 'noreturn' attr bc this suppresses missing return statements,
   * allowing changes to debug builds to accidentally to break release builds.
   *
   * For example `lib_assert(0);` at the end of a fn that returns a val,
   * will hide that it's missing a return. */

  abort();
}
