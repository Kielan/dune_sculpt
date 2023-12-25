/* Dynamically sized string ADT. */

#include <stdio.h>
#include <stdlib.h> /* malloc */
#include <string.h>

#include "lib_dynstr.h"
#include "lib_memarena.h"
#include "lib_string.h"
#include "lib_utildefines.h"
#include "mem_guardedalloc.h"

typedef struct DynStrElem DynStrElem;
struct DynStrElem {
  DynStrElem *next;

  char *str;
};

struct DynStr {
  DynStrElem *elems, *last;
  int curlen;
  MemArena *memarena;
};

DynStr *lib_dynstr_new(void)
{
  DynStr *ds = mem_malloc(sizeof(*ds), "DynStr");
  ds->elems = ds->last = NULL;
  ds->curlen = 0;
  ds->memarena = NULL;

  return ds;
}

DynStr *lib_dynstr_new_memarena(void)
{
  DynStr *ds = mem_malloc(sizeof(*ds), "DynStr");
  ds->elems = ds->last = NULL;
  ds->curlen = 0;
  ds->memarena = lib_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);

  return ds;
}

LIB_INLINE void *dynstr_alloc(DynStr *__restrict ds, size_t size)
{
  return ds->memarena ? lib_memarena_alloc(ds->memarena, size) : malloc(size);
}

void lib_dynstr_append(DynStr *__restrict ds, const char *cstr)
{
  DynStrElem *dse = dynstr_alloc(ds, sizeof(*dse));
  int cstrlen = strlen(cstr);

  dse->str = dynstr_alloc(ds, cstrlen + 1);
  memcpy(dse->str, cstr, cstrlen + 1);
  dse->next = NULL;

  if (!ds->last) {
    ds->last = ds->elems = dse;
  }
  else {
    ds->last = ds->last->next = dse;
  }

  ds->curlen += cstrlen;
}

void lib_dynstr_nappend(DynStr *__restrict ds, const char *cstr, int len)
{
  DynStrElem *dse = dynstr_alloc(ds, sizeof(*dse));
  int cstrlen = lib_strnlen(cstr, len);

  dse->str = dynstr_alloc(ds, cstrlen + 1);
  memcpy(dse->str, cstr, cstrlen);
  dse->str[cstrlen] = '\0';
  dse->next = NULL;

  if (!ds->last) {
    ds->last = ds->elems = dse;
  }
  else {
    ds->last = ds->last->next = dse;
  }

  ds->curlen += cstrlen;
}

void lib_dynstr_vappendf(DynStr *__restrict ds, const char *__restrict format, va_list args)
{
  char *str, fixed_buf[256];
  size_t str_len;
  str = lib_vsprintf_with_buf(fixed_buf, sizeof(fixed_buf), &str_len, format, args);
  lib_dynstr_append(ds, str);
  if (str != fixed_buf) {
    mem_free(str);
  }
}

void lib_dynstr_appendf(DynStr *__restrict ds, const char *__restrict format, ...)
{
  va_list args;
  char *str, fixed_buf[256];
  size_t str_len;
  va_start(args, format);
  str = lib_vsprintfN_with_buf(fixed_buf, sizeof(fixed_buf), &str_len, format, args);
  va_end(args);
  if (LIKELY(str)) {
    lib_dynstr_append(ds, str);
    if (str != fixed_buf) {
      mem_free(str);
    }
  }
}

int lib_dynstr_get_len(const DynStr *ds)
{
  return ds->curlen;
}

void lib_dynstr_get_cstring_ex(const DynStr *__restrict ds, char *__restrict rets)
{
  char *s;
  const DynStrElem *dse;

  for (s = rets, dse = ds->elems; dse; dse = dse->next) {
    int slen = strlen(dse->str);

    memcpy(s, dse->str, slen);

    s += slen;
  }
  lib_assert((s - rets) == ds->curlen);
  rets[ds->curlen] = '\0';
}

char *lib_dynstr_get_cstring(const DynStr *ds)
{
  char *rets = mem_malloc(ds->curlen + 1, "dynstr_cstring");
  lib_dynstr_get_cstring_ex(ds, rets);
  return rets;
}

void lib_dynstr_clear(DynStr *ds)
{
  if (ds->memarena) {
    lib_memarena_clear(ds->memarena);
  }
  else {
    for (DynStrElem *dse_next, *dse = ds->elems; dse; dse = dse_next) {
      dse_next = dse->next;

      free(dse->str);
      free(dse);
    }
  }

  ds->elems = ds->last = NULL;
  ds->curlen = 0;
}

void lib_dynstr_free(DynStr *ds)
{
  if (ds->memarena) {
    lib_memarena_free(ds->memarena);
  }
  else {
    lib_dynstr_clear(ds);
  }

  mem_free(ds);
}
