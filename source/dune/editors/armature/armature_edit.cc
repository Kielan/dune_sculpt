/* Armature EditMode tools transforms, chain based editing, and other settings. */
#include "types_armature.h"
#include "types_constraint.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lang.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "dune_action.h"
#include "dune_armature.hh"
#include "dune_constraint.h"
#include "dune_cxt.hh"
#include "dune_global.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_ob.hh"
#include "dune_report.h"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_outliner.hh"
#include "ed_screen.hh"
#include "ed_view3d.hh"

#include "anim_bone_collections.hh"

#include "graph.hh"

#include "armature_intern.h"

/* Ob Tools Public API */
/* These fns are exported to the Ob module to be called from the tools there */
void ed_armature_edit_transform(Armature *arm, const float mat[4][4], const bool do_props)
{
  float scale = mat4_to_scale(mat); /* store the scale of the matrix here to use on envelopes */
  float mat3[3][3];

  copy_m3_m4(mat3, mat);
  normalize_m3(mat3);
  /* Do the rotations */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    float tmat[3][3];

    /* find the current bone's roll matrix */
    ed_armature_ebone_to_mat3(ebone, tmat);

    /* transform the roll matrix */
    mul_m3_m3m3(tmat, mat3, tmat);

    /* transform the bone */
    mul_m4_v3(mat, ebone->head);
    mul_m4_v3(mat, ebone->tail);

    /* apply the transformed roll back */
    mat3_to_vec_roll(tmat, nullptr, &ebone->roll);

    if (do_props) {
      ebone->rad_head *= scale;
      ebone->rad_tail *= scale;
      ebone->dist *= scale;

      /* we could be smarter and scale by the matrix along the x & z axis */
      ebone->xwidth *= scale;
      ebone->zwidth *= scale;
    }
  }
}

void ed_armature_transform(Armature *arm, const float mat[4][4], const bool do_props)
{
  if (arm->edbo) {
    ed_armature_edit_transform(arm, mat, do_props);
  }
  else {
    dune_armature_transform(arm, mat, do_props);
  }
}

void ed_armature_origin_set(
    Main *main, Ob *ob, const float cursor[3], int centermode, int around)
{
  const bool is_editmode = dune_ob_is_in_editmode(ob);
  Armature *arm = static_cast<Armature *>(ob->data);
  float cent[3];

  /* Put the armature into edit-mode. */
  if (is_editmode == false) {
    ed_armature_to_edit(arm);
  }

  /* Find the center-point. */
  if (centermode == 2) {
    copy_v3_v3(cent, cursor) {
    invert_m4_m4(ob->world_to_ob, ob->ob_to_world);
    mul_m4_v3(ob->world_to_ob, cent);
  }
  else {
    if (around == V3D_AROUND_CENTER_BOUNDS) {
      float min[3], max[3];
      INIT_MINMAX(min, max);
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        minmax_v3v3_v3(min, max, ebone->head);
        minmax_v3v3_v3(min, max, ebone->tail);
      }
      mid_v3_v3v3(cent, min, max);
    }
    else { /* #V3D_AROUND_CENTER_MEDIAN. */
      int total = 0;
      zero_v3(cent);
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        total += 2;
        add_v3_v3(cent, ebone->head);
        add_v3_v3(cent, ebone->tail);
      }
      if (total) {
        mul_v3_fl(cent, 1.0f / float(total));
      }
    }
  }

  /* Do the adjustments */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    sub_v3_v3(ebone->head, cent);
    sub_v3_v3(ebone->tail, cent);
  }

  /* Turn the list into an armature */
  if (is_editmode == false) {
    ed_armature_from_edit(main, arm);
    ed_armature_edit_free(arm);
  }

  /* Adjust ob location for new center-point. */
  if (centermode && (is_editmode == false)) {
    mul_mat3_m4_v3(ob->ob_to_world, cent); /* omit translation part */
    add_v3_v3(ob->loc, cent);
  }
}

/* Bone Roll Calc Op */
float ed_armature_ebone_roll_to_vector(const EditBone *bone,
                                       const float align_axis[3],
                                       const bool axis_only)
{
  float mat[3][3], nor[3];
  float vec[3], align_axis_proj[3], roll = 0.0f;

  LIB_ASSERT_UNIT_V3(align_axis);

  sub_v3_v3v3(nor, bone->tail, bone->head);

  /* If tail == head or the bone is aligned with the axis... */
  if (normalize_v3(nor) <= FLT_EPSILON ||
      (fabsf(dot_v3v3(align_axis, nor)) >= (1.0f - FLT_EPSILON))) {
    return roll;
  }

  vec_roll_to_mat3_normalized(nor, 0.0f, mat);

  /* project the new_up_axis along the normal */
  project_v3_v3v3_normalized(vec, align_axis, nor);
  sub_v3_v3v3(align_axis_proj, align_axis, vec);

  if (axis_only) {
    if (angle_v3v3(align_axis_proj, mat[2]) > float(M_PI_2)) {
      negate_v3(align_axis_proj);
    }
  }

  roll = angle_v3v3(align_axis_proj, mat[2]);

  cross_v3_v3v3(vec, mat[2], align_axis_proj);

  if (dot_v3v3(vec, nor) < 0.0f) {
    return -roll;
  }
  return roll;
}

/* Ranges arithmetic is used below. */
enum eCalcRollTypes {
  /* pos */
  CALC_ROLL_POS_X = 0,
  CALC_ROLL_POS_Y,
  CALC_ROLL_POS_Z,

  CALC_ROLL_TAN_POS_X,
  CALC_ROLL_TAN_POS_Z,

  /* neg */
  CALC_ROLL_NEG_X,
  CALC_ROLL_NEG_Y,
  CALC_ROLL_NEG_Z,

  CALC_ROLL_TAN_NEG_X,
  CALC_ROLL_TAN_NEG_Z,

  /* no sign */
  CALC_ROLL_ACTIVE,
  CALC_ROLL_VIEW,
  CALC_ROLL_CURSOR,
};

static const EnumPropertyItem prop_calc_roll_types[] = {
    RNA_ENUM_ITEM_HEADING(N_("Positive"), nullptr),
    {CALC_ROLL_TAN_POS_X, "POS_X", 0, "Local +X Tangent", ""},
    {CALC_ROLL_TAN_POS_Z, "POS_Z", 0, "Local +Z Tangent", ""},

    {CALC_ROLL_POS_X, "GLOBAL_POS_X", 0, "Global +X Axis", ""},
    {CALC_ROLL_POS_Y, "GLOBAL_POS_Y", 0, "Global +Y Axis", ""},
    {CALC_ROLL_POS_Z, "GLOBAL_POS_Z", 0, "Global +Z Axis", ""},

    RNA_ENUM_ITEM_HEADING(N_("Negative"), nullptr),
    {CALC_ROLL_TAN_NEG_X, "NEG_X", 0, "Local -X Tangent", ""},
    {CALC_ROLL_TAN_NEG_Z, "NEG_Z", 0, "Local -Z Tangent", ""},

    {CALC_ROLL_NEG_X, "GLOBAL_NEG_X", 0, "Global -X Axis", ""},
    {CALC_ROLL_NEG_Y, "GLOBAL_NEG_Y", 0, "Global -Y Axis", ""},
    {CALC_ROLL_NEG_Z, "GLOBAL_NEG_Z", 0, "Global -Z Axis", ""},

    RNA_ENUM_ITEM_HEADING(N_("Other"), nullptr),
    {CALC_ROLL_ACTIVE, "ACTIVE", 0, "Active Bone", ""},
    {CALC_ROLL_VIEW, "VIEW", 0, "View Axis", ""},
    {CALC_ROLL_CURSOR, "CURSOR", 0, "Cursor", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static int armature_calc_roll_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_active = cxt_data_edit_ob(C);
  int ret = OP_FINISHED;

  eCalcRollTypes type = eCalcRollTypes(apu_enum_get(op->ptr, "type"));
  const bool axis_only = api_bool_get(op->ptr, "axis_only");
  /* axis_flip when matching the active bone never makes sense */
  bool axis_flip = ((type >= CALC_ROLL_ACTIVE)    ? api_bool_get(op->ptr, "axis_flip") :
                    (type >= CALC_ROLL_TAN_NEG_X) ? true :
                                                    false);

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    float imat[3][3];
    EditBone *ebone;

    if ((type >= CALC_ROLL_NEG_X) && (type <= CALC_ROLL_TAN_NEG_Z)) {
      type = eCalcRollTypes(int(type) - (CALC_ROLL_ACTIVE - CALC_ROLL_NEG_X));
      axis_flip = true;
    }

    copy_m3_m4(imat, ob->object_to_world);
    invert_m3(imat);

    if (type == CALC_ROLL_CURSOR) { /* Cursor */
      float cursor_local[3];
      const View3DCursor *cursor = &scene->cursor;

      invert_m4_m4(ob->world_to_ob, ob->ob_to_world);
      copy_v3_v3(cursor_local, cursor->location);
      mul_m4_v3(ob->world_to_ob, cursor_local);

      /* cursor */
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        if (EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone)) {
          float cursor_rel[3];
          sub_v3_v3v3(cursor_rel, cursor_local, ebone->head);
          if (axis_flip) {
            negate_v3(cursor_rel);
          }
          if (normalize_v3(cursor_rel) != 0.0f) {
            ebone->roll = ED_armature_ebone_roll_to_vector(ebone, cursor_rel, axis_only);
            changed = true;
          }
        }
      }
    }
    else if (ELEM(type, CALC_ROLL_TAN_POS_X, CALC_ROLL_TAN_POS_Z)) {
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        if (ebone->parent) {
          bool is_edit = (EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone));
          bool is_edit_parent = (EBONE_VISIBLE(arm, ebone->parent) &&
                                 EBONE_EDITABLE(ebone->parent));

          if (is_edit || is_edit_parent) {
            EditBone *ebone_other = ebone->parent;
            float dir_a[3];
            float dir_b[3];
            float vec[3];
            bool is_vec_zero;

            sub_v3_v3v3(dir_a, ebone->tail, ebone->head);
            normalize_v3(dir_a);

            /* find the first bone in the chain with a different direction */
            do {
              sub_v3_v3v3(dir_b, ebone_other->head, ebone_other->tail);
              normalize_v3(dir_b);

              if (type == CALC_ROLL_TAN_POS_Z) {
                cross_v3_v3v3(vec, dir_a, dir_b);
              }
              else {
                add_v3_v3v3(vec, dir_a, dir_b);
              }
            } while ((is_vec_zero = (normalize_v3(vec) < 0.00001f)) &&
                     (ebone_other = ebone_other->parent));

            if (!is_vec_zero) {
              if (axis_flip) {
                negate_v3(vec);
              }

              if (is_edit) {
                ebone->roll = ed_armature_ebone_roll_to_vector(ebone, vec, axis_only);
                changed = true;
              }

              /* parentless bones use cross product with child */
              if (is_edit_parent) {
                if (ebone->parent->parent == nullptr) {
                  ebone->parent->roll = ed_armature_ebone_roll_to_vector(
                      ebone->parent, vec, axis_only);
                  changed = true;
                }
              }
            }
          }
        }
      }
    }
    else {
      float vec[3] = {0.0f, 0.0f, 0.0f};
      if (type == CALC_ROLL_VIEW) { /* View */
        RgnView3D *rv3d = cxt_win_rgn_view3d(C);
        if (rv3d == nullptr) {
          dune_report(op->reports, RPT_ERROR, "No rgn view3d available");
          ret = OP_CANCELLED;
          goto cleanup;
        }

        copy_v3_v3(vec, rv3d->viewinv[2]);
        mul_m3_v3(imat, vec);
      }
      else if (type == CALC_ROLL_ACTIVE) {
        float mat[3][3];
        Armature *arm_active = static_cast<Armature *>(ob_active->data);
        ebone = (EditBone *)arm_active->act_edbone;
        if (ebone == nullptr) {
          dune_report(op->reports, RPT_ERROR, "No active bone set");
          ret = OP_CANCELLED;
          goto cleanup;
        }

        ed_armature_ebone_to_mat3(ebone, mat);
        copy_v3_v3(vec, mat[2]);
      }
      else if (type < 6) { /* Always true, check to quiet GCC12.2 `-Warray-bounds`. */
        /* Axis */
        if (type < 3) {
          vec[type] = 1.0f;
        }
        else {
          vec[type - 2] = -1.0f;
        }
        mul_m3_v3(imat, vec);
        normalize_v3(vec);
      }
      else {
        /* The prev block should handle all remaining cases. */
        lib_assert_unreachable();
      }

      if (axis_flip) {
        negate_v3(vec);
      }

      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        if (EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone)) {
          /* roll fn is a cb which assumes that all is well */
          ebone->roll = ed_armature_ebone_roll_to_vector(ebone, vec, axis_only);
          changed = true;
        }
      }
    }

    if (arm->flag & ARM_MIRROR_EDIT) {
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        if ((EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone)) == 0) {
          EditBone *ebone_mirr = ed_armature_ebone_get_mirrored(arm->edbo, ebone);
          if (ebone_mirr && (EBONE_VISIBLE(arm, ebone_mirr) && EBONE_EDITABLE(ebone_mirr))) {
            ebone->roll = -ebone_mirr->roll;
          }
        }
      }
    }

    if (changed) {
      /* Notifier may evolve. */
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&arm->id, ID_RECALC_SEL);
    }
  }

cleanup:
  mem_free(obs);
  return ret;
}

void ARMATURE_OT_calc_roll(WinOpType *ot)
{
  /* ids */
  ot->name = "Recalc Roll";
  ot->idname = "ARMATURE_OT_calc_roll";
  ot->description = "Automatically fix alignment of select bones' axes";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = armature_calc_roll_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_calc_roll_types, CALC_ROLL_TAN_POS_X, "Type", "");
  api_def_bool(ot->sapi, "axis_flip", false, "Flip Axis", "Negate the alignment axis");
  api_def_bool(ot->sapi,
                  "axis_only",
                  false,
                  "Shortest Rotation",
                  "Ignore the axis direction, use the shortest rotation to align");
}

static int armature_roll_clear_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  const float roll = api_float_get(op->ptr, "roll");

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone)) {
        /* Roll fn is a cb which assumes that all is well. */
        ebone->roll = roll;
        changed = true;
      }
    }

    if (arm->flag & ARM_MIRROR_EDIT) {
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        if ((EBONE_VISIBLE(arm, ebone) && EBONE_EDITABLE(ebone)) == 0) {
          EditBone *ebone_mirr = ed_armature_ebone_get_mirrored(arm->edbo, ebone);
          if (ebone_mirr && (EBONE_VISIBLE(arm, ebone_mirr) && EBONE_EDITABLE(ebone_mirr))) {
            ebone->roll = -ebone_mirr->roll;
            changed = true;
          }
        }
      }
    }

    if (changed) {
      /* Notifier might evolve. */
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&arm->id, ID_RECALC_SEL);
    }
  }
  mem_free(obs);

  return OP_FINISHED;
}

void ARMATURE_OT_roll_clear(WinOpType *ot)
{
  /* ids */
  ot->name = "Clear Roll";
  ot->idname = "ARMATURE_OT_roll_clear";
  ot->description = "Clear roll for sel bones";

  /* api cbs */
  ot->ex = armature_roll_clear_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_float_rotation(ot->sapi,
                         "roll",
                         0,
                         nullptr,
                         DEG2RADF(-360.0f),
                         DEG2RADF(360.0f),
                         "Roll",
                         "",
                         DEG2RADF(-360.0f),
                         DEG2RADF(360.0f));
}

/* Chain-Based Tool Utils */
/* tmp data-structure for merge/fill bones */
struct EditBonePoint {
  EditBonePoint *next, *prev;

  EditBone *head_owner; /* EditBone which uses this point as a 'head' point */
  EditBone *tail_owner; /* EditBone which uses this point as a 'tail' point */

  float vec[3]; /* the actual location of the point in local/EditMode space */
};

/* find chain-tips (i.e. bones without children) */
static void chains_find_tips(List *edbo, List *list)
{
  EditBone *ebo;
  LinkData *ld;

  /* This is potentially very slow ... there's got to be a better way. */
  LIST_FOREACH (EditBone *, curBone, edbo) {
    short stop = 0;

    /* is this bone contained within any existing chain? (skip if so) */
    LIST_FOREACH (LinkData *, ld, list) {
      for (ebo = static_cast<EditBone *>(ld->data); ebo; ebo = ebo->parent) {
        if (ebo == curBone) {
          stop = 1;
          break;
        }
      }

      if (stop) {
        break;
      }
    }
    /* skip current bone if it is part of an existing chain */
    if (stop) {
      continue;
    }

    /* is any existing chain part of the chain formed by this bone? */
    stop = 0;
    for (ebo = curBone->parent; ebo; ebo = ebo->parent) {
      LIST_FOREACH (LinkData *, ld, list) {
        if (ld->data == ebo) {
          ld->data = curBone;
          stop = 1;
          break;
        }
      }

      if (stop) {
        break;
      }
    }
    /* current bone has alrdy been added to a chain? */
    if (stop) {
      continue;
    }

    /* add current bone to a new chain */
    ld = static_cast<LinkData *>(MEM_callocN(sizeof(LinkData), "BoneChain"));
    ld->data = curBone;
    lib_addtail(list, ld);
  }
}

/* Fill Op */
static void fill_add_joint(EditBone *ebo, short eb_tail, List *points)
{
  EditBonePoint *ebp;
  float vec[3];
  short found = 0;

  if (eb_tail) {
    copy_v3_v3(vec, ebo->tail);
  }
  else {
    copy_v3_v3(vec, ebo->head);
  }

  LIST_FOREACH (EditBonePoint *, ebp, points) {
    if (equals_v3v3(ebp->vec, vec)) {
      if (eb_tail) {
        if ((ebp->head_owner) && (ebp->head_owner->parent == ebo)) {
          /* so this bone's tail owner is this bone */
          ebp->tail_owner = ebo;
          found = 1;
          break;
        }
      }
      else {
        if ((ebp->tail_owner) && (ebo->parent == ebp->tail_owner)) {
          /* so this bone's head owner is this bone */
          ebp->head_owner = ebo;
          found = 1;
          break;
        }
      }
    }
  }

  /* alloc a new point if no existing point was related */
  if (found == 0) {
    ebp = static_cast<EditBonePoint *>(mem_calloc(sizeof(EditBonePoint), "EditBonePoint"));

    if (eb_tail) {
      copy_v3_v3(ebp->vec, ebo->tail);
      ebp->tail_owner = ebo;
    }
    else {
      copy_v3_v3(ebp->vec, ebo->head);
      ebp->head_owner = ebo;
    }

    lib_addtail(points, ebp);
  }
}

/* bone adding between sel joints */
static int armature_fill_bones_ex(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);
  View3D *v3d = cxt_win_view3d(C);
  List points = {nullptr, nullptr};
  EditBone *newbone = nullptr;
  int count;
  bool mixed_ob_error = false;

  /* loop over all bones, and only consider if visible */
  Armature *arm = nullptr;
  CXT_DATA_BEGIN_WITH_ID (C, EditBone *, ebone, visible_bones, bArmature *, arm_iter) {
    bool check = false;
    if (!(ebone->flag & BONE_CONNECTED) && (ebone->flag & BONE_ROOTSEL)) {
      fill_add_joint(ebone, 0, &points);
      check = true;
    }
    if (ebone->flag & BONE_TIPSEL) {
      fill_add_joint(ebone, 1, &points);
      check = true;
    }

    if (check) {
      if (arm && (arm != arm_iter)) {
        mixed_ob_error = true;
      }
      arm = arm_iter;
    }
  }
  CXT_DATA_END;

  /* the number of joints determines how we fill:
   *  1) between joint and cursor (joint=head, cursor=tail)
   *  2) between the two joints (order is dependent on active-bone/hierarchy)
   *  3+) error (a smarter method involving finding chains needs to be worked out */
  count = lib_list_count(&points);

  if (count == 0) {
    dune_report(op->reports, RPT_ERROR, "No joints selected");
    return OP_CANCELLED;
  }

  if (mixed_ob_error) {
    dune_report(op->reports, RPT_ERROR, "Bones for different objects selected");
    lib_freelist(&points);
    return OP_CANCELLED;
  }

  Ob *obedit = nullptr;
  {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    FOREACH_OB_IN_EDIT_MODE_BEGIN (scene, view_layer, v3d, ob_iter) {
      if (ob_iter->data == arm) {
        obedit = ob_iter;
      }
    }
    FOREACH_OB_IN_MODE_END;
  }
  lib_assert(obedit != nullptr);

  if (count == 1) {
    EditBonePoint *ebp;
    float curs[3];

    /* Get Points sel joint */
    ebp = static_cast<EditBonePoint *>(points.first);

    /* Get points - cursor (tail) */
    invert_m4_m4(obedit->world_to_ob, obedit->ob_to_world);
    mul_v3_m4v3(curs, obedit->world_to_ob, scene->cursor.location);

    /* Create a bone */
    newbone = add_points_bone(obedit, ebp->vec, curs);
  }
  else if (count == 2) {
    EditBonePoint *ebp_a, *ebp_b;
    float head[3], tail[3];
    short headtail = 0;

    /* check that the points don't belong to the same bone */
    ebp_a = (EditBonePoint *)points.first;
    ebp_b = ebp_a->next;

    if (((ebp_a->head_owner == ebp_b->tail_owner) && (ebp_a->head_owner != nullptr)) ||
        ((ebp_a->tail_owner == ebp_b->head_owner) && (ebp_a->tail_owner != nullptr)))
    {
      dune_report(op->reports, RPT_ERROR, "Same bone selected...");
      lib_freelist(&points);
      return OP_CANCELLED;
    }

    /* find which one should be the 'head' */
    if ((ebp_a->head_owner && ebp_b->head_owner) || (ebp_a->tail_owner && ebp_b->tail_owner)) {
      /* use active, nice predictable */
      if (arm->act_edbone && ELEM(arm->act_edbone, ebp_a->head_owner, ebp_a->tail_owner)) {
        headtail = 1;
      }
      else if (arm->act_edbone && ELEM(arm->act_edbone, ebp_b->head_owner, ebp_b->tail_owner)) {
        headtail = 2;
      }
      else {
        /* rule: whichever one is closer to 3d-cursor */
        float curs[3];
        float dist_sq_a, dist_sq_b;

        /* get cursor location */
        invert_m4_m4(obedit->world_to_ob, obedit->ob_to_world);
        mul_v3_m4v3(curs, obedit->world_to_ob, scene->cursor.location);

        /* get distances */
        dist_sq_a = len_squared_v3v3(ebp_a->vec, curs);
        dist_sq_b = len_squared_v3v3(ebp_b->vec, curs);

        /* compare distances - closer one therefore acts as direction for bone to go */
        headtail = (dist_sq_a < dist_sq_b) ? 2 : 1;
      }
    }
    else if (ebp_a->head_owner) {
      headtail = 1;
    }
    else if (ebp_b->head_owner) {
      headtail = 2;
    }

    /* assign head/tail combinations */
    if (headtail == 2) {
      copy_v3_v3(head, ebp_a->vec);
      copy_v3_v3(tail, ebp_b->vec);
    }
    else if (headtail == 1) {
      copy_v3_v3(head, ebp_b->vec);
      copy_v3_v3(tail, ebp_a->vec);
    }

    /* add new bone and parent it to the appropriate end */
    if (headtail) {
      newbone = add_points_bone(obedit, head, tail);

      /* do parenting (will need to set connected flag too) */
      if (headtail == 2) {
        /* ebp tail or head - tail gets priority */
        if (ebp_a->tail_owner) {
          newbone->parent = ebp_a->tail_owner;
        }
        else {
          newbone->parent = ebp_a->head_owner;
        }
      }
      else {
        /* ebp_b tail or head tail gets priority */
        if (ebp_b->tail_owner) {
          newbone->parent = ebp_b->tail_owner;
        }
        else {
          newbone->parent = ebp_b->head_owner;
        }
      }

      /* don't set for bone connecting two head points of bones */
      if (ebp_a->tail_owner || ebp_b->tail_owner) {
        newbone->flag |= BONE_CONNECTED;
      }
    }
  }
  else {
    dune_reportf(op->reports, RPT_ERROR, "Too many points sel: %d", count);
    lib_freelist(&points);
    return OP_CANCELLED;
  }

  if (newbone) {
    ed_armature_edit_desel_all(obedit);
    arm->act_edbone = newbone;
    newbone->flag |= BONE_TIPSEL;
  }

  /* updates */
  win_ev_add_notifier(C, NC_OB | ND_POSE, obedit);
  graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);

  /* free points */
  lib_freelist(&points);

  return OP_FINISHED;
}

void ARMATURE_OT_fill(WinOpType *ot)
{
  /* ids */
  ot->name = "Fill Between Joints";
  ot->idname = "ARMATURE_OT_fill";
  ot->description = "Add bone between sel joint(s) and/or 3D cursor";
  
  /* cbs */
  ot->ex = armature_fill_bones_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;


/* Switch Direction Op
 * Currently this doesnt use cxt loops as cxt loops are not
 * easy to retrieve for hierarchical/chain relationships which are necessary for
 * this to be done easily. */

/* helper to clear BONE_TRANSFORM flags */
static void armature_clear_swap_done_flags(Armature *arm)
{
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    ebone->flag &= ~BONE_TRANSFORM;
  }
}

static int armature_switch_direction_exec(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);

  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);

    List chains = {nullptr, nullptr};

    /* get chains of bones (ends on chains) */
    chains_find_tips(arm->edbo, &chains);
    if (lib_list_is_empty(&chains)) {
      continue;
    }

    /* ensure that mirror bones will also be op on */
    armature_tag_sel_mirrored(arm);

    /* Clear BONE_TRANSFORM flags
     * - Used to prevent dup/canceling ops from occurring #34123.
     * - BONE_DONE cannot be used here as that's alrdy used for mirroring.  */
    armature_clear_swap_done_flags(arm);

    /* loop over chains, only considering sel and visible bones */
    LIST_FOREACH (LinkData *, chain, &chains) {
      EditBone *ebo, *child = nullptr, *parent = nullptr;

      /* loop over bones in chain */
      for (ebo = static_cast<EditBone *>(chain->data); ebo; ebo = parent) {
        /* parent is this bone's original parent
         * - we store this, as the next bone that is checked is this one
         *   but the val of ebo->parent may change here... */
        parent = ebo->parent;

        /* skip bone if alrdy handled, see #34123. */
        if ((ebo->flag & BONE_TRANSFORM) == 0) {
          /* only if sel and editable */
          if (EBONE_VISIBLE(arm, ebo) && EBONE_EDITABLE(ebo)) {
            /* swap head and tail coords */
            swap_v3(ebo->head, ebo->tail);

            /* do parent swapping:
             *- use 'child' as new parent
             *- connected flag is only set if points are coincidental
        */
            ebo->parent = child;
            if ((child) && equals_v3v3(ebo->head, child->tail)) {
              ebo->flag |= BONE_CONNECTED;
            }
            else {
              ebo->flag &= ~BONE_CONNECTED;
            }

            /* get next bones
             * child will become the new parent of next bone */
            child = ebo;
          }
          else {
            /* not swapping this bone, however, if its 'parent' got swapped, unparent us from it
             * as it will be facing in opposite direction  */
            if ((parent) && (EBONE_VISIBLE(arm, parent) && EBONE_EDITABLE(parent))) {
              ebo->parent = nullptr;
              ebo->flag &= ~BONE_CONNECTED;
            }

            /* get next bones
             * child will become new parent of next bone (not swapping occurred,
             * so set to nullptr to prevent infinite-loop) */
            child = nullptr;
          }

          /* tag as done (to prevent double-swaps) */
          ebo->flag |= BONE_TRANSFORM;
        }
      }
    }

    /* free chains */
    lib_freelist(&chains);

    /* clear tmp flags */
    armature_clear_swap_done_flags(arm);
    armature_tag_unsel(arm);

    /* notifier might evolve. */
    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
    graph_id_tag_update(&arm->id, ID_RECALC_SEL);
  }
  mem_free(obs);

  return OP_FINISHED;
}

void ARMATURE_OT_switch_direction(WinOpType *ot)
{
  /* ids */
  ot->name = "Switch Direction";
  ot->idname = "ARMATURE_OT_switch_direction";
  ot->description = "Change the direction that a chain of bones points in (head and tail swap)";

  /* api cbs */
  ot->ex = armature_switch_direction_ex;
  ot->poll = es_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Align Op */

/* Helper to fix a ebone position if its parent has moved due to alignment. */
static void fix_connected_bone(EditBone *ebone)
{
  float diff[3];

  if (!(ebone->parent) || !(ebone->flag & BONE_CONNECTED) ||
      equals_v3v3(ebone->parent->tail, ebone->head))
  {
    return;
  }

  /* if the parent has moved we translate child's head and tail accordingly */
  sub_v3_v3v3(diff, ebone->parent->tail, ebone->head);
  add_v3_v3(ebone->head, diff);
  add_v3_v3(ebone->tail, diff);
}

/* helper to recursively find chains of connected bones starting at ebone and fix their position */
static void fix_editbone_connected_children(List *edbo, EditBone *ebone)
{
  LIST_FOREACH (EditBone *, selbone, edbo) {
    if ((selbone->parent) && (selbone->parent == ebone) && (selbone->flag & BONE_CONNECTED)) {
      fix_connected_bone(selbone);
      fix_editbone_connected_children(edbo, selbone);
    }
  }
}

static void bone_align_to_bone(Lis *edbo, EditBone *selbone, EditBone *actbone)
{
  float selboneaxis[3], actboneaxis[3], length;

  sub_v3_v3v3(actboneaxis, actbone->tail, actbone->head);
  normalize_v3(actboneaxis);

  sub_v3_v3v3(selboneaxis, selbone->tail, selbone->head);
  length = len_v3(selboneaxis);

  mul_v3_fl(actboneaxis, length);
  add_v3_v3v3(selbone->tail, selbone->head, actboneaxis);
  selbone->roll = actbone->roll;

  /* If the bone being aligned has connected descendants they must be moved
   * according to their parent new position, otherwise they would be left
   * in an inconsistent state: connected but away from the parent. */
  fix_editbone_connected_children(edbo, selbone);
}

static int armature_align_bones_ex(Cxt *C, WinOp *op)
{
  Ob *ob = cxt_data_edit_ob(C);
  Armature *arm = (Armature *)ob->data;
  EditBone *actbone = cxt_data_active_bone(C);
  EditBone *actmirb = nullptr;
  int num_sel_bones;

  /* there must be an active bone */
  if (actbone == nullptr) {
    dune_report(op->reports, RPT_ERROR, "Op requires an active bone");
    return OP_CANCELLED;
  }

  if (arm->flag & ARM_MIRROR_EDIT) {
    /* For X-Axis Mirror Editing option, we may need a mirror copy of actbone
     * - if there's a mirrored copy of selbone, try to find a mirrored copy of actbone
     *   (i.e.  selbone="child.L" and actbone="parent.L", find "child.R" and "parent.R").
     *   This is useful for arm-chains, for example parenting lower arm to upper arm
     * - if there's no mirrored copy of actbone (i.e. actbone = "parent.C" or "parent")
     *   then just use actbone. Useful when doing upper arm to spine.
     */
    actmirb = ed_armature_ebone_get_mirrored(arm->edbo, actbone);
    if (actmirb == nullptr) {
      actmirb = actbone;
    }
  }

  /* if there is only 1 selected bone, we assume that it is the active bone,
   * since a user will need to have clicked on a bone (thus selecting it) to make it active */
  num_sel_bones = CXT_DATA_COUNT(C, selected_editable_bones);
  if (num_sel_bones <= 1) {
    /* When only the active bone is sel, and it has a parent,
     * align it to the parent, as that is the only possible outcome.
     */
    if (actbone->parent) {
      bone_align_to_bone(arm->edbo, actbone, actbone->parent);

      if ((arm->flag & ARM_MIRROR_EDIT) && (actmirb->parent)) {
        bone_align_to_bone(arm->edbo, actmirb, actmirb->parent);
      }

      dune_reportf(op->reports, RPT_INFO, "Aligned bone '%s' to parent", actbone->name);
    }
  }
  else {
    /* Align 'sel' bones to the active one
     * - the cxt iter contains both selected bones and their mirrored copies,
     *   so we assume that unsel bones are mirrored copies of some sel bone
     * - since the active one (and/or its mirror) will also be sel, we also need
     *   to check that we are not trying to op on them, since such an op
     *   would cause error */

    /* align sel bones to the active one */
    CXT_DATA_BEGIN (C, EditBone *, ebone, sel_editable_bones) {
      if (ELEM(ebone, actbone, actmirb) == 0) {
        if (ebone->flag & BONE_SEL) {
          bone_align_to_bone(arm->edbo, ebone, actbone);
        }
        else {
          bone_align_to_bone(arm->edbo, ebone, actmirb);
        }
      }
    }
    CXT_DATA_END;

    dune_reportf(
        op->reports, RPT_INFO, "%d bones aligned to bone '%s'", num_selected_bones, actbone->name);
  }

  /* Notifier might evolve. */
  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
  graph_id_tag_update(&arm->id, ID_RECALC_SEL);

  return OP_FINISHED;
}

void ARMATURE_OT_align(WinOpType *ot)
{
  /* ids */
  ot->name = "Align Bones";
  ot->idname = "ARMATURE_OT_align";
  ot->description = "Align sel bones to the active bone (or to their parent)";

  /* api cbs */
  ot->ex = armature_align_bones_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Split Op */

static int armature_split_exec(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);

    LIST_FOREACH (EditBone *, bone, arm->edbo) {
      if (bone->parent && (bone->flag & BONE_SEL) != (bone->parent->flag & BONE_SEL)) {
        bone->parent = nullptr;
        bone->flag &= ~BONE_CONNECTED;
      }
    }
    LIST_FOREACH (EditBone *, bone, arm->edbo) {
      ed_armature_ebone_sel_set(bone, (bone->flag & BONE_SEL) != 0);
    }

    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
    graph_id_tag_update(&arm->id, ID_RECALC_SEL);
  }

  mem_free(obs);
  return OP_FINISHED;
}

void ARMATURE_OT_split(WinOpType *ot)
{
  /* ids */
  ot->name = "Split";
  ot->idname = "ARMATURE_OT_split";
  ot->description = "Split off sel bones from connected unselected bones";

  /* api cbs */
  ot->ex = armature_split_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Del Op */

static bool armature_delete_ebone_cb(const char *bone_name, void *arm_p)
{
  Armature *arm = static_cast<Armature *>(arm_p);
  EditBone *ebone;

  ebone = ED_armature_ebone_find_name(arm->edbo, bone_name);
  return (ebone && (ebone->flag & BONE_SEL) && anim_bonecoll_is_visible_editbone(arm, ebone));
}

/* previously delete_armature */
/* only editmode! */
static int armature_delete_sel_ex(Cxt *C, WinOp * /*op*/)
{
  EditBone *curBone, *ebone_next;
  bool changed_multi = false;

  /* cancel if nothing sel */
  if (CXT_DATA_COUNT(C, sel_bones) == 0) {
    return OP_CANCELLED;
  }

  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *obedit = obs[ob_index];
    Armature *arm = static_cast<Armature *>(obedit->data);
    bool changed = false;

    armature_sel_mirrored(arm);

    dune_pose_channels_remove(obedit, armature_delete_ebone_cb, arm);

    for (curBone = static_cast<EditBone *>(arm->edbo->first); curBone; curBone = ebone_next) {
      ebone_next = curBone->next;
      if (anime_bonecoll_is_visible_editbone(arm, curBone)) {
        if (curBone->flag & BONE_SEL) {
          if (curBone == arm->act_edbone) {
            arm->act_edbone = nullptr;
          }
          ed_armature_ebone_remove(arm, curBone);
          changed = true;
        }
      }
    }

    if (changed) {
      changed_multi = true;

      ed_armature_edit_sync_sel(arm->edbo);
      dune_pose_tag_recalc(cxt_data_main(C), obedit->pose);
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
      graph_id_tag_update(&arm->id, ID_RECALC_SEL);
      ed_outliner_select_sync_from_edit_bone_tag(C);
    }
  }
  mem_free(obs);

  if (!changed_multi) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void ARMATURE_OT_delete(WinOpType *ot)
{
  /* ids */
  ot->name = "Delete Sel Bone(s)";
  ot->idname = "ARMATURE_OT_delete";
  ot->description = "Remove sel bones from the armature";

  /* api cbs */
  ot->invoke = win_op_confirm_or_ex;
  ot->ex = armature_delete_sel_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  win_op_props_confirm_or_ex(ot);
}

static bool armature_dissolve_ebone_cb(const char *bone_name, void *arm_p)
{
  Armature *arm = static_cast<Armature *>(arm_p);
  EditBone *ebone;

  ebone = ed_armature_ebone_find_name(arm->edbo, bone_name);
  return (ebone && (ebone->flag & BONE_DONE));
}

static int armature_dissolve_sel_ex(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  EditBone *ebone, *ebone_next;
  bool changed_multi = false;

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *obedit = obs[ob_index];
    Armature *arm = static_cast<Armature *>(obedit->data);
    bool changed = false;

    /* store for mirror */
    GHash *ebone_flag_orig = nullptr;
    int ebone_num = 0;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      ebone->tmp.p = nullptr;
      ebone->flag &= ~BONE_DONE;
      ebone_num++;
    }

    if (arm->flag & ARM_MIRROR_EDIT) {
      GHashIter gh_iter;

      ebone_flag_orig = lib_ghash_ptr_new_ex(__func__, ebone_num);
      LIST_FOREACH (EditBone *, ebone, arm->edbo) {
        union {
          int flag;
          void *p;
        } val = {0};
        val.flag = ebone->flag;
        lib_ghash_insert(ebone_flag_orig, ebone, val.p);
      }

      armature_sel_mirrored_ex(arm, BONE_SEL | BONE_ROOTSEL | BONE_TIPSEL);

      GHASH_ITER (gh_iter, ebone_flag_orig) {
        union Val {
          int flag;
          void *p;
        } *val_p = (Val *)lib_ghashIter_getVal_p(&gh_iter);
        ebone = static_cast<EditBone *>(lib_ghashIter_getKey(&gh_iter));
        val_p->flag = ebone->flag & ~val_p->flag;
      }
    }

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (ebone->parent && ebone->flag & BONE_CONNECTED) {
        if (ebone->parent->tmp.ebone == ebone->parent) {
          /* ignore */
        }
        else if (ebone->parent->tmp.ebone) {
          /* set ignored */
          ebone->parent->tmp.ebone = ebone->parent;
        }
        else {
          /* set child */
          ebone->parent->tmp.ebone = ebone;
        }
      }
    }

    /* cleanup multiple used bones */
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (ebone->temp.ebone == ebone) {
        ebone->temp.ebone = nullptr;
      }
    }

    LISTo_FOREACH (EditBone *, ebone, arm->edbo) {
      /* break connections for unseen bones */
      if ((ANIM_bonecoll_is_visible_editbone(arm, ebone) &&
           (ED_armature_ebone_selectflag_get(ebone) & (BONE_TIPSEL | BONE_SELECTED))) == 0)
      {
        ebone->temp.ebone = nullptr;
      }

      if ((ANIM_bonecoll_is_visible_editbone(arm, ebone) &&
           (ED_armature_ebone_selectflag_get(ebone) & (BONE_ROOTSEL | BONE_SELECTED))) == 0)
      {
        if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
          ebone->parent->temp.ebone = nullptr;
        }
      }
    }

    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {

      if (ebone->parent && (ebone->parent->temp.ebone == ebone)) {
        ebone->flag |= BONE_DONE;
      }
    }

    BKE_pose_channels_remove(obedit, armature_dissolve_ebone_cb, arm);

    for (ebone = static_cast<EditBone *>(arm->edbo->first); ebone; ebone = ebone_next) {
      ebone_next = ebone->next;

      if (ebone->flag & BONE_DONE) {
        copy_v3_v3(ebone->parent->tail, ebone->tail);
        ebone->parent->rad_tail = ebone->rad_tail;
        SET_FLAG_FROM_TEST(ebone->parent->flag, ebone->flag & BONE_TIPSEL, BONE_TIPSEL);

        ED_armature_ebone_remove_ex(arm, ebone, false);
        changed = true;
      }
    }

    if (changed) {
      LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
        if (ebone->parent && ebone->parent->temp.ebone && (ebone->flag & BONE_CONNECTED)) {
          ebone->rad_head = ebone->parent->rad_tail;
        }
      }

      if (arm->flag & ARM_MIRROR_EDIT) {
        LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
          union Value {
            int flag;
            void *p;
          } *val_p = (Value *)BLI_ghash_lookup_p(ebone_flag_orig, ebone);
          if (val_p && val_p->flag) {
            ebone->flag &= ~val_p->flag;
          }
        }
      }
    }

    if (arm->flag & ARM_MIRROR_EDIT) {
      BLI_ghash_free(ebone_flag_orig, nullptr, nullptr);
    }

    if (changed) {
      changed_multi = true;
      ED_armature_edit_sync_selection(arm->edbo);
      WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, obedit);
      DEG_id_tag_update(&arm->id, ID_RECALC_SELECT);
      ED_outliner_select_sync_from_edit_bone_tag(C);
    }
  }
  MEM_freeN(objects);

  if (!changed_multi) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void ARMATURE_OT_dissolve(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Dissolve Selected Bone(s)";
  ot->idname = "ARMATURE_OT_dissolve";
  ot->description = "Dissolve selected bones from the armature";

  /* api callbacks */
  ot->exec = armature_dissolve_selected_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Hide Operator
 * \{ */

static int armature_hide_exec(bContext *C, wmOperator *op)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const int invert = RNA_boolean_get(op->ptr, "unselected") ? BONE_SELECTED : 0;

  /* cancel if nothing selected */
  if (CTX_DATA_COUNT(C, selected_bones) == 0) {
    return OPERATOR_CANCELLED;
  }

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    bArmature *arm = static_cast<bArmature *>(obedit->data);
    bool changed = false;

    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_VISIBLE(arm, ebone)) {
        if ((ebone->flag & BONE_SELECTED) != invert) {
          ebone->flag &= ~(BONE_TIPSEL | BONE_SELECTED | BONE_ROOTSEL);
          ebone->flag |= BONE_HIDDEN_A;
          changed = true;
        }
      }
    }

    if (!changed) {
      continue;
    }
    ED_armature_edit_validate_active(arm);
    ED_armature_edit_sync_selection(arm->edbo);

    WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, obedit);
    DEG_id_tag_update(&arm->id, ID_RECALC_SELECT);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void ARMATURE_OT_hide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Hide Selected";
  ot->idname = "ARMATURE_OT_hide";
  ot->description = "Tag selected bones to not be visible in Edit Mode";

  /* api callbacks */
  ot->exec = armature_hide_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(
      ot->srna, "unselected", false, "Unselected", "Hide unselected rather than selected");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reveal Operator
 * \{ */

static int armature_reveal_exec(bContext *C, wmOperator *op)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const bool select = RNA_boolean_get(op->ptr, "select");
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    bArmature *arm = static_cast<bArmature *>(obedit->data);
    bool changed = false;

    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (ANIM_bonecoll_is_visible_editbone(arm, ebone)) {
        if (ebone->flag & BONE_HIDDEN_A) {
          if (!(ebone->flag & BONE_UNSELECTABLE)) {
            SET_FLAG_FROM_TEST(ebone->flag, select, (BONE_TIPSEL | BONE_SELECTED | BONE_ROOTSEL));
          }
          ebone->flag &= ~BONE_HIDDEN_A;
          changed = true;
        }
      }
    }

    if (changed) {
      ED_armature_edit_validate_active(arm);
      ED_armature_edit_sync_selection(arm->edbo);

      WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, obedit);
      DEG_id_tag_update(&arm->id, ID_RECALC_SELECT);
    }
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void ARMATURE_OT_reveal(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reveal Hidden";
  ot->idname = "ARMATURE_OT_reveal";
  ot->description = "Reveal all bones hidden in Edit Mode";

  /* api callbacks */
  ot->exec = armature_reveal_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna, "select", true, "Select", "");
}

/** \} */
