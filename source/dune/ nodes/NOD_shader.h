#pragma once

#include "dune_node.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct NodeTreeType *ntreeType_Shader;

void register_node_type_sh_custom_group(NodeType *ntype);

struct NodeTreeEx *ntreeShaderBeginExTree(struct NodeTree *ntree);
void ntreeShaderEndExTree(struct NodeTreeEx *ex);

/* Find an output node of the shader tree.
 *
 * It will only return output which is NOT in the group, which isn't how
 * rndr engines works but it's how the GPU shader compilation works. This we
 * can change in the future and make it a generic fn, but for now it stays
 * private here.
 */
struct Node *ntreeShaderOutputNode(struct NodeTree *ntree, int target);

/**
 * This one needs to work on a local tree.
 */
void ntreeGPUMaterialNodes(struct NodeTree *localtree, struct GPUMaterial *mat);

#ifdef __cplusplus
}
#endif
