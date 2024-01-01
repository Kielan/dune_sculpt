/* A (mainly) macro array lib.
 *
 * An array lib: used to manage array (re)alloc.
 * Primarily accessed via macros,
 * fns used to implement some of the internals.
 * Example usage:
 * int *arr = NULL;
 * lib_array_declare(arr);
 * int i;
 *
 * for (i = 0; i < 10; i++) {
 *     lib_arr_grow_one(arr);
 *     arr[i] = something;
 * }
 * lib_arr_free(arr);
 *
 * Arrs are over alloc, so each realloc the arr size is doubled.
 * In situations where contiguous arr access isn't needed,
 * other solutions for alloc are avail.
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
