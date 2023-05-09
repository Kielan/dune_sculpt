#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_string.h"
#include "BLI_system.h" /* for 'BLI_system_backtrace' stub. */
#include "BLI_utildefines.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"
#include "RNA_types.h"

#include "rna_internal.h"

#ifdef _WIN32
#  ifndef snprintf
#    define snprintf _snprintf
#  endif
#endif

#include "CLG_log.h"

static CLG_LogRef LOG = {"makesrna"};

/**
 * Variable to control debug output of makesrna.
 * debugSRNA:
 * - 0 = no output, except errors
 * - 1 = detail actions
 */
static int debugSRNA = 0;

/* stub for BLI_abort() */
#ifndef NDEBUG
void BLI_system_backtrace(FILE *fp)
{
  (void)fp;
}
#endif

/* Replace if different */
#define TMP_EXT ".tmp"

/* copied from BLI_file_older */
#include <sys/stat.h>
static int file_older(const char *file1, const char *file2)
{
  struct stat st1, st2;
  if (debugSRNA > 0) {
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
static const char *makesrna_path = NULL;

/* forward declarations */
static void rna_generate_static_parameter_prototypes(FILE *f,
                                                     StructRNA *srna,
                                                     FunctionDefRNA *dfunc,
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
        CLOG_ERROR(&LOG, "remove error (%s): \"%s\"", strerror(errno), orgfile); \
        return -1; \
      } \
    } \
  } \
  if (rename(tmpfile, orgfile) != 0) { \
    CLOG_ERROR(&LOG, "rename error (%s): \"%s\" -> \"%s\"", strerror(errno), tmpfile, orgfile); \
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
     * for development on makesrna.c you may want to disable this */
    if (file_older(orgfile, __FILE__)) {
      REN_IF_DIFF;
    }

    if (file_older(orgfile, makesrna_path)) {
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
    CLOG_ERROR(&LOG, "open error: \"%s\"", tmpfile);
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
  arr_new = MEM_mallocN(sizeof(char) * len_new, "rna_cmp_file_new");
  arr_org = MEM_mallocN(sizeof(char) * len_org, "rna_cmp_file_org");

  if (fread(arr_new, sizeof(char), len_new, fp_new) != len_new) {
    CLOG_ERROR(&LOG, "unable to read file %s for comparison.", tmpfile);
  }
  if (fread(arr_org, sizeof(char), len_org, fp_org) != len_org) {
    CLOG_ERROR(&LOG, "unable to read file %s for comparison.", orgfile);
  }

  fclose(fp_new);
  fp_new = NULL;
  fclose(fp_org);
  fp_org = NULL;

  cmp = memcmp(arr_new, arr_org, len_new);

  MEM_freeN(arr_new);
  MEM_freeN(arr_org);

  if (cmp) {
    REN_IF_DIFF;
  }
  remove(tmpfile);
  return 0;

#undef REN_IF_DIFF
}

/* Helper to solve keyword problems with C/C++ */

static const char *rna_safe_id(const char *id)
{
  if (STREQ(id, "default")) {
    return "default_value";
  }
  if (STREQ(id, "operator")) {
    return "operator_value";
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
  const StructRNA *structa = *(const StructRNA **)a;
  const StructRNA *structb = *(const StructRNA **)b;

  return strcmp(structa->identifier, structb->identifier);
}

static int cmp_property(const void *a, const void *b)
{
  const PropertyRNA *propa = *(const PropertyRNA **)a;
  const PropertyRNA *propb = *(const PropertyRNA **)b;

  if (STREQ(propa->identifier, "rna_type")) {
    return -1;
  }
  if (STREQ(propb->identifier, "rna_type")) {
    return 1;
  }

  if (STREQ(propa->identifier, "name")) {
    return -1;
  }
  if (STREQ(propb->identifier, "name")) {
    return 1;
  }

  return strcmp(propa->name, propb->name);
}

static int cmp_def_struct(const void *a, const void *b)
{
  const StructDefRNA *dsa = *(const StructDefRNA **)a;
  const StructDefRNA *dsb = *(const StructDefRNA **)b;

  return cmp_struct(&dsa->srna, &dsb->srna);
}

static int cmp_def_property(const void *a, const void *b)
{
  const PropertyDefRNA *dpa = *(const PropertyDefRNA **)a;
  const PropertyDefRNA *dpb = *(const PropertyDefRNA **)b;

  return cmp_property(&dpa->prop, &dpb->prop);
}

static void rna_sortlist(ListBase *listbase, int (*cmp)(const void *, const void *))
{
  Link *link;
  void **array;
  int a, size;

  if (listbase->first == listbase->last) {
    return;
  }

  for (size = 0, link = listbase->first; link; link = link->next) {
    size++;
  }

  array = MEM_mallocN(sizeof(void *) * size, "rna_sortlist");
  for (a = 0, link = listbase->first; link; link = link->next, a++) {
    array[a] = link;
  }

  qsort(array, size, sizeof(void *), cmp);

  listbase->first = listbase->last = NULL;
  for (a = 0; a < size; a++) {
    link = array[a];
    link->next = link->prev = NULL;
    rna_addtail(listbase, link);
  }

  MEM_freeN(array);
}

/* Preprocessing */

static void rna_print_c_string(FILE *f, const char *str)
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

static void rna_print_data_get(FILE *f, PropertyDefRNA *dp)
{
  if (dp->dnastructfromname && dp->dnastructfromprop) {
    fprintf(f,
            "    %s *data = (%s *)(((%s *)ptr->data)->%s);\n",
            dp->dnastructname,
            dp->dnastructname,
            dp->dnastructfromname,
            dp->dnastructfromprop);
  }
  else {
    fprintf(f, "    %s *data = (%s *)(ptr->data);\n", dp->dnastructname, dp->dnastructname);
  }
}

static void rna_print_id_get(FILE *f, PropertyDefRNA *UNUSED(dp))
{
  fprintf(f, "    ID *id = ptr->owner_id;\n");
}

static void rna_construct_function_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
}

static void rna_construct_wrapper_function_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  if (type == NULL || type[0] == '\0') {
    snprintf(buffer, size, "%s_%s", structname, propname);
  }
  else {
    snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
  }
}

void *rna_alloc_from_buffer(const char *buffer, int buffer_len)
{
  AllocDefRNA *alloc = MEM_callocN(sizeof(AllocDefRNA), "AllocDefRNA");
  alloc->mem = MEM_mallocN(buffer_len, __func__);
  memcpy(alloc->mem, buffer, buffer_len);
  rna_addtail(&DefRNA.allocs, alloc);
  return alloc->mem;
}

void *rna_calloc(int buffer_len)
{
  AllocDefRNA *alloc = MEM_callocN(sizeof(AllocDefRNA), "AllocDefRNA");
  alloc->mem = MEM_callocN(buffer_len, __func__);
  rna_addtail(&DefRNA.allocs, alloc);
  return alloc->mem;
}

static char *rna_alloc_function_name(const char *structname,
                                     const char *propname,
                                     const char *type)
{
  char buffer[2048];
  rna_construct_function_name(buffer, sizeof(buffer), structname, propname, type);
  return rna_alloc_from_buffer(buffer, strlen(buffer) + 1);
}

static StructRNA *rna_find_struct(const char *identifier)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->srna->identifier, identifier)) {
      return ds->srna;
    }
  }

  return NULL;
}

static const char *rna_find_type(const char *type)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (ds->dnaname && STREQ(ds->dnaname, type)) {
      return ds->srna->identifier;
    }
  }

  return NULL;
}

static const char *rna_find_dna_type(const char *type)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->srna->identifier, type)) {
      return ds->dnaname;
    }
  }

  return NULL;
}

static const char *rna_type_type_name(PropertyRNA *prop)
{
  switch (prop->type) {
    case PROP_BOOLEAN:
      return "bool";
    case PROP_INT:
      return "int";
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
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

static const char *rna_type_type(PropertyRNA *prop)
{
  const char *type;

  type = rna_type_type_name(prop);

  if (type) {
    return type;
  }

  return "PointerRNA";
}

static const char *rna_type_struct(PropertyRNA *prop)
{
  const char *type;

  type = rna_type_type_name(prop);

  if (type) {
    return "";
  }

  return "struct ";
}

static const char *rna_parameter_type_name(PropertyRNA *parm)
{
  const char *type;

  type = rna_type_type_name(parm);

  if (type) {
    return type;
  }

  switch (parm->type) {
    case PROP_POINTER: {
      PointerPropertyRNA *pparm = (PointerPropertyRNA *)parm;

      if (parm->flag_parameter & PARM_RNAPTR) {
        return "PointerRNA";
      }
      return rna_find_dna_type((const char *)pparm->type);
    }
    case PROP_COLLECTION: {
      return "CollectionListBase";
    }
    default:
      return "<error, no type specified>";
  }
}

static int rna_enum_bitmask(PropertyRNA *prop)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  int a, mask = 0;

  if (eprop->item) {
    for (a = 0; a < eprop->totitem; a++) {
      if (eprop->item[a].identifier[0]) {
        mask |= eprop->item[a].value;
      }
    }
  }

  return mask;
}

static int rna_color_quantize(PropertyRNA *prop, PropertyDefRNA *dp)
{
  return ((prop->type == PROP_FLOAT) && (ELEM(prop->subtype, PROP_COLOR, PROP_COLOR_GAMMA)) &&
          (IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0));
}

/**
 * Return the identifier for an enum which is defined in "RNA_enum_items.h".
 *
 * Prevents expanding duplicate enums bloating the binary size.
 */
static const char *rna_enum_id_from_pointer(const EnumPropertyItem *item)
{
#define RNA_MAKESRNA
#define DEF_ENUM(id) \
  if (item == id) { \
    return STRINGIFY(id); \
  }
#include "RNA_enum_items.h"
#undef RNA_MAKESRNA
  return NULL;
}

static const char *rna_function_string(const void *func)
{
  return (func) ? (const char *)func : "NULL";
}

static void rna_float_print(FILE *f, float num)
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

static void rna_int_print(FILE *f, int64_t num)
{
  if (num == INT_MIN) {
    fprintf(f, "INT_MIN");
  }
  else if (num == INT_MAX) {
    fprintf(f, "INT_MAX");
  }
  else if (num == INT64_MIN) {
    fprintf(f, "INT64_MIN");
  }
  else if (num == INT64_MAX) {
    fprintf(f, "INT64_MAX");
  }
  else if (num < INT_MIN || num > INT_MAX) {
    fprintf(f, "%" PRId64 "LL", num);
  }
  else {
    fprintf(f, "%d", (int)num);
  }
}

static char *rna_def_property_get_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      return NULL;
    }

    /* Type check. */
    if (dp->dnatype && *dp->dnatype) {

      if (prop->type == PROP_FLOAT) {
        if (IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0) {
          /* Colors are an exception. these get translated. */
          if (prop->subtype != PROP_COLOR_GAMMA) {
            CLOG_ERROR(&LOG,
                       "%s.%s is a '%s' but wrapped as type '%s'.",
                       srna->identifier,
                       prop->identifier,
                       dp->dnatype,
                       RNA_property_typename(prop->type));
            DefRNA.error = true;
            return NULL;
          }
        }
      }
      else if (prop->type == PROP_BOOLEAN) {
        if (IS_DNATYPE_BOOLEAN_COMPAT(dp->dnatype) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     srna->identifier,
                     prop->identifier,
                     dp->dnatype,
                     RNA_property_typename(prop->type));
          DefRNA.error = true;
          return NULL;
        }
      }
      else if (ELEM(prop->type, PROP_INT, PROP_ENUM)) {
        if (IS_DNATYPE_INT_COMPAT(dp->dnatype) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     srna->identifier,
                     prop->identifier,
                     dp->dnatype,
                     RNA_property_typename(prop->type));
          DefRNA.error = true;
          return NULL;
        }
      }
    }

    /* Check log scale sliders for negative range. */
    if (prop->type == PROP_FLOAT) {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
      /* NOTE: UI_BTYPE_NUM_SLIDER can't have a softmin of zero. */
      if ((fprop->ui_scale_type == PROP_SCALE_LOG) && (fprop->hardmin < 0 || fprop->softmin < 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale < 0.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }
    if (prop->type == PROP_INT) {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
      /* Only UI_BTYPE_NUM_SLIDER is implemented and that one can't have a softmin of zero. */
      if ((iprop->ui_scale_type == PROP_SCALE_LOG) &&
          (iprop->hardmin <= 0 || iprop->softmin <= 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale <= 0.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "get");

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      fprintf(f, "void %s(PointerRNA *ptr, char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropertySubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        rna_print_data_get(f, dp);

        if (dp->dnapointerlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(f, "    if (data->%s == NULL) {\n", dp->dnaname);
          fprintf(f, "        *value = '\\0';\n");
          fprintf(f, "        return;\n");
          fprintf(f, "    }\n");
          fprintf(f,
                  "    %s(value, data->%s, strlen(data->%s) + 1);\n",
                  string_copy_func,
                  dp->dnaname,
                  dp->dnaname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(value, data->%s, %d);\n",
                    string_copy_func,
                    dp->dnaname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(value, data->%s, sizeof(data->%s));\n",
                    string_copy_func,
                    dp->dnaname,
                    dp->dnaname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_POINTER: {
      fprintf(f, "PointerRNA %s(PointerRNA *ptr)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    return %s(ptr);\n", manualfunc);
      }
      else {
        PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
        rna_print_data_get(f, dp);
        if (dp->dnapointerlevel == 0) {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(ptr, &RNA_%s, &data->%s);\n",
                  (const char *)pprop->type,
                  dp->dnaname);
        }
        else {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(ptr, &RNA_%s, data->%s);\n",
                  (const char *)pprop->type,
                  dp->dnaname);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;

      fprintf(f, "static PointerRNA %s(CollectionPropertyIterator *iter)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        if (STR_ELEM(manualfunc,
                     "rna_iterator_listbase_get",
                     "rna_iterator_array_get",
                     "rna_iterator_array_dereference_get")) {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(&iter->parent, &RNA_%s, %s(iter));\n",
                  (cprop->item_type) ? (const char *)cprop->item_type : "UnknownType",
                  manualfunc);
        }
        else {
          fprintf(f, "    return %s(iter);\n", manualfunc);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(PointerRNA *ptr, %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(PointerRNA *ptr, %s values[%u])\n",
                  func,
                  rna_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfunc = rna_alloc_function_name(
                srna->identifier, rna_safe_id(prop->identifier), "get_length");
            fprintf(f, "    unsigned int arraylen[RNA_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int i;\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfunc);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            MEM_freeN(lenfunc);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->dnaarraylength == 1) {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s & (",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i)) != 0);\n");
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((&data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
            }
          }
          else {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s[i] & ",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, ") != 0);\n");
            }
            else if (rna_color_quantize(prop, dp)) {
              fprintf(f,
                      "        values[i] = (%s)(data->%s[i] * (1.0f / 255.0f));\n",
                      rna_type_type(prop),
                      dp->dnaname);
            }
            else if (dp->dnatype) {
              fprintf(f,
                      "        values[i] = (%s)%s(((%s *)data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnatype,
                      dp->dnaname);
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
            }
          }
          fprintf(f, "    }\n");
        }
        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "%s %s(PointerRNA *ptr)\n", rna_type_type(prop), func);
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    return %s(ptr);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);
          if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
            fprintf(
                f, "    return %s(((data->%s) & ", (dp->booleannegative) ? "!" : "", dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, ") != 0);\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    return ((data->%s) & ", dp->dnaname);
            rna_int_print(f, rna_enum_bitmask(prop));
            fprintf(f, ");\n");
          }
          else {
            fprintf(f,
                    "    return (%s)%s(data->%s);\n",
                    rna_type_type(prop),
                    (dp->booleannegative) ? "!" : "",
                    dp->dnaname);
          }
        }

        fprintf(f, "}\n\n");
      }
      break;
  }

  return func;
}

/* defined min/max variables to be used by rna_clamp_value() */
static void rna_clamp_value_range(FILE *f, PropertyRNA *prop)
{
  if (prop->type == PROP_FLOAT) {
    FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
    if (fprop->range) {
      fprintf(f,
              "    float prop_clamp_min = -FLT_MAX, prop_clamp_max = FLT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              rna_function_string(fprop->range));
    }
  }
  else if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
    if (iprop->range) {
      fprintf(f,
              "    int prop_clamp_min = INT_MIN, prop_clamp_max = INT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              rna_function_string(iprop->range));
    }
  }
}

#ifdef USE_RNA_RANGE_CHECK
static void rna_clamp_value_range_check(FILE *f,
                                        PropertyRNA *prop,
                                        const char *dnaname_prefix,
                                        const char *dnaname)
{
  if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
    fprintf(f,
            "    { BLI_STATIC_ASSERT("
            "(TYPEOF_MAX(%s%s) >= %d) && "
            "(TYPEOF_MIN(%s%s) <= %d), "
            "\"invalid limits\"); }\n",
            dnaname_prefix,
            dnaname,
            iprop->hardmax,
            dnaname_prefix,
            dnaname,
            iprop->hardmin);
  }
}
#endif /* USE_RNA_RANGE_CHECK */

static void rna_clamp_value(FILE *f, PropertyRNA *prop, int array)
{
  if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

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
        rna_int_print(f, iprop->hardmin);
        fprintf(f, ", ");
        rna_int_print(f, iprop->hardmax);
        fprintf(f, ");\n");
      }
      return;
    }
  }
  else if (prop->type == PROP_FLOAT) {
    FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

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
        rna_float_print(f, fprop->hardmin);
        fprintf(f, ", ");
        rna_float_print(f, fprop->hardmax);
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

static char *rna_def_property_set_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func;

  if (!(prop->flag & PROP_EDITABLE)) {
    return NULL;
  }
  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      if (prop->flag & PROP_EDITABLE) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
        DefRNA.error = true;
      }
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "set");

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      fprintf(f, "void %s(PointerRNA *ptr, const char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropertySubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        rna_print_data_get(f, dp);

        if (dp->dnapointerlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(
              f, "    if (data->%s != NULL) { MEM_freeN(data->%s); }\n", dp->dnaname, dp->dnaname);
          fprintf(f, "    const int length = strlen(value);\n");
          fprintf(f, "    data->%s = MEM_mallocN(length + 1, __func__);\n", dp->dnaname);
          fprintf(f, "    %s(data->%s, value, length + 1);\n", string_copy_func, dp->dnaname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(data->%s, value, %d);\n",
                    string_copy_func,
                    dp->dnaname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(data->%s, value, sizeof(data->%s));\n",
                    string_copy_func,
                    dp->dnaname,
                    dp->dnaname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_POINTER: {
      fprintf(f, "void %s(PointerRNA *ptr, PointerRNA value, struct ReportList *reports)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value, reports);\n", manualfunc);
      }
      else {
        rna_print_data_get(f, dp);

        if (prop->flag & PROP_ID_SELF_CHECK) {
          rna_print_id_get(f, dp);
          fprintf(f, "    if (id == value.data) {\n");
          fprintf(f, "      return;\n");
          fprintf(f, "    }\n");
        }

        if (prop->flag & PROP_ID_REFCOUNT) {
          fprintf(f, "\n    if (data->%s) {\n", dp->dnaname);
          fprintf(f, "        id_us_min((ID *)data->%s);\n", dp->dnaname);
          fprintf(f, "    }\n");
          fprintf(f, "    if (value.data) {\n");
          fprintf(f, "        id_us_plus((ID *)value.data);\n");
          fprintf(f, "    }\n");
        }
        else {
          PointerPropertyRNA *pprop = (PointerPropertyRNA *)dp->prop;
          StructRNA *type = (pprop->type) ? rna_find_struct((const char *)pprop->type) : NULL;
          if (type && (type->flag & STRUCT_ID)) {
            fprintf(f, "    if (value.data) {\n");
            fprintf(f, "        id_lib_extern((ID *)value.data);\n");
            fprintf(f, "    }\n");
          }
        }

        fprintf(f, "    data->%s = value.data;\n", dp->dnaname);
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(PointerRNA *ptr, const %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(PointerRNA *ptr, const %s values[%u])\n",
                  func,
                  rna_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfunc = rna_alloc_function_name(
                srna->identifier, rna_safe_id(prop->identifier), "set_length");
            fprintf(f, "    unsigned int i, arraylen[RNA_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfunc);
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            MEM_freeN(lenfunc);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->dnaarraylength == 1) {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s |= (",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
              fprintf(f, "        else { data->%s &= ~(", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
            }
            else {
              fprintf(
                  f, "        (&data->%s)[i] = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
              rna_clamp_value(f, prop, 1);
            }
          }
          else {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s[i] |= ",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
              fprintf(f, "        else { data->%s[i] &= ~", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
            }
            else if (rna_color_quantize(prop, dp)) {
              fprintf(
                  f, "        data->%s[i] = unit_float_to_uchar_clamp(values[i]);\n", dp->dnaname);
            }
            else {
              if (dp->dnatype) {
                fprintf(f,
                        "        ((%s *)data->%s)[i] = %s",
                        dp->dnatype,
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              else {
                fprintf(f,
                        "        (data->%s)[i] = %s",
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              rna_clamp_value(f, prop, 1);
            }
          }
          fprintf(f, "    }\n");
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          if (dp->dnaarraylength == 1) {
            rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
          }
          else {
            rna_clamp_value_range_check(f, prop, "*data->", dp->dnaname);
          }
        }
#endif

        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "void %s(PointerRNA *ptr, %s value)\n", func, rna_type_type(prop));
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, value);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);
          if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
            fprintf(f,
                    "    if (%svalue) { data->%s |= ",
                    (dp->booleannegative) ? "!" : "",
                    dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
            fprintf(f, "    else { data->%s &= ~", dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    data->%s &= ~", dp->dnaname);
            rna_int_print(f, rna_enum_bitmask(prop));
            fprintf(f, ";\n");
            fprintf(f, "    data->%s |= value;\n", dp->dnaname);
          }
          else {
            rna_clamp_value_range(f, prop);
            fprintf(f, "    data->%s = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
            rna_clamp_value(f, prop, 0);
          }
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
        }
#endif

        fprintf(f, "}\n\n");
      }
      break;
  }

  return func;
}
