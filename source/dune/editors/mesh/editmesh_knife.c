#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#endif

#include "MEM_guardedalloc.h"

#include "BLF_api.h"

#include "LI_alloca.h"
#include "LI_array.h"
#include "LI_linklist.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_memarena.h"
#include "LI_smallhash.h"
#include "LI_stack.h"
#include "LI_string.h"

#include "LANG_translation.h"

#include "KE_bvhutils.h"
#include "KE_context.h"
#include "KE_editmesh.h"
#include "KE_editmesh_bvh.h"
#include "KE_layer.h"
#include "KE_report.h"
#include "KE_scene.h"
#include "KE_unit.h"

#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_state.h"

#include "ED_mesh.h"
#include "ED_numinput.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_transform.h"
#include "ED_view3d.h"

#include "WM_api.h"
#include "WM_types.h"

#include "TYPES_object.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "API_access.h"
#include "API_define.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "mesh_intern.h" /* Own include. */

/* Detect isolated holes and fill them. */
#define USE_NET_ISLAND_CONNECT

#define KMAXDIST (10 * U.dpi_fac) /* Max mouse distance from edge before not detecting it. */

/* WARNING: Knife float precision is fragile:
 * Be careful before making changes here see: (T43229, T42864, T42459, T41164).
 */
#define KNIFE_FLT_EPS 0.00001f
#define KNIFE_FLT_EPS_SQUARED (KNIFE_FLT_EPS * KNIFE_FLT_EPS)
#define KNIFE_FLT_EPSBIG 0.0005f

#define KNIFE_FLT_EPS_PX_VERT 0.5f
#define KNIFE_FLT_EPS_PX_EDGE 0.05f
#define KNIFE_FLT_EPS_PX_FACE 0.05f

#define KNIFE_DEFAULT_ANGLE_SNAPPING_INCREMENT 30.0f
#define KNIFE_MIN_ANGLE_SNAPPING_INCREMENT 0.0f
#define KNIFE_MAX_ANGLE_SNAPPING_INCREMENT 180.0f

typedef struct KnifeColors {
  uchar line[3];
  uchar edge[3];
  uchar edge_extra[3];
  uchar curpoint[3];
  uchar curpoint_a[4];
  uchar point[3];
  uchar point_a[4];
  uchar xaxis[3];
  uchar yaxis[3];
  uchar zaxis[3];
  uchar axis_extra[3];
} KnifeColors;

/* Knife-tool Operator. */
typedef struct KnifeVert {
  Object *ob;
  uint base_index;
  DuneMeshVert *v; /* Non-NULL if this is an original vert. */
  ListBase edges;
  ListBase faces;

  float co[3], cageco[3];
  bool is_face, in_space;
  bool is_cut; /* Along a cut created by user input (will draw too). */
  bool is_invalid;
  bool is_splitting; /* Created when an edge was split. */
} KnifeVert;

typedef struct Ref {
  struct Ref *next, *prev;
  void *ref;
} Ref;

typedef struct KnifeEdge {
  KnifeVert *v1, *v2;
  BMFace *basef; /* Face to restrict face fill to. */
  ListBase faces;

  BMEdge *e;   /* Non-NULL if this is an original edge. */
  bool is_cut; /* Along a cut created by user input (will draw too). */
  bool is_invalid;
  int splits; /* Number of times this edge has been split. */
} KnifeEdge;

typedef struct KnifeLineHit {
  float hit[3], cagehit[3];
  float schit[2]; /* Screen coordinates for cagehit. */
  float l;        /* Lambda along cut line. */
  float perc;     /* Lambda along hit line. */
  float m;        /* Depth front-to-back. */

  /* Exactly one of kfe, v, or f should be non-NULL,
   * saying whether cut line crosses and edge,
   * is snapped to a vert, or is in the middle of some face. */
  KnifeEdge *kfe;
  KnifeVert *v;
  BMFace *f;
  Object *ob;
  uint base_index;
} KnifeLineHit;

typedef struct KnifePosData {
  float co[3];
  float cage[3];

  /* At most one of vert, edge, or bmface should be non-NULL,
   * saying whether the point is snapped to a vertex, edge, or in a face.
   * If none are set, this point is in space and is_space should be true. */
  KnifeVert *vert;
  KnifeEdge *edge;
  BMFace *bmface;
  Object *ob; /* Object of the vert, edge or bmface. */
  uint base_index;

  /* When true, the cursor isn't over a face. */
  bool is_space;

  float mval[2]; /* Mouse screen position (may be non-integral if snapped to something). */
} KnifePosData;

typedef struct KnifeMeasureData {
  float cage[3];
  float mval[2];
  bool is_stored;
} KnifeMeasureData;

typedef struct KnifeUndoFrame {
  int cuts;         /* Line hits cause multiple edges/cuts to be created at once. */
  int splits;       /* Number of edges split. */
  KnifePosData pos; /* Store previous KnifePosData. */
  KnifeMeasureData mdata;

} KnifeUndoFrame;

typedef struct KnifeBVH {
  BVHTree *tree;          /* Knife Custom BVH Tree. */
  BMLoop *(*looptris)[3]; /* Used by #knife_bvh_raycast_cb to store the intersecting looptri. */
  float uv[2];            /* Used by #knife_bvh_raycast_cb to store the intersecting uv. */
  uint base_index;

  /* Use #bm_ray_cast_cb_elem_not_in_face_check. */
  bool (*filter_cb)(BMFace *f, void *userdata);
  void *filter_data;

} KnifeBVH;

/* struct for properties used while drawing */
typedef struct KnifeTool_OpData {
  ARegion *region;   /* Region that knifetool was activated in. */
  void *draw_handle; /* For drawing preview loop. */
  ViewContext vc;    /* NOTE: _don't_ use 'mval', instead use the one we define below. */
  float mval[2];     /* Mouse value with snapping applied. */

  Scene *scene;

  /* Used for swapping current object when in multi-object edit mode. */
  Object **objects;
  uint objects_len;

  MemArena *arena;

  /* Reused for edge-net filling. */
  struct {
    /* Cleared each use. */
    GSet *edge_visit;
#ifdef USE_NET_ISLAND_CONNECT
    MemArena *arena;
#endif
  } edgenet;

  GHash *origvertmap;
  GHash *origedgemap;
  GHash *kedgefacemap;
  GHash *facetrimap;

  KnifeBVH bvh;
  const float (**cagecos)[3];

  BLI_mempool *kverts;
  BLI_mempool *kedges;
  bool no_cuts; /* A cut has not been made yet. */

  BLI_Stack *undostack;
  BLI_Stack *splitstack; /* Store edge splits by #knife_split_edge. */

  float vthresh;
  float ethresh;

  /* Used for drag-cutting. */
  KnifeLineHit *linehits;
  int totlinehit;

  /* Data for mouse-position-derived data. */
  KnifePosData curr; /* Current point under the cursor. */
  KnifePosData prev; /* Last added cut (a line draws from the cursor to this). */
  KnifePosData init; /* The first point in the cut-list, used for closing the loop. */

  /* Number of knife edges `kedges`. */
  int totkedge;
  /* Number of knife vertices, `kverts`. */
  int totkvert;

  BLI_mempool *refs;

  KnifeColors colors;

  /* Run by the UI or not. */
  bool is_interactive;

  /* Operator options. */
  bool cut_through;   /* Preference, can be modified at runtime (that feature may go). */
  bool only_select;   /* Set on initialization. */
  bool select_result; /* Set on initialization. */

  bool is_ortho;
  float ortho_extent;
  float ortho_extent_center[3];

  float clipsta, clipend;

  enum { MODE_IDLE, MODE_DRAGGING, MODE_CONNECT, MODE_PANNING } mode;
  bool is_drag_hold;

  int prevmode;
  bool snap_midpoints;
  bool ignore_edge_snapping;
  bool ignore_vert_snapping;

  NumInput num;
  float angle_snapping_increment; /* Degrees */

  /* Use to check if we're currently dragging an angle snapped line. */
  short angle_snapping_mode;
  bool is_angle_snapping;
  bool angle_snapping;
  float angle;
  /* Relative angle snapping reference edge. */
  KnifeEdge *snap_ref_edge;
  int snap_ref_edges_count;
  int snap_edge; /* Used by #KNF_MODAL_CYCLE_ANGLE_SNAP_EDGE to choose an edge for snapping. */

  short constrain_axis;
  short constrain_axis_mode;
  bool axis_constrained;
  char axis_string[2];

  short dist_angle_mode;
  bool show_dist_angle;
  KnifeMeasureData mdata; /* Data for distance and angle drawing calculations. */

  KnifeUndoFrame *undo; /* Current undo frame. */
  bool is_drag_undo;

  bool depth_test;
} KnifeTool_OpData;

enum {
  KNF_MODAL_CANCEL = 1,
  KNF_MODAL_CONFIRM,
  KNF_MODAL_UNDO,
  KNF_MODAL_MIDPOINT_ON,
  KNF_MODAL_MIDPOINT_OFF,
  KNF_MODAL_NEW_CUT,
  KNF_MODAL_IGNORE_SNAP_ON,
  KNF_MODAL_IGNORE_SNAP_OFF,
  KNF_MODAL_ADD_CUT,
  KNF_MODAL_ANGLE_SNAP_TOGGLE,
  KNF_MODAL_CYCLE_ANGLE_SNAP_EDGE,
  KNF_MODAL_CUT_THROUGH_TOGGLE,
  KNF_MODAL_SHOW_DISTANCE_ANGLE_TOGGLE,
  KNF_MODAL_DEPTH_TEST_TOGGLE,
  KNF_MODAL_PANNING,
  KNF_MODAL_X_AXIS,
  KNF_MODAL_Y_AXIS,
  KNF_MODAL_Z_AXIS,
  KNF_MODAL_ADD_CUT_CLOSED,
};

enum {
  KNF_CONSTRAIN_ANGLE_MODE_NONE = 0,
  KNF_CONSTRAIN_ANGLE_MODE_SCREEN = 1,
  KNF_CONSTRAIN_ANGLE_MODE_RELATIVE = 2
};

enum {
  KNF_CONSTRAIN_AXIS_NONE = 0,
  KNF_CONSTRAIN_AXIS_X = 1,
  KNF_CONSTRAIN_AXIS_Y = 2,
  KNF_CONSTRAIN_AXIS_Z = 3
};

enum {
  KNF_CONSTRAIN_AXIS_MODE_NONE = 0,
  KNF_CONSTRAIN_AXIS_MODE_GLOBAL = 1,
  KNF_CONSTRAIN_AXIS_MODE_LOCAL = 2
};

enum {
  KNF_MEASUREMENT_NONE = 0,
  KNF_MEASUREMENT_BOTH = 1,
  KNF_MEASUREMENT_DISTANCE = 2,
  KNF_MEASUREMENT_ANGLE = 3
};

/* -------------------------------------------------------------------- */
/** \name Drawing
 * \{ */

static void knifetool_raycast_planes(const KnifeTool_OpData *kcd, float r_v1[3], float r_v2[3])
{
  float planes[4][4];
  planes_from_projmat(
      kcd->vc.rv3d->persmat, planes[2], planes[0], planes[1], planes[3], NULL, NULL);

  /* Ray-cast all planes. */
  {
    float ray_dir[3];
    float ray_hit_best[2][3] = {{UNPACK3(kcd->prev.cage)}, {UNPACK3(kcd->curr.cage)}};
    float lambda_best[2] = {-FLT_MAX, FLT_MAX};
    int i;

    {
      float curr_cage_adjust[3];
      float co_depth[3];

      copy_v3_v3(co_depth, kcd->prev.cage);
      ED_view3d_win_to_3d(kcd->vc.v3d, kcd->region, co_depth, kcd->curr.mval, curr_cage_adjust);

      sub_v3_v3v3(ray_dir, curr_cage_adjust, kcd->prev.cage);
    }

    for (i = 0; i < 4; i++) {
      float ray_hit[3];
      float lambda_test;
      if (isect_ray_plane_v3(kcd->prev.cage, ray_dir, planes[i], &lambda_test, false)) {
        madd_v3_v3v3fl(ray_hit, kcd->prev.cage, ray_dir, lambda_test);
        if (lambda_test < 0.0f) {
          if (lambda_test > lambda_best[0]) {
            copy_v3_v3(ray_hit_best[0], ray_hit);
            lambda_best[0] = lambda_test;
          }
        }
        else {
          if (lambda_test < lambda_best[1]) {
            copy_v3_v3(ray_hit_best[1], ray_hit);
            lambda_best[1] = lambda_test;
          }
        }
      }
    }

    copy_v3_v3(r_v1, ray_hit_best[0]);
    copy_v3_v3(r_v2, ray_hit_best[1]);
  }
}

static void knifetool_draw_angle_snapping(const KnifeTool_OpData *kcd)
{
  float v1[3], v2[3];

  knifetool_raycast_planes(kcd, v1, v2);

  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformThemeColor3(TH_TRANSFORM);
  GPU_line_width(2.0);

  immBegin(GPU_PRIM_LINES, 2);
  immVertex3fv(pos, v1);
  immVertex3fv(pos, v2);
  immEnd();

  immUnbindProgram();
}

static void knifetool_draw_orientation_locking(const KnifeTool_OpData *kcd)
{
  if (!compare_v3v3(kcd->prev.cage, kcd->curr.cage, KNIFE_FLT_EPSBIG)) {
    float v1[3], v2[3];

    /* This is causing buggy behavior when `prev.cage` and `curr.cage` are too close together. */
    knifetool_raycast_planes(kcd, v1, v2);

    uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

    switch (kcd->constrain_axis) {
      case KNF_CONSTRAIN_AXIS_X: {
        immUniformColor3ubv(kcd->colors.xaxis);
        break;
      }
      case KNF_CONSTRAIN_AXIS_Y: {
        immUniformColor3ubv(kcd->colors.yaxis);
        break;
      }
      case KNF_CONSTRAIN_AXIS_Z: {
        immUniformColor3ubv(kcd->colors.zaxis);
        break;
      }
      default: {
        immUniformColor3ubv(kcd->colors.axis_extra);
        break;
      }
    }

    GPU_line_width(2.0);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex3fv(pos, v1);
    immVertex3fv(pos, v2);
    immEnd();

    immUnbindProgram();
  }
}

static void knifetool_draw_visible_distances(const KnifeTool_OpData *kcd)
{
  GPU_matrix_push_projection();
  GPU_matrix_push();
  GPU_matrix_identity_set();
  wmOrtho2_region_pixelspace(kcd->region);

  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

  char numstr[256];
  float numstr_size[2];
  float posit[2];
  const float bg_margin = 4.0f * U.dpi_fac;
  const float font_size = 14.0f * U.pixelsize;
  const int distance_precision = 4;

  /* Calculate distance and convert to string. */
  const float cut_len = len_v3v3(kcd->prev.cage, kcd->curr.cage);

  UnitSettings *unit = &kcd->scene->unit;
  if (unit->system == USER_UNIT_NONE) {
    BLI_snprintf(numstr, sizeof(numstr), "%.*f", distance_precision, cut_len);
  }
  else {
    BKE_unit_value_as_string(numstr,
                             sizeof(numstr),
                             (double)(cut_len * unit->scale_length),
                             distance_precision,
                             B_UNIT_LENGTH,
                             unit,
                             false);
  }

  BLF_enable(blf_mono_font, BLF_ROTATION);
  BLF_size(blf_mono_font, font_size, U.dpi);
  BLF_rotation(blf_mono_font, 0.0f);
  BLF_width_and_height(blf_mono_font, numstr, sizeof(numstr), &numstr_size[0], &numstr_size[1]);

  /* Center text. */
  mid_v2_v2v2(posit, kcd->prev.mval, kcd->curr.mval);
  posit[0] -= numstr_size[0] / 2.0f;
  posit[1] -= numstr_size[1] / 2.0f;

  /* Draw text background. */
  float color_back[4] = {0.0f, 0.0f, 0.0f, 0.5f}; /* TODO: Replace with theme color. */
  immUniformColor4fv(color_back);

  GPU_blend(GPU_BLEND_ALPHA);
  immRectf(pos,
           posit[0] - bg_margin,
           posit[1] - bg_margin,
           posit[0] + bg_margin + numstr_size[0],
           posit[1] + bg_margin + numstr_size[1]);
  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();

  /* Draw text. */
  uchar color_text[3];
  UI_GetThemeColor3ubv(TH_TEXT, color_text);

  BLF_color3ubv(blf_mono_font, color_text);
  BLF_position(blf_mono_font, posit[0], posit[1], 0.0f);
  BLF_draw(blf_mono_font, numstr, sizeof(numstr));
  BLF_disable(blf_mono_font, BLF_ROTATION);

  GPU_matrix_pop();
  GPU_matrix_pop_projection();
}

static void knifetool_draw_angle(const KnifeTool_OpData *kcd,
                                 const float start[3],
                                 const float mid[3],
                                 const float end[3],
                                 const float start_ss[2],
                                 const float mid_ss[2],
                                 const float end_ss[2],
                                 const float angle)
{
  const RegionView3D *rv3d = kcd->region->regiondata;
  const int arc_steps = 24;
  const float arc_size = 64.0f * U.dpi_fac;
  const float bg_margin = 4.0f * U.dpi_fac;
  const float cap_size = 4.0f * U.dpi_fac;
  const float font_size = 14.0f * U.pixelsize;
  const int angle_precision = 3;

  /* Angle arc in 3d space. */
  GPU_blend(GPU_BLEND_ALPHA);

  const uint pos_3d = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  {
    float dir_tmp[3];
    float ar_coord[3];

    float dir_a[3];
    float dir_b[3];
    float quat[4];
    float axis[3];
    float arc_angle;

    const float inverse_average_scale = 1 /
                                        (kcd->curr.ob->obmat[0][0] + kcd->curr.ob->obmat[1][1] +
                                         kcd->curr.ob->obmat[2][2]);

    const float px_scale =
        3.0f * inverse_average_scale *
        (ED_view3d_pixel_size_no_ui_scale(rv3d, mid) *
         min_fff(arc_size, len_v2v2(start_ss, mid_ss) / 2.0f, len_v2v2(end_ss, mid_ss) / 2.0f));

    sub_v3_v3v3(dir_a, start, mid);
    sub_v3_v3v3(dir_b, end, mid);
    normalize_v3(dir_a);
    normalize_v3(dir_b);

    cross_v3_v3v3(axis, dir_a, dir_b);
    arc_angle = angle_normalized_v3v3(dir_a, dir_b);

    axis_angle_to_quat(quat, axis, arc_angle / arc_steps);

    copy_v3_v3(dir_tmp, dir_a);

    immUniformThemeColor3(TH_WIRE);
    GPU_line_width(1.0);

    immBegin(GPU_PRIM_LINE_STRIP, arc_steps + 1);
    for (int j = 0; j <= arc_steps; j++) {
      madd_v3_v3v3fl(ar_coord, mid, dir_tmp, px_scale);
      mul_qt_v3(quat, dir_tmp);

      immVertex3fv(pos_3d, ar_coord);
    }
    immEnd();
  }

  immUnbindProgram();

  /* Angle text and background in 2d space. */
  GPU_matrix_push_projection();
  GPU_matrix_push();
  GPU_matrix_identity_set();
  wmOrtho2_region_pixelspace(kcd->region);

  uint pos_2d = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

  /* Angle as string. */
  char numstr[256];
  float numstr_size[2];
  float posit[2];

  UnitSettings *unit = &kcd->scene->unit;
  if (unit->system == USER_UNIT_NONE) {
    BLI_snprintf(numstr, sizeof(numstr), "%.*f°", angle_precision, RAD2DEGF(angle));
  }
  else {
    BKE_unit_value_as_string(
        numstr, sizeof(numstr), (double)angle, angle_precision, B_UNIT_ROTATION, unit, false);
  }

  BLF_enable(blf_mono_font, BLF_ROTATION);
  BLF_size(blf_mono_font, font_size, U.dpi);
  BLF_rotation(blf_mono_font, 0.0f);
  BLF_width_and_height(blf_mono_font, numstr, sizeof(numstr), &numstr_size[0], &numstr_size[1]);

  posit[0] = mid_ss[0] + (cap_size * 2.0f);
  posit[1] = mid_ss[1] - (numstr_size[1] / 2.0f);

  /* Draw text background. */
  float color_back[4] = {0.0f, 0.0f, 0.0f, 0.5f}; /* TODO: Replace with theme color. */
  immUniformColor4fv(color_back);

  GPU_blend(GPU_BLEND_ALPHA);
  immRectf(pos_2d,
           posit[0] - bg_margin,
           posit[1] - bg_margin,
           posit[0] + bg_margin + numstr_size[0],
           posit[1] + bg_margin + numstr_size[1]);
  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();

  /* Draw text. */
  uchar color_text[3];
  UI_GetThemeColor3ubv(TH_TEXT, color_text);

  BLF_color3ubv(blf_mono_font, color_text);
  BLF_position(blf_mono_font, posit[0], posit[1], 0.0f);
  BLF_rotation(blf_mono_font, 0.0f);
  BLF_draw(blf_mono_font, numstr, sizeof(numstr));
  BLF_disable(blf_mono_font, BLF_ROTATION);

  GPU_matrix_pop();
  GPU_matrix_pop_projection();

  GPU_blend(GPU_BLEND_NONE);
}

static void knifetool_draw_visible_angles(const KnifeTool_OpData *kcd)
{
  Ref *ref;
  KnifeVert *kfv;
  KnifeVert *tempkfv;
  KnifeEdge *kfe;
  KnifeEdge *tempkfe;

  if (kcd->curr.vert) {
    kfv = kcd->curr.vert;

    float min_angle = FLT_MAX;
    float angle = 0.0f;
    float *end;

    kfe = ((Ref *)kfv->edges.first)->ref;
    for (ref = kfv->edges.first; ref; ref = ref->next) {
      tempkfe = ref->ref;
      if (tempkfe->v1 != kfv) {
        tempkfv = tempkfe->v1;
      }
      else {
        tempkfv = tempkfe->v2;
      }
      angle = angle_v3v3v3(kcd->prev.cage, kcd->curr.cage, tempkfv->cageco);
      if (angle < min_angle) {
        min_angle = angle;
        kfe = tempkfe;
        end = tempkfv->cageco;
      }
    }

    if (min_angle > KNIFE_FLT_EPSBIG) {
      /* Last vertex in screen space. */
      float end_ss[2];
      ED_view3d_project_float_global(kcd->region, end, end_ss, V3D_PROJ_TEST_NOP);

      knifetool_draw_angle(kcd,
                           kcd->prev.cage,
                           kcd->curr.cage,
                           end,
                           kcd->prev.mval,
                           kcd->curr.mval,
                           end_ss,
                           min_angle);
    }
  }
  else if (kcd->curr.edge) {
    kfe = kcd->curr.edge;

    /* Check for most recent cut (if cage is part of previous cut). */
    if (!compare_v3v3(kfe->v1->cageco, kcd->prev.cage, KNIFE_FLT_EPSBIG) &&
        !compare_v3v3(kfe->v2->cageco, kcd->prev.cage, KNIFE_FLT_EPSBIG)) {
      /* Determine acute angle. */
      float angle1 = angle_v3v3v3(kcd->prev.cage, kcd->curr.cage, kfe->v1->cageco);
      float angle2 = angle_v3v3v3(kcd->prev.cage, kcd->curr.cage, kfe->v2->cageco);

      float angle;
      float *end;
      if (angle1 < angle2) {
        angle = angle1;
        end = kfe->v1->cageco;
      }
      else {
        angle = angle2;
        end = kfe->v2->cageco;
      }

      /* Last vertex in screen space. */
      float end_ss[2];
      ED_view3d_project_float_global(kcd->region, end, end_ss, V3D_PROJ_TEST_NOP);

      knifetool_draw_angle(
          kcd, kcd->prev.cage, kcd->curr.cage, end, kcd->prev.mval, kcd->curr.mval, end_ss, angle);
    }
  }

  if (kcd->prev.vert) {
    kfv = kcd->prev.vert;
    float min_angle = FLT_MAX;
    float angle = 0.0f;
    float *end;

    /* If using relative angle snapping, always draw angle to reference edge. */
    if (kcd->is_angle_snapping && kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_RELATIVE) {
      kfe = kcd->snap_ref_edge;
      if (kfe->v1 != kfv) {
        tempkfv = kfe->v1;
      }
      else {
        tempkfv = kfe->v2;
      }
      min_angle = angle_v3v3v3(kcd->curr.cage, kcd->prev.cage, tempkfv->cageco);
      end = tempkfv->cageco;
    }
    else {
      /* Choose minimum angle edge. */
      kfe = ((Ref *)kfv->edges.first)->ref;
      for (ref = kfv->edges.first; ref; ref = ref->next) {
        tempkfe = ref->ref;
        if (tempkfe->v1 != kfv) {
          tempkfv = tempkfe->v1;
        }
        else {
          tempkfv = tempkfe->v2;
        }
        angle = angle_v3v3v3(kcd->curr.cage, kcd->prev.cage, tempkfv->cageco);
        if (angle < min_angle) {
          min_angle = angle;
          kfe = tempkfe;
          end = tempkfv->cageco;
        }
      }
    }

    if (min_angle > KNIFE_FLT_EPSBIG) {
      /* Last vertex in screen space. */
      float end_ss[2];
      ED_view3d_project_float_global(kcd->region, end, end_ss, V3D_PROJ_TEST_NOP);

      knifetool_draw_angle(kcd,
                           kcd->curr.cage,
                           kcd->prev.cage,
                           end,
                           kcd->curr.mval,
                           kcd->prev.mval,
                           end_ss,
                           min_angle);
    }
  }
  else if (kcd->prev.edge) {
    /* Determine acute angle. */
    kfe = kcd->prev.edge;
    float angle1 = angle_v3v3v3(kcd->curr.cage, kcd->prev.cage, kfe->v1->cageco);
    float angle2 = angle_v3v3v3(kcd->curr.cage, kcd->prev.cage, kfe->v2->cageco);

    float angle;
    float *end;
    /* kcd->prev.edge can have one vertex part of cut and one part of mesh? */
    /* This never seems to happen for kcd->curr.edge. */
    if ((!kcd->prev.vert || kcd->prev.vert->v == kfe->v1->v) || kfe->v1->is_cut) {
      angle = angle2;
      end = kfe->v2->cageco;
    }
    else if ((!kcd->prev.vert || kcd->prev.vert->v == kfe->v2->v) || kfe->v2->is_cut) {
      angle = angle1;
      end = kfe->v1->cageco;
    }
    else {
      if (angle1 < angle2) {
        angle = angle1;
        end = kfe->v1->cageco;
      }
      else {
        angle = angle2;
        end = kfe->v2->cageco;
      }
    }

    /* Last vertex in screen space. */
    float end_ss[2];
    ED_view3d_project_float_global(kcd->region, end, end_ss, V3D_PROJ_TEST_NOP);

    knifetool_draw_angle(
        kcd, kcd->curr.cage, kcd->prev.cage, end, kcd->curr.mval, kcd->prev.mval, end_ss, angle);
  }
  else if (kcd->mdata.is_stored && !kcd->prev.is_space) {
    float angle = angle_v3v3v3(kcd->curr.cage, kcd->prev.cage, kcd->mdata.cage);
    knifetool_draw_angle(kcd,
                         kcd->curr.cage,
                         kcd->prev.cage,
                         kcd->mdata.cage,
                         kcd->curr.mval,
                         kcd->prev.mval,
                         kcd->mdata.mval,
                         angle);
  }
}

static void knifetool_draw_dist_angle(const KnifeTool_OpData *kcd)
{
  switch (kcd->dist_angle_mode) {
    case KNF_MEASUREMENT_BOTH: {
      knifetool_draw_visible_distances(kcd);
      knifetool_draw_visible_angles(kcd);
      break;
    }
    case KNF_MEASUREMENT_DISTANCE: {
      knifetool_draw_visible_distances(kcd);
      break;
    }
    case KNF_MEASUREMENT_ANGLE: {
      knifetool_draw_visible_angles(kcd);
      break;
    }
  }
}

/* Modal loop selection drawing callback. */
static void knifetool_draw(const bContext *UNUSED(C), ARegion *UNUSED(region), void *arg)
{
  const KnifeTool_OpData *kcd = arg;
  GPU_depth_test(GPU_DEPTH_NONE);

  GPU_matrix_push_projection();
  GPU_polygon_offset(1.0f, 1.0f);

  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  if (kcd->mode == MODE_DRAGGING) {
    immUniformColor3ubv(kcd->colors.line);
    GPU_line_width(2.0);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex3fv(pos, kcd->prev.cage);
    immVertex3fv(pos, kcd->curr.cage);
    immEnd();
  }

  if (kcd->prev.vert) {
    immUniformColor3ubv(kcd->colors.point);
    GPU_point_size(11 * UI_DPI_FAC);

    immBegin(GPU_PRIM_POINTS, 1);
    immVertex3fv(pos, kcd->prev.cage);
    immEnd();
  }

  if (kcd->prev.bmface || kcd->prev.edge) {
    immUniformColor3ubv(kcd->colors.curpoint);
    GPU_point_size(9 * UI_DPI_FAC);

    immBegin(GPU_PRIM_POINTS, 1);
    immVertex3fv(pos, kcd->prev.cage);
    immEnd();
  }

  if (kcd->curr.vert) {
    immUniformColor3ubv(kcd->colors.point);
    GPU_point_size(11 * UI_DPI_FAC);

    immBegin(GPU_PRIM_POINTS, 1);
    immVertex3fv(pos, kcd->curr.cage);
    immEnd();
  }
  else if (kcd->curr.edge) {
    immUniformColor3ubv(kcd->colors.edge);
    GPU_line_width(2.0);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex3fv(pos, kcd->curr.edge->v1->cageco);
    immVertex3fv(pos, kcd->curr.edge->v2->cageco);
    immEnd();
  }

  if (kcd->curr.bmface || kcd->curr.edge) {
    immUniformColor3ubv(kcd->colors.curpoint);
    GPU_point_size(9 * UI_DPI_FAC);

    immBegin(GPU_PRIM_POINTS, 1);
    immVertex3fv(pos, kcd->curr.cage);
    immEnd();
  }

  if (kcd->depth_test) {
    GPU_depth_test(GPU_DEPTH_LESS_EQUAL);
  }

  if (kcd->totkedge > 0) {
    BLI_mempool_iter iter;
    KnifeEdge *kfe;

    immUniformColor3ubv(kcd->colors.line);
    GPU_line_width(1.0);

    GPUBatch *batch = immBeginBatchAtMost(GPU_PRIM_LINES, BLI_mempool_len(kcd->kedges) * 2);

    BLI_mempool_iternew(kcd->kedges, &iter);
    for (kfe = BLI_mempool_iterstep(&iter); kfe; kfe = BLI_mempool_iterstep(&iter)) {
      if (!kfe->is_cut || kfe->is_invalid) {
        continue;
      }

      immVertex3fv(pos, kfe->v1->cageco);
      immVertex3fv(pos, kfe->v2->cageco);
    }

    immEnd();

    GPU_batch_draw(batch);
    GPU_batch_discard(batch);
  }

  if (kcd->totkvert > 0) {
    BLI_mempool_iter iter;
    KnifeVert *kfv;

    immUniformColor3ubv(kcd->colors.point);
    GPU_point_size(5.0 * UI_DPI_FAC);

    GPUBatch *batch = immBeginBatchAtMost(GPU_PRIM_POINTS, BLI_mempool_len(kcd->kverts));

    BLI_mempool_iternew(kcd->kverts, &iter);
    for (kfv = BLI_mempool_iterstep(&iter); kfv; kfv = BLI_mempool_iterstep(&iter)) {
      if (!kfv->is_cut || kfv->is_invalid) {
        continue;
      }

      immVertex3fv(pos, kfv->cageco);
    }

    immEnd();

    GPU_batch_draw(batch);
    GPU_batch_discard(batch);
  }

  /* Draw relative angle snapping reference edge. */
  if (kcd->is_angle_snapping && kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_RELATIVE) {
    immUniformColor3ubv(kcd->colors.edge_extra);
    GPU_line_width(2.0);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex3fv(pos, kcd->snap_ref_edge->v1->cageco);
    immVertex3fv(pos, kcd->snap_ref_edge->v2->cageco);
    immEnd();
  }

  if (kcd->totlinehit > 0) {
    KnifeLineHit *lh;
    int i, snapped_verts_count, other_verts_count;
    float fcol[4];

    GPU_blend(GPU_BLEND_ALPHA);

    GPUVertBuf *vert = GPU_vertbuf_create_with_format(format);
    GPU_vertbuf_data_alloc(vert, kcd->totlinehit);

    lh = kcd->linehits;
    for (i = 0, snapped_verts_count = 0, other_verts_count = 0; i < kcd->totlinehit; i++, lh++) {
      if (lh->v) {
        GPU_vertbuf_attr_set(vert, pos, snapped_verts_count++, lh->cagehit);
      }
      else {
        GPU_vertbuf_attr_set(vert, pos, kcd->totlinehit - 1 - other_verts_count++, lh->cagehit);
      }
    }

    GPUBatch *batch = GPU_batch_create_ex(GPU_PRIM_POINTS, vert, NULL, GPU_BATCH_OWNS_VBO);
    GPU_batch_program_set_builtin(batch, GPU_SHADER_3D_UNIFORM_COLOR);

    /* Draw any snapped verts first. */
    rgba_uchar_to_float(fcol, kcd->colors.point_a);
    GPU_batch_uniform_4fv(batch, "color", fcol);
    GPU_point_size(11 * UI_DPI_FAC);
    if (snapped_verts_count > 0) {
      GPU_batch_draw_range(batch, 0, snapped_verts_count);
    }

    /* Now draw the rest. */
    rgba_uchar_to_float(fcol, kcd->colors.curpoint_a);
    GPU_batch_uniform_4fv(batch, "color", fcol);
    GPU_point_size(7 * UI_DPI_FAC);
    if (other_verts_count > 0) {
      GPU_batch_draw_range(batch, snapped_verts_count, other_verts_count);
    }

    GPU_batch_discard(batch);

    GPU_blend(GPU_BLEND_NONE);
  }

  immUnbindProgram();

  GPU_depth_test(GPU_DEPTH_NONE);

  if (kcd->mode == MODE_DRAGGING) {
    if (kcd->is_angle_snapping) {
      knifetool_draw_angle_snapping(kcd);
    }
    else if (kcd->axis_constrained) {
      knifetool_draw_orientation_locking(kcd);
    }

    if (kcd->show_dist_angle) {
      knifetool_draw_dist_angle(kcd);
    }
  }

  GPU_matrix_pop_projection();

  /* Reset default. */
  GPU_depth_test(GPU_DEPTH_LESS_EQUAL);
}

/* -------------------------------------------------------------------- */
/** \name Header
 * \{ */

static void knife_update_header(bContext *C, wmOperator *op, KnifeTool_OpData *kcd)
{
  char header[UI_MAX_DRAW_STR];
  char buf[UI_MAX_DRAW_STR];

  char *p = buf;
  int available_len = sizeof(buf);

#define WM_MODALKEY(_id) \
  WM_modalkeymap_operator_items_to_string_buf( \
      op->type, (_id), true, UI_MAX_SHORTCUT_STR, &available_len, &p)

  BLI_snprintf(
      header,
      sizeof(header),
      TIP_("%s: confirm, %s: cancel, %s: undo, "
           "%s: start/define cut, %s: close cut, %s: new cut, "
           "%s: midpoint snap (%s), %s: ignore snap (%s), "
           "%s: angle constraint %.2f(%.2f) (%s%s%s%s), %s: cut through (%s), "
           "%s: panning, %s%s%s: orientation lock (%s), "
           "%s: distance/angle measurements (%s), "
           "%s: x-ray (%s)"),
      WM_MODALKEY(KNF_MODAL_CONFIRM),
      WM_MODALKEY(KNF_MODAL_CANCEL),
      WM_MODALKEY(KNF_MODAL_UNDO),
      WM_MODALKEY(KNF_MODAL_ADD_CUT),
      WM_MODALKEY(KNF_MODAL_ADD_CUT_CLOSED),
      WM_MODALKEY(KNF_MODAL_NEW_CUT),
      WM_MODALKEY(KNF_MODAL_MIDPOINT_ON),
      WM_bool_as_string(kcd->snap_midpoints),
      WM_MODALKEY(KNF_MODAL_IGNORE_SNAP_ON),
      WM_bool_as_string(kcd->ignore_edge_snapping),
      WM_MODALKEY(KNF_MODAL_ANGLE_SNAP_TOGGLE),
      (kcd->angle >= 0.0f) ? RAD2DEGF(kcd->angle) : 360.0f + RAD2DEGF(kcd->angle),
      (kcd->angle_snapping_increment > KNIFE_MIN_ANGLE_SNAPPING_INCREMENT &&
       kcd->angle_snapping_increment <= KNIFE_MAX_ANGLE_SNAPPING_INCREMENT) ?
          kcd->angle_snapping_increment :
          KNIFE_DEFAULT_ANGLE_SNAPPING_INCREMENT,
      kcd->angle_snapping ?
          ((kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_SCREEN) ? "Screen" : "Relative") :
          "OFF",
      /* TODO: Can this be simplified? */
      (kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_RELATIVE) ? " - " : "",
      (kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_RELATIVE) ?
          WM_MODALKEY(KNF_MODAL_CYCLE_ANGLE_SNAP_EDGE) :
          "",
      (kcd->angle_snapping_mode == KNF_CONSTRAIN_ANGLE_MODE_RELATIVE) ? ": cycle edge" : "",
      /**/
      WM_MODALKEY(KNF_MODAL_CUT_THROUGH_TOGGLE),
      WM_bool_as_string(kcd->cut_through),
      WM_MODALKEY(KNF_MODAL_PANNING),
      WM_MODALKEY(KNF_MODAL_X_AXIS),
      WM_MODALKEY(KNF_MODAL_Y_AXIS),
      WM_MODALKEY(KNF_MODAL_Z_AXIS),
      (kcd->axis_constrained ? kcd->axis_string : WM_bool_as_string(kcd->axis_constrained)),
      WM_MODALKEY(KNF_MODAL_SHOW_DISTANCE_ANGLE_TOGGLE),
      WM_bool_as_string(kcd->show_dist_angle),
      WM_MODALKEY(KNF_MODAL_DEPTH_TEST_TOGGLE),
      WM_bool_as_string(!kcd->depth_test));

#undef WM_MODALKEY

  ED_workspace_status_text(C, header);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Knife BVH Utils
 * \{ */

static bool knife_bm_face_is_select(BMFace *f)
{
  return (BM_elem_flag_test(f, BM_ELEM_SELECT) != 0);
}

static bool knife_bm_face_is_not_hidden(BMFace *f)
{
  return (BM_elem_flag_test(f, BM_ELEM_HIDDEN) == 0);
}

static void knife_bvh_init(KnifeTool_OpData *kcd)
{
  Object *ob;
  BMEditMesh *em;

  /* Test Function. */
  bool (*test_fn)(BMFace *);
  if ((kcd->only_select && kcd->cut_through)) {
    test_fn = knife_bm_face_is_select;
  }
  else {
    test_fn = knife_bm_face_is_not_hidden;
  }

  /* Construct BVH Tree. */
  float cos[3][3];
  const float epsilon = FLT_EPSILON * 2.0f;
  int tottri = 0;
  int ob_tottri = 0;
  BMLoop *(*looptris)[3];
  BMFace *f_test = NULL, *f_test_prev = NULL;
  bool test_fn_ret = false;

  /* Calculate tottri. */
  for (uint b = 0; b < kcd->objects_len; b++) {
    ob_tottri = 0;
    ob = kcd->objects[b];
    em = BKE_editmesh_from_object(ob);

    for (int i = 0; i < em->tottri; i++) {
      f_test = em->looptris[i][0]->f;
      if (f_test != f_test_prev) {
        test_fn_ret = test_fn(f_test);
        f_test_prev = f_test;
      }

      if (test_fn_ret) {
        ob_tottri++;
      }
    }

    tottri += ob_tottri;
  }

  kcd->bvh.tree = BLI_bvhtree_new(tottri, epsilon, 8, 8);

  f_test_prev = NULL;
  test_fn_ret = false;

  /* Add tri's for each object.
   * TODO:
   * test_fn can leave large gaps between bvh tree indices.
   * Compacting bvh tree indices may be possible.
   * Don't forget to update #knife_bvh_intersect_plane!
   */
  tottri = 0;
  for (uint b = 0; b < kcd->objects_len; b++) {
    ob = kcd->objects[b];
    em = BKE_editmesh_from_object(ob);
    looptris = em->looptris;

    for (int i = 0; i < em->tottri; i++) {

      f_test = looptris[i][0]->f;
      if (f_test != f_test_prev) {
        test_fn_ret = test_fn(f_test);
        f_test_prev = f_test;
      }

      if (!test_fn_ret) {
        continue;
      }

      copy_v3_v3(cos[0], kcd->cagecos[b][BM_elem_index_get(looptris[i][0]->v)]);
      copy_v3_v3(cos[1], kcd->cagecos[b][BM_elem_index_get(looptris[i][1]->v)]);
      copy_v3_v3(cos[2], kcd->cagecos[b][BM_elem_index_get(looptris[i][2]->v)]);

      /* Convert to world-space. */
      mul_m4_v3(ob->obmat, cos[0]);
      mul_m4_v3(ob->obmat, cos[1]);
      mul_m4_v3(ob->obmat, cos[2]);

      BLI_bvhtree_insert(kcd->bvh.tree, i + tottri, (float *)cos, 3);
    }

    tottri += em->tottri;
  }

  BLI_bvhtree_balance(kcd->bvh.tree);
}

/* Wrapper for #BLI_bvhtree_free. */
static void knife_bvh_free(KnifeTool_OpData *kcd)
{
  if (kcd->bvh.tree) {
    BLI_bvhtree_free(kcd->bvh.tree);
    kcd->bvh.tree = NULL;
  }
}

static void knife_bvh_raycast_cb(void *userdata,
                                 int index,
                                 const BVHTreeRay *ray,
                                 BVHTreeRayHit *hit)
{
  KnifeTool_OpData *kcd = userdata;
  BMLoop **ltri;
  Object *ob;
  BMEditMesh *em;

  float dist, uv[2];
  bool isect;
  int tottri;
  float tri_cos[3][3];

  if (index != -1) {
    tottri = 0;
    uint b = 0;
    for (; b < kcd->objects_len; b++) {
      index -= tottri;
      ob = kcd->objects[b];
      em = BKE_editmesh_from_object(ob);
      tottri = em->tottri;
      if (index < tottri) {
        ltri = em->looptris[index];
        break;
      }
    }

    if (kcd->bvh.filter_cb) {
      if (!kcd->bvh.filter_cb(ltri[0]->f, kcd->bvh.filter_data)) {
        return;
      }
    }

    copy_v3_v3(tri_cos[0], kcd->cagecos[b][BM_elem_index_get(ltri[0]->v)]);
    copy_v3_v3(tri_cos[1], kcd->cagecos[b][BM_elem_index_get(ltri[1]->v)]);
    copy_v3_v3(tri_cos[2], kcd->cagecos[b][BM_elem_index_get(ltri[2]->v)]);
    mul_m4_v3(ob->obmat, tri_cos[0]);
    mul_m4_v3(ob->obmat, tri_cos[1]);
    mul_m4_v3(ob->obmat, tri_cos[2]);

    isect =
        (ray->radius > 0.0f ?
             isect_ray_tri_epsilon_v3(ray->origin,
                                      ray->direction,
                                      tri_cos[0],
                                      tri_cos[1],
                                      tri_cos[2],
                                      &dist,
                                      uv,
                                      ray->radius) :
#ifdef USE_KDOPBVH_WATERTIGHT
             isect_ray_tri_watertight_v3(
                 ray->origin, ray->isect_precalc, tri_cos[0], tri_cos[1], tri_cos[2], &dist, uv));
#else
             isect_ray_tri_v3(
                 ray->origin, ray->direction, tri_cos[0], tri_cos[1], tri_cos[2], &dist, uv));
#endif

    if (isect && dist < hit->dist) {
      hit->dist = dist;
      hit->index = index;

      copy_v3_v3(hit->no, ltri[0]->f->no);

      madd_v3_v3v3fl(hit->co, ray->origin, ray->direction, dist);

      kcd->bvh.looptris = em->looptris;
      copy_v2_v2(kcd->bvh.uv, uv);
      kcd->bvh.base_index = b;
    }
  }
}

/* `co` is expected to be in world space. */
static BMFace *knife_bvh_raycast(KnifeTool_OpData *kcd,
                                 const float co[3],
                                 const float dir[3],
                                 const float radius,
                                 float *r_dist,
                                 float r_hitout[3],
                                 float r_cagehit[3],
                                 uint *r_base_index)
{
  BMFace *face;
  BMLoop **ltri;
  BVHTreeRayHit hit;
  const float dist = r_dist ? *r_dist : FLT_MAX;
  hit.dist = dist;
  hit.index = -1;

  BLI_bvhtree_ray_cast(kcd->bvh.tree, co, dir, radius, &hit, knife_bvh_raycast_cb, kcd);

  /* Handle Hit */
  if (hit.index != -1 && hit.dist != dist) {
    face = kcd->bvh.looptris[hit.index][0]->f;

    /* Hits returned in world space. */
    if (r_hitout) {
      ltri = kcd->bvh.looptris[hit.index];
      interp_v3_v3v3v3_uv(r_hitout, ltri[0]->v->co, ltri[1]->v->co, ltri[2]->v->co, kcd->bvh.uv);

      if (r_cagehit) {
        copy_v3_v3(r_cagehit, hit.co);
      }
    }

    if (r_dist) {
      *r_dist = hit.dist;
    }

    if (r_base_index) {
      *r_base_index = kcd->bvh.base_index;
    }

    return face;
  }
  return NULL;
}

/* `co` is expected to be in world space. */
static BMFace *knife_bvh_raycast_filter(KnifeTool_OpData *kcd,
                                        const float co[3],
                                        const float dir[3],
                                        const float radius,
                                        float *r_dist,
                                        float r_hitout[3],
                                        float r_cagehit[3],
                                        uint *r_base_index,
                                        bool (*filter_cb)(BMFace *f, void *userdata),
                                        void *filter_userdata)
{
  kcd->bvh.filter_cb = filter_cb;
  kcd->bvh.filter_data = filter_userdata;

  BMFace *face;
  BMLoop **ltri;
  BVHTreeRayHit hit;
  const float dist = r_dist ? *r_dist : FLT_MAX;
  hit.dist = dist;
  hit.index = -1;

  BLI_bvhtree_ray_cast(kcd->bvh.tree, co, dir, radius, &hit, knife_bvh_raycast_cb, kcd);

  kcd->bvh.filter_cb = NULL;
  kcd->bvh.filter_data = NULL;

  /* Handle Hit */
  if (hit.index != -1 && hit.dist != dist) {
    face = kcd->bvh.looptris[hit.index][0]->f;

    /* Hits returned in world space. */
    if (r_hitout) {
      ltri = kcd->bvh.looptris[hit.index];
      interp_v3_v3v3v3_uv(r_hitout, ltri[0]->v->co, ltri[1]->v->co, ltri[2]->v->co, kcd->bvh.uv);

      if (r_cagehit) {
        copy_v3_v3(r_cagehit, hit.co);
      }
    }

    if (r_dist) {
      *r_dist = hit.dist;
    }

    if (r_base_index) {
      *r_base_index = kcd->bvh.base_index;
    }

    return face;
  }
  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Geometry Utils
 * \{ */

static void knife_project_v2(const KnifeTool_OpData *kcd, const float co[3], float sco[2])
{
  ED_view3d_project_float_global(kcd->region, co, sco, V3D_PROJ_TEST_NOP);
}

/* Ray is returned in world space. */
static void knife_input_ray_segment(KnifeTool_OpData *kcd,
                                    const float mval[2],
                                    const float ofs,
                                    float r_origin[3],
                                    float r_origin_ofs[3])
{
  /* Unproject to find view ray. */
  ED_view3d_unproject_v3(kcd->vc.region, mval[0], mval[1], 0.0f, r_origin);
  ED_view3d_unproject_v3(kcd->vc.region, mval[0], mval[1], ofs, r_origin_ofs);
}

/* No longer used, but may be useful in the future. */
static void UNUSED_FUNCTION(knifetool_recast_cageco)(KnifeTool_OpData *kcd,
                                                     float mval[3],
                                                     float r_cage[3])
{
  float origin[3];
  float origin_ofs[3];
  float ray[3], ray_normal[3];
  float co[3]; /* Unused. */

  knife_input_ray_segment(kcd, mval, 1.0f, origin, origin_ofs);

  sub_v3_v3v3(ray, origin_ofs, origin);
  normalize_v3_v3(ray_normal, ray);

  knife_bvh_raycast(kcd, origin, ray_normal, 0.0f, NULL, co, r_cage, NULL);
}

static bool knife_verts_edge_in_face(KnifeVert *v1, KnifeVert *v2, BMFace *f)
{
  bool v1_inside, v2_inside;
  bool v1_inface, v2_inface;
  BMLoop *l1, *l2;

  if (!f || !v1 || !v2) {
    return false;
  }

  l1 = v1->v ? BM_face_vert_share_loop(f, v1->v) : NULL;
  l2 = v2->v ? BM_face_vert_share_loop(f, v2->v) : NULL;

  if ((l1 && l2) && BM_loop_is_adjacent(l1, l2)) {
    /* Boundary-case, always false to avoid edge-in-face checks below. */
    return false;
  }

  /* Find out if v1 and v2, if set, are part of the face. */
  v1_inface = (l1 != NULL);
  v2_inface = (l2 != NULL);

  /* BM_face_point_inside_test uses best-axis projection so this isn't most accurate test... */
  v1_inside = v1_inface ? false : BM_face_point_inside_test(f, v1->co);
  v2_inside = v2_inface ? false : BM_face_point_inside_test(f, v2->co);
  if ((v1_inface && v2_inside) || (v2_inface && v1_inside) || (v1_inside && v2_inside)) {
    return true;
  }

  if (v1_inface && v2_inface) {
    float mid[3];
    /* Can have case where v1 and v2 are on shared chain between two faces.
     * BM_face_splits_check_legal does visibility and self-intersection tests,
     * but it is expensive and maybe a bit buggy, so use a simple
     * "is the midpoint in the face" test. */
    mid_v3_v3v3(mid, v1->co, v2->co);
    return BM_face_point_inside_test(f, mid);
  }
  return false;
}

static void knife_recalc_ortho(KnifeTool_OpData *kcd)
{
  kcd->is_ortho = ED_view3d_clip_range_get(
      kcd->vc.depsgraph, kcd->vc.v3d, kcd->vc.rv3d, &kcd->clipsta, &kcd->clipend, true);
}
