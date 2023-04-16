/** Main functions for beveling a BMesh (used by the tool and modifier) **/

#include "mem_guardedalloc.h"

#include "types_curveprofile.h"
#include "types_meshdata.h"
#include "types_modifier.h"
#include "types_scene.h"

#include "lib_alloca.h"
#include "lib_array.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_utildefines.h"

#include "dune_curveprofile.h"
#include "dune_customdata.h"
#include "dune_deform.h"
#include "dune_mesh.h"

#include "eigen_capi.h"

#include "mesh.h"
#include "mesh_bevel.h" /* own include */

#include "./intern/mesh_private.h"

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
  MeshVert *v;
  float co[3];
  char _pad[4];
} NewVert;

struct BoundVert;

/* Data for one end of an edge involved in a bevel. */
typedef struct EdgeHalf {
  /** Other EdgeHalves connected to the same BevVert, in CCW order. */
  struct EdgeHalf *next, *prev;
  /** Original mesh edge. */
  MeshEdge *e;
  /** Face between this edge and previous, if any. */
  MeshFace *fprev;
  /** Face between this edge and next, if any. */
  MeshFace *fnext;
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
  MeshVert *v;
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
  MeshEdge **wire_edges;
  /** Mesh structure for replacing vertex. */
  VMesh *vmesh;
} BevVert;

/**
 * Face classification.
 * depends on `F_RECON > F_EDGE > F_VERT`.
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
  Mesh *bm;
  /** Dune units to offset each side of a beveled edge. */
  float offset;
  /** How offset is measured; enum defined in mesh_operators.h. */
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

/* Use the unused _MESH_ELEM_TAG_ALT flag to flag the 'long' loops (parallel to beveled edge)
 * of edge-polygons. */
#define MESH_ELEM_LONG_TAG (1 << 6)

/* These flag values will get set on geom we want to return in 'out' slots for edges and verts. */
#define EDGE_OUT 4
#define VERT_OUT 8

/* If we're called from the modifier, tool flags aren't available,
 * but don't need output geometry. */
static void flag_out_edge(Mesh *mesh, MeshEdge *mesh)
{
  if (mesh->use_toolflags) {
    mesh_op_edge_flag_enable(mesh, mesh, EDGE_OUT);
  }
}

static void flag_out_vert(Mesh *mesh, MeshVert *meshv)
{
  if (mesh->use_toolflags) {
    mesh_op_vert_flag_enable(mesh, meshv, VERT_OUT);
  }
}

static void disable_flag_out_edge(Mesh *mesh, MeshEdge *me)
{
  if (mesh->use_toolflags) {
    mesh_op_edge_flag_disable(mesh, me, EDGE_OUT);
  }
}

static void record_face_kind(BevelParams *bp, MeshFace *f, FKind fkind)
{
  if (bp->face_hash) {
    lib_ghash_insert(bp->face_hash, f, PTR_FROM_INT(fkind));
  }
}

static FKind get_face_kind(BevelParams *bp, MeshFace *f)
{
  void *val = lib_ghash_lookup(bp->face_hash, f);
  return val ? (FKind)PTR_AS_INT(val) : F_ORIG;
}

/* Are d1 and d2 parallel or nearly so? */
static bool nearly_parallel(const float d1[3], const float d2[3])
{
  float ang = angle_v3v3(d1, d2);

  return (fabsf(ang) < BEVEL_EPSILON_ANG) || (fabsf(ang - (float)M_PI) < BEVEL_EPSILON_ANG);
}

/** True if d1 and d2 are parallel or nearly parallel. **/
static bool nearly_parallel_normalized(const float d1[3], const float d2[3])
{
  LIB_ASSERT_UNIT_V3(d1);
  LIB_ASSERT_UNIT_V3(d2);

  const float direction_dot = dot_v3v3(d1, d2);
  return compare_ff(fabsf(direction_dot), 1.0f, BEVEL_EPSILON_ANG_DOT);
}

/**
 * calculate the determinant of a matrix formed by three vectors
 * return dot(a, cross(b, c)) = determinant(a, b, c)
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
  BoundVert *ans = (BoundVert *)lib_memarena_alloc(mem_arena, sizeof(BoundVert));

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

LIB_INLINE void adjust_bound_vert(BoundVert *bv, const float co[3])
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

static void create_mesh_bmvert(Mesh *mesh, VMesh *vm, int i, int j, int k, MeshVert *eg)
{
  NewVert *nv = mesh_vert(vm, i, j, k);
  nv->v = mesh_vert_create(mesh, nv->co, eg, MESH_CREATE_NOP);
  mesh_elem_flag_disable(nv->v, MESH_ELEM_TAG);
  flag_out_vert(mesh, nv->v);
}

static void copy_mesh_vert(VMesh *vm, int ito, int jto, int kto, int ifrom, int jfrom, int kfrom)
{
  NewVert *nvto = mesh_vert(vm, ito, jto, kto);
  NewVert *nvfrom = mesh_vert(vm, ifrom, jfrom, kfrom);
  nvto->v = nvfrom->v;
  copy_v3_v3(nvto->co, nvfrom->co);
}

/* Find the EdgeHalf in bv's array that has edge bme. */
static EdgeHalf *find_edge_half(BevVert *bv, MeshEdge *me)
{
  for (int i = 0; i < bv->edgecount; i++) {
    if (bv->edges[i].e == me) {
      return &bv->edges[i];
    }
  }
  return NULL;
}

/* Find the BevVert corresponding to MeshVert mv. */
static BevVert *find_bevvert(BevelParams *bp, MeshVert *mv)
{
  return lib_ghash_lookup(bp->vert_hash, bmv);
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
    lib_assert(eother != NULL);
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
 * where the next or previous edge in the face must be me2. */
static bool edges_face_connected_at_vert(MeshEdge *me1, MeshEdge *me2)
{
  MeshIter iter;
  MLoop *l;
  MESH_ELEM_ITER (l, &iter, me1, MESH_LOOPS_OF_EDGE) {
    if (l->prev->e == me2 || l->next->e == me2) {
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
static MeshFace *boundvert_rep_face(BoundVert *v, MeshFace **r_fother)
{
  MeshFace *frep;

  MeshFace *frep2 = NULL;
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
static void offset_meet(BevelParams *bp,
                        EdgeHalf *e1,
                        EdgeHalf *e2,
                        BMVert *v,
                        BMFace *f,
                        bool edges_between,
                        float meetco[3],
                        const EdgeHalf *e_in_plane)
{
  /* Get direction vectors for two offset lines. */
  float dir1[3], dir2[3];
  sub_v3_v3v3(dir1, v->co, BM_edge_other_vert(e1->e, v)->co);
  sub_v3_v3v3(dir2, BM_edge_other_vert(e2->e, v)->co, v->co);

  float dir1n[3], dir2p[3];
  if (edges_between) {
    EdgeHalf *e1next = e1->next;
    EdgeHalf *e2prev = e2->prev;
    sub_v3_v3v3(dir1n, BM_edge_other_vert(e1next->e, v)->co, v->co);
    sub_v3_v3v3(dir2p, v->co, BM_edge_other_vert(e2prev->e, v)->co);
  }
  else {
    /* Shut up 'maybe unused' warnings. */
    zero_v3(dir1n);
    zero_v3(dir2p);
  }

  float ang = angle_v3v3(dir1, dir2);
  float norm_perp1[3];
  if (ang < BEVEL_EPSILON_ANG) {
    /* Special case: e1 and e2 are parallel; put offset point perp to both, from v.
     * need to find a suitable plane.
     * This code used to just use offset and dir1, but that makes for visible errors
     * on a circle with > 200 sides, which trips this "nearly perp" code (see T61214).
     * so use the average of the two, and the offset formula for angle bisector.
     * If offsets are different, we're out of luck:
     * Use the max of the two (so get consistent looking results if the same situation
     * arises elsewhere in the object but with opposite roles for e1 and e2. */
    float norm_v[3];
    if (f) {
      copy_v3_v3(norm_v, f->no);
    }
    else {
      /* Get average of face norms of faces between e and e2. */
      int fcount = 0;
      zero_v3(norm_v);
      for (EdgeHalf *eloop = e1; eloop != e2; eloop = eloop->next) {
        if (eloop->fnext != NULL) {
          add_v3_v3(norm_v, eloop->fnext->no);
          fcount++;
        }
      }
      if (fcount == 0) {
        copy_v3_v3(norm_v, v->no);
      }
      else {
        mul_v3_fl(norm_v, 1.0f / fcount);
      }
    }
    add_v3_v3(dir1, dir2);
    cross_v3_v3v3(norm_perp1, dir1, norm_v);
    normalize_v3(norm_perp1);
    float off1a[3];
    copy_v3_v3(off1a, v->co);
    float d = max_ff(e1->offset_r, e2->offset_l);
    d = d / cosf(ang / 2.0f);
    madd_v3_v3fl(off1a, norm_perp1, d);
    copy_v3_v3(meetco, off1a);
  }
  else if (fabsf(ang - (float)M_PI) < BEVEL_EPSILON_ANG) {
    /* Special case: e1 and e2 are antiparallel, so bevel is into a zero-area face.
     * Just make the offset point on the common line, at offset distance from v. */
    float d = max_ff(e1->offset_r, e2->offset_l);
    slide_dist(e2, v, d, meetco);
  }
  else {
    /* Get normal to plane where meet point should be, using cross product instead of f->no
     * in case f is non-planar.
     * Except: sometimes locally there can be a small angle between dir1 and dir2 that leads
     * to a normal that is actually almost perpendicular to the face normal;
     * in this case it looks wrong to use the local (cross-product) normal, so use the face normal
     * if the angle between dir1 and dir2 is smallish.
     * If e1-v-e2 is a reflex angle (viewed from vertex normal side), need to flip.
     * Use f->no to figure out which side to look at angle from, as even if f is non-planar,
     * will be more accurate than vertex normal. */
    float norm_v1[3], norm_v2[3];
    if (f && ang < BEVEL_SMALL_ANG) {
      copy_v3_v3(norm_v1, f->no);
      copy_v3_v3(norm_v2, f->no);
    }
    else if (!edges_between) {
      cross_v3_v3v3(norm_v1, dir2, dir1);
      normalize_v3(norm_v1);
      if (dot_v3v3(norm_v1, f ? f->no : v->no) < 0.0f) {
        negate_v3(norm_v1);
      }
      copy_v3_v3(norm_v2, norm_v1);
    }
    else {
      /* Separate faces; get face norms at corners for each separately. */
      cross_v3_v3v3(norm_v1, dir1n, dir1);
      normalize_v3(norm_v1);
      f = e1->fnext;
      if (dot_v3v3(norm_v1, f ? f->no : v->no) < 0.0f) {
        negate_v3(norm_v1);
      }
      cross_v3_v3v3(norm_v2, dir2, dir2p);
      normalize_v3(norm_v2);
      f = e2->fprev;
      if (dot_v3v3(norm_v2, f ? f->no : v->no) < 0.0f) {
        negate_v3(norm_v2);
      }
    }

    /* Get vectors perp to each edge, perp to norm_v, and pointing into face. */
    float norm_perp2[3];
    cross_v3_v3v3(norm_perp1, dir1, norm_v1);
    cross_v3_v3v3(norm_perp2, dir2, norm_v2);
    normalize_v3(norm_perp1);
    normalize_v3(norm_perp2);

    float off1a[3], off1b[3], off2a[3], off2b[3];
    if (ELEM(bp->offset_type, BEVEL_AMT_PERCENT, BEVEL_AMT_ABSOLUTE)) {
      offset_meet_lines_percent_or_absolute(bp, e1, e2, v, off1a, off1b, off2a, off2b);
    }
    else {
      /* Get points that are offset distances from each line, then another point on each line. */
      copy_v3_v3(off1a, v->co);
      madd_v3_v3fl(off1a, norm_perp1, e1->offset_r);
      add_v3_v3v3(off1b, off1a, dir1);
      copy_v3_v3(off2a, v->co);
      madd_v3_v3fl(off2a, norm_perp2, e2->offset_l);
      add_v3_v3v3(off2b, off2a, dir2);
    }

    /* Intersect the offset lines. */
    float isect2[3];
    int isect_kind = isect_line_line_v3(off1a, off1b, off2a, off2b, meetco, isect2);
    if (isect_kind == 0) {
      /* Lines are collinear: we already tested for this, but this used a different epsilon. */
      copy_v3_v3(meetco, off1a); /* Just to do something. */
    }
    else {
      /* The lines intersect, but is it at a reasonable place?
       * One problem to check: if one of the offsets is 0, then we don't want an intersection
       * that is outside that edge itself. This can happen if angle between them is > 180 degrees,
       * or if the offset amount is > the edge length. */
      BMVert *closer_v;
      if (e1->offset_r == 0.0f && is_outside_edge(e1, meetco, &closer_v)) {
        copy_v3_v3(meetco, closer_v->co);
      }
      if (e2->offset_l == 0.0f && is_outside_edge(e2, meetco, &closer_v)) {
        copy_v3_v3(meetco, closer_v->co);
      }
      if (edges_between && e1->offset_r > 0.0f && e2->offset_l > 0.0f) {
        /* Try to drop meetco to a face between e1 and e2. */
        if (isect_kind == 2) {
          /* Lines didn't meet in 3d: get average of meetco and isect2. */
          mid_v3_v3v3(meetco, meetco, isect2);
        }
        for (EdgeHalf *e = e1; e != e2; e = e->next) {
          BMFace *fnext = e->fnext;
          if (!fnext) {
            continue;
          }
          float plane[4];
          plane_from_point_normal_v3(plane, v->co, fnext->no);
          float dropco[3];
          closest_to_plane_normalized_v3(dropco, plane, meetco);
          /* Don't drop to the faces next to the in plane edge. */
          if (e_in_plane) {
            ang = angle_v3v3(fnext->no, e_in_plane->fnext->no);
            if ((fabsf(ang) < BEVEL_SMALL_ANG) || (fabsf(ang - (float)M_PI) < BEVEL_SMALL_ANG)) {
              continue;
            }
          }
          if (point_between_edges(dropco, v, fnext, e, e->next)) {
            copy_v3_v3(meetco, dropco);
            break;
          }
        }
      }
    }
  }
}

/* This was changed from 0.25f to fix bug T86768.
 * Original bug T44961 remains fixed with this value. */
#define BEVEL_GOOD_ANGLE 0.0001f

/**
 * Calculate the meeting point between e1 and e2 (one of which should have zero offsets),
 * where \a e1 precedes \a e2 in CCW order around their common vertex \a v
 * (viewed from normal side).
 * If \a r_angle is provided, return the angle between \a e and \a meetco in `*r_angle`.
 * If the angle is 0, or it is 180 degrees or larger, there will be no meeting point;
 * return false in that case, else true.
 */
static bool offset_meet_edge(
    EdgeHalf *e1, EdgeHalf *e2, BMVert *v, float meetco[3], float *r_angle)
{
  float dir1[3], dir2[3];
  sub_v3_v3v3(dir1, BM_edge_other_vert(e1->e, v)->co, v->co);
  sub_v3_v3v3(dir2, BM_edge_other_vert(e2->e, v)->co, v->co);
  normalize_v3(dir1);
  normalize_v3(dir2);

  /* Find angle from dir1 to dir2 as viewed from vertex normal side. */
  float ang = angle_normalized_v3v3(dir1, dir2);
  if (fabsf(ang) < BEVEL_GOOD_ANGLE) {
    if (r_angle) {
      *r_angle = 0.0f;
    }
    return false;
  }
  float fno[3];
  cross_v3_v3v3(fno, dir1, dir2);
  if (dot_v3v3(fno, v->no) < 0.0f) {
    ang = 2.0f * (float)M_PI - ang; /* Angle is reflex. */
    if (r_angle) {
      *r_angle = ang;
    }
    return false;
  }
  if (r_angle) {
    *r_angle = ang;
  }

  if (fabsf(ang - (float)M_PI) < BEVEL_GOOD_ANGLE) {
    return false;
  }

  float sinang = sinf(ang);

  copy_v3_v3(meetco, v->co);
  if (e1->offset_r == 0.0f) {
    madd_v3_v3fl(meetco, dir1, e2->offset_l / sinang);
  }
  else {
    madd_v3_v3fl(meetco, dir2, e1->offset_r / sinang);
  }
  return true;
}

/**
 * Return true if it will look good to put the meeting point where offset_on_edge_between
 * would put it. This means that neither side sees a reflex angle.
 */
static bool good_offset_on_edge_between(EdgeHalf *e1, EdgeHalf *e2, EdgeHalf *emid, BMVert *v)
{
  float ang;
  float meet[3];

  return offset_meet_edge(e1, emid, v, meet, &ang) && offset_meet_edge(emid, e2, v, meet, &ang);
}

/**
 * Calculate the best place for a meeting point for the offsets from edges e1 and e2 on the
 * in-between edge emid. Viewed from the vertex normal side, the CCW order of these edges is e1,
 * emid, e2. Return true if we placed meetco as compromise between where two edges met. If we did,
 * put the ratio of sines of angles in *r_sinratio too.
 * However, if the bp->offset_type is BEVEL_AMT_PERCENT or BEVEL_AMT_ABSOLUTE, we just slide
 * along emid by the specified amount.
 */
static bool offset_on_edge_between(BevelParams *bp,
                                   EdgeHalf *e1,
                                   EdgeHalf *e2,
                                   EdgeHalf *emid,
                                   BMVert *v,
                                   float meetco[3],
                                   float *r_sinratio)
{
  bool retval = false;

  BLI_assert(e1->is_bev && e2->is_bev && !emid->is_bev);

  float ang1, ang2;
  float meet1[3], meet2[3];
  bool ok1 = offset_meet_edge(e1, emid, v, meet1, &ang1);
  bool ok2 = offset_meet_edge(emid, e2, v, meet2, &ang2);
  if (ELEM(bp->offset_type, BEVEL_AMT_PERCENT, BEVEL_AMT_ABSOLUTE)) {
    BMVert *v2 = BM_edge_other_vert(emid->e, v);
    if (bp->offset_type == BEVEL_AMT_PERCENT) {
      float wt = 1.0;
      if (bp->use_weights) {
        CustomData *cd = &bp->bm->edata;
        wt = 0.5f * (BM_elem_float_data_get(cd, e1->e, CD_BWEIGHT) +
                     BM_elem_float_data_get(cd, e2->e, CD_BWEIGHT));
      }
      interp_v3_v3v3(meetco, v->co, v2->co, wt * bp->offset / 100.0f);
    }
    else {
      float dir[3];
      sub_v3_v3v3(dir, v2->co, v->co);
      normalize_v3(dir);
      madd_v3_v3v3fl(meetco, v->co, dir, bp->offset);
    }
    if (r_sinratio) {
      *r_sinratio = (ang1 == 0.0f) ? 1.0f : sinf(ang2) / sinf(ang1);
    }
    return true;
  }
  if (ok1 && ok2) {
    mid_v3_v3v3(meetco, meet1, meet2);
    if (r_sinratio) {
      /* ang1 should not be 0, but be paranoid. */
      *r_sinratio = (ang1 == 0.0f) ? 1.0f : sinf(ang2) / sinf(ang1);
    }
    retval = true;
  }
  else if (ok1 && !ok2) {
    copy_v3_v3(meetco, meet1);
  }
  else if (!ok1 && ok2) {
    copy_v3_v3(meetco, meet2);
  }
  else {
    /* Neither offset line met emid.
     * This should only happen if all three lines are on top of each other. */
    slide_dist(emid, v, e1->offset_r, meetco);
  }

  return retval;
}

/* Offset by e->offset in plane with normal plane_no, on left if left==true, else on right.
 * If plane_no is NULL, choose an arbitrary plane different from eh's direction. */
static void offset_in_plane(EdgeHalf *e, const float plane_no[3], bool left, float r_co[3])
{
  BMVert *v = e->is_rev ? e->e->v2 : e->e->v1;

  float dir[3], no[3];
  sub_v3_v3v3(dir, BM_edge_other_vert(e->e, v)->co, v->co);
  normalize_v3(dir);
  if (plane_no) {
    copy_v3_v3(no, plane_no);
  }
  else {
    zero_v3(no);
    if (fabsf(dir[0]) < fabsf(dir[1])) {
      no[0] = 1.0f;
    }
    else {
      no[1] = 1.0f;
    }
  }

  float fdir[3];
  if (left) {
    cross_v3_v3v3(fdir, dir, no);
  }
  else {
    cross_v3_v3v3(fdir, no, dir);
  }
  normalize_v3(fdir);
  copy_v3_v3(r_co, v->co);
  madd_v3_v3fl(r_co, fdir, left ? e->offset_l : e->offset_r);
}

/* Calculate the point on e where line (co_a, co_b) comes closest to and return it in projco. */
static void project_to_edge(const BMEdge *e,
                            const float co_a[3],
                            const float co_b[3],
                            float projco[3])
{
  float otherco[3];
  if (!isect_line_line_v3(e->v1->co, e->v2->co, co_a, co_b, projco, otherco)) {
#ifdef BEVEL_ASSERT_PROJECT
    BLI_assert_msg(0, "project meet failure");
#endif
    copy_v3_v3(projco, e->v1->co);
  }
}

/* If there is a bndv->ebev edge, find the mid control point if necessary.
 * It is the closest point on the beveled edge to the line segment between bndv and bndv->next. */
static void set_profile_params(BevelParams *bp, BevVert *bv, BoundVert *bndv)
{
  bool do_linear_interp = true;
  EdgeHalf *e = bndv->ebev;
  Profile *pro = &bndv->profile;

  float start[3], end[3];
  copy_v3_v3(start, bndv->nv.co);
  copy_v3_v3(end, bndv->next->nv.co);
  if (e) {
    do_linear_interp = false;
    pro->super_r = bp->pro_super_r;
    /* Projection direction is direction of the edge. */
    sub_v3_v3v3(pro->proj_dir, e->e->v1->co, e->e->v2->co);
    if (e->is_rev) {
      negate_v3(pro->proj_dir);
    }
    normalize_v3(pro->proj_dir);
    project_to_edge(e->e, start, end, pro->middle);
    copy_v3_v3(pro->start, start);
    copy_v3_v3(pro->end, end);
    /* Default plane to project onto is the one with triangle start - middle - end in it. */
    float d1[3], d2[3];
    sub_v3_v3v3(d1, pro->middle, start);
    sub_v3_v3v3(d2, pro->middle, end);
    normalize_v3(d1);
    normalize_v3(d2);
    cross_v3_v3v3(pro->plane_no, d1, d2);
    normalize_v3(pro->plane_no);
    if (nearly_parallel(d1, d2)) {
      /* Start - middle - end are collinear.
       * It should be the case that beveled edge is coplanar with two boundary verts.
       * We want to move the profile to that common plane, if possible.
       * That makes the multi-segment bevels curve nicely in that plane, as users expect.
       * The new middle should be either v (when neighbor edges are unbeveled)
       * or the intersection of the offset lines (if they are).
       * If the profile is going to lead into unbeveled edges on each side
       * (that is, both BoundVerts are "on-edge" points on non-beveled edges). */
      copy_v3_v3(pro->middle, bv->v->co);
      if (e->prev->is_bev && e->next->is_bev && bv->selcount >= 3) {
        /* Want mid at the meet point of next and prev offset edges. */
        float d3[3], d4[3], co4[3], meetco[3], isect2[3];
        int isect_kind;

        sub_v3_v3v3(d3, e->prev->e->v1->co, e->prev->e->v2->co);
        sub_v3_v3v3(d4, e->next->e->v1->co, e->next->e->v2->co);
        normalize_v3(d3);
        normalize_v3(d4);
        if (nearly_parallel(d3, d4)) {
          /* Offset lines are collinear - want linear interpolation. */
          mid_v3_v3v3(pro->middle, start, end);
          do_linear_interp = true;
        }
        else {
          float co3[3];
          add_v3_v3v3(co3, start, d3);
          add_v3_v3v3(co4, end, d4);
          isect_kind = isect_line_line_v3(start, co3, end, co4, meetco, isect2);
          if (isect_kind != 0) {
            copy_v3_v3(pro->middle, meetco);
          }
          else {
            /* Offset lines don't intersect - want linear interpolation. */
            mid_v3_v3v3(pro->middle, start, end);
            do_linear_interp = true;
          }
        }
      }
      copy_v3_v3(pro->end, end);
      sub_v3_v3v3(d1, pro->middle, start);
      normalize_v3(d1);
      sub_v3_v3v3(d2, pro->middle, end);
      normalize_v3(d2);
      cross_v3_v3v3(pro->plane_no, d1, d2);
      normalize_v3(pro->plane_no);
      if (nearly_parallel(d1, d2)) {
        /* Whole profile is collinear with edge: just interpolate. */
        do_linear_interp = true;
      }
      else {
        copy_v3_v3(pro->plane_co, bv->v->co);
        copy_v3_v3(pro->proj_dir, pro->plane_no);
      }
    }
    copy_v3_v3(pro->plane_co, start);
  }
  else if (bndv->is_arc_start) {
    /* Assume pro->middle was already set. */
    copy_v3_v3(pro->start, start);
    copy_v3_v3(pro->end, end);
    pro->super_r = PRO_CIRCLE_R;
    zero_v3(pro->plane_co);
    zero_v3(pro->plane_no);
    zero_v3(pro->proj_dir);
    do_linear_interp = false;
  }
  else if (bp->affect_type == BEVEL_AFFECT_VERTICES) {
    copy_v3_v3(pro->start, start);
    copy_v3_v3(pro->middle, bv->v->co);
    copy_v3_v3(pro->end, end);
    pro->super_r = bp->pro_super_r;
    zero_v3(pro->plane_co);
    zero_v3(pro->plane_no);
    zero_v3(pro->proj_dir);
    do_linear_interp = false;
  }

  if (do_linear_interp) {
    pro->super_r = PRO_LINE_R;
    copy_v3_v3(pro->start, start);
    copy_v3_v3(pro->end, end);
    mid_v3_v3v3(pro->middle, start, end);
    /* Won't use projection for this line profile. */
    zero_v3(pro->plane_co);
    zero_v3(pro->plane_no);
    zero_v3(pro->proj_dir);
  }
}

/**
 * Maybe move the profile plane for bndv->ebev to the plane its profile's start, and the
 * original beveled vert, bmv. This will usually be the plane containing its adjacent
 * non-beveled edges, but sometimes the start and the end are not on those edges.
 *
 * Currently just used in #build_boundary_terminal_edge.
 */
static void move_profile_plane(BoundVert *bndv, BMVert *bmvert)
{
  Profile *pro = &bndv->profile;

  /* Only do this if projecting, and start, end, and proj_dir are not coplanar. */
  if (is_zero_v3(pro->proj_dir)) {
    return;
  }

  float d1[3], d2[3];
  sub_v3_v3v3(d1, bmvert->co, pro->start);
  normalize_v3(d1);
  sub_v3_v3v3(d2, bmvert->co, pro->end);
  normalize_v3(d2);
  float no[3], no2[3], no3[3];
  cross_v3_v3v3(no, d1, d2);
  cross_v3_v3v3(no2, d1, pro->proj_dir);
  cross_v3_v3v3(no3, d2, pro->proj_dir);

  if (normalize_v3(no) > BEVEL_EPSILON_BIG && normalize_v3(no2) > BEVEL_EPSILON_BIG &&
      normalize_v3(no3) > BEVEL_EPSILON_BIG) {
    float dot2 = dot_v3v3(no, no2);
    float dot3 = dot_v3v3(no, no3);
    if (fabsf(dot2) < (1 - BEVEL_EPSILON_BIG) && fabsf(dot3) < (1 - BEVEL_EPSILON_BIG)) {
      copy_v3_v3(bndv->profile.plane_no, no);
    }
  }

  /* We've changed the parameters from their defaults, so don't recalculate them later. */
  pro->special_params = true;
}

/**
 * Move the profile plane for the two BoundVerts involved in a weld.
 * We want the plane that is most likely to have the intersections of the
 * two edges' profile projections on it. bndv1 and bndv2 are by construction the
 * intersection points of the outside parts of the profiles.
 * The original vertex should form a third point of the desired plane.
 */
static void move_weld_profile_planes(BevVert *bv, BoundVert *bndv1, BoundVert *bndv2)
{
  /* Only do this if projecting, and d1, d2, and proj_dir are not coplanar. */
  if (is_zero_v3(bndv1->profile.proj_dir) || is_zero_v3(bndv2->profile.proj_dir)) {
    return;
  }
  float d1[3], d2[3], no[3];
  sub_v3_v3v3(d1, bv->v->co, bndv1->nv.co);
  sub_v3_v3v3(d2, bv->v->co, bndv2->nv.co);
  cross_v3_v3v3(no, d1, d2);
  float l1 = normalize_v3(no);

  /* "no" is new normal projection plane, but don't move if it is coplanar with both of the
   * projection directions. */
  float no2[3], no3[3];
  cross_v3_v3v3(no2, d1, bndv1->profile.proj_dir);
  float l2 = normalize_v3(no2);
  cross_v3_v3v3(no3, d2, bndv2->profile.proj_dir);
  float l3 = normalize_v3(no3);
  if (l1 > BEVEL_EPSILON && (l2 > BEVEL_EPSILON || l3 > BEVEL_EPSILON)) {
    float dot1 = fabsf(dot_v3v3(no, no2));
    float dot2 = fabsf(dot_v3v3(no, no3));
    if (fabsf(dot1 - 1.0f) > BEVEL_EPSILON) {
      copy_v3_v3(bndv1->profile.plane_no, no);
    }
    if (fabsf(dot2 - 1.0f) > BEVEL_EPSILON) {
      copy_v3_v3(bndv2->profile.plane_no, no);
    }
  }

  /* We've changed the parameters from their defaults, so don't recalculate them later. */
  bndv1->profile.special_params = true;
  bndv2->profile.special_params = true;
}

/* Return 1 if a and b are in CCW order on the normal side of f,
 * and -1 if they are reversed, and 0 if there is no shared face f. */
static int bev_ccw_test(MeshEdge *a, MeshEdge *b, MeshFace *f)
{
  if (!f) {
    return 0;
  }
  MeshLoop *la = mesh_face_edge_share_loop(f, a);
  MeshLoop *lb = mesh_face_edge_share_loop(f, b);
  if (!la || !lb) {
    return 0;
  }
  return lb->next == la ? 1 : -1;
}

/**
 * Fill matrix r_mat so that a point in the sheared parallelogram with corners
 * va, vmid, vb (and the 4th that is implied by it being a parallelogram)
 * is the result of transforming the unit square by multiplication with r_mat.
 * If it can't be done because the parallelogram is degenerate, return false,
 * else return true.
 * Method:
 * Find vo, the origin of the parallelogram with other three points va, vmid, vb.
 * Also find vd, which is in direction normal to parallelogram and 1 unit away
 * from the origin.
 * The quarter circle in first quadrant of unit square will be mapped to the
 * quadrant of a sheared ellipse in the parallelogram, using a matrix.
 * The matrix mat is calculated to map:
 *    (0,1,0) -> va
 *    (1,1,0) -> vmid
 *    (1,0,0) -> vb
 *    (0,1,1) -> vd
 * We want M to make M*A=B where A has the left side above, as columns
 * and B has the right side as columns - both extended into homogeneous coords.
 * So M = B*(Ainverse).  Doing Ainverse by hand gives the code below.
 */
static bool make_unit_square_map(const float va[3],
                                 const float vmid[3],
                                 const float vb[3],
                                 float r_mat[4][4])
{
  float vb_vmid[3], va_vmid[3];
  sub_v3_v3v3(va_vmid, vmid, va);
  sub_v3_v3v3(vb_vmid, vmid, vb);

  if (is_zero_v3(va_vmid) || is_zero_v3(vb_vmid)) {
    return false;
  }

  if (fabsf(angle_v3v3(va_vmid, vb_vmid) - (float)M_PI) <= BEVEL_EPSILON_ANG) {
    return false;
  }

  float vo[3], vd[3], vddir[3];
  sub_v3_v3v3(vo, va, vb_vmid);
  cross_v3_v3v3(vddir, vb_vmid, va_vmid);
  normalize_v3(vddir);
  add_v3_v3v3(vd, vo, vddir);

  /* The cols of m are: {vmid - va, vmid - vb, vmid + vd - va -vb, va + vb - vmid;
   * Blender transform matrices are stored such that m[i][*] is ith column;
   * the last elements of each col remain as they are in unity matrix. */
  sub_v3_v3v3(&r_mat[0][0], vmid, va);
  r_mat[0][3] = 0.0f;
  sub_v3_v3v3(&r_mat[1][0], vmid, vb);
  r_mat[1][3] = 0.0f;
  add_v3_v3v3(&r_mat[2][0], vmid, vd);
  sub_v3_v3(&r_mat[2][0], va);
  sub_v3_v3(&r_mat[2][0], vb);
  r_mat[2][3] = 0.0f;
  add_v3_v3v3(&r_mat[3][0], va, vb);
  sub_v3_v3(&r_mat[3][0], vmid);
  r_mat[3][3] = 1.0f;

  return true;
}

/**
 * Like make_unit_square_map, but this one makes a matrix that transforms the
 * (1,1,1) corner of a unit cube into an arbitrary corner with corner vert d
 * and verts around it a, b, c (in CCW order, viewed from d normal dir).
 * The matrix mat is calculated to map:
 *    (1,0,0) -> va
 *    (0,1,0) -> vb
 *    (0,0,1) -> vc
 *    (1,1,1) -> vd
 * We want M to make M*A=B where A has the left side above, as columns
 * and B has the right side as columns - both extended into homogeneous coords.
 * So `M = B*(Ainverse)`.  Doing `Ainverse` by hand gives the code below.
 * The cols of M are `1/2{va-vb+vc-vd}`, `1/2{-va+vb-vc+vd}`, `1/2{-va-vb+vc+vd}`,
 * and `1/2{va+vb+vc-vd}`
 * and Dune matrices have cols at m[i][*].
 */
static void make_unit_cube_map(
    const float va[3], const float vb[3], const float vc[3], const float vd[3], float r_mat[4][4])
{
  copy_v3_v3(r_mat[0], va);
  sub_v3_v3(r_mat[0], vb);
  sub_v3_v3(r_mat[0], vc);
  add_v3_v3(r_mat[0], vd);
  mul_v3_fl(r_mat[0], 0.5f);
  r_mat[0][3] = 0.0f;
  copy_v3_v3(r_mat[1], vb);
  sub_v3_v3(r_mat[1], va);
  sub_v3_v3(r_mat[1], vc);
  add_v3_v3(r_mat[1], vd);
  mul_v3_fl(r_mat[1], 0.5f);
  r_mat[1][3] = 0.0f;
  copy_v3_v3(r_mat[2], vc);
  sub_v3_v3(r_mat[2], va);
  sub_v3_v3(r_mat[2], vb);
  add_v3_v3(r_mat[2], vd);
  mul_v3_fl(r_mat[2], 0.5f);
  r_mat[2][3] = 0.0f;
  copy_v3_v3(r_mat[3], va);
  add_v3_v3(r_mat[3], vb);
  add_v3_v3(r_mat[3], vc);
  sub_v3_v3(r_mat[3], vd);
  mul_v3_fl(r_mat[3], 0.5f);
  r_mat[3][3] = 1.0f;
}

/**
 * Get the coordinate on the superellipse (x^r + y^r = 1), at parameter value x
 * (or, if !rbig, mirrored (y=x)-line).
 * rbig should be true if r > 1.0 and false if <= 1.0.
 * Assume r > 0.0.
 */
static double superellipse_co(double x, float r, bool rbig)
{
  lib_assert(r > 0.0f);

  /* If r<1, mirror the superellipse function by (y=x)-line to get a numerically stable range
   * Possible because of symmetry, later mirror back. */
  if (rbig) {
    return pow((1.0 - pow(x, r)), (1.0 / r));
  }
  return 1.0 - pow((1.0 - pow(1.0 - x, r)), (1.0 / r));
}

/**
 * Find the point on given profile at parameter i which goes from 0 to nseg as
 * the profile moves from pro->start to pro->end.
 * We assume that nseg is either the global seg number or a power of 2 less than
 * or equal to the power of 2 >= seg.
 * In the latter case, we subsample the profile for seg_2, which will not necessarily
 * give equal spaced chords, but is in fact more what is desired by the cubic subdivision
 * method used to make the vmesh pattern.
 */
static void get_profile_point(BevelParams *bp, const Profile *pro, int i, int nseg, float r_co[3])
{
  if (bp->seg == 1) {
    if (i == 0) {
      copy_v3_v3(r_co, pro->start);
    }
    else {
      copy_v3_v3(r_co, pro->end);
    }
  }

  else {
    if (nseg == bp->seg) {
      lib_assert(pro->prof_co != NULL);
      copy_v3_v3(r_co, pro->prof_co + 3 * i);
    }
    else {
      lib_assert(is_power_of_2_i(nseg) && nseg <= bp->pro_spacing.seg_2);
      /* Find spacing between subsamples in prof_co_2. */
      int subsample_spacing = bp->pro_spacing.seg_2 / nseg;
      copy_v3_v3(r_co, pro->prof_co_2 + 3 * i * subsample_spacing);
    }
  }
}

/**
 * Helper for calculate_profile that builds the 3D locations for the segments
 * and the higher power of 2 segments.
 */
static void calculate_profile_segments(const Profile *profile,
                                       const float map[4][4],
                                       const bool use_map,
                                       const bool reversed,
                                       const int ns,
                                       const double *xvals,
                                       const double *yvals,
                                       float *r_prof_co)
{
  /* Iterate over the vertices along the boundary arc. */
  for (int k = 0; k <= ns; k++) {
    float co[3];
    if (k == 0) {
      copy_v3_v3(co, profile->start);
    }
    else if (k == ns) {
      copy_v3_v3(co, profile->end);
    }
    else {
      if (use_map) {
        const float p[3] = {
            reversed ? (float)yvals[ns - k] : (float)xvals[k],
            reversed ? (float)xvals[ns - k] : (float)yvals[k],
            0.0f,
        };
        /* Do the 2D->3D transformation of the profile coordinates. */
        mul_v3_m4v3(co, map, p);
      }
      else {
        interp_v3_v3v3(co, profile->start, profile->end, (float)k / (float)ns);
      }
    }
    /* Finish the 2D->3D transformation by projecting onto the final profile plane. */
    float *prof_co_k = r_prof_co + 3 * k;
    if (!is_zero_v3(profile->proj_dir)) {
      float co2[3];
      add_v3_v3v3(co2, co, profile->proj_dir);
      /* pro->plane_co and pro->plane_no are filled in #set_profile_params. */
      if (!isect_line_plane_v3(prof_co_k, co, co2, profile->plane_co, profile->plane_no)) {
        /* Shouldn't happen. */
        copy_v3_v3(prof_co_k, co);
      }
    }
    else {
      copy_v3_v3(prof_co_k, co);
    }
  }
}

/**
 * Calculate the actual coordinate values for bndv's profile.
 * This is only needed if bp->seg > 1.
 * Allocate the space for them if that hasn't been done already.
 * If bp->seg is not a power of 2, also need to calculate
 * the coordinate values for the power of 2 >= bp->seg, because the ADJ pattern needs power-of-2
 * boundaries during construction.
 */
static void calculate_profile(BevelParams *bp, BoundVert *bndv, bool reversed, bool miter)
{
  Profile *pro = &bndv->profile;
  ProfileSpacing *pro_spacing = (miter) ? &bp->pro_spacing_miter : &bp->pro_spacing;

  if (bp->seg == 1) {
    return;
  }

  bool need_2 = bp->seg != bp->pro_spacing.seg_2;
  if (pro->prof_co == NULL) {
    pro->prof_co = (float *)lib_memarena_alloc(bp->mem_arena, sizeof(float[3]) * (bp->seg + 1));
    if (need_2) {
      pro->prof_co_2 = (float *)lib_memarena_alloc(bp->mem_arena,
                                                   sizeof(float[3]) * (bp->pro_spacing.seg_2 + 1));
    }
    else {
      pro->prof_co_2 = pro->prof_co;
    }
  }

  bool use_map;
  float map[4][4];
  if (bp->profile_type == BEVEL_PROFILE_SUPERELLIPSE && pro->super_r == PRO_LINE_R) {
    use_map = false;
  }
  else {
    use_map = make_unit_square_map(pro->start, pro->middle, pro->end, map);
  }

  if (bp->vmesh_method == BEVEL_VMESH_CUTOFF && use_map) {
    /* Calculate the "height" of the profile by putting the (0,0) and (1,1) corners of the
     * un-transformed profile through the 2D->3D map and calculating the distance between them. */
    float bottom_corner[3] = {0.0f, 0.0f, 0.0f};
    mul_v3_m4v3(bottom_corner, map, bottom_corner);
    float top_corner[3] = {1.0f, 1.0f, 0.0f};
    mul_v3_m4v3(top_corner, map, top_corner);

    pro->height = len_v3v3(bottom_corner, top_corner);
  }

  /* Calculate the 3D locations for the profile points */
  calculate_profile_segments(
      pro, map, use_map, reversed, bp->seg, pro_spacing->xvals, pro_spacing->yvals, pro->prof_co);
  /* Also calculate for the seg_2 case if it's needed. */
  if (need_2) {
    calculate_profile_segments(pro,
                               map,
                               use_map,
                               reversed,
                               bp->pro_spacing.seg_2,
                               pro_spacing->xvals_2,
                               pro_spacing->yvals_2,
                               pro->prof_co_2);
  }
}

/**
 * Snap a direction co to a superellipsoid with parameter super_r.
 * For square profiles, midline says whether or not to snap to both planes.
 *
 * Only currently used for the pipe and cube corner special cases.
 */
static void snap_to_superellipsoid(float co[3], const float super_r, bool midline)
{
  float r = super_r;
  if (r == PRO_CIRCLE_R) {
    normalize_v3(co);
    return;
  }

  float a = max_ff(0.0f, co[0]);
  float b = max_ff(0.0f, co[1]);
  float c = max_ff(0.0f, co[2]);
  float x = a;
  float y = b;
  float z = c;
  if (ELEM(r, PRO_SQUARE_R, PRO_SQUARE_IN_R)) {
    /* Will only be called for 2d profile. */
    LIB_assert(fabsf(z) < BEVEL_EPSILON);
    z = 0.0f;
    x = min_ff(1.0f, x);
    y = min_ff(1.0f, y);
    if (r == PRO_SQUARE_R) {
      /* Snap to closer of x==1 and y==1 lines, or maybe both. */
      float dx = 1.0f - x;
      float dy = 1.0f - y;
      if (dx < dy) {
        x = 1.0f;
        y = midline ? 1.0f : y;
      }
      else {
        y = 1.0f;
        x = midline ? 1.0f : x;
      }
    }
    else {
      /* Snap to closer of x==0 and y==0 lines, or maybe both. */
      if (x < y) {
        x = 0.0f;
        y = midline ? 0.0f : y;
      }
      else {
        y = 0.0f;
        x = midline ? 0.0f : x;
      }
    }
  }
  else {
    float rinv = 1.0f / r;
    if (a == 0.0f) {
      if (b == 0.0f) {
        x = 0.0f;
        y = 0.0f;
        z = powf(c, rinv);
      }
      else {
        x = 0.0f;
        y = powf(1.0f / (1.0f + powf(c / b, r)), rinv);
        z = c * y / b;
      }
    }
    else {
      x = powf(1.0f / (1.0f + powf(b / a, r) + powf(c / a, r)), rinv);
      y = b * x / a;
      z = c * x / a;
    }
  }
  co[0] = x;
  co[1] = y;
  co[2] = z;
}

#define BEV_EXTEND_EDGE_DATA_CHECK(eh, flag) (mesh_elem_flag_test(eh->e, flag))

static void check_edge_data_seam_sharp_edges(BevVert *bv, int flag, bool neg)
{
  EdgeHalf *e = &bv->edges[0], *efirst = &bv->edges[0];

  /* First edge with seam or sharp edge data. */
  while ((!neg && !BEV_EXTEND_EDGE_DATA_CHECK(e, flag)) ||
         (neg && BEV_EXTEND_EDGE_DATA_CHECK(e, flag))) {
    e = e->next;
    if (e == efirst) {
      break;
    }
  }

  /* If no such edge found, return. */
  if ((!neg && !BEV_EXTEND_EDGE_DATA_CHECK(e, flag)) ||
      (neg && BEV_EXTEND_EDGE_DATA_CHECK(e, flag))) {
    return;
  }

  /* Set efirst to this first encountered edge. */
  efirst = e;

  do {
    int flag_count = 0;
    EdgeHalf *ne = e->next;

    while (((!neg && !BEV_EXTEND_EDGE_DATA_CHECK(ne, flag)) ||
            (neg && BEV_EXTEND_EDGE_DATA_CHECK(ne, flag))) &&
           ne != efirst) {
      if (ne->is_bev) {
        flag_count++;
      }
      ne = ne->next;
    }
    if (ne == e || (ne == efirst && ((!neg && !BEV_EXTEND_EDGE_DATA_CHECK(efirst, flag)) ||
                                     (neg && BEV_EXTEND_EDGE_DATA_CHECK(efirst, flag))))) {
      break;
    }
    /* Set seam_len / sharp_len of starting edge. */
    if (flag == MESH_ELEM_SEAM) {
      e->rightv->seam_len = flag_count;
    }
    else if (flag == MESH_ELEM_SMOOTH) {
      e->rightv->sharp_len = flag_count;
    }
    e = ne;
  } while (e != efirst);
}

static void bevel_extend_edge_data(BevVert *bv)
{
  VMesh *vm = bv->vmesh;

  if (vm->mesh_kind == M_TRI_FAN) {
    return;
  }

  BoundVert *bcur = bv->vmesh->boundstart, *start = bcur;

  do {
    /* If current boundvert has a seam length > 0 then it has a seam running along its edges. */
    if (bcur->seam_len) {
      if (!bv->vmesh->boundstart->seam_len && start == bv->vmesh->boundstart) {
        start = bcur; /* Set start to first boundvert with seam_len > 0. */
      }

      /* Now for all the mesh_verts starting at current index and ending at idxlen
       * we go through outermost ring and through all its segments and add seams
       * for those edges. */
      int idxlen = bcur->index + bcur->seam_len;
      for (int i = bcur->index; i < idxlen; i++) {
        MeshVert *v1 = mesh_vert(vm, i % vm->count, 0, 0)->v, *v2;
        MeshEdge *e;
        for (int k = 1; k < vm->seg; k++) {
          v2 = mesh_vert(vm, i % vm->count, 0, k)->v;

          /* Here v1 & v2 are current and next BMverts,
           * we find common edge and set its edge data. */
          e = v1->e;
          while (e->v1 != v2 && e->v2 != v2) {
            if (e->v1 == v1) {
              e = e->v1_disk_link.next;
            }
            else {
              e = e->v2_disk_link.next;
            }
          }
          mesh_elem_flag_set(e, MESH_ELEM_SEAM, true);
          v1 = v2;
        }
        MeshVert *v3 = mesh_vert(vm, (i + 1) % vm->count, 0, 0)->v;
        e = v1->e; /* Do same as above for first and last vert. */
        while (e->v1 != v3 && e->v2 != v3) {
          if (e->v1 == v1) {
            e = e->v1_disk_link.next;
          }
          else {
            e = e->v2_disk_link.next;
          }
        }
        mesh_elem_flag_set(e, MESH_ELEM_SEAM, true);
        bcur = bcur->next;
      }
    }
    else {
      bcur = bcur->next;
    }
  } while (bcur != start);

  bcur = bv->vmesh->boundstart;
  start = bcur;
  do {
    if (bcur->sharp_len) {
      if (!bv->vmesh->boundstart->sharp_len && start == bv->vmesh->boundstart) {
        start = bcur;
      }

      int idxlen = bcur->index + bcur->sharp_len;
      for (int i = bcur->index; i < idxlen; i++) {
        BMVert *v1 = mesh_vert(vm, i % vm->count, 0, 0)->v, *v2;
        BMEdge *e;
        for (int k = 1; k < vm->seg; k++) {
          v2 = mesh_vert(vm, i % vm->count, 0, k)->v;

          e = v1->e;
          while (e->v1 != v2 && e->v2 != v2) {
            if (e->v1 == v1) {
              e = e->v1_disk_link.next;
            }
            else {
              e = e->v2_disk_link.next;
            }
          }
          BM_elem_flag_set(e, BM_ELEM_SMOOTH, false);
          v1 = v2;
        }
        BMVert *v3 = mesh_vert(vm, (i + 1) % vm->count, 0, 0)->v;
        e = v1->e;
        while (e->v1 != v3 && e->v2 != v3) {
          if (e->v1 == v1) {
            e = e->v1_disk_link.next;
          }
          else {
            e = e->v2_disk_link.next;
          }
        }
        BM_elem_flag_set(e, BM_ELEM_SMOOTH, false);
        bcur = bcur->next;
      }
    }
    else {
      bcur = bcur->next;
    }
  } while (bcur != start);
}

/* Mark edges as sharp if they are between a smooth reconstructed face and a new face. */
static void bevel_edges_sharp_boundary(BMesh *bm, BevelParams *bp)
{
  BMIter fiter;
  BMFace *f;
  BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
    if (!BM_elem_flag_test(f, BM_ELEM_SMOOTH)) {
      continue;
    }
    if (get_face_kind(bp, f) != F_RECON) {
      continue;
    }
    BMIter liter;
    BMLoop *l;
    BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
      /* Cases we care about will have exactly one adjacent face. */
      BMLoop *lother = l->radial_next;
      BMFace *fother = lother->f;
      if (lother != l && fother) {
        FKind fkind = get_face_kind(bp, lother->f);
        if (ELEM(fkind, F_EDGE, F_VERT)) {
          BM_elem_flag_disable(l->e, BM_ELEM_SMOOTH);
        }
      }
    }
  }
}

/**
 * \brief Harden normals for bevel.
 *
 * The desired effect is that the newly created #F_EDGE and #F_VERT faces appear smoothly shaded
 * with the normals at the boundaries with #F_RECON faces matching those recon faces.
 * And at boundaries between #F_EDGE and #F_VERT faces, the normals should match the #F_EDGE ones.
 * Assumes custom loop normals are in use.
 */
static void bevel_harden_normals(BevelParams *bp, BMesh *bm)
{
  if (bp->offset == 0.0 || !bp->harden_normals) {
    return;
  }

  /* Recalculate all face and vertex normals. Side effect: ensures vertex, edge, face indices. */
  /* I suspect this is not necessary. TODO: test that guess. */
  BM_mesh_normals_update(bm);

  int cd_clnors_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);

  /* If there is not already a custom split normal layer then making one (with BM_lnorspace_update)
   * will not respect the autosmooth angle between smooth faces. To get that to happen, we have
   * to mark the sharpen the edges that are only sharp because of the angle test -- otherwise would
   * be smooth. */
  if (cd_clnors_offset == -1) {
    BM_edges_sharp_from_angle_set(bm, bp->smoothresh);
    bevel_edges_sharp_boundary(bm, bp);
  }

  /* Ensure that bm->lnor_spacearr has properly stored loop normals.
   * Side effect: ensures loop indices. */
  BM_lnorspace_update(bm);

  if (cd_clnors_offset == -1) {
    cd_clnors_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);
  }

  BMIter fiter;
  BMFace *f;
  BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
    FKind fkind = get_face_kind(bp, f);
    if (ELEM(fkind, F_ORIG, F_RECON)) {
      continue;
    }
    BMIter liter;
    BMLoop *l;
    BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
      BMEdge *estep = l->prev->e; /* Causes CW walk around l->v fan. */
      BMLoop *lprev = BM_vert_step_fan_loop(l, &estep);
      estep = l->e; /* Causes CCW walk around l->v fan. */
      BMLoop *lnext = BM_vert_step_fan_loop(l, &estep);
      FKind fprevkind = lprev ? get_face_kind(bp, lprev->f) : F_NONE;
      FKind fnextkind = lnext ? get_face_kind(bp, lnext->f) : F_NONE;

      float norm[3];
      float *pnorm = NULL;
      if (fkind == F_EDGE) {
        if (fprevkind == F_EDGE && BM_elem_flag_test(l, BM_ELEM_LONG_TAG)) {
          add_v3_v3v3(norm, f->no, lprev->f->no);
          pnorm = norm;
        }
        else if (fnextkind == F_EDGE && BM_elem_flag_test(lnext, BM_ELEM_LONG_TAG)) {
          add_v3_v3v3(norm, f->no, lnext->f->no);
          pnorm = norm;
        }
        else if (fprevkind == F_RECON && BM_elem_flag_test(l, BM_ELEM_LONG_TAG)) {
          pnorm = lprev->f->no;
        }
        else if (fnextkind == F_RECON && BM_elem_flag_test(l->prev, BM_ELEM_LONG_TAG)) {
          pnorm = lnext->f->no;
        }
        else {
          // printf("unexpected harden case (edge)\n");
        }
      }
      else if (fkind == F_VERT) {
        if (fprevkind == F_VERT && fnextkind == F_VERT) {
          pnorm = l->v->no;
        }
        else if (fprevkind == F_RECON) {
          pnorm = lprev->f->no;
        }
        else if (fnextkind == F_RECON) {
          pnorm = lnext->f->no;
        }
        else {
          BMLoop *lprevprev, *lnextnext;
          if (lprev) {
            estep = lprev->prev->e;
            lprevprev = BM_vert_step_fan_loop(lprev, &estep);
          }
          else {
            lprevprev = NULL;
          }
          if (lnext) {
            estep = lnext->e;
            lnextnext = BM_vert_step_fan_loop(lnext, &estep);
          }
          else {
            lnextnext = NULL;
          }
          FKind fprevprevkind = lprevprev ? get_face_kind(bp, lprevprev->f) : F_NONE;
          FKind fnextnextkind = lnextnext ? get_face_kind(bp, lnextnext->f) : F_NONE;
          if (fprevkind == F_EDGE && fprevprevkind == F_RECON) {
            pnorm = lprevprev->f->no;
          }
          else if (fprevkind == F_EDGE && fnextkind == F_VERT && fprevprevkind == F_EDGE) {
            add_v3_v3v3(norm, lprev->f->no, lprevprev->f->no);
            pnorm = norm;
          }
          else if (fnextkind == F_EDGE && fprevkind == F_VERT && fnextnextkind == F_EDGE) {
            add_v3_v3v3(norm, lnext->f->no, lnextnext->f->no);
            pnorm = norm;
          }
          else {
            // printf("unexpected harden case (vert)\n");
          }
        }
      }
      if (pnorm) {
        if (pnorm == norm) {
          normalize_v3(norm);
        }
        int l_index = BM_elem_index_get(l);
        short *clnors = BM_ELEM_CD_GET_VOID_P(l, cd_clnors_offset);
        BKE_lnor_space_custom_normal_to_data(bm->lnor_spacearr->lspacearr[l_index], pnorm, clnors);
      }
    }
  }
}

static void bevel_set_weighted_normal_face_strength(BMesh *bm, BevelParams *bp)
{
  const int mode = bp->face_strength_mode;
  const char *wn_layer_id = MOD_WEIGHTEDNORMALS_FACEWEIGHT_CDLAYER_ID;
  int cd_prop_int_idx = CustomData_get_named_layer_index(&bm->pdata, CD_PROP_INT32, wn_layer_id);

  if (cd_prop_int_idx == -1) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_INT32, wn_layer_id);
    cd_prop_int_idx = CustomData_get_named_layer_index(&bm->pdata, CD_PROP_INT32, wn_layer_id);
  }
  cd_prop_int_idx -= CustomData_get_layer_index(&bm->pdata, CD_PROP_INT32);
  const int cd_prop_int_offset = CustomData_get_n_offset(
      &bm->pdata, CD_PROP_INT32, cd_prop_int_idx);

  BMIter fiter;
  BMFace *f;
  BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
    FKind fkind = get_face_kind(bp, f);
    bool do_set_strength = true;
    int strength;
    switch (fkind) {
      case F_VERT:
        strength = FACE_STRENGTH_WEAK;
        do_set_strength = (mode >= BEVEL_FACE_STRENGTH_NEW);
        break;
      case F_EDGE:
        strength = FACE_STRENGTH_MEDIUM;
        do_set_strength = (mode >= BEVEL_FACE_STRENGTH_NEW);
        break;
      case F_RECON:
        strength = FACE_STRENGTH_STRONG;
        do_set_strength = (mode >= BEVEL_FACE_STRENGTH_AFFECTED);
        break;
      case F_ORIG:
        strength = FACE_STRENGTH_STRONG;
        do_set_strength = (mode == BEVEL_FACE_STRENGTH_ALL);
        break;
      default:
        do_set_strength = false;
    }
    if (do_set_strength) {
      int *strength_ptr = BM_ELEM_CD_GET_VOID_P(f, cd_prop_int_offset);
      *strength_ptr = strength;
    }
  }
}

/* Set the any_seam property for a BevVert and all its BoundVerts. */
static void set_bound_vert_seams(BevVert *bv, bool mark_seam, bool mark_sharp)
{
  bv->any_seam = false;
  BoundVert *v = bv->vmesh->boundstart;
  do {
    v->any_seam = false;
    for (EdgeHalf *e = v->efirst; e; e = e->next) {
      v->any_seam |= e->is_seam;
      if (e == v->elast) {
        break;
      }
    }
    bv->any_seam |= v->any_seam;
  } while ((v = v->next) != bv->vmesh->boundstart);

  if (mark_seam) {
    check_edge_data_seam_sharp_edges(bv, BM_ELEM_SEAM, false);
  }
  if (mark_sharp) {
    check_edge_data_seam_sharp_edges(bv, BM_ELEM_SMOOTH, true);
  }
}

static int count_bound_vert_seams(BevVert *bv)
{
  if (!bv->any_seam) {
    return 0;
  }

  int ans = 0;
  for (int i = 0; i < bv->edgecount; i++) {
    if (bv->edges[i].is_seam) {
      ans++;
    }
  }
  return ans;
}

/* Is e between two faces with a 180 degree angle between their normals? */
static bool eh_on_plane(EdgeHalf *e)
{
  if (e->fprev && e->fnext) {
    float dot = dot_v3v3(e->fprev->no, e->fnext->no);
    if (fabsf(dot + 1.0f) <= BEVEL_EPSILON_BIG || fabsf(dot - 1.0f) <= BEVEL_EPSILON_BIG) {
      return true;
    }
  }
  return false;
}

/**
 * Calculate the profiles for all the BoundVerts of VMesh vm.
 *
 * \note This should only be called once for each BevVert, after all changes have been made to the
 * profile's parameters.
 */
static void calculate_vm_profiles(BevelParams *bp, BevVert *bv, VMesh *vm)
{
  BoundVert *bndv = vm->boundstart;
  do {
    /* In special cases the params will have already been set. */
    if (!bndv->profile.special_params) {
      set_profile_params(bp, bv, bndv);
    }
    bool miter_profile = false;
    bool reverse_profile = false;
    if (bp->profile_type == BEVEL_PROFILE_CUSTOM) {
      /* Use the miter profile spacing struct if the default is filled with the custom profile. */
      miter_profile = (bndv->is_arc_start || bndv->is_patch_start);
      /* Don't bother reversing the profile if it's a miter profile */
      reverse_profile = !bndv->is_profile_start && !miter_profile;
    }
    calculate_profile(bp, bndv, reverse_profile, miter_profile);
  } while ((bndv = bndv->next) != vm->boundstart);
}

/* Implements build_boundary for the vertex-only case. */
static void build_boundary_vertex_only(BevelParams *bp, BevVert *bv, bool construct)
{
  VMesh *vm = bv->vmesh;

  BLI_assert(bp->affect_type == BEVEL_AFFECT_VERTICES);

  EdgeHalf *efirst = &bv->edges[0];
  EdgeHalf *e = efirst;
  do {
    float co[3];
    slide_dist(e, bv->v, e->offset_l, co);
    if (construct) {
      BoundVert *v = add_new_bound_vert(bp->mem_arena, vm, co);
      v->efirst = v->elast = e;
      e->leftv = e->rightv = v;
    }
    else {
      adjust_bound_vert(e->leftv, co);
    }
  } while ((e = e->next) != efirst);

  if (construct) {
    set_bound_vert_seams(bv, bp->mark_seam, bp->mark_sharp);
    if (vm->count == 2) {
      vm->mesh_kind = M_NONE;
    }
    else if (bp->seg == 1) {
      vm->mesh_kind = M_POLY;
    }
    else {
      vm->mesh_kind = M_ADJ;
    }
  }
}

/**
 * Special case of build_boundary when a single edge is beveled.
 * The 'width adjust' part of build_boundary has been done already,
 * and \a efirst is the first beveled edge at vertex \a bv.
 */
static void build_boundary_terminal_edge(BevelParams *bp,
                                         BevVert *bv,
                                         EdgeHalf *efirst,
                                         const bool construct)
{
  MemArena *mem_arena = bp->mem_arena;
  VMesh *vm = bv->vmesh;

  EdgeHalf *e = efirst;
  float co[3];
  if (bv->edgecount == 2) {
    /* Only 2 edges in, so terminate the edge with an artificial vertex on the unbeveled edge.
     * If the offset type is BEVEL_AMT_PERCENT or BEVEL_AMT_ABSOLUTE, what to do is a bit
     * undefined (there aren't two "legs"), so just let the code do what it does. */
    const float *no = e->fprev ? e->fprev->no : (e->fnext ? e->fnext->no : NULL);
    offset_in_plane(e, no, true, co);
    if (construct) {
      BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
      bndv->efirst = bndv->elast = bndv->ebev = e;
      e->leftv = bndv;
    }
    else {
      adjust_bound_vert(e->leftv, co);
    }
    no = e->fnext ? e->fnext->no : (e->fprev ? e->fprev->no : NULL);
    offset_in_plane(e, no, false, co);
    if (construct) {
      BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
      bndv->efirst = bndv->elast = e;
      e->rightv = bndv;
    }
    else {
      adjust_bound_vert(e->rightv, co);
    }
    /* Make artificial extra point along unbeveled edge, and form triangle. */
    slide_dist(e->next, bv->v, e->offset_l, co);
    if (construct) {
      BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
      bndv->efirst = bndv->elast = e->next;
      e->next->leftv = e->next->rightv = bndv;
      set_bound_vert_seams(bv, bp->mark_seam, bp->mark_sharp);
    }
    else {
      adjust_bound_vert(e->next->leftv, co);
    }
  }
  else {
    /* More than 2 edges in. Put on-edge verts on all the other edges and join with the beveled
     * edge to make a poly or adj mesh, because e->prev has offset 0, offset_meet will put co on
     * that edge. */
    /* TODO: should do something else if angle between e and e->prev > 180 */
    bool leg_slide = bp->offset_type == BEVEL_AMT_PERCENT || bp->offset_type == BEVEL_AMT_ABSOLUTE;
    if (leg_slide) {
      slide_dist(e->prev, bv->v, e->offset_l, co);
    }
    else {
      offset_meet(bp, e->prev, e, bv->v, e->fprev, false, co, NULL);
    }
    if (construct) {
      BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
      bndv->efirst = e->prev;
      bndv->elast = bndv->ebev = e;
      e->leftv = bndv;
      e->prev->leftv = e->prev->rightv = bndv;
    }
    else {
      adjust_bound_vert(e->leftv, co);
    }
    e = e->next;
    if (leg_slide) {
      slide_dist(e, bv->v, e->prev->offset_r, co);
    }
    else {
      offset_meet(bp, e->prev, e, bv->v, e->fprev, false, co, NULL);
    }
    if (construct) {
      BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
      bndv->efirst = e->prev;
      bndv->elast = e;
      e->leftv = e->rightv = bndv;
      e->prev->rightv = bndv;
    }
    else {
      adjust_bound_vert(e->leftv, co);
    }
    /* For the edges not adjacent to the beveled edge, slide the bevel amount along. */
    float d = efirst->offset_l_spec;
    if (bp->profile_type == BEVEL_PROFILE_CUSTOM || bp->profile < 0.25f) {
      d *= sqrtf(2.0f); /* Need to go further along the edge to make room for full profile area. */
    }
    for (e = e->next; e->next != efirst; e = e->next) {
      slide_dist(e, bv->v, d, co);
      if (construct) {
        BoundVert *bndv = add_new_bound_vert(mem_arena, vm, co);
        bndv->efirst = bndv->elast = e;
        e->leftv = e->rightv = bndv;
      }
      else {
        adjust_bound_vert(e->leftv, co);
      }
    }
  }

  if (bv->edgecount >= 3) {
    /* Special case: snap profile to plane of adjacent two edges. */
    BoundVert *bndv = vm->boundstart;
    BLI_assert(bndv->ebev != NULL);
    set_profile_params(bp, bv, bndv);
    move_profile_plane(bndv, bv->v);
  }

  if (construct) {
    set_bound_vert_seams(bv, bp->mark_seam, bp->mark_sharp);

    if (vm->count == 2 && bv->edgecount == 3) {
      vm->mesh_kind = M_NONE;
    }
    else if (vm->count == 3) {
      bool use_tri_fan = true;
      if (bp->profile_type == BEVEL_PROFILE_CUSTOM) {
        /* Prevent overhanging edges: use M_POLY if the extra point is planar with the profile. */
        BoundVert *bndv = efirst->leftv;
        float profile_plane[4];
        plane_from_point_normal_v3(profile_plane, bndv->profile.plane_co, bndv->profile.plane_no);
        bndv = efirst->rightv->next; /* The added boundvert placed along the non-adjacent edge. */
        if (dist_squared_to_plane_v3(bndv->nv.co, profile_plane) < BEVEL_EPSILON_BIG) {
          use_tri_fan = false;
        }
      }
      vm->mesh_kind = (use_tri_fan) ? M_TRI_FAN : M_POLY;
    }
    else {
      vm->mesh_kind = M_POLY;
    }
  }
}

/* Helper for build_boundary to handle special miters. */
static void adjust_miter_coords(BevelParams *bp, BevVert *bv, EdgeHalf *emiter)
{
  int miter_outer = bp->miter_outer;

  BoundVert *v1 = emiter->rightv;
  BoundVert *v2, *v3;
  if (miter_outer == BEVEL_MITER_PATCH) {
    v2 = v1->next;
    v3 = v2->next;
  }
  else {
    BLI_assert(miter_outer == BEVEL_MITER_ARC);
    v2 = NULL;
    v3 = v1->next;
  }
  BoundVert *v1prev = v1->prev;
  BoundVert *v3next = v3->next;
  float co2[3];
  copy_v3_v3(co2, v1->nv.co);
  if (v1->is_arc_start) {
    copy_v3_v3(v1->profile.middle, co2);
  }

  /* co1 is intersection of line through co2 in dir of emiter->e
   * and plane with normal the dir of emiter->e and through v1prev. */
  float co1[3], edge_dir[3], line_p[3];
  BMVert *vother = BM_edge_other_vert(emiter->e, bv->v);
  sub_v3_v3v3(edge_dir, bv->v->co, vother->co);
  normalize_v3(edge_dir);
  float d = bp->offset / (bp->seg / 2.0f); /* A fallback amount to move. */
  madd_v3_v3v3fl(line_p, co2, edge_dir, d);
  if (!isect_line_plane_v3(co1, co2, line_p, v1prev->nv.co, edge_dir)) {
    copy_v3_v3(co1, line_p);
  }
  adjust_bound_vert(v1, co1);

  /* co3 is similar, but plane is through v3next and line is other side of miter edge. */
  float co3[3];
  EdgeHalf *emiter_other = v3->elast;
  vother = BM_edge_other_vert(emiter_other->e, bv->v);
  sub_v3_v3v3(edge_dir, bv->v->co, vother->co);
  normalize_v3(edge_dir);
  madd_v3_v3v3fl(line_p, co2, edge_dir, d);
  if (!isect_line_plane_v3(co3, co2, line_p, v3next->nv.co, edge_dir)) {
    copy_v3_v3(co1, line_p);
  }
  adjust_bound_vert(v3, co3);
}

static void adjust_miter_inner_coords(BevelParams *bp, BevVert *bv, EdgeHalf *emiter)
{
  BoundVert *vstart = bv->vmesh->boundstart;
  BoundVert *v = vstart;
  do {
    if (v->is_arc_start) {
      BoundVert *v3 = v->next;
      EdgeHalf *e = v->efirst;
      if (e != emiter) {
        float edge_dir[3], co[3];
        copy_v3_v3(co, v->nv.co);
        BMVert *vother = BM_edge_other_vert(e->e, bv->v);
        sub_v3_v3v3(edge_dir, vother->co, bv->v->co);
        normalize_v3(edge_dir);
        madd_v3_v3v3fl(v->nv.co, co, edge_dir, bp->spread);
        e = v3->elast;
        vother = BM_edge_other_vert(e->e, bv->v);
        sub_v3_v3v3(edge_dir, vother->co, bv->v->co);
        normalize_v3(edge_dir);
        madd_v3_v3v3fl(v3->nv.co, co, edge_dir, bp->spread);
      }
      v = v3->next;
    }
    else {
      v = v->next;
    }
  } while (v != vstart);
}

/**
 * Make a circular list of BoundVerts for bv, each of which has the coordinates of a vertex on the
 * boundary of the beveled vertex bv->v. This may adjust some EdgeHalf widths, and there might have
 * to be a subsequent pass to make the widths as consistent as possible.
 * Doesn't make the actual BMVerts.
 *
 * For a width consistency pass, we just recalculate the coordinates of the #BoundVerts. If the
 * other ends have been (re)built already, then we copy the offsets from there to match, else we
 * use the ideal (user-specified) widths.
 *
 * \param construct: The first time through, construct will be true and we are making the
 * #BoundVerts and setting up the #BoundVert and #EdgeHalf pointers appropriately.
 * Also, if construct, decide on the mesh pattern that will be used inside the boundary.
 */
static void build_boundary(BevelParams *bp, BevVert *bv, bool construct)
{
  MemArena *mem_arena = bp->mem_arena;

  /* Current bevel does nothing if only one edge into a vertex. */
  if (bv->edgecount <= 1) {
    return;
  }

  if (bp->affect_type == BEVEL_AFFECT_VERTICES) {
    build_boundary_vertex_only(bp, bv, construct);
    return;
  }

  VMesh *vm = bv->vmesh;

  /* Find a beveled edge to be efirst. */
  EdgeHalf *efirst = next_bev(bv, NULL);
  BLI_assert(efirst->is_bev);

  if (bv->selcount == 1) {
    /* Special case: only one beveled edge in. */
    build_boundary_terminal_edge(bp, bv, efirst, construct);
    return;
  }

  /* Special miters outside only for 3 or more beveled edges. */
  int miter_outer = (bv->selcount >= 3) ? bp->miter_outer : BEVEL_MITER_SHARP;
  int miter_inner = bp->miter_inner;

  /* Keep track of the first beveled edge of an outside miter (there can be at most 1 per bv). */
  EdgeHalf *emiter = NULL;

  /* There is more than one beveled edge.
   * We make BoundVerts to connect the sides of the beveled edges.
   * Non-beveled edges in between will just join to the appropriate juncture point. */
  EdgeHalf *e = efirst;
  do {
    BLI_assert(e->is_bev);
    EdgeHalf *eon = NULL;
    /* Make the BoundVert for the right side of e; the other side will be made when the beveled
     * edge to the left of e is handled.
     * Analyze edges until next beveled edge: They are either "in plane" (preceding and subsequent
     * faces are coplanar) or not. The "non-in-plane" edges affect the silhouette and we prefer to
     * slide along one of those if possible. */
    int in_plane = 0; /* Counts of in-plane / not-in-plane. */
    int not_in_plane = 0;
    EdgeHalf *enip = NULL; /* Representatives of each type. */
    EdgeHalf *eip = NULL;
    EdgeHalf *e2;
    for (e2 = e->next; !e2->is_bev; e2 = e2->next) {
      if (eh_on_plane(e2)) {
        in_plane++;
        eip = e2;
      }
      else {
        not_in_plane++;
        enip = e2;
      }
    }

    float r, co[3];
    if (in_plane == 0 && not_in_plane == 0) {
      offset_meet(bp, e, e2, bv->v, e->fnext, false, co, NULL);
    }
    else if (not_in_plane > 0) {
      if (bp->loop_slide && not_in_plane == 1 && good_offset_on_edge_between(e, e2, enip, bv->v)) {
        if (offset_on_edge_between(bp, e, e2, enip, bv->v, co, &r)) {
          eon = enip;
        }
      }
      else {
        offset_meet(bp, e, e2, bv->v, NULL, true, co, eip);
      }
    }
    else {
      /* n_in_plane > 0 and n_not_in_plane == 0. */
      if (bp->loop_slide && in_plane == 1 && good_offset_on_edge_between(e, e2, eip, bv->v)) {
        if (offset_on_edge_between(bp, e, e2, eip, bv->v, co, &r)) {
          eon = eip;
        }
      }
      else {
        /* Since all edges between e and e2 are in the same plane, it is OK
         * to treat this like the case where there are no edges between. */
        offset_meet(bp, e, e2, bv->v, e->fnext, false, co, NULL);
      }
    }

    if (construct) {
      BoundVert *v = add_new_bound_vert(mem_arena, vm, co);
      v->efirst = e;
      v->elast = e2;
      v->ebev = e2;
      v->eon = eon;
      if (eon) {
        v->sinratio = r;
      }
      e->rightv = v;
      e2->leftv = v;
      for (EdgeHalf *e3 = e->next; e3 != e2; e3 = e3->next) {
        e3->leftv = e3->rightv = v;
      }
      AngleKind ang_kind = edges_angle_kind(e, e2, bv->v);

      /* Are we doing special mitering?
       * There can only be one outer reflex angle, so only one outer miter,
       * and emiter will be set to the first edge of such an edge.
       * A miter kind of BEVEL_MITER_SHARP means no special miter */
      if ((miter_outer != BEVEL_MITER_SHARP && !emiter && ang_kind == ANGLE_LARGER) ||
          (miter_inner != BEVEL_MITER_SHARP && ang_kind == ANGLE_SMALLER)) {
        if (ang_kind == ANGLE_LARGER) {
          emiter = e;
        }
        /* Make one or two more boundverts; for now all will have same co. */
        BoundVert *v1 = v;
        v1->ebev = NULL;
        BoundVert *v2;
        if (ang_kind == ANGLE_LARGER && miter_outer == BEVEL_MITER_PATCH) {
          v2 = add_new_bound_vert(mem_arena, vm, co);
        }
        else {
          v2 = NULL;
        }
        BoundVert *v3 = add_new_bound_vert(mem_arena, vm, co);
        v3->ebev = e2;
        v3->efirst = e2;
        v3->elast = e2;
        v3->eon = NULL;
        e2->leftv = v3;
        if (ang_kind == ANGLE_LARGER && miter_outer == BEVEL_MITER_PATCH) {
          v1->is_patch_start = true;
          v2->eon = v1->eon;
          v2->sinratio = v1->sinratio;
          v2->ebev = NULL;
          v1->eon = NULL;
          v1->sinratio = 1.0f;
          v1->elast = e;
          if (e->next == e2) {
            v2->efirst = NULL;
            v2->elast = NULL;
          }
          else {
            v2->efirst = e->next;
            for (EdgeHalf *e3 = e->next; e3 != e2; e3 = e3->next) {
              e3->leftv = e3->rightv = v2;
              v2->elast = e3;
            }
          }
        }
        else {
          v1->is_arc_start = true;
          copy_v3_v3(v1->profile.middle, co);
          if (e->next == e2) {
            v1->elast = v1->efirst;
          }
          else {
            int between = in_plane + not_in_plane;
            int bet2 = between / 2;
            bool betodd = (between % 2) == 1;
            int i = 0;
            /* Put first half of in-between edges at index 0, second half at index bp->seg.
             * If between is odd, put middle one at mid-index. */
            for (EdgeHalf *e3 = e->next; e3 != e2; e3 = e3->next) {
              v1->elast = e3;
              if (i < bet2) {
                e3->profile_index = 0;
              }
              else if (betodd && i == bet2) {
                e3->profile_index = bp->seg / 2;
              }
              else {
                e3->profile_index = bp->seg;
              }
              i++;
            }
          }
        }
      }
    }
    else { /* construct == false. */
      AngleKind ang_kind = edges_angle_kind(e, e2, bv->v);
      if ((miter_outer != BEVEL_MITER_SHARP && !emiter && ang_kind == ANGLE_LARGER) ||
          (miter_inner != BEVEL_MITER_SHARP && ang_kind == ANGLE_SMALLER)) {
        if (ang_kind == ANGLE_LARGER) {
          emiter = e;
        }
        BoundVert *v1 = e->rightv;
        BoundVert *v2;
        BoundVert *v3;
        if (ang_kind == ANGLE_LARGER && miter_outer == BEVEL_MITER_PATCH) {
          v2 = v1->next;
          v3 = v2->next;
        }
        else {
          v2 = NULL;
          v3 = v1->next;
        }
        adjust_bound_vert(v1, co);
        if (v2) {
          adjust_bound_vert(v2, co);
        }
        adjust_bound_vert(v3, co);
      }
      else {
        adjust_bound_vert(e->rightv, co);
      }
    }
    e = e2;
  } while (e != efirst);

  if (miter_inner != BEVEL_MITER_SHARP) {
    adjust_miter_inner_coords(bp, bv, emiter);
  }
  if (emiter) {
    adjust_miter_coords(bp, bv, emiter);
  }

  if (construct) {
    set_bound_vert_seams(bv, bp->mark_seam, bp->mark_sharp);

    if (vm->count == 2) {
      vm->mesh_kind = M_NONE;
    }
    else if (efirst->seg == 1) {
      vm->mesh_kind = M_POLY;
    }
    else {
      switch (bp->vmesh_method) {
        case BEVEL_VMESH_ADJ:
          vm->mesh_kind = M_ADJ;
          break;
        case BEVEL_VMESH_CUTOFF:
          vm->mesh_kind = M_CUTOFF;
          break;
      }
    }
  }
}
