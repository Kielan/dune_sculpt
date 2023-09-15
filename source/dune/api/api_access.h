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

/* Ptr
 * These fns will fill in api ptrs, this can be done in three ways:
 * - a ptr Main is created by just passing the data ptr
 * - a ptr to a datablock can be created with the type and id data ptr
 * - a ptr to data contained in a datablock can be created with the id type
 *   and id data ptr, and the data type and ptr to the struct itself.
 *
 * There is also a way to get a pointer with the information about all structs. */

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
/* Remove an id-property. */
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
 * Access to struct props. All this works with RNA pointers rather than
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
/* Return true when `RNA_property_collection_length(ptr, prop) == 0`,
 * without having to iterate over items in the collection (needed for some kinds of collections). */
bool api_prop_collection_is_empty(ApiPtr *ptr, ApiProp *prop);
int api_prop_collection_lookup_index(ApiPtr *ptr, ApiProp *prop, ApiPtr *t_ptr);
int api_prop_collection_lookup_int(PointerRNA *ptr,
                                   ApiProp *prop,
                                   int key,
                                   ApiPtr *r_ptr);
int api_prop_collection_lookup_string(ApiPtr *ptr,
                                      ApiProp *prop,
                                      const char *key,
                                      ApiPtr *r_ptr);
int api_prop_collection_lookup_string_index(
    PointerRNA *ptr, PropertyRNA *prop, const char *key, ApiPtr *r_ptr, int *r_index);
/**
 * Zero return is an assignment error.
 */
int RNA_prop_collection_assign_int(ApiPtr *ptr,
                                   ApiProp *prop,
                                   int key,
                                   const ApiPtr *assign_ptr);
bool RNA_prop_collection_type_get(ApiPtr *ptr, ApiProp *prop, PointerRNA *r_ptr);

/* efficient functions to set properties for arrays */
int RNA_property_collection_raw_array(PointerRNA *ptr,
                                      PropertyRNA *prop,
                                      PropertyRNA *itemprop,
                                      RawArray *array);
int RNA_property_collection_raw_get(struct ReportList *reports,
                                    PointerRNA *ptr,
                                    PropertyRNA *prop,
                                    const char *propname,
                                    void *array,
                                    RawPropertyType type,
                                    int len);
int RNA_property_collection_raw_set(struct ReportList *reports,
                                    PointerRNA *ptr,
                                    PropertyRNA *prop,
                                    const char *propname,
                                    void *array,
                                    RawPropertyType type,
                                    int len);
int RNA_raw_type_sizeof(RawPropertyType type);
RawPropertyType RNA_property_raw_type(PropertyRNA *prop);

/* to create ID property groups */
void RNA_property_pointer_add(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_pointer_remove(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_collection_add(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr);
bool RNA_property_collection_remove(PointerRNA *ptr, PropertyRNA *prop, int key);
void RNA_property_collection_clear(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_collection_move(PointerRNA *ptr, PropertyRNA *prop, int key, int pos);
/* copy/reset */
bool RNA_property_copy(
    struct Main *bmain, PointerRNA *ptr, PointerRNA *fromptr, PropertyRNA *prop, int index);
bool RNA_property_reset(PointerRNA *ptr, PropertyRNA *prop, int index);
bool RNA_property_assign_default(PointerRNA *ptr, PropertyRNA *prop);

/* Path
 *
 * Experimental method to refer to structs and properties with a string,
 * using a syntax like: scenes[0].objects["Cube"].data.verts[7].co
 *
 * This provides a way to refer to RNA data while being detached from any
 * particular pointers, which is useful in a number of applications, like
 * UI code or Actions, though efficiency is a concern. */

char *RNA_path_append(
    const char *path, PointerRNA *ptr, PropertyRNA *prop, int intkey, const char *strkey);
#if 0 /* UNUSED. */
char *RNA_path_back(const char *path);
#endif

/* RNA_path_resolve() variants only ensure that a valid pointer (and optionally property) exist. */

/**
 * Resolve the given RNA Path to find the pointer and/or property
 * indicated by fully resolving the path.
 *
 * \warning Unlike \a RNA_path_resolve_property(), that one *will* try to follow RNAPointers,
 * e.g. the path 'parent' applied to a RNAObject \a ptr will return the object.parent in \a r_ptr,
 * and a NULL \a r_prop...
 *
 * \note Assumes all pointers provided are valid
 * \return True if path can be resolved to a valid "pointer + property" OR "pointer only"
 */
bool RNA_path_resolve(PointerRNA *ptr, const char *path, PointerRNA *r_ptr, PropertyRNA **r_prop);

/**
 * Resolve the given RNA Path to find the pointer and/or property + array index
 * indicated by fully resolving the path.
 *
 * \note Assumes all pointers provided are valid.
 * \return True if path can be resolved to a valid "pointer + property" OR "pointer only"
 */
bool RNA_path_resolve_full(
    PointerRNA *ptr, const char *path, PointerRNA *r_ptr, PropertyRNA **r_prop, int *r_index);
/**
 * A version of #RNA_path_resolve_full doesn't check the value of #PointerRNA.data.
 *
 * \note While it's correct to ignore the value of #PointerRNA.data
 * most callers need to know if the resulting pointer was found and not null.
 */
bool RNA_path_resolve_full_maybe_null(
    PointerRNA *ptr, const char *path, PointerRNA *r_ptr, PropertyRNA **r_prop, int *r_index);

/* RNA_path_resolve_property() variants ensure that pointer + property both exist. */

/**
 * Resolve the given RNA Path to find both the pointer AND property
 * indicated by fully resolving the path.
 *
 * This is a convenience method to avoid logic errors and ugly syntax.
 * \note Assumes all pointers provided are valid
 * \return True only if both a valid pointer and property are found after resolving the path
 */
bool RNA_path_resolve_property(PointerRNA *ptr,
                               const char *path,
                               PointerRNA *r_ptr,
                               PropertyRNA **r_prop);

/**
 * Resolve the given RNA Path to find the pointer AND property (as well as the array index)
 * indicated by fully resolving the path.
 *
 * This is a convenience method to avoid logic errors and ugly syntax.
 * \note Assumes all pointers provided are valid
 * \return True only if both a valid pointer and property are found after resolving the path
 */
bool RNA_path_resolve_property_full(
    PointerRNA *ptr, const char *path, PointerRNA *r_ptr, PropertyRNA **r_prop, int *r_index);

/* RNA_path_resolve_property_and_item_pointer() variants ensure that pointer + property both exist,
 * and resolve last Pointer value if possible (Pointer prop or item of a Collection prop). */

/**
 * Resolve the given RNA Path to find both the pointer AND property
 * indicated by fully resolving the path, and get the value of the Pointer property
 * (or item of the collection).
 *
 * This is a convenience method to avoid logic errors and ugly syntax,
 * it combines both \a RNA_path_resolve and #RNA_path_resolve_property in a single call.
 * note Assumes all pointers provided are valid.
 * param r_item_ptr: The final Pointer or Collection item value.
 * You must check for its validity before use!
 * return True only if both a valid pointer and property are found after resolving the path
 */
bool api_path_resolve_prop_and_item_ptr(ApiPtr *ptr,
                                        const char *path,
                                        PointerRNA *r_ptr,
                                        PropertyRNA **r_prop,
                                        PointerRNA *r_item_ptr);

/* Resolve the given RNA Path to find both the pointer AND property (as well as the array index)
 * indicated by fully resolving the path,
 * and get the value of the Pointer property (or item of the collection).
 *
 * This is a convenience method to avoid logic errors and ugly syntax,
 * it combines both \a RNA_path_resolve_full and
 * \a RNA_path_resolve_property_full in a single call.
 * \note Assumes all pointers provided are valid.
 * \param r_item_ptr: The final Pointer or Collection item value.
 * You must check for its validity before use!
 * \return True only if both a valid pointer and property are found after resolving the path
 */
bool RNA_path_resolve_property_and_item_pointer_full(PointerRNA *ptr,
                                                     const char *path,
                                                     PointerRNA *r_ptr,
                                                     PropertyRNA **r_prop,
                                                     int *r_index,
                                                     PointerRNA *r_item_ptr);

typedef struct PropertyElemRNA PropertyElemRNA;
struct PropertyElemRNA {
  PropertyElemRNA *next, *prev;
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;
};
/**
 * Resolve the given RNA Path into a linked list of #PropertyElemRNA's.
 *
 * To be used when complex operations over path are needed, like e.g. get relative paths,
 * to avoid too much string operations.
 *
 * \return True if there was no error while resolving the path
 * \note Assumes all pointers provided are valid
 */
bool RNA_path_resolve_elements(PointerRNA *ptr, const char *path, struct ListBase *r_elements);

/**
 * Find the path from the structure referenced by the pointer to the runtime RNA-defined
 * #IDProperty object.
 *
 * \note Does *not* handle pure user-defined IDProperties (a.k.a. custom properties).
 *
 * \param ptr: Reference to the object owning the custom property storage.
 * \param needle: Custom property object to find.
 * \return Relative path or NULL.
 */
char *RNA_path_from_struct_to_idproperty(PointerRNA *ptr, struct IDProperty *needle);

/**
 * Find the actual ID pointer and path from it to the given ID.
 *
 * \param id: ID reference to search the global owner for.
 * \param[out] r_path: Path from the real ID to the initial ID.
 * \return The ID pointer, or NULL in case of failure.
 */
struct ID *RNA_find_real_ID_and_path(struct Main *bmain, struct ID *id, const char **r_path);

char *RNA_path_from_ID_to_struct(const PointerRNA *ptr);

char *RNA_path_from_real_ID_to_struct(struct Main *bmain, PointerRNA *ptr, struct ID **r_real);

char *RNA_path_from_ID_to_property(PointerRNA *ptr, PropertyRNA *prop);
/**
 * \param index_dim: The dimension to show, 0 disables. 1 for 1d array, 2 for 2d. etc.
 * \param index: The *flattened* index to use when \a `index_dim > 0`,
 * this is expanded when used with multi-dimensional arrays.
 */
char *RNA_path_from_ID_to_property_index(PointerRNA *ptr,
                                         PropertyRNA *prop,
                                         int index_dim,
                                         int index);

char *RNA_path_from_real_ID_to_property_index(struct Main *bmain,
                                              PointerRNA *ptr,
                                              PropertyRNA *prop,
                                              int index_dim,
                                              int index,
                                              struct ID **r_real_id);

/**
 * \return the path to given ptr/prop from the closest ancestor of given type,
 * if any (else return NULL).
 */
char *RNA_path_resolve_from_type_to_property(struct PointerRNA *ptr,
                                             struct PropertyRNA *prop,
                                             const struct StructRNA *type);

/**
 * Get the ID as a python representation, eg:
 *   bpy.data.foo["bar"]
 */
char *RNA_path_full_ID_py(struct Main *bmain, struct ID *id);
/**
 * Get the ID.struct as a python representation, eg:
 *   bpy.data.foo["bar"].some_struct
 */
char *RNA_path_full_struct_py(struct Main *bmain, struct PointerRNA *ptr);
/**
 * Get the ID.struct.property as a python representation, eg:
 *   bpy.data.foo["bar"].some_struct.some_prop[10]
 */
char *RNA_path_full_property_py_ex(
    struct Main *bmain, PointerRNA *ptr, PropertyRNA *prop, int index, bool use_fallback);
char *RNA_path_full_property_py(struct Main *bmain,
                                struct PointerRNA *ptr,
                                struct PropertyRNA *prop,
                                int index);
/**
 * Get the struct.property as a python representation, eg:
 *   some_struct.some_prop[10]
 */
char *RNA_path_struct_property_py(struct PointerRNA *ptr, struct PropertyRNA *prop, int index);
/**
 * Get the struct.property as a python representation, eg:
 *   some_prop[10]
 */
char *RNA_path_property_py(const struct PointerRNA *ptr, struct PropertyRNA *prop, int index);

/* Quick name based property access
 *
 * These are just an easier way to access property values without having to
 * call RNA_struct_find_property. The names have to exist as RNA properties
 * for the type in the pointer, if they do not exist an error will be printed.
 *
 * There is no support for pointers and collections here yet, these can be
 * added when ID properties support them. */

bool RNA_boolean_get(PointerRNA *ptr, const char *name);
void RNA_boolean_set(PointerRNA *ptr, const char *name, bool value);
void RNA_boolean_get_array(PointerRNA *ptr, const char *name, bool *values);
void RNA_boolean_set_array(PointerRNA *ptr, const char *name, const bool *values);

int RNA_int_get(PointerRNA *ptr, const char *name);
void RNA_int_set(PointerRNA *ptr, const char *name, int value);
void RNA_int_get_array(PointerRNA *ptr, const char *name, int *values);
void RNA_int_set_array(PointerRNA *ptr, const char *name, const int *values);

float RNA_float_get(PointerRNA *ptr, const char *name);
void RNA_float_set(PointerRNA *ptr, const char *name, float value);
void RNA_float_get_array(PointerRNA *ptr, const char *name, float *values);
void RNA_float_set_array(PointerRNA *ptr, const char *name, const float *values);

int RNA_enum_get(PointerRNA *ptr, const char *name);
void RNA_enum_set(PointerRNA *ptr, const char *name, int value);
void RNA_enum_set_identifier(struct bContext *C,
                             PointerRNA *ptr,
                             const char *name,
                             const char *id);
bool RNA_enum_is_equal(struct bContext *C,
                       PointerRNA *ptr,
                       const char *name,
                       const char *enumname);

/* Lower level functions that don't use a PointerRNA. */
bool RNA_enum_value_from_id(const EnumPropertyItem *item, const char *identifier, int *r_value);
bool RNA_enum_id_from_value(const EnumPropertyItem *item, int value, const char **r_identifier);
bool RNA_enum_icon_from_value(const EnumPropertyItem *item, int value, int *r_icon);
bool RNA_enum_name_from_value(const EnumPropertyItem *item, int value, const char **r_name);

void RNA_string_get(PointerRNA *ptr, const char *name, char *value);
char *RNA_string_get_alloc(
    PointerRNA *ptr, const char *name, char *fixedbuf, int fixedlen, int *r_len);
int RNA_string_length(PointerRNA *ptr, const char *name);
void RNA_string_set(PointerRNA *ptr, const char *name, const char *value);

/**
 * Retrieve the named property from PointerRNA.
 */
PointerRNA RNA_pointer_get(PointerRNA *ptr, const char *name);
/* Set the property name of PointerRNA ptr to ptr_value */
void RNA_pointer_set(PointerRNA *ptr, const char *name, PointerRNA ptr_value);
void RNA_pointer_add(PointerRNA *ptr, const char *name);

void RNA_collection_begin(PointerRNA *ptr, const char *name, CollectionPropertyIterator *iter);
int RNA_collection_length(PointerRNA *ptr, const char *name);
bool RNA_collection_is_empty(PointerRNA *ptr, const char *name);
void RNA_collection_add(PointerRNA *ptr, const char *name, PointerRNA *r_value);
void RNA_collection_clear(PointerRNA *ptr, const char *name);

#define RNA_BEGIN(sptr, itemptr, propname) \
  { \
    CollectionPropertyIterator rna_macro_iter; \
    for (RNA_collection_begin(sptr, propname, &rna_macro_iter); rna_macro_iter.valid; \
         RNA_property_collection_next(&rna_macro_iter)) { \
      PointerRNA itemptr = rna_macro_iter.ptr;

#define RNA_END \
  } \
  RNA_property_collection_end(&rna_macro_iter); \
  } \
  ((void)0)

#define RNA_PROP_BEGIN(sptr, itemptr, prop) \
  { \
    CollectionPropertyIterator rna_macro_iter; \
    for (RNA_property_collection_begin(sptr, prop, &rna_macro_iter); rna_macro_iter.valid; \
         RNA_property_collection_next(&rna_macro_iter)) { \
      PointerRNA itemptr = rna_macro_iter.ptr;

#define RNA_PROP_END \
  } \
  RNA_property_collection_end(&rna_macro_iter); \
  } \
  ((void)0)

#define RNA_STRUCT_BEGIN(sptr, prop) \
  { \
    CollectionPropertyIterator rna_macro_iter; \
    for (RNA_property_collection_begin( \
             sptr, RNA_struct_iterator_property((sptr)->type), &rna_macro_iter); \
         rna_macro_iter.valid; \
         RNA_property_collection_next(&rna_macro_iter)) { \
      PropertyRNA *prop = (PropertyRNA *)rna_macro_iter.ptr.data;

#define RNA_STRUCT_BEGIN_SKIP_RNA_TYPE(sptr, prop) \
  { \
    CollectionPropertyIterator rna_macro_iter; \
    RNA_property_collection_begin( \
        sptr, RNA_struct_iterator_property((sptr)->type), &rna_macro_iter); \
    if (rna_macro_iter.valid) { \
      RNA_property_collection_next(&rna_macro_iter); \
    } \
    for (; rna_macro_iter.valid; RNA_property_collection_next(&rna_macro_iter)) { \
      PropertyRNA *prop = (PropertyRNA *)rna_macro_iter.ptr.data;

#define RNA_STRUCT_END \
  } \
  RNA_property_collection_end(&rna_macro_iter); \
  } \
  ((void)0)

/**
 * Check if the #IDproperty exists, for operators.
 *
 * \param use_ghost: Internally an #IDProperty may exist,
 * without the RNA considering it to be "set", see #IDP_FLAG_GHOST.
 * This is used for operators, where executing an operator that has run previously
 * will re-use the last value (unless #PROP_SKIP_SAVE property is set).
 * In this case, the presence of the an existing value shouldn't prevent it being initialized
 * from the context. Even though the this value will be returned if it's requested,
 * it's not considered to be set (as it would if the menu item or key-map defined it's value).
 * Set `use_ghost` to true for default behavior, otherwise false to check if there is a value
 * exists internally and would be returned on request.
 */
bool RNA_property_is_set_ex(PointerRNA *ptr, PropertyRNA *prop, bool use_ghost);
bool RNA_property_is_set(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_unset(PointerRNA *ptr, PropertyRNA *prop);
/** See #RNA_property_is_set_ex documentation.  */
bool RNA_struct_property_is_set_ex(PointerRNA *ptr, const char *identifier, bool use_ghost);
bool RNA_struct_property_is_set(PointerRNA *ptr, const char *identifier);
bool RNA_property_is_idprop(const PropertyRNA *prop);
/**
 * \note Mainly for the UI.
 */
bool RNA_property_is_unlink(PropertyRNA *prop);
void RNA_struct_property_unset(PointerRNA *ptr, const char *identifier);

/**
 * Python compatible string representation of this property, (must be freed!).
 */
char *RNA_property_as_string(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int index, int max_prop_length);
/**
 * String representation of a property, Python compatible but can be used for display too.
 * \param C: can be NULL.
 */
char *RNA_pointer_as_string_id(struct bContext *C, PointerRNA *ptr);
char *RNA_pointer_as_string(struct bContext *C,
                            PointerRNA *ptr,
                            PropertyRNA *prop_ptr,
                            PointerRNA *ptr_prop);
/**
 * \param C: can be NULL.
 */
char *RNA_pointer_as_string_keywords_ex(struct bContext *C,
                                        PointerRNA *ptr,
                                        bool as_function,
                                        bool all_args,
                                        bool nested_args,
                                        int max_prop_length,
                                        PropertyRNA *iterprop);
char *RNA_pointer_as_string_keywords(struct bContext *C,
                                     PointerRNA *ptr,
                                     bool as_function,
                                     bool all_args,
                                     bool nested_args,
                                     int max_prop_length);
char *RNA_function_as_string_keywords(
    struct bContext *C, FunctionRNA *func, bool as_function, bool all_args, int max_prop_length);

/* Function */

const char *RNA_function_identifier(FunctionRNA *func);
const char *RNA_function_ui_description(FunctionRNA *func);
const char *RNA_function_ui_description_raw(FunctionRNA *func);
int RNA_function_flag(FunctionRNA *func);
int RNA_function_defined(FunctionRNA *func);

PropertyRNA *RNA_function_get_parameter(PointerRNA *ptr, FunctionRNA *func, int index);
PropertyRNA *RNA_function_find_parameter(PointerRNA *ptr,
                                         FunctionRNA *func,
                                         const char *identifier);
const struct ListBase *RNA_function_defined_parameters(FunctionRNA *func);

/* Utility */

int RNA_parameter_flag(PropertyRNA *prop);

ParameterList *RNA_parameter_list_create(ParameterList *parms, PointerRNA *ptr, FunctionRNA *func);
void RNA_parameter_list_free(ParameterList *parms);
int RNA_parameter_list_size(const ParameterList *parms);
int RNA_parameter_list_arg_count(const ParameterList *parms);
int RNA_parameter_list_ret_count(const ParameterList *parms);

void RNA_parameter_list_begin(ParameterList *parms, ParameterIterator *iter);
void RNA_parameter_list_next(ParameterIterator *iter);
void RNA_parameter_list_end(ParameterIterator *iter);

void RNA_parameter_get(ParameterList *parms, PropertyRNA *parm, void **value);
void RNA_parameter_get_lookup(ParameterList *parms, const char *identifier, void **value);
void RNA_parameter_set(ParameterList *parms, PropertyRNA *parm, const void *value);
void RNA_parameter_set_lookup(ParameterList *parms, const char *identifier, const void *value);

/* Only for PROP_DYNAMIC properties! */

int RNA_parameter_dynamic_length_get(ParameterList *parms, PropertyRNA *parm);
int RNA_parameter_dynamic_length_get_data(ParameterList *parms, PropertyRNA *parm, void *data);
void RNA_parameter_dynamic_length_set(ParameterList *parms, PropertyRNA *parm, int length);
void RNA_parameter_dynamic_length_set_data(ParameterList *parms,
                                           PropertyRNA *parm,
                                           void *data,
                                           int length);

int RNA_function_call(struct bContext *C,
                      struct ReportList *reports,
                      PointerRNA *ptr,
                      FunctionRNA *func,
                      ParameterList *parms);
int RNA_function_call_lookup(struct bContext *C,
                             struct ReportList *reports,
                             PointerRNA *ptr,
                             const char *identifier,
                             ParameterList *parms);

int RNA_function_call_direct(struct bContext *C,
                             struct ReportList *reports,
                             PointerRNA *ptr,
                             FunctionRNA *func,
                             const char *format,
                             ...) ATTR_PRINTF_FORMAT(5, 6);
int RNA_function_call_direct_lookup(struct bContext *C,
                                    struct ReportList *reports,
                                    PointerRNA *ptr,
                                    const char *identifier,
                                    const char *format,
                                    ...) ATTR_PRINTF_FORMAT(5, 6);
int RNA_function_call_direct_va(struct bContext *C,
                                struct ReportList *reports,
                                PointerRNA *ptr,
                                FunctionRNA *func,
                                const char *format,
                                va_list args);
int RNA_function_call_direct_va_lookup(struct bContext *C,
                                       struct ReportList *reports,
                                       PointerRNA *ptr,
                                       const char *identifier,
                                       const char *format,
                                       va_list args);

const char *RNA_translate_ui_text(const char *text,
                                  const char *text_ctxt,
                                  struct StructRNA *type,
                                  struct PropertyRNA *prop,
                                  int translate);

/* ID */

short RNA_type_to_ID_code(const StructRNA *type);
StructRNA *ID_code_to_RNA_type(short idcode);

#define RNA_POINTER_INVALIDATE(ptr) \
  { \
    /* this is checked for validity */ \
    (ptr)->type = NULL; /* should not be needed but prevent bad pointer access, just in case */ \
    (ptr)->owner_id = NULL; \
  } \
  (void)0

/* macro which inserts the function name */
#if defined __GNUC__
#  define RNA_warning(format, args...) _RNA_warning("%s: " format "\n", __func__, ##args)
#else
#  define RNA_warning(format, ...) _RNA_warning("%s: " format "\n", __FUNCTION__, __VA_ARGS__)
#endif

/** Use to implement the #RNA_warning macro which includes `__func__` suffix. */
void _RNA_warning(const char *format, ...) ATTR_PRINTF_FORMAT(1, 2);

/* Equals test. */

/**
 * \note In practice, #EQ_STRICT and #EQ_COMPARE have same behavior currently,
 * and will yield same result.
 */
typedef enum eRNACompareMode {
  /* Only care about equality, not full comparison. */
  /** Set/unset ignored. */
  RNA_EQ_STRICT,
  /** Unset property matches anything. */
  RNA_EQ_UNSET_MATCH_ANY,
  /** Unset property never matches set property. */
  RNA_EQ_UNSET_MATCH_NONE,
  /** Full comparison. */
  RNA_EQ_COMPARE,
} eRNACompareMode;

bool RNA_property_equals(struct Main *bmain,
                         struct PointerRNA *ptr_a,
                         struct PointerRNA *ptr_b,
                         struct PropertyRNA *prop,
                         eRNACompareMode mode);
bool RNA_struct_equals(struct Main *bmain,
                       struct PointerRNA *ptr_a,
                       struct PointerRNA *ptr_b,
                       eRNACompareMode mode);

/* Override. */

/** Flags for #RNA_struct_override_matches. */
typedef enum eRNAOverrideMatch {
  /** Do not compare properties that are not overridable. */
  RNA_OVERRIDE_COMPARE_IGNORE_NON_OVERRIDABLE = 1 << 0,
  /** Do not compare properties that are already overridden. */
  RNA_OVERRIDE_COMPARE_IGNORE_OVERRIDDEN = 1 << 1,

  /** Create new property override if needed and possible. */
  RNA_OVERRIDE_COMPARE_CREATE = 1 << 16,
  /** Restore property's value(s) to reference ones if needed and possible. */
  RNA_OVERRIDE_COMPARE_RESTORE = 1 << 17,
} eRNAOverrideMatch;

typedef enum eRNAOverrideMatchResult {
  /**
   * Some new property overrides were created to take into account
   * differences between local and reference.
   */
  RNA_OVERRIDE_MATCH_RESULT_CREATED = 1 << 0,
  /** Some properties were reset to reference values. */
  RNA_OVERRIDE_MATCH_RESULT_RESTORED = 1 << 1,
} eRNAOverrideMatchResult;

typedef enum eRNAOverrideStatus {
  /** The property is overridable. */
  RNA_OVERRIDE_STATUS_OVERRIDABLE = 1 << 0,
  /** The property is overridden. */
  RNA_OVERRIDE_STATUS_OVERRIDDEN = 1 << 1,
  /** Overriding this property is mandatory when creating an override. */
  RNA_OVERRIDE_STATUS_MANDATORY = 1 << 2,
  /** The override status of this property is locked. */
  RNA_OVERRIDE_STATUS_LOCKED = 1 << 3,
} eRNAOverrideStatus;

/**
 * Check whether reference and local overridden data match (are the same),
 * with respect to given restrictive sets of properties.
 * If requested, will generate needed new property overrides, and/or restore values from reference.
 *
 * \param r_report_flags: If given,
 * will be set with flags matching actions taken by the function on \a ptr_local.
 *
 * \return True if _resulting_ \a ptr_local does match \a ptr_reference.
 */
bool RNA_struct_override_matches(struct Main *bmain,
                                 struct PointerRNA *ptr_local,
                                 struct PointerRNA *ptr_reference,
                                 const char *root_path,
                                 size_t root_path_len,
                                 struct IDOverrideLibrary *override,
                                 eRNAOverrideMatch flags,
                                 eRNAOverrideMatchResult *r_report_flags);

/**
 * Store needed second operands into \a storage data-block
 * for differential override operations.
 */
bool RNA_struct_override_store(struct Main *bmain,
                               struct PointerRNA *ptr_local,
                               struct PointerRNA *ptr_reference,
                               PointerRNA *ptr_storage,
                               struct IDOverrideLibrary *override);

typedef enum eRNAOverrideApplyFlag {
  RNA_OVERRIDE_APPLY_FLAG_NOP = 0,
  /**
   * Hack to work around/fix older broken overrides: Do not apply override operations affecting ID
   * pointers properties, unless the destination original value (the one being overridden) is NULL.
   */
  RNA_OVERRIDE_APPLY_FLAG_IGNORE_ID_POINTERS = 1 << 0,
} eRNAOverrideApplyFlag;

/**
 * Apply given \a override operations on \a ptr_dst, using \a ptr_src
 * (and \a ptr_storage for differential ops) as source.
 */
void RNA_struct_override_apply(struct Main *bmain,
                               struct PointerRNA *ptr_dst,
                               struct PointerRNA *ptr_src,
                               struct PointerRNA *ptr_storage,
                               struct IDOverrideLibrary *override,
                               eRNAOverrideApplyFlag flag);

struct IDOverrideLibraryProperty *RNA_property_override_property_find(struct Main *bmain,
                                                                      PointerRNA *ptr,
                                                                      PropertyRNA *prop,
                                                                      struct ID **r_owner_id);
struct IDOverrideLibraryProperty *RNA_property_override_property_get(struct Main *bmain,
                                                                     PointerRNA *ptr,
                                                                     PropertyRNA *prop,
                                                                     bool *r_created);

struct IDOverrideLibraryPropertyOperation *RNA_property_override_property_operation_find(
    struct Main *bmain,
    PointerRNA *ptr,
    PropertyRNA *prop,
    int index,
    bool strict,
    bool *r_strict);
struct IDOverrideLibraryPropertyOperation *RNA_property_override_property_operation_get(
    struct Main *bmain,
    PointerRNA *ptr,
    PropertyRNA *prop,
    short operation,
    int index,
    bool strict,
    bool *r_strict,
    bool *r_created);

eRNAOverrideStatus RNA_property_override_library_status(struct Main *bmainm,
                                                        PointerRNA *ptr,
                                                        PropertyRNA *prop,
                                                        int index);

void RNA_struct_state_owner_set(const char *name);
const char *RNA_struct_state_owner_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __API_ACCESS_H__ */
