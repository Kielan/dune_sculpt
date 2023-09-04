/* Parsing of and code generation using GLSL shaders in gpu/shaders/material. */
#pragma once

#include "gpu_material.h"

#define MAX_FN_NAME 64
#define MAX_PARAM 36

struct GSet;

typedef struct GPUMaterialLib {
  char *code;
  struct GPUMaterialLib *dependencies[8];
} GPUMaterialLib;

typedef enum {
  FN_QUAL_IN,
  FN_QUAL_OUT,
  FN_QUAL_INOUT,
} GPUFnQual;

typedef struct GPUFn {
  char name[MAX_FN_NAME];
  eGPUType paramtype[MAX_PARAM];
  GPUFnQual paramqual[MAX_PARAM];
  int totparam;
  GPUMaterialLib *lib;
} GPUFn;

/* Module */

void gpu_material_lib_init(void);
void gpu_material_lib_exit(void);

/* Code Generation */

GPUFn *gpu_material_lib_use_fn(struct GSet *used_libs, const char *name);
char *gpu_material_lib_generate_code(struct GSet *used_libs, const char *frag_lib);

/* Code Parsing */

const char *gpu_str_skip_token(const char *str, char *token, int max);
const char *gpu_data_type_to_string(eGPUType type);
