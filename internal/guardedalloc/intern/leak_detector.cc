#include <cstdio> /* Needed for `printf` on WIN32/APPLE. */
#include <cstdlib>

#include "mem_guardedalloc.h"
#include "mallocn_intern.h"

bool leak_detector_has_run = false;
char free_after_leak_detection_message[] =
    "Freeing memory after the leak detector has run. This can happen when using "
    "static variables in C++ that are defined outside of functions. To fix this "
    "error, use the 'construct on first use' idiom.";

namespace {

bool fail_on_memleak = false;
bool ignore_memleak = false;

class MemLeakPrinter {
 public:
  ~MemLeakPrinter()
  {
    if (ignore_memleak) {
      return;
    }
    leak_detector_has_run = true;
    const uint leaked_blocks = mem_get_memory_blocks_in_use();
    if (leaked_blocks == 0) {
      return;
    }
    const size_t mem_in_use = mem_get_memory_in_use();
    printf("Error: Not freed memory blocks: %u, total unfreed memory %f MB\n",
           leaked_blocks,
           (double)mem_in_use / 1024 / 1024);
    mem_printmemlist();

    if (fail_on_memleak) {
      /* There are many other ways to change the exit code to failure here:
       * - Make the destructor noexcept(false) and throw an exception.
       * - Call exit(EXIT_FAILURE).
       * - Call terminate().
       */
      abort();
    }
  }
};
}  // namespace

void mem_init_memleak_detection()
{
  /**
   * This variable is constructed when this function is first called. This should happen as soon as
   * possible when the program starts.
   *
   * It is destructed when the program exits. During destruction, it will print information about
   * leaked memory blocks. Static variables are destructed in reversed order of their
   * construction. Therefore, all static variables that own memory have to be constructed after
   * this function has been called.
   */
  static MemLeakPrinter printer;
}

void mem_use_memleak_detection(bool enabled)
{
  ignore_memleak = !enabled;
}

void mem_enable_fail_on_memleak()
{
  fail_on_memleak = true;
}
