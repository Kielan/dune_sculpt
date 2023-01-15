/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_kdopbvh.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"
#include "BLT_translation.h"

#include "DNA_action_types.h"
#include "DNA_armature_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "DNA_lattice_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_scene_types.h"
#include "DNA_tracking_types.h"

#include "BKE_action.h"
#include "BKE_anim_path.h"
#include "BKE_animsys.h"
#include "BKE_armature.h"
#include "BKE_bvhutils.h"
#include "BKE_cachefile.h"
#include "BKE_camera.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_deform.h"
#include "BKE_displist.h"
#include "BKE_editmesh.h"
#include "BKE_fcurve_driver.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_lib_id.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"
#include "BKE_movieclip.h"
#include "BKE_object.h"
#include "BKE_scene.h"
#include "BKE_shrinkwrap.h"
#include "BKE_tracking.h"

#include "BIK_api.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "BLO_read_write.h"

#include "CLG_log.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#ifdef WITH_ALEMBIC
#  include "ABC_alembic.h"
#endif

#ifdef WITH_USD
#  include "usd.h"
#endif

/* ---------------------------------------------------------------------------- */
/* Useful macros for testing various common flag combinations */

/* Constraint Target Macros */
#define VALID_CONS_TARGET(ct) ((ct) && (ct->tar))

static CLG_LogRef LOG = {"bke.constraint"};

/* ************************ Constraints - General Utilities *************************** */
/* These functions here don't act on any specific constraints, and are therefore should/will
 * not require any of the special function-pointers afforded by the relevant constraint
 * type-info structs.
 */

static void damptrack_do_transform(float matrix[4][4], const float tarvec[3], int track_axis);

static bConstraint *constraint_find_original(Object *ob,
                                             bPoseChannel *pchan,
                                             bConstraint *con,
                                             Object **r_orig_ob);
static bConstraint *constraint_find_original_for_update(bConstraintOb *cob, bConstraint *con);

/* -------------- Naming -------------- */

void BKE_constraint_unique_name(bConstraint *con, ListBase *list)
{
  BLI_uniquename(list, con, DATA_("Const"), '.', offsetof(bConstraint, name), sizeof(con->name));
}

/* ----------------- Evaluation Loop Preparation --------------- */

/* package an object/bone for use in constraint evaluation */
bConstraintOb *BKE_constraints_make_evalob(
    Depsgraph *depsgraph, Scene *scene, Object *ob, void *subdata, short datatype)
{
  bConstraintOb *cob;

  /* create regardless of whether we have any data! */
  cob = MEM_callocN(sizeof(bConstraintOb), "bConstraintOb");

  /* for system time, part of deglobalization, code nicer later with local time (ton) */
  cob->scene = scene;
  cob->depsgraph = depsgraph;

  /* based on type of available data */
  switch (datatype) {
    case CONSTRAINT_OBTYPE_OBJECT: {
      /* disregard subdata... calloc should set other values right */
      if (ob) {
        cob->ob = ob;
        cob->type = datatype;

        if (cob->ob->rotmode > 0) {
          /* Should be some kind of Euler order, so use it */
          /* NOTE: Versions <= 2.76 assumed that "default" order
           *       would always get used, so we may seem some rig
           *       breakage as a result. However, this change here
           *       is needed to fix T46599
           */
          cob->rotOrder = ob->rotmode;
        }
        else {
          /* Quats/Axis-Angle, so Eulers should just use default order */
          cob->rotOrder = EULER_ORDER_DEFAULT;
        }
        copy_m4_m4(cob->matrix, ob->obmat);
      }
      else {
        unit_m4(cob->matrix);
      }

      copy_m4_m4(cob->startmat, cob->matrix);
      break;
    }
    case CONSTRAINT_OBTYPE_BONE: {
      /* only set if we have valid bone, otherwise default */
      if (ob && subdata) {
        cob->ob = ob;
        cob->pchan = (bPoseChannel *)subdata;
        cob->type = datatype;

        if (cob->pchan->rotmode > 0) {
          /* should be some type of Euler order */
          cob->rotOrder = cob->pchan->rotmode;
        }
        else {
          /* Quats, so eulers should just use default order */
          cob->rotOrder = EULER_ORDER_DEFAULT;
        }

        /* matrix in world-space */
        mul_m4_m4m4(cob->matrix, ob->obmat, cob->pchan->pose_mat);
      }
      else {
        unit_m4(cob->matrix);
      }

      copy_m4_m4(cob->startmat, cob->matrix);
      break;
    }
    default: /* other types not yet handled */
      unit_m4(cob->matrix);
      unit_m4(cob->startmat);
      break;
  }

  return cob;
}

void BKE_constraints_clear_evalob(bConstraintOb *cob)
{
  float delta[4][4], imat[4][4];

  /* prevent crashes */
  if (cob == NULL) {
    return;
  }

  /* calculate delta of constraints evaluation */
  invert_m4_m4(imat, cob->startmat);
  /* XXX This would seem to be in wrong order. However, it does not work in 'right' order -
   *     would be nice to understand why premul is needed here instead of usual postmul?
   *     In any case, we **do not get a delta** here (e.g. startmat & matrix having same location,
   *     still gives a 'delta' with non-null translation component :/ ). */
  mul_m4_m4m4(delta, cob->matrix, imat);

  /* copy matrices back to source */
  switch (cob->type) {
    case CONSTRAINT_OBTYPE_OBJECT: {
      /* cob->ob might not exist! */
      if (cob->ob) {
        /* copy new ob-matrix back to owner */
        copy_m4_m4(cob->ob->obmat, cob->matrix);

        /* copy inverse of delta back to owner */
        invert_m4_m4(cob->ob->constinv, delta);
      }
      break;
    }
    case CONSTRAINT_OBTYPE_BONE: {
      /* cob->ob or cob->pchan might not exist */
      if (cob->ob && cob->pchan) {
        /* copy new pose-matrix back to owner */
        mul_m4_m4m4(cob->pchan->pose_mat, cob->ob->imat, cob->matrix);

        /* copy inverse of delta back to owner */
        invert_m4_m4(cob->pchan->constinv, delta);
      }
      break;
    }
  }

  /* free tempolary struct */
  MEM_freeN(cob);
}

/* -------------- Space-Conversion API -------------- */

void BKE_constraint_mat_convertspace(Object *ob,
                                     bPoseChannel *pchan,
                                     bConstraintOb *cob,
                                     float mat[4][4],
                                     short from,
                                     short to,
                                     const bool keep_scale)
{
  float diff_mat[4][4];
  float imat[4][4];

  /* Prevent crashes in these unlikely events. */
  if (ob == NULL || mat == NULL) {
    return;
  }
  /* optimize trick - check if need to do anything */
  if (from == to) {
    return;
  }

  /* are we dealing with pose-channels or objects */
  if (pchan) {
    /* pose channels */
    switch (from) {
      case CONSTRAINT_SPACE_WORLD: /* ---------- FROM WORLDSPACE ---------- */
      {
        if (to == CONSTRAINT_SPACE_CUSTOM) {
          /* World to custom. */
          BLI_assert(cob);
          invert_m4_m4(imat, cob->space_obj_world_matrix);
          mul_m4_m4m4(mat, imat, mat);
        }
        else {
          /* World to pose. */
          invert_m4_m4(imat, ob->obmat);
          mul_m4_m4m4(mat, imat, mat);

          /* Use pose-space as stepping stone for other spaces. */
          if (ELEM(to,
                   CONSTRAINT_SPACE_LOCAL,
                   CONSTRAINT_SPACE_PARLOCAL,
                   CONSTRAINT_SPACE_OWNLOCAL)) {
            /* Call self with slightly different values. */
            BKE_constraint_mat_convertspace(
                ob, pchan, cob, mat, CONSTRAINT_SPACE_POSE, to, keep_scale);
          }
        }
        break;
      }
      case CONSTRAINT_SPACE_POSE: /* ---------- FROM POSESPACE ---------- */
      {
        /* pose to local */
        if (to == CONSTRAINT_SPACE_LOCAL) {
          if (pchan->bone) {
            BKE_armature_mat_pose_to_bone(pchan, mat, mat);
          }
        }
        /* pose to owner local */
        else if (to == CONSTRAINT_SPACE_OWNLOCAL) {
          /* pose to local */
          if (pchan->bone) {
            BKE_armature_mat_pose_to_bone(pchan, mat, mat);
          }

          /* local to owner local (recursive) */
          BKE_constraint_mat_convertspace(
              ob, pchan, cob, mat, CONSTRAINT_SPACE_LOCAL, to, keep_scale);
        }
        /* pose to local with parent */
        else if (to == CONSTRAINT_SPACE_PARLOCAL) {
          if (pchan->bone) {
            invert_m4_m4(imat, pchan->bone->arm_mat);
            mul_m4_m4m4(mat, imat, mat);
          }
        }
        else {
          /* Pose to world. */
          mul_m4_m4m4(mat, ob->obmat, mat);
          /* Use world-space as stepping stone for other spaces. */
          if (to != CONSTRAINT_SPACE_WORLD) {
            /* Call self with slightly different values. */
            BKE_constraint_mat_convertspace(
                ob, pchan, cob, mat, CONSTRAINT_SPACE_WORLD, to, keep_scale);
          }
        }
        break;
      }
      case CONSTRAINT_SPACE_LOCAL: /* ------------ FROM LOCALSPACE --------- */
      {
        /* local to owner local */
        if (to == CONSTRAINT_SPACE_OWNLOCAL) {
          if (pchan->bone) {
            copy_m4_m4(diff_mat, pchan->bone->arm_mat);

            if (cob && cob->pchan && cob->pchan->bone) {
              invert_m4_m4(imat, cob->pchan->bone->arm_mat);
              mul_m4_m4m4(diff_mat, imat, diff_mat);
            }

            zero_v3(diff_mat[3]);
            invert_m4_m4(imat, diff_mat);
            mul_m4_series(mat, diff_mat, mat, imat);
          }
        }
        /* local to pose - do inverse procedure that was done for pose to local */
        else {
          if (pchan->bone) {
            /* we need the posespace_matrix = local_matrix + (parent_posespace_matrix + restpos) */
            BKE_armature_mat_bone_to_pose(pchan, mat, mat);
          }

          /* use pose-space as stepping stone for other spaces */
          if (ELEM(to,
                   CONSTRAINT_SPACE_WORLD,
                   CONSTRAINT_SPACE_PARLOCAL,
                   CONSTRAINT_SPACE_CUSTOM)) {
            /* call self with slightly different values */
            BKE_constraint_mat_convertspace(
                ob, pchan, cob, mat, CONSTRAINT_SPACE_POSE, to, keep_scale);
          }
        }
        break;
      }
      case CONSTRAINT_SPACE_OWNLOCAL: { /* -------------- FROM OWNER LOCAL ---------- */
        /* owner local to local */
        if (pchan->bone) {
          copy_m4_m4(diff_mat, pchan->bone->arm_mat);

          if (cob && cob->pchan && cob->pchan->bone) {
            invert_m4_m4(imat, cob->pchan->bone->arm_mat);
            mul_m4_m4m4(diff_mat, imat, diff_mat);
          }

          zero_v3(diff_mat[3]);
          invert_m4_m4(imat, diff_mat);
          mul_m4_series(mat, imat, mat, diff_mat);
        }

        if (to != CONSTRAINT_SPACE_LOCAL) {
          /* call self with slightly different values */
          BKE_constraint_mat_convertspace(
              ob, pchan, cob, mat, CONSTRAINT_SPACE_LOCAL, to, keep_scale);
        }
        break;
      }
      case CONSTRAINT_SPACE_PARLOCAL: /* -------------- FROM LOCAL WITH PARENT ---------- */
      {
        /* local + parent to pose */
        if (pchan->bone) {
          mul_m4_m4m4(mat, pchan->bone->arm_mat, mat);
        }

        /* use pose-space as stepping stone for other spaces */
        if (ELEM(to,
                 CONSTRAINT_SPACE_WORLD,
                 CONSTRAINT_SPACE_LOCAL,
                 CONSTRAINT_SPACE_OWNLOCAL,
                 CONSTRAINT_SPACE_CUSTOM)) {
          /* call self with slightly different values */
          BKE_constraint_mat_convertspace(
              ob, pchan, cob, mat, CONSTRAINT_SPACE_POSE, to, keep_scale);
        }
        break;
      }
      case CONSTRAINT_SPACE_CUSTOM: /* -------------- FROM CUSTOM SPACE ---------- */
      {
        /* Custom to world. */
        BLI_assert(cob);
        mul_m4_m4m4(mat, cob->space_obj_world_matrix, mat);

        /* Use world-space as stepping stone for other spaces. */
        if (to != CONSTRAINT_SPACE_WORLD) {
          /* Call self with slightly different values. */
          BKE_constraint_mat_convertspace(
              ob, pchan, cob, mat, CONSTRAINT_SPACE_WORLD, to, keep_scale);
        }
        break;
      }
    }
  }
  else {
    /* objects */
    if (from == CONSTRAINT_SPACE_WORLD) {
      if (to == CONSTRAINT_SPACE_LOCAL) {
        /* Check if object has a parent. */
        if (ob->parent) {
          /* 'subtract' parent's effects from owner. */
          mul_m4_m4m4(diff_mat, ob->parent->obmat, ob->parentinv);
          invert_m4_m4_safe(imat, diff_mat);
          mul_m4_m4m4(mat, imat, mat);
        }
        else {
          /* Local space in this case will have to be defined as local to the owner's
           * transform-property-rotated axes. So subtract this rotation component.
           */
          /* XXX This is actually an ugly hack, local space of a parent-less object *is* the same
           * as global space! Think what we want actually here is some kind of 'Final Space', i.e
           *     . once transformations are applied - users are often confused about this too,
           *     this is not consistent with bones
           *     local space either... Meh :|
           *     --mont29
           */
          BKE_object_to_mat4(ob, diff_mat);
          if (!keep_scale) {
            normalize_m4(diff_mat);
          }
          zero_v3(diff_mat[3]);

          invert_m4_m4_safe(imat, diff_mat);
          mul_m4_m4m4(mat, imat, mat);
        }
      }
      else if (to == CONSTRAINT_SPACE_CUSTOM) {
        /* 'subtract' custom objects's effects from owner. */
        BLI_assert(cob);
        invert_m4_m4_safe(imat, cob->space_obj_world_matrix);
        mul_m4_m4m4(mat, imat, mat);
      }
    }
    else if (from == CONSTRAINT_SPACE_LOCAL) {
      /* check that object has a parent - otherwise this won't work */
      if (ob->parent) {
        /* 'add' parent's effect back to owner */
        mul_m4_m4m4(diff_mat, ob->parent->obmat, ob->parentinv);
        mul_m4_m4m4(mat, diff_mat, mat);
      }
      else {
        /* Local space in this case will have to be defined as local to the owner's
         * transform-property-rotated axes. So add back this rotation component.
         */
        /* XXX See comment above for world->local case... */
        BKE_object_to_mat4(ob, diff_mat);
        if (!keep_scale) {
          normalize_m4(diff_mat);
        }
        zero_v3(diff_mat[3]);

        mul_m4_m4m4(mat, diff_mat, mat);
      }
      if (to == CONSTRAINT_SPACE_CUSTOM) {
        /* 'subtract' objects's effects from owner. */
        BLI_assert(cob);
        invert_m4_m4_safe(imat, cob->space_obj_world_matrix);
        mul_m4_m4m4(mat, imat, mat);
      }
    }
    else if (from == CONSTRAINT_SPACE_CUSTOM) {
      /* Custom to world. */
      BLI_assert(cob);
      mul_m4_m4m4(mat, cob->space_obj_world_matrix, mat);

      /* Use world-space as stepping stone for other spaces. */
      if (to != CONSTRAINT_SPACE_WORLD) {
        /* Call self with slightly different values. */
        BKE_constraint_mat_convertspace(
            ob, pchan, cob, mat, CONSTRAINT_SPACE_WORLD, to, keep_scale);
      }
    }
  }
}

/* ------------ General Target Matrix Tools ---------- */

/* function that sets the given matrix based on given vertex group in mesh */
static void contarget_get_mesh_mat(Object *ob, const char *substring, float mat[4][4])
{
  /* when not in EditMode, use the 'final' evaluated mesh, depsgraph
   * ensures we build with CD_MDEFORMVERT layer
   */
  const Mesh *me_eval = BKE_object_get_evaluated_mesh(ob);
  BMEditMesh *em = BKE_editmesh_from_object(ob);
  float plane[3];
  float imat[3][3], tmat[3][3];
  const int defgroup = BKE_object_defgroup_name_index(ob, substring);

  /* initialize target matrix using target matrix */
  copy_m4_m4(mat, ob->obmat);

  /* get index of vertex group */
  if (defgroup == -1) {
    return;
  }

  float vec[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 0.0f};
  float weightsum = 0.0f;
  if (me_eval) {
    const float(*vert_normals)[3] = BKE_mesh_vertex_normals_ensure(me_eval);
    const MDeformVert *dvert = CustomData_get_layer(&me_eval->vdata, CD_MDEFORMVERT);
    int numVerts = me_eval->totvert;

    /* check that dvert is a valid pointers (just in case) */
    if (dvert) {

      /* get the average of all verts with that are in the vertex-group */
      for (int i = 0; i < numVerts; i++) {
        const MDeformVert *dv = &dvert[i];
        const MVert *mv = &me_eval->mvert[i];
        const MDeformWeight *dw = BKE_defvert_find_index(dv, defgroup);

        if (dw && dw->weight > 0.0f) {
          madd_v3_v3fl(vec, mv->co, dw->weight);
          madd_v3_v3fl(normal, vert_normals[i], dw->weight);
          weightsum += dw->weight;
        }
      }
    }
  }
  else if (em) {
    if (CustomData_has_layer(&em->bm->vdata, CD_MDEFORMVERT)) {
      BMVert *v;
      BMIter iter;

      BM_ITER_MESH (v, &iter, em->bm, BM_VERTS_OF_MESH) {
        MDeformVert *dv = CustomData_bmesh_get(&em->bm->vdata, v->head.data, CD_MDEFORMVERT);
        MDeformWeight *dw = BKE_defvert_find_index(dv, defgroup);

        if (dw && dw->weight > 0.0f) {
          madd_v3_v3fl(vec, v->co, dw->weight);
          madd_v3_v3fl(normal, v->no, dw->weight);
          weightsum += dw->weight;
        }
      }
    }
  }
  else {
    /* No valid edit or evaluated mesh, just abort. */
    return;
  }

  /* calculate averages of normal and coordinates */
  if (weightsum > 0) {
    mul_v3_fl(vec, 1.0f / weightsum);
    mul_v3_fl(normal, 1.0f / weightsum);
  }

  /* derive the rotation from the average normal:
   * - code taken from transform_gizmo.c,
   *   calc_gizmo_stats, V3D_ORIENT_NORMAL case */

  /* We need the transpose of the inverse for a normal. */
  copy_m3_m4(imat, ob->obmat);

  invert_m3_m3(tmat, imat);
  transpose_m3(tmat);
  mul_m3_v3(tmat, normal);

  normalize_v3(normal);
  copy_v3_v3(plane, tmat[1]);

  cross_v3_v3v3(mat[0], normal, plane);
  if (len_squared_v3(mat[0]) < square_f(1e-3f)) {
    copy_v3_v3(plane, tmat[0]);
    cross_v3_v3v3(mat[0], normal, plane);
  }

  copy_v3_v3(mat[2], normal);
  cross_v3_v3v3(mat[1], mat[2], mat[0]);

  normalize_m4(mat);

  /* apply the average coordinate as the new location */
  mul_v3_m4v3(mat[3], ob->obmat, vec);
}

/* function that sets the given matrix based on given vertex group in lattice */
static void contarget_get_lattice_mat(Object *ob, const char *substring, float mat[4][4])
{
  Lattice *lt = (Lattice *)ob->data;

  DispList *dl = ob->runtime.curve_cache ?
                     BKE_displist_find(&ob->runtime.curve_cache->disp, DL_VERTS) :
                     NULL;
  const float *co = dl ? dl->verts : NULL;
  BPoint *bp = lt->def;

  MDeformVert *dv = lt->dvert;
  int tot_verts = lt->pntsu * lt->pntsv * lt->pntsw;
  float vec[3] = {0.0f, 0.0f, 0.0f}, tvec[3];
  int grouped = 0;
  int i, n;
  const int defgroup = BKE_object_defgroup_name_index(ob, substring);

  /* initialize target matrix using target matrix */
  copy_m4_m4(mat, ob->obmat);

  /* get index of vertex group */
  if (defgroup == -1) {
    return;
  }
  if (dv == NULL) {
    return;
  }

  /* 1. Loop through control-points checking if in nominated vertex-group.
   * 2. If it is, add it to vec to find the average point.
   */
  for (i = 0; i < tot_verts; i++, dv++) {
    for (n = 0; n < dv->totweight; n++) {
      MDeformWeight *dw = BKE_defvert_find_index(dv, defgroup);
      if (dw && dw->weight > 0.0f) {
        /* copy coordinates of point to temporary vector, then add to find average */
        memcpy(tvec, co ? co : bp->vec, sizeof(float[3]));

        add_v3_v3(vec, tvec);
        grouped++;
      }
    }

    /* advance pointer to coordinate data */
    if (co) {
      co += 3;
    }
    else {
      bp++;
    }
  }

  /* find average location, then multiply by ob->obmat to find world-space location */
  if (grouped) {
    mul_v3_fl(vec, 1.0f / grouped);
  }
  mul_v3_m4v3(tvec, ob->obmat, vec);

  /* copy new location to matrix */
  copy_v3_v3(mat[3], tvec);
}

/* generic function to get the appropriate matrix for most target cases */
/* The cases where the target can be object data have not been implemented */
static void constraint_target_to_mat4(Object *ob,
                                      const char *substring,
                                      bConstraintOb *cob,
                                      float mat[4][4],
                                      short from,
                                      short to,
                                      short flag,
                                      float headtail)
{
  /* Case OBJECT */
  if (substring[0] == '\0') {
    copy_m4_m4(mat, ob->obmat);
    BKE_constraint_mat_convertspace(ob, NULL, cob, mat, from, to, false);
  }
  /* Case VERTEXGROUP */
  /* Current method just takes the average location of all the points in the
   * VertexGroup, and uses that as the location value of the targets. Where
   * possible, the orientation will also be calculated, by calculating an
   * 'average' vertex normal, and deriving the rotation from that.
   *
   * NOTE: EditMode is not currently supported, and will most likely remain that
   *       way as constraints can only really affect things on object/bone level.
   */
  else if (ob->type == OB_MESH) {
    contarget_get_mesh_mat(ob, substring, mat);
    BKE_constraint_mat_convertspace(ob, NULL, cob, mat, from, to, false);
  }
  else if (ob->type == OB_LATTICE) {
    contarget_get_lattice_mat(ob, substring, mat);
    BKE_constraint_mat_convertspace(ob, NULL, cob, mat, from, to, false);
  }
  /* Case BONE */
  else {
    bPoseChannel *pchan;

    pchan = BKE_pose_channel_find_name(ob->pose, substring);
    if (pchan) {
      /* Multiply the PoseSpace accumulation/final matrix for this
       * PoseChannel by the Armature Object's Matrix to get a world-space matrix.
       */
      bool is_bbone = (pchan->bone) && (pchan->bone->segments > 1) &&
                      (flag & CONSTRAINT_BBONE_SHAPE);
      bool full_bbone = (flag & CONSTRAINT_BBONE_SHAPE_FULL) != 0;

      if (headtail < 0.000001f && !(is_bbone && full_bbone)) {
        /* skip length interpolation if set to head */
        mul_m4_m4m4(mat, ob->obmat, pchan->pose_mat);
      }
      else if (is_bbone && pchan->bone->segments == pchan->runtime.bbone_segments) {
        /* use point along bbone */
        Mat4 *bbone = pchan->runtime.bbone_pose_mats;
        float tempmat[4][4];
        float loc[3], fac;
        int index;

        /* figure out which segment(s) the headtail value falls in */
        BKE_pchan_bbone_deform_segment_index(pchan, headtail, &index, &fac);

        /* apply full transformation of the segment if requested */
        if (full_bbone) {
          interp_m4_m4m4(tempmat, bbone[index].mat, bbone[index + 1].mat, fac);

          mul_m4_m4m4(tempmat, pchan->pose_mat, tempmat);
        }
        /* only interpolate location */
        else {
          interp_v3_v3v3(loc, bbone[index].mat[3], bbone[index + 1].mat[3], fac);

          copy_m4_m4(tempmat, pchan->pose_mat);
          mul_v3_m4v3(tempmat[3], pchan->pose_mat, loc);
        }

        mul_m4_m4m4(mat, ob->obmat, tempmat);
      }
      else {
        float tempmat[4][4], loc[3];

        /* interpolate along length of bone */
        interp_v3_v3v3(loc, pchan->pose_head, pchan->pose_tail, headtail);

        /* use interpolated distance for subtarget */
        copy_m4_m4(tempmat, pchan->pose_mat);
        copy_v3_v3(tempmat[3], loc);

        mul_m4_m4m4(mat, ob->obmat, tempmat);
      }
    }
    else {
      copy_m4_m4(mat, ob->obmat);
    }

    /* convert matrix space as required */
    BKE_constraint_mat_convertspace(ob, pchan, cob, mat, from, to, false);
  }
}

/* ************************* Specific Constraints ***************************** */
/* Each constraint defines a set of functions, which will be called at the appropriate
 * times. In addition to this, each constraint should have a type-info struct, where
 * its functions are attached for use.
 */

/* Template for type-info data:
 * - make a copy of this when creating new constraints, and just change the functions
 *   pointed to as necessary
 * - although the naming of functions doesn't matter, it would help for code
 *   readability, to follow the same naming convention as is presented here
 * - any functions that a constraint doesn't need to define, don't define
 *   for such cases, just use NULL
 * - these should be defined after all the functions have been defined, so that
 *   forward-definitions/prototypes don't need to be used!
 * - keep this copy #if-def'd so that future constraints can get based off this
 */
#if 0
static bConstraintTypeInfo CTI_CONSTRNAME = {
    CONSTRAINT_TYPE_CONSTRNAME,    /* type */
    sizeof(bConstrNameConstraint), /* size */
    "ConstrName",                  /* name */
    "bConstrNameConstraint",       /* struct name */
    constrname_free,               /* free data */
    constrname_id_looper,          /* id looper */
    constrname_copy,               /* copy data */
    constrname_new_data,           /* new data */
    constrname_get_tars,           /* get constraint targets */
    constrname_flush_tars,         /* flush constraint targets */
    constrname_get_tarmat,         /* get target matrix */
    constrname_evaluate,           /* evaluate */
};
#endif

/* This function should be used for the get_target_matrix member of all
 * constraints that are not picky about what happens to their target matrix.
 */
static void default_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                               bConstraint *con,
                               bConstraintOb *cob,
                               bConstraintTarget *ct,
                               float UNUSED(ctime))
{
  if (VALID_CONS_TARGET(ct)) {
    constraint_target_to_mat4(ct->tar,
                              ct->subtarget,
                              cob,
                              ct->matrix,
                              CONSTRAINT_SPACE_WORLD,
                              ct->space,
                              con->flag,
                              con->headtail);
  }
  else if (ct) {
    unit_m4(ct->matrix);
  }
}

/* This is a variant that extracts full transformation from B-Bone segments.
 */
static void default_get_tarmat_full_bbone(struct Depsgraph *UNUSED(depsgraph),
                                          bConstraint *con,
                                          bConstraintOb *cob,
                                          bConstraintTarget *ct,
                                          float UNUSED(ctime))
{
  if (VALID_CONS_TARGET(ct)) {
    constraint_target_to_mat4(ct->tar,
                              ct->subtarget,
                              cob,
                              ct->matrix,
                              CONSTRAINT_SPACE_WORLD,
                              ct->space,
                              con->flag | CONSTRAINT_BBONE_SHAPE_FULL,
                              con->headtail);
  }
  else if (ct) {
    unit_m4(ct->matrix);
  }
}

/* This following macro should be used for all standard single-target *_get_tars functions
 * to save typing and reduce maintenance woes.
 * (Hopefully all compilers will be happy with the lines with just a space on them.
 * Those are really just to help this code easier to read).
 */
/* TODO: cope with getting rotation order... */
#define SINGLETARGET_GET_TARS(con, datatar, datasubtarget, ct, list) \
  { \
    ct = MEM_callocN(sizeof(bConstraintTarget), "tempConstraintTarget"); \
\
    ct->tar = datatar; \
    BLI_strncpy(ct->subtarget, datasubtarget, sizeof(ct->subtarget)); \
    ct->space = con->tarspace; \
    ct->flag = CONSTRAINT_TAR_TEMP; \
\
    if (ct->tar) { \
      if ((ct->tar->type == OB_ARMATURE) && (ct->subtarget[0])) { \
        bPoseChannel *pchan = BKE_pose_channel_find_name(ct->tar->pose, ct->subtarget); \
        ct->type = CONSTRAINT_OBTYPE_BONE; \
        ct->rotOrder = (pchan) ? (pchan->rotmode) : EULER_ORDER_DEFAULT; \
      } \
      else if (OB_TYPE_SUPPORT_VGROUP(ct->tar->type) && (ct->subtarget[0])) { \
        ct->type = CONSTRAINT_OBTYPE_VERT; \
        ct->rotOrder = EULER_ORDER_DEFAULT; \
      } \
      else { \
        ct->type = CONSTRAINT_OBTYPE_OBJECT; \
        ct->rotOrder = ct->tar->rotmode; \
      } \
    } \
\
    BLI_addtail(list, ct); \
  } \
  (void)0

/* This following macro should be used for all standard single-target *_get_tars functions
 * to save typing and reduce maintenance woes. It does not do the subtarget related operations
 * (Hopefully all compilers will be happy with the lines with just a space on them. Those are
 *  really just to help this code easier to read)
 */
/* TODO: cope with getting rotation order... */
#define SINGLETARGETNS_GET_TARS(con, datatar, ct, list) \
  { \
    ct = MEM_callocN(sizeof(bConstraintTarget), "tempConstraintTarget"); \
\
    ct->tar = datatar; \
    ct->space = con->tarspace; \
    ct->flag = CONSTRAINT_TAR_TEMP; \
\
    if (ct->tar) { \
      ct->type = CONSTRAINT_OBTYPE_OBJECT; \
    } \
    BLI_addtail(list, ct); \
  } \
  (void)0

/* This following macro should be used for all standard single-target *_flush_tars functions
 * to save typing and reduce maintenance woes.
 * NOTE: the pointer to ct will be changed to point to the next in the list (as it gets removed)
 * (Hopefully all compilers will be happy with the lines with just a space on them. Those are
 *  really just to help this code easier to read)
 */
#define SINGLETARGET_FLUSH_TARS(con, datatar, datasubtarget, ct, list, no_copy) \
  { \
    if (ct) { \
      bConstraintTarget *ctn = ct->next; \
      if (no_copy == 0) { \
        datatar = ct->tar; \
        BLI_strncpy(datasubtarget, ct->subtarget, sizeof(datasubtarget)); \
        con->tarspace = (char)ct->space; \
      } \
\
      BLI_freelinkN(list, ct); \
      ct = ctn; \
    } \
  } \
  (void)0

/* This following macro should be used for all standard single-target *_flush_tars functions
 * to save typing and reduce maintenance woes. It does not do the subtarget related operations.
 * NOTE: the pointer to ct will be changed to point to the next in the list (as it gets removed)
 * (Hopefully all compilers will be happy with the lines with just a space on them. Those are
 *  really just to help this code easier to read)
 */
#define SINGLETARGETNS_FLUSH_TARS(con, datatar, ct, list, no_copy) \
  { \
    if (ct) { \
      bConstraintTarget *ctn = ct->next; \
      if (no_copy == 0) { \
        datatar = ct->tar; \
        con->tarspace = (char)ct->space; \
      } \
\
      BLI_freelinkN(list, ct); \
      ct = ctn; \
    } \
  } \
  (void)0

static void custom_space_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  func(con, (ID **)&con->space_object, false, userdata);
}

static int get_space_tar(bConstraint *con, ListBase *list)
{
  if (!con || !list ||
      (con->ownspace != CONSTRAINT_SPACE_CUSTOM && con->tarspace != CONSTRAINT_SPACE_CUSTOM)) {
    return 0;
  }
  bConstraintTarget *ct;
  SINGLETARGET_GET_TARS(con, con->space_object, con->space_subtarget, ct, list);
  return 1;
}
