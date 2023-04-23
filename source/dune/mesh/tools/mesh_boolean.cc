/** Main functions for beveling a BMesh (used by the tool and modifier) **/

#include "mem_guardedalloc.h"

#include "types_curveprofile.h"
#include "types_meshdata.h"
#include "types_modifiers.h"
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
 * number of segments. The it uses MeshFace indices to index into it, so will
 * only be valid as long MeshFaces are not added or deleted in the BMesh.
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
    MESH_NONE,    /* No polygon mesh needed. */
    MESH_POLY,    /* A simple polygon. */
    MESH_ADJ,     /* "Adjacent edges" mesh pattern. */
    MESH_TRI_FAN, /* A simple polygon - fan filled. */
    MESH_CUTOFF,  /* A triangulated face at the end of each profile. */
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
  FACE_NONE,
  /** Original face, not touched. */
  FACE_ORIG,
  /** Face for construction around a vert. */
  FACE_VERT,
  /** Face for a beveled edge. */
  FACE_EDGE,
  /** Reconstructed original face with some new verts. */
  FACE_RECON,
} FaceKind;

/** Helper for keeping track of angle kind. */
typedef enum AngleKind {
  /** Angle less than 180 degrees. */
  ANGLE_SMALLER = -1,
  /** 180 degree angle. */
  ANGLE_STRAIGHT = 0,
  /** Angle greater than 180 degrees. */
  ANGLE_LARGER = 1,
} AngleKind;
