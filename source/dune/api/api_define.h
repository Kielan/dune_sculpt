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
PropertyRNA *RNA_def_float_dynamic_array(StructOrFunctionRNA *cont,
                                         const char *identifier,
                                         float hardmin,
                                         float hardmax,
                                         const char *ui_name,
                                         const char *ui_description,
                                         float softmin,
                                         float softmax,
                                         unsigned int dimension,
                                         unsigned short dim_size[]);
#endif

PropertyRNA *RNA_def_float_percentage(StructOrFunctionRNA *cont,
                                      const char *identifier,
                                      float default_value,
                                      float hardmin,
                                      float hardmax,
                                      const char *ui_name,
                                      const char *ui_description,
                                      float softmin,
                                      float softmax);
PropertyRNA *RNA_def_float_factor(StructOrFunctionRNA *cont,
                                  const char *identifier,
                                  float default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax);

PropertyRNA *RNA_def_pointer(StructOrFunctionRNA *cont,
                             const char *identifier,
                             const char *type,
                             const char *ui_name,
                             const char *ui_description);
PropertyRNA *RNA_def_pointer_runtime(StructOrFunctionRNA *cont,
                                     const char *identifier,
                                     StructRNA *type,
                                     const char *ui_name,
                                     const char *ui_description);

PropertyRNA *RNA_def_collection(StructOrFunctionRNA *cont,
                                const char *identifier,
                                const char *type,
                                const char *ui_name,
                                const char *ui_description);
PropertyRNA *RNA_def_collection_runtime(StructOrFunctionRNA *cont,
                                        const char *identifier,
                                        StructRNA *type,
                                        const char *ui_name,
                                        const char *ui_description);

/* Extended Property Definitions */

PropertyRNA *RNA_def_property(StructOrFunctionRNA *cont,
                              const char *identifier,
                              int type,
                              int subtype);

void RNA_def_property_boolean_sdna(PropertyRNA *prop,
                                   const char *structname,
                                   const char *propname,
                                   int64_t bit);
void RNA_def_property_boolean_negative_sdna(PropertyRNA *prop,
                                            const char *structname,
                                            const char *propname,
                                            int64_t bit);
void RNA_def_property_int_sdna(PropertyRNA *prop, const char *structname, const char *propname);
void RNA_def_property_float_sdna(PropertyRNA *prop, const char *structname, const char *propname);
void RNA_def_property_string_sdna(PropertyRNA *prop, const char *structname, const char *propname);
void RNA_def_property_enum_sdna(PropertyRNA *prop, const char *structname, const char *propname);
void RNA_def_property_enum_bitflag_sdna(PropertyRNA *prop,
                                        const char *structname,
                                        const char *propname);
void RNA_def_property_pointer_sdna(PropertyRNA *prop,
                                   const char *structname,
                                   const char *propname);
void RNA_def_property_collection_sdna(PropertyRNA *prop,
                                      const char *structname,
                                      const char *propname,
                                      const char *lengthpropname);

void RNA_def_property_flag(PropertyRNA *prop, PropertyFlag flag);
void RNA_def_property_clear_flag(PropertyRNA *prop, PropertyFlag flag);
void RNA_def_property_override_flag(PropertyRNA *prop, PropertyOverrideFlag flag);
void RNA_def_property_override_clear_flag(PropertyRNA *prop, PropertyOverrideFlag flag);
/**
 * Add the property-tags passed as \a tags to \a prop (if valid).
 *
 * \note Multiple tags can be set by passing them within \a tags (using bit-flags).
 * \note Doesn't do any type-checking with the tags defined in the parent #StructRNA
 * of \a prop. This should be done before (e.g. see #WM_operatortype_prop_tag).
 */
void RNA_def_property_tags(PropertyRNA *prop, int tags);
void RNA_def_property_subtype(PropertyRNA *prop, PropertySubType subtype);
void RNA_def_property_array(PropertyRNA *prop, int length);
void RNA_def_property_multi_array(PropertyRNA *prop, int dimension, const int length[]);
void RNA_def_property_range(PropertyRNA *prop, double min, double max);

void RNA_def_property_enum_items(PropertyRNA *prop, const EnumPropertyItem *item);
void RNA_def_property_enum_native_type(PropertyRNA *prop, const char *native_enum_type);
void RNA_def_property_string_maxlength(PropertyRNA *prop, int maxlength);
void RNA_def_property_struct_type(PropertyRNA *prop, const char *type);
void RNA_def_property_struct_runtime(StructOrFunctionRNA *cont,
                                     PropertyRNA *prop,
                                     StructRNA *type);

void RNA_def_property_boolean_default(PropertyRNA *prop, bool value);
void RNA_def_property_boolean_array_default(PropertyRNA *prop, const bool *array);
void RNA_def_property_int_default(PropertyRNA *prop, int value);
void RNA_def_property_int_array_default(PropertyRNA *prop, const int *array);
void RNA_def_property_float_default(PropertyRNA *prop, float value);
/**
 * Array must remain valid after this function finishes.
 */
void RNA_def_property_float_array_default(PropertyRNA *prop, const float *array);
void RNA_def_property_enum_default(PropertyRNA *prop, int value);
void RNA_def_property_string_default(PropertyRNA *prop, const char *value);

void RNA_def_property_ui_text(PropertyRNA *prop, const char *name, const char *description);
/**
 * The values hare are a little confusing:
 *
 * \param step: Used as the value to increase/decrease when clicking on number buttons,
 * as well as scaling mouse input for click-dragging number buttons.
 * For floats this is (step * UI_PRECISION_FLOAT_SCALE), why? - nobody knows.
 * For ints, whole values are used.
 *
 * \param precision: The number of zeros to show
 * (as a whole number - common range is 1 - 6), see UI_PRECISION_FLOAT_MAX
 */
void RNA_def_property_ui_range(
    PropertyRNA *prop, double min, double max, double step, int precision);
void RNA_def_property_ui_scale_type(PropertyRNA *prop, PropertyScaleType scale_type);
void RNA_def_property_ui_icon(PropertyRNA *prop, int icon, int consecutive);

void RNA_def_property_update(PropertyRNA *prop, int noteflag, const char *updatefunc);
void RNA_def_property_editable_func(PropertyRNA *prop, const char *editable);
void RNA_def_property_editable_array_func(PropertyRNA *prop, const char *editable);

/**
 * Set custom callbacks for override operations handling.
 *
 * \note \a diff callback will also be used by RNA comparison/equality functions.
 */
void RNA_def_property_override_funcs(PropertyRNA *prop,
                                     const char *diff,
                                     const char *store,
                                     const char *apply);

void RNA_def_property_update_runtime(PropertyRNA *prop, const void *func);
void RNA_def_property_poll_runtime(PropertyRNA *prop, const void *func);

void RNA_def_property_dynamic_array_funcs(PropertyRNA *prop, const char *getlength);
void RNA_def_property_boolean_funcs(PropertyRNA *prop, const char *get, const char *set);
void RNA_def_property_int_funcs(PropertyRNA *prop,
                                const char *get,
                                const char *set,
                                const char *range);
void RNA_def_property_float_funcs(PropertyRNA *prop,
                                  const char *get,
                                  const char *set,
                                  const char *range);
void RNA_def_property_enum_funcs(PropertyRNA *prop,
                                 const char *get,
                                 const char *set,
                                 const char *item);
void RNA_def_property_string_funcs(PropertyRNA *prop,
                                   const char *get,
                                   const char *length,
                                   const char *set);
void RNA_def_property_pointer_funcs(
    PropertyRNA *prop, const char *get, const char *set, const char *type_fn, const char *poll);
void RNA_def_property_collection_funcs(PropertyRNA *prop,
                                       const char *begin,
                                       const char *next,
                                       const char *end,
                                       const char *get,
                                       const char *length,
                                       const char *lookupint,
                                       const char *lookupstring,
                                       const char *assignint);
void RNA_def_property_srna(PropertyRNA *prop, const char *type);
void RNA_def_py_data(PropertyRNA *prop, void *py_data);

void RNA_def_property_boolean_funcs_runtime(PropertyRNA *prop,
                                            BooleanPropertyGetFunc getfunc,
                                            BooleanPropertySetFunc setfunc);
void RNA_def_property_boolean_array_funcs_runtime(PropertyRNA *prop,
                                                  BooleanArrayPropertyGetFunc getfunc,
                                                  BooleanArrayPropertySetFunc setfunc);
void RNA_def_property_int_funcs_runtime(PropertyRNA *prop,
                                        IntPropertyGetFunc getfunc,
                                        IntPropertySetFunc setfunc,
                                        IntPropertyRangeFunc rangefunc);
void RNA_def_property_int_array_funcs_runtime(PropertyRNA *prop,
                                              IntArrayPropertyGetFunc getfunc,
                                              IntArrayPropertySetFunc setfunc,
                                              IntPropertyRangeFunc rangefunc);
void RNA_def_prop_float_fns_runtime(ApiProp *prop,
                                    FloatPropertyGetFunc getfunc,
                                    FloatPropertySetFunc setfunc,
                                    FloatPropertyRangeFunc rangefunc);
void RNA_def_prop_float_array_fns_runtime(PropertyRNA *prop,
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
void RNA_def_param_clear_flags(PropRNA *prop,
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
