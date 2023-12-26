#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib_math_base.h"
#include "lib_string.h"
#include "lib_sys.h"
#include "lib_utildefines.h"

#include "lib_guardedalloc.h"

/* for backtrace and gethostname/GetComputerName */
#if defined(WIN32)
#  include <intrin.h>

#  include "lib_winstuff.h"
#else
#  if defined(HAVE_EXECINFO_H)
#    include <execinfo.h>
#  endif
#  include <unistd.h>
#endif

int lib_cpu_support_sse2(void)
{
#if defined(__x86_64__) || defined(_M_X64)
  /* x86_64 always has SSE2 instructions */
  return 1;
#elif defined(__GNUC__) && defined(i386)
  /* for GCC x86 we check cpuid */
  uint d;
  __asm__(
      "pushl %%ebx\n\t"
      "cpuid\n\t"
      "popl %%ebx\n\t"
      : "=d"(d)
      : "a"(1));
  return (d & 0x04000000) != 0;
#elif (defined(_MSC_VER) && defined(_M_IX86))
  /* also check cpuid for MSVC x86 */
  uint d;
  __asm {
    xor     eax, eax
    inc eax
    push ebx
    cpuid
    pop ebx
    mov d, edx
  }
  return (d & 0x04000000) != 0;
#else
  return 0;
#endif
}

/* Windows stack-walk lives in system_win32.c */
#if !defined(_MSC_VER)
void lib_sys_backtrace(FILE *fp)
{
  /* If sys as exinfo.h */
#  if defined(HAVE_EXINFO_H)

#    define SIZE 100
  void *buf[SIZE];
  int nptrs;
  char **strings;
  int i;

  /* Include a back-trace for good measure.
   *
   * Often vals printed are addresses (no line numbers of fn names),
   * this info can be expanded using `addr2line`, a util is included to
   * conveniently run addr2line on the output generated here:
   *
   *   `./tools/utils/addr2line_backtrace.py --exe=/path/to/dune trace.txt`  */
  nptrs = backtrace(buf, SIZE);
  strings = backtrace_symbols(buf, nptrs);
  for (i = 0; i < nptrs; i++) {
    fputs(strings[i], fp);
    fputc('\n', fp);
  }

  free(strings);
#    undef SIZE

#  else
  /* Non MSVC/Apple/Linux. */
  (void)fp;
#  endif
}
#endif
/* end lib_sys_backtrace */
/* The code for CPU brand string is adopted from Cycles. */
#if !defined(_WIN32) || defined(FREE_WINDOWS)
static void __cpuid(
    /* Cannot be const, bc it is modified below.
     * NOLINTNEXTLINE: readability-non-const-param. */
    int data[4],
    int selector)
{
#  if defined(__x86_64__)
  asm("cpuid" : "=a"(data[0]), "=b"(data[1]), "=c"(data[2]), "=d"(data[3]) : "a"(selector));
#  elif defined(__i386__)
  asm("pushl %%ebx    \n\t"
      "cpuid          \n\t"
      "movl %%ebx, %1 \n\t"
      "popl %%ebx     \n\t"
      : "=a"(data[0]), "=r"(data[1]), "=c"(data[2]), "=d"(data[3])
      : "a"(selector)
      : "ebx");
#  else
  (void)sel;
  data[0] = data[1] = data[2] = data[3] = 0;
#  endif
}
#endif

char *lib_cpu_brand_string(void)
{
  char buf[49] = {0};
  int result[4] = {0};
  __cpuid(result, 0x80000000);
  if (result[0] >= (int)0x80000004) {
    __cpuid((int *)(buf + 0), 0x80000002);
    __cpuid((int *)(buf + 16), 0x80000003);
    __cpuid((int *)(buf + 32), 0x80000004);
    char *brand = lib_strdup(buf);
    /* TODO: Make it a bit more presentable by removing trademark. */
    return brand;
  }
  return NULL;
}

int lib_cpu_support_sse41(void)
{
  int result[4], num;
  __cpuid(result, 0);
  num = result[0];

  if (num >= 1) {
    __cpuid(result, 0x00000001);
    return (result[2] & ((int)1 << 19)) != 0;
  }
  return 0;
}

void lib_hostname_get(char *buf, size_t bufsize)
{
#ifndef WIN32
  if (gethostname(buf, bufsize - 1) < 0) {
    lib_strncpy(buf, "-unknown-", bufsize);
  }
  /* When `gethostname()` truncates, it doesn't guarantee the trailing `\0`. */
  buf[bufsize - 1] = '\0';
#else
  DWORD bufsize_inout = bufsize;
  if (!GetComputerName(buf, &bufsize_inout)) {
    lib_strncpy(buf, "-unknown-", bufsize);
  }
#endif
}

size_t lib_sys_mem_max_in_megabytes(voice)
{
  /* Max addressable bytes on this platform
   * Due to the shift arithmetic this is a half of the mem. */
  const size_t limit_bytes_half = (((size_t)1) << (sizeof(size_t[8]) - 1));
  /* Convert it to megabytes and return. */
  return (limit_bytes_half >> 20) * 2;
}

int lib_sys_mem_max_in_megabytes_int(void)
{
  const size_t limit_megabytes = lib_sys_mem_max_in_megabytes();
  /* The result will fit into integer. */
  return (int)min_zz(limit_megabytes, (size_t)INT_MAX);
}
