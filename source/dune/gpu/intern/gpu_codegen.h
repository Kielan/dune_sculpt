/* Generate shader code from the intermediate node graph. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct GPUMaterial;
struct GPUNodeGraph;
struct GPUShader;

typedef struct GPUPass {
  struct GPUPass *next;

  struct GPUShader *shader;
  char *fragmentcode;
  char *geometrycode;
  char *vertexcode;
  char *defines;
  uint refcount; /* Orphaned GPUPasses gets freed by the garbage collector. */
  uint32_t hash; /* Identity hash generated from all GLSL code. */
  bool compiled; /* Did we already tried to compile the attached GPUShader. */
} GPUPass;

/* Pass */
GPUPass *gpu_generate_pass(struct GPUMaterial *material,
                           struct GPUNodeGraph *graph,
                           const char *vert_code,
                           const char *geom_code,
                           const char *frag_lib,
                           const char *defines);
struct GPUShader *gpu_pass_shader_get(GPUPass *pass);
bool gpu_pass_compile(GPUPass *pass, const char *shname);
void gpu_pass_release(GPUPass *pass);

/* Module */
void gpu_codegen_init(void);
void gpu_codegen_exit(void);

#ifdef __cplusplus
}
#endif
