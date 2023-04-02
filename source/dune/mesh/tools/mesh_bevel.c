/** \file
 * \ingroup bmesh
 *
 * Main functions for beveling a BMesh (used by the tool and modifier)
 */

#include "MEM_guardedalloc.h"

#include "DNA_curveprofile_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_scene_types.h"

#include "BLI_alloca.h"
#include "BLI_array.h"
#include "BLI_math.h"
#include "BLI_memarena.h"
#include "BLI_utildefines.h"

#include "BKE_curveprofile.h"
#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_mesh.h"

#include "eigen_capi.h"

#include "bmesh.h"
#include "bmesh_bevel.h" /* own include */

#include "./intern/bmesh_private.h"

// #define BEVEL_DEBUG_TIME
#ifdef BEVEL_DEBUG_TIME
#  include "PIL_time.h"
#endif

#define BEVEL_EPSILON_D 1e-6
#define BEVEL_EPSILON 1e-6f
#define BEVEL_EPSILON_SQ 1e-12f
#define BEVEL_EPSILON_BIG 1e-4f
#define BEVEL_EPSILON_BIG_SQ 1e-8f
#define BEVEL_EPSILON_ANG DEG2RADF(2.0f)
#define BEVEL_SMALL_ANG DEG2RADF(10.0f)
/** Difference in dot products that corresponds to 10 degree difference between vectors. */
#define BEVEL_SMALL_ANG_DOT (1.0f - cosf(BEVEL_SMALL_ANG))
/** Difference in dot products that corresponds to 2.0 degree difference between vectors. */
#define BEVEL_EPSILON_ANG_DOT (1.0f - cosf(BEVEL_EPSILON_ANG))
#define BEVEL_MAX_ADJUST_PCT 10.0f
#define BEVEL_MAX_AUTO_ADJUST_PCT 300.0f
#define BEVEL_MATCH_SPEC_WEIGHT 0.2

//#define DEBUG_CUSTOM_PROFILE_CUTOFF
/* Happens far too often, uncomment for development. */
// #define BEVEL_ASSERT_PROJECT

/* for testing */
// #pragma GCC diagnostic error "-Wpadded"

/* Constructed vertex, sometimes later instantiated as BMVert. */
typedef struct NewVert {
  BMVert *v;
  float co[3];
  char _pad[4];
} NewVert;

struct BoundVert;

/* Data for one end of an edge involved in a bevel. */
typedef struct EdgeHalf {
  /** Other EdgeHalves connected to the same BevVert, in CCW order. */
  struct EdgeHalf *next, *prev;
  /** Original mesh edge. */
  BMEdge *e;
  /** Face between this edge and previous, if any. */
  BMFace *fprev;
  /** Face between this edge and next, if any. */
  BMFace *fnext;
  /** Left boundary vert (looking along edge to end). */
  struct BoundVert *leftv;
  /** Right boundary vert, if beveled. */
  struct BoundVert *rightv;
  /** Offset into profile to attach non-beveled edge. */
  int profile_index;
  /** How many segments for the bevel. */
  int seg;
  /** Offset for this edge, on left side. */
  float offset_l;
  /** Offset for this edge, on right side. */
  float offset_r;
  /** User specification for offset_l. */
  float offset_l_spec;
  /** User specification for offset_r. */
  float offset_r_spec;
  /** Is this edge beveled? */
  bool is_bev;
  /** Is e->v2 the vertex at this end? */
  bool is_rev;
  /** Is e a seam for custom loop-data (e.g., UV's). */
  bool is_seam;
  /** Used during the custom profile orientation pass. */
  bool visited_rpo;
  char _pad[4];
} EdgeHalf;

/**
 * Profile specification:
 * The profile is a path defined with start, middle, and end control points projected onto a
 * plane (plane_no is normal, plane_co is a point on it) via lines in a given direction (proj_dir).
 *
 * Many interesting profiles are in family of superellipses:
 *     (abs(x/a))^r + abs(y/b))^r = 1
 * r==2 => ellipse; r==1 => line; r < 1 => concave; r > 1 => bulging out.
 * Special cases: let r==0 mean straight-inward, and r==4 mean straight outward.
 *
 * After the parameters are all set, the actual profile points are calculated and pointed to
 * by prof_co. We also may need profile points for a higher resolution number of segments
 * for the subdivision while making the ADJ vertex mesh pattern, and that goes in prof_co_2.
 */
typedef struct Profile {
  /** Superellipse r parameter. */
  float super_r;
  /** Height for profile cutoff face sides. */
  float height;
  /** Start control point for profile. */
  float start[3];
  /** Mid control point for profile. */
  float middle[3];
  /** End control point for profile. */
  float end[3];
  /** Normal of plane to project to. */
  float plane_no[3];
  /** Coordinate on plane to project to. */
  float plane_co[3];
  /** Direction of projection line. */
  float proj_dir[3];
  /** seg+1 profile coordinates (triples of floats). */
  float *prof_co;
  /** Like prof_co, but for seg power of 2 >= seg. */
  float *prof_co_2;
  /** Mark a special case so the these parameters aren't reset with others. */
  bool special_params;
} Profile;
#define PRO_SQUARE_R 1e4f
#define PRO_CIRCLE_R 2.0f
#define PRO_LINE_R 1.0f
#define PRO_SQUARE_IN_R 0.0f

/**
 * The un-transformed 2D storage of profile vertex locations. Also, for non-custom profiles
 * this serves as a cache for the results of the expensive calculation of u parameter values to
 * get even spacing on superellipse for current BevelParams seg and pro_super_r.
 */
typedef struct ProfileSpacing {
  /** The profile's seg+1 x values. */
  double *xvals;
  /** The profile's seg+1 y values. */
  double *yvals;
  /** The profile's seg_2+1 x values, (seg_2 = power of 2 >= seg). */
  double *xvals_2;
  /** The profile's seg_2+1 y values, (seg_2 = power of 2 >= seg). */
  double *yvals_2;
  /** The power of two greater than or equal to the number of segments. */
  int seg_2;
  /** How far "out" the profile is, used at the start of subdivision. */
  float fullness;
} ProfileSpacing;

/**
 * If the mesh has custom data Loop layers that 'have math' we use this
 * data to help decide which face to use as representative when there
 * is an ambiguous choice as to which face to use, which happens
 * when there is an odd number of segments.
 *
 * The face_compent field of the following will only be set if there are an odd
 * number of segments. The it uses BMFace indices to index into it, so will
 * only be valid as long BMFaces are not added or deleted in the BMesh.
 * "Connected Component" here means connected in UV space:
 * i.e., one face is directly connected to another if they share an edge and
 * all of Loop UV custom layers are contiguous across that edge.
 */
typedef struct MathLayerInfo {
  /** A connected-component id for each BMFace in the mesh. */
  int *face_component;
  /** Does the mesh have any custom loop uv layers? */
  bool has_math_layers;
} MathLayerInfo;

/**
 * An element in a cyclic boundary of a Vertex Mesh (VMesh), placed on each side of beveled edges
 * where each profile starts, or on each side of a miter.
 */
typedef struct BoundVert {
  /** In CCW order. */
  struct BoundVert *next, *prev;
  NewVert nv;
  /** First of edges attached here: in CCW order. */
  EdgeHalf *efirst;
  EdgeHalf *elast;
  /** The "edge between" that this boundvert on, in offset_on_edge_between case. */
  EdgeHalf *eon;
  /** Beveled edge whose left side is attached here, if any. */
  EdgeHalf *ebev;
  /** Used for vmesh indexing. */
  int index;
  /** When eon set, ratio of sines of angles to eon edge. */
  float sinratio;
  /** Adjustment chain or cycle link pointer. */
  struct BoundVert *adjchain;
  /** Edge profile between this and next BoundVert. */
  Profile profile;
  /** Are any of the edges attached here seams? */
  bool any_seam;
  /** Used during delta adjust pass. */
  bool visited;
  /** This boundvert begins an arc profile. */
  bool is_arc_start;
  /** This boundvert begins a patch profile. */
  bool is_patch_start;
  /** Is this boundvert the side of the custom profile's start. */
  bool is_profile_start;
  char _pad[3];
  /** Length of seam starting from current boundvert to next boundvert with CCW ordering. */
  int seam_len;
  /** Same as seam_len but defines length of sharp edges. */
  int sharp_len;
} BoundVert;

/** Data for the mesh structure replacing a vertex. */
typedef struct VMesh {
  /** Allocated array - size and structure depends on kind. */
  NewVert *mesh;
  /** Start of boundary double-linked list. */
  BoundVert *boundstart;
  /** Number of vertices in the boundary. */
  int count;
  /** Common number of segments for segmented edges (same as bp->seg). */
  int seg;
  /** The kind of mesh to build at the corner vertex meshes. */
  enum {
    M_NONE,    /* No polygon mesh needed. */
    M_POLY,    /* A simple polygon. */
    M_ADJ,     /* "Adjacent edges" mesh pattern. */
    M_TRI_FAN, /* A simple polygon - fan filled. */
    M_CUTOFF,  /* A triangulated face at the end of each profile. */
  } mesh_kind;

  int _pad;
} VMesh;

/* Data for a vertex involved in a bevel. */
typedef struct BevVert {
  /** Original mesh vertex. */
  BMVert *v;
  /** Total number of edges around the vertex (excluding wire edges if edge beveling). */
  int edgecount;
  /** Number of selected edges around the vertex. */
  int selcount;
  /** Count of wire edges. */
  int wirecount;
  /** Offset for this vertex, if vertex only bevel. */
  float offset;
  /** Any seams on attached edges? */
  bool any_seam;
  /** Used in graph traversal for adjusting offsets. */
  bool visited;
  /** Array of size edgecount; CCW order from vertex normal side. */
  char _pad[6];
  EdgeHalf *edges;
  /** Array of size wirecount of wire edges. */
  BMEdge **wire_edges;
  /** Mesh structure for replacing vertex. */
  VMesh *vmesh;
} BevVert;

/**
 * Face classification.
 * \note depends on `F_RECON > F_EDGE > F_VERT`.
 */
typedef enum {
  /** Used when there is no face at all. */
  F_NONE,
  /** Original face, not touched. */
  F_ORIG,
  /** Face for construction around a vert. */
  F_VERT,
  /** Face for a beveled edge. */
  F_EDGE,
  /** Reconstructed original face with some new verts. */
  F_RECON,
} FKind;

/** Helper for keeping track of angle kind. */
typedef enum AngleKind {
  /** Angle less than 180 degrees. */
  ANGLE_SMALLER = -1,
  /** 180 degree angle. */
  ANGLE_STRAIGHT = 0,
  /** Angle greater than 180 degrees. */
  ANGLE_LARGER = 1,
} AngleKind;

/** Bevel parameters and state. */
typedef struct BevelParams {
  /** Records BevVerts made: key BMVert*, value BevVert* */
  GHash *vert_hash;
  /** Records new faces: key BMFace*, value one of {VERT/EDGE/RECON}_POLY. */
  GHash *face_hash;
  /** Use for all allocs while bevel runs. NOTE: If we need to free we can switch to mempool. */
  MemArena *mem_arena;
  /** Profile vertex location and spacings. */
  ProfileSpacing pro_spacing;
  /** Parameter values for evenly spaced profile points for the miter profiles. */
  ProfileSpacing pro_spacing_miter;
  /** Information about 'math' loop layers, like UV layers. */
  MathLayerInfo math_layer_info;
  /** The argument BMesh. */
  BMesh *bm;
  /** Blender units to offset each side of a beveled edge. */
  float offset;
  /** How offset is measured; enum defined in bmesh_operators.h. */
  int offset_type;
  /** Profile type: radius, superellipse, or custom */
  int profile_type;
  /** Bevel vertices only or edges. */
  int affect_type;
  /** Number of segments in beveled edge profile. */
  int seg;
  /** User profile setting. */
  float profile;
  /** Superellipse parameter for edge profile. */
  float pro_super_r;
  /** Bevel amount affected by weights on edges or verts. */
  bool use_weights;
  /** Should bevel prefer to slide along edges rather than keep widths spec? */
  bool loop_slide;
  /** Should offsets be limited by collisions? */
  bool limit_offset;
  /** Should offsets be adjusted to try to get even widths? */
  bool offset_adjust;
  /** Should we propagate seam edge markings? */
  bool mark_seam;
  /** Should we propagate sharp edge markings? */
  bool mark_sharp;
  /** Should we harden normals? */
  bool harden_normals;
  char _pad[1];
  /** The struct used to store the custom profile input. */
  const struct CurveProfile *custom_profile;
  /** Vertex group array, maybe set if vertex only. */
  const struct MDeformVert *dvert;
  /** Vertex group index, maybe set if vertex only. */
  int vertex_group;
  /** If >= 0, material number for bevel; else material comes from adjacent faces. */
  int mat_nr;
  /** Setting face strength if > 0. */
  int face_strength_mode;
  /** What kind of miter pattern to use on reflex angles. */
  int miter_outer;
  /** What kind of miter pattern to use on non-reflex angles. */
  int miter_inner;
  /** The method to use for vertex mesh creation */
  int vmesh_method;
  /** Amount to spread when doing inside miter. */
  float spread;
  /** Mesh's smoothresh, used if hardening. */
  float smoothresh;
} BevelParams;

// #pragma GCC diagnostic ignored "-Wpadded"

/* Only for debugging, this file shouldn't be in blender repository. */
// #include "bevdebug.c"

/* Use the unused _BM_ELEM_TAG_ALT flag to flag the 'long' loops (parallel to beveled edge)
 * of edge-polygons. */
#define BM_ELEM_LONG_TAG (1 << 6)

/* These flag values will get set on geom we want to return in 'out' slots for edges and verts. */
#define EDGE_OUT 4
#define VERT_OUT 8

/* If we're called from the modifier, tool flags aren't available,
 * but don't need output geometry. */
static void flag_out_edge(BMesh *bm, BMEdge *bme)
{
  if (bm->use_toolflags) {
    BMO_edge_flag_enable(bm, bme, EDGE_OUT);
  }
}

static void flag_out_vert(BMesh *bm, BMVert *bmv)
{
  if (bm->use_toolflags) {
    BMO_vert_flag_enable(bm, bmv, VERT_OUT);
  }
}

static void disable_flag_out_edge(BMesh *bm, BMEdge *bme)
{
  if (bm->use_toolflags) {
    BMO_edge_flag_disable(bm, bme, EDGE_OUT);
  }
}

static void record_face_kind(BevelParams *bp, BMFace *f, FKind fkind)
{
  if (bp->face_hash) {
    BLI_ghash_insert(bp->face_hash, f, POINTER_FROM_INT(fkind));
  }
}

static FKind get_face_kind(BevelParams *bp, BMFace *f)
{
  void *val = BLI_ghash_lookup(bp->face_hash, f);
  return val ? (FKind)POINTER_AS_INT(val) : F_ORIG;
}

/* Are d1 and d2 parallel or nearly so? */
static bool nearly_parallel(const float d1[3], const float d2[3])
{
  float ang = angle_v3v3(d1, d2);

  return (fabsf(ang) < BEVEL_EPSILON_ANG) || (fabsf(ang - (float)M_PI) < BEVEL_EPSILON_ANG);
}

/**
 * \return True if d1 and d2 are parallel or nearly parallel.
 */
static bool nearly_parallel_normalized(const float d1[3], const float d2[3])
{
  BLI_ASSERT_UNIT_V3(d1);
  BLI_ASSERT_UNIT_V3(d2);

  const float direction_dot = dot_v3v3(d1, d2);
  return compare_ff(fabsf(direction_dot), 1.0f, BEVEL_EPSILON_ANG_DOT);
}

/**
 * calculate the determinant of a matrix formed by three vectors
 * \return dot(a, cross(b, c)) = determinant(a, b, c)
 */
static float determinant_v3v3v3(const float a[3], const float b[3], const float c[3])
{
  return a[0] * b[1] * c[2] + a[1] * b[2] * c[0] + a[2] * b[0] * c[1] - a[0] * b[2] * c[1] -
         a[1] * b[0] * c[2] - a[2] * b[1] * c[0];
}

/* Make a new BoundVert of the given kind, inserting it at the end of the circular linked
 * list with entry point bv->boundstart, and return it. */
static BoundVert *add_new_bound_vert(MemArena *mem_arena, VMesh *vm, const float co[3])
{
  BoundVert *ans = (BoundVert *)BLI_memarena_alloc(mem_arena, sizeof(BoundVert));

  copy_v3_v3(ans->nv.co, co);
  if (!vm->boundstart) {
    ans->index = 0;
    vm->boundstart = ans;
    ans->next = ans->prev = ans;
  }
  else {
    BoundVert *tail = vm->boundstart->prev;
    ans->index = tail->index + 1;
    ans->prev = tail;
    ans->next = vm->boundstart;
    tail->next = ans;
    vm->boundstart->prev = ans;
  }
  ans->profile.super_r = PRO_LINE_R;
  ans->adjchain = NULL;
  ans->sinratio = 1.0f;
  ans->visited = false;
  ans->any_seam = false;
  ans->is_arc_start = false;
  ans->is_patch_start = false;
  ans->is_profile_start = false;
  vm->count++;
  return ans;
}

BLI_INLINE void adjust_bound_vert(BoundVert *bv, const float co[3])
{
  copy_v3_v3(bv->nv.co, co);
}

/* Mesh verts are indexed (i, j, k) where
 * i = boundvert index (0 <= i < nv)
 * j = ring index (0 <= j <= ns2)
 * k = segment index (0 <= k <= ns)
 * Not all of these are used, and some will share BMVerts. */
static NewVert *mesh_vert(VMesh *vm, int i, int j, int k)
{
  int nj = (vm->seg / 2) + 1;
  int nk = vm->seg + 1;

  return &vm->mesh[i * nk * nj + j * nk + k];
}

static void create_mesh_bmvert(BMesh *bm, VMesh *vm, int i, int j, int k, BMVert *eg)
{
  NewVert *nv = mesh_vert(vm, i, j, k);
  nv->v = BM_vert_create(bm, nv->co, eg, BM_CREATE_NOP);
  BM_elem_flag_disable(nv->v, BM_ELEM_TAG);
  flag_out_vert(bm, nv->v);
}

static void copy_mesh_vert(VMesh *vm, int ito, int jto, int kto, int ifrom, int jfrom, int kfrom)
{
  NewVert *nvto = mesh_vert(vm, ito, jto, kto);
  NewVert *nvfrom = mesh_vert(vm, ifrom, jfrom, kfrom);
  nvto->v = nvfrom->v;
  copy_v3_v3(nvto->co, nvfrom->co);
}

/* Find the EdgeHalf in bv's array that has edge bme. */
static EdgeHalf *find_edge_half(BevVert *bv, BMEdge *bme)
{
  for (int i = 0; i < bv->edgecount; i++) {
    if (bv->edges[i].e == bme) {
      return &bv->edges[i];
    }
  }
  return NULL;
}

/* Find the BevVert corresponding to BMVert bmv. */
static BevVert *find_bevvert(BevelParams *bp, BMVert *bmv)
{
  return BLI_ghash_lookup(bp->vert_hash, bmv);
}

/**
 * Find the EdgeHalf representing the other end of e->e.
 * \return other end's BevVert in *r_bvother, if r_bvother is provided. That may not have
 * been constructed yet, in which case return NULL.
 */
static EdgeHalf *find_other_end_edge_half(BevelParams *bp, EdgeHalf *e, BevVert **r_bvother)
{
  BevVert *bvo = find_bevvert(bp, e->is_rev ? e->e->v1 : e->e->v2);
  if (bvo) {
    if (r_bvother) {
      *r_bvother = bvo;
    }
    EdgeHalf *eother = find_edge_half(bvo, e->e);
    BLI_assert(eother != NULL);
    return eother;
  }
  if (r_bvother) {
    *r_bvother = NULL;
  }
  return NULL;
}

/* Return the next EdgeHalf after from_e that is beveled.
 * If from_e is NULL, find the first beveled edge. */
static EdgeHalf *next_bev(BevVert *bv, EdgeHalf *from_e)
{
  if (from_e == NULL) {
    from_e = &bv->edges[bv->edgecount - 1];
  }
  EdgeHalf *e = from_e;
  do {
    if (e->is_bev) {
      return e;
    }
  } while ((e = e->next) != from_e);
  return NULL;
}

/* Return the count of edges between e1 and e2 when going around bv CCW. */
static int count_ccw_edges_between(EdgeHalf *e1, EdgeHalf *e2)
{
  int count = 0;
  EdgeHalf *e = e1;

  do {
    if (e == e2) {
      break;
    }
    e = e->next;
    count++;
  } while (e != e1);
  return count;
}

/* Assume bme1 and bme2 both share some vert. Do they share a face?
 * If they share a face then there is some loop around bme1 that is in a face
 * where the next or previous edge in the face must be bme2. */
static bool edges_face_connected_at_vert(BMEdge *bme1, BMEdge *bme2)
{
  BMIter iter;
  BMLoop *l;
  BM_ITER_ELEM (l, &iter, bme1, BM_LOOPS_OF_EDGE) {
    if (l->prev->e == bme2 || l->next->e == bme2) {
      return true;
    }
  }
  return false;
}

/**
 * Return a good representative face (for materials, etc.) for faces
 * created around/near BoundVert v.
 * Sometimes care about a second choice, if there is one.
 * If r_fother parameter is non-NULL and there is another, different,
 * possible frep, return the other one in that parameter.
 */
static BMFace *boundvert_rep_face(BoundVert *v, BMFace **r_fother)
{
  BMFace *frep;

  BMFace *frep2 = NULL;
  if (v->ebev) {
    frep = v->ebev->fprev;
    if (v->efirst->fprev != frep) {
      frep2 = v->efirst->fprev;
    }
  }
  else if (v->efirst) {
    frep = v->efirst->fprev;
    if (frep) {
      if (v->elast->fnext != frep) {
        frep2 = v->elast->fnext;
      }
      else if (v->efirst->fnext != frep) {
        frep2 = v->efirst->fnext;
      }
      else if (v->elast->fprev != frep) {
        frep2 = v->efirst->fprev;
      }
    }
    else if (v->efirst->fnext) {
      frep = v->efirst->fnext;
      if (v->elast->fnext != frep) {
        frep2 = v->elast->fnext;
      }
    }
    else if (v->elast->fprev) {
      frep = v->elast->fprev;
    }
  }
  else if (v->prev->elast) {
    frep = v->prev->elast->fnext;
    if (v->next->efirst) {
      if (frep) {
        frep2 = v->next->efirst->fprev;
      }
      else {
        frep = v->next->efirst->fprev;
      }
    }
  }
  else {
    frep = NULL;
  }
  if (r_fother) {
    *r_fother = frep2;
  }
  return frep;
}

/**
 * Make ngon from verts alone.
 * Make sure to properly copy face attributes and do custom data interpolation from
 * corresponding elements of face_arr, if that is non-NULL, else from facerep.
 * If edge_arr is non-NULL, then for interpolation purposes only, the corresponding
 * elements of vert_arr are snapped to any non-NULL edges in that array.
 * If mat_nr >= 0 then the material of the face is set to that.
 *
 * \note ALL face creation goes through this function, this is important to keep!
 */
static BMFace *bev_create_ngon(BMesh *bm,
                               BMVert **vert_arr,
                               const int totv,
                               BMFace **face_arr,
                               BMFace *facerep,
                               BMEdge **edge_arr,
                               int mat_nr,
                               bool do_interp)
{
  BMFace *f = BM_face_create_verts(bm, vert_arr, totv, facerep, BM_CREATE_NOP, true);

  if ((facerep || (face_arr && face_arr[0])) && f) {
    BM_elem_attrs_copy(bm, bm, facerep ? facerep : face_arr[0], f);
    if (do_interp) {
      int i = 0;
      BMIter iter;
      BMLoop *l;
      BM_ITER_ELEM (l, &iter, f, BM_LOOPS_OF_FACE) {
        BMFace *interp_f;
        if (face_arr) {
          /* Assume loops of created face are in same order as verts. */
          BLI_assert(l->v == vert_arr[i]);
          interp_f = face_arr[i];
        }
        else {
          interp_f = facerep;
        }
        if (interp_f) {
          BMEdge *bme = NULL;
          if (edge_arr) {
            bme = edge_arr[i];
          }
          float save_co[3];
          if (bme) {
            copy_v3_v3(save_co, l->v->co);
            closest_to_line_segment_v3(l->v->co, save_co, bme->v1->co, bme->v2->co);
          }
          BM_loop_interp_from_face(bm, l, interp_f, true, true);
          if (bme) {
            copy_v3_v3(l->v->co, save_co);
          }
        }
        i++;
      }
    }
  }

  /* Not essential for bevels own internal logic,
   * this is done so the operator can select newly created geometry. */
  if (f) {
    BM_elem_flag_enable(f, BM_ELEM_TAG);
    BMIter iter;
    BMEdge *bme;
    BM_ITER_ELEM (bme, &iter, f, BM_EDGES_OF_FACE) {
      flag_out_edge(bm, bme);
    }
  }

  if (mat_nr >= 0) {
    f->mat_nr = (short)mat_nr;
  }
  return f;
}

static BMFace *bev_create_quad(BMesh *bm,
                               BMVert *v1,
                               BMVert *v2,
                               BMVert *v3,
                               BMVert *v4,
                               BMFace *f1,
                               BMFace *f2,
                               BMFace *f3,
                               BMFace *f4,
                               int mat_nr)
{
  BMVert *varr[4] = {v1, v2, v3, v4};
  BMFace *farr[4] = {f1, f2, f3, f4};
  return bev_create_ngon(bm, varr, 4, farr, f1, NULL, mat_nr, true);
}

static BMFace *bev_create_quad_ex(BMesh *bm,
                                  BMVert *v1,
                                  BMVert *v2,
                                  BMVert *v3,
                                  BMVert *v4,
                                  BMFace *f1,
                                  BMFace *f2,
                                  BMFace *f3,
                                  BMFace *f4,
                                  BMEdge *e1,
                                  BMEdge *e2,
                                  BMEdge *e3,
                                  BMEdge *e4,
                                  BMFace *frep,
                                  int mat_nr)
{
  BMVert *varr[4] = {v1, v2, v3, v4};
  BMFace *farr[4] = {f1, f2, f3, f4};
  BMEdge *earr[4] = {e1, e2, e3, e4};
  return bev_create_ngon(bm, varr, 4, farr, frep, earr, mat_nr, true);
}

/* Is Loop layer layer_index contiguous across shared vertex of l1 and l2? */
static bool contig_ldata_across_loops(BMesh *bm, BMLoop *l1, BMLoop *l2, int layer_index)
{
  const int offset = bm->ldata.layers[layer_index].offset;
  const int type = bm->ldata.layers[layer_index].type;

  return CustomData_data_equals(
      type, (char *)l1->head.data + offset, (char *)l2->head.data + offset);
}

/* Are all loop layers with have math (e.g., UVs)
 * contiguous from face f1 to face f2 across edge e?
 */
static bool contig_ldata_across_edge(BMesh *bm, BMEdge *e, BMFace *f1, BMFace *f2)
{
  if (bm->ldata.totlayer == 0) {
    return true;
  }

  BMLoop *lef1, *lef2;
  if (!BM_edge_loop_pair(e, &lef1, &lef2)) {
    return false;
  }
  /* If faces are oriented consistently around e,
   * should now have lef1 and lef2 being f1 and f2 in either order.
   */
  if (lef1->f == f2) {
    SWAP(BMLoop *, lef1, lef2);
  }
  if (lef1->f != f1 || lef2->f != f2) {
    return false;
  }
  BMVert *v1 = lef1->v;
  BMVert *v2 = lef2->v;
  if (v1 == v2) {
    return false;
  }
  BLI_assert((v1 == e->v1 && v2 == e->v2) || (v1 == e->v2 && v2 == e->v1));
  UNUSED_VARS_NDEBUG(v1, v2);
  BMLoop *lv1f1 = lef1;
  BMLoop *lv2f1 = lef1->next;
  BMLoop *lv1f2 = lef2->next;
  BMLoop *lv2f2 = lef2;
  BLI_assert(lv1f1->v == v1 && lv1f1->f == f1 && lv2f1->v == v2 && lv2f1->f == f1 &&
             lv1f2->v == v1 && lv1f2->f == f2 && lv2f2->v == v2 && lv2f2->f == f2);
  for (int i = 0; i < bm->ldata.totlayer; i++) {
    if (CustomData_layer_has_math(&bm->ldata, i)) {
      if (!contig_ldata_across_loops(bm, lv1f1, lv1f2, i) ||
          !contig_ldata_across_loops(bm, lv2f1, lv2f2, i)) {
        return false;
      }
    }
  }
  return true;
}

/*
 * Set up the fields of bp->math_layer_info.
 * We always set has_math_layers to the correct value.
 * Only if there are UV layers and the number of segments is odd,
 * we need to calculate connected face components in UV space.
 */
static void math_layer_info_init(BevelParams *bp, BMesh *bm)
{
  bp->math_layer_info.has_math_layers = false;
  bp->math_layer_info.face_component = NULL;
  for (int i = 0; i < bm->ldata.totlayer; i++) {
    if (CustomData_has_layer(&bm->ldata, CD_MLOOPUV)) {
      bp->math_layer_info.has_math_layers = true;
      break;
    }
  }
  if (!bp->math_layer_info.has_math_layers || (bp->seg % 2) == 0) {
    return;
  }

  BM_mesh_elem_index_ensure(bm, BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_FACE);
  int totface = bm->totface;
  int *face_component = BLI_memarena_alloc(bp->mem_arena, sizeof(int) * totface);
  bp->math_layer_info.face_component = face_component;

  /* Use an array as a stack. Stack size can't exceed total faces if keep track of what is in
   * stack. */
  BMFace **stack = MEM_malloc_arrayN(totface, sizeof(BMFace *), __func__);
  bool *in_stack = MEM_malloc_arrayN(totface, sizeof(bool), __func__);

  /* Set all component ids by DFS from faces with unassigned components. */
  for (int f = 0; f < totface; f++) {
    face_component[f] = -1;
    in_stack[f] = false;
  }
  int current_component = -1;
  for (int f = 0; f < totface; f++) {
    if (face_component[f] == -1 && !in_stack[f]) {
      int stack_top = 0;
      current_component++;
      BLI_assert(stack_top < totface);
      stack[stack_top] = BM_face_at_index(bm, f);
      in_stack[f] = true;
      while (stack_top >= 0) {
        BMFace *bmf = stack[stack_top];
        stack_top--;
        int bmf_index = BM_elem_index_get(bmf);
        in_stack[bmf_index] = false;
        if (face_component[bmf_index] != -1) {
          continue;
        }
        face_component[bmf_index] = current_component;
        /* Neighbors are faces that share an edge with bmf and
         * are where contig_ldata_across_edge(...) is true for the
         * shared edge and two faces.
         */
        BMIter eiter;
        BMEdge *bme;
        BM_ITER_ELEM (bme, &eiter, bmf, BM_EDGES_OF_FACE) {
          BMIter fiter;
          BMFace *bmf_other;
          BM_ITER_ELEM (bmf_other, &fiter, bme, BM_FACES_OF_EDGE) {
            if (bmf_other != bmf) {
              int bmf_other_index = BM_elem_index_get(bmf_other);
              if (face_component[bmf_other_index] != -1 || in_stack[bmf_other_index]) {
                continue;
              }
              if (contig_ldata_across_edge(bm, bme, bmf, bmf_other)) {
                stack_top++;
                BLI_assert(stack_top < totface);
                stack[stack_top] = bmf_other;
                in_stack[bmf_other_index] = true;
              }
            }
          }
        }
      }
    }
  }
  MEM_freeN(stack);
  MEM_freeN(in_stack);
}

/**
 * Use a tie-breaking rule to choose a representative face when
 * there are number of choices, `face[0]`, `face[1]`, ..., `face[nfaces]`.
 * This is needed when there are an odd number of segments, and the center
 * segment (and its continuation into vmesh) can usually arbitrarily be
 * the previous face or the next face.
 * Or, for the center polygon of a corner, all of the faces around
 * the vertex are possibleface_component choices.
 * If we just choose randomly, the resulting UV maps or material
 * assignment can look ugly/inconsistent.
 * Allow for the case when arguments are null.
 */
static BMFace *choose_rep_face(BevelParams *bp, BMFace **face, int nfaces)
{
#define VEC_VALUE_LEN 6
  float(*value_vecs)[VEC_VALUE_LEN] = NULL;
  int num_viable = 0;

  value_vecs = BLI_array_alloca(value_vecs, nfaces);
  bool *still_viable = BLI_array_alloca(still_viable, nfaces);
  for (int f = 0; f < nfaces; f++) {
    BMFace *bmf = face[f];
    if (bmf == NULL) {
      still_viable[f] = false;
      continue;
    }
    still_viable[f] = true;
    num_viable++;
    int bmf_index = BM_elem_index_get(bmf);
    int value_index = 0;
    /* First tie-breaker: lower math-layer connected component id. */
    value_vecs[f][value_index++] = bp->math_layer_info.face_component ?
                                       (float)bp->math_layer_info.face_component[bmf_index] :
                                       0.0f;
    /* Next tie-breaker: selected face beats unselected one. */
    value_vecs[f][value_index++] = BM_elem_flag_test(bmf, BM_ELEM_SELECT) ? 0.0f : 1.0f;
    /* Next tie-breaker: lower material index. */
    value_vecs[f][value_index++] = bmf->mat_nr >= 0 ? (float)bmf->mat_nr : 0.0f;
    /* Next three tie-breakers: z, x, y components of face center. */
    float cent[3];
    BM_face_calc_center_bounds(bmf, cent);
    value_vecs[f][value_index++] = cent[2];
    value_vecs[f][value_index++] = cent[0];
    value_vecs[f][value_index++] = cent[1];
    BLI_assert(value_index == VEC_VALUE_LEN);
  }

  /* Look for a face that has a unique minimum value for in a value_index,
   * trying each value_index in turn until find a unique minimum.
   */
  int best_f = -1;
  for (int value_index = 0; num_viable > 1 && value_index < VEC_VALUE_LEN; value_index++) {
    for (int f = 0; f < nfaces; f++) {
      if (!still_viable[f] || f == best_f) {
        continue;
      }
      if (best_f == -1) {
        best_f = f;
        continue;
      }
      if (value_vecs[f][value_index] < value_vecs[best_f][value_index]) {
        best_f = f;
        /* Previous f's are now not viable any more. */
        for (int i = f - 1; i >= 0; i--) {
          if (still_viable[i]) {
            still_viable[i] = false;
            num_viable--;
          }
        }
      }
      else if (value_vecs[f][value_index] > value_vecs[best_f][value_index]) {
        still_viable[f] = false;
        num_viable--;
      }
    }
  }
  if (best_f == -1) {
    best_f = 0;
  }
  return face[best_f];
#undef VEC_VALUE_LEN
}

/* Merge (using average) all the UV values for loops of v's faces.
 * Caller should ensure that no seams are violated by doing this. */
static void bev_merge_uvs(BMesh *bm, BMVert *v)
{
  int num_of_uv_layers = CustomData_number_of_layers(&bm->ldata, CD_MLOOPUV);

  for (int i = 0; i < num_of_uv_layers; i++) {
    int cd_loop_uv_offset = CustomData_get_n_offset(&bm->ldata, CD_MLOOPUV, i);

    if (cd_loop_uv_offset == -1) {
      return;
    }

    int n = 0;
    float uv[2] = {0.0f, 0.0f};
    BMIter iter;
    BMLoop *l;
    BM_ITER_ELEM (l, &iter, v, BM_LOOPS_OF_VERT) {
      MLoopUV *luv = BM_ELEM_CD_GET_VOID_P(l, cd_loop_uv_offset);
      add_v2_v2(uv, luv->uv);
      n++;
    }
    if (n > 1) {
      mul_v2_fl(uv, 1.0f / (float)n);
      BM_ITER_ELEM (l, &iter, v, BM_LOOPS_OF_VERT) {
        MLoopUV *luv = BM_ELEM_CD_GET_VOID_P(l, cd_loop_uv_offset);
        copy_v2_v2(luv->uv, uv);
      }
    }
  }
}

/* Merge (using average) the UV values for two specific loops of v: those for faces containing v,
 * and part of faces that share edge bme. */
static void bev_merge_edge_uvs(BMesh *bm, BMEdge *bme, BMVert *v)
{
  int num_of_uv_layers = CustomData_number_of_layers(&bm->ldata, CD_MLOOPUV);

  BMLoop *l1 = NULL;
  BMLoop *l2 = NULL;
  BMIter iter;
  BMLoop *l;
  BM_ITER_ELEM (l, &iter, v, BM_LOOPS_OF_VERT) {
    if (l->e == bme) {
      l1 = l;
    }
    else if (l->prev->e == bme) {
      l2 = l;
    }
  }
  if (l1 == NULL || l2 == NULL) {
    return;
  }

  for (int i = 0; i < num_of_uv_layers; i++) {
    int cd_loop_uv_offset = CustomData_get_n_offset(&bm->ldata, CD_MLOOPUV, i);

    if (cd_loop_uv_offset == -1) {
      return;
    }

    float uv[2] = {0.0f, 0.0f};
    MLoopUV *luv = BM_ELEM_CD_GET_VOID_P(l1, cd_loop_uv_offset);
    add_v2_v2(uv, luv->uv);
    luv = BM_ELEM_CD_GET_VOID_P(l2, cd_loop_uv_offset);
    add_v2_v2(uv, luv->uv);
    mul_v2_fl(uv, 0.5f);
    luv = BM_ELEM_CD_GET_VOID_P(l1, cd_loop_uv_offset);
    copy_v2_v2(luv->uv, uv);
    luv = BM_ELEM_CD_GET_VOID_P(l2, cd_loop_uv_offset);
    copy_v2_v2(luv->uv, uv);
  }
}

/* Calculate coordinates of a point a distance d from v on e->e and return it in slideco. */
static void slide_dist(EdgeHalf *e, BMVert *v, float d, float r_slideco[3])
{
  float dir[3];
  sub_v3_v3v3(dir, v->co, BM_edge_other_vert(e->e, v)->co);
  float len = normalize_v3(dir);

  if (d > len) {
    d = len - (float)(50.0 * BEVEL_EPSILON_D);
  }
  copy_v3_v3(r_slideco, v->co);
  madd_v3_v3fl(r_slideco, dir, -d);
}

/* Is co not on the edge e? If not, return the closer end of e in ret_closer_v. */
static bool is_outside_edge(EdgeHalf *e, const float co[3], BMVert **ret_closer_v)
{
  float h[3], u[3];
  float *l1 = e->e->v1->co;

  sub_v3_v3v3(u, e->e->v2->co, l1);
  sub_v3_v3v3(h, co, l1);
  float lenu = normalize_v3(u);
  float lambda = dot_v3v3(u, h);
  if (lambda <= -BEVEL_EPSILON_BIG * lenu) {
    *ret_closer_v = e->e->v1;
    return true;
  }
  if (lambda >= (1.0f + BEVEL_EPSILON_BIG) * lenu) {
    *ret_closer_v = e->e->v2;
    return true;
  }
  return false;
}

/* Return whether the angle is less than, equal to, or larger than 180 degrees. */
static AngleKind edges_angle_kind(EdgeHalf *e1, EdgeHalf *e2, BMVert *v)
{
  BMVert *v1 = BM_edge_other_vert(e1->e, v);
  BMVert *v2 = BM_edge_other_vert(e2->e, v);
  float dir1[3], dir2[3];
  sub_v3_v3v3(dir1, v->co, v1->co);
  sub_v3_v3v3(dir2, v->co, v2->co);
  normalize_v3(dir1);
  normalize_v3(dir2);

  /* First check for in-line edges using a simpler test. */
  if (nearly_parallel_normalized(dir1, dir2)) {
    return ANGLE_STRAIGHT;
  }

  /* Angles are in [0,pi]. Need to compare cross product with normal to see if they are reflex. */
  float cross[3];
  cross_v3_v3v3(cross, dir1, dir2);
  normalize_v3(cross);
  float *no;
  if (e1->fnext) {
    no = e1->fnext->no;
  }
  else if (e2->fprev) {
    no = e2->fprev->no;
  }
  else {
    no = v->no;
  }

  if (dot_v3v3(cross, no) < 0.0f) {
    return ANGLE_LARGER;
  }
  return ANGLE_SMALLER;
}

/* co should be approximately on the plane between e1 and e2, which share common vert v and common
 * face f (which cannot be NULL). Is it between those edges, sweeping CCW? */
static bool point_between_edges(
    const float co[3], BMVert *v, BMFace *f, EdgeHalf *e1, EdgeHalf *e2)
{
  float dir1[3], dir2[3], dirco[3], no[3];

  BMVert *v1 = BM_edge_other_vert(e1->e, v);
  BMVert *v2 = BM_edge_other_vert(e2->e, v);
  sub_v3_v3v3(dir1, v->co, v1->co);
  sub_v3_v3v3(dir2, v->co, v2->co);
  sub_v3_v3v3(dirco, v->co, co);
  normalize_v3(dir1);
  normalize_v3(dir2);
  normalize_v3(dirco);
  float ang11 = angle_normalized_v3v3(dir1, dir2);
  float ang1co = angle_normalized_v3v3(dir1, dirco);
  /* Angles are in [0,pi]. Need to compare cross product with normal to see if they are reflex. */
  cross_v3_v3v3(no, dir1, dir2);
  if (dot_v3v3(no, f->no) < 0.0f) {
    ang11 = (float)(M_PI * 2.0) - ang11;
  }
  cross_v3_v3v3(no, dir1, dirco);
  if (dot_v3v3(no, f->no) < 0.0f) {
    ang1co = (float)(M_PI * 2.0) - ang1co;
  }
  return (ang11 - ang1co > -BEVEL_EPSILON_ANG);
}

/* Is the angle swept from e1 to e2, CCW when viewed from the normal side of f,
 * not a reflex angle or a straight angle? Assume e1 and e2 share a vert. */
static bool edge_edge_angle_less_than_180(const BMEdge *e1, const BMEdge *e2, const BMFace *f)
{
  float dir1[3], dir2[3], cross[3];
  BLI_assert(f != NULL);
  BMVert *v, *v1, *v2;
  if (e1->v1 == e2->v1) {
    v = e1->v1;
    v1 = e1->v2;
    v2 = e2->v2;
  }
  else if (e1->v1 == e2->v2) {
    v = e1->v1;
    v1 = e1->v2;
    v2 = e2->v1;
  }
  else if (e1->v2 == e2->v1) {
    v = e1->v2;
    v1 = e1->v1;
    v2 = e2->v2;
  }
  else if (e1->v2 == e2->v2) {
    v = e1->v2;
    v1 = e1->v1;
    v2 = e2->v1;
  }
  else {
    BLI_assert(false);
    return false;
  }
  sub_v3_v3v3(dir1, v1->co, v->co);
  sub_v3_v3v3(dir2, v2->co, v->co);
  cross_v3_v3v3(cross, dir1, dir2);
  return dot_v3v3(cross, f->no) > 0.0f;
}

/* When the offset_type is BEVEL_AMT_PERCENT or BEVEL_AMT_ABSOLUTE, fill in the coordinates
 * of the lines whose intersection defines the boundary point between e1 and e2 with common
 * vert v, as defined in the parameters of offset_meet.
 */
static void offset_meet_lines_percent_or_absolute(BevelParams *bp,
                                                  EdgeHalf *e1,
                                                  EdgeHalf *e2,
                                                  BMVert *v,
                                                  float r_l1a[3],
                                                  float r_l1b[3],
                                                  float r_l2a[3],
                                                  float r_l2b[3])
{
  /* Get points the specified distance along each leg.
   * NOTE: not all BevVerts and EdgeHalfs have been made yet, so we have
   * to find required edges by moving around faces and use fake EdgeHalfs for
   * some of the edges. If there aren't faces to move around, we have to give up.
   * The legs we need are:
   *   e0 : the next edge around e1->fnext (==f1) after e1.
   *   e3 : the prev edge around e2->fprev (==f2) before e2.
   *   e4 : the previous edge around f1 before e1 (may be e2).
   *   e5 : the next edge around f2 after e2 (may be e1).
   */
  BMVert *v1, *v2;
  EdgeHalf e0, e3, e4, e5;
  BMFace *f1, *f2;
  float d0, d3, d4, d5;
  float e1_wt, e2_wt;
  v1 = BM_edge_other_vert(e1->e, v);
  v2 = BM_edge_other_vert(e2->e, v);
  f1 = e1->fnext;
  f2 = e2->fprev;
  bool no_offsets = f1 == NULL || f2 == NULL;
  if (!no_offsets) {
    BMLoop *l = BM_face_vert_share_loop(f1, v1);
    e0.e = l->e;
    l = BM_face_vert_share_loop(f2, v2);
    e3.e = l->prev->e;
    l = BM_face_vert_share_loop(f1, v);
    e4.e = l->prev->e;
    l = BM_face_vert_share_loop(f2, v);
    e5.e = l->e;
    /* All the legs must be visible from their opposite legs. */
    no_offsets = !edge_edge_angle_less_than_180(e0.e, e1->e, f1) ||
                 !edge_edge_angle_less_than_180(e1->e, e4.e, f1) ||
                 !edge_edge_angle_less_than_180(e2->e, e3.e, f2) ||
                 !edge_edge_angle_less_than_180(e5.e, e2->e, f1);
    if (!no_offsets) {
      if (bp->offset_type == BEVEL_AMT_ABSOLUTE) {
        d0 = d3 = d4 = d5 = bp->offset;
      }
      else {
        d0 = bp->offset * BM_edge_calc_length(e0.e) / 100.0f;
        d3 = bp->offset * BM_edge_calc_length(e3.e) / 100.0f;
        d4 = bp->offset * BM_edge_calc_length(e4.e) / 100.0f;
        d5 = bp->offset * BM_edge_calc_length(e5.e) / 100.0f;
      }
      if (bp->use_weights) {
        CustomData *cd = &bp->bm->edata;
        e1_wt = BM_elem_float_data_get(cd, e1->e, CD_BWEIGHT);
        e2_wt = BM_elem_float_data_get(cd, e2->e, CD_BWEIGHT);
      }
      else {
        e1_wt = 1.0f;
        e2_wt = 1.0f;
      }
      slide_dist(&e4, v, d4 * e1_wt, r_l1a);
      slide_dist(&e0, v1, d0 * e1_wt, r_l1b);
      slide_dist(&e5, v, d5 * e2_wt, r_l2a);
      slide_dist(&e3, v2, d3 * e2_wt, r_l2b);
    }
  }
  if (no_offsets) {
    copy_v3_v3(r_l1a, v->co);
    copy_v3_v3(r_l1b, v1->co);
    copy_v3_v3(r_l2a, v->co);
    copy_v3_v3(r_l2b, v2->co);
  }
}

/**
 * Calculate the meeting point between the offset edges for e1 and e2, putting answer in meetco.
 * e1 and e2 share vertex v and face f (may be NULL) and viewed from the normal side of
 * the bevel vertex, e1 precedes e2 in CCW order.
 * Offset edge is on right of both edges, where e1 enters v and e2 leave it.
 * When offsets are equal, the new point is on the edge bisector, with length offset/sin(angle/2),
 * but if the offsets are not equal (we allow for because the bevel modifier has edge weights that
 * may lead to different offsets) then the meeting point can be found by intersecting offset lines.
 * If making the meeting point significantly changes the left or right offset from the user spec,
 * record the change in offset_l (or offset_r); later we can tell that a change has happened
 * because the offset will differ from its original value in offset_l_spec (or offset_r_spec).
 *
 * param edges_between: If this is true, there are edges between e1 and e2 in CCW order so they
 * don't share a common face. We want the meeting point to be on an existing face so it
 * should be dropped onto one of the intermediate faces, if possible.
 * param e_in_plane: If we need to drop from the calculated offset lines to one of the faces,
 * we don't want to drop onto the 'in plane' face, so if this is not null skip this edge's faces.
 */
