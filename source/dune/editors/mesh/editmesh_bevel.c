#include "mem_guardedalloc.h"

#include "types_object.h"

#include "lib_math.h"
#include "lib_string.h"

#include "lang.h"

#include "dune_cxt.h"
#include "dune_editmesh.h"
#include "dune_global.h"
#include "dune_layer.h"
#include "dune_unit.h"

#include "types_curveprofile.h"
#include "types_mesh.h"

#include "api_access.h"
#include "api_define.h"
#include "api_prototypes.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "ed_mesh.h"
#include "ed_numinput.h"
#include "ed_screen.h"
#include "ed_space_api.h"
#include "ed_transform.h"
#include "ed_util.h"
#include "ed_view3d.h"

#include "mesh_intern.h" /* own include */

#define MVAL_PIXEL_MARGIN 5.0f

#define PROFILE_HARD_MIN 0.0f

#define SEGMENTS_HARD_MAX 1000

/* which value is mouse movement and numeric input controlling? */
#define OFFSET_VAL 0
#define OFFSET_VAL_PERCENT 1
#define PROFILE_VAL 2
#define SEGMENTS_VAL 3
#define NUM_VAL_KINDS 4

static const char *val_api_name[NUM_VAL_KINDS] = {
    "offset", "offset_pct", "profile", "segments"};
static const float val_clamp_min[NUM_VAL_KINDS] = {0.0f, 0.0f, PROFILE_HARD_MIN, 1.0f};
static const float val_clamp_max[NUM_VAL_KINDS] = {1e6, 100.0f, 1.0f, SEGMENTS_HARD_MAX};
static const float val_start[NUM_VAL_KINDS] = {0.0f, 0.0f, 0.5f, 1.0f};
static const float val_scale_per_inch[NUM_VAL_KINDS] = {0.0f, 100.0f, 1.0f, 4.0f};

typedef struct {
  /* Every object must have a valid MeshEdit. */
  Object *ob;
  MeshBackup mesh_backup;
} BevelObjectStore;

typedef struct {
  float initial_length[NUM_VALUE_KINDS];
  float scale[NUM_VALUE_KINDS];
  NumInput num_input[NUM_VALUE_KINDS];
  /* The current value when shift is pressed. Negative when shift not active. */
  float shift_val[NUM_VALUE_KINDS];
  float max_obj_scale;
  bool is_modal;

  BevelObjectStore *ob_store;
  uint ob_store_len;

  /* modal only */
  int launch_event;
  float mcenter[2];
  void *draw_handle_pixel;
  short value_mode; /* Which value does mouse movement and numeric input affect? */
  float segments;   /* Segments as float so smooth mouse pan works in small increments */

  CurveProfile *custom_profile;
} BevelData;

enum {
  BEV_MODAL_CANCEL = 1,
  BEV_MODAL_CONFIRM,
  BEV_MODAL_VAL_OFFSET,
  BEV_MODAL_VAL_PROFILE,
  BEV_MODAL_VAL_SEGMENTS,
  BEV_MODAL_SEGMENTS_UP,
  BEV_MODAL_SEGMENTS_DOWN,
  BEV_MODAL_OFFSET_MODE_CHANGE,
  BEV_MODAL_CLAMP_OVERLAP_TOGGLE,
  BEV_MODAL_AFFECT_CHANGE,
  BEV_MODAL_HARDEN_NORMALS_TOGGLE,
  BEV_MODAL_MARK_SEAM_TOGGLE,
  BEV_MODAL_MARK_SHARP_TOGGLE,
  BEV_MODAL_OUTER_MITER_CHANGE,
  BEV_MODAL_INNER_MITER_CHANGE,
  BEV_MODAL_PROFILE_TYPE_CHANGE,
  BEV_MODAL_VERT_MESH_CHANGE,
};

static float get_bevel_offset(wmOp *op)
{
  if (api_enum_get(op->ptr, "offset_type") == BEVEL_AMT_PERCENT) {
    return api_float_get(op->ptr, "offset_pct");
  }
  return api_float_get(op->ptr, "offset");
}

static void edbm_bevel_update_status_txt(Cx *C, wmOp *op)
{
  char status_text[UI_MAX_DRW_STR];
  char buf[UI_MAX_DRW_STR];
  char *p = buf;
  int available_len = sizeof(buf);
  Scene *sce = cx_data_scene(C);

#define WM_MODALKEY(_id) \
  wm_modalkeymap_op_items_to_string_buf( \
      op->type, (_id), true, UI_MAX_SHORTCUT_STR, &available_len, &p)

  char offset_str[NUM_STR_REP_LEN];
  if (api_enum_get(op->ptr, "offset_type") == BEVEL_AMT_PERCENT) {
    lib_snprintf(offset_str, NUM_STR_REP_LEN, "%.1f%%", api_float_get(op->ptr, "offset_pct"));
  }
  else {
    double offset_val = (double)api_float_get(op->ptr, "offset");
    dune_unit_value_as_string(offset_str,
                             NUM_STR_REP_LEN,
                             offset_val * sce->unit.scale_length,
                             3,
                             B_UNIT_LENGTH,
                             &sce->unit,
                             true);
  }
    
  ApiProp *prop;
  const char *mode_str, *omiter_str, *imiter_str, *vmesh_str, *profile_type_str, *affect_str;
  prop = api_struct_find_prop(op->ptr, "offset_type");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &mode_str);
  prop = api_struct_find_prop(op->ptr, "profile_type");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &profile_type_str);
  prop = api_struct_find_prop(op->ptr, "miter_outer");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &omiter_str);
  prop = api_struct_find_prop(op->ptr, "miter_inner");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &imiter_str);
  prop = api_struct_find_prop(op->ptr, "vmesh_method");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &vmesh_str);
  prop = api_struct_find_prop(op->ptr, "affect");
  api_prop_enum_name_gettexted(
      C, op->ptr, prop, api_prop_enum_get(op->ptr, prop), &affect_str);

  lib_snprintf(status_text,
               sizeof(status_text),
               TIP_("%s: Confirm, "
                    "%s: Cancel, "
                    "%s: Mode (%s), "
                    "%s: Width (%s), "
                    "%s: Segments (%d), "
                    "%s: Profile (%.3f), "
                    "%s: Clamp Overlap (%s), "
                    "%s: Affect (%s), "
                    "%s: Outer Miter (%s), "
                    "%s: Inner Miter (%s), "
                    "%s: Harden Normals (%s), "
                    "%s: Mark Seam (%s), "
                    "%s: Mark Sharp (%s), "
                    "%s: Profile Type (%s), "
                    "%s: Intersection (%s)"),
               WM_MODALKEY(BEV_MODAL_CONFIRM),
               WM_MODALKEY(BEV_MODAL_CANCEL),
               WM_MODALKEY(BEV_MODAL_OFFSET_MODE_CHANGE),
               mode_str,
               WM_MODALKEY(BEV_MODAL_VALUE_OFFSET),
               offset_str,
               WM_MODALKEY(BEV_MODAL_VALUE_SEGMENTS),
               RNA_int_get(op->ptr, "segments"),
               WM_MODALKEY(BEV_MODAL_VALUE_PROFILE),
               RNA_float_get(op->ptr, "profile"),
               WM_MODALKEY(BEV_MODAL_CLAMP_OVERLAP_TOGGLE),
               WM_bool_as_string(RNA_boolean_get(op->ptr, "clamp_overlap")),
               WM_MODALKEY(BEV_MODAL_AFFECT_CHANGE),
               affect_str,
               WM_MODALKEY(BEV_MODAL_OUTER_MITER_CHANGE),
               omiter_str,
               WM_MODALKEY(BEV_MODAL_INNER_MITER_CHANGE),
               imiter_str,
               WM_MODALKEY(BEV_MODAL_HARDEN_NORMALS_TOGGLE),
               WM_bool_as_string(RNA_boolean_get(op->ptr, "harden_normals")),
               WM_MODALKEY(BEV_MODAL_MARK_SEAM_TOGGLE),
               WM_bool_as_string(RNA_boolean_get(op->ptr, "mark_seam")),
               WM_MODALKEY(BEV_MODAL_MARK_SHARP_TOGGLE),
               WM_bool_as_string(RNA_boolean_get(op->ptr, "mark_sharp")),
               WM_MODALKEY(BEV_MODAL_PROFILE_TYPE_CHANGE),
               profile_type_str,
               WM_MODALKEY(BEV_MODAL_VERTEX_MESH_CHANGE),
               vmesh_str);

#undef WM_MODALKEY

  ed_workspace_status_text(C, status_text);
}

static bool edm_bevel_init(Cxt *C, wmOp *op, const bool is_modal)
{
  Scene *scene = cx_data_scene(C);
  View3D *v3d = cx_wm_view3d(C);
  ToolSettings *ts = cx_data_tool_settings(C);
  ViewLayer *view_layer = cx_data_view_layer(C);

  if (is_modal) {
    api_float_set(op->ptr, "offset", 0.0f);
    api_float_set(op->ptr, "offset_pct", 0.0f);
  }

  op->customdata = mem_mallocn(sizeof(BevelData), "beveldata_mesh_operator");
  BevelData *opdata = op->customdata;
  uint objects_used_len = 0;
  opdata->max_obj_scale = FLT_MIN;

  /* Put the Curve Profile from the toolsettings into the opdata struct */
  opdata->custom_profile = ts->custom_bevel_profile_preset;

  {
    uint ob_store_len = 0;
    Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
        view_layer, v3d, &ob_store_len);
    opdata->ob_store = mem_malloc_arrayn(ob_store_len, sizeof(*opdata->ob_store), __func__);
    for (uint ob_index = 0; ob_index < ob_store_len; ob_index++) {
      Object *obedit = objects[ob_index];
      float scale = mat4_to_scale(obedit->obmat);
      opdata->max_obj_scale = max_ff(opdata->max_obj_scale, scale);
      MeshEdit *em = dune_editmesh_from_object(obedit);
      if (em->bm->totvertsel > 0) {
        opdata->ob_store[objects_used_len].ob = obedit;
        objects_used_len++;
      }
    }
    mem_freen(objects);
    opdata->ob_store_len = objects_used_len;
  }

  opdata->is_modal = is_modal;
  int otype = api_enum_get(op->ptr, "offset_type");
  opdata->value_mode = (otype == BEVEL_AMT_PERCENT) ? OFFSET_VAL_PERCENT : OFFSET_VAL;
  opdata->segments = (float)api_int_get(op->ptr, "segments");
  float pixels_per_inch = U.dpi * U.pixelsize;

  for (int i = 0; i < NUM_VAL_KINDS; i++) {
    opdata->shift_val[i] = -1.0f;
    opdata->initial_length[i] = -1.0f;
    /* NOTE: scale for OFFSET_VAL will get overwritten in #edbm_bevel_invoke. */
    opdata->scale[i] = val_scale_per_inch[i] / pixels_per_inch;

    initNumInput(&opdata->num_input[i]);
    opdata->num_input[i].idx_max = 0;
    opdata->num_input[i].val_flag[0] |= NUM_NO_NEGATIVE;
    opdata->num_input[i].unit_type[0] = B_UNIT_NONE;
    if (i == SEGMENTS_VAL) {
      opdata->num_input[i].val_flag[0] |= NUM_NO_FRACTION | NUM_NO_ZERO;
    }
    if (i == OFFSET_VAL) {
      opdata->num_input[i].unit_sys = scene->unit.system;
      opdata->num_input[i].unit_type[0] = B_UNIT_LENGTH;
    }
  }

  /* avoid the cost of allocating a bm copy */
  if (is_modal) {
    ARgn *rgn = cx_wm_rgn(C);

    for (uint ob_index = 0; ob_index < opdata->ob_store_len; ob_index++) {
      Object *obedit = opdata->ob_store[ob_index].ob;
      MeshEdit *em = dune_editmesh_from_object(obedit);
      opdata->ob_store[ob_index].mesh_backup = editmesh_redo_state_store(em);
    }
    opdata->draw_handle_pixel = ed_rgn_drw_cb_activate(
        rgn->type, ed_regn_drw_mouse_line_cb, opdata->mcenter, RGN_DRW_POST_PIXEL);
    G.moving = G_TRANSFORM_EDIT;
  }

  return true;
}

static bool edbm_bevel_calc(wmOp *op)
{
  BevelData *opdata = op->customdata;
  MeshOp mop;
  bool changed = false;

  const float offset = get_bevel_offset(op);
  const int offset_type = api_enum_get(op->ptr, "offset_type");
  const int profile_type = api_enum_get(op->ptr, "profile_type");
  const int segments = api_int_get(op->ptr, "segments");
  const float profile = api_float_get(op->ptr, "profile");
  const bool affect = api_enum_get(op->ptr, "affect");
  const bool clamp_overlap = api_bool_get(op->ptr, "clamp_overlap");
  const int material_init = api_int_get(op->ptr, "material");
  const bool loop_slide = api_bool_get(op->ptr, "loop_slide");
  const bool mark_seam = api_bool_get(op->ptr, "mark_seam");
  const bool mark_sharp = api_bool_get(op->ptr, "mark_sharp");
  const bool harden_normals = api_bool_get(op->ptr, "harden_normals");
  const int face_strength_mode = api_enum_get(op->ptr, "face_strength_mode");
  const int miter_outer = api_enum_get(op->ptr, "miter_outer");
  const int miter_inner = api_enum_get(op->ptr, "miter_inner");
  const float spread = api_float_get(op->ptr, "spread");
  const int vmesh_method = api_enum_get(op->ptr, "vmesh_method");

  for (uint ob_index = 0; ob_index < opdata->ob_store_len; ob_index++) {
    Object *obedit = opdata->ob_store[ob_index].ob;
    MeshEdit *em = dune_editmesh_from_object(obedit);

    /* revert to original mesh */
    if (opdata->is_modal) {
      editmesh_redo_state_restore(&opdata->ob_store[ob_index].mesh_backup, em, false);
    }

    const int material = CLAMPIS(material_init, -1, obedit->totcol - 1);

    Mesh *me = obedit->data;

    if (harden_normals && !(me->flag & ME_AUTOSMOOTH)) {
      /* harden_normals only has a visible effect if autosmooth is on, so turn it on */
      me->flag |= ME_AUTOSMOOTH;
    }

    editmesh_op_init(em,
                 &mop,
                 op,
                 "bevel geom=%hev offset=%f segments=%i affect=%i offset_type=%i "
                 "profile_type=%i profile=%f clamp_overlap=%b material=%i loop_slide=%b "
                 "mark_seam=%b mark_sharp=%b harden_normals=%b face_strength_mode=%i "
                 "miter_outer=%i miter_inner=%i spread=%f smoothresh=%f custom_profile=%p "
                 "vmesh_method=%i",
                 MESH_ELEM_SELECT,
                 offset,
                 segments,
                 affect,
                 offset_type,
                 profile_type,
                 profile,
                 clamp_overlap,
                 material,
                 loop_slide,
                 mark_seam,
                 mark_sharp,
                 harden_normals,
                 face_strength_mode,
                 miter_outer,
                 miter_inner,
                 spread,
                 me->smoothresh,
                 opdata->custom_profile,
                 vmesh_method);

    MO_op_ex(em->mesh, &mop);

    if (offset != 0.0f) {
      /* not essential, but we may have some loose geometry that
       * won't get bevel'd and better not leave it selected */
      EDBM_flag_disable_all(em, BM_ELEM_SELECT);
      MO_slot_buffer_hflag_enable(
          em->mesh, mop.slots_out, "faces.out", BM_FACE, BM_ELEM_SELECT, true);
    }

    /* no need to de-select existing geometry */
    if (!EDBM_op_finish(em, &mop, op, true)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });
    changed = true;
  }
  return changed;
}

static void edbm_bevel_exit(Cxt *C, wmOp *op)
{
  BevelData *opdata = op->customdata;
  ScrArea *area = cxt_wm_area(C);

  if (area) {
    ed_area_status_text(area, NULL);
  }

  for (uint ob_index = 0; ob_index < opdata->ob_store_len; ob_index++) {
    Object *obedit = opdata->ob_store[ob_index].ob;
    MeshEdit *em = dune_editmesh_from_object(obedit);
    /* Without this, faces surrounded by selected edges/verts will be unselected. */
    if ((em->selectmode & SCE_SELECT_FACE) == 0) {
      EDBM_selectmode_flush(em);
    }
  }

  if (opdata->is_modal) {
    ARgn *rgn = cx_wm_rgn(C);
    for (uint ob_index = 0; ob_index < opdata->ob_store_len; ob_index++) {
      EDBM_redo_state_free(&opdata->ob_store[ob_index].mesh_backup);
    }
    ed_rgn_drw_cb_exit(region->type, opdata->drw_handle_pixel);
    G.moving = 0;
  }
  MEM_SAFE_FREE(opdata->ob_store);
  MEM_SAFE_FREE(op->customdata);
  op->customdata = NULL;
}

static void edmesh_bevel_cancel(Cxt *C, wmOp *op)
{
  BevelData *opdata = op->customdata;
  if (opdata->is_modal) {
    for (uint ob_index = 0; ob_index < opdata->ob_store_len; ob_index++) {
      Object *obedit = opdata->ob_store[ob_index].ob;
      MeshEdit *em = dune_editmesh_from_object(obedit);
      EDBM_redo_state_restore_and_free(&opdata->ob_store[ob_index].mesh_backup, em, true);
      EDBM_update(obedit->data,
                  &(const struct EDMUpdate_Params){
                      .calc_looptri = false,
                      .calc_normals = true,
                      .is_destructive = true,
                  });
    }
  }

  edmesh_bevel_exit(C, op);

  /* need to force redisplay or we may still view the modified result */
  ed_rgn_tag_redrw(cx_wm_rgn(C));
}

/* bevel! yay!! */
static int edbm_bevel_ex(Cx *C, wmOp *op)
{
  if (!edbm_bevel_init(C, op, false)) {
    return OP_CANCELLED;
  }

  if (!edm_bevel_calc(op)) {
    edm_bevel_cancel(C, op);
    return OP_CANCELLED;
  }

  edm_bevel_exit(C, op);

  return OP_FINISHED;
}

static void edbm_bevel_calc_initial_length(wmOp *op, const wmEvent *event, bool mode_changed)
{
  BevelData *opdata = op->customdata;
  const float mlen[2] = {
      opdata->mcenter[0] - event->mval[0],
      opdata->mcenter[1] - event->mval[1],
  };
  float len = len_v2(mlen);
  int vmode = opdata->value_mode;
  if (mode_changed || opdata->initial_length[vmode] == -1.0f) {
    /* If current value is not default start value, adjust len so that
     * the scaling and offset in edbm_bevel_mouse_set_value will
     * start at current value */
    float value = (vmode == SEGMENTS_VAL) ? opdata->segments :
                                              RNA_float_get(op->ptr, value_rna_name[vmode]);
    float sc = opdata->scale[vmode];
    float st = value_start[vmode];
    if (value != value_start[vmode]) {
      len = (st + sc * (len - MVAL_PIXEL_MARGIN) - value) / sc;
    }
  }
  opdata->initial_length[opdata->value_mode] = len;
}

static int edm_bevel_invoke(Cx *C, wmOp *op, const wmEvent *event)
{
  RgnView3D *rv3d = cx_wm_rgn_view3d(C);

  if (!edm_bevel_init(C, op, true)) {
    return OP_CANCELLED;
  }

  BevelData *opdata = op->customdata;

  opdata->launch_event = wm_userdef_event_type_from_keymap_type(event->type);

  /* initialize mouse values */
  float center_3d[3];
  if (!calculateTransformCenter(C, V3D_AROUND_CENTER_MEDIAN, center_3d, opdata->mcenter)) {
    /* in this case the tool will likely do nothing,
     * ideally this will never happen and should be checked for above */
    opdata->mcenter[0] = opdata->mcenter[1] = 0;
  }

  /* for OFFSET_VALUE only, the scale is the size of a pixel under the mouse in 3d space */
  opdata->scale[OFFSET_VAL] = rv3d ? ed_view3d_pixel_size(rv3d, center_3d) : 1.0f;
  /* since we are affecting untransformed object but seeing in transformed space,
   * compensate for that */
  opdata->scale[OFFSET_VAL] /= opdata->max_obj_scale;

  edm_bevel_calc_initial_length(op, event, false);

  edm_bevel_update_status_txt(C, op);

  if (!edm_bevel_calc(op)) {
    edm_bevel_cancel(C, op);
    ed_workspace_status_txt(C, NULL);
    return OP_CANCELLED;
  }

  wm_event_add_modal_handler(C, op);

  return OP_RUNNING_MODAL;
}

static void edm_bevel_mouse_set_val(wmOp *op, const wmEvent *event)
{
  BevelData *opdata = op->customdata;
  int vmode = opdata->value_mode;

  const float mdiff[2] = {
      opdata->mcenter[0] - event->mval[0],
      opdata->mcenter[1] - event->mval[1],
  };

  float val = ((len_v2(mdiff) - MVAL_PIXEL_MARGIN) - opdata->initial_length[vmode]);

  /* Scale according to value mode */
  val = value_start[vmode] + val * opdata->scale[vmode];

  /* Fake shift-transform... */
  if (event->mod & KM_SHIFT) {
    if (opdata->shift_val[vmode] < 0.0f) {
      opdata->shift_val[vmode] = (vmode == SEGMENTS_VALUE) ?
                                       opdata->segments :
                                       api_float_get(op->ptr, val_api_name[vmode]);
    }
    val = (val - opdata->shift_val[vmode]) * 0.1f + opdata->shift_val[vmode];
  }
  else if (opdata->shift_val[vmode] >= 0.0f) {
    opdata->shift_value[vmode] = -1.0f;
  }

  /* Clamp according to value mode, and store value back. */
  CLAMP(val, val_clamp_min[vmode], val_clamp_max[vmode]);
  if (vmode == SEGMENTS_VALUE) {
    opdata->segments = val;
    api_int_set(op->ptr, "segments", (int)(val + 0.5f));
  }
  else {
    api_float_set(op->ptr, val_api_name[vmode], val);
  }
}

static void edm_bevel_numinput_set_val(wmOp *op)
{
  BevelData *opdata = op->customdata;

  int vmode = opdata->value_mode;
  float val = (vmode == SEGMENTS_VAL) ? opdata->segments :
                                            api_float_get(op->ptr, value_api_name[vmode]);
  applyNumInput(&opdata->num_input[vmode], &val);
  CLAMP(val, val_clamp_min[vmode], val_clamp_max[vmode]);
  if (vmode == SEGMENTS_VAL) {
    opdata->segments = val;
    api_int_set(op->ptr, "segments", (int)val);
  }
  else {
    api_float_set(op->ptr, val_api_name[vmode], val);
  }
}

wmKeyMap *bevel_modal_keymap(wmKeyConfig *keyconf)
{
  static const EnumPropItem modal_items[] = {
      {BEV_MODAL_CANCEL, "CANCEL", 0, "Cancel", "Cancel bevel"},
      {BEV_MODAL_CONFIRM, "CONFIRM", 0, "Confirm", "Confirm bevel"},
      {BEV_MODAL_VALUE_OFFSET, "VALUE_OFFSET", 0, "Change Offset", "Value changes offset"},
      {BEV_MODAL_VALUE_PROFILE, "VALUE_PROFILE", 0, "Change Profile", "Value changes profile"},
      {BEV_MODAL_VALUE_SEGMENTS, "VALUE_SEGMENTS", 0, "Change Segments", "Value changes segments"},
      {BEV_MODAL_SEGMENTS_UP, "SEGMENTS_UP", 0, "Increase Segments", "Increase segments"},
      {BEV_MODAL_SEGMENTS_DOWN, "SEGMENTS_DOWN", 0, "Decrease Segments", "Decrease segments"},
      {BEV_MODAL_OFFSET_MODE_CHANGE,
       "OFFSET_MODE_CHANGE",
       0,
       "Change Offset Mode",
       "Cycle through offset modes"},
      {BEV_MODAL_CLAMP_OVERLAP_TOGGLE,
       "CLAMP_OVERLAP_TOGGLE",
       0,
       "Toggle Clamp Overlap",
       "Toggle clamp overlap flag"},
      {BEV_MODAL_AFFECT_CHANGE,
       "AFFECT_CHANGE",
       0,
       "Change Affect Type",
       "Change which geometry type the operation affects, edges or vertices"},
      {BEV_MODAL_HARDEN_NORMALS_TOGGLE,
       "HARDEN_NORMALS_TOGGLE",
       0,
       "Toggle Harden Normals",
       "Toggle harden normals flag"},
      {BEV_MODAL_MARK_SEAM_TOGGLE,
       "MARK_SEAM_TOGGLE",
       0,
       "Toggle Mark Seam",
       "Toggle mark seam flag"},
      {BEV_MODAL_MARK_SHARP_TOGGLE,
       "MARK_SHARP_TOGGLE",
       0,
       "Toggle Mark Sharp",
       "Toggle mark sharp flag"},
      {BEV_MODAL_OUTER_MITER_CHANGE,
       "OUTER_MITER_CHANGE",
       0,
       "Change Outer Miter",
       "Cycle through outer miter kinds"},
      {BEV_MODAL_INNER_MITER_CHANGE,
       "INNER_MITER_CHANGE",
       0,
       "Change Inner Miter",
       "Cycle through inner miter kinds"},
      {BEV_MODAL_PROFILE_TYPE_CHANGE, "PROFILE_TYPE_CHANGE", 0, "Cycle through profile types", ""},
      {BEV_MODAL_VERTEX_MESH_CHANGE,
       "VERTEX_MESH_CHANGE",
       0,
       "Change Intersection Method",
       "Cycle through intersection methods"},
      {0, NULL, 0, NULL, NULL},
  };

  wmKeyMap *keymap = wm_modalkeymap_find(keyconf, "Bevel Modal Map");

  /* This function is called for each spacetype, only needs to add map once */
  if (keymap && keymap->modal_items) {
    return NULL;
  }

  keymap = wm_modalkeymap_ensure(keyconf, "Bevel Modal Map", modal_items);

  wm_modalkeymap_assign(keymap, "mesh_ot_bevel");

  return keymap;
}

static int edbm_bev_modal(Cx *C, wmOp *op, const wmEvent *event)
{
  BevData *opdata = op->customdata;
  const bool has_numinput = hasNumInput(&opdata->num_input[opdata->value_mode]);
  bool handled = false;
  short etype = event->type;
  short eval = event->val;

  /* When activated from toolbar, need to convert leftmouse release to confirm */
  if (ELEM(etype, LEFTMOUSE, opdata->launch_event) && (eval == KM_RELEASE) &&
      api_bool_get(op->ptr, "release_confirm")) {
    etype = EVT_MODAL_MAP;
    eval = BEV_MODAL_CONFIRM;
  }
  /* Modal numinput active, try to handle numeric inputs first... */
  if (etype != EVT_MODAL_MAP && eval == KM_PRESS && has_numinput &&
      handleNumInput(C, &opdata->num_input[opdata->value_mode], event)) {
    edbm_bevel_numinput_set_value(op);
    edbm_bevel_calc(op);
    edbm_bevel_update_status_text(C, op);
    return OP_RUNNING_MODAL;
  }
  if (etype == MOUSEMOVE) {
    if (!has_numinput) {
      edbm_bev_mouse_set_val(op, event);
      edbm_bev_calc(op);
      edbm_bev_update_status_txt(C, op);
      handled = true;
    }
  }
  else if (etype == MOUSEPAN) {
    float delta = 0.02f * (event->xy[1] - event->prev_xy[1]);
    if (opdata->segments >= 1 && opdata->segments + delta < 1) {
      opdata->segments = 1;
    }
    else {
      opdata->segments += delta;
    }
    api_int_set(op->ptr, "segments", (int)opdata->segments);
    edm_bev_calc(op);
    edm_bev_update_status_txt(C, op);
    handled = true;
  }
  else if (etype == EVT_MODAL_MAP) {
    switch (eval) {
      case BEV_MODAL_CANCEL:
        edbm_bev_cancel(C, op);
        ed_workspace_status_txt(C, NULL);
        return OP_CANCELLED;

      case BEV_MODAL_CONFIRM:
        edbm_bev_calc(op);
        edbm_bev_exit(C, op);
        ed_workspace_status_txt(C, NULL);
        return OP_FINISHED;

      case BEV_MODAL_SEGMENTS_UP:
        opdata->segments = opdata->segments + 1;
        api_int_set(op->ptr, "segments", (int)opdata->segments);
        edbm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;

      case BEV_MODAL_SEGMENTS_DOWN:
        opdata->segments = max_ff(opdata->segments - 1, 1);
        api_int_set(op->ptr, "segments", (int)opdata->segments);
        edbm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;

      case BEV_MODAL_OFFSET_MODE_CHANGE: {
        int type = api_enum_get(op->ptr, "offset_type");
        type++;
        if (type > BEVEL_AMT_PERCENT) {
          type = BEVEL_AMT_OFFSET;
        }
        if (opdata->val_mode == OFFSET_VAL && type == BEV_AMT_PERCENT) {
          opdata->val_mode = OFFSET_VALUE_PERCENT;
        }
        else if (opdata->val_mode == OFFSET_VAL_PERCENT && type != BEVEL_AMT_PERCENT) {
          opdata->val_mode = OFFSET_VALUE;
        }
        api_enum_set(op->ptr, "offset_type", type);
        if (opdata->initial_length[opdata->value_mode] == -1.0f) {
          edm_bev_calc_initial_length(op, event, true);
        }
      }
        /* Update offset accordingly to new offset_type. */
        if (!has_numinput && (ELEM(opdata->value_mode, OFFSET_VAL, OFFSET_VALUE_PERCENT))) {
          edm_bev_mouse_set_val(op, event);
        }
        edm_bev_calc(op);
        edm_bev_update_status_txt(C, op);
        handled = true;
        break;

      case BEV_MODAL_CLAMP_OVERLAP_TOGGLE: {
        bool clamp_overlap = api_bool_get(op->ptr, "clamp_overlap");
        api_bool_set(op->ptr, "clamp_overlap", !clamp_overlap);
        edm_bevel_calc(op);
        edm_bevel_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_VAL_OFFSET:
        opdata->val_mode = OFFSET_VAL;
        edm_bevel_calc_initial_length(op, event, true);
        break;

      case BEV_MODAL_VAL_PROFILE:
        opdata->val_mode = PROFILE_VAL;
        edm_bev_calc_initial_length(op, event, true);
        break;

      case BEV_MODAL_VAL_SEGMENTS:
        opdata->val_mode = SEGMENTS_VAL;
        edm_bev_calc_initial_length(op, event, true);
        break;

      case BEV_MODAL_AFFECT_CHANGE: {
        int affect_type = api_enum_get(op->ptr, "affect");
        affect_type++;
        if (affect_type > BEV_AFFECT_EDGES) {
          affect_type = BEV_AFFECT_VERTICES;
        }
        api_enum_set(op->ptr, "affect", affect_type);
        edm_bev_calc(op);
        edm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_MARK_SEAM_TOGGLE: {
        bool mark_seam = api_bool_get(op->ptr, "mark_seam");
        api_bool_set(op->ptr, "mark_seam", !mark_seam);
        edbm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_MARK_SHARP_TOGGLE: {
        bool mark_sharp = api_bool_get(op->ptr, "mark_sharp");
        api_bool_set(op->ptr, "mark_sharp", !mark_sharp);
        edm_bev_calc(op);
        edm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_INNER_MITER_CHANGE: {
        int miter_inner = api_enum_get(op->ptr, "miter_inner");
        miter_inner++;
        if (miter_inner == BEV_MITER_PATCH) {
          miter_inner++; /* no patch option for inner miter */
        }
        if (miter_inner > BEV_MITER_ARC) {
          miter_inner = BEV_MITER_SHARP;
        }
        api_enum_set(op->ptr, "miter_inner", miter_inner);
        edm_bev_calc(op);
        edm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_OUTER_MITER_CHANGE: {
        int miter_outer = api_enum_get(op->ptr, "miter_outer");
        miter_outer++;
        if (miter_outer > BEV_MITER_ARC) {
          miter_outer = BEV_MITER_SHARP;
        }
        api_enum_set(op->ptr, "miter_outer", miter_outer);
        edm_bev_calc(op);
        edm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_HARDEN_NORMALS_TOGGLE: {
        bool harden_normals = api_bool_get(op->ptr, "harden_normals");
        api_bool_set(op->ptr, "harden_normals", !harden_normals);
        edm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_PROFILE_TYPE_CHANGE: {
        int profile_type = api_enum_get(op->ptr, "profile_type");
        profile_type++;
        if (profile_type > BEV_PROFILE_CUSTOM) {
          profile_type = BEV_PROFILE_SUPERELLIPSE;
        }
        api_enum_set(op->ptr, "profile_type", profile_type);
        edbm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }

      case BEV_MODAL_VERT_MESH_CHANGE: {
        int vmesh_method = api_enum_get(op->ptr, "vmesh_method");
        vmesh_method++;
        if (vmesh_method > BEV_VMESH_CUTOFF) {
          vmesh_method = BEV_VMESH_ADJ;
        }
        api_enum_set(op->ptr, "vmesh_method", vmesh_method);
        edbm_bev_calc(op);
        edbm_bev_update_status_txt(C, op);
        handled = true;
        break;
      }
    }
  }

  /* Modal numinput inactive, try to handle numeric inputs last... */
  if (!handled && eval == KM_PRESS &&
      handleNumInput(C, &opdata->num_input[opdata->val_mode], event)) {
    edbm_bev_numinput_set_val(op);
    edbm_bev_calc(op);
    edbm_bev_update_status_txt(C, op);
    return OP_RUNNING_MODAL;
  }

  return OPERATOR_RUNNING_MODAL;
}

static void edbm_bev_ui(Cx *C, wmOp *op)
{
  uiLayout *layout = op->layout;
  uiLayout *col, *row;
  ApiPtr toolsettings_ptr;

  int profile_type = api_enum_get(op->ptr, "profile_type");
  int offset_type = api_enum_get(op->ptr, "offset_type");
  bool affect_type = api_enum_get(op->ptr, "affect");

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  row = uiLayoutRow(layout, false);
  uiItemR(row, op->ptr, "affect", UI_ITEM_R_EXPAND, NULL, ICON_NONE);

  uiItemS(layout);

  uiItemR(layout, op->ptr, "offset_type", 0, NULL, ICON_NONE);

  if (offset_type == BEV_AMT_PERCENT) {
    uiItemR(layout, op->ptr, "offset_pct", 0, NULL, ICON_NONE);
  }
  else {
    uiItemR(layout, op->ptr, "offset", 0, NULL, ICON_NONE);
  }

  uiItemR(layout, op->ptr, "segments", 0, NULL, ICON_NONE);
  if (ELEM(profile_type, BEV_PROFILE_SUPERELLIPSE, BEV_PROFILE_CUSTOM)) {
    uiItemR(layout,
            op->ptr,
            "profile",
            UI_ITEM_R_SLIDER,
            (profile_type == BEVEL_PROFILE_SUPERELLIPSE) ? IFACE_("Shape") : IFACE_("Miter Shape"),
            ICON_NONE);
  }
  uiItemR(layout, op->ptr, "material", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, op->ptr, "harden_normals", 0, NULL, ICON_NONE);
  uiItemR(col, op->ptr, "clamp_overlap", 0, NULL, ICON_NONE);
  uiItemR(col, op->ptr, "loop_slide", 0, NULL, ICON_NONE);

  col = uiLayoutColumnWithHeading(layout, true, IFACE_("Mark"));
  uiLayoutSetActive(col, affect_type == BEV_AFFECT_EDGES);
  uiItemR(col, op->ptr, "mark_seam", 0, IFACE_("Seams"), ICON_NONE);
  uiItemR(col, op->ptr, "mark_sharp", 0, IFACE_("Sharp"), ICON_NONE);

  uiItemS(layout);

  col = uiLayoutColumn(layout, false);
  uiLayoutSetActive(col, affect_type == BEV_AFFECT_EDGES);
  uiItemR(col, op->ptr, "miter_outer", 0, IFACE_("Miter Outer"), ICON_NONE);
  uiItemR(col, op->ptr, "miter_inner", 0, IFACE_("Inner"), ICON_NONE);
  if (api_enum_get(op->ptr, "miter_inner") == BEV_MITER_ARC) {
    uiItemR(col, op->ptr, "spread", 0, NULL, ICON_NONE);
  }

  uiItemS(layout);

  col = uiLayoutColumn(layout, false);
  uiLayoutSetActive(col, affect_type == BEV_AFFECT_EDGES);
  uiItemR(col, op->ptr, "vmesh_method", 0, IFACE_("Intersection Type"), ICON_NONE);

  uiItemR(layout, op->ptr, "face_strength_mode", 0, IFACE_("Face Strength"), ICON_NONE);

  uiItemS(layout);

  row = uiLayoutRow(layout, false);
  uiItemR(row, op->ptr, "profile_type", UI_ITEM_R_EXPAND, NULL, ICON_NONE);
  if (profile_type == BEV_PROFILE_CUSTOM) {
    /* Get an RNA pointer to ToolSettings to give to the curve profile template code. */
    Scene *scene = cx_data_scene(C);
    api_ptr_create(&scene->id, &Api_ToolSettings, scene->toolsettings, &toolsettings_ptr);
    uiTemplateCurveProfile(layout, &toolsettings_ptr, "custom_bev_profile_preset");
  }
}

void mesh_ot_bev(wmOpType *ot)
{
  ApiProp *prop;

  static const EnumPropItem offset_type_items[] = {
      {BEV_AMT_OFFSET, "OFFSET", 0, "Offset", "Amount is offset of new edges from original"},
      {BEV_AMT_WIDTH, "WIDTH", 0, "Width", "Amount is width of new face"},
      {BEV_AMT_DEPTH,
       "DEPTH",
       0,
       "Depth",
       "Amount is perpendicular distance from original edge to bevel face"},
      {BEV_AMT_PERCENT, "PERCENT", 0, "Percent", "Amount is percent of adjacent edge length"},
      {BEV_AMT_ABSOLUTE,
       "ABSOLUTE",
       0,
       "Absolute",
       "Amount is absolute distance along adjacent edge"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_profile_type_items[] = {
      {BEV_PROFILE_SUPERELLIPSE,
       "SUPERELLIPSE",
       0,
       "Superellipse",
       "The profile can be a concave or convex curve"},
      {BEV_PROFILE_CUSTOM,
       "CUSTOM",
       0,
       "Custom",
       "The profile can be any arbitrary path between its endpoints"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem face_strength_mode_items[] = {
      {BEV_FACE_STRENGTH_NONE, "NONE", 0, "None", "Do not set face strength"},
      {BEV_FACE_STRENGTH_NEW, "NEW", 0, "New", "Set face strength on new faces only"},
      {BEV_FACE_STRENGTH_AFFECTED,
       "AFFECTED",
       0,
       "Affected",
       "Set face strength on new and modified faces only"},
      {BEV_FACE_STRENGTH_ALL, "ALL", 0, "All", "Set face strength on all faces"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem miter_outer_items[] = {
      {BEV_MITER_SHARP, "SHARP", 0, "Sharp", "Outside of miter is sharp"},
      {BEV_MITER_PATCH, "PATCH", 0, "Patch", "Outside of miter is squared-off patch"},
      {BEV_MITER_ARC, "ARC", 0, "Arc", "Outside of miter is arc"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem miter_inner_items[] = {
      {BEV_MITER_SHARP, "SHARP", 0, "Sharp", "Inside of miter is sharp"},
      {BEV_MITER_ARC, "ARC", 0, "Arc", "Inside of miter is arc"},
      {0, NULL, 0, NULL, NULL},
  };

  static EnumPropItem vmesh_method_items[] = {
      {BEV_VMESH_ADJ, "ADJ", 0, "Grid Fill", "Default patterned fill"},
      {BEV_VMESH_CUTOFF,
       "CUTOFF",
       0,
       "Cutoff",
       "A cutoff at each profile's end before the intersection"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_affect_items[] = {
      {BEV_AFFECT_VERTS, "VERTS", 0, "Verts", "Affect only verts"},
      {BEV_AFFECT_EDGES, "EDGES", 0, "Edges", "Affect only edges"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Bevel";
  ot->description = "Cut into selected items at an angle to create bevel or chamfer";
  ot->idname = "mesh_ot_bev";

  /* api callbacks */
  ot->exec = edbm_bev_exec;
  ot->invoke = edbm_bev_invoke;
  ot->modal = edbm_bev_modal;
  ot->cancel = edbm_bev_cancel;
  ot->poll = ed_op_editmesh;
  ot->ui = edbm_bev_ui;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_GRAB_CURSOR_XY | OPTYPE_BLOCKING;

  /* props */
  api_def_enum(ot->sapi,
               "offset_type",
               offset_type_items,
               0,
               "Width Type",
               "The method for determining the size of the bevel");
  prop = api_def_prop(ot->sapi, "offset", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 0.0, 1e6);
  api_def_prop_ui_range(prop, 0.0, 100.0, 1, 3);
  api_def_prop_ui_txt(prop, "Width", "Bevel amount");

  RNA_def_enum(ot->srna,
               "profile_type",
               prop_profile_type_items,
               0,
               "Profile Type",
               "The type of shape used to rebuild a beveled section");

  prop = RNA_def_property(ot->srna, "offset_pct", PROP_FLOAT, PROP_PERCENTAGE);
  RNA_def_property_range(prop, 0.0, 100);
  RNA_def_property_ui_text(prop, "Width Percent", "Bevel amount for percentage method");

  RNA_def_int(ot->srna,
              "segments",
              1,
              1,
              SEGMENTS_HARD_MAX,
              "Segments",
              "Segments for curved edge",
              1,
              100);

  RNA_def_float(ot->srna,
                "profile",
                0.5f,
                PROFILE_HARD_MIN,
                1.0f,
                "Profile",
                "Controls profile shape (0.5 = round)",
                PROFILE_HARD_MIN,
                1.0f);

  RNA_def_enum(ot->srna,
               "affect",
               prop_affect_items,
               BEVEL_AFFECT_EDGES,
               "Affect",
               "Affect edges or vertices");

  RNA_def_boolean(ot->srna,
                  "clamp_overlap",
                  false,
                  "Clamp Overlap",
                  "Do not allow beveled edges/vertices to overlap each other");

  RNA_def_boolean(
      ot->srna, "loop_slide", true, "Loop Slide", "Prefer sliding along edges to even widths");

  RNA_def_boolean(ot->srna, "mark_seam", false, "Mark Seams", "Mark Seams along beveled edges");

  RNA_def_boolean(ot->srna, "mark_sharp", false, "Mark Sharp", "Mark beveled edges as sharp");

  RNA_def_int(ot->srna,
              "material",
              -1,
              -1,
              INT_MAX,
              "Material Index",
              "Material for bevel faces (-1 means use adjacent faces)",
              -1,
              100);

  RNA_def_boolean(ot->srna,
                  "harden_normals",
                  false,
                  "Harden Normals",
                  "Match normals of new faces to adjacent faces");

  api_def_enum(ot->sapi,
               "face_strength_mode",
               face_strength_mode_items,
               BEVEL_FACE_STRENGTH_NONE,
               "Face Strength Mode",
               "Whether to set face strength, and which faces to set face strength on");

  api_def_enum(ot->sapi,
               "miter_outer",
               miter_outer_items,
               BEVEL_MITER_SHARP,
               "Outer Miter",
               "Pattern to use for outside of miters");

  api_def_enum(ot->sapi,
               "miter_inner",
               miter_inner_items,
               BEVEL_MITER_SHARP,
               "Inner Miter",
               "Pattern to use for inside of miters");

  api_def_float(ot->sapi,
                "spread",
                0.1f,
                0.0f,
                1e6f,
                "Spread",
                "Amount to spread arcs for arc inner miters",
                0.0f,
                100.0f);

  api_def_enum(ot->sapi,
               "vmesh_method",
               vmesh_method_items,
               BEVEL_VMESH_ADJ,
               "Vertex Mesh Method",
               "The method to use to create meshes at intersections");

  prop = api_def_bool(ot->sapi, "release_confirm", 0, "Confirm on Release", "");
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}
