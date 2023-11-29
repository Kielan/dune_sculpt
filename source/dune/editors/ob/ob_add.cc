#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "mem_guardedalloc.h"

#include "types_anim.h"
#include "types_camera.h"
#include "types_collection.h"
#include "types_curve.h"
#include "types_pen_legacy.h"
#include "types_pen_mod.h"
#include "types_key.h"
#include "types_light.h"
#include "types_lightprobe.h"
#include "types_material.h"
#include "types_mesh.h"
#include "types_meta.h"
#include "types_mod.h"
#include "types_ob_fluidsim.h"
#include "types_ob_force.h"
#include "types_ob.h"
#include "types_pointcloud.h"
#include "types_scene.h"
#include "types_vfont.h"

#include "lib_ghash.h"
#include "lib_list.h"
#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_string.h"
#include "lib_string_utf8.h"
#include "lib_utildefines.h"
#include "lib_vector.hh"

#include "lang.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_armature.hh"
#include "dune_camera.h"
#include "dune_collection.h"
#include "dune_constraint.h"
#include "dune_cxt.hh"
#include "dune_curve.hh"
#include "dune_curve_to_mesh.hh"
#include "dune_curves.h"
#include "dune_displist.h"
#include "dune_duplilist.h"
#include "dune_effect.h"
#include "dune_geometry_set.hh"
#include "dune_geometry_set_instances.hh"
#include "dune_pen_curve_legacy.h"
#include "dune_pen_geom_legacy.h"
#include "dune_pen_legacy.h"
#include "dune_pen_mod_legacy.h"
#include "dune_pen.hh"
#include "dune_key.h"
#include "dune_lattice.hh"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_lib_override.hh"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_light.h"
#include "dune_lightprobe.h"
#include "dune_main.h"
#include "dune_material.h"
#include "dune_mball.h"
#include "dune_mesh.hh"
#include "dune_mesh_runtime.hh"
#include "dune_nla.h"
#include "dune_node.hh"
#include "dune_ob.hh"
#include "dune_ob_types.hh"
#include "dune_particle.h"
#include "dune_pointcloud.h"
#include "dune_report.h"
#include "dune_scene.h"
#include "dune_speaker.h"
#include "dune_vfont.h"
#include "dune_volume.hh"

#include "graph.hh"
#include "graph_build.hh"
#include "graph_query.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "ui.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_curve.hh"
#include "ed_curves.hh"
#include "ed_pen_legacy.hh"
#include "ed_pen.hh"
#include "ED_mball.hh"
#include "ed_mesh.hh"
#include "ed_node.hh"
#include "ed_ob.hh"
#include "ed_outliner.hh"
#include "ed_physics.hh"
#include "ed_render.hh"
#include "ed_screen.hh"
#include "ed_sel_utils.hh"
#include "ed_transform.hh"
#include "ed_view3d.hh"

#include "anim_bone_collections.h"

#include "ui_resources.hh"

#include "ob_intern.h"

using dune::float3;
using dune::float4x4;
using dune::Vector;

/* Local Enum Declarations */
/* This is an exact copy of the define in `rna_light.cc`
 * kept here because of linking order.
 * Icons are only defined here. */
const EnumPropItem api_enum_light_type_items[] = {
    {LA_LOCAL, "POINT", ICON_LIGHT_POINT, "Point", "Omnidirectional point light source"},
    {LA_SUN, "SUN", ICON_LIGHT_SUN, "Sun", "Constant direction parallel ray light source"},
    {LA_SPOT, "SPOT", ICON_LIGHT_SPOT, "Spot", "Directional cone light source"},
    {LA_AREA, "AREA", ICON_LIGHT_AREA, "Area", "Directional area light source"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* copy from api_ob_force.cc */
static const EnumPropItem field_type_items[] = {
    {PFIELD_FORCE, "FORCE", ICON_FORCE_FORCE, "Force", ""},
    {PFIELD_WIND, "WIND", ICON_FORCE_WIND, "Wind", ""},
    {PFIELD_VORTEX, "VORTEX", ICON_FORCE_VORTEX, "Vortex", ""},
    {PFIELD_MAGNET, "MAGNET", ICON_FORCE_MAGNETIC, "Magnetic", ""},
    {PFIELD_HARMONIC, "HARMONIC", ICON_FORCE_HARMONIC, "Harmonic", ""},
    {PFIELD_CHARGE, "CHARGE", ICON_FORCE_CHARGE, "Charge", ""},
    {PFIELD_LENNARDJ, "LENNARDJ", ICON_FORCE_LENNARDJONES, "Lennard-Jones", ""},
    {PFIELD_TEXTURE, "TEXTURE", ICON_FORCE_TEXTURE, "Texture", ""},
    {PFIELD_GUIDE, "GUIDE", ICON_FORCE_CURVE, "Curve Guide", ""},
    {PFIELD_BOID, "BOID", ICON_FORCE_BOID, "Boid", ""},
    {PFIELD_TURBULENCE, "TURBULENCE", ICON_FORCE_TURBULENCE, "Turbulence", ""},
    {PFIELD_DRAG, "DRAG", ICON_FORCE_DRAG, "Drag", ""},
    {PFIELD_FLUIDFLOW, "FLUID", ICON_FORCE_FLUIDFLOW, "Fluid Flow", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static EnumPropeItem lightprobe_type_items[] = {
    {LIGHTPROBE_TYPE_SPHERE,
     "SPHERE",
     ICON_LIGHTPROBE_SPHERE,
     "Sphere",
     "Light probe that captures precise lighting from all directions at a single point in space"},
    {LIGHTPROBE_TYPE_PLANE,
     "PLANE",
     ICON_LIGHTPROBE_PLANE,
     "Plane",
     "Light probe that captures incoming light from a single direction on a plane"},
    {LIGHTPROBE_TYPE_VOLUME,
     "VOLUME",
     ICON_LIGHTPROBE_VOLUME,
     "Volume",
     "Light probe that captures low frequency lighting inside a volume"},
    {0, nullptr, 0, nullptr, nullptr},
};

enum {
  ALIGN_WORLD = 0,
  ALIGN_VIEW,
  ALIGN_CURSOR,
};

static const EnumPropItem align_options[] = {
    {ALIGN_WORLD, "WORLD", 0, "World", "Align the new ob to the world"},
    {ALIGN_VIEW, "VIEW", 0, "View", "Align the new ob to the view"},
    {ALIGN_CURSOR, "CURSOR", 0, "3D Cursor", "Use the 3D cursor orientation for the new object"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* Local Helpers */
/* Op props for creating an object under a screen space (2D) coordinate.
 * Used for ob dropping like behavior (drag object and drop into 3D View). */
static void ob_add_drop_xy_props(WinOpType *ot)
{
  ApiProp *prop;

  prop = api_def_int(ot->sapi,
                     "drop_x",
                     0,
                     INT_MIN,
                     INT_MAX,
                     "Drop X",
                     "X-coordinate (screen space) to place the new ob under",
                     INT_MIN,
                     INT_MAX);
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
  prop = api_def_int(ot->sapi,
                     "drop_y",
                     0,
                     INT_MIN,
                     INT_MAX,
                     "Drop Y",
                     "Y-coordinate (screen space) to place the new object under",
                     INT_MIN,
                     INT_MAX);
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
}

static bool ob_add_drop_xy_is_set(const WinOp *op)
{
  return api_struct_prop_is_set(op->ptr, "drop_x") &&
         api_struct_prop_is_set(op->ptr, "drop_y");
}

/* Query the currently set X- and Y-coordinate to position the new object under.
 * param r_mval: Returned pointer to the coordinate in rgn-space. */
static bool ob_add_drop_xy_get(Cxt *C, WinOp *op, int (*r_mval)[2])
{
  if (!ob_add_drop_xy_is_set(op)) {
    (*r_mval)[0] = 0.0f;
    (*r_mval)[1] = 0.0f;
    return false;
  }

  const ARgn *rgnn = cxt_win_rgn(C);
  (*r_mval)[0] = api_int_get(op->ptr, "drop_x") - rgn->winrct.xmin;
  (*r_mval)[1] = api_int_get(op->ptr, "drop_y") - rgn->winrct.ymin;

  return true;
}

/* Set the drop coordinate to the mouse position (if not alrdy set) and call the op's
 * `exec()` cb. */
static int ob_add_drop_xy_generic_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  if (!ob_add_drop_xy_is_set(op)) {
    api_int_set(op->ptr, "drop_x", ev->xy[0]);
    api_int_set(op->ptr, "drop_y", ev->xy[1]);
  }
  return op->type->ex(C, op);
}

/* Public Add Ob AP */
void ed_ob_location_from_view(Cxt *C, float loc[3])
{
  const Scene *scene = cxt_data_scene(C);
  copy_v3_v3(loc, scene->cursor.location);
}

void ed_ob_rotation_from_quat(float rot[3], const float viewquat[4], const char align_axis)
{
  lib_assert(align_axis >= 'X' && align_axis <= 'Z');

  switch (align_axis) {
    case 'X': {
      /* Same as 'rv3d->viewinv[1]' */
      const float axis_y[4] = {0.0f, 1.0f, 0.0f};
      float quat_y[4], quat[4];
      axis_angle_to_quat(quat_y, axis_y, M_PI_2);
      mul_qt_qtqt(quat, viewquat, quat_y);
      quat_to_eul(rot, quat);
      break;
    }
    case 'Y': {
      quat_to_eul(rot, viewquat);
      rot[0] -= float(M_PI_2);
      break;
    }
    case 'Z': {
      quat_to_eul(rot, viewquat);
      break;
    }
  }
}

void ed_ob_rotation_from_view(Cxt *C, float rot[3], const char align_axis)
{
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  lib_assert(align_axis >= 'X' && align_axis <= 'Z');
  if (rv3d) {
    float viewquat[4];
    copy_qt_qt(viewquat, rv3d->viewquat);
    viewquat[0] *= -1.0f;
    ed_ob_rotation_from_quat(rot, viewquat, align_axis);
  }
  else {
    zero_v3(rot);
  }
}

void ed_ob_base_init_transform_on_add(Ob *ob, const float loc[3], const float rot[3])
{
  if (loc) {
    copy_v3_v3(ob->loc, loc);
  }

  if (rot) {
    copy_v3_v3(ob->rot, rot);
  }

  dune_ob_to_mat4(ob, ob->ob_to_world);
}

float ed_ob_new_primitive_matrix(Cxt *C,
                                 Ob *obedit,
                                 const float loc[3],
                                 const float rot[3],
                                 const float scale[3],
                                 float r_primmat[4][4])
{
  Scene *scene = cxt_data_scene(C);
  View3D *v3d = cxt_win_view3d(C);
  float mat[3][3], rmat[3][3], cmat[3][3], imat[3][3];

  unit_m4(r_primmat);

  eul_to_mat3(rmat, rot);
  invert_m3(rmat);

  /* inverse transform for initial rotation and object */
  copy_m3_m4(mat, obedit->ob_to_world);
  mul_m3_m3m3(cmat, rmat, mat);
  invert_m3_m3(imat, cmat);
  copy_m4_m3(r_primmat, imat);

  /* center */
  copy_v3_v3(r_primmat[3], loc);
  sub_v3_v3v3(r_primmat[3], r_primmat[3], obedit->ob_to_world[3]);
  invert_m3_m3(imat, mat);
  mul_m3_v3(imat, r_primmat[3]);

  if (scale != nullptr) {
    rescale_m4(r_primmat, scale);
  }

  {
    const float dia = v3d ? ed_view3d_grid_scale(scene, v3d, nullptr) :
                            ed_scene_grid_scale(scene, nullptr);
    return dia;
  }

  // return 1.0f;
}

/* Add Ob Op */
static void view_align_update(Main * /*main*/, Scene * /*scene*/, ApiPtr *ptr)
{
  api_struct_idprops_unset(ptr, "rotation");
}

void ed_ob_add_unit_props_size(WinOpType *ot)
{
  api_def_float_distance(
      ot->sapi, "size", 2.0f, 0.0, OB_ADD_SIZE_MAXF, "Size", "", 0.001, 100.00);
}

void ed_ob_add_unit_props_radius_ex(WinOpType *ot, float default_val)
{
  api_def_float_distance(
      ot->sapi, "radius", default_val, 0.0, OB_ADD_SIZE_MAXF, "Radius", "", 0.001, 100.00);
}

void ed_ob_add_unit_props_radius(WinOpType *ot)
{
  ed_ob_add_unit_props_radius_ex(ot, 1.0f);
}

void ed_ob_add_generic_props(WinOpType *ot, bool do_editmode)
{
  ApiProp *prop;

  if (do_editmode) {
    prop = api_def_bool(ot->sapi,
                        "enter_editmode",
                        false,
                        "Enter Edit Mode",
                        "Enter edit mode when adding this ob");
    api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
  }
  /* NOTE: this prop gets hidden for add-camera ob. */
  prop = api_def_enum(
      ot->sapi, "align", align_options, ALIGN_WORLD, "Align", "The alignment of the new object");
  api_def_prop_update_runtime(prop, view_align_update);

  prop = api_def_float_vector_xyz(ot->sapi,
                                  "location",
                                  3,
                                  nullptr,
                                  -OB_ADD_SIZE_MAXF,
                                  OB_ADD_SIZE_MAXF,
                                  "Location",
                                  "Location for the newly added object",
                                  -1000.0f,
                                  1000.0f);
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_float_rotation(ot->sapi,
                                "rotation",
                                3,
                                nullptr,
                                -OB_ADD_SIZE_MAXF,
                                OB_ADD_SIZE_MAXF,
                                "Rotation",
                                "Rotation for the newly added object",
                                DEG2RADF(-360.0f),
                                DEG2RADF(360.0f));
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_float_vector_xyz(ot->sapi,
                                  "scale",
                                  3,
                                  nullptr,
                                  -OB_ADD_SIZE_MAXF,
                                  OB_ADD_SIZE_MAXF,
                                  "Scale",
                                  "Scale for the newly added object",
                                  -1000.0f,
                                  1000.0f);
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
}

void ed_ob_add_mesh_props(WinOpType *ot)
{
  api_def_bool(ot->sapi, "calc_uvs", true, "Generate UVs", "Generate a default UV map");
}

bool ed_ob_add_generic_get_opts(Cxt *C,
                                WinOp *op,
                                const char view_align_axis,
                                float r_loc[3],
                                float r_rot[3],
                                float r_scale[3],
                                bool *r_enter_editmode,
                                ushort *r_local_view_bits,
                                bool *r_is_view_aligned)
{
  /* Edit Mode! (optional) */
  {
    bool _enter_editmode;
    if (!r_enter_editmode) {
      r_enter_editmode = &_enter_editmode;
    }
    /* Only to ensure the value is _always_ set.
     * Typically the prop will exist when the argument is non-nullptr. */
    *r_enter_editmode = false;

    ApiProp *prop = api_struct_find_prop(op->ptr, "enter_editmode");
    if (prop != nullptr) {
      if (api_prop_is_set(op->ptr, prop) && r_enter_editmode) {
        *r_enter_editmode = api_prop_bool_get(op->ptr, prop);
      }
      else {
        *r_enter_editmode = (U.flag & USER_ADD_EDITMODE) != 0;
        api_prop_bool_set(op->ptr, prop, *r_enter_editmode);
      }
    }
  }

  if (r_local_view_bits) {
    View3D *v3d = cxt_win_view3d(C);
    *r_local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
  }

  /* Location! */
  {
    float _loc[3];
    if (!r_loc) {
      r_loc = _loc;
    }

    if (api_struct_prop_is_set(op->ptr, "location")) {
      api_float_get_array(op->ptr, "location", r_loc);
    }
    else {
      ed_ob_location_from_view(C, r_loc);
      api_float_set_array(op->ptr, "location", r_loc);
    }
  }

  /* Rotation! */
  {
    bool _is_view_aligned;
    float _rot[3];
    if (!r_is_view_aligned) {
      r_is_view_aligned = &_is_view_aligned;
    }
    if (!r_rot) {
      r_rot = _rot;
    }

    if (api_struct_prop_is_set(op->ptr, "rotation")) {
      /* If rotation is set, always use it. Alignment (and corresponding user pref)
       * can be ignored since this is in world space anyways.
       * To not confuse (e.g. on redo), don't set it to ALIGN_WORLD in the op UI though. */
      *r_is_view_aligned = false;
      api_float_get_array(op->ptr, "rotation", r_rot);
    }
    else {
      int alignment = ALIGN_WORLD;
      ApiProp *prop = api_struct_find_prop(op->ptr, "align");

      if (api_prop_is_set(op->ptr, prop)) {
        /* If alignment is set, always use it. */
        *r_is_view_aligned = alignment == ALIGN_VIEW;
        alignment = api_prop_enum_get(op->ptr, prop);
      }
      else {
        /* If alignment is not set, use User Prefs. */
        *r_is_view_aligned = (U.flag & USER_ADD_VIEWALIGNED) != 0;
        if (*r_is_view_aligned) {
          api_prop_enum_set(op->ptr, prop, ALIGN_VIEW);
          alignment = ALIGN_VIEW;
        }
        else if ((U.flag & USER_ADD_CURSORALIGNED) != 0) {
          api_prop_enum_set(op->ptr, prop, ALIGN_CURSOR);
          alignment = ALIGN_CURSOR;
        }
        else {
          api_prop_enum_set(op->ptr, prop, ALIGN_WORLD);
          alignment = ALIGN_WORLD;
        }
      }
      switch (alignment) {
        case ALIGN_WORLD:
          api_float_get_array(op->ptr, "rotation", r_rot);
          break;
        case ALIGN_VIEW:
          ed_ob_rotation_from_view(C, r_rot, view_align_axis);
          qpi_float_set_array(op->ptr, "rotation", r_rot);
          break;
        case ALIGN_CURSOR: {
          const Scene *scene = cxt_data_scene(C);
          float tmat[3][3];
          dune_scene_cursor_rot_to_mat3(&scene->cursor, tmat);
          mat3_normalized_to_eul(r_rot, tmat);
          api_float_set_array(op->ptr, "rotation", r_rot);
          break;
        }
      }
    }
  }

  /* Scale! */
  {
    float _scale[3];
    if (!r_scale) {
      r_scale = _scale;
    }

    /* For now this is optional, we can make it always use. */
    copy_v3_fl(r_scale, 1.0f);

    ApiProp *prop = api_struct_find_prop(op->ptr, "scale");
    if (prop != nullptr) {
      if (api_prop_is_set(op->ptr, prop)) {
        api_prop_float_get_array(op->ptr, prop, r_scale);
      }
      else {
        copy_v3_fl(r_scale, 1.0f);
        api_prop_float_set_array(op->ptr, prop, r_scale);
      }
    }
  }

  return true;
}

Ob *ed_ob_add_type_with_obdata(Cxt *C,
                               const int type,
                               const char *name,
                               const float loc[3],
                               const float rot[3],
                               const bool enter_editmode,
                               const ushort local_view_bits,
                               Id *obdata)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  {
    dune_view_layer_synced_ensure(scene, view_layer);
    Ob *obedit = dune_view_layer_edit_ob_get(view_layer);
    if (obedit != nullptr) {
      ed_ob_editmode_exit_ex(main, scene, obedit, EM_FREEDATA);
    }
  }

  /* desel all, sets active ob */
  Ob *ob;
  if (obdata != nullptr) {
    lib_assert(type == dune_ob_obdata_to_type(obdata));
    ob = dune_ob_add_for_data(main, scene, view_layer, type, name, obdata, true);
    const short *materials_len_p = dune_id_material_len_p(obdata);
    if (materials_len_p && *materials_len_p > 0) {
      dune_ob_materials_test(main, ob, static_cast<Id *>(ob->data));
    }
  }
  else {
    ob = dune_ob_add(main, scene, view_layer, type, name);
  }

  dune_view_layer_synced_ensure(scene, view_layer);
  Base *ob_base_act = dune_view_layer_active_base_get(view_layer);
  /* While not getting a valid base is not a good thing, it can happen in convoluted corner cases,
   * better not crash on it in releases. */
  lib_assert(ob_base_act != nullptr);
  if (ob_base_act != nullptr) {
    ob_base_act->local_view_bits = local_view_bits;
    /* editor level activate, notifiers */
    ed_ob_base_activate(C, ob_base_act);
  }

  /* more editor stuff */
  ed_ob_base_init_transform_on_add(ob, loc, rot);

  /* TODO: Strange to manually tag obs for update, better to
   * use graph_id_tag_update here perhaps. */
  graph_id_type_tag(main, ID_OB);
  graph_relations_tag_update(main);
  if (ob->data != nullptr) {
    graph_id_tag_update_ex(main, (Id *)ob->data, ID_RECALC_EDITORS);
  }

  if (enter_editmode) {
    ed_ob_editmode_enter_ex(main, scene, ob, 0);
  }

  win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);

  ed_outliner_sel_sync_from_ob_tag(C);

  return ob;
}

Ob *ed_ob_add_type(Cxt *C,
                   const int type,
                   const char *name,
                   const float loc[3],
                   const float rot[3],
                   const bool enter_editmode,
                   const ushort local_view_bits)
{
  return ed_ob_add_type_with_obdata(
      C, type, name, loc, rot, enter_editmode, local_view_bits, nullptr);
}

/* for ob add op */
static int ob_add_ex(Cxt *C, WinOp *op)
{
  ushort local_view_bits;
  bool enter_editmode;
  float loc[3], rot[3], radius;
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  radius = api_float_get(op->ptr, "radius");
  Ob *ob = ed_ob_add_type(
      C, api_enum_get(op->ptr, "type"), nullptr, loc, rot, enter_editmode, local_view_bits);

  if (ob->type == OB_LATTICE) {
    /* lattice is a special case!
     * we never want to scale the obdata since that is the rest-state */
    copy_v3_fl(ob->scale, radius);
  }
  else {
    dune_ob_obdata_size_init(ob, radius);
  }

  return OP_FINISHED;
}

void OB_OT_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Ob";
  ot->description = "Add an ob to the scene";
  ot->idname = "OB_OT_add";

  /* api cb */
  ot->ex = ob_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ed_ob_add_unit_props_radius(ot);
  ApiProp *prop = api_def_enum(ot->sapi, "type", api_enum_ob_type_items, 0, "Type", "");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_ID);

  ed_ob_add_generic_props(ot, true);
}

/* Add Probe Op */
/* for ob add op */
static const char *get_lightprobe_defname(int type)
{
  switch (type) {
    case LIGHTPROBE_TYPE_VOLUME:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Volume");
    case LIGHTPROBE_TYPE_PLANE:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Plane");
    case LIGHTPROBE_TYPE_SPHERE:
      return CTX_DATA_(LANG_CXT_ID_LIGHT, "Sphere");
    default:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_LIGHT, "LightProbe");
  }
}

static int lightprobe_add_exec(bContext *C, wmOperator *op)
{
  bool enter_editmode;
  ushort local_view_bits;
  float loc[3], rot[3];
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  int type = api_enum_get(op->ptr, "type");
  float radius = api_float_get(op->ptr, "radius");

  Ob *ob = ed_ob_add_type(
      C, OB_LIGHTPROBE, get_lightprobe_defname(type), loc, rot, false, local_view_bits);
  copy_v3_fl(ob->scale, radius);

  LightProbe *probe = (LightProbe *)ob->data;

  dune_lightprobe_type_set(probe, type);

  return OP_FINISHED;
}

void OB_OT_lightprobe_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Light Probe";
  ot->description = "Add a light probe ob";
  ot->idname = "OB_OT_lightprobe_add";

  /* api cbs */
  ot->ex = lightprobe_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", lightprobe_type_items, 0, "Type", "");

  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, true);
}

/* Add Effector Op */
/* for ob add op */
static const char *get_effector_defname(ePFieldType type)
{
  switch (type) {
    case PFIELD_FORCE:
      return CXT_DATA_(LANG_CXT_ID_OB, "Force");
    case PFIELD_VORTEX:
      return CXT_DATA_(LANG_CXT_ID_OB, "Vortex");
    case PFIELD_MAGNET:
      return CXT_DATA_(LANG_CXT_ID_OB, "Magnet");
    case PFIELD_WIND:
      return CXT_DATA_(LANG_CXT_ID_OB, "Wind");
    case PFIELD_GUIDE:
      return CXT_DATA_(LANG_CXT_ID_OB, "CurveGuide");
    case PFIELD_TEXTURE:
      return CXT_DATA_(LANG_CXT_ID_OB, "TextureField");
    case PFIELD_HARMONIC:
      return CXT_DATA_(LANG_CXT_ID_OB, "Harmonic");
    case PFIELD_CHARGE:
      return CXT_DATA_(LANG_CXT_ID_OB, "Charge");
    case PFIELD_LENNARDJ:
      return CXT_DATA_(LANG_CXT_ID_OB, "Lennard-Jones");
    case PFIELD_BOID:
      return CXT_DATA_(LANG_CXT_ID_OB, "Boid");
    case PFIELD_TURBULENCE:
      return CXT_DATA_(LANG_CXT_ID_OB, "Turbulence");
    case PFIELD_DRAG:
      return CXT_DATA_(LANG_CXT_ID_OB, "Drag");
    case PFIELD_FLUIDFLOW:
      return CXT_DATA_(LANG_CXT_ID_OB, "FluidField");
    case PFIELD_NULL:
      return CXT_DATA_(LANG_CXT_ID_OB, "Field");
    case NUM_PFIELD_TYPES:
      break;
  }

  lib_assert(false);
  return CXT_DATA_(LANG_CXT_ID_OB, "Field");
}

static int effector_add_ex(Cxt *C, WinOp *op)
{
  bool enter_editmode;
  ushort local_view_bits;
  float loc[3], rot[3];
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  const ePFieldType type = static_cast<ePFieldType>(api_enum_get(op->ptr, "type"));
  float dia = api_float_get(op->ptr, "radius");

  Ob *ob;
  if (type == PFIELD_GUIDE) {
    Main *main = cxt_data_main(C);
    Scene *scene = cxt_data_scene(C);
    ob = ed_ob_add_type(
        C, OB_CURVES_LEGACY, get_effector_defname(type), loc, rot, false, local_view_bits);

    Curve *cu = static_cast<Curve *>(ob->data);
    cu->flag |= CU_PATH | CU_3D;
    ed_ob_editmode_enter_ex(main, scene, ob, 0);

    float mat[4][4];
    ed_ob_new_primitive_matrix(C, ob, loc, rot, nullptr, mat);
    mul_mat3_m4_fl(mat, dia);
    lib_addtail(&cu->editnurb->nurbs,
                ed_curve_add_nurbs_primitive(C, ob, mat, CU_NURBS | CU_PRIM_PATH, 1));
    if (!enter_editmode) {
      ed_ob_editmode_exit_ex(main, scene, ob, EM_FREEDATA);
    }
  }
  else {
    ob = ed_ob_add_type(
        C, OB_EMPTY, get_effector_defname(type), loc, rot, false, local_view_bits);
    dune_ob_obdata_size_init(ob, dia);
    if (ELEM(type, PFIELD_WIND, PFIELD_VORTEX)) {
      ob->empty_drwtype = OB_SINGLE_ARROW;
    }
  }

  ob->pd = dune_partdeflect_new(type);

  return OP_FINISHED;
}

void OB_OT_effector_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Effector";
  ot->description = "Add an empty ob with a physics effector to the scene";
  ot->idname = "OB_OT_effector_add";

  /* api cbs */
  ot->ex = effector_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", field_type_items, 0, "Type", "");

  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, true);
}

/* Add Camera Op */
static int ob_camera_add_ex(Cxt *C, WinOp *op)
{
  View3D *v3d = cxt_win_view3d(C);
  Scene *scene = cxt_data_scene(C);

  /* force view align for cameras */
  aou_enum_set(op->ptr, "align", ALIGN_VIEW);

  ushort local_view_bits;
  bool enter_editmode;
  float loc[3], rot[3];
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  Ob *ob = ed_ob_add_type(C, OB_CAMERA, nullptr, loc, rot, false, local_view_bits);

  if (v3d) {
    if (v3d->camera == nullptr) {
      v3d->camera = ob;
    }
    if (v3d->scenelock && scene->camera == nullptr) {
      scene->camera = ob;
    }
  }

  Camera *cam = static_cast<Camera *>(ob->data);
  cam->drwsize = v3d ? ed_view3d_grid_scale(scene, v3d, nullptr) :
                        ed_scene_grid_scale(scene, nullptr);

  return OP_FINISHED;
}

void OB_OT_camera_add(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Add Camera";
  ot->description = "Add a camera ob to the scene";
  ot->idname = "OB_OT_camera_add";

  /* api cbs */
  ot->ex = ob_camera_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_ob_add_generic_props(ot, true);

  /* hide this for cameras, default */
  prop = api_struct_type_find_prop(ot->sapi, "align");
  api_def_prop_flag(prop, PROP_HIDDEN);
}

/* Add Metaball Op */
static int ob_metaball_add_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  ushort local_view_bits;
  bool enter_editmode;
  float loc[3], rot[3];
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  bool newob = false;
  dune_view_layer_synced_ensure(scene, view_layer);
  Ob *obedit = dune_view_layer_edit_ob_get(view_layer);
  if (obedit == nullptr || obedit->type != OB_MBALL) {
    obedit = ed_ob_add_type(C, OB_MBALL, nullptr, loc, rot, true, local_view_bits);
    newob = true;
  }
  else {
    graph_id_tag_update(&obedit->id, ID_RECALC_GEOMETRY);
  }

  float mat[4][4];
  ed_ob_new_primitive_matrix(C, obedit, loc, rot, nullptr, mat);
  /* Halving here is done to account for constant values from #BKE_mball_element_add.
   * While the default radius of the resulting meta element is 2,
   * we want to pass in 1 so other values such as resolution are scaled by 1.0. */
  float dia = api_float_get(op->ptr, "radius") / 2;

  ed_mball_add_primitive(C, obedit, newob, mat, dia, api_enum_get(op->ptr, "type"));

  /* userdef */
  if (newob && !enter_editmode) {
    ed_ob_editmode_exit_ex(main, scene, obedit, EM_FREEDATA);
  }
  else {
    /* Only needed in edit-mode (ed_ob_add_type normally handles this). */
    win_ev_add_notifier(C, NC_OB | ND_DRW, obedit);
  }

  return OP_FINISHED;
}

void OB_OT_metaball_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Metaball";
  ot->description = "Add an metaball object to the scene";
  ot->idname = "OB_OT_metaball_add";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = ob_metaball_add_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = api_def_enum(ot->sapi, "type", api_enum_metaelem_type_items, 0, "Primitive", "");

  ed_ob_add_unit_props_radius_ex(ot, 2.0f);
  ed_ob_add_generic_props(ot, true);
}

/* Add Txt Op */
static int ob_add_txt_ex(Cxt *C, WinOp *op)
{
  Ob *obedit = cxt_data_edit_ob(C);
  bool enter_editmode;
  ushort local_view_bits;
  float loc[3], rot[3];

  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  if (obedit && obedit->type == OB_FONT) {
    return OP_CANCELLED;
  }

  obedit = ed_ob_add_type(C, OB_FONT, nullptr, loc, rot, enter_editmode, local_view_bits);
  dune_ob_obdata_size_init(obedit, api_float_get(op->ptr, "radius"));

  return OP_FINISHED;
}

void OB_OT_txt_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Txt";
  ot->description = "Add a txt ob to the scene";
  ot->idname = "OB_OT_txt_add";

  /* api cbs */
  ot->ex = ob_add_txt_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, true);
}

/* Add Armature Op */
static int ob_armature_add_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  dune_view_layer_synced_ensure(scene, view_layer);
  Ob *obedit = dune_view_layer_edit_ob_get(view_layer);

  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  bool newob = false;
  bool enter_editmode;
  ushort local_view_bits;
  float loc[3], rot[3], dia;
  bool view_aligned = rv3d && (U.flag & USER_ADD_VIEWALIGNED);

  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, &enter_editmode, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  if ((obedit == nullptr) || (obedit->type != OB_ARMATURE)) {
    obedit = ed_ob_add_type(C, OB_ARMATURE, nullptr, loc, rot, true, local_view_bits);
    ed_ob_editmode_enter_ex(main, scene, obedit, 0);
    newob = true;
  }
  else {
    graph_id_tag_update(&obedit->id, ID_RECALC_GEOMETRY);
  }

  if (obedit == nullptr) {
    dune_report(op->reports, RPT_ERROR, "Cannot create editmode armature");
    return OP_CANCELLED;
  }

  /* Give the Armature its default bone collection. */
  Armature *armature = static_cast<Armature *>(obedit->data);
  BoneCollection *default_bonecoll = anim_armature_bonecoll_new(armature, "");
  anim_armature_bonecoll_active_set(armature, default_bonecoll);

  dia = api_float_get(op->ptr, "radius");
  ed_armature_ebone_add_primitive(obedit, dia, view_aligned);

  /* userdef */
  if (newob && !enter_editmode) {
    ed_ob_editmode_exit_ex(main, scene, obedit, EM_FREEDATA);
  }

  return OP_FINISHED;
}

void OB_OT_armature_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Armature";
  ot->description = "Add an armature ob to the scene";
  ot->idname = "OB_OT_armature_add";

  /* api cbs */
  ot->ex = ob_armature_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* prop */
  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, true);
}

/* Add Empty Op */
static int ob_empty_add_ex(Cxt *C, WinOp *op)
{
  Ob *ob;
  int type = api_enum_get(op->ptr, "type");
  ushort local_view_bits;
  float loc[3], rot[3];

  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  ob = ed_ob_add_type(C, OB_EMPTY, nullptr, loc, rot, false, local_view_bits);

  dune_ob_empty_drw_type_set(ob, type);
  dune_ob_obdata_size_init(ob, api_float_get(op->ptr, "radius"));

  return OP_FINISHED;
}

void OB_OT_empty_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Empty";
  ot->description = "Add an empty object to the scene";
  ot->idname = "OB_OT_empty_add";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = ob_empty_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->srna, "type", api_enum_ob_empty_drwtype_items, 0, "Type", "");

  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, false);
}

static int empty_drop_named_img_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Scene *scene = cxt_data_scene(C);

  Img *img = nullptr;

  img = (Img *)win_op_drop_load_path(C, op, ID_IM);
  if (!img) {
    return OP_CANCELLED;
  }
  /* handled below */
  id_us_min(&img->id);

  Ob *ob = nullptr;
  Ob *ob_cursor = ed_view3d_give_ob_under_cursor(C, ev->mval);

  /* either change empty under cursor or create a new empty */
  if (ob_cursor && ob_cursor->type == OB_EMPTY) {
    win_ev_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
    graph_id_tag_update((ID *)ob_cursor, ID_RECALC_TRANSFORM);
    ob = ob_cursor;
  }
  else {
    /* add new empty */
    ushort local_view_bits;
    float rot[3];

    if (!ed_ob_add_generic_get_opts(
            C, op, 'Z', nullptr, rot, nullptr, nullptr, &local_view_bits, nullptr))
    {
      return OP_CANCELLED;
    }
    ob = ed_ob_add_type(C, OB_EMPTY, nullptr, nullptr, rot, false, local_view_bits);

    ed_ob_location_from_view(C, ob->loc);
    ed_view3d_cursor3d_position(C, ev->mval, false, ob->loc);
    ed_ob_rotation_from_view(C, ob->rot, 'Z');
    ob->empty_drwsize = 5.0f;
  }

  dune_ob_empty_drw_type_set(ob, OB_EMPTY_IMG);

  id_us_min(static_cast<Id *>(ob->data));
  ob->data = img;
  id_us_plus(static_cast<Id *>(ob->data));

  return OP_FINISHED;
}

void OB_OT_drop_named_img(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Add Empty Img/Drop Img to Empty";
  ot->description = "Add an empty img type to scene with data";
  ot->idname = "OB_OT_drop_named_img";

  /* api cb */
  ot->invoke = empty_drop_named_img_invoke;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  prop = api_def_string(ot->sapi, "filepath", nullptr, FILE_MAX, "Filepath", "Path to image file");
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
  api_def_bool(ot->sapi,
               "relative_path",
               true,
               "Relative Path",
               "Sel the file relative to the blend file");
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));

  win_op_props_id_lookup(ot, true);

  ed_ob_add_generic_props(ot, false);
}

/* Add Pen (legacy) Op */
static bool ob_pen_add_poll(Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  Ob *obact = cxt_data_active_object(C);

  if ((scene == nullptr) || ID_IS_LINKED(scene) || ID_IS_OVERRIDE_LIBRARY(scene)) {
    return false;
  }

  if (obact && obact->type == OB_PEN_LEGACY) {
    if (obact->mode != OB_MODE_OB) {
      return false;
    }
  }

  return true;
}

static int ob_pen_add_ex(Cxt *C, WinOp *op)
{
  Ob *ob = cxt_data_active_ob(C), *ob_orig = ob;
  PenData *pd = (ob && (ob->type == OB_PEN_LEGACY)) ? static_cast<PenData *>(ob->data) :
                                                    nullptr;

  const int type = api_enum_get(op->ptr, "type");
  const bool use_in_front = api_bool_get(op->ptr, "use_in_front");
  const bool use_lights = api_bool_get(op->ptr, "use_lights");
  const int stroke_depth_order = api_enum_get(op->ptr, "stroke_depth_order");
  const float stroke_depth_offset = api_float_get(op->ptr, "stroke_depth_offset");

  ushort local_view_bits;
  float loc[3], rot[3];
  bool newob = false;

  /* NOTE: We use 'Y' here (not 'Z'), as. */
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Y', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  /* Add new ob if not currently editing a Pen ob. */
  if ((pd == nullptr) || (PEN_ANY_MODE(pd) == false)) {
    const char *ob_name = nullptr;
    switch (type) {
      case PEN_EMPTY: {
        ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Pen");
        break;
      }
      case PEN_MONKEY: {
        ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Suzanne");
        break;
      }
      case PEN_STROKE: {
        ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Stroke");
        break;
      }
      case PEN_LRT_OB:
      case PEN_LRT_SCENE:
      case PEN_LRT_COLLECTION: {
        ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "LineArt");
        break;
      }
      default: {
        break;
      }
    }

    ob = ed_ob_add_type(C, OB_PEN_LEGACY, ob_name, loc, rot, true, local_view_bits);
    pd = static_cast<PenData *>(ob->data);
    newob = true;
  }
  else {
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    win_ev_add_notifier(C, NC_PEN | ND_DATA | NA_ADDED, nullptr);
  }

  /* create relevant geometry */
  switch (type) {
    case PEN_EMPTY: {
      float mat[4][4];

      ed_ob_new_primitive_matrix(C, ob, loc, rot, nullptr, mat);
      ed_pen_create_blank(C, ob, mat);
      break;
    }
    case PEN_STROKE: {
      float radius = api_float_get(op->ptr, "radius");
      float scale[3];
      copy_v3_fl(scale, radius);
      float mat[4][4];

      ed_ob_new_primitive_matrix(C, ob, loc, rot, scale, mat);

      ed_pen_create_stroke(C, ob, mat);
      break;
    }
    case PEN_MONKEY: {
      float radius = api_float_get(op->ptr, "radius");
      float scale[3];
      copy_v3_fl(scale, radius);
      float mat[4][4];

      ed_ob_new_primitive_matrix(C, ob, loc, rot, scale, mat);

      ed_pen_create_monkey(C, ob, mat);
      break;
    }
    case PEN_LRT_SCENE:
    case PEN_LRT_COLLECTION:
    case PEN_LRT_OB: {
      float radius = api_float_get(op->ptr, "radius");
      float scale[3];
      copy_v3_fl(scale, radius);
      float mat[4][4];

      ed_ob_new_primitive_matrix(C, ob, loc, rot, scale, mat);

      ed_pen_create_lineart(C, ob);

      pd = static_cast<PenData *>(ob->data);

      /* Add Line Art mod */
      LineartPenModData *md = (LineartPenModData *)dune_pen_mod_new(
          ePenModTypeLineart);
      lib_addtail(&ob->pen_mods, md);
      dune_pen_mod_unique_name(&ob->pen_mods, (PenModData *)md);

      if (type == PEN_LRT_COLLECTION) {
        md->src_type = LRT_SRC_COLLECTION;
        md->src_collection = cxt_data_collection(C);
      }
      else if (type == PEN_LRT_OB) {
        md->src_type = LRT_SRC_OB;
        md->src_ob = ob_orig;
      }
      else {
        /* Whole scene. */
        md->src_type = LRT_SRC_SCENE;
      }
      /* Only created one layer and one material. */
      STRNCPY(md->target_layer, ((PenDataLayer *)pd->layers.first)->info);
      md->target_material = dune_pen_material(ob, 1);
      if (md->target_material) {
        id_us_plus(&md->target_material->id);
      }

      if (use_lights) {
        ob->dtx |= OB_USE_PEN_LIGHTS;
      }
      else {
        ob->dtx &= ~OB_USE_PEN_LIGHTS;
      }

      /* Stroke object is drawn in front of meshes by default. */
      if (use_in_front) {
        ob->dtx |= OB_DRW_IN_FRONT;
      }
      else {
        if (stroke_depth_order == PEN_DRWMODE_3D) {
          pd->drw_mode = PEN_DRWMODE_3D;
        }
        md->stroke_depth_offset = stroke_depth_offset;
      }

      break;
    }
    default:
      dune_report(op->reports, RPT_WARNING, "Not implemented");
      break;
  }

  /* If this is a new object, init default stuff (colors, etc.) */
  if (newob) {
    /* set default viewport color to black */
    copy_v3_fl(ob->color, 0.0f);
      
ed_pen_add_defaults(C, ob);
  }

  return OP_FINISHED;
}

static void ob_add_ui(Cxt * /*C*/, WinOp *op)
{
  uiLayout *layout = op->layout;

  uiLayoutSetPropSep(layout, true);

  uiItemR(layout, op->ptr, "radius", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "align", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "location", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "rotation", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "type", UI_ITEM_NONE, nullptr, ICON_NONE);

  int type = api_enum_get(op->ptr, "type");
  if (ELEM(type, PEN_LRT_COLLECTION, PEN_LRT_OB, PEN_LRT_SCENE)) {
    uiItemR(layout, op->ptr, "use_lights", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "use_in_front", UI_ITEM_NONE, nullptr, ICON_NONE);
    bool in_front = api_bool_get(op->ptr, "use_in_front");
    uiLayout *col = uiLayoutColumn(layout, false);
    uiLayoutSetActive(col, !in_front);
    uiItemR(col, op->ptr, "stroke_depth_offset", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(col, op->ptr, "stroke_depth_order", UI_ITEM_NONE, nullptr, ICON_NONE);
  }
}

static EnumPropItem api_enum_pen_add_stroke_depth_order_items[] = {
    {PEN_DRWMODE_2D,
     "2D",
     0,
     "2D Layers",
     "Display strokes using pen layers to define order"},
    {PEN_DRWMODE_3D, "3D", 0, "3D Location", "Display strokes using real 3D position in 3D space"},
    {0, nullptr, 0, nullptr, nullptr},
};

void OB_OT_pen_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Pen";
  ot->description = "Add a Pen ob to the scene";
  ot->idname = "OB_OT_pen_add";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = ob_pen_add_ex;
  ot->poll = ob_pen_add_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* ui */
  ot->ui = ob_add_ui;

  /* props */
  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, false);

  ot->prop = api_def_enum(ot->sapi, "type", api_enum_object_gpencil_type_items, 0, "Type", "");
  api_def_bool(ot->sapi,
                  "use_in_front",
                  true,
                  "Show In Front",
                  "Show line art pen in front of everything");
  api_def_float(ot->sapi,
                "stroke_depth_offset",
                0.05f,
                0.0f,
                FLT_MAX,
                "Stroke Offset",
                "Stroke offset for the line art mod",
                0.0f,
                0.5f);
  api_def_bool(
      ot->sapi, "use_lights", false, "Use Lights", "Use lights for this grease pencil object");
  api_def_enum(
      ot->sapi,
      "stroke_depth_order",
      api_enum_pen_add_stroke_depth_order_items,
      PEN_DRWMODE_3D,
      "Stroke Depth Order",
      "Defines how the strokes are ordered in 3D space (for objects not displayed 'In Front')");
}

/* Add Pen Op */
static int ob_pen_add_ex(Cxt *C, WinOp *op)
{
  using namespace dune::ed;
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  /* TODO: For now, only support adding the 'Stroke' type. */
  const int type = api_enum_get(op->ptr, "type");

  ushort local_view_bits;
  float loc[3], rot[3];

  /* We use 'Y' here (not 'Z'), as. */
  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Y', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  const char *ob_name = nullptr;
  switch (type) {
    case PEN_EMPTY: {
      ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Pen");
      break;
    }
    case PEN_STROKE: {
      ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Stroke");
      break;
    }
    case PEN_MONKEY: {
      ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "Suzanne");
      break;
    }
    case PEN_LRT_OB:
    case PEN_LRT_SCENE:
    case PEN_LRT_COLLECTION: {
      ob_name = CXT_DATA_(LANG_CXT_ID_PEN, "LineArt");
      break;
    }
    default: {
      break;
    }
  }

  Ob *ob = ed_ob_add_type(
      C, OB_PEN, ob_name, loc, rot, false, local_view_bits);
  Pen &pen_id = *static_cast<Pen *>(ob->data);
  switch (type) {
    case PEN_EMPTY: {
      pen::create_blank(*main, *ob, scene->r.cfra);
      break;
    }
    case PEN_STROKE: {
      const float radius = api_float_get(op->ptr, "radius");
      const float3 scale(radius);

      float4x4 mat;
      ed_ob_new_primitive_matrix(C, ob, loc, rot, scale, mat.ptr());

      pen::create_stroke(*main, *ob, mat, scene->r.cfra);
      break;
    }
    case PEN_MONKEY: {
      const float radius = api_float_get(op->ptr, "radius");
      const float3 scale(radius);

      float4x4 mat;
      ed_ob_new_primitive_matrix(C, ob, loc, rot, scale, mat.ptr());

      pen::create_suzanne(*main, *ob, mat, scene->r.cfra);
      break;
    }
    case PEN_LRT_OB:
    case PEN_LRT_SCENE:
    case PEN_LRT_COLLECTION: {
      /* TODO. */
      break;
    }
  }

  graph_id_tag_update(&pen_id.id, ID_RECALC_GEOMETRY);
  win_main_add_notifier(NC_GEOM | ND_DATA, &pen_id id);

  return OP_FINISHED;
}

void OB_OT_pen_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Pen";
  ot->description = "Add a Pen ob to the scene";
  ot->idname = "OB_OT_pen_add";

  /* api cbs */
  ot->ex = ob_pen_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = api_def_enum(ot->sapi, "type", api_enum_ob_pen_type_items, 0, "Type", "");

  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, false);
}

/* Add Light Op */
static const char *get_light_defname(int type)
{
  switch (type) {
    case LA_LOCAL:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Point");
    case LA_SUN:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Sun");
    case LA_SPOT:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Spot");
    case LA_AREA:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Area");
    default:
      return CXT_DATA_(LANG_CXT_ID_LIGHT, "Light");
  }
}

static int ob_light_add_ex(Cxt *C, WinOp *op)
{
  Ob *ob;
  Light *la;
  int type = api_enum_get(op->ptr, "type");
  ushort local_view_bits;
  float loc[3], rot[3];

  win_op_view3d_unit_defaults(C, op);
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  ob = ed_ob_add_type(C, OB_LAMP, get_light_defname(type), loc, rot, false, local_view_bits);

  float size = api_float_get(op->ptr, "radius");
  /* Better defaults for light size. */
  switch (type) {
    case LA_LOCAL:
    case LA_SPOT:
      break;
    case LA_AREA:
      size *= 4.0f;
      break;
    default:
      size *= 0.5f;
      break;
  }
  dune_ob_obdata_size_init(ob, size);

  la = (Light *)ob->data;
  la->type = type;

  if (type == LA_SUN) {
    la->energy = 1.0f;
  }

  return OP_FINISHED;
}

void OB_OT_light_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Light";
  ot->description = "Add a light object to the scene";
  ot->idname = "OB_OT_light_add";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = ob_light_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", rna_enum_light_type_items, 0, "Type", "");
  api_def_prop_translation_cxt(ot->prop, BLT_I18NCONTEXT_ID_LIGHT);

  ed_ob_add_unit_props_radius(ot);
  ed_ob_add_generic_props(ot, false);
}

/* Add Collection Instance Op */
struct CollectionAddInfo {
  /* The collection that is supposed to be added, determined through operator properties. */
  Collection *collection;
  /* The local-view bits (if any) the object should have set to become visible in current coxt */
  ushort local_view_bits;
  /* The transform that should be applied to the collection, determined through op props
   * if set (e.g. to place the collection under the cursor), otherwise through context (e.g. 3D
   * cursor location). */
  float loc[3], rot[3];
};

static std::optional<CollectionAddInfo> collection_add_info_get_from_op(Cxt *C,
                                                                        WinOp *op)
{
  CollectionAddInfo add_info{};

  Main *main = cxt_data_main(C);

  ApiProp *prop_location = api_struct_find_prop(op->ptr, "location");

  add_info.collection = reinterpret_cast<Collection *>(
      win_op_props_id_lookup_from_name_or_session_uuid(main, op->ptr, ID_GR));

  bool update_location_if_necessary = false;
  if (add_info.collection) {
    update_location_if_necessary = true;
  }
  else {
    add_info.collection = static_cast<Collection *>(
        lib_findlink(&main->collections, api_enum_get(op->ptr, "collection")));
  }

  if (update_location_if_necessary && cxt_win_rgn_view3d(C)) {
    int mval[2];
    if (!api_prop_is_set(op->ptr, prop_location) && ob_add_drop_xy_get(C, op, &mval)) {
      ed_ob_location_from_view(C, add_info.loc);
      ed_view3d_cursor3d_position(C, mval, false, add_info.loc);
      api_prop_float_set_array(op->ptr, prop_location, add_info.loc);
    }
  }

  if (add_info.collection == nullptr) {
    return std::nullopt;
  }

  if (!ed_ob_add_generic_get_opts(C,
                                  op,
                                  'Z',
                                  add_info.loc,
                                  add_info.rot,
                                  nullptr,
                                  nullptr,
                                  &add_info.local_view_bits,
                                  nullptr))
  {
    return std::nullopt;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);

  /* Avoid dependency cycles. */
  LayerCollection *active_lc = dune_layer_collection_get_active(view_layer);
  while (dune_collection_cycle_find(active_lc->collection, add_info.collection)) {
    active_lc = dune_layer_collection_activate_parent(view_layer, active_lc);
  }

  return add_info;
}

static int collection_instance_add_ex(Cxt *C, WinOp *op)
{
  std::optional<CollectionAddInfo> add_info = collection_add_info_get_from_op(C, op);
  if (!add_info) {
    return OP_CANCELLED;
  }

  Ob *ob = ed_ob_add_type(C,
                          OB_EMPTY,
                          add_info->collection->id.name + 2,
                          add_info->loc,
                          add_info->rot,
                          false,
                          add_info->local_view_bits);
  ob->instance_collection = add_info->collection;
  ob->empty_drwsize = U.collection_instance_empty_size;
  ob->transflag |= OB_DUPLICOLLECTION;
  id_us_plus(&add_info->collection->id);

  return OP_FINISHED;
}

static int ob_instance_add_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  if (!ob_add_drop_xy_is_set(op)) {
    api_int_set(op->ptr, "drop_x", ev->xy[0]);
    api_int_set(op->ptr, "drop_y", ev->xy[1]);
  }

  if (!win_op_props_id_lookup_is_set(op->ptr)) {
    return win_enum_search_invoke(C, op, ev);
  }
  return op->type->ex(C, op);
}

void OB_OT_collection_instance_add(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Add Collection Instance";
  ot->description = "Add a collection instance";
  ot->idname = "OB_OT_collection_instance_add";

  /* api cbs */
  ot->invoke = ob_instance_add_invoke;
  ot->ex = collection_instance_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_string(
      ot->sapi, "name", "Collection", MAX_ID_NAME - 2, "Name", "Collection name to add");
  prop = api_def_enum(ot->sapi, "collection", api_enum_dummy_NULL_items, 0, "Collection", "");
  api_def_enum_fns(prop, api_collection_itemf);
  api_def_prop_flag(prop, PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;
  ed_ob_add_generic_props(ot, false);

  win_op_props_id_lookup(ot, false);

  ob_add_drop_xy_props(ot);
}

/* Collection Drop Op
 *
 * Internal op for collection dropping.
 *
 * This is tied closely together to the drop-box cbs, so it shouldn't be used on its
 *          own.
 *
 * The drop-box callback imports the collection, links it into the view-layer, selects all imported
 * obs (which may include peripheral objects like parents or boolean-objects of an object in
 * the collection) and activates one. Only the callback has enough info to do this reliably. Based
 * on the instancing operator option, this operator then does one of two things:
 * - Instancing enabled: Unlink the collection again, and instead add a collection instance empty
 *   at the drop position.
 * - Instancing disabled: Transform the objects to the drop position, keeping all relative
 *   transforms of the objects to each other as is */

static int collection_drop_exec(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  LayerCollection *active_collection = cxt_data_layer_collection(C);
  std::optional<CollectionAddInfo> add_info = collection_add_info_get_from_op(C, op);
  if (!add_info) {
    return OP_CANCELLED;
  }

  if (api_bool_get(op->ptr, "use_instance")) {
    dune_collection_child_remove(main, active_collection->collection, add_info->collection);
    graph_id_tag_update(&active_collection->collection->id, ID_RECALC_COPY_ON_WRITE);
    graph_relations_tag_update(main);

    Ob *ob = ed_ob_add_type(C,
                            OB_EMPTY,
                            add_info->collection->id.name + 2,
                            add_info->loc,
                            add_info->rot,
                            false,
                            add_info->local_view_bits);
    ob->instance_collection = add_info->collection;
    ob->empty_drwsize = U.collection_instance_empty_size;
    ob->transflag |= OB_DUPCOLLECTION;
    id_us_plus(&add_info->collection->id);
  }
  else {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    float delta_mat[4][4];
    unit_m4(delta_mat);

    const float scale[3] = {1.0f, 1.0f, 1.0f};
    loc_eul_size_to_mat4(delta_mat, add_info->loc, add_info->rot, scale);

    float offset[3];
    /* Reverse apply the instance offset, so toggling the Instance option doesn't cause the
     * collection to jump. */
    negate_v3_v3(offset, add_info->collection->instance_offset);
    translate_m4(delta_mat, UNPACK3(offset));

    ObInViewLayerParams params = {0};
    uint obs_len;
    Ob **obs = dune_view_layer_array_sel_ob_params(
        view_layer, nullptr, &ob_len, &params);
    ed_ob_xform_array_m4(obs, obs_len, delta_mat);

    mem_free(obs);
  }

  return OP_FINISHED;
}

void OB_OT_collection_external_asset_drop(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  /* Name should only be displayed in the drag tooltip. */
  ot->name = "Add Collection";
  ot->description = "Add the dragged collection to the scene";
  ot->idname = "OB_OT_collection_external_asset_drop";

  /* api cbs */
  ot->invoke = ob_instance_add_invoke;
  ot->ex = collection_drop_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* props */
  win_op_props_id_lookup(ot, false);

  ed_ob_add_generic_props(ot, false);

  /* IMPORTANT: Instancing option. Intentionally remembered across executions (no PROP_SKIP_SAVE). */
  api_def_bool(ot->sapi,
                  "use_instance",
                  true,
                  "Instance",
                  "Add the dropped collection as collection instance");

  ob_add_drop_xy_props(ot);

  prop = ap_def_enum(ot->sapi, "collection", api_enum_dummy_NULL_items, 0, "Collection", "");
  api_def_enum_fns(prop, api_collection_itemf);
  api_def_prop_flag(prop,
                   (PropeFlag)(PROP_SKIP_SAVE | PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE));
  ot->prop = prop;
}

/* Add Data Instance Op
 * Use for dropping Id's from the outliner. */
static int ob_data_instance_add_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Id *id = nullptr;
  ushort local_view_bits;
  float loc[3], rot[3];

  ApiProp *prop_type = api_struct_find_prop(op->ptr, "type");
  ApiProp *prop_location = api_struct_find_prop(op->ptr, "location");

  const short id_type = api_prop_enum_get(op->ptr, prop_type);
  id = win_op_props_id_lookup_from_name_or_session_uuid(
      main, op->ptr, (IdType)id_type);
  if (id == nullptr) {
    return OP_CANCELLED;
  }
  const int ob_type = dune_ob_obdata_to_type(id);
  if (ob_type == -1) {
    return OP_CANCELLED;
  }

  if (cxt_win_rgn_view3d(C)) {
    int mval[2];
    if (!api_prop_is_set(op->ptr, prop_location) && ob_add_drop_xy_get(C, op, &mval)) {
      ed_ob_location_from_view(C, loc);
      ed_view3d_cursor3d_position(C, mval, false, loc);
      api_prop_float_set_array(op->ptr, prop_location, loc);
    }
  }

  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  ed_ob_add_type_with_obdata(
      C, ob_type, id->name + 2, loc, rot, false, local_view_bits, id);

  return OP_FINISHED;
}

void OB_OT_data_instance_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Ob Data Instance";
  ot->description = "Add an ob data instance";
  ot->idname = "OB_OT_data_instance_add";

  /* api cbs */
  ot->invoke = ob_add_drop_xy_generic_invoke;
  ot->ex = ob_data_instance_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  win_op_props_id_lookup(ot, true);
  ApiProp *prop = api_def_enum(ot->sapi, "type", api_enum_id_type_items, 0, "Type", "");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_ID);
  ed_ob_add_generic_props(ot, false);

  ob_add_drop_xy_props(ot);
}

/* Add Speaker Op */
static int ob_speaker_add_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);

  ushort local_view_bits;
  float loc[3], rot[3];
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }
  Ob *ob = ed_ob_add_type(C, OB_SPEAKER, nullptr, loc, rot, false, local_view_bits);
  const bool is_liboverride = ID_IS_OVERRIDE_LIB(ob);

  /* To make it easier to start using this immediately in NLA, a default sound clip is created
   * ready to be moved around to re-time the sound and/or make new sound clips. */
  {
    /* create new data for NLA hierarchy */
    AnimData *adt = dune_animdata_ensure_id(&ob->id);
    NlaTrack *nlt = dune_nlatrack_new_tail(&adt->nla_tracks, is_liboverride);
    dune_nlatrack_set_active(&adt->nla_tracks, nlt);
    NlaStrip *strip = dune_nla_add_soundstrip(main, scene, static_cast<Speaker *>(ob->data));
    strip->start = scene->r.cfra;
    strip->end += strip->start;

    /* hook them up */
    dune_nlatrack_add_strip(nlt, strip, is_liboverride);

    /* Auto-name the strip, and give the track an interesting name. */
    STRNCPY_UTF8(nlt->name, DATA_("SoundTrack"));
    dune_nlastrip_validate_name(adt, strip);

    win_ev_add_notifier(C, NC_ANIM | ND_NLA | NA_ADDED, nullptr);
  }

  return OP_FINISHED;
}

void OB_OT_speaker_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Speaker";
  ot->description = "Add a speaker ob to the scene";
  ot->idname = "OB_OT_speaker_add";

  /* api cbs */
  ot->ex = ob_speaker_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_ob_add_generic_props(ot, true);
}

/* Add Curves Op */
static int ob_curves_random_add_ex(Cxt *C, WinOp *op)
{
  using namespace dune;

  ushort local_view_bits;
  float loc[3], rot[3];
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  Ob *ob = ed_ob_add_type(C, OB_CURVES, nullptr, loc, rot, false, local_view_bits);

  Curves *curves_id = static_cast<Curves *>(ob->data);
  curves_id->geometry.wrap() = ed::curves::primitive_random_sphere(500, 8);

  return OP_FINISHED;
}

void OB_OT_curves_random_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Random Curves";
  ot->description = "Add a curves obj with random curves to the scene";
  ot->idname = "OB_OT_curves_random_add";

  /* api cbs */
  ot->ex = ob_curves_random_add_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_ob_add_generic_props(ot, false);
}

static int ob_curves_empty_hair_add_ex(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);

  ushort local_view_bits;
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', nullptr, nullptr, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  Ob *surface_ob = cxt_data_active_ob(C);
  lib_assert(surface_ob != nullptr);

  Ob *curves_ob = ed_ob_add_type(
      C, OB_CURVES, nullptr, nullptr, nullptr, false, local_view_bits);
  dune_ob_apply_mat4(curves_ob, surface_ob->ob_to_world, false, false);

  /* Set surface ob */
  Curves *curves_id = static_cast<Curves *>(curves_ob->data);
  curves_id->surface = surface_ob;

  /* Parent to surface ob. */
  ed_ob_parent_set(
      op->reports, C, scene, curves_ob, surface_ob, PAR_OB, false, true, nullptr);

  /* Decide which UV map to use for attachment. */
  Mesh *surface_mesh = static_cast<Mesh *>(surface_ob->data);
  const char *uv_name = CustomData_get_active_layer_name(&surface_mesh->loop_data, CD_PROP_FLOAT2);
  if (uv_name != nullptr) {
    curves_id->surface_uv_map = lib_strdup(uv_name);
  }

  /* Add deformation mod */
  dune::ed::curves::ensure_surface_deformation_node_exists(*C, *curves_ob);

  /* Make sure the surface object has a rest position attribute which is necessary for
   * deformations. */
  surface_ob->mod_flag |= OB_MOD_FLAG_ADD_REST_POSITION;

  return OP_FINISHED;
}

static bool ob_curves_empty_hair_add_poll(Cxt *C)
{
  if (!ed_op_ob_mode(C)) {
    return false;
  }
  Ob *ob = cxt_data_active_ob(C);
  if (ob == nullptr || ob->type != OB_MESH) {
    cxt_win_op_poll_msg_set(C, "No active mesh ob");
    return false;
  }
  return true;
}

void OB_OT_curves_empty_hair_add(WinOpType *ot)
{
  ot->name = "Add Empty Curves";
  ot->description = "Add an empty curve ob to the scene with the selected mesh as surface";
  ot->idname = "OB_OT_curves_empty_hair_add";

  ot->ex = ob_curves_empty_hair_add_ex;
  ot->poll = ob_curves_empty_hair_add_poll;

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_ob_add_generic_props(ot, false);
}

/* Add Point Cloud Op */
static bool ob_pointcloud_add_poll(Cxt *C)
{
  if (!U.experimental.use_new_point_cloud_type) {
    return false;
  }
  return ed_op_obmode(C);
}

static int ob_pointcloud_add_ex(Cxt *C, WinOp *op)
{
  ushort local_view_bits;
  float loc[3], rot[3];
  if (!ed_ob_add_generic_get_opts(
          C, op, 'Z', loc, rot, nullptr, nullptr, &local_view_bits, nullptr))
  {
    return OP_CANCELLED;
  }

  Ob *ob = ed_ob_add_type(C, OB_POINTCLOUD, nullptr, loc, rot, false, local_view_bits);
  ob->dtx |= OB_DRAWBOUNDOX; /* TODO: remove once there is actual drawing. */

  return OP_FINISHED;
}

void OB_OT_pointcloud_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Point Cloud";
  ot->description = "Add a point cloud object to the scene";
  ot->idname = "OB_OT_pointcloud_add";

  /* api cbs */
  ot->ex = ob_pointcloud_add_ex;
  ot->poll = ob_pointcloud_add_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_ob_add_generic_props(ot, false);
}

/* Del Ob Op */
void ed_ob_base_free_and_unlink(Main *main, Scene *scene, Ob *ob)
{
  if (ID_REAL_USERS(ob) <= 1 && ID_EXTRA_USERS(ob) == 0 &&
      dune_lib_ed_is_indirectly_used(main, ob))
  {
    /* We cannot delete indirectly used ob... */
    printf(
        "WARNING, undeletable object '%s', should have been caught before reaching this "
        "function!",
        ob->id.name + 2);
    return;
  }
  if (!dune_lib_override_lib_id_is_user_deletable(bain, &ob->id)) {
    /* Do not del obs used by overrides of collections. */
    return;
  }

  graph_id_tag_update_ex(main, &ob->id, ID_RECALC_BASE_FLAGS);

  dune_scene_collections_ob_remove(main, scene, ob, true);
}

void ed_ob_base_free_and_unlink_no_indirect_check(Main *main, Scene *scene, Ob *ob)
{
  lib_assert(!dune_lib_id_is_indirectly_used(main, ob));
  graph_id_tag_update_ex(main, &ob->id, ID_RECALC_BASE_FLAGS);
  dune_scene_collections_ob_remove(main, scene, ob, true);
}

static int ob_del_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  WinMngr *wm = cxt_wm(C);
  const bool use_global = api_bool_get(op->ptr, "use_global");
  const bool confirm = op->flag & OP_IS_INVOKE;
  uint changed_count = 0;
  uint tagged_count = 0;

  if (cxt_data_edit_ob(C)) {
    return OP_CANCELLED;
  }

  dune_main_id_tag_all(main, LIB_TAG_DOIT, false);

    //CXT_DATA_BEGIN migrated to CXT_DATA_ITER
  CXT_DATA_ITER (C, Ob *, ob, sel_obs) {
    if (ob->id.tag & LIB_TAG_INDIRECT) {
      /* Can this case ever happen? */
      dune_reportf(op->reports,
                  RPT_WARNING,
                  "Cannot del indirectly linked ob '%s'",
                  ob->id.name + 2);
      continue;
    }
    /* dune_lib_override_lib_id_is_user_deletable
     * migrated to
     * dune_override_lib_id_user_can_be_del */
     
    if (!dune_lib_override_lib_id_is_user_deletable(main, &ob->id)) {
      dune_reportf(op->reports,
                  RPT_WARNING,
                  "Cannot del ob '%s' as it is used by override collections",
                  ob->id.name + 2);
      continue;
    }

    if (ID_REAL_USERS(ob) <= 1 && ID_EXTRA_USERS(ob) == 0 &&
        dune_lib_id_is_indirectly_used(main, ob);
    {
      dune_reportf(op->reports,
                  RPT_WARNING,
                  "Cannot del ob '%s' from scene '%s', indirectly used objects need at "
                  "least one user",
                  ob->id.name + 2,
                  scene->id.name + 2);
      continue;
    }

    /* if pen ob, set cache as dirty */
    if (ob->type == OB_PEN_LEGACY) {
      PenData *pd = (PenData *)ob->data;
      graph_id_tag_update(&pd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    }

    /* Use multi tagged del if `use_global=True`, or the ob is used only in one scene. */
    if (use_global || ID_REAL_USERS(ob) <= 1) {
      ob->id.tag |= LIB_TAG_DOIT;
      tagged_count += 1;
    }
    else {
      /* Ob is used in multiple scenes. Del the ob from the current scene only. */
      ed_ob_base_free_and_unlink_no_indirect_check(main, scene, ob);
      changed_count += 1;

      /* FIXME: this will also remove parent from grease pencil from other scenes. */
      /* Remove from Pen parent */
      LIST_FOREACH (PenData *, pd, &main->pens) {
        LIST_FOREACH (PenDataLayer *, pl, &pd->layers) {
          if (pl->parent != nullptr) {
            if (pl->parent == ob) {
              pl->parent = nullptr;
            }
          }
        }
      }
    }
  }
  CXT_DATA_END;

  if ((changed_count + tagged_count) == 0) {
    return OP_CANCELLED;
  }

  if (tagged_count > 0) {
    dune_id_multi_tagged_delete(main);
  }

  if (confirm) {
    dune_reportf(op->reports, RPT_INFO, "Del %u ob(s)", (changed_count + tagged_count));
  }

  /* delete has to handle all open scenes */
  dune_main_id_tag_list(&main->scenes, LIB_TAG_DOIT, true);
  LIST_FOREACH (Win *, win, &wm->wins) {
    scene = win_get_active_scene(win);

    if (scene->id.tag & LIB_TAG_DOIT) {
      scene->id.tag &= ~LIB_TAG_DOIT;

      graph_relations_tag_update(main);

      graph_id_tag_update(&scene->id, ID_RECALC_SEL);
      win_ev_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
      win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);
    }
  }

  return OP_FINISHED;
}

void OB_OT_del(WinOpType *ot)
{
  /* id */
  ot->name = "Del";
  ot->description = "Del sel obs";
  ot->idname = "OB_OT_del";

  /* api cbs */
  ot->invoke = win_op_confirm_or_ex;
  ot->ex = ob_del_ex;
  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ApiProp *prop;
  prop = api_def_bool(
      ot->sapi, "use_global", false, "Del Globally", "Remove ob from all scenes");
  api_def_prop_flag(prop, (PropFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));
  win_op_props_confirm_or_ex(ot);
}

/* Copy Ob Utils */
/* after copying obs, copied data should get new ptrs */
static void copy_ob_set_idnew(Cxt *C)
{
  Main *main = cxt_data_main(C);

  CXT_DATA_BEGIN (C, Ob *, ob, sel_editable_obs) {
    dune_libblock_relink_to_newid(main, &ob->id, 0);
  }
  CXT_DATA_END;

#ifndef NDEBUG
  /* Call to `dune_libblock_relink_to_newid` above is supposed to have cleared all those flags. */
  Id *id_iter;
  FOREACH_MAIN_ID_BEGIN (main, id_iter) {
    if (GS(id_iter->name) == ID_OB) {
      /* Not all dup obs would be used by other newly dup data, so their flag
       * will not always be cleared. */
      continue;
    }
    lib_assert((id_iter->tag & LIB_TAG_NEW) == 0);
  }
  FOREACH_MAIN_ID_END;
#endif

  dune_main_id_newptr_and_tag_clear(main);
}

/* Make Instanced Obs Real Op */

/* That whole hierarchy handling based on persistent_id tricks is
 * very confusing and convoluted, and it will fail in many cases besides basic ones.
 * Think this should be replaced by a proper tree-like representation of the instantiations,
 * should help a lot in both readability, and precise consistent rebuilding of hierarchy. */

/* regarding hashing dup-obs which come from OB_DUPCOLLECTION,
 * skip the first member of DupOb.persistent_id
 * since its a unique id and we only want to know if the group ob are from the same
 * dup-group instance.
 *
 * regarding hashing dup-obs which come from non-OB_DUPCOLLECTION,
 * include the first member of DupOb.persistent_id
 * since its the index of the vertex/face the ob is instantiated on and we want to identify
 * obs on the same vertex/face.
 * In other words, we consider each group of obs from a same item as being
 * the 'local group' where to check for parents. */
static uint dupob_hash(const void *ptr)
{
  const DupOb *dob = static_cast<const DupOb *>(ptr);
  uint hash = lib_ghashutil_ptrhash(dob->ob);

  if (dob->type == OB_DUPCOLLECTION) {
    for (int i = 1; (i < MAX_DUPLI_RECUR) && dob->persistent_id[i] != INT_MAX; i++) {
      hash ^= (dob->persistent_id[i] ^ i);
    }
  }
  else {
    hash ^= (dob->persistent_id[0] ^ 0);
  }
  return hash;
}

/* regarding hashing dup-obs when using OB_DUPCOLLECTION,
 * skip the first member of DupOb.persistent_id
 * since its a unique index and we only want to know if the group obs are from the same
 * dup-group instance */
static uint dupob_instancer_hash(const void *ptr)
{
  const DupOb *dob = static_cast<const DupOp *>(ptr);
  uint hash = lib_ghashutil_inthash(dob->persistent_id[0]);
  for (int i = 1; (i < MAX_DUP_RECUR) && dob->persistent_id[i] != INT_MAX; i++) {
    hash ^= (dob->persistent_id[i] ^ i);
  }
  return hash;
}

/* Compare fn that matches dupob_hash. */
static bool dupob_cmp(const void *a_, const void *b_)
{
  const DupOb *a = static_cast<const DupOb *>(a_);
  const DupOb *b = static_cast<const DupOb *>(b_);

  if (a->ob != b->ob) {
    return true;
  }

  if (a->type != b->type) {
    return true;
  }

  if (a->type == OB_DUPCOLLECTION) {
    for (int i = 1; (i < MAX_DUP_RECUR); i++) {
      if (a->persistent_id[i] != b->persistent_id[i]) {
        return true;
      }
      if (a->persistent_id[i] == INT_MAX) {
        break;
      }
    }
  }
  else {
    if (a->persistent_id[0] != b->persistent_id[0]) {
      return true;
    }
  }

  /* matching */
  return false;
}

/* Cmp fn that matches dupob_instancer_hash. */
static bool dupob_instancer_cmp(const void *a_, const void *b_)
{
  const DupOb *a = static_cast<const DupOb *>(a_);
  const DupOb *b = static_cast<const DupOb *>(b_);

  for (int i = 0; (i < MAX_DUP_RECUR); i++) {
    if (a->persistent_id[i] != b->persistent_id[i]) {
      return true;
    }
    if (a->persistent_id[i] == INT_MAX) {
      break;
    }
  }

  /* matching */
  return false;
}

static void make_ob_duplist_real(Cxt *C,
                                 Graph *graph,
                                 Scene *scene,
                                 Base *base,
                                 const bool use_base_parent,
                                 const bool use_hierarchy)
{
  Main *main = cxt_data_main(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  GHash *parent_gh = nullptr, *instancer_gh = nullptr;

  Ob *ob_eval = graph_get_eval_ob(graph, base->ob);

  if (!(base->ob->transflag & OB_DUP) &&
      !dune::dune::ob_has_geometry_set_instances(*ob_eval))
  {
    return;
  }

  List *list_dup = ob_list_dup(graph, scene, ob_eval);

  if (lib_list_is_empty(list_dup)) {
    free_ob_duplilist(list_dup);
    return;
  }

  GHash *dup_gh = lib_ghash_ptr_new(__func__);
  if (use_hierarchy) {
    parent_gh = lib_ghash_new(dupob_hash, dupob_cmp, __func__);

    if (use_base_parent) {
      instancer_gh = lib_ghash_new(
          dupob_instancer_hash, dupob_instancer_cmp, __func__);
    }
  }

  LIST_FOREACH (DupOb *, dob, lb_duplis) {
    Ob *ob_src = graph_get_original_ob(dob->ob);
    Ob *ob_dst = static_cast<Ob *>(ID_NEW_SET(ob_src, dune_id_copy(main, &ob_src->id)));
    id_us_min(&ob_dst->id);

    /* font listdup can have a totcol wo material, we get them from parent
     * should be impl better... */
    if (ob_dst->mat == nullptr) {
      ob_dst->totcol = 0;
    }

    dune_collection_ob_add_from(main, scene, base->ob, ob_dst);
    dune_view_layer_synced_ensure(scene, view_layer);
    Base *base_dst = dune_view_layer_base_find(view_layer, ob_dst);
    lib_assert(base_dst != nullptr);

    ed_ob_base_sel(base_dst, BA_SEL);
    graph_id_tag_update(&ob_dst->id, ID_RECALC_SEL);

    dune_scene_ob_base_flag_sync_from_base(base_dst);

    /* make sure apply works */
    dune_animdata_free(&ob_dst->id, true);
    ob_dst->adt = nullptr;

    ob_dst->parent = nullptr;
    dune_constraints_free(&ob_dst->constraints);
    ob_dst->runtime->curve_cache = nullptr;
    const bool is_dup_instancer = (ob_dst->transflag & OB_DUP) != 0;
    ob_dst->transflag &= ~OB_DUP;
    /* Remove instantiated collection, it's annoying to keep it here
     * (and get potentially a lot of usages of it then...). */
    id_us_min((Id *)ob_dst->instance_collection);
    ob_dst->instance_collection = nullptr;

    copy_m4_m4(ob_dst->ob_to_world, dob->mat);
    dune_ob_apply_mat4(ob_dst, ob_dst->ob_to_world, false, false);

    lib_ghash_insert(dupli_gh, dob, ob_dst);
    if (parent_gh) {
      void **val;
      /* Due to nature of hash/comparison of this ghash, a lot of dups may be considered as
       * 'the same', this avoids trying to insert same key several time and
       * raise asserts in debug builds... */
      if (!lib_ghash_ensure_p(parent_gh, dob, &val)) {
        *val = ob_dst;
      }

      if (is_dup_instancer && instancer_gh) {
        /* Same as above, we may have several 'hits'. */
        if (!lib_ghash_ensure_p(instancer_gh, dob, &val)) {
          *val = ob_dst;
        }
      }
    }
  }

  LIST_FOREACH (DupOb *, dob, list_dup) {
    Ob *ob_src = dob->ob;
    Ob *ob_dst = static_cast<Ob *>(lib_ghash_lookup(dup_gh, dob));
      
    /* Remap new ob to itself, and clear again newid ptr of orig ob */
    dune_libblock_relink_to_newid(main, &ob_dst->id, 0);

    graph_id_tag_update(&ob_dst->id, ID_RECALC_GEOMETRY);

    if (use_hierarchy) {
      /* original parents */
      Ob *ob_src_par = ob_src->parent;
      Ob *ob_dst_par = nullptr;

      /* find parent that was also made real */
      if (ob_src_par) {
        /* OK to keep most of the members uninitialized,
         * they won't be read, this is simply for a hash lookup. */
        DupOb dob_key;
        dob_key.ob = ob_src_par;
        dob_key.type = dob->type;
        if (dob->type == OB_DUPCOLLECTION) {
          memcpy(&dob_key.persistent_id[1],
                 &dob->persistent_id[1],
                 sizeof(dob->persistent_id[1]) * (MAX_DUP_RECUR - 1));
        }
        else {
          dob_key.persistent_id[0] = dob->persistent_id[0];
        }
        ob_dst_par = static_cast<Ob *>(lib_ghash_lookup(parent_gh, &dob_key));
      }

      if (ob_dst_par) {
        /* allow for all possible parent types */
        ob_dst->partype = ob_src->partype;
        STRNCPY(ob_dst->parsubstr, ob_src->parsubstr);
        ob_dst->par1 = ob_src->par1;
        ob_dst->par2 = ob_src->par2;
        ob_dst->par3 = ob_src->par3;

        copy_m4_m4(ob_dst->parentinv, ob_src->parentinv);

        ob_dst->parent = ob_dst_par;
      }
    }
    if (use_base_parent && ob_dst->parent == nullptr) {
      Ob *ob_dst_par = nullptr;

      if (instancer_gh != nullptr) {
        /* OK to keep most of the members uninitialized,
         * they won't be read, this is simply for a hash lookup. */
        DupOb dob_key;
        /* We are looking one step upper in hierarchy so must 'shift' the `persistent_id`,
         * ignoring the first item.
         * We only check on persistent_id here bc we have no idea what ob it might be. */
        memcpy(&dob_key.persistent_id[0],
               &dob->persistent_id[1],
               sizeof(dob_key.persistent_id[0]) * (MAX_DUP_RECUR - 1));
        ob_dst_par = static_cast<Ob *>(lib_ghash_lookup(instancer_gh, &dob_key));
      }

      if (ob_dst_par == nullptr) {
        /* Default to parenting to root object...
         * Always the case when use_hierarchy is false. */
        ob_dst_par = base->ob;
      }

      ob_dst->parent = ob_dst_par;
      ob_dst->partype = PAROB;
    }

    if (ob_dst->parent) {
      /* This may be the parent of other obs, but it should
       * still work out ok */
      dune_ob_apply_mat4(ob_dst, dob->mat, false, true);

      /* to set ob_dst->orig and in case there's any other discrepancies */
      graph_id_tag_update(&ob_dst->id, ID_RECALC_TRANSFORM);
    }
  }

  if (base->ob->transflag & OB_DUPCOLLECTION && base->ob->instance_collection) {
    base->ob->instance_collection = nullptr;
  }

  ed_ob_base_sel(base, BA_DESEL);
  graph_id_tag_update(&base->ob->id, ID_RECALC_SEL);

  lib_ghash_free(dup_gh, nullptr, nullptr);
  if (parent_gh) {
    lib_ghash_free(parent_gh, nullptr, nullptr);
  }
  if (instancer_gh) {
    lib_ghash_free(instancer_gh, nullptr, nullptr);
  }

  free_ob_listdup(list_dup);

  dune_main_id_newptr_and_tag_clear(main);

  base->ob->transflag &= ~OB_DUP;
  graph_id_tag_update(&base->ob->id, ID_RECALC_COPY_ON_WRITE);
}

static int ob_dups_make_real_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);

  const bool use_base_parent = api_bool_get(op->ptr, "use_base_parent");
  const bool use_hierarchy = api_bool_get(op->ptr, "use_hierarchy");

  dune_main_id_newptr_and_tag_clear(main);

  CXT_DATA_BEGIN (C, Base *, base, sel_editable_bases) {
    make_ob_listdup_real(C, graph, scene, base, use_base_parent, use_hierarchy);

    /* dependencies were changed */
    win_ev_add_notifier(C, NC_OB | ND_PARENT, base->ob);
  }
  CXT_DATA_END;

  graph_tag_update(main);
  win_ev_add_notifier(C, NC_SCENE, scene);
  win_main_add_notifier(NC_OB | ND_DRW, nullptr);
  ed_outliner_sel_sync_from_ob_tag(C);

  return OP_FINISHED;
}

void OB_OT_dups_make_real(WinOpType *ot)
{
  /* ids */
  ot->name = "Make Instances Real";
  ot->description = "Make instanced obs attached to this ob real";
  ot->idname = "OB_OT_dups_make_real";

  /* api cbs */
  ot->ex = ob_dups_make_real_ex;

  ot->poll = ed_op_obmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi,
                  "use_base_parent",
                  false,
                  "Parent",
                  "Parent newly created objects to the original instancer");
  api_def_bool(
      ot->sapi, "use_hierarchy", false, "Keep Hierarchy", "Maintain parent child relationships");
}

/* Data Convert Op */
static const EnumPropItem convert_target_items[] = {
    {OB_CURVES_LEGACY,
     "CURVE",
     ICON_OUTLINER_OB_CURVE,
     "Curve",
     "Curve from Mesh or Txt obs"},
    {OB_MESH,
     "MESH",
     ICON_OUTLINER_OB_MESH,
     "Mesh",
#ifdef WITH_POINT_CLOUD
     "Mesh from Curve, Surface, Metaball, Text, or Point Cloud objects"},
#else
     "Mesh from Curve, Surface, Metaball, or Text objects"},
#endif
    {OB_PEN_LEGACY,
     "PEN",
     ICON_OUTLINER_OB_PEN,
     "Pen",
     "Pen from Curve or Mesh obs"},
#ifdef WITH_POINT_CLOUD
    {OB_POINTCLOUD,
     "POINTCLOUD",
     ICON_OUTLINER_OB_POINTCLOUD,
     "Point Cloud",
     "Point Cloud from Mesh obs"},
#endif
    {OB_CURVES, "CURVES", ICON_OUTLINER_OB_CURVES, "Curves", "Curves from evald curve data"},
#ifdef WITH_PEN_V3
    {OB_PEN,
     "PEN",
     ICON_OUTLINER_OB_PEN,
     "Pen v3",
     "Pen v3 from Pen"},
#endif
    {0, nullptr, 0, nullptr, nullptr},
};

static void ob_data_convert_curve_to_mesh(Main *main, Graph *graph, Ob *ob)
{
  Ob *ob_eval = graph_get_eval_ob(graph, ob);
  Curve *curve = static_cast<Curve *>(ob->data);

  Mesh *mesh = dune_mesh_new_from_ob_to_main(main, graph, ob_eval, true);
  if (mesh == nullptr) {
    /* Unable to convert the curve to a mesh. */
    return;
  }

  dune_ob_free_mods(ob, 0);
  /* Replace curve used by the ob itself. */
  ob->data = mesh;
  ob->type = OB_MESH;
  id_us_min(&curve->id);
  id_us_plus(&mesh->id);
  /* Change obs which are using same curve.
   * A bit annoying, but:
   * It's possible to have multiple curve obs sel which are sharing the same curve
   *   data-block. We don't want mesh to be created for every of those obs.
   * This is how conversion worked for a long time. */
  LIST_FOREACH (Ob *, other_ob, &main->obs) {
    if (other_ob->data == curve) {
      other_ob->type = OB_MESH;

      id_us_min((Id *)other_ob->data);
      other_ob->data = ob->data;
      id_us_plus((Id *)other_ob->data);
    }
  }
}

static bool ob_convert_poll(Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  Base *base_act = cxt_data_active_base(C);
  Ob *obact = base_act ? base_act->ob : nullptr;

  if (obact == nullptr || obact->data == nullptr || ID_IS_LINKED(obact) ||
      ID_IS_OVERRIDE_LIB(obact) || ID_IS_OVERRIDE_LIB(obact->data))
  {
    return false;
  }

  return (!ID_IS_LINKED(scene) && (dune_ob_is_in_editmode(obact) == false) &&
          (base_act->flag & BASE_SEL));
}

/* Helper for ob_convert_ex */
static Base *basedup_for_convert(
    Main *main, Graph *graph, Scene *scene, ViewLayer *view_layer, Base *base, Ob *ob)
{
  if (ob == nullptr) {
    ob = base->ob;
  }

  Ob *obn = (Ob *)dune_id_copy(main, &ob->id);
  id_us_min(&obn->id);
  graph_id_tag_update(&obn->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIM);
  dune_collection_ob_add_from(main, scene, ob, obn);

  dune_view_layer_synced_ensure(scene, view_layer);
  Base *basen = dune_view_layer_base_find(view_layer, obn);
  ed_ob_base_sel(basen, BA_SEL);
  ed_ob_base_sel(base, BA_DESEL);

  /* Ugly hack needed bc if we re-run graph with some new meta-ball objs
   * having same 'family name' as orig ones they will affect end result of meta-ball computation.
   * Until we get rid of that name-based thingy in meta-balls, that should do the trick
   * (weak but other solution (to change name of `obn`) is even worse IMHO).
   * See #65996. */
  const bool is_meta_ball = (obn->type == OB_MBALL);
  void *obdata = obn->data;
  if (is_meta_ball) {
    obn->type = OB_EMPTY;
    obn->data = nullptr;
  }

  /* Doing that here is stupid, it means we update and re-eval the whole graph every
   * time we need to dup an ob to convert it. Even worse, this is not 100% correct, since
   * we do not yet have dup'd obdata.
   * However, that is a safe solution for now. Proper, longer-term solution is to refactor
   * ob_convert_ex to:
   *  - dup all data it needs to in a first loop.
   *  - do a single update.
   *  - convert data in a second loop. */
  graph_tag_relations_update(graph);
  CustomData_MeshMasks customdata_mask_prev = scene->customdata_mask;
  CustomData_MeshMasks_update(&scene->customdata_mask, &CD_MASK_MESH);
  dune_scene_graph_update_tagged(graph, main);
  scene->customdata_mask = customdata_mask_prev;

  if (is_meta_ball) {
    obn->type = OB_MBALL;
    obn->data = obdata;
  }

  return basen;
}

static int ob_convert_ex(Cxt *C, WinOp *op)
{
  using namespace dune;
  Main *main = cxt_data_main(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);
  Base *basen = nullptr, *basact = nullptr;
  Ob *ob1, *obact = cxt_data_active_object(C);
  const short target = api_enum_get(op->ptr, "target");
  bool keep_original = api_bool_get(op->ptr, "keep_original");
  const bool do_merge_customdata = api_bool_get(op->ptr, "merge_customdata");

  const float angle = api_float_get(op->ptr, "angle");
  const int thickness = api_int_get(op->ptr, "thickness");
  const bool use_seams = api_bool_get(op->ptr, "seams");
  const bool use_faces = api_bool_get(op->ptr, "faces");
  const float offset = api_float_get(op->ptr, "offset");

  int mballConverted = 0;
  bool penConverted = false;
  bool penCurveConverted = false;

  /* don't forget multiple users! */

  {
    FOREACH_SCENE_OB_BEGIN (scene, ob) {
      ob->flag &= ~OB_DONE;

      /* flag data that's not been edited (only needed for !keep_original) */
      if (ob->data) {
        ((ID *)ob->data)->tag |= LIB_TAG_DOIT;
      }

      /* possible metaball basis is not in this scene */
      if (ob->type == OB_MBALL && target == OB_MESH) {
        if (dune_mball_is_basis(ob) == false) {
          Object *ob_basis;
          ob_basis = dune_mball_basis_find(scene, ob);
          if (ob_basis) {
            ob_basis->flag &= ~OB_DONE;
          }
        }
      }
    }
    FOREACH_SCENE_OBJECT_END;
  }

  List sel_editable_bases;
  cxt_data_sel_editable_bases(C, &sel_editable_bases);

  /* Ensure we get all meshes calc'd w a sufficient data-mask,
   * needed since re-evaling single moda causes bugs if they depend
   * on other obs data masks too, see: #50950. */
  {
    LIST_FOREACH (CollectionPointerLink *, link, &sel_editable_bases) {
      Base *base = static_cast<Base *>(link->ptr.data);
      Ob *ob = base->object;

      /* The way object type conversion works currently (enforcing conversion of *all* objects
       * using converted object-data, even some un-selected/hidden/another scene ones,
       * sounds totally bad to me.
       * However, changing this is more design than bug-fix, not to mention convoluted code below,
       * so that will be for later.
       * But at the very least, do not do that with linked IDs! */
      if ((!dune_id_is_editable(bmain, &ob->id) ||
           (ob->data && !dune_id_is_editable(bmain, static_cast<ID *>(ob->data)))) &&
          !keep_original)
      {
        keep_original = true;
        dune_report(op->reports,
                   RPT_INFO,
                   "Converting some non-editable object/object data, enforcing 'Keep Original' "
                   "option to True");
      }

      graph_id_tag_update(&base->ob->id, ID_RECALC_GEOMETRY);
    }

    CustomDataMeshMasks customdata_mask_prev = scene->customdata_mask;
    CustomDataMeshMasks_update(&scene->customdata_mask, &CD_MASK_MESH);
    dune_scene_graph_update_tagged(graph, main);
    scene->customdata_mask = customdata_mask_prev;
  }

  LIST_FOREACH (CollectionPtrLink *, link, &selected_editable_bases) {
    Ob *newob = nullptr;
    Base *base = static_cast<Base *>(link->ptr.data);
    Ob *ob = base->ob;

    if (ob->flag & OB_DONE || !IS_TAGGED(ob->data)) {
      if (ob->type != target) {
        base->flag &= ~SEL;
        ob->flag &= ~SEL;
      }

      /* obdata already modified */
      if (!IS_TAGGED(ob->data)) {
        /* When 2 objects with linked data are selected, converting both
         * would keep modifiers on all but the converted object #26003. */
        if (ob->type == OB_MESH) {
          dune_ob_free_mods(ob, 0); /* after derivedmesh calls! */
        }
        if (ob->type == OB_PEN_LEGACY) {
          dune_ob_free_mods(ob, 0); /* after derivedmesh calls! */
          dune_ob_free_shaderfx(ob, 0);
        }
      }
    }
    else if (ob->type == OB_MESH && target == OB_CURVES_LEGACY) {
      ob->flag |= OB_DONE;

      if (keep_original) {
        basen = dupbase_for_convert(main, graph, scene, view_layer, base, nullptr);
        newob = basen->ob;

        /* Decrement original mesh's usage count. */
        Mesh *me = static_cast<Mesh *>(newob->data);
        id_us_min(&me->id);

        /* Make a new copy of the mesh. */
        newob->data = dune_id_copy(main, &me->id);
      }
      else {
        newob = ob;
      }

      dune_mesh_to_curve(main, graph, scene, newob);

      if (newob->type == OB_CURVES_LEGACY) {
        dune_ob_free_mods(newob, 0); /* after derivedmesh calls! */
        if (newob->rigidbody_ob != nullptr) {
          ed_rigidbody_ob_remove(main, scene, newob);
        }
      }
    }
    else if (ob->type == OB_MESH && target == OB_GPENCIL_LEGACY) {
      ob->flag |= OB_DONE;

      /* Create a new pen ob and copy transformations. */
      ushort local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
      float loc[3], size[3], rot[3][3], eul[3];
      float matrix[4][4];
      mat4_to_loc_rot_size(loc, rot, size, ob->object_to_world);
      mat3_to_eul(eul, rot);

      Object *ob_gpencil = ED_gpencil_add_ob(C, loc, local_view_bits);
      copy_v3_v3(ob_gpencil->loc, loc);
      copy_v3_v3(ob_gpencil->rot, eul);
      copy_v3_v3(ob_gpencil->scale, size);
      unit_m4(matrix);
      /* Set object in 3D mode. */
      PenData *pd = (PenData *)ob_pen->data;
      pd->drw_mode = PEN_DRWMODE_3D;

      penConverted |= dune_pen_convert_mesh(main,
                                                graph,
                                                scene,
                                                ob_pen,
                                                ob,
                                                angle,
                                                thickness,
                                                offset,
                                                matrix,
                                                0,
                                                use_seams,
                                                use_faces,
                                                true);

      /* Remove unused materials. */
      int actcol = ob_pen->actcol;
      for (int slot = 1; slot <= ob_pen->totcol; slot++) {
        while (slot <= ob_pen->totcol && !dune_ob_material_slot_used(ob_pen, slot)) {
          ob_pen->actcol = slot;
          dune_ob_material_slot_remove(cxt_data_main(C), ob_pen);

          if (actcol >= slot) {
            actcol--;
          }
        }
      }
      ob_pen->actcol = actcol;
    }
    else if (U.experimental.use_grease_pencil_version3 && ob->type == OB_GPENCIL_LEGACY &&
             target == OB_GREASE_PENCIL)
    {
      ob->flag |= OB_DONE;

      bGPdata *gpd = static_cast<bGPdata *>(ob->data);

      if (keep_original) {
        BLI_assert_unreachable();
      }
      else {
        newob = ob;
      }

      GreasePencil *new_grease_pencil = static_cast<GreasePencil *>(
          BKE_id_new(bmain, ID_GP, newob->id.name + 2));
      newob->data = new_grease_pencil;
      newob->type = OB_GREASE_PENCIL;

      bke::greasepencil::convert::legacy_gpencil_to_grease_pencil(
          *bmain, *new_grease_pencil, *gpd);

      BKE_object_free_derived_caches(newob);
      BKE_object_free_modifiers(newob, 0);
    }
    else if (target == OB_CURVES) {
      ob->flag |= OB_DONE;

      Object *ob_eval = DEG_get_evaluated_object(depsgraph, ob);
      bke::GeometrySet geometry;
      if (ob_eval->runtime->geometry_set_eval != nullptr) {
        geometry = *ob_eval->runtime->geometry_set_eval;
      }

      if (geometry.has_curves()) {
        if (keep_original) {
          basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
          newob = basen->object;

          /* Decrement original curve's usage count. */
          Curve *legacy_curve = static_cast<Curve *>(newob->data);
          id_us_min(&legacy_curve->id);

          /* Make a copy of the curve. */
          newob->data = BKE_id_copy(bmain, &legacy_curve->id);
        }
        else {
          newob = ob;
        }

        const Curves *curves_eval = geometry.get_curves();
        Curves *new_curves = static_cast<Curves *>(BKE_id_new(bmain, ID_CV, newob->id.name + 2));

        newob->data = new_curves;
        newob->type = OB_CURVES;

        new_curves->geometry.wrap() = curves_eval->geometry.wrap();
        BKE_object_material_from_eval_data(bmain, newob, &curves_eval->id);

        BKE_object_free_derived_caches(newob);
        BKE_object_free_modifiers(newob, 0);
      }
      else {
        BKE_reportf(
            op->reports, RPT_WARNING, "Object '%s' has no evaluated curves data", ob->id.name + 2);
      }
    }
    else if (ob->type == OB_MESH && target == OB_POINTCLOUD) {
      ob->flag |= OB_DONE;

      if (keep_original) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
        newob = basen->object;

        /* Decrement original mesh's usage count. */
        Mesh *me = static_cast<Mesh *>(newob->data);
        id_us_min(&me->id);

        /* Make a new copy of the mesh. */
        newob->data = BKE_id_copy(bmain, &me->id);
      }
      else {
        newob = ob;
      }

      BKE_mesh_to_pointcloud(bmain, depsgraph, scene, newob);

      if (newob->type == OB_POINTCLOUD) {
        BKE_object_free_modifiers(newob, 0); /* after derivedmesh calls! */
        ED_rigidbody_object_remove(bmain, scene, newob);
      }
    }
    else if (ob->type == OB_MESH) {
      ob->flag |= OB_DONE;

      if (keep_original) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
        newob = basen->object;

        /* Decrement original mesh's usage count. */
        Mesh *me = static_cast<Mesh *>(newob->data);
        id_us_min(&me->id);

        /* Make a new copy of the mesh. */
        newob->data = BKE_id_copy(bmain, &me->id);
      }
      else {
        newob = ob;
        DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION);
      }

      /* make new mesh data from the original copy */
      /* NOTE: get the mesh from the original, not from the copy in some
       * cases this doesn't give correct results (when MDEF is used for eg)
       */
      const Object *ob_eval = DEG_get_evaluated_object(depsgraph, ob);
      const Mesh *mesh_eval = BKE_object_get_evaluated_mesh(ob_eval);
      Mesh *new_mesh = mesh_eval ? BKE_mesh_copy_for_eval(mesh_eval) :
                                   BKE_mesh_new_nomain(0, 0, 0, 0);
      BKE_object_material_from_eval_data(bmain, newob, &new_mesh->id);
      /* Anonymous attributes shouldn't be available on the applied geometry. */
      new_mesh->attributes_for_write().remove_anonymous();
      if (do_merge_customdata) {
        BKE_mesh_merge_customdata_for_apply_modifier(new_mesh);
      }

      Mesh *ob_data_mesh = (Mesh *)newob->data;
      BKE_mesh_nomain_to_mesh(new_mesh, ob_data_mesh, newob);

      BKE_object_free_modifiers(newob, 0); /* after derivedmesh calls! */
    }
    else if (ob->type == OB_FONT) {
      ob->flag |= OB_DONE;

      if (keep_original) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
        newob = basen->object;

        /* Decrement original curve's usage count. */
        id_us_min(&((Curve *)newob->data)->id);

        /* Make a new copy of the curve. */
        newob->data = BKE_id_copy(bmain, static_cast<ID *>(ob->data));
      }
      else {
        newob = ob;
      }

      Curve *cu = static_cast<Curve *>(newob->data);

      Object *ob_eval = DEG_get_evaluated_object(depsgraph, ob);
      BKE_vfont_to_curve_ex(ob_eval,
                            static_cast<Curve *>(ob_eval->data),
                            FO_EDIT,
                            &cu->nurb,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr);

      newob->type = OB_CURVES_LEGACY;
      cu->type = OB_CURVES_LEGACY;

      if (cu->vfont) {
        id_us_min(&cu->vfont->id);
        cu->vfont = nullptr;
      }
      if (cu->vfontb) {
        id_us_min(&cu->vfontb->id);
        cu->vfontb = nullptr;
      }
      if (cu->vfonti) {
        id_us_min(&cu->vfonti->id);
        cu->vfonti = nullptr;
      }
      if (cu->vfontbi) {
        id_us_min(&cu->vfontbi->id);
        cu->vfontbi = nullptr;
      }

      if (!keep_original) {
        /* other users */
        if (ID_REAL_USERS(&cu->id) > 1) {
          for (ob1 = static_cast<Object *>(bmain->objects.first); ob1;
               ob1 = static_cast<Object *>(ob1->id.next))
          {
            if (ob1->data == ob->data) {
              ob1->type = OB_CURVES_LEGACY;
              DEG_id_tag_update(&ob1->id,
                                ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION);
            }
          }
        }
      }

      LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
        nu->charidx = 0;
      }

      cu->flag &= ~CU_3D;
      BKE_curve_dimension_update(cu);

      if (target == OB_MESH) {
        /* No assumption should be made that the resulting objects is a mesh, as conversion can
         * fail. */
        object_data_convert_curve_to_mesh(bmain, depsgraph, newob);
        /* Meshes doesn't use the "curve cache". */
        BKE_object_free_curve_cache(newob);
      }
      else if (target == OB_GPENCIL_LEGACY) {
        ushort local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
        Object *ob_gpencil = ED_gpencil_add_object(C, newob->loc, local_view_bits);
        copy_v3_v3(ob_gpencil->rot, newob->rot);
        copy_v3_v3(ob_gpencil->scale, newob->scale);
        BKE_gpencil_convert_curve(bmain, scene, ob_gpencil, newob, false, 1.0f, 0.0f);
        gpencilConverted = true;
        gpencilCurveConverted = true;
        basen = nullptr;
      }
    }
    else if (ELEM(ob->type, OB_CURVES_LEGACY, OB_SURF)) {
      ob->flag |= OB_DONE;

      if (target == OB_MESH) {
        if (keep_original) {
          basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
          newob = basen->object;

          /* Decrement original curve's usage count. */
          id_us_min(&((Curve *)newob->data)->id);

          /* make a new copy of the curve */
          newob->data = BKE_id_copy(bmain, static_cast<ID *>(ob->data));
        }
        else {
          newob = ob;
        }

        /* No assumption should be made that the resulting objects is a mesh, as conversion can
         * fail. */
        object_data_convert_curve_to_mesh(bmain, depsgraph, newob);
        /* Meshes don't use the "curve cache". */
        BKE_object_free_curve_cache(newob);
      }
      else if (target == OB_GPENCIL_LEGACY) {
        if (ob->type != OB_CURVES_LEGACY) {
          ob->flag &= ~OB_DONE;
          BKE_report(op->reports, RPT_ERROR, "Convert Surfaces to Grease Pencil is not supported");
        }
        else {
          /* Create a new grease pencil object and copy transformations.
           * Nurbs Surface are not supported.
           */
          ushort local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
          Object *ob_gpencil = ED_gpencil_add_object(C, ob->loc, local_view_bits);
          copy_v3_v3(ob_gpencil->rot, ob->rot);
          copy_v3_v3(ob_gpencil->scale, ob->scale);
          BKE_gpencil_convert_curve(bmain, scene, ob_gpencil, ob, false, 1.0f, 0.0f);
          gpencilConverted = true;
        }
      }
    }
    else if (ob->type == OB_MBALL && target == OB_MESH) {
      Object *baseob;

      base->flag &= ~BASE_SELECTED;
      ob->base_flag &= ~BASE_SELECTED;

      baseob = BKE_mball_basis_find(scene, ob);

      if (ob != baseob) {
        /* If mother-ball is converting it would be marked as done later. */
        ob->flag |= OB_DONE;
      }

      if (!(baseob->flag & OB_DONE)) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, baseob);
        newob = basen->object;

        MetaBall *mb = static_cast<MetaBall *>(newob->data);
        id_us_min(&mb->id);

        /* Find the evaluated mesh of the basis metaball object. */
        Object *object_eval = DEG_get_evaluated_object(depsgraph, baseob);
        Mesh *mesh = BKE_mesh_new_from_object_to_bmain(bmain, depsgraph, object_eval, true);

        id_us_plus(&mesh->id);
        newob->data = mesh;
        newob->type = OB_MESH;

        if (obact->type == OB_MBALL) {
          basact = basen;
        }

        baseob->flag |= OB_DONE;
        mballConverted = 1;
      }
    }
    else if (ob->type == OB_POINTCLOUD && target == OB_MESH) {
      ob->flag |= OB_DONE;

      if (keep_original) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
        newob = basen->object;

        /* Decrement original point cloud's usage count. */
        PointCloud *pointcloud = static_cast<PointCloud *>(newob->data);
        id_us_min(&pointcloud->id);

        /* Make a new copy of the point cloud. */
        newob->data = BKE_id_copy(bmain, &pointcloud->id);
      }
      else {
        newob = ob;
      }

      BKE_pointcloud_to_mesh(bmain, depsgraph, scene, newob);

      if (newob->type == OB_MESH) {
        BKE_object_free_modifiers(newob, 0); /* after derivedmesh calls! */
        ED_rigidbody_object_remove(bmain, scene, newob);
      }
    }
    else if (ob->type == OB_CURVES && target == OB_MESH) {
      ob->flag |= OB_DONE;

      Object *ob_eval = DEG_get_evaluated_object(depsgraph, ob);
      bke::GeometrySet geometry;
      if (ob_eval->runtime->geometry_set_eval != nullptr) {
        geometry = *ob_eval->runtime->geometry_set_eval;
      }

      if (keep_original) {
        basen = duplibase_for_convert(bmain, depsgraph, scene, view_layer, base, nullptr);
        newob = basen->object;

        Curves *curves = static_cast<Curves *>(newob->data);
        id_us_min(&curves->id);

        newob->data = BKE_id_copy(bmain, &curves->id);
      }
      else {
        newob = ob;
      }

      Mesh *new_mesh = static_cast<Mesh *>(BKE_id_new(bmain, ID_ME, newob->id.name + 2));
      newob->data = new_mesh;
      newob->type = OB_MESH;

      if (const Mesh *mesh_eval = geometry.get_mesh()) {
        BKE_mesh_nomain_to_mesh(BKE_mesh_copy_for_eval(mesh_eval), new_mesh, newob);
        BKE_object_material_from_eval_data(bmain, newob, &mesh_eval->id);
        new_mesh->attributes_for_write().remove_anonymous();
      }
      else if (const Curves *curves_eval = geometry.get_curves()) {
        bke::AnonymousAttributePropagationInfo propagation_info;
        propagation_info.propagate_all = false;
        Mesh *mesh = bke::curve_to_wire_mesh(curves_eval->geometry.wrap(), propagation_info);
        if (!mesh) {
          mesh = BKE_mesh_new_nomain(0, 0, 0, 0);
        }
        BKE_mesh_nomain_to_mesh(mesh, new_mesh, newob);
        BKE_object_material_from_eval_data(bmain, newob, &curves_eval->id);
      }
      else {
        BKE_reportf(op->reports,
                    RPT_WARNING,
                    "Object '%s' has no evaluated mesh or curves data",
                    ob->id.name + 2);
      }

      BKE_object_free_derived_caches(newob);
      BKE_object_free_modifiers(newob, 0);
    }
    else {
      continue;
    }

    /* Ensure new object has consistent material data with its new obdata. */
    if (newob) {
      BKE_object_materials_test(bmain, newob, static_cast<ID *>(newob->data));
    }

    /* tag obdata if it was been changed */

    /* If the original object is active then make this object active */
    if (basen) {
      if (ob == obact) {
        /* Store new active base to update view layer. */
        basact = basen;
      }

      basen = nullptr;
    }

    if (!keep_original && (ob->flag & OB_DONE)) {
      /* NOTE: Tag transform for update because object parenting to curve with path is handled
       * differently from all other cases. Converting curve to mesh and mesh to curve will likely
       * affect the way children are evaluated.
       * It is not enough to tag only geometry and rely on the curve parenting relations because
       * this relation is lost when curve is converted to mesh. */
      DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY | ID_RECALC_TRANSFORM);
      ((ID *)ob->data)->tag &= ~LIB_TAG_DOIT; /* flag not to convert this datablock again */
    }
  }
  BLI_freelistN(&selected_editable_bases);

  if (!keep_original) {
    if (mballConverted) {
      /* We need to remove non-basis MBalls first, otherwise we won't be able to detect them if
       * their basis happens to be removed first. */
      FOREACH_SCENE_OBJECT_BEGIN (scene, ob_mball) {
        if (ob_mball->type == OB_MBALL) {
          Object *ob_basis = nullptr;
          if (!BKE_mball_is_basis(ob_mball) &&
              ((ob_basis = BKE_mball_basis_find(scene, ob_mball)) && (ob_basis->flag & OB_DONE)))
          {
            ED_object_base_free_and_unlink(bmain, scene, ob_mball);
          }
        }
      }
      FOREACH_SCENE_OBJECT_END;
      FOREACH_SCENE_OBJECT_BEGIN (scene, ob_mball) {
        if (ob_mball->type == OB_MBALL) {
          if (ob_mball->flag & OB_DONE) {
            if (BKE_mball_is_basis(ob_mball)) {
              ED_object_base_free_and_unlink(bmain, scene, ob_mball);
            }
          }
        }
      }
      FOREACH_SCENE_OBJECT_END;
    }
    /* Remove curves and meshes converted to Grease Pencil object. */
    if (gpencilConverted) {
      FOREACH_SCENE_OBJECT_BEGIN (scene, ob_delete) {
        if (ELEM(ob_delete->type, OB_CURVES_LEGACY, OB_MESH)) {
          if (ob_delete->flag & OB_DONE) {
            ED_object_base_free_and_unlink(bmain, scene, ob_delete);
          }
        }
      }
      FOREACH_SCENE_OBJECT_END;
    }
  }
  else {
    /* Remove Text curves converted to Grease Pencil object to avoid duplicated curves. */
    if (gpencilCurveConverted) {
      FOREACH_SCENE_OBJECT_BEGIN (scene, ob_delete) {
        if (ELEM(ob_delete->type, OB_CURVES_LEGACY) && (ob_delete->flag & OB_DONE)) {
          ED_object_base_free_and_unlink(bmain, scene, ob_delete);
        }
      }
      FOREACH_SCENE_OBJECT_END;
    }
  }

  // XXX: ED_object_editmode_enter(C, 0);
  // XXX: exit_editmode(C, EM_FREEDATA|); /* free data, but no undo. */

  if (basact) {
    /* active base was changed */
    ED_object_base_activate(C, basact);
    view_layer->basact = basact;
  }
  else {
    BKE_view_layer_synced_ensure(scene, view_layer);
    Object *object = BKE_view_layer_active_object_get(view_layer);
    if (object->flag & OB_DONE) {
      WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, object);
      WM_event_add_notifier(C, NC_OBJECT | ND_DATA, object);
    }
  }

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

  return OPERATOR_FINISHED;
}

static void object_convert_ui(bContext * /*C*/, wmOperator *op)
{
  uiLayout *layout = op->layout;

  uiLayoutSetPropSep(layout, true);

  uiItemR(layout, op->ptr, "target", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "keep_original", UI_ITEM_NONE, nullptr, ICON_NONE);

  const int target = RNA_enum_get(op->ptr, "target");
  if (target == OB_MESH) {
    uiItemR(layout, op->ptr, "merge_customdata", UI_ITEM_NONE, nullptr, ICON_NONE);
  }
  else if (target == OB_GPENCIL_LEGACY) {
    uiItemR(layout, op->ptr, "thickness", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "angle", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "offset", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "seams", UI_ITEM_NONE, nullptr, ICON_NONE);
    uiItemR(layout, op->ptr, "faces", UI_ITEM_NONE, nullptr, ICON_NONE);
  }
}

void OBJECT_OT_convert(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Convert To";
  ot->description = "Convert selected objects to another type";
  ot->idname = "OBJECT_OT_convert";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = object_convert_exec;
  ot->poll = object_convert_poll;
  ot->ui = object_convert_ui;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_enum(
      ot->srna, "target", convert_target_items, OB_MESH, "Target", "Type of object to convert to");
  prop = RNA_def_boolean(ot->srna,
                         "keep_original",
                         false,
                         "Keep Original",
                         "Keep original objects instead of replacing them");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_OBJECT);

  RNA_def_boolean(
      ot->srna,
      "merge_customdata",
      true,
      "Merge UVs",
      "Merge UV coordinates that share a vertex to account for imprecision in some modifiers");

  prop = RNA_def_float_rotation(ot->srna,
                                "angle",
                                0,
                                nullptr,
                                DEG2RADF(0.0f),
                                DEG2RADF(180.0f),
                                "Threshold Angle",
                                "Threshold to determine ends of the strokes",
                                DEG2RADF(0.0f),
                                DEG2RADF(180.0f));
  RNA_def_property_float_default(prop, DEG2RADF(70.0f));

  RNA_def_int(ot->srna, "thickness", 5, 1, 100, "Thickness", "", 1, 100);
  RNA_def_boolean(ot->srna, "seams", false, "Only Seam Edges", "Convert only seam edges");
  RNA_def_boolean(ot->srna, "faces", true, "Export Faces", "Export faces as filled strokes");
  RNA_def_float_distance(ot->srna,
                         "offset",
                         0.01f,
                         0.0,
                         OBJECT_ADD_SIZE_MAXF,
                         "Stroke Offset",
                         "Offset strokes from fill",
                         0.0,
                         100.00);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Duplicate Object Operator
 * \{ */

static void object_add_sync_base_collection(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base_src, Object *object_new)
{
  if ((base_src != nullptr) && (base_src->flag & BASE_ENABLED_AND_MAYBE_VISIBLE_IN_VIEWPORT)) {
    BKE_collection_object_add_from(bmain, scene, base_src->object, object_new);
  }
  else {
    LayerCollection *layer_collection = BKE_layer_collection_get_active(view_layer);
    BKE_collection_object_add(bmain, layer_collection->collection, object_new);
  }
}

static void object_add_sync_local_view(Base *base_src, Base *base_new)
{
  base_new->local_view_bits = base_src->local_view_bits;
}

static void object_add_sync_rigid_body(Main *bmain, Object *object_src, Object *object_new)
{
  /* 1) duplis should end up in same collection as the original
   * 2) Rigid Body sim participants MUST always be part of a collection...
   */
  /* XXX: is 2) really a good measure here? */
  if (object_src->rigidbody_object || object_src->rigidbody_constraint) {
    LISTBASE_FOREACH (Collection *, collection, &bmain->collections) {
      if (BKE_collection_has_object(collection, object_src)) {
        BKE_collection_object_add(bmain, collection, object_new);
      }
    }
  }
}

/**
 * - Assumes `id.new` is correct.
 * - Leaves selection of base/object unaltered.
 * - Sets #ID.newid pointers.
 */
static void object_add_duplicate_internal(Main *bmain,
                                          Object *ob,
                                          const eDupli_ID_Flags dupflag,
                                          const eLibIDDuplicateFlags duplicate_options,
                                          Object **r_ob_new)
{
  if (ob->mode & OB_MODE_POSE) {
    return;
  }

  Object *obn = static_cast<Object *>(
      ID_NEW_SET(ob, BKE_object_duplicate(bmain, ob, dupflag, duplicate_options)));
  if (r_ob_new) {
    *r_ob_new = obn;
  }
  DEG_id_tag_update(&obn->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  return;
}

static Base *object_add_duplicate_internal(Main *bmain,
                                           Scene *scene,
                                           ViewLayer *view_layer,
                                           Object *ob,
                                           const eDupli_ID_Flags dupflag,
                                           const eLibIDDuplicateFlags duplicate_options,
                                           Object **r_ob_new)
{
  Object *object_new = nullptr;
  object_add_duplicate_internal(bmain, ob, dupflag, duplicate_options, &object_new);
  if (r_ob_new) {
    *r_ob_new = object_new;
  }
  if (object_new == nullptr) {
    return nullptr;
  }

  BKE_view_layer_synced_ensure(scene, view_layer);
  Base *base_src = BKE_view_layer_base_find(view_layer, ob);
  object_add_sync_base_collection(bmain, scene, view_layer, base_src, object_new);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Base *base_new = BKE_view_layer_base_find(view_layer, object_new);
  if (base_src && base_new) {
    object_add_sync_local_view(base_src, base_new);
  }
  object_add_sync_rigid_body(bmain, ob, object_new);
  return base_new;
}

Base *ED_object_add_duplicate(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base, const eDupli_ID_Flags dupflag)
{
  Base *basen;
  Object *ob;

  basen = object_add_duplicate_internal(bmain,
                                        scene,
                                        view_layer,
                                        base->object,
                                        dupflag,
                                        LIB_ID_DUPLICATE_IS_SUBPROCESS |
                                            LIB_ID_DUPLICATE_IS_ROOT_ID,
                                        nullptr);
  if (basen == nullptr) {
    return nullptr;
  }

  ob = basen->object;

  /* Link own references to the newly duplicated data #26816.
   * Note that this function can be called from edit-mode code, in which case we may have to
   * enforce remapping obdata (by default this is forbidden in edit mode). */
  const int remap_flag = BKE_object_is_in_editmode(ob) ? ID_REMAP_FORCE_OBDATA_IN_EDITMODE : 0;
  BKE_libblock_relink_to_newid(bmain, &ob->id, remap_flag);

  /* Correct but the caller must do this. */
  // DAG_relations_tag_update(bmain);

  if (ob->data != nullptr) {
    DEG_id_tag_update_ex(bmain, (ID *)ob->data, ID_RECALC_EDITORS);
  }

  BKE_main_id_newptr_and_tag_clear(bmain);

  return basen;
}

/* contextual operator dupli */
static int duplicate_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const bool linked = RNA_boolean_get(op->ptr, "linked");
  const eDupli_ID_Flags dupflag = (linked) ? (eDupli_ID_Flags)0 : (eDupli_ID_Flags)U.dupflag;

  /* We need to handle that here ourselves, because we may duplicate several objects, in which case
   * we also want to remap pointers between those... */
  BKE_main_id_newptr_and_tag_clear(bmain);

  /* Duplicate the selected objects, remember data needed to process
   * after the sync. */
  struct DuplicateObjectLink {
    Base *base_src = nullptr;
    Object *object_new = nullptr;

    DuplicateObjectLink(Base *base_src) : base_src(base_src) {}
  };

  blender::Vector<DuplicateObjectLink> object_base_links;
  CTX_DATA_BEGIN (C, Base *, base, selected_bases) {
    object_base_links.append(DuplicateObjectLink(base));
  }
  CTX_DATA_END;

  bool new_objects_created = false;
  for (DuplicateObjectLink &link : object_base_links) {
    object_add_duplicate_internal(bmain,
                                  link.base_src->object,
                                  dupflag,
                                  LIB_ID_DUPLICATE_IS_SUBPROCESS | LIB_ID_DUPLICATE_IS_ROOT_ID,
                                  &link.object_new);
    if (link.object_new) {
      new_objects_created = true;
    }
  }

  if (!new_objects_created) {
    return OPERATOR_CANCELLED;
  }

  /* Sync that could tag the view_layer out of sync. */
  for (DuplicateObjectLink &link : object_base_links) {
    /* note that this is safe to do with this context iterator,
     * the list is made in advance */
    ED_object_base_select(link.base_src, BA_DESELECT);
    if (link.object_new) {
      object_add_sync_base_collection(bmain, scene, view_layer, link.base_src, link.object_new);
      object_add_sync_rigid_body(bmain, link.base_src->object, link.object_new);
    }
  }

  /* Sync the view layer. Everything else should not tag the view_layer out of sync. */
  BKE_view_layer_synced_ensure(scene, view_layer);
  const Base *active_base = BKE_view_layer_active_base_get(view_layer);
  for (DuplicateObjectLink &link : object_base_links) {
    if (!link.object_new) {
      continue;
    }

    Base *base_new = BKE_view_layer_base_find(view_layer, link.object_new);
    BLI_assert(base_new);
    ED_object_base_select(base_new, BA_SELECT);
    if (active_base == link.base_src) {
      ED_object_base_activate(C, base_new);
    }

    if (link.object_new->data) {
      DEG_id_tag_update(static_cast<ID *>(link.object_new->data), 0);
    }

    object_add_sync_local_view(link.base_src, base_new);
  }

  /* Note that this will also clear newid pointers and tags. */
  copy_object_set_idnew(C);

  ED_outliner_select_sync_from_object_tag(C);

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE | ID_RECALC_SELECT);

  WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

  return OPERATOR_FINISHED;
}

void OBJECT_OT_duplicate(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Duplicate Objects";
  ot->description = "Duplicate selected objects";
  ot->idname = "OBJECT_OT_duplicate";

  /* api callbacks */
  ot->exec = duplicate_exec;
  ot->poll = ED_operator_objectmode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  prop = RNA_def_boolean(ot->srna,
                         "linked",
                         false,
                         "Linked",
                         "Duplicate object but not object data, linking to the original data");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);

  prop = RNA_def_enum(
      ot->srna, "mode", rna_enum_transform_mode_type_items, TFM_TRANSLATION, "Mode", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Add Named Object Operator
 *
 * Use for drag & drop.
 * \{ */

static int object_add_named_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const bool linked = RNA_boolean_get(op->ptr, "linked");
  const eDupli_ID_Flags dupflag = (linked) ? (eDupli_ID_Flags)0 : (eDupli_ID_Flags)U.dupflag;

  /* Find object, create fake base. */

  Object *ob = reinterpret_cast<Object *>(
      WM_operator_properties_id_lookup_from_name_or_session_uuid(bmain, op->ptr, ID_OB));

  if (ob == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Object not found");
    return OPERATOR_CANCELLED;
  }

  /* prepare dupli */
  Base *basen = object_add_duplicate_internal(
      bmain,
      scene,
      view_layer,
      ob,
      dupflag,
      /* Sub-process flag because the new-ID remapping (#BKE_libblock_relink_to_newid()) in this
       * function will only work if the object is already linked in the view layer, which is not
       * the case here. So we have to do the new-ID relinking ourselves
       * (#copy_object_set_idnew()).
       */
      LIB_ID_DUPLICATE_IS_SUBPROCESS | LIB_ID_DUPLICATE_IS_ROOT_ID,
      nullptr);

  if (basen == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Object could not be duplicated");
    return OPERATOR_CANCELLED;
  }

  basen->object->visibility_flag &= ~OB_HIDE_VIEWPORT;
  /* Do immediately, as #copy_object_set_idnew() below operates on visible objects. */
  BKE_base_eval_flags(basen);

  /* object_add_duplicate_internal() doesn't deselect other objects, unlike object_add_common() or
   * BKE_view_layer_base_deselect_all(). */
  ED_object_base_deselect_all(scene, view_layer, nullptr, SEL_DESELECT);
  ED_object_base_select(basen, BA_SELECT);
  ED_object_base_activate(C, basen);

  copy_object_set_idnew(C);

  /* TODO(sergey): Only update relations for the current scene. */
  DEG_relations_tag_update(bmain);

  DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
  WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);
  ED_outliner_select_sync_from_object_tag(C);

  PropertyRNA *prop_matrix = RNA_struct_find_property(op->ptr, "matrix");
  if (RNA_property_is_set(op->ptr, prop_matrix)) {
    Object *ob_add = basen->object;
    RNA_property_float_get_array(op->ptr, prop_matrix, &ob_add->object_to_world[0][0]);
    BKE_object_apply_mat4(ob_add, ob_add->object_to_world, true, true);

    DEG_id_tag_update(&ob_add->id, ID_RECALC_TRANSFORM);
  }
  else if (CTX_wm_region_view3d(C)) {
    int mval[2];
    if (object_add_drop_xy_get(C, op, &mval)) {
      ED_object_location_from_view(C, basen->object->loc);
      ED_view3d_cursor3d_position(C, mval, false, basen->object->loc);
    }
  }

  return OPERATOR_FINISHED;
}

void OBJECT_OT_add_named(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Object";
  ot->description = "Add named object";
  ot->idname = "OBJECT_OT_add_named";

  /* api callbacks */
  ot->invoke = object_add_drop_xy_generic_invoke;
  ot->exec = object_add_named_exec;
  ot->poll = ED_operator_objectmode_poll_msg;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  PropertyRNA *prop;
  RNA_def_boolean(ot->srna,
                  "linked",
                  false,
                  "Linked",
                  "Duplicate object but not object data, linking to the original data");

  WM_operator_properties_id_lookup(ot, true);

  prop = RNA_def_float_matrix(
      ot->srna, "matrix", 4, 4, nullptr, 0.0f, 0.0f, "Matrix", "", 0.0f, 0.0f);
  RNA_def_property_flag(prop, (PropertyFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));

  object_add_drop_xy_props(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Transform Object to Mouse Operator
 * \{ */

/**
 * Alternate behavior for dropping an asset that positions the appended object(s).
 */
static int object_transform_to_mouse_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  Object *ob = reinterpret_cast<Object *>(
      WM_operator_properties_id_lookup_from_name_or_session_uuid(bmain, op->ptr, ID_OB));

  if (!ob) {
    BKE_view_layer_synced_ensure(scene, view_layer);
    ob = BKE_view_layer_active_object_get(view_layer);
  }

  if (ob == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Object not found");
    return OPERATOR_CANCELLED;
  }

  /* Don't transform a linked object. There's just nothing to do here in this case, so return
   * #OPERATOR_FINISHED. */
  if (!BKE_id_is_editable(bmain, &ob->id)) {
    return OPERATOR_FINISHED;
  }

  /* Ensure the locations are updated so snap reads the evaluated active location. */
  CTX_data_ensure_evaluated_depsgraph(C);

  PropertyRNA *prop_matrix = RNA_struct_find_property(op->ptr, "matrix");
  if (RNA_property_is_set(op->ptr, prop_matrix)) {
    ObjectsInViewLayerParams params = {0};
    uint objects_len;
    Object **objects = BKE_view_layer_array_selected_objects_params(
        view_layer, nullptr, &objects_len, &params);

    float matrix[4][4];
    RNA_property_float_get_array(op->ptr, prop_matrix, &matrix[0][0]);

    float mat_src_unit[4][4];
    float mat_dst_unit[4][4];
    float final_delta[4][4];

    normalize_m4_m4(mat_src_unit, ob->object_to_world);
    normalize_m4_m4(mat_dst_unit, matrix);
    invert_m4(mat_src_unit);
    mul_m4_m4m4(final_delta, mat_dst_unit, mat_src_unit);

    ED_object_xform_array_m4(objects, objects_len, final_delta);

    MEM_freeN(objects);
  }
  else if (CTX_wm_region_view3d(C)) {
    int mval[2];
    if (object_add_drop_xy_get(C, op, &mval)) {
      float cursor[3];
      ED_object_location_from_view(C, cursor);
      ED_view3d_cursor3d_position(C, mval, false, cursor);

      /* Use the active objects location since this is the ID which the user selected to drop.
       *
       * This transforms all selected objects, so that dropping a single object which links in
       * other objects will have their relative transformation preserved.
       * For example a child/parent relationship or other objects used with a boolean modifier.
       *
       * The caller is responsible for ensuring the selection state gives useful results.
       * Link/append does this using #FILE_AUTOSELECT. */
      ED_view3d_snap_selected_to_location(C, cursor, V3D_AROUND_ACTIVE);
    }
  }

  return OPERATOR_FINISHED;
}

void OBJECT_OT_transform_to_mouse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Place Object Under Mouse";
  ot->description = "Snap selected item(s) to the mouse location";
  ot->idname = "OBJECT_OT_transform_to_mouse";

  /* api callbacks */
  ot->invoke = object_add_drop_xy_generic_invoke;
  ot->exec = object_transform_to_mouse_exec;
  ot->poll = ED_operator_objectmode_poll_msg;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  PropertyRNA *prop;
  prop = RNA_def_string(
      ot->srna,
      "name",
      nullptr,
      MAX_ID_NAME - 2,
      "Name",
      "Object name to place (uses the active object when this and 'session_uuid' are unset)");
  RNA_def_property_flag(prop, (PropertyFlag)(PROP_SKIP_SAVE | PROP_HIDDEN));
  prop = RNA_def_int(ot->srna,
                     "session_uuid",
                     0,
                     INT32_MIN,
                     INT32_MAX,
                     "Session UUID",
                     "Session UUID of the object to place (uses the active object when this and "
                     "'name' are unset)",
                     INT32_MIN,
                     INT32_MAX);
  RNA_def_property_flag(prop, (PropertyFlag)(PROP_SKIP_SAVE | PROP_HIDDEN));

  prop = RNA_def_float_matrix(
      ot->srna, "matrix", 4, 4, nullptr, 0.0f, 0.0f, "Matrix", "", 0.0f, 0.0f);
  RNA_def_property_flag(prop, (PropertyFlag)(PROP_HIDDEN | PROP_SKIP_SAVE));

  object_add_drop_xy_props(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Join Object Operator
 * \{ */

static bool object_join_poll(bContext *C)
{
  Object *ob = CTX_data_active_object(C);

  if (ob == nullptr || ob->data == nullptr || ID_IS_LINKED(ob) || ID_IS_OVERRIDE_LIBRARY(ob) ||
      ID_IS_OVERRIDE_LIBRARY(ob->data))
  {
    return false;
  }

  if (ELEM(ob->type, OB_MESH, OB_CURVES_LEGACY, OB_SURF, OB_ARMATURE, OB_GPENCIL_LEGACY)) {
    return true;
  }
  return false;
}

static int object_join_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Object *ob = CTX_data_active_object(C);

  if (ob->mode & OB_MODE_EDIT) {
    BKE_report(op->reports, RPT_ERROR, "This data does not support joining in edit mode");
    return OPERATOR_CANCELLED;
  }
  if (BKE_object_obdata_is_libdata(ob)) {
    BKE_report(op->reports, RPT_ERROR, "Cannot edit external library data");
    return OPERATOR_CANCELLED;
  }
  if (!BKE_lib_override_library_id_is_user_deletable(bmain, &ob->id)) {
    dune_reportf(op->reports,
                RPT_WARNING,
                "Cannot edit object '%s' as it is used by override collections",
                ob->id.name + 2);
    return OP_CANCELLED;
  }

  if (ob->type == OB_PEN_LEGACY) {
    PenData *pd = (PenData *)ob->data;
    if ((!pd) || PEN_ANY_MODE(gpd)) {
      dune_report(op->reports, RPT_ERROR, "This data does not support joining in this mode");
      return OP_CANCELLED;
    }
  }

  int ret = OP_CANCELLED;
  if (ob->type == OB_MESH) {
    ret = ed_mesh_join_obs_ex(C, op);
  }
  else if (ELEM(ob->type, OB_CURVES_LEGACY, OB_SURF)) {
    ret = ed_curve_join_obs_ex(C, op);
  }
  else if (ob->type == OB_ARMATURE) {
    ret = ed_armature_join_obs_ex(C, op);
  }
  else if (ob->type == OB_PEN_LEGACY) {
    ret = ed_pen_join_obs_ex(C, op);
  }

  if (ret & OP_FINISHED) {
    /* Even though internally failure to invert is accounted for with a fallback,
     * show a warning since the result may not be what the user expects. See #80077.
     *
     * Failure to invert the matrix is typically caused by zero scaled axes
     * (which can be caused by constraints, even if the input scale isn't zero).
     *
     * Internally the join functions use #invert_m4_m4_safe_ortho which creates
     * an inevitable matrix from one that has one or more degenerate axes.
     *
     * In most cases we don't worry about special handling for non-inevitable matrices however for
     * joining objs there may be flat 2D objects where it's not obvious the scale is zero.
     * In this case, using #invert_m4_m4_safe_ortho works as well as we can expect,
     * joining the contents, flattening on the axis that's zero scaled.
     * If the zero scale is removed, the data on this axis remains un-scaled
     * (something that wouldn't work for #minvert_m4_m4_safe). */
    float imat_test[4][4];
    if (!invert_m4_m4(imat_test, ob->ob_to_world)) {
      dune_report(op->reports,
                 RPT_WARNING,
                 "Active ob final transform has one or more zero scaled axes");
    }
  }

  return ret;
}

void OB_OT_join(WinOpType *ot)
{
  /* ids */
  ot->name = "Join";
  ot->description = "Join sel obs into active ob";
  ot->idname = "OB_OT_join";

  /* api cbs */
  ot->ex = ob_join_ex;
  ot->poll = ob_join_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Join as Shape Key Op */
static bool join_shapes_poll(Cxt *C)
{
  Ob *ob = cxt_data_active_ob(C);

  if (ob == nullptr || ob->data == nullptr || ID_IS_LINKED(ob) || ID_IS_OVERRIDE_LIBRARY(ob) ||
      ID_IS_OVERRIDE_LIB(ob->data))
  {
    return false;
  }

  /* only meshes supported at the moment */
  if (ob->type == OB_MESH) {
    return ed_op_screenactive(C);
  }
  return false;
}

static int join_shapes_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Ob *ob = cxt_data_active_ob(C);

  if (ob->mode & OB_MODE_EDIT) {
    dune_report(op->reports, RPT_ERROR, "This data does not support joining in edit mode");
    return OP_CANCELLED;
  }
  if (dune_ob_obdata_is_libdata(ob)) {
    dune_report(op->reports, RPT_ERROR, "Cannot edit external lib data");
    return OP_CANCELLED;
  }
  if (!dune_lib_override_lib_id_is_user_deletable(main, &ob->id)) {
    dune_reportf(op->reports,
                RPT_WARNING,
                "Cannot edit object '%s' as it is used by override collections",
                ob->id.name + 2);
    return OP_CANCELLED;
  }

  if (ob->type == OB_MESH) {
    return ed_mesh_shapes_join_obs_ex(C, op);
  }

  return OP_CANCELLED;
}

void OB_OT_join_shapes(WinOpType *ot)
{
  /* ids */
  ot->name = "Join as Shapes";
  ot->description = "Copy the current resulting shape of another selected object to this one";
  ot->idname = "OBJECT_OT_join_shapes";

  /* api callbacks */
  ot->exec = join_shapes_exec;
  ot->poll = join_shapes_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
