#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_constraint_types.h"
#include "DNA_modifier_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_alloca.h"
#include "BLI_blenlib.h"
#include "BLI_dynstr.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BLF_api.h"
#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_collection.h"
#include "BKE_context.h"
#include "BKE_fcurve.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_api.h"
#include "WM_message.h"

/* flush updates */
#include "DNA_object_types.h"
#include "WM_types.h"

#include "rna_access_internal.h"
#include "rna_internal.h"

const PointerRNA PointerRNA_NULL = {NULL};

/* Init/Exit */

void RNA_init(void)
{
  StructRNA *srna;
  PropertyRNA *prop;

  BLENDER_RNA.structs_map = BLI_ghash_str_new_ex(__func__, 2048);
  BLENDER_RNA.structs_len = 0;

  for (srna = BLENDER_RNA.structs.first; srna; srna = srna->cont.next) {
    if (!srna->cont.prophash) {
      srna->cont.prophash = BLI_ghash_str_new("RNA_init gh");

      for (prop = srna->cont.properties.first; prop; prop = prop->next) {
        if (!(prop->flag_internal & PROP_INTERN_BUILTIN)) {
          BLI_ghash_insert(srna->cont.prophash, (void *)prop->identifier, prop);
        }
      }
    }
    BLI_assert(srna->flag & STRUCT_PUBLIC_NAMESPACE);
    BLI_ghash_insert(BLENDER_RNA.structs_map, (void *)srna->identifier, srna);
    BLENDER_RNA.structs_len += 1;
  }
}

void RNA_exit(void)
{
  StructRNA *srna;

  for (srna = BLENDER_RNA.structs.first; srna; srna = srna->cont.next) {
    if (srna->cont.prophash) {
      BLI_ghash_free(srna->cont.prophash, NULL, NULL);
      srna->cont.prophash = NULL;
    }
  }

  RNA_free(&BLENDER_RNA);
}

/* Pointer */

void RNA_main_pointer_create(struct Main *main, PointerRNA *r_ptr)
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

void RNA_pointer_create(ID *id, StructRNA *type, void *data, PointerRNA *r_ptr)
{
#if 0 /* UNUSED */
  StructRNA *idtype = NULL;

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

void RNA_blender_rna_pointer_create(PointerRNA *r_ptr)
{
  r_ptr->owner_id = NULL;
  r_ptr->type = &RNA_BlenderRNA;
  r_ptr->data = &BLENDER_RNA;
}

PointerRNA rna_pointer_inherit_refine(PointerRNA *ptr, StructRNA *type, void *data)
{
  if (data) {
    PointerRNA result;
    result.data = data;
    result.type = type;
    rna_pointer_inherit_id(type, ptr, &result);

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
