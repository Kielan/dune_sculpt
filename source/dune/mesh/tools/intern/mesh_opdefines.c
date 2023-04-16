/**
 * Mesh operator definitions.
 *
 * This file defines (and documents) all bmesh operators (bmops).
 *
 * Do not rename any operator or slot names! otherwise you must go
 * through the code and find all references to them!
 *
 * A word on slot names:
 *
 * For geometry input slots, the following are valid names:
 * - verts
 * - edges
 * - faces
 * - edgefacein
 * - vertfacein
 * - vertedgein
 * - vertfacein
 * - geom
 *
 * The basic rules are, for single-type geometry slots, use the plural of the
 * type name (e.g. edges).  for double-type slots, use the two type names plus
 * "in" (e.g. edgefacein).  for three-type slots, use geom.
 *
 * for output slots, for single-type geometry slots, use the type name plus "out",
 * (e.g. verts.out), for double-type slots, use the two type names plus "out",
 * (e.g. vertfaces.out), for three-type slots, use geom.  note that you can also
 * use more esoteric names (e.g. geom_skirt.out) so long as the comment next to the
 * slot definition tells you what types of elements are in it.
 */

#include "lib_utildefines.h"

#include "mesh.h"
#include "intern/mesh_ops_private.h"

#include "types_modifier.h"

/**
 * The formatting of these bmesh operators is parsed by
 * 'doc/python_api/rst_from_bmesh_opdefines.py'
 * for use in python docs, so reStructuredText may be used
 * rather than doxygen syntax.
 *
 * template (py quotes used because nested comments don't work
 * on all C compilers):
 *
 * """
 * Region Extend.
 *
 * paragraph1, Extends on the title above.
 *
 * Another paragraph.
 *
 * Another paragraph.
 * """
 *
 * so the first line is the "title" of the bmop.
 * subsequent line blocks separated by blank lines
 * are paragraphs.  individual descriptions of slots
 * are extracted from comments next to them.
 *
 * eg:
 *     {MESH_OP_SLOT_ELEMENT_BUF, "geom.out"},  """ output slot, boundary region """
 *
 * ... or:
 *
 * """ output slot, boundary region """
 *     {MESH_OP_SLOT_ELEMENT_BUF, "geom.out"},
 *
 * Both are acceptable.
 * note that '//' comments are ignored.
 */

/* Keep struct definition from wrapping. */
/* clang-format off */

/* enums shared between multiple operators */

static MeshFlagSet mesh_enum_axis_xyz[] = {
  {0, "X"},
  {1, "Y"},
  {2, "Z"},
  {0, NULL},
};

static MeshFlagSet mesh_enum_axis_neg_xyz_and_xyz[] = {
  {0, "-X"},
  {1, "-Y"},
  {2, "-Z"},
  {3, "X"},
  {4, "Y"},
  {5, "Z"},
  {0, NULL},
};

static MeshFlagSet mesh_op_enum_falloff_type[] = {
  {SUBD_FALLOFF_SMOOTH, "SMOOTH"},
  {SUBD_FALLOFF_SPHERE, "SPHERE"},
  {SUBD_FALLOFF_ROOT, "ROOT"},
  {SUBD_FALLOFF_SHARP, "SHARP"},
  {SUBD_FALLOFF_LIN, "LINEAR"},
  {SUBD_FALLOFF_INVSQUARE, "INVERSE_SQUARE"},
  {0, NULL},
};

/* Quiet 'enum-conversion' warning. */
#define MESH_FACE ((int)MESH_FACE)
#define MESH_EDGE ((int)MESH_EDGE)
#define MESH_VERT ((int)MESH_VERT)

/*
 * Vertex Smooth.
 *
 * Smooths vertices by using a basic vertex averaging scheme.
 */
static MeshOpDefines mesh_smooth_vert_def = {
  "smooth_vert",
  /* slots_in */
  {{"verts", MESH_OP_SLOT_ELEMENT_BUF, {BM_VERT}},    /* input vertices */
   {"factor", MESH_OP_SLOT_FLT},           /* smoothing factor */
   {"mirror_clip_x", MESH_OP_SLOT_BOOL},   /* set vertices close to the x axis before the operation to 0 */
   {"mirror_clip_y", MESH_OP_SLOT_BOOL},   /* set vertices close to the y axis before the operation to 0 */
   {"mirror_clip_z", MESH_OP_SLOT_BOOL},   /* set vertices close to the z axis before the operation to 0 */
   {"clip_dist",  MESH_OP_SLOT_FLT},       /* clipping threshold for the above three slots */
   {"use_axis_x", MESH_OP_SLOT_BOOL},      /* smooth vertices along X axis */
   {"use_axis_y", MESH_OP_SLOT_BOOL},      /* smooth vertices along Y axis */
   {"use_axis_z", MESH_OP_SLOT_BOOL},      /* smooth vertices along Z axis */
   {{'\0'}},
  },
  {{{'\0'}}},  /* no output */
  mesh_smooth_vert_exec,
  (MESH_OPTYPE_FLAG_NORMALS_CALC),
};

/*
 * Vertex Smooth Laplacian.
 *
 * Smooths vertices by using Laplacian smoothing propose by.
 * Desbrun, et al. Implicit Fairing of Irregular Meshes using Diffusion and Curvature Flow.
 */
static MeshOpDefine mesh_smooth_laplacian_vert_def = {
  "smooth_laplacian_vert",
  /* slots_in */
  {{"verts", MESH_OP_SLOT_ELEMENT_BUF, {BM_VERT}},    /* input vertices */
   {"lambda_factor", MESH_OP_SLOT_FLT},           /* lambda param */
   {"lambda_border", MESH_OP_SLOT_FLT},    /* lambda param in border */
   {"use_x", MESH_OP_SLOT_BOOL},           /* Smooth object along X axis */
   {"use_y", MESH_OP_SLOT_BOOL},           /* Smooth object along Y axis */
   {"use_z", MESH_OP_SLOT_BOOL},           /* Smooth object along Z axis */
   {"preserve_volume", MESH_OP_SLOT_BOOL}, /* Apply volume preservation after smooth */
   {{'\0'}},
  },
  {{{'\0'}}},  /* no output */
  mesh_smooth_laplacian_vert_exec,
  (MESH_OPTYPE_FLAG_NORMALS_CALC),
};

/*
 * Right-Hand Faces.
 *
 * Computes an "outside" normal for the specified input faces.
 */
static MeshOpDefine mesh_op_recalc_face_normals_def = {
  "recalc_face_normals",
  /* slots_in */
  {{"faces", MESH_OP_SLOT_ELEMENT_BUF, {MESH_FACE}}, /* input faces */
   {{'\0'}},
  },
  {{{'\0'}}},  /* no output */
  mesh_recalc_face_normals_exec,
  (MESH_OPTYPE_FLAG_UNTAN_MULTIRES |
   MESH_OPTYPE_FLAG_NORMALS_CALC),
};

/*
 * Planar Faces.
 *
 * Iteratively flatten faces.
 */
static MeshOpDefine mesh_op_planar_faces_def = {
  "planar_faces",
  /* slots_in */
  {{"faces", MESH_OP_SLOT_ELEMENT_BUF, {BM_FACE}},    /* input geometry. */
   {"iterations", MESH_OP_SLOT_INT},  /* Number of times to flatten faces (for when connected faces are used) */
   {"factor", MESH_OP_SLOT_FLT},  /* Influence for making planar each iteration */
   {{'\0'}},
  },
  /* slots_out */
  {{"geom.out", MESH_OP_SLOT_ELEMENT_BUF, {MESH_VERT | MESH_EDGE | BM_FACE}}, /* output slot, computed boundary geometry. */
   {{'\0'}},
  },
  mesh_planar_faces_ex,
  (MESH_OPTYPE_FLAG_SELECT_FLUSH |
   MESH_OPTYPE_FLAG_SELECT_VALIDATE),
};

/*
 * Region Extend.
 *
 * used to implement the select more/less tools.
 * this puts some geometry surrounding regions of
 * geometry in geom into geom.out.
 *
 * if use_faces is 0 then geom.out spits out verts and edges,
 * otherwise it spits out faces.
 */
static MeshOpDefine mesh_op_region_extend_def = {
  "region_extend",
  /* slots_in */
  {{"geom", MESH_OP_SLOT_ELEMENT_BUF, {BM_VERT | BM_EDGE | BM_FACE}},     /* input geometry */
   {"use_contract", MESH_OP_SLOT_BOOL},    /* find boundary inside the regions, not outside. */
   {"use_faces", MESH_OP_SLOT_BOOL},       /* extend from faces instead of edges */
   {"use_face_step", MESH_OP_SLOT_BOOL},   /* step over connected faces */
   {{'\0'}},
  },
  /* slots_out */
  {{"geom.out", MESH_OP_SLOT_ELEMENT_BUF, {BM_VERT | BM_EDGE | BM_FACE}}, /* output slot, computed boundary geometry. */
   {{'\0'}},
  },
  mesh_op_region_extend_ex,
  (MESH_OPTYPE_FLAG_SELECT_FLUSH |
   MESH_OPTYPE_FLAG_SELECT_VALIDATE),
};

/*
 * Edge Rotate.
 *
 * Rotates edges topologically.  Also known as "spin edge" to some people.
 * Simple example: `[/] becomes [|] then [\]`.
 */
static MeshOpDefine mesh_op_rotate_edges_def = {
  "rotate_edges",
  /* slots_in */
  {{"edges", MESH_OP_SLOT_ELEMENT_BUF, {MESH_EDGE}},    /* input edges */
   {"use_ccw", MESH_OP_SLOT_BOOL},         /* rotate edge counter-clockwise if true, otherwise clockwise */
   {{'\0'}},
  },
  /* slots_out */
  {{"edges.out", MESH_OP_SLOT_ELEMENT_BUF, {MESH_EDGE}}, /* newly spun edges */
   {{'\0'}},
  },
  mesh_rotate_edges_ex,
  (MESH_OPTYPE_FLAG_UNTAN_MULTIRES |
   MESH_OPTYPE_FLAG_NORMALS_CALC |
   MESH_OPTYPE_FLAG_SELECT_FLUSH |
   MESH_OPTYPE_FLAG_SELECT_VALIDATE),
};

/*
 * Reverse Faces.
 *
 * Reverses the winding (vertex order) of faces.
 * This has the effect of flipping the normal.
 */
static MeshOpDefine mesh_op_reverse_faces_def = {
  "reverse_faces",
  /* slots_in */
  {{"faces", MESH_OP_SLOT_ELEMENT_BUF, {MESH_FACE}},    /* input faces */
   {"flip_multires", MESH_OP_SLOT_BOOL},  /* maintain multi-res offset */
   {{'\0'}},
  },
  {{{'\0'}}},  /* no output */
  mesh_op_reverse_faces_ex,
  (MESH_OPTYPE_FLAG_UNTAN_MULTIRES |
   MESH_OPTYPE_FLAG_NORMALS_CALC),
};

/*
 * Edge Bisect.
 *
 * Splits input edges (but doesn't do anything else).
 * This creates a 2-valence vert.
 */
static MeshOpDefine mesh_op_bisect_edges_def = {
  "bisect_edges",
  /* slots_in */
  {{"edges", MESH_OP_SLOT_ELEMENT_BUF, {BM_EDGE}}, /* input edges */
   {"cuts", MESH_OP_SLOT_INT}, /* number of cuts */
   {"edge_percents", MESH_OP_SLOT_MAPPING, {(int)BMO_OP_SLOT_SUBTYPE_MAP_FLT}},
   {{'\0'}},
  },
  /* slots_out */
  {{"geom_split.out", MESH_OP_SLOT_ELEMENT_BUF, {BM_VERT | BM_EDGE | BM_FACE}}, /* newly created vertices and edges */
   {{'\0'}},
  },
  mesh_bisect_edges_exec,
  (MESH_OPTYPE_FLAG_UNTAN_MULTIRES |
   MESH_OPTYPE_FLAG_NORMALS_CALC |
   MESH_OPTYPE_FLAG_SELECT_FLUSH |
   MESH_OPTYPE_FLAG_SELECT_VALIDATE),
};

/*
 * Mirror.
 *
 * Mirrors geometry along an axis.  The resulting geometry is welded on using
 * merge_dist.  Pairs of original/mirrored vertices are welded using the merge_dist
 * parameter (which defines the minimum distance for welding to happen).
 */
static MeshOpDefine mesh_op_mirror_def = {
  "mirror",
  /* slots_in */
  {{"geom", BMO_OP_SLOT_ELEMENT_BUF, {MESH_VERT | MESH_EDGE | MESH_FACE}},     /* input geometry */
   {"matrix",          MESH_OP_SLOT_MAT},   /* matrix defining the mirror transformation */
   {"merge_dist",      MESH_OP_SLOT_FLT},   /* maximum distance for merging.  does no merging if 0. */
   {"axis",            MESH_OP_SLOT_INT, {(int)BMO_OP_SLOT_SUBTYPE_INT_ENUM}, bmo_enum_axis_xyz},   /* the axis to use. */
   {"mirror_u",        MESH_OP_SLOT_BOOL},  /* mirror UVs across the u axis */
   {"mirror_v",        MESH_OP_SLOT_BOOL},  /* mirror UVs across the v axis */
   {"mirror_udim",     MESH_OP_SLOT_BOOL},  /* mirror UVs in each tile */
   {"use_shapekey",    MESH_OP_SLOT_BOOL},  /* Transform shape keys too. */
   {{'\0'}},
  },
  /* slots_out */
  {{"geom.out", BMO_OP_SLOT_ELEMENT_BUF, {BM_VERT | BM_EDGE | BM_FACE}}, /* output geometry, mirrored */
   {{'\0'}},
  },
  bmo_mirror_exec,
  (BMO_OPTYPE_FLAG_NORMALS_CALC |
   BMO_OPTYPE_FLAG_SELECT_FLUSH |
   BMO_OPTYPE_FLAG_SELECT_VALIDATE),
};

/*
 * Find Doubles.
 *
 * Takes input verts and find vertices they should weld to.
 * Outputs a mapping slot suitable for use with the weld verts bmop.
 *
 * If keep_verts is used, vertices outside that set can only be merged
 * with vertices in that set.
 */
static BMOpDefine bmo_find_doubles_def = {
  "find_doubles",
  /* slots_in */
  {{"verts", BMO_OP_SLOT_ELEMENT_BUF, {BM_VERT}}, /* input vertices */
   {"keep_verts", BMO_OP_SLOT_ELEMENT_BUF, {BM_VERT}}, /* list of verts to keep */
   {"dist",         BMO_OP_SLOT_FLT}, /* maximum distance */
   {{'\0'}},
  },
  /* slots_out */
  {{"targetmap.out", BMO_OP_SLOT_MAPPING, {(int)BMO_OP_SLOT_SUBTYPE_MAP_ELEM}},
   {{'\0'}},
  },
  bmo_find_doubles_exec,
  (BMO_OPTYPE_FLAG_NOP),
};
