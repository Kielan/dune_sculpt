/* Intermediate node graph for generating GLSL shaders. */

#pragma once

#include "types_customdata.h"
#include "types_list.h"

#include "gpu_material.h"
#include "gpu_shader.h"

struct GPUNode;
struct GPUOutput;
struct List;

typedef enum eGPUDataSource {
  GPU_SOURCE_OUTPUT,
  GPU_SOURCE_CONSTANT,
  GPU_SOURCE_UNIFORM,
  GPU_SOURCE_ATTR,
  GPU_SOURCE_UNIFORM_ATTR,
  GPU_SOURCE_BUILTIN,
  GPU_SOURCE_STRUCT,
  GPU_SOURCE_TEX,
  GPU_SOURCE_TEX_TILED_MAPPING,
  GPU_SOURCE_VOLUME_GRID,
  GPU_SOURCE_VOLUME_GRID_TRANSFORM,
} eGPUDataSource;

typedef enum {
  GPU_NODE_LINK_NONE = 0,
  GPU_NODE_LINK_ATTR,
  GPU_NODE_LINK_UNIFORM_ATTR,
  GPU_NODE_LINK_BUILTIN,
  GPU_NODE_LINK_COLORBAND,
  GPU_NODE_LINK_CONSTANT,
  GPU_NODE_LINK_IMAGE,
  GPU_NODE_LINK_IMAGE_TILED,
  GPU_NODE_LINK_IMAGE_TILED_MAPPING,
  GPU_NODE_LINK_VOLUME_GRID,
  GPU_NODE_LINK_VOLUME_GRID_TRANSFORM,
  GPU_NODE_LINK_OUTPUT,
  GPU_NODE_LINK_UNIFORM,
} GPUNodeLinkType;

struct GPUNode {
  struct GPUNode *next, *prev;

  const char *name;

  /* Internal flag to mark nodes during pruning */
  bool tag;

  List inputs;
  List outputs;
};

struct GPUNodeLink {
  GPUNodeStack *socket;

  GPUNodeLinkType link_type;
  int users; /* Refcount */

  union {
    /* GPU_NODE_LINK_CONSTANT | GPU_NODE_LINK_UNIFORM */
    const float *data;
    /* GPU_NODE_LINK_BUILTIN */
    eGPUBuiltin builtin;
    /* GPU_NODE_LINK_COLORBAND */
    struct GPUTexture **colorband;
    /* GPU_NODE_LINK_VOLUME_GRID */
    struct GPUMaterialVolumeGrid *volume_grid;
    /* GPU_NODE_LINK_OUTPUT */
    struct GPUOutput *output;
    /* GPU_NODE_LINK_ATTR */
    struct GPUMaterialAttribute *attr;
    /* GPU_NODE_LINK_UNIFORM_ATTR */
    struct GPUUniformAttr *uniform_attr;
    /* GPU_NODE_LINK_IMAGE_BLENDER */
    struct GPUMaterialTexture *texture;
  };
};

typedef struct GPUOutput {
  struct GPUOutput *next, *prev;

  GPUNode *node;
  eGPUType type;     /* data type = length of vector/matrix */
  GPUNodeLink *link; /* output link */
  int id;            /* unique id as created by code generator */
} GPUOutput;

typedef struct GPUInput {
  struct GPUInput *next, *prev;

  GPUNode *node;
  eGPUType type; /* data-type. */
  GPUNodeLink *link;
  int id; /* unique id as created by code generator */

  eGPUDataSource source; /* data source */

  /* Content based on eGPUDataSource */
  union {
    /* GPU_SOURCE_CONSTANT | GPU_SOURCE_UNIFORM */
    float vec[16]; /* vector data */
    /* GPU_SOURCE_BUILTIN */
    eGPUBuiltin builtin; /* builtin uniform */
    /* GPU_SOURCE_TEX | GPU_SOURCE_TEX_TILED_MAPPING */
    struct GPUMaterialTexture *texture;
    /* GPU_SOURCE_ATTR */
    struct GPUMaterialAttribute *attr;
    /* GPU_SOURCE_UNIFORM_ATTR */
    struct GPUUniformAttr *uniform_attr;
    /* GPU_SOURCE_VOLUME_GRID | GPU_SOURCE_VOLUME_GRID_TRANSFORM */
    struct GPUMaterialVolumeGrid *volume_grid;
  };
} GPUInput;

typedef struct GPUNodeGraphOutputLink {
  struct GPUNodeGraphOutputLink *next, *prev;
  int hash;
  GPUNodeLink *outlink;
} GPUNodeGraphOutputLink;

typedef struct GPUNodeGraph {
  /* Nodes */
  List nodes;

  /* Main Output. */
  GPUNodeLink *outlink;
  /* List of GPUNodeGraphOutputLink */
  List outlink_aovs;

  /* Requested attributes and textures. */
  List attributes;
  List textures;
  List volume_grids;

  /* The list of uniform attributes. */
  GPUUniformAttrList uniform_attrs;
} GPUNodeGraph;

/* Node Graph */

void gpu_node_graph_prune_unused(GPUNodeGraph *graph);
void gpu_node_graph_finalize_uniform_attrs(GPUNodeGraph *graph);
/* Free intermediate node graph */
void gpu_node_graph_free_nodes(GPUNodeGraph *graph);
/* Free both node graph and requested attributes and textures. */
void gpu_node_graph_free(GPUNodeGraph *graph);

/* Material calls */
struct GPUNodeGraph *gpu_material_node_graph(struct GPUMaterial *material);
/* Returns the address of the future pointer to coba_tex. */
struct GPUTexture **gpu_material_ramp_texture_row_set(struct GPUMaterial *mat,
                                                      int size,
                                                      float *pixels,
                                                      float *row);

struct GSet *gpu_material_used_libs(struct GPUMaterial *material);
