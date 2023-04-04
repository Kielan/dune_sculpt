#include <string.h>

#include "BLI_utildefines.h"

#include "BKE_customdata.h"

#include "bmesh.h"
#include "intern/bmesh_walkers_private.h"

/* Pop into stack memory (common operation). */
#define BMW_state_remove_r(walker, owalk) \
  { \
    memcpy(owalk, BMW_current_state(walker), sizeof(*(owalk))); \
    BMW_state_remove(walker); \
  } \
  (void)0

/* -------------------------------------------------------------------- */
/** \name Mask Flag Checks
 * \{ */

static bool bmw_mask_check_vert(BMWalker *walker, BMVert *v)
{
  if ((walker->flag & BMW_FLAG_TEST_HIDDEN) && BM_elem_flag_test(v, BM_ELEM_HIDDEN)) {
    return false;
  }
  if (walker->mask_vert && !BMO_vert_flag_test(walker->bm, v, walker->mask_vert)) {
    return false;
  }
  return true;
}

static bool bmw_mask_check_edge(BMWalker *walker, BMEdge *e)
{
  if ((walker->flag & BMW_FLAG_TEST_HIDDEN) && BM_elem_flag_test(e, BM_ELEM_HIDDEN)) {
    return false;
  }
  if (walker->mask_edge && !BMO_edge_flag_test(walker->bm, e, walker->mask_edge)) {
    return false;
  }
  return true;
}

static bool bmw_mask_check_face(BMWalker *walker, BMFace *f)
{
  if ((walker->flag & BMW_FLAG_TEST_HIDDEN) && BM_elem_flag_test(f, BM_ELEM_HIDDEN)) {
    return false;
  }
  if (walker->mask_face && !BMO_face_flag_test(walker->bm, f, walker->mask_face)) {
    return false;
  }
  return true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name BMesh Queries (modified to check walker flags)
 * \{ */

/**
 * Check for a wire edge, taking ignoring hidden.
 */
static bool bmw_edge_is_wire(const BMWalker *walker, const BMEdge *e)
{
  if (walker->flag & BMW_FLAG_TEST_HIDDEN) {
    /* Check if this is a wire edge, ignoring hidden faces. */
    if (BM_edge_is_wire(e)) {
      return true;
    }
    return BM_edge_is_all_face_flag_test(e, BM_ELEM_HIDDEN, false);
  }
  return BM_edge_is_wire(e);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shell Walker
 *
 * Starts at a vertex on the mesh and walks over the 'shell' it belongs
 * to via visiting connected edges.
 *
 * takes an edge or vertex as an argument, and spits out edges,
 * restrict flag acts on the edges as well.
 *
 * \todo Add restriction flag/callback for wire edges.
 * \{ */

static void bmw_VertShellWalker_visitEdge(BMWalker *walker, BMEdge *e)
{
  BMwShellWalker *shellWalk = NULL;

  if (BLI_gset_haskey(walker->visit_set, e)) {
    return;
  }

  if (!bmw_mask_check_edge(walker, e)) {
    return;
  }

  shellWalk = BMW_state_add(walker);
  shellWalk->curedge = e;
  BLI_gset_insert(walker->visit_set, e);
}

static void bmw_VertShellWalker_begin(BMWalker *walker, void *data)
{
  BMIter eiter;
  BMHeader *h = data;
  BMEdge *e;
  BMVert *v;

  if (UNLIKELY(h == NULL)) {
    return;
  }

  switch (h->htype) {
    case BM_VERT: {
      /* Starting the walk at a vert, add all the edges to the work-list. */
      v = (BMVert *)h;
      BM_ITER_ELEM (e, &eiter, v, BM_EDGES_OF_VERT) {
        bmw_VertShellWalker_visitEdge(walker, e);
      }
      break;
    }

    case BM_EDGE: {
      /* Starting the walk at an edge, add the single edge to the work-list. */
      e = (BMEdge *)h;
      bmw_VertShellWalker_visitEdge(walker, e);
      break;
    }
    default:
      BLI_assert(0);
  }
}

static void *bmw_VertShellWalker_yield(BMWalker *walker)
{
  BMwShellWalker *shellWalk = BMW_current_state(walker);
  return shellWalk->curedge;
}

static void *bmw_VertShellWalker_step(BMWalker *walker)
{
  BMwShellWalker *swalk, owalk;
  BMEdge *e, *e2;
  BMVert *v;
  BMIter iter;
  int i;

  BMW_state_remove_r(walker, &owalk);
  swalk = &owalk;

  e = swalk->curedge;

  for (i = 0; i < 2; i++) {
    v = i ? e->v2 : e->v1;
    BM_ITER_ELEM (e2, &iter, v, BM_EDGES_OF_VERT) {
      bmw_VertShellWalker_visitEdge(walker, e2);
    }
  }

  return e;
}

#if 0
static void *bmw_VertShellWalker_step(BMWalker *walker)
{
  BMEdge *curedge, *next = NULL;
  BMVert *v_old = NULL;
  bool restrictpass = true;
  BMwShellWalker shellWalk = *((BMwShellWalker *)BMW_current_state(walker));

  if (!BLI_gset_haskey(walker->visit_set, shellWalk.base)) {
    BLI_gset_insert(walker->visit_set, shellWalk.base);
  }

  BMW_state_remove(walker);

  /* Find the next edge whose other vertex has not been visited. */
  curedge = shellWalk.curedge;
  do {
    if (!BLI_gset_haskey(walker->visit_set, curedge)) {
      if (!walker->visibility_flag ||
          (walker->visibility_flag &&
           BMO_edge_flag_test(walker->bm, curedge, walker->visibility_flag))) {
        BMwShellWalker *newstate;

        v_old = BM_edge_other_vert(curedge, shellWalk.base);

        /* Push a new state onto the stack. */
        newState = BMW_state_add(walker);
        BLI_gset_insert(walker->visit_set, curedge);

        /* Populate the new state. */

        newState->base = v_old;
        newState->curedge = curedge;
      }
    }
  } while ((curedge = bmesh_disk_edge_next(curedge, shellWalk.base)) != shellWalk.curedge);

  return shellWalk.curedge;
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name LoopShell Walker
 *
 * Starts at any element on the mesh and walks over the 'shell' it belongs
 * to via visiting connected loops.
 *
 * \note this is mainly useful to loop over a shell delimited by edges.
 * \{ */

static void bmw_LoopShellWalker_visitLoop(BMWalker *walker, BMLoop *l)
{
  BMwLoopShellWalker *shellWalk = NULL;

  if (BLI_gset_haskey(walker->visit_set, l)) {
    return;
  }

  if (!bmw_mask_check_face(walker, l->f)) {
    return;
  }

  shellWalk = BMW_state_add(walker);
  shellWalk->curloop = l;
  BLI_gset_insert(walker->visit_set, l);
}

static void bmw_LoopShellWalker_begin(BMWalker *walker, void *data)
{
  BMIter iter;
  BMHeader *h = data;

  if (UNLIKELY(h == NULL)) {
    return;
  }

  switch (h->htype) {
    case BM_LOOP: {
      /* Starting the walk at a vert, add all the edges to the work-list. */
      BMLoop *l = (BMLoop *)h;
      bmw_LoopShellWalker_visitLoop(walker, l);
      break;
    }

    case BM_VERT: {
      BMVert *v = (BMVert *)h;
      BMLoop *l;
      BM_ITER_ELEM (l, &iter, v, BM_LOOPS_OF_VERT) {
        bmw_LoopShellWalker_visitLoop(walker, l);
      }
      break;
    }
    case BM_EDGE: {
      BMEdge *e = (BMEdge *)h;
      BMLoop *l;
      BM_ITER_ELEM (l, &iter, e, BM_LOOPS_OF_EDGE) {
        bmw_LoopShellWalker_visitLoop(walker, l);
      }
      break;
    }
    case BM_FACE: {
      BMFace *f = (BMFace *)h;
      BMLoop *l = BM_FACE_FIRST_LOOP(f);
      /* Walker will handle other loops within the face. */
      bmw_LoopShellWalker_visitLoop(walker, l);
      break;
    }
    default:
      BLI_assert(0);
  }
}

static void *bmw_LoopShellWalker_yield(BMWalker *walker)
{
  BMwLoopShellWalker *shellWalk = BMW_current_state(walker);
  return shellWalk->curloop;
}

static void bmw_LoopShellWalker_step_impl(BMWalker *walker, BMLoop *l)
{
  BMEdge *e_edj_pair[2];
  int i;

  /* Seems paranoid, but one caller also walks edges. */
  BLI_assert(l->head.htype == BM_LOOP);

  bmw_LoopShellWalker_visitLoop(walker, l->next);
  bmw_LoopShellWalker_visitLoop(walker, l->prev);

  e_edj_pair[0] = l->e;
  e_edj_pair[1] = l->prev->e;

  for (i = 0; i < 2; i++) {
    BMEdge *e = e_edj_pair[i];
    if (bmw_mask_check_edge(walker, e)) {
      BMLoop *l_iter, *l_first;

      l_iter = l_first = e->l;
      do {
        BMLoop *l_radial = (l_iter->v == l->v) ? l_iter : l_iter->next;
        BLI_assert(l_radial->v == l->v);
        if (l != l_radial) {
          bmw_LoopShellWalker_visitLoop(walker, l_radial);
        }
      } while ((l_iter = l_iter->radial_next) != l_first);
    }
  }
}

static void *bmw_LoopShellWalker_step(BMWalker *walker)
{
  BMwLoopShellWalker *swalk, owalk;
  BMLoop *l;

  BMW_state_remove_r(walker, &owalk);
  swalk = &owalk;

  l = swalk->curloop;
  bmw_LoopShellWalker_step_impl(walker, l);

  return l;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name LoopShell & 'Wire' Walker
 *
 * Piggyback on top of #BMwLoopShellWalker, but also walk over wire edges
 * This isn't elegant but users expect it when selecting linked,
 * so we can support delimiters _and_ walking over wire edges.
 *
 * Details:
 * - can yield edges (as well as loops)
 * - only step over wire edges.
 * - verts and edges are stored in `visit_set_alt`.
 * \{ */

static void bmw_LoopShellWalker_visitEdgeWire(BMWalker *walker, BMEdge *e)
{
  BMwLoopShellWireWalker *shellWalk = NULL;

  BLI_assert(bmw_edge_is_wire(walker, e));

  if (BLI_gset_haskey(walker->visit_set_alt, e)) {
    return;
  }

  if (!bmw_mask_check_edge(walker, e)) {
    return;
  }

  shellWalk = BMW_state_add(walker);
  shellWalk->curelem = (BMElem *)e;
  BLI_gset_insert(walker->visit_set_alt, e);
}

static void bmw_LoopShellWireWalker_visitVert(BMWalker *walker, BMVert *v, const BMEdge *e_from)
{
  BMEdge *e;

  BLI_assert(v->head.htype == BM_VERT);

  if (BLI_gset_haskey(walker->visit_set_alt, v)) {
    return;
  }

  if (!bmw_mask_check_vert(walker, v)) {
    return;
  }

  e = v->e;
  do {
    if (bmw_edge_is_wire(walker, e) && (e != e_from)) {
      BMVert *v_other;
      BMIter iter;
      BMLoop *l;

      bmw_LoopShellWalker_visitEdgeWire(walker, e);

      /* Check if we step onto a non-wire vertex. */
      v_other = BM_edge_other_vert(e, v);
      BM_ITER_ELEM (l, &iter, v_other, BM_LOOPS_OF_VERT) {

        bmw_LoopShellWalker_visitLoop(walker, l);
      }
    }
  } while ((e = BM_DISK_EDGE_NEXT(e, v)) != v->e);

  BLI_gset_insert(walker->visit_set_alt, v);
}

static void bmw_LoopShellWireWalker_begin(BMWalker *walker, void *data)
{
  BMHeader *h = data;

  if (UNLIKELY(h == NULL)) {
    return;
  }

  bmw_LoopShellWalker_begin(walker, data);

  switch (h->htype) {
    case BM_LOOP: {
      BMLoop *l = (BMLoop *)h;
      bmw_LoopShellWireWalker_visitVert(walker, l->v, NULL);
      break;
    }

    case BM_VERT: {
      BMVert *v = (BMVert *)h;
      if (v->e) {
        bmw_LoopShellWireWalker_visitVert(walker, v, NULL);
      }
      break;
    }
    case BM_EDGE: {
      BMEdge *e = (BMEdge *)h;
      if (bmw_mask_check_edge(walker, e)) {
        bmw_LoopShellWireWalker_visitVert(walker, e->v1, NULL);
        bmw_LoopShellWireWalker_visitVert(walker, e->v2, NULL);
      }
      else if (e->l) {
        BMLoop *l_iter, *l_first;

        l_iter = l_first = e->l;
        do {
          bmw_LoopShellWalker_visitLoop(walker, l_iter);
          bmw_LoopShellWalker_visitLoop(walker, l_iter->next);
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
      break;
    }
    case BM_FACE: {
      /* Wire verts will be walked over. */
      break;
    }
    default:
      BLI_assert(0);
  }
}

static void *bmw_LoopShellWireWalker_yield(BMWalker *walker)
{
  BMwLoopShellWireWalker *shellWalk = BMW_current_state(walker);
  return shellWalk->curelem;
}

static void *bmw_LoopShellWireWalker_step(BMWalker *walker)
{
  BMwLoopShellWireWalker *swalk, owalk;

  BMW_state_remove_r(walker, &owalk);
  swalk = &owalk;

  if (swalk->curelem->head.htype == BM_LOOP) {
    BMLoop *l = (BMLoop *)swalk->curelem;

    bmw_LoopShellWalker_step_impl(walker, l);

    bmw_LoopShellWireWalker_visitVert(walker, l->v, NULL);

    return l;
  }

  BMEdge *e = (BMEdge *)swalk->curelem;

  BLI_assert(e->head.htype == BM_EDGE);

  bmw_LoopShellWireWalker_visitVert(walker, e->v1, e);
  bmw_LoopShellWireWalker_visitVert(walker, e->v2, e);

  return e;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name FaceShell Walker
 *
 * Starts at an edge on the mesh and walks over the 'shell' it belongs
 * to via visiting connected faces.
 * \{ */

static void bmw_FaceShellWalker_visitEdge(BMWalker *walker, BMEdge *e)
{
  BMwShellWalker *shellWalk = NULL;

  if (BLI_gset_haskey(walker->visit_set, e)) {
    return;
  }

  if (!bmw_mask_check_edge(walker, e)) {
    return;
  }

  shellWalk = BMW_state_add(walker);
  shellWalk->curedge = e;
  BLI_gset_insert(walker->visit_set, e);
}

static void bmw_FaceShellWalker_begin(BMWalker *walker, void *data)
{
  BMEdge *e = data;
  bmw_FaceShellWalker_visitEdge(walker, e);
}

static void *bmw_FaceShellWalker_yield(BMWalker *walker)
{
  BMwShellWalker *shellWalk = BMW_current_state(walker);
  return shellWalk->curedge;
}

static void *bmw_FaceShellWalker_step(BMWalker *walker)
{
  BMwShellWalker *swalk, owalk;
  BMEdge *e, *e2;
  BMIter iter;

  BMW_state_remove_r(walker, &owalk);
  swalk = &owalk;

  e = swalk->curedge;

  if (e->l) {
    BMLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      BM_ITER_ELEM (e2, &iter, l_iter->f, BM_EDGES_OF_FACE) {
        if (e2 != e) {
          bmw_FaceShellWalker_visitEdge(walker, e2);
        }
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return e;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Connected Vertex Walker
 *
 * Similar to shell walker, but visits vertices instead of edges.
 *
 * Walk from a vertex to all connected vertices.
 * \{ */

static void bmw_ConnectedVertexWalker_visitVertex(BMWalker *walker, BMVert *v)
{
  BMwConnectedVertexWalker *vwalk;

  if (BLI_gset_haskey(walker->visit_set, v)) {
    /* Already visited. */
    return;
  }

  if (!bmw_mask_check_vert(walker, v)) {
    /* Not flagged for walk. */
    return;
  }

  vwalk = BMW_state_add(walker);
  vwalk->curvert = v;
  BLI_gset_insert(walker->visit_set, v);
}

static void bmw_ConnectedVertexWalker_begin(BMWalker *walker, void *data)
{
  BMVert *v = data;
  bmw_ConnectedVertexWalker_visitVertex(walker, v);
}

static void *bmw_ConnectedVertexWalker_yield(BMWalker *walker)
{
  BMwConnectedVertexWalker *vwalk = BMW_current_state(walker);
  return vwalk->curvert;
}

static void *bmw_ConnectedVertexWalker_step(BMWalker *walker)
{
  BMwConnectedVertexWalker *vwalk, owalk;
  BMVert *v, *v2;
  BMEdge *e;
  BMIter iter;

  BMW_state_remove_r(walker, &owalk);
  vwalk = &owalk;

  v = vwalk->curvert;

  BM_ITER_ELEM (e, &iter, v, BM_EDGES_OF_VERT) {
    v2 = BM_edge_other_vert(e, v);
    if (!BLI_gset_haskey(walker->visit_set, v2)) {
      bmw_ConnectedVertexWalker_visitVertex(walker, v2);
    }
  }

  return v;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Island Boundary Walker
 *
 * Starts at a edge on the mesh and walks over the boundary of an island it belongs to.
 *
 * \note that this doesn't work on non-manifold geometry.
 * it might be better to rewrite this to extract
 * boundary info from the island walker, rather than directly walking
 * over the boundary.  raises an error if it encounters non-manifold geometry.
 *
 * \todo Add restriction flag/callback for wire edges.
 * \{ */

static void bmw_IslandboundWalker_begin(BMWalker *walker, void *data)
{
  BMLoop *l = data;
  BMwIslandboundWalker *iwalk = NULL;

  iwalk = BMW_state_add(walker);

  iwalk->base = iwalk->curloop = l;
  iwalk->lastv = l->v;

  BLI_gset_insert(walker->visit_set, data);
}

static void *bmw_IslandboundWalker_yield(BMWalker *walker)
{
  BMwIslandboundWalker *iwalk = BMW_current_state(walker);

  return iwalk->curloop;
}

static void *bmw_IslandboundWalker_step(BMWalker *walker)
{
  BMwIslandboundWalker *iwalk, owalk;
  BMVert *v;
  BMEdge *e;
  BMFace *f;
  BMLoop *l;

  memcpy(&owalk, BMW_current_state(walker), sizeof(owalk));
  /* Normally we'd remove here, but delay until after error checking. */
  iwalk = &owalk;

  l = iwalk->curloop;
  e = l->e;

  v = BM_edge_other_vert(e, iwalk->lastv);

  /* Pop off current state. */
  BMW_state_remove(walker);

  f = l->f;

  while (1) {
    l = BM_loop_other_edge_loop(l, v);
    if (BM_loop_is_manifold(l)) {
      l = l->radial_next;
      f = l->f;
      e = l->e;

      if (!bmw_mask_check_face(walker, f)) {
        l = l->radial_next;
        break;
      }
    }
    else {
      /* Treat non-manifold edges as boundaries. */
      f = l->f;
      e = l->e;
      break;
    }
  }

  if (l == owalk.curloop) {
    return NULL;
  }
  if (BLI_gset_haskey(walker->visit_set, l)) {
    return owalk.curloop;
  }

  BLI_gset_insert(walker->visit_set, l);
  iwalk = BMW_state_add(walker);
  iwalk->base = owalk.base;

#if 0
  if (!BMO_face_flag_test(walker->bm, l->f, walker->visibility_flag)) {
    iwalk->curloop = l->radial_next;
  }
  else {
    iwalk->curloop = l;
  }
#else
  iwalk->curloop = l;
#endif
  iwalk->lastv = v;

  return owalk.curloop;
}

/* -------------------------------------------------------------------- */
/** \name Island Walker
 *
 * Starts at a tool flagged-face and walks over the face region
 *
 * \todo Add restriction flag/callback for wire edges.
 * \{ */

static void bmw_IslandWalker_begin(BMWalker *walker, void *data)
{
  BMwIslandWalker *iwalk = NULL;

  if (!bmw_mask_check_face(walker, data)) {
    return;
  }

  iwalk = BMW_state_add(walker);
  BLI_gset_insert(walker->visit_set, data);

  iwalk->cur = data;
}

static void *bmw_IslandWalker_yield(BMWalker *walker)
{
  BMwIslandWalker *iwalk = BMW_current_state(walker);

  return iwalk->cur;
}

static void *bmw_IslandWalker_step_ex(BMWalker *walker, bool only_manifold)
{
  BMwIslandWalker *iwalk, owalk;
  BMLoop *l_iter, *l_first;

  BMW_state_remove_r(walker, &owalk);
  iwalk = &owalk;

  l_iter = l_first = BM_FACE_FIRST_LOOP(iwalk->cur);
  do {
    /* Could skip loop here too, but don't add unless we need it. */
    if (!bmw_mask_check_edge(walker, l_iter->e)) {
      continue;
    }

    BMLoop *l_radial_iter;

    if (only_manifold && (l_iter->radial_next != l_iter)) {
      int face_count = 1;
      /* Check other faces (not this one), ensure only one other Can be walked onto. */
      l_radial_iter = l_iter->radial_next;
      do {
        if (bmw_mask_check_face(walker, l_radial_iter->f)) {
          face_count++;
          if (face_count == 3) {
            break;
          }
        }
      } while ((l_radial_iter = l_radial_iter->radial_next) != l_iter);

      if (face_count != 2) {
        continue;
      }
    }

    l_radial_iter = l_iter;
    while ((l_radial_iter = l_radial_iter->radial_next) != l_iter) {
      BMFace *f = l_radial_iter->f;

      if (!bmw_mask_check_face(walker, f)) {
        continue;
      }

      /* Saves checking #BLI_gset_haskey below (manifold edges there's a 50% chance). */
      if (f == iwalk->cur) {
        continue;
      }

      if (BLI_gset_haskey(walker->visit_set, f)) {
        continue;
      }

      iwalk = BMW_state_add(walker);
      iwalk->cur = f;
      BLI_gset_insert(walker->visit_set, f);
      break;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return owalk.cur;
}

static void *bmw_IslandWalker_step(BMWalker *walker)
{
  return bmw_IslandWalker_step_ex(walker, false);
}

/**
 * Ignore edges that don't have 2x usable faces.
 */
static void *bmw_IslandManifoldWalker_step(BMWalker *walker)
{
  return bmw_IslandWalker_step_ex(walker, true);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edge Loop Walker
 *
 * Starts at a tool-flagged edge and walks over the edge loop
 * \{ */

/* utility function to see if an edge is a part of an ngon boundary */
static bool bm_edge_is_single(BMEdge *e)
{
  return (BM_edge_is_boundary(e) && (e->l->f->len > 4) &&
          (BM_edge_is_boundary(e->l->next->e) || BM_edge_is_boundary(e->l->prev->e)));
}

static void bmw_EdgeLoopWalker_begin(BMWalker *walker, void *data)
{
  BMwEdgeLoopWalker *lwalk = NULL, owalk, *owalk_pt;
  BMEdge *e = data;
  BMVert *v;
  const int vert_edge_count[2] = {
      BM_vert_edge_count_nonwire(e->v1),
      BM_vert_edge_count_nonwire(e->v2),
  };
  const int vert_face_count[2] = {
      BM_vert_face_count(e->v1),
      BM_vert_face_count(e->v2),
  };

  v = e->v1;

  lwalk = BMW_state_add(walker);
  BLI_gset_insert(walker->visit_set, e);

  lwalk->cur = lwalk->start = e;
  lwalk->lastv = lwalk->startv = v;
  lwalk->is_boundary = BM_edge_is_boundary(e);
  lwalk->is_single = (lwalk->is_boundary && bm_edge_is_single(e));

  /**
   * Detect an NGon (face-hub)
   * =========================
   *
   * The face-hub - #BMwEdgeLoopWalker.f_hub - is set when there is an ngon
   * on one side of the edge and a series of faces on the other,
   * loop around the ngon for as long as it's connected to faces which would form an edge loop
   * in the absence of the ngon (used as the hub).
   *
   * This isn't simply ignoring the ngon though, as the edges looped over must all be
   * connected to the hub.
   *
   * NGon in Grid Example
   * --------------------
   * \code{.txt}
   * +-----+-----+-----+-----+-----+
   * |     |     |     |     |     |
   * +-----va=ea=+==eb=+==ec=vb----+
   * |     |                 |     |
   * +-----+                 +-----+
   * |     |      f_hub      |     |
   * +-----+                 +-----+
   * |     |                 |     |
   * +-----+-----+-----+-----+-----+
   * |     |     |     |     |     |
   * +-----+-----+-----+-----+-----+
   * \endcode
   *
   * In the example above, starting from edges marked `ea/eb/ec`,
   * the will detect `f_hub` and walk along the edge loop between `va -> vb`.
   * The same is true for any of the un-marked sides of the ngon,
   * walking stops for vertices with >= 3 connected faces (in this case they look like corners).
   *
   * Mixed Triangle-Fan & Quad Example
   * ---------------------------------
   * \code{.txt}
   * +-----------------------------------------------+
   * |              f_hub                            |
   * va-ea-vb=eb=+==ec=+=ed==+=ee=vc=ef=vd----------ve
   * |     |\    |     |     |    /     / \          |
   * |     | \    \    |    /    /     /   \         |
   * |     |  \   |    |    |   /     /     \        |
   * |     |   \   \   |   /   /     /       \       |
   * |     |    \  |   |   |  /     /         \      |
   * |     |     \  \  |  /  /     /           \     |
   * |     |      \ |  |  | /     /             \    |
   * |     |       \ \ | / /     /               \   |
   * |     |        \| | |/     /                 \  |
   * |     |         \\|//     /                   \ |
   * |     |          \|/     /                     \|
   * +-----+-----------+-----+-----------------------+
   * \endcode
   *
   * In the example above, starting from edges marked `eb/eb/ed/ed/ef`,
   * the will detect `f_hub` and walk along the edge loop between `vb -> vd`.
   *
   * Notice `vb` and `vd` delimit the loop, since the faces connected to `vb`
   * excluding `f_hub` don't share an edge, which isn't walked over in the case
   * of boundaries either.
   *
   * Notice `vc` doesn't delimit stepping from `ee` onto `ef` as the stepping method used
   * doesn't differentiate between the number of sides of faces opposite `f_hub`,
   * only that each of the connected faces share an edge.
   */
  if ((lwalk->is_boundary == false) &&
      /* Without checking the face count, the 3 edges could be this edge
       * plus two boundary edges (which would not be stepped over), see T84906. */
      ((vert_edge_count[0] == 3 && vert_face_count[0] == 3) ||
       (vert_edge_count[1] == 3 && vert_face_count[1] == 3))) {
    BMIter iter;
    BMFace *f_iter;
    BMFace *f_best = NULL;

    BM_ITER_ELEM (f_iter, &iter, e, BM_FACES_OF_EDGE) {
      if (f_best == NULL || f_best->len < f_iter->len) {
        f_best = f_iter;
      }
    }

    if (f_best) {
      /* Only use hub selection for 5+ sides else this could
       * conflict with normal edge loop selection. */
      lwalk->f_hub = f_best->len > 4 ? f_best : NULL;
    }
    else {
      /* Edge doesn't have any faces connected to it. */
      lwalk->f_hub = NULL;
    }
  }
  else {
    lwalk->f_hub = NULL;
  }

  /* Rewind. */
  while ((owalk_pt = BMW_current_state(walker))) {
    owalk = *((BMwEdgeLoopWalker *)owalk_pt);
    BMW_walk(walker);
  }

  lwalk = BMW_state_add(walker);
  *lwalk = owalk;

  lwalk->lastv = lwalk->startv = BM_edge_other_vert(owalk.cur, lwalk->lastv);

  BLI_gset_clear(walker->visit_set, NULL);
  BLI_gset_insert(walker->visit_set, owalk.cur);
}
