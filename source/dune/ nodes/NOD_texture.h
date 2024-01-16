#pragma once

#include "dune_node.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct NodeTreeType *ntreeType_Texture;

void ntreeTexCheckCyclics(struct NodeTree *ntree);
struct NodeTreeEx *ntreeTexBeginExTree(struct NodeTree *ntree);
void ntreeTexEndExTree(struct NodeTreeEx *ex);
int ntreeTexExTree(struct NodeTree *ntree,
                     struct TexResult *target,
                     const float co[3],
                     float dxt[3],
                     float dyt[3],
                     int osatex,
                     short thread,
                     const struct Tex *tex,
                     short which_output,
                     int cfra,
                     int preview,
                     struct MTex *mtex);

#ifdef __cplusplus
}
#endif
