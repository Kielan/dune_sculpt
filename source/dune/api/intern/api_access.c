#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_id.h"
#include "types_constraint.h"
#include "types_mod.h"
#include "types_scene.h"
#include "types_wm.h"

#include "lib_alloca.h"
#include "lib_dunelib.h"
#include "lib_dynstr.h"
#include "lib_ghash.h"
#include "lib_math.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "font_api.h"
#include "lang.h"

#include "dune_anim_data.h"
#include "dune_collection.h"
#include "dune_cxt.h"
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

#include "api_access_internal.h"
#include "api_internal.h"

const ApiPtr ApiPtr_NULL = {NULL};

/* Init/Exit */
void api_init(void)
{
  ApiStruct *sapi;
  ApiProp *prop;

  DUNE_API.structs_map = lib_ghash_str_new_ex(__func__, 2048);
  DUNE_API.structs_len = 0;

  for (sapi = DUNE_API.structs.first; sapi; sapi = sapi->cont.next) {
    if (!sapi->cont.prophash) {
      sapi->cont.prophash = lib_ghash_str_new("api_init gh");

      for (prop = sapi->cont.props.first; prop; prop = prop->next) {
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
  ApiStruct *sapi;

  for (sapi = DUNE_API.structs.first; sapi; sapi = sapi->cont.next) {
    if (sapi->cont.prophash) {
      lib_ghash_free(sapi->cont.prophash, NULL, NULL);
      sapi->cont.prophash = NULL;
    }
  }

  api_free(&DUNE_API);
}

/* Pointer */
void api_main_ptr_create(struct Main *main, ApiPtr *r_ptr)
{
  r_ptr->owner_id = NULL;
  r_ptr->type = &Api_DuneData;
  r_ptr->data = main;
}

void api_id_ptr_create(Id *id, ApiPtr *r_ptr)
{
  ApiStruct *type, *idtype = NULL;

  if (id) {
    ApiPtr tmp = {NULL};
    tmp.data = id;
    idtype = api_id_refine(&tmp);

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
    ApiPtr tmp = {0};
    tmp.data = id;
    idtype = api_id_refine(&tmp);
  }
#endif

  r_ptr->owner_id = id;
  r_ptr->type = type;
  r_ptr->data = data;

  if (data) {
    while (r_ptr->type && r_ptr->type->refine) {
      ApiStruct *rtype = r_ptr->type->refine(r_ptr);

      if (rtype == r_ptr->type) {
        break;
      }
      r_ptr->type = rtype;
    }
  }
}

bool api_ptr_is_null(const ApiPtr *ptr)
{
  return (ptr->data == NULL) || (ptr->owner_id == NULL) || (ptr->type == NULL);
}

static void api_ptr_inherit_id(ApiStruct *type, ApiPtr *parent, ApiPtr *ptr)
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
  return ApiPtr_NULL;
}

void api_ptr_recast(ApiPtr *ptr, ApiPtr *r_ptr)
{
#if 0 /* works but this case if covered by more general code below. */
  if (api_struct_is_id(ptr->type)) {
    /* simple case */
    api_id_ptr_create(ptr->owner_id, r_ptr);
  }
  else
#endif
  {
    ApiStruct *base;
    ApiPtr t_ptr;
    *r_ptr = *ptr; /* initialize as the same in case can't recast */

    for (base = ptr->type->base; base; base = base->base) {
      t_ptr = api_ptr_inherit_refine(ptr, base, ptr->data);
      if (t_ptr.type && t_ptr.type != ptr->type) {
        *r_ptr = t_ptr;
      }
    }
  }
}

/* Id Props */
void api_idprop_touch(IdProp *idprop)
{
  /* so the prop is seen as 'set' by rna */
  idprop->flag &= ~IDP_FLAG_GHOST;
}

IdProp **api_struct_idprops_p(ApiPtr *ptr)
{
  ApiStruct *type = ptr->type;
  if (type == NULL) {
    return NULL;
  }
  if (type->idprops == NULL) {
    return NULL;
  }

  return type->idprops(ptr);
}

IdProp *api_struct_idprops(ApiPtr *ptr, bool create)
{
  IdProp **prop_ptr = api_struct_idprops_p(ptr);
  if (prop_ptr == NULL) {
    return NULL;
  }

  if (create && *prop_ptr == NULL) {
    IdPropTemplate val = {0};
    *prop_ptr = IDP_New(IDP_GROUP, &val, __func__);
  }

  return *prop_ptr;
}

bool api_struct_idprops_check(ApiStruct *sapi)
{
  return (sapi && sapi->idprops);
}

IdProp *api_idprop_find(ApiPtr *ptr, const char *name)
{
  IdProp *group = api_struct_idprops(ptr, 0);

  if (group) {
    if (group->type == IDP_GROUP) {
      return IDP_GetPropFromGroup(group, name);
    }
    /* Not sure why that happens sometimes, with nested props... */
    /* Seems to be actually array prop, name is usually "0"... To be sorted out later. */
#if 0
      printf(
          "Got unexpected IdProp container when trying to retrieve %s: %d\n", name, group->type);
#endif
  }

  return NULL;
}

static void api_idprop_free(ApiPtr *ptr, const char *name)
{
  IdProp *group = api_struct_idprops(ptr, 0);

  if (group) {
    IDProperty *idprop = IDP_GetPropFromGroup(group, name);
    if (idprop) {
      IDP_FreeFromGroup(group, idprop);
    }
  }
}

static int api_ensure_prop_array_length(ApiPtr *ptr, ApiProp *prop)
{
  if (prop->magic == API_MAGIC) {
    int arraylen[API_MAX_ARRAY_DIMENSION];
    return (prop->getlength && ptr->data) ? prop->getlength(ptr, arraylen) : prop->totarraylength;
  }
  IdProp *idprop = (IdProp *)prop;

  if (idprop->type == IDP_ARRAY) {
    return idprop->len;
  }
  return 0;
}

static bool api_ensure_prop_array_check(ApiProp *prop)
{
  if (prop->magic == API_MAGIC) {
    return (prop->getlength || prop->totarraylength);
  }
  IdProp *idprop = (IdProp *)prop;

  return (idprop->type == IDP_ARRAY);
}

static void api_ensure_prop_multi_array_length(ApiPtr *ptr,
                                               ApiProp *prop,
                                               int length[])
{
  if (prop->magic == API_MAGIC) {
    if (prop->getlength) {
      prop->getlength(ptr, length);
    }
    else {
      memcpy(length, prop->arraylength, prop->arraydimension * sizeof(int));
    }
  }
  else {
    IdProp *idprop = (IdProp *)prop;

    if (idprop->type == IDP_ARRAY) {
      length[0] = idprop->len;
    }
    else {
      length[0] = 0;
    }
  }
}

static bool api_idprop_verify_valid(ApiPtr *ptr, ApiProp *prop, IdProp *idprop)
{
  /* this verifies if the idprop actually matches the property
   * description and otherwise removes it. this is to ensure that
   * rna prop access is type safe, e.g. if you defined the rna
   * to have a certain array length you can count on that staying so */

  switch (idprop->type) {
    case IDP_IDPARRAY:
      if (prop->type != PROP_COLLECTION) {
        return false;
      }
      break;
    case IDP_ARRAY:
      if (api_ensure_prop_array_length(ptr, prop) != idprop->len) {
        return false;
      }

      if (idprop->subtype == IDP_FLOAT && prop->type != PROP_FLOAT) {
        return false;
      }
      if (idprop->subtype == IDP_INT && !ELEM(prop->type, PROP_BOOL, PROP_INT, PROP_ENUM)) {
        return false;
      }

      break;
    case IDP_INT:
      if (!ELEM(prop->type, PROP_BOOL, PROP_INT, PROP_ENUM)) {
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
      if (prop->type != PROP_PTR) {
        return false;
      }
      break;
    default:
      return false;
  }

  return true;
}

static ApiProp *typemap[IDP_NUMTYPES] = {
    &api_PropGroupItem_string,
    &api_PropGroupItem_int,
    &api_PropGroupItem_float,
    NULL,
    NULL,
    NULL,
    &api_PropGroupItem_group,
    &api_PropGroupItem_id,
    &api_PropGroupItem_double,
    &rna_PropGroupItem_idp_array,
};

static ApiProp *arraytypemap[IDP_NUMTYPES] = {
    NULL,
    &api_PropGroupItem_int_array,
    &api_PropGroupItem_float_array,
    NULL,
    NULL,
    NULL,
    &api_PropGroupItem_collection,
    NULL,
    &api_PropGroupItem_double_array,
};

void api_prop_api_or_id_get(ApiProp *prop,
                            ApiPtr *ptr,
                            PropApiOrId *r_prop_api_or_id)
{
  /* This is quite a hack, but avoids some complexity in the API. we
   * pass IdProp structs as ApiProp ptrs to the outside.
   * We store some bytes in ApiProp structs that allows us 
   * distinguish it from IdProp structs. If it is an id prop,
   * we look up an IDP ApiProp based on the type, and set the data
   * pointer to the IdProp. */
  memset(r_prop_api_or_id, 0, sizeof(*r_prop_api_or_id));

  r_prop_api_or_id->ptr = *ptr;
  r_prop_api_or_id->rawprop = prop;

  if (prop->magic == API_MAGIC) {
    r_prop_api_or_id->apiprop = prop;
    r_prop_api_or_id->id = prop->id;

    r_prop_api_or_id->is_array = prop->getlength || prop->totarraylength;
    if (r_prop_api_or_id->is_array) {
      int arraylen[API_MAX_ARRAY_DIMENSION];
      r_prop_api_or_id->array_len = (prop->getlength && ptr->data) ?
                                    (uint)prop->getlength(ptr, arraylen) :
                                    prop->totarraylength;
    }

    if (prop->flag & PROP_IDPROP) {
      IdProp *idprop = api_idprop_find(ptr, prop->id);

      if (idprop != NULL && !api_idprop_verify_valid(ptr, prop, idprop)) {
        IdProp *group = api_struct_idprops(ptr, 0);

        IDP_FreeFromGroup(group, idprop);
        idprop = NULL;
      }

      r_prop_api_or_id->idprop = idprop;
      r_prop_api_or_id->is_set = idprop != NULL && (idprop->flag & IDP_FLAG_GHOST) == 0;
    }
    else {
      /* Full static api props are always set. */
      r_prop_api_or_id->is_set = true;
    }
  }
  else {
    IdProp *idprop = (IdProp *)prop;
    /* Given prop may come from the custom properties of another data, ensure we get the one from
     * given data ptr. */
    IdProp *idprop_evaluated = api_idprop_find(ptr, idprop->name);
    if (idprop_evaluated != NULL && idprop->type != idprop_evaluated->type) {
      idprop_evaluated = NULL;
    }

    r_prop_api_or_id->idprop = idprop_evaluated;
    r_prop_api_or_id->is_idprop = true;
    /* Full IdProps are always set, if it exists. */
    r_prop_api_or_id->is_set = (idprop_evaluated != NULL);

    r_prop_api_or_id->id = idprop->name;
    if (idprop->type == IDP_ARRAY) {
      r_prop_api_or_id->rnaprop = arraytypemap[(int)(idprop->subtype)];
      r_prop_api_or_id->is_array = true;
      r_prop_api_or_id->array_len = idprop_evaluated != NULL ? (uint)idprop_evaluated->len : 0;
    }
    else {
      r_prop_api_or_id->apiprop = typemap[(int)(idprop->type)];
    }
  }
}

IdProp *api_idprop_check(ApiProp **prop, ApiPtr *ptr)
{
  PropApiOrId prop_api_or_id;

  api_prop_api_or_id_get(*prop, ptr, &prop_api_or_id);

  *prop = prop_api_or_id.apiprop;
  return prop_api_or_id.idprop;
}

ApiProp api_ensure_prop_realdata(ApiProp **prop, ApiPtr *ptr)
{
  PropApiOrId prop_api_or_id;

  api_prop_api_or_id_get(*prop, ptr, &prop_rna_or_id);

  *prop = prop_api_or_id.rnaprop;
  return (prop_api_or_id.is_idprop || prop_api_or_id.idprop != NULL) ?
             (ApiProp *)prop_api_or_id.idprop :
             prop_rna_or_id.apiprop;
}

ApiProp *api_ensure_prop(ApiProp *prop)
{
  /* the quick version if we don't need the idproperty */

  if (prop->magic == API_MAGIC) {
    return prop;
  }

  {
    IdProp *idprop = (IdProp *)prop;

    if (idprop->type == IDP_ARRAY) {
      return arraytypemap[(int)(idprop->subtype)];
    }
    return typemap[(int)(idprop->type)];
  }
}

static const char *api_ensure_prop_id(const ApiProp *prop)
{
  if (prop->magic == API_MAGIC) {
    return prop->id;
  }
  return ((const IdProp *)prop)->name;
}

static const char *api_ensure_prop_description(const ApiProp *prop)
{
  if (prop->magic == API_MAGIC) {
    return prop->description;
  }

  const IdProp *idprop = (const IdProp *)prop;
  if (idprop->ui_data) {
    const IdPropUIData *ui_data = idprop->ui_data;
    return ui_data->description;
  }

  return "";
}

static const char *api_ensure_prop_name(const ApiProp *prop)
{
  const char *name;

  if (prop->magic == API_MAGIC) {
    name = prop->name;
  }
  else {
    name = ((const IdProp *)prop)->name;
  }

  return name;
}

/* Structs */
ApiStruct *api_struct_find(const char *id)
{
  return lib_ghash_lookup(DUNE_API.structs_map, id);
}

const char *api_struct_id(const ApiStruct *type)
{
  return type->id;
}

const char *api_struct_ui_name(const ApiStruct *type)
{
  return CXT_IFACE_(type->lang_cxt, type->name);
}

const char *api_struct_ui_name_raw(const ApiStruct *type)
{
  return type->name;
}

int api_struct_ui_icon(const ApiStruct *type)
{
  if (type) {
    return type->icon;
  }
  return ICON_DOT;
}

const char *api_struct_ui_description(const ApiStruct *type)
{
  return TIP_(type->description);
}

const char *api_struct_ui_description_raw(const ApiStruct *type)
{
  return type->description;
}

const char *api_struct_lang_cxt(const ApiStruct *type)
{
  return type->lang_cxt;
}

ApiProp *api_struct_name_prop(const ApiStruct *type)
{
  return type->nameprop;
}

const EnumPropItem *api_struct_prop_tag_defines(const ApiStruct *type)
{
  return type->prop_tag_defines;
}

ApiProp *api_struct_iter_prop(ApiStruct *type)
{
  return type->iterprop;
}

ApiStruct *api_struct_base(ApiStruct *type)
{
  return type->base;
}

const ApiStruct *api_struct_base_child_of(const ApiStruct *type, const ApiStruct *parent_type)
{
  while (type) {
    if (type->base == parent_type) {
      return type;
    }
    type = type->base;
  }
  return NULL;
}

bool api_struct_is_id(const ApiStruct *type)
{
  return (type->flag & STRUCT_ID) != 0;
}

bool api_struct_undo_check(const ApiStruct *type)
{
  return (type->flag & STRUCT_UNDO) != 0;
}

bool api_struct_idprops_register_check(const ApiStruct *type)
{
  return (type->flag & STRUCT_NO_IDPROPS) == 0;
}

bool api_struct_idprops_datablock_allowed(const ApiStruct *type)
{
  return (type->flag & (STRUCT_NO_DATABLOCK_IDPROPS | STRUCT_NO_IDPROPS)) == 0;
}

bool api_struct_idprops_contains_datablock(const ApiStruct *type)
{
  return (type->flag & (STRUCT_CONTAINS_DATABLOCK_IDPROPS | STRUCT_ID)) != 0;
}

bool api_struct_idprops_unset(PointerRNA *ptr, const char *id)
{
  IdProp *group = api_struct_idprops(ptr, 0);

  if (group) {
    IdProp *idp = IDP_GetPropFromGroup(group, id);
    if (idp) {
      IDP_FreeFromGroup(group, idp);

      return true;
    }
  }
  return false;
}

bool api_struct_is_a(const ApiStruct *type, const StructRNA *srna)
{
  const ApiStruct *base;

  if (srna == &Api_AnyType) {
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

ApiProp *api_struct_find_prop(ApiPtr *ptr, const char *id)
{
  if (id[0] == '[' && id[1] == '"') {
    /* id prop lookup, not so common */
    AoiProp *r_prop = NULL;
    ApiPtr r_ptr; /* only support single level props */
    if (api_path_resolve_prop(ptr, id, &r_ptr, &r_prop) && (r_ptr.type == ptr->type) &&
        (r_ptr.data == ptr->data)) {
      return r_prop;
    }
  }
  else {
    /* most common case */
    ApiProp *iterprop = api_struct_iter_prop(ptr->type);
    ApiPtr propptr;

    if (api_prop_collection_lookup_string(ptr, iterprop, id, &propptr)) {
      return propptr.data;
    }
  }

  return NULL;
}

/* Find the prop which uses the given nested struct */
static ApiProp *api_struct_find_nested(ApiPtr *ptr, ApiStruct *sapi)
{
  ApiProp *prop = NULL;

  API_STRUCT_BEGIN (ptr, iprop) {
    /* This assumes that there can only be one user of this nested struct */
    if (api_prop_ptr_type(ptr, iprop) == sapi) {
      prop = iprop;
      break;
    }
  }
  API_PROP_END;

  return prop;
}

bool api_struct_contains_prop(ApiPtr *ptr, apiProp *prop_test)
{
  /* NOTE: prop_test could be freed memory, only use for comparison. */

  /* validate the RNA is ok */
  ApiProp *iterprop;
  bool found = false;

  iterprop = api_struct_iter_prop(ptr->type);

  API_PROP_BEGIN (ptr, itemptr, iterprop) {
    /* ApiProp *prop = itemptr.data; */
    if (prop_test == (ApiProp *)itemptr.data) {
      found = true;
      break;
    }
  }
  API_PROP_END;

  return found;
}

unsigned int api_struct_count_props(ApiStruct *sapi)
{
  ApiPtr struct_ptr;
  unsigned int counter = 0;

  api_ptr_create(NULL, sapi, NULL, &struct_ptr);

  API_STRUCT_BEGIN (&struct_ptr, prop) {
    counter++;
    UNUSED_VARS(prop);
  }
  API_STRUCT_END;

  return counter;
}

const struct List *api_struct_type_props(ApiStruct *sapi)
{
  return &sapi->cont.props;
}

ApiProp *api_struct_type_find_prop_no_base(ApiStruct *sapi, const char *id)
{
  return lib_findstring_ptr(&sapi->cont.props, id, offsetof(ApiProp, id));
}

ApiProp *api_struct_type_find_prop(ApiStruct *sapi, const char *id)
{
  for (; sapi; sapi = sapi->base) {
    ApiProp *prop = api_struct_type_find_prop_no_base(sapi, id);
    if (prop != NULL) {
      return prop;
    }
  }
  return NULL;
}

ApiFn *api_struct_find_fn(ApiStruct *sapi, const char *id)
{
#if 1
  ApiFn *fn;
  for (; sapi; sapi = sapi->base) {
    fn = (ApiFn *)lib_findstring_ptr,
        &sapi->fns, id, offsetof(ApiFn, id));
    if (fn) {
      return fn;
    }
  }
  return NULL;

  /* functional but slow */
#else
  ApiPtr tptr;
  ApiProp *iterprop;
  ApiFn *fn;

  api_pyr_create(NULL, &Api_Struct, sapi, &tptr);
  iterprop = api_struct_find_prop(&tptr, "functions");

  fn = NULL;

  API_PROP_BEGIN (&tptr, fnptr, iterprop) {
    if (STREQ(id, api_fn_id(fnptr.data))) {
      fn = fnptr.data;
      break;
    }
  }
  API_PROP_END;

  return fn;
#endif
}

const List *api_struct_type_fns(ApiStruct *sapi)
{
  return &sapi->fns;
}

StructRegisterFn api_struct_register(ApiStruct *type)
{
  return type->reg;
}

StructUnregisterFn api_struct_unregister(ApiStruct *type)
{
  do {
    if (type->unreg) {
      return type->unreg;
    }
  } while ((type = type->base));

  return NULL;
}

void **api_struct_instance(ApiPtr *ptr)
{
  ApiStruct *type = ptr->type;

  do {
    if (type->instance) {
      return type->instance(ptr);
    }
  } while ((type = type->base));

  return NULL;
}

void *api_struct_py_type_get(AoiStruct *sapi)
{
  return sapi->py_type;
}

void api_struct_py_type_set(ApiStruct *sapi, void *py_type)
{
  sapi->py_type = py_type;
}

void *api_struct_dune_type_get(ApiStruct *sapi)
{
  return sapi->dune_type;
}

void api_struct_dune_type_set(ApiStruct *sapi, void *dune_type)
{
  sapi->dune_type = dune_type;
}

char *api_struct_name_get_alloc(ApiPtr *ptr, char *fixedbuf, int fixedlen, int *r_len)
{
  ApiProp *nameprop;

  if (ptr->data && (nameprop = api_struct_name_prop(ptr->type))) {
    return api_prop_string_get_alloc(ptr, nameprop, fixedbuf, fixedlen, r_len);
  }

  return NULL;
}

bool api_struct_available_or_report(ReportList *reports, const char *id)
{
  const ApiStruct *sapi_exists = api_struct_find(id);
  if (UNLIKELY(sapi_exists != NULL)) {
    /* Use comprehensive string construction since this is such a rare occurrence
     * and information here may cut down time troubleshooting. */
    DynStr *dynstr = lib_dynstr_new();
    lib_dynstr_appendf(dynstr, "Type id '%s' is already in use: '", id);
    lib_dynstr_append(dynstr, sapi_exists->id);
    int i = 0;
    if (sapi_exists->base) {
      for (const ApiStruct *base = sapi_exists->base; base; base = base->base) {
        lib_dynstr_append(dynstr, "(");
        lib_dynstr_append(dynstr, base->id);
        i += 1;
      }
      while (i--) {
        lib_dynstr_append(dynstr, ")");
      }
    }
    lib_dynstr_append(dynstr, "'.");
    char *result = lib_dynstr_get_cstring(dynstr);
    lib_dynstr_free(dynstr);
    dune_report(reports, RPT_ERROR, result);
    mem_freen(result);
    return false;
  }
  return true;
}

bool api_struct_bl_idname_ok_or_report(ReportList *reports,
                                       const char *id,
                                       const char *sep)
{
  const int len_sep = strlen(sep);
  const int len_id = strlen(id);
  const char *p = strstr(id, sep);
  /* TODO: make error, for now warning until add-ons update. */
#if 1
  const int report_level = RPT_WARNING;
  const bool failure = true;
#else
  const int report_level = RPT_ERROR;
  const bool failure = false;
#endif
  if (p == NULL || p == identifier || p + len_sep >= identifier + len_id) {
    dune_reportf(reports,
                report_level,
                "'%s' does not contain '%s' with prefix and suffix",
                id,
                sep);
    return failure;
  }

  const char *c, *start, *end, *last;
  start = id;
  end = p;
  last = end - 1;
  for (c = start; c != end; c++) {
    if (((*c >= 'A' && *c <= 'Z') || ((c != start) && (*c >= '0' && *c <= '9')) ||
         ((c != start) && (c != last) && (*c == '_'))) == 0) {
      dune_reportf(
          reports, report_level, "'%s' doesn't have upper case alpha-numeric prefix", identifier);
      return failure;
    }
  }

  start = p + len_sep;
  end = id + len_id;
  last = end - 1;
  for (c = start; c != end; c++) {
    if (((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
         ((c != start) && (c != last) && (*c == '_'))) == 0) {
      dune_reportf(reports, report_level, "'%s' doesn't have an alpha-numeric suffix", identifier);
      return failure;
    }
  }
  return true;
}

/* Prop Information */
const char *api_prop_id(const ApiProp *prop)
{
  return api_ensure_prop_id(prop);
}

const char *api_prop_description(ApiProp *prop)
{
  return TIP_(api_ensure_prop_description(prop));
}

PropType api_prop_type(ApiProp *prop)
{
  return api_ensure_prop(prop)->type;
}

PropSubType api_prop_subtype(ApiProp *prop)
{
  ApiProp *api_prop = api_ensure_prop(prop);

  /* For custom props, find and parse the 'subtype' metadata field. */
  if (prop->magic != API_MAGIC) {
    IdProp *idprop = (IdProp *)prop;

    if (idprop->ui_data) {
      IdPropUIData *ui_data = idprop->ui_data;
      return (PropSubType)ui_data->api_subtype;
    }
  }

  return api_prop->subtype;
}

PropUnit api_prop_unit(ApiProp *prop)
{
  return API_SUBTYPE_UNIT(api_prop_subtype(prop));
}

PropScaleType api_prop_ui_scale(ApiProp *prop)
{
  ApiProp *api_prop = api_ensure_prop(prop);

  switch (api_prop->type) {
    case PROP_INT: {
      ApiIntProp *iprop = (AoiIntProp *)api_prop;
      return iprop->ui_scale_type;
    }
    case PROP_FLOAT: {
      ApiFloatProp *fprop = (ApiFloatProp *)api_prop;
      return fprop->ui_scale_type;
    }
    default:
      return PROP_SCALE_LINEAR;
  }
}

int api_prop_flag(ApiProp *prop)
{
  return api_ensure_prop(prop)->flag;
}

int api_prop_tags(ApiProp *prop)
{
  return api_ensure_prop(prop)->tags;
}

bool api_prop_builtin(ApiProp *prop)
{
  return (api_ensure_prop(prop)->flag_internal & PROP_INTERN_BUILTIN) != 0;
}

void *api_prop_py_data_get(ApiProp *prop)
{
  return prop->py_data;
}

int api_prop_array_length(ApiPtr *ptr, ApiProp *prop)
{
  return api_ensure_prop_array_length(ptr, prop);
}

bool api_prop_array_check(ApiProp *prop)
{
  return api_ensure_prop_array_check(prop);
}

int api_prop_array_dimension(ApiPtr *ptr, ApiProp *prop, int length[])
{
  ApiProp *rprop = api_ensure_prop(prop);

  if (length) {
    api_ensure_prop_multi_array_length(ptr, prop, length);
  }

  return rprop->arraydimension;
}

int api_prop_multi_array_length(ApiPtr *ptr, AoiProp *prop, int dim)
{
  int len[API_MAX_ARRAY_DIMENSION];

  api_ensure_prop_multi_array_length(ptr, prop, len);

  return len[dim];
}

char api_prop_array_item_char(ApiProp *prop, int index)
{
  const char *vectoritem = "XYZW";
  const char *quatitem = "WXYZ";
  const char *coloritem = "RGBA";
  PropSubType subtype = api_prop_subtype(prop);

  lib_assert(index >= 0);

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

int api_prop_array_item_index(ApiProp *prop, char name)
{
  /* Don't use custom prop subtypes in api path lookup. */
  PropSubType subtype = api_ensure_prop(prop)->subtype;

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

void api_prop_int_range(ApiPtr *ptr, ApiProp *prop, int *hardmin, int *hardmax)
{
  ApiIntProp *iprop = (ApiIntProp *)api_ensure_prop(prop);
  int softmin, softmax;

  if (prop->magic != API_MAGIC) {
    const IdProp *idprop = (IdProp *)prop;
    if (idprop->ui_data) {
      IdPropUIDataInt *ui_data = (IdPropUIDataInt *)idprop->ui_data;
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

void api_prop_int_ui_range(
    ApiPtr *ptr, ApiProp *prop, int *softmin, int *softmax, int *step)
{
  ApiIntProp *iprop = (ApiIntProp *)api_ensure_prop(prop);
  int hardmin, hardmax;

  if (prop->magic != API_MAGIC) {
    const IdProp *idprop = (IdProp *)prop;
    if (idprop->ui_data) {
      IdPropUIDataInt *ui_data_int = (IdPropUIDataInt *)idprop->ui_data;
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

void api_prop_float_range(ApiPtr *ptr, ApiProp *prop, float *hardmin, float *hardmax)
{
  ApiFloatProp *fprop = (ApiFloatProp *)api_ensure_prop(prop);
  float softmin, softmax;

  if (prop->magic != API_MAGIC) {
    const IDProp *idprop = (IdProp *)prop;
    if (idprop->ui_data) {
      IdPropUIDataFloat *ui_data = (IdPropUIDataFloat *)idprop->ui_data;
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

void api_prop_float_ui_range(ApiPtr *ptr,
                             ApiProp *prop,
                             float *softmin,
                             float *softmax,
                             float *step,
                             float *precision)
{
  ApiFloatProp *fprop = (ApiFloatProp *)api_ensure_prop(prop);
  float hardmin, hardmax;

  if (prop->magic != API_MAGIC) {
    const IdProp *idprop = (IdProp *)prop;
    if (idprop->ui_data) {
      IdPropUIDataFloat *ui_data = (IdPropUIDataFloat *)idprop->ui_data;
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

int api_prop_float_clamp(ApiPtr *ptr, ApiProp *prop, float *value)
{
  float min, max;

  api_prop_float_range(ptr, prop, &min, &max);

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

int api_prop_int_clamp(ApiPtr *ptr, ApiProp *prop, int *value)
{
  int min, max;

  api_prop_int_range(ptr, prop, &min, &max);

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

int api_prop_string_maxlength(ApiProp *prop)
{
  ApiStringProp *sprop = (ApiStringProp *)api_ensure_prop(prop);
  return sprop->maxlength;
}

ApiStruct *api_prop_ptr_type(ApiPtr *ptr, ApiProp *prop)
{
  prop = api_ensure_prop(prop);

  if (prop->type == PROP_POINTER) {
    ApiPtrProp *pprop = (ApiPtrProp *)prop;

    if (pprop->type_fn) {
      return pprop->type_fn(ptr);
    }
    if (pprop->type) {
      return pprop->type;
    }
  }
  else if (prop->type == PROP_COLLECTION) {
    ApiCollectionProp *cprop = (ApiCollectionProp *)prop;

    if (cprop->item_type) {
      return cprop->item_type;
    }
  }
  /* ignore other types, Rapi_struct_find_nested calls with unchecked props */

  return &Api_UnknownType;
}

bool api_prop_ptr_poll(ApiPtr *ptr, ApiProp *prop, ApiPtr *value)
{
  prop = api_ensure_prop(prop);

  if (prop->type == PROP_PTR) {
    ApiPtrProp *pprop = (ApiPtrProp *)prop;

    if (pprop->poll) {
      if (api_idprop_check(&prop, ptr)) {
        return ((PropPtrPollFnPy)pprop->poll)(ptr, *value, prop);
      }
      return pprop->poll(ptr, *value);
    }

    return 1;
  }

  printf("%s: %s is not a ptr prop.\n", __func__, prop->identifier);
  return 0;
}

void api_prop_enum_items_ex(Cxt *C,
                            ApiPtr *ptr,
                            ApiProp *prop,
                            const bool use_static,
                            const EnumPropItem **r_item,
                            int *r_totitem,
                            bool *r_free)
{
  ApiEnumProp *eprop = (ApiEnumProp *)api_ensure_prop(prop);

  *r_free = false;

  if (!use_static && (eprop->item_fn != NULL)) {
    const bool no_cxt = (prop->flag & PROP_ENUM_NO_CXT) ||
                            ((ptr->type->flag & STRUCT_NO_CXT_WITHOUT_OWNER_ID) &&
                             (ptr->owner_id == NULL));
    if (C != NULL || no_cxt) {
      const EnumPropItem *item;

      item = eprop->item_fn(no_cxt ? NULL : C, ptr, prop, r_free);

      /* any cbs returning NULL should be fixed */
      lib_assert(item != NULL);

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

void api_prop_enum_items(Cxt *C,
                         ApiPtr *ptr,
                         ApiProp *prop,
                         const EnumPropItem **r_item,
                         int *r_totitem,
                         bool *r_free)
{
  api_prop_enum_items_ex(C, ptr, prop, false, r_item, r_totitem, r_free);
}

#ifdef WITH_INTERNATIONAL
static void prop_enum_lang(ApiProp *prop,
                           EnumPropItem **r_item,
                           const int *totitem,
                           bool *r_free)
{
  if (!(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
    int i;

    /* NOTE: Only do those tests once, and then use BLT_pgettext. */
    bool do_iface = lang_iface();
    bool do_tooltip = lang_tooltips();
    EnumPropItem *nitem;

    if (!(do_iface || do_tooltip)) {
      return;
    }

    if (*r_free) {
      nitem = *r_item;
    }
    else {
      const EnumPropItem *item = *r_item;
      int tot;

      if (totitem) {
        tot = *totitem;
      }
      else {
        /* count */
        for (tot = 0; item[tot].id; tot++) {
          /* pass */
        }
      }

      nitem = mem_mallocn(sizeof(EnumPropItem) * (tot + 1), "enum_items_gettexted");
      memcpy(nitem, item, sizeof(EnumPropItem) * (tot + 1));

      *r_free = true;
    }

    for (i = 0; nitem[i].id; i++) {
      if (nitem[i].name && do_iface) {
        nitem[i].name = lang_pgettext(prop->lang_cxt, nitem[i].name);
      }
      if (nitem[i].description && do_tooltip) {
        nitem[i].description = lang_pgettext(NULL, nitem[i].description);
      }
    }

    *r_item = nitem;
  }
}
#endif

void api_prop_enum_items_gettexted(Cxt *C,
                                   ApiPtr *ptr,
                                   ApiProp *prop,
                                   const EnumPropItem **r_item,
                                   int *r_totitem,
                                   bool *r_free)
{
  api_prop_enum_items(C, ptr, prop, r_item, r_totitem, r_free);

#ifdef WITH_INTERNATIONAL
  /* Normally dropping 'const' is _not_ ok, in this case it's only modified if we own the memory
   * so allow the exception (callers are creating new arrays in this case). */
  prop_enum_lang(prop, (EnumPropItem **)r_item, r_totitem, r_free);
#endif
}

void api_prop_enum_items_gettexted_all(Cxt *C,
                                       ApiPtr *ptr,
                                       ApiProp *prop,
                                       const EnumPropItem **r_item,
                                       int *r_totitem,
                                       bool *r_free)
{
  ApiEnumProp *eprop = (ApiEnumProp *)api_ensure_prop(prop);
  int mem_size = sizeof(EnumPropItem) * (eprop->totitem + 1);
  /* first return all items */
  EnumPropItem *item_array = mem_mallocn(mem_size, "enum_gettext_all");
  *r_free = true;
  memcpy(item_array, eprop->item, mem_size);

  if (r_totitem) {
    *r_totitem = eprop->totitem;
  }

  if (eprop->item_fn != NULL) {
    const bool no_cxt = (prop->flag & PROP_ENUM_NO_CXT) ||
                            ((ptr->type->flag & STRUCT_NO_CXT_WITHOUT_OWNER_ID) &&
                             (ptr->owner_id == NULL));
    if (C != NULL || no_cxt) {
      const EnumPropItem *item;
      int i;
      bool free = false;

      item = eprop->item_fn(no_cxt ? NULL : NULL, ptr, prop, &free);

      /* any callbacks returning NULL should be fixed */
      lib_assert(item != NULL);

      for (i = 0; i < eprop->totitem; i++) {
        bool exists = false;
        int i_fixed;

        /* Items that do not exist on list are returned,
         * but have their names/ids NULL'ed out. */
        for (i_fixed = 0; item[i_fixed].id; i_fixed++) {
          if (STREQ(item[i_fixed].id, item_array[i].identifier)) {
            exists = true;
            break;
          }
        }

        if (!exists) {
          item_array[i].name = NULL;
          item_array[i].id = "";
        }
      }

      if (free) {
        mem_freen((void *)item);
      }
    }
  }

#ifdef WITH_INTERNATIONAL
  prop_enum_lang(prop, &item_array, r_totitem, r_free);
#endif
  *r_item = item_array;
}

bool api_prop_enum_value(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const char *id, int *r_value)
{
  const EnumPropItem *item;
  bool free;
  bool found;

  api_prop_enum_items(C, ptr, prop, &item, NULL, &free);

  if (item) {
    const int i = api_enum_from_id(item, id);
    if (i != -1) {
      *r_value = item[i].value;
      found = true;
    }
    else {
      found = false;
    }

    if (free) {
      mem_freen((void *)item);
    }
  }
  else {
    found = false;
  }
  return found;
}

//check this one
bool api_enum_id(const EnumPropItem *item, const int value, const char **r_id)
{
  const int i = api_enum_from_value(item, value);
  if (i != -1) {
    *r_id = item[i].id;
    return true;
  }
  return false;
}

int api_enum_bitflag_id(const EnumPropItem *item,
                        const int value,
                        const char **r_id)
{
  int index = 0;
  for (; item->id; item++) {
    if (item->id[0] && item->value & value) {
      r_id[index++] = item->id;
    }
  }
  r_id[index] = NULL;
  return index;
}

bool api_enum_name(const EnumPropItem *item, const int value, const char **r_name)
{
  const int i = api_enum_from_value(item, value);
  if (i != -1) {
    *r_name = item[i].name;
    return true;
  }
  return false;
}

bool api_enum_description(const EnumPropItem *item,
                          const int value,
                          const char **r_description)
{
  const int i = api_enum_from_value(item, value);
  if (i != -1) {
    *r_description = item[i].description;
    return true;
  }
  return false;
}

int api_enum_from_id(const EnumPropItem *item, const char *id)
{
  int i = 0;
  for (; item->id; item++, i++) {
    if (item->id[0] && STREQ(item->id, id)) {
      return i;
    }
  }
  return -1;
}

int api_enum_from_name(const EnumPropItem *item, const char *name)
{
  int i = 0;
  for (; item->id; item++, i++) {
    if (item->id[0] && STREQ(item->name, name)) {
      return i;
    }
  }
  return -1;
}

int api_enum_from_value(const EnumPropItem *item, const int value)
{
  int i = 0;
  for (; item->identifier; item++, i++) {
    if (item->identifier[0] && item->value == value) {
      return i;
    }
  }
  return -1;
}

unsigned int api_enum_items_count(const EnumPropItem *item)
{
  unsigned int i = 0;

  while (item->id) {
    item++;
    i++;
  }

  return i;
}

bool api_prop_enum_id(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const int value, const char **id)
{
  const EnumPropItem *item = NULL;
  bool free;

  api_prop_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    bool result;
    result = api_enum_id(item, value, identifier);
    if (free) {
      mem_freen((void *)item);
    }
    return result;
  }
  return false;
}

bool api_prop_enum_name(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const int value, const char **name)
{
  const EnumPropItem *item = NULL;
  bool free;

  api_prop_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    bool result;
    result = api_enum_name(item, value, name);
    if (free) {
      mem_freen((void *)item);
    }

    return result;
  }
  return false;
}

bool api_prop_enum_name_gettexted(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const int value, const char **name)
{
  bool result;

  result = api_prop_enum_name(C, ptr, prop, value, name);

  if (result) {
    if (!(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
      if (lang_iface()) {
        *name = lang_pgettext(prop->lang_cxt, *name);
      }
    }
  }

  return result;
}

bool api_prop_enum_item_from_value(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const int value, EnumPropItem *r_item)
{
  const EnumPropItem *item = NULL;
  bool free;

  api_prop_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    const int i = api_enum_from_value(item, value);
    bool result;

    if (i != -1) {
      *r_item = item[i];
      result = true;
    }
    else {
      result = false;
    }

    if (free) {
      mem_freen((void *)item);
    }

    return result;
  }
  return false;
}

bool api_prop_enum_item_from_value_gettexted(
     xt *C, ApiPtr *ptr, ApiProp *prop, const int value, EnumPropItem *r_item)
{
  const bool result = api_prop_enum_item_from_value(C, ptr, prop, value, r_item);

  if (result && !(prop->flag & PROP_ENUM_NO_TRANSLATE)) {
    if (lang_iface()) {
      r_item->name = lang_pgettext(prop->lang_cxt, r_item->name);
    }
  }

  return result;
}

int api_prop_enum_bitflag_ids(
    Cxt *C, ApiPtr *ptr, ApiProp *prop, const int value, const char **id)
{
  const EnumPropItem *item = NULL;
  bool free;

  api_prop_enum_items(C, ptr, prop, &item, NULL, &free);
  if (item) {
    int result;
    result = api_enum_bitflag_ids(item, value, id);
    if (free) {
      mem_freen((void *)item);
    }

    return result;
  }
  return 0;
}

const char *api_prop_ui_name(const ApiProp *prop)
{
  return CXT_IFACE_(prop->lang_cxt, api_ensure_prop_name(prop));
}

const char *api_prop_ui_name_raw(const ApiProp *prop)
{
  return api_ensure_prop_name(prop);
}

const char *api_prop_ui_description(const ApiProp *prop)
{
  return TIP_(api_ensure_prop_description(prop));
}

const char *api_prop_ui_description_raw(const ApiProp *prop)
{
  return api_ensure_prop_description(prop);
}

const char *api_prop_lang_cxt(const ApiProp *prop)
{
  return api_ensure_prop((ApiProp *)prop)->lang_cxt;
}

int api_prop_ui_icon(const ApiProp *prop)
{
  return api_ensure_prop((ApiProp *)prop)->icon;
}

static bool api_prop_editable_do(ApiPtr *ptr,
                                 ApiProp *prop_orig,
                                 const int index,
                                 const char **r_info)
{
  Id *id = ptr->owner_id;

  ApiProp *prop = api_ensure_prop(prop_orig);

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
      *r_info = N_("This prop is for internal use only and can't be edited");
    }
    return false;
  }

  /* If there is no owning Id, the prop is editable at this point. */
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
  if (ID_IS_OVERRIDE_LIB(id) && !api_prop_overridable_get(ptr, prop_orig)) {
    if (r_info != NULL && (*r_info)[0] == '\0') {
      *r_info = N_("Can't edit this prop from an override data-block");
    }
    return false;
  }

  /* At this point, prop is owned by a local Id and therefore fully editable. */
  return true;
}

bool api_prop_editable(ApiPtr *ptr, ApiProp *prop)
{
  return api_prop_editable_do(ptr, prop, -1, NULL);
}

bool api_prop_editable_info(ApiPtr *ptr, ApiProp *prop, const char **r_info)
{
  return api_prop_editable_do(ptr, prop, -1, r_info);
}

bool api_prop_editable_flag(ApiPtr *ptr, ApiProp *prop)
{
  int flag;
  const char *dummy_info;

  prop = api_ensure_prop(prop);
  flag = prop->editable ? prop->editable(ptr, &dummy_info) : prop->flag;
  return (flag & PROP_EDITABLE) != 0;
}

bool api_prop_editable_index(ApiPtr *ptr, ApiProp *prop, const int index)
{
  lib_assert(index >= 0);

  return api_prop_editable_do(ptr, prop, index, NULL);
}

bool api_prop_animateable(ApiPtr *ptr, ApiProp *prop)
{
/* check that base Id-block can support animation data */
  if (!id_can_have_animdata(ptr->owner_id)) {
    return false;
  }

  prop = api_ensure_prop(prop);

  if (!(prop->flag & PROP_ANIMATABLE)) {
    return false;
  }

  return (prop->flag & PROP_EDITABLE) != 0;
}

bool api_prop_animated(ApiPtr *ptr, ApiProp *prop)
{
  int len = 1, index;
  bool driven, special;

  if (!prop) {
    return false;
  }

  if (api_prop_array_check(prop)) {
    len = api_prop_array_length(ptr, prop);
  }

  for (index = 0; index < len; index++) {
    if (dune_fcurve_find_by_api(ptr, prop, index, NULL, NULL, &driven, &special)) {
      return true;
    }
  }

  return false;
}
bool api_prop_path_from_id_check(ApiPtr *ptr, ApiProp *prop)
{
  char *path = api_path_from_id_to_prop(ptr, prop);
  bool ret = false;

  if (path) {
    ApiPtr id_ptr;
    ApiPtr r_ptr;
    ApiProp *r_prop;

    api_id_ptr_create(ptr->owner_id, &id_ptr);
    if (api_path_resolve(&id_ptr, path, &r_ptr, &r_prop) == true) {
      ret = (prop == r_prop);
    }
    mem_freen(path);
  }

  return ret;
}

static void api_prop_update(
    Cxt *C, Main *main, Scene *scene, Apitr *ptr, ApiProp *prop)
{
  const bool is_api = (prop->magic == API_MAGIC);
  prop = api_ensure_prop(prop);

  if (is_api) {
    if (prop->update) {
      /* ideally no cxt would be needed for update, but there's some
       * parts of the code that need it still, so we have this exception */
      if (prop->flag & PROP_CXT_UPDATE) {
        if (C) {
          if ((prop->flag & PROP_CXT_PROP_UPDATE) == PROP_CXT_PROP_UPDATE) {
            ((CxtPropUpdateFn)prop->update)(C, ptr, prop);
          }
          else {
            ((CxtUpdateFn)prop->update)(C, ptr);
          }
        }
      }
      else {
        prop->update(main, scene, ptr);
      }
    }

#if 1
    /* TODO: Should eventually be replaced entirely by message bus (below)
     * for now keep since COW, bugs are hard to track when we have other missing updates. */
    if (prop->noteflag) {
      wm_main_add_notifier(prop->noteflag, ptr->owner_id);
    }
#endif

    /* if C is NULL, we're updating from animation.
     * avoid slow-down from f-curves by not publishing (for now). */
    if (C != NULL) {
      struct wmMsgBus *mbus = cxt_wm_message_bus(C);
      /* we could add NULL check, for now don't */
      wm_msg_publish_api(mbus, ptr, prop);
    }
    if (ptr->owner_id != NULL && ((prop->flag & PROP_NO_GRAPH_UPDATE) == 0)) {
      const short id_type = GS(ptr->owner_id->name);
      if (ID_TYPE_IS_COW(id_type)) {
        graph_id_tag_update(ptr->owner_id, ID_RECALC_COPY_ON_WRITE);
      }
    }
    /* End message bus. */
  }

  if (!is_api || (prop->flag & PROP_IDPROP)) {

    /* Disclaimer: this logic is not applied consistently, causing some confusing behavior.
     *
     * - When animated (which skips update functions).
     * - When Id-props are edited via Python (since RNA props aren't used in this case).
     *
     * Adding updates will add a lot of overhead in the case of animation.
     * For Python it may cause unexpected slow-downs for developers using ID-properties
     * for data storage. Further, the root ID isn't available with nested data-structures.
     *
     * So editing custom properties only causes updates in the UI,
     * keep this exception because it happens to be useful for driving settings.
     * Python developers on the other hand will need to manually 'update_tag', see: T74000. */
    graph_id_tag_update(ptr->owner_id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_PARAMS);

    /* When updating an ID pointer property, tag depsgraph for update. */
    if (prop->type == PROP_POINTER && RNA_struct_is_ID(RNA_property_ptr_type(ptr, prop))) {
      graph_relations_tag_update(main);
    }

    wm_main_add_notifier(NC_WINDOW, NULL);
    /* Not nice as well, but the only way to make sure material preview
     * is updated with custom nodes.
     */
    if ((prop->flag & PROP_IDPROP) != 0 && (ptr->owner_id != NULL) &&
        (GS(ptr->owner_id->name) == ID_NT)) {
      wm_main_add_notifier(NC_MATERIAL | ND_SHADING, NULL);
    }
  }
}

bool api_prop_update_check(ApiProp *prop)
{
  /* NOTE: must keep in sync with api_prop_update. */
  return (prop->magic != API_MAGIC || prop->update || prop->noteflag);
}

void api_prop_update(Cxt *C, ApiPtr *ptr, ApiProp *prop)
{
  api_prop_update(C, cxt_data_main(C), cxt_data_scene(C), ptr, prop);
}

void api_prop_update_main(Main *main, Scene *scene, ApiPtr *ptr, ApiProp *prop)
{
  api_prop_update(NULL, main, scene, ptr, prop);
}

/* Property Data */
bool api_prop_bool_get(ApiPtr *ptr, ApiProp *prop)
{
  ApiBoolProp *bprop = (ApiBoolProp *)prop;
  IdProp *idprop;
  bool value;

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) == false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
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

  lib_assert(ELEM(value, false, true));

  return value;
}

void api_prop_bool_set(ApiPtr *ptr, ApiProp *prop, bool value)
{
  ApiBoolProp *bprop = (ApiBoolProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) == false);
  lib_assert(ELEM(value, false, true));

  /* just in case other values are passed */
  lib_assert(ELEM(value, true, false));

  if ((idprop = api_idprop_check(&prop, ptr))) {
    IDP_Int(idprop) = (int)value;
    api_idprop_touch(idprop);
  }
  else if (bprop->set) {
    bprop->set(ptr, value);
  }
  else if (bprop->set_ex) {
    bprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    val.i = value;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_INT, &val, prop->identifier));
    }
  }
}

static void api_prop_bool_fill_default_array_values(
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

static void api_prop_bool_get_default_array_values(ApiPtr *ptr,
                                                   ApiBoolProp *bprop,
                                                   bool *r_values)
{
  int length = bprop->prop.totarraylength;
  int out_length = api_prop_array_length(ptr, (ApiProp *)bprop);

  api_prop_bool_fill_default_array_values(
      bprop->defaultarray, length, bprop->defaultvalue, out_length, r_values);
}

void api_prop_bool_get_array(ApiPtr *ptr, ApiProp *prop, bool *values)
{
  ApiBoolProp *bprop = (ApiBoolProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    if (prop->arraydimension == 0) {
      values[0] = api_prop_bool_get(ptr, prop);
    }
    else {
      int *values_src = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values[i] = (bool)values_src[i];
      }
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = api_prop_bool_get(ptr, prop);
  }
  else if (bprop->getarray) {
    bprop->getarray(ptr, values);
  }
  else if (bprop->getarray_ex) {
    bprop->getarray_ex(ptr, prop, values);
  }
  else {
    api_prop_bool_get_default_array_values(ptr, bprop, values);
  }
}

bool api_prop_bool_get_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  bool tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);
  bool value;

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_bool_get_array(ptr, prop, tmp);
    value = tmp[index];
  }
  else {
    bool *tmparray;

    tmparray = mem_mallocn(sizeof(bool) * len, __func__);
    api_prop_bool_get_array(ptr, prop, tmparray);
    value = tmparray[index];
    mem_freen(tmparray);
  }

  lib_assert(ELEM(value, false, true));

  return value;
}

void api_prop_bool_set_array(ApiPtr *ptr, ApiProp *prop, const bool *values)
{
  ApiBoolProp *bprop = (ApiBoolProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    if (prop->arraydimension == 0) {
      IDP_Int(idprop) = values[0];
    }
    else {
      int *values_dst = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values_dst[i] = (int)values[i];
      }
    }
    api_idprop_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    api_prop_bool_set(ptr, prop, values[0]);
  }
  else if (bprop->setarray) {
    bprop->setarray(ptr, values);
  }
  else if (bprop->setarray_ex) {
    bprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    val.array.len = prop->totarraylength;
    val.array.type = IDP_INT;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->id);
      IDP_AddToGroup(group, idprop);
      int *values_dst = IDP_Array(idprop);
      for (uint i = 0; i < idprop->len; i++) {
        values_dst[i] = (int)values[i];
      }
    }
  }
}

void api_prop_bool_set_index(ApiPtr *ptr, ApiProp *prop, int index, bool value)
{
  bool tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_BOOLEAN);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);
  lib_assert(ELEM(value, false, true));

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_bool_get_array(ptr, prop, tmp);
    tmp[index] = value;
    api_prop_bool_set_array(ptr, prop, tmp);
  }
  else {
    bool *tmparray;

    tmparray = mem_mallocn(sizeof(bool) * len, __func__);
    api_prop_bool_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    api_property_bool_set_array(ptr, prop, tmparray);
    mem_freen(tmparray);
  }
}

bool api_prop_bool_get_default(ApiPtr *UNUSED(ptr), ApiProp *prop)
{
  ApiBoolProp *bprop = (ApiBoolProp *)api_ensure_prop(prop);

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) == false);
  lib_assert(ELEM(bprop->defaultvalue, false, true));

  return bprop->defaultvalue;
}

void api_prop_bool_get_default_array(ApiPtr *ptr, ApiProp *prop, bool *values)
{
  ApiBoolProp *bprop = (ApiBoolProp *)api_ensure_prop(prop);

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) != false);

  if (prop->arraydimension == 0) {
    values[0] = bprop->defaultvalue;
  }
  else {
    api_prop_bool_get_default_array_values(ptr, bprop, values);
  }
}

bool api_prop_bool_get_default_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  bool tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_BOOL);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_bool_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  bool *tmparray, value;

  tmparray = mem_mallocn(sizeof(bool) * len, __func__);
  api_prop_bool_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  mem_freen(tmparray);

  return value;
}

int api_prop_int_get(ApiPtr *ptr, ApiProp *prop)
{
  ApiIntProp *iprop = (IntProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) == false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
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

void api_prop_int_set(ApiPtr *ptr, ApiProp *prop, int value)
{
  ApiIntProp *iprop = (ApiIntProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) == false);
  /* useful to check on bad values but set fn should clamp */
  // lib_assert(api_prop_int_clamp(ptr, prop, &value) == 0);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    api_prop_int_clamp(ptr, prop, &value);
    IDP_Int(idprop) = value;
    api_idprop_touch(idprop);
  }
  else if (iprop->set) {
    iprop->set(ptr, value);
  }
  else if (iprop->set_ex) {
    iprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    api_prop_int_clamp(ptr, prop, &value);

    val.i = value;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_INT, &val, prop->id));
    }
  }
}

static void api_prop_int_fill_default_array_values(
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

static void api_prop_int_get_default_array_values(ApiPtr *ptr,
                                                  ApiIntProp *iprop,
                                                  int *r_values)
{
  int length = iprop->prop.totarraylength;
  int out_length = api_prop_array_length(ptr, (ApiProp *)iprop);

  api_prop_int_fill_default_array_values(
      iprop->defaultarray, length, iprop->defaultvalue, out_length, r_values);
}

void api_prop_int_get_array(ApiPtr *ptr, ApiProp *prop, int *values)
{
  ApiIntProp *iprop = (ApiIntProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    lib_assert(idprop->len == api_prop_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROP));
    if (prop->arraydimension == 0) {
      values[0] = api_prop_int_get(ptr, prop);
    }
    else {
      memcpy(values, IDP_Array(idprop), sizeof(int) * idprop->len);
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = api_prop_int_get(ptr, prop);
  }
  else if (iprop->getarray) {
    iprop->getarray(ptr, values);
  }
  else if (iprop->getarray_ex) {
    iprop->getarray_ex(ptr, prop, values);
  }
  else {
    api_prop_int_get_default_array_values(ptr, iprop, values);
  }
}

void api_prop_int_get_array_range(ApiPtr *ptr, ApiProp *prop, int values[2])
{
  const int array_len = api_prop_array_length(ptr, prop);

  if (array_len <= 0) {
    values[0] = 0;
    values[1] = 0;
  }
  else if (array_len == 1) {
    api_prop_int_get_array(ptr, prop, values);
    values[1] = values[0];
  }
  else {
    int arr_stack[32];
    int *arr;
    int i;

    if (array_len > 32) {
      arr = mem_mallocn(sizeof(int) * array_len, __func__);
    }
    else {
      arr = arr_stack;
    }

    api_prop_int_get_array(ptr, prop, arr);
    values[0] = values[1] = arr[0];
    for (i = 1; i < array_len; i++) {
      values[0] = MIN2(values[0], arr[i]);
      values[1] = MAX2(values[1], arr[i]);
    }

    if (arr != arr_stack) {
      mem_freen(arr);
    }
  }
}

int api_prop_int_get_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  int tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_int_get_array(ptr, prop, tmp);
    return tmp[index];
  }
  int *tmparray, value;

  tmparray = mem_mallocn(sizeof(int) * len, __func__);
  api_prop_int_get_array(ptr, prop, tmparray);
  value = tmparray[index];
  mem_freen(tmparray);

  return value;
}

void api_prop_int_set_array(ApiPtr *ptr, ApiProp *prop, const int *values)
{
  ApiIntProp *iprop = (ApiIntProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    lib_assert(idprop->len == api_prop_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROP));
    if (prop->arraydimension == 0) {
      IDP_Int(idprop) = values[0];
    }
    else {
      memcpy(IDP_Array(idprop), values, sizeof(int) * idprop->len);
    }

    api_idprop_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    api_prop_int_set(ptr, prop, values[0]);
  }
  else if (iprop->setarray) {
    iprop->setarray(ptr, values);
  }
  else if (iprop->setarray_ex) {
    iprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    /* TODO: api_prop_int_clamp_array(ptr, prop, &value); */
    val.array.len = prop->totarraylength;
    val.array.type = IDP_INT;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->id);
      IDP_AddToGroup(group, idprop);
      memcpy(IDP_Array(idprop), values, sizeof(int) * idprop->len);
    }
  }
}

void api_prop_int_set_index(ApiPtr *ptr, ApiProp *prop, int index, int value)
{
  int tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_int_get_array(ptr, prop, tmp);
    tmp[index] = value;
    api_prop_int_set_array(ptr, prop, tmp);
  }
  else {
    int *tmparray;

    tmparray = mem_mallocn(sizeof(int) * len, __func__);
    api_prop_int_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    api_prop_int_set_array(ptr, prop, tmparray);
    mem_freen(tmparray);
  }
}

int api_prop_int_get_default(ApiPtr *UNUSED(ptr), ApiProp *prop)
{
  ApiIntProp *iprop = (ApiIntProp *)api_ensure_prop(prop);

  if (prop->magic != API_MAGIC) {
    const IdProp *idprop = (const IdProp *)prop;
    if (idprop->ui_data) {
      const IdPropUIDataInt *ui_data = (const IdPropUIDataInt *)idprop->ui_data;
      return ui_data->default_value;
    }
  }

  return iprop->defaultvalue;
}

bool api_prop_int_set_default(ApiProp *prop, int value)
{
  if (prop->magic == API_MAGIC) {
    return false;
  }

  IdProp *idprop = (IdProp *)prop;
  lib_assert(idprop->type == IDP_INT);

  IdPropUIDataInt *ui_data = (IdPropUIDataInt *)IDP_ui_data_ensure(idprop);
  ui_data->default_value = value;
  return true;
}

void api_prop_int_get_default_array(ApiPtr *ptr, ApiProp *prop, int *values)
{
  ApiIntProp *iprop = (ApiIntProp *)api_ensure_prop(prop);

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);

  if (prop->magic != API_MAGIC) {
    int length = api_ensure_prop_array_length(ptr, prop);

    const IdProp *idprop = (const IdProp *)prop;
    if (idprop->ui_data) {
      lib_assert(idprop->type == IDP_ARRAY);
      lib_assert(idprop->subtype == IDP_INT);
      const IdPropUIDataInt *ui_data = (const IdPropUIDataInt *)idprop->ui_data;
      if (ui_data->default_array) {
        api_prop_int_fill_default_array_values(ui_data->default_array,
                                               ui_data->default_array_len,
                                               ui_data->default_value,
                                               length,
                                               values);
      }
      else {
        api_prop_int_fill_default_array_values(
            NULL, 0, ui_data->default_value, length, values);
      }
    }
  }
  else if (prop->arraydimension == 0) {
    values[0] = iprop->defaultvalue;
  }
  else {
    api_prop_int_get_default_array_values(ptr, iprop, values);
  }
}

int api_prop_int_get_default_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  int tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_INT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_int_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  int *tmparray, value;

  tmparray = mem_mallocn(sizeof(int) * len, __func__);
  api_prop_int_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  mem_freen(tmparray);

  return value;
}

float api_prop_float_get(ApiPtr *ptr, ApiProp *prop)
{
  ApiFloatProp *fprop = (ApiFloatProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) == false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
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

void api_prop_float_set(ApiPtr *ptr, ApiProp *prop, float value)
{
  ApiFloatProp *fprop = (ApiFloatProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) == false);
  /* useful to check on bad values but set function should clamp */
  // BLI_assert(api_prop_float_clamp(ptr, prop, &value) == 0);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    api_prop_float_clamp(ptr, prop, &value);
    if (idprop->type == IDP_FLOAT) {
      IDP_Float(idprop) = value;
    }
    else {
      IDP_Double(idprop) = value;
    }

    api_idprop_touch(idprop);
  }
  else if (fprop->set) {
    fprop->set(ptr, value);
  }
  else if (fprop->set_ex) {
    fprop->set_ex(ptr, prop, value);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    api_prop_float_clamp(ptr, prop, &value);

    val.f = value;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_FLOAT, &val, prop->id));
    }
  }
}

static void api_prop_float_fill_default_array_values(
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

/* The same logic as #rna_property_float_fill_default_array_values for a double array */
static void api_prop_float_fill_default_array_values_double(const double *default_array,
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

static void api_prop_float_get_default_array_values(ApiPtr *ptr,
                                                    ApiFloatProp *fprop,
                                                    float *r_values)
{
  int length = fprop->prop.totarraylength;
  int out_length = api_prop_array_length(ptr, (ApiPropu *)fprop);

  api_prop_float_fill_default_array_values(
      fprop->defaultarray, length, fprop->defaultvalue, out_length, r_values);
}

void api_prop_float_get_array(ApiPtr *ptr, ApiProp *prop, float *values)
{
  ApiFloatProp *fprop = (ApiFloatProp *)prop;
  IdProp *idprop;
  int i;

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    lib_assert(idprop->len == api_prop_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROP));
    if (prop->arraydimension == 0) {
      values[0] = api_prop_float_get(ptr, prop);
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
    values[0] = api_prop_float_get(ptr, prop);
  }
  else if (fprop->getarray) {
    fprop->getarray(ptr, values);
  }
  else if (fprop->getarray_ex) {
    fprop->getarray_ex(ptr, prop, values);
  }
  else {
    api_prop_float_get_default_array_values(ptr, fprop, values);
  }
}

void api_prop_float_get_array_range(ApiPtr *ptr, ApiProp *prop, float values[2])
{
  const int array_len = api_prop_array_length(ptr, prop);

  if (array_len <= 0) {
    values[0] = 0.0f;
    values[1] = 0.0f;
  }
  else if (array_len == 1) {
    api_prop_float_get_array(ptr, prop, values);
    values[1] = values[0];
  }
  else {
    float arr_stack[32];
    float *arr;
    int i;

    if (array_len > 32) {
      arr = mem_mallocn(sizeof(float) * array_len, __func__);
    }
    else {
      arr = arr_stack;
    }

    api_prop_float_get_array(ptr, prop, arr);
    values[0] = values[1] = arr[0];
    for (i = 1; i < array_len; i++) {
      values[0] = MIN2(values[0], arr[i]);
      values[1] = MAX2(values[1], arr[i]);
    }

    if (arr != arr_stack) {
      mem_freen(arr);
    }
  }
}

float api_prop_float_get_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  float tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_float_get_array(ptr, prop, tmp);
    return tmp[index];
  }
  float *tmparray, value;

  tmparray = mem_mallocn(sizeof(float) * len, __func__);
  api_prop_float_get_array(ptr, prop, tmparray);
  value = tmparray[index];
  mem_freen(tmparray);

  return value;
}

void api_prop_float_set_array(ApiPtr *ptr, ApiProp *prop, const float *values)
{
  ApiFloatProp *fprop = (ApiFloatProp *)prop;
  IdProp *idprop;
  int i;

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    lib_assert(idprop->len == api_prop_array_length(ptr, prop) ||
               (prop->flag & PROP_IDPROP));
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

    api_idprop_touch(idprop);
  }
  else if (prop->arraydimension == 0) {
    api_prop_float_set(ptr, prop, values[0]);
  }
  else if (fprop->setarray) {
    fprop->setarray(ptr, values);
  }
  else if (fprop->setarray_ex) {
    fprop->setarray_ex(ptr, prop, values);
  }
  else if (prop->flag & PROP_EDITABLE) {
    IdPropTemplate val = {0};
    IdProp *group;

    /* TODO: api_prop_float_clamp_array(ptr, prop, &value); */
    val.array.len = prop->totarraylength;
    val.array.type = IDP_FLOAT;

    group = api_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_New(IDP_ARRAY, &val, prop->id);
      IDP_AddToGroup(group, idprop);
      memcpy(IDP_Array(idprop), values, sizeof(float) * idprop->len);
    }
  }
}

void api_prop_float_set_index(ApiPtr *ptr, ApiProp *prop, int index, float value)
{
  float tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_prop_array_length(ptr, prop);

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_float_get_array(ptr, prop, tmp);
    tmp[index] = value;
    api_prop_float_set_array(ptr, prop, tmp);
  }
  else {
    float *tmparray;

    tmparray = mem_mallocn(sizeof(float) * len, __func__);
    api_prop_float_get_array(ptr, prop, tmparray);
    tmparray[index] = value;
    api_prop_float_set_array(ptr, prop, tmparray);
    mem_freen(tmparray);
  }
}

float api_prop_float_get_default(ApiPtr *UNUSED(ptr), ApiProp *prop)
{
  ApiFloatProp *fprop = (ApiFloatProp *)api_ensure_prop(prop);

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) == false);

  if (prop->magic != API_MAGIC) {
    const IdProp *idprop = (const IdProp *)prop;
    if (idprop->ui_data) {
      lib_assert(ELEM(idprop->type, IDP_FLOAT, IDP_DOUBLE));
      const IdPropUIDataFloat *ui_data = (const IdPropUIDataFloat *)idprop->ui_data;
      return (float)ui_data->default_value;
    }
  }

  return fprop->defaultvalue;
}

bool api_prop_float_set_default(ApiProp *prop, float value)
{
  if (prop->magic == API_MAGIC) {
    return false;
  }

  IdProp *idprop = (IdProp *)prop;
  lib_assert(idprop->type == IDP_FLOAT);

  IdPropUIDataFloat *ui_data = (IdPropUIDataFloat *)IDP_ui_data_ensure(idprop);
  ui_data->default_value = (double)value;
  return true;
}

void api_prop_float_get_default_array(ApiPtr *ptr, ApiProp *prop, float *values)
{
  ApiFloatProp *fprop = (ApiFloatProp *)api_ensure_prop(prop);

  lib_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);

  if (prop->magic != API_MAGIC) {
    int length = api_ensure_prop_array_length(ptr, prop);

    const IdProp *idprop = (const IdProp *)prop;
    if (idprop->ui_data) {
      lib_assert(idprop->type == IDP_ARRAY);
      lib_assert(ELEM(idprop->subtype, IDP_FLOAT, IDP_DOUBLE));
      const IdPropUIDataFloat *ui_data = (const IdPropUIDataFloat *)idprop->ui_data;
      api_prop_float_fill_default_array_values_double(ui_data->default_array,
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
    api_prop_float_get_default_array_values(ptr, fprop, values);
  }
}

float api_prop_float_get_default_index(ApiPtr *ptr, ApiProp *prop, int index)
{
  float tmp[API_MAX_ARRAY_LENGTH];
  int len = api_ensure_property_array_length(ptr, prop);

  lin_assert(api_prop_type(prop) == PROP_FLOAT);
  lib_assert(api_prop_array_check(prop) != false);
  lib_assert(index >= 0);
  lib_assert(index < len);

  if (len <= API_MAX_ARRAY_LENGTH) {
    api_prop_float_get_default_array(ptr, prop, tmp);
    return tmp[index];
  }
  float *tmparray, value;

  tmparray = mem_mallocn(sizeof(float) * len, __func__);
  api_prop_float_get_default_array(ptr, prop, tmparray);
  value = tmparray[index];
  mem_freen(tmparray);

  return value;
}

void api_prop_string_get(PointerRNA *ptr, PropertyRNA *prop, char *value)
{
  ApiStringProp *sprop = (StringPropertyRNA *)prop;
  IDProperty *idprop;

  lib_assert(RNA_property_type(prop) == PROP_STRING);

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

char *api_prop_string_get_alloc(
    ApiPtr *ptr, ApiProp *prop, char *fixedbuf, int fixedlen, int *r_len)
{
  char *buf;
  int length;

  lib_assert(api_prop_type(prop) == PROP_STRING);

  length = api_prop_string_length(ptr, prop);

  if (length + 1 < fixedlen) {
    buf = fixedbuf;
  }
  else {
    buf = mem_mallocn(sizeof(char) * (length + 1), __func__);
  }

#ifndef NDEBUG
  /* safety check to ensure the string is actually set */
  buf[length] = 255;
#endif

  api_prop_string_get(ptr, prop, buf);

#ifndef NDEBUG
  lib_assert(buf[length] == '\0');
#endif

  if (r_len) {
    *r_len = length;
  }

  return buf;
}

int api_prop_string_length(ApiPtr *ptr, ApiProp *prop)
{
  ApiStringProp *sprop = (ApiStringProp *)prop;
  IdProp *idprop;

  lib_assert(api_prop_type(prop) == PROP_STRING);

  if ((idprop = api_idprop_check(&prop, ptr))) {
    if (idprop->subtype == IDP_STRING_SUB_BYTE) {
      return idprop->len;
    }
#ifndef NDEBUG
    /* these _must_ stay in sync */
    lib_assert(strlen(IDP_String(idprop)) == idprop->len - 1);
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

void RNA_property_string_set(PointerRNA *ptr, PropertyRNA *prop, const char *value)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    /* both IDP_STRING_SUB_BYTE / IDP_STRING_SUB_UTF8 */
    IDP_AssignString(idprop, value, RNA_property_string_maxlength(prop) - 1);
    rna_idproperty_touch(idprop);
  }
  else if (sprop->set) {
    sprop->set(ptr, value); /* set function needs to clamp itself */
  }
  else if (sprop->set_ex) {
    sprop->set_ex(ptr, prop, value); /* set function needs to clamp itself */
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDProperty *group;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group,
                     IDP_NewString(value, prop->identifier, RNA_property_string_maxlength(prop)));
    }
  }
}

void RNA_property_string_set_bytes(PointerRNA *ptr, PropertyRNA *prop, const char *value, int len)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);
  BLI_assert(RNA_property_subtype(prop) == PROP_BYTESTRING);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDP_ResizeArray(idprop, len);
    memcpy(idprop->data.pointer, value, (size_t)len);

    rna_idproperty_touch(idprop);
  }
  else if (sprop->set) {
    /* XXX, should take length argument (currently not used). */
    sprop->set(ptr, value); /* set function needs to clamp itself */
  }
  else if (sprop->set_ex) {
    /* XXX, should take length argument (currently not used). */
    sprop->set_ex(ptr, prop, value); /* set function needs to clamp itself */
  }
  else if (prop->flag & PROP_EDITABLE) {
    IDProperty *group;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDPropertyTemplate val = {0};
      val.string.str = value;
      val.string.len = len;
      val.string.subtype = IDP_STRING_SUB_BYTE;
      IDP_AddToGroup(group, IDP_New(IDP_STRING, &val, prop->identifier));
    }
  }
}

void RNA_property_string_get_default(PropertyRNA *prop, char *value, const int max_len)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)rna_ensure_property(prop);

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      BLI_assert(idprop->type == IDP_STRING);
      const IDPropertyUIDataString *ui_data = (const IDPropertyUIDataString *)idprop->ui_data;
      BLI_strncpy(value, ui_data->default_value, max_len);
      return;
    }

    strcpy(value, "");
    return;
  }

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  strcpy(value, sprop->defaultvalue);
}

char *RNA_property_string_get_default_alloc(
    PointerRNA *ptr, PropertyRNA *prop, char *fixedbuf, int fixedlen, int *r_len)
{
  char *buf;
  int length;

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  length = RNA_property_string_default_length(ptr, prop);

  if (length + 1 < fixedlen) {
    buf = fixedbuf;
  }
  else {
    buf = MEM_callocN(sizeof(char) * (length + 1), __func__);
  }

  RNA_property_string_get_default(prop, buf, length + 1);

  if (r_len) {
    *r_len = length;
  }

  return buf;
}

int RNA_property_string_default_length(PointerRNA *UNUSED(ptr), PropertyRNA *prop)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)rna_ensure_property(prop);

  if (prop->magic != RNA_MAGIC) {
    const IDProperty *idprop = (const IDProperty *)prop;
    if (idprop->ui_data) {
      BLI_assert(idprop->type == IDP_STRING);
      const IDPropertyUIDataString *ui_data = (const IDPropertyUIDataString *)idprop->ui_data;
      if (ui_data->default_value != NULL) {
        return strlen(ui_data->default_value);
      }
    }

    return 0;
  }

  BLI_assert(RNA_property_type(prop) == PROP_STRING);

  return strlen(sprop->defaultvalue);
}

int RNA_property_enum_get(PointerRNA *ptr, PropertyRNA *prop)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_ENUM);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    return IDP_Int(idprop);
  }
  if (eprop->get) {
    return eprop->get(ptr);
  }
  if (eprop->get_ex) {
    return eprop->get_ex(ptr, prop);
  }
  return eprop->defaultvalue;
}

void RNA_property_enum_set(PointerRNA *ptr, PropertyRNA *prop, int value)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_ENUM);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDP_Int(idprop) = value;
    rna_idproperty_touch(idprop);
  }
  else if (eprop->set) {
    eprop->set(ptr, value);
  }
  else if (eprop->set_ex) {
    eprop->set_ex(ptr, prop, value);
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

int RNA_property_enum_get_default(PointerRNA *UNUSED(ptr), PropertyRNA *prop)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_ENUM);

  return eprop->defaultvalue;
}

int RNA_property_enum_step(
    const bContext *C, PointerRNA *ptr, PropertyRNA *prop, int from_value, int step)
{
  const EnumPropertyItem *item_array;
  int totitem;
  bool free;
  int result_value = from_value;
  int i, i_init;
  int single_step = (step < 0) ? -1 : 1;
  int step_tot = 0;

  RNA_property_enum_items((bContext *)C, ptr, prop, &item_array, &totitem, &free);
  i = RNA_enum_from_value(item_array, from_value);
  i_init = i;

  do {
    i = mod_i(i + single_step, totitem);
    if (item_array[i].identifier[0]) {
      step_tot += single_step;
    }
  } while ((i != i_init) && (step_tot != step));

  if (i != i_init) {
    result_value = item_array[i].value;
  }

  if (free) {
    MEM_freeN((void *)item_array);
  }

  return result_value;
}

PointerRNA RNA_property_pointer_get(PointerRNA *ptr, PropertyRNA *prop)
{
  PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
  IDProperty *idprop;

  static ThreadMutex lock = BLI_MUTEX_INITIALIZER;

  BLI_assert(RNA_property_type(prop) == PROP_POINTER);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    pprop = (PointerPropertyRNA *)prop;

    if (RNA_struct_is_ID(pprop->type)) {
      return rna_pointer_inherit_refine(ptr, pprop->type, IDP_Id(idprop));
    }

    /* for groups, data is idprop itself */
    if (pprop->type_fn) {
      return rna_pointer_inherit_refine(ptr, pprop->type_fn(ptr), idprop);
    }
    return rna_pointer_inherit_refine(ptr, pprop->type, idprop);
  }
  if (pprop->get) {
    return pprop->get(ptr);
  }
  if (prop->flag & PROP_IDPROPERTY) {
    /* NOTE: While creating/writing data in an accessor is really bad design-wise, this is
     * currently very difficult to avoid in that case. So a global mutex is used to keep ensuring
     * thread safety. */
    BLI_mutex_lock(&lock);
    /* NOTE: We do not need to check again for existence of the pointer after locking here, since
     * this is also done in #RNA_property_pointer_add itself. */
    RNA_property_pointer_add(ptr, prop);
    BLI_mutex_unlock(&lock);
    return RNA_property_pointer_get(ptr, prop);
  }
  return PointerRNA_NULL;
}

void RNA_property_pointer_set(PointerRNA *ptr,
                              PropertyRNA *prop,
                              PointerRNA ptr_value,
                              ReportList *reports)
{
  /* Detect IDProperty and retrieve the actual PropertyRNA pointer before cast. */
  IDProperty *idprop = rna_idproperty_check(&prop, ptr);

  PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
  BLI_assert(RNA_property_type(prop) == PROP_POINTER);

  /* Check types. */
  if (pprop->set != NULL) {
    /* Assigning to a real RNA property. */
    if (ptr_value.type != NULL && !RNA_struct_is_a(ptr_value.type, pprop->type)) {
      BKE_reportf(reports,
                  RPT_ERROR,
                  "%s: expected %s type, not %s",
                  __func__,
                  pprop->type->identifier,
                  ptr_value.type->identifier);
      return;
    }
  }
  else {
    /* Assigning to an IDProperty disguised as RNA one. */
    if (ptr_value.type != NULL && !RNA_struct_is_a(ptr_value.type, &RNA_ID)) {
      BKE_reportf(reports,
                  RPT_ERROR,
                  "%s: expected ID type, not %s",
                  __func__,
                  ptr_value.type->identifier);
      return;
    }
  }

  /* We got an existing IDProperty. */
  if (idprop != NULL) {
    /* Not-yet-defined ID IDProps have an IDP_GROUP type, not an IDP_ID one - because of reasons?
     * XXX This has to be investigated fully - there might be a good reason for it, but off hands
     * this seems really weird... */
    if (idprop->type == IDP_ID) {
      IDP_AssignID(idprop, ptr_value.data, 0);
      rna_idproperty_touch(idprop);
    }
    else {
      BLI_assert(idprop->type == IDP_GROUP);

      IDPropertyTemplate val = {.id = ptr_value.data};
      IDProperty *group = RNA_struct_idprops(ptr, true);
      BLI_assert(group != NULL);

      IDP_ReplaceInGroup_ex(group, IDP_New(IDP_ID, &val, idprop->name), idprop);
    }
  }
  /* RNA property. */
  else if (pprop->set) {
    if (!((prop->flag & PROP_NEVER_NULL) && ptr_value.data == NULL) &&
        !((prop->flag & PROP_ID_SELF_CHECK) && ptr->owner_id == ptr_value.owner_id)) {
      pprop->set(ptr, ptr_value, reports);
    }
  }
  /* IDProperty disguised as RNA property (and not yet defined in ptr). */
  else if (prop->flag & PROP_EDITABLE) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    val.id = ptr_value.data;

    group = RNA_struct_idprops(ptr, true);
    if (group) {
      IDP_ReplaceInGroup(group, IDP_New(IDP_ID, &val, prop->identifier));
    }
  }
}

PointerRNA RNA_property_pointer_get_default(PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop))
{
  // PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;

  // BLI_assert(RNA_property_type(prop) == PROP_POINTER);

  return PointerRNA_NULL; /* FIXME: there has to be a way... */
}

void RNA_property_pointer_add(PointerRNA *ptr, PropertyRNA *prop)
{
  // IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_POINTER);

  if ((/*idprop=*/rna_idproperty_check(&prop, ptr))) {
    /* already exists */
  }
  else if (prop->flag & PROP_IDPROPERTY) {
    IDPropertyTemplate val = {0};
    IDProperty *group;

    val.i = 0;

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      IDP_AddToGroup(group, IDP_New(IDP_GROUP, &val, prop->identifier));
    }
  }
  else {
    printf("%s %s.%s: only supported for id properties.\n",
           __func__,
           ptr->type->identifier,
           prop->identifier);
  }
}

void RNA_property_pointer_remove(PointerRNA *ptr, PropertyRNA *prop)
{
  IDProperty *idprop, *group;

  BLI_assert(RNA_property_type(prop) == PROP_POINTER);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    group = RNA_struct_idprops(ptr, 0);

    if (group) {
      IDP_FreeFromGroup(group, idprop);
    }
  }
  else {
    printf("%s %s.%s: only supported for id properties.\n",
           __func__,
           ptr->type->identifier,
           prop->identifier);
  }
}

static void rna_property_collection_get_idp(CollectionPropertyIterator *iter)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)iter->prop;

  iter->ptr.data = rna_iterator_array_get(iter);
  iter->ptr.type = cprop->item_type;
  rna_pointer_inherit_id(cprop->item_type, &iter->parent, &iter->ptr);
}

void RNA_property_collection_begin(PointerRNA *ptr,
                                   PropertyRNA *prop,
                                   CollectionPropertyIterator *iter)
{
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  memset(iter, 0, sizeof(*iter));

  if ((idprop = rna_idproperty_check(&prop, ptr)) || (prop->flag & PROP_IDPROPERTY)) {
    iter->parent = *ptr;
    iter->prop = prop;

    if (idprop) {
      rna_iterator_array_begin(
          iter, IDP_IDPArray(idprop), sizeof(IDProperty), idprop->len, 0, NULL);
    }
    else {
      rna_iterator_array_begin(iter, NULL, sizeof(IDProperty), 0, 0, NULL);
    }

    if (iter->valid) {
      rna_property_collection_get_idp(iter);
    }

    iter->idprop = 1;
  }
  else {
    CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
    cprop->begin(iter, ptr);
  }
}

void RNA_property_collection_next(CollectionPropertyIterator *iter)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(iter->prop);

  if (iter->idprop) {
    rna_iterator_array_next(iter);

    if (iter->valid) {
      rna_property_collection_get_idp(iter);
    }
  }
  else {
    cprop->next(iter);
  }
}

void RNA_property_collection_skip(CollectionPropertyIterator *iter, int num)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(iter->prop);
  int i;

  if (num > 1 && (iter->idprop || (cprop->property.flag_internal & PROP_INTERN_RAW_ARRAY))) {
    /* fast skip for array */
    ArrayIterator *internal = &iter->internal.array;

    if (!internal->skip) {
      internal->ptr += internal->itemsize * (num - 1);
      iter->valid = (internal->ptr < internal->endptr);
      if (iter->valid) {
        RNA_property_collection_next(iter);
      }
      return;
    }
  }

  /* slow iteration otherwise */
  for (i = 0; i < num && iter->valid; i++) {
    RNA_property_collection_next(iter);
  }
}

void RNA_property_collection_end(CollectionPropertyIterator *iter)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(iter->prop);

  if (iter->idprop) {
    rna_iterator_array_end(iter);
  }
  else {
    cprop->end(iter);
  }
}

int RNA_property_collection_length(PointerRNA *ptr, PropertyRNA *prop)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    return idprop->len;
  }
  if (cprop->length) {
    return cprop->length(ptr);
  }
  CollectionPropertyIterator iter;
  int length = 0;

  RNA_property_collection_begin(ptr, prop, &iter);
  for (; iter.valid; RNA_property_collection_next(&iter)) {
    length++;
  }
  RNA_property_collection_end(&iter);

  return length;
}

bool RNA_property_collection_is_empty(PointerRNA *ptr, PropertyRNA *prop)
{
  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);
  CollectionPropertyIterator iter;
  RNA_property_collection_begin(ptr, prop, &iter);
  bool test = iter.valid;
  RNA_property_collection_end(&iter);
  return !test;
}

/* This helper checks whether given collection property itself is editable (we only currently
 * support a limited set of operations, insertion of new items, and re-ordering of those new items
 * exclusively). */
static bool property_collection_liboverride_editable(PointerRNA *ptr,
                                                     PropertyRNA *prop,
                                                     bool *r_is_liboverride)
{
  ID *id = ptr->owner_id;
  if (id == NULL) {
    *r_is_liboverride = false;
    return true;
  }

  const bool is_liboverride = *r_is_liboverride = ID_IS_OVERRIDE_LIBRARY(id);

  if (!is_liboverride) {
    /* We return True also for linked data, as it allows tricks like py scripts 'overriding' data
     * of those. */
    return true;
  }

  if (!RNA_property_overridable_get(ptr, prop)) {
    return false;
  }

  if (prop->magic != RNA_MAGIC || (prop->flag & PROP_IDPROPERTY) == 0) {
    /* Insertion and such not supported for pure IDProperties for now, nor for pure RNA/DNA ones.
     */
    return false;
  }
  if ((prop->flag_override & PROPOVERRIDE_LIBRARY_INSERTION) == 0) {
    return false;
  }

  /* No more checks to do, this collections is overridable. */
  return true;
}

void RNA_property_collection_add(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr)
{
  IDProperty *idprop;
  /* CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop; */

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  bool is_liboverride;
  if (!property_collection_liboverride_editable(ptr, prop, &is_liboverride)) {
    if (r_ptr) {
      memset(r_ptr, 0, sizeof(*r_ptr));
    }
    return;
  }

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDPropertyTemplate val = {0};
    IDProperty *item;

    item = IDP_New(IDP_GROUP, &val, "");
    if (is_liboverride) {
      item->flag |= IDP_FLAG_OVERRIDELIBRARY_LOCAL;
    }
    IDP_AppendArray(idprop, item);
    /* IDP_AppendArray does a shallow copy (memcpy), only free memory. */
    // IDP_FreePropertyContent(item);
    MEM_freeN(item);
    rna_idproperty_touch(idprop);
  }
  else if (prop->flag & PROP_IDPROPERTY) {
    IDProperty *group, *item;
    IDPropertyTemplate val = {0};

    group = RNA_struct_idprops(ptr, 1);
    if (group) {
      idprop = IDP_NewIDPArray(prop->identifier);
      IDP_AddToGroup(group, idprop);

      item = IDP_New(IDP_GROUP, &val, "");
      if (is_liboverride) {
        item->flag |= IDP_FLAG_OVERRIDELIBRARY_LOCAL;
      }
      IDP_AppendArray(idprop, item);
      /* IDP_AppendArray does a shallow copy (memcpy), only free memory */
      /* IDP_FreePropertyContent(item); */
      MEM_freeN(item);
    }
  }

  /* py api calls directly */
#if 0
  else if (cprop->add) {
    if (!(cprop->add->flag & FUNC_USE_CONTEXT)) { /* XXX check for this somewhere else */
      ParameterList params;
      RNA_parameter_list_create(&params, ptr, cprop->add);
      RNA_function_call(NULL, NULL, ptr, cprop->add, &params);
      RNA_parameter_list_free(&params);
    }
  }
#  if 0
  else {
    printf("%s %s.%s: not implemented for this property.\n",
           __func__,
           ptr->type->identifier,
           prop->identifier);
  }
#  endif
#endif

  if (r_ptr) {
    if (idprop) {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;

      r_ptr->data = IDP_GetIndexArray(idprop, idprop->len - 1);
      r_ptr->type = cprop->item_type;
      rna_pointer_inherit_id(NULL, ptr, r_ptr);
    }
    else {
      memset(r_ptr, 0, sizeof(*r_ptr));
    }
  }
}

bool RNA_property_collection_remove(PointerRNA *ptr, PropertyRNA *prop, int key)
{
  IDProperty *idprop;
  /*  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop; */

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  bool is_liboverride;
  if (!property_collection_liboverride_editable(ptr, prop, &is_liboverride)) {
    return false;
  }

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDProperty tmp, *array;
    int len;

    len = idprop->len;
    array = IDP_IDPArray(idprop);

    if (key >= 0 && key < len) {
      if (is_liboverride && (array[key].flag & IDP_FLAG_OVERRIDELIBRARY_LOCAL) == 0) {
        /* We can only remove items that we actually inserted in the local override. */
        return false;
      }

      if (key + 1 < len) {
        /* move element to be removed to the back */
        memcpy(&tmp, &array[key], sizeof(IDProperty));
        memmove(array + key, array + key + 1, sizeof(IDProperty) * (len - (key + 1)));
        memcpy(&array[len - 1], &tmp, sizeof(IDProperty));
      }

      IDP_ResizeIDPArray(idprop, len - 1);
    }

    return true;
  }
  if (prop->flag & PROP_IDPROPERTY) {
    return true;
  }

  /* py api calls directly */
#if 0
  else if (cprop->remove) {
    if (!(cprop->remove->flag & FUNC_USE_CONTEXT)) { /* XXX check for this somewhere else */
      ParameterList params;
      RNA_parameter_list_create(&params, ptr, cprop->remove);
      RNA_function_call(NULL, NULL, ptr, cprop->remove, &params);
      RNA_parameter_list_free(&params);
    }

    return false;
  }
#  if 0
  else {
    printf("%s %s.%s: only supported for id properties.\n",
           __func__,
           ptr->type->identifier,
           prop->identifier);
  }
#  endif
#endif
  return false;
}

bool RNA_property_collection_move(PointerRNA *ptr, PropertyRNA *prop, int key, int pos)
{
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  bool is_liboverride;
  if (!property_collection_liboverride_editable(ptr, prop, &is_liboverride)) {
    return false;
  }

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    IDProperty tmp, *array;
    int len;

    len = idprop->len;
    array = IDP_IDPArray(idprop);

    if (key >= 0 && key < len && pos >= 0 && pos < len && key != pos) {
      if (is_liboverride && (array[key].flag & IDP_FLAG_OVERRIDELIBRARY_LOCAL) == 0) {
        /* We can only move items that we actually inserted in the local override. */
        return false;
      }

      memcpy(&tmp, &array[key], sizeof(IDProperty));
      if (pos < key) {
        memmove(array + pos + 1, array + pos, sizeof(IDProperty) * (key - pos));
      }
      else {
        memmove(array + key, array + key + 1, sizeof(IDProperty) * (pos - key));
      }
      memcpy(&array[pos], &tmp, sizeof(IDProperty));
    }

    return true;
  }
  if (prop->flag & PROP_IDPROPERTY) {
    return true;
  }

  return false;
}

void RNA_property_collection_clear(PointerRNA *ptr, PropertyRNA *prop)
{
  IDProperty *idprop;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  bool is_liboverride;
  if (!property_collection_liboverride_editable(ptr, prop, &is_liboverride)) {
    return;
  }

  if ((idprop = rna_idproperty_check(&prop, ptr))) {
    if (is_liboverride) {
      /* We can only move items that we actually inserted in the local override. */
      int len = idprop->len;
      IDProperty tmp, *array = IDP_IDPArray(idprop);
      for (int i = 0; i < len; i++) {
        if ((array[i].flag & IDP_FLAG_OVERRIDELIBRARY_LOCAL) != 0) {
          memcpy(&tmp, &array[i], sizeof(IDProperty));
          memmove(array + i, array + i + 1, sizeof(IDProperty) * (len - (i + 1)));
          memcpy(&array[len - 1], &tmp, sizeof(IDProperty));
          IDP_ResizeIDPArray(idprop, --len);
          i--;
        }
      }
    }
    else {
      IDP_ResizeIDPArray(idprop, 0);
    }
    rna_idproperty_touch(idprop);
  }
}

int RNA_property_collection_lookup_index(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *t_ptr)
{
  CollectionPropertyIterator iter;
  int index = 0;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  RNA_property_collection_begin(ptr, prop, &iter);
  for (index = 0; iter.valid; RNA_property_collection_next(&iter), index++) {
    if (iter.ptr.data == t_ptr->data) {
      break;
    }
  }
  RNA_property_collection_end(&iter);

  /* did we find it? */
  if (iter.valid) {
    return index;
  }
  return -1;
}

int RNA_property_collection_lookup_int(PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       int key,
                                       PointerRNA *r_ptr)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  if (cprop->lookupint) {
    /* we have a callback defined, use it */
    return cprop->lookupint(ptr, key, r_ptr);
  }
  /* no callback defined, just iterate and find the nth item */
  CollectionPropertyIterator iter;
  int i;

  RNA_property_collection_begin(ptr, prop, &iter);
  for (i = 0; iter.valid; RNA_property_collection_next(&iter), i++) {
    if (i == key) {
      *r_ptr = iter.ptr;
      break;
    }
  }
  RNA_property_collection_end(&iter);

  if (!iter.valid) {
    memset(r_ptr, 0, sizeof(*r_ptr));
  }

  return iter.valid;
}

int RNA_property_collection_lookup_string_index(
    PointerRNA *ptr, PropertyRNA *prop, const char *key, PointerRNA *r_ptr, int *r_index)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  if (cprop->lookupstring) {
    /* we have a callback defined, use it */
    return cprop->lookupstring(ptr, key, r_ptr);
  }
  /* no callback defined, compare with name properties if they exist */
  CollectionPropertyIterator iter;
  PropertyRNA *nameprop;
  char name[256], *nameptr;
  int found = 0;
  int keylen = strlen(key);
  int namelen;
  int index = 0;

  RNA_property_collection_begin(ptr, prop, &iter);
  for (; iter.valid; RNA_property_collection_next(&iter), index++) {
    if (iter.ptr.data && iter.ptr.type->nameproperty) {
      nameprop = iter.ptr.type->nameproperty;

      nameptr = RNA_property_string_get_alloc(&iter.ptr, nameprop, name, sizeof(name), &namelen);

      if ((keylen == namelen) && STREQ(nameptr, key)) {
        *r_ptr = iter.ptr;
        found = 1;
      }

      if ((char *)&name != nameptr) {
        MEM_freeN(nameptr);
      }

      if (found) {
        break;
      }
    }
  }
  RNA_property_collection_end(&iter);

  if (!iter.valid) {
    memset(r_ptr, 0, sizeof(*r_ptr));
    *r_index = -1;
  }
  else {
    *r_index = index;
  }

  return iter.valid;
}

int RNA_property_collection_lookup_string(PointerRNA *ptr,
                                          PropertyRNA *prop,
                                          const char *key,
                                          PointerRNA *r_ptr)
{
  int index;
  return RNA_property_collection_lookup_string_index(ptr, prop, key, r_ptr, &index);
}

int RNA_property_collection_assign_int(PointerRNA *ptr,
                                       PropertyRNA *prop,
                                       const int key,
                                       const PointerRNA *assign_ptr)
{
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)rna_ensure_property(prop);

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  if (cprop->assignint) {
    /* we have a callback defined, use it */
    return cprop->assignint(ptr, key, assign_ptr);
  }

  return 0;
}

bool RNA_property_collection_type_get(PointerRNA *ptr, PropertyRNA *prop, PointerRNA *r_ptr)
{
  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  *r_ptr = *ptr;
  return ((r_ptr->type = rna_ensure_property(prop)->srna) ? 1 : 0);
}

int RNA_property_collection_raw_array(PointerRNA *ptr,
                                      PropertyRNA *prop,
                                      PropertyRNA *itemprop,
                                      RawArray *array)
{
  CollectionPropertyIterator iter;
  ArrayIterator *internal;
  char *arrayp;

  BLI_assert(RNA_property_type(prop) == PROP_COLLECTION);

  if (!(prop->flag_internal & PROP_INTERN_RAW_ARRAY) ||
      !(itemprop->flag_internal & PROP_INTERN_RAW_ACCESS)) {
    return 0;
  }

  RNA_property_collection_begin(ptr, prop, &iter);

  if (iter.valid) {
    /* get data from array iterator and item property */
    internal = &iter.internal.array;
    arrayp = (iter.valid) ? iter.ptr.data : NULL;

    if (internal->skip || !RNA_property_editable(&iter.ptr, itemprop)) {
      /* we might skip some items, so it's not a proper array */
      RNA_property_collection_end(&iter);
      return 0;
    }

    array->array = arrayp + itemprop->rawoffset;
    array->stride = internal->itemsize;
    array->len = ((char *)internal->endptr - arrayp) / internal->itemsize;
    array->type = itemprop->rawtype;
  }
  else {
    memset(array, 0, sizeof(RawArray));
  }

  RNA_property_collection_end(&iter);

  return 1;
}

#define RAW_GET(dtype, var, raw, a) \
  { \
    switch (raw.type) { \
      case PROP_RAW_CHAR: \
        var = (dtype)((char *)raw.array)[a]; \
        break; \
      case PROP_RAW_SHORT: \
        var = (dtype)((short *)raw.array)[a]; \
        break; \
      case PROP_RAW_INT: \
        var = (dtype)((int *)raw.array)[a]; \
        break; \
      case PROP_RAW_BOOLEAN: \
        var = (dtype)((bool *)raw.array)[a]; \
        break; \
      case PROP_RAW_FLOAT: \
        var = (dtype)((float *)raw.array)[a]; \
        break; \
      case PROP_RAW_DOUBLE: \
        var = (dtype)((double *)raw.array)[a]; \
        break; \
      default: \
        var = (dtype)0; \
    } \
  } \
  (void)0

#define RAW_SET(dtype, raw, a, var) \
  { \
    switch (raw.type) { \
      case PROP_RAW_CHAR: \
        ((char *)raw.array)[a] = (char)var; \
        break; \
      case PROP_RAW_SHORT: \
        ((short *)raw.array)[a] = (short)var; \
        break; \
      case PROP_RAW_INT: \
        ((int *)raw.array)[a] = (int)var; \
        break; \
      case PROP_RAW_BOOLEAN: \
        ((bool *)raw.array)[a] = (bool)var; \
        break; \
      case PROP_RAW_FLOAT: \
        ((float *)raw.array)[a] = (float)var; \
        break; \
      case PROP_RAW_DOUBLE: \
        ((double *)raw.array)[a] = (double)var; \
        break; \
      default: \
        break; \
    } \
  } \
  (void)0

int RNA_raw_type_sizeof(RawPropertyType type)
{
  switch (type) {
    case PROP_RAW_CHAR:
      return sizeof(char);
    case PROP_RAW_SHORT:
      return sizeof(short);
    case PROP_RAW_INT:
      return sizeof(int);
    case PROP_RAW_BOOLEAN:
      return sizeof(bool);
    case PROP_RAW_FLOAT:
      return sizeof(float);
    case PROP_RAW_DOUBLE:
      return sizeof(double);
    default:
      return 0;
  }
}

static int rna_property_array_length_all_dimensions(PointerRNA *ptr, PropertyRNA *prop)
{
  int i, len[RNA_MAX_ARRAY_DIMENSION];
  const int dim = RNA_property_array_dimension(ptr, prop, len);
  int size;

  if (dim == 0) {
    return 0;
  }

  for (size = 1, i = 0; i < dim; i++) {
    size *= len[i];
  }

  return size;
}

static int rna_raw_access(ReportList *reports,
                          PointerRNA *ptr,
                          PropertyRNA *prop,
                          const char *propname,
                          void *inarray,
                          RawPropertyType intype,
                          int inlen,
                          int set)
{
  StructRNA *ptype;
  PointerRNA itemptr_base;
  PropertyRNA *itemprop, *iprop;
  PropertyType itemtype = 0;
  RawArray in;
  int itemlen = 0;

  /* initialize in array, stride assumed 0 in following code */
  in.array = inarray;
  in.type = intype;
  in.len = inlen;
  in.stride = 0;

  ptype = RNA_property_pointer_type(ptr, prop);

  /* try to get item property pointer */
  RNA_pointer_create(NULL, ptype, NULL, &itemptr_base);
  itemprop = RNA_struct_find_property(&itemptr_base, propname);

  if (itemprop) {
    /* we have item property pointer */
    RawArray out;

    /* check type */
    itemtype = RNA_property_type(itemprop);

    if (!ELEM(itemtype, PROP_BOOLEAN, PROP_INT, PROP_FLOAT, PROP_ENUM)) {
      BKE_report(reports, RPT_ERROR, "Only boolean, int float and enum properties supported");
      return 0;
    }

    /* check item array */
    itemlen = RNA_property_array_length(&itemptr_base, itemprop);

    /* dynamic array? need to get length per item */
    if (itemprop->getlength) {
      itemprop = NULL;
    }
    /* try to access as raw array */
    else if (RNA_property_collection_raw_array(ptr, prop, itemprop, &out)) {
      int arraylen = (itemlen == 0) ? 1 : itemlen;
      if (in.len != arraylen * out.len) {
        BKE_reportf(reports,
                    RPT_ERROR,
                    "Array length mismatch (expected %d, got %d)",
                    out.len * arraylen,
                    in.len);
        return 0;
      }

      /* matching raw types */
      if (out.type == in.type) {
        void *inp = in.array;
        void *outp = out.array;
        int a, size;

        size = RNA_raw_type_sizeof(out.type) * arraylen;

        for (a = 0; a < out.len; a++) {
          if (set) {
            memcpy(outp, inp, size);
          }
          else {
            memcpy(inp, outp, size);
          }

          inp = (char *)inp + size;
          outp = (char *)outp + out.stride;
        }

        return 1;
      }

      /* Could also be faster with non-matching types,
       * for now we just do slower loop. */
    }
  }

  {
    void *tmparray = NULL;
    int tmplen = 0;
    int err = 0, j, a = 0;
    int needconv = 1;

    if (((itemtype == PROP_INT) && (in.type == PROP_RAW_INT)) ||
        ((itemtype == PROP_BOOLEAN) && (in.type == PROP_RAW_BOOLEAN)) ||
        ((itemtype == PROP_FLOAT) && (in.type == PROP_RAW_FLOAT))) {
      /* avoid creating temporary buffer if the data type match */
      needconv = 0;
    }
    /* no item property pointer, can still be id property, or
     * property of a type derived from the collection pointer type */
    RNA_PROP_BEGIN (ptr, itemptr, prop) {
      if (itemptr.data) {
        if (itemprop) {
          /* we got the property already */
          iprop = itemprop;
        }
        else {
          /* not yet, look it up and verify if it is valid */
          iprop = RNA_struct_find_property(&itemptr, propname);

          if (iprop) {
            itemlen = rna_property_array_length_all_dimensions(&itemptr, iprop);
            itemtype = RNA_property_type(iprop);
          }
          else {
            BKE_reportf(reports, RPT_ERROR, "Property named '%s' not found", propname);
            err = 1;
            break;
          }

          if (!ELEM(itemtype, PROP_BOOLEAN, PROP_INT, PROP_FLOAT)) {
            BKE_report(reports, RPT_ERROR, "Only boolean, int and float properties supported");
            err = 1;
            break;
          }
        }

        /* editable check */
        if (!set || RNA_property_editable(&itemptr, iprop)) {
          if (a + itemlen > in.len) {
            BKE_reportf(
                reports, RPT_ERROR, "Array length mismatch (got %d, expected more)", in.len);
            err = 1;
            break;
          }

          if (itemlen == 0) {
            /* handle conversions */
            if (set) {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  int b;
                  RAW_GET(bool, b, in, a);
                  RNA_property_boolean_set(&itemptr, iprop, b);
                  break;
                }
                case PROP_INT: {
                  int i;
                  RAW_GET(int, i, in, a);
                  RNA_property_int_set(&itemptr, iprop, i);
                  break;
                }
                case PROP_FLOAT: {
                  float f;
                  RAW_GET(float, f, in, a);
                  RNA_property_float_set(&itemptr, iprop, f);
                  break;
                }
                default:
                  break;
              }
            }
            else {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  int b = RNA_property_boolean_get(&itemptr, iprop);
                  RAW_SET(bool, in, a, b);
                  break;
                }
                case PROP_INT: {
                  int i = RNA_property_int_get(&itemptr, iprop);
                  RAW_SET(int, in, a, i);
                  break;
                }
                case PROP_FLOAT: {
                  float f = RNA_property_float_get(&itemptr, iprop);
                  RAW_SET(float, in, a, f);
                  break;
                }
                default:
                  break;
              }
            }
            a++;
          }
          else if (needconv == 1) {
            /* allocate temporary array if needed */
            if (tmparray && tmplen != itemlen) {
              MEM_freeN(tmparray);
              tmparray = NULL;
            }
            if (!tmparray) {
              tmparray = MEM_callocN(sizeof(float) * itemlen, "RNA tmparray");
              tmplen = itemlen;
            }

            /* handle conversions */
            if (set) {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_GET(bool, ((bool *)tmparray)[j], in, a);
                  }
                  RNA_property_boolean_set_array(&itemptr, iprop, tmparray);
                  break;
                }
                case PROP_INT: {
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_GET(int, ((int *)tmparray)[j], in, a);
                  }
                  RNA_property_int_set_array(&itemptr, iprop, tmparray);
                  break;
                }
                case PROP_FLOAT: {
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_GET(float, ((float *)tmparray)[j], in, a);
                  }
                  RNA_property_float_set_array(&itemptr, iprop, tmparray);
                  break;
                }
                default:
                  break;
              }
            }
            else {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  RNA_property_boolean_get_array(&itemptr, iprop, tmparray);
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_SET(int, in, a, ((bool *)tmparray)[j]);
                  }
                  break;
                }
                case PROP_INT: {
                  RNA_property_int_get_array(&itemptr, iprop, tmparray);
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_SET(int, in, a, ((int *)tmparray)[j]);
                  }
                  break;
                }
                case PROP_FLOAT: {
                  RNA_property_float_get_array(&itemptr, iprop, tmparray);
                  for (j = 0; j < itemlen; j++, a++) {
                    RAW_SET(float, in, a, ((float *)tmparray)[j]);
                  }
                  break;
                }
                default:
                  break;
              }
            }
          }
          else {
            if (set) {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  RNA_property_boolean_set_array(&itemptr, iprop, &((bool *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                case PROP_INT: {
                  RNA_property_int_set_array(&itemptr, iprop, &((int *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                case PROP_FLOAT: {
                  RNA_property_float_set_array(&itemptr, iprop, &((float *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                default:
                  break;
              }
            }
            else {
              switch (itemtype) {
                case PROP_BOOLEAN: {
                  RNA_property_boolean_get_array(&itemptr, iprop, &((bool *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                case PROP_INT: {
                  RNA_property_int_get_array(&itemptr, iprop, &((int *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                case PROP_FLOAT: {
                  RNA_property_float_get_array(&itemptr, iprop, &((float *)in.array)[a]);
                  a += itemlen;
                  break;
                }
                default:
                  break;
              }
            }
          }
        }
      }
    }
    RNA_PROP_END;

    if (tmparray) {
      MEM_freeN(tmparray);
    }

    return !err;
  }
}
