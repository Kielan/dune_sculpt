/* A general arg parsing module */
#include <ctype.h> /* for tolower */
#include <stdio.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_args.h"
#include "lib_ghash.h"
#include "lib_list.h"
#include "lib_string.h"
#include "lib_utildefines.h"

static char NO_DOCS[] = "NO DOCUMENTATION SPECIFIED";

struct ArgDoc;
typedef struct ArgDoc {
  struct ArgDoc *next, *prev;
  const char *short_arg;
  const char *long_arg;
  const char *documentation;
  bool done;
} ArgDoc;

typedef struct AKey {
  const char *arg;
  uintptr_t pass; /* cast easier */
  int case_str;   /* case specific or not */
} AKey;

typedef struct Argument {
  AKey *key;
  AArgCb fn;
  void *data;
  ArgDoc *doc;
} Argument;

struct Args {
  List docs;
  GHash *items;
  int argc;
  const char **argv;
  int *passes;
  /* For printing help txt, defaults to `stdout`. */
  ArgPrintFn print_fn;
  void *print_user_data;

  /* Only use when initing args. */
  int current_pass;
};

static uint case_strhash(const void *ptr)
{
  const char *s = ptr;
  uint i = 0;
  uchar c;

  while ((c = tolower(*s++))) {
    i = i * 37 + c;
  }

  return i;
}

static uint keyhash(const void *ptr)
{
  const AKey *k = ptr;
  return case_strhash(k->arg); /* ^ lib_ghashutil_inthash((void *)k->pass); */
}

static bool keycmp(const void *a, const void *b)
{
  const AKey *ka = a;
  const AKey *kb = b;
  if (ka->pass == kb->pass || ka->pass == -1 || kb->pass == -1) { /* -1 is wildcard for pass */
    if (ka->case_str == 1 || kb->case_str == 1) {
      return (lib_strcasecmp(ka->arg, kb->arg) != 0);
    }
    return !STREQ(ka->arg, kb->arg);
  }
  return lib_ghashutil_intcmp((const void *)ka->pass, (const void *)kb->pass);
}

static Argument *lookUp(Args *ba, const char *arg, int pass, int case_str)
{
  AKey key;

  key.case_str = case_str;
  key.pass = pass;
  key.arg = arg;

  return lib_ghash_lookup(ba->items, &key);
}

/* Default print fn. */
ATTR_PRINTF_FORMAT(2, 0)
static void args_print_wrapper(void *UNUSED(user_data), const char *format, va_list args)
{
  vprintf(format, args);
}

bArgs *lib_args_create(int argc, const char **argv)
{
  Args *ba = mem_calloc(sizeof(bArgs), "bArgs");
  ba->passes = mem_calloc(sizeof(int) * argc, "bArgs passes");
  ba->items = lib_ghash_new(keyhash, keycmp, "bArgs passes gh");
  lib_list_clear(&ba->docs);
  ba->argc = argc;
  ba->argv = argv;

  /* Must be init by lib_args_pass_set. */
  ba->current_pass = 0;

  lib_args_print_fn_set(ba, args_print_wrapper, NULL);

  return ba;
}

void lib_args_destroy(Args *ba)
{
  lib_ghash_free(ba->items, MEM_freeN, MEM_freeN);
  mem_free(ba->passes);
  lib_freelist(&ba->docs);
  mem_free(ba);
}

void lib_args_printf(Args *ba, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  ba->print_fn(ba->print_user_data, format, args);
  va_end(args);
}

void lib_args_print_fn_set(Args *ba, ArgPrintFn print_fn, void *user_data)
{
  ba->print_fn = print_fn;
  ba->print_user_data = user_data;
}

void lib_args_pass_set(Args *ba, int current_pass)
{
  lib_assert((current_pass != 0) && (current_pass >= -1));
  ba->current_pass = current_pass;
}

void lib_args_print(bArgs *ba)
{
  int i;
  for (i = 0; i < ba->argc; i++) {
    printf("argv[%d] = %s\n", i, ba->argv[i]);
  }
}

static ArgDoc *internalDocs(Args *ba,
                             const char *short_arg,
                             const char *long_arg,
                             const char *doc)
{
  ArgDoc *d;

  d = mem_calloc(sizeof(ArgDoc), "ArgDoc");

  if (doc == NULL) {
    doc = NO_DOCS;
  }

  d->short_arg = short_arg;
  d->long_arg = long_arg;
  d->documentation = doc;

  lib_addtail(&ba->docs, d);

  return d;
}

static void internalAdd(
    Args *ba, const char *arg, int case_str, BA_ArgCallback cb, void *data, bArgDoc *d)
{
  const int pass = ba->current_pass;
  Argument *a;
  AKey *key;

  a = lookUp(ba, arg, pass, case_str);

  if (a) {
    printf("WARNING: conflicting argument\n");
    printf("\ttrying to add '%s' on pass %i, %scase sensitive\n",
           arg,
           pass,
           case_str == 1 ? "not " : "");
    printf("\tconflict with '%s' on pass %i, %scase sensitive\n\n",
           a->key->arg,
           (int)a->key->pass,
           a->key->case_str == 1 ? "not " : "");
  }

  a = mem_calloc(sizeof(Arg), "Arg");
  key = mem_calloc(sizeof(AKey), "AKey");

  key->arg = arg;
  key->pass = pass;
  key->case_str = case_str;

  a->key = key;
  a->fn = cb;
  a->data = data;
  a->doc = d;

  lib_ghash_insert(ba->items, key, a);
}

void lib_args_add_case(Args *ba,
                       const char *short_arg,
                       int short_case,
                       const char *long_arg,
                       int long_case,
                       const char *doc,
                       AArgCb cb,
                       void *data)
{
  ArgDoc *d = internalDocs(ba, short_arg, long_arg, doc);

  if (short_arg) {
    internalAdd(ba, short_arg, short_case, cb, data, d);
  }

  if (long_arg) {
    internalAdd(ba, long_arg, long_case, cb, data, d);
  }
}

void lib_args_add(Args *ba,
                  const char *short_arg,
                  const char *long_arg,
                  const char *doc,
                  BASrgCb cb,
                  void *data)
{
  lib_args_add_case(ba, short_arg, 0, long_arg, 0, doc, cb, data);
}

static void internalDocPrint(Args *args, ArgDoc *d)
{
  if (d->short_arg && d->long_arg) {
    lib_args_printf(args, "%s or %s", d->short_arg, d->long_arg);
  }
  else if (d->short_arg) {
    lib_args_printf(args, "%s", d->short_arg);
  }
  else if (d->long_arg) {
    lib_args_printf(args, "%s", d->long_arg);
  }

  lib_args_printf(args, " %s\n\n", d->documentation);
}

void lib_args_print_arg_doc(Args *args, const char *arg)
{
  Argument *a = lookUp(args, arg, -1, -1);

  if (a) {
    ArgDoc *d = a->doc;

    internalDocPrint(ba, d);

    d->done = true;
  }
}

void lib_args_print_other_doc(Args *args)
{
  ArgDoc *d;

  for (d = args->docs.first; d; d = d->next) {
    if (d->done == 0) {
      internalDocPrint(args, d);
    }
  }
}

bool lib_args_has_other_doc(const Args *args)
{
  for (const ArgDoc *d = args->docs.first; d; d = d->next) {
    if (d->done == 0) {
      return true;
    }
  }
  return false;
}

void lib_args_parse(Args *args, int pass, A_ArgCb default_cb, void *default_data)
{
  lib_assert((pass != 0) && (pass >= -1));
  int i = 0;

  for (i = 1; i < args->argc; i++) { /* skip argv[0] */
    if (args->passes[i] == 0) {
      /* -1 signal what side of the cmp it is */
      Arg *a = lookUp(args, args->argv[i], pass, -1);
      AArgCb fn = NULL;
      void *data = NULL;

      if (a) {
        fn = a->fn;
        data = a->data;
      }
      else {
        fn = default_cb;
        data = default_data;
      }

      if (fn) {
        int retval = fn(args->argc - i, args->argv + i, data);

        if (retval >= 0) {
          int j;

          /* use extra args */
          for (j = 0; j <= retval; j++) {
            args->passes[i + j] = pass;
          }
          i += retval;
        }
        else if (retval == -1) {
          if (a) {
            if (a->key->pass != -1) {
              args->passes[i] = pass;
            }
          }
          break;
        }
      }
    }
  }
}
