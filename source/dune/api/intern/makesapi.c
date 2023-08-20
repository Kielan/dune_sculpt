#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_string.h"
#include "lib_system.h" /* for 'BLI_system_backtrace' stub. */
#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"
#include "api_types.h"

#include "api_internal.h"

#ifdef _WIN32
#  ifndef snprintf
#    define snprintf _snprintf
#  endif
#endif

#include "log.h"

static LogRef LOG = {"api"};

/** Variable to control debug output of makesapi.
 * Sapidebug:
 * - 0 = no output, except errors
 * - 1 = detail action */
static int debugSAPI = 0;

/* stub for lib_abort() */
#ifndef NDEBUG
void lib_system_backtrace(FILE *fp)
{
  (void)fp;
}
#endif

/* Replace if different */
#define TMP_EXT ".tmp"

/* copied from lib_file_older */
#include <sys/stat.h>
static int file_older(const char *file1, const char *file2)
{
  struct stat st1, st2;
  if (debugSAPI > 0) {
    printf("compare: %s %s\n", file1, file2);
  }

  if (stat(file1, &st1)) {
    return 0;
  }
  if (stat(file2, &st2)) {
    return 0;
  }

  return (st1.st_mtime < st2.st_mtime);
}
static const char *makesapi_path = NULL;

/* forward declarations */
static void api_generate_static_param_prototypes(FILE *f,
                                                 ApiStruct *sapi,
                                                 ApiFnDef *dfn,
                                                 const char *name_override,
                                                 int close_prototype);

/* helpers */
#define WRITE_COMMA \
  { \
    if (!first) { \
      fprintf(f, ", "); \
    } \
    first = 0; \
  } \
  (void)0

#define WRITE_PARAM(param) \
  { \
    WRITE_COMMA; \
    fprintf(f, param); \
  } \
  (void)0

static int replace_if_different(const char *tmpfile, const char *dep_files[])
{
  /* return 0; */ /* use for testing had edited rna */
#define REN_IF_DIFF \
  { \
    FILE *file_test = fopen(orgfile, "rb"); \
    if (file_test) { \
      fclose(file_test); \
      if (fp_org) { \
        fclose(fp_org); \
      } \
      if (fp_new) { \
        fclose(fp_new); \
      } \
      if (remove(orgfile) != 0) { \
        LOG_ERROR(&LOG, "remove error (%s): \"%s\"", strerror(errno), orgfile); \
        return -1; \
      } \
    } \
  } \
  if (rename(tmpfile, orgfile) != 0) { \
    LOG_ERROR(&LOG, "rename error (%s): \"%s\" -> \"%s\"", strerror(errno), tmpfile, orgfile); \
    return -1; \
  } \
  remove(tmpfile); \
  return 1

  /* end REN_IF_DIFF */
  FILE *fp_new = NULL, *fp_org = NULL;
  int len_new, len_org;
  char *arr_new, *arr_org;
  int cmp;

  char orgfile[4096];

  strcpy(orgfile, tmpfile);
  orgfile[strlen(orgfile) - strlen(TMP_EXT)] = '\0'; /* strip '.tmp' */

  fp_org = fopen(orgfile, "rb");

  if (fp_org == NULL) {
    REN_IF_DIFF;
  }

  /* XXX, trick to work around dependency problem
   * assumes dep_files is in the same dir as makesrna.c, which is true for now. */
  if (1) {
    /* first check if makesrna.c is newer than generated files
     * for development on makesapi.c you may want to disable this */
    if (file_older(orgfile, __FILE__)) {
      REN_IF_DIFF;
    }

    if (file_older(orgfile, makesapi_path)) {
      REN_IF_DIFF;
    }

    /* now check if any files we depend on are newer than any generated files */
    if (dep_files) {
      int pass;
      for (pass = 0; dep_files[pass]; pass++) {
        const char from_path[4096] = __FILE__;
        char *p1, *p2;

        /* dir only */
        p1 = strrchr(from_path, '/');
        p2 = strrchr(from_path, '\\');
        strcpy((p1 > p2 ? p1 : p2) + 1, dep_files[pass]);
        /* account for build deps, if makesrna.c (this file) is newer */
        if (file_older(orgfile, from_path)) {
          REN_IF_DIFF;
        }
      }
    }
  }
  /* XXX end dep trick */
  fp_new = fopen(tmpfile, "rb");

  if (fp_new == NULL) {
    /* shouldn't happen, just to be safe */
    LOG_ERROR(&LOG, "open error: \"%s\"", tmpfile);
    fclose(fp_org);
    return -1;
  }

  fseek(fp_new, 0L, SEEK_END);
  len_new = ftell(fp_new);
  fseek(fp_new, 0L, SEEK_SET);
  fseek(fp_org, 0L, SEEK_END);
  len_org = ftell(fp_org);
  fseek(fp_org, 0L, SEEK_SET);

  if (len_new != len_org) {
    fclose(fp_new);
    fp_new = NULL;
    fclose(fp_org);
    fp_org = NULL;
    REN_IF_DIFF;
  }

  /* now compare the files... */
  arr_new = mem_mallocn(sizeof(char) * len_new, "api_cmp_file_new")
  arr_org = mem_mallocn(sizeof(char) * len_org, "api_cmp_file_org");

  if (fread(arr_new, sizeof(char), len_new, fp_new) != len_new) {
    LOG_ERROR(&LOG, "unable to read file %s for comparison.", tmpfile);
  }
  if (fread(arr_org, sizeof(char), len_org, fp_org) != len_org) {
    LOG_ERROR(&LOG, "unable to read file %s for comparison.", orgfile);
  }

  fclose(fp_new);
  fp_new = NULL;
  fclose(fp_org);
  fp_org = NULL;

  cmp = memcmp(arr_new, arr_org, len_new);

  mem_freen(arr_new);
  mem_freen(arr_org);

  if (cmp) {
    REN_IF_DIFF;
  }
  remove(tmpfile);
  return 0;

#undef REN_IF_DIFF
}

/* Helper to solve keyword problems with C/C++ */
static const char *api_safe_id(const char *id)
{
  if (STREQ(id, "default")) {
    return "default_value";
  }
  if (STREQ(id, "op")) {
    return "op_alue";
  }
  if (STREQ(id, "new")) {
    return "create";
  }
  if (STREQ(id, "co_return")) {
    /* MSVC2015, C++ uses for coroutines */
    return "coord_return";
  }

  return id;
}

/* Sorting */
static int cmp_struct(const void *a, const void *b)
{
  const ApiStruct *structa = *(const ApiStruct **)a;
  const ApiStruct *structb = *(const ApiStruct **)b;

  return strcmp(structa->id, structb->id);
}

static int cmp_prop(const void *a, const void *b)
{
  const ApiProp *propa = *(const ApiProp **)a;
  const ApiProp *propb = *(const ApiProp **)b;

  if (STREQ(propa->id, "api_type")) {
    return -1;
  }
  if (STREQ(propb->id, "api_type")) {
    return 1;
  }

  if (STREQ(propa->id, "name")) {
    return -1;
  }
  if (STREQ(propb->id, "name")) {
    return 1;
  }

  return strcmp(propa->name, propb->name);
}

static int cmp_def_struct(const void *a, const void *b)
{
  const ApiStructDef *dsa = *(const ApiStructDef **)a;
  const ApiStructDef *dsb = *(const ApiStructDef **)b;

  return cmp_struct(&dsa->sapi, &dsb->sapi);
}

static int cmp_def_prop(const void *a, const void *b)
{
  const ApiPropDef *dpa = *(const ApiPropDef **)a;
  const ApiPropDef *dpb = *(const ApiPropDef **)b;

  return cmp_prop(&dpa->prop, &dpb->prop);
}

static void api_sortlist(List *list, int (*cmp)(const void *, const void *))
{
  Link *link;
  void **array;
  int a, size;

  if (list->first == list->last) {
    return;
  }

  for (size = 0, link = list->first; link; link = link->next) {
    size++;
  }

  array = mem_mallocn(sizeof(void *) * size, "api_sortlist");
  for (a = 0, link = list->first; link; link = link->next, a++) {
    array[a] = link;
  }

  qsort(array, size, sizeof(void *), cmp);

  list->first = list->last = NULL;
  for (a = 0; a < size; a++) {
    link = array[a];
    link->next = link->prev = NULL;
    api_addtail(list, link);
  }

  mem_freen(array);
}

/* Preprocessing */

static void api_print_c_string(FILE *f, const char *str)
{
  static const char *escape[] = {
      "\''", "\"\"", "\??", "\\\\", "\aa", "\bb", "\ff", "\nn", "\rr", "\tt", "\vv", NULL};
  int i, j;

  if (!str) {
    fprintf(f, "NULL");
    return;
  }

  fprintf(f, "\"");
  for (i = 0; str[i]; i++) {
    for (j = 0; escape[j]; j++) {
      if (str[i] == escape[j][0]) {
        break;
      }
    }

    if (escape[j]) {
      fprintf(f, "\\%c", escape[j][1]);
    }
    else {
      fprintf(f, "%c", str[i]);
    }
  }
  fprintf(f, "\"");
}

static void api_print_data_get(FILE *f, ApiPropDef *dp)
{
  if (dp->typestructfromname && dp->yyprstructfromprop) {
    fprintf(f,
            "    %s *data = (%s *)(((%s *)ptr->data)->%s);\n",
            dp->typestructname,
            dp->typestructname,
            dp->typestructfromname,
            dp->typestructfromprop);
  } else {
    fprintf(f, "    %s *data = (%s *)(ptr->data);\n", dp->typestructname, dp->typestructname);
  }
}

static void api_print_id_get(FILE *f, ApiPropDef *UNUSED(dp))
{
  fprintf(f, "    Id *id = ptr->owner_id;\n");
}

static void api_construct_fn_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
}

static void api_construct_wrapper_fn_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  if (type == NULL || type[0] == '\0') {
    snprintf(buffer, size, "%s_%s", structname, propname);
  } else {
    snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
  }
}

void *api_alloc_from_buffer(const char *buffer, int buffer_len)
{
  ApiAllocDef *alloc = mem_callocn(sizeof(ApiAllocDef), "ApiAllocDef");
  alloc->mem = mem_mallocn(buffer_len, __func__);
  memcpy(alloc->mem, buffer, buffer_len);
  api_addtail(&ApiDef.allocs, alloc);
  return alloc->mem;
}

void *api_calloc(int buffer_len)
{
  ApiAllocDef *alloc = mem_callocn(sizeof(ApiAllocDef), "ApiAllocDef");
  alloc->mem = mem_callocn(buffer_len, __func__);
  rna_addtail(&ApiDef.allocs, alloc);
  return alloc->mem;
}

static char *api_alloc_fn_name(const char *structname,
                               const char *propname,
                               const char *type)
{
  char buffer[2048];
  api_construct_fn_name(buffer, sizeof(buffer), structname, propname, type);
  return api_alloc_from_buffer(buffer, strlen(buffer) + 1);
}

static ApiStruct *api_find_struct(const char *id)
{
  ApiStructDef *ds;

  for (ds = ApiDef.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->sapi->id, id)) {
      return ds->sapi;
    }
  }

  return NULL;
}

static const char *api_find_type(const char *type)
{
  ApiStructDef *ds;

  for (ds = ApiDef.structs.first; ds; ds = ds->cont.next) {
    if (ds->typesname && STREQ(ds->typesname, type)) {
      return ds->sapi->id;
    }
  }

  return NULL;
}

static const char *api_find_type(const char *type)
{
  ApiStructDef *ds;

  for (ds = ApiDef.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->sapi->id, type)) {
      return ds->typesname;
    }
  }

  return NULL;
}

static const char *api_type_type_name(ApiProp *prop)
{
  switch (prop->type) {
    case PROP_BOOL:
      return "bool";
    case PROP_INT:
      return "int";
    case PROP_ENUM: {
      EnumApiProp *eprop = (EnumApiProp *)prop;
      if (eprop->native_enum_type) {
        return eprop->native_enum_type;
      }
      return "int";
    }
    case PROP_FLOAT:
      return "float";
    case PROP_STRING:
      if (prop->flag & PROP_THICK_WRAP) {
        return "char *";
      }
      else {
        return "const char *";
      }
    default:
      return NULL;
  }
}

static const char *api_type_type(ApiProp *prop)
{
  const char *type;

  type = api_type_type_name(prop);

  if (type) {
    return type;
  }

  return "ApiPtr";
}

static const char *api_type_struct(ApiProp *prop)
{
  const char *type;

  type = api_type_type_name(prop);

  if (type) {
    return "";
  }

  return "struct ";
}

static const char *api_param_type_name(ApiProp *parm)
{
  const char *type;

  type = api_type_type_name(parm);

  if (type) {
    return type;
  }

  switch (parm->type) {
    case PROP_PTR: {
      PtrProp *pparm = (ApiPtrProp *)parm;

      if (parm->flag_param & PARM_APIPTR) {
        return "ApiPtr";
      }
      return api_find_types_type((const char *)pparm->type);
    }
    case PROP_COLLECTION: {
      return "CollectionList";
    }
    default:
      return "<error, no type specified>";
  }
}

static int api_enum_bitmask(ApiProp *prop)
{
  ApiEnumProp *eprop = (ApiEnumProp *)prop;
  int a, mask = 0;

  if (eprop->item) {
    for (a = 0; a < eprop->totitem; a++) {
      if (eprop->item[a].id[0]) {
        mask |= eprop->item[a].value;
      }
    }
  }

  return mask;
}

static int api_color_quantize(ApiProp *prop, ApiPropDef *dp)
{
  return ((prop->type == PROP_FLOAT) && (ELEM(prop->subtype, PROP_COLOR, PROP_COLOR_GAMMA)) &&
          (IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0));
}

/** Return the id for an enum which is defined in api_enum_items.h".
 *
 * Prevents expanding duplicate enums bloating the binary size */
static const char *api_enum_id_from_ptr(const EnumPropItem *item)
{
#define API_MAKESAPI
#define DEF_ENUM(id) \
  if (item == id) { \
    return STRINGIFY(id); \
  }
#include "api_enum_items.h"
#undef API_MAKESAPI
  return NULL;
}

static const char *api_fn_string(const void *fn)
{
  return (fn) ? (const char *)fn : "NULL";
}

static void api_float_print(FILE *f, float num)
{
  if (num == -FLT_MAX) {
    fprintf(f, "-FLT_MAX");
  }
  else if (num == FLT_MAX) {
    fprintf(f, "FLT_MAX");
  }
  else if ((fabsf(num) < (float)INT64_MAX) && ((int64_t)num == num)) {
    fprintf(f, "%.1ff", num);
  }
  else {
    fprintf(f, "%.10ff", num);
  }
}

static void api_int_print(FILE *f, int64_t num)
{
  if (num == INT_MIN) {
    fprintf(f, "INT_MIN");
  } else if (num == INT_MAX) {
    fprintf(f, "INT_MAX");
  } else if (num == INT64_MIN) {
    fprintf(f, "INT64_MIN");
  } else if (num == INT64_MAX) {
    fprintf(f, "INT64_MAX");
  } else if (num < INT_MIN || num > INT_MAX) {
    fprintf(f, "%" PRId64 "LL", num);
  } else {
    fprintf(f, "%d", (int)num);
  }
}

static char *api_def_prop_get_fn(
    FILE *f, ApiStruct *apistruct, ApiProp *prop, ApiPropDef *dp, const char *manualfn)
{
  char *fn;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typename) {
      LOG_ERROR(&LOG, "%s.%s has no valid types info.", sapi->id, prop->id);
      ApiDef.error = true;
      return NULL;
    }

    /* Type check. */
    if (dp->type && *dp->type) {

      if (prop->type == PROP_FLOAT) {
        if (IS_TYPE_FLOAT_COMPAT(dp->type) == 0) {
          /* Colors are an exception. these get translated. */
          if (prop->subtype != PROP_COLOR_GAMMA) {
            LOG_ERROR(&LOG,
                       "%s.%s is a '%s' but wrapped as type '%s'.",
                       api->id,
                       prop->id,
                       dp->type,
                       api_prop_typename(prop->type));
            ApiDef.error = true;
            return NULL;
          }
        }
      }
      else if (prop->type == PROP_BOOL) {
        if (IS_TYPE_BOOL_COMPAT(dp->type) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     sapi->id,
                     prop->id,
                     dp->type,
                     api_prop_typename(prop->type));
          ApiDef.error = true;
          return NULL;
        }
      }
      else if (ELEM(prop->type, PROP_INT, PROP_ENUM)) {
        if (IS_TYPE_INT_COMPAT(dp->type) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     sapi->id,
                     prop->id,
                     dp->type,
                     api_prop_typename(prop->type));
          ApiDef.error = true;
          return NULL;
        }
      }
    }

    /* Check log scale sliders for negative range. */
    if (prop->type == PROP_FLOAT) {
      ApiFloatProp *fprop = (ApiFloatProp *)prop;
      /* NOTE: UI_BTYPE_NUM_SLIDER can't have a softmin of zero. */
      if ((fprop->ui_scale_type == PROP_SCALE_LOG) && (fprop->hardmin < 0 || fprop->softmin < 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale < 0.", sapi->id, prop->id);
        DefRNA.error = true;
        return NULL;
      }
    }
    if (prop->type == PROP_INT) {
      ApiIntProp *iprop = (ApiIntProp *)prop;
      /* Only UI_BTYPE_NUM_SLIDER is implemented and that one can't have a softmin of zero. */
      if ((iprop->ui_scale_type == PROP_SCALE_LOG) &&
          (iprop->hardmin <= 0 || iprop->softmin <= 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale <= 0.", sapi->id, prop->id);
        ApiDef.error = true;
        return NULL;
      }
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "get");

  switch (prop->type) {
    case PROP_STRING: {
      ApiStringProp *sprop = (ApiStringProp *)prop;
      fprintf(f, "void %s(ApiPtr *ptr, char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropSubType subtype = prop->subtype;
        const char *string_copy_fn =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "lib_strncpy" :
                "lib_strncpy_utf8";

        api_print_data_get(f, dp);

        if (dp->typeptrlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(f, "    if (data->%s == NULL) {\n", dp->typesname);
          fprintf(f, "        *value = '\\0';\n");
          fprintf(f, "        return;\n");
          fprintf(f, "    }\n");
          fprintf(f,
                  "    %s(value, data->%s, strlen(data->%s) + 1);\n",
                  string_copy_fn,
                  dp->typesname,
                  dp->typesname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(value, data->%s, %d);\n",
                    string_copy_fn,
                    dp->typesname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(value, data->%s, sizeof(data->%s));\n",
                    string_copy_fn,
                    dp->typesname,
                    dp->typesname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_PTR: {
      fprintf(f, "ApiPtr %s(ApiPtr *ptr)\n", fn);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    return %s(ptr);\n", manualfn);
      }
      else {
        ApiPtrProp *pprop = (ApiPtrProp *)prop;
        api_print_data_get(f, dp);
        if (dp->typeptrlevel == 0) {
          fprintf(f,
                  "    return api_ptr_inherit_refine(ptr, &Api%s, &data->%s);\n",
                  (const char *)pprop->type,
                  dp->typesname);
        }
        else {
          fprintf(f,
                  "    return api_ptr_inherit_refine(ptr, &Api%s, data->%s);\n",
                  (const char *)pprop->type,
                  dp->typesname);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_COLLECTION: {
      ApiCollectionProp *cprop = (ApiCollectionProp *)prop;

      fprintf(f, "static ApiPtr %s(CollectionPropIter *iter)\n", fn);
      fprintf(f, "{\n");
      if (manualfn) {
        if (STR_ELEM(manualfn,
                     "api_iter_list_get",
                     "api_iter_array_get",
                     "api_iter_array_dereference_get")) {
          fprintf(f,
                  "    return api_ptr_inherit_refine(&iter->parent, &Api_%s, %s(iter));\n",
                  (cprop->item_type) ? (const char *)cprop->item_type : "UnknownType",
                  manualfn);
        }
        else {
          fprintf(f, "    return %s(iter);\n", manualfn);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(ApiPtr *ptr, %s values[])\n", fn, api_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(ApiPtr *ptr, %s values[%u])\n",
                  fn,
                  api_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    %s(ptr, values);\n", manualfn);
        }
        else {
          api_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfn = api_alloc_fn_name(
                sapi->id, api_safe_id(prop->id), "get_length");
            fprintf(f, "    unsigned int arraylen[API_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int i;\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfn);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            mem_freen(lenfn);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->typearraylength == 1) {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s & (",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, " << i)) != 0);\n");
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((&data->%s)[i]);\n",
                      api_type_type(prop),
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
            }
          }
          else {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s[i] & ",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, ") != 0);\n");
            }
            else if (api_color_quantize(prop, dp)) {
              fprintf(f,
                      "        values[i] = (%s)(data->%s[i] * (1.0f / 255.0f));\n",
                      api_type_type(prop),
                      dp->typesname);
            }
            else if (dp->type) {
              fprintf(f,
                      "        values[i] = (%s)%s(((%s *)data->%s)[i]);\n",
                      api_type_type(prop),
                      (dp->boolnegative) ? "!" : "",
                      dp->type,
                      dp->typesname);
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((data->%s)[i]);\n",
                      api_type_type(prop),
                      (dp->boolnegative) ? "!" : "",
                      dp->name);
            }
          }
          fprintf(f, "    }\n");
        }
        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "%s %s(ApiPtr *ptr)\n", rna_type_type(prop), fn);
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    return %s(ptr);\n", manualfn);
        }
        else {
          api_print_data_get(f, dp);
          if (prop->type == PROP_BOOL && dp->boolbit) {
            fprintf(
                f, "    return %s(((data->%s) & ", (dp->boolnegative) ? "!" : "", dp->dnaname);
            api_int_print(f, dp->boolbit);
            fprintf(f, ") != 0);\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    return ((data->%s) & ", dp->typesname);
            api_int_print(f, api_enum_bitmask(prop));
            fprintf(f, ");\n");
          }
          else {
            fprintf(f,
                    "    return (%s)%s(data->%s);\n",
                    api_type_type(prop),
                    (dp->boolnegative) ? "!" : "",
                    dp->typesname);
          }
        }

        fprintf(f, "}\n\n");
      }
      break;
  }

  return fn;
}

/* defined min/max variables to be used by api_clamp_value() */
static void api_clamp_value_range(FILE *f, ApiProp *prop)
{
  if (prop->type == PROP_FLOAT) {
    ApiFloatProp *fprop = (ApiFloatProp *)prop;
    if (fprop->range) {
      fprintf(f,
              "    float prop_clamp_min = -FLT_MAX, prop_clamp_max = FLT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              api_fn_string(fprop->range));
    }
  }
  else if (prop->type == PROP_INT) {
    ApiIntProp *iprop = (ApiIntProp *)prop;
    if (iprop->range) {
      fprintf(f,
              "    int prop_clamp_min = INT_MIN, prop_clamp_max = INT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              api_fn_string(iprop->range));
    }
  }
}

#ifdef USE_API_RANGE_CHECK
static void api_clamp_value_range_check(FILE *f,
                                        ApiProp *prop,
                                        const char *typesname_prefix,
                                        const char *typesname)
{
  if (prop->type == PROP_INT) {
    ApiIntProp *iprop = (ApiIntProp *)prop;
    fprintf(f,
            "    { LIB_STATIC_ASSERT("
            "(TYPEOF_MAX(%s%s) >= %d) && "
            "(TYPEOF_MIN(%s%s) <= %d), "
            "\"invalid limits\"); }\n",
            typesname_prefix,
            typesname,
            iprop->hardmax,
            typesname_prefix,
            typesname,
            iprop->hardmin);
  }
}
#endif /* USE_API_RANGE_CHECK */

static void api_clamp_value(FILE *f, ApiProp *prop, int array)
{
  if (prop->type == PROP_INT) {
    ApiIntProp *iprop = (ApiIntProp *)prop;

    if (iprop->hardmin != INT_MIN || iprop->hardmax != INT_MAX || iprop->range) {
      if (array) {
        fprintf(f, "CLAMPIS(values[i], ");
      }
      else {
        fprintf(f, "CLAMPIS(value, ");
      }
      if (iprop->range) {
        fprintf(f, "prop_clamp_min, prop_clamp_max);");
      }
      else {
        api_int_print(f, iprop->hardmin);
        fprintf(f, ", ");
        api_int_print(f, iprop->hardmax);
        fprintf(f, ");\n");
      }
      return;
    }
  }
  else if (prop->type == PROP_FLOAT) {
    ApiFloatProp *fprop = (ApiFloatProp *)prop;

    if (fprop->hardmin != -FLT_MAX || fprop->hardmax != FLT_MAX || fprop->range) {
      if (array) {
        fprintf(f, "CLAMPIS(values[i], ");
      }
      else {
        fprintf(f, "CLAMPIS(value, ");
      }
      if (fprop->range) {
        fprintf(f, "prop_clamp_min, prop_clamp_max);");
      }
      else {
        api_float_print(f, fprop->hardmin);
        fprintf(f, ", ");
        api_float_print(f, fprop->hardmax);
        fprintf(f, ");\n");
      }
      return;
    }
  }

  if (array) {
    fprintf(f, "values[i];\n");
  }
  else {
    fprintf(f, "value;\n");
  }
}

static char *api_def_prop_set_fn(
    FILE *f, ApiStruct *sapi, ApiProp *prop, ApiPropDef *dp, const char *manualfn)
{
  char *fn;

  if (!(prop->flag & PROP_EDITABLE)) {
    return NULL;
  }
  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typesname) {
      if (prop->flag & PROP_EDITABLE) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", sapi->id, prop->id);
        ApiDef.error = true;
      }
      return NULL;
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "set");

  switch (prop->type) {
    case PROP_STRING: {
      ApiStringProp *sprop = (ApiStringProp *)prop;
      fprintf(f, "void %s(ApiPtr *ptr, const char *value)\n", fn);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    %s(ptr, value);\n", manualfn);
      }
      else {
        const PropSubType subtype = prop->subtype;
        const char *string_copy_fn =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "lib_strncpy" :
                "lib_strncpy_utf8";

        api_print_data_get(f, dp);

        if (dp->typeptrlevel == 1) {
          /* Handle allocated char ptr props. */
          fprintf(
              f, "    if (data->%s != NULL) { mem_freen(data->%s); }\n", dp->dnaname, dp->dnaname);
          fprintf(f, "    const int length = strlen(value);\n");
          fprintf(f, "    data->%s = mem_mallocn(length + 1, __func__);\n", dp->dnaname);
          fprintf(f, "    %s(data->%s, value, length + 1);\n", string_copy_func, dp->dnaname);
        }
        else {
          /* Handle char array props. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(data->%s, value, %d);\n",
                    string_copy_fn,
                    dp->typesname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(data->%s, value, sizeof(data->%s));\n",
                    string_copy_fn,
                    dp->typesname,
                    dp->typesname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_PTR: {
      fprintf(f, "void %s(ApiPtr *ptr, ApiPtr value, struct ReportList *reports)\n", fn);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    %s(ptr, value, reports);\n", manualfn);
      }
      else {
        api_print_data_get(f, dp);

        if (prop->flag & PROP_ID_SELF_CHECK) {
          api_print_id_get(f, dp);
          fprintf(f, "    if (id == value.data) {\n");
          fprintf(f, "      return;\n");
          fprintf(f, "    }\n");
        }

        if (prop->flag & PROP_ID_REFCOUNT) {
          fprintf(f, "\n    if (data->%s) {\n", dp->typesname);
          fprintf(f, "        id_us_min((Id *)data->%s);\n", dp->typesname);
          fprintf(f, "    }\n");
          fprintf(f, "    if (value.data) {\n");
          fprintf(f, "        id_us_plus((Id *)value.data);\n");
          fprintf(f, "    }\n");
        }
        else {
          ApiPtrProp *pprop = (ApiPtrProp *)dp->prop;
          ApiStruct *type = (pprop->type) ? api_find_struct((const char *)pprop->type) : NULL;
          if (type && (type->flag & STRUCT_ID)) {
            fprintf(f, "    if (value.data) {\n");
            fprintf(f, "        id_lib_extern((Id *)value.data);\n");
            fprintf(f, "    }\n");
          }
        }

        fprintf(f, "    data->%s = value.data;\n", dp->typesname);
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(ApiPtr *ptr, const %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(ApiPtr *ptr, const %s values[%u])\n",
                  fn,
                  api_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    %s(ptr, values);\n", manualfn);
        }
        else {
          api_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfn = api_alloc_fn_name(
                sapi->id, api_safe_id(prop->id), "set_length");
            fprintf(f, "    unsigned int i, arraylen[API_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfn);
            api_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            mem_freen(lenfn);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            api_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->typesarraylength == 1) {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s |= (",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolebit);
              fprintf(f, " << i); }\n");
              fprintf(f, "        else { data->%s &= ~(", dp->typesname);
              api_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
            }
            else {
              fprintf(
                  f, "        (&data->%s)[i] = %s", dp->typesname, (dp->boolnegative) ? "!" : "");
              api_clamp_value(f, prop, 1);
            }
          }
          else {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s[i] |= ",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, "; }\n");
              fprintf(f, "        else { data->%s[i] &= ~", dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, "; }\n");
            }
            else if (api_color_quantize(prop, dp)) {
              fprintf(
                  f, "        data->%s[i] = unit_float_to_uchar_clamp(values[i]);\n", dp->dnaname);
            }
            else {
              if (dp->typestype) {
                fprintf(f,
                        "        ((%s *)data->%s)[i] = %s",
                        dp->type,
                        dp->typesname,
                        (dp->boolnegative) ? "!" : "");
              }
              else {
                fprintf(f,
                        "        (data->%s)[i] = %s",
                        dp->typesname,
                        (dp->boolnegative) ? "!" : "");
              }
              api_clamp_value(f, prop, 1);
            }
          }
          fprintf(f, "    }\n");
        }

#ifdef USE_API_RANGE_CHECK
        if (dp->typesname && manualfn == NULL) {
          if (dp->typesarraylength == 1) {
            api_clamp_value_range_check(f, prop, "data->", dp->typesname);
          }
          else {
            api_clamp_value_range_check(f, prop, "*data->", dp->typesname);
          }
        }
#endif

        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "void %s(ApiPtr *ptr, %s value)\n", fn, api_type_type(prop));
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    %s(ptr, value);\n", manualfunc);
        }
        else {
          api_print_data_get(f, dp);
          if (prop->type == PROP_BOOL && dp->boolbit) {
            fprintf(f,
                    "    if (%svalue) { data->%s |= ",
                    (dp->boolnegative) ? "!" : "",
                    dp->typesname);
            api_int_print(f, dp->boolbit);
            fprintf(f, "; }\n");
            fprintf(f, "    else { data->%s &= ~", dp->typesname);
            rna_int_print(f, dp->boolbit);
            fprintf(f, "; }\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    data->%s &= ~", dp->typesname);
            api_int_print(f, api_enum_bitmask(prop));
            fprintf(f, ";\n");
            fprintf(f, "    data->%s |= value;\n", dp->typesname);
          }
          else {
            api_clamp_value_range(f, prop);
            fprintf(f, "    data->%s = %s", dp->typesname, (dp->boolnegative) ? "!" : "");
            api_clamp_value(f, prop, 0);
          }
        }

#ifdef USE_API_RANGE_CHECK
        if (dp->typesname && manualfn == NULL) {
          api_clamp_value_range_check(f, prop, "data->", dp->typesname);
        }
#endif

        fprintf(f, "}\n\n");
      }
      break;
  }

  return fn;
}

static char *api_def_prop_set_fn(
    FILE *f, ApiStruct *sapi, ApiProp *prop, ApiPropDef *dp, const char *manualfn)
{
  char *func;

  if (!(prop->flag & PROP_EDITABLE)) {
    return NULL;
  }
  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typesname) {
      if (prop->flag & PROP_EDITABLE) {
        CLOG_ERROR(&LOG, "%s.%s has no valid type info.", sapi->id, prop->id);
        ApiDef.error = true;
      }
      return NULL;
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "set");

  switch (prop->type) {
    case PROP_STRING: {
      ApiStringProp *sprop = (ApiStringProp *)prop;
      fprintf(f, "void %s(ApiPtr *ptr, const char *value)\n", fn);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    %s(ptr, value);\n", manualfn);
      }
      else {
        const PropSubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        api_print_data_get(f, dp);

        if (dp->typesptrlevel == 1) {
          /* Handle allocated char ptr props. */
          fprintf(
              f, "    if (data->%s != NULL) { mem_freen(data->%s); }\n", dp->typesname, dp->typesname);
          fprintf(f, "    const int length = strlen(value);\n");
          fprintf(f, "    data->%s = mem_mallocn(length + 1, __func__);\n", dp->typesname);
          fprintf(f, "    %s(data->%s, value, length + 1);\n", string_copy_func, dp->typesname);
        }
        else {
          /* Handle char array props. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(data->%s, value, %d);\n",
                    string_copy_fn,
                    dp->typesname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(data->%s, value, sizeof(data->%s));\n",
                    string_copy_fn,
                    dp->typesname,
                    dp->typesname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_PTR: {
      fprintf(f, "void %s(ApiPtr *ptr, ApiPtr value, struct ReportList *reports)\n", func);
      fprintf(f, "{\n");
      if (manualfn) {
        fprintf(f, "    %s(ptr, value, reports);\n", manualfn);
      }
      else {
        api_print_data_get(f, dp);

        if (prop->flag & PROP_ID_SELF_CHECK) {
          api_print_id_get(f, dp);
          fprintf(f, "    if (id == value.data) {\n");
          fprintf(f, "      return;\n");
          fprintf(f, "    }\n");
        }

        if (prop->flag & PROP_ID_REFCOUNT) {
          fprintf(f, "\n    if (data->%s) {\n", dp->typesname);
          fprintf(f, "        id_us_min((Id *)data->%s);\n", dp->typesname);
          fprintf(f, "    }\n");
          fprintf(f, "    if (value.data) {\n");
          fprintf(f, "        id_us_plus((Id *)value.data);\n");
          fprintf(f, "    }\n");
        }
        else {
          ApiPtrProp *pprop = (ApiPtrProp *)dp->prop;
          ApiStruct *type = (pprop->type) ? api_find_struct((const char *)pprop->type) : NULL;
          if (type && (type->flag & STRUCT_ID)) {
            fprintf(f, "    if (value.data) {\n");
            fprintf(f, "        id_lib_extern((Id *)value.data);\n");
            fprintf(f, "    }\n");
          }
        }

        fprintf(f, "    data->%s = value.data;\n", dp->typesname);
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(ApiPtr *ptr, const %s values[])\n", fn, api_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(ApiPtr *ptr, const %s values[%u])\n",
                  fn,
                  api_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          api_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfn = api_alloc_fn_name(
                sapi->id, api_safe_id(prop->id), "set_length");
            fprintf(f, "    unsigned int i, arraylen[API_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfn);
            api_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            mem_freen(lenfn);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            type_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->typearraylength == 1) {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s |= (",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, " << i); }\n");
              fprintf(f, "        else { data->%s &= ~(", dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, " << i); }\n");
            }
            else {
              fprintf(
                  f, "        (&data->%s)[i] = %s", dp->typesname, (dp->boolnegative) ? "!" : "");
              api_clamp_value(f, prop, 1);
            }
          }
          else {
            if (prop->type == PROP_BOOL && dp->boolbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s[i] |= ",
                      (dp->boolnegative) ? "!" : "",
                      dp->typesname);
              api_int_print(f, dp->boolbit);
              fprintf(f, "; }\n");
              fprintf(f, "        else { data->%s[i] &= ~", dp->dnaname);
              api_int_print(f, dp->boolbit);
              fprintf(f, "; }\n");
            }
            else if (api_color_quantize(prop, dp)) {
              fprintf(
                  f, "        data->%s[i] = unit_float_to_uchar_clamp(values[i]);\n", dp->dnaname);
            }
            else {
              if (dp->type) {
                fprintf(f,
                        "        ((%s *)data->%s)[i] = %s",
                        dp->type,
                        dp->typesname,
                        (dp->boolnegative) ? "!" : "");
              }
              else {
                fprintf(f,
                        "        (data->%s)[i] = %s",
                        dp->typesname,
                        (dp->boolnegative) ? "!" : "");
              }
              api_clamp_value(f, prop, 1);
            }
          }
          fprintf(f, "    }\n");
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->typesname && manualfn == NULL) {
          if (dp->typearraylength == 1) {
            api_clamp_value_range_check(f, prop, "data->", dp->dnaname);
          }
          else {
            api_clamp_value_range_check(f, prop, "*data->", dp->dnaname);
          }
        }
#endif

        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "void %s(ApiPtr *ptr, %s value)\n", fn, api_type_type(prop));
        fprintf(f, "{\n");

        if (manualfn) {
          fprintf(f, "    %s(ptr, value);\n", manualfn);
        }
        else {
          api_print_data_get(f, dp);
          if (prop->type == PROP_BOOL && dp->boolbit) {
            fprintf(f,
                    "    if (%svalue) { data->%s |= ",
                    (dp->boolnegative) ? "!" : "",
                    dp->typesname);
            api_int_print(f, dp->boolbit);
            fprintf(f, "; }\n");
            fprintf(f, "    else { data->%s &= ~", dp->dnaname);
            api_int_print(f, dp->boolbit);
            fprintf(f, "; }\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    data->%s &= ~", dp->dnaname);
            api_int_print(f, api_enum_bitmask(prop));
            fprintf(f, ";\n");
            fprintf(f, "    data->%s |= value;\n", dp->typesname);
          }
          else {
            api_clamp_value_range(f, prop);
            fprintf(f, "    data->%s = %s", dp->typesname, (dp->boolnegative) ? "!" : "");
            api_clamp_value(f, prop, 0);
          }
        }

#ifdef USE_API_RANGE_CHECK
        if (dp->typesname && manualfn == NULL) {
          api_clamp_value_range_check(f, prop, "data->", dp->typesname);
        }
#endif

        fprintf(f, "}\n\n");
      }
      break;
  }

  return fn;
}

static char *api_def_prop_length_fn(
    FILE *f, ApiStruct *sapi, ApiProp *prop, ApiPropDef *dp, const char *manualfn)
{
  char *fn = NULL;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (prop->type == PROP_STRING) {
    if (!manualfn) {
      if (!dp->typestructname || !dp->typesname) {
        CLOG_ERROR(&LOG, "%s.%s has no valid types info.", sapi->id, prop->id);
        ApiDef.error = true;
        return NULL;
      }
    }

    fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "length");

    fprintf(f, "int %s(ApiPtr *ptr)\n", fn);
    fprintf(f, "{\n");
    if (manualfn) {
      fprintf(f, "    return %s(ptr);\n", manualfn);
    }
    else {
      api_print_data_get(f, dp);
      if (dp->typeptrlevel == 1) {
        /* Handle allocated char pointer properties. */
        fprintf(f,
                "    return (data->%s == NULL) ? 0 : strlen(data->%s);\n",
                dp->typesname,
                dp->typesname);
      }
      else {
        /* Handle char array properties. */
        fprintf(f, "    return strlen(data->%s);\n", dp->dnaname);
      }
    }
    fprintf(f, "}\n\n");
  }
  else if (prop->type == PROP_COLLECTION) {
    if (!manualfn) {
      if (prop->type == PROP_COLLECTION &&
          (!(dp->typelengthname || dp->typelengthfixed) || !dp->typesname)) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", sapi->id, prop->id);
        ApiDef.error = true;
        return NULL;
      }
    }

    fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "length");

    fprintf(f, "int %s(ApiPtr *ptr)\n", fn);
    fprintf(f, "{\n");
    if (manualfn) {
      fprintf(f, "    return %s(ptr);\n", manualfn);
    }
    else {
      if (dp->typesarraylength <= 1 || dp->typelengthname) {
        api_print_data_get(f, dp);
      }

      if (dp->typearraylength > 1) {
        fprintf(f, "    return ");
      }
      else {
        fprintf(f, "    return (data->%s == NULL) ? 0 : ", dp->dnaname);
      }

      if (dp->typelengthname) {
        fprintf(f, "data->%s;\n", dp->typelengthname);
      }
      else {
        fprintf(f, "%d;\n", dp->typelengthfixed);
      }
    }
    fprintf(f, "}\n\n");
  }

  return fn;
}

static char *api_def_prop_begin_fn(
    FILE *f, ApiStruct *sapi, ApiProp *prop, ApiPropDef *dp, const char *manualfn)
{
  char *fn, *getfn;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typesname) {
      CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", sapi->id, prop->id);
      ApiDef.error = true;
      return NULL;
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "begin");

  fprintf(f, "void %s(CollectionPropIter *iter, ApiPtr *ptr)\n", fn);
  fprintf(f, "{\n");

  if (!manualfn) {
    api_print_data_get(f, dp);
  }

  fprintf(f, "\n    memset(iter, 0, sizeof(*iter));\n");
  fprintf(f, "    iter->parent = *ptr;\n");
  fprintf(f, "    iter->prop = (ApiProp *)&api_%s_%s;\n", sapi->id, prop->id);

  if (dp->typelengthname || dp->typelengthfixed) {
    if (manualfn) {
      fprintf(f, "\n    %s(iter, ptr);\n", manualfn);
    }
    else {
      if (dp->typelengthname) {
        fprintf(f,
                "\n    api_iter_array_begin(iter, data->%s, sizeof(data->%s[0]), data->%s, 0, "
                "NULL);\n",
                dp->typesname,
                dp->typesname,
                dp->typelengthname);
      }
      else {
        fprintf(
            f,
            "\n    type_iter_array_begin(iter, data->%s, sizeof(data->%s[0]), %d, 0, NULL);\n",
            dp->typesname,
            dp->typesname,
            dp->typelengthfixed);
      }
    }
  }
  else {
    if (manualfn) {
      fprintf(f, "\n    %s(iter, ptr);\n", manualfn);
    }
    else if (dp->typeptrlevel == 0) {
      fprintf(f, "\n    api_iter_list_begin(iter, &data->%s, NULL);\n", dp->typesname);
    }
    else {
      fprintf(f, "\n    api_iter_list_begin(iter, data->%s, NULL);\n", dp->typesname);
    }
  }

  getfn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "get");

  fprintf(f, "\n    if (iter->valid) {\n");
  fprintf(f, "        iter->ptr = %s(iter);", getfn);
  fprintf(f, "\n    }\n");

  fprintf(f, "}\n\n");

  return fn;
}

static char *api_def_prop_lookup_int_fn(FILE *f,
                                        ApiStruct *sapi,
                                        ApiProp *prop,
                                        ApiPropDef *dp,
                                        const char *manualfn,
                                        const char *nextfn)
{
  /* note on indices, this is for external functions and ignores skipped values.
   * so the index can only be checked against the length when there is no 'skip' function. */
  char *fn;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typesname) {
      return NULL;
    }

    /* only supported in case of standard next fns */
    if (STREQ(nextfn, "api_iter_array_next")) {
    }
    else if (STREQ(nextfb, "api_iter_list_next")) {
    }
    else {
      return NULL;
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "lookup_int");

  fprintf(f, "int %s(ApiPtr *ptr, int index, ApiPtr *r_ptr)\n", fn);
  fprintf(f, "{\n");

  if (manualfn) {
    fprintf(f, "\n    return %s(ptr, index, r_ptr);\n", manualfn);
    fprintf(f, "}\n\n");
    return fn;
  }

  fprintf(f, "    int found = 0;\n");
  fprintf(f, "    CollectionPropIter iter;\n\n");

  fprintf(f, "    %s_%s_begin(&iter, ptr);\n\n", sapi->id, api_safe_id(prop->id));
  fprintf(f, "    if (iter.valid) {\n");

  if (STREQ(nextfn, "api_iter_array_next")) {
    fprintf(f, "        ArrayIter *internal = &iter.internal.array;\n");
    fprintf(f, "        if (index < 0 || index >= internal->length) {\n");
    fprintf(f, "#ifdef __GNUC__\n");
    fprintf(f,
            "            printf(\"Array iterator out of range: %%s (index %%d)\\n\", __func__, "
            "index);\n");
    fprintf(f, "#else\n");
    fprintf(f, "            printf(\"Array iterator out of range: (index %%d)\\n\", index);\n");
    fprintf(f, "#endif\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else if (internal->skip) {\n");
    fprintf(f, "            while (index-- > 0 && iter.valid) {\n");
    fprintf(f, "                api_iter_array_next(&iter);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && iter.valid);\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else {\n");
    fprintf(f, "            internal->ptr += internal->itemsize * index;\n");
    fprintf(f, "            found = 1;\n");
    fprintf(f, "        }\n");
  }
  else if (STREQ(nextfn, "api_iter_list_next")) {
    fprintf(f, "        ListIter *internal = &iter.internal.list;\n");
    fprintf(f, "        if (internal->skip) {\n");
    fprintf(f, "            while (index-- > 0 && iter.valid) {\n");
    fprintf(f, "                api_iter_list_next(&iter);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && iter.valid);\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else {\n");
    fprintf(f, "            while (index-- > 0 && internal->link) {\n");
    fprintf(f, "                internal->link = internal->link->next;\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && internal->link);\n");
    fprintf(f, "        }\n");
  }

  fprintf(f,
          "        if (found) { *r_ptr = %s_%s_get(&iter); }\n",
          sapi->id,
          api_safe_id(prop->id));
  fprintf(f, "    }\n\n");
  fprintf(f, "    %s_%s_end(&iter);\n\n", srna->id, api_safe_id(prop->id));

  fprintf(f, "    return found;\n");

#if 0
  api_print_data_get(f, dp);
  item_type = (cprop->item_type) ? (const char *)cprop->item_type : "UnknownType";

  if (dp->typelengthname || dp->typelengthfixed) {
    if (dp->typelengthname) {
      fprintf(f,
              "\n    api_array_lookup_int(ptr, &Api_%s, data->%s, sizeof(data->%s[0]), data->%s, "
              "index);\n",
              item_type,
              dp->typesname,
              dp->typesname,
              dp->typeslengthname);
    }
    else {
      fprintf(
          f,
          "\n    api_array_lookup_int(ptr, &Api%s, data->%s, sizeof(data->%s[0]), %d, index);\n",
          item_type,
          dp->typesname,
          dp->typesname,
          dp->typelengthfixed);
    }
  }
  else {
    if (dp->typeptrlevel == 0) {
      fprintf(f,
              "\n    return api_list_lookup_int(ptr, &Api%s, &data->%s, index);\n",
              item_type,
              dp->typesname);
    }
    else {
      fprintf(f,
              "\n    return api_list_lookup_int(ptr, &Api%s, data->%s, index);\n",
              item_type,
              dp->typesname);
    }
  }
#endif

  fprintf(f, "}\n\n");

  return fn;
}

static char *api_def_prop_lookup_string_fn(FILE *f,
                                           ApiStruct *sapi,
                                           ApiProp *prop,
                                           ApiPropDef *dp,
                                           const char *manualfn,
                                           const char *item_type)
{
  char *fn;
  ApiStruct *item_sapi, *item_name_base;
  ApiProp *item_name_prop;
  const int namebuflen = 1024;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    if (!dp->typestructname || !dp->typesname) {
      return NULL;
    }

    /* only supported for collection items with name props */
    item_sapi = api_find_struct(item_type);
    if (item_sapi && item_sapi->nameprop) {
      item_name_prop = item_sapi->nameprop;
      item_name_base = item_sapi;
      while (item_name_base->base && item_name_base->base->nameprop == item_name_prop) {
        item_name_base = item_name_base->base;
      }
    }
    else {
      return NULL;
    }
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "lookup_string");
  fprintf(f, "int %s(ApiPtr *ptr, const char *key, ApiPtr *r_ptr)\n", fn);
  fprintf(f, "{\n");

  if (manualfn) {
    fprintf(f, "    return %s(ptr, key, r_ptr);\n", manualfn);
    fprintf(f, "}\n\n");
    return fn;
  }

  /* XXX extern declaration could be avoid by including ApiDune.h, but this has lots of unknown
   * DNA types in functions, leading to conflicting function signatures. */
  fprintf(f,
          "    extern int %s_%s_length(ApiPtr *);\n",
          item_name_base->id,
          api_safe_id(item_name_prop->id));
  fprintf(f,
          "    extern void %s_%s_get(ApiPtr *, char *);\n\n",
          item_name_base->id,
          api_safe_id(item_name_prop->id));

  fprintf(f, "    bool found = false;\n");
  fprintf(f, "    CollectionPropIter iter;\n");
  fprintf(f, "    char namebuf[%d];\n", namebuflen);
  fprintf(f, "    char *name;\n\n");

  fprintf(f, "    %s_%s_begin(&iter, ptr);\n\n", srna->identifier, rna_safe_id(prop->identifier));

  fprintf(f, "    while (iter.valid) {\n");
  fprintf(f, "        if (iter.ptr.data) {\n");
  fprintf(f,
          "            int namelen = %s_%s_length(&iter.ptr);\n",
          item_name_base->id,
          api_safe_id(item_name_prop->id));
  fprintf(f, "            if (namelen < %d) {\n", namebuflen);
  fprintf(f,
          "                %s_%s_get(&iter.ptr, namebuf);\n",
          item_name_base->id,
          api_safe_id(item_name_prop->id));
  fprintf(f, "                if (strcmp(namebuf, key) == 0) {\n");
  fprintf(f, "                    found = true;\n");
  fprintf(f, "                    *r_ptr = iter.ptr;\n");
  fprintf(f, "                    break;\n");
  fprintf(f, "                }\n");
  fprintf(f, "            }\n");
  fprintf(f, "            else {\n");
  fprintf(f, "                name = mem_mallocn(namelen+1, \"name string\");\n");
  fprintf(f,
          "                %s_%s_get(&iter.ptr, name);\n",
          item_name_base->id,
          api_safe_id(item_name_prop->id));
  fprintf(f, "                if (strcmp(name, key) == 0) {\n");
  fprintf(f, "                    mem_freen(name);\n\n");
  fprintf(f, "                    found = true;\n");
  fprintf(f, "                    *r_ptr = iter.ptr;\n");
  fprintf(f, "                    break;\n");
  fprintf(f, "                }\n");
  fprintf(f, "                else {\n");
  fprintf(f, "                    mem_freen(name);\n");
  fprintf(f, "                }\n");
  fprintf(f, "            }\n");
  fprintf(f, "        }\n");
  fprintf(f, "        %s_%s_next(&iter);\n", sapi->id, api_safe_id(prop->id));
  fprintf(f, "    }\n");
  fprintf(f, "    %s_%s_end(&iter);\n\n", sapi->id, api_safe_id(prop->id));

  fprintf(f, "    return found;\n");
  fprintf(f, "}\n\n");

  return fn;
}

static char *api_def_prop_next_fn(FILE *f,
                                  ApiStruct *sapi,
                                  ApiProp *prop,
                                  ApiPropDef *UNUSED(dp),
                                  const char *manualfn)
{
  char *fn, *getfn;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  if (!manualfn) {
    return NULL;
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "next");

  fprintf(f, "void %s(CollectionPropIter *iter)\n", fn);
  fprintf(f, "{\n");
  fprintf(f, "    %s(iter);\n", manualfn);

  getfn = api_alloc_fn_name(sapi->id, api_safe_id(prop->identifier), "get");

  fprintf(f, "\n    if (iter->valid) {\n");
  fprintf(f, "        iter->ptr = %s(iter);", getfunc);
  fprintf(f, "\n    }\n");

  fprintf(f, "}\n\n");

  return fn;
}

static char *api_def_prop_end_fn(FILE *f,
                                       ApiStruct *sapi,
                                       ApiProp *prop,
                                       ApiPropDef *UNUSED(dp),
                                       const char *manualfn)
{
  char *func;

  if (prop->flag & PROP_IDPROP && manualfn == NULL) {
    return NULL;
  }

  func = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "end");

  fprintf(f, "void %s(CollectionPropIter *iter)\n", fn);
  fprintf(f, "{\n");
  if (manualfn) {
    fprintf(f, "    %s(iter);\n", manualfn);
  }
  fprintf(f, "}\n\n");

  return fn;
}

static void api_set_raw_prop(ApiPropDef *dp, ApiProp *prop)
{
  if (dp->typeptrlevel != 0) {
    return;
  }
  if (!dp->type || !dp->typesname || !dp->typestructname) {
    return;
  }

  if (STREQ(dp->dnatype, "char")) {
    prop->rawtype = PROP_RAW_CHAR;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->dnatype, "short")) {
    prop->rawtype = PROP_RAW_SHORT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->type, "int")) {
    prop->rawtype = PROP_RAW_INT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->type, "float")) {
    prop->rawtype = PROP_RAW_FLOAT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->type, "double")) {
    prop->rawtype = PROP_RAW_DOUBLE;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
}

static void api_set_raw_offset(FILE *f, ApiStruct *sapi, ApiProp *prop)
{
  ApiPropDef *dp = api_find_struct_prop_def(srna, prop);

  fprintf(f, "\toffsetof(%s, %s), %d", dp->typestructname, dp->typesname, prop->rawtype);
}

static void api_def_prop_fns(FILE *f, ApiStruct *sapi, ApiPropDef *dp)
{
  ApiProp *prop;

  prop = dp->prop;

  switch (prop->type) {
    case PROP_BOOL: {
      ApiBoolProp *bprop = (ApiBoolProp *)prop;

      if (!prop->arraydimension) {
        if (!bprop->get && !bprop->set && !dp->boolbit) {
          api_set_raw_prop(dp, prop);
        }

        bprop->get = (void *)api_def_prop_get_fn(
            f, sapi, prop, dp, (const char *)bprop->get);
        bprop->set = (void *)api_def_prop_set_fn(
            f, sapi, prop, dp, (const char *)bprop->set);
      }
      else {
        bprop->getarray = (void *)api_def_prop_get_fn(
            f, sapi, prop, dp, (const char *)bprop->getarray);
        bprop->setarray = (void *)api_def_prop_set_fn(
            f, sapi, prop, dp, (const char *)bprop->setarray);
      }
      break;
    }
    case PROP_INT: {
      ApiIntProp *iprop = (ApiIntProp *)prop;

      if (!prop->arraydimension) {
        if (!iprop->get && !iprop->set) {
          api_set_raw_prop(dp, prop);
        }

        iprop->get = (void *)api_def_prop_get_fn(
            f, sapi, prop, dp, (const char *)iprop->get);
        iprop->set = (void *)api_def_prop_set_fn(
            f, sapi, prop, dp, (const char *)iprop->set);
      }
      else {
        if (!iprop->getarray && !iprop->setarray) {
          api_set_raw_prop(dp, prop);
        }

        iprop->getarray = (void *)api_def_prop_get_fn(
            f, sapi, prop, dp, (const char *)iprop->getarray);
        iprop->setarray = (void *)api_def_prop_set_fn(
            f, sapi, prop, dp, (const char *)iprop->setarray);
      }
      break;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (ApiFloatProp *)prop;

      if (!prop->arraydimension) {
        if (!fprop->get && !fprop->set) {
          rna_set_raw_prop(dp, prop);
        }

        fprop->get = (void *)api_def_prop_get_fn(
            f, sapi, prop, dp, (const char *)fprop->get);
        fprop->set = (void *)api_def_prop_set_fn(
            f, sapi, prop, dp, (const char *)fprop->set);
      }
      else {
        if (!fprop->getarray && !fprop->setarray) {
          api_set_raw_prop(dp, prop);
        }

        fprop->getarray = (void *)api_def_prop_get_fn(
            f, srna, prop, dp, (const char *)fprop->getarray);
        fprop->setarray = (void *)api_def_prop_set_fn(
            f, srna, prop, dp, (const char *)fprop->setarray);
      }
      break;
    }
    case PROP_ENUM: {
      ApiEnumProp *eprop = (ApiEnumProp *)prop;

      if (!eprop->get && !eprop->set) {
        api_set_raw_prop(dp, prop);
      }

      eprop->get = (void *)api_def_prop_get_fn(f, sapi, prop, dp, (const char *)eprop->get);
      eprop->set = (void *)api_def_prop_set_fn(f, sapi, prop, dp, (const char *)eprop->set);
      break;
    }
    case PROP_STRING: {
      ApiStringProp *sprop = (ApiStringProp *)prop;

      sprop->get = (void *)api_def_prop_get_fn(f, sapi, prop, dp, (const char *)sprop->get);
      sprop->length = (void *)api_def_prop_length_fn(
          f, srna, prop, dp, (const char *)sprop->length);
      sprop->set = (void *)api_def_prop_set_fn(f, sapi, prop, dp, (const char *)sprop->set);
      break;
    }
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (ApiPtrProp *)prop;

      pprop->get = (void *)api_def_prop_get_fn(f, sapi, prop, dp, (const char *)pprop->get);
      pprop->set = (void *)api_def_prop_set_fn(f, sapi, prop, dp, (const char *)pprop->set);
      if (!pprop->type) {
        CLOG_ERROR(
            &LOG, "%s.%s, ptr must have a struct type.", srna->id, prop->id);
        ApiDef.error = true;
      }
      break;
    }
    case PROP_COLLECTION: {
      ApiCollectionProp *cprop = (ApiCollectionProp *)prop;
      const char *nextfn = (const char *)cprop->next;
      const char *item_type = (const char *)cprop->item_type;

      if (cprop->length) {
        /* always generate if we have a manual implementation */
        cprop->length = (void *)api_def_prop_length_fna(
            f, sapi, prop, dp, (const char *)cprop->length);
      }
      else if (dp->type && STREQ(dp->type, "List")) {
        /* pass */
      }
      else if (dp->typeslengthname || dp->typelengthfixed) {
        cprop->length = (void *)api_def_prop_length_fn(
            f, sapi, prop, dp, (const char *)cprop->length);
      }

      /* test if we can allow raw array access, if it is using our standard
       * array get/next function, we can be sure it is an actual array */
      if (cprop->next && cprop->get) {
        if (STREQ((const char *)cprop->next, "api_iter_array_next") &&
            STREQ((const char *)cprop->get, "api_iter_array_get")) {
          prop->flag_internal |= PROP_INTERN_RAW_ARRAY;
        }
      }

      cprop->get = (void *)api_def_prop_get_fn(fn, sapi, prop, dp, (const char *)cprop->get);
      cprop->begin = (void *)api_def_prop_begin_fn(
          f, sapi, prop, dp, (const char *)cprop->begin);
      cprop->next = (void *)api_def_prop_next_fn(
          f, sapi, prop, dp, (const char *)cprop->next);
      cprop->end = (void *)api_def_prop_end_fn(f, srna, prop, dp, (const char *)cprop->end);
      cprop->lookupint = (void *)api_def_prop_lookup_int_fn(
          f, sapi, prop, dp, (const char *)cprop->lookupint, nextfn);
      cprop->lookupstring = (void *)rna_def_prop_lookup_string_fn(
          f, sapi, prop, dp, (const char *)cprop->lookupstring, item_type);

      if (!(prop->flag & PROP_IDPROP)) {
        if (!cprop->begin) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a begin function.",
                     sapi->id,
                     prop->id);
          ApiDef.error = true;
        }
        if (!cprop->next) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a next fn.",
                     sapi->id,
                     prop->id);
          ApiDef.error = true;
        }
        if (!cprop->get) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a get fn.",
                     sapi->id,
                     prop->id);
          ApiDef.error = true;
        }
      }
      if (!cprop->item_type) {
        CLOG_ERROR(&LOG,
                   "%s.%s, collection must have a struct type.",
                   srna->id,
                   prop->id);
        ApiDef.error = true;
      }
      break;
    }
  }
}

static void api_def_prop_fns_header(FILE *f, ApiStruct *sapi, ApiPropDef *dp)
{
  ApiProp *prop;
  const char *fn;

  prop = dp->prop;

  if (prop->flag & PROP_IDPROP || prop->flag_internal & PROP_INTERN_BUILTIN) {
    return;
  }

  fn = api_alloc_fn_name(sapi->id, api_safe_id(prop->id), "");

  switch (prop->type) {
    case PROP_BOOL: {
      if (!prop->arraydimension) {
        fprintf(f, "bool %sget(ApiPtr *ptr);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, bool value);\n", fn);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(ApiPtr *ptr, bool values[%u]);\n", fn, prop->totarraylength);
        fprintf(f,
                "void %sset(ApiPtr *ptr, const bool values[%u]);\n",
                fn,
                prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(ApiPtr *ptr, bool values[]);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, const bool values[]);\n", fn);
      }
      break;
    }
    case PROP_INT: {
      if (!prop->arraydimension) {
        fprintf(f, "int %sget(ApiPtr *ptr);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, int value);\n", fn);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(ApiPtr *ptr, int values[%u]);\n", fn, prop->totarraylength);
        fprintf(
            f, "void %sset(ApiPtr *ptr, const int values[%u]);\n", fn, prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(ApiPtr *ptr, int values[]);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, const int values[]);\n", fn);
      }
      break;
    }
    case PROP_FLOAT: {
      if (!prop->arraydimension) {
        fprintf(f, "float %sget(ApiPtr *ptr);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, float value);\n", fn);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(ApiPtr *ptr, float values[%u]);\n", fn, prop->totarraylength);
        fprintf(f,
                "void %sset(ApiPtr *ptr, const float values[%u]);\n",
                fn,
                prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(ApiPtr *ptr, float values[]);\n", fn);
        fprintf(f, "void %sset(ApiPtr *ptr, const float values[]);", fn);
      }
      break;
    }
    case PROP_ENUM: {
      ApiEnumProp *eprop = (ApiEnumProp *)prop;
      int i;

      if (eprop->item && eprop->totitem) {
        fprintf(f, "enum {\n");

        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].id[0]) {
            fprintf(f,
                    "\t%s_%s_%s = %d,\n",
                    sapi->id,
                    prop->id,
                    eprop->item[i].id,
                    eprop->item[i].value);
          }
        }

        fprintf(f, "};\n\n");
      }


      
      fprintf(f, "int %sget(ApiPtr *ptr);\n", fn);
      fprintf(f, "void %sset(ApiPtr *ptr, int value);\n", fn);

      break;
    }
    case PROP_STRING: {
      ApiStringProp *sprop = (ApiStringProp *)prop;

      if (sprop->maxlength) {
        fprintf(
            f, "#define %s_%s_MAX %d\n\n", sapi->id, prop->id, sprop->maxlength);
      }

      fprintf(f, "void %sget(ApiPtr *ptr, char *value);\n", fn);
      fprintf(f, "int %slength(ApiPtr *ptr);\n", fn);
      fprintf(f, "void %sset(ApiPtr *ptr, const char *value);\n", fn);

      break;
    }
    case PROP_PTR: {
      fprintf(f, "ApiPtr %sget(ApiPtr *ptr);\n", fn);
      /*fprintf(f, "void %sset(ApiPtr *ptr, ApiPtr value);\n", fn); */
      break;
    }
    case PROP_COLLECTION: {
      ApiCollectionProp *cprop = (ApiCollectionProp *)prop;
      fprintf(f, "void %sbegin(CollectionPropIter *iter, ApiPtr *ptr);\n", fn);
      fprintf(f, "void %snext(CollectionPropIter *iter);\n", fn);
      fprintf(f, "void %send(CollectionPropIter *iter);\n", fn);
      if (cprop->length) {
        fprintf(f, "int %slength(ApiPtr *ptr);\n", fn);
      }
      if (cprop->lookupint) {
        fprintf(f, "int %slookup_int(ApiPtr *ptr, int key, ApiPtr *r_ptr);\n", fn);
      }
      if (cprop->lookupstring) {
        fprintf(f,
                "int %slookup_string(ApiPtr *ptr, const char *key, ApiPtr *r_ptr);\n",
                fn);
      }
      break;
    }
  }

  if (prop->getlength) {
    char fnname[2048];
    api_construct_wrapper_fn_name(
        fnname, sizeof(fnname), sapi->id, prop->id, "get_length");
    fprintf(f, "int %s(ApiPtr *ptr, int *arraylen);\n", fnname);
  }

  fprintf(f, "\n");
}

static void api_def_fn_fns_header(FILE *f, ApiStruct *sapi, ApiFnDef *dfn)
{
  ApiFn *fn = dfn->fn;
  char fnname[2048];

  api_construct_wrapper_fn_name(
      fnname, sizeof(fnname), sapi->id, fn->id, "fn");
  api_generate_static_param_prototypes(f, sapi, dfn, fnname, 1);
}

static void api_def_prop_fns_header_cpp(FILE *f, ApiStruct *sapi, ApiPropDef *dp)
{
  ApiProp *prop;

  prop = dp->prop;

  if (prop->flag & PROP_IDPROP || prop->flag_internal & PROP_INTERN_BUILTIN) {
    return;
  }

  /* Disabled for now to avoid MSVC compiler error due to large file size. */
#if 0
  if (prop->name && prop->description && prop->description[0] != '\0') {
    fprintf(f, "\t/* %s: %s */\n", prop->name, prop->description);
  }
  else if (prop->name) {
    fprintf(f, "\t/* %s */\n", prop->name);
  }
  else {
    fprintf(f, "\t/* */\n");
  }
#endif

  switch (prop->type) {
    case PROP_BOOL: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline bool %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(bool value);", api_safe_id(prop->id));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<bool, %u> %s(void);\n",
                prop->totarraylength,
                api_safe_id(prop->id));
        fprintf(f,
                "\tinline void %s(bool values[%u]);",
                api_safe_id(prop->id),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<bool> %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(bool values[]);", api_safe_id(prop->id));
      }
      break;
    }
    case PROP_INT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline int %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(int value);", api_safe_id(prop->id));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<int, %u> %s(void);\n",
                prop->totarraylength,
                api_safe_id(prop->id));
        fprintf(f,
                "\tinline void %s(int values[%u]);",
                api_safe_id(prop->id),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<int> %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(int values[]);", api_safe_id(prop->id));
      }
      break;
    }
    case PROP_FLOAT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline float %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(float value);", api_safe_id(prop->id));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<float, %u> %s(void);\n",
                prop->totarraylength,
                api_safe_id(prop->id));
        fprintf(f,
                "\tinline void %s(float values[%u]);",
                api_safe_id(prop->id),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<float> %s(void);\n", api_safe_id(prop->id));
        fprintf(f, "\tinline void %s(float values[]);", api_safe_id(prop->id));
      }
      break;
    }
    case PROP_ENUM: {
      ApiEnumProp *eprop = (ApiEnumProp *)prop;
      int i;

      if (eprop->item) {
        fprintf(f, "\tenum %s_enum {\n", api_safe_id(prop->id));

        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].id[0]) {
            fprintf(f,
                    "\t\t%s_%s = %d,\n",
                    api_safe_id(prop->id),
                    eprop->item[i].id,
                    eprop->item[i].value);
          }
        }

        fprintf(f, "\t};\n");
      }

      fprintf(f,
              "\tinline %s_enum %s(void);\n",
              api_safe_id(prop->id),
              api_safe_id(prop->id));
      fprintf(f,
              "\tinline void %s(%s_enum value);",
              api_safe_id(prop->id),
              api_safe_id(prop->id));
      break;
    }
    case PROP_STRING: {
      fprintf(f, "\tinline std::string %s(void);\n", api_safe_id(prop->id));
      fprintf(f, "\tinline void %s(const std::string& value);", api_safe_id(prop->id));
      break;
    }
    case PROP_PTR: {
      ApiPtrProp *pprop = (ApiPtrProp *)dp->prop;

      if (pprop->type) {
        fprintf(
            f, "\tinline %s %s(void);", (const char *)pprop->type, api_safe_id(prop->id));
      }
      else {
        fprintf(f, "\tinline %s %s(void);", "UnknownType", api_safe_id(prop->id));
      }
      break;
    }
    case PROP_COLLECTION: {
      ApiCollectionProp *cprop = (ApiCollectionProp *)dp->prop;
      const char *collection_fns = "DefaultCollectionFns";

      if (!(dp->prop->flag & PROP_IDPROP || dp->prop->flag_internal & PROP_INTERN_BUILTIN) &&
          cprop->prop.sapi) {
        collection_fns = (char *)cprop->prop.sapi;
      }

      if (cprop->item_type) {
        fprintf(f,
                "\tCOLLECTION_PROP(%s, %s, %s, %s, %s, %s, %s)",
                collection_fns,
                (const char *)cprop->item_type,
                sapi->id,
                api_safe_id(prop->id),
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
      else {
        fprintf(f,
                "\tCOLLECTION_PROP(%s, %s, %s, %s, %s, %s, %s)",
                collection_fns,
                "UnknownType",
                sapi->id,
                api_safe_id(prop->id),
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
      break;
    }
  }

  fprintf(f, "\n");
}

static const char *api_param_type_cpp_name(ApiProp *prop)
{
  if (prop->type == PROP_PTR) {
    /* for cpp api we need to use api structures names for ptrs */
    ApiPtrProp *pprop = (ApiPtrProp *)prop;

    return (const char *)pprop->type;
  }
  return api_param_type_name(prop);
}

static void api_def_struct_fn_prototype_cpp(FILE *f,
                                                  ApiStruct *UNUSED(sapi),
                                                  ApiFnDef *dfn,
                                                  const char *namespace,
                                                  int close_prototype)
{
  ApiPropDef *dp;
  ApiFn *fn = dfn->fn;

  int first = 1;
  const char *retval_type = "void";

  if (fn->c_ret) {
    dp = api_find_param_def(fn->c_ret);
    retval_type = api_param_type_cpp_name(dp->prop);
  }

  if (namespace && namespace[0]) {
    fprintf(f, "\tinline %s %s::%s(", retval_type, namespace, api_safe_id(fn->id));
  }
  else {
    fprintf(f, "\tinline %s %s(", retval_type, api_safe_id(fn->id));
  }

  if (fn->flag & FN_USE_MAIN) {
    WRITE_PARAM("void *main");
  }

  if (fn->flag & FN_USE_CXT) {
    WRITE_PARAM("Context C");
  }

  for (dp = dfn->cont.props.first; dp; dp = dp->next) {
    int type, flag, flag_param, pout;
    const char *ptrstr;

    if (dp->prop == fn->c_ret) {
      continue;
    }

    type = dp->prop->type;
    flag = dp->prop->flag;
    flag_param = dp->prop->flag_param;
    pout = (flag_param & PARM_OUTPUT);

    if (flag & PROP_DYNAMIC) {
      if (type == PROP_STRING) {
        ptrstr = pout ? "*" : "";
      }
      else {
        ptrstr = pout ? "**" : "*";
      }
    }
    else if (type == PROP_POINTER) {
      ptrstr = pout ? "*" : "";
    }
    else if (dp->prop->arraydimension) {
      ptrstr = "*";
    }
    else if (type == PROP_STRING && (flag & PROP_THICK_WRAP)) {
      ptrstr = "";
    }
    else {
      ptrstr = pout ? "*" : "";
    }

    WRITE_COMMA;

    if (flag & PROP_DYNAMIC) {
      fprintf(
          f, "int %s%s_len, ", (flag_parameter & PARM_OUTPUT) ? "*" : "", dp->prop->identifier);
    }

    if (!(flag & PROP_DYNAMIC) && dp->prop->arraydimension) {
      fprintf(f,
              "%s %s[%u]",
              rna_parameter_type_cpp_name(dp->prop),
              rna_safe_id(dp->prop->identifier),
              dp->prop->totarraylength);
    }
    else {
      fprintf(f,
              "%s%s%s%s",
              rna_parameter_type_cpp_name(dp->prop),
              (dp->prop->type == PROP_POINTER && ptrstr[0] == '\0') ? "& " : " ",
              ptrstr,
              rna_safe_id(dp->prop->identifier));
    }
  }

  fprintf(f, ")");
  if (close_prototype) {
    fprintf(f, ";\n");
  }
}

static void rna_def_struct_function_header_cpp(FILE *f, StructRNA *srna, FunctionDefRNA *dfunc)
{
  if (dfunc->call) {
    /* Disabled for now to avoid MSVC compiler error due to large file size. */
#if 0
    FunctionRNA *func = dfunc->func;
    fprintf(f, "\n\t/* %s */\n", func->description);
#endif

    rna_def_struct_function_prototype_cpp(f, srna, dfunc, NULL, 1);
  }
}

static void rna_def_property_funcs_impl_cpp(FILE *f, StructRNA *srna, PropertyDefRNA *dp)
{
  PropertyRNA *prop;

  prop = dp->prop;

  if (prop->flag & PROP_IDPROPERTY || prop->flag_internal & PROP_INTERN_BUILTIN) {
    return;
  }

  switch (prop->type) {
    case PROP_BOOLEAN: {
      if (!prop->arraydimension) {
        fprintf(f, "\tBOOLEAN_PROPERTY(%s, %s)", srna->identifier, rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tBOOLEAN_ARRAY_PROPERTY(%s, %u, %s)",
                srna->identifier,
                prop->totarraylength,
                rna_safe_id(prop->identifier));
      }
      else if (prop->getlength) {
        fprintf(f,
                "\tBOOLEAN_DYNAMIC_ARRAY_PROPERTY(%s, %s)",
                srna->identifier,
                rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_INT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tINT_PROPERTY(%s, %s)", srna->identifier, rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tINT_ARRAY_PROPERTY(%s, %u, %s)",
                srna->identifier,
                prop->totarraylength,
                rna_safe_id(prop->identifier));
      }
      else if (prop->getlength) {
        fprintf(f,
                "\tINT_DYNAMIC_ARRAY_PROPERTY(%s, %s)",
                srna->identifier,
                rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_FLOAT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tFLOAT_PROPERTY(%s, %s)", srna->identifier, rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tFLOAT_ARRAY_PROPERTY(%s, %u, %s)",
                srna->identifier,
                prop->totarraylength,
                rna_safe_id(prop->identifier));
      }
      else if (prop->getlength) {
        fprintf(f,
                "\tFLOAT_DYNAMIC_ARRAY_PROPERTY(%s, %s)",
                srna->identifier,
                rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_ENUM: {
      fprintf(f,
              "\tENUM_PROPERTY(%s_enum, %s, %s)",
              rna_safe_id(prop->identifier),
              srna->identifier,
              rna_safe_id(prop->identifier));

      break;
    }
    case PROP_STRING: {
      fprintf(f, "\tSTRING_PROPERTY(%s, %s)", srna->identifier, rna_safe_id(prop->identifier));
      break;
    }
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)dp->prop;

      if (pprop->type) {
        fprintf(f,
                "\tPOINTER_PROPERTY(%s, %s, %s)",
                (const char *)pprop->type,
                srna->identifier,
                rna_safe_id(prop->identifier));
      }
      else {
        fprintf(f,
                "\tPOINTER_PROPERTY(%s, %s, %s)",
                "UnknownType",
                srna->identifier,
                rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_COLLECTION: {
#if 0
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)dp->prop;

      if (cprop->type) {
        fprintf(f,
                "\tCOLLECTION_PROPERTY(%s, %s, %s, %s, %s, %s)",
                (const char *)cprop->type,
                srna->identifier,
                prop->identifier,
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
      else {
        fprintf(f,
                "\tCOLLECTION_PROPERTY(%s, %s, %s, %s, %s, %s)",
                "UnknownType",
                srna->identifier,
                prop->identifier,
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
#endif
      break;
    }
  }

  fprintf(f, "\n");
}

static void rna_def_struct_function_call_impl_cpp(FILE *f, StructRNA *srna, FunctionDefRNA *dfunc)
{
  PropertyDefRNA *dp;
  StructDefRNA *dsrna;
  FunctionRNA *func = dfunc->func;
  char funcname[2048];

  int first = 1;

  rna_construct_wrapper_function_name(
      funcname, sizeof(funcname), srna->identifier, func->identifier, "func");

  fprintf(f, "%s(", funcname);

  dsrna = rna_find_struct_def(srna);

  if (func->flag & FUNC_USE_SELF_ID) {
    WRITE_PARAM("(::ID *) ptr.owner_id");
  }

  if ((func->flag & FUNC_NO_SELF) == 0) {
    WRITE_COMMA;
    if (dsrna->dnafromprop) {
      fprintf(f, "(::%s *) this->ptr.data", dsrna->dnafromname);
    }
    else if (dsrna->dnaname) {
      fprintf(f, "(::%s *) this->ptr.data", dsrna->dnaname);
    }
    else {
      fprintf(f, "(::%s *) this->ptr.data", srna->identifier);
    }
  }
  else if (func->flag & FUNC_USE_SELF_TYPE) {
    WRITE_COMMA;
    fprintf(f, "this->ptr.type");
  }

  if (func->flag & FUNC_USE_MAIN) {
    WRITE_PARAM("(::Main *) main");
  }

  if (func->flag & FUNC_USE_CONTEXT) {
    WRITE_PARAM("(::bContext *) C.ptr.data");
  }

  if (func->flag & FUNC_USE_REPORTS) {
    WRITE_PARAM("NULL");
  }

  dp = dfunc->cont.properties.first;
  for (; dp; dp = dp->next) {
    if (dp->prop == func->c_ret) {
      continue;
    }

    WRITE_COMMA;

    if (dp->prop->flag & PROP_DYNAMIC) {
      fprintf(f, "%s_len, ", dp->prop->identifier);
    }

    if (dp->prop->type == PROP_POINTER) {
      if ((dp->prop->flag_parameter & PARM_RNAPTR) && !(dp->prop->flag & PROP_THICK_WRAP)) {
        fprintf(f,
                "(::%s *) &%s.ptr",
                rna_parameter_type_name(dp->prop),
                rna_safe_id(dp->prop->identifier));
      }
      else if (dp->prop->flag_parameter & PARM_OUTPUT) {
        if (dp->prop->flag_parameter & PARM_RNAPTR) {
          fprintf(f, "&%s->ptr", rna_safe_id(dp->prop->identifier));
        }
        else {
          fprintf(f,
                  "(::%s **) &%s->ptr.data",
                  rna_parameter_type_name(dp->prop),
                  rna_safe_id(dp->prop->identifier));
        }
      }
      else if (dp->prop->flag_parameter & PARM_RNAPTR) {
        fprintf(f,
                "(::%s *) &%s",
                rna_parameter_type_name(dp->prop),
                rna_safe_id(dp->prop->identifier));
      }
      else {
        fprintf(f,
                "(::%s *) %s.ptr.data",
                rna_parameter_type_name(dp->prop),
                rna_safe_id(dp->prop->identifier));
      }
    }
    else {
      fprintf(f, "%s", rna_safe_id(dp->prop->identifier));
    }
  }

  fprintf(f, ");\n");
}

static void rna_def_struct_function_impl_cpp(FILE *f, StructRNA *srna, FunctionDefRNA *dfunc)
{
  PropertyDefRNA *dp;
  PointerPropertyRNA *pprop;

  FunctionRNA *func = dfunc->func;

  if (!dfunc->call) {
    return;
  }

  rna_def_struct_function_prototype_cpp(f, srna, dfunc, srna->identifier, 0);

  fprintf(f, " {\n");

  if (func->c_ret) {
    dp = rna_find_parameter_def(func->c_ret);

    if (dp->prop->type == PROP_POINTER) {
      pprop = (PointerPropertyRNA *)dp->prop;

      fprintf(f, "\t\tPointerRNA result;\n");

      if ((dp->prop->flag_parameter & PARM_RNAPTR) == 0) {
        StructRNA *ret_srna = rna_find_struct((const char *)pprop->type);
        fprintf(f, "\t\t::%s *retdata = ", rna_parameter_type_name(dp->prop));
        rna_def_struct_function_call_impl_cpp(f, srna, dfunc);
        if (ret_srna->flag & STRUCT_ID) {
          fprintf(f, "\t\tRNA_id_pointer_create((::ID *) retdata, &result);\n");
        }
        else {
          fprintf(f,
                  "\t\tRNA_pointer_create((::ID *) ptr.owner_id, &RNA_%s, retdata, &result);\n",
                  (const char *)pprop->type);
        }
      }
      else {
        fprintf(f, "\t\tresult = ");
        rna_def_struct_function_call_impl_cpp(f, srna, dfunc);
      }

      fprintf(f, "\t\treturn %s(result);\n", (const char *)pprop->type);
    }
    else {
      fprintf(f, "\t\treturn ");
      rna_def_struct_function_call_impl_cpp(f, srna, dfunc);
    }
  }
  else {
    fprintf(f, "\t\t");
    rna_def_struct_function_call_impl_cpp(f, srna, dfunc);
  }

  fprintf(f, "\t}\n\n");
}

static void rna_def_property_wrapper_funcs(FILE *f, StructDefRNA *dsrna, PropertyDefRNA *dp)
{
  if (dp->prop->getlength) {
    char funcname[2048];
    rna_construct_wrapper_function_name(
        funcname, sizeof(funcname), dsrna->srna->identifier, dp->prop->identifier, "get_length");
    fprintf(f, "int %s(PointerRNA *ptr, int *arraylen)\n", funcname);
    fprintf(f, "{\n");
    fprintf(f, "\treturn %s(ptr, arraylen);\n", rna_function_string(dp->prop->getlength));
    fprintf(f, "}\n\n");
  }
}

static void rna_def_function_wrapper_funcs(FILE *f, StructDefRNA *dsrna, FunctionDefRNA *dfunc)
{
  StructRNA *srna = dsrna->srna;
  FunctionRNA *func = dfunc->func;
  PropertyDefRNA *dparm;

  int first;
  char funcname[2048];

  if (!dfunc->call) {
    return;
  }

  rna_construct_wrapper_function_name(
      funcname, sizeof(funcname), srna->identifier, func->identifier, "func");

  rna_generate_static_parameter_prototypes(f, srna, dfunc, funcname, 0);

  fprintf(f, "\n{\n");

  if (func->c_ret) {
    fprintf(f, "\treturn %s(", dfunc->call);
  }
  else {
    fprintf(f, "\t%s(", dfunc->call);
  }

  first = 1;

  if (func->flag & FUNC_USE_SELF_ID) {
    WRITE_PARAM("_selfid");
  }

  if ((func->flag & FUNC_NO_SELF) == 0) {
    WRITE_PARAM("_self");
  }
  else if (func->flag & FUNC_USE_SELF_TYPE) {
    WRITE_PARAM("_type");
  }

  if (func->flag & FUNC_USE_MAIN) {
    WRITE_PARAM("bmain");
  }

  if (func->flag & FUNC_USE_CONTEXT) {
    WRITE_PARAM("C");
  }

  if (func->flag & FUNC_USE_REPORTS) {
    WRITE_PARAM("reports");
  }

  dparm = dfunc->cont.properties.first;
  for (; dparm; dparm = dparm->next) {
    if (dparm->prop == func->c_ret) {
      continue;
    }

    WRITE_COMMA;

    if (dparm->prop->flag & PROP_DYNAMIC) {
      fprintf(f, "%s_len, %s", dparm->prop->identifier, dparm->prop->identifier);
    }
    else {
      fprintf(f, "%s", rna_safe_id(dparm->prop->identifier));
    }
  }

  fprintf(f, ");\n");
  fprintf(f, "}\n\n");
}

static void rna_def_function_funcs(FILE *f, StructDefRNA *dsrna, FunctionDefRNA *dfunc)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyDefRNA *dparm;
  PropertyType type;
  const char *funcname, *valstr;
  const char *ptrstr;
  const bool has_data = (dfunc->cont.properties.first != NULL);
  int flag, flag_parameter, pout, cptr, first;

  srna = dsrna->srna;
  func = dfunc->func;

  if (!dfunc->call) {
    return;
  }

  funcname = rna_alloc_function_name(srna->identifier, func->identifier, "call");

  /* function definition */
  fprintf(f,
          "void %s(bContext *C, ReportList *reports, PointerRNA *_ptr, ParameterList *_parms)",
          funcname);
  fprintf(f, "\n{\n");

  /* variable definitions */

  if (func->flag & FUNC_USE_SELF_ID) {
    fprintf(f, "\tstruct ID *_selfid;\n");
  }

  if ((func->flag & FUNC_NO_SELF) == 0) {
    if (dsrna->dnafromprop) {
      fprintf(f, "\tstruct %s *_self;\n", dsrna->dnafromname);
    }
    else if (dsrna->dnaname) {
      fprintf(f, "\tstruct %s *_self;\n", dsrna->dnaname);
    }
    else {
      fprintf(f, "\tstruct %s *_self;\n", srna->identifier);
    }
  }
  else if (func->flag & FUNC_USE_SELF_TYPE) {
    fprintf(f, "\tstruct StructRNA *_type;\n");
  }

  dparm = dfunc->cont.properties.first;
  for (; dparm; dparm = dparm->next) {
    type = dparm->prop->type;
    flag = dparm->prop->flag;
    flag_parameter = dparm->prop->flag_parameter;
    pout = (flag_parameter & PARM_OUTPUT);
    cptr = ((type == PROP_POINTER) && !(flag_parameter & PARM_RNAPTR));

    if (dparm->prop == func->c_ret) {
      ptrstr = cptr || dparm->prop->arraydimension ? "*" : "";
      /* XXX only arrays and strings are allowed to be dynamic, is this checked anywhere? */
    }
    else if (cptr || (flag & PROP_DYNAMIC)) {
      if (type == PROP_STRING) {
        ptrstr = pout ? "*" : "";
      }
      else {
        ptrstr = pout ? "**" : "*";
      }
      /* Fixed size arrays and RNA pointers are pre-allocated on the ParameterList stack,
       * pass a pointer to it. */
    }
    else if (type == PROP_POINTER || dparm->prop->arraydimension) {
      ptrstr = "*";
    }
    else if ((type == PROP_POINTER) && (flag_parameter & PARM_RNAPTR) &&
             !(flag & PROP_THICK_WRAP)) {
      ptrstr = "*";
      /* PROP_THICK_WRAP strings are pre-allocated on the ParameterList stack,
       * but type name for string props is already (char *), so leave empty */
    }
    else if (type == PROP_STRING && (flag & PROP_THICK_WRAP)) {
      ptrstr = "";
    }
    else {
      ptrstr = pout ? "*" : "";
    }

    /* for dynamic parameters we pass an additional int for the length of the parameter */
    if (flag & PROP_DYNAMIC) {
      fprintf(f, "\tint %s%s_len;\n", pout ? "*" : "", dparm->prop->identifier);
    }

    fprintf(f,
            "\t%s%s %s%s;\n",
            rna_type_struct(dparm->prop),
            rna_parameter_type_name(dparm->prop),
            ptrstr,
            dparm->prop->identifier);
  }

  if (has_data) {
    fprintf(f, "\tchar *_data");
    if (func->c_ret) {
      fprintf(f, ", *_retdata");
    }
    fprintf(f, ";\n");
    fprintf(f, "\t\n");
  }

  /* assign self */
  if (func->flag & FUNC_USE_SELF_ID) {
    fprintf(f, "\t_selfid = (struct ID *)_ptr->owner_id;\n");
  }

  if ((func->flag & FUNC_NO_SELF) == 0) {
    if (dsrna->dnafromprop) {
      fprintf(f, "\t_self = (struct %s *)_ptr->data;\n", dsrna->dnafromname);
    }
    else if (dsrna->dnaname) {
      fprintf(f, "\t_self = (struct %s *)_ptr->data;\n", dsrna->dnaname);
    }
    else {
      fprintf(f, "\t_self = (struct %s *)_ptr->data;\n", srna->identifier);
    }
  }
  else if (func->flag & FUNC_USE_SELF_TYPE) {
    fprintf(f, "\t_type = _ptr->type;\n");
  }

  if (has_data) {
    fprintf(f, "\t_data = (char *)_parms->data;\n");
  }

  dparm = dfunc->cont.properties.first;
  for (; dparm; dparm = dparm->next) {
    type = dparm->prop->type;
    flag = dparm->prop->flag;
    flag_parameter = dparm->prop->flag_parameter;
    pout = (flag_parameter & PARM_OUTPUT);
    cptr = ((type == PROP_POINTER) && !(flag_parameter & PARM_RNAPTR));

    if (dparm->prop == func->c_ret) {
      fprintf(f, "\t_retdata = _data;\n");
    }
    else {
      const char *data_str;
      if (cptr || (flag & PROP_DYNAMIC)) {
        if (type == PROP_STRING) {
          ptrstr = "*";
          valstr = "";
        }
        else {
          ptrstr = "**";
          valstr = "*";
        }
      }
      else if ((type == PROP_POINTER) && !(flag & PROP_THICK_WRAP)) {
        ptrstr = "**";
        valstr = "*";
      }
      else if (type == PROP_POINTER || dparm->prop->arraydimension) {
        ptrstr = "*";
        valstr = "";
      }
      else if (type == PROP_STRING && (flag & PROP_THICK_WRAP)) {
        ptrstr = "";
        valstr = "";
      }
      else {
        ptrstr = "*";
        valstr = "*";
      }

      /* This must be kept in sync with RNA_parameter_dynamic_length_get_data and
       * RNA_parameter_get, we could just call the function directly, but this is faster. */
      if (flag & PROP_DYNAMIC) {
        fprintf(f,
                "\t%s_len = %s((ParameterDynAlloc *)_data)->array_tot;\n",
                dparm->prop->identifier,
                pout ? "(int *)&" : "(int)");
        data_str = "(&(((ParameterDynAlloc *)_data)->array))";
      }
      else {
        data_str = "_data";
      }
      fprintf(f, "\t%s = ", dparm->prop->identifier);

      if (!pout) {
        fprintf(f, "%s", valstr);
      }

      fprintf(f,
              "((%s%s %s)%s);\n",
              rna_type_struct(dparm->prop),
              rna_parameter_type_name(dparm->prop),
              ptrstr,
              data_str);
    }

    if (dparm->next) {
      fprintf(f, "\t_data += %d;\n", rna_parameter_size(dparm->prop));
    }
  }

  if (dfunc->call) {
    fprintf(f, "\t\n");
    fprintf(f, "\t");
    if (func->c_ret) {
      fprintf(f, "%s = ", func->c_ret->identifier);
    }
    fprintf(f, "%s(", dfunc->call);

    first = 1;

    if (func->flag & FUNC_USE_SELF_ID) {
      fprintf(f, "_selfid");
      first = 0;
    }

    if ((func->flag & FUNC_NO_SELF) == 0) {
      if (!first) {
        fprintf(f, ", ");
      }
      fprintf(f, "_self");
      first = 0;
    }
    else if (func->flag & FUNC_USE_SELF_TYPE) {
      if (!first) {
        fprintf(f, ", ");
      }
      fprintf(f, "_type");
      first = 0;
    }

    if (func->flag & FUNC_USE_MAIN) {
      if (!first) {
        fprintf(f, ", ");
      }
      first = 0;
      fprintf(f, "CTX_data_main(C)"); /* may have direct access later */
    }

    if (func->flag & FUNC_USE_CONTEXT) {
      if (!first) {
        fprintf(f, ", ");
      }
      first = 0;
      fprintf(f, "C");
    }

    if (func->flag & FUNC_USE_REPORTS) {
      if (!first) {
        fprintf(f, ", ");
      }
      first = 0;
      fprintf(f, "reports");
    }

    dparm = dfunc->cont.properties.first;
    for (; dparm; dparm = dparm->next) {
      if (dparm->prop == func->c_ret) {
        continue;
      }

      if (!first) {
        fprintf(f, ", ");
      }
      first = 0;

      if (dparm->prop->flag & PROP_DYNAMIC) {
        fprintf(f, "%s_len, %s", dparm->prop->identifier, dparm->prop->identifier);
      }
      else {
        fprintf(f, "%s", dparm->prop->identifier);
      }
    }

    fprintf(f, ");\n");

    if (func->c_ret) {
      dparm = rna_find_parameter_def(func->c_ret);
      ptrstr = (((dparm->prop->type == PROP_POINTER) &&
                 !(dparm->prop->flag_parameter & PARM_RNAPTR)) ||
                (dparm->prop->arraydimension)) ?
                   "*" :
                   "";
      fprintf(f,
              "\t*((%s%s %s*)_retdata) = %s;\n",
              rna_type_struct(dparm->prop),
              rna_parameter_type_name(dparm->prop),
              ptrstr,
              func->c_ret->identifier);
    }
  }

  fprintf(f, "}\n\n");

  dfunc->gencall = funcname;
}


