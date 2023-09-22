#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_id.h"
#include "typed_constraint.h"
#include "types_modifier.h"
#include "types_scene.h"
#include "types_windowmanager.h"

#include "lib_alloca.h"
#include "lib_dunelib.h"
#include "lib_dynstr.h"
#include "lib_ghash.h"
#include "lib_math.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "BLF_api.h"
#include "BLT_translation.h"

#include "dune_anim_data.h"
#include "dune_collection.h"
#include "dune_context.h"
#include "dune_fcurve.h"
#include "dune_idprop.h"
#include "dune_idtype.h"
#include "dune_main.h"
#include "dune_node.h"
#include "dune_report.h"

#include "graph.h"
#include "graph_build.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "wm_api.h"
#include "wm_message.h"

/* flush updates */
#include "types_object.h"
#include "wm_types.h"

#include "api_access_internal.h
#include "api_internal.h"

const ApiPtt ApiPtr_NULL = {NULL};

/* Init/Exit */

void api_init(void)
{
  ApiStruct *srna;
  ApiProp *prop;

  DUNE_API.structs_map = lib_ghash_str_new_ex(__func__, 2048);
  DUNE_API.structs_len = 0;

  for (srna = DUNE_API.structs.first; srna; srna = srna->cont.next) {
    if (!srna->cont.prophash) {
      srna->cont.prophash = lib_ghash_str_new("RNA_init gh");

      for (prop = srna->cont.properties.first; prop; prop = prop->next) {
        if (!(prop->flag_internal & PROP_INTERN_BUILTIN)) {
          lib_ghash_insert(srna->cont.prophash, (void *)prop->id, prop);
        }
      }
    }
    lib_assert(srna->flag & STRUCT_PUBLIC_NAMESPACE);
    lib_ghash_insert(DUNE_API.structs_map, (void *)srna->identifier, srna);
    DUNE_API.structs_len += 1;
  }
}

void api_exit(void)
{
  ApiStruct *srna;

  for (srna = DUNE_API.structs.first; srna; srna = srna->cont.next) {
    if (srna->cont.prophash) {
      lib_ghash_free(srna->cont.prophash, NULL, NULL);
      srna->cont.prophash = NULL;
    }
  }

  api_free(&DUNE_API);
}

/* Pointer */

void api_main_ptr_create(struct Main *main, PointerRNA *r_ptr)
{
  r_ptr->owner_id = NULL;
  r_ptr->type = &RNA_BlendData;
  r_ptr->data = main;
}

void RNA_id_pointer_create(ID *id, PointerRNA *r_ptr)
{
  StructRNA *type, *idtype = NULL;

  if (id) {
    PointerRNA tmp = {NULL};
    tmp.data = id;
    idtype = rna_ID_refine(&tmp);

    while (idtype->refine) {
      type = idtype->refine(&tmp);

      if (type == idtype) {
        break;
      }
      idtype = type;
    }
  }

  r_ptr->owner_id = id;
  r_ptr->type = idtype;
  r_ptr->data = id;
}

void api_ptr_create(Id *id, ApiStruct *type, void *data, ApiPtr *r_ptr)
{
#if 0 /* UNUSED */
  ApiStruct *idtype = NULL;

  if (id) {
    PointerRNA tmp = {0};
    tmp.data = id;
    idtype = rna_ID_refine(&tmp);
  }
#endif

  r_ptr->owner_id = id;
  r_ptr->type = type;
  r_ptr->data = data;

  if (data) {
    while (r_ptr->type && r_ptr->type->refine) {
      StructRNA *rtype = r_ptr->type->refine(r_ptr);

      if (rtype == r_ptr->type) {
        break;
      }
      r_ptr->type = rtype;
    }
  }
}

bool RNA_pointer_is_null(const PointerRNA *ptr)
{
  return (ptr->data == NULL) || (ptr->owner_id == NULL) || (ptr->type == NULL);
}

static void rna_pointer_inherit_id(StructRNA *type, PointerRNA *parent, PointerRNA *ptr)
{
  if (type && type->flag & STRUCT_ID) {
    ptr->owner_id = ptr->data;
  }
  else {
    ptr->owner_id = parent->owner_id;
  }
}

void api_dune_api_ptr_create(ApiPtr *r_ptr)
{
  r_ptr->owner_id = NULL;
  r_ptr->type = &DuneApi;
  r_ptr->data = &DuneAPI;
}

ApiPtr api_ptr_inherit_refine(ApiPtr *ptr, StructRNA *type, void *data)
{
  if (data) {
    ApiPtr result;
    result.data = data;
    result.type = type;
    api_ptr_inherit_id(type, ptr, &result);

    while (result.type->refine) {
      type = result.type->refine(&result);

      if (type == result.type) {
        break;
      }
      result.type = type;
    }
    return result;
  }
  return PointerRNA_NULL;
}

void RNA_pointer_recast(PointerRNA *ptr, PointerRNA *r_ptr)
{
#if 0 /* works but this case if covered by more general code below. */
  if (RNA_struct_is_ID(ptr->type)) {
    /* simple case */
    RNA_id_pointer_create(ptr->owner_id, r_ptr);
  }
  else
#endif
  {
    StructRNA *base;
    PointerRNA t_ptr;
    *r_ptr = *ptr; /* initialize as the same in case can't recast */

    for (base = ptr->type->base; base; base = base->base) {
      t_ptr = rna_pointer_inherit_refine(ptr, base, ptr->data);
      if (t_ptr.type && t_ptr.type != ptr->type) {
        *r_ptr = t_ptr;
      }
    }
  }
}

/* ID Properties */

void rna_idproperty_touch(IDProperty *idprop)
{
  /* so the property is seen as 'set' by rna */
  idprop->flag &= ~IDP_FLAG_GHOST;
}

IDProperty **RNA_struct_idprops_p(PointerRNA *ptr)
{
  StructRNA *type = ptr->type;
  if (type == NULL) {
    return NULL;
  }
  if (type->idproperties == NULL) {
    return NULL;
  }

  return type->idproperties(ptr);
}

IDProperty *RNA_struct_idprops(PointerRNA *ptr, bool create)
{
  IDProperty **property_ptr = RNA_struct_idprops_p(ptr);
  if (property_ptr == NULL) {
    return NULL;
  }

  if (create && *property_ptr == NULL) {
    IDPropertyTemplate val = {0};
    *property_ptr = IDP_New(IDP_GROUP, &val, __func__);
  }

  return *property_ptr;
}

bool RNA_struct_idprops_check(StructRNA *srna)
{
  return (srna && srna->idproperties);
}

IDProperty *rna_idproperty_find(PointerRNA *ptr, const char *name)
{
  IDProperty *group = RNA_struct_idprops(ptr, 0);

  if (group) {
    if (group->type == IDP_GROUP) {
      return IDP_GetPropertyFromGroup(group, name);
    }
    /* Not sure why that happens sometimes, with nested properties... */
    /* Seems to be actually array prop, name is usually "0"... To be sorted out later. */
#if 0
      printf(
          "Got unexpected IDProp container when trying to retrieve %s: %d\n", name, group->type);
#endif
  }

  return NULL;
}

static void rna_idproperty_free(PointerRNA *ptr, const char *name)
{
  IDProperty *group = RNA_struct_idprops(ptr, 0);

  if (group) {
    IDProperty *idprop = IDP_GetPropertyFromGroup(group, name);
    if (idprop) {
      IDP_FreeFromGroup(group, idprop);
    }
  }
}

static int rna_ensure_property_array_length(PointerRNA *ptr, PropertyRNA *prop)
{
  if (prop->magic == RNA_MAGIC) {
    int arraylen[RNA_MAX_ARRAY_DIMENSION];
    return (prop->getlength && ptr->data) ? prop->getlength(ptr, arraylen) : prop->totarraylength;
  }
  IDProperty *idprop = (IDProperty *)prop;

  if (idprop->type == IDP_ARRAY) {
    return idprop->len;
  }
  return 0;
}

static bool rna_ensure_property_array_check(PropertyRNA *prop)
{
  if (prop->magic == RNA_MAGIC) {
    return (prop->getlength || prop->totarraylength);
  }
  IDProperty *idprop = (IDProperty *)prop;

  return (idprop->type == IDP_ARRAY);
}

static void rna_ensure_property_multi_array_length(PointerRNA *ptr,
                                                   PropertyRNA *prop,
                                                   int length[])
{
  if (prop->magic == RNA_MAGIC) {
    if (prop->getlength) {
      prop->getlength(ptr, length);
    }
    else {
      memcpy(length, prop->arraylength, prop->arraydimension * sizeof(int));
    }
  }
  else {
    IDProperty *idprop = (IDProperty *)prop;

    if (idprop->type == IDP_ARRAY) {
      length[0] = idprop->len;
    }
    else {
      length[0] = 0;
    }
  }
}

static bool rna_idproperty_verify_valid(PointerRNA *ptr, PropertyRNA *prop, IDProperty *idprop)
{
  /* this verifies if the idproperty actually matches the property
   * description and otherwise removes it. this is to ensure that
   * rna property access is type safe, e.g. if you defined the rna
   * to have a certain array length you can count on that staying so */

  switch (idprop->type) {
    case IDP_IDPARRAY:
      if (prop->type != PROP_COLLECTION) {
        return false;
      }
      break;
    case IDP_ARRAY:
      if (rna_ensure_property_array_length(ptr, prop) != idprop->len) {
        return false;
      }

      if (idprop->subtype == IDP_FLOAT && prop->type != PROP_FLOAT) {
        return false;
      }
      if (idprop->subtype == IDP_INT && !ELEM(prop->type, PROP_BOOLEAN, PROP_INT, PROP_ENUM)) {
        return false;
      }

      break;
    case IDP_INT:
      if (!ELEM(prop->type, PROP_BOOLEAN, PROP_INT, PROP_ENUM)) {
        return false;
      }
      break;
    case IDP_FLOAT:
    case IDP_DOUBLE:
      if (prop->type != PROP_FLOAT) {
        return false;
      }
      break;
    case IDP_STRING:
      if (prop->type != PROP_STRING) {
        return false;
      }
      break;
    case IDP_GROUP:
    case IDP_ID:
      if (prop->type != PROP_POINTER) {
        return false;
      }
      break;
    default:
      return false;
  }

  return true;
}

static PropertyRNA *typemap[IDP_NUMTYPES] = {
    &rna_PropertyGroupItem_string,
    &rna_PropertyGroupItem_int,
    &rna_PropertyGroupItem_float,
    NULL,
    NULL,
    NULL,
    &rna_PropertyGroupItem_group,
    &rna_PropertyGroupItem_id,
    &rna_PropertyGroupItem_double,
    &rna_PropertyGroupItem_idp_array,
};

static PropertyRNA *arraytypemap[IDP_NUMTYPES] = {
    NULL,
    &rna_PropertyGroupItem_int_array,
    &rna_PropertyGroupItem_float_array,
    NULL,
    NULL,
    NULL,
    &rna_PropertyGroupItem_collection,
    NULL,
    &rna_PropertyGroupItem_double_array,
};

void rna_property_rna_or_id_get(PropertyRNA *prop,
                                PointerRNA *ptr,
                                PropertyRNAOrID *r_prop_rna_or_id)
{
  /* This is quite a hack, but avoids some complexity in the API. we
   * pass IDProperty structs as PropertyRNA pointers to the outside.
   * We store some bytes in PropertyRNA structs that allows us to
   * distinguish it from IDProperty structs. If it is an ID property,
   * we look up an IDP PropertyRNA based on the type, and set the data
   * pointer to the IDProperty. */
  memset(r_prop_rna_or_id, 0, sizeof(*r_prop_rna_or_id));

  r_prop_rna_or_id->ptr = *ptr;
  r_prop_rna_or_id->rawprop = prop;

  if (prop->magic == RNA_MAGIC) {
    r_prop_rna_or_id->rnaprop = prop;
    r_prop_rna_or_id->identifier = prop->identifier;

    r_prop_rna_or_id->is_array = prop->getlength || prop->totarraylength;
    if (r_prop_rna_or_id->is_array) {
      int arraylen[RNA_MAX_ARRAY_DIMENSION];
      r_prop_rna_or_id->array_len = (prop->getlength && ptr->data) ?
                                        (uint)prop->getlength(ptr, arraylen) :
                                        prop->totarraylength;
    }

    if (prop->flag & PROP_IDPROPERTY) {
      IDProperty *idprop = rna_idproperty_find(ptr, prop->identifier);

      if (idprop != NULL && !rna_idproperty_verify_valid(ptr, prop, idprop)) {
        IDProperty *group = RNA_struct_idprops(ptr, 0);

        IDP_FreeFromGroup(group, idprop);
        idprop = NULL;
      }

      r_prop_rna_or_id->idprop = idprop;
      r_prop_rna_or_id->is_set = idprop != NULL && (idprop->flag & IDP_FLAG_GHOST) == 0;
    }
    else {
      /* Full static RNA properties are always set. */
      r_prop_rna_or_id->is_set = true;
    }
  }
  else {
    IDProperty *idprop = (IDProperty *)prop;
    /* Given prop may come from the custom properties of another data, ensure we get the one from
     * given data ptr. */
    IDProperty *idprop_evaluated = rna_idproperty_find(ptr, idprop->name);
    if (idprop_evaluated != NULL && idprop->type != idprop_evaluated->type) {
      idprop_evaluated = NULL;
    }

    r_prop_rna_or_id->idprop = idprop_evaluated;
    r_prop_rna_or_id->is_idprop = true;
    /* Full IDProperties are always set, if it exists. */
    r_prop_rna_or_id->is_set = (idprop_evaluated != NULL);

    r_prop_rna_or_id->identifier = idprop->name;
    if (idprop->type == IDP_ARRAY) {
      r_prop_rna_or_id->rnaprop = arraytypemap[(int)(idprop->subtype)];
      r_prop_rna_or_id->is_array = true;
      r_prop_rna_or_id->array_len = idprop_evaluated != NULL ? (uint)idprop_evaluated->len : 0;
    }
    else {
      r_prop_rna_or_id->rnaprop = typemap[(int)(idprop->type)];
    }
  }
}

IDProperty *rna_idproperty_check(PropertyRNA **prop, PointerRNA *ptr)
{
  PropertyRNAOrID prop_rna_or_id;

  rna_property_rna_or_id_get(*prop, ptr, &prop_rna_or_id);

  *prop = prop_rna_or_id.rnaprop;
  return prop_rna_or_id.idprop;
}

PropertyRNA *rna_ensure_property_realdata(PropertyRNA **prop, PointerRNA *ptr)
{
  PropertyRNAOrID prop_rna_or_id;

  rna_property_rna_or_id_get(*prop, ptr, &prop_rna_or_id);

  *prop = prop_rna_or_id.rnaprop;
  return (prop_rna_or_id.is_idprop || prop_rna_or_id.idprop != NULL) ?
             (PropertyRNA *)prop_rna_or_id.idprop :
             prop_rna_or_id.rnaprop;
}

PropertyRNA *rna_ensure_property(PropertyRNA *prop)
{
  /* the quick version if we don't need the idproperty */

  if (prop->magic == RNA_MAGIC) {
    return prop;
  }

  {
    IDProperty *idprop = (IDProperty *)prop;

    if (idprop->type == IDP_ARRAY) {
      return arraytypemap[(int)(idprop->subtype)];
    }
    return typemap[(int)(idprop->type)];
  }
}

static const char *rna_ensure_property_identifier(const PropertyRNA *prop)
{
  if (prop->magic == RNA_MAGIC) {
    return prop->identifier;
  }
  return ((const IDProperty *)prop)->name;
}

static const char *rna_ensure_property_description(const PropertyRNA *prop)
{
  if (prop->magic == RNA_MAGIC) {
    return prop->description;
  }

  const IDProperty *idprop = (const IDProperty *)prop;
  if (idprop->ui_data) {
    const IDPropertyUIData *ui_data = idprop->ui_data;
    return ui_data->description;
  }

  return "";
}

static const char *rna_ensure_property_name(const PropertyRNA *prop)
{
  const char *name;

  if (prop->magic == RNA_MAGIC) {
    name = prop->name;
  }
  else {
    name = ((const IDProperty *)prop)->name;
  }

  return name;
}

/* Structs */

StructRNA *RNA_struct_find(const char *identifier)
{
  return BLI_ghash_lookup(BLENDER_RNA.structs_map, identifier);
}

const char *RNA_struct_identifier(const StructRNA *type)
{
  return type->identifier;
}

const char *RNA_struct_ui_name(const StructRNA *type)
{
  return CTX_IFACE_(type->translation_context, type->name);
}

const char *RNA_struct_ui_name_raw(const StructRNA *type)
{
  return type->name;
}

int RNA_struct_ui_icon(const StructRNA *type)
{
  if (type) {
    return type->icon;
  }
  return ICON_DOT;
}

const char *RNA_struct_ui_description(const StructRNA *type)
{
  return TIP_(type->description);
}

const char *RNA_struct_ui_description_raw(const StructRNA *type)
{
  return type->description;
}

const char *RNA_struct_translation_context(const StructRNA *type)
{
  return type->translation_context;
}

PropertyRNA *RNA_struct_name_property(const StructRNA *type)
{
  return type->nameproperty;
}

const EnumPropertyItem *RNA_struct_property_tag_defines(const StructRNA *type)
{
  return type->prop_tag_defines;
}

PropertyRNA *RNA_struct_iterator_property(StructRNA *type)
{
  return type->iteratorproperty;
}

StructRNA *RNA_struct_base(StructRNA *type)
{
  return type->base;
}

const StructRNA *RNA_struct_base_child_of(const StructRNA *type, const StructRNA *parent_type)
{
  while (type) {
    if (type->base == parent_type) {
      return type;
    }
    type = type->base;
  }
  return NULL;
}

bool RNA_struct_is_ID(const StructRNA *type)
{
  return (type->flag & STRUCT_ID) != 0;
}

bool RNA_struct_undo_check(const StructRNA *type)
{
  return (type->flag & STRUCT_UNDO) != 0;
}

bool RNA_struct_idprops_register_check(const StructRNA *type)
{
  return (type->flag & STRUCT_NO_IDPROPERTIES) == 0;
}

bool RNA_struct_idprops_datablock_allowed(const StructRNA *type)
{
  return (type->flag & (STRUCT_NO_DATABLOCK_IDPROPERTIES | STRUCT_NO_IDPROPERTIES)) == 0;
}

bool RNA_struct_idprops_contains_datablock(const StructRNA *type)
{
  return (type->flag & (STRUCT_CONTAINS_DATABLOCK_IDPROPERTIES | STRUCT_ID)) != 0;
}

bool RNA_struct_idprops_unset(PointerRNA *ptr, const char *identifier)
{
  IDProperty *group = RNA_struct_idprops(ptr, 0);

  if (group) {
    IDProperty *idp = IDP_GetPropertyFromGroup(group, identifier);
    if (idp) {
      IDP_FreeFromGroup(group, idp);

      return true;
    }
  }
  return false;
}

bool RNA_struct_is_a(const StructRNA *type, const StructRNA *srna)
{
  const StructRNA *base;

  if (srna == &RNA_AnyType) {
    return true;
  }

  if (!type) {
    return false;
  }

  /* ptr->type is always maximally refined */
  for (base = type; base; base = base->base) {
    if (base == srna) {
      return true;
    }
  }

  return false;
}

PropertyRNA *RNA_struct_find_property(PointerRNA *ptr, const char *identifier)
{
  if (identifier[0] == '[' && identifier[1] == '"') {
    /* id prop lookup, not so common */
    PropertyRNA *r_prop = NULL;
    PointerRNA r_ptr; /* only support single level props */
    if (RNA_path_resolve_property(ptr, identifier, &r_ptr, &r_prop) && (r_ptr.type == ptr->type) &&
        (r_ptr.data == ptr->data)) {
      return r_prop;
    }
  }
  else {
    /* most common case */
    PropertyRNA *iterprop = RNA_struct_iterator_property(ptr->type);
    PointerRNA propptr;

    if (RNA_property_collection_lookup_string(ptr, iterprop, identifier, &propptr)) {
      return propptr.data;
    }
  }

  return NULL;
}

/* Find the property which uses the given nested struct */
static PropertyRNA *RNA_struct_find_nested(PointerRNA *ptr, StructRNA *srna)
{
  PropertyRNA *prop = NULL;

  RNA_STRUCT_BEGIN (ptr, iprop) {
    /* This assumes that there can only be one user of this nested struct */
    if (RNA_property_pointer_type(ptr, iprop) == srna) {
      prop = iprop;
      break;
    }
  }
  RNA_PROP_END;

  return prop;
}

bool RNA_struct_contains_property(PointerRNA *ptr, PropertyRNA *prop_test)
{
  /* NOTE: prop_test could be freed memory, only use for comparison. */

  /* validate the RNA is ok */
  PropertyRNA *iterprop;
  bool found = false;

  iterprop = RNA_struct_iterator_property(ptr->type);

  RNA_PROP_BEGIN (ptr, itemptr, iterprop) {
    /* PropertyRNA *prop = itemptr.data; */
    if (prop_test == (PropertyRNA *)itemptr.data) {
      found = true;
      break;
    }
  }
  RNA_PROP_END;

  return found;
}

unsigned int RNA_struct_count_properties(StructRNA *srna)
{
  PointerRNA struct_ptr;
  unsigned int counter = 0;

  RNA_pointer_create(NULL, srna, NULL, &struct_ptr);

  RNA_STRUCT_BEGIN (&struct_ptr, prop) {
    counter++;
    UNUSED_VARS(prop);
  }
  RNA_STRUCT_END;

  return counter;
}

const struct ListBase *RNA_struct_type_properties(StructRNA *srna)
{
  return &srna->cont.properties;
}

PropertyRNA *RNA_struct_type_find_property_no_base(StructRNA *srna, const char *identifier)
{
  return BLI_findstring_ptr(&srna->cont.properties, identifier, offsetof(PropertyRNA, identifier));
}

PropertyRNA *RNA_struct_type_find_property(StructRNA *srna, const char *identifier)
{
  for (; srna; srna = srna->base) {
    PropertyRNA *prop = RNA_struct_type_find_property_no_base(srna, identifier);
    if (prop != NULL) {
      return prop;
    }
  }
  return NULL;
}

FunctionRNA *RNA_struct_find_function(StructRNA *srna, const char *identifier)
{
#if 1
  FunctionRNA *func;
  for (; srna; srna = srna->base) {
    func = (FunctionRNA *)BLI_findstring_ptr(
        &srna->functions, identifier, offsetof(FunctionRNA, identifier));
    if (func) {
      return func;
    }
  }
  return NULL;

  /* functional but slow */
#else
  PointerRNA tptr;
  PropertyRNA *iterprop;
  FunctionRNA *func;

  RNA_pointer_create(NULL, &RNA_Struct, srna, &tptr);
  iterprop = RNA_struct_find_property(&tptr, "functions");

  func = NULL;

  RNA_PROP_BEGIN (&tptr, funcptr, iterprop) {
    if (STREQ(identifier, RNA_function_identifier(funcptr.data))) {
      func = funcptr.data;
      break;
    }
  }
  RNA_PROP_END;

  return func;
#endif
}

const ListBase *RNA_struct_type_functions(StructRNA *srna)
{
  return &srna->functions;
}

StructRegisterFunc RNA_struct_register(StructRNA *type)
{
  return type->reg;
}

StructUnregisterFunc RNA_struct_unregister(StructRNA *type)
{
  do {
    if (type->unreg) {
      return type->unreg;
    }
  } while ((type = type->base));

  return NULL;
}

void **RNA_struct_instance(PointerRNA *ptr)
{
  StructRNA *type = ptr->type;

  do {
    if (type->instance) {
      return type->instance(ptr);
    }
  } while ((type = type->base));

  return NULL;
}

void *RNA_struct_py_type_get(StructRNA *srna)
{
  return srna->py_type;
}

void RNA_struct_py_type_set(StructRNA *srna, void *py_type)
{
  srna->py_type = py_type;
}

void *RNA_struct_blender_type_get(StructRNA *srna)
{
  return srna->blender_type;
}

void RNA_struct_blender_type_set(StructRNA *srna, void *blender_type)
{
  srna->blender_type = blender_type;
}

char *RNA_struct_name_get_alloc(PointerRNA *ptr, char *fixedbuf, int fixedlen, int *r_len)
{
  PropertyRNA *nameprop;

  if (ptr->data && (nameprop = RNA_struct_name_property(ptr->type))) {
    return RNA_property_string_get_alloc(ptr, nameprop, fixedbuf, fixedlen, r_len);
  }

  return NULL;
}

bool RNA_struct_available_or_report(ReportList *reports, const char *identifier)
{
  const StructRNA *srna_exists = RNA_struct_find(identifier);
  if (UNLIKELY(srna_exists != NULL)) {
    /* Use comprehensive string construction since this is such a rare occurrence
     * and information here may cut down time troubleshooting. */
    DynStr *dynstr = BLI_dynstr_new();
    BLI_dynstr_appendf(dynstr, "Type identifier '%s' is already in use: '", identifier);
    BLI_dynstr_append(dynstr, srna_exists->identifier);
    int i = 0;
    if (srna_exists->base) {
      for (const StructRNA *base = srna_exists->base; base; base = base->base) {
        BLI_dynstr_append(dynstr, "(");
        BLI_dynstr_append(dynstr, base->identifier);
        i += 1;
      }
      while (i--) {
        BLI_dynstr_append(dynstr, ")");
      }
    }
    BLI_dynstr_append(dynstr, "'.");
    char *result = BLI_dynstr_get_cstring(dynstr);
    BLI_dynstr_free(dynstr);
    BKE_report(reports, RPT_ERROR, result);
    MEM_freeN(result);
    return false;
  }
  return true;
}

bool RNA_struct_bl_idname_ok_or_report(ReportList *reports,
                                       const char *identifier,
                                       const char *sep)
{
  const int len_sep = strlen(sep);
  const int len_id = strlen(identifier);
  const char *p = strstr(identifier, sep);
  /* TODO: make error, for now warning until add-ons update. */
#if 1
  const int report_level = RPT_WARNING;
  const bool failure = true;
#else
  const int report_level = RPT_ERROR;
  const bool failure = false;
#endif
  if (p == NULL || p == identifier || p + len_sep >= identifier + len_id) {
    BKE_reportf(reports,
                report_level,
                "'%s' does not contain '%s' with prefix and suffix",
                identifier,
                sep);
    return failure;
  }

  const char *c, *start, *end, *last;
  start = identifier;
  end = p;
  last = end - 1;
  for (c = start; c != end; c++) {
    if (((*c >= 'A' && *c <= 'Z') || ((c != start) && (*c >= '0' && *c <= '9')) ||
         ((c != start) && (c != last) && (*c == '_'))) == 0) {
      BKE_reportf(
          reports, report_level, "'%s' doesn't have upper case alpha-numeric prefix", identifier);
      return failure;
    }
  }

  start = p + len_sep;
  end = identifier + len_id;
  last = end - 1;
  for (c = start; c != end; c++) {
    if (((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
         ((c != start) && (c != last) && (*c == '_'))) == 0) {
      BKE_reportf(reports, report_level, "'%s' doesn't have an alpha-numeric suffix", identifier);
      return failure;
    }
  }
  return true;
}

/* Property Information */

const char *RNA_property_identifier(const PropertyRNA *prop)
{
  return rna_ensure_property_identifier(prop);
}

const char *RNA_property_description(PropertyRNA *prop)
{
  return TIP_(rna_ensure_property_description(prop));
}

PropertyType RNA_property_type(PropertyRNA *prop)
{
  return rna_ensure_property(prop)->type;
}

PropertySubType RNA_property_subtype(PropertyRNA *prop)
{
  PropertyRNA *rna_prop = rna_ensure_property(prop);

  /* For custom properties, find and parse the 'subtype' metadata field. */
  if (prop->magic != RNA_MAGIC) {
    IDProperty *idprop = (IDProperty *)prop;

    if (idprop->ui_data) {
      IDPropertyUIData *ui_data = idprop->ui_data;
      return (PropertySubType)ui_data->rna_subtype;
    }
  }

  return rna_prop->subtype;
}

PropertyUnit RNA_property_unit(PropertyRNA *prop)
{
  return RNA_SUBTYPE_UNIT(RNA_property_subtype(prop));
}

PropertyScaleType RNA_property_ui_scale(PropertyRNA *prop)
{
  PropertyRNA *rna_prop = rna_ensure_property(prop);

  switch (rna_prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)rna_prop;
      return iprop->ui_scale_type;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)rna_prop;
      return fprop->ui_scale_type;
    }
    default:
      return PROP_SCALE_LINEAR;
  }
}

int RNA_property_flag(PropertyRNA *prop)
{
  return rna_ensure_property(prop)->flag;
}

int RNA_property_tags(PropertyRNA *prop)
{
  return rna_ensure_property(prop)->tags;
}

bool RNA_property_builtin(PropertyRNA *prop)
{
  return (rna_ensure_property(prop)->flag_internal & PROP_INTERN_BUILTIN) != 0;
}

void *RNA_property_py_data_get(PropertyRNA *prop)
{
  return prop->py_data;
}

int RNA_property_array_length(PointerRNA *ptr, PropertyRNA *prop)
{
  return rna_ensure_property_array_length(ptr, prop);
}

bool RNA_property_array_check(PropertyRNA *prop)
{
  return rna_ensure_property_array_check(prop);
}

int RNA_property_array_dimension(PointerRNA *ptr, PropertyRNA *prop, int length[])
{
  PropertyRNA *rprop = rna_ensure_property(prop);

  if (length) {
    rna_ensure_property_multi_array_length(ptr, prop, length);
  }

  return rprop->arraydimension;
}

int RNA_property_multi_array_length(PointerRNA *ptr, PropertyRNA *prop, int dim)
{
  int len[RNA_MAX_ARRAY_DIMENSION];

  rna_ensure_property_multi_array_length(ptr, prop, len);

  return len[dim];
}

char RNA_property_array_item_char(PropertyRNA *prop, int index)
{
  const char *vectoritem = "XYZW";
  const char *quatitem = "WXYZ";
  const char *coloritem = "RGBA";
  PropertySubType subtype = RNA_property_subtype(prop);

  BLI_assert(index >= 0);

  /* get string to use for array index */
  if ((index < 4) && ELEM(subtype, PROP_QUATERNION, PROP_AXISANGLE)) {
    return quatitem[index];
  }
  if ((index < 4) && ELEM(subtype,
                          PROP_TRANSLATION,
                          PROP_DIRECTION,
                          PROP_XYZ,
                          PROP_XYZ_LENGTH,
                          PROP_EULER,
                          PROP_VELOCITY,
                          PROP_ACCELERATION,
                          PROP_COORDS)) {
    return vectoritem[index];
  }
  if ((index < 4) && ELEM(subtype, PROP_COLOR, PROP_COLOR_GAMMA)) {
    return coloritem[index];
  }

  return '\0';
}

int RNA_property_array_item_index(PropertyRNA *prop, char name)
{
  /* Don't use custom property subtypes in RNA path lookup. */
  PropertySubType subtype = rna_ensure_property(prop)->subtype;

  /* get index based on string name/alias */
  /* maybe a function to find char index in string would be better than all the switches */
  if (ELEM(subtype, PROP_QUATERNION, PROP_AXISANGLE)) {
    switch (name) {
      case 'w':
        return 0;
      case 'x':
        return 1;
      case 'y':
        return 2;
      case 'z':
        return 3;
    }
  }
  else if (ELEM(subtype,
                PROP_TRANSLATION,
                PROP_DIRECTION,
                PROP_XYZ,
                PROP_XYZ_LENGTH,
                PROP_EULER,
                PROP_VELOCITY,
                PROP_ACCELERATION)) {
    switch (name) {
      case 'x':
        return 0;
      case 'y':
        return 1;
      case 'z':
        return 2;
      case 'w':
        return 3;
    }
  }
  else if (ELEM(subtype, PROP_COLOR, PROP_COLOR_GAMMA)) {
    switch (name) {
      case 'r':
        return 0;
      case 'g':
        return 1;
      case 'b':
        return 2;
      case 'a':
        return 3;
    }
  }

  return -1;
}

void RNA_property_int_range(PointerRNA *ptr, PropertyRNA *prop, int *hardmin, int *hardmax)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)rna_ensure_property(prop);
  int softmin, softmax;

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (IDProperty *)prop;
    if (idprop->ui_data) {
      IDPropertyUIDataInt *ui_data = (IDPropertyUIDataInt *)idprop->ui_data;
      *hardmin = ui_data->min;
      *hardmax = ui_data->max;
    }
    else {
      *hardmin = INT_MIN;
      *hardmax = INT_MAX;
    }
    return;
  }

  if (iprop->range) {
    *hardmin = INT_MIN;
    *hardmax = INT_MAX;

    iprop->range(ptr, hardmin, hardmax, &softmin, &softmax);
  }
  else if (iprop->range_ex) {
    *hardmin = INT_MIN;
    *hardmax = INT_MAX;

    iprop->range_ex(ptr, prop, hardmin, hardmax, &softmin, &softmax);
  }
  else {
    *hardmin = iprop->hardmin;
    *hardmax = iprop->hardmax;
  }
}

void RNA_property_int_ui_range(
    PointerRNA *ptr, PropertyRNA *prop, int *softmin, int *softmax, int *step)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)rna_ensure_property(prop);
  int hardmin, hardmax;

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (IDProperty *)prop;
    if (idprop->ui_data) {
      IDPropertyUIDataInt *ui_data_int = (IDPropertyUIDataInt *)idprop->ui_data;
      *softmin = ui_data_int->soft_min;
      *softmax = ui_data_int->soft_max;
      *step = ui_data_int->step;
    }
    else {
      *softmin = INT_MIN;
      *softmax = INT_MAX;
      *step = 1;
    }
    return;
  }

  *softmin = iprop->softmin;
  *softmax = iprop->softmax;

  if (iprop->range) {
    hardmin = INT_MIN;
    hardmax = INT_MAX;

    iprop->range(ptr, &hardmin, &hardmax, softmin, softmax);

    *softmin = max_ii(*softmin, hardmin);
    *softmax = min_ii(*softmax, hardmax);
  }
  else if (iprop->range_ex) {
    hardmin = INT_MIN;
    hardmax = INT_MAX;

    iprop->range_ex(ptr, prop, &hardmin, &hardmax, softmin, softmax);

    *softmin = max_ii(*softmin, hardmin);
    *softmax = min_ii(*softmax, hardmax);
  }

  *step = iprop->step;
}

void RNA_property_float_range(PointerRNA *ptr, PropertyRNA *prop, float *hardmin, float *hardmax)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)rna_ensure_property(prop);
  float softmin, softmax;

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (IDProperty *)prop;
    if (idprop->ui_data) {
      IDPropertyUIDataFloat *ui_data = (IDPropertyUIDataFloat *)idprop->ui_data;
      *hardmin = (float)ui_data->min;
      *hardmax = (float)ui_data->max;
    }
    else {
      *hardmin = -FLT_MAX;
      *hardmax = FLT_MAX;
    }
    return;
  }

  if (fprop->range) {
    *hardmin = -FLT_MAX;
    *hardmax = FLT_MAX;

    fprop->range(ptr, hardmin, hardmax, &softmin, &softmax);
  }
  else if (fprop->range_ex) {
    *hardmin = -FLT_MAX;
    *hardmax = FLT_MAX;

    fprop->range_ex(ptr, prop, hardmin, hardmax, &softmin, &softmax);
  }
  else {
    *hardmin = fprop->hardmin;
    *hardmax = fprop->hardmax;
  }
}

void RNA_property_float_ui_range(PointerRNA *ptr,
                                 PropertyRNA *prop,
                                 float *softmin,
                                 float *softmax,
                                 float *step,
                                 float *precision)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)rna_ensure_property(prop);
  float hardmin, hardmax;

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (IDProperty *)prop;
    if (idprop->ui_data) {
      IDPropertyUIDataFloat *ui_data = (IDPropertyUIDataFloat *)idprop->ui_data;
      *softmin = (float)ui_data->soft_min;
      *softmax = (float)ui_data->soft_max;
      *step = ui_data->step;
      *precision = (float)ui_data->precision;
    }
    else {
      *softmin = -FLT_MAX;
      *softmax = FLT_MAX;
      *step = 1.0f;
      *precision = 3.0f;
    }
    return;
  }

  *softmin = fprop->softmin;
  *softmax = fprop->softmax;

  if (fprop->range) {
    hardmin = -FLT_MAX;
    hardmax = FLT_MAX;

    fprop->range(ptr, &hardmin, &hardmax, softmin, softmax);

    *softmin = max_ff(*softmin, hardmin);
    *softmax = min_ff(*softmax, hardmax);
  }
  else if (fprop->range_ex) {
    hardmin = -FLT_MAX;
    hardmax = FLT_MAX;

    fprop->range_ex(ptr, prop, &hardmin, &hardmax, softmin, softmax);

    *softmin = max_ff(*softmin, hardmin);
    *softmax = min_ff(*softmax, hardmax);
  }

  *step = fprop->step;
  *precision = (float)fprop->precision;
}

int RNA_property_float_clamp(PointerRNA *ptr, PropertyRNA *prop, float *value)
{
  float min, max;

  RNA_property_float_range(ptr, prop, &min, &max);

  if (*value < min) {
    *value = min;
    return -1;
  }
  if (*value > max) {
    *value = max;
    return 1;
  }
  return 0;
}

int RNA_property_int_clamp(PointerRNA *ptr, PropertyRNA *prop, int *value)
{
  int min, max;

  RNA_property_int_range(ptr, prop, &min, &max);

  if (*value < min) {
    *value = min;
    return -1;
  }
  if (*value > max) {
    *value = max;
    return 1;
  }
  return 0;
}

int RNA_property_string_maxlength(PropertyRNA *prop)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)rna_ensure_property(prop);
  return sprop->maxlength;
}

StructRNA *RNA_property_pointer_type(PointerRNA *ptr, PropertyRNA *prop)
{
  prop = rna_ensure_property(prop);

  if (prop->type == PROP_POINTER) {
    PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;

    if (pprop->type_fn) {
      return pprop->type_fn(ptr);
    }
    if (pprop->type) {
      return pprop->type;
    }
  }
  else if (prop->type == PROP_COLLECTION) {
    CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;

    if (cprop->item_type) {
      return cprop->item_type;
    }
  }
  /* ignore other types, RNA_struct_find_nested calls with unchecked props */

  return &RNA_UnknownType;
}

bool RNA_property_pointer_poll(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *value)
{
  prop = rna_ensure_property(prop);

  if (prop->type == PROP_POINTER) {
    PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;

    if (pprop->poll) {
      if (rna_idproperty_check(&prop, ptr)) {
        return ((PropPointerPollFuncPy)pprop->poll)(ptr, *value, prop);
      }
      return pprop->poll(ptr, *value);
    }

    return 1;
  }

  printf("%s: %s is not a pointer property.\n", __func__, prop->identifier);
  return 0;
}

void RNA_property_enum_items_ex(bContext *C,
                                PointerRNA *ptr,
                                PropertyRNA *prop,
                                const bool use_static,
                                const EnumPropertyItem **r_item,
                                int *r_totitem,
                                bool *r_free)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)rna_ensure_property(prop);

  *r_free = false;

  if (!use_static && (eprop->item_fn != NULL)) {
    const bool no_context = (prop->flag & PROP_ENUM_NO_CONTEXT) ||
                            ((ptr->type->flag & STRUCT_NO_CONTEXT_WITHOUT_OWNER_ID) &&
                             (ptr->owner_id == NULL));
    if (C != NULL || no_context) {
      const EnumPropertyItem *item;

      item = eprop->item_fn(no_context ? NULL : C, ptr, prop, r_free);

      /* any callbacks returning NULL should be fixed */
      BLI_assert(item != NULL);

      if (r_totitem) {
        int tot;
        for (tot = 0; item[tot].identifier; tot++) {
          /* pass */
        }
        *r_totitem = tot;
      }

      *r_item = item;
      return;
    }
  }

  *r_item = eprop->item;
  if (r_totitem) {
    *r_totitem = eprop->totitem;
  }
}

void RNA_property_enum_items(bContext *C,
                             PointerRNA *ptr,
                             PropertyRNA *prop,
                             const EnumPropertyItem **r_item,
                             int *r_totitem,
                             bool *r_free)
{
  RNA_property_enum_items_ex(C, ptr, prop, false, r_item, r_totitem, r_free);
}

#ifdef WITH_INTERNATIONAL
static void property_enum_translate(PropertyRNA *prop,
                                    EnumPropertyItem **r_item,
                                    const int *totitem,
                                    bool *r_free)
{
  if (!(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
    int i;

    /* NOTE: Only do those tests once, and then use BLT_pgettext. */
    bool do_iface = BLT_translate_iface();
    bool do_tooltip = BLT_translate_tooltips();
    EnumPropertyItem *nitem;

    if (!(do_iface || do_tooltip)) {
      return;
    }

    if (*r_free) {
      nitem = *r_item;
    }
    else {
      const EnumPropertyItem *item = *r_item;
      int tot;

      if (totitem) {
        tot = *totitem;
      }
      else {
        /* count */
        for (tot = 0; item[tot].identifier; tot++) {
          /* pass */
        }
      }

      nitem = MEM_mallocN(sizeof(EnumPropertyItem) * (tot + 1), "enum_items_gettexted");
      memcpy(nitem, item, sizeof(EnumPropertyItem) * (tot + 1));

      *r_free = true;
    }

    for (i = 0; nitem[i].identifier; i++) {
      if (nitem[i].name && do_iface) {
        nitem[i].name = BLT_pgettext(prop->translation_context, nitem[i].name);
      }
      if (nitem[i].description && do_tooltip) {
        nitem[i].description = BLT_pgettext(NULL, nitem[i].description);
      }
    }

    *r_item = nitem;
  }
}
#endif

void RNA_property_enum_items_gettexted(bContext *C,
                                       PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       const EnumPropertyItem **r_item,
                                       int *r_totitem,
                                       bool *r_free)
{
  RNA_property_enum_items(C, ptr, prop, r_item, r_totitem, r_free);

#ifdef WITH_INTERNATIONAL
  /* Normally dropping 'const' is _not_ ok, in this case it's only modified if we own the memory
   * so allow the exception (callers are creating new arrays in this case). */
  property_enum_translate(prop, (EnumPropertyItem **)r_item, r_totitem, r_free);
#endif
}

void RNA_property_enum_items_gettexted_all(bContext *C,
                                           PointerRNA *ptr,
                                           PropertyRNA *prop,
                                           const EnumPropertyItem **r_item,
                                           int *r_totitem,
                                           bool *r_free)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)rna_ensure_property(prop);
  int mem_size = sizeof(EnumPropertyItem) * (eprop->totitem + 1);
  /* first return all items */
  EnumPropertyItem *item_array = MEM_mallocN(mem_size, "enum_gettext_all");
  *r_free = true;
  memcpy(item_array, eprop->item, mem_size);

  if (r_totitem) {
    *r_totitem = eprop->totitem;
  }

  if (eprop->item_fn != NULL) {
    const bool no_context = (prop->flag & PROP_ENUM_NO_CONTEXT) ||
                            ((ptr->type->flag & STRUCT_NO_CONTEXT_WITHOUT_OWNER_ID) &&
                             (ptr->owner_id == NULL));
    if (C != NULL || no_context) {
      const EnumPropertyItem *item;
      int i;
      bool free = false;

      item = eprop->item_fn(no_context ? NULL : NULL, ptr, prop, &free);

      /* any callbacks returning NULL should be fixed */
      BLI_assert(item != NULL);

      for (i = 0; i < eprop->totitem; i++) {
        bool exists = false;
        int i_fixed;

        /* Items that do not exist on list are returned,
         * but have their names/identifiers NULL'ed out. */
        for (i_fixed = 0; item[i_fixed].identifier; i_fixed++) {
          if (STREQ(item[i_fixed].identifier, item_array[i].identifier)) {
            exists = true;
            break;
          }
        }

        if (!exists) {
          item_array[i].name = NULL;
          item_array[i].identifier = "";
        }
      }

      if (free) {
        MEM_freeN((void *)item);
      }
    }
  }

#ifdef WITH_INTERNATIONAL
  property_enum_translate(prop, &item_array, r_totitem, r_free);
#endif
  *r_item = item_array;
}

bool RNA_property_enum_value(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const char *identifier, int *r_value)
{
  const EnumPropertyItem *item;
  bool free;
  bool found;

  RNA_property_enum_items(C, ptr, prop, &item, NULL, &free);

  if (item) {
    const int i = RNA_enum_from_identifier(item, identifier);
    if (i != -1) {
      *r_value = item[i].value;
      found = true;
    }
    else {
      found = false;
    }

    if (free) {
      MEM_freeN((void *)item);
    }
  }
  else {
    found = false;
  }
  return found;
}

bool RNA_enum_identifier(const EnumPropertyItem *item, const int value, const char **r_identifier)
{
  const int i = RNA_enum_from_value(item, value);
  if (i != -1) {
    *r_identifier = item[i].identifier;
    return true;
  }
  return false;
}

int RNA_enum_bitflag_identifiers(const EnumPropertyItem *item,
                                 const int value,
                                 const char **r_identifier)
{
  int index = 0;
  for (; item->identifier; item++) {
    if (item->identifier[0] && item->value & value) {
      r_identifier[index++] = item->identifier;
    }
  }
  r_identifier[index] = NULL;
  return index;
}

bool RNA_enum_name(const EnumPropertyItem *item, const int value, const char **r_name)
{
  const int i = RNA_enum_from_value(item, value);
  if (i != -1) {
    *r_name = item[i].name;
    return true;
  }
  return false;
}

bool RNA_enum_description(const EnumPropertyItem *item,
                          const int value,
                          const char **r_description)
{
  const int i = RNA_enum_from_value(item, value);
  if (i != -1) {
    *r_description = item[i].description;
    return true;
  }
  return false;
}

int RNA_enum_from_identifier(const EnumPropertyItem *item, const char *identifier)
{
  int i = 0;
  for (; item->identifier; item++, i++) {
    if (item->identifier[0] && STREQ(item->identifier, identifier)) {
      return i;
    }
  }
  return -1;
}

int RNA_enum_from_name(const EnumPropertyItem *item, const char *name)
{
  int i = 0;
  for (; item->identifier; item++, i++) {
    if (item->identifier[0] && STREQ(item->name, name)) {
      return i;
    }
  }
  return -1;
}

int RNA_enum_from_value(const EnumPropertyItem *item, const int value)
{
  int i = 0;
  for (; item->identifier; item++, i++) {
    if (item->identifier[0] && item->value == value) {
      return i;
    }
  }
  return -1;
}

unsigned int RNA_enum_items_count(const EnumPropertyItem *item)
{
  unsigned int i = 0;

  while (item->identifier) {
    item++;
    i++;
  }

  return i;
}

bool RNA_property_enum_identifier(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **identifier)
{
  const EnumPropertyItem *item = NULL;
  bool free;

  RNA_property_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    bool result;
    result = RNA_enum_identifier(item, value, identifier);
    if (free) {
      MEM_freeN((void *)item);
    }
    return result;
  }
  return false;
}

bool RNA_property_enum_name(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **name)
{
  const EnumPropertyItem *item = NULL;
  bool free;

  RNA_property_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    bool result;
    result = RNA_enum_name(item, value, name);
    if (free) {
      MEM_freeN((void *)item);
    }

    return result;
  }
  return false;
}

bool RNA_property_enum_name_gettexted(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **name)
{
  bool result;

  result = RNA_property_enum_name(C, ptr, prop, value, name);

  if (result) {
    if (!(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
      if (BLT_translate_iface()) {
        *name = BLT_pgettext(prop->translation_context, *name);
      }
    }
  }

  return result;
}

bool RNA_property_enum_item_from_value(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, EnumPropertyItem *r_item)
{
  const EnumPropertyItem *item = NULL;
  bool free;

  RNA_property_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    const int i = RNA_enum_from_value(item, value);
    bool result;

    if (i != -1) {
      *r_item = item[i];
      result = true;
    }
    else {
      result = false;
    }

    if (free) {
      MEM_freeN((void *)item);
    }

    return result;
  }
  return false;
}

bool RNA_property_enum_item_from_value_gettexted(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, EnumPropertyItem *r_item)
{
  const bool result = RNA_property_enum_item_from_value(C, ptr, prop, value, r_item);

  if (result && !(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
    if (BLT_translate_iface()) {
      r_item->name = BLT_pgettext(prop->translation_context, r_item->name);
    }
  }

  return result;
}

int RNA_property_enum_bitflag_identifiers(
    bContext *C, PointerRNA *ptr, PropertyRNA *prop, const int value, const char **identifier)
{
  const EnumPropertyItem *item = NULL;
  bool free;

  RNA_property_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    int result;
    result = RNA_enum_bitflag_identifiers(item, value, identifier);
    if (free) {
      MEM_freeN((void *)item);
    }

    return result;
  }
  return 0;
}

const char *RNA_property_ui_name(const PropertyRNA *prop)
{
  return CTX_IFACE_(prop->translation_context, rna_ensure_property_name(prop));
}

const char *RNA_property_ui_name_raw(const PropertyRNA *prop)
{
  return rna_ensure_property_name(prop);
}

const char *RNA_property_ui_description(const PropertyRNA *prop)
{
  return TIP_(rna_ensure_property_description(prop));
}

const char *RNA_property_ui_description_raw(const PropertyRNA *prop)
{
  return rna_ensure_property_description(prop);
}

const char *RNA_property_translation_context(const PropertyRNA *prop)
{
  return rna_ensure_property((PropertyRNA *)prop)->translation_context;
}

int RNA_property_ui_icon(const PropertyRNA *prop)
{
  return rna_ensure_property((PropertyRNA *)prop)->icon;
}

static bool rna_property_editable_do(PointerRNA *ptr,
                                     PropertyRNA *prop_orig,
                                     const int index,
                                     const char **r_info)
{
  ID *id = ptr->owner_id;

  PropertyRNA *prop = rna_ensure_property(prop_orig);

  const char *info = "";
  const int flag = (prop->itemeditable != NULL && index >= 0) ?
                       prop->itemeditable(ptr, index) :
                       (prop->editable != NULL ? prop->editable(ptr, &info) : prop->flag);
  if (r_info != NULL) {
    *r_info = info;
  }

  /* Early return if the property itself is not editable. */
  if ((flag & PROP_EDITABLE) == 0 || (flag & PROP_REGISTER) != 0) {
    if (r_info != NULL && (*r_info)[0] == '\0') {
      *r_info = N_("This property is for internal use only and can't be edited");
    }
    return false;
  }

  /* If there is no owning ID, the property is editable at this point. */
  if (id == NULL) {
    return true;
  }

  /* Handle linked or liboverride ID cases. */
  const bool is_linked_prop_exception = (prop->flag & PROP_LIB_EXCEPTION) != 0;
  if (ID_IS_LINKED(id) && !is_linked_prop_exception) {
    if (r_info != NULL && (*r_info)[0] == '\0') {
      *r_info = N_("Can't edit this property from a linked data-block");
    }
    return false;
  }
  if (ID_IS_OVERRIDE_LIBRARY(id) && !RNA_property_overridable_get(ptr, prop_orig)) {
    if (r_info != NULL && (*r_info)[0] == '\0') {
      *r_info = N_("Can't edit this property from an override data-block");
    }
    return false;
  }

  /* At this point, property is owned by a local ID and therefore fully editable. */
  return true;
}

bool RNA_property_editable(PointerRNA *ptr, PropertyRNA *prop)
{
  return rna_property_editable_do(ptr, prop, -1, NULL);
}

bool RNA_property_editable_info(PointerRNA *ptr, PropertyRNA *prop, const char **r_info)
{
  return rna_property_editable_do(ptr, prop, -1, r_info);
}

bool RNA_property_editable_flag(PointerRNA *ptr, PropertyRNA *prop)
{
  int flag;
  const char *dummy_info;

  prop = rna_ensure_property(prop);
  flag = prop->editable ? prop->editable(ptr, &dummy_info) : prop->flag;
  return (flag & PROP_EDITABLE) != 0;
}

bool RNA_property_editable_index(PointerRNA *ptr, PropertyRNA *prop, const int index)
{
  BLI_assert(index >= 0);

  return rna_property_editable_do(ptr, prop, index, NULL);
}

bool RNA_property_animateable(PointerRNA *ptr, PropertyRNA *prop)
{
  /* check that base ID-block can support animation data */
  if (!id_can_have_animdata(ptr->owner_id)) {
    return false;
  }

  prop = rna_ensure_property(prop);

  if (!(prop->flag & PROP_ANIMATABLE)) {
    return false;
  }

  return (prop->flag & PROP_EDITABLE) != 0;
}

bool RNA_property_animated(PointerRNA *ptr, PropertyRNA *prop)
{
  int len = 1, index;
  bool driven, special;

  if (!prop) {
    return false;
  }

  if (RNA_property_array_check(prop)) {
    len = RNA_property_array_length(ptr, prop);
  }

  for (index = 0; index < len; index++) {
    if (BKE_fcurve_find_by_rna(ptr, prop, index, NULL, NULL, &driven, &special)) {
      return true;
    }
  }

  return false;
}
bool RNA_property_path_from_ID_check(PointerRNA *ptr, PropertyRNA *prop)
{
  char *path = RNA_path_from_ID_to_property(ptr, prop);
  bool ret = false;

  if (path) {
    PointerRNA id_ptr;
    PointerRNA r_ptr;
    PropertyRNA *r_prop;

    RNA_id_pointer_create(ptr->owner_id, &id_ptr);
    if (RNA_path_resolve(&id_ptr, path, &r_ptr, &r_prop) == true) {
      ret = (prop == r_prop);
    }
    MEM_freeN(path);
  }

  return ret;
}

static void rna_property_update(
    bContext *C, Main *bmain, Scene *scene, PointerRNA *ptr, PropertyRNA *prop)
{
  const bool is_rna = (prop->magic == RNA_MAGIC);
  prop = rna_ensure_property(prop);

  if (is_rna) {
    if (prop->update) {
      /* ideally no context would be needed for update, but there's some
       * parts of the code that need it still, so we have this exception */
      if (prop->flag & PROP_CONTEXT_UPDATE) {
        if (C) {
          if ((prop->flag & PROP_CONTEXT_PROPERTY_UPDATE) == PROP_CONTEXT_PROPERTY_UPDATE) {
            ((ContextPropUpdateFunc)prop->update)(C, ptr, prop);
          }
          else {
            ((ContextUpdateFunc)prop->update)(C, ptr);
          }
        }
      }
      else {
        prop->update(bmain, scene, ptr);
      }
    }

#if 1
    /* TODO(campbell): Should eventually be replaced entirely by message bus (below)
     * for now keep since COW, bugs are hard to track when we have other missing updates. */
    if (prop->noteflag) {
      WM_main_add_notifier(prop->noteflag, ptr->owner_id);
    }
#endif

    /* if C is NULL, we're updating from animation.
     * avoid slow-down from f-curves by not publishing (for now). */
    if (C != NULL) {
      struct wmMsgBus *mbus = CTX_wm_message_bus(C);
      /* we could add NULL check, for now don't */
      WM_msg_publish_rna(mbus, ptr, prop);
    }
    if (ptr->owner_id != NULL && ((prop->flag & PROP_NO_DEG_UPDATE) == 0)) {
      const short id_type = GS(ptr->owner_id->name);
      if (ID_TYPE_IS_COW(id_type)) {
        DEG_id_tag_update(ptr->owner_id, ID_RECALC_COPY_ON_WRITE);
      }
    }
    /* End message bus. */
  }

  if (!is_rna || (prop->flag & PROP_IDPROPERTY)) {

    /* Disclaimer: this logic is not applied consistently, causing some confusing behavior.
     *
     * - When animated (which skips update functions).
     * - When ID-properties are edited via Python (since RNA properties aren't used in this case).
     *
     * Adding updates will add a lot of overhead in the case of animation.
     * For Python it may cause unexpected slow-downs for developers using ID-properties
     * for data storage. Further, the root ID isn't available with nested data-structures.
     *
     * So editing custom properties only causes updates in the UI,
     * keep this exception because it happens to be useful for driving settings.
     * Python developers on the other hand will need to manually 'update_tag', see: T74000. */
    DEG_id_tag_update(ptr->owner_id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_PARAMETERS);

    /* When updating an ID pointer property, tag depsgraph for update. */
    if (prop->type == PROP_POINTER && RNA_struct_is_ID(RNA_property_pointer_type(ptr, prop))) {
      DEG_relations_tag_update(bmain);
    }

    WM_main_add_notifier(NC_WINDOW, NULL);
    /* Not nice as well, but the only way to make sure material preview
     * is updated with custom nodes.
     */
    if ((prop->flag & PROP_IDPROPERTY) != 0 && (ptr->owner_id != NULL) &&
        (GS(ptr->owner_id->name) == ID_NT)) {
      WM_main_add_notifier(NC_MATERIAL | ND_SHADING, NULL);
    }
  }
}

bool RNA_property_update_check(PropertyRNA *prop)
{
  /* NOTE: must keep in sync with #rna_property_update. */
  return (prop->magic != RNA_MAGIC || prop->update || prop->noteflag);
}

void RNA_property_update(bContext *C, PointerRNA *ptr, PropertyRNA *prop)
{
  rna_property_update(C, CTX_data_main(C), CTX_data_scene(C), ptr, prop);
}

void RNA_property_update_main(Main *bmain, Scene *scene, PointerRNA *ptr, PropertyRNA *prop)
{
  rna_property_update(NULL, bmain, scene, ptr, prop);
}

/* ---------------------------------------------------------------------- */

/* Property Data */

bool RNA_property_boolean_get(PointerRNA *ptr, PropertyRNA *prop)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
  IDProperty *idprop;
  bool value;

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) == false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    value = IDP_Int(idprop) != 0;
  }
  else if (bprop->get) {
    value = bprop->get(ptr);
  }
  else if (bprop->get_ex) {
    value = bprop->get_ex(ptr, prop);
  }
  else {
    value = bprop->defaultvalue;
  }

  BLI_assert(ELEM(value, false, true));

  return value;
}

void RNA_property_boolean_set(PointerRNA *ptr, PropertyRNA *prop, bool value)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) == false);
  BLI_assert(ELEM(value, false, true));

  /* just in case other values are passed */
  BLI_assert(ELEM(value, true, false));

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDP_Int(idprop) = (int)value;
    rna_idproperty_touch(idprop);
  }
  else if (bprop->set) {
    bprop->set(ptr, value);
  }
  else if (bprop->set_ex) {
    bprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    val.i = value;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_INT, &val, prop->identifier));
    }
  }
}

static void rna_property_boolean_fill_default_array_values(
    const bool *defarr, int defarr_length, bool defvalue, int out_length, bool *r_values)
{
  if (defarr && defarr_length > 0) {
    defarr_length = MIN2(defarr_length, out_length);
    memcpy(r_values, defarr, sizeof(bool) * defarr_length);
  }
  else {
    defarr_length = 0;
  }

  for (int i = defarr_length; i < out_length; i++) {
    r_values[i] = defvalue;
  }
}

static void rna_property_boolean_get_default_array_values(PointerRNA *ptr,
                                                          BoolPropertyRNA *bprop,
                                                          bool *r_values)
{
  int length = bprop->property.totarraylength;
  int out_length = RNA_property_array_length(ptr, (PropertyRNA *)bprop);

  rna_property_boolean_fill_default_array_values(
      bprop->defaultarray, length, bprop->defaultvalue, out_length, r_values);
}

void RNA_property_boolean_get_array(PointerRNA *ptr, PropertyRNA *prop, bool *values)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    if (prop->arraydimension == 0) {
      values[0] = RNA_property_boolean_get(ptr, prop);
    }
    else {
      int *values_src = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values[i] = (bool)values_src[i];
      }
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = RNA_property_boolean_get(ptr, prop);
  }
  else if (bprop->getarray) {
    bprop->getarray(ptr, values);
  }
  else if (bprop->getarray_ex) {
    bprop->getarray_ex(ptr, prop, values);
  }
  else {
    rna_property_boolean_get_default_array_values(ptr, bprop, values);
  }
}

bool RNA_property_boolean_get_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  bool tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);
  bool value;

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_boolean_get_array(ptr, prop, tmp);
    value = tmp[index];
  }
  else {
    bool *tmparray;

    tmparray = MEM_mallocN(sizeof(bool) * len, __func__);
    RNA_property_boolean_get_array(ptr, prop, tmparray);
    value = tmparray[index];
    MEM_freeN(tmparray);
  }

  BLI_assert(ELEM(value, false, true));

  return value;
}

void RNA_property_boolean_set_array(PointerRNA *ptr, PropertyRNA *prop, const bool *values)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    if (prop->arraydimension == 0) {
      IDP_Int(idprop) = values[0];
    }
    else {
      int *values_dst = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values_dst[i] = (int)values[i];
      }
    }
    rna_idproperty_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    RNA_property_boolean_set(ptr, prop, values[0]);
  }
  else if (bprop->setarray) {
    bprop->setarray(ptr, values);
  }
  else if (bprop->setarray_ex) {
    bprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    val.array.len = prop->totarraylength;
    val.array.type = IDP_INT;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->identifier);
      IDP_AddToGroup(group, idprop);
      int *values_dst = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values_dst[i] = (int)values[i];
      }
    }
  }
}

void RNA_property_boolean_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, bool value)
{
  bool tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);
  BLI_assert(ELEM(value, false, true));

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_boolean_get_array(ptr, prop, tmp);
    tmp[index] = value;
    RNA_property_boolean_set_array(ptr, prop, tmp);
  }
  else {
    bool *tmparray;

    tmparray = MEM_mallocN(sizeof(bool) * len, __func__);
    RNA_property_boolean_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    RNA_property_boolean_set_array(ptr, prop, tmparray);
    MEM_freeN(tmparray);
  }
}

bool RNA_property_boolean_get_default(PointerRNA *UNUSED(ptr), PropertyRNA *prop)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) == false);
  BLI_assert(ELEM(bprop->defaultvalue, false, true));

  return bprop->defaultvalue;
}

void RNA_property_boolean_get_default_array(PointerRNA *ptr, PropertyRNA *prop, bool *values)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);

  if (prop->arraydimension == 0) {
    values[0] = bprop->defaultvalue;
  }
  else {
    rna_property_boolean_get_default_array_values(ptr, bprop, values);
  }
}

bool RNA_property_boolean_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  bool tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_BOOLEAN);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_boolean_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  bool *tmparray, value;

  tmparray = MEM_mallocN(sizeof(bool) * len, __func__);
  RNA_property_boolean_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  MEM_freeN(tmparray);

  return value;
}

int RNA_property_int_get(PointerRNA *ptr, PropertyRNA *prop)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) == false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    return IDP_Int(idprop);
  }
  if (iprop->get) {
    return iprop->get(ptr);
  }
  if (iprop->get_ex) {
    return iprop->get_ex(ptr, prop);
  }
  return iprop->defaultvalue;
}

void RNA_property_int_set(PointerRNA *ptr, PropertyRNA *prop, int value)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) == false);
  /* useful to check on bad values but set function should clamp */
  // BLI_assert(RNA_property_int_clamp(ptr, prop, &value) == 0);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    RNA_property_int_clamp(ptr, prop, &value);
    IDP_Int(idprop) = value;
    rna_idproperty_touch(idprop);
  }
  else if (iprop->set) {
    iprop->set(ptr, value);
  }
  else if (iprop->set_ex) {
    iprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    RNA_property_int_clamp(ptr, prop, &value);

    val.i = value;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_INT, &val, prop->identifier));
    }
  }
}

static void rna_property_int_fill_default_array_values(
    const int *defarr, int defarr_length, int defvalue, int out_length, int *r_values)
{
  if (defarr && defarr_length > 0) {
    defarr_length = MIN2(defarr_length, out_length);
    memcpy(r_values, defarr, sizeof(int) * defarr_length);
  }
  else {
    defarr_length = 0;
  }

  for (int i = defarr_length; i < out_length; i++) {
    r_values[i] = defvalue;
  }
}

static void rna_property_int_get_default_array_values(PointerRNA *ptr,
                                                      IntPropertyRNA *iprop,
                                                      int *r_values)
{
  int length = iprop->property.totarraylength;
  int out_length = RNA_property_array_length(ptr, (PropertyRNA *)iprop);

  rna_property_int_fill_default_array_values(
      iprop->defaultarray, length, iprop->defaultvalue, out_length, r_values);
}

void RNA_property_int_get_array(PointerRNA *ptr, PropertyRNA *prop, int *values)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    BLI_assert(idprop->len == RNA_property_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROPERTY));
    if (prop->arraydimension == 0) {
      values[0] = RNA_property_int_get(ptr, prop);
    }
    else {
      memcpy(values, IDP_Array(idprop), sizeof(int) * idprop->len);
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = RNA_property_int_get(ptr, prop);
  }
  else if (iprop->getarray) {
    iprop->getarray(ptr, values);
  }
  else if (iprop->getarray_ex) {
    iprop->getarray_ex(ptr, prop, values);
  }
  else {
    rna_property_int_get_default_array_values(ptr, iprop, values);
  }
}

void RNA_property_int_get_array_range(PointerRNA *ptr, PropertyRNA *prop, int values[2])
{
  const int array_len = RNA_property_array_length(ptr, prop);

  if (array_len <= 0) {
    values[0] = 0;
    values[1] = 0;
  }
  else if (array_len == 1) {
    RNA_property_int_get_array(ptr, prop, values);
    values[1] = values[0];
  }
  else {
    int arr_stack[32];
    int *arr;
    int i;

    if (array_len > 32) {
      arr = MEM_mallocN(sizeof(int) * array_len, __func__);
    }
    else {
      arr = arr_stack;
    }

    RNA_property_int_get_array(ptr, prop, arr);
    values[0] = values[1] = arr[0];
    for (i = 1; i < array_len; i++) {
      values[0] = MIN2(values[0], arr[i]);
      values[1] = MAX2(values[1], arr[i]);
    }

    if (arr != arr_stack) {
      MEM_freeN(arr);
    }
  }
}

int RNA_property_int_get_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  int tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_int_get_array(ptr, prop, tmp);
    return tmp[index];
  }
  int *tmparray, value;

  tmparray = MEM_mallocN(sizeof(int) * len, __func__);
  RNA_property_int_get_array(ptr, prop, tmparray);
  value = tmparray[index];
  MEM_freeN(tmparray);

  return value;
}

void RNA_property_int_set_array(PointerRNA *ptr, PropertyRNA *prop, const int *values)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    BLI_assert(idprop->len == RNA_property_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROPERTY));
    if (prop->arraydimension == 0) {
      IDP_Int(idprop) = values[0];
    }
    else {
      memcpy(IDP_Array(idprop), values, sizeof(int) * idprop->len);
    }

    rna_idproperty_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    RNA_property_int_set(ptr, prop, values[0]);
  }
  else if (iprop->setarray) {
    iprop->setarray(ptr, values);
  }
  else if (iprop->setarray_ex) {
    iprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    /* TODO: RNA_property_int_clamp_array(ptr, prop, &value); */

    val.array.len = prop->totarraylength;
    val.array.type = IDP_INT;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->identifier);
      IDP_AddToGroup(group, idprop);
      memcpy(IDP_Array(idprop), values, sizeof(int) * idprop->len);
    }
  }
}

void RNA_property_int_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, int value)
{
  int tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_int_get_array(ptr, prop, tmp);
    tmp[index] = value;
    RNA_property_int_set_array(ptr, prop, tmp);
  }
  else {
    int *tmparray;

    tmparray = MEM_mallocN(sizeof(int) * len, __func__);
    RNA_property_int_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    RNA_property_int_set_array(ptr, prop, tmparray);
    MEM_freeN(tmparray);
  }
}

int RNA_property_int_get_default(PointerRNA *UNUSED(ptr), PropertyRNA *prop)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)rna_ensure_property(prop);

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      const IDPropertyUIDataInt *ui_data = (const IDPropertyUIDataInt *)idprop->ui_data;
      return ui_data->default_value;
    }
  }

  return iprop->defaultvalue;
}

bool RNA_property_int_set_default(PropertyRNA *prop, int value)
{
  if (prop->magic == RNA_MAGIC) {
    return false;
  }

  IDProperty *idprop = (IDProperty *)prop;
  BLI_assert(idprop->type == IDP_INT);

  IDPropertyUIDataInt *ui_data = (IDPropertyUIDataInt *)IDP_ui_data_ensure(idprop);
  ui_data->default_value = value;
  return true;
}

void RNA_property_int_get_default_array(PointerRNA *ptr, PropertyRNA *prop, int *values)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if (prop->magic != RNA_MAGIC) {
    int length = rna_ensure_property_array_length(ptr, prop);

    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      BLI_assert(idprop->type == IDP_ARRAY);
      BLI_assert(idprop->subtype == IDP_INT);
      const IDPropertyUIDataInt *ui_data = (const IDPropertyUIDataInt *)idprop->ui_data;
      if (ui_data->default_array) {
        rna_property_int_fill_default_array_values(ui_data->default_array,
                                                   ui_data->default_array_len,
                                                   ui_data->default_value,
                                                   length,
                                                   values);
      }
      else {
        rna_property_int_fill_default_array_values(
            NULL, 0, ui_data->default_value, length, values);
      }
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = iprop->defaultvalue;
  }
  else {
    rna_property_int_get_default_array_values(ptr, iprop, values);
  }
}

int RNA_property_int_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  int tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_INT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_int_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  int *tmparray, value;

  tmparray = MEM_mallocN(sizeof(int) * len, __func__);
  RNA_property_int_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  MEM_freeN(tmparray);

  return value;
}

float RNA_property_float_get(PointerRNA *ptr, PropertyRNA *prop)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) == false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    if (idprop->type == IDP_FLOAT) {
      return IDP_Float(idprop);
    }
    return (float)IDP_Double(idprop);
  }
  if (fprop->get) {
    return fprop->get(ptr);
  }
  if (fprop->get_ex) {
    return fprop->get_ex(ptr, prop);
  }
  return fprop->defaultvalue;
}

void RNA_property_float_set(PointerRNA *ptr, PropertyRNA *prop, float value)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) == false);
  /* useful to check on bad values but set function should clamp */
  // BLI_assert(RNA_property_float_clamp(ptr, prop, &value) == 0);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    RNA_property_float_clamp(ptr, prop, &value);
    if (idprop->type == IDP_FLOAT) {
      IDP_Float(idprop) = value;
    }
    else {
      IDP_Double(idprop) = value;
    }

    rna_idproperty_touch(idprop);
  }
  else if (fprop->set) {
    fprop->set(ptr, value);
  }
  else if (fprop->set_ex) {
    fprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    RNA_property_float_clamp(ptr, prop, &value);

    val.f = value;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_FLOAT, &val, prop->identifier));
    }
  }
}

static void rna_property_float_fill_default_array_values(
    const float *defarr, int defarr_length, float defvalue, int out_length, float *r_values)
{
  if (defarr && defarr_length > 0) {
    defarr_length = MIN2(defarr_length, out_length);
    memcpy(r_values, defarr, sizeof(float) * defarr_length);
  }
  else {
    defarr_length = 0;
  }

  for (int i = defarr_length; i < out_length; i++) {
    r_values[i] = defvalue;
  }
}

/**
 * The same logic as #rna_property_float_fill_default_array_values for a double array.
 */
static void rna_property_float_fill_default_array_values_double(const double *default_array,
                                                                const int default_array_len,
                                                                const double default_value,
                                                                const int out_length,
                                                                float *r_values)
{
  const int array_copy_len = MIN2(out_length, default_array_len);

  for (int i = 0; i < array_copy_len; i++) {
    r_values[i] = (float)default_array[i];
  }

  for (int i = array_copy_len; i < out_length; i++) {
    r_values[i] = (float)default_value;
  }
}

static void rna_property_float_get_default_array_values(PointerRNA *ptr,
                                                        FloatPropertyRNA *fprop,
                                                        float *r_values)
{
  int length = fprop->property.totarraylength;
  int out_length = RNA_property_array_length(ptr, (PropertyRNA *)fprop);

  rna_property_float_fill_default_array_values(
      fprop->defaultarray, length, fprop->defaultvalue, out_length, r_values);
}

void RNA_property_float_get_array(PointerRNA *ptr, PropertyRNA *prop, float *values)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
  IDProperty *idprop;
  int i;

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    BLI_assert(idprop->len == RNA_property_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROPERTY));
    if (prop->arraydimension == 0) {
      values[0] = RNA_property_float_get(ptr, prop);
    }
    else if (idprop->subtype == IDP_FLOAT) {
      memcpy(values, IDP_Array(idprop), sizeof(float) * idprop->len);
    }
    else {
      for (i = 0; i < idprop->len; i++) {
        values[i] = (float)(((double *)IDP_Array(idprop))[i]);
      }
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = RNA_property_float_get(ptr, prop);
  }
  else if (fprop->getarray) {
    fprop->getarray(ptr, values);
  }
  else if (fprop->getarray_ex) {
    fprop->getarray_ex(ptr, prop, values);
  }
  else {
    rna_property_float_get_default_array_values(ptr, fprop, values);
  }
}

void RNA_property_float_get_array_range(PointerRNA *ptr, PropertyRNA *prop, float values[2])
{
  const int array_len = RNA_property_array_length(ptr, prop);

  if (array_len <= 0) {
    values[0] = 0.0f;
    values[1] = 0.0f;
  }
  else if (array_len == 1) {
    RNA_property_float_get_array(ptr, prop, values);
    values[1] = values[0];
  }
  else {
    float arr_stack[32];
    float *arr;
    int i;

    if (array_len > 32) {
      arr = MEM_mallocN(sizeof(float) * array_len, __func__);
    }
    else {
      arr = arr_stack;
    }

    RNA_property_float_get_array(ptr, prop, arr);
    values[0] = values[1] = arr[0];
    for (i = 1; i < array_len; i++) {
      values[0] = MIN2(values[0], arr[i]);
      values[1] = MAX2(values[1], arr[i]);
    }

    if (arr != arr_stack) {
      MEM_freeN(arr);
    }
  }
}

float RNA_property_float_get_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  float tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_float_get_array(ptr, prop, tmp);
    return tmp[index];
  }
  float *tmparray, value;

  tmparray = MEM_mallocN(sizeof(float) * len, __func__);
  RNA_property_float_get_array(ptr, prop, tmparray);
  value = tmparray[index];
  MEM_freeN(tmparray);

  return value;
}

void RNA_property_float_set_array(PointerRNA *ptr, PropertyRNA *prop, const float *values)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
  IDProperty *idprop;
  int i;

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    BLI_assert(idprop->len == RNA_property_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROPERTY));
    if (prop->arraydimension == 0) {
      if (idprop->type == IDP_FLOAT) {
        IDP_Float(idprop) = values[0];
      }
      else {
        IDP_Double(idprop) = values[0];
      }
    }
    else if (idprop->subtype == IDP_FLOAT) {
      memcpy(IDP_Array(idprop), values, sizeof(float) * idprop->len);
    }
    else {
      for (i = 0; i < idprop->len; i++) {
        ((double *)IDP_Array(idprop))[i] = values[i];
      }
    }

    rna_idproperty_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    RNA_property_float_set(ptr, prop, values[0]);
  }
  else if (fprop->setarray) {
    fprop->setarray(ptr, values);
  }
  else if (fprop->setarray_ex) {
    fprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    /* TODO: RNA_property_float_clamp_array(ptr, prop, &value); */

    val.array.len = prop->totarraylength;
    val.array.type = IDP_FLOAT;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->identifier);
      IDP_AddToGroup(group, idprop);
      memcpy(IDP_Array(idprop), values, sizeof(float) * idprop->len);
    }
  }
}

void RNA_property_float_set_index(PointerRNA *ptr, PropertyRNA *prop, int index, float value)
{
  float tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_float_get_array(ptr, prop, tmp);
    tmp[index] = value;
    RNA_property_float_set_array(ptr, prop, tmp);
  }
  else {
    float *tmparray;

    tmparray = MEM_mallocN(sizeof(float) * len, __func__);
    RNA_property_float_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    RNA_property_float_set_array(ptr, prop, tmparray);
    MEM_freeN(tmparray);
  }
}

float RNA_property_float_get_default(PointerRNA *UNUSED(ptr), PropertyRNA *prop)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) == false);

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      BLI_assert(ELEM(idprop->type, IDP_FLOAT, IDP_DOUBLE));
      const IDPropertyUIDataFloat *ui_data = (const IDPropertyUIDataFloat *)idprop->ui_data;
      return (float)ui_data->default_value;
    }
  }

  return fprop->defaultvalue;
}

bool RNA_property_float_set_default(PropertyRNA *prop, float value)
{
  if (prop->magic == RNA_MAGIC) {
    return false;
  }

  IDProperty *idprop = (IDProperty *)prop;
  BLI_assert(idprop->type == IDP_FLOAT);

  IDPropertyUIDataFloat *ui_data = (IDPropertyUIDataFloat *)IDP_ui_data_ensure(idprop);
  ui_data->default_value = (double)value;
  return true;
}

void RNA_property_float_get_default_array(PointerRNA *ptr, PropertyRNA *prop, float *values)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);

  if (prop->magic != RNA_MAGIC) {
    int length = rna_ensure_property_array_length(ptr, prop);

    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      BLI_assert(idprop->type == IDP_ARRAY);
      BLI_assert(ELEM(idprop->subtype, IDP_FLOAT, IDP_DOUBLE));
      const IDPropertyUIDataFloat *ui_data = (const IDPropertyUIDataFloat *)idprop->ui_data;
      rna_property_float_fill_default_array_values_double(ui_data->default_array,
                                                          ui_data->default_array_len,
                                                          ui_data->default_value,
                                                          length,
                                                          values);
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = fprop->defaultvalue;
  }
  else {
    rna_property_float_get_default_array_values(ptr, fprop, values);
  }
}

float RNA_property_float_get_default_index(PointerRNA *ptr, PropertyRNA *prop, int index)
{
  float tmp[RNA_MAX_ARRAY_LENGTH];
  int len = rna_ensure_property_array_length(ptr, prop);

  BLI_assert(RNA_property_type(prop) == PROP_FLOAT);
  BLI_assert(RNA_property_array_check(prop) != false);
  BLI_assert(index >= 0);
  BLI_assert(index < len);

  if (len <= RNA_MAX_ARRAY_LENGTH) {
    RNA_property_float_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  float *tmparray, value;

  tmparray = MEM_mallocN(sizeof(float) * len, __func__);
  RNA_property_float_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  MEM_freeN(tmparray);

  return value;
}

void RNA_property_string_get(PointerRNA *ptr, PropertyRNA *prop, char *value)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    /* editing bytes is not 100% supported
     * since they can contain NIL chars */
    if (idprop->subtype == IDP_STRING_SUB_BYTE) {
      memcpy(value, IDP_String(idprop), idprop->len);
      value[idprop->len] = '\0';
    }
    else {
      memcpy(value, IDP_String(idprop), idprop->len);
    }
  }
  else if (sprop->get) {
    sprop->get(ptr, value);
  }
  else if (sprop->get_ex) {
    sprop->get_ex(ptr, prop, value);
  }
  else {
    strcpy(value, sprop->defaultvalue);
  }
}

char *RNA_property_string_get_alloc(
    PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen, int *r_len)
{
  char *buf;
  int length;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  length = RNA_property_string_length(ptr, prop);

  if (length + 1 < fixedlen) {
    buf = fixedbuf;
  }
  else {
    buf = MEM_mallocN(sizeof(char) * (length + 1), __func__);
  }

#ifndef NDEBUG
  /* safety check to ensure the string is actually set */
  buf[length] = 255;
#endif

  RNA_property_string_get(ptr, prop, buf);

#ifndef NDEBUG
  BLI_assert(buf[length] == '\0');
#endif

  if (r_len) {
    *r_len = length;
  }

  return buf;
}

int RNA_property_string_length(PointerRNA *ptr, PropertyRNA *prop)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    if (idprop->subtype == IDP_STRING_SUB_BYTE) {
      return idprop->len;
    }
#ifndef NDEBUG
    /* these _must_ stay in sync */
    BLI_assert(strlen(IDP_String(idprop)) == idprop->len - 1);
#endif
    return idprop->len - 1;
  }
  if (sprop->length) {
    return sprop->length(ptr);
  }
  if (sprop->length_ex) {
    return sprop->length_ex(ptr, prop);
  }
  return strlen(sprop->defaultvalue);
}
