#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CLG_log.h"

#include "BLI_bitmap.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BLI_sys_types.h"

#include "BLI_edgehash.h"
#include "BLI_math_base.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_mesh.h"

#include "DEG_depsgraph.h"

#include "MEM_guardedalloc.h"

/* loop v/e are unsigned, so using max uint_32 value as invalid marker... */
#define INVALID_LOOP_EDGE_MARKER 4294967295u

static CLG_LogRef LOG = {"bke.mesh"};

/* -------------------------------------------------------------------- */
/** \name Internal functions
 * \{ */

union EdgeUUID {
  uint32_t verts[2];
  int64_t edval;
};

struct SortFace {
  EdgeUUID es[4];
  uint index;
};

/* Used to detect polys (faces) using exactly the same vertices. */
/* Used to detect loops used by no (disjoint) or more than one (intersect) polys. */
struct SortPoly {
  int *verts;
  int numverts;
  int loopstart;
  uint index;
  bool invalid; /* Poly index. */
};

static void edge_store_assign(uint32_t verts[2], const uint32_t v1, const uint32_t v2)
{
  if (v1 < v2) {
    verts[0] = v1;
    verts[1] = v2;
  }
  else {
    verts[0] = v2;
    verts[1] = v1;
  }
}

static void edge_store_from_mface_quad(EdgeUUID es[4], MFace *mf)
{
  edge_store_assign(es[0].verts, mf->v1, mf->v2);
  edge_store_assign(es[1].verts, mf->v2, mf->v3);
  edge_store_assign(es[2].verts, mf->v3, mf->v4);
  edge_store_assign(es[3].verts, mf->v4, mf->v1);
}

static void edge_store_from_mface_tri(EdgeUUID es[4], MFace *mf)
{
  edge_store_assign(es[0].verts, mf->v1, mf->v2);
  edge_store_assign(es[1].verts, mf->v2, mf->v3);
  edge_store_assign(es[2].verts, mf->v3, mf->v1);
  es[3].verts[0] = es[3].verts[1] = UINT_MAX;
}

static int int64_cmp(const void *v1, const void *v2)
{
  const int64_t x1 = *(const int64_t *)v1;
  const int64_t x2 = *(const int64_t *)v2;

  if (x1 > x2) {
    return 1;
  }
  if (x1 < x2) {
    return -1;
  }

  return 0;
}

static int search_face_cmp(const void *v1, const void *v2)
{
  const SortFace *sfa = static_cast<const SortFace *>(v1);
  const SortFace *sfb = static_cast<const SortFace *>(v2);

  if (sfa->es[0].edval > sfb->es[0].edval) {
    return 1;
  }
  if (sfa->es[0].edval < sfb->es[0].edval) {
    return -1;
  }

  if (sfa->es[1].edval > sfb->es[1].edval) {
    return 1;
  }
  if (sfa->es[1].edval < sfb->es[1].edval) {
    return -1;
  }

  if (sfa->es[2].edval > sfb->es[2].edval) {
    return 1;
  }
  if (sfa->es[2].edval < sfb->es[2].edval) {
    return -1;
  }

  if (sfa->es[3].edval > sfb->es[3].edval) {
    return 1;
  }
  if (sfa->es[3].edval < sfb->es[3].edval) {
    return -1;
  }

  return 0;
}

/* TODO: check there is not some standard define of this somewhere! */
static int int_cmp(const void *v1, const void *v2)
{
  return *(int *)v1 > *(int *)v2 ? 1 : *(int *)v1 < *(int *)v2 ? -1 : 0;
}

static int search_poly_cmp(const void *v1, const void *v2)
{
  const SortPoly *sp1 = static_cast<const SortPoly *>(v1);
  const SortPoly *sp2 = static_cast<const SortPoly *>(v2);

  /* Reject all invalid polys at end of list! */
  if (sp1->invalid || sp2->invalid) {
    return sp1->invalid ? (sp2->invalid ? 0 : 1) : -1;
  }
  /* Else, sort on first non-equal verts (remember verts of valid polys are sorted). */
  const int max_idx = sp1->numverts > sp2->numverts ? sp2->numverts : sp1->numverts;
  for (int idx = 0; idx < max_idx; idx++) {
    const int v1_i = sp1->verts[idx];
    const int v2_i = sp2->verts[idx];
    if (v1_i != v2_i) {
      return (v1_i > v2_i) ? 1 : -1;
    }
  }
  return sp1->numverts > sp2->numverts ? 1 : sp1->numverts < sp2->numverts ? -1 : 0;
}

static int search_polyloop_cmp(const void *v1, const void *v2)
{
  const SortPoly *sp1 = static_cast<const SortPoly *>(v1);
  const SortPoly *sp2 = static_cast<const SortPoly *>(v2);

  /* Reject all invalid polys at end of list! */
  if (sp1->invalid || sp2->invalid) {
    return sp1->invalid && sp2->invalid ? 0 : sp1->invalid ? 1 : -1;
  }
  /* Else, sort on loopstart. */
  return sp1->loopstart > sp2->loopstart ? 1 : sp1->loopstart < sp2->loopstart ? -1 : 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Validation
 * \{ */

#define PRINT_MSG(...) \
  if (do_verbose) { \
    CLOG_INFO(&LOG, 1, __VA_ARGS__); \
  } \
  ((void)0)

#define PRINT_ERR(...) \
  do { \
    is_valid = false; \
    if (do_verbose) { \
      CLOG_ERROR(&LOG, __VA_ARGS__); \
    } \
  } while (0)

/* NOLINTNEXTLINE: readability-function-size */
bool BKE_mesh_validate_arrays(Mesh *mesh,
                              MVert *mverts,
                              uint totvert,
                              MEdge *medges,
                              uint totedge,
                              MFace *mfaces,
                              uint totface,
                              MLoop *mloops,
                              uint totloop,
                              MPoly *mpolys,
                              uint totpoly,
                              MDeformVert *dverts, /* assume totvert length */
                              const bool do_verbose,
                              const bool do_fixes,
                              bool *r_changed)
{
#define REMOVE_EDGE_TAG(_me) \
  { \
    _me->v2 = _me->v1; \
    free_flag.edges = do_fixes; \
  } \
  (void)0
#define IS_REMOVED_EDGE(_me) (_me->v2 == _me->v1)

#define REMOVE_LOOP_TAG(_ml) \
  { \
    _ml->e = INVALID_LOOP_EDGE_MARKER; \
    free_flag.polyloops = do_fixes; \
  } \
  (void)0
#define REMOVE_POLY_TAG(_mp) \
  { \
    _mp->totloop *= -1; \
    free_flag.polyloops = do_fixes; \
  } \
  (void)0

  MVert *mv = mverts;
  MEdge *me;
  MLoop *ml;
  MPoly *mp;
  uint i, j;
  int *v;

  bool is_valid = true;

  union {
    struct {
      int verts : 1;
      int verts_weight : 1;
      int loops_edge : 1;
    };
    int as_flag;
  } fix_flag;

  union {
    struct {
      int edges : 1;
      int faces : 1;
      /* This regroups loops and polys! */
      int polyloops : 1;
      int mselect : 1;
    };
    int as_flag;
  } free_flag;

  union {
    struct {
      int edges : 1;
    };
    int as_flag;
  } recalc_flag;

  EdgeHash *edge_hash = BLI_edgehash_new_ex(__func__, totedge);

  BLI_assert(!(do_fixes && mesh == nullptr));

  fix_flag.as_flag = 0;
  free_flag.as_flag = 0;
  recalc_flag.as_flag = 0;

  PRINT_MSG("verts(%u), edges(%u), loops(%u), polygons(%u)", totvert, totedge, totloop, totpoly);

  if (totedge == 0 && totpoly != 0) {
    PRINT_ERR("\tLogical error, %u polygons and 0 edges", totpoly);
    recalc_flag.edges = do_fixes;
  }

  const float(*vert_normals)[3] = nullptr;
  BKE_mesh_assert_normals_dirty_or_calculated(mesh);
  if (!BKE_mesh_vertex_normals_are_dirty(mesh)) {
    vert_normals = BKE_mesh_vertex_normals_ensure(mesh);
  }

  for (i = 0; i < totvert; i++, mv++) {
    bool fix_normal = true;

    for (j = 0; j < 3; j++) {
      if (!isfinite(mv->co[j])) {
        PRINT_ERR("\tVertex %u: has invalid coordinate", i);

        if (do_fixes) {
          zero_v3(mv->co);

          fix_flag.verts = true;
        }
      }

      if (vert_normals && vert_normals[i][j] != 0.0f) {
        fix_normal = false;
        break;
      }
    }

    if (vert_normals && fix_normal) {
      /* If the vertex normal accumulates to zero or isn't part of a face, the location is used.
       * When the location is also zero, a zero normal warning should not be raised.
       * since this is the expected behavior of normal calculation.
       *
       * This avoids false positives but isn't foolproof as it's possible the vertex
       * is part of a polygon that has a normal which this vertex should be using,
       * although it's also possible degenerate/opposite faces accumulate to a zero vector.
       * To detect this a full normal recalculation would be needed, which is out of scope
       * for a basic validity check (see "Vertex Normal" in the doc-string). */
      if (!is_zero_v3(mv->co)) {
        PRINT_ERR("\tVertex %u: has zero normal, assuming Z-up normal", i);
        if (do_fixes) {
          float *normal = (float *)vert_normals[i];
          normal[2] = 1.0f;
          fix_flag.verts = true;
        }
      }
    }
  }

  for (i = 0, me = medges; i < totedge; i++, me++) {
    bool remove = false;

    if (me->v1 == me->v2) {
      PRINT_ERR("\tEdge %u: has matching verts, both %u", i, me->v1);
      remove = do_fixes;
    }
    if (me->v1 >= totvert) {
      PRINT_ERR("\tEdge %u: v1 index out of range, %u", i, me->v1);
      remove = do_fixes;
    }
    if (me->v2 >= totvert) {
      PRINT_ERR("\tEdge %u: v2 index out of range, %u", i, me->v2);
      remove = do_fixes;
    }

    if ((me->v1 != me->v2) && BLI_edgehash_haskey(edge_hash, me->v1, me->v2)) {
      PRINT_ERR("\tEdge %u: is a duplicate of %d",
                i,
                POINTER_AS_INT(BLI_edgehash_lookup(edge_hash, me->v1, me->v2)));
      remove = do_fixes;
    }

    if (remove == false) {
      if (me->v1 != me->v2) {
        BLI_edgehash_insert(edge_hash, me->v1, me->v2, POINTER_FROM_INT(i));
      }
    }
    else {
      REMOVE_EDGE_TAG(me);
    }
  }

  if (mfaces && !mpolys) {
#define REMOVE_FACE_TAG(_mf) \
  { \
    _mf->v3 = 0; \
    free_flag.faces = do_fixes; \
  } \
  (void)0
#define CHECK_FACE_VERT_INDEX(a, b) \
  if (mf->a == mf->b) { \
    PRINT_ERR("    face %u: verts invalid, " STRINGIFY(a) "/" STRINGIFY(b) " both %u", i, mf->a); \
    remove = do_fixes; \
  } \
  (void)0
#define CHECK_FACE_EDGE(a, b) \
  if (!BLI_edgehash_haskey(edge_hash, mf->a, mf->b)) { \
    PRINT_ERR("    face %u: edge " STRINGIFY(a) "/" STRINGIFY(b) " (%u,%u) is missing edge data", \
              i, \
              mf->a, \
              mf->b); \
    recalc_flag.edges = do_fixes; \
  } \
  (void)0

    MFace *mf;
    MFace *mf_prev;

    SortFace *sort_faces = (SortFace *)MEM_callocN(sizeof(SortFace) * totface, "search faces");
    SortFace *sf;
    SortFace *sf_prev;
    uint totsortface = 0;

    PRINT_ERR("No Polys, only tessellated Faces");

    for (i = 0, mf = mfaces, sf = sort_faces; i < totface; i++, mf++) {
      bool remove = false;
      int fidx;
      uint fv[4];

      fidx = mf->v4 ? 3 : 2;
      do {
        fv[fidx] = *(&(mf->v1) + fidx);
        if (fv[fidx] >= totvert) {
          PRINT_ERR("\tFace %u: 'v%d' index out of range, %u", i, fidx + 1, fv[fidx]);
          remove = do_fixes;
        }
      } while (fidx--);

      if (remove == false) {
        if (mf->v4) {
          CHECK_FACE_VERT_INDEX(v1, v2);
          CHECK_FACE_VERT_INDEX(v1, v3);
          CHECK_FACE_VERT_INDEX(v1, v4);

          CHECK_FACE_VERT_INDEX(v2, v3);
          CHECK_FACE_VERT_INDEX(v2, v4);

          CHECK_FACE_VERT_INDEX(v3, v4);
        }
        else {
          CHECK_FACE_VERT_INDEX(v1, v2);
          CHECK_FACE_VERT_INDEX(v1, v3);

          CHECK_FACE_VERT_INDEX(v2, v3);
        }

        if (remove == false) {
          if (totedge) {
            if (mf->v4) {
              CHECK_FACE_EDGE(v1, v2);
              CHECK_FACE_EDGE(v2, v3);
              CHECK_FACE_EDGE(v3, v4);
              CHECK_FACE_EDGE(v4, v1);
            }
            else {
              CHECK_FACE_EDGE(v1, v2);
              CHECK_FACE_EDGE(v2, v3);
              CHECK_FACE_EDGE(v3, v1);
            }
          }

          sf->index = i;

          if (mf->v4) {
            edge_store_from_mface_quad(sf->es, mf);

            qsort(sf->es, 4, sizeof(int64_t), int64_cmp);
          }
          else {
            edge_store_from_mface_tri(sf->es, mf);
            qsort(sf->es, 3, sizeof(int64_t), int64_cmp);
          }

          totsortface++;
          sf++;
        }
      }

      if (remove) {
        REMOVE_FACE_TAG(mf);
      }
    }

    qsort(sort_faces, totsortface, sizeof(SortFace), search_face_cmp);

    sf = sort_faces;
    sf_prev = sf;
    sf++;

    for (i = 1; i < totsortface; i++, sf++) {
      bool remove = false;

      /* on a valid mesh, code below will never run */
      if (memcmp(sf->es, sf_prev->es, sizeof(sf_prev->es)) == 0) {
        mf = mfaces + sf->index;

        if (do_verbose) {
          mf_prev = mfaces + sf_prev->index;

          if (mf->v4) {
            PRINT_ERR("\tFace %u & %u: are duplicates (%u,%u,%u,%u) (%u,%u,%u,%u)",
                      sf->index,
                      sf_prev->index,
                      mf->v1,
                      mf->v2,
                      mf->v3,
                      mf->v4,
                      mf_prev->v1,
                      mf_prev->v2,
                      mf_prev->v3,
                      mf_prev->v4);
          }
          else {
            PRINT_ERR("\tFace %u & %u: are duplicates (%u,%u,%u) (%u,%u,%u)",
                      sf->index,
                      sf_prev->index,
                      mf->v1,
                      mf->v2,
                      mf->v3,
                      mf_prev->v1,
                      mf_prev->v2,
                      mf_prev->v3);
          }
        }

        remove = do_fixes;
      }
      else {
        sf_prev = sf;
      }

      if (remove) {
        REMOVE_FACE_TAG(mf);
      }
    }

    MEM_freeN(sort_faces);

#undef REMOVE_FACE_TAG
#undef CHECK_FACE_VERT_INDEX
#undef CHECK_FACE_EDGE
