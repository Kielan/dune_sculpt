#pragma once

/* Fns used during preprocess and runtime, for defining the api */
#include <float.h>
#include <inttypes.h>
#include <limits.h>

#include "types_list.h"
#include "api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef UNIT_TEST
#  define API_MAX_ARRAY_LENGTH 64
#else
#  define API_MAX_ARRAY_LENGTH 32
#endif

#define API_MAX_ARRAY_DIMENSION 3

/* Dune Api */
DuneApi *api_create(void);
void api_define_free(DuneApi *dapi);
void api_free(DuneApi *dapi);
void api_define_verify_stype(bool verify);
void api_define_animate_stype(bool animate);
void api_define_fallback_prop_update(int noteflag, const char *updatefn);
/* Props defined when this is enabled are lib-overridable by default
 * (except for Pointer ones). */
void api_define_lib_overridable(bool make_overridable);

void api_init(void);
void api_exit(void);

/* Struct */
/* Struct Definition. */
ApiStruct *api_def_struct_ptr(DuneApi *dapi, const char *id, ApiStruct *sapifrom);
ApiStruct *api_def_struct(DuneApi *dapi, const char *id, const char *from);
void api_def_struct_stype(ApiStruct *sapi, const char *structname);
void api_def_struct_stype_from(ApiStruct *sapi, const char *structname, const char *propname);
void api_def_struct_name_prop(ApiStruct *sapi, ApiProp *prop);
void api_def_struct_nested(DuneApi *dapi, ApiStruct *sapi, const char *structname);
void api_def_struct_flag(ApiStruct *sapi, int flag);
void api_def_struct_clear_flag(ApiStruct *sapi, int flag);
void api_def_struct_prop_tags(ApiStruct *sapi, const EnumPropItem *prop_tag_defines);
void api_def_struct_refine_fn(ApiStruct *sapi, const char *refine);
void api_def_struct_idprops_fn(ApiStruct *sapi, const char *idprops);
void api_def_struct_register_fns(ApiStruct *sapi,
                                 const char *reg,
                                 const char *unreg,
                                 const char *instance);
void api_def_struct_path_fn(ApiStruct *sapi, const char *path);
/* Only used in one case when we name the struct for the purpose of useful error messages. */
void api_def_struct_id_no_struct_map(ApiStruct *sapi, const char *id);
void api_def_struct_id(DuneApi *dapi, ApiStruct *sapu, const char *id);
void api_def_struct_ui_text(ApiStruct *sapi, const char *name, const char *description);
void api_def_struct_ui_icon(ApiStruct *sapi, int icon);
void api_struct_free_extension(ApiStruct *sapi, ApiExtension *api_ext);
void api_struct_free(DuneApi *dapu, ApiStruct *sapi);

void api_def_struct_lang_cxt(ApiStruct *sapi, const char *context);

/* Compact Prop Definitions */
typedef void ApiStructOrFn;

ApiProp *api_def_bool(ApiStructOrFn *cont,
                      const char *id,
                      bool default_value,
                      const char *ui_name,
                      const char *ui_description);
ApiProp *api_def_bool_array(ApiStructOrFn *cont,
                            const char *id,
                            int len,
                            bool *default_value,
                            const char *ui_name,
                            const char *ui_description);
ApiProp *api_def_bool_layer(ApiStructOrFn *cont,
                            const char *id,
                            int len,
                            bool *default_value,
                            const char *ui_name,
                            const char *ui_description);
ApiProp *api_def_bool_layer_member(ApiStructOrFn *cont,
                                   const char *id,
                                   int len,
                                   bool *default_value,
                                   const char *ui_name,
                                   const char *ui_description);
ApiProp *api_def_bool_vector(ApiStructOrFn *cont,
                             const char *id,
                             int len,
                             bool *default_value,
                             const char *ui_name,
                             const char *ui_description);

ApiProp *api_def_int(ApiStructOrFn *cont,
                         const char *id,
                         int default_value,
                         int hardmin,
                         int hardmax,
                         const char *ui_name,
                         const char *ui_description,
                         int softmin,
                         int softmax);
ApiProp *api_def_int_vector(ApiStructOrFn *cont,
                            const char *id,
                            int len,
                            const int *default_value,
                            int hardmin,
                            int hardmax,
                            const char *ui_name,
                            const char *ui_description,
                            int softmin,
                            int softmax);
ApiProp *api_def_int_array(ApiStructOrFn *cont,
                           const char *id,
                           int len,
                           const int *default_value,
                           int hardmin,
                           int hardmax,
                           const char *ui_name,
                           const char *ui_description,
                           int softmin,
                           int softmax);

ApiProp *api_def_string(ApiStructOrFn *cont,
                        const char *id,
                        const char *default_value,
                        int maxlen,
                        const char *ui_name,
                        const char *ui_description);
ApiProp *api_def_string_file_path(ApiStructOrFn *cont,
                                      const char *id,
                                      const char *default_value,
                                      int maxlen,
                                      const char *ui_name,
                                      const char *ui_description);
ApiProp *api_def_string_dir_path(ApiStructOrFn *cont,
                                     const char *id,
                                     const char *default_value,
                                     int maxlen,
                                     const char *ui_name,
                                     const char *ui_description);
ApiProp *api_def_string_file_name(ApiStructOrFn *cont,
                                  const char *id,
                                  const char *default_value,
                                  int maxlen,
                                  const char *ui_name,
                                  const char *ui_description);

ApiProp *api_def_enum(ApiStructOrFn *cont,
                      const char *id,
                      const EnumPropItem *items,
                      int default_value,
                      const char *ui_name,
                      const char *ui_description);
/* Same as above but sets PROP_ENUM_FLAG before setting the default value */
ApiProp *api_def_enum_flag(ApiStructOrFn *cont,
                           const char *id,
                           const EnumPropItem *items,
                           int default_value,
                           const char *ui_name,
                           const char *ui_description);
void api_def_enum_fns(ApiProp *prop, EnumPropItemFn itemfn);

ApiProp *api_def_float(ApiStructOrFn *cont,
                       const char *id,
                       float default_value,
                       float hardmin,
                       float hardmax,
                       const char *ui_name,
                       const char *ui_description,
                       float softmin,
                       float softmax);
ApiProp *api_def_float_vector(ApiStructOrFn *cont,
                              const char *id,
                              int len,
                              const float *default_value,
                              float hardmin,
                              float hardmax,
                              const char *ui_name,
                              const char *ui_description,
                              float softmin,
                              float softmax);
ApiProp *api_def_float_vector_xyz(ApiStructOrFn *cont,
                                  const char *id,
                                  int len,
                                  const float *default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax);
ApiProp *api_def_float_color(ApiStructOrFn *cont,
                             const char *id,
                             int len,
                             const float *default_value,
                             float hardmin,
                             float hardmax,
                             const char *ui_name,
                             const char *ui_description,
                             float softmin,
                             float softmax);
ApiProp *api_def_float_matrix(ApiStructOrFn *cont,
                              const char *id,
                              int rows,
                              int columns,
                              const float *default_value,
                              float hardmin,
                              float hardmax,
                              const char *ui_name,
                              const char *ui_description,
                              float softmin,
                              float softmax);
ApiProp *api_def_float_lang(ApiStructOrFn *cont,
                            const char *id,
                            int len,
                            const float *default_value,
                            float hardmin,
                            float hardmax,
                            const char *ui_name,
                            const char *ui_description,
                            float softmin,
                            float softmax);
ApiProp *api_def_float_rotation(ApiStructOrFn *cont,
                                const char *identifier,
                                int len,
                                const float *default_value,
                                float hardmin,
                                float hardmax,
                                const char *ui_name,
                                const char *ui_description,
                                float softmin,
                                float softmax);
ApiProp *api_def_float_distance(ApiStructOrFn *cont,
                                const char *id,
                                float default_value,
                                float hardmin,
                                float hardmax,
                                const char *ui_name,
                                const char *ui_description,
                                float softmin,
                                float softmax);
ApiProp *api_def_float_array(ApiStructOrFn *cont,
                             const char *id,
                             int len,
                             const float *default_value,
                             float hardmin,
                             float hardmax,
                             const char *ui_name,
                             const char *ui_description,
                             float softmin,
                             float softmax);

#if 0
ApiProp *api_def_float_dynamic_array(ApiStructOrFn *cont,
                                     const char *id,
                                     float hardmin,
                                     float hardmax,
                                     const char *ui_name,
                                     const char *ui_description,
                                     float softmin,
                                     float softmax,
                                     unsigned int dimension,
                                     unsigned short dim_size[]);
#endif

ApiProp *api_def_float_percentage(ApiStructOrFn *cont,
                                  const char *id,
                                  float default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax);
ApiProp *api_def_float_factor(ApiStructOrFn *cont,
                              const char *id,
                              float default_value,
                              float hardmin,
                              float hardmax,
                              const char *ui_name,
                              const char *ui_description,
                              float softmin,
                              float softmax);

ApiProp *api_def_ptr(ApiStructOrFn *cont,
                     const char *id,
                     const char *type,
                     const char *ui_name,
                     const char *ui_description);
ApiProp *api_def_ptr_runtime(ApiStructOrFn *cont,
                             const char *id,
                             ApiStruct *type,
                             const char *ui_name,
                             const char *ui_description);

ApiProp *api_def_collection(ApiStructOrFn *cont,
                                const char *id,
                                const char *type,
                                const char *ui_name,
                                const char *ui_description);
ApiProp *api_def_collection_runtime(ApiStructOrFn *cont,
                                    const char *id,
                                    ApiStruct *type,
                                    const char *ui_name,
                                    const char *ui_description);

/* Extended Prop Definitions */
ApiProp *api_def_prop(ApiStructOrFn *cont,
                      const char *id,
                      int type,
                      int subtype);

void api_def_prop_bool_stype(ApiProp *prop,
                             const char *structname,
                             const char *propname,
                             int64_t bit);
void api_def_prop_bool_negative_stype(ApiProp *prop,
                                     const char *structname,
                                     const char *propname,
                                     int64_t bit);
void api_def_prop_int_stype(ApiProp *prop, const char *structname, const char *propname);
void api_def_prop_float_stype(ApiProp *prop, const char *structname, const char *propname);
void api_def_prop_string_stype(ApiProp *prop, const char *structname, const char *propname);
void api_def_prop_enum_stype(ApiProp *prop, const char *structname, const char *propname);
void api_def_prop_enum_bitflag_stype(ApiProp *prop,
                                     const char *structname,
                                     const char *propname);
void api_def_prop_ptr_stype(ApiProp *prop,
                            const char *structname,
                            const char *propname);
void api_def_prop_collection_stype(ApiProp *prop,
                                   const char *structname,
                                   const char *propname,
                                   const char *lengthpropname);

void api_def_prop_flag(ApiProp *prop, PropertyFlag flag);
void api_def_prop_clear_flag(ApiProp *prop, PropFlag flag);
void api_def_prop_override_flag(ApiProp *prop, PropOverrideFlag flag);
void api_def_prop_override_clear_flag(ApiProp *prop, PropOverrideFlag flag);
/* Add the prop-tags passed as tags to prop (if valid).
 *
 * note Multiple tags can be set by passing them within tags (using bit-flags).
 * note Doesn't do any type-checking with the tags defined in the parent ApiStruct
 * of prop. This should be done before (e.g. see wm_optype_prop_tag). */
void api_def_prop_tags(ApiProp *prop, int tags);
void api_def_prop_subtype(ApiProp *prop, PropSubType subtype);
void api_def_prop_array(ApiProp *prop, int length);
void api_def_prop_multi_array(ApiProp *prop, int dimension, const int length[]);
void api_def_prop_range(ApiProp *prop, double min, double max);

void api_def_prop_enum_items(ApiProp *prop, const EnumPropItem *item);
void api_def_prop_enum_native_type(ApiProp *prop, const char *native_enum_type);
void api_def_prop_string_maxlength(ApiProp *prop, int maxlength);
void api_def_prop_struct_type(ApuProp *prop, const char *type);
void api_def_prop_struct_runtime(ApiStructOrFn *cont,
                                 ApiProp *prop,
                                 ApiStruct *type);

void api_def_prop_bool_default(ApiProp *prop, bool value);
void api_def_prop_bool_array_default(ApiProp *prop, const bool *array);
void api_def_prop_int_default(ApiProp *prop, int value);
void api_def_prop_int_array_default(ApiProp *prop, const int *array);
void api_def_prop_float_default(ApiProp *prop, float value);
/* Array must remain valid after this function finishes. */
void api_def_prop_float_array_default(ApiProp *prop, const float *array);
void api_def_prop_enum_default(ApiProp *prop, int value);
void api_def_prop_string_default(ApiProp *prop, const char *value);

void api_def_prop_ui_text(ApiProp *prop, const char *name, const char *description);
/* The values hare are a little confusing:
 * param step: Used as the value to increase/decrease when clicking on number buttons,
 * as well as scaling mouse input for click-dragging number buttons.
 * For floats this is (step * UI_PRECISION_FLOAT_SCALE), why? - nobody knows.
 * For ints, whole values are used.
 *
 * param precision: The number of zeros to show
 * (as a whole number - common range is 1 - 6), see UI_PRECISION_FLOAT_MAX */
void api_def_prop_ui_range(
    ApiProp *prop, double min, double max, double step, int precision);
void api_def_prop_ui_scale_type(ApiProp *prop, PropScaleType scale_type);
void api_def_prop_ui_icon(ApiProp *prop, int icon, int consecutive);

void api_def_prop_update(ApiProp *prop, int noteflag, const char *updatefn);
void api_def_prop_editable_fn(ApiProp *prop, const char *editable);
void api_def_prop_editable_array_fn(ApiProp *prop, const char *editable);

/* Set custom cbs for override ops handling.
 * note diff cb will also be used by api comparison/equality fns. */
void api_def_prop_override_fns(ApiProp *prop,
                               const char *diff,
                               const char *store,
                               const char *apply);

void api_def_prop_update_runtime(ApiProp *prop, const void *fn);
void api_def_prop_poll_runtime(ApiProp *prop, const void *fn);

void api_def_prop_dynamic_array_fns(ApiProp *prop, const char *getlength);
void api_def_prop_bool_fns(ApiProp *prop, const char *get, const char *set);
void api_def_prop_int_fns(ApiProp *prop,
                          const char *get,
                          const char *set,
                          const char *range);
void api_def_prop_float_fns(ApiProp *prop,
                            const char *get,
                            const char *set,
                            const char *range);
void api_def_prop_enum_fns(ApiProp *prop,
                           const char *get,
                           const char *set,
                           const char *item);
void RNA_def_prop_string_fns(ApiProp *prop,
                             const char *get,
                             const char *length,
                             const char *set);
void RNA_def_property_pointer_funcs(
    PropertyRNA *prop, const char *get, const char *set, const char *type_fn, const char *poll);
void RNA_def_property_collection_funcs(ApiProp *prop,
                                       const char *begin,
                                       const char *next,
                                       const char *end,
                                       const char *get,
                                       const char *length,
                                       const char *lookupint,
                                       const char *lookupstring,
                                       const char *assignint);
void api_def_prop_sapi(ApiProp *prop, const char *type);
void api_def_py_data(ApiProp *prop, void *py_data);

void api_def_prop_bool_fns_runtime(PropertyRNA *prop,
                                   BoolPropGetFn getfn,
                                   BoolPropSetFn setfn);
void api_def_prop_bool_array_fns_runtime(ApiProp *prop,
                                         BoolArrayPropertyGetFunc getfunc,
                                         BoolArrayPropertySetFunc setfunc);
void api_def_prop_int_fns_runtime(ApiProp *prop,
                                  IntPropGetFn getfn,
                                  IntPropSetFn setfn,
                                  IntPropRangeFn rangefn);
void api_def_prop_int_array_fns_runtime(ApiProp *prop,
                                        IntArrayPropGetFn getfn,
                                        IntArrayPropSetFn setfn,
                                        IntPropRangeFn rangefn);
void api_def_prop_float_fns_runtime(ApiProp *prop,
                                    FloatPropGetFn getfn,
                                    FloatPropSetFn setfn,
                                    FloatPropRangeFn rangefn);
void api_def_prop_float_array_fns_runtime(ApiProp *prop,
                                          FloatArrayPropGetFn getfn,
                                          FloatArrayPropSetFn setfn,
                                          FloatPropRangeFn rangefn);
void api_def_prop_enum_fns_runtime(ApiProp *prop,
                                   EnumPropGetFn getfn,
                                   EnumPropSetFn setfn,
                                   EnumPropItemFn itemfn);
void api_def_prop_string_fns_runtime(ApiProp *prop,
                                     StringPropGetFn getfn,
                                     StringPropLengthFn lengthfn,
                                     StringPropSetFn setfn);

void api_def_prop_lang_cxt(ApiProp *prop, const char *cxt);

/* Fn */
ApiFn *api_def_fn(ApiStruct *sapi, const char *id, const char *call);
ApiFn *api_def_fn_runtime(ApiStruct *sapi, const char *id, CallFn call);
/* C return value only! multiple api returns can be done with api_def_fn_output */
void api_def_fn_return(ApiFn *fn, ApiProp *ret);
void api_def_fn_output(ApiFn *fn, ApiProp *ret);
void api_def_fn_flag(ApiFn *fn, int flag);
void api_def_fn_ui_description(ApiFn *fn, const char *description);

void api_def_param_flags(ApiProp *prop,
                             PropFlag flag_prop,
                             ParamFlag flag_param);
void api_def_param_clear_flags(PropRNA *prop,
                                   PropFlag flag_prop,
                                   ParamFlag flag_param);

/* Dynamic Enums
 * strings are not freed, assumed pointing to static location. */
void api_enum_item_add(EnumPropItem **items, int *totitem, const EnumPropItem *item);
void api_enum_item_add_separator(EnumPropItem **items, int *totitem);
void api_enum_items_add(EnumPropItem **items, int *totitem, const EnumPropItem *item);
void api_enum_items_add_value(EnumPropItem **items,
                              int *totitem,
                              const EnumPropItem *item,
                              int value);
void api_enum_item_end(EnumPropItem **items, int *totitem);

/* Memory management */
void api_def_struct_duplicate_ptrs(DuneApi *dapi, ApiStruct *sapi);
void api_def_struct_free_ptrs(DuneApi *dapi, ApiStruct *sapi);
void api_def_fn_duplicate_ptrs(ApiFn *fn);
void api_def_fn_free_pts(ApiFn *fn);
void api_def_prop_duplicate_ptrs(ApiStructOrFn *cont_, ApiProp *prop);
void api_def_prop_free_ptrs(ApiProp *prop);
int api_def_prop_free_id(ApiStructOrFn *cont_, const char *id);

int api_def_prop_free_id_deferred_prepare(ApiStructOrFn *cont_,
                                          const char *id,
                                          void **handle);
void api_def_prop_free_id_deferred_finish(ApiStructOrFn *cont_, void *handle);

void api_def_prop_free_ptrs_set_py_data_cb(
    void (*py_data_clear_fn)(ApiProp *prop));

/* Utilities. */
const char *api_prop_typename(PropType type);
#define IS_TYPE_FLOAT_COMPAT(_str) (strcmp(_str, "float") == 0 || strcmp(_str, "double") == 0)
#define IS_TYPE_INT_COMPAT(_str) \
  (strcmp(_str, "int") == 0 || strcmp(_str, "short") == 0 || strcmp(_str, "char") == 0 || \
   strcmp(_str, "uchar") == 0 || strcmp(_str, "ushort") == 0 || strcmp(_str, "int8_t") == 0)
#define IS_TYPE_BOOL_COMPAT(_str) \
  (IS_TYPE_INT_COMPAT(_str) || strcmp(_str, "int64_t") == 0 || strcmp(_str, "uint64_t") == 0)

void api_id_sanitize(char *id, int prop);

/* Common arguments for length. */
extern const int api_matrix_dimsize_3x3[];
extern const int api_matrix_dimsize_4x4[];
extern const int api_matrix_dimsize_4x2[];

/* Common arguments for defaults. */
extern const float api_default_axis_angle[4];
extern const float api_default_quaternion[4];
extern const float api_default_scale_3d[3];

/* Maximum size for dynamic defined type descriptors, this value is arbitrary. */
#define API_DYN_DESCR_MAX 240

#ifdef __cplusplus
}
#endif
