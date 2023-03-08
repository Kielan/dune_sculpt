#include "../mem_guardedalloc.h"
#include <new>

void *operator new(size_t size, const char *str);
void *operator new[](size_t size, const char *str);

/* not default but can be used when needing to set a string */
void *operator new(size_t size, const char *str)
{
  return mem_mallocn(size, str);
}
void *operator new[](size_t size, const char *str)
{
  return mem_mallocn(size, str);
}

void *operator new(size_t size)
{
  return mem_mallocn(size, "C++/anonymous");
}
void *operator new[](size_t size)
{
  return mem_mallocn(size, "C++/anonymous[]");
}

void operator delete(void *p) throw()
{
  /* delete NULL is valid in c++ */
  if (p)
    mem_freen(p);
}
void operator delete[](void *p) throw()
{
  /* delete NULL is valid in c++ */
  if (p)
    mem_freen(p);
}
