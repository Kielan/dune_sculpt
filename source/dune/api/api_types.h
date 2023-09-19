/* Use a define instead of `#pragma once` because of `BKE_addon.h`, `ED_object.h` & others. */
#ifndef __API_TYPES_H__
#define __API_TYPES_H__

#include "../dunelib/lib_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DuneApi;
struct ApiFn;
struct Id;
struct Main;
struct ParamList;
struct ApiProp;
struct ReportList;
struct ApiStruct;
struct Cxt;

/* Api ptrs are not a single C ptr but include the type,
 * and a ptr to the id struct that owns the struct, since
 * in some cases this information is needed to correctly get/set
 * the props and validate them. */

typedef struct ApiPtr {
  struct Id *owner_id;
  struct ApiStruct *type;
  void *data;
} ApiPtr;

typedef struct ApiPropPtr {
  ApiPtr ptr;
  struct ApiProp *prop;
} ApiPropPtr;
  
/* Stored result of an api path lookup (as used by anim-system) */
typedef struct ApiPathResolved {
  struct ApiPtr ptr;
  struct ApiProp *prop;
  /* -1 for non-array access. */
  int prop_index;
} ApiPathResolved;

/* Prop */
typedef enum PropType {
  PROP_BOOL = 0,
  PROP_INT = 1,
  PROP_FLOAT = 2,
  PROP_STRING = 3,
  PROP_ENUM = 4,
  PROP_POINTER = 5,
  PROP_COLLECTION = 6,
} PropType;

/* also update rna_property_subtype_unit when you change this */
typedef enum PropUnit {
  PROP_UNIT_NONE = (0 << 16),
  PROP_UNIT_LENGTH = (1 << 16),        /* m */
  PROP_UNIT_AREA = (2 << 16),          /* m^2 */
  PROP_UNIT_VOLUME = (3 << 16),        /* m^3 */
  PROP_UNIT_MASS = (4 << 16),          /* kg */
  PROP_UNIT_ROTATION = (5 << 16),      /* radians */
  PROP_UNIT_TIME = (6 << 16),          /* frame */
  PROP_UNIT_TIME_ABSOLUTE = (7 << 16), /* time in seconds (independent of scene) */
  PROP_UNIT_VELOCITY = (8 << 16),      /* m/s */
  PROP_UNIT_ACCELERATION = (9 << 16),  /* m/(s^2) */
  PROP_UNIT_CAMERA = (10 << 16),       /* mm */
  PROP_UNIT_POWER = (11 << 16),        /* W */
  PROP_UNIT_TEMPERATURE = (12 << 16),  /* C */
} PropUnit;

/* Use values besides PROP_SCALE_LINEAR
 * so the movement of the mouse doesn't map linearly to the value of the slider.
 *
 * For some settings it's useful to space motion in a non-linear way, see T77868.
 *
 * NOTE: The scale types are available for all float sliders.
 * For integer sliders they are only available if they use the visible value bar.
 * Sliders with logarithmic scale and value bar must have a range > 0
 * while logarithmic sliders without the value bar can have a range of >= 0. */
typedef enum PropScaleType {
  /* Linear scale (default). */
  PROP_SCALE_LINEAR = 0,
  /* Logarithmic scale
   * - Maximum range: `0 <= x < inf */
  PROP_SCALE_LOG = 1,
  /* Cubic scale.
   * - Maximum range: `-inf < x < inf` */
  PROP_SCALE_CUBIC = 2,
} PropScaleType;

#define API_SUBTYPE_UNIT(subtype) ((subtype)&0x00FF0000)
#define API_SUBTYPE_VALUE(subtype) ((subtype) & ~0x00FF0000)
#define API_SUBTYPE_UNIT_VALUE(subtype) ((subtype) >> 16)

#define API_ENUM_BITFLAG_SIZE 32

#define API_TRANSLATION_PREC_DEFAULT 5

#define API_STACK_ARRAY 32

/* note Also update enums in bpy_props.c and rna_rna.c when adding items here.
 * Watch it: these values are written to files as part of node socket button subtypes! */
typedef enum PropSubType {
  PROP_NONE = 0,

  /* strings */
  PROP_FILEPATH = 1,
  PROP_DIRPATH = 2,
  PROP_FILENAME = 3,
  /* A string which should be represented as bytes in python, NULL terminated though. */
  PROP_BYTESTRING = 4,
  /* 5 was used by "PROP_TRANSLATE" sub-type, which is now a flag. */
  /* A string which should not be displayed in UI. */
  PROP_PASSWORD = 6,

  /* numbers */
  /* A dimension in pixel units, possibly before DPI scaling (so value may not be the final pixel
   * value but the one to apply DPI scale to). */
  PROP_PIXEL = 12,
  PROP_UNSIGNED = 13,
  PROP_PERCENTAGE = 14,
  PROP_FACTOR = 15,
  PROP_ANGLE = 16 | PROP_UNIT_ROTATION,
  PROP_TIME = 17 | PROP_UNIT_TIME,
  PROP_TIME_ABSOLUTE = 17 | PROP_UNIT_TIME_ABSOLUTE,
  /* Distance in 3d space, don't use for pixel distance for eg. */
  PROP_DISTANCE = 18 | PROP_UNIT_LENGTH,
  PROP_DISTANCE_CAMERA = 19 | PROP_UNIT_CAMERA,

  /* number arrays */
  PROP_COLOR = 20,
  PROP_TRANSLATION = 21 | PROP_UNIT_LENGTH,
  PROP_DIRECTION = 22,
  PROP_VELOCITY = 23 | PROP_UNIT_VELOCITY,
  PROP_ACCELERATION = 24 | PROP_UNIT_ACCELERATION,
  PROP_MATRIX = 25,
  PROP_EULER = 26 | PROP_UNIT_ROTATION,
  PROP_QUATERNION = 27,
  PROP_AXISANGLE = 28,
  PROP_XYZ = 29,
  PROP_XYZ_LENGTH = 29 | PROP_UNIT_LENGTH,
  /* Used for colors which would be color managed before display. */
  PROP_COLOR_GAMMA = 30,
  /* Generic array, no units applied, only that x/y/z/w are used (Python vector). */
  PROP_COORDS = 31,

  /* booleans */
  PROP_LAYER = 40,
  PROP_LAYER_MEMBER = 41,

  /* Light */
  PROP_POWER = 42 | PROP_UNIT_POWER,

  /* temperature */
  PROP_TEMPERATURE = 43 | PROP_UNIT_TEMPERATURE,
} PropSubType;

/* Make sure enums are updated with these */
/* HIGHEST FLAG IN USE: 1 << 31
 * FREE FLAGS: 2, 9, 11, 13, 14, 15, 30 */
typedef enum PropFlag {
  /* Editable means the prop is editable in the user
   * interface, props are editable by default except
   * for ptrs and collections. */
  PROP_EDITABLE = (1 << 0),
  /* This prop is editable even if it is lib linked,
   * meaning it will get lost on reload, but it's useful
   * for editing. */
  PROP_LIB_EXCEPTION = (1 << 16),
  /* Animatable means the prop can be driven by some
   * other input, be it animation curves, expressions, ..
   * props are animatable by default except for ptrs
   * and collections. */
  PROP_ANIMATABLE = (1 << 1),
  /* This flag means when the prop's widget is in 'text-edit' mode, it will be updated
   * after every typed char, instead of waiting final validation. Used e.g. for text search-box.
   * It will also cause UI_BUT_VALUE_CLEAR to be set for text btns. We could add an own flag
   * for search/filter props, but this works just fine for now. */
  PROP_TEXTEDIT_UPDATE = (1u << 31),

  /* icon */
  PROP_ICONS_CONSECUTIVE = (1 << 12),
  PROP_ICONS_REVERSE = (1 << 8),

  /* Hidden in the user interface. */
  PROP_HIDDEN = (1 << 19),
  /* Do not write in presets. */
  PROP_SKIP_SAVE = (1 << 28),

  /* numbers */
  /* Each value is related proportionally (object scale, image size). */
  PROP_PROPORTIONAL = (1 << 26),

  /* ptrs */
  PROP_ID_REFCOUNT = (1 << 6),

  /* Disallow assigning a variable to itself, eg an object tracking itself
   * only apply this to types that are derived from an ID (). */
  PROP_ID_SELF_CHECK = (1 << 20),
  /* Use for...
   * - ptrs: in the UI and python so unsetting or setting to None won't work.
   * - strings: so our internal generated get/length/set
   *   fns know to do NULL checks before access T30865. */
  PROP_NEVER_NULL = (1 << 18),
  /* Currently only used for UI, this is similar to PROP_NEVER_NULL
   * except that the value may be NULL at times, used for ObData, where an Empty's will be NULL
   * but setting NULL on a mesh object is not possible.
   * So if it's not NULL, setting NULL can't be done */
  PROP_NEVER_UNLINK = (1 << 25),

  /* Ptrs to data that is not owned by the struct.
   * Typical example: Bone.parent, Bone.child, etc., and nearly all Id ptrs.
   * This is crucial information for processes that walk the whole data of an Id e.g.
   * (like lib override).
   * Note that all ID ptrs are enforced to this by default,
   * this probably will need to be rechecked
   * (see ugly infamous node-trees of material/texture/scene/etc.).  */
  PROP_PTR_NO_OWNERSHIP = (1 << 7),

  /* flag contains multiple enums.
   * NOTE: not to be confused with `prop->enumbitflags`
   * this exposes the flag as multiple options in python and the UI.
   *
   * note These can't be animated so use with care. */
  PROP_ENUM_FLAG = (1 << 21),

  /* need cxt for update function */
  PROP_CXT_UPDATE = (1 << 22),
  PROP_CXT_PROP_UPDATE = PROP_CXT_UPDATE | (1 << 27),

  /* registering */
  PROP_REGISTER = (1 << 4),
  PROP_REGISTER_OPTIONAL = PROP_REGISTER | (1 << 5),

  /* Use for allocated function return values of arrays or strings
   * for any data that should not have a reference kept.
   *
   * It can be used for properties which are dynamically allocated too.
   *
   * note Currently dynamic sized thick wrapped data isn't supported.
   * This would be a useful addition and avoid a fixed maximum sized as in done at the moment */
  PROP_THICK_WRAP = (1 << 23),

  /* This is an IdProp, not a DNA one. */
  PROP_IDPROP = (1 << 10),
  /* For dynamic arrays, and retvals of type string. */
  PROP_DYNAMIC = (1 << 17),
  /* For enum that shouldn't be contextual */
  PROP_ENUM_NO_CXT = (1 << 24),
  /* For enums not to be translated (e.g. viewlayers' names in nodes). */
  PROP_ENUM_NO_TRANSLATE = (1 << 29),

  /* Don't do dependency graph tag from a property update callback.
   * Use this for properties which defines interface state, for example,
   * properties which denotes whether modifier panel is collapsed or not. */
  PROP_NO_GRAPH_UPDATE = (1 << 30),
} PropFlag;

/* Flags related to comparing and overriding api props.
 * Make sure enums are updated with these.
 * FREE FLAGS: 2, 3, 4, 5, 6, 7, 8, 9, 12 and above. */
typedef enum PropOverrideFlag {
  /* Means that the property can be overridden by a local override of some linked datablock. */
  PROPOVERRIDE_OVERRIDABLE_LIB = (1 << 0),

  /* Forbid usage of this property in comparison (& hence override) code.
   * Useful e.g. for collections of data like mesh's geometry, particles, etc.
   * Also for runtime data that should never be considered as part of actual Dune data (e.g.
   * depsgraph from ViewLayers...). */
  PROPOVERRIDE_NO_COMPARISON = (1 << 1),

  /* Means the prop can be fully ignored by override process.
   * Unlike NO_COMPARISON, it can still be used by diffing code, but no override operation will be
   * created for it, and no attempt to restore the data from linked reference either.
   *
   * WARNING: This flag should be used with a lot of caution, as it completely by-passes override
   * system. It is currently only used for ID's names, since we cannot prevent local override to
   * get a different name from the linked ref, and ID names are 'rna name property' (i.e. are
   * used in overrides of collections of IDs). See also `dune_lib_override_lib_update()` where
   * we deal manually with the value of that property at DNA level. */
  PROPOVERRIDE_IGNORE = (1 << 2),

  /* Collections-related */
  /* The prop supports insertion (collections only). */
  PROPOVERRIDE_LIB_INSERTION = (1 << 10),

  /* Only use indices to compare items in the property, never names (collections only).
   * Useful when nameprop of the items is generated from other data
   * (e.g. name of material slots is actually name of assigned material). */
  PROPOVERRIDE_NO_PROP_NAME = (1 << 11),
} PropOverrideFlag;

/* Fn params flags.
 * warning 16bits only */
typedef enum ParamFlag {
  PARM_REQUIRED = (1 << 0),
  PARM_OUTPUT = (1 << 1),
  PARM_APIPTR = (1 << 2),
  /* This allows for non-breaking API updates,
   * when adding non-critical new parameter to a callback function.
   * This way, old py code defining funcs without that parameter would still work.
   * WARNING: any parameter after the first PYFUNC_OPTIONAL one will be considered as optional!
   * note only for input params!  */
  PARM_PYFUNC_OPTIONAL = (1 << 3),
} ParamFlag;

struct CollectionPropIter;
struct Link;
typedef int (*IterSkipFn)(struct CollectionPropIter *iter, void *data);

typedef struct ListIter {
  struct Link *link;
  int flag;
  IterSkipFn skip;
} ListIter;

typedef struct ArrayIter {
  char *ptr;
  /* Past the last valid pointer, only for comparisons, ignores skipped values. */
  char *endptr;
  /* Will be freed if set. */
  void *free_ptr;
  int itemsize;

  /* Array length with no skip functions applied,
   * take care not to compare against index from animsys or Python indice  */
  int length;

  /* Optional skip function,
   * when set the array as viewed by rna can contain only a subset of the members.
   * this changes indices so quick array index lookups are not possible when skip function is used.  */
  IterSkipFn skip;
} ArrayIter;

typedef struct CountIter {
  void *ptr;
  int item;
} CountIter;

typedef struct CollectionPropIter {
  /* internal */
  ApiPtr parent;
  ApiPtr builtin_parent;
  struct ApiProp *prop;
  union {
    ArrayIter array;
    ListIter list;
    CountIter count;
    void *custom;
  } internal;
  int idprop;
  int level;

  /* external */
  ApiPtr ptr;
  int valid;
} CollectionPropIter;

typedef struct CollectionPtrLink {
  struct CollectionPtrLink *next, *prev;
  ApiPtr ptr;
} CollectionPtrLink;

/* Copy of List for API. */
typedef struct CollectionList {
  struct CollectionPtrLink *first, *last;
} CollectionList;

typedef enum RawPropType {
  PROP_RAW_UNSET = -1,
  PROP_RAW_INT, /* XXX: abused for types that are not set, eg. MFace.verts, needs fixing. */
  PROP_RAW_SHORT,
  PROP_RAW_CHAR,
  PROP_RAW_BOOL,
  PROP_RAW_DOUBLE,
  PROP_RAW_FLOAT,
} RawPropType;

typedef struct RawArray {
  void *array;
  RawPropType type;
  int len;
  int stride;
} RawArray;

/* This struct is are typically defined in arrays which define an *enum* for RNA,
 * which is used by the api api both for user-interface and the Python AP */
typedef struct EnumPropItem {
  /* The internal value of the enum, not exposed to users. */
  int value;
  /* Note that identifiers must be unique within the array,
   * by convention they're upper case with underscores for separators.
   * - An empty string is used to define menu separators.
   * - NULL denotes the end of the array of items. */
  const char *id;
  /* Optional icon, typically 'ICON_NONE' */
  int icon;
  /* Name displayed in the interface. */
  const char *name;
  /* Longer description used in the interface. */
  const char *description;
} EnumPropItem;

/* extended versions with ApiProp argument */
typedef bool (*BoolPropGetFn)(struct ApiPtr *ptr, struct ApiProp *prop);
typedef void (*BoolPropSetFn)(struct ApiPtr *ptr,
                              struct ApiProp *prop,
                              bool value);
typedef void (*BoolArrayPropGetFn)(struct ApiPtr *ptr,
                                   struct ApiProp *prop,
                                   bool *values);
typedef void (*BoolArrayPropSetFn)(struct ApiPtr *ptr,
                                   struct ApiProp *prop,
                                   const bool *values);
typedef int (*IntPropGetFn)(struct ApiPtr *ptr, struct ApiProp *prop);
typedef void (*IntPropSetFn)(struct ApiPtr *ptr, struct ApiProp *prop, int value);
typedef void (*IntArrayPropGetFn)(struct ApiPtr *ptr,
                                  struct ApiProp *prop,
                                  int *values);
typedef void (*IntArrayPropSetFn)(struct ApiPtr *ptr,
                                  struct ApiProp *prop,
                                  const int *values);
typedef void (*IntPropRangeFn)(struct ApiPtr *ptr,
                               struct ApiProp *prop,
                               int *min,
                               int *max,
                               int *softmin,
                               int *softmax);
typedef float (*FloatPropGetFn)(struct ApiPtr *ptr, struct ApiProp *prop);
typedef void (*FloatPropSetFn)(struct ApiPtr *ptr,
                               struct ApiProp *prop,
                               float value);
typedef void (*FloatArrayPropGetFn)(struct ApiPtr *ptr,
                                    struct ApiProp *prop,
                                    float *values);
typedef void (*FloatArrayPropSetFn)(struct ApiPtr *ptr,
                                    struct ApiProp *prop,
                                    const float *values);
typedef void (*FloatPropRangeFn)(struct ApiPtr *ptr,
                                 struct ApiProp *prop,
                                 float *min,
                                 float *max,
                                 float *softmin,
                                 float *softmax);
typedef void (*StringPropGetFn)(struct ApiPtr *ptr,
                                struct ApiProp *prop,
                                char *value);
typedef int (*StringPropLengthFn)(struct ApiPtr *ptr, struct ApiProp *prop);
typedef void (*StringPropSetFn)(struct ApiPtr *ptr,
                                struct ApiProp *prop,
                                const char *value);
typedef int (*EnumPropGetFn)(struct ApiPtr *ptr, struct ApiPropA *prop);
typedef void (*EnumPropSetFn)(struct ApiPtr *ptr, struct ApiProp *prop, int value);
/* same as PropEnumItemFn */
typedef const EnumPropItem *(*EnumPropItemFn)(struct Cxt *C,
                                              ApiPtr *ptr,
                                              struct ApiProp *prop,
                                              bool *r_free);

typedef struct ApiProp ApiProp;

/* Param List */
typedef struct ParamList {
  /* Storage for params */
  void *data;

  /* Fn passed at creation time. */
  struct ApiFn *fn;

  /* Store the param size. */
  int alloc_size;

  int arg_count, ret_count;
} ParamList;

typedef struct ParamIter {
  struct ParamList *parms;
  // ApiPtr fncptr; /* UNUSED */
  void *data;
  int size, offset;

  ApiProp *parm;
  int valid;
} ParamIter;

/* Mainly to avoid confusing casts. */
typedef struct ParamDynAlloc {
  /* Important, this breaks when set to an int. */
  intptr_t array_tot;
  void *array;
} ParamDynAlloc;

/* Fn */
/* Opts affecting cb signature.
 *
 * Those add additional params at the beginning of the C cb, like that:
 * api_my_fn([Id *_selfid],
 *             [<TYPE_STRUCT> *self|ApiStruct *type],
 *             [Main *main],
 *             [Cxt *C],
 *             [ReportList *reports],
 *             <other Api-defined params>); */
typedef enum FnFlag {
  /* Pass Id owning 'self' data
   * (i.e. ptr->owner_id, might be same as self in case data is an ID...) */
  FN_USE_SELF_ID = (1 << 11),

  /* Do not pass the object (struct pointer) from which it is called,
   * used to define static or class functions. */
  FN_NO_SELF = (1 << 0),
  /** Pass API type, used to define class functions, only valid when #FUNC_NO_SELF is set. */
  FN_USE_SELF_TYPE = (1 << 1),

  /* Pass Main, Cxt and/or ReportList. */
  FN_USE_MAIN = (1 << 2),
  FN_USE_CXT = (1 << 3),
  FN_USE_REPORTS = (1 << 4),

  /* Registering of Python subclasses. *****/
  /* This function is part of the registerable class' interface,
   * and can be implemented/redefined in Python. */
  FN_REGISTER = (1 << 5),
  /** Subclasses can choose not to implement this function. */
  FN_REGISTER_OPTIONAL = FN_REGISTER | (1 << 6),
  /* If not set, the Python function implementing this call
   * is not allowed to write into data-blocks.
   * Except for WindowManager and Screen currently, see api_id_write_error() in bpy_rna.  */
  FN_ALLOW_WRITE = (1 << 12),

  /* Internal flags. *****/
  /* UNUSED CURRENTLY? ??? */
  FN_BUILTIN = (1 << 7),
  /* UNUSED CURRENTLY. ??? */
  FN_EXPORT = (1 << 8),
  /* Fn has been defined at runtime, not statically in api source code. */
  FN_RUNTIME = (1 << 9),
  /* UNUSED CURRENTLY? Fn owns its id and description strings,
   * and has to free them when deleted */
  FN_FREE_PTRS = (1 << 10),
} FnFlag;

typedef void (*CallFn)(struct Cxt *C,
                         struct ReportList *reports,
                         ApiPtr *ptr,
                         ParamList *parms);

typedef struct ApiFn ApiFn;

/* Struct */
typedef enum StructFlag {
  /* Indicates that this struct is an ID struct, and to use reference-counting. */
  STRUCT_ID = (1 << 0),
  STRUCT_ID_REFCOUNT = (1 << 1),
  /* defaults on, indicates when changes in members of a StructRNA should trigger undo steps. */
  STRUCT_UNDO = (1 << 2),

  /* internal flags */
  STRUCT_RUNTIME = (1 << 3),
  /* STRUCT_GENERATED = (1 << 4), */ /* UNUSED */
  STRUCT_FREE_PTRS = (1 << 5),
  /* Menus and Panels don't need props */
  STRUCT_NO_IDPROPS = (1 << 6),
  /* e.g. for Operator */
  STRUCT_NO_DATABLOCK_IDPROPS = (1 << 7),
  /* for PropGroup which contains ptrs to datablocks */
  STRUCT_CONTAINS_DATABLOCK_IDPROPS = (1 << 8),
  /* Added to type-map DuneApi.structs_map */
  STRUCT_PUBLIC_NAMESPACE = (1 << 9),
  /* All subtypes are added too. */
  STRUCT_PUBLIC_NAMESPACE_INHERIT = (1 << 10),
  /* When the ApiPtr.owner_id is NULL, this signifies the prop should be accessed
   * without any cxt (the key-map UI and import/export for example).
   * So accessing the prop should not read from the current cxt to derive values/limits. */
  STRUCT_NO_CXT_WITHOUT_OWNER_ID = (1 << 11),
} StructFlag;

typedef int (*StructValidateFn)(struct ApiPtr *ptr, void *data, int *have_fn);
typedef int (*StructCbFn)(struct Cxt *C,
                          struct ApiPtr *ptr,
                          struct ApiFn *fn,
                          ParamList *list);
typedef void (*StructFreeFn)(void *data);
typedef struct ApiStruct *(*StructRegisterFn)(struct Main *main,
                                              struct ReportList *reports,
                                              void *data,
                                              const char *id,
                                              StructValidateFn validate,
                                              StructCbFn call,
                                              StructFreeFn free);

typedef void (*StructUnregisterFn)(struct Main *main, struct ApiStruct *type);
typedef void **(*StructInstanceFn)(ApiPtr *ptr);

typedef struct ApiStruct ApiStruct;

/* Dune Api
 * Root API data structure that lists all struct types. */
typedef struct ApiDune ApiDune;

/* Extending
 * This struct must be embedded in *Type structs in
 * order to make them definable through API */
typedef struct ApiExtension {
  void *data;
  ApiStruct *api;
  StructCbFb call;
  StructFreeFn free;
} ApiExtension;

#ifdef __cplusplus
}
#endif

#endif /* __API_TYPES_H__ */
