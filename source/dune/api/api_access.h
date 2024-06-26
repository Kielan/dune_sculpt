/* Use a define instead of `#pragma once` because of `api_internal.h` */
#ifndef __API_ACCESS_H__
#define __API_ACCESS_H__

#include <stdarg.h>

#include "api_types.h"

#include "lib_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Id;
struct IdOverrideLib;
struct IdOverrideLibProp;
struct IdOverrideLibPropOp;
struct IdProp;
struct List;
struct Main;
struct ReportList;
struct Scene;
struct Cxt;

/* Types */
extern DuneApi DUNE_API;

/* These fns will fill in api ptrs, can be done in three ways:
 * - ptr Main is created by passing the data ptr
 * - ptr to a datablock created w type and id data ptr
 * - ptr to data in a datablock created w: id type,
 *   id data ptr, data type, and ptr to the struct itself.
 * Also possible to get a ptr w information about all structs. */
void api_main_ptr_create(struct Main *main, ApiPtr *r_ptr);
void api_id_ptr_create(struct Id *id, ApiPtr *r_ptr);
void api_ptr_create(struct Id *id, ApiStruct *type, void *data, ApiPtr *r_ptr);
bool api_ptr_is_null(const ApiPtr *ptr);

bool api_path_resolved_create(ApiPtr *ptr,
                              struct ApiProp *prop,
                              int prop_index,
                              ApiPathResolved *r_anim_api);

void api_dune_api_ptr_create(ApiPtr *r_ptr);
void api_ptr_recast(ApiPtr *ptr, ApiPtr *r_ptr);

extern const ApiPtr ApiPtr_NULL;

/* Structs */
ApiStruct *api_struct_find(const char *id);
const char *api_struct_id(const ApiStruct *type);
const char *api_struct_ui_name(const ApiStruct *type);
const char *api_struct_ui_name_raw(const ApiStruct *type);
const char *api_struct_ui_description(const ApiStruct *type);
const char *api_struct_ui_description_raw(const ApiStruct *type);
const char *api_struct_lang_cxt(const ApiStruct *type);
int api_struct_ui_icon(const ApiStruct *type);

ApiProp *api_struct_name_prop(const ApiStruct *type);
const EnumPropItem *api_struct_prop_tag_defines(const ApiStruct *type);
ApiProp *api_struct_iter_prop(ApiStruct *type);
ApiStruct *api_struct_base(ApiStruct *type);
/* Use to find the sub-type directly below a base-type.
 * So if type were `Api_SpotLight`, `api_struct_base_of(type, &API_ID)` would return `&Api_Light`. */
const ApiStruct *api_struct_base_child_of(const ApiStruct *type, const ApiStruct *parent_type);

bool api_struct_is_id(const ApiStruct *type);
bool api_struct_is_a(const ApiStruct *type, const ApiStruct *sapi);

bool api_struct_undo_check(const ApiStruct *type);

StructRegisterFn api_struct_register(ApiStruct *type);
StructUnregisterFn api_struct_unregister(ApiStruct *type);
void **api_struct_instance(ApiPtr *ptr);

void *api_struct_py_type_get(ApiStruct *sapi);
void api_struct_py_type_set(ApiStruct *sapi, void *py_type);

void *api_struct_dune_type_get(ApiStruct *sapi);
void api_struct_dune_type_set(ApiStruct *sapi, void *dune_type);

struct IdProp **api_struct_idprops_p(ApiPtr *ptr);
struct IdProp *api_struct_idprops(ApiPtr *ptr, bool create);
bool api_struct_idprops_check(ApiStruct *sapi);
bool api_struct_idprops_register_check(const ApiStruct *type);
bool api_struct_idprops_datablock_allowed(const ApiStruct *type);
/* Whether given type implies datablock usage by IdProps.
 * This is used to prevent classes allowed to have IdProps,
 * but not datablock ones, to indirectly use some
 * (e.g. by assigning an IDP_GROUP containing some IDP_ID ptrs...). */
bool api_struct_idprops_contains_datablock(const ApiStruct *type);
/* Remove an id-prop. */
bool api_struct_idprops_unset(ApiPtr *ptr, const char *id);

ApiProp *api_struct_find_prop(ApiPtr *ptr, const char *id);
bool api_struct_contains_prop(ApiPtr *ptr, ApiProp *prop_test);
unsigned int api_struct_count_props(ApiStruct *sapi);

/* Low level direct access to type->properties,
 * note this ignores parent classes so should be used with care. */
const struct List *api_struct_type_props(ApiStruct *sapi);
ApiProp *api_struct_type_find_prop_no_base(ApiStruct *sapi, const char *id);
/* api_struct_find_prop is a higher level alternative to this function
 * which takes a ApiPtr instead of a ApiStruct. */
ApiProp *api_struct_type_find_prop(ApiStruct *sapi, const char *id);

ApiFn *api_struct_find_fn(ApiStruct *sapi, const char *id);
const struct List *api_struct_type_fns(ApiStruct *sapi);

char *api_struct_name_get_alloc(ApiPtr *ptr, char *fixedbuf, int fixedlen, int *r_len);

/* Use when registering structs with the STRUCT_PUBLIC_NAMESPACE flag. */
bool api_struct_available_or_report(struct ReportList *reports, const char *id);
bool api_struct_bl_idname_ok_or_report(struct ReportList *reports,
                                       const char *id,
                                       const char *sep);
/* Props
 * Access to struct props. All this works with api pointers rather than
 * direct pointers to the data. */
/* Prop Information */
const char *api_prop_id(const ApiProp *prop);
const char *api_prop_description(ApiProp *prop);

PropType api_prop_type(ApiProp *prop);
PropSubType api_prop_subtype(ApiProp *prop);
PropUnit api_prop_unit(ApiProp *prop);
PropScaleType api_prop_ui_scale(ApiProp *prop);
int api_prop_flag(ApiProp *prop);
int api_prop_override_flag(ApiProp *prop);
/* Get the tags set for prop as int bitfield.
 * Doesn't perform any validity check on the set bits. api_def_prop_tags does this
 * in debug builds (to avoid performance issues in non-debug builds), which should be
 * the only way to set tags. Hence, at this point we assume the tag bitfield to be valid. */
int api_prop_tags(ApiProp *prop);
bool api_prop_builtin(ApiProp *prop);
void *api_prop_py_data_get(ApiProp *prop);

int api_prop_array_length(ApiPtr *ptr, ApiProp *prop);
bool api_prop_array_check(ApiProp *prop);
/* Return the size of Nth dimension. */
int api_prop_multi_array_length(ApiPtr *ptr, ApiProp *prop, int dimension);
/* Used by BPY to make an array from the python object */
int api_prop_array_dimension(ApiPtr *ptr, ApiProp *prop, int length[]);
char api_prop_array_item_char(ApiProp *prop, int index);
int api_prop_array_item_index(ApiProp *prop, char name);

/* return the maximum length including the \0 terminator. '0' is used when there is no maximum. */
int api_prop_string_maxlength(ApiProp *prop);

const char *api_prop_ui_name(const ApiProp *prop);
const char *api_prop_ui_name_raw(const ApiProp *prop);
const char *api_prop_ui_description(const ApiProp *prop);
const char *api_prop_ui_description_raw(const ApiProp *prop);
const char *api_prop_lang_cxt(const ApiProp *prop);
int api_prop_ui_icon(const ApiProp *prop);

/* Dynamic Prop Information */
void api_prop_int_range(ApiPtr *ptr, ApiProp *prop, int *hardmin, int *hardmax);
void api_prop_int_ui_range(
    ApiPtr *ptr, ApiProp *prop, int *softmin, int *softmax, int *step);

void api_prop_float_range(ApiPtr *ptr, ApiProp *prop, float *hardmin, float *hardmax);
void api_prop_float_ui_range(ApiPtr *ptr,
                             ApiProp *prop,
                             float *softmin,
                             float *softmax,
                             float *step,
                             float *precision);

int api_prop_float_clamp(ApiPtr *ptr, ApiProp *prop, float *value);
int api_prop_int_clamp(ApiPtr *ptr, ApiProp *prop, int *value);

bool api_enum_id(const EnumPropItem *item, int value, const char **id);
int api_enum_bitflag_ids(const EnumPropItem *item, int value, const char **id);
bool api_enum_name(const EnumPropItem *item, int value, const char **r_name);
bool api_enum_description(const EnumPropItem *item, int value, const char **description);
int api_enum_from_value(const EnumPropItem *item, int value);
int api_enum_from_id(const EnumPropItem *item, const char *id);
/* Take care using this with translated enums,
 * prefer api_enum_from_id where possible. */
int api_enum_from_name(const EnumPropItem *item, const char *name);
unsigned int api_enum_items_count(const EnumPropItem *item);

void api_prop_enum_items_ex(struct Cxt *C,
                            ApiPtr *ptr,
                            ApiProp *prop,
                            bool use_static,
                            const EnumPropItem **r_item,
                            int *r_totitem,
                            bool *r_free);
void api_prop_enum_items(struct Cxt *C,
                         ApiPtr *ptr,
                         ApiProp *prop,
                         const EnumPropItem **r_item,
                         int *r_totitem,
                         bool *r_free);
void api_prop_enum_items_gettexted(struct Cxt *C,
                                   ApiPtr *ptr,
                                   ApiProp *prop,
                                   const EnumPropItem **r_item,
                                   int *r_totitem,
                                   bool *r_free);
void api_prop_enum_items_gettexted_all(struct Cxt *C,
                                       ApiPtr *ptr,
                                       ApiProp *prop,
                                       const EnumPropItem **r_item,
                                       int *r_totitem,
                                       bool *r_free);
bool api_prop_enum_value(
    struct Cxt *C, ApuPtr *ptr, ApiProp *prop, const char *id, int *r_value);
bool api_prop_enum_id(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, const char **id);
bool api_prop_enum_name(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, const char **name);
bool api_prop_enum_name_gettexted(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, const char **name);

bool api_prop_enum_item_from_value(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, EnumPropItem *r_item);
bool api_prop_enum_item_from_value_gettexted(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, EnumPropItem *r_item);

int api_prop_enum_bitflag_ids(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int value, const char **id);

ApiStruct *api_prop_ptr_type(ApiPtr *ptr, ApiProp *prop);
bool api_prop_ptr_poll(ApiPtr *ptr, ApiProp *prop, ApiPtr *value);

bool api_prop_editable(ApiPtr *ptr, ApiProp *prop);
/* Version of api_prop_editable that tries to return additional info in r_info
 * that can be exposed in UI. */
bool api_prop_editable_info(ApiPtr *ptr, ApiProp *prop, const char **r_info);
/* Same as api_prop_editable(), except this checks individual items in an array. */
bool api_prop_editable_index(ApiPtr *ptr, ApiProp *prop, const int index);

/* Without lib check, only checks the flag. */
bool api_prop_editable_flag(ApiPtr *ptr, ApiProp *prop);

bool api_prop_animateable(ApiPtr *ptr, ApiProp *prop);
bool api_prop_animated(ApiPtr *ptr, ApiProp *prop);
/* Does not take into account editable status, this has to be checked separately
 * (using api_prop_editable_flag() usually). */
bool api_prop_overridable_get(ApiPtr *ptr, ApiProp *prop);
/* Should only be used for custom properties. */
bool api_prop_overridable_lib_set(ApiPtr *ptr, ApiProp *prop, bool is_overridable);
bool api_prop_overridden(ApiPtr *ptr, ApiProp *prop);
bool api_prop_comparable(ApiPtr *ptr, ApiProp *prop);
/* This fn is to check if its possible to create a valid path from the ID
 * its slow so don't call in a loop. */
bool api_prop_path_from_id_check(ApiPtr *ptr, ApiProp *prop); /* slow, use with care */

void api_prop_update(struct Cxt *C, ApiPtr *ptr, ApiProp *prop);
/* param scene: may be NULL. */
void api_prop_update_main(struct Main *main,
                          struct Scene *scene,
                          ApiPtr *ptr,
                          ApiProp *prop);
/* note its possible this returns a false positive in the case of #PROP_CONTEXT_UPDATE
 * but this isn't likely to be a performance problem. */
bool api_prop_update_check(struct ApiProp *prop);

/* Prop Data */
bool api_prop_bool_get(ApiPtr *ptr, ApiProp *prop);
void api_prop_bool_set(ApiPtr *ptr, ApiProp *prop, bool value);
void api_prop_bool_get_array(ApiPtr *ptr, ApiProp *prop, bool *values);
bool api_prop_bool_get_index(ApiPtr *ptr, ApiProp *prop, int index);
void api_prop_bool_set_array(ApiPtr *ptr, ApiProp *prop, const bool *values);
void api_prop_bool_set_index(ApiPtr *ptr, ApiProp *prop, int index, bool value);
bool api_prop_bool_get_default(ApiPtr *ptr, ApiProp *prop);
void api_prop_bool_get_default_array(ApiPtr *ptr, ApiProp *prop, bool *values);
bool api_prop_bool_get_default_index(ApiPtr *ptr, ApiProp *prop, int index);

int api_prop_int_get(ApiPtr *ptr, ApiProp *prop);
void api_prop_int_set(ApiPtr *ptr, ApiProp *prop, int value);
void api_prop_int_get_array(ApiPtr *ptr, ApiProp *prop, int *values);
void api_prop_int_get_array_range(ApiPtr *ptr, ApiProp *prop, int values[2]);
int api_prop_int_get_index(ApiPtr *ptr, ApiProp *prop, int index);
void api_prop_int_set_array(ApiPtr *ptr, ApiProp *prop, const int *values);
void api_prop_int_set_index(ApiPtr *ptr, ApiProp *prop, int index, int value);
int api_prop_int_get_default(ApiPtr *ptr, ApiProp *prop);
bool api_prop_int_set_default(ApiProp *prop, int value);
void api_prop_int_get_default_array(ApiPtr *ptr, ApiProp *prop, int *values);
int api_prop_int_get_default_index(ApiPtr *ptr, ApiProp *prop, int index);

float api_prop_float_get(ApiPtr *ptr, ApiProp *prop);
void api_prop_float_set(ApiPtr *ptr, ApiProp *prop, float value);
void api_prop_float_get_array(ApiPtr *ptr, ApiProp *prop, float *values);
void api_prop_float_get_array_range(ApiPtr *ptr, ApiProp *prop, float values[2]);
float api_prop_float_get_index(ApiPtr *ptr, ApiProp *prop, int index);
void api_prop_float_set_array(ApiPtr *ptr, ApiProp *prop, const float *values);
void api_prop_float_set_index(ApiPtr *ptr, ApiProp *prop, int index, float value);
float api_prop_float_get_default(ApiPtr *ptr, ApiProp *prop);
bool api_prop_float_set_default(ApiProp *prop, float value);
void api_prop_float_get_default_array(ApiPtr *ptr, ApiProp *prop, float *values);
float api_prop_float_get_default_index(ApiPtr *ptr, ApiProp *prop, int index);

void api_prop_string_get(ApiPtr *ptr, ApiProp *prop, char *value);
char *api_prop_string_get_alloc(
    ApiPtr *ptr, ApiProp *prop, char *fixedbuf, int fixedlen, int *r_len);
void api_prop_string_set(ApiPtr *ptr, ApiProp *prop, const char *value);
void api_prop_string_set_bytes(ApiPtr *ptr, ApiProp *prop, const char *value, int len);
/* return the length without `\0` terminator. */
int api_prop_string_length(ApiPtr *ptr, ApiProp *prop);
void api_prop_string_get_default(ApiProp *prop, char *value, int max_len);
char *api_prop_string_get_default_alloc(
    ApiPtr *ptr, ApiProp *prop, char *fixedbuf, int fixedlen, int *r_len);
/* return the length without `\0` terminator. */
int api_prop_string_default_length(ApiPtr *ptr, PropertyRNA *prop);

int api_prop_enum_get(ApiPtr *ptr, ApiProp *prop);
void api_prop_enum_set(ApiPtr *ptr, ApiProp *prop, int value);
int api_prop_enum_get_default(ApiPtr *ptr, ApiProp *prop);
/* Get the value of the item that is step items away from from_value.
 * param from_value: Item value to start stepping from.
 * param step: Absolute value defines step size, sign defines direction.
 * E.g to get the next item, pass 1, for the previous -1 */
int api_prop_enum_step(
    const struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int from_value, int step);

ApiPtr api_prop_ptr_get(ApiPtr *ptr, ApiProp *prop) ATTR_NONNULL(1, 2);
void api_prop_ptr_set(ApiPtr *ptr,
                      ApiProp *prop,
                      ApiPtr ptr_value,
                      struct ReportList *reports) ATTR_NONNULL(1, 2);
ApiPtr api_prop_ptr_get_default(ApiPtr *ptr, ApiProp *prop) ATTR_NONNULL(1, 2);

void api_prop_collection_begin(ApiPtr *ptr,
                              ApiProp *prop,
                              CollectionPropIter *iter);
void api_prop_collection_next(CollectionPropIter *iter);
void api_prop_collection_skip(CollectionPropIter *iter, int num);
void api_prop_collection_end(CollectionPropIter *iter);
int api_prop_collection_length(ApiPtr *ptr, ApiProp *prop);
/* Return true when `api_prop_collection_length(ptr, prop) == 0`,
 * without having to iterate over items in the collection (needed for some kinds of collections). */
bool api_prop_collection_is_empty(ApiPtr *ptr, ApiProp *prop);
int api_prop_collection_lookup_index(ApiPtr *ptr, ApiProp *prop, ApiPtr *t_ptr);
int api_prop_collection_lookup_int(ApiPtr *ptr,
                                   ApiProp *prop,
                                   int key,
                                   ApiPtr *r_ptr);
int api_prop_collection_lookup_string(ApiPtr *ptr,
                                      ApiProp *prop,
                                      const char *key,
                                      ApiPtr *r_ptr);
int api_prop_collection_lookup_string_index(
    ApiPtr *ptr, ApiProp *prop, const char *key, ApiPtr *r_ptr, int *r_index);
/* Zero return is an assignment error. */
int api_prop_collection_assign_int(ApiPtr *ptr,
                                   ApiProp *prop,
                                   int key,
                                   const ApiPtr *assign_ptr);
bool api_prop_collection_type_get(ApiPtr *ptr, ApiProp *prop, PointerRNA *r_ptr);

/* efficient functions to set properties for arrays */
int api_prop_collection_raw_array(ApiPtr *ptr,
                                  ApiProp *prop,
                                  ApiProp *itemprop,
                                  RawArray *array);
int api_prop_collection_raw_get(struct ReportList *reports,
                                ApiPtr *ptr,
                                ApiProp *prop,
                                const char *propname,
                                void *array,
                                RawPropType type,
                                int len);
int api_prop_collection_raw_set(struct ReportList *reports,
                                ApiPtr *ptr,
                                ApiProp *prop,
                                const char *propname,
                                void *array,
                                RawPropType type,
                                int len);
int api_raw_type_sizeof(RawPropType type);
RawPropType api_prop_raw_type(ApiProp *prop);

/* to create id prop groups */
void api_prop_ptr_add(ApiPtr *ptr, ApiProp *prop);
void api_prop_ptr_remove(ApiPtr *ptr, ApiProp *prop);
void api_prop_collection_add(ApiPtr *ptr, ApiProp *prop, ApiPtr *r_ptr);
bool api_prop_collection_remove(ApiPtr *ptr, ApiProp *prop, int key);
void api_prop_collection_clear(ApiPtr *ptr, ApiProp *prop);
bool api_prop_collection_move(ApiPtr *ptr, ApiProp *prop, int key, int pos);
/* copy/reset */
bool api_prop_copy(
    struct Main *main, ApiPtr *ptr, ApiPtr *fromptr, ApiProp *prop, int index);
bool api_prop_reset(ApiPtr *ptr, ApiProp *prop, int index);
bool api_prop_assign_default(ApiPtr *ptr, ApiProp *prop);

/* Path
 * Experimental method to refer to structs and props w a string,
 * using a syntax like: scenes[0].objects["Cube"].data.verts[7].co
 *
 * This provides a way to refer to api data while being detached from any
 * particular ptrs, which is useful in a number of applications, like
 * UI code or Actions, though efficiency is a concern. */
char *api_path_append(
    const char *path, ApiPtr *ptr, ApiProp *prop, int intkey, const char *strkey);
#if 0 /* UNUSED. */
char *api_path_back(const char *path);
#endif

/* api_path_resolve() variants only ensure that a valid ptr (and optionally prop) exist. */

/* Resolve the given api Path to find the ptr and/or prop
 * indicated by fully resolving the path.
 *
 * warning Unlike api_path_resolve_prop(), that one *will* try to follow ApiPtrs,
 * e.g. the path 'parent' applied to a ApiObject ptr will return the object.parent in \a r_ptr,
 * and a NULL r_prop...
 *
 * note Assumes all ptrs provided are valid
 * return True if path can be resolved to a valid "ptr + prop" OR "ptr only" */
bool api_path_resolve(ApiPtr *ptr, const char *path, ApiPtr *r_ptr, ApiProp **r_prop);

/* Resolve the given api Path to find the pointer and/or property + array index
 * indicated by fully resolving the path.
 *
 * note Assumes all ptrs provided are valid.
 * return True if path can be resolved to a valid "pointer + property" OR "pointer only" */
bool api_path_resolve_full(
    ApiPtr *ptr, const char *path, ApiPtr *r_ptr, ApiProp **r_prop, int *r_index);
/* A version of api_path_resolve_full doesn't check the value of ApiPtr.data.
 *
 * note While it's correct to ignore the value of ApiPtr.data
 * most callers need to know if the resulting pointer was found and not null */
bool api_path_resolve_full_maybe_null(
    ApiPtr *ptr, const char *path, ApiPtr *r_ptr, ApiProp **r_prop, int *r_index);

/* api_path_resolve_prop() variants ensure that ptr + prop both exist. */

/* Resolve the given api Path to find both the ptr AND prop
 * indicated by fully resolving the path.
 *
 * This is a convenience method to avoid logic errors and ugly syntax.
 * note Assumes all ptrs provided are valid
 * return True only if both a valid ptr and prop are found after resolving the path */
bool api_path_resolve_prop(ApiPtr *ptr,
                           const char *path,
                           ApiPtr *r_ptr,
                           ApiProp **r_prop);

/* Resolve the given api Path to find the pointer AND property (as well as the array index)
 * indicated by fully resolving the path.
 *
 * This is a convenience method to avoid logic errors and ugly syntax.
 * note Assumes all pointers provided are valid
 * return True only if both a valid pointer and property are found after resolving the path */
bool api_path_resolve_prop_full(
    ApiPtr *ptr, const char *path, ApiPtr *r_ptr, ApiProp **r_prop, int *r_index);

/* api_path_resolve_prop_and_item_ptr() variants ensure that ptr + prop both exist,
 * and resolve last Ptr value if possible (Ptr prop or item of a Collection prop). */

/* Resolve the given api Path to find both the pointer AND property
 * indicated by fully resolving the path, and get the value of the Pointer property
 * (or item of the collection).
 *
 * This is a convenience method to avoid logic errors and ugly syntax,
 * it combines both api_path_resolve and api_path_resolve_prop in a single call.
 * note Assumes all ptrs provided are valid.
 * param r_item_ptr: The final Pointer or Collection item value.
 * You must check for its validity before use!
 * return True only if both a valid pointer and prop are found after resolving the path */
bool api_path_resolve_prop_and_item_ptr(ApiPtr *ptr,
                                        const char *path,
                                        ApiPtr *r_ptr,
                                        ApiProp **r_prop,
                                        ApiPtr *r_item_ptr);

/* Resolve the given api Path to find both the ptr AND prop (as well as the array index)
 * indicated by fully resolving the path,
 * and get the value of the Ptr prop (or item of the collection).
 *
 * This is a convenience method to avoid logic errors and ugly syntax,
 * it combines both api_path_resolve_full and
 * api_path_resolve_prop_full in a single call.
 * Assumes all pointers provided are valid.
 * param r_item_ptr: The final Ptr or Collection item value.
 * You must check for its validity before use!
 * return True only if both a valid ptr and prop are found after resolving the path */
bool api_path_resolve_prop_and_item_ptr_full(ApiPtr *ptr,
                                             const char *path,
                                             ApiPtr *r_ptr,
                                             ApiProp **r_prop,
                                             int *r_index,
                                             ApiPtr *r_item_ptr);

typedef struct ApiPropElem ApiPropElem;
struct ApiPropElem {
  ApiPropElem *next, *prev;
  ApiPtr ptr;
  ApiProp *prop;
  int index;
};
/* Resolve the given api Path into a linked list of ApiPropElem's.
 * To be used when complex ops over path are needed, like e.g. get relative paths,
 * to avoid too much string ops.
 *
 * return True if there was no error while resolving the path
 * Assumes all ptrs provided are valid */
bool api_path_resolve_elements(ApiPtr *ptr, const char *path, struct List *r_elements);

/* Find the path from the structure refd by the ptr to the runtime api-defined
 * IdProp object.
 *
 * note Does *not* handle pure user-defined IdProps (a.k.a. custom props).
 *
 * param ptr: Ref to the object owning the custom prop storage.
 * param needle: Custom prop object to find.
 * return Relative path or NULL. */
char *api_path_from_struct_to_idprop(ApiPtr *ptr, struct IdProp *needle);

/* Find the actual Id ptr and path from it to the given Id.
 *
 * param id: Id ref to search the global owner for.
 * param[out] r_path: Path from the real Id to the initial Id.
 * return The Id ptr, or NULL in case of failure. */
struct Id *api_find_real_id_and_path(struct Main *main, struct Id *id, const char **r_path);

char *api_path_from_id_to_struct(const ApiPtr *ptr);

char *api_path_from_real_id_to_struct(struct Main *main, ApiPtr *ptr, struct Id **r_real);

char *api_path_from_id_to_prop(ApiPtr *ptr, ApiProp *prop);
/* param index_dim: The dimension to show, 0 disables. 1 for 1d array, 2 for 2d. etc.
 * param index: The *flattened* index to use when `index_dim > 0`,
 * this is expanded when used with multi-dimensional arrays. */
char *api_path_from_id_to_prop_index(ApiPtr *ptr,
                                     ApiProp *prop,
                                     int index_dim,
                                     int index);

char *api_path_from_real_id_to_prop_index(struct Main *main,
                                          ApiPtr *ptr,
                                          ApiProp *prop,
                                          int index_dim,
                                          int index,
                                          struct Id **r_real_id);

/* return the path to given ptr/prop from the closest ancestor of given type,
 * if any (else return NULL). */
char *api_path_resolve_from_type_to_prop(struct ApiPtr *ptr,
                                         struct ApiProp *prop,
                                         const struct ApiStruct *type);

/* Get the Id as a python representation, eg:
 *   bpy.data.foo["bar"] */
char *api_path_full_id_py(struct Main *main, struct Id *id);
/* Get the ID.struct as a python representation, eg:
 *   bpy.data.foo["bar"].some_struct */
char *api_path_full_struct_py(struct Main *main, struct ApiPtr *ptr);
/* Get the ID.struct.prop as a python representation, eg:
 *   bpy.data.foo["bar"].some_struct.some_prop[10] */
char *api_path_full_prop_py_ex(
    struct Main *main, ApiPtr *ptr, ApiProp *prop, int index, bool use_fallback);
char *api_path_full_prop_py(struct Main *main,
                            struct ApiPtr *ptr,
                            struct ApiProp *prop,
                            int index);
/* Get the struct.prop as a python representation, eg:
 *   some_struct.some_prop[10] */
char *api_path_struct_prop_py(struct ApiPtr *ptr, struct ApiProp *prop, int index);
/* Get the struct.prop as a python representation, eg:
 *   some_prop[10] */
char *api_path_prop_py(const struct ApiPtr *ptr, struct ApiProp *prop, int index);

/* Quick name based prop access
 *
 * These are just an easier way to access property values without having to
 * call api_struct_find_prop. The names have to exist as api props
 * for the type in the ptr, if they do not exist an error will be printed.
 *
 * There is no support for ptrs and collections here yet, these can be
 * added when Id props support them. */
bool api_bool_get(ApiPtr *ptr, const char *name);
void api_bool_set(ApiPtr *ptr, const char *name, bool value);
void api_bool_get_array(ApiPtr *ptr, const char *name, bool *values);
void api_bool_set_array(ApiPtr *ptr, const char *name, const bool *values);

int api_int_get(ApiPtr *ptr, const char *name);
void api_int_set(ApiPtr *ptr, const char *name, int value);
void api_int_get_array(ApiPtr *ptr, const char *name, int *values);
void api_int_set_array(ApiPtr *ptr, const char *name, const int *values);

float api_float_get(ApiPtr *ptr, const char *name);
void api_float_set(ApiPtr *ptr, const char *name, float value);
void api_float_get_array(ApiPtr *ptr, const char *name, float *values);
void api_float_set_array(ApiPtr *ptr, const char *name, const float *values);

int api_enum_get(ApiPtr *ptr, const char *name);
void api_enum_set(ApiPtr *ptr, const char *name, int value);
void api_enum_set_id(struct Cxt *C,
                     ApiPtr *ptr,
                     const char *name,
                     const char *id);
bool api_enum_is_equal(struct Cxt *C,
                       ApiPtr *ptr,
                       const char *name,
                       const char *enumname);

/* Lower level functions that don't use a ApiPtr. */
bool api_enum_value_from_id(const EnumPropItem *item, const char *id, int *r_value);
bool api_enum_id_from_value(const EnumPropItem *item, int value, const char **r_id);
bool api_enum_icon_from_value(const EnumPropItem *item, int value, int *r_icon);
bool api_enum_name_from_value(const EnumPropItem *item, int value, const char **r_name);

void api_string_get(ApiPtr *ptr, const char *name, char *value);
char *api_string_get_alloc(
    ApiPtr *ptr, const char *name, char *fixedbuf, int fixedlen, int *r_len);
int api_string_length(ApiPtr *ptr, const char *name);
void api_string_set(ApiPtr *ptr, const char *name, const char *value);

/* Retrieve the named property from ApiPtr */
ApiPtr api_ptr_get(ApiPtr *ptr, const char *name);
/* Set the prop name of ApiPtr ptr to ptr_value */
void api_ptr_set(ApiPtr *ptr, const char *name, ApiPtr ptr_value);
void api_ptr_add(ApiPtr *ptr, const char *name);

void api_collection_begin(ApiPtr *ptr, const char *name, CollectionPropIter *iter);
int api_collection_length(ApiPtr *ptr, const char *name);
bool api_collection_is_empty(ApiPtr *ptr, const char *name);
void api_collection_add(ApiPtr *ptr, const char *name, ApiPtr *r_value);
void api_collection_clear(ApiPtr *ptr, const char *name);

#define API_BEGIN(sptr, itemptr, propname) \
  { \
    CollectionPropIter api_macro_iter; \
    for (api_collection_begin(sptr, propname, &api_macro_iter); api_macro_iter.valid; \
         api_prop_collection_next(&api_macro_iter)) { \
      ApiPtr itemptr = api_macro_iter.ptr;

#define API_END \
  } \
  api_prop_collection_end(&api_macro_iter); \
  } \
  ((void)0)

#define API_PROP_BEGIN(sptr, itemptr, prop) \
  { \
    CollectionPropIter api_macro_iter; \
    for (api_prop_collection_begin(sptr, prop, &api_macro_iter); api_macro_iter.valid; \
         api_prop_collection_next(&api_macro_iter)) { \
      ApiPtr itemptr = api_macro_iter.ptr;

#define API_PROP_END \
  } \
  api_prop_collection_end(&api_macro_iter); \
  } \
  ((void)0)

#define API_STRUCT_BEGIN(sptr, prop) \
  { \
    CollectionPropIter api_macro_iter; \
    for (api_prop_collection_begin( \
             sptr, api_struct_iter_prop((sptr)->type), &api_macro_iter); \
         api_macro_iter.valid; \
         api_prop_collection_next(&api_macro_iter)) { \
      ApiProp *prop = (ApiProp *)api_macro_iter.ptr.data;

#define API_STRUCT_BEGIN_SKIP_API_TYPE(sptr, prop) \
  { \
    CollectionPropIter api_macro_iter; \
    api_prop_collection_begin( \
        sptr, api_struct_iter_prop((sptr)->type), &api_macro_iter); \
    if (api_macro_iter.valid) { \
      api_prop_collection_next(&api_macro_iter); \
    } \
    for (; api_macro_iter.valid; api_prop_collection_next(&api_macro_iter)) { \
      ApiProp *prop = (ApiProp *)api_macro_iter.ptr.data;

#define API_STRUCT_END \
  } \
  api_prop_collection_end(&api_macro_iter); \
  } \
  ((void)0)

/* Check if the Idprop exists, for ops.
 *
 * param use_ghost: Internally an IdProp may exist,
 * without the api considering it to be "set", see IDP_FLAG_GHOST.
 * This is used for ops, where executing an op that has run previously
 * will re-use the last value (unless PROP_SKIP_SAVE prop is set).
 * In this case, the presence of the an existing value shouldn't prevent it being initialized
 * from the context. Even though the this value will be returned if it's requested,
 * it's not considered to be set (as it would if the menu item or key-map defined it's value).
 * Set `use_ghost` to true for default behavior, otherwise false to check if there is a value
 * exists internally and would be returned on request. */
bool api_prop_is_set_ex(ApiPtr *ptr, ApiProp *prop, bool use_ghost);
bool api_prop_is_set(ApiPtr *ptr, ApiProp *prop);
void api_prop_unset(ApiPtr *ptr, ApiProp *prop);
/** See api_prop_is_set_ex documentation.  */
bool api_struct_prop_is_set_ex(ApiPtr *ptr, const char *id, bool use_ghost);
bool api_struct_prop_is_set(ApiPtr *ptr, const char *id);
bool api_prop_is_idprop(const AoiProp *prop);
/* Mainly for the UI. */
bool api_prop_is_unlink(ApiProp *prop);
void api_struct_prop_unset(ApiPtr *ptr, const char *id);

/* Python compatible string representation of this prop, (must be freed!). */
char *api_prop_as_string(
    struct Cxt *C, ApiPtr *ptr, ApiProp *prop, int index, int max_prop_length);
/* String representation of a prop, Python compatible but can be used for display too.
 * param C: can be NULL. */
char *api_ptr_as_string_id(struct Cxt *C, ApiPtr *ptr);
char *api_ptr_as_string(struct Cxt *C,
                            ApiPtr *ptr,
                            ApiProp *prop_ptr,
                            ApiPtr *ptr_prop);
/* param C: can be NULL. */
char *api_ptr_as_string_keywords_ex(struct Cxt *C,
                                    ApiPtr *ptr,
                                    bool as_fn,
                                    bool all_args,
                                    bool nested_args,
                                    int max_prop_length,
                                    ApiProp *iterprop);
char *api_ptr_as_string_keywords(struct Cxt *C,
                                     ApiPtr *ptr,
                                     bool as_fn,
                                     bool all_args,
                                     bool nested_args,
                                     int max_prop_length);
char *api_fn_as_string_keywords(
    struct Cxt *C, ApiFn *fn, bool as_fn, bool all_args, int max_prop_length);

/* Fn */
const char *api_fn_id(ApiFn *fn);
const char *api_fn_ui_description(ApiFn *fn);
const char *api_fn_ui_description_raw(ApiFn *fn);
int api_fn_flag(ApiFn *fn);
int api_fn_defined(ApiFn *fn);

ApiProp *api_fn_get_param(ApiPtr *ptr, ApiFn *fn, int index);
ApiProp *api_fn_find_param(ApiPtr *ptr,
                           ApiFn *fn,
                           const char *id);
const struct List *api_fn_defined_params(ApiFn *fn);

/* Util */
int api_param_flag(ApiProp *prop);

ParamList *api_param_list_create(ParamList *parms, ApiPtr *ptr, ApiFn *fn);
void api_param_list_free(ParamList *parms);
int api_param_list_size(const ParamList *parms);
int api_param_list_arg_count(const ParamList *parms);
int api_param_list_ret_count(const ParamList *parms);

void api_param_list_begin(ParamList *parms, ParamIter *iter);
void api_param_list_next(ParamIter *iter);
void api_param_list_end(ParamIter *iter);

void api_param_get(ParamList *parms, ApiProp *parm, void **value);
void api_param_get_lookup(ParamList *parms, const char *id, void **value);
void api_param_set(ParamList *parms, ApiProp *parm, const void *value);
void api_param_set_lookup(ParamList *parms, const char *id, const void *value);

/* Only for PROP_DYNAMIC properties! */
int api_param_dynamic_length_get(ParamList *parms, ApiProp *parm);
int api_param_dynamic_length_get_data(ParamList *parms, ApiProp *parm, void *data);
void api_param_dynamic_length_set(ParamList *parms, ApiProp *parm, int length);
void api_param_dynamic_length_set_data(ParamList *parms,
                                       ApiProp *parm,
                                       void *data,
                                       int length);

int api_fn_call(struct Cxt *C,
                struct ReportList *reports,
                ApiPtr *ptr,
                ApiFn *fn,
                ParamList *parms);
int api_fn_call_lookup(struct Cxt *C,
                       struct ReportList *reports,
                       ApiPtr *ptr,
                       const char *id,
                       ParamList *parms);

int api_fn_call_direct(struct Cxt *C,
                       struct ReportList *reports,
                       ApiPtr *ptr,
                       ApiFn *fn,
                       const char *format,
                       ...) ATTR_PRINTF_FORMAT(5, 6);
int api_fn_call_direct_lookup(struct Cxt *C,
                              struct ReportList *reports,
                              ApiPtr *ptr,
                              const char *id,
                              const char *format,
                              ...) ATTR_PRINTF_FORMAT(5, 6);
int api_fn_call_direct_va(struct Cxt *C,
                          struct ReportList *reports,
                          ApiPtr *ptr,
                          ApiFn *fn,
                          const char *format,
                          va_list args);
int api_fn_call_direct_va_lookup(struct Cxt *C,
                                 struct ReportList *reports,
                                 ApiPtr *ptr,
                                 const char *id,
                                 const char *format,
                                 va_list args);

const char *api_lang_ui_text(const char *text,
                             const char *text_ctxt,
                             struct ApiStruct *type,
                             struct ApiProp *prop,
                             int lang);

/* Id */
short api_type_to_id_code(const ApiStruct *type);
ApiStruct *id_code_to_api_type(short idcode);

#define API_PTR_INVALIDATE(ptr) \
  { \
    /* this is checked for validity */ \
    (ptr)->type = NULL; /* should not be needed but prevent bad pointer access, just in case */ \
    (ptr)->owner_id = NULL; \
  } \
  (void)0

/* macro which inserts the function name */
#if defined __GNUC__
#  define api_warning(format, args...) _api_warning("%s: " format "\n", __func__, ##args)
#else
#  define api_warning(format, ...) _api_warning("%s: " format "\n", __FUNCTION__, __VA_ARGS__)
#endif

/* Use to implement the api_warning macro which includes `__func__` suffix. */
void _api_warning(const char *format, ...) ATTR_PRINTF_FORMAT(1, 2);

/* Equals test. */
/* In practice, EQ_STRICT and EQ_COMPARE have same behavior currently,
 * and will yield same result. */
typedef enum eApiCompareMode {
  /* Only care about equality, not full comparison. */
  /* Set/unset ignored. */
  API_EQ_STRICT,
  /* Unset prop matches anything. */
  API_EQ_UNSET_MATCH_ANY,
  /* Unset prop never matches set prop. */
  API_EQ_UNSET_MATCH_NONE,
  /* Full comparison. */
  API_EQ_COMPARE,
} eApiCompareMode;

bool api_prop_equals(struct Main *main,
                     struct ApiPtr *ptr_a,
                     struct ApiPtr *ptr_b,
                     struct ApiProp *prop,
                     eApiCompareMode mode);
bool api_struct_equals(struct Main *main,
                       struct ApiPtr *ptr_a,
                       struct ApiPtr *ptr_b,
                       eApiCompareMode mode);

/* Override. */
/* Flags for api_struct_override_matches. */
typedef enum eApiOverrideMatch {
  /* Do not compare props that are not overridable. */
  API_OVERRIDE_COMPARE_IGNORE_NON_OVERRIDABLE = 1 << 0,
  /* Do not compare props that are already overridden. */
  API_OVERRIDE_COMPARE_IGNORE_OVERRIDDEN = 1 << 1,
  /* Create new prop override if needed and possible. */
  API_OVERRIDE_COMPARE_CREATE = 1 << 16,
  /* Restore prop's value(s) to reference ones if needed and possible. */
  API_OVERRIDE_COMPARE_RESTORE = 1 << 17,
} eApiOverrideMatch;

typedef enum eApiOverrideMatchResult {
  /* Some new prop overrides were created to take into account
   * differences between local and ref. */
  API_OVERRIDE_MATCH_RESULT_CREATED = 1 << 0,
  /* Some properties were reset to reference values. */
  API_OVERRIDE_MATCH_RESULT_RESTORED = 1 << 1,
} eRNAOverrideMatchResult;

typedef enum eApiOverrideStatus {
  /* The prop is overridable. */
  API_OVERRIDE_STATUS_OVERRIDABLE = 1 << 0,
  /* The prop is overridden. */
  API_OVERRIDE_STATUS_OVERRIDDEN = 1 << 1,
  /* Overriding this prop is mandatory when creating an override. */
  API_OVERRIDE_STATUS_MANDATORY = 1 << 2,
  /* The override status of this property is locked. */
  API_OVERRIDE_STATUS_LOCKED = 1 << 3,
} eApiOverrideStatus;

/* Check whether re and local overridden data match (are the same),
 * with respect to given restrictive sets of props.
 * If requested, will generate needed new prop overrides, and/or restore values from reference.
 *
 * param r_report_flags: If given,
 * will be set with flags matching actions taken by the fn on ptr_local.
 *
 * return True if _resulting_ ptr_local does match ptr_ref. */
bool api_struct_override_matches(struct Main *main,
                                 struct ApiPtr *ptr_local,
                                 struct ApiPtr *ptr_ref,
                                 const char *root_path,
                                 size_t root_path_len,
                                 struct IdOverrideLib *override,
                                 eApiOverrideMatch flags,
                                 eApiOverrideMatchResult *r_report_flags);

/* Store needed second operands into storage data-block
 * for differential override ops. */
bool api_struct_override_store(struct Main *main,
                               struct ApiPtr *ptr_local,
                               struct ApiPtr *ptr_ref,
                               ApiPtr *ptr_storage,
                               struct IdOverrideLib *override);

typedef enum eApiOverrideApplyFlag {
  API_OVERRIDE_APPLY_FLAG_NOP = 0,
  /* Hack to work around/fix older broken overrides: Do not apply override ops affecting Id
   * ptrs props, unless the destination original value (the one being overridden) is NULL. */
  API_OVERRIDE_APPLY_FLAG_IGNORE_ID_PTRS = 1 << 0,
} eApiOverrideApplyFlag;

/* Apply given override operations on ptr_dst, using ptr_src
 * (and ptr_storage for differential ops) as src. */
void api_struct_override_apply(struct Main *main,
                               struct ApiPtr *ptr_dst,
                               struct ApiPtr *ptr_src,
                               struct ApiPtr *ptr_storage,
                               struct IdOverrideLib *override,
                               eApiOverrideApplyFlag flag);

struct IdOverrideLibProp *api_prop_override_prop_find(struct Main *main,
                                                              ApiPtr *ptr,
                                                              ApiProp *prop,
                                                              struct Id **r_owner_id);
struct IdOverrideLibProp *api_prop_override_prop_get(struct Main *main,
                                                     ApiPtr *ptr,
                                                     ApiProp *prop,
                                                     bool *r_created);

struct IdOverrideLibPropOp *api_prop_override_prop_op_find(
    struct Main *main,
    ApiPtr *ptr,
    ApiProp *prop,
    int index,
    bool strict,
    bool *r_strict);
struct IdOverrideLibPropOp *api_prop_override_prop_op_get(
    struct Main *main,
    ApiPtr *ptr,
    ApiProp *prop,
    short oper,
    int index,
    bool strict,
    bool *r_strict,
    bool *r_created);

eApiOverrideStatus api_prop_override_lib_status(struct Main *main,
                                                ApiPtr *ptr,
                                                ApiProp *prop,
                                                int index);

void api_struct_state_owner_set(const char *name);
const char *api_struct_state_owner_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __API_ACCESS_H__ */
