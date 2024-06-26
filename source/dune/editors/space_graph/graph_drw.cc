#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "lib_dunelib.h"
#include "lib_math_vector_types.hh"
#include "lib_utildefines.h"
#include "lib_vector.hh"

#include "types_anim.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_userdef.h"
#include "types_win.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_cxt.hh"
#include "dune_curve.hh"
#include "dune_fcurve.h"
#include "dune_nla.h"

#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gou_state.h"

#include "ed_anim_api.hh"

#include "graph_intern.h"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

static void graph_drw_driver_debug(AnimCxt *ac, Id *id, FCurve *fcu);

/* Util Drwing Defines */
/* determine the alpha val that should be used when
 * drawing components for some F-Curve (fcu)
 * - selected F-Curves should be more visible than partially visible ones */
static float fcurve_display_alpha(FCurve *fcu)
{
  return (fcu->flag & FCURVE_SELECTED) ? 1.0f : U.fcu_inactive_alpha;
}

/* Get the first and last index to the bezt array that are just outside min and max. */
static dune::int2 get_bounding_bezt_indices(FCurve *fcu, const float min, const float max)
{
  bool replace;
  int first, last;
  first = dune_fcurve_bezt_binarysearch_index(fcu->bezt, min, fcu->totvert, &replace);
  first = clamp_i(first - 1, 0, fcu->totvert - 1);

  last = dune_fcurve_bezt_binarysearch_index(fcu->bezt, max, fcu->totvert, &replace);
  last = replace ? last + 1 : last;
  last = clamp_i(last, 0, fcu->totvert - 1);
  return {first, last};
}

/* FCurve Mod Drwing */

/* Envelope */

/* TODO: draw a shaded poly showing the region of influence too!!! */
/* param adt_nla_remap: Send nullptr if no NLA remapping necessary. */
static void draw_fcurve_mod_ctrls_envelope(FMod *fcm,
                                              View2D *v2d,
                                              AnimData *adt_nla_remap)
{
  FModEnvelope *env = (FModEnvelope *)fcm->data;
  FCMEnvelopeData *fed;
  const float fac = 0.05f * lib_rctf_size_x(&v2d->cur);
  int i;

  const uint shdr_pos = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  gpu_line_width(1.0f);

  immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  gpu_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2] / UI_SCALE_FAC, viewport_size[3] / UI_SCALE_FAC);
  immUniform1i("colors_len", 0); /* Simple dashes. */
  immUniformColor3f(0.0f, 0.0f, 0.0f);
  immUniform1f("dash_width", 10.0f);
  immUniform1f("udash_factor", 0.5f);

  /* draw two black lines showing the standard reference levels */
  immBegin(GPU_PRIM_LINES, 4);
  immVertex2f(shdr_pos, v2d->cur.xmin, env->midval + env->min);
  immVertex2f(shdr_pos, v2d->cur.xmax, env->midval + env->min);

  immVertex2f(shdr_pos, v2d->cur.xmin, env->midval + env->max);
  immVertex2f(shdr_pos, v2d->cur.xmax, env->midval + env->max);
  immEnd();

  immUnbindProgram();

  if (env->totvert > 0) {
    /* set size of vertices (non-adjustable for now) */
    gpu_point_size(2.0f);

    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

    /* for now, point color is fixed, and is white */
    immUniformColor3f(1.0f, 1.0f, 1.0f);

    immBeginAtMost(GPU_PRIM_POINTS, env->totvert * 2);

    for (i = 0, fed = env->data; i < env->totvert; i++, fed++) {
      const float env_scene_time = dune_nla_tweakedit_remap(
          adt_nla_remap, fed->time, NLATIME_CONVERT_MAP);

      /* only draw if visible
       * - min/max here are fixed, not relative */
      if (IN_RANGE(env_scene_time, (v2d->cur.xmin - fac), (v2d->cur.xmax + fac))) {
        immVertex2f(shdr_pos, env_scene_time, fed->min);
        immVertex2f(shdr_pos, env_scene_time, fed->max);
      }
    }

    immEnd();

    immUnbindProgram();
  }
}

/* FCurve Mod Drwing */
/* Points */

/* helper fn - set color to drw F-Curve data w */
static void set_fcurve_vertex_color(FCurve *fcu, bool sel)
{
  float color[4];
  float diff;

  /* Set color of curve vertex based on state of curve (i.e. 'Edit' Mode) */
  if ((fcu->flag & FCURVE_PROTECTED) == 0) {
    /* Curve's points ARE BEING edited */
    ui_GetThemeColor3fv(sel ? TH_VERTEX_SEL : TH_VERTEX, color);
  }
  else {
    /* Curve's points CANNOT BE edited */
    ui_GetThemeColorShade4fv(TH_HEADER, 50, color);
  }

  /* Fade the 'intensity' of the vertices based on the sel of the curves too
   * - Only fade by 50% the amount the curves were faded by, so that the points
   *   still stand out for easier sel */
  diff = 1.0f - fcurve_display_alpha(fcu);
  color[3] = 1.0f - (diff * 0.5f);
  CLAMP(color[3], 0.2f, 1.0f);

  immUniformColor4fv(color);
}

/* Drw a cross at the given position. Shader must already be bound.
 * NOTE: the caller MUST HAVE GL_LINE_SMOOTH & GL_BLEND ENABLED, otherwise the controls don't
 * have a consistent appearance (due to off-pixel alignments) */
static void draw_cross(float position[2], float scale[2], uint attr_id)
{
  gpu_matrix_push();
  gpu_matrix_translate_2fv(position);
  gpu_matrix_scale_2f(1.0f / scale[0], 1.0f / scale[1]);

  /* Drw X shape. */
  const float line_length = 0.7f;
  immBegin(GPU_PRIM_LINES, 4);
  immVertex2f(attr_id, -line_length, -line_length);
  immVertex2f(attr_id, +line_length, +line_length);

  immVertex2f(attr_id, -line_length, +line_length);
  immVertex2f(attr_id, +line_length, -line_length);
  immEnd();

  gpu_matrix_pop();
}

static void draw_fcurve_sel_keyframe_vertices(FCurve *fcu, View2D *v2d, bool sel, uint pos)
{
  const float fac = 0.05f * lib_rctf_size_x(&v2d->cur);

  set_fcurve_vertex_color(fcu, sel);

  immBeginAtMost(GPU_PRIM_POINTS, fcu->totvert);

  BezTriple *bezt = fcu->bezt;
  for (int i = 0; i < fcu->totvert; i++, bezt++) {
    /* As an optimization step, only draw those in view
     * We apply a correction factor to ensure that points
     *   don't pop in/out due to slight twitches of view size */
    if (IN_RANGE(bezt->vec[1][0], (v2d->cur.xmin - fac), (v2d->cur.xmax + fac))) {
      /* 'Keyframe' vertex only, as handle lines and handles have alrdy been drawn
       * - only drw those with correct sel state for the current drawing colo */
      if ((bezt->f2 & SEL) == sel) {
        immVertex2fv(pos, bezt->vec[1]);
      }
    }
  }

  immEnd();
}

/* Drw the extra indicator for the active point */
static void drw_fcurve_active_vertex(const FCurve *fcu, const View2D *v2d, const uint pos)
{
  const int active_keyframe_index = dune_fcurve_active_keyframe_index(fcu);
  if (!(fcu->flag & FCURVE_ACTIVE) || active_keyframe_index == FCURVE_ACTIVE_KEYFRAME_NONE) {
    return;
  }

  const float fac = 0.05f * lib_rctf_size_x(&v2d->cur);
  const BezTriple *bezt = &fcu->bezt[active_keyframe_index];

  if (!IN_RANGE(bezt->vec[1][0], (v2d->cur.xmin - fac), (v2d->cur.xmax + fac))) {
    return;
  }
  if (!(bezt->f2 & SEL)) {
    return;
  }

  immBegin(GPU_PRIM_POINTS, 1);
  immUniformThemeColor(TH_VERTEX_ACTIVE);
  immVertex2fv(pos, bezt->vec[1]);
  immEnd();
}

/* helper fn, drw keyframe vertices only for an F-Curve */
static void draw_fcurve_keyframe_vertices(FCurve *fcu, View2D *v2d, const uint pos)
{
  immBindBuiltinProgram(GPU_SHADER_2D_POINT_UNIFORM_SIZE_UNIFORM_COLOR_AA);

  if ((fcu->flag & FCURVE_PROTECTED) == 0) {
    immUniform1f("size", ui_GetThemeValuef(TH_VERTEX_SIZE) * UI_SCALE_FAC);
  }
  else {
    /* Drw keyframes on locked curves slightly smaller to give them less visual weight. */
    immUniform1f("size", (ui_GetThemeValuef(TH_VERTEX_SIZE) * UI_SCALE_FAC) * 0.8f);
  }

  drw_fcurve_sel_keyframe_vertices(fcu, v2d, false, pos);
  drw_fcurve_sel_keyframe_vertices(fcu, v2d, true, pos);
  drw_fcurve_active_vertex(fcu, v2d, pos);

  immUnbindProgram();
}

/* helper fn, drw handle vertices only for an F-Curve (if it is not protected) */
static void draw_fcurve_selected_handle_vertices(
    FCurve *fcu, View2D *v2d, bool sel, bool sel_handle_only, uint pos)
{
  const dune::int2 bounding_indices = get_bounding_bezt_indices(
      fcu, v2d->cur.xmin, v2d->cur.xmax);

  /* set handle color */
  float hcolor[3];
  ui_GetThemeColor3fv(sel ? TH_HANDLE_VERTEX_SEL : TH_HANDLE_VERTEX, hcolor);
  immUniform4f("outlineColor", hcolor[0], hcolor[1], hcolor[2], 1.0f);
  immUniformColor3fvAlpha(hcolor, 0.01f); /* almost invisible only keep for smoothness */

  immBeginAtMost(GPU_PRIM_POINTS, fcu->totvert * 2);

  BezTriple *prevbezt = nullptr;
  for (int i = bounding_indices[0]; i <= bounding_indices[1]; i++) {
    BezTriple *bezt = &fcu->bezt[i];
    /* Drw the editmode handles for a bezier curve (others don't have handles)
     * if their sel status matches the sel status we're drwing for
     * 1st handle only if previous beztriple was bezier-mode
     * 2nd handle only if current beztriple is bezier-mode
     *
     * Also, need to take into account whether the keyframe was selected
     * if a Graph Editor option to only show handles of sel keys is on. */
    if (!sel_handle_only || BEZT_ISSEL_ANY(bezt)) {
      if ((!prevbezt && (bezt->ipo == BEZT_IPO_BEZ)) ||
          (prevbezt && (prevbezt->ipo == BEZT_IPO_BEZ))) {
        if ((bezt->f1 & SEL) == sel
            /* && v2d->cur.xmin < bezt->vec[0][0] < v2d->cur.xmax) */)
        {
          immVertex2fv(pos, bezt->vec[0]);
        }
      }

      if (bezt->ipo == BEZT_IPO_BEZ) {
        if ((bezt->f3 & SEL) == sel
            /* && v2d->cur.xmin < bezt->vec[2][0] < v2d->cur.xmax) */)
        {
          immVertex2fv(pos, bezt->vec[2]);
        }
      }
    }
    prevbezt = bezt;
  }

  immEnd();
}

/* Drw the extra handles for the active point */
static void drw_fcurve_active_handle_vertices(const FCurve *fcu,
                                               const bool sel_handle_only,
                                               const uint pos)
{
  const int active_keyframe_index = dune_fcurve_active_keyframe_index(fcu);
  if (!(fcu->flag & FCURVE_ACTIVE) || active_keyframe_index == FCURVE_ACTIVE_KEYFRAME_NONE) {
    return;
  }

  const BezTriple *bezt = &fcu->bezt[active_keyframe_index];

  if (sel_handle_only && !BEZT_ISSEL_ANY(bezt)) {
    return;
  }

  float active_col[4];
  ui_GetThemeColor4fv(TH_VERTEX_ACTIVE, active_col);
  immUniform4fv("outlineColor", active_col);
  immUniformColor3fvAlpha(active_col, 0.01f); /* Almost invisible only keep for smoothness. */
  immBeginAtMost(GPU_PRIM_POINTS, 2);

  const BezTriple *left_bezt = active_keyframe_index > 0 ? &fcu->bezt[active_keyframe_index - 1] :
                                                           bezt;
  if (left_bezt->ipo == BEZT_IPO_BEZ && (bezt->f1 & SELECT)) {
    immVertex2fv(pos, bezt->vec[0]);
  }
  if (bezt->ipo == BEZT_IPO_BEZ && (bezt->f3 & SEL)) {
    immVertex2fv(pos, bezt->vec[2]);
  }
  immEnd();
}

/* helper fn drw handle vertices only for an F-Curve (if it is not protected) */
static void drw_fcurve_handle_vertices(FCurve *fcu, View2D *v2d, bool sel_handle_only, uint pos)
{
  /* smooth outlines for more consistent appearance */
  immBindBuiltinProgram(GPU_SHADER_2D_POINT_UNIFORM_SIZE_UNIFORM_COLOR_OUTLINE_AA);

  /* set handle size */
  immUniform1f("size", (1.4f * ui_GetThemeValuef(TH_HANDLE_VERTEX_SIZE)) * UI_SCALE_FAC);
  immUniform1f("outlineWidth", 1.5f * UI_SCALE_FAC);

  drw_fcurve_sel_handle_vertices(fcu, v2d, false, sel_handle_only, pos);
  drw_fcurve_sel_handle_vertices(fcu, v2d, true, sel_handle_only, pos);
  drw_fcurve_active_handle_vertices(fcu, sel_handle_only, pos);

  immUnbindProgram();
}

static void drw_fcurve_vertices(ARgn *rgn,
                                FCurve *fcu,
                                bool do_handles,
                                bool sel_handle_only)
{
  View2D *v2d = &rgn->v2d;

  /* only drw points if curve is visible
   * - Drw unsel points before sel points as separate passes
   *    to make sure in the case of overlapping points that the sel is always visible
   * - Drw handles before keyframes, so that keyframes will overlap handles
   *   (keyframes are more important for users) */

  uint pos = gpu_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  gpu_blend(GPU_BLEND_ALPHA);
  gpu_program_point_size(true);

  /* drw the two handles first (if they're shown, the curve doesn't
   * have just a single keyframe, and the curve is being edited) */
  if (do_handles) {
    drw_fcurve_handle_vertices(fcu, v2d, sel_handle_only, pos);
  }

  /* dre keyframes over the handles */
  drw_fcurve_keyframe_vertices(fcu, v2d, pos);

  gpu_program_point_size(false);
  gpu_blend(GPU_BLEND_NONE);
}

/* Handles */
static bool drw_fcurve_handles_check(SpaceGraph *sipo, FCurve *fcu)
{
  /* don't drw handle lines if handles are not to be shown */
  if (/* handles shouldn't be shown anywhere */
      (sipo->flag & SIPO_NOHANDLES) ||
      /* keyframes aren't editable */
      (fcu->flag & FCURVE_PROTECTED) ||
#if 0
      /* handles can still be sel and handle types set, better dw */
      /* editing the handles here will cause weird/incorrect interpolation issues */
      (fcu->flag & FCURVE_INT_VALUES) ||
#endif
      /* group that curve belongs to is not editable */
      ((fcu->grp) && (fcu->grp->flag & AGRP_PROTECTED)))
  {
    return false;
  }
  return true;
}

/* drw lines for F-Curve handles only (this is only done in EditMode)
 * drw_fcurve_handles_check must be checked before running this. */
static void drw_fcurve_handles(SpaceGraph *sipo, ARgn *rgn, FCurve *fcu)
{
  using namespace dune;

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  uint color = gpu_vertformat_attr_add(
      format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);
  immBindBuiltinProgram(GPU_SHADER_3D_FLAT_COLOR);
  if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
    gpu_line_smooth(true);
  }
  gpu_blend(GPU_BLEND_ALPHA);

  immBeginAtMost(GPU_PRIM_LINES, 4 * 2 * fcu->totvert);

  const int2 bounding_indices = get_bounding_bezt_indices(
      fcu, rgn->v2d.cur.xmin, rgn->v2d.cur.xmax);

  /* slightly hacky, but we want to drw unsel points before sel ones
   * so that sel points are clearly visible */
  for (int sel = 0; sel < 2; sel++) {
    int basecol = (sel) ? TH_HANDLE_SEL_FREE : TH_HANDLE_FREE;
    uchar col[4];

    BezTriple *prevbezt = nullptr;
    for (int i = bounding_indices[0]; i <= bounding_indices[1]; i++) {
      BezTriple *bezt = &fcu->bezt[i];
      /* if only sel keyframes can get their handles shown,
       * check that keyframe is sel */
      if (sipo->flag & SIPO_SELVHANDLESONLY) {
        if (BEZT_ISSEL_ANY(bezt) == 0) {
          prevbezt = bezt;
          continue;
        }
      }

      /* drw handle with appropriate set of colors if sel is ok */
      if ((bezt->f2 & SEL) == sel) {
        /* only draw first handle if previous segment had handles */
        if ((!prevbezt && (bezt->ipo == BEZT_IPO_BEZ)) ||
            (prevbezt && (prevbezt->ipo == BEZT_IPO_BEZ))) {
          ui_GetThemeColor3ubv(basecol + bezt->h1, col);
          col[3] = fcurve_display_alpha(fcu) * 255;
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[0]);
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[1]);
        }

        /* only drw second handle if this segment is bezier */
        if (bezt->ipo == BEZT_IPO_BEZ) {
          ui_GetThemeColor3ubv(basecol + bezt->h2, col);
          col[3] = fcurve_display_alpha(fcu) * 255;
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[1]);
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[2]);
        }
      }
      else {
        /* only drw first handle if previous segment was had handles, and sel is ok */
        if (((bezt->f1 & SEL) == sel) && ((!prevbezt && (bezt->ipo == BEZT_IPO_BEZ)) ||
                                          (prevbezt && (prevbezt->ipo == BEZT_IPO_BEZ))))
        {
          ui_GetThemeColor3ubv(basecol + bezt->h1, col);
          col[3] = fcurve_display_alpha(fcu) * 255;
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[0]);
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[1]);
        }

        /* only drw second handle if this segment is bezier, and sel is ok */
        if (((bezt->f3 & SEL) == sel) && (bezt->ipo == BEZT_IPO_BEZ)) {
          ui_GetThemeColor3ubv(basecol + bezt->h2, col);
          col[3] = fcurve_display_alpha(fcu) * 255;
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[1]);
          immAttr4ubv(color, col);
          immVertex2fv(pos, bezt->vec[2]);
        }
      }
      prevbezt = bezt;
    }
  }

  immEnd();
  immUnbindProgram();
  gpu_blend(GPU_BLEND_NONE);
  if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRAWING) {
    gpu_line_smooth(false);
  }
}

/* Samples */
/* helper fn, drw keyframe vertices only for an F-Curve */
static void drw_fcurve_samples(ARgn *rgn, FCurve *fcu, const float unit_scale)
{
  FPoint *first, *last;
  float scale[2];

  /* get view settings */
  const float hsize = ui_GetThemeValuef(TH_VERTEX_SIZE);
  ui_view2d_scale_get(&rgn->v2d, &scale[0], &scale[1]);

  scale[0] /= hsize;
  scale[1] /= hsize / unit_scale;

  /* get verts */
  first = fcu->fpt;
  last = (first) ? (first + (fcu->totvert - 1)) : (nullptr);

  /* drw */
  if (first && last) {
    /* anti-aliased lines for more consistent appearance */
    if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
      gpu_line_smooth(true);
    }
    gpu_blend(GPU_BLEND_ALPHA);

    uint pos = gpu_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

    immUniformThemeColor((fcu->flag & FCURVE_SEL) ? TH_TXT_HI : TH_TXT);

    draw_cross(first->vec, scale, pos);
    drw_cross(last->vec, scale, pos);

    immUnbindProgram();

    gpu_blend(GPU_BLEND_NONE);
    if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
      gpu_line_smooth(false);
    }
  }
}

/* Curve */
/* Helper fn, drw the F-Curve by sampling the visible rgn
 * (for drwing curves with mods). */
static void drw_fcurve_curve(AnimCxt *ac,
                             Id *id,
                             FCurve *fcu_,
                             View2D *v2d,
                             uint pos,
                             const bool use_nla_remap,
                             const bool draw_extrapolation)
{
  short mapping_flag = anim_get_normalization_flags(ac->sl);

  /* when opening .dune file on a different sized screen or while dragging the toolbar this can
   * happen best just bail out in this case. */
  if (ui_view2d_scale_get_x(v2d) <= 0.0f) {
    return;
  }

  /* disable any drivers */
  FCurve fcurve_for_drw = *fcu_;
  fcurve_for_drw.driver = nullptr;

  /* compute unit correction factor */
  float offset;
  float unitFac = anim_unit_mapping_get_factor(
      ac->scene, id, &fcurve_for_drw, mapping_flag, &offset);

  /* About sampling frequency:
   * Ideally, chosen that we have 1-2 pixels = 1 segment
   * which means that our curves can be as smooth as possible. However,
   * this does mean that curves may not be fully accurate (i.e. if they have
   * sudden spikes which happen at the sampling point, we may have problems).
   * Also, this may introduce lower performance on less densely detailed curves,
   * though it is impossible to predict this from the mods!
   *
   * If the automatically determined sampling frequency is likely to cause an infinite
   * loop (i.e. too close to 0), then clamp it to a determined "safe" val. The value
   * chosen here is just the coarsest val which still looks reasonable */

  /* TODO: perhaps we should have 1.0 frames
   * as upper limit so that curves don't get too distorted? */
  float pixels_per_sample = 1.5f;
  float samplefreq = pixels_per_sample / ui_view2d_scale_get_x(v2d);

  if (!(U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING)) {
    /* Low Precision = coarse lower-bound clamping
     * Though the "Beauty Drw" flag was originally for AA'd
     * line drwing, the sampling rate here has a much greater
     * impact on performance (e.g. for #40372)!
     *
     * This one still amounts to 10 sample-frames for each 1-frame interval
     * which should be quite a decent approximation in many situations. */
    if (samplefreq < 0.1f) {
      samplefreq = 0.1f;
    }
  }
  else {
    /* "Higher Precision" but slower especially on larger windows (e.g. #40372) */
    if (samplefreq < 0.00001f) {
      samplefreq = 0.00001f;
    }
  }

  /* the start/end times are simply the horizontal extents of the 'cur' rect */
  float stime = v2d->cur.xmin;
  float etime = v2d->cur.xmax;

  AnimData *adt = use_nla_remap ? dune_animdata_from_id(id) : nullptr;

  /* If not drwing extrapolation, then change fcurve drwing bounds to its keyframe bounds clamped
   * by graph editor bounds. */
  if (!drw_extrapolation) {
    float fcu_start = 0;
    float fcu_end = 0;
    dune_fcurve_calc_range(fcu_, &fcu_start, &fcu_end, false);

    fcu_start = dune_nla_tweakedit_remap(adt, fcu_start, NLATIME_CONVERT_MAP);
    fcu_end = dune_nla_tweakedit_remap(adt, fcu_end, NLATIME_CONVERT_MAP);

    /* Account for reversed NLA strip effect. */
    if (fcu_end < fcu_start) {
      SWAP(float, fcu_start, fcu_end);
    }

    /* Clamp to graph editor rendering bounds. */
    stime = max_ff(stime, fcu_start);
    etime = min_ff(etime, fcu_end);
  }

  const int total_samples = roundf((etime - stime) / samplefreq);
  if (total_samples <= 0) {
    return;
  }

  /* NLA remapping is linear so we don't have to remap per iter. */
  const float eval_start = dune_nla_tweakedit_remap(adt, stime, NLATIME_CONVERT_UNMAP);
  const float eval_freq = dune_nla_tweakedit_remap(adt, stime + samplefreq, NLATIME_CONVERT_UNMAP) -
                          eval_start;
  const float eval_end = dune_nla_tweakedit_remap(adt, etime, NLATIME_CONVERT_UNMAP);

  immBegin(GPU_PRIM_LINE_STRIP, (total_samples + 1));

  /* At each sampling interval add a new vertex.
   * Apply the unit correction factor to the calc'd vals so that the displayed vals appear
   * correctly in the viewport */
  for (int i = 0; i < total_samples; i++) {
    const float ctime = stime + i * samplefreq;
    float eval_time = eval_start + i * eval_freq;

    /* Prevent drwing past bounds, due to floating point problems.
     * User-wise, prevent visual flickering.
     * This is to cover the case where:
     * eval_start + total_samples * eval_freq > eval_end
     * due to floating point problems. */
    if (eval_time > eval_end) {
      eval_time = eval_end;
    }

    immVertex2f(pos, ctime, (eval_fcurve(&fcurve_for_drw, eval_time) + offset) * unitFac);
  }

  /* Ensure we include end boundary point.
   * Userwise, prevent visual flickering.
   * Covers the case where:
   * eval_start + total_samples * eval_freq < eval_end
   * due to floating point problems */
  immVertex2f(pos, etime, (eval_fcurve(&fcurve_for_drw, eval_end) + offset) * unitFac);

  immEnd();
}

/* helper fn, drw a samples-based F-Curve */
static void drw_fcurve_curve_samples(AnimCxt *ac,
                                     Id *id,
                                     FCurve *fcu,
                                     View2D *v2d,
                                     const uint shdr_pos,
                                     const bool drw_extrapolation)
{
  if (!drw_extrapolation && fcu->totvert == 1) {
    return;
  }

  FPoint *prevfpt = fcu->fpt;
  FPoint *fpt = prevfpt + 1;
  float fac, v[2];
  int b = fcu->totvert;
  float unit_scale, offset;
  short mapping_flag = anim_get_normalization_flags(ac->sl);
  int count = fcu->totvert;

  const bool extrap_left = drw_extrapolation && prevfpt->vec[0] > v2d->cur.xmin;
  if (extrap_left) {
    count++;
  }

  const bool extrap_right = dre_extrapolation && (prevfpt + b - 1)->vec[0] < v2d->cur.xmax;
  if (extrap_right) {
    count++;
  }

  /* apply unit mapping */
  gpu_matrix_push();
  unit_scale = anim_unit_mapping_get_factor(ac->scene, id, fcu, mapping_flag, &offset);
  gpu_matrix_scale_2f(1.0f, unit_scale);
  gpu_matrix_translate_2f(0.0f, offset);

  immBegin(GPU_PRIM_LINE_STRIP, count);

  /* extrapolate to left? - left-side of view comes before first keyframe? */
  if (extrap_left) {
    v[0] = v2d->cur.xmin;

    /* y-value depends on the interpolation */
    if ((fcu->extend == FCURVE_EXTRAPOLATE_CONSTANT) || (fcu->flag & FCURVE_INT_VALUES) ||
        (fcu->totvert == 1))
    {
      /* just extend across the first keyframe's val */
      v[1] = prevfpt->vec[1];
    }
    else {
      /* extrapolate linear doesn't use the handle, use the next points center instead */
      fac = (prevfpt->vec[0] - fpt->vec[0]) / (prevfpt->vec[0] - v[0]);
      if (fac) {
        fac = 1.0f / fac;
      }
      v[1] = prevfpt->vec[1] - fac * (prevfpt->vec[1] - fpt->vec[1]);
    }

    immVertex2fv(shdr_pos, v);
  }

  /* loop over samples, drawing segments */
  /* drw curve between first and last keyframe (if there are enough to do so) */
  while (b--) {
    /* Linear interpolation: just add one point (which should add a new line segment) */
    immVertex2fv(shdr_pos, prevfpt->vec);

    /* get next ptrs */
    if (b > 0) {
      prevfpt++;
    }
  }

  /* extrapolate to right? (see code for left-extrapolation above too) */
  if (extrap_right) {
    v[0] = v2d->cur.xmax;

    /* y-value depends on the interpolation */
    if ((fcu->extend == FCURVE_EXTRAPOLATE_CONSTANT) || (fcu->flag & FCURVE_INT_VALUES) ||
        (fcu->totvert == 1))
    {
      /* based on last keyframe's val */
      v[1] = prevfpt->vec[1];
    }
    else {
      /* extrapolate linear doesn't use the handle, use the previous points center instead */
      fpt = prevfpt - 1;
      fac = (prevfpt->vec[0] - fpt->vec[0]) / (prevfpt->vec[0] - v[0]);
      if (fac) {
        fac = 1.0f / fac;
      }
      v[1] = prevfpt->vec[1] - fac * (prevfpt->vec[1] - fpt->vec[1]);
    }

    immVertex2fv(shdr_pos, v);
  }

  immEnd();

  gpu_matrix_pop();
}

static int calc_bezt_drw_resolution(BezTriple *bezt,
                                    BezTriple *prevbezt,
                                    const dune::float2 pixels_per_unit)
{
  const float points_per_pixel = 0.25f;
  const int resolution_x = int(((bezt->vec[1][0] - prevbezt->vec[1][0]) * pixels_per_unit[0]) *
                               points_per_pixel);
  /* Include the handles in the resolution calc to cover the case where keys have the same
   * y-val, but their handles are offset to create an arc. */
  const float min_y = min_ffff(
      bezt->vec[1][1], bezt->vec[2][1], prevbezt->vec[1][1], prevbezt->vec[0][1]);
  const float max_y = max_ffff(
      bezt->vec[1][1], bezt->vec[2][1], prevbezt->vec[1][1], prevbezt->vec[0][1]);
  const int resolution_y = int(((max_y - min_y) * pixels_per_unit[1]) * points_per_pixel);

  /* Using a simple sum instead of calculating the diagonal. This gives a slightly higher
   * resolution but it does compensate for the fact that bezier curves can create long arcs between
   * keys. */
  return resolution_x + resolution_y;
}

/* Add points on the bezier between `prevbezt` and `bezt` to `curve_vertices`.
 * Amount of points added is based on the given `resolution`. */
static void add_bezt_vertices(BezTriple *bezt,
                              BezTriple *prevbezt,
                              int resolution,
                              dune::Vector<dune::float2> &curve_vertices)
{
  if (resolution < 2) {
    curve_vertices.append({prevbezt->vec[1][0], prevbezt->vec[1][1]});
    return;
  }

  /* If the resolution goes too high the line will not end exactly at the keyframe. Probably due to
   * accumulating floating point issues in dune_curve_forward_diff_bezier.*/
  resolution = min_ii(64, resolution);

  float prev_key[2], prev_handle[2], bez_handle[2], bez_key[2];
  /* Alloc needs +1 on resolution because dune_curve_forward_diff_bezier uses it to iter
   * inclusively. */
  float *bezier_diff_points = static_cast<float *>(
      mem_malloc(sizeof(float) * ((resolution + 1) * 2), "Draw bezt data"));

  prev_key[0] = prevbezt->vec[1][0];
  prev_key[1] = prevbezt->vec[1][1];
  prev_handle[0] = prevbezt->vec[2][0];
  prev_handle[1] = prevbezt->vec[2][1];

  bez_handle[0] = bezt->vec[0][0];
  bez_handle[1] = bezt->vec[0][1];
  bez_key[0] = bezt->vec[1][0];
  bez_key[1] = bezt->vec[1][1];

  dune_fcurve_correct_bezpart(prev_key, prev_handle, bez_handle, bez_key);

  dune_curve_forward_diff_bezier(prev_key[0],
                                prev_handle[0],
                                bez_handle[0],
                                bez_key[0],
                                bezier_diff_points,
                                resolution,
                                sizeof(float[2]));
  dune_curve_forward_diff_bezier(prev_key[1],
                                 prev_handle[1],
                                 bez_handle[1],
                                 bez_key[1],
                                 bezier_diff_points + 1,
                                 resolution,
                                 sizeof(float[2]));

  for (float *fp = bezier_diff_points; resolution; resolution--, fp += 2) {
    const float x = *fp;
    const float y = *(fp + 1);
    curve_vertices.append({x, y});
  }
  mem_free(bezier_diff_points);
}

static void add_extrapolation_point_left(FCurve *fcu,
                                         const float v2d_xmin,
                                         dune::Vector<dune::float2> &curve_vertices)
{
  /* left-side of view comes before first keyframe, so need to extend as not cyclic */
  float vertex_position[2];
  vertex_position[0] = v2d_xmin;
  BezTriple *bezt = &fcu->bezt[0];

  /* y-val depends on the interpolation */
  if ((fcu->extend == FCURVE_EXTRAPOLATE_CONSTANT) || (bezt->ipo == BEZT_IPO_CONST) ||
      (bezt->ipo == BEZT_IPO_LIN && fcu->totvert == 1))
  {
    /* just extend across the first keyframe's value */
    vertex_position[1] = bezt->vec[1][1];
  }
  else if (bezt->ipo == BEZT_IPO_LIN) {
    BezTriple *next_bezt = bezt + 1;
    /* extrapolate linear doesn't use the handle, use the next points center instead */
    float fac = (bezt->vec[1][0] - next_bezt->vec[1][0]) / (bezt->vec[1][0] - vertex_position[0]);
    if (fac) {
      fac = 1.0f / fac;
    }
    vertex_position[1] = bezt->vec[1][1] - fac * (bezt->vec[1][1] - next_bezt->vec[1][1]);
  }
  else {
    /* based on angle of handle 1 (relative to keyframe) */
    float fac = (bezt->vec[0][0] - bezt->vec[1][0]) / (bezt->vec[1][0] - vertex_position[0]);
    if (fac) {
      fac = 1.0f / fac;
    }
    vertex_position[1] = bezt->vec[1][1] - fac * (bezt->vec[0][1] - bezt->vec[1][1]);
  }

  curve_vertices.append(vertex_position);
}

static void add_extrapolation_point_right(FCurve *fcu,
                                          const float v2d_xmax,
                                          dune::Vector<dune::float2> &curve_vertices)
{
  float vertex_position[2];
  vertex_position[0] = v2d_xmax;
  BezTriple *bezt = &fcu->bezt[fcu->totvert - 1];

  /* y-val depends on the interpolation. */
  if ((fcu->extend == FCURVE_EXTRAPOLATE_CONSTANT) || (fcu->flag & FCURVE_INT_VALS) ||
      (bezt->ipo == BEZT_IPO_CONST) || (bezt->ipo == BEZT_IPO_LIN && fcu->totvert == 1))
  {
    /* based on last keyframe's val */
    vertex_position[1] = bezt->vec[1][1];
  }
  else if (bezt->ipo == BEZT_IPO_LIN) {
    /* Extrapolate linear doesn't use the handle, use the previous points center instead. */
    BezTriple *prev_bezt = bezt - 1;
    float fac = (bezt->vec[1][0] - prev_bezt->vec[1][0]) / (bezt->vec[1][0] - vertex_position[0]);
    if (fac) {
      fac = 1.0f / fac;
    }
    vertex_position[1] = bezt->vec[1][1] - fac * (bezt->vec[1][1] - prev_bezt->vec[1][1]);
  }
  else {
    /* Based on angle of handle 1 (relative to keyframe). */
    float fac = (bezt->vec[2][0] - bezt->vec[1][0]) / (bezt->vec[1][0] - vertex_position[0]);
    if (fac) {
      fac = 1.0f / fac;
    }
    vertex_position[1] = bezt->vec[1][1] - fac * (bezt->vec[2][1] - bezt->vec[1][1]);
  }

  curve_vertices.append(vertex_position);
}

static dune::float2 calc_pixels_per_unit(View2D *v2d)
{
  const int win_width = lib_rcti_size_x(&v2d->mask);
  const int win_height = lib_rcti_size_y(&v2d->mask);

  const float v2d_frame_range = lib_rctf_size_x(&v2d->cur);
  const float v2d_val_range = lib_rctf_size_y(&v2d->cur);
  const dune::float2 pixels_per_unit = {win_width / v2d_frame_range,
                                        win_height / v2d_val_range};
  return pixels_per_unit;
}

static float calc_pixel_distance(const rctf &bounds, const dune::float2 pixels_per_unit)
{
  return lib_rctf_size_x(&bounds) * pixels_per_unit[0] +
         lib_rctf_size_y(&bounds) * pixels_per_unit[1];
}

static void expand_key_bounds(const BezTriple *left_key, const BezTriple *right_key, rctf &bounds)
{
  bounds.xmax = right_key->vec[1][0];
  if (left_key->ipo == BEZT_IPO_BEZ) {
    /* Respect handles of bezier keys. */
    bounds.ymin = min_ffff(
        bounds.ymin, right_key->vec[1][1], right_key->vec[0][1], left_key->vec[2][1]);
    bounds.ymax = max_ffff(
        bounds.ymax, right_key->vec[1][1], right_key->vec[0][1], left_key->vec[2][1]);
  }
  else {
    bounds.ymax = max_ff(bounds.ymax, right_key->vec[1][1]);
    bounds.ymin = min_ff(bounds.ymin, right_key->vec[1][1]);
  }
}

/* Helper fn drw one repeat of an F-Curve (using Bezier curve approximations). */
static void drw_fcurve_curve_keys(
    AnimCxt *ac, Id *id, FCurve *fcu, View2D *v2d, uint pos, const bool drw_extrapolation)
{
  using namespace dune;
  if (!drw_extrapolation && fcu->totvert == 1) {
    return;
  }

  /* Apply unit mapping. */
  gpu_matrix_push();
  float offset;
  short mapping_flag = anim_get_normalization_flags(ac->sl);
  const float unit_scale = anim_unit_mapping_get_factor(ac->scene, id, fcu, mapping_flag, &offset);
  gpu_matrix_scale_2f(1.0f, unit_scale);
  gpu_matrix_translate_2f(0.0f, offset);

  Vector<float2> curve_vertices;

  /* Extrapolate to the left? */
  if (drw_extrapolation && fcu->bezt[0].vec[1][0] > v2d->cur.xmin) {
    add_extrapolation_point_left(fcu, v2d->cur.xmin, curve_vertices);
  }

  const int2 bounding_indices = get_bounding_bezt_indices(fcu, v2d->cur.xmin, v2d->cur.xmax);

  /* Always add the first point so the extrapolation line doesn't jump. */
  curve_vertices.append(
      {fcu->bezt[bounding_indices[0]].vec[1][0], fcu->bezt[bounding_indices[0]].vec[1][1]});

  const dune::float2 pixels_per_unit = cal_pixels_per_unit(v2d);
  const int win_width = lib_rcti_size_x(&v2d->mask);
  const float v2d_frame_range = lib_rctf_size_x(&v2d->cur);
  const float pixel_width = v2d_frame_range / win_width;
  const float samples_per_pixel = 0.66f;
  const float eval_step = pixel_width / samples_per_pixel;

  BezTriple *first_key = &fcu->bezt[bounding_indices[0]];
  rctf key_bounds = {
      first_key->vec[1][0], first_key->vec[1][1], first_key->vec[1][0], first_key->vec[1][1]};
  /* Used when skipping keys. */
  bool has_skipped_keys = false;
  const float min_pixel_distance = 3.0f;

  /* Drw curve between first and last keyframe (if there are enough to do so). */
  for (int i = bounding_indices[0] + 1; i <= bounding_indices[1]; i++) {
    BezTriple *prevbezt = &fcu->bezt[i - 1];
    BezTriple *bezt = &fcu->bezt[i];
    expand_key_bounds(prevbezt, bezt, key_bounds);
    float pixel_distance = calc_pixel_distance(key_bounds, pixels_per_unit);

    if (pixel_distance >= min_pixel_distance && has_skipped_keys) {
      /* When the pixel distance is greater than the threshold, and we've skipped at least one, add
       * a point. The point position is the average of all keys from INCLUDING prevbezt to
       * EXCLUDING bezt. prevbezt then gets reset to the key before bezt because the distance
       * between those is potentially below the threshold. */
      curve_vertices.append({lib_rctf_cent_x(&key_bounds), lib_rctf_cent_y(&key_bounds)});
      has_skipped_keys = false;
      key_bounds = {
          prevbezt->vec[1][0], prevbezt->vec[1][1], prevbezt->vec[1][0], prevbezt->vec[1][1]};
      expand_key_bounds(prevbezt, bezt, key_bounds);
      /* Calc again based on the new prevbezt. */
      pixel_distance = calc_pixel_distance(key_bounds, pixels_per_unit);
    }

    if (pixel_distance < min_pixel_distance) {
      /* Skip any keys that are too close to each other in screen space. */
      has_skipped_keys = true;
      continue;
    }

    switch (prevbezt->ipo) {

      case BEZT_IPO_CONST:
        /* Constant-Interpolation: drw segment between previous keyframe and next,
         * but holding same val */
        curve_vertices.append({prevbezt->vec[1][0], prevbezt->vec[1][1]});
        curve_vertices.append({bezt->vec[1][0], prevbezt->vec[1][1]});
        break;

      case BEZT_IPO_LIN:
        /* Linear interpolation: just add one point (which should add a new line segment) */
        curve_vertices.append({prevbezt->vec[1][0], prevbezt->vec[1][1]});
        break;

      case BEZT_IPO_BEZ: {
        const int resolution = calc_bezt_drw_resolution(bezt, prevbezt, pixels_per_unit);
        add_bezt_vertices(bezt, prevbezt, resolution, curve_vertices);
        break;
      }

      default: {
        /* In case there is no other way to get curve points, eval the FCurve. */
        curve_vertices.append(prevbezt->vec[1]);
        float current_frame = prevbezt->vec[1][0] + eval_step;
        while (current_frame < bezt->vec[1][0]) {
          curve_vertices.append({current_frame, eval_fcurve(fcu, current_frame)});
          current_frame += eval_step;
        }
        break;
      }
    }

    prevbezt = bezt;
  }

  /* Always add the last point so the extrapolation line doesn't jump. */
  curve_vertices.append(
      {fcu->bezt[bounding_indices[1]].vec[1][0], fcu->bezt[bounding_indices[1]].vec[1][1]});

  /* Extrapolate to the right? (see code for left-extrapolation above too) */
  if (draw_extrapolation && fcu->bezt[fcu->totvert - 1].vec[1][0] < v2d->cur.xmax) {
    add_extrapolation_point_right(fcu, v2d->cur.xmax, curve_vertices);
  }

  if (curve_vertices.size() < 2) {
    gpu_matrix_pop();
    return;
  }

  immBegin(GPU_PRIM_LINE_STRIP, curve_vertices.size());
  for (const float2 vertex : curve_vertices) {
    immVertex2fv(pos, vertex);
  }
  immEnd();

  gpu_matrix_pop();
}

static void drw_fcurve(AnimCxt *ac, SpaceGraph *sipo, ARgn *rgn, AnimListElem *ale)
{
  FCurve *fcu = (FCurve *)ale->key_data;
  FMod *fcm = find_active_fmod(&fcu->mods);
  AnimData *adt = anim_nla_mapping_get(ac, ale);

  /* map keyframes for drwing if scaled F-Curve */
  anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), false, false);

  /* drw curve:
   * curve line may be result of one or more destructive mods or just the raw data,
   *   mustccheck which method should be used
   * ctrls from active mod take precedence over keyframes
   *   (editing tools need to take this into account!)
   */

  /* 1) drw curve line */
  if (((fcu->mods.first) || (fcu->flag & FCURVE_INT_VALS)) ||
      (((fcu->bezt) || (fcu->fpt)) && (fcu->totvert)))
  {
    /* set color/drwing style for curve itself */
    /* drw active F-Curve thicker than the rest to make it stand out */
    if (fcu->flag & FCURVE_ACTIVE && !dune_fcurve_is_protected(fcu)) {
      gpu_line_width(2.5);
    }
    else {
      gpu_line_width(1.0);
    }

    /* anti-aliased lines for less jagged appearance */
    if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
      gpu_line_smooth(true);
    }
    gpu_blend(GPU_BLEND_ALPHA);

    const uint shdr_pos = gpu_vertformat_attr_add(
        immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

    float viewport_size[4];
    gpu_viewport_size_get_f(viewport_size);

    if (dune_fcurve_is_protected(fcu)) {
      /* Protected curves (non editable) are drawn with dotted lines. */
      immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);
      immUniform2f(
          "viewport_size", viewport_size[2] / UI_SCALE_FAC, viewport_size[3] / UI_SCALE_FAC);
      immUniform1i("colors_len", 0); /* Simple dashes. */
      immUniform1f("dash_width", 16.0f * U.scale_factor);
      immUniform1f("udash_factor", 0.35f * U.scale_factor);
    }
    else {
      immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
      immUniform2fv("viewportSize", &viewport_size[2]);
      immUniform1f("lineWidth", GPU_line_width_get());
    }

    if (((fcu->grp) && (fcu->grp->flag & AGRP_MUTED)) || (fcu->flag & FCURVE_MUTED)) {
      /* muted curves are drwn in a grayish hue */
      /* should we have some variations? */
      immUniformThemeColorShade(TH_HEADER, 50);
    }
    else {
      /* set the color the curve has set
       * unsel curves drw less opaque to help distinguish the sel ones */
      immUniformColor3fvAlpha(fcu->color, fcurve_display_alpha(fcu));
    }

    const bool drw_extrapolation = (sipo->flag & SIPO_NO_DRE_EXTRAPOLATION) == 0;
    /* draw F-Curve */
    if ((fcu->mods.first) || (fcu->flag & FCURVE_INT_VALS)) {
      /* drw a curve affected by mods or only allowed to have int vals
       * by sampling it at various small-intervals over the visible rgn */
      if (adt) {
        /* Req'd to do this mapping dance bc
         * keyframes were remapped but the F-mod evals are not.
         * Undo the keyframe remapping, instead remap the eval time when drwing the
         * curve itself. After, go back and redo the keyframe remapping so the ctrls are
         * drwn correct. */
        anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), true, false);
        drw_fcurve_curve(ac, ale->id, fcu, &rgn->v2d, shdr_pos, true, drw_extrapolation);
        anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), false, false);
      }
      else {
        drw_fcurve_curve(ac, ale->id, fcu, &rgn->v2d, shdr_pos, false, drw_extrapolation);
      }
    }
    else if (((fcu->bezt) || (fcu->fpt)) && (fcu->totvert)) {
      /* just drw curve based on defined data (i.e. no mods) */
      if (fcu->bezt) {
        drw_fcurve_curve_keys(ac, ale->id, fcu, &rgn->v2d, shdr_pos, drw_extrapolation);
      }
      else if (fcu->fpt) {
        drw_fcurve_curve_samples(ac, ale->id, fcu, &rgn->v2d, shdr_pos, draw_extrapolation);
      }
    }

    immUnbindProgram();

    if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
      gpu_line_smooth(false);
    }
    gpu_blend(GPU_BLEND_NONE);
  }

  /* 2) drw handles and vertices as appropriate based on active
   * If opt to show only ctrls if the F-Curve is sel is enabled,
   *   we must obey this. */
  if (!(U.anim_flag & USER_ANIM_ONLY_SHOW_SEL_CURVE_KEYS) ||
      (fcu->flag & FCURVE_SEL)) {
    if (!dune_fcurve_are_keyframes_usable(fcu) && !(fcu->fpt && fcu->totvert)) {
      /* only drw ctrls if this is the active mod */
      if ((fcu->flag & FCURVE_ACTIVE) && (fcm)) {
        switch (fcm->type) {
          case FMOD_TYPE_ENVELOPE: /* envelope */
            draw_fcurve_mod_ctrls_envelope(fcm, &rgn->v2d, adt);
            break;
        }
      }
    }
    else if (((fcu->bezt) || (fcu->fpt)) && (fcu->totvert)) {
      short mapping_flag = anim_get_normalization_flags(ac->sl);
      float offset;
      const float unit_scale = anim_unit_mapping_get_factor(
          ac->scene, ale->id, fcu, mapping_flag, &offset);

      /* apply unit-scaling to all vals via OpenGL */
      gpu_matrix_push();
      gpu_matrix_scale_2f(1.0f, unit_scale);
      gpu_matrix_translate_2f(0.0f, offset);

      /* Set this once and for all -
       * all handles and handle-verts should use the same thickness. */
      gpu_line_width(1.0);

      if (fcu->bezt) {
        bool do_handles = drw_fcurve_handles_check(sipo, fcu);

        if (do_handles) {
          /* only drw handles/vertices on keyframes */
          drw_fcurve_handles(sipo, rgn, fcu);
        }

        drw_fcurve_vertices(rgn, fcu, do_handles, (sipo->flag & SIPO_SELVHANDLESONLY));
      }
      else {
        /* samples: only drw two indicators at either end as indicators */
        drw_fcurve_samples(rgn, fcu, unit_scale);
      }

      gpu_matrix_pop();
    }
  }

  /* 3) drw driver debugging stuff */
  if ((ac->datatype == ANIMCONT_DRIVERS) && (fcu->flag & FCURVE_ACTIVE)) {
    graph_drw_driver_debug(ac, ale->id, fcu);
  }

  /* undo mapping of keyframes for drwing if scaled F-Curve */
  if (adt) {
    anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), true, false);
  }
}

/* Debugging */
l
/* Drw indicators which show val calc from driver,
 * and how this is mapped to the val that comes out of it.
 * For helping users understand how to interpret
 * the graphs also facilitates debugging.*/

static void graph_drw_driver_debug(AnimCxt *ac, Id *id, FCurve *fcu)
{
  ChannelDriver *driver = fcu->driver;
  View2D *v2d = &ac->rgn->v2d;
  short mapping_flag = anim_get_normalization_flags(ac->sl);
  float offset;
  float unitfac = anim_unit_mapping_get_factor(ac->scene, id, fcu, mapping_flag, &offset);

  const uint shdr_pos = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  gpu_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2] / UI_SCALE_FAC, viewport_size[3] / UI_SCALE_FAC);

  immUniform1i("colors_len", 0); /* Simple dashes. */

  /* No curve to modify/visualize the result?
   * => We still want to show the 1-1 default...
   */
  if ((fcu->totvert == 0) && lib_list_is_empty(&fcu->mods)) {
    float t;

    /* drw with thin dotted lines in style of what curve would have been */
    immUniformColor3fv(fcu->color);

    immUniform1f("dash_width", 40.0f);
    immUniform1f("udash_factor", 0.5f);
    gpu_line_width(2.0f);

    /* drw 1-1 line, stretching just past the screen limits
     * Need to scale the y-vals to be valid for the units */
    immBegin(GPU_PRIM_LINES, 2);

    t = v2d->cur.xmin;
    immVertex2f(shdr_pos, t, (t + offset) * unitfac);

    t = v2d->cur.xmax;
    immVertex2f(shdr_pos, t, (t + offset) * unitfac);

    immEnd();
  }

  /* drw driver only if actually functional */
  if ((driver->flag & DRIVER_FLAG_INVALID) == 0) {
    /* grab "coords" for driver outputs */
    float x = driver->curval;
    float y = fcu->curval * unitfac;

    /* Only drw indicators if the point is in range. */
    if (x >= v2d->cur.xmin) {
      float co[2];

      /* drw dotted lines leading towards this point from both axes */
      immUniformColor3f(0.9f, 0.9f, 0.9f);
      immUniform1f("dash_width", 10.0f);
      immUniform1f("udash_factor", 0.5f);
      gpu_line_width(1.0f);

      immBegin(GPU_PRIM_LINES, (y <= v2d->cur.ymax) ? 4 : 2);

      /* x-axis lookup */
      co[0] = x;

      if (y <= v2d->cur.ymax) {
        co[1] = v2d->cur.ymax + 1.0f;
        immVertex2fv(shdr_pos, co);

        co[1] = y;
        immVertex2fv(shdr_pos, co);
      }

      /* y-axis lookup */
      co[1] = y;

      co[0] = v2d->cur.xmin - 1.0f;
      immVertex2fv(shdr_pos, co);

      co[0] = x;
      immVertex2fv(shdr_pos, co);

      immEnd();

      immUnbindProgram();

      /* GPU_PRIM_POINTS do not survive dashed line geometry shader... */
      immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

      /* x marks the spot . */
      /* -> outer frame */
      immUniformColor3f(0.9f, 0.9f, 0.9f);
      gpu_point_size(7.0);

      immBegin(GPU_PRIM_POINTS, 1);
      immVertex2f(shdr_pos, x, y);
      immEnd();

      /* inner frame */
      immUniformColor3f(0.9f, 0.0f, 0.0f);
      gpu_point_size(3.0);

      immBegin(GPU_PRIM_POINTS, 1);
      immVertex2f(shdr_pos, x, y);
      immEnd();
    }
  }

  immUnbindProgram();
}

/* Public Curve-Drwing API */
void graph_drw_ghost_curves(AnimCxt *ac, SpaceGraph *sipo, ARgn *rgn)
{
  /* drw with thick dotted lines */
  gpu_line_width(3.0f);

  /* anti-aliased lines for less jagged appearance */
  if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
    gpu_line_smooth(true);
  }
  gpu_blend(GPU_BLEND_ALPHA);

  const uint shdr_pos = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  gpu_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2] / UI_SCALE_FAC, viewport_size[3] / UI_SCALE_FAC);

  immUniform1i("colors_len", 0); /* Simple dashes. */
  immUniform1f("dash_width", 20.0f);
  immUniform1f("udash_factor", 0.5f);

  /* Don't drw extrapolation on sampled ghost curves bc it doesn't
   * match the curves they're ghosting anyway.
   * See issue #109920 for details. */
  const bool draw_extrapolation = false;
  /* ghost curves are simply sampled F-Curves stored in sipo->runtime.ghost_curves */
  LIST_FOREACH (FCurve *, fcu, &sipo->runtime.ghost_curves) {
    /* set color the curve has set
     * Set by the fn which creates these
     * Drw w a fixed opacity of 2  */
    immUniformColor3fvAlpha(fcu->color, 0.5f);

    /* simply drw the stored samples */
    drw_fcurve_curve_samples(ac, nullptr, fcu, &rgn->v2d, shdr_pos, draw_extrapolation);
  }

  immUnbindProgram();

  if (U.anim_flag & USER_ANIM_HIGH_QUALITY_DRWING) {
    gpu_line_smooth(false);
  }
  gpu_blend(GPU_BLEND_NONE);
}

void graph_draw_curves(bAnimContext *ac, SpaceGraph *sipo, ARgn *rgm, short sel)
{
  List anim_data = {nullptr, nullptr};
  int filter;

  /* build list of curves to drw */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FCURVESONLY);
  filter |= ((sel) ? (ANIMFILTER_SEL) : (ANIMFILTER_UNSEL));
  anim_animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  /* foreach curve:
   * drw curve, then handle-lines, and finally vertices in this order so that
   * the data will be layered correctly */
  AnimListElem *ale_active_fcurve = nullptr;
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    const FCurve *fcu = (FCurve *)ale->key_data;
    if (fcu->flag & FCURVE_ACTIVE) {
      ale_active_fcurve = ale;
      continue;
    }
    drw_fcurve(ac, sipo, rgn, ale);
  }

  /* Drw the active FCurve last so that it (especially the active keyframe)
   * shows on top of the other curves. */
  if (ale_active_fcurve != nullptr) {
    drw_fcurve(ac, sipo, rgn, ale_active_fcurve);
  }

  /* free list of curves */
  anim_animdata_freelist(&anim_data);


/* Channel List */
void graph_drw_channel_names(Cxt *C, AnimCxt *ac, ARgn *rgn)
{
  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  int filter;

  View2D *v2d = &rgn->v2d;
  float height;
  size_t items;

  /* build list of channels to drw */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_LIST_CHANNELS |
            ANIMFILTER_FCURVESONLY);
  items = anim_animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  /* Update max-extent of channels here (taking into account scrollers):
   * this is done to allow the channel list to be scrollable, but must be done here
   * to avoid regening the list again and/or also bc channels list is drwn first */
  height = anim_ui_get_channels_total_height(v2d, items);
  v2d->tot.ymin = -height;
  const float channel_step = anim_ui_get_channel_step();

  /* Loop through channels, and set up drwing depending on their type. */
  { /* first pass: just the standard GL-drwing for backdrop + txt */
    size_t channel_index = 0;
    float ymax = anim_ui_get_first_channel_top(v2d);

    for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
         ale = ale->next, ymax -= channel_step, channel_index++)
    {
      const float ymin = ymax - anim_ui_get_channel_height();

      /* check if visible */
      if (IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)) {
        /* drw all channels using standard channel-drwing API */
        anim_channel_drw(ac, ale, ymin, ymax, channel_index);
      }
    }
  }
  { /* second pass: widgets */
    uiBlock *block = ui_block_begin(C, rgn, __func__, UI_EMBOSS);
    size_t channel_index = 0;
    float ymax = anim_ui_get_first_channel_top(v2d);

    /* set blending again, as may not be set in prev step */
    gpu_blend(GPU_BLEND_ALPHA);

    for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
         ale = ale->next, ymax -= channel_step, channel_index++)
    {
      const float ymin = ymax - anim_ui_get_channel_height();

      /* check if visible */
      if (IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)) {
        /* draw all channels using standard channel-drwing API */
        rctf channel_rect;
        lib_rctf_init(&channel_rect, 0, v2d->cur.xmax - V2D_SCROLL_WIDTH, ymin, ymax);
        anim_channel_drw_widgets(C, ac, ale, block, &channel_rect, channel_index);
      }
    }

    ui_block_end(C, block);
    ui_block_draw(C, block);

    gpu_blend(GPU_BLEND_NONE);
  }

  /* Free tmp channels. */
  anim_animdata_freelist(&anim_data);
}
