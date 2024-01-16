#pragma once

#include "dune_node.h"

namespace dune::realtime_compositor {
class RndrCxt;
}

struct NodeTreeType;
struct CryptomatteSess;
struct Scene;
struct RndrData;
struct Rndr;
struct ViewLayer;

extern NodeTreeType *ntreeType_Composite;

void node_cmp_rlayers_outputs(NodeTree *ntree, Node *node);
void node_cmp_rlayers_register_pass(NodeTree *ntree,
                                    Node *node,
                                    Scene *scene,
                                    ViewLayer *view_layer,
                                    const char *name,
                                    eNodeSocketDatatype type);
const char *node_cmp_rlayers_sock_to_pass(int sock_index);

void register_node_type_cmp_custom_group(NodeType *ntype);

void ntreeCompositExTree(Rndr *render,
                         Scene *scene,
                         NodeTree *ntree,
                         RndrData *rd,
                         bool rendering,
                         int do_previews,
                         const char *view_name,
                         dune::realtime_compositor::RndrCxt *rndr_cxt);

/* Called from rndr pipeline, to tag rndr input and output.
 * need to do all scenes, to prevent errs when you re-render 1 scene. */
void ntreeCompositTagRender(Scene *scene);

void ntreeCompositTagNeedExec(Node *node);

/* Update the outputs of the render layer nodes.
 * Since the outputs depend on the render engine, this part is a bit complex:
 * - ntreeCompositUpdateRLayers is called and loops over all render layer nodes.
 * - Each rndr layer node calls the update function of the
 *   rndr engine that's used for its scene.
 * - The rndr engine calls rndr_engine_register_pass for each pass.
 * - rndr_engine_register_pass calls node_cmp_rlayers_register_pass.*/
void ntreeCompositUpdateRLayers(NodeTree *ntree);

void ntreeCompositClearTags(NodeTree *ntree);

NodeSocket *ntreeCompositOutputFileAddSocket(NodeTree *ntree,
                                             Node *node,
                                             const char *name,
                                             const ImgFormatData *im_format);

int ntreeCompositOutputFileRemoveActiveSocket(NodeTree *ntree, Node *node);
void ntreeCompositOutputFileSetPath(Node *node, NodeSocket *sock, const char *name);
void ntreeCompositOutputFileSetLayer(Node *node, NodeSocket *sock, const char *name);
/* needed in do_versions */
void ntreeCompositOutputFileUniquePath(List *list,
                                       NodeSocket *sock,
                                       const char defname[],
                                       char delim);
void ntreeCompositOutputFileUniqueLayer(List *list,
                                        NodeSocket *sock,
                                        const char defname[],
                                        char delim);

void ntreeCompositColorBalanceSyncFromLGG(NodeTree *ntree, Node *node);
void ntreeCompositColorBalanceSyncFromCDL(NodeTree *ntree, Node *node);

void ntreeCompositCryptomatteSyncFromAdd(const Scene *scene, Node *node);
void ntreeCompositCryptomatteSyncFromRemove(Node *node);
NodeSocket *ntreeCompositCryptomatteAddSocket(NodeTree *ntree, Node *node);
int ntreeCompositCryptomatteRemoveSocket(NodeTree *ntree, Node *node);
void ntreeCompositCryptomatteLayerPrefix(const Scene *scene,
                                         const Node *node,
                                         char *r_prefix,
                                         size_t prefix_maxncpy);

/* Update the runtime layer names with the crypto-matte layer names of the refs render layer
 * or img. */
void ntreeCompositCryptomatteUpdateLayerNames(const Scene *scene, Node *node);
CryptomatteSess *ntreeCompositCryptomatteSess(const Scene *scene, Node *node);
