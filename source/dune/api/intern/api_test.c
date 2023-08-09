/* Defines a structure with properties used for array manipulation tests in BPY. */

#include <stdlib.h>
#include <string.h>

#include "api_define.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  ifdef ARRAY_SIZE
#    undef ARRAY_SIZE
#  endif

#  define ARRAY_SIZE 3
#  define DYNAMIC_ARRAY_SIZE 64
#  define MARRAY_DIM [3][4][5]
#  define MARRAY_TOTDIM 3
#  define MARRAY_DIMSIZE 4, 5
#  define MARRAY_SIZE(type) (sizeof(type MARRAY_DIM) / sizeof(type))
#  define DYNAMIC_MARRAY_DIM [3][4][5]
#  define DYNAMIC_MARRAY_SIZE(type) (sizeof(type DYNAMIC_MARRAY_DIM) / sizeof(type))

#  ifdef UNIT_TEST

#    define DEF_VARS(type, prefix) \
      static type prefix##arr[ARRAY_SIZE]; \
      static type prefix##darr[DYNAMIC_ARRAY_SIZE]; \
      static int prefix##darr_len = ARRAY_SIZE; \
      static type prefix##marr MARRAY_DIM; \
      static type prefix##dmarr DYNAMIC_MARRAY_DIM; \
      static int prefix##dmarr_len = sizeof(prefix##dmarr); \
      (void)0

#    define DEF_GET_SET(type, arr) \
      void api_Test_##arr##_get(ApiPtr *ptr, type *values) \
      { \
        memcpy(values, arr, sizeof(arr)); \
      } \
\
      void rna_Test_##arr##_set(ApiPtr *ptr, const type *values) \
      { \
        memcpy(arr, values, sizeof(arr)); \
      } \
      ((void)0)

#    define DEF_GET_SET_LEN(arr, max) \
      static int api_Test_##arr##_get_length(ApiPtr *ptr) \
      { \
        return arr##_len; \
      } \
\
      static int api_Test_##arr##_set_length(ApiPtr *ptr, int length) \
      { \
        if (length > max) { \
          return 0; \
        } \
        arr##_len = length; \
\
        return 1; \
      } \
      ((void)0)

DEF_VARS(float, f);
DEF_VARS(int, i);
DEF_VARS(int, b);

DEF_GET_SET(float, farr);
DEF_GET_SET(int, iarr);
DEF_GET_SET(int, barr);

DEF_GET_SET(float, fmarr);
DEF_GET_SET(int, imarr);
DEF_GET_SET(int, bmarr);

DEF_GET_SET(float, fdarr);
DEF_GET_SET_LEN(fdarr, DYNAMIC_ARRAY_SIZE);
DEF_GET_SET(int, idarr);
DEF_GET_SET_LEN(idarr, DYNAMIC_ARRAY_SIZE);
DEF_GET_SET(int, bdarr);
DEF_GET_SET_LEN(bdarr, DYNAMIC_ARRAY_SIZE);

DEF_GET_SET(float, fdmarr);
DEF_GET_SET_LEN(fdmarr, DYNAMIC_MARRAY_SIZE(float));
DEF_GET_SET(int, idmarr);
DEF_GET_SET_LEN(idmarr, DYNAMIC_MARRAY_SIZE(int));
DEF_GET_SET(int, bdmarr);
DEF_GET_SET_LEN(bdmarr, DYNAMIC_MARRAY_SIZE(int));

#  endif

#else

void api_def_test(DuneApi *dapi)
{
#  ifdef UNIT_TEST
  ApiStruct *sapi;
  ApiProp *prop;
  ushort dimsize[] = {MARRAY_DIMSIZE};

  sapi = api_def_struct(dapi, "Test", NULL);
  api_def_struct_stype(sapi, "Test");

  prop = api_def_float_array(
      srna, "farr", ARRAY_SIZE, NULL, 0.0f, 0.0f, "farr", "float array", 0.0f, 0.0f);
  api_def_prop_float_fns(prop, "api_Test_farr_get", "api_Test_farr_set", NULL);

  prop = api_def_int_array(sapi, "iarr", ARRAY_SIZE, NULL, 0, 0, "iarr", "int array", 0, 0);
  api_def_prop_int_fns(prop, "api_Test_iarr_get", "api_Test_iarr_set", NULL);

  prop = api_def_bool_array(sapi, "barr", ARRAY_SIZE, NULL, "barr", "bool array");
  api_def_prop_bool_fns(prop, "api_Test_barr_get", "api_Test_barr_set");

  /* dynamic arrays */
  prop = api_def_float_array(sapi,
                             "fdarr",
                             DYNAMIC_ARRAY_SIZE,
                             NULL,
                             0.0f,
                             0.0f,
                             "fdarr",
                             "dynamic float array",
                             0.0f,
                             0.0f);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_fdarr_get_length", "api_Test_fdarr_set_length");
  api_def_prop_float_fns(prop, "api_Test_fdarr_get", "api_Test_fdarr_set", NULL);

  prop = api_def_int_array(
      srna, "idarr", DYNAMIC_ARRAY_SIZE, NULL, 0, 0, "idarr", "int array", 0, 0);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_idarr_get_length", "api_Test_idarr_set_length");
  api_def_prop_int_fns(prop, "api_Test_idarr_get", "api_Test_idarr_set", NULL);

  prop = api_def_bool_array(sapi, "bdarr", DYNAMIC_ARRAY_SIZE, NULL, "bdarr", "boolean array");
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_bdarr_get_length", "api_Test_bdarr_set_length");
  api_def_prop_bool_fns(prop, "api_Test_bdarr_get", "api_Test_bdarr_set");

  /* multidimensional arrays */

  prop = api_def_prop(sapi, "fmarr", PROP_FLOAT, PROP_NONE);
  api_def_prop_multidimensional_array(prop, MARRAY_SIZE(float), MARRAY_TOTDIM, dimsize);
  api_def_prop_float_fns(prop, "api_Test_fmarr_get", "api_Test_fmarr_set", NULL);

  prop = api_def_prop(sapi, "imarr", PROP_INT, PROP_NONE);
  api_def_prop_multidimensional_array(prop, MARRAY_SIZE(int), MARRAY_TOTDIM, dimsize);
  Po_def_prop_int_fns(prop, "api_Test_imarr_get", "api_Test_imarr_set", NULL);

  prop = api_def_prop(sapi, "bmarr", PROP_BOOL, PROP_NONE);
  api_def_prop_multidimensional_array(prop, MARRAY_SIZE(int), MARRAY_TOTDIM, dimsize);
  api_def_prop_bool_fns(prop, "api_Test_bmarr_get", "api_Test_bmarr_set");

  /* dynamic multidimensional arrays */
  prop = api_def_prop(sapi, "fdmarr", PROP_FLOAT, PROP_NONE);
  api_def_prop_multidimensional_array(
      prop, DYNAMIC_MARRAY_SIZE(float), MARRAY_TOTDIM, dimsize);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_fdmarr_get_length", "api_Test_fdmarr_set_length");
  api_def_prop_float_fns(prop, "api_Test_fdmarr_get", "api_Test_fdmarr_set", NULL);

  prop = api_def_prop(sapi, "idmarr", PROP_INT, PROP_NONE);
  api_def_prop_multidimensional_array(prop, DYNAMIC_MARRAY_SIZE(int), MARRAY_TOTDIM, dimsize);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_idmarr_get_length", "api_Test_idmarr_set_length");
  api_def_prop_int_fns(prop, "api_Test_idmarr_get", "api_Test_idmarr_set", NULL);

  prop = api_def_prop(sapi, "bdmarr", PROP_BOOL, PROP_NONE);
  api_def_prop_multidimensional_array(prop, DYNAMIC_MARRAY_SIZE(int), MARRAY_TOTDIM, dimsize);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(
      prop, "api_Test_bdmarr_get_length", "api_Test_bdmarr_set_length");
  api_def_prop_bool_fns(prop, "api_Test_bdmarr_get", "api_Test_bdmarr_set");
#  else
  (void)dapi;
#  endif
}

#endif /* RNA_RUNTIME */
