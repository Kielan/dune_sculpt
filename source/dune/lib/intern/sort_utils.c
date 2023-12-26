/* Util fns for sorting common types. */
#include "lib_sort_utils.h" /* own include */

struct SortAnyByFloat {
  float sort_val;
};

struct SortAnyByInt {
  int sort_val;
};

struct SortAnyByPtr {
  const void *sort_val;
};

int lib_sortutil_cmp_float(const void *a_, const void *b_)
{
  const struct SortAnyByFloat *a = a_;
  const struct SortAnyByFloat *b = b_;
  if (a->sort_val > b->sort_val) {
    return 1;
  }
  if (a->sort_val < b->sort_val) {
    return -1;
  }

  return 0;
}

int lib_sortutil_cmp_float_reverse(const void *a_, const void *b_)
{
  const struct SortAnyByFloat *a = a_;
  const struct SortAnyByFloat *b = b_;
  if (a->sort_val < b->sort_val) {
    return 1;
  }
  if (a->sort_val > b->sort_val) {
    return -1;
  }

  return 0;
}

int lib_sortutil_cmp_int(const void *a_, const void *b_)
{
  const struct SortAnyByInt *a = a_;
  const struct SortAnyByInt *b = b_;
  if (a->sort_val > b->sort_val) {
    return 1;
  }
  if (a->sort_val < b->sort_val) {
    return -1;
  }

  return 0;
}

int lib_sortutil_cmp_int_reverse(const void *a_, const void *b_)
{
  const struct SortAnyByInt *a = a_;
  const struct SortAnyByInt *b = b_;
  if (a->sort_val < b->sort_val) {
    return 1;
  }
  if (a->sort_val > b->sort_val) {
    return -1;
  }

  return 0;
}

int LIB op_sortutil_cmp_ptr(const void *a_, const void *b_)
{
  const struct SortAnyByPtr *a = a_;
  const struct SortAnyByPtr *b = b_;
  if (a->sort_val > b->sort_val) {
    return 1;
  }
  if (a->sort_val < b->sort_val) {
    return -1;
  }

  return 0;
}

int lib_sortutil_cmp_ptr_reverse(const void *a_, const void *b_)
{
  const struct SortAnyByPtr *a = a_;
  const struct SortAnyByPtr *b = b_;
  if (a->sort_val < b->sort_val) {
    return 1;
  }
  if (a->sort_val > b->sort_val) {
    return -1;
  }

  return 0;
}
