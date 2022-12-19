#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "STRUCTS_action_types.h"
#include "STRUCTS_anim_types.h"
#include "STRUCTS_collection_types.h"
#include "STRUCTS_gpencil_types.h"
#include "STRUCTS_light_types.h"
#include "STRUCTS_linestyle_types.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_modifier_types.h"
#include "STRUCTS_node_types.h"
#include "STRUCTS_scene_types.h"
#include "STRUCTS_simulation_types.h"
#include "STRUCTS_texture_types.h"
#include "STRUCTS_world_types.h"

#include "LIB_color.hh"
#include "LIB_ghash.h"
#include "LIB_listbase.h"
#include "LIB_map.hh"
#include "LIB_path_util.h"
#include "LIB_set.hh"
#include "LIB_stack.hh"
#include "LIB_string.h"
#include "LIB_string_utils.h"
#include "LIB_threads.h"
#include "LIB_utildefines.h"
#include "LIB_vector_set.hh"
#include "TRANSLATION_translation.h"

#include "KERNEL_anim_data.h"
#include "KERNEL_animsys.h"
#include "KERNEL_bpath.h"
#include "KERNEL_colortools.h"
#include "KERNEL_context.h"
#include "KERNEL_cryptomatte.h"
#include "KERNEL_global.h"
#include "KERNEL_icons.h"
#include "KERNEL_idprop.h"
#include "KERNEL_idtype.h"
#include "KERNEL_image_format.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_lib_query.h"
#include "KERNEL_main.h"
#include "KERNEL_node.h"
#include "KERNEL_node_tree_update.h"

#include "API_access.h"
#include "API_define.h"
#include "API_prototypes.h"

#include "NOD_common.h"
#include "NOD_composite.h"
#include "NOD_function.h"
#include "NOD_geometry.h"
#include "NOD_node_declaration.hh"
#include "NOD_node_tree_ref.hh"
#include "NOD_shader.h"
#include "NOD_socket.h"
#include "NOD_texture.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "LOADER_read_write.h"

#include "MOD_nodes.h"

#define NODE_DEFAULT_MAX_WIDTH 700

using blender::Array;
using blender::Map;
using blender::MutableSpan;
using blender::Set;
using blender::Span;
using blender::Stack;
using blender::StringRef;
using blender::Vector;
using blender::VectorSet;
using blender::nodes::FieldInferencingInterface;
using blender::nodes::InputSocketFieldType;
using blender::nodes::NodeDeclaration;
using blender::nodes::OutputFieldDependency;
using blender::nodes::OutputSocketFieldType;
using blender::nodes::SocketDeclaration;
using namespace blender::nodes::node_tree_ref_types;

/* Fallback types for undefined tree, nodes, sockets */
static bNodeTreeType NodeTreeTypeUndefined;
bNodeType NodeTypeUndefined;
bNodeSocketType NodeSocketTypeUndefined;

static CLG_LogRef LOG = {"bke.node"};

static void ntree_set_typeinfo(bNodeTree *ntree, bNodeTreeType *typeinfo);
static void node_socket_copy(bNodeSocket *sock_dst, const bNodeSocket *sock_src, const int flag);
static void free_localized_node_groups(bNodeTree *ntree);
static void node_free_node(bNodeTree *ntree, bNode *node);
static void node_socket_interface_free(bNodeTree *UNUSED(ntree),
                                       bNodeSocket *sock,
                                       const bool do_id_user);
static void nodeMuteRerouteOutputLinks(struct bNodeTree *ntree,
                                       struct bNode *node,
                                       const bool mute);

static void ntree_init_data(ID *id)
{
  bNodeTree *ntree = (bNodeTree *)id;
  ntree_set_typeinfo(ntree, nullptr);
}

static void ntree_copy_data(Main *UNUSED(bmain), ID *id_dst, const ID *id_src, const int flag)
{
  bNodeTree *ntree_dst = (bNodeTree *)id_dst;
  const bNodeTree *ntree_src = (const bNodeTree *)id_src;

  /* We never handle usercount here for own data. */
  const int flag_subdata = flag | LIB_ID_CREATE_NO_USER_REFCOUNT;

  /* in case a running nodetree is copied */
  ntree_dst->execdata = nullptr;

  LIB_listbase_clear(&ntree_dst->nodes);
  LIB_listbase_clear(&ntree_dst->links);

  Map<const bNode *, bNode *> node_map;
  Map<const bNodeSocket *, bNodeSocket *> socket_map;

  LIB_listbase_clear(&ntree_dst->nodes);
  LISTBASE_FOREACH (const bNode *, src_node, &ntree_src->nodes) {
    /* Don't find a unique name for every node, since they should have valid names already. */
    bNode *new_node = blender::bke::node_copy_with_mapping(
        ntree_dst, *src_node, flag_subdata, false, socket_map);
    node_map.add(src_node, new_node);
  }

  /* copy links */
  LIB_listbase_clear(&ntree_dst->links);
  LISTBASE_FOREACH (const bNodeLink *, src_link, &ntree_src->links) {
    bNodeLink *dst_link = (bNodeLink *)MEM_dupallocN(src_link);
    dst_link->fromnode = node_map.lookup(src_link->fromnode);
    dst_link->fromsock = socket_map.lookup(src_link->fromsock);
    dst_link->tonode = node_map.lookup(src_link->tonode);
    dst_link->tosock = socket_map.lookup(src_link->tosock);
    LIB_assert(dst_link->tosock);
    dst_link->tosock->link = dst_link;
    LIB_addtail(&ntree_dst->links, dst_link);
  }

  /* copy interface sockets */
  LIB_listbase_clear(&ntree_dst->inputs);
  LISTBASE_FOREACH (const bNodeSocket *, src_socket, &ntree_src->inputs) {
    bNodeSocket *dst_socket = (bNodeSocket *)MEM_dupallocN(src_socket);
    node_socket_copy(dst_socket, src_socket, flag_subdata);
    LIB_addtail(&ntree_dst->inputs, dst_socket);
  }
  LIB_listbase_clear(&ntree_dst->outputs);
  LISTBASE_FOREACH (const bNodeSocket *, src_socket, &ntree_src->outputs) {
    bNodeSocket *dst_socket = (bNodeSocket *)MEM_dupallocN(src_socket);
    node_socket_copy(dst_socket, src_socket, flag_subdata);
    LIB_addtail(&ntree_dst->outputs, dst_socket);
  }

  /* copy preview hash */
  if (ntree_src->previews && (flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    bNodeInstanceHashIterator iter;

    ntree_dst->previews = BKE_node_instance_hash_new("node previews");

    NODE_INSTANCE_HASH_ITER (iter, ntree_src->previews) {
      bNodeInstanceKey key = BKE_node_instance_hash_iterator_get_key(&iter);
      bNodePreview *preview = (bNodePreview *)BKE_node_instance_hash_iterator_get_value(&iter);
      BKE_node_instance_hash_insert(ntree_dst->previews, key, BKE_node_preview_copy(preview));
    }
  }
  else {
    ntree_dst->previews = nullptr;
  }

  /* update node->parent pointers */
  LISTBASE_FOREACH (bNode *, new_node, &ntree_dst->nodes) {
    if (new_node->parent) {
      new_node->parent = node_map.lookup(new_node->parent);
    }
  }
  /* node tree will generate its own interface type */
  ntree_dst->interface_type = nullptr;

  if (ntree_src->field_inferencing_interface) {
    ntree_dst->field_inferencing_interface = new FieldInferencingInterface(
        *ntree_src->field_inferencing_interface);
  }

  if (flag & LIB_ID_COPY_NO_PREVIEW) {
    ntree_dst->preview = nullptr;
  }
  else {
    BKE_previewimg_id_copy(&ntree_dst->id, &ntree_src->id);
  }
}

static void ntree_free_data(ID *id)
{
  bNodeTree *ntree = (bNodeTree *)id;

  /* XXX hack! node trees should not store execution graphs at all.
   * This should be removed when old tree types no longer require it.
   * Currently the execution data for texture nodes remains in the tree
   * after execution, until the node tree is updated or freed. */
  if (ntree->execdata) {
    switch (ntree->type) {
      case NTREE_SHADER:
        ntreeShaderEndExecTree(ntree->execdata);
        break;
      case NTREE_TEXTURE:
        ntreeTexEndExecTree(ntree->execdata);
        ntree->execdata = nullptr;
        break;
    }
  }

  /* XXX not nice, but needed to free localized node groups properly */
  free_localized_node_groups(ntree);

  /* Unregister associated RNA types. */
  ntreeInterfaceTypeFree(ntree);

  LIB_freelistN(&ntree->links);

  LISTBASE_FOREACH_MUTABLE (bNode *, node, &ntree->nodes) {
    node_free_node(ntree, node);
  }

  /* free interface sockets */
  LISTBASE_FOREACH_MUTABLE (bNodeSocket *, sock, &ntree->inputs) {
    node_socket_interface_free(ntree, sock, false);
    MEM_freeN(sock);
  }
  LISTBASE_FOREACH_MUTABLE (bNodeSocket *, sock, &ntree->outputs) {
    node_socket_interface_free(ntree, sock, false);
    MEM_freeN(sock);
  }

  delete ntree->field_inferencing_interface;

  /* free preview hash */
  if (ntree->previews) {
    KERNEL_node_instance_hash_free(ntree->previews, (bNodeInstanceValueFP)BKE_node_preview_free);
  }

  if (ntree->id.tag & LIB_TAG_LOCALIZED) {
    KERNEL_libblock_free_data(&ntree->id, true);
  }

  KERNEL_previewimg_free(&ntree->preview);
}

static void library_foreach_node_socket(LibraryForeachIDData *data, bNodeSocket *sock)
{
  KERNEL_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data,
      IDP_foreach_property(
          sock->prop, IDP_TYPE_FILTER_ID, BKE_lib_query_idpropertiesForeachIDLink_callback, data));

  switch ((eNodeSocketDatatype)sock->type) {
    case SOCK_OBJECT: {
      bNodeSocketValueObject *default_value = (bNodeSocketValueObject *)sock->default_value;
      KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_IMAGE: {
      bNodeSocketValueImage *default_value = (bNodeSocketValueImage *)sock->default_value;
      KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_COLLECTION: {
      bNodeSocketValueCollection *default_value = (bNodeSocketValueCollection *)
                                                      sock->default_value;
      KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_TEXTURE: {
      bNodeSocketValueTexture *default_value = (bNodeSocketValueTexture *)sock->default_value;
      KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_MATERIAL: {
      bNodeSocketValueMaterial *default_value = (bNodeSocketValueMaterial *)sock->default_value;
      KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_FLOAT:
    case SOCK_VECTOR:
    case SOCK_RGBA:
    case SOCK_BOOLEAN:
    case SOCK_INT:
    case SOCK_STRING:
    case __SOCK_MESH:
    case SOCK_CUSTOM:
    case SOCK_SHADER:
    case SOCK_GEOMETRY:
      break;
  }
}
