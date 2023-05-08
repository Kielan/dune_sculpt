/* Use a define instead of `#pragma once` because of `rna_internal.h` */
#ifndef __RNA_ACCESS_H__
#define __RNA_ACCESS_H__

/** \file
 * \ingroup RNA
 */

#include <stdarg.h>

#include "RNA_types.h"

#include "BLI_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ID;
struct IDOverrideLibrary;
struct IDOverrideLibraryProperty;
struct IDOverrideLibraryPropertyOperation;
struct IDProperty;
struct ListBase;
struct Main;
struct ReportList;
struct Scene;
struct bContext;

/* Types */
extern BlenderRNA BLENDER_RNA;

/* Pointer
 *
 * These functions will fill in RNA pointers, this can be done in three ways:
 * - a pointer Main is created by just passing the data pointer
 * - a pointer to a datablock can be created with the type and id data pointer
 * - a pointer to data contained in a datablock can be created with the id type
 *   and id data pointer, and the data type and pointer to the struct itself.
 *
 * There is also a way to get a pointer with the information about all structs.
 */

void RNA_main_pointer_create(struct Main *main, PointerRNA *r_ptr);
void RNA_id_pointer_create(struct ID *id, PointerRNA *r_ptr);
void RNA_pointer_create(struct ID *id, StructRNA *type, void *data, PointerRNA *r_ptr);
bool RNA_pointer_is_null(const PointerRNA *ptr);

bool RNA_path_resolved_create(PointerRNA *ptr,
                              struct PropertyRNA *prop,
                              int prop_index,
                              PathResolvedRNA *r_anim_rna);

void RNA_blender_rna_pointer_create(PointerRNA *r_ptr);
void RNA_pointer_recast(PointerRNA *ptr, PointerRNA *r_ptr);

extern const PointerRNA PointerRNA_NULL;

/* Structs */

StructRNA *RNA_struct_find(const char *identifier);

const char *RNA_struct_identifier(const StructRNA *type);
const char *RNA_struct_ui_name(const StructRNA *type);
const char *RNA_struct_ui_name_raw(const StructRNA *type);
const char *RNA_struct_ui_description(const StructRNA *type);
const char *RNA_struct_ui_description_raw(const StructRNA *type);
const char *RNA_struct_translation_context(const StructRNA *type);
int RNA_struct_ui_icon(const StructRNA *type);

PropertyRNA *RNA_struct_name_property(const StructRNA *type);
const EnumPropertyItem *RNA_struct_property_tag_defines(const StructRNA *type);
PropertyRNA *RNA_struct_iterator_property(StructRNA *type);
StructRNA *RNA_struct_base(StructRNA *type);
/**
 * Use to find the sub-type directly below a base-type.
 *
 * So if type were `RNA_SpotLight`, `RNA_struct_base_of(type, &RNA_ID)` would return `&RNA_Light`.
 */
const StructRNA *RNA_struct_base_child_of(const StructRNA *type, const StructRNA *parent_type);

bool RNA_struct_is_ID(const StructRNA *type);
bool RNA_struct_is_a(const StructRNA *type, const StructRNA *srna);

bool RNA_struct_undo_check(const StructRNA *type);

StructRegisterFunc RNA_struct_register(StructRNA *type);
StructUnregisterFunc RNA_struct_unregister(StructRNA *type);
void **RNA_struct_instance(PointerRNA *ptr);

void *RNA_struct_py_type_get(StructRNA *srna);
void RNA_struct_py_type_set(StructRNA *srna, void *py_type);

void *RNA_struct_blender_type_get(StructRNA *srna);
void RNA_struct_blender_type_set(StructRNA *srna, void *blender_type);

struct IDProperty **RNA_struct_idprops_p(PointerRNA *ptr);
struct IDProperty *RNA_struct_idprops(PointerRNA *ptr, bool create);
bool RNA_struct_idprops_check(StructRNA *srna);
bool RNA_struct_idprops_register_check(const StructRNA *type);
bool RNA_struct_idprops_datablock_allowed(const StructRNA *type);
/**
 * Whether given type implies datablock usage by IDProperties.
 * This is used to prevent classes allowed to have IDProperties,
 * but not datablock ones, to indirectly use some
 * (e.g. by assigning an IDP_GROUP containing some IDP_ID pointers...).
 */
bool RNA_struct_idprops_contains_datablock(const StructRNA *type);
/**
 * Remove an id-property.
 */
bool RNA_struct_idprops_unset(PointerRNA *ptr, const char *identifier);

PropertyRNA *RNA_struct_find_property(PointerRNA *ptr, const char *identifier);
bool RNA_struct_contains_property(PointerRNA *ptr, PropertyRNA *prop_test);
unsigned int RNA_struct_count_properties(StructRNA *srna);

/**
 * Low level direct access to type->properties,
 * note this ignores parent classes so should be used with care.
 */
const struct ListBase *RNA_struct_type_properties(StructRNA *srna);
PropertyRNA *RNA_struct_type_find_property_no_base(StructRNA *srna, const char *identifier);
/**
 * \note #RNA_struct_find_property is a higher level alternative to this function
 * which takes a #PointerRNA instead of a #StructRNA.
 */
PropertyRNA *RNA_struct_type_find_property(StructRNA *srna, const char *identifier);

FunctionRNA *RNA_struct_find_function(StructRNA *srna, const char *identifier);
const struct ListBase *RNA_struct_type_functions(StructRNA *srna);

char *RNA_struct_name_get_alloc(PointerRNA *ptr, char *fixedbuf, int fixedlen, int *r_len);

/**
 * Use when registering structs with the #STRUCT_PUBLIC_NAMESPACE flag.
 */
bool RNA_struct_available_or_report(struct ReportList *reports, const char *identifier);
bool RNA_struct_bl_idname_ok_or_report(struct ReportList *reports,
                                       const char *identifier,
                                       const char *sep);

/* Properties
 *
 * Access to struct properties. All this works with RNA pointers rather than
 * direct pointers to the data. */

/* Property Information */

const char *RNA_property_identifier(const PropertyRNA *prop);
const char *RNA_property_description(PropertyRNA *prop);

PropertyType RNA_property_type(PropertyRNA *prop);
PropertySubType RNA_property_subtype(PropertyRNA *prop);
PropertyUnit RNA_property_unit(PropertyRNA *prop);
PropertyScaleType RNA_property_ui_scale(PropertyRNA *prop);
int RNA_property_flag(PropertyRNA *prop);
int RNA_property_override_flag(PropertyRNA *prop);
/**
 * Get the tags set for \a prop as int bitfield.
 * \note Doesn't perform any validity check on the set bits. #RNA_def_property_tags does this
 *       in debug builds (to avoid performance issues in non-debug builds), which should be
 *       the only way to set tags. Hence, at this point we assume the tag bitfield to be valid.
 */
int RNA_property_tags(PropertyRNA *prop);
bool RNA_property_builtin(PropertyRNA *prop);
void *RNA_property_py_data_get(PropertyRNA *prop);

int RNA_property_array_length(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_array_check(PropertyRNA *prop);
/**
 * Return the size of Nth dimension.
 */
int RNA_property_multi_array_length(PointerRNA *ptr, PropertyRNA *prop, int dimension);
/**
 * Used by BPY to make an array from the python object.
 */
int RNA_property_array_dimension(PointerRNA *ptr, PropertyRNA *prop, int length[]);
char RNA_property_array_item_char(PropertyRNA *prop, int index);
int RNA_property_array_item_index(PropertyRNA *prop, char name);

/**
 * \return the maximum length including the \0 terminator. '0' is used when there is no maximum.
 */
int RNA_property_string_maxlength(PropertyRNA *prop);

const char *RNA_property_ui_name(const PropertyRNA *prop);
const char *RNA_property_ui_name_raw(const PropertyRNA *prop);
const char *RNA_property_ui_description(const PropertyRNA *prop);
const char *RNA_property_ui_description_raw(const PropertyRNA *prop);
const char *RNA_property_translation_context(const PropertyRNA *prop);
int RNA_property_ui_icon(const PropertyRNA *prop);

/* Dynamic Property Information */

void RNA_property_int_range(PointerRNA *ptr, PropertyRNA *prop, int *hardmin, int *hardmax);
void RNA_property_int_ui_range(
    PointerRNA *ptr, PropertyRNA *prop, int *softmin, int *softmax, int *step);

void RNA_property_float_range(PointerRNA *ptr, PropertyRNA *prop, float *hardmin, float *hardmax);
void RNA_property_float_ui_range(PointerRNA *ptr,
                                 PropertyRNA *prop,
                                 float *softmin,
                                 float *softmax,
                                 float *step,
                                 float *precision);

int RNA_property_float_clamp(PointerRNA *ptr, PropertyRNA *prop, float *value);
int RNA_property_int_clamp(PointerRNA *ptr, PropertyRNA *prop, int *value);

bool RNA_enum_identifier(const EnumPropertyItem *item, int value, const char **identifier);
int RNA_enum_bitflag_identifiers(const EnumPropertyItem *item, int value, const char **identifier);
bool RNA_enum_name(const EnumPropertyItem *item, int value, const char **r_name);
bool RNA_enum_description(const EnumPropertyItem *item, int value, const char **description);
int RNA_enum_from_value(const EnumPropertyItem *item, int value);
int RNA_enum_from_identifier(const EnumPropertyItem *item, const char *identifier);
/**
 * Take care using this with translated enums,
 * prefer #RNA_enum_from_identifier where possible.
 */
int RNA_enum_from_name(const EnumPropertyItem *item, const char *name);
unsigned int RNA_enum_items_count(const EnumPropertyItem *item);

void RNA_property_enum_items_ex(struct bContext *C,
                                PointerRNA *ptr,
                                PropertyRNA *prop,
                                bool use_static,
                                const EnumPropertyItem **r_item,
                                int *r_totitem,
                                bool *r_free);
void RNA_property_enum_items(struct bContext *C,
                             PointerRNA *ptr,
                             PropertyRNA *prop,
                             const EnumPropertyItem **r_item,
                             int *r_totitem,
                             bool *r_free);
void RNA_property_enum_items_gettexted(struct bContext *C,
                                       PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       const EnumPropertyItem **r_item,
                                       int *r_totitem,
                                       bool *r_free);
void RNA_property_enum_items_gettexted_all(struct bContext *C,
                                           PointerRNA *ptr,
                                           PropertyRNA *prop,
                                           const EnumPropertyItem **r_item,
                                           int *r_totitem,
                                           bool *r_free);
bool RNA_property_enum_value(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, const char *identifier, int *r_value);
bool RNA_property_enum_identifier(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, const char **identifier);
bool RNA_property_enum_name(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, const char **name);
bool RNA_property_enum_name_gettexted(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, const char **name);

bool RNA_property_enum_item_from_value(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, EnumPropertyItem *r_item);
bool RNA_property_enum_item_from_value_gettexted(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, EnumPropertyItem *r_item);

int RNA_property_enum_bitflag_identifiers(
    struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int value, const char **identifier);

StructRNA *RNA_property_pointer_type(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_pointer_poll(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *value);

bool RNA_property_editable(PointerRNA *ptr, PropertyRNA *prop);
/**
 * Version of #RNA_property_editable that tries to return additional info in \a r_info
 * that can be exposed in UI.
 */
bool RNA_property_editable_info(PointerRNA *ptr, PropertyRNA *prop, const char **r_info);
/**
 * Same as RNA_property_editable(), except this checks individual items in an array.
 */
bool RNA_property_editable_index(PointerRNA *ptr, PropertyRNA *prop, const int index);

/**
 * Without lib check, only checks the flag.
 */
bool RNA_property_editable_flag(PointerRNA *ptr, PropertyRNA *prop);

bool RNA_property_animateable(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_animated(PointerRNA *ptr, PropertyRNA *prop);
/**
 * \note Does not take into account editable status, this has to be checked separately
 * (using #RNA_property_editable_flag() usually).
 */
bool RNA_property_overridable_get(PointerRNA *ptr, PropertyRNA *prop);
/**
 * Should only be used for custom properties.
 */
bool RNA_property_overridable_library_set(PointerRNA *ptr, PropertyRNA *prop, bool is_overridable);
bool RNA_property_overridden(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_comparable(PointerRNA *ptr, PropertyRNA *prop);
/**
 * This function is to check if its possible to create a valid path from the ID
 * its slow so don't call in a loop.
 */
bool RNA_property_path_from_ID_check(PointerRNA *ptr, PropertyRNA *prop); /* slow, use with care */

void RNA_property_update(struct bContext *C, PointerRNA *ptr, PropertyRNA *prop);
/**
 * \param scene: may be NULL.
 */
void RNA_property_update_main(struct Main *bmain,
                              struct Scene *scene,
                              PointerRNA *ptr,
                              PropertyRNA *prop);
/**
 * \note its possible this returns a false positive in the case of #PROP_CONTEXT_UPDATE
 * but this isn't likely to be a performance problem.
 */
bool RNA_property_update_check(struct PropertyRNA *prop);

/* Property Data */

bool RNA_property_boolean_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_boolean_set(PointerRNA *ptr, PropertyRNA *prop, bool value);
void RNA_property_boolean_get_array(PointerRNA *ptr, PropertyRNA *prop, bool *values);
bool RNA_property_boolean_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_boolean_set_array(PointerRNA *ptr, PropertyRNA *prop, const bool *values);
void RNA_property_boolean_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, bool value);
bool RNA_property_boolean_get_default(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_boolean_get_default_array(PointerRNA *ptr, PropertyRNA *prop, bool *values);
bool RNA_property_boolean_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

int RNA_property_int_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_int_set(PointerRNA *ptr, PropertyRNA *prop, int value);
void RNA_property_int_get_array(PointerRNA *ptr, PropertyRNA *prop, int *values);
void RNA_property_int_get_array_range(PointerRNA *ptr, PropertyRNA *prop, int values[2]);
int RNA_property_int_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_int_set_array(PointerRNA *ptr, PropertyRNA *prop, const int *values);
void RNA_property_int_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, int value);
int RNA_property_int_get_default(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_int_set_default(PropertyRNA *prop, int value);
void RNA_property_int_get_default_array(PointerRNA *ptr, PropertyRNA *prop, int *values);
int RNA_property_int_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

float RNA_property_float_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_float_set(PointerRNA *ptr, PropertyRNA *prop, float value);
void RNA_property_float_get_array(PointerRNA *ptr, PropertyRNA *prop, float *values);
void RNA_property_float_get_array_range(PointerRNA *ptr, PropertyRNA *prop, float values[2]);
float RNA_property_float_get_index(PointerRNA *ptr, PropertyRNA *prop, int index);
void RNA_property_float_set_array(PointerRNA *ptr, PropertyRNA *prop, const float *values);
void RNA_property_float_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, float value);
float RNA_property_float_get_default(PointerRNA *ptr, PropertyRNA *prop);
bool RNA_property_float_set_default(PropertyRNA *prop, float value);
void RNA_property_float_get_default_array(PointerRNA *ptr, PropertyRNA *prop, float *values);
float RNA_property_float_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index);

void RNA_property_string_get(PointerRNA *ptr, PropertyRNA *prop, char *value);
char *RNA_property_string_get_alloc(
    PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen, int *r_len);
void RNA_property_string_set(PointerRNA *ptr, PropertyRNA *prop, const char *value);
void RNA_property_string_set_bytes(PointerRNA *ptr, PropertyRNA *prop, const char *value, int len);
/**
 * \return the length without `\0` terminator.
 */
int RNA_property_string_length(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_string_get_default(PropertyRNA *prop, char *value, int max_len);
char *RNA_property_string_get_default_alloc(
    PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen, int *r_len);
/**
 * \return the length without `\0` terminator.
 */
int RNA_property_string_default_length(PointerRNA *ptr, PropertyRNA *prop);

int RNA_property_enum_get(PointerRNA *ptr, PropertyRNA *prop);
void RNA_property_enum_set(PointerRNA *ptr, PropertyRNA *prop, int value);
int RNA_property_enum_get_default(PointerRNA *ptr, PropertyRNA *prop);
/**
 * Get the value of the item that is \a step items away from \a from_value.
 *
 * \param from_value: Item value to start stepping from.
 * \param step: Absolute value defines step size, sign defines direction.
 * E.g to get the next item, pass 1, for the previous -1.
 */
int RNA_property_enum_step(
    const struct bContext *C, PointerRNA *ptr, PropertyRNA *prop, int from_value, int step);

PointerRNA RNA_property_pointer_get(PointerRNA *ptr, PropertyRNA *prop) ATTR_NONNULL(1, 2);
void RNA_property_pointer_set(PointerRNA *ptr,
                              PropertyRNA *prop,
                              PointerRNA ptr_value,
                              struct ReportList *reports) ATTR_NONNULL(1, 2);
PointerRNA RNA_property_pointer_get_default(PointerRNA *ptr, PropertyRNA *prop) ATTR_NONNULL(1, 2);

void RNA_property_collection_begin(PointerRNA *ptr,
                                   PropertyRNA *prop,
                                   CollectionPropertyIterator *iter);
void RNA_property_collection_next(CollectionPropertyIterator *iter);
void RNA_property_collection_skip(CollectionPropertyIterator *iter, int num);
void RNA_property_collection_end(CollectionPropertyIterator *iter);
int RNA_property_collection_length(PointerRNA *ptr, PropertyRNA *prop);
/**
 * Return true when `RNA_property_collection_length(ptr, prop) == 0`,
 * without having to iterate over items in the collection (needed for some kinds of collections).
 */
bool RNA_property_collection_is_empty(PointerRNA *ptr, PropertyRNA *prop);
int RNA_property_collection_lookup_index(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *t_ptr);
int RNA_property_collection_lookup_int(PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       int key,
                                       PointerRNA *r_ptr);
int RNA_property_collection_lookup_string(PointerRNA *ptr,
                                          PropertyRNA *prop,
                                          const char *key,
                                          PointerRNA *r_ptr);
int RNA_property_collection_lookup_string_index(
    PointerRNA *ptr, PropertyRNA *prop, const char *key, PointerRNA *r_ptr, int *r_index);
/**
 * Zero return is an assignment error.
 */
int RNA_property_collection_assign_int(PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       int key,
                                       const PointerRNA *assign_ptr);
bool RNA_property_collection_type_get(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr);

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
