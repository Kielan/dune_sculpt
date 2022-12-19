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


/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2005 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup bke
 */

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_action_types.h"
#include "DNA_anim_types.h"
#include "DNA_collection_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_light_types.h"
#include "DNA_linestyle_types.h"
#include "DNA_material_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_simulation_types.h"
#include "DNA_texture_types.h"
#include "DNA_world_types.h"

#include "BLI_color.hh"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_path_util.h"
#include "BLI_set.hh"
#include "BLI_stack.hh"
#include "BLI_string.h"
#include "BLI_string_utils.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_vector_set.hh"
#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_bpath.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_cryptomatte.h"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_image_format.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_node_tree_update.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_prototypes.h"

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

#include "BLO_read_write.h"

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

  BLI_listbase_clear(&ntree_dst->nodes);
  BLI_listbase_clear(&ntree_dst->links);

  Map<const bNode *, bNode *> node_map;
  Map<const bNodeSocket *, bNodeSocket *> socket_map;

  BLI_listbase_clear(&ntree_dst->nodes);
  LISTBASE_FOREACH (const bNode *, src_node, &ntree_src->nodes) {
    /* Don't find a unique name for every node, since they should have valid names already. */
    bNode *new_node = blender::bke::node_copy_with_mapping(
        ntree_dst, *src_node, flag_subdata, false, socket_map);
    node_map.add(src_node, new_node);
  }

  /* copy links */
  BLI_listbase_clear(&ntree_dst->links);
  LISTBASE_FOREACH (const bNodeLink *, src_link, &ntree_src->links) {
    bNodeLink *dst_link = (bNodeLink *)MEM_dupallocN(src_link);
    dst_link->fromnode = node_map.lookup(src_link->fromnode);
    dst_link->fromsock = socket_map.lookup(src_link->fromsock);
    dst_link->tonode = node_map.lookup(src_link->tonode);
    dst_link->tosock = socket_map.lookup(src_link->tosock);
    BLI_assert(dst_link->tosock);
    dst_link->tosock->link = dst_link;
    BLI_addtail(&ntree_dst->links, dst_link);
  }

  /* copy interface sockets */
  BLI_listbase_clear(&ntree_dst->inputs);
  LISTBASE_FOREACH (const bNodeSocket *, src_socket, &ntree_src->inputs) {
    bNodeSocket *dst_socket = (bNodeSocket *)MEM_dupallocN(src_socket);
    node_socket_copy(dst_socket, src_socket, flag_subdata);
    BLI_addtail(&ntree_dst->inputs, dst_socket);
  }
  BLI_listbase_clear(&ntree_dst->outputs);
  LISTBASE_FOREACH (const bNodeSocket *, src_socket, &ntree_src->outputs) {
    bNodeSocket *dst_socket = (bNodeSocket *)MEM_dupallocN(src_socket);
    node_socket_copy(dst_socket, src_socket, flag_subdata);
    BLI_addtail(&ntree_dst->outputs, dst_socket);
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

  BLI_freelistN(&ntree->links);

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
    BKE_node_instance_hash_free(ntree->previews, (bNodeInstanceValueFP)BKE_node_preview_free);
  }

  if (ntree->id.tag & LIB_TAG_LOCALIZED) {
    BKE_libblock_free_data(&ntree->id, true);
  }

  BKE_previewimg_free(&ntree->preview);
}

static void library_foreach_node_socket(LibraryForeachIDData *data, bNodeSocket *sock)
{
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data,
      IDP_foreach_property(
          sock->prop, IDP_TYPE_FILTER_ID, BKE_lib_query_idpropertiesForeachIDLink_callback, data));

  switch ((eNodeSocketDatatype)sock->type) {
    case SOCK_OBJECT: {
      bNodeSocketValueObject *default_value = (bNodeSocketValueObject *)sock->default_value;
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_IMAGE: {
      bNodeSocketValueImage *default_value = (bNodeSocketValueImage *)sock->default_value;
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_COLLECTION: {
      bNodeSocketValueCollection *default_value = (bNodeSocketValueCollection *)
                                                      sock->default_value;
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_TEXTURE: {
      bNodeSocketValueTexture *default_value = (bNodeSocketValueTexture *)sock->default_value;
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
      break;
    }
    case SOCK_MATERIAL: {
      bNodeSocketValueMaterial *default_value = (bNodeSocketValueMaterial *)sock->default_value;
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, default_value->value, IDWALK_CB_USER);
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

static void node_foreach_id(ID *id, LibraryForeachIDData *data)
{
  bNodeTree *ntree = (bNodeTree *)id;

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, ntree->gpd, IDWALK_CB_USER);

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    BKE_LIB_FOREACHID_PROCESS_ID(data, node->id, IDWALK_CB_USER);

    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data,
        IDP_foreach_property(node->prop,
                             IDP_TYPE_FILTER_ID,
                             BKE_lib_query_idpropertiesForeachIDLink_callback,
                             data));
    LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, library_foreach_node_socket(data, sock));
    }
    LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, library_foreach_node_socket(data, sock));
    }
  }

  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->inputs) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, library_foreach_node_socket(data, sock));
  }
  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->outputs) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data, library_foreach_node_socket(data, sock));
  }
}

static void node_foreach_cache(ID *id,
                               IDTypeForeachCacheFunctionCallback function_callback,
                               void *user_data)
{
  bNodeTree *nodetree = (bNodeTree *)id;
  IDCacheKey key = {0};
  key.id_session_uuid = id->session_uuid;
  key.offset_in_ID = offsetof(bNodeTree, previews);
  key.cache_v = nodetree->previews;

  /* TODO: see also `direct_link_nodetree()` in readfile.c. */
#if 0
  function_callback(id, &key, (void **)&nodetree->previews, 0, user_data);
#endif

  if (nodetree->type == NTREE_COMPOSIT) {
    LISTBASE_FOREACH (bNode *, node, &nodetree->nodes) {
      if (node->type == CMP_NODE_MOVIEDISTORTION) {
        key.offset_in_ID = (size_t)BLI_ghashutil_strhash_p(node->name);
        key.cache_v = node->storage;
        function_callback(id, &key, (void **)&node->storage, 0, user_data);
      }
    }
  }
}

static void node_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  bNodeTree *ntree = reinterpret_cast<bNodeTree *>(id);

  switch (ntree->type) {
    case NTREE_SHADER: {
      LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
        if (node->type == SH_NODE_SCRIPT) {
          NodeShaderScript *nss = reinterpret_cast<NodeShaderScript *>(node->storage);
          BKE_bpath_foreach_path_fixed_process(bpath_data, nss->filepath);
        }
        else if (node->type == SH_NODE_TEX_IES) {
          NodeShaderTexIES *ies = reinterpret_cast<NodeShaderTexIES *>(node->storage);
          BKE_bpath_foreach_path_fixed_process(bpath_data, ies->filepath);
        }
      }
      break;
    }
    default:
      break;
  }
}

static ID *node_owner_get(Main *bmain, ID *id)
{
  if ((id->flag & LIB_EMBEDDED_DATA) == 0) {
    return id;
  }
  /* TODO: Sort this NO_MAIN or not for embedded node trees. See T86119. */
  // BLI_assert((id->tag & LIB_TAG_NO_MAIN) == 0);

  ListBase *lists[] = {&bmain->materials,
                       &bmain->lights,
                       &bmain->worlds,
                       &bmain->textures,
                       &bmain->scenes,
                       &bmain->linestyles,
                       &bmain->simulations,
                       nullptr};

  bNodeTree *ntree = (bNodeTree *)id;
  for (int i = 0; lists[i] != nullptr; i++) {
    LISTBASE_FOREACH (ID *, id_iter, lists[i]) {
      if (ntreeFromID(id_iter) == ntree) {
        return id_iter;
      }
    }
  }

  BLI_assert_msg(0, "Embedded node tree with no owner. Critical Main inconsistency.");
  return nullptr;
}

static void write_node_socket_default_value(BlendWriter *writer, bNodeSocket *sock)
{
  if (sock->default_value == nullptr) {
    return;
  }

  switch ((eNodeSocketDatatype)sock->type) {
    case SOCK_FLOAT:
      BLO_write_struct(writer, bNodeSocketValueFloat, sock->default_value);
      break;
    case SOCK_VECTOR:
      BLO_write_struct(writer, bNodeSocketValueVector, sock->default_value);
      break;
    case SOCK_RGBA:
      BLO_write_struct(writer, bNodeSocketValueRGBA, sock->default_value);
      break;
    case SOCK_BOOLEAN:
      BLO_write_struct(writer, bNodeSocketValueBoolean, sock->default_value);
      break;
    case SOCK_INT:
      BLO_write_struct(writer, bNodeSocketValueInt, sock->default_value);
      break;
    case SOCK_STRING:
      BLO_write_struct(writer, bNodeSocketValueString, sock->default_value);
      break;
    case SOCK_OBJECT:
      BLO_write_struct(writer, bNodeSocketValueObject, sock->default_value);
      break;
    case SOCK_IMAGE:
      BLO_write_struct(writer, bNodeSocketValueImage, sock->default_value);
      break;
    case SOCK_COLLECTION:
      BLO_write_struct(writer, bNodeSocketValueCollection, sock->default_value);
      break;
    case SOCK_TEXTURE:
      BLO_write_struct(writer, bNodeSocketValueTexture, sock->default_value);
      break;
    case SOCK_MATERIAL:
      BLO_write_struct(writer, bNodeSocketValueMaterial, sock->default_value);
      break;
    case SOCK_CUSTOM:
      /* Custom node sockets where default_value is defined uses custom properties for storage. */
      break;
    case __SOCK_MESH:
    case SOCK_SHADER:
    case SOCK_GEOMETRY:
      BLI_assert_unreachable();
      break;
  }
}

static void write_node_socket(BlendWriter *writer, bNodeSocket *sock)
{
  BLO_write_struct(writer, bNodeSocket, sock);

  if (sock->prop) {
    IDP_BlendWrite(writer, sock->prop);
  }

  write_node_socket_default_value(writer, sock);
}
static void write_node_socket_interface(BlendWriter *writer, bNodeSocket *sock)
{
  BLO_write_struct(writer, bNodeSocket, sock);

  if (sock->prop) {
    IDP_BlendWrite(writer, sock->prop);
  }

  write_node_socket_default_value(writer, sock);
}

void ntreeBlendWrite(BlendWriter *writer, bNodeTree *ntree)
{
  BKE_id_blend_write(writer, &ntree->id);

  if (ntree->adt) {
    BKE_animdata_blend_write(writer, ntree->adt);
  }

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    BLO_write_struct(writer, bNode, node);

    if (node->prop) {
      IDP_BlendWrite(writer, node->prop);
    }

    LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
      write_node_socket(writer, sock);
    }
    LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
      write_node_socket(writer, sock);
    }

    LISTBASE_FOREACH (bNodeLink *, link, &node->internal_links) {
      BLO_write_struct(writer, bNodeLink, link);
    }

    if (node->storage) {
      if (ELEM(ntree->type, NTREE_SHADER, NTREE_GEOMETRY) &&
          ELEM(node->type, SH_NODE_CURVE_VEC, SH_NODE_CURVE_RGB, SH_NODE_CURVE_FLOAT)) {
        BKE_curvemapping_blend_write(writer, (const CurveMapping *)node->storage);
      }
      else if (ntree->type == NTREE_SHADER && (node->type == SH_NODE_SCRIPT)) {
        NodeShaderScript *nss = (NodeShaderScript *)node->storage;
        if (nss->bytecode) {
          BLO_write_string(writer, nss->bytecode);
        }
        BLO_write_struct_by_name(writer, node->typeinfo->storagename, node->storage);
      }
      else if ((ntree->type == NTREE_COMPOSIT) && ELEM(node->type,
                                                       CMP_NODE_TIME,
                                                       CMP_NODE_CURVE_VEC,
                                                       CMP_NODE_CURVE_RGB,
                                                       CMP_NODE_HUECORRECT)) {
        BKE_curvemapping_blend_write(writer, (const CurveMapping *)node->storage);
      }
      else if ((ntree->type == NTREE_TEXTURE) &&
               ELEM(node->type, TEX_NODE_CURVE_RGB, TEX_NODE_CURVE_TIME)) {
        BKE_curvemapping_blend_write(writer, (const CurveMapping *)node->storage);
      }
      else if ((ntree->type == NTREE_COMPOSIT) && (node->type == CMP_NODE_MOVIEDISTORTION)) {
        /* pass */
      }
      else if ((ntree->type == NTREE_COMPOSIT) && (node->type == CMP_NODE_GLARE)) {
        /* Simple forward compatibility for fix for T50736.
         * Not ideal (there is no ideal solution here), but should do for now. */
        NodeGlare *ndg = (NodeGlare *)node->storage;
        /* Not in undo case. */
        if (!BLO_write_is_undo(writer)) {
          switch (ndg->type) {
            case 2: /* Grrrr! magic numbers :( */
              ndg->angle = ndg->streaks;
              break;
            case 0:
              ndg->angle = ndg->star_45;
              break;
            default:
              break;
          }
        }
        BLO_write_struct_by_name(writer, node->typeinfo->storagename, node->storage);
      }
      else if ((ntree->type == NTREE_COMPOSIT) &&
               ELEM(node->type, CMP_NODE_CRYPTOMATTE, CMP_NODE_CRYPTOMATTE_LEGACY)) {
        NodeCryptomatte *nc = (NodeCryptomatte *)node->storage;
        BLO_write_string(writer, nc->matte_id);
        LISTBASE_FOREACH (CryptomatteEntry *, entry, &nc->entries) {
          BLO_write_struct(writer, CryptomatteEntry, entry);
        }
        BLO_write_struct_by_name(writer, node->typeinfo->storagename, node->storage);
      }
      else if (node->type == FN_NODE_INPUT_STRING) {
        NodeInputString *storage = (NodeInputString *)node->storage;
        if (storage->string) {
          BLO_write_string(writer, storage->string);
        }
        BLO_write_struct_by_name(writer, node->typeinfo->storagename, storage);
      }
      else if (node->typeinfo != &NodeTypeUndefined) {
        BLO_write_struct_by_name(writer, node->typeinfo->storagename, node->storage);
      }
    }

    if (node->type == CMP_NODE_OUTPUT_FILE) {
      /* Inputs have their own storage data. */
      NodeImageMultiFile *nimf = (NodeImageMultiFile *)node->storage;
      BKE_image_format_blend_write(writer, &nimf->format);

      LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
        NodeImageMultiFileSocket *sockdata = (NodeImageMultiFileSocket *)sock->storage;
        BLO_write_struct(writer, NodeImageMultiFileSocket, sockdata);
        BKE_image_format_blend_write(writer, &sockdata->format);
      }
    }
    if (ELEM(node->type, CMP_NODE_IMAGE, CMP_NODE_R_LAYERS)) {
      /* Write extra socket info. */
      LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
        BLO_write_struct(writer, NodeImageLayer, sock->storage);
      }
    }
  }

  LISTBASE_FOREACH (bNodeLink *, link, &ntree->links) {
    BLO_write_struct(writer, bNodeLink, link);
  }

  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->inputs) {
    write_node_socket_interface(writer, sock);
  }
  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->outputs) {
    write_node_socket_interface(writer, sock);
  }

  BKE_previewimg_blend_write(writer, ntree->preview);
}

static void ntree_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  bNodeTree *ntree = (bNodeTree *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  ntree->is_updating = false;
  ntree->typeinfo = nullptr;
  ntree->interface_type = nullptr;
  ntree->progress = nullptr;
  ntree->execdata = nullptr;

  BLO_write_id_struct(writer, bNodeTree, id_address, &ntree->id);

  ntreeBlendWrite(writer, ntree);
}

static void direct_link_node_socket(BlendDataReader *reader, bNodeSocket *sock)
{
  BLO_read_data_address(reader, &sock->prop);
  IDP_BlendDataRead(reader, &sock->prop);

  BLO_read_data_address(reader, &sock->link);
  sock->typeinfo = nullptr;
  BLO_read_data_address(reader, &sock->storage);
  BLO_read_data_address(reader, &sock->default_value);
  sock->total_inputs = 0; /* Clear runtime data set before drawing. */
  sock->cache = nullptr;
  sock->declaration = nullptr;
}

void ntreeBlendReadData(BlendDataReader *reader, bNodeTree *ntree)
{
  /* NOTE: writing and reading goes in sync, for speed. */
  ntree->is_updating = false;
  ntree->typeinfo = nullptr;
  ntree->interface_type = nullptr;

  ntree->progress = nullptr;
  ntree->execdata = nullptr;

  ntree->field_inferencing_interface = nullptr;
  BKE_ntree_update_tag_missing_runtime_data(ntree);

  BLO_read_data_address(reader, &ntree->adt);
  BKE_animdata_blend_read_data(reader, ntree->adt);

  BLO_read_list(reader, &ntree->nodes);
  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    node->typeinfo = nullptr;
    node->declaration = nullptr;

    BLO_read_list(reader, &node->inputs);
    BLO_read_list(reader, &node->outputs);

    BLO_read_data_address(reader, &node->prop);
    IDP_BlendDataRead(reader, &node->prop);

    BLO_read_list(reader, &node->internal_links);
    LISTBASE_FOREACH (bNodeLink *, link, &node->internal_links) {
      BLO_read_data_address(reader, &link->fromnode);
      BLO_read_data_address(reader, &link->fromsock);
      BLO_read_data_address(reader, &link->tonode);
      BLO_read_data_address(reader, &link->tosock);
    }

    if (node->type == CMP_NODE_MOVIEDISTORTION) {
      /* Do nothing, this is runtime cache and hence handled by generic code using
       * `IDTypeInfo.foreach_cache` callback. */
    }
    else {
      BLO_read_data_address(reader, &node->storage);
    }

    if (node->storage) {
      switch (node->type) {
        case SH_NODE_CURVE_VEC:
        case SH_NODE_CURVE_RGB:
        case SH_NODE_CURVE_FLOAT:
        case CMP_NODE_TIME:
        case CMP_NODE_CURVE_VEC:
        case CMP_NODE_CURVE_RGB:
        case CMP_NODE_HUECORRECT:
        case TEX_NODE_CURVE_RGB:
        case TEX_NODE_CURVE_TIME: {
          BKE_curvemapping_blend_read(reader, (CurveMapping *)node->storage);
          break;
        }
        case SH_NODE_SCRIPT: {
          NodeShaderScript *nss = (NodeShaderScript *)node->storage;
          BLO_read_data_address(reader, &nss->bytecode);
          break;
        }
        case SH_NODE_TEX_POINTDENSITY: {
          NodeShaderTexPointDensity *npd = (NodeShaderTexPointDensity *)node->storage;
          memset(&npd->pd, 0, sizeof(npd->pd));
          break;
        }
        case SH_NODE_TEX_IMAGE: {
          NodeTexImage *tex = (NodeTexImage *)node->storage;
          tex->iuser.scene = nullptr;
          break;
        }
        case SH_NODE_TEX_ENVIRONMENT: {
          NodeTexEnvironment *tex = (NodeTexEnvironment *)node->storage;
          tex->iuser.scene = nullptr;
          break;
        }
        case CMP_NODE_IMAGE:
        case CMP_NODE_R_LAYERS:
        case CMP_NODE_VIEWER:
        case CMP_NODE_SPLITVIEWER: {
          ImageUser *iuser = (ImageUser *)node->storage;
          iuser->scene = nullptr;
          break;
        }
        case CMP_NODE_CRYPTOMATTE_LEGACY:
        case CMP_NODE_CRYPTOMATTE: {
          NodeCryptomatte *nc = (NodeCryptomatte *)node->storage;
          BLO_read_data_address(reader, &nc->matte_id);
          BLO_read_list(reader, &nc->entries);
          BLI_listbase_clear(&nc->runtime.layers);
          break;
        }
        case TEX_NODE_IMAGE: {
          ImageUser *iuser = (ImageUser *)node->storage;
          iuser->scene = nullptr;
          break;
        }
        case CMP_NODE_OUTPUT_FILE: {
          NodeImageMultiFile *nimf = (NodeImageMultiFile *)node->storage;
          BKE_image_format_blend_read_data(reader, &nimf->format);
          break;
        }
        case FN_NODE_INPUT_STRING: {
          NodeInputString *storage = (NodeInputString *)node->storage;
          BLO_read_data_address(reader, &storage->string);
          break;
        }
        default:
          break;
      }
    }
  }
  BLO_read_list(reader, &ntree->links);

  /* and we connect the rest */
  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    BLO_read_data_address(reader, &node->parent);

    LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
      direct_link_node_socket(reader, sock);
    }
    LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
      direct_link_node_socket(reader, sock);
    }

    /* Socket storage. */
    if (node->type == CMP_NODE_OUTPUT_FILE) {
      LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
        NodeImageMultiFileSocket *sockdata = (NodeImageMultiFileSocket *)sock->storage;
        BKE_image_format_blend_read_data(reader, &sockdata->format);
      }
    }
  }

  /* interface socket lists */
  BLO_read_list(reader, &ntree->inputs);
  BLO_read_list(reader, &ntree->outputs);
  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->inputs) {
    direct_link_node_socket(reader, sock);
  }
  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->outputs) {
    direct_link_node_socket(reader, sock);
  }

  LISTBASE_FOREACH (bNodeLink *, link, &ntree->links) {
    BLO_read_data_address(reader, &link->fromnode);
    BLO_read_data_address(reader, &link->tonode);
    BLO_read_data_address(reader, &link->fromsock);
    BLO_read_data_address(reader, &link->tosock);
  }

  /* TODO: should be dealt by new generic cache handling of IDs... */
  ntree->previews = nullptr;

  BLO_read_data_address(reader, &ntree->preview);
  BKE_previewimg_blend_read(reader, ntree->preview);

  /* type verification is in lib-link */
}

static void ntree_blend_read_data(BlendDataReader *reader, ID *id)
{
  bNodeTree *ntree = (bNodeTree *)id;
  ntreeBlendReadData(reader, ntree);
}

static void lib_link_node_socket(BlendLibReader *reader, Library *lib, bNodeSocket *sock)
{
  IDP_DuneReadLib(reader, sock->prop);

  /* This can happen for all socket types when a file is saved in an older version of Blender than
   * it was originally created in (T86298). Some socket types still require a default value. The
   * default value of those sockets will be created in `ntreeSetTypes`. */
  if (sock->default_value == nullptr) {
    return;
  }

  switch ((eNodeSocketDatatype)sock->type) {
    case SOCK_OBJECT: {
      bNodeSocketValueObject *default_value = (bNodeSocketValueObject *)sock->default_value;
      LOADER_read_id_address(reader, lib, &default_value->value);
      break;
    }
    case SOCK_IMAGE: {
      bNodeSocketValueImage *default_value = (bNodeSocketValueImage *)sock->default_value;
      LOADER_read_id_address(reader, lib, &default_value->value);
      break;
    }
    case SOCK_COLLECTION: {
      bNodeSocketValueCollection *default_value = (bNodeSocketValueCollection *)
                                                      sock->default_value;
      LOADER_read_id_address(reader, lib, &default_value->value);
      break;
    }
    case SOCK_TEXTURE: {
      bNodeSocketValueTexture *default_value = (bNodeSocketValueTexture *)sock->default_value;
      LOADER_read_id_address(reader, lib, &default_value->value);
      break;
    }
    case SOCK_MATERIAL: {
      bNodeSocketValueMaterial *default_value = (bNodeSocketValueMaterial *)sock->default_value;
      LOADER_read_id_address(reader, lib, &default_value->value);
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

static void lib_link_node_sockets(DuneLibReader *reader, Library *lib, ListBase *sockets)
{
  LISTBASE_FOREACH (bNodeSocket *, sock, sockets) {
    lib_link_node_socket(reader, lib, sock);
  }
}

void ntreeDuneReadLib(struct DuneLibReader *reader, struct bNodeTree *ntree)
{
  Library *lib = ntree->id.lib;

  LOADER_read_id_address(reader, lib, &ntree->gpd);

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    /* Link ID Properties -- and copy this comment EXACTLY for easy finding
     * of library blocks that implement this. */
    IDP_DuneReadLib(reader, node->prop);

    LOADER_read_id_address(reader, lib, &node->id);

    lib_link_node_sockets(reader, lib, &node->inputs);
    lib_link_node_sockets(reader, lib, &node->outputs);
  }

  lib_link_node_sockets(reader, lib, &ntree->inputs);
  lib_link_node_sockets(reader, lib, &ntree->outputs);

  /* Set node->typeinfo pointers. This is done in lib linking, after the
   * first versioning that can change types still without functions that
   * update the typeinfo pointers. Versioning after lib linking needs
   * these top be valid. */
  ntreeSetTypes(nullptr, ntree);

  /* For nodes with static socket layout, add/remove sockets as needed
   * to match the static layout. */
  if (!LOADER_read_lib_is_undo(reader)) {
    LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
      node_verify_sockets(ntree, node, false);
    }
  }
}

static void ntree_dune_read_lib(DuneLibReader *reader, ID *id)
{
  bNodeTree *ntree = (bNodeTree *)id;
  ntreeDuneReadLib(reader, ntree);
}

static void expand_node_socket(DuneExpander *expander, bNodeSocket *sock)
{
  IDP_DuneReadExpand(expander, sock->prop);

  if (sock->default_value != nullptr) {

    switch ((eNodeSocketDatatype)sock->type) {
      case SOCK_OBJECT: {
        bNodeSocketValueObject *default_value = (bNodeSocketValueObject *)sock->default_value;
        LOADER_expand(expander, default_value->value);
        break;
      }
      case SOCK_IMAGE: {
        bNodeSocketValueImage *default_value = (bNodeSocketValueImage *)sock->default_value;
        LOADER_expand(expander, default_value->value);
        break;
      }
      case SOCK_COLLECTION: {
        bNodeSocketValueCollection *default_value = (bNodeSocketValueCollection *)
                                                        sock->default_value;
        LOADER_expand(expander, default_value->value);
        break;
      }
      case SOCK_TEXTURE: {
        bNodeSocketValueTexture *default_value = (bNodeSocketValueTexture *)sock->default_value;
        LOADER_expand(expander, default_value->value);
        break;
      }
      case SOCK_MATERIAL: {
        bNodeSocketValueMaterial *default_value = (bNodeSocketValueMaterial *)sock->default_value;
        LOADER_expand(expander, default_value->value);
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
}

static void expand_node_sockets(DuneExpander *expander, ListBase *sockets)
{
  LISTBASE_FOREACH (bNodeSocket *, sock, sockets) {
    expand_node_socket(expander, sock);
  }
}

void ntreeDuneReadExpand(DuneExpander *expander, bNodeTree *ntree)
{
  if (ntree->gpd) {
    LOADER_expand(expander, ntree->gpd);
  }

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    if (node->id && !(node->type == CMP_NODE_R_LAYERS) &&
        !(node->type == CMP_NODE_CRYPTOMATTE && node->custom1 == CMP_CRYPTOMATTE_SRC_RENDER)) {
      LOADER_expand(expander, node->id);
    }

    IDP_DuneReadExpand(expander, node->prop);

    expand_node_sockets(expander, &node->inputs);
    expand_node_sockets(expander, &node->outputs);
  }

  expand_node_sockets(expander, &ntree->inputs);
  expand_node_sockets(expander, &ntree->outputs);
}

static void ntree_blend_read_expand(BlendExpander *expander, ID *id)
{
  bNodeTree *ntree = (bNodeTree *)id;
  ntreeBlendReadExpand(expander, ntree);
}

IDTypeInfo IDType_ID_NT = {
    /* id_code */ ID_NT,
    /* id_filter */ FILTER_ID_NT,
    /* main_listbase_index */ INDEX_ID_NT,
    /* struct_size */ sizeof(bNodeTree),
    /* name */ "NodeTree",
    /* name_plural */ "node_groups",
    /* translation_context */ BLT_I18NCONTEXT_ID_NODETREE,
    /* flags */ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /* asset_type_info */ nullptr,

    /* init_data */ ntree_init_data,
    /* copy_data */ ntree_copy_data,
    /* free_data */ ntree_free_data,
    /* make_local */ nullptr,
    /* foreach_id */ node_foreach_id,
    /* foreach_cache */ node_foreach_cache,
    /* foreach_path */ node_foreach_path,
    /* owner_get */ node_owner_get,

    /* blend_write */ ntree_blend_write,
    /* blend_read_data */ ntree_blend_read_data,
    /* blend_read_lib */ ntree_blend_read_lib,
    /* blend_read_expand */ ntree_blend_read_expand,

    /* blend_read_undo_preserve */ nullptr,

    /* lib_override_apply_post */ nullptr,
};

static void node_add_sockets_from_type(bNodeTree *ntree, bNode *node, bNodeType *ntype)
{
  if (ntype->declare != nullptr) {
    node_verify_sockets(ntree, node, true);
    return;
  }
  bNodeSocketTemplate *sockdef;

  if (ntype->inputs) {
    sockdef = ntype->inputs;
    while (sockdef->type != -1) {
      node_add_socket_from_template(ntree, node, sockdef, SOCK_IN);
      sockdef++;
    }
  }
  if (ntype->outputs) {
    sockdef = ntype->outputs;
    while (sockdef->type != -1) {
      node_add_socket_from_template(ntree, node, sockdef, SOCK_OUT);
      sockdef++;
    }
  }
}

/* NOTE: This function is called to initialize node data based on the type.
 * The bNodeType may not be registered at creation time of the node,
 * so this can be delayed until the node type gets registered.
 */
static void node_init(const struct bContext *C, bNodeTree *ntree, bNode *node)
{
  bNodeType *ntype = node->typeinfo;
  if (ntype == &NodeTypeUndefined) {
    return;
  }

  /* only do this once */
  if (node->flag & NODE_INIT) {
    return;
  }

  node->flag = NODE_SELECT | NODE_OPTIONS | ntype->flag;
  node->width = ntype->width;
  node->miniwidth = 42.0f;
  node->height = ntype->height;
  node->color[0] = node->color[1] = node->color[2] = 0.608; /* default theme color */
  /* initialize the node name with the node label.
   * NOTE: do this after the initfunc so nodes get their data set which may be used in naming
   * (node groups for example) */
  /* XXX Do not use nodeLabel() here, it returns translated content for UI,
   *     which should *only* be used in UI, *never* in data...
   *     Data have their own translation option!
   *     This solution may be a bit rougher than nodeLabel()'s returned string, but it's simpler
   *     than adding "do_translate" flags to this func (and labelfunc() as well). */
  BLI_strncpy(node->name, DATA_(ntype->ui_name), NODE_MAXSTR);
  nodeUniqueName(ntree, node);

  node_add_sockets_from_type(ntree, node, ntype);

  if (ntype->initfunc != nullptr) {
    ntype->initfunc(ntree, node);
  }

  if (ntree->typeinfo->node_add_init != nullptr) {
    ntree->typeinfo->node_add_init(ntree, node);
  }

  if (node->id) {
    id_us_plus(node->id);
  }

  /* extra init callback */
  if (ntype->initfunc_api) {
    PointerRNA ptr;
    RNA_pointer_create((ID *)ntree, &RNA_Node, node, &ptr);

    /* XXX Warning: context can be nullptr in case nodes are added in do_versions.
     * Delayed init is not supported for nodes with context-based `initfunc_api` at the moment. */
    BLI_assert(C != nullptr);
    ntype->initfunc_api(C, &ptr);
  }

  node->flag |= NODE_INIT;
}

static void ntree_set_typeinfo(bNodeTree *ntree, bNodeTreeType *typeinfo)
{
  if (typeinfo) {
    ntree->typeinfo = typeinfo;
  }
  else {
    ntree->typeinfo = &NodeTreeTypeUndefined;
  }

  /* Deprecated integer type. */
  ntree->type = ntree->typeinfo->type;
  BKE_ntree_update_tag_all(ntree);
}

static void node_set_typeinfo(const struct bContext *C,
                              bNodeTree *ntree,
                              bNode *node,
                              bNodeType *typeinfo)
{
  /* for nodes saved in older versions storage can get lost, make undefined then */
  if (node->flag & NODE_INIT) {
    if (typeinfo && typeinfo->storagename[0] && !node->storage) {
      typeinfo = nullptr;
    }
  }

  if (typeinfo) {
    node->typeinfo = typeinfo;

    /* deprecated integer type */
    node->type = typeinfo->type;

    /* initialize the node if necessary */
    node_init(C, ntree, node);
  }
  else {
    node->typeinfo = &NodeTypeUndefined;
  }
}

static void node_socket_set_typeinfo(bNodeTree *ntree,
                                     bNodeSocket *sock,
                                     bNodeSocketType *typeinfo)
{
  if (typeinfo) {
    sock->typeinfo = typeinfo;

    /* deprecated integer type */
    sock->type = typeinfo->type;

    if (sock->default_value == nullptr) {
      /* initialize the default_value pointer used by standard socket types */
      node_socket_init_default_value(sock);
    }
  }
  else {
    sock->typeinfo = &NodeSocketTypeUndefined;
  }
  BKE_ntree_update_tag_socket_type(ntree, sock);
}

/* Set specific typeinfo pointers in all node trees on register/unregister */
static void update_typeinfo(Main *bmain,
                            const struct bContext *C,
                            bNodeTreeType *treetype,
                            bNodeType *nodetype,
                            bNodeSocketType *socktype,
                            bool unregister)
{
  if (!bmain) {
    return;
  }

  FOREACH_NODETREE_BEGIN (bmain, ntree, id) {
    if (treetype && STREQ(ntree->idname, treetype->idname)) {
      ntree_set_typeinfo(ntree, unregister ? nullptr : treetype);
    }

    /* initialize nodes */
    LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
      if (nodetype && STREQ(node->idname, nodetype->idname)) {
        node_set_typeinfo(C, ntree, node, unregister ? nullptr : nodetype);
      }

      /* initialize node sockets */
      LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
        if (socktype && STREQ(sock->idname, socktype->idname)) {
          node_socket_set_typeinfo(ntree, sock, unregister ? nullptr : socktype);
        }
      }
      LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
        if (socktype && STREQ(sock->idname, socktype->idname)) {
          node_socket_set_typeinfo(ntree, sock, unregister ? nullptr : socktype);
        }
      }
    }

    /* initialize tree sockets */
    LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->inputs) {
      if (socktype && STREQ(sock->idname, socktype->idname)) {
        node_socket_set_typeinfo(ntree, sock, unregister ? nullptr : socktype);
      }
    }
    LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->outputs) {
      if (socktype && STREQ(sock->idname, socktype->idname)) {
        node_socket_set_typeinfo(ntree, sock, unregister ? nullptr : socktype);
      }
    }
  }
  FOREACH_NODETREE_END;
}

void ntreeSetTypes(const struct bContext *C, bNodeTree *ntree)
{
  ntree_set_typeinfo(ntree, ntreeTypeFind(ntree->idname));

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    node_set_typeinfo(C, ntree, node, nodeTypeFind(node->idname));

    LISTBASE_FOREACH (bNodeSocket *, sock, &node->inputs) {
      node_socket_set_typeinfo(ntree, sock, nodeSocketTypeFind(sock->idname));
    }
    LISTBASE_FOREACH (bNodeSocket *, sock, &node->outputs) {
      node_socket_set_typeinfo(ntree, sock, nodeSocketTypeFind(sock->idname));
    }
  }

  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->inputs) {
    node_socket_set_typeinfo(ntree, sock, nodeSocketTypeFind(sock->idname));
  }
  LISTBASE_FOREACH (bNodeSocket *, sock, &ntree->outputs) {
    node_socket_set_typeinfo(ntree, sock, nodeSocketTypeFind(sock->idname));
  }
}

static GHash *nodetreetypes_hash = nullptr;
static GHash *nodetypes_hash = nullptr;
static GHash *nodesockettypes_hash = nullptr;

bNodeTreeType *ntreeTypeFind(const char *idname)
{
  if (idname[0]) {
    bNodeTreeType *nt = (bNodeTreeType *)BLI_ghash_lookup(nodetreetypes_hash, idname);
    if (nt) {
      return nt;
    }
  }

  return nullptr;
}

void ntreeTypeAdd(bNodeTreeType *nt)
{
  BLI_ghash_insert(nodetreetypes_hash, nt->idname, nt);
  /* XXX pass Main to register function? */
  /* Probably not. It is pretty much expected we want to update G_MAIN here I think -
   * or we'd want to update *all* active Mains, which we cannot do anyway currently. */
  update_typeinfo(G_MAIN, nullptr, nt, nullptr, nullptr, false);
}

/* callback for hash value free function */
static void ntree_free_type(void *treetype_v)
{
  bNodeTreeType *treetype = (bNodeTreeType *)treetype_v;
  /* XXX pass Main to unregister function? */
  /* Probably not. It is pretty much expected we want to update G_MAIN here I think -
   * or we'd want to update *all* active Mains, which we cannot do anyway currently. */
  update_typeinfo(G_MAIN, nullptr, treetype, nullptr, nullptr, true);
  MEM_freeN(treetype);
}

void ntreeTypeFreeLink(const bNodeTreeType *nt)
{
  BLI_ghash_remove(nodetreetypes_hash, nt->idname, nullptr, ntree_free_type);
}

bool ntreeIsRegistered(bNodeTree *ntree)
{
  return (ntree->typeinfo != &NodeTreeTypeUndefined);
}

GHashIterator *ntreeTypeGetIterator()
{
  return BLI_ghashIterator_new(nodetreetypes_hash);
}

bNodeType *nodeTypeFind(const char *idname)
{
  if (idname[0]) {
    bNodeType *nt = (bNodeType *)BLI_ghash_lookup(nodetypes_hash, idname);
    if (nt) {
      return nt;
    }
  }

  return nullptr;
}
