#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_dynlib.h"

struct DynamicLib {
  void *handle;
};

#ifdef WIN32
#  define WIN32_LEAN_AND_MEAN
#  include "utf_winfn.hh"
#  include "utfconv.hh"
#  include <windows.h>

DynamicLib *lib_dynlib_open(const char *name)
{
  DynamicLib *lib;
  void *handle;

  UTF16_ENCODE(name);
  handle = LoadLib(name_16);
  UTF16_UN_ENCODE(name);

  if (!handle) {
    return NULL;
  }
  
  lib = mem_cnew<DynamicLib>("Dynamic Lib");
  lib->handle = handle;

  return lib;
}

void *lib_dynlib_find_symbol(DynamicLib *lib, const char *symname)
{
  return GetProcAddress(HMODULE(lib->handle), symname);
}

char *lib_dynlib_get_error_as_string(DynamicLib *lib)
{
  int err;

  /* if lib is NULL reset the last error code */
  err = GetLastError();
  if (!lib) {
    SetLastError(ERROR_SUCCESS);
  }

  if (err) {
    static char buf[1024];

    if (FormatMsg(FORMAT_MSG_FROM_SYS | FORMAT_MSG_IGNORE_INSERTS,
                      NULL,
                      err,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      buf,
                      sizeof(buf),
                      NULL))
    {
      return buf;
    }
  }

  return NULL;
}

void lib_dynlib_close(DynamicLib *lib)
{
  FreeLib(HMODULE(lib->handle));
  mem_free(lib);
}

#else /* Unix */

#  include <dlfcn.h>

DynamicLib *lib_dynlib_open(const char *name)
{
  DynamicLib *lib;
  void *handle = dlopen(name, RTLD_LAZY);

  if (!handle) {
    return nullptr;
  }

  lib = mem_cnew<DynamicLib>("Dynamic Lib");
  lib->handle = handle;

  return lib;
}

void *lib_dynlib_find_symbol(DynamicLib *lib, const char *symname)
{
  return dlsym(lib->handle, symname);
}

char *lib_dynlib_get_error_as_string(DynamicLib *lib)
{
  (void)lib; /* unused */
  return dlerror();
}

void lib_dynlib_close(DynamicLib *lib)
{
  dlclose(lib->handle);
  mem_free(lib);
}

#endif
