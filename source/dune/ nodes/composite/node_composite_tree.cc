#include <cstdio>

#include "lib_string.h"

#include "types_color_types.h"
#include "types_node_types.h"
#include "types_scene_types.h"

#include "dune_cx.hh"
#include "dune_global.h"
#include "dunr_img.h"
#include "dune_main.hh"
#include "dune_node.hh"
#include "dune_node_runtime.hh"
#include "dune_node_tree_update.hh"
#include "dune_tracking.h"

#include "ui_resources.hh"

#include "node_common.h"
#include "node_util.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "node_composite.hh"
#include "node_composite_util.hh"

#ifdef WITH_COMPOSITOR_CPU
#  include "COM_compositor.hh"
#endif

static void composite_get_from_cx(
    const Cx *C, NodeTreeType * /*treetype*/, NodeTree **r_ntree, Id **r_id, Id **r_from)
{
  Scene *scene = cx_data_scene(C);

  *r_from = nullptr;
  *r_id = &scene->id;
  *r_ntree = scene->nodetree;
}

static void foreach_nodeclass(Scene * /*scene*/, void *calldata, NodeClassCb fn)
{
  func(calldata, NODE_CLASS_INPUT, N_("Input"));
  func(calldata, NODE_CLASS_OUTPUT, N_("Output"));
  func(calldata, NODE_CLASS_OP_COLOR, N_("Color"));
  func(calldata, NODE_CLASS_OP_VECTOR, N_("Vector"));
  func(calldata, NODE_CLASS_OP_FILTER, N_("Filter"));
  func(calldata, NODE_CLASS_CONVERTER, N_("Converter"));
  func(calldata, NODE_CLASS_MATTE, N_("Matte"));
  func(calldata, NODE_CLASS_DISTORT, N_("Distort"));
  func(calldata, NODE_CLASS_GROUP, N_("Group"));
  func(calldata, NODE_CLASS_INTERFACE, N_("Interface"));
  func(calldata, NODE_CLASS_LAYOUT, N_("Layout"));
}

/* local tree then owns all compbufs */
static void localize(NodeTree *localtree, NodeTree *ntree)
{

  Node *node = (bNode *)ntree->nodes.first;
  Node *local_node = (bNode *)localtree->nodes.first;
  while (node != nullptr) {

    /* Ensure new user input gets handled ok. */
    node->runtime->need_exec = 0;
    local_node->runtime->original = node;

    /* move over the compbufs */
    /* right after #dune::ntreeCopyTree() `oldsock` ptrs are valid */

    if (node->type == CMP_NODE_VIEWER) {
      if (node->id) {
        if (node->flag & NODE_DO_OUTPUT) {
          local_node->id = (ID *)node->id;
        }
        else {
          local_node->id = nullptr;
        }
      }
    }

    node = node->next;
    local_node = local_node->next;
  }
}

static void local_merge(Main *main, NodeTree *localtree, NodeTree *ntree)
{
  /* move over the compbufs and previews */
  dune::node_preview_merge_tree(ntree, localtree, true);

  LIST_FOREACH (Node *, lnode, &localtree->nodes) {
    if (Node *orig_node = nodeFindNodebyName(ntree, lnode->name)) {
      if (lnode->type == CMP_NODE_VIEWER) {
        if (lnode->id && (lnode->flag & NODE_DO_OUTPUT)) {
          /* image_merge does sanity check for pointers */
          dune_img_merge(main, (Image *)orig_node->id, (Image *)lnode->id);
        }
      }
      else if (lnode->type == CMP_NODE_MOVIEDISTORTION) {
        /* special case for distortion node: distortion context is allocating in exec fn
         * and to achieve much better performance on further calls this cx should be
         * copied back to original node */
        if (lnode->storage) {
          if (orig_node->storage) {
            dune_tracking_distortion_free((MovieDistortion *)orig_node->storage);
          }

          orig_node->storage = BKE_tracking_distortion_copy((MovieDistortion *)lnode->storage);
        }
      }
    }
  }
}

static void update(bNodeTree *ntree)
{
  ntreeSetOutput(ntree);

  ntree_update_reroute_nodes(ntree);
}

static void composite_node_add_init(bNodeTree * /*bnodetree*/, bNode *bnode)
{
  /* Composite node will only show previews for input classes
   * by default, other will be hidden
   * but can be made visible with the show_preview option */
  if (bnode->typeinfo->nclass != NODE_CLASS_INPUT) {
    bnode->flag &= ~NODE_PREVIEW;
  }
}

static bool composite_node_tree_socket_type_valid(NodeTreeType * /*ntreetype*/,
                                                  NodeSocketType *socket_type)
{
  return dime::nodeIsStaticSocketType(socket_type) &&
         ELEM(socket_type->type, SOCK_FLOAT, SOCK_VECTOR, SOCK_RGBA);
}

NodeTreeType *ntreeType_Composite;

void register_node_tree_type_cmp()
{
  NodeTreeType *tt = ntreeType_Composite = MEM_cnew<NodeTreeType>(__func__);

  tt->type = NTREE_COMPOSIT;
  STRNCPY(tt->idname, "CompositorNodeTree");
  STRNCPY(tt->group_idname, "CompositorNodeGroup");
  STRNCPY(tt->ui_name, N_("Compositor"));
  tt->ui_icon = ICON_NODE_COMPOSITING;
  STRNCPY(tt->ui_description, N_("Compositing nodes"));

  tt->foreach_nodeclass = foreach_nodeclass;
  tt->localize = localize;
  tt->local_merge = local_merge;
  tt->update = update;
  tt->get_from_cx = composite_get_from_cx;
  tt->node_add_init = composite_node_add_init;
  tt->valid_socket_type = composite_node_tree_socket_type_valid;

  tt->rna_ext.srna = &Api_CompositorNodeTree;

  ntreeTypeAdd(tt);
}

void ntreeCompositExecTree(Rndr *render,
                           Scene *scene,
                           NodeTree *ntree,
                           RndrData *rd,
                           bool rndring,
                           int do_preview,
                           const char *view_name,
                           dune::realtime_compositor::RndrCx *rndr_cx)
{
#ifdef WITH_COMPOSITOR_CPU
  COM_execute(rndr, rd, scene, ntree, rndring, view_name, rndr_cx);
#else
  UNUSED_VARS(rndr, scene, ntree, rd, rndring, view_name, rndr_cx);
#endif

  UNUSED_VARS(do_preview);
}

void ntreeCompositeUpdateRLayers(NodeTree *ntree)
{
  if (ntree == nullptr) {
    return;
  }

  for (Node *node : ntree->all_nodes()) {
    if (node->type == CMP_NODE_R_LAYERS) {
      node_cmp_rlayers_outputs(ntree, node);
    }
  }
}

void ntreeCompositeTagRndr(Scene *scene)
{
  /* XXX Think using G_MAIN here is valid, since you want to update current file's scene nodes,
   * not the ones in temp main generated for rendering?
   * This is still rather weak though,
   * ideally render struct would store own main AND original G_MAIN. */

  for (Scene *sce_iter = (Scene *)G_MAIN->scenes.first; sce_iter;
       sce_iter = (Scene *)sce_iter->id.next)
  {
    if (sce_iter->nodetree) {
      for (Node *node : sce_iter->nodetree->all_nodes()) {
        if (node->id == (ID *)scene || node->type == CMP_NODE_COMPOSITE) {
          dune_ntree_update_tag_node_prop(sce_iter->nodetree, node);
        }
        else if (node->type == CMP_NODE_TEXTURE) /* uses scene size_x/size_y */ {
          dune_ntree_update_tag_node_prop(sce_iter->nodetree, node);
        }
      }
    }
  }
  dune_ntree_update_main(G_MAIN, nullptr);
}

void ntreeCompositeClearTags(NodeTree *ntree)
{
  /* XXX: after render animation sys gets a refresh, this call allows composite to end clean. */
  if (ntree == nullptr) {
    return;
  }

  for (bNode *node : ntree->all_nodes()) {
    node->runtime->need_exec = 0;
    if (node->type == NODE_GROUP) {
      ntreeCompositClearTags((NodeTree *)node->id);
    }
  }
}

void ntreeCompositTagNeedExec(Node *node)
{
  node->runtime->need_exec = true;
}
