/* Generic mem manipulation API.
 * This is to extend on existing fns
 * such as `memcpy` & `memcmp`. */
#include <string.h>

#include "lib_sys_types.h"
#include "lib_utildefines.h"

#include "lib_mem_utils.h"

#include "lib_strict_flags.h"

bool lib_mem_is_zero(const void *arr, const size_t arr_size)
{
  const char *arr_byte = arr;
  const char *arr_end = (const char *)arr + arr_size;

  while ((arr_byte != arr_end) && (*arr_byte == 0)) {
    arr_byte++;
  }

  return (arr_byte == arr_end);
}
