/**
 * Functions to abstract looping over mesh data structures.
 * See:mesh_iters_inlin.c too, some functions are here for speed reasons.
 */

#include "mem_guardedalloc.h"

#include "lib_bitmap.h"
#include "lib_utildefines.h"

#include "mesh.h"
#include "intern/mesh_private.h"

const char mesh_iter_itype_htype_map[MESH_ITYPE_MAX] = {
    '\0',
    MESH_VERT, /* M_VERTS_OF_MESH */
    MESH_EDGE, /* M_EDGES_OF_MESH */
    MESH_FACE, /* M_FACES_OF_MESH */
    MESH_EDGE, /* M_EDGES_OF_VERT */
    MESH_FACE, /* M_FACES_OF_VERT */
    MESH_LOOP, /* M_LOOPS_OF_VERT */
    MESH_VERT, /* M_VERTS_OF_EDGE */
    MESH_FACE, /* M_FACES_OF_EDGE */
    MESH_VERT, /* M_VERTS_OF_FACE */
    MESH_EDGE, /* M_EDGES_OF_FACE */
    MESH_LOOP, /* M_LOOPS_OF_FACE */
    MESH_LOOP, /* M_LOOPS_OF_LOOP */
    MESH_LOOP, /* M_LOOPS_OF_EDGE */
};

int mesh_iter_mesh_count(const char itype, BMesh *bm)
{
  int count;

  switch (itype) {
    case MESH_VERTS_OF_MESH:
      count = m->totvert;
      break;
    case MESH_EDGES_OF_MESH:
      count = m->totedge;
      break;
    case MESH_FACES_OF_MESH:
      count = m->totface;
      break;
    default:
      count = 0;
      lib_assert(0);
      break;
  }

  return count;
}

void *mesh_iter_at_index(Mesh *m, const char itype, void *data, int index)
{
  MeshIter iter;
  void *val;
  int i;

  /* sanity check */
  if (index < 0) {
    return NULL;
  }

  val = mesh_iter_new(&iter, mesh, itype, data);

  i = 0;
  while (i < index) {
    val = mesh_iter_step(&iter);
    i++;
  }

  return val;
}

int mesh_iter_as_array(Mesh *mesh, const char itype, void *data, void **array, const int len)
{
  int i = 0;

  /* sanity check */
  if (len > 0) {
    MeshIter iter;
    void *ele;

    for (ele = mesh_iter_new(&iter, mesh, itype, data); ele; ele = BM_iter_step(&iter)) {
      array[i] = ele;
      i++;
      if (i == len) {
        return len;
      }
    }
  }

  return i;
}
int mesh_iter_as_array(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                       const char *slot_name,
                       const char restrictmask,
                       void **array,
                       const int len)
{
  int i = 0;

  /* sanity check */
  if (len > 0) {
    MeshOpIter oiter;
    void *ele;

    for (ele = mesh_op_iter_new(&oiter, slot_args, slot_name, restrictmask); ele;
         ele = mesh_op_iter_step(&oiter)) {
      array[i] = ele;
      i++;
      if (i == len) {
        return len;
      }
    }
  }

  return i;
}

void *mesh_iter_as_arrayN(Mesh *mesh,
                          const char itype,
                          void *data,
                          int *r_len,
                          /* optional args to avoid an alloc (normally stack array) */
                          void **stack_array,
                          int stack_array_size)
{
  MeshIter iter;

  lib_assert(stack_array_size == 0 || (stack_array_size && stack_array));

  /* We can't rely on MeshIter.count being set. */
  switch (itype) {
    case MESH_VERTS_OF_MESH:
      iter.count = mesh->totvert;
      break;
    case MESH_EDGES_OF_MESH:
      iter.count = mesh->totedge;
      break;
    case MESH_FACES_OF_MESH:
      iter.count = mesh->totface;
      break;
    default:
      break;
  }

  if (mesh_iter_init(&iter, mesh, itype, data) && iter.count > 0) {
    MeshElem *ele;
    MeshElem **array = iter.count > stack_array_size ?
                         mem_mallocn(sizeof(ele) * iter.count, __func__) :
                         stack_array;
    int i = 0;

    *r_len = iter.count; /* set before iterating */

    while ((ele = mesh_iter_step(&iter))) {
      array[i++] = ele;
    }
    return array;
  }

  *r_len = 0;
  return NULL;
}

void *mesh_iter_as_arrayN(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                         const char *slot_name,
                         const char restrictmask,
                         int *r_len,
                         /* optional args to avoid an alloc (normally stack array) */
                         void **stack_array,
                         int stack_array_size)
{
  MeshOpIter iter;
  MeshElem *ele;
  const int slot_len = mesh_slot_buffer_len(slot_args, slot_name);

  lib_assert(stack_array_size == 0 || (stack_array_size && stack_array));

  if ((ele = mesh_op_iter_new(&iter, slot_args, slot_name, restrictmask)) && slot_len > 0) {
    MeshElem **array = slot_len > stack_array_size ? mem_mallocN(sizeof(ele) * slot_len, __func__) :
                                                   stack_array;
    int i = 0;

    do {
      array[i++] = ele;
    } while ((ele = mesh_op_iter_step(&iter)));
    lib_assert(i <= slot_len);

    if (i != slot_len) {
      if ((void **)array != stack_array) {
        array = mem_reallocn(array, sizeof(ele) * i);
      }
    }
    *r_len = i;
    return array;
  }

  *r_len = 0;
  return NULL;
}

int mesh_iter_bitmap_from_filter(const char itype,
                                 Mesh *mesh,
                                 lib_bitmap *bitmap,
                                 bool (*test_fn)(MeshElem *, void *user_data),
                                 void *user_data)
{
  MeshIter iter;
  MeshElem *ele;
  int i;
  int bitmap_enabled = 0;

  MESH_ITER_MESH_INDEX (ele, &iter, mesh, itype, i) {
    if (test_fn(ele, user_data)) {
      LIB_BITMAP_ENABLE(bitmap, i);
      bitmap_enabled++;
    }
    else {
      LIB_BITMAP_DISABLE(bitmap, i);
    }
  }

  return bitmap_enabled;
}

int mesh_iter_bitmap_from_filter_tessface(Mesh *mesh,
                                          lib_bitmap *bitmap,
                                          bool (*test_fn)(MeshFace *, void *user_data),
                                          void *user_data)
{
  MeshIter iter;
  MeshFace *f;
  int i;
  int j = 0;
  int bitmap_enabled = 0;

  MESH_ITER_MESH_INDEX (f, &iter, mesh, MESH_FACES_OF_MESH, i) {
    if (test_fn(f, user_data)) {
      for (int tri = 2; tri < f->len; tri++) {
        LIB_BITMAP_ENABLE(bitmap, j);
        bitmap_enabled++;
        j++;
      }
    }
    else {
      for (int tri = 2; tri < f->len; tri++) {
        LIB_BITMAP_DISABLE(bitmap, j);
        j++;
      }
    }
  }

  return bitmap_enabled;
}

int mesh_iter_elem_count_flag(const char itype, void *data, const char hflag, const bool value)
{
  MeshIter iter;
  MeshElem *ele;
  int count = 0;

  MESH_ITER_ELEM (ele, &iter, data, itype) {
    if (mesh_elem_flag_test_bool(ele, hflag) == value) {
      count++;
    }
  }

  return count;
}

int mesh_op_iter_elem_count_flag(
    Mesh *mesh, const char itype, void *data, const short oflag, const bool value)
{
  MeshIter iter;
  int count = 0;

  /* loops have no header flags */
  lib_assert(mesh_iter_itype_htype_map[itype] != MESH_LOOP);

  switch (mesh_iter_itype_htype_map[itype]) {
    case MESH_VERT: {
      MeshVert *ele;
      MESH_ITER_ELEM (ele, &iter, data, itype) {
        if (mesh_op_vert_flag_test_bool(mesh, ele, oflag) == value) {
          count++;
        }
      }
      break;
    }
    case MESH_EDGE: {
      MeshEdge *ele;
      MESH_ITER_ELEM (ele, &iter, data, itype) {
        if (mesh_op_edge_flag_test_bool(mesh, ele, oflag) == value) {
          count++;
        }
      }
      break;
    }
    case MESH_FACE: {
      MeshFace *ele;
      MESH_ITER_ELEM (ele, &iter, data, itype) {
        if (mesh_face_flag_test_bool(mesh, ele, oflag) == value) {
          count++;
        }
      }
      break;
    }
  }
  return count;
}

int mesh_iter_mesh_count_flag(const char itype, Mesh *mesh, const char hflag, const bool value)
{
  MeshIter iter;
  MeshElem *ele;
  int count = 0;

  MESH_ITER_MESH (ele, &iter, mesh, itype) {
    if (mesh_elem_flag_test_bool(ele, hflag) == value) {
      count++;
    }
  }

  return count;
}

/**
 * Notes on iterator implementation:
 *
 * Iterators keep track of the next element in a sequence.
 * When a step() callback is invoked the current value of 'next'
 * is stored to be returned later and the next variable is incremented.
 *
 * When the end of a sequence is reached, next should always equal NULL
 *
 * The 'meshiter__' prefix is used because these are used in
 * mesh_iterators_inine.c but should otherwise be seen as
 * private.
 */

/*
 * VERT OF MESH CALLBACKS
 */

/* see bug T36923 for why we need this,
 * allow adding but not removing, this isn't _totally_ safe since
 * you could add/remove within the same loop, but catches common cases
 */
#ifdef DEBUG
#  define USE_IMMUTABLE_ASSERT
#endif

void meshiter__elem_of_mesh_begin(struct MeshIter__elem_of_mesh *iter)
{
#ifdef USE_IMMUTABLE_ASSERT
  ((MeshIter *)iter)->count = lib_mempool_len(iter->pooliter.pool);
#endif
  lib_mempool_iternew(iter->pooliter.pool, &iter->pooliter);
}

void *bmiter__elem_of_mesh_step(struct MeshIter__elem_of_mesh *iter)
{
#ifdef USE_IMMUTABLE_ASSERT
  lib_assert(((MeshIter *)iter)->count <= lib_mempool_len(iter->pooliter.pool));
#endif
  return lib_mempool_iterstep(&iter->pooliter);
}

#ifdef USE_IMMUTABLE_ASSERT
#  undef USE_IMMUTABLE_ASSERT
#endif

/** EDGE OF VERT CALLBACKS */

void meshiter__edge_of_vert_begin(struct MeshIter__edge_of_vert *iter)
{
  if (iter->vdata->e) {
    iter->e_first = iter->vdata->e;
    iter->e_next = iter->vdata->e;
  }
  else {
    iter->e_first = NULL;
    iter->e_next = NULL;
  }
}

void *meshiter__edge_of_vert_step(struct MeshIter__edge_of_vert *iter)
{
  MeshEdge *e_curr = iter->e_next;

  if (iter->e_next) {
    iter->e_next = mesh_disk_edge_next(iter->e_next, iter->vdata);
    if (iter->e_next == iter->e_first) {
      iter->e_next = NULL;
    }
  }

  return e_curr;
}

/* * FACE OF VERT CALLBACKS */

void meshiter__face_of_vert_begin(struct MeshIter__face_of_vert *iter)
{
  ((MeshIter *)iter)->count = mesh_disk_facevert_count(iter->vdata);
  if (((MeshIter *)iter)->count) {
    iter->l_first = mesh_disk_faceloop_find_first(iter->vdata->e, iter->vdata);
    iter->e_first = iter->l_first->e;
    iter->e_next = iter->e_first;
    iter->l_next = iter->l_first;
  }
  else {
    iter->l_first = iter->l_next = NULL;
    iter->e_first = iter->e_next = NULL;
  }
}
void *meshiter__face_of_vert_step(struct MeshIter__face_of_vert *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (((MeshIter *)iter)->count && iter->l_next) {
    ((MeshIter *)iter)->count--;
    iter->l_next = mesh_radial_faceloop_find_next(iter->l_next, iter->vdata);
    if (iter->l_next == iter->l_first) {
      iter->e_next = mesh_disk_faceedge_find_next(iter->e_next, iter->vdata);
      iter->l_first = mesh_radial_faceloop_find_first(iter->e_next->l, iter->vdata);
      iter->l_next = iter->l_first;
    }
  }

  if (!((MeshIter *)iter)->count) {
    iter->l_next = NULL;
  }

  return l_curr ? l_curr->f : NULL;
}

/** LOOP OF VERT CALLBACKS **/

void meshiter__loop_of_vert_begin(struct MeshIter__loop_of_vert *iter)
{
  ((MeshIter *)iter)->count = mesh_disk_facevert_count(iter->vdata);
  if (((MeshIter *)iter)->count) {
    iter->l_first = mesh_disk_faceloop_find_first(iter->vdata->e, iter->vdata);
    iter->e_first = iter->l_first->e;
    iter->e_next = iter->e_first;
    iter->l_next = iter->l_first;
  }
  else {
    iter->l_first = iter->l_next = NULL;
    iter->e_first = iter->e_next = NULL;
  }
}
void *meshiter__loop_of_vert_step(struct MeshIter__loop_of_vert *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (((MeshIter *)iter)->count) {
    ((MeshIter *)iter)->count--;
    iter->l_next = mesh_radial_faceloop_find_next(iter->l_next, iter->vdata);
    if (iter->l_next == iter->l_first) {
      iter->e_next = mesh_disk_faceedge_find_next(iter->e_next, iter->vdata);
      iter->l_first = mesh_radial_faceloop_find_first(iter->e_next->l, iter->vdata);
      iter->l_next = iter->l_first;
    }
  }

  if (!((MeshIter *)iter)->count) {
    iter->l_next = NULL;
  }

  /* NULL on finish */
  return l_curr;
}

/** LOOP OF EDGE CALLBACKS */

void meshiter__loop_of_edge_begin(struct MeshIter__loop_of_edge *iter)
{
  iter->l_first = iter->l_next = iter->edata->l;
}

void *miter__loop_of_edge_step(struct MeshIter__loop_of_edge *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->radial_next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  /* NULL on finish */
  return l_curr;
}

/** LOOP OF LOOP CALLBACKS */

void meshiter__loop_of_loop_begin(struct BMIter__loop_of_loop *iter)
{
  iter->l_first = iter->ldata;
  iter->l_next = iter->l_first->radial_next;

  if (iter->l_next == iter->l_first) {
    iter->l_next = NULL;
  }
}

void *meshiter__loop_of_loop_step(struct MeshIter__loop_of_loop *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->radial_next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  /* NULL on finish */
  return l_curr;
}

/** FACE OF EDGE CALLBACKS **/

void meshiter__face_of_edge_begin(struct BMIter__face_of_edge *iter)
{
  iter->l_first = iter->l_next = iter->edata->l;
}

void *meshiter__face_of_edge_step(struct MeshIter__face_of_edge *iter)
{
  MeshLoop *current = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->radial_next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  return current ? current->f : NULL;
}

/** VERTS OF EDGE CALLBACKS */

void meshiter__vert_of_edge_begin(struct BMIter__vert_of_edge *iter)
{
  ((MeshIter *)iter)->count = 0;
}

void *bmiter__vert_of_edge_step(struct BMIter__vert_of_edge *iter)
{
  switch (((BMIter *)iter)->count++) {
    case 0:
      return iter->edata->v1;
    case 1:
      return iter->edata->v2;
    default:
      return NULL;
  }
}

/** VERT OF FACE CALLBACKS */

void meshiter__vert_of_face_begin(struct MeshIter__vert_of_face *iter)
{
  iter->l_first = iter->l_next = MESH_FACE_FIRST_LOOP(iter->pdata);
}

void *meshiter__vert_of_face_step(struct MeshIter__vert_of_face *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  return l_curr ? l_curr->v : NULL;
}

/** EDGE OF FACE CALLBACKS */

void meshiter__edge_of_face_begin(struct MeshIter__edge_of_face *iter)
{
  iter->l_first = iter->l_next = MESH_FACE_FIRST_LOOP(iter->pdata);
}

void *meshiter__edge_of_face_step(struct MeshIter__edge_of_face *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  return l_curr ? l_curr->e : NULL;
}

/** LOOP OF FACE CALLBACKS **/

void meshiter__loop_of_face_begin(struct BMIter__loop_of_face *iter)
{
  iter->l_first = iter->l_next = BM_FACE_FIRST_LOOP(iter->pdata);
}

void *meshiter__loop_of_face_step(struct BMIter__loop_of_face *iter)
{
  MeshLoop *l_curr = iter->l_next;

  if (iter->l_next) {
    iter->l_next = iter->l_next->next;
    if (iter->l_next == iter->l_first) {
      iter->l_next = NULL;
    }
  }

  return l_curr;
}
