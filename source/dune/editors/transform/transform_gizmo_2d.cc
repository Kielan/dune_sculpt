/* 2D Transform Gizmo
 * Used for UV/Img Editor */

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"
#include "lib_math_vector_types.hh"

#include "types_object.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_view3d.h"

#include "dune_cxt.hh"
#include "dune_global.h"
#include "dune_layer.h"

#include "api_access.hh"

#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "win_api.hh"
#include "win_msg.hh"
#include "win_types.hh"

#include "ed_gizmo_lib.hh"
#include "ed_gizmo_utils.hh"
#include "ed_img.hh"
#include "ed_screen.hh"
#include "ed_uvedit.hh"

#include "seq_channels.hh"
#include "seq_iter.hh"
#include "seq_seq.hh"
#include "seq_time.hh"
#include "seq_transform.hh"

#include "transform.hh"
#include "transform_gizmo.hh"

/* Shared Cb's */
static bool gizmo2d_generic_poll(const Cxt *C, WinGizmoGroupType *gzgt)
{
  if (!ed_gizmo_poll_or_unlink_delayed_from_tool(C, gzgt)) {
    return false;
  }

  if ((U.gizmo_flag & USER_GIZMO_DRW) == 0) {
    return false;
  }

  if (G.moving) {
    return false;
  }

  ScrArea *area = cxt_win_area(C);
  if (area == nullptr) {
    return false;
  }

  /* below this is assumed to be a tool gizmo.
   * If there are cases that need to check other flags this fn could be split. */
  switch (area->spacetype) {
    case SPACE_IMG: {
      const SpaceImg *simg = static_cast<const SpaceImg *>(area->spacedata.first);
      Ob *obedit = cxt_data_edit_ob(C);
      if (!ed_space_img_show_uvedit(simg, obedit)) {
        return false;
      }
      break;
    }
    case SPACE_SEQ: {
      const SpaceSeq *sseq = static_cast<const SpaceSeq *>(area->spacedata.first);
      if (sseq->gizmo_flag & (SEQ_GIZMO_HIDE | SEQ_GIZMO_HIDE_TOOL)) {
        return false;
      }
      if (sseq->main != SEQ_DRW_IMG_IMBUF) {
        return false;
      }
      Scene *scene = cxt_data_scene(C);
      Editing *ed = seq_editing_get(scene);
      if (ed == nullptr) {
        return false;
      }
      break;
    }
  }

  return true;
}

static void gizmo2d_pivot_point_msg_sub(WinGizmoGroup *gzgroup,
                                        WinMsgBus *mbus, /* Additional args. */
                                        Screen *screen,
                                        ScrArea *area,
                                        ARgn *rgn)
{
  WinMsgSubVal msg_sub_val_gz_tag_refresh{};
  msg_sub_val_gz_tag_refresh.owner = rgn;
  msg_sub_val_gz_tag_refresh.user_data = gzgroup->parent_gzmap;
  msg_sub_val_gz_tag_refresh.notify = win_gizmo_do_msg_notify_tag_refresh;

  switch (area->spacetype) {
    case SPACE_IMG: {
      SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
      ApiPtr ptr = api_ptr_create(&screen->id, &ApiSpaceImgEditor, sima);
      {
        const ApiProp *props[] = {
            &api_SpaceImgEditor_pivot_point,
            (simg->around == V3D_AROUND_CURSOR) ? &api_SpaceImgEditor_cursor_location : nullptr,
        };
        for (int i = 0; i < ARRAY_SIZE(props); i++) {
          if (props[i] == nullptr) {
            continue;
          }
          win_msg_sub_api(mbus, &ptr, props[i], &msg_sub_val_gz_tag_refresh, __func__);
        }
      }
      break;
    }
  }
}

/* Arrow / Cage Gizmo Group
 * Defines public fns, not the gizmo itself:
 * - ed_widgetgroup_gizmo2d_xform_cbs_set
 * - ed_widgetgroup_gizmo2d_xform_no_cage_cbs_set */
/* axes as index */
enum {
  MAN2D_AXIS_TRANS_X = 0,
  MAN2D_AXIS_TRANS_Y,

  MAN2D_AXIS_LAST,
};

struct GizmoGroup2D {
  WinGizmo *translate_xy[3];
  WinGizmo *cage;

  /* Current origin in view space, used to update widget origin for possible view changes */
  float origin[2];
  float min[2];
  float max[2];
  float rotation;

  bool no_cage;
};

/* Utils */
static void gizmo2d_get_axis_color(const int axis_idx, float *r_col, float *r_col_hi)
{
  const float alpha = 0.6f;
  const float alpha_hi = 1.0f;
  int col_id;

  switch (axis_idx) {
    case MAN2D_AXIS_TRANS_X:
      col_id = TH_AXIS_X;
      break;
    case MAN2D_AXIS_TRANS_Y:
      col_id = TH_AXIS_Y;
      break;
    default:
      lib_assert(0);
      col_id = TH_AXIS_Y;
      break;
  }

  ui_GetThemeColor4fv(col_id, r_col);

  copy_v4_v4(r_col_hi, r_col);
  r_col[3] *= alpha;
  r_col_hi[3] *= alpha_hi;
}

static GizmoGroup2D *gizmogroup2d_init(WinGizmoGroup *gzgroup)
{
  const WinGizmoType *gzt_arrow = win_gizmotype_find("GIZMO_GT_arrow_3d", true);
  const WinGizmoType *gzt_cage = win_gizmotype_find("GIZMO_GT_cage_2d", true);
  const WinGizmoType *gzt_button = win_gizmotype_find("GIZMO_GT_btn_2d", true);

  GizmoGroup2D *ggd = static_cast<GizmoGroup2D *>(mem_calloc(sizeof(GizmoGroup2D), __func__));

  ggd->translate_xy[0] = win_gizmo_new_ptr(gzt_arrow, gzgroup, nullptr);
  ggd->translate_xy[1] = win_gizmo_new_ptr(gzt_arrow, gzgroup, nullptr);
  ggd->translate_xy[2] = win_gizmo_new_ptr(gzt_button, gzgroup, nullptr);
  ggd->cage = win_gizmo_new_ptr(gzt_cage, gzgroup, nullptr);

  api_enum_set(ggd->cage->ptr,
               "transform",
               ED_GIZMO_CAGE_XFORM_FLAG_TRANSLATE | ED_GIZMO_CAGE_XFORM_FLAG_SCALE |
                   ED_GIZMO_CAGE_XFORM_FLAG_ROTATE);

  return ggd;
}

/* Calcs origin in view space, use with gizmo2d_origin_to_rgn. */
static bool gizmo2d_calc_bounds(const bContext *C, float *r_center, float *r_min, float *r_max)
{
  float min_buf[2], max_buf[2];
  if (r_min == nullptr) {
    r_min = min_buf;
  }
  if (r_max == nullptr) {
    r_max = max_buf;
  }

  ScrArea *area = cxt_win_area(C);
  bool has_sel = false;
  if (area->spacetype == SPACE_IMG) {
    Scene *scene = cxt_data_scene(C);
    ViewLayer *view_layer = cxt_data_view_layer(C);
    uint obs_len = 0;
    Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data_with_uvs(
        scene, view_layer, nullptr, &obs_len);
    if (ed_uvedit_minmax_multi(scene, obs, obs_len, r_min, r_max)) {
      has_sel = true;
    }
    mem_free(obs);
  }
  else if (area->spacetype == SPACE_SEQ) {
    Scene *scene = cxt_data_scene(C);
    Editing *ed = seq_editing_get(scene);
    List *seqbase = seq_active_seqbase_get(ed);
    List *channels = seq_channels_displayed_get(ed);
    dune::VectorSet strips = seq_query_rendered_strips(
        scene, channels, seqbase, scene->r.cfra, 0);
    strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });
    int selected_strips = strips.size();
    if (sel_strips > 0) {
      has_sel = true;
      seq_img_transform_bounding_box_from_collection(
          scene, strips, sel_strips != 1, r_min, r_max);
    }
    if (sel_strips > 1) {
      /* Don't drw the cage as transforming multiple strips isn't currently very useful as it
       * doesn't behave as one would expect.
       * This is bc our current transform sys doesn't support shearing which would make the
       * scaling transforms of the bounding box behave weirdly.
       * In addition, the rotation of the bounding box can not currently be hooked up
       * properly to read the result from the transform sys (when transforming multiple strips). */
      const int pivot_point = scene->toolsettings->seq_tool_settings->pivot_point;
      if (pivot_point == V3D_AROUND_CURSOR) {
        SpaceSeq *sseq = static_cast<SpaceSeq *>(area->spacedata.first);
        seq_img_preview_unit_to_px(scene, sseq->cursor, r_center);
      }
      else {
        mid_v2_v2v2(r_center, r_min, r_max);
      }
      zero_v2(r_min);
      zero_v2(r_max);
      return has_sel;
    }
  }

  if (has_sel == false) {
    zero_v2(r_min);
    zero_v2(r_max);
  }

  mid_v2_v2v2(r_center, r_min, r_max);
  return has_sel;
}

static int gizmo2d_calc_transform_orientation(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area->spacetype != SPACE_SEQ) {
    return V3D_ORIENT_GLOBAL;
  }

  Scene *scene = cxt_data_scene(C);
  Editing *ed = seq_editing_get(scene);
  List *seqbase = seq_active_seqbase_get(ed);
  List *channels = seq_channels_displayed_get(ed);
  dune::VectorSet strips = seq_query_rendered_strips(
      scene, channels, seqbase, scene->r.cfra, 0);
  strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });

  bool use_local_orient = strips.size() == 1;

  if (use_local_orient) {
    return V3D_ORIENT_LOCAL;
  }
  return V3D_ORIENT_GLOBAL;
}

static float gizmo2d_calc_rotation(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area->spacetype != SPACE_SEQ) {
    return 0.0f;
  }

  Scene *scene = cxt_data_scene(C);
  Editing *ed = seq_editing_get(scene);
  List *seqbase = seq_active_seqbase_get(ed);
  List *channels = seq_channels_displayed_get(ed);
  dune::VectorSet strips = seq_query_rendered_strips(
      scene, channels, seqbase, scene->r.cfra, 0);
  strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });

  if (strips.size() == 1) {
    /* Only return the strip rotation if only one is sel */
    for (Seq *seq : strips) {
      StripTransform *transform = seq->strip->transform;
      float mirror[2];
      seq_img_transform_mirror_factor_get(seq, mirror);
      return transform->rotation * mirror[0] * mirror[1];
    }
  }

  return 0.0f;
}

static bool seq_get_strip_pivot_median(const Scene *scene, float r_pivot[2])
{
  zero_v2(r_pivot);

  Editing *ed = seq_editing_get(scene);
  List *seqbase = seq_active_seqbase_get(ed);
  List *channels = seq_channels_displayed_get(ed);
  dune::VectorSet strips = seq_query_rendered_strips(
      scene, channels, seqbase, scene->r.cfra, 0);
  strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });
  bool has_sel = !strips.is_empty();

  if (has_sel) {
    for (Seq *seq : strips) {
      float origin[2];
      seq_img_transform_origin_offset_pixelspace_get(scene, seq, origin);
      add_v2_v2(r_pivot, origin);
    }
    mul_v2_fl(r_pivot, 1.0f / strips.size());
  }

  return has_sel;
}

static bool gizmo2d_calc_transform_pivot(const Cxt *C, float r_pivot[2])
{
  ScrArea *area = cxt_win_area(C);
  Scene *scene = cxt_data_scene(C);
  bool has_sel = false;

  if (area->spacetype == SPACE_IMG) {
    SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
    ViewLayer *view_layer = cxt_data_view_layer(C);
    ed_uvedit_center_from_pivot_ex(simg, scene, view_layer, r_pivot, simg->around, &has_sel);
  }
  else if (area->spacetype == SPACE_SEQ) {
    SpaceSeq *sseq = static_cast<SpaceSeq *>(area->spacedata.first);
    const int pivot_point = scene->toolsettings->seq_tool_settings->pivot_point;

    if (pivot_point == V3D_AROUND_CURSOR) {
      seq_img_preview_unit_to_px(scene, sseq->cursor, r_pivot);

      Editing *ed = seq_editing_get(scene);
      List *seqlist = seq_active_seqbase_get(ed);
      List *channels = seq_channels_displayed_get(ed);
      dune::VectorSet strips = seq_query_rendered_strips(
          scene, channels, seqbase, scene->r.cfra, 0);
      strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });
      has_sel = !strips.is_empty();
    }
    else if (pivot_point == V3D_AROUND_CENTER_BOUNDS) {
      has_sel = gizmo2d_calc_bounds(C, r_pivot, nullptr, nullptr);
    }
    else {
      has_sel = seq_get_strip_pivot_median(scene, r_pivot);
    }
  }
  else {
    lib_assert_msg(0, "Unhandled space type!");
  }
  return has_sel;
}

/* Convert origin (or any other point) from view to rgn space. */
LIB_INLINE void gizmo2d_origin_to_rgn(ARgn *rgn, float *r_origin)
{
  ui_view2d_view_to_rgn_fl(&rgn->v2d, r_origin[0], r_origin[1], &r_origin[0], &r_origin[1]);
}

/* Custom handler for gizmo widgets */
static int gizmo2d_modal(Cxt *C,
                         WinGizmo *widget,
                         const WinEv * /*ev*/,
                         eWinGizmoFlagTweak /*tweak_flag*/)
{
  ARgn *rgn = cxt_win_rgn(C);
  float origin[3];

  gizmo2d_calc_transform_pivot(C, origin);
  gizmo2d_origin_to_rgn(rgn, origin);
  win_gizmo_set_matrix_location(widget, origin);

  ed_rgn_tag_redraw_editor_overlays(rgn);

  return OP_RUNNING_MODAL;
}

static void gizmo2d_xform_setup(const Cxt * /*C*/, WinGizmoGroup *gzgroup)
{
  WinOpType *ot_translate = win_optype_find("TRANSFORM_OT_translate", true);
  GizmoGroup2D *ggd = gizmogroup2d_init(gzgroup);
  gzgroup->customdata = ggd;

  for (int i = 0; i < ARRAY_SIZE(ggd->translate_xy); i++) {
    WinGizmo *gz = ggd->translate_xy[i];

    /* custom handler! */
    win_gizmo_set_fn_custom_modal(gz, gizmo2d_modal);

    if (i < 2) {
      float color[4], color_hi[4];
      gizmo2d_get_axis_color(i, color, color_hi);

      /* set up widget data */
      api_float_set(gz->ptr, "length", 0.8f);
      float axis[3] = {0.0f};
      axis[i] = 1.0f;
      win_gizmo_set_matrix_rotation_from_z_axis(gz, axis);

      float offset[3] = {0, 0, 0};
      offset[2] = 0.18f;
      win_gizmo_set_matrix_offset_location(gz, offset);
      gz->flag |= WIN_GIZMO_DRW_OFFSET_SCALE;

      win_gizmo_set_line_width(gz, GIZMO_AXIS_LINE_WIDTH);
      win_gizmo_set_color(gz, color);
      win_gizmo_set_color_highlight(gz, color_hi);

      win_gizmo_set_scale(gz, 1.0f);
    }
    else {
      float color[4], color_hi[4];
      ui_GetThemeColor4fv(TH_GIZMO_VIEW_ALIGN, color);
      copy_v4_v4(color_hi, color);
      color[3] *= 0.6f;

      ApiProp *prop = api_struct_find_prop(gz->ptr, "icon");
      api_prop_enum_set(gz->ptr, prop, ICON_NONE);

      api_enum_set(gz->ptr, "drw_options", ED_GIZMO_BTN_SHOW_BACKDROP);
      /* Make the center low alpha. */
      win_gizmo_set_line_width(gz, 2.0f);
      api_float_set(gz->ptr, "backdrop_fill_alpha", 0.0);
      win_gizmo_set_color(gz, color);
      win_gizmo_set_color_highlight(gz, color_hi);

      win_gizmo_set_scale(gz, 0.2f);
    }

    /* Assign op */
    ApiPtr *ptr = win_gizmo_op_set(gz, 0, ot_translate, nullptr);
    if (i < 2) {
      bool constraint[3] = {false};
      constraint[i] = true;
      if (api_struct_find_prop(ptr, "constraint_axis")) {
        api_bool_set_array(ptr, "constraint_axis", constraint);
      }
    }

    api_bool_set(ptr, "release_confirm", true);
  }

  {
    WinOpType *ot_resize = win_optype_find("TRANSFORM_OT_resize", true);
    WinOpType *ot_rotate = win_optype_find("TRANSFORM_OT_rotate", true);
    ApiPtr *ptr;

    /* assign op */
    ptr = win_gizmo_op_set(ggd->cage, 0, ot_translate, nullptr);
    api_bool_set(ptr, "release_confirm", true);

    const bool constraint_x[3] = {true, false, false};
    const bool constraint_y[3] = {false, true, false};

    ptr = win_gizmo_op_set(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X, ot_resize, nullptr);
    ApiProp *prop_release_confirm = api_struct_find_prop(ptr, "release_confirm");
    ApiProp *prop_constraint_axis = api_struct_find_prop(ptr, "constraint_axis");
    api_prop_bool_set(ptr, prop_release_confirm, true);
    api_prop_bool_set_array(ptr, prop_constraint_axis, constraint_x);
    ptr = win_gizmo_op_set(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    api_prop_bool_set_array(ptr, prop_constraint_axis, constraint_x);
    ptr = win_gizmo_op_set(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    api_prop_bool_set_array(ptr, prop_constraint_axis, constraint_y);
    ptr = win_gizmo_op_set(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    api_prop_bool_set_array(ptr, prop_constraint_axis, constraint_y);

    ptr = win_gizmo_op_set(
        ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    ptr = win_gizmo_op_set(
        ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    ptr = win_gizmo_op_set(
        ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    ptr = win_gizmo_op_set(
        ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y, ot_resize, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
    ptr = win_gizmo_op_set(ggd->cage, ED_GIZMO_CAGE2D_PART_ROTATE, ot_rotate, nullptr);
    api_prop_bool_set(ptr, prop_release_confirm, true);
  }
}

static void rotate_around_center_v2(float point[2], const float center[2], const float angle)
{
  float tmp[2];

  sub_v2_v2v2(tmp, point, center);
  rotate_v2_v2fl(point, tmp, angle);
  add_v2_v2(point, center);
}

static void gizmo2d_xform_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  GizmoGroup2D *ggd = static_cast<GizmoGroup2D *>(gzgroup->customdata);
  bool has_sel;
  if (ggd->no_cage) {
    has_sel = gizmo2d_calc_transform_pivot(C, ggd->origin);
  }
  else {
    has_sel = gizmo2d_calc_bounds(C, ggd->origin, ggd->min, ggd->max);
    ggd->rotation = gizmo2d_calc_rotation(C);
  }

  bool show_cage = !ggd->no_cage && !equals_v2v2(ggd->min, ggd->max);

  if (has_sel == false) {
    /* Nothing sel. Disable gizmo drwing and return. */
    ggd->cage->flag |= WIN_GIZMO_HIDDEN;
    for (int i = 0; i < ARRAY_SIZE(ggd->translate_xy); i++) {
      ggd->translate_xy[i]->flag |= WIN_GIZMO_HIDDEN;
    }
    return;
  }

  if (!show_cage) {
    /* Disable cage gizmo drawing and return. */
    ggd->cage->flag |= WM_GIZMO_HIDDEN;
    for (int i = 0; i < ARRAY_SIZE(ggd->translate_xy); i++) {
      ggd->translate_xy[i]->flag &= ~WM_GIZMO_HIDDEN;
    }
    return;
  }

  /* We will show the cage gizmo! Setup all necessary data. */
  ggd->cage->flag &= ~WIN_GIZMO_HIDDEN;
  for (int i = 0; i < ARRAY_SIZE(ggd->translate_xy); i++) {
    ggd->translate_xy[i]->flag |= WIN_GIZMO_HIDDEN;
  }
}

static void gizmo2d_xform_drw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ARgn *rgn = cxt_win_rgn(C);
  GizmoGroup2D *ggd = static_cast<GizmoGroup2D *>(gzgroup->customdata);
  float origin[3] = {UNPACK2(ggd->origin), 0.0f};

  gizmo2d_origin_to_rgn(rgn, origin);

  for (int i = 0; i < ARRAY_SIZE(ggd->translate_xy); i++) {
    WinGizmo *gz = ggd->translate_xy[i];
    win_gizmo_set_matrix_location(gz, origin);
  }

  ui_view2d_view_to_rgn_m4(&region->v2d, ggd->cage->matrix_space);
  /* Define the bounding box of the gizmo in the offset transform matrix. */
  unit_m4(ggd->cage->matrix_offset);
  const float min_gizmo_pixel_size = 0.001f; /* Drw Gizmo larger than this many pixels. */
  const float min_scale_axis_x = min_gizmo_pixel_size / ggd->cage->matrix_space[0][0];
  const float min_scale_axis_y = min_gizmo_pixel_size / ggd->cage->matrix_space[1][1];
  ggd->cage->matrix_offset[0][0] = max_ff(min_scale_axis_x, ggd->max[0] - ggd->min[0]);
  ggd->cage->matrix_offset[1][1] = max_ff(min_scale_axis_y, ggd->max[1] - ggd->min[1]);

  ScrArea *area = cxt_win_area(C);

  if (area->spacetype == SPACE_SEQ) {
    Scene *scene = cxt_data_scene(C);
    seq_get_strip_pivot_median(scene, origin);

    float matrix_rotate[4][4];
    unit_m4(matrix_rotate);
    copy_v3_v3(matrix_rotate[3], origin);
    rotate_m4(matrix_rotate, 'Z', ggd->rotation);
    unit_m4(ggd->cage->matrix_basis);
    mul_m4_m4m4(ggd->cage->matrix_basis, matrix_rotate, ggd->cage->matrix_basis);

    float mid[2];
    sub_v2_v2v2(mid, origin, ggd->origin);
    mul_v2_fl(mid, -1.0f);
    copy_v2_v2(ggd->cage->matrix_offset[3], mid);
  }
  else {
    const float origin_aa[3] = {UNPACK2(ggd->origin), 0.0f};
    win_gizmo_set_matrix_offset_location(ggd->cage, origin_aa);
  }
}

static void gizmo2d_xform_invoke_prepare(const Cxt *C,
                                         WinGizmoGroup *gzgroup,
                                         WinGizmo * /*gz*/,
                                         const WinEv * /*ev*/)
{
  GizmoGroup2D *ggd = static_cast<GizmoGroup2D *>(gzgroup->customdata);
  WinGizmoOpElem *gzop;
  const float *mid = ggd->origin;
  const float *min = ggd->min;
  const float *max = ggd->max;

  /* Define the different transform center points that will be used when grabbing the corners or
   * rotating with the gizmo.
   *
   * The coordinates are referred to as their cardinal directions:
   *       N
   *       o
   *NW     |     NE
   * x-----------x
   * |           |
   *W|     C     |E
   * |           |
   * x-----------x
   *SW     S     SE */
  float n[3] = {mid[0], max[1], 0.0f};
  float w[3] = {min[0], mid[1], 0.0f};
  float e[3] = {max[0], mid[1], 0.0f};
  float s[3] = {mid[0], min[1], 0.0f};

  float nw[3] = {min[0], max[1], 0.0f};
  float ne[3] = {max[0], max[1], 0.0f};
  float sw[3] = {min[0], min[1], 0.0f};
  float se[3] = {max[0], min[1], 0.0f};

  float c[3] = {mid[0], mid[1], 0.0f};

  float orient_matrix[3][3];
  unit_m3(orient_matrix);

  ScrArea *area = cxt_win_area(C);

  if (ggd->rotation != 0.0f && area->spacetype == SPACE_SEQ) {
    float origin[3];
    Scene *scene = cxt_data_scene(C);
    seq_get_strip_pivot_median(scene, origin);
    /* We need to rotate the cardinal points so they align with the rotated bounding box. */

    rotate_around_center_v2(n, origin, ggd->rotation);
    rotate_around_center_v2(w, origin, ggd->rotation);
    rotate_around_center_v2(e, origin, ggd->rotation);
    rotate_around_center_v2(s, origin, ggd->rotation);

    rotate_around_center_v2(nw, origin, ggd->rotation);
    rotate_around_center_v2(ne, origin, ggd->rotation);
    rotate_around_center_v2(sw, origin, ggd->rotation);
    rotate_around_center_v2(se, origin, ggd->rotation);

    rotate_around_center_v2(c, origin, ggd->rotation);

    axis_angle_to_mat3_single(orient_matrix, 'Z', ggd->rotation);
  }

  int orient_type = gizmo2d_calc_transform_orientation(C);

  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X);
  ApiProp *prop_center_override = api_struct_find_prop(&gzop->ptr, "center_override");
  ApiProp *prop_mouse_dir = api_struct_find_prop(&gzop->ptr, "mouse_dir_constraint");
  api_prop_float_set_array(&gzop->ptr, prop_center_override, e);
  api_prop_float_set_array(&gzop->ptr, prop_mouse_dir, orient_matrix[0]);
  api_enum_set(&gzop->ptr, "orient_type", orient_type);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, w);
  api_prop_float_set_array(&gzop->ptr, prop_mouse_dir, orient_matrix[0]);
  api_enum_set(&gzop->ptr, "orient_type", orient_type);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, n);
  api_prop_float_set_array(&gzop->ptr, prop_mouse_dir, orient_matrix[1]);
  api_enum_set(&gzop->ptr, "orient_type", orient_type);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, s);
  api_prop_float_set_array(&gzop->ptr, prop_mouse_dir, orient_matrix[1]);
  api_enum_set(&gzop->ptr, "orient_type", orient_type);

  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, ne);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, se);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, nw);
  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, sw);

  gzop = win_gizmo_op_get(ggd->cage, ED_GIZMO_CAGE2D_PART_ROTATE);
  api_prop_float_set_array(&gzop->ptr, prop_center_override, c);
}

void ed_widgetgroup_gizmo2d_xform_cbs_set(WinGizmoGroupType *gzgt)
{
  gzgt->poll = gizmo2d_generic_poll;
  gzgt->setup = gizmo2d_xform_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = gizmo2d_xform_refresh;
  gzgt->drw_prepare = gizmo2d_xform_drw_prepare;
  gzgt->invoke_prepare = gizmo2d_xform_invoke_prepare;
}

static void gizmo2d_xform_setup_no_cage(const Cxt *C, WinGizmoGroup *gzgroup)
{
  gizmo2d_xform_setup(C, gzgroup);
  GizmoGroup2D *ggd = static_cast<GizmoGroup2D *>(gzgroup->customdata);
  ggd->no_cage = true;
}

static void gizmo2d_xform_no_cage_msg_sub(const Cxt *C,
                                          WinGizmoGroup *gzgroup,
                                          WinMsgBus *mbus)
{
  Screen *screen = cxt_win_screen(C);
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);
  gizmo2d_pivot_point_msg_sub(gzgroup, mbus, screen, area, rgn);
}

void ed_widgetgroup_gizmo2d_xform_no_cage_cbs_set(WinGizmoGroupType *gzgt)
{
  ed_widgetgroup_gizmo2d_xform_cbs_set(gzgt);
  gzgt->setup = gizmo2d_xform_setup_no_cage;
  gzgt->msg_sub = gizmo2d_xform_no_cage_msg_sub;
}

/* Scale Handles
 * Defines public fns, not the gizmo itself:
 * ed_widgetgroup_gizmo2d_resize_cbs_set */
struct GizmoGroup_Resize2D {
  WinGizmo *gizmo_xy[3];
  float origin[2];
  float rotation;
};

static GizmoGroupResize2D *gizmogroup2d_resize_init(WinGizmoGroup *gzgroup)
{
  const WinGizmoType *gzt_arrow = win_gizmotype_find("GIZMO_GT_arrow_3d", true);
  const WinGizmoType *gzt_btn = win_gizmotype_find("GIZMO_GT_btn_2d", true);

  GizmoGroupResize2D *ggd = static_cast<GizmoGroupResize2D *>(
      mem_calloc(sizeof(GizmoGroupResize2D), __func__));

  ggd->gizmo_xy[0] = win_gizmo_new_ptr(gzt_arrow, gzgroup, nullptr);
  ggd->gizmo_xy[1] = win_gizmo_new_ptr(gzt_arrow, gzgroup, nullptr);
  ggd->gizmo_xy[2] = win_gizmo_new_ptr(gzt_btn, gzgroup, nullptr);

  return ggd;
}

static void gizmo2d_resize_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  GizmoGroupResize2D *ggd = static_cast<GizmoGroupResize2D *>(gzgroup->customdata);
  float origin[3];
  const bool has_sel = gizmo2d_calc_transform_pivot(C, origin);

  if (has_sel == false) {
    for (int i = 0; i < ARRAY_SIZE(ggd->gizmo_xy); i++) {
      ggd->gizmo_xy[i]->flag |= WIN_GIZMO_HIDDEN;
    }
  }
  else {
    for (int i = 0; i < ARRAY_SIZE(ggd->gizmo_xy); i++) {
      ggd->gizmo_xy[i]->flag &= ~WIN_GIZMO_HIDDEN;
    }
    copy_v2_v2(ggd->origin, origin);
    ggd->rotation = gizmo2d_calc_rotation(C);
  }
}

static void gizmo2d_resize_drw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ARgn *rgn = cxt_win_rgn(C);
  GizmoGroupResize2D *ggd = static_cast<GizmoGroupResize2D *>(gzgroup->customdata);
  float origin[3] = {UNPACK2(ggd->origin), 0.0f};

  gizmo2d_origin_to_rgn(rgn, origin);

  for (int i = 0; i < ARRAY_SIZE(ggd->gizmo_xy); i++) {
    WinGizmo *gz = ggd->gizmo_xy[i];
    win_gizmo_set_matrix_location(gz, origin);

    if (i < 2) {
      float axis[3] = {0.0f}, rotated_axis[3];
      axis[i] = 1.0f;
      rotate_v3_v3v3fl(rotated_axis, axis, dune::float3{0, 0, 1}, ggd->rotation);
      win_gizmo_set_matrix_rotation_from_z_axis(gz, rotated_axis);
    }
  }
}

static void gizmo2d_resize_setup(const Cxt * /*C*/, WinGizmoGroup *gzgroup)
{

  WinOpType *ot_resize = win_optype_find("TRANSFORM_OT_resize", true);
  GizmoGroupResize2D *ggd = gizmogroup2d_resize_init(gzgroup);
  gzgroup->customdata = ggd;

  for (int i = 0; i < ARRAY_SIZE(ggd->gizmo_xy); i++) {
    WinGizmo *gz = ggd->gizmo_xy[i];

    /* custom handler! */
    win_gizmo_set_fn_custom_modal(gz, gizmo2d_modal);

    if (i < 2) {
      float color[4], color_hi[4];
      gizmo2d_get_axis_color(i, color, color_hi);

      /* set up widget data */
      api_float_set(gz->ptr, "length", 1.0f);
      api_enum_set(gz->ptr, "drw_style", ED_GIZMO_ARROW_STYLE_BOX);

      win_gizmo_set_line_width(gz, GIZMO_AXIS_LINE_WIDTH);
      win_gizmo_set_color(gz, color);
      win_gizmo_set_color_highlight(gz, color_hi);

      win_gizmo_set_scale(gz, 1.0f);
    }
    else {
      float color[4], color_hi[4];
      ui_GetThemeColor4fv(TH_GIZMO_VIEW_ALIGN, color);
      copy_v4_v4(color_hi, color);
      color[3] *= 0.6f;

      ApiProp *prop = api_struct_find_prop(gz->ptr, "icon");
      api_prop_enum_set(gz->ptr, prop, ICON_NONE);

      api_enum_set(gz->ptr, "drw_options", ED_GIZMO_BTN_SHOW_BACKDROP);
      /* Make the center low alpha. */
      win_gizmo_set_line_width(gz, 2.0f);
      api_float_set(gz->ptr, "backdrop_fill_alpha", 0.0);
      win_gizmo_set_color(gz, color);
      win_gizmo_set_color_highlight(gz, color_hi);

      win_gizmo_set_scale(gz, 1.2f);
    }

    /* Assign op */
    ApiPtr *ptr = win_gizmo_op_set(gz, 0, ot_resize, nullptr);
    if (i < 2) {
      bool constraint[3] = {false};
      constraint[i] = true;
      if (api_struct_find_prop(ptr, "constraint_axis")) {
        api_bool_set_array(ptr, "constraint_axis", constraint);
      }
    }
    api_bool_set(ptr, "release_confirm", true);
  }
}

static void gizmo2d_resize_invoke_prepare(const Cxt *C,
                                          WinGizmoGroup * /*gzgroup*/,
                                          WinGizmo *gz,
                                          const WinEv * /*ev*/)
{
  WinGizmoOpElem *gzop;
  int orient_type = gizmo2d_calc_transform_orientation(C);

  gzop = win_gizmo_op_get(gz, 0);
  api_enum_set(&gzop->ptr, "orient_type", orient_type);
}

static void gizmo2d_resize_msg_sub(const Cxt *C,
                                   WinGizmoGroup *gzgroup,
                                   WinMsgBus *mbus)
{
  Screen *screen = cxt_win_screen(C);
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);
  gizmo2d_pivot_point_msg_sub(gzgroup, mbus, screen, area, rgn);
}

void ed_widgetgroup_gizmo2d_resize_cbs_set(WinGizmoGroupType *gzgt)
{
  gzgt->poll = gizmo2d_generic_poll;
  gzgt->setup = gizmo2d_resize_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = gizmo2d_resize_refresh;
  gzgt->drw_prepare = gizmo2d_resize_drw_prepare;
  gzgt->invoke_prepare = gizmo2d_resize_invoke_prepare;
  gzgt->msg_sub = gizmo2d_resize_msg_sub;
}


/* Rotate Handles
 * Defines public fns, not the gizmo itself:
 * - ed_widgetgroup_gizmo2d_rotate_setup */
struct GizmoGroupRotate2D {
  WinGizmo *gizmo;
  float origin[2];
};

static GizmoGroupRotate2D *gizmogroup2d_rotate_init(WinGizmoGroup *gzgroup)
{
  const WinGizmoType *gzt_btn = win_gizmotype_find("GIZMO_GT_btn_2d", true);

  GizmoGroupRotate2D *ggd = static_cast<GizmoGroupRotate2D *>(
      mem_calloc(sizeof(GizmoGroupRotate2D), __func__));

  ggd->gizmo = win_gizmo_new_ptr(gzt_btn, gzgroup, nullptr);

  return ggd;
}

static void gizmo2d_rotate_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  GizmoGroupRotate2D *ggd = static_cast<GizmoGroupRotate2D *>(gzgroup->customdata);
  float origin[3];
  const bool has_sel = gizmo2d_calc_transform_pivot(C, origin);

  if (has_select == false) {
    ggd->gizmo->flag |= WIN_GIZMO_HIDDEN;
  }
  else {
    ggd->gizmo->flag &= ~WIN_GIZMO_HIDDEN;
    copy_v2_v2(ggd->origin, origin);
  }
}

static void gizmo2d_rotate_drw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ARgn *rgn = cxt_win_rgn(C);
  GizmoGroupRotate2D *ggd = static_cast<GizmoGroupRotate2D *>(gzgroup->customdata);
  float origin[3] = {UNPACK2(ggd->origin), 0.0f};

  gizmo2d_origin_to_rgn(rgn, origin);

  WinGizmo *gz = ggd->gizmo;
  win_gizmo_set_matrix_location(gz, origin);
}

static void gizmo2d_rotate_setup(const Cxt * /*C*/, WinGizmoGroup *gzgroup)
{

  WinOpType *ot_resize = win_optype_find("TRANSFORM_OT_rotate", true);
  GizmoGroupRotate2D *ggd = gizmogroup2d_rotate_init(gzgroup);
  gzgroup->customdata = ggd;

  /* Other setup fns iter over axis. */
  {
    winGizmo *gz = ggd->gizmo;

    /* custom handler! */
    win_gizmo_set_fn_custom_modal(gz, gizmo2d_modal);
    win_gizmo_set_scale(gz, 1.2f);

    {
      float color[4];
      ui_GetThemeColor4fv(TH_GIZMO_VIEW_ALIGN, color);

      ApiProp *prop = api_struct_find_prop(gz->ptr, "icon");
      api_prop_enum_set(gz->ptr, prop, ICON_NONE);

      api_enum_set(gz->ptr, "drw_options", ED_GIZMO_BTN_SHOW_BACKDROP);
      /* Make the center low alpha. */
      win_gizmo_set_line_width(gz, 2.0f);
      api_float_set(gz->ptr, "backdrop_fill_alpha", 0.0);
      win_gizmo_set_color(gz, color);
      win_gizmo_set_color_highlight(gz, color);
    }

    /* Assign op. */
    ApiPtr *ptr = win_gizmo_op_set(gz, 0, ot_resize, nullptr);
    api_bool_set(ptr, "release_confirm", true);
  }
}

static void gizmo2d_rotate_msg_sub(const Cxt *C,
                                   WinGizmoGroup *gzgroup,
                                   WinMsgBus *mbus)
{
  Screen *screen = cxt_win_screen(C);
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);
  gizmo2d_pivot_point_msg_sub(gzgroup, mbus, screen, area, rgn);
}

void ed_widgetgroup_gizmo2d_rotate_cbs_set(WinGizmoGroupType *gzgt)
{
  gzgt->poll = gizmo2d_generic_poll;
  gzgt->setup = gizmo2d_rotate_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = gizmo2d_rotate_refresh;
  gzgt->drw_prepare = gizmo2d_rotate_drw_prepare;
  gzgt->msg_sub = gizmo2d_rotate_msg_sub;
}
