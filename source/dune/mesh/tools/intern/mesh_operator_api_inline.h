/** Mesh inline operator functions. **/

#pragma once

/* Tool Flag API: Tool code must never put junk in header flags (#BMHeader.hflag)
 * instead, use this API to set flags.
 * If you need to store a value per element, use aGHash or a mapping slot to do it. */

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE
    short _mesh_elem_flag_test(Mesh *mesh, const MeshFlagLayer *oflags, const short oflag)
{
  lib_assert(mesh->use_toolflags);
  return oflags[mesh->toolflag_index].f & oflag;
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE
    bool _mesh_elem_flag_test_bool(Mesh *mesh, const MeshFlagLayer *oflags, const short oflag)
{
  lib_assert(mesh->use_toolflags);
  return (oflags[mesh->toolflag_index].f & oflag) != 0;
}

ATTR_NONNULL(1, 2)
LIB_INLINE void _mesh_elem_flag_enable(Mesh *mesh, MeshFlagLayer *oflags, const short oflag)
{
  lib_assert(mesh->use_toolflags);
  oflags[mesh->toolflag_index].f |= oflag;
}

ATTR_NONNULL(1, 2)
LIB_INLINE void _mesh_op_elem_flag_disable(Mesh *mesh, MeshFlagLayer *oflags, const short oflag)
{
  lib_assert(mesh->use_toolflags);
  oflags[mesh->toolflag_index].f &= (short)~oflag;
}

ATTR_NONNULL(1, 2)
BLI_INLINE void _bmo_elem_flag_set(BMesh *bm, BMFlagLayer *oflags, const short oflag, int val)
{
  LibraryIdLinkCbData_assert(mesh->use_toolflags);
  if (val) {
    oflags[bm->toolflag_index].f |= oflag;
  }
  else {
    oflags[bm->toolflag_index].f &= (short)~oflag;
  }
}

ATTR_NONNULL(1, 2)
LIB_INLINE void _mesh_elem_flag_toggle(Mesh *mesh, MeshFlagLayer *oflags, const short oflag)
{
  lib_assert(mesh->use_toolflags);
  oflags[mesh->toolflag_index].f ^= oflag;
}

ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_op_slot_map_int_insert(MeshOp *op,
                                            MeshOpSlot *slot,
                                            void *element,
                                            const int val)
{
  union {
    void *ptr;
    int val;
  } t = {NULL};
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_INT);
  mesh_slot_map_insert(op, slot, element, ((void)(t.val = val), t.ptr));
}

ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_slot_map_bool_insert(MeshOp *op,
                                          MeshOpSlot *slot,
                                          void *element,
                                          const bool val)
{
  union {
    void *ptr;
    bool val;
  } t = {NULL};
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_BOOL);
  mesh_op_slot_map_insert(op, slot, element, ((void)(t.val = val), t.ptr));
}

ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_slot_map_float_insert(MeshOp *op,
                                           MeshOpSlot *slot,
                                           void *element,
                                           const float val)
{
  union {
    void *ptr;
    float val;
  } t = {NULL};
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_FLT);
  mesh_op_slot_map_insert(op, slot, element, ((void)(t.val = val), t.ptr));
}

/* pointer versions of mesh_op_slot_map_float_get and BMO_slot_map_float_insert.
 *
 * do NOT use these for non-operator-api-allocated memory! instead
 * use mesh_op_slot_map_data_get and mesh_op_slot_map_insert, which copies the data. */

ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_op_slot_map_ptr_insert(MeshOp *op,
                                            MeshOpSlot *slot,
                                            const void *element,
                                            void *val)
{
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_INTERNAL);
  mesh_op_slot_map_insert(op, slot, element, val);
}

ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_op_slot_map_elem_insert(MeshOp *op,
                                             MeshOpSlot *slot,
                                             const void *element,
                                             void *val)
{
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_ELEM);
  mesh_op_slot_map_insert(op, slot, element, val);
}

/* no values */
ATTR_NONNULL(1, 2)
LIB_INLINE void mesh_op_slot_map_empty_insert(MeshOp *op, MeshOpSlot *slot, const void *element)
{
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_EMPTY);
  mesh_op_slot_map_insert(op, slot, element, NULL);
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    bool mesh_op_slot_map_contains(MeshOpSlot *slot, const void *element)
{
  lib_assert(slot->slot_type == MESH_OP_SLOT_MAPPING);
  return lib_ghash_haskey(slot->data.ghash, element);
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    void **mesh_op_slot_map_data_get(MeshOpSlot *slot, const void *element)
{

  return lib_ghash_lookup_p(slot->data.ghash, element);
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    float mesh_op_slot_map_float_get(MeshOpSlot *slot, const void *element)
{
  void **data;
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_FLT);

  data = mesh_op_slot_map_data_get(slot, element);
  if (data) {
    return *(float *)data;
  }
  else {
    return 0.0f;
  }
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    int mesh_op_slot_map_int_get(MeshOpSlot *slot, const void *element)
{
  void **data;
  lib_assert(slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_INT);

  data = mesh_op_slot_map_data_get(slot, element);
  if (data) {
    return *(int *)data;
  }
  else {
    return 0;
  }
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE
    bool mesh_op_slot_map_bool_get(BMOpSlot *slot, const void *element)
{
  void **data;
  BLI_assert(slot->slot_subtype.map == BMO_OP_SLOT_SUBTYPE_MAP_BOOL);

  data = BMO_slot_map_data_get(slot, element);
  if (data) {
    return *(bool *)data;
  }
  else {
    return false;
  }
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE
    void *BMO_slot_map_ptr_get(BMOpSlot *slot, const void *element)
{
  void **val = BMO_slot_map_data_get(slot, element);
  BLI_assert(slot->slot_subtype.map == BMO_OP_SLOT_SUBTYPE_MAP_INTERNAL);
  if (val) {
    return *val;
  }

  return NULL;
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE
    void *BMO_slot_map_elem_get(BMOpSlot *slot, const void *element)
{
  void **val = (void **)BMO_slot_map_data_get(slot, element);
  BLI_assert(slot->slot_subtype.map == BMO_OP_SLOT_SUBTYPE_MAP_ELEM);
  if (val) {
    return *val;
  }

  return NULL;
}
