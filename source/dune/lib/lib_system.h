#pragma once

#include <stdio.h>

int lib_cpu_support_sse2(void);
int lib_cpu_support_sse41(void);
void lib_system_backtrace(FILE *fp);

/** Get CPU brand, result is to be MEM_freeN()-ed. */
char *lib_cpu_brand_string(void);

/**
 * Obtain the hostname from the system.
 *
 * This simply determines the host's name, and doesn't do any DNS lookup of any
 * IP address of the machine. As such, it's only usable for identification
 * purposes, and not for reachability over a network.
 *
 * param buffer: Character buffer to write the hostname into.
 * param bufsize: Size of the character buffer, including trailing '\0'.
 */
void lib_hostname_get(char *buffer, size_t bufsize);

/** Get maximum addressable memory in megabytes. */
size_t lib_system_memory_max_in_megabytes(void);
/** Get maximum addressable memory in megabytes (clamped to #INT_MAX). */
int lib_system_memory_max_in_megabytes_int(void);

/* For `getpid`. */
#ifdef WIN32
#  define LIB_SYSTEM_PID_H <process.h>

/**
 * Use `void *` for `exception` since we really do not want to drag Windows.h
 * in to get the proper `typedef`.
 */
void lib_windows_handle_exception(void *exception);

#else
#  define LIB_SYSTEM_PID_H <unistd.h>
#endif
