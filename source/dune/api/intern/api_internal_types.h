#pragma once

#include "list.h"

#include "api_types.h"

struct DuneApi;
struct CollectionPropIter;
struct ApiContainer;
struct ApiFn;
struct GHash;
struct IdOverrideLib;
struct IdOverrideLibPropOp;
struct IdProp;
struct Main;
struct ApiPtr;
struct ApiProp;
struct ReportList;
struct Scene;
struct ApiStruct;
struct Cxt;

typedef struct IdProp IdProp;

/* Function Callbacks */
/** Update callback for an apo prop.
 *
 *  This is NOT called automatically when writing into the prop, it needs to be called
 * manually (through api_prop_update or api_prop_update_main) when needed.
 *
 *  param main: the Main data-base to which `ptr` data belongs.
 *  param active_scene: The current active scene (may be NULL in some cases).
 *  param ptr: The api ptr data to update. */
typedef void (*UpdateFn)(struct Main *main, struct Scene *active_scene, struct ApiPtr *ptr);
typedef void (*CxtPropUpdateFn)(struct Cxt *C,
                                struct ApiPtr *ptr,
                                struct ApiProp *prop);
typedef void (*CxtUpdateFn)(struct Cxt *C, struct ApiPtr *ptr);

typedef int (*EditableFn)(struct ApiPtr *ptr, const char **r_info);
typedef int (*ItemEditableFn)(struct ApiPtr *ptr, int index);
typedef struct IdProp **(*IdPropsFn)(struct ApiPtr *ptr);
typedef struct ApiStruct *(*StructRefineFn)(struct ApiPtr *ptr);
typedef char *(*StructPathFn)(struct ApiPtr *ptr);

typedef int (*PropArrayLengthGetFn)(struct ApiPtr *ptr, int length[API_MAX_ARRAY_DIMENSION]);
typedef bool (*PropBoolGetFn)(struct ApiPtr *ptr);
typedef void (*PropBoolSetFn)(struct ApiPtr *ptr, bool value);
typedef void (*PropBoolArrayGetFn)(struct ApiPtr *ptr, bool *values);
typedef void (*PropBoolArraySetn)(struct ApiPtr *ptr, const bool *values);
typedef int (*PropIntGetFn)(struct ApiPte *ptr);
typedef void (*PropIntSetFn)(struct ApiPtr *ptr, int value);
typedef void (*PropIntArrayGetFn)(struct ApiPtr *ptr, int *values);
typedef void (*PropIntArraySetFn)(struct ApiPtr *ptr, const int *values);
typedef void (*PropIntRangeFn)(
    struct ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax);
typedef float (*PropFloatGetFn)(struct ApiPtr *ptr);
typedef void (*PropFloatSetFn)(struct ApiPtr *ptr, float value);
typedef void (*PropFloatArrayGetFn)(struct ApiPtr *ptr, float *values);
typedef void (*PropFloatArraySetFn)(struct ApiPtr *ptr, const float *values);
typedef void (*PropFloatRangeFn)(
    struct ApiPtr *ptr, float *min, float *max, float *softmin, float *softmax);
typedef void (*PropStringGetFn)(struct ApiPtr *ptr, char *value);
typedef int (*PropStringLengthFn)(struct ApiPtr *ptr);
typedef void (*PropStringSetFn)(struct ApiPtr *ptr, const char *value);
typedef int (*PropEnumGetFn)(struct ApiPtr *ptr);
typedef void (*PropEnumSetFn)(struct ApiPtr *ptr, int value);
typedef const EnumPropItem *(*PropEnumItemFn)(struct Cxt *C,
                                              struct ApiPtr *ptr,
                                              struct ApiProp *prop,
                                              bool *r_free);
typedef ApiPtr (*PropPtrGetFn)(struct ApiPtr *ptr);
typedef ApiStruct *(*PropPtrTypeFn)(struct ApiPtr *ptr);
typedef void (*PropPtrSetFn)(struct ApiPtr *ptr,
                             const ApiPtr value,
                             struct ReportList *reports);
typedef bool (*PropPtrPollFn)(struct ApiPtr *ptr, const ApiPtr value);
typedef bool (*PropPtrPollFnPy)(struct ApiPtr *ptr,
                                const ApiPtr value,
                                const ApiProp *prop);
typedef void (*PropCollectionBeginFn)(struct CollectionPropIter *iter,
                                      struct ApiPtr *ptr);
typedef void (*PropCollectionNextFn)(struct CollectionPropIter *iter);
typedef void (*PropCollectionEndFn)(struct CollectionPropIter *iter);
typedef ApiPtr (*PropCollectionGetFn)(struct CollectionPropIter *iter);
typedef int (*PropCollectionLengthFn)(struct ApiPtr *ptr);
typedef int (*PropCollectionLookupIntFn)(struct ApiPtr *ptr,
                                         int key,
                                         struct ApiPtr *r_ptr);
typedef int (*PropCollectionLookupStringFn)(struct ApiPtr *ptr,
                                            const char *key,
                                            struct ApiPtr *r_ptr);
typedef int (*PropCollectionAssignIntFn)(struct ApiPtr *ptr,
                                         int key,
                                         const struct ApiPtr *assign_ptr);

/* extended versions with PropertyRNA argument */
typedef bool (*PropBoolGetFnEx)(struct ApiPtr *ptr, struct ApiProp *prop);
typedef void (*PropBoolSetFnEx)(struct ApiPtr *ptr, struct ApiProp *prop, bool value);
typedef void (*PropBoolArrayGetFnEx)(struct ApiPtr *ptr,
                                          struct PropertyRNA *prop,
                                          bool *values);
typedef void (*PropBooleanArraySetFuncEx)(struct PointerRNA *ptr,
                                          struct PropertyRNA *prop,
                                          const bool *values);
typedef int (*PropIntGetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop);
typedef void (*PropIntSetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop, int value);
typedef void (*PropIntArrayGetFuncEx)(struct PointerRNA *ptr,
                                      struct PropertyRNA *prop,
                                      int *values);
typedef void (*PropIntArraySetFuncEx)(struct PointerRNA *ptr,
                                      struct PropertyRNA *prop,
                                      const int *values);
typedef void (*PropIntRangeFuncEx)(struct PointerRNA *ptr,
                                   struct PropertyRNA *prop,
                                   int *min,
                                   int *max,
                                   int *softmin,
                                   int *softmax);
typedef float (*PropFloatGetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop);
typedef void (*PropFloatSetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop, float value);
typedef void (*PropFloatArrayGetFuncEx)(struct PointerRNA *ptr,
                                        struct PropertyRNA *prop,
                                        float *values);
typedef void (*PropFloatArraySetFuncEx)(struct PointerRNA *ptr,
                                        struct PropertyRNA *prop,
                                        const float *values);
typedef void (*PropFloatRangeFuncEx)(struct PointerRNA *ptr,
                                     struct PropertyRNA *prop,
                                     float *min,
                                     float *max,
                                     float *softmin,
                                     float *softmax);
typedef void (*PropStringGetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop, char *value);
typedef int (*PropStringLengthFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop);
typedef void (*PropStringSetFuncEx)(struct PointerRNA *ptr,
                                    struct PropertyRNA *prop,
                                    const char *value);
typedef int (*PropEnumGetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop);
typedef void (*PropEnumSetFuncEx)(struct PointerRNA *ptr, struct PropertyRNA *prop, int value);

/* Handling override operations, and also comparison. */

/** Structure storing all needed data to process all three kinds of RNA properties. */
typedef struct PropertyRNAOrID {
  PointerRNA ptr;

  /** The PropertyRNA passed as parameter, used to generate that structure's content:
   * - Static RNA: The RNA property (same as `rnaprop`), never NULL.
   * - Runtime RNA: The RNA property (same as `rnaprop`), never NULL.
   * - IDProperty: The IDProperty, never NULL.
   */
  PropertyRNA *rawprop;
  /** The real RNA property of this property, never NULL:
   * - Static RNA: The rna property, also gives direct access to the data (from any matching
   *               PointerRNA).
   * - Runtime RNA: The rna property, does not directly gives access to the data.
   * - IDProperty: The generic PropertyRNA matching its type.
   */
  PropertyRNA *rnaprop;
  /** The IDProperty storing the data of this property, may be NULL:
   * - Static RNA: Always NULL.
   * - Runtime RNA: The IDProperty storing the data of that property, may be NULL if never set yet.
   * - IDProperty: The IDProperty, never NULL.
   */
  IDProperty *idprop;
  /** The name of the property. */
  const char *identifier;

  /** Whether this property is a 'pure' IDProperty or not. */
  bool is_idprop;
  /** For runtime RNA properties, whether it is set, defined, or not.
   * WARNING: This DOES take into account the `IDP_FLAG_GHOST` flag, i.e. it matches result of
   *          `RNA_property_is_set`. */
  bool is_set;

  bool is_array;
  uint array_len;
} PropertyRNAOrID;

/**
 * If \a override is NULL, merely do comparison between prop_a and prop_b,
 * following comparison mode given.
 * If \a override and \a rna_path are not NULL, it will add a new override operation for
 * overridable properties that differ and have not yet been overridden
 * (and set accordingly \a r_override_changed if given).
 *
 * override, api_path and r_override_changed may be NULL pointers.
 */
typedef int (*ApiPropOverrideDiff)(struct Main *main,
                                   struct PropApiOrId *prop_a,
                                   struct PropApiOrId *prop_b,
                                   int mode,
                                   struct IdOverrideLib *override,
                                   const char *api_path,
                                   size_t api_path_len,
                                   int flags,
                                   bool *r_override_changed);

/**
 * Only used for differential override (add, sub, etc.).
 * Store into storage the value needed to transform reference's value into local's value.
 *
 * Given Prop are final (in case of IdProps...).
 * In non-array cases, len values are 0.
 * Might change given override operation (e.g. change 'add' one into 'sub'),
 * in case computed storage value is out of range
 * (or even change it to basic 'set' operation if nothing else works).
 */
typedef bool (*RNAPropOverrideStore)(struct Main *bmain,
                                     struct ApiPtr *ptr_local,
                                     struct ApiPtr *ptr_ref,
                                     struct ApiPtr *ptr_storage,
                                     struct ApiProp *prop_local,
                                     struct ApiProp *prop_ref,
                                     struct ApiProp *prop_storage,
                                     int len_local,
                                     int len_ref,
                                     int len_storage,
                                     struct IdOverrideLibProp op);

/**
 * Apply given override operation from src to dst (using value from storage as second operand
 * for differential operations).
 *
 * Given ApiProp are final (in case of IdProps...).
 * In non-array cases, len values are 0.
 */
typedef bool (*RNAPropOverrideApply)(struct Main *main,
                                     struct ApiPtr *ptr_dst,
                                     struct ApiPtr *ptr_src,
                                     struct ApiPtr *ptr_storage,
                                     struct ApiProp *prop_dst,
                                     struct ApiProp *prop_src,
                                     struct ApiProp *prop_storage,
                                     int len_dst,
                                     int len_src,
                                     int len_storage,
                                     struct PointerRNA *ptr_item_dst,
                                     struct PointerRNA *ptr_item_src,
                                     struct PointerRNA *ptr_item_storage,
                                     struct IDOverrideLibraryPropertyOperation *opop);

/* Container - generic abstracted container of RNA properties */
typedef struct ContainerRNA {
  void *next, *prev;

  struct GHash *prophash;
  ListBase properties;
} ContainerRNA;

struct FunctionRNA {
  /* structs are containers of properties */
  ContainerRNA cont;

  /* unique identifier, keep after 'cont' */
  const char *identifier;
  /* various options */
  int flag;

  /* single line description, displayed in the tooltip for example */
  const char *description;

  /* callback to execute the function */
  CallFunc call;

  /* parameter for the return value
   * NOTE: this is only the C return value, rna functions can have multiple return values. */
  PropertyRNA *c_ret;
};

struct PropertyRNA {
  struct PropertyRNA *next, *prev;

  /* magic bytes to distinguish with IDProperty */
  int magic;

  /* unique identifier */
  const char *identifier;
  /* various options */
  int flag;
  /* various override options */
  int flag_override;
  /* Function parameters flags. */
  short flag_parameter;
  /* Internal ("private") flags. */
  short flag_internal;
  /* The subset of StructRNA.prop_tag_defines values that applies to this property. */
  short tags;

  /* user readable name */
  const char *name;
  /* single line description, displayed in the tooltip for example */
  const char *description;
  /* icon ID */
  int icon;
  /* context for translation */
  const char *translation_context;

  /* property type as it appears to the outside */
  PropertyType type;
  /* subtype, 'interpretation' of the property */
  PropertySubType subtype;
  /* if non-NULL, overrides arraylength. Must not return 0? */
  PropArrayLengthGetFunc getlength;
  /* dimension of array */
  unsigned int arraydimension;
  /* Array lengths for all dimensions (when `arraydimension > 0`). */
  unsigned int arraylength[RNA_MAX_ARRAY_DIMENSION];
  unsigned int totarraylength;

  /* callback for updates on change */
  UpdateFunc update;
  int noteflag;

  /* Callback for testing if editable. Its r_info parameter can be used to
   * return info on editable state that might be shown to user. E.g. tooltips
   * of disabled buttons can show reason why button is disabled using this. */
  EditableFunc editable;
  /* callback for testing if array-item editable (if applicable) */
  ItemEditableFunc itemeditable;

  /* Override handling callbacks (diff is also used for comparison). */
  RNAPropOverrideDiff override_diff;
  RNAPropOverrideStore override_store;
  RNAPropOverrideApply override_apply;

  /* raw access */
  int rawoffset;
  RawPropertyType rawtype;

  /* This is used for accessing props/functions of this property
   * any property can have this but should only be used for collections and arrays
   * since python will convert int/bool/pointer's */
  struct StructRNA *srna; /* attributes attached directly to this collection */

  /* python handle to hold all callbacks
   * (in a pointer array at the moment, may later be a tuple) */
  void *py_data;
};

/* internal flags WARNING! 16bits only! */
typedef enum PropertyFlagIntern {
  PROP_INTERN_BUILTIN = (1 << 0),
  PROP_INTERN_RUNTIME = (1 << 1),
  PROP_INTERN_RAW_ACCESS = (1 << 2),
  PROP_INTERN_RAW_ARRAY = (1 << 3),
  PROP_INTERN_FREE_POINTERS = (1 << 4),
  /* Negative mirror of PROP_PTR_NO_OWNERSHIP, used to prevent automatically setting that one in
   * makesrna when pointer is an ID... */
  PROP_INTERN_PTR_OWNERSHIP_FORCED = (1 << 5),
} PropertyFlagIntern;

/* Property Types */

typedef struct BoolPropertyRNA {
  PropertyRNA property;

  PropBooleanGetFunc get;
  PropBooleanSetFunc set;
  PropBooleanArrayGetFunc getarray;
  PropBooleanArraySetFunc setarray;

  PropBooleanGetFuncEx get_ex;
  PropBooleanSetFuncEx set_ex;
  PropBooleanArrayGetFuncEx getarray_ex;
  PropBooleanArraySetFuncEx setarray_ex;

  bool defaultvalue;
  const bool *defaultarray;
} BoolPropertyRNA;

typedef struct IntPropertyRNA {
  PropertyRNA property;

  PropIntGetFunc get;
  PropIntSetFunc set;
  PropIntArrayGetFunc getarray;
  PropIntArraySetFunc setarray;
  PropIntRangeFunc range;

  PropIntGetFuncEx get_ex;
  PropIntSetFuncEx set_ex;
  PropIntArrayGetFuncEx getarray_ex;
  PropIntArraySetFuncEx setarray_ex;
  PropIntRangeFuncEx range_ex;

  PropertyScaleType ui_scale_type;
  int softmin, softmax;
  int hardmin, hardmax;
  int step;

  int defaultvalue;
  const int *defaultarray;
} IntPropertyRNA;

typedef struct FloatPropertyRNA {
  PropertyRNA property;

  PropFloatGetFunc get;
  PropFloatSetFunc set;
  PropFloatArrayGetFunc getarray;
  PropFloatArraySetFunc setarray;
  PropFloatRangeFunc range;

  PropFloatGetFuncEx get_ex;
  PropFloatSetFuncEx set_ex;
  PropFloatArrayGetFuncEx getarray_ex;
  PropFloatArraySetFuncEx setarray_ex;
  PropFloatRangeFuncEx range_ex;

  PropertyScaleType ui_scale_type;
  float softmin, softmax;
  float hardmin, hardmax;
  float step;
  int precision;

  float defaultvalue;
  const float *defaultarray;
} FloatPropertyRNA;

typedef struct StringPropertyRNA {
  PropertyRNA property;

  PropStringGetFunc get;
  PropStringLengthFunc length;
  PropStringSetFunc set;

  PropStringGetFuncEx get_ex;
  PropStringLengthFuncEx length_ex;
  PropStringSetFuncEx set_ex;

  int maxlength; /* includes string terminator! */

  const char *defaultvalue;
} StringPropertyRNA;

typedef struct EnumPropertyRNA {
  PropertyRNA property;

  PropEnumGetFunc get;
  PropEnumSetFunc set;
  PropEnumItemFunc item_fn;

  PropEnumGetFuncEx get_ex;
  PropEnumSetFuncEx set_ex;

  const EnumPropertyItem *item;
  int totitem;

  int defaultvalue;
  const char *native_enum_type;
} EnumPropertyRNA;

typedef struct PointerPropertyRNA {
  PropertyRNA property;

  PropPointerGetFunc get;
  PropPointerSetFunc set;
  PropPointerTypeFunc type_fn;
  /** unlike operators, 'set' can still run if poll fails, used for filtering display. */
  PropPointerPollFunc poll;

  struct StructRNA *type;
} PointerPropertyRNA;

typedef struct CollectionPropertyRNA {
  PropertyRNA property;

  PropCollectionBeginFunc begin;
  PropCollectionNextFunc next;
  PropCollectionEndFunc end; /* optional */
  PropCollectionGetFunc get;
  PropCollectionLengthFunc length;             /* optional */
  PropCollectionLookupIntFunc lookupint;       /* optional */
  PropCollectionLookupStringFunc lookupstring; /* optional */
  PropCollectionAssignIntFunc assignint;       /* optional */

  struct StructRNA *item_type; /* the type of this item */
} CollectionPropertyRNA;

/* changes to this struct require updating rna_generate_struct in makesrna.c */
struct StructRNA {
  /* structs are containers of properties */
  ContainerRNA cont;

  /* unique identifier, keep after 'cont' */
  const char *identifier;

  /** Python type, this is a subtype of #pyrna_struct_Type
   * but used so each struct can have its own type which is useful for subclassing RNA. */
  void *py_type;
  void *blender_type;

  /* various options */
  int flag;
  /* Each StructRNA type can define own tags which properties can set
   * (PropertyRNA.tags) for changed behavior based on struct-type. */
  const EnumPropertyItem *prop_tag_defines;

  /* user readable name */
  const char *name;
  /* single line description, displayed in the tooltip for example */
  const char *description;
  /* context for translation */
  const char *translation_context;
  /* icon ID */
  int icon;

  /* property that defines the name */
  PropertyRNA *nameproperty;

  /* property to iterate over properties */
  PropertyRNA *iteratorproperty;

  /* struct this is derivedfrom */
  struct StructRNA *base;

  /* only use for nested structs, where both the parent and child access
   * the same C Struct but nesting is used for grouping properties.
   * The parent property is used so we know NULL checks are not needed,
   * and that this struct will never exist without its parent */
  struct StructRNA *nested;

  /* function to give the more specific type */
  StructRefineFunc refine;

  /* function to find path to this struct in an ID */
  StructPathFunc path;

  /* function to register/unregister subclasses */
  StructRegisterFunc reg;
  StructUnregisterFunc unreg;
  /**
   * Optionally support reusing Python instances for this type.
   *
   * Without this, an operator class created for #wmOperatorType.invoke (for example)
   * would have a different instance passed to the #wmOperatorType.modal callback.
   * So any variables assigned to `self` from Python would not be available to other callbacks.
   *
   * Being able to access the instance also has the advantage that we can invalidate
   * the Python instance when the data has been removed, see: #BPY_DECREF_RNA_INVALIDATE
   * so accessing the variables from Python raises an exception instead of crashing.
   */
  StructInstanceFunc instance;

  /** Return the location of the struct's pointer to the root group IDProperty. */
  IDPropertiesFunc idproperties;

  /* functions of this struct */
  ListBase functions;
};

/* Blender RNA
 *
 * Root RNA data structure that lists all struct types. */

struct BlenderRNA {
  ListBase structs;
  /* A map of structs: {StructRNA.identifier -> StructRNA}
   * These are ensured to have unique names (with STRUCT_PUBLIC_NAMESPACE enabled). */
  struct GHash *structs_map;
  /* Needed because types with an empty identifier aren't included in 'structs_map'. */
  unsigned int structs_len;
};

#define CONTAINER_RNA_ID(cont) (*(const char **)(((ContainerRNA *)(cont)) + 1))
