/** Mesh inline iterator functions. */

#pragma once

/* inline here optimizes out the switch statement when called with
 * constant values (which is very common), nicer for loop-in-loop situations */

/* Iterator Step
 * Calls an iterators step function to return the next element.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE void *mesh_iter_step(MeshIter *iter)
{
  return iter->step(iter);
}

/**
 * Iterator Init
 *
 * Takes a mesh iterator structure and fills
 * it with the appropriate function pointers based
 * upon its type.
 */
ATTR_NONNULL(1)
LIB_INLINE bool mesh_iter_init(MeshIter *iter, Mesh *mesh, const char itype, void *data)
{
  /* int argtype; */
  iter->itype = itype;

  /* inlining optimizes out this switch when called with the defined type */
  switch ((MeshIterType)itype) {
    case MESH_VERTS_OF_MESH:
      lib_assert(mesh != NULL);
      lib_assert(data == NULL);
      iter->begin = (MeshIter__begin_cb)bmiter__elem_of_mesh_begin;
      iter->step = (MeshIter__step_cb)bmiter__elem_of_mesh_step;
      iter->data.elem_of_mesh.pooliter.pool = bm->vpool;
      break;
    case MESH_EDGES_OF_MESH:
      lib_assert(mesh != NULL);
      lib_assert(data == NULL);
      iter->begin = (MeshIter__begin_cb)bmiter__elem_of_mesh_begin;
      iter->step = (MeshIter__step_cb)bmiter__elem_of_mesh_step;
      iter->data.elem_of_mesh.pooliter.pool = bm->epool;
      break;
    case MESH_FACES_OF_MESH:
      lib_assert(mesh != NULL);
      lib_assert(data == NULL);
      iter->begin = (MeshIter__begin_cb)bmiter__elem_of_mesh_begin;
      iter->step = (MeshIter__step_cb)bmiter__elem_of_mesh_step;
      iter->data.elem_of_mesh.pooliter.pool = mesh->fpool;
      break;
    case MESH_EDGES_OF_VERT:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_VERT);
      iter->begin = (MeshIter__begin_cb)bmiter__edge_of_vert_begin;
      iter->step = (MeshIter__step_cb)bmiter__edge_of_vert_step;
      iter->data.edge_of_vert.vdata = (MeshVert *)data;
      break;
    case MESH_FACES_OF_VERT:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_VERT);
      iter->begin = (MeshIter__begin_cb)bmiter__face_of_vert_begin;
      iter->step = (MeshIter__step_cb)bmiter__face_of_vert_step;
      iter->data.face_of_vert.vdata = (MeshVert *)data;
      break;
    case MESH_LOOPS_OF_VERT:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_VERT);
      iter->begin = (MeshIter__begin_cb)bmiter__loop_of_vert_begin;
      iter->step = (MeshIter__step_cb)bmiter__loop_of_vert_step;
      iter->data.loop_of_vert.vdata = (MeshVert *)data;
      break;
    case MESH_VERTS_OF_EDGE:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_EDGE);
      iter->begin = (MeshIter__begin_cb)bmiter__vert_of_edge_begin;
      iter->step = (MeshIter__step_cb)bmiter__vert_of_edge_step;
      iter->data.vert_of_edge.edata = (MeshEdge *)data;
      break;
    case BM_FACES_OF_EDGE:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_EDGE);
      iter->begin = (MeshIter__begin_cb)bmiter__face_of_edge_begin;
      iter->step = (MeshIter__step_cb)bmiter__face_of_edge_step;
      iter->data.face_of_edge.edata = (MeshEdge *)data;
      break;
    case MESH_VERTS_OF_FACE:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_FACE);
      iter->begin = (MeshIter__begin_cb)bmiter__vert_of_face_begin;
      iter->step = (MeshIter__step_cb)bmiter__vert_of_face_step;
      iter->data.vert_of_face.pdata = (MeshFace *)data;
      break;
    case MESH_EDGES_OF_FACE:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == BM_FACE);
      iter->begin = (MeshIter__begin_cb)bmiter__edge_of_face_begin;
      iter->step = (MeshIter__step_cb)bmiter__edge_of_face_step;
      iter->data.edge_of_face.pdata = (MeshFace *)data;
      break;
    case MESH_LOOPS_OF_FACE:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_FACE);
      iter->begin = (MeshIter__begin_cb)bmiter__loop_of_face_begin;
      iter->step = (MeshIter__step_cb)bmiter__loop_of_face_step;
      iter->data.loop_of_face.pdata = (MeshFace *)data;
      break;
    case MESH_LOOPS_OF_LOOP:
      lib_assert(data != NULL);
      lib_assert(((MeshElem *)data)->head.htype == MESH_LOOP);
      iter->begin = (MeshIter__begin_cb)bmiter__loop_of_loop_begin;
      iter->step = (MeshIter__step_cb)bmiter__loop_of_loop_step;
      iter->data.loop_of_loop.ldata = (MeshLoop*)data;
      break;
    case BM_LOOPS_OF_EDGE:
      BLI_assert(data != NULL);
      BLI_assert(((BMElem *)data)->head.htype == BM_EDGE);
      iter->begin = (BMIter__begin_cb)bmiter__loop_of_edge_begin;
      iter->step = (BMIter__step_cb)bmiter__loop_of_edge_step;
      iter->data.loop_of_edge.edata = (BMEdge *)data;
      break;
    default:
      /* should never happen */
      lib_assert(0);
      return false;
      break;
  }

  iter->begin(iter);

  return true;
}

/**
 * Iterator New
 *
 * Takes a mesh iterator structure and fills
 * it with the appropriate function pointers based
 * upon its type and then calls MeshIter_step()
 * to return the first element of the iterator.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    void *mesh_iter_new(MeshIter *iter, Mesh *mesh, const char itype, void *data)
{
  if (LIKELY(mesh_iter_init(iter, mesh, itype, data))) {
    return mesh_iter_step(iter);
  }
  else {
    return NULL;
  }
}

/**
 * Parallel (threaded) iterator,
 * only available for most basic itertypes (verts/edges/faces of mesh).
 *
 * Uses lib_task_parallel_mempool to iterate over all items of underlying matching mempool.
 *
 * You have to include lib_task.h before Mesh includes to be able to use this function!
 */

#ifdef __LIB_TASK_H__

ATTR_NONNULL(1)
LIB_INLINE void mesh_iter_parallel(Mesh *mesn,
                                   const char itype,
                                   TaskParallelMempoolFunc func,
                                   void *userdata,
                                   const TaskParallelSettings *settings)
{
  /* inlining optimizes out this switch when called with the defined type */
  switch ((MeshIterType)itype) {
    case MESH_VERTS_OF_MESH:
      lib_task_parallel_mempool(mesh->vpool, userdata, fn, settings);
      break;
    case MESH_EDGES_OF_MESH:
      lib_task_parallel_mempool(mesh->epool, userdata, fn, settings);
      break;
    case MESH_FACES_OF_MESH:
      lib_task_parallel_mempool(mesh->fpool, userdata, fn, settings);
      break;
    default:
      /* should never happen */
      lib_assert(0);
      break;
  }
}

#endif /* __LIB_TASK_H__ */
