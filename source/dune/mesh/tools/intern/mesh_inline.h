#pragma once

/* stuff for dealing with header flags */
#define mesh_elem_flag_test(ele, hflag) _mesh_elem_flag_test(&(ele)->head, hflag)
#define mesh_elem_flag_test_bool(ele, hflag) _mesh_elem_flag_test_bool(&(ele)->head, hflag)
#define mesh_elem_flag_enable(ele, hflag) _mesh_elem_flag_enable(&(ele)->head, hflag)
#define mesh_elem_flag_disable(ele, hflag) _mesh_elem_flag_disable(&(ele)->head, hflag)
#define mesh_elem_flag_set(ele, hflag, val) _mesh_elem_flag_set(&(ele)->head, hflag, val)
#define mesh_elem_flag_toggle(ele, hflag) _mesh_elem_flag_toggle(&(ele)->head, hflag)
#define mesh_elem_flag_merge(ele_a, ele_b) _mesh_elem_flag_merge(&(ele_a)->head, &(ele_b)->head)
#define mesh_elem_flag_merge_ex(ele_a, ele_b, hflag_and) \
  _mesh_elem_flag_merge_ex(&(ele_a)->head, &(ele_b)->head, hflag_and)
#define mesh_elem_flag_merge_into(ele, ele_a, ele_b) \
  _mesh_elem_flag_merge_into(&(ele)->head, &(ele_a)->head, &(ele_b)->head)

ATTR_WARN_UNUSED_RESULT
LIB_INLINE char _mesh_elem_flag_test(const MeshHeader *head, const char hflag)
{
  return head->hflag & hflag;
}

ATTR_WARN_UNUSED_RESULT
LIB_INLINE bool _mesh_elem_flag_test_bool(const MeshHeader *head, const char hflag)
{
  return (head->hflag & hflag) != 0;
}

LIB_INLINE void _mesh_elem_flag_enable(MeshHeader *head, const char hflag)
{
  head->hflag |= hflag;
}

LIB_INLINE void _mesh_elem_flag_disable(MeshHeader *head, const char hflag)
{
  head->hflag &= (char)~hflag;
}

LIB_INLINE void _mesh_elem_flag_set(MeshHeader *head, const char hflag, const int val)
{
  if (val) {
    _mesh_elem_flag_enable(head, hflag);
  }
  else {
    _mesh_elem_flag_disable(head, hflag);
  }
}

LIB_INLINE void _mesh_elem_flag_toggle(MeshHeader *head, const char hflag)
{
  head->hflag ^= hflag;
}

LIB_INLINE void _mesh_elem_flag_merge(MeshHeader *head_a, MeshHeader *head_b)
{
  head_a->hflag = head_b->hflag = head_a->hflag | head_b->hflag;
}

LIB_INLINE void _mesh_elem_flag_merge_ex(MeshHeader *head_a, MeshHeader *head_b, const char hflag_and)
{
  if (((head_a->hflag & head_b->hflag) & hflag_and) == 0) {
    head_a->hflag &= ~hflag_and;
    head_b->hflag &= ~hflag_and;
  }
  _mesh_elem_flag_merge(head_a, head_b);
}

LIB_INLINE void _mesh_elem_flag_merge_into(MeshHeader *head,
                                         const MeshHeader *head_a,
                                         const MeshHeader *head_b)
{
  head->hflag = head_a->hflag | head_b->hflag;
}

/**
 * notes on mesh_elem_index_set(...) usage,
 * Set index is sometimes abused as temp storage, other times we can't be
 * sure if the index values are valid because certain operations have modified
 * the mesh structure.
 *
 * To set the elements to valid indices 'mesh_elem_index_ensure' should be used
 * rather than adding inline loops, however there are cases where we still
 * set the index directly
 *
 * In an attempt to manage this,
 * here are 5 tags I'm adding to uses of mesh_elem_index_set
 *
 * - 'set_inline'  -- since the data is already being looped over set to a
 *                    valid value inline.
 *
 * - 'set_dirty!'  -- intentionally sets the index to an invalid value,
 *                    flagging 'mesh->elem_index_dirty' so we don't use it.
 *
 * - 'set_ok'      -- this is valid use since the part of the code is low level.
 *
 * - 'set_ok_invalid'  -- set to -1 on purpose since this should not be
 *                    used without a full array re-index, do this on
 *                    adding new vert/edge/faces since they may be added at
 *                    the end of the array.
 *
 * - campbell */

#define mesh_elem_index_get(ele) _mesh_elem_index_get(&(ele)->head)
#define mesh_elem_index_set(ele, index) _mesh_elem_index_set(&(ele)->head, index)

LIB_INLINE void _mesh_elem_index_set(MeshHeader *head, const int index)
{
  head->index = index;
}

ATTR_WARN_UNUSED_RESULT
BLI_INLINE int _bm_elem_index_get(const BMHeader *head)
{
  return head->index;
}
