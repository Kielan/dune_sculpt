#pragma once

#include <stdio.h>

int LIB_cpu_support_sse2(void);
int LIB_cpu_support_sse41(void);
void LIB_system_backtrace(FILE *fp);

/** Get CPU brand, result is to be MEM_freeN()-ed. */
char *LIB_cpu_brand_string(void);
