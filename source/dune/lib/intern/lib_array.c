/* A (mainly) macro array lib.
 *
 * This is an array lib, used to manage array (re)allocation.
 *
 * This is primarily accessed via macros,
 * fns are used to implement some of the internals.
 *
 * Example usage:
 *
 * \code{.c}
 * int *arr = NULL;
 * lib_array_declare(arr);
 * int i;
 *
 * for (i = 0; i < 10; i++) {
 *     lib_array_grow_one(arr);
 *     arr[i] = something;
 * }
 * lib_array_free(arr);
 * \endcode
 *
 * Arrays are over allocated, so each reallocation the array size is doubled.
 * In situations where contiguous array access isn't needed,
 * other solutions for alloc are available.
 * Consider using on of: lib_memarena.c, lib_mempool.c, lib_stack.c */

#include <string.h>

#include "lib_array.h"

#include "lib_sys_types.h"

#include "mem_guardedalloc.h"

void _lib_array_grow_fn(void **arr_p,
                          const void *arr_static,
                          const int sizeof_arr_p,
                          const int arr_len,
                          const int num,
                          const char *alloc_str)
{
  void *arr = *arr_p;
  void *arr_tmp;

  arr_tmp = mem_malloc(sizeof_arr_p * ((num < arr_len) ? (arr_len * 2 + 2) : (arr_len + num)),
                        alloc_str);

  if (arr) {
    memcpy(arr_tmp, arr, sizeof_arr_p * arr_len);

    if (arr != arr_static) {
      mem_free(arr);
    }
  }

  *arr_p = arr_tmp;
}
