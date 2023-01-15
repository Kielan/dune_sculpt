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

static void flush_space_tar(bConstraint *con, ListBase *list, bool no_copy)
{
  if (!con || !list ||
      (con->ownspace != CONSTRAINT_SPACE_CUSTOM && con->tarspace != CONSTRAINT_SPACE_CUSTOM)) {
    return;
  }
  bConstraintTarget *ct = (bConstraintTarget *)list->last;
  SINGLETARGET_FLUSH_TARS(con, con->space_object, con->space_subtarget, ct, list, no_copy);
}

/* --------- ChildOf Constraint ------------ */

static void childof_new_data(void *cdata)
{
  bChildOfConstraint *data = (bChildOfConstraint *)cdata;

  data->flag = (CHILDOF_LOCX | CHILDOF_LOCY | CHILDOF_LOCZ | CHILDOF_ROTX | CHILDOF_ROTY |
                CHILDOF_ROTZ | CHILDOF_SIZEX | CHILDOF_SIZEY | CHILDOF_SIZEZ |
                CHILDOF_SET_INVERSE);
  unit_m4(data->invmat);
}

static void childof_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bChildOfConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int childof_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void childof_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

static void childof_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bChildOfConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (!VALID_CONS_TARGET(ct)) {
    return;
  }

  float parmat[4][4];
  float inverse_matrix[4][4];
  /* Simple matrix parenting. */
  if ((data->flag & CHILDOF_ALL) == CHILDOF_ALL) {
    copy_m4_m4(parmat, ct->matrix);
    copy_m4_m4(inverse_matrix, data->invmat);
  }
  /* Filter the parent matrix by channel. */
  else {
    float loc[3], eul[3], size[3];
    float loco[3], eulo[3], sizeo[3];

    /* extract components of both matrices */
    copy_v3_v3(loc, ct->matrix[3]);
    mat4_to_eulO(eul, ct->rotOrder, ct->matrix);
    mat4_to_size(size, ct->matrix);

    copy_v3_v3(loco, data->invmat[3]);
    mat4_to_eulO(eulo, cob->rotOrder, data->invmat);
    mat4_to_size(sizeo, data->invmat);

    /* Reset the locked channels to their no-op values. */
    if (!(data->flag & CHILDOF_LOCX)) {
      loc[0] = loco[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCY)) {
      loc[1] = loco[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCZ)) {
      loc[2] = loco[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTX)) {
      eul[0] = eulo[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTY)) {
      eul[1] = eulo[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTZ)) {
      eul[2] = eulo[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_SIZEX)) {
      size[0] = sizeo[0] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEY)) {
      size[1] = sizeo[1] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEZ)) {
      size[2] = sizeo[2] = 1.0f;
    }

    /* Construct the new matrices given the disabled channels. */
    loc_eulO_size_to_mat4(parmat, loc, eul, size, ct->rotOrder);
    loc_eulO_size_to_mat4(inverse_matrix, loco, eulo, sizeo, cob->rotOrder);
  }

  /* If requested, compute the inverse matrix from the computed parent matrix. */
  if (data->flag & CHILDOF_SET_INVERSE) {
    invert_m4_m4(data->invmat, parmat);
    if (cob->pchan != NULL) {
      mul_m4_series(data->invmat, data->invmat, cob->ob->obmat);
    }

    copy_m4_m4(inverse_matrix, data->invmat);

    data->flag &= ~CHILDOF_SET_INVERSE;

    /* Write the computed matrix back to the master copy if in COW evaluation. */
    bConstraint *orig_con = constraint_find_original_for_update(cob, con);

    if (orig_con != NULL) {
      bChildOfConstraint *orig_data = orig_con->data;

      copy_m4_m4(orig_data->invmat, data->invmat);
      orig_data->flag &= ~CHILDOF_SET_INVERSE;
    }
  }

  /* Multiply together the target (parent) matrix, parent inverse,
   * and the owner transform matrix to get the effect of this constraint
   * (i.e.  owner is 'parented' to parent). */
  float orig_cob_matrix[4][4];
  copy_m4_m4(orig_cob_matrix, cob->matrix);
  mul_m4_series(cob->matrix, parmat, inverse_matrix, orig_cob_matrix);

  /* Without this, changes to scale and rotation can change location
   * of a parentless bone or a disconnected bone. Even though its set
   * to zero above. */
  if (!(data->flag & CHILDOF_LOCX)) {
    cob->matrix[3][0] = orig_cob_matrix[3][0];
  }
  if (!(data->flag & CHILDOF_LOCY)) {
    cob->matrix[3][1] = orig_cob_matrix[3][1];
  }
  if (!(data->flag & CHILDOF_LOCZ)) {
    cob->matrix[3][2] = orig_cob_matrix[3][2];
  }
}

/* XXX NOTE: con->flag should be CONSTRAINT_SPACEONCE for bone-childof, patched in `readfile.c`. */
static bConstraintTypeInfo CTI_CHILDOF = {
    CONSTRAINT_TYPE_CHILDOF,    /* type */
    sizeof(bChildOfConstraint), /* size */
    "Child Of",                 /* name */
    "bChildOfConstraint",       /* struct name */
    NULL,                       /* free data */
    childof_id_looper,          /* id looper */
    NULL,                       /* copy data */
    childof_new_data,           /* new data */
    childof_get_tars,           /* get constraint targets */
    childof_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get a target matrix */
    childof_evaluate,           /* evaluate */
};

/* -------- TrackTo Constraint ------- */

static void trackto_new_data(void *cdata)
{
  bTrackToConstraint *data = (bTrackToConstraint *)cdata;

  data->reserved1 = TRACK_nZ;
  data->reserved2 = UP_Y;
}

static void trackto_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bTrackToConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int trackto_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void trackto_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static int basis_cross(int n, int m)
{
  switch (n - m) {
    case 1:
    case -2:
      return 1;

    case -1:
    case 2:
      return -1;

    default:
      return 0;
  }
}

static void vectomat(const float vec[3],
                     const float target_up[3],
                     short axis,
                     short upflag,
                     short flags,
                     float m[3][3])
{
  float n[3];
  float u[3]; /* vector specifying the up axis */
  float proj[3];
  float right[3];
  float neg = -1;
  int right_index;

  if (normalize_v3_v3(n, vec) == 0.0f) {
    n[0] = 0.0f;
    n[1] = 0.0f;
    n[2] = 1.0f;
  }
  if (axis > 2) {
    axis -= 3;
  }
  else {
    negate_v3(n);
  }

  /* n specifies the transformation of the track axis */
  if (flags & TARGET_Z_UP) {
    /* target Z axis is the global up axis */
    copy_v3_v3(u, target_up);
  }
  else {
    /* world Z axis is the global up axis */
    u[0] = 0;
    u[1] = 0;
    u[2] = 1;
  }

  /* NOTE: even though 'n' is normalized, don't use 'project_v3_v3v3_normalized' below
   * because precision issues cause a problem in near degenerate states, see: T53455. */

  /* project the up vector onto the plane specified by n */
  project_v3_v3v3(proj, u, n); /* first u onto n... */
  sub_v3_v3v3(proj, u, proj);  /* then onto the plane */
  /* proj specifies the transformation of the up axis */

  if (normalize_v3(proj) == 0.0f) { /* degenerate projection */
    proj[0] = 0.0f;
    proj[1] = 1.0f;
    proj[2] = 0.0f;
  }

  /* Normalized cross product of n and proj specifies transformation of the right axis */
  cross_v3_v3v3(right, proj, n);
  normalize_v3(right);

  if (axis != upflag) {
    right_index = 3 - axis - upflag;
    neg = (float)basis_cross(axis, upflag);

    /* account for up direction, track direction */
    m[right_index][0] = neg * right[0];
    m[right_index][1] = neg * right[1];
    m[right_index][2] = neg * right[2];

    copy_v3_v3(m[upflag], proj);

    copy_v3_v3(m[axis], n);
  }
  /* identity matrix - don't do anything if the two axes are the same */
  else {
    unit_m3(m);
  }
}

static void trackto_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bTrackToConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float size[3], vec[3];
    float totmat[3][3];

    /* Get size property, since ob->scale is only the object's own relative size,
     * not its global one. */
    mat4_to_size(size, cob->matrix);

    /* Clear the object's rotation */
    cob->matrix[0][0] = size[0];
    cob->matrix[0][1] = 0;
    cob->matrix[0][2] = 0;
    cob->matrix[1][0] = 0;
    cob->matrix[1][1] = size[1];
    cob->matrix[1][2] = 0;
    cob->matrix[2][0] = 0;
    cob->matrix[2][1] = 0;
    cob->matrix[2][2] = size[2];

    /* targetmat[2] instead of ownermat[2] is passed to vectomat
     * for backwards compatibility it seems... (Aligorith)
     */
    sub_v3_v3v3(vec, cob->matrix[3], ct->matrix[3]);
    vectomat(
        vec, ct->matrix[2], (short)data->reserved1, (short)data->reserved2, data->flags, totmat);

    mul_m4_m3m4(cob->matrix, totmat, cob->matrix);
  }
}

static bConstraintTypeInfo CTI_TRACKTO = {
    CONSTRAINT_TYPE_TRACKTO,    /* type */
    sizeof(bTrackToConstraint), /* size */
    "Track To",                 /* name */
    "bTrackToConstraint",       /* struct name */
    NULL,                       /* free data */
    trackto_id_looper,          /* id looper */
    NULL,                       /* copy data */
    trackto_new_data,           /* new data */
    trackto_get_tars,           /* get constraint targets */
    trackto_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get target matrix */
    trackto_evaluate,           /* evaluate */
};

/* --------- Inverse-Kinematics --------- */

static void kinematic_new_data(void *cdata)
{
  bKinematicConstraint *data = (bKinematicConstraint *)cdata;

  data->weight = 1.0f;
  data->orientweight = 1.0f;
  data->iterations = 500;
  data->dist = 1.0f;
  data->flag = CONSTRAINT_IK_TIP | CONSTRAINT_IK_STRETCH | CONSTRAINT_IK_POS;
}

static void kinematic_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bKinematicConstraint *data = con->data;

  /* chain target */
  func(con, (ID **)&data->tar, false, userdata);

  /* poletarget */
  func(con, (ID **)&data->poletar, false, userdata);
}

static int kinematic_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints is used twice here */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);
    SINGLETARGET_GET_TARS(con, data->poletar, data->polesubtarget, ct, list);

    return 2;
  }

  return 0;
}

static void kinematic_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    SINGLETARGET_FLUSH_TARS(con, data->poletar, data->polesubtarget, ct, list, no_copy);
  }
}

static void kinematic_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                 bConstraint *con,
                                 bConstraintOb *cob,
                                 bConstraintTarget *ct,
                                 float UNUSED(ctime))
{
  bKinematicConstraint *data = con->data;

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
    if (data->flag & CONSTRAINT_IK_AUTO) {
      Object *ob = cob->ob;

      if (ob == NULL) {
        unit_m4(ct->matrix);
      }
      else {
        float vec[3];
        /* move grabtarget into world space */
        mul_v3_m4v3(vec, ob->obmat, data->grabtarget);
        copy_m4_m4(ct->matrix, ob->obmat);
        copy_v3_v3(ct->matrix[3], vec);
      }
    }
    else {
      unit_m4(ct->matrix);
    }
  }
}

static bConstraintTypeInfo CTI_KINEMATIC = {
    CONSTRAINT_TYPE_KINEMATIC,    /* type */
    sizeof(bKinematicConstraint), /* size */
    "IK",                         /* name */
    "bKinematicConstraint",       /* struct name */
    NULL,                         /* free data */
    kinematic_id_looper,          /* id looper */
    NULL,                         /* copy data */
    kinematic_new_data,           /* new data */
    kinematic_get_tars,           /* get constraint targets */
    kinematic_flush_tars,         /* flush constraint targets */
    kinematic_get_tarmat,         /* get target matrix */
    NULL,                         /* evaluate - solved as separate loop */
};

/* -------- Follow-Path Constraint ---------- */


/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2001-2002 NaN Holding BV. All rights reserved. */

/** \file
 * \ingroup bke
 */

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

static void flush_space_tar(bConstraint *con, ListBase *list, bool no_copy)
{
  if (!con || !list ||
      (con->ownspace != CONSTRAINT_SPACE_CUSTOM && con->tarspace != CONSTRAINT_SPACE_CUSTOM)) {
    return;
  }
  bConstraintTarget *ct = (bConstraintTarget *)list->last;
  SINGLETARGET_FLUSH_TARS(con, con->space_object, con->space_subtarget, ct, list, no_copy);
}

/* --------- ChildOf Constraint ------------ */

static void childof_new_data(void *cdata)
{
  bChildOfConstraint *data = (bChildOfConstraint *)cdata;

  data->flag = (CHILDOF_LOCX | CHILDOF_LOCY | CHILDOF_LOCZ | CHILDOF_ROTX | CHILDOF_ROTY |
                CHILDOF_ROTZ | CHILDOF_SIZEX | CHILDOF_SIZEY | CHILDOF_SIZEZ |
                CHILDOF_SET_INVERSE);
  unit_m4(data->invmat);
}

static void childof_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bChildOfConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int childof_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void childof_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

static void childof_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bChildOfConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (!VALID_CONS_TARGET(ct)) {
    return;
  }

  float parmat[4][4];
  float inverse_matrix[4][4];
  /* Simple matrix parenting. */
  if ((data->flag & CHILDOF_ALL) == CHILDOF_ALL) {
    copy_m4_m4(parmat, ct->matrix);
    copy_m4_m4(inverse_matrix, data->invmat);
  }
  /* Filter the parent matrix by channel. */
  else {
    float loc[3], eul[3], size[3];
    float loco[3], eulo[3], sizeo[3];

    /* extract components of both matrices */
    copy_v3_v3(loc, ct->matrix[3]);
    mat4_to_eulO(eul, ct->rotOrder, ct->matrix);
    mat4_to_size(size, ct->matrix);

    copy_v3_v3(loco, data->invmat[3]);
    mat4_to_eulO(eulo, cob->rotOrder, data->invmat);
    mat4_to_size(sizeo, data->invmat);

    /* Reset the locked channels to their no-op values. */
    if (!(data->flag & CHILDOF_LOCX)) {
      loc[0] = loco[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCY)) {
      loc[1] = loco[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCZ)) {
      loc[2] = loco[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTX)) {
      eul[0] = eulo[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTY)) {
      eul[1] = eulo[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTZ)) {
      eul[2] = eulo[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_SIZEX)) {
      size[0] = sizeo[0] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEY)) {
      size[1] = sizeo[1] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEZ)) {
      size[2] = sizeo[2] = 1.0f;
    }

    /* Construct the new matrices given the disabled channels. */
    loc_eulO_size_to_mat4(parmat, loc, eul, size, ct->rotOrder);
    loc_eulO_size_to_mat4(inverse_matrix, loco, eulo, sizeo, cob->rotOrder);
  }

  /* If requested, compute the inverse matrix from the computed parent matrix. */
  if (data->flag & CHILDOF_SET_INVERSE) {
    invert_m4_m4(data->invmat, parmat);
    if (cob->pchan != NULL) {
      mul_m4_series(data->invmat, data->invmat, cob->ob->obmat);
    }

    copy_m4_m4(inverse_matrix, data->invmat);

    data->flag &= ~CHILDOF_SET_INVERSE;

    /* Write the computed matrix back to the master copy if in COW evaluation. */
    bConstraint *orig_con = constraint_find_original_for_update(cob, con);

    if (orig_con != NULL) {
      bChildOfConstraint *orig_data = orig_con->data;

      copy_m4_m4(orig_data->invmat, data->invmat);
      orig_data->flag &= ~CHILDOF_SET_INVERSE;
    }
  }

  /* Multiply together the target (parent) matrix, parent inverse,
   * and the owner transform matrix to get the effect of this constraint
   * (i.e.  owner is 'parented' to parent). */
  float orig_cob_matrix[4][4];
  copy_m4_m4(orig_cob_matrix, cob->matrix);
  mul_m4_series(cob->matrix, parmat, inverse_matrix, orig_cob_matrix);

  /* Without this, changes to scale and rotation can change location
   * of a parentless bone or a disconnected bone. Even though its set
   * to zero above. */
  if (!(data->flag & CHILDOF_LOCX)) {
    cob->matrix[3][0] = orig_cob_matrix[3][0];
  }
  if (!(data->flag & CHILDOF_LOCY)) {
    cob->matrix[3][1] = orig_cob_matrix[3][1];
  }
  if (!(data->flag & CHILDOF_LOCZ)) {
    cob->matrix[3][2] = orig_cob_matrix[3][2];
  }
}

/* XXX NOTE: con->flag should be CONSTRAINT_SPACEONCE for bone-childof, patched in `readfile.c`. */
static bConstraintTypeInfo CTI_CHILDOF = {
    CONSTRAINT_TYPE_CHILDOF,    /* type */
    sizeof(bChildOfConstraint), /* size */
    "Child Of",                 /* name */
    "bChildOfConstraint",       /* struct name */
    NULL,                       /* free data */
    childof_id_looper,          /* id looper */
    NULL,                       /* copy data */
    childof_new_data,           /* new data */
    childof_get_tars,           /* get constraint targets */
    childof_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get a target matrix */
    childof_evaluate,           /* evaluate */
};

/* -------- TrackTo Constraint ------- */

static void trackto_new_data(void *cdata)
{
  bTrackToConstraint *data = (bTrackToConstraint *)cdata;

  data->reserved1 = TRACK_nZ;
  data->reserved2 = UP_Y;
}

static void trackto_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bTrackToConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int trackto_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void trackto_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static int basis_cross(int n, int m)
{
  switch (n - m) {
    case 1:
    case -2:
      return 1;

    case -1:
    case 2:
      return -1;

    default:
      return 0;
  }
}

static void vectomat(const float vec[3],
                     const float target_up[3],
                     short axis,
                     short upflag,
                     short flags,
                     float m[3][3])
{
  float n[3];
  float u[3]; /* vector specifying the up axis */
  float proj[3];
  float right[3];
  float neg = -1;
  int right_index;

  if (normalize_v3_v3(n, vec) == 0.0f) {
    n[0] = 0.0f;
    n[1] = 0.0f;
    n[2] = 1.0f;
  }
  if (axis > 2) {
    axis -= 3;
  }
  else {
    negate_v3(n);
  }

  /* n specifies the transformation of the track axis */
  if (flags & TARGET_Z_UP) {
    /* target Z axis is the global up axis */
    copy_v3_v3(u, target_up);
  }
  else {
    /* world Z axis is the global up axis */
    u[0] = 0;
    u[1] = 0;
    u[2] = 1;
  }

  /* NOTE: even though 'n' is normalized, don't use 'project_v3_v3v3_normalized' below
   * because precision issues cause a problem in near degenerate states, see: T53455. */

  /* project the up vector onto the plane specified by n */
  project_v3_v3v3(proj, u, n); /* first u onto n... */
  sub_v3_v3v3(proj, u, proj);  /* then onto the plane */
  /* proj specifies the transformation of the up axis */

  if (normalize_v3(proj) == 0.0f) { /* degenerate projection */
    proj[0] = 0.0f;
    proj[1] = 1.0f;
    proj[2] = 0.0f;
  }

  /* Normalized cross product of n and proj specifies transformation of the right axis */
  cross_v3_v3v3(right, proj, n);
  normalize_v3(right);

  if (axis != upflag) {
    right_index = 3 - axis - upflag;
    neg = (float)basis_cross(axis, upflag);

    /* account for up direction, track direction */
    m[right_index][0] = neg * right[0];
    m[right_index][1] = neg * right[1];
    m[right_index][2] = neg * right[2];

    copy_v3_v3(m[upflag], proj);

    copy_v3_v3(m[axis], n);
  }
  /* identity matrix - don't do anything if the two axes are the same */
  else {
    unit_m3(m);
  }
}

static void trackto_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bTrackToConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float size[3], vec[3];
    float totmat[3][3];

    /* Get size property, since ob->scale is only the object's own relative size,
     * not its global one. */
    mat4_to_size(size, cob->matrix);

    /* Clear the object's rotation */
    cob->matrix[0][0] = size[0];
    cob->matrix[0][1] = 0;
    cob->matrix[0][2] = 0;
    cob->matrix[1][0] = 0;
    cob->matrix[1][1] = size[1];
    cob->matrix[1][2] = 0;
    cob->matrix[2][0] = 0;
    cob->matrix[2][1] = 0;
    cob->matrix[2][2] = size[2];

    /* targetmat[2] instead of ownermat[2] is passed to vectomat
     * for backwards compatibility it seems... (Aligorith)
     */
    sub_v3_v3v3(vec, cob->matrix[3], ct->matrix[3]);
    vectomat(
        vec, ct->matrix[2], (short)data->reserved1, (short)data->reserved2, data->flags, totmat);

    mul_m4_m3m4(cob->matrix, totmat, cob->matrix);
  }
}

static bConstraintTypeInfo CTI_TRACKTO = {
    CONSTRAINT_TYPE_TRACKTO,    /* type */
    sizeof(bTrackToConstraint), /* size */
    "Track To",                 /* name */
    "bTrackToConstraint",       /* struct name */
    NULL,                       /* free data */
    trackto_id_looper,          /* id looper */
    NULL,                       /* copy data */
    trackto_new_data,           /* new data */
    trackto_get_tars,           /* get constraint targets */
    trackto_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get target matrix */
    trackto_evaluate,           /* evaluate */
};

/* --------- Inverse-Kinematics --------- */

static void kinematic_new_data(void *cdata)
{
  bKinematicConstraint *data = (bKinematicConstraint *)cdata;

  data->weight = 1.0f;
  data->orientweight = 1.0f;
  data->iterations = 500;
  data->dist = 1.0f;
  data->flag = CONSTRAINT_IK_TIP | CONSTRAINT_IK_STRETCH | CONSTRAINT_IK_POS;
}

static void kinematic_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bKinematicConstraint *data = con->data;

  /* chain target */
  func(con, (ID **)&data->tar, false, userdata);

  /* poletarget */
  func(con, (ID **)&data->poletar, false, userdata);
}

static int kinematic_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints is used twice here */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);
    SINGLETARGET_GET_TARS(con, data->poletar, data->polesubtarget, ct, list);

    return 2;
  }

  return 0;
}

static void kinematic_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    SINGLETARGET_FLUSH_TARS(con, data->poletar, data->polesubtarget, ct, list, no_copy);
  }
}

static void kinematic_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                 bConstraint *con,
                                 bConstraintOb *cob,
                                 bConstraintTarget *ct,
                                 float UNUSED(ctime))
{
  bKinematicConstraint *data = con->data;

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
    if (data->flag & CONSTRAINT_IK_AUTO) {
      Object *ob = cob->ob;

      if (ob == NULL) {
        unit_m4(ct->matrix);
      }
      else {
        float vec[3];
        /* move grabtarget into world space */
        mul_v3_m4v3(vec, ob->obmat, data->grabtarget);
        copy_m4_m4(ct->matrix, ob->obmat);
        copy_v3_v3(ct->matrix[3], vec);
      }
    }
    else {
      unit_m4(ct->matrix);
    }
  }
}

static bConstraintTypeInfo CTI_KINEMATIC = {
    CONSTRAINT_TYPE_KINEMATIC,    /* type */
    sizeof(bKinematicConstraint), /* size */
    "IK",                         /* name */
    "bKinematicConstraint",       /* struct name */
    NULL,                         /* free data */
    kinematic_id_looper,          /* id looper */
    NULL,                         /* copy data */
    kinematic_new_data,           /* new data */
    kinematic_get_tars,           /* get constraint targets */
    kinematic_flush_tars,         /* flush constraint targets */
    kinematic_get_tarmat,         /* get target matrix */
    NULL,                         /* evaluate - solved as separate loop */
};

/* -------- Follow-Path Constraint ---------- */

static void followpath_new_data(void *cdata)
{
  bFollowPathConstraint *data = (bFollowPathConstraint *)cdata;

  data->trackflag = TRACK_Y;
  data->upflag = UP_Z;
  data->offset = 0;
  data->followflag = 0;
}

static void followpath_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bFollowPathConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int followpath_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bFollowPathConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints without subtargets */
    SINGLETARGETNS_GET_TARS(con, data->tar, ct, list);

    return 1;
  }

  return 0;
}

static void followpath_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bFollowPathConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGETNS_FLUSH_TARS(con, data->tar, ct, list, no_copy);
  }
}

static void followpath_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                  bConstraint *con,
                                  bConstraintOb *UNUSED(cob),
                                  bConstraintTarget *ct,
                                  float UNUSED(ctime))
{
  bFollowPathConstraint *data = con->data;

  if (VALID_CONS_TARGET(ct) && (ct->tar->type == OB_CURVES_LEGACY)) {
    Curve *cu = ct->tar->data;
    float vec[4], dir[3], radius;
    float curvetime;

    unit_m4(ct->matrix);

    /* NOTE: when creating constraints that follow path, the curve gets the CU_PATH set now,
     * currently for paths to work it needs to go through the bevlist/displist system (ton)
     */

    if (ct->tar->runtime.curve_cache && ct->tar->runtime.curve_cache->anim_path_accum_length) {
      float quat[4];
      if ((data->followflag & FOLLOWPATH_STATIC) == 0) {
        /* animated position along curve depending on time */
        curvetime = cu->ctime - data->offset;

        /* ctime is now a proper var setting of Curve which gets set by Animato like any other var
         * that's animated, but this will only work if it actually is animated...
         *
         * we divide the curvetime calculated in the previous step by the length of the path,
         * to get a time factor. */
        curvetime /= cu->pathlen;

        Nurb *nu = cu->nurb.first;
        if (!(nu && nu->flagu & CU_NURB_CYCLIC) && cu->flag & CU_PATH_CLAMP) {
          /* If curve is not cyclic, clamp to the begin/end points if the curve clamp option is on.
           */
          CLAMP(curvetime, 0.0f, 1.0f);
        }
      }
      else {
        /* fixed position along curve */
        curvetime = data->offset_fac;
      }

      if (BKE_where_on_path(ct->tar,
                            curvetime,
                            vec,
                            dir,
                            (data->followflag & FOLLOWPATH_FOLLOW) ? quat : NULL,
                            &radius,
                            NULL)) { /* quat_pt is quat or NULL. */
        float totmat[4][4];
        unit_m4(totmat);

        if (data->followflag & FOLLOWPATH_FOLLOW) {
          quat_apply_track(quat, data->trackflag, data->upflag);
          quat_to_mat4(totmat, quat);
        }

        if (data->followflag & FOLLOWPATH_RADIUS) {
          float tmat[4][4], rmat[4][4];
          scale_m4_fl(tmat, radius);
          mul_m4_m4m4(rmat, tmat, totmat);
          copy_m4_m4(totmat, rmat);
        }

        copy_v3_v3(totmat[3], vec);

        mul_m4_m4m4(ct->matrix, ct->tar->obmat, totmat);
      }
    }
  }
  else if (ct) {
    unit_m4(ct->matrix);
  }
}

static void followpath_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float obmat[4][4];
    float size[3];
    bFollowPathConstraint *data = con->data;

    /* get Object transform (loc/rot/size) to determine transformation from path */
    /* TODO: this used to be local at one point, but is probably more useful as-is */
    copy_m4_m4(obmat, cob->matrix);

    /* get scaling of object before applying constraint */
    mat4_to_size(size, cob->matrix);

    /* apply targetmat - containing location on path, and rotation */
    mul_m4_m4m4(cob->matrix, ct->matrix, obmat);

    /* un-apply scaling caused by path */
    if ((data->followflag & FOLLOWPATH_RADIUS) == 0) {
      /* XXX(campbell): Assume that scale correction means that radius
       * will have some scale error in it. */
      float obsize[3];

      mat4_to_size(obsize, cob->matrix);
      if (obsize[0]) {
        mul_v3_fl(cob->matrix[0], size[0] / obsize[0]);
      }
      if (obsize[1]) {
        mul_v3_fl(cob->matrix[1], size[1] / obsize[1]);
      }
      if (obsize[2]) {
        mul_v3_fl(cob->matrix[2], size[2] / obsize[2]);
      }
    }
  }
}

static bConstraintTypeInfo CTI_FOLLOWPATH = {
    CONSTRAINT_TYPE_FOLLOWPATH,    /* type */
    sizeof(bFollowPathConstraint), /* size */
    "Follow Path",                 /* name */
    "bFollowPathConstraint",       /* struct name */
    NULL,                          /* free data */
    followpath_id_looper,          /* id looper */
    NULL,                          /* copy data */
    followpath_new_data,           /* new data */
    followpath_get_tars,           /* get constraint targets */
    followpath_flush_tars,         /* flush constraint targets */
    followpath_get_tarmat,         /* get target matrix */
    followpath_evaluate,           /* evaluate */
};

/* --------- Limit Location --------- */

static void loclimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bLocLimitConstraint *data = con->data;

  if (data->flag & LIMIT_XMIN) {
    if (cob->matrix[3][0] < data->xmin) {
      cob->matrix[3][0] = data->xmin;
    }
  }
  if (data->flag & LIMIT_XMAX) {
    if (cob->matrix[3][0] > data->xmax) {
      cob->matrix[3][0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YMIN) {
    if (cob->matrix[3][1] < data->ymin) {
      cob->matrix[3][1] = data->ymin;
    }
  }
  if (data->flag & LIMIT_YMAX) {
    if (cob->matrix[3][1] > data->ymax) {
      cob->matrix[3][1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZMIN) {
    if (cob->matrix[3][2] < data->zmin) {
      cob->matrix[3][2] = data->zmin;
    }
  }
  if (data->flag & LIMIT_ZMAX) {
    if (cob->matrix[3][2] > data->zmax) {
      cob->matrix[3][2] = data->zmax;
    }
  }
}

static bConstraintTypeInfo CTI_LOCLIMIT = {
    CONSTRAINT_TYPE_LOCLIMIT,    /* type */
    sizeof(bLocLimitConstraint), /* size */
    "Limit Location",            /* name */
    "bLocLimitConstraint",       /* struct name */
    NULL,                        /* free data */
    custom_space_id_looper,      /* id looper */
    NULL,                        /* copy data */
    NULL,                        /* new data */
    get_space_tar,               /* get constraint targets */
    flush_space_tar,             /* flush constraint targets */
    NULL,                        /* get target matrix */
    loclimit_evaluate,           /* evaluate */
};

/* -------- Limit Rotation --------- */

static void rotlimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bRotLimitConstraint *data = con->data;
  float loc[3];
  float eul[3];
  float size[3];

  /* This constraint is based on euler rotation math, which doesn't work well with shear.
   * The Y axis is chosen as the main one because constraints are most commonly used on bones.
   * This also allows using the constraint to simply remove shear. */
  orthogonalize_m4_stable(cob->matrix, 1, false);

  /* Only do the complex processing if some limits are actually enabled. */
  if (!(data->flag & (LIMIT_XROT | LIMIT_YROT | LIMIT_ZROT))) {
    return;
  }

  /* Select the Euler rotation order, defaulting to the owner value. */
  short rot_order = cob->rotOrder;

  if (data->euler_order != CONSTRAINT_EULER_AUTO) {
    rot_order = data->euler_order;
  }

  /* Decompose the matrix using the specified order. */
  copy_v3_v3(loc, cob->matrix[3]);
  mat4_to_size(size, cob->matrix);

  mat4_to_eulO(eul, rot_order, cob->matrix);

  /* constraint data uses radians internally */

  /* limiting of euler values... */
  if (data->flag & LIMIT_XROT) {
    if (eul[0] < data->xmin) {
      eul[0] = data->xmin;
    }

    if (eul[0] > data->xmax) {
      eul[0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YROT) {
    if (eul[1] < data->ymin) {
      eul[1] = data->ymin;
    }

    if (eul[1] > data->ymax) {
      eul[1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZROT) {
    if (eul[2] < data->zmin) {
      eul[2] = data->zmin;
    }

    if (eul[2] > data->zmax) {
      eul[2] = data->zmax;
    }
  }

  loc_eulO_size_to_mat4(cob->matrix, loc, eul, size, rot_order);
}

static bConstraintTypeInfo CTI_ROTLIMIT = {
    CONSTRAINT_TYPE_ROTLIMIT,    /* type */
    sizeof(bRotLimitConstraint), /* size */
    "Limit Rotation",            /* name */
    "bRotLimitConstraint",       /* struct name */
    NULL,                        /* free data */
    custom_space_id_looper,      /* id looper */
    NULL,                        /* copy data */
    NULL,                        /* new data */
    get_space_tar,               /* get constraint targets */
    flush_space_tar,             /* flush constraint targets */
    NULL,                        /* get target matrix */
    rotlimit_evaluate,           /* evaluate */
};

/* --------- Limit Scale --------- */

static void sizelimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bSizeLimitConstraint *data = con->data;
  float obsize[3], size[3];

  mat4_to_size(size, cob->matrix);
  mat4_to_size(obsize, cob->matrix);

  if (data->flag & LIMIT_XMIN) {
    if (size[0] < data->xmin) {
      size[0] = data->xmin;
    }
  }
  if (data->flag & LIMIT_XMAX) {
    if (size[0] > data->xmax) {
      size[0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YMIN) {
    if (size[1] < data->ymin) {
      size[1] = data->ymin;
    }
  }
  if (data->flag & LIMIT_YMAX) {
    if (size[1] > data->ymax) {
      size[1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZMIN) {
    if (size[2] < data->zmin) {
      size[2] = data->zmin;
    }
  }
  if (data->flag & LIMIT_ZMAX) {
    if (size[2] > data->zmax) {
      size[2] = data->zmax;
    }
  }

  if (obsize[0]) {
    mul_v3_fl(cob->matrix[0], size[0] / obsize[0]);
  }
  if (obsize[1]) {
    mul_v3_fl(cob->matrix[1], size[1] / obsize[1]);
  }
  if (obsize[2]) {
    mul_v3_fl(cob->matrix[2], size[2] / obsize[2]);
  }
}

static bConstraintTypeInfo CTI_SIZELIMIT = {
    CONSTRAINT_TYPE_SIZELIMIT,    /* type */
    sizeof(bSizeLimitConstraint), /* size */
    "Limit Scale",                /* name */
    "bSizeLimitConstraint",       /* struct name */
    NULL,                         /* free data */
    custom_space_id_looper,       /* id looper */
    NULL,                         /* copy data */
    NULL,                         /* new data */
    get_space_tar,                /* get constraint targets */
    flush_space_tar,              /* flush constraint targets */
    NULL,                         /* get target matrix */
    sizelimit_evaluate,           /* evaluate */
};

/* ----------- Copy Location ------------- */

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2001-2002 NaN Holding BV. All rights reserved. */

/** \file
 * \ingroup bke
 */

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

static void flush_space_tar(bConstraint *con, ListBase *list, bool no_copy)
{
  if (!con || !list ||
      (con->ownspace != CONSTRAINT_SPACE_CUSTOM && con->tarspace != CONSTRAINT_SPACE_CUSTOM)) {
    return;
  }
  bConstraintTarget *ct = (bConstraintTarget *)list->last;
  SINGLETARGET_FLUSH_TARS(con, con->space_object, con->space_subtarget, ct, list, no_copy);
}

/* --------- ChildOf Constraint ------------ */

static void childof_new_data(void *cdata)
{
  bChildOfConstraint *data = (bChildOfConstraint *)cdata;

  data->flag = (CHILDOF_LOCX | CHILDOF_LOCY | CHILDOF_LOCZ | CHILDOF_ROTX | CHILDOF_ROTY |
                CHILDOF_ROTZ | CHILDOF_SIZEX | CHILDOF_SIZEY | CHILDOF_SIZEZ |
                CHILDOF_SET_INVERSE);
  unit_m4(data->invmat);
}

static void childof_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bChildOfConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int childof_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void childof_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bChildOfConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

static void childof_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bChildOfConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (!VALID_CONS_TARGET(ct)) {
    return;
  }

  float parmat[4][4];
  float inverse_matrix[4][4];
  /* Simple matrix parenting. */
  if ((data->flag & CHILDOF_ALL) == CHILDOF_ALL) {
    copy_m4_m4(parmat, ct->matrix);
    copy_m4_m4(inverse_matrix, data->invmat);
  }
  /* Filter the parent matrix by channel. */
  else {
    float loc[3], eul[3], size[3];
    float loco[3], eulo[3], sizeo[3];

    /* extract components of both matrices */
    copy_v3_v3(loc, ct->matrix[3]);
    mat4_to_eulO(eul, ct->rotOrder, ct->matrix);
    mat4_to_size(size, ct->matrix);

    copy_v3_v3(loco, data->invmat[3]);
    mat4_to_eulO(eulo, cob->rotOrder, data->invmat);
    mat4_to_size(sizeo, data->invmat);

    /* Reset the locked channels to their no-op values. */
    if (!(data->flag & CHILDOF_LOCX)) {
      loc[0] = loco[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCY)) {
      loc[1] = loco[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_LOCZ)) {
      loc[2] = loco[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTX)) {
      eul[0] = eulo[0] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTY)) {
      eul[1] = eulo[1] = 0.0f;
    }
    if (!(data->flag & CHILDOF_ROTZ)) {
      eul[2] = eulo[2] = 0.0f;
    }
    if (!(data->flag & CHILDOF_SIZEX)) {
      size[0] = sizeo[0] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEY)) {
      size[1] = sizeo[1] = 1.0f;
    }
    if (!(data->flag & CHILDOF_SIZEZ)) {
      size[2] = sizeo[2] = 1.0f;
    }

    /* Construct the new matrices given the disabled channels. */
    loc_eulO_size_to_mat4(parmat, loc, eul, size, ct->rotOrder);
    loc_eulO_size_to_mat4(inverse_matrix, loco, eulo, sizeo, cob->rotOrder);
  }

  /* If requested, compute the inverse matrix from the computed parent matrix. */
  if (data->flag & CHILDOF_SET_INVERSE) {
    invert_m4_m4(data->invmat, parmat);
    if (cob->pchan != NULL) {
      mul_m4_series(data->invmat, data->invmat, cob->ob->obmat);
    }

    copy_m4_m4(inverse_matrix, data->invmat);

    data->flag &= ~CHILDOF_SET_INVERSE;

    /* Write the computed matrix back to the master copy if in COW evaluation. */
    bConstraint *orig_con = constraint_find_original_for_update(cob, con);

    if (orig_con != NULL) {
      bChildOfConstraint *orig_data = orig_con->data;

      copy_m4_m4(orig_data->invmat, data->invmat);
      orig_data->flag &= ~CHILDOF_SET_INVERSE;
    }
  }

  /* Multiply together the target (parent) matrix, parent inverse,
   * and the owner transform matrix to get the effect of this constraint
   * (i.e.  owner is 'parented' to parent). */
  float orig_cob_matrix[4][4];
  copy_m4_m4(orig_cob_matrix, cob->matrix);
  mul_m4_series(cob->matrix, parmat, inverse_matrix, orig_cob_matrix);

  /* Without this, changes to scale and rotation can change location
   * of a parentless bone or a disconnected bone. Even though its set
   * to zero above. */
  if (!(data->flag & CHILDOF_LOCX)) {
    cob->matrix[3][0] = orig_cob_matrix[3][0];
  }
  if (!(data->flag & CHILDOF_LOCY)) {
    cob->matrix[3][1] = orig_cob_matrix[3][1];
  }
  if (!(data->flag & CHILDOF_LOCZ)) {
    cob->matrix[3][2] = orig_cob_matrix[3][2];
  }
}

/* XXX NOTE: con->flag should be CONSTRAINT_SPACEONCE for bone-childof, patched in `readfile.c`. */
static bConstraintTypeInfo CTI_CHILDOF = {
    CONSTRAINT_TYPE_CHILDOF,    /* type */
    sizeof(bChildOfConstraint), /* size */
    "Child Of",                 /* name */
    "bChildOfConstraint",       /* struct name */
    NULL,                       /* free data */
    childof_id_looper,          /* id looper */
    NULL,                       /* copy data */
    childof_new_data,           /* new data */
    childof_get_tars,           /* get constraint targets */
    childof_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get a target matrix */
    childof_evaluate,           /* evaluate */
};

/* -------- TrackTo Constraint ------- */

static void trackto_new_data(void *cdata)
{
  bTrackToConstraint *data = (bTrackToConstraint *)cdata;

  data->reserved1 = TRACK_nZ;
  data->reserved2 = UP_Y;
}

static void trackto_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bTrackToConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int trackto_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void trackto_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bTrackToConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static int basis_cross(int n, int m)
{
  switch (n - m) {
    case 1:
    case -2:
      return 1;

    case -1:
    case 2:
      return -1;

    default:
      return 0;
  }
}

static void vectomat(const float vec[3],
                     const float target_up[3],
                     short axis,
                     short upflag,
                     short flags,
                     float m[3][3])
{
  float n[3];
  float u[3]; /* vector specifying the up axis */
  float proj[3];
  float right[3];
  float neg = -1;
  int right_index;

  if (normalize_v3_v3(n, vec) == 0.0f) {
    n[0] = 0.0f;
    n[1] = 0.0f;
    n[2] = 1.0f;
  }
  if (axis > 2) {
    axis -= 3;
  }
  else {
    negate_v3(n);
  }

  /* n specifies the transformation of the track axis */
  if (flags & TARGET_Z_UP) {
    /* target Z axis is the global up axis */
    copy_v3_v3(u, target_up);
  }
  else {
    /* world Z axis is the global up axis */
    u[0] = 0;
    u[1] = 0;
    u[2] = 1;
  }

  /* NOTE: even though 'n' is normalized, don't use 'project_v3_v3v3_normalized' below
   * because precision issues cause a problem in near degenerate states, see: T53455. */

  /* project the up vector onto the plane specified by n */
  project_v3_v3v3(proj, u, n); /* first u onto n... */
  sub_v3_v3v3(proj, u, proj);  /* then onto the plane */
  /* proj specifies the transformation of the up axis */

  if (normalize_v3(proj) == 0.0f) { /* degenerate projection */
    proj[0] = 0.0f;
    proj[1] = 1.0f;
    proj[2] = 0.0f;
  }

  /* Normalized cross product of n and proj specifies transformation of the right axis */
  cross_v3_v3v3(right, proj, n);
  normalize_v3(right);

  if (axis != upflag) {
    right_index = 3 - axis - upflag;
    neg = (float)basis_cross(axis, upflag);

    /* account for up direction, track direction */
    m[right_index][0] = neg * right[0];
    m[right_index][1] = neg * right[1];
    m[right_index][2] = neg * right[2];

    copy_v3_v3(m[upflag], proj);

    copy_v3_v3(m[axis], n);
  }
  /* identity matrix - don't do anything if the two axes are the same */
  else {
    unit_m3(m);
  }
}

static void trackto_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bTrackToConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float size[3], vec[3];
    float totmat[3][3];

    /* Get size property, since ob->scale is only the object's own relative size,
     * not its global one. */
    mat4_to_size(size, cob->matrix);

    /* Clear the object's rotation */
    cob->matrix[0][0] = size[0];
    cob->matrix[0][1] = 0;
    cob->matrix[0][2] = 0;
    cob->matrix[1][0] = 0;
    cob->matrix[1][1] = size[1];
    cob->matrix[1][2] = 0;
    cob->matrix[2][0] = 0;
    cob->matrix[2][1] = 0;
    cob->matrix[2][2] = size[2];

    /* targetmat[2] instead of ownermat[2] is passed to vectomat
     * for backwards compatibility it seems... (Aligorith)
     */
    sub_v3_v3v3(vec, cob->matrix[3], ct->matrix[3]);
    vectomat(
        vec, ct->matrix[2], (short)data->reserved1, (short)data->reserved2, data->flags, totmat);

    mul_m4_m3m4(cob->matrix, totmat, cob->matrix);
  }
}

static bConstraintTypeInfo CTI_TRACKTO = {
    CONSTRAINT_TYPE_TRACKTO,    /* type */
    sizeof(bTrackToConstraint), /* size */
    "Track To",                 /* name */
    "bTrackToConstraint",       /* struct name */
    NULL,                       /* free data */
    trackto_id_looper,          /* id looper */
    NULL,                       /* copy data */
    trackto_new_data,           /* new data */
    trackto_get_tars,           /* get constraint targets */
    trackto_flush_tars,         /* flush constraint targets */
    default_get_tarmat,         /* get target matrix */
    trackto_evaluate,           /* evaluate */
};

/* --------- Inverse-Kinematics --------- */

static void kinematic_new_data(void *cdata)
{
  bKinematicConstraint *data = (bKinematicConstraint *)cdata;

  data->weight = 1.0f;
  data->orientweight = 1.0f;
  data->iterations = 500;
  data->dist = 1.0f;
  data->flag = CONSTRAINT_IK_TIP | CONSTRAINT_IK_STRETCH | CONSTRAINT_IK_POS;
}

static void kinematic_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bKinematicConstraint *data = con->data;

  /* chain target */
  func(con, (ID **)&data->tar, false, userdata);

  /* poletarget */
  func(con, (ID **)&data->poletar, false, userdata);
}

static int kinematic_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints is used twice here */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);
    SINGLETARGET_GET_TARS(con, data->poletar, data->polesubtarget, ct, list);

    return 2;
  }

  return 0;
}

static void kinematic_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bKinematicConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    SINGLETARGET_FLUSH_TARS(con, data->poletar, data->polesubtarget, ct, list, no_copy);
  }
}

static void kinematic_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                 bConstraint *con,
                                 bConstraintOb *cob,
                                 bConstraintTarget *ct,
                                 float UNUSED(ctime))
{
  bKinematicConstraint *data = con->data;

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
    if (data->flag & CONSTRAINT_IK_AUTO) {
      Object *ob = cob->ob;

      if (ob == NULL) {
        unit_m4(ct->matrix);
      }
      else {
        float vec[3];
        /* move grabtarget into world space */
        mul_v3_m4v3(vec, ob->obmat, data->grabtarget);
        copy_m4_m4(ct->matrix, ob->obmat);
        copy_v3_v3(ct->matrix[3], vec);
      }
    }
    else {
      unit_m4(ct->matrix);
    }
  }
}

static bConstraintTypeInfo CTI_KINEMATIC = {
    CONSTRAINT_TYPE_KINEMATIC,    /* type */
    sizeof(bKinematicConstraint), /* size */
    "IK",                         /* name */
    "bKinematicConstraint",       /* struct name */
    NULL,                         /* free data */
    kinematic_id_looper,          /* id looper */
    NULL,                         /* copy data */
    kinematic_new_data,           /* new data */
    kinematic_get_tars,           /* get constraint targets */
    kinematic_flush_tars,         /* flush constraint targets */
    kinematic_get_tarmat,         /* get target matrix */
    NULL,                         /* evaluate - solved as separate loop */
};

/* -------- Follow-Path Constraint ---------- */

static void followpath_new_data(void *cdata)
{
  bFollowPathConstraint *data = (bFollowPathConstraint *)cdata;

  data->trackflag = TRACK_Y;
  data->upflag = UP_Z;
  data->offset = 0;
  data->followflag = 0;
}

static void followpath_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bFollowPathConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int followpath_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bFollowPathConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints without subtargets */
    SINGLETARGETNS_GET_TARS(con, data->tar, ct, list);

    return 1;
  }

  return 0;
}

static void followpath_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bFollowPathConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGETNS_FLUSH_TARS(con, data->tar, ct, list, no_copy);
  }
}

static void followpath_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                  bConstraint *con,
                                  bConstraintOb *UNUSED(cob),
                                  bConstraintTarget *ct,
                                  float UNUSED(ctime))
{
  bFollowPathConstraint *data = con->data;

  if (VALID_CONS_TARGET(ct) && (ct->tar->type == OB_CURVES_LEGACY)) {
    Curve *cu = ct->tar->data;
    float vec[4], dir[3], radius;
    float curvetime;

    unit_m4(ct->matrix);

    /* NOTE: when creating constraints that follow path, the curve gets the CU_PATH set now,
     * currently for paths to work it needs to go through the bevlist/displist system (ton)
     */

    if (ct->tar->runtime.curve_cache && ct->tar->runtime.curve_cache->anim_path_accum_length) {
      float quat[4];
      if ((data->followflag & FOLLOWPATH_STATIC) == 0) {
        /* animated position along curve depending on time */
        curvetime = cu->ctime - data->offset;

        /* ctime is now a proper var setting of Curve which gets set by Animato like any other var
         * that's animated, but this will only work if it actually is animated...
         *
         * we divide the curvetime calculated in the previous step by the length of the path,
         * to get a time factor. */
        curvetime /= cu->pathlen;

        Nurb *nu = cu->nurb.first;
        if (!(nu && nu->flagu & CU_NURB_CYCLIC) && cu->flag & CU_PATH_CLAMP) {
          /* If curve is not cyclic, clamp to the begin/end points if the curve clamp option is on.
           */
          CLAMP(curvetime, 0.0f, 1.0f);
        }
      }
      else {
        /* fixed position along curve */
        curvetime = data->offset_fac;
      }

      if (BKE_where_on_path(ct->tar,
                            curvetime,
                            vec,
                            dir,
                            (data->followflag & FOLLOWPATH_FOLLOW) ? quat : NULL,
                            &radius,
                            NULL)) { /* quat_pt is quat or NULL. */
        float totmat[4][4];
        unit_m4(totmat);

        if (data->followflag & FOLLOWPATH_FOLLOW) {
          quat_apply_track(quat, data->trackflag, data->upflag);
          quat_to_mat4(totmat, quat);
        }

        if (data->followflag & FOLLOWPATH_RADIUS) {
          float tmat[4][4], rmat[4][4];
          scale_m4_fl(tmat, radius);
          mul_m4_m4m4(rmat, tmat, totmat);
          copy_m4_m4(totmat, rmat);
        }

        copy_v3_v3(totmat[3], vec);

        mul_m4_m4m4(ct->matrix, ct->tar->obmat, totmat);
      }
    }
  }
  else if (ct) {
    unit_m4(ct->matrix);
  }
}

static void followpath_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float obmat[4][4];
    float size[3];
    bFollowPathConstraint *data = con->data;

    /* get Object transform (loc/rot/size) to determine transformation from path */
    /* TODO: this used to be local at one point, but is probably more useful as-is */
    copy_m4_m4(obmat, cob->matrix);

    /* get scaling of object before applying constraint */
    mat4_to_size(size, cob->matrix);

    /* apply targetmat - containing location on path, and rotation */
    mul_m4_m4m4(cob->matrix, ct->matrix, obmat);

    /* un-apply scaling caused by path */
    if ((data->followflag & FOLLOWPATH_RADIUS) == 0) {
      /* XXX(campbell): Assume that scale correction means that radius
       * will have some scale error in it. */
      float obsize[3];

      mat4_to_size(obsize, cob->matrix);
      if (obsize[0]) {
        mul_v3_fl(cob->matrix[0], size[0] / obsize[0]);
      }
      if (obsize[1]) {
        mul_v3_fl(cob->matrix[1], size[1] / obsize[1]);
      }
      if (obsize[2]) {
        mul_v3_fl(cob->matrix[2], size[2] / obsize[2]);
      }
    }
  }
}

static bConstraintTypeInfo CTI_FOLLOWPATH = {
    CONSTRAINT_TYPE_FOLLOWPATH,    /* type */
    sizeof(bFollowPathConstraint), /* size */
    "Follow Path",                 /* name */
    "bFollowPathConstraint",       /* struct name */
    NULL,                          /* free data */
    followpath_id_looper,          /* id looper */
    NULL,                          /* copy data */
    followpath_new_data,           /* new data */
    followpath_get_tars,           /* get constraint targets */
    followpath_flush_tars,         /* flush constraint targets */
    followpath_get_tarmat,         /* get target matrix */
    followpath_evaluate,           /* evaluate */
};

/* --------- Limit Location --------- */

static void loclimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bLocLimitConstraint *data = con->data;

  if (data->flag & LIMIT_XMIN) {
    if (cob->matrix[3][0] < data->xmin) {
      cob->matrix[3][0] = data->xmin;
    }
  }
  if (data->flag & LIMIT_XMAX) {
    if (cob->matrix[3][0] > data->xmax) {
      cob->matrix[3][0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YMIN) {
    if (cob->matrix[3][1] < data->ymin) {
      cob->matrix[3][1] = data->ymin;
    }
  }
  if (data->flag & LIMIT_YMAX) {
    if (cob->matrix[3][1] > data->ymax) {
      cob->matrix[3][1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZMIN) {
    if (cob->matrix[3][2] < data->zmin) {
      cob->matrix[3][2] = data->zmin;
    }
  }
  if (data->flag & LIMIT_ZMAX) {
    if (cob->matrix[3][2] > data->zmax) {
      cob->matrix[3][2] = data->zmax;
    }
  }
}

static bConstraintTypeInfo CTI_LOCLIMIT = {
    CONSTRAINT_TYPE_LOCLIMIT,    /* type */
    sizeof(bLocLimitConstraint), /* size */
    "Limit Location",            /* name */
    "bLocLimitConstraint",       /* struct name */
    NULL,                        /* free data */
    custom_space_id_looper,      /* id looper */
    NULL,                        /* copy data */
    NULL,                        /* new data */
    get_space_tar,               /* get constraint targets */
    flush_space_tar,             /* flush constraint targets */
    NULL,                        /* get target matrix */
    loclimit_evaluate,           /* evaluate */
};

/* -------- Limit Rotation --------- */

static void rotlimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bRotLimitConstraint *data = con->data;
  float loc[3];
  float eul[3];
  float size[3];

  /* This constraint is based on euler rotation math, which doesn't work well with shear.
   * The Y axis is chosen as the main one because constraints are most commonly used on bones.
   * This also allows using the constraint to simply remove shear. */
  orthogonalize_m4_stable(cob->matrix, 1, false);

  /* Only do the complex processing if some limits are actually enabled. */
  if (!(data->flag & (LIMIT_XROT | LIMIT_YROT | LIMIT_ZROT))) {
    return;
  }

  /* Select the Euler rotation order, defaulting to the owner value. */
  short rot_order = cob->rotOrder;

  if (data->euler_order != CONSTRAINT_EULER_AUTO) {
    rot_order = data->euler_order;
  }

  /* Decompose the matrix using the specified order. */
  copy_v3_v3(loc, cob->matrix[3]);
  mat4_to_size(size, cob->matrix);

  mat4_to_eulO(eul, rot_order, cob->matrix);

  /* constraint data uses radians internally */

  /* limiting of euler values... */
  if (data->flag & LIMIT_XROT) {
    if (eul[0] < data->xmin) {
      eul[0] = data->xmin;
    }

    if (eul[0] > data->xmax) {
      eul[0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YROT) {
    if (eul[1] < data->ymin) {
      eul[1] = data->ymin;
    }

    if (eul[1] > data->ymax) {
      eul[1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZROT) {
    if (eul[2] < data->zmin) {
      eul[2] = data->zmin;
    }

    if (eul[2] > data->zmax) {
      eul[2] = data->zmax;
    }
  }

  loc_eulO_size_to_mat4(cob->matrix, loc, eul, size, rot_order);
}

static bConstraintTypeInfo CTI_ROTLIMIT = {
    CONSTRAINT_TYPE_ROTLIMIT,    /* type */
    sizeof(bRotLimitConstraint), /* size */
    "Limit Rotation",            /* name */
    "bRotLimitConstraint",       /* struct name */
    NULL,                        /* free data */
    custom_space_id_looper,      /* id looper */
    NULL,                        /* copy data */
    NULL,                        /* new data */
    get_space_tar,               /* get constraint targets */
    flush_space_tar,             /* flush constraint targets */
    NULL,                        /* get target matrix */
    rotlimit_evaluate,           /* evaluate */
};

/* --------- Limit Scale --------- */

static void sizelimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bSizeLimitConstraint *data = con->data;
  float obsize[3], size[3];

  mat4_to_size(size, cob->matrix);
  mat4_to_size(obsize, cob->matrix);

  if (data->flag & LIMIT_XMIN) {
    if (size[0] < data->xmin) {
      size[0] = data->xmin;
    }
  }
  if (data->flag & LIMIT_XMAX) {
    if (size[0] > data->xmax) {
      size[0] = data->xmax;
    }
  }
  if (data->flag & LIMIT_YMIN) {
    if (size[1] < data->ymin) {
      size[1] = data->ymin;
    }
  }
  if (data->flag & LIMIT_YMAX) {
    if (size[1] > data->ymax) {
      size[1] = data->ymax;
    }
  }
  if (data->flag & LIMIT_ZMIN) {
    if (size[2] < data->zmin) {
      size[2] = data->zmin;
    }
  }
  if (data->flag & LIMIT_ZMAX) {
    if (size[2] > data->zmax) {
      size[2] = data->zmax;
    }
  }

  if (obsize[0]) {
    mul_v3_fl(cob->matrix[0], size[0] / obsize[0]);
  }
  if (obsize[1]) {
    mul_v3_fl(cob->matrix[1], size[1] / obsize[1]);
  }
  if (obsize[2]) {
    mul_v3_fl(cob->matrix[2], size[2] / obsize[2]);
  }
}

static bConstraintTypeInfo CTI_SIZELIMIT = {
    CONSTRAINT_TYPE_SIZELIMIT,    /* type */
    sizeof(bSizeLimitConstraint), /* size */
    "Limit Scale",                /* name */
    "bSizeLimitConstraint",       /* struct name */
    NULL,                         /* free data */
    custom_space_id_looper,       /* id looper */
    NULL,                         /* copy data */
    NULL,                         /* new data */
    get_space_tar,                /* get constraint targets */
    flush_space_tar,              /* flush constraint targets */
    NULL,                         /* get target matrix */
    sizelimit_evaluate,           /* evaluate */
};

/* ----------- Copy Location ------------- */

static void loclike_new_data(void *cdata)
{
  bLocateLikeConstraint *data = (bLocateLikeConstraint *)cdata;

  data->flag = LOCLIKE_X | LOCLIKE_Y | LOCLIKE_Z;
}

static void loclike_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bLocateLikeConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int loclike_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bLocateLikeConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void loclike_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bLocateLikeConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void loclike_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bLocateLikeConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float offset[3] = {0.0f, 0.0f, 0.0f};

    if (data->flag & LOCLIKE_OFFSET) {
      copy_v3_v3(offset, cob->matrix[3]);
    }

    if (data->flag & LOCLIKE_X) {
      cob->matrix[3][0] = ct->matrix[3][0];

      if (data->flag & LOCLIKE_X_INVERT) {
        cob->matrix[3][0] *= -1;
      }
      cob->matrix[3][0] += offset[0];
    }
    if (data->flag & LOCLIKE_Y) {
      cob->matrix[3][1] = ct->matrix[3][1];

      if (data->flag & LOCLIKE_Y_INVERT) {
        cob->matrix[3][1] *= -1;
      }
      cob->matrix[3][1] += offset[1];
    }
    if (data->flag & LOCLIKE_Z) {
      cob->matrix[3][2] = ct->matrix[3][2];

      if (data->flag & LOCLIKE_Z_INVERT) {
        cob->matrix[3][2] *= -1;
      }
      cob->matrix[3][2] += offset[2];
    }
  }
}

static bConstraintTypeInfo CTI_LOCLIKE = {
    CONSTRAINT_TYPE_LOCLIKE,       /* type */
    sizeof(bLocateLikeConstraint), /* size */
    "Copy Location",               /* name */
    "bLocateLikeConstraint",       /* struct name */
    NULL,                          /* free data */
    loclike_id_looper,             /* id looper */
    NULL,                          /* copy data */
    loclike_new_data,              /* new data */
    loclike_get_tars,              /* get constraint targets */
    loclike_flush_tars,            /* flush constraint targets */
    default_get_tarmat,            /* get target matrix */
    loclike_evaluate,              /* evaluate */
};

/* ----------- Copy Rotation ------------- */

static void rotlike_new_data(void *cdata)
{
  bRotateLikeConstraint *data = (bRotateLikeConstraint *)cdata;

  data->flag = ROTLIKE_X | ROTLIKE_Y | ROTLIKE_Z;
}

static void rotlike_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bRotateLikeConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int rotlike_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bRotateLikeConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void rotlike_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bRotateLikeConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void rotlike_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bRotateLikeConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float loc[3], size[3], oldrot[3][3], newrot[3][3];
    float eul[3], obeul[3], defeul[3];

    mat4_to_loc_rot_size(loc, oldrot, size, cob->matrix);

    /* Select the Euler rotation order, defaulting to the owner. */
    short rot_order = cob->rotOrder;

    if (data->euler_order != CONSTRAINT_EULER_AUTO) {
      rot_order = data->euler_order;
    }

    /* To allow compatible rotations, must get both rotations in the order of the owner... */
    mat4_to_eulO(obeul, rot_order, cob->matrix);
    /* We must get compatible eulers from the beginning because
     * some of them can be modified below (see bug T21875).
     * Additionally, since this constraint is based on euler rotation math, it doesn't work well
     * with shear. The Y axis is chosen as the main axis when we orthogonalize the matrix because
     * constraints are used most commonly on bones. */
    float mat[4][4];
    copy_m4_m4(mat, ct->matrix);
    orthogonalize_m4_stable(mat, 1, true);
    mat4_to_compatible_eulO(eul, obeul, rot_order, mat);

    /* Prepare the copied euler rotation. */
    bool legacy_offset = false;

    switch (data->mix_mode) {
      case ROTLIKE_MIX_OFFSET:
        legacy_offset = true;
        copy_v3_v3(defeul, obeul);
        break;

      case ROTLIKE_MIX_REPLACE:
        copy_v3_v3(defeul, obeul);
        break;

      default:
        zero_v3(defeul);
    }

    if ((data->flag & ROTLIKE_X) == 0) {
      eul[0] = defeul[0];
    }
    else {
      if (legacy_offset) {
        rotate_eulO(eul, rot_order, 'X', obeul[0]);
      }

      if (data->flag & ROTLIKE_X_INVERT) {
        eul[0] *= -1;
      }
    }

    if ((data->flag & ROTLIKE_Y) == 0) {
      eul[1] = defeul[1];
    }
    else {
      if (legacy_offset) {
        rotate_eulO(eul, rot_order, 'Y', obeul[1]);
      }

      if (data->flag & ROTLIKE_Y_INVERT) {
        eul[1] *= -1;
      }
    }

    if ((data->flag & ROTLIKE_Z) == 0) {
      eul[2] = defeul[2];
    }
    else {
      if (legacy_offset) {
        rotate_eulO(eul, rot_order, 'Z', obeul[2]);
      }

      if (data->flag & ROTLIKE_Z_INVERT) {
        eul[2] *= -1;
      }
    }

    /* Add the euler components together if needed. */
    if (data->mix_mode == ROTLIKE_MIX_ADD) {
      add_v3_v3(eul, obeul);
    }

    /* Good to make eulers compatible again,
     * since we don't know how much they were changed above. */
    compatible_eul(eul, obeul);
    eulO_to_mat3(newrot, eul, rot_order);

    /* Mix the rotation matrices: */
    switch (data->mix_mode) {
      case ROTLIKE_MIX_REPLACE:
      case ROTLIKE_MIX_OFFSET:
      case ROTLIKE_MIX_ADD:
        break;

      case ROTLIKE_MIX_BEFORE:
        mul_m3_m3m3(newrot, newrot, oldrot);
        break;

      case ROTLIKE_MIX_AFTER:
        mul_m3_m3m3(newrot, oldrot, newrot);
        break;

      default:
        BLI_assert(false);
    }

    loc_rot_size_to_mat4(cob->matrix, loc, newrot, size);
  }
}

static bConstraintTypeInfo CTI_ROTLIKE = {
    CONSTRAINT_TYPE_ROTLIKE,       /* type */
    sizeof(bRotateLikeConstraint), /* size */
    "Copy Rotation",               /* name */
    "bRotateLikeConstraint",       /* struct name */
    NULL,                          /* free data */
    rotlike_id_looper,             /* id looper */
    NULL,                          /* copy data */
    rotlike_new_data,              /* new data */
    rotlike_get_tars,              /* get constraint targets */
    rotlike_flush_tars,            /* flush constraint targets */
    default_get_tarmat,            /* get target matrix */
    rotlike_evaluate,              /* evaluate */
};

/* ---------- Copy Scale ---------- */

static void sizelike_new_data(void *cdata)
{
  bSizeLikeConstraint *data = (bSizeLikeConstraint *)cdata;

  data->flag = SIZELIKE_X | SIZELIKE_Y | SIZELIKE_Z | SIZELIKE_MULTIPLY;
  data->power = 1.0f;
}

static void sizelike_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bSizeLikeConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int sizelike_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bSizeLikeConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void sizelike_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bSizeLikeConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void sizelike_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bSizeLikeConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float obsize[3], size[3];

    mat4_to_size(obsize, cob->matrix);

    /* Compute one uniform scale factor to apply to all three axes. */
    if (data->flag & SIZELIKE_UNIFORM) {
      const int all_axes = SIZELIKE_X | SIZELIKE_Y | SIZELIKE_Z;
      float total = 1.0f;

      /* If all axes are selected, use the determinant. */
      if ((data->flag & all_axes) == all_axes) {
        total = fabsf(mat4_to_volume_scale(ct->matrix));
      }
      /* Otherwise multiply individual values. */
      else {
        mat4_to_size(size, ct->matrix);

        if (data->flag & SIZELIKE_X) {
          total *= size[0];
        }
        if (data->flag & SIZELIKE_Y) {
          total *= size[1];
        }
        if (data->flag & SIZELIKE_Z) {
          total *= size[2];
        }
      }

      copy_v3_fl(size, cbrt(total));
    }
    /* Regular per-axis scaling. */
    else {
      mat4_to_size(size, ct->matrix);
    }

    for (int i = 0; i < 3; i++) {
      size[i] = powf(size[i], data->power);
    }

    if (data->flag & SIZELIKE_OFFSET) {
      /* Scale is a multiplicative quantity, so adding it makes no sense.
       * However, the additive mode has to stay for backward compatibility. */
      if (data->flag & SIZELIKE_MULTIPLY) {
        /* size[i] *= obsize[i] */
        mul_v3_v3(size, obsize);
      }
      else {
        /* 2.7 compatibility mode: size[i] += (obsize[i] - 1.0f) */
        add_v3_v3(size, obsize);
        add_v3_fl(size, -1.0f);
      }
    }

    if ((data->flag & (SIZELIKE_X | SIZELIKE_UNIFORM)) && (obsize[0] != 0)) {
      mul_v3_fl(cob->matrix[0], size[0] / obsize[0]);
    }
    if ((data->flag & (SIZELIKE_Y | SIZELIKE_UNIFORM)) && (obsize[1] != 0)) {
      mul_v3_fl(cob->matrix[1], size[1] / obsize[1]);
    }
    if ((data->flag & (SIZELIKE_Z | SIZELIKE_UNIFORM)) && (obsize[2] != 0)) {
      mul_v3_fl(cob->matrix[2], size[2] / obsize[2]);
    }
  }
}

static bConstraintTypeInfo CTI_SIZELIKE = {
    CONSTRAINT_TYPE_SIZELIKE,    /* type */
    sizeof(bSizeLikeConstraint), /* size */
    "Copy Scale",                /* name */
    "bSizeLikeConstraint",       /* struct name */
    NULL,                        /* free data */
    sizelike_id_looper,          /* id looper */
    NULL,                        /* copy data */
    sizelike_new_data,           /* new data */
    sizelike_get_tars,           /* get constraint targets */
    sizelike_flush_tars,         /* flush constraint targets */
    default_get_tarmat,          /* get target matrix */
    sizelike_evaluate,           /* evaluate */
};

/* ----------- Copy Transforms ------------- */

static void translike_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bTransLikeConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int translike_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bTransLikeConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void translike_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bTransLikeConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void translike_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bTransLikeConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float target_mat[4][4];

    copy_m4_m4(target_mat, ct->matrix);

    /* Remove the shear of the target matrix if enabled.
     * Use Y as the axis since it's the natural default for bones. */
    if (data->flag & TRANSLIKE_REMOVE_TARGET_SHEAR) {
      orthogonalize_m4_stable(target_mat, 1, false);
    }

    /* Finally, combine the matrices. */
    switch (data->mix_mode) {
      case TRANSLIKE_MIX_REPLACE:
        copy_m4_m4(cob->matrix, target_mat);
        break;

      /* Simple matrix multiplication. */
      case TRANSLIKE_MIX_BEFORE_FULL:
        mul_m4_m4m4(cob->matrix, target_mat, cob->matrix);
        break;

      case TRANSLIKE_MIX_AFTER_FULL:
        mul_m4_m4m4(cob->matrix, cob->matrix, target_mat);
        break;

      /* Aligned Inherit Scale emulation. */
      case TRANSLIKE_MIX_BEFORE:
        mul_m4_m4m4_aligned_scale(cob->matrix, target_mat, cob->matrix);
        break;

      case TRANSLIKE_MIX_AFTER:
        mul_m4_m4m4_aligned_scale(cob->matrix, cob->matrix, target_mat);
        break;

      /* Fully separate handling of channels. */
      case TRANSLIKE_MIX_BEFORE_SPLIT:
        mul_m4_m4m4_split_channels(cob->matrix, target_mat, cob->matrix);
        break;

      case TRANSLIKE_MIX_AFTER_SPLIT:
        mul_m4_m4m4_split_channels(cob->matrix, cob->matrix, target_mat);
        break;

      default:
        BLI_assert_msg(0, "Unknown Copy Transforms mix mode");
    }
  }
}

static bConstraintTypeInfo CTI_TRANSLIKE = {
    CONSTRAINT_TYPE_TRANSLIKE,     /* type */
    sizeof(bTransLikeConstraint),  /* size */
    "Copy Transforms",             /* name */
    "bTransLikeConstraint",        /* struct name */
    NULL,                          /* free data */
    translike_id_looper,           /* id looper */
    NULL,                          /* copy data */
    NULL,                          /* new data */
    translike_get_tars,            /* get constraint targets */
    translike_flush_tars,          /* flush constraint targets */
    default_get_tarmat_full_bbone, /* get target matrix */
    translike_evaluate,            /* evaluate */
};

/* ---------- Maintain Volume ---------- */

static void samevolume_new_data(void *cdata)
{
  bSameVolumeConstraint *data = (bSameVolumeConstraint *)cdata;

  data->free_axis = SAMEVOL_Y;
  data->volume = 1.0f;
}

static void samevolume_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *UNUSED(targets))
{
  bSameVolumeConstraint *data = con->data;

  float volume = data->volume;
  float fac = 1.0f, total_scale = 1.0f;
  float obsize[3];

  mat4_to_size(obsize, cob->matrix);

  /* calculate normalizing scale factor for non-essential values */
  switch (data->mode) {
    case SAMEVOL_STRICT:
      total_scale = obsize[0] * obsize[1] * obsize[2];
      break;
    case SAMEVOL_UNIFORM:
      total_scale = pow3f(obsize[data->free_axis]);
      break;
    case SAMEVOL_SINGLE_AXIS:
      total_scale = obsize[data->free_axis];
      break;
  }

  if (total_scale != 0) {
    fac = sqrtf(volume / total_scale);
  }

  /* apply scaling factor to the channels not being kept */
  switch (data->free_axis) {
    case SAMEVOL_X:
      mul_v3_fl(cob->matrix[1], fac);
      mul_v3_fl(cob->matrix[2], fac);
      break;
    case SAMEVOL_Y:
      mul_v3_fl(cob->matrix[0], fac);
      mul_v3_fl(cob->matrix[2], fac);
      break;
    case SAMEVOL_Z:
      mul_v3_fl(cob->matrix[0], fac);
      mul_v3_fl(cob->matrix[1], fac);
      break;
  }
}

static bConstraintTypeInfo CTI_SAMEVOL = {
    CONSTRAINT_TYPE_SAMEVOL,       /* type */
    sizeof(bSameVolumeConstraint), /* size */
    "Maintain Volume",             /* name */
    "bSameVolumeConstraint",       /* struct name */
    NULL,                          /* free data */
    custom_space_id_looper,        /* id looper */
    NULL,                          /* copy data */
    samevolume_new_data,           /* new data */
    get_space_tar,                 /* get constraint targets */
    flush_space_tar,               /* flush constraint targets */
    NULL,                          /* get target matrix */
    samevolume_evaluate,           /* evaluate */
};

/* ----------- Python Constraint -------------- */

static void pycon_free(bConstraint *con)
{
  bPythonConstraint *data = con->data;

  /* id-properties */
  IDP_FreeProperty(data->prop);

  /* multiple targets */
  BLI_freelistN(&data->targets);
}

static void pycon_copy(bConstraint *con, bConstraint *srccon)
{
  bPythonConstraint *pycon = (bPythonConstraint *)con->data;
  bPythonConstraint *opycon = (bPythonConstraint *)srccon->data;

  pycon->prop = IDP_CopyProperty(opycon->prop);
  BLI_duplicatelist(&pycon->targets, &opycon->targets);
}

static void pycon_new_data(void *cdata)
{
  bPythonConstraint *data = (bPythonConstraint *)cdata;

  /* Everything should be set correctly by calloc, except for the prop->type constant. */
  data->prop = MEM_callocN(sizeof(IDProperty), "PyConstraintProps");
  data->prop->type = IDP_GROUP;
}

static int pycon_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bPythonConstraint *data = con->data;

    list->first = data->targets.first;
    list->last = data->targets.last;

    return data->tarnum;
  }

  return 0;
}

static void pycon_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bPythonConstraint *data = con->data;

  /* targets */
  LISTBASE_FOREACH (bConstraintTarget *, ct, &data->targets) {
    func(con, (ID **)&ct->tar, false, userdata);
  }

  /* script */
  func(con, (ID **)&data->text, true, userdata);
}

/* Whether this approach is maintained remains to be seen (aligorith) */
static void pycon_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                             bConstraint *con,
                             bConstraintOb *cob,
                             bConstraintTarget *ct,
                             float UNUSED(ctime))
{
#ifdef WITH_PYTHON
  bPythonConstraint *data = con->data;
#endif

  if (VALID_CONS_TARGET(ct)) {
    if (ct->tar->type == OB_CURVES_LEGACY && ct->tar->runtime.curve_cache == NULL) {
      unit_m4(ct->matrix);
      return;
    }

    /* firstly calculate the matrix the normal way, then let the py-function override
     * this matrix if it needs to do so
     */
    constraint_target_to_mat4(ct->tar,
                              ct->subtarget,
                              cob,
                              ct->matrix,
                              CONSTRAINT_SPACE_WORLD,
                              ct->space,
                              con->flag,
                              con->headtail);

    /* only execute target calculation if allowed */
#ifdef WITH_PYTHON
    if (G.f & G_FLAG_SCRIPT_AUTOEXEC) {
      BPY_pyconstraint_target(data, ct);
    }
#endif
  }
  else if (ct) {
    unit_m4(ct->matrix);
  }
}

static void pycon_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
#ifndef WITH_PYTHON
  UNUSED_VARS(con, cob, targets);
  return;
#else
  bPythonConstraint *data = con->data;

  /* only evaluate in python if we're allowed to do so */
  if ((G.f & G_FLAG_SCRIPT_AUTOEXEC) == 0) {
    return;
  }

  /* Now, run the actual 'constraint' function, which should only access the matrices */
  BPY_pyconstraint_exec(data, cob, targets);
#endif /* WITH_PYTHON */
}

static bConstraintTypeInfo CTI_PYTHON = {
    CONSTRAINT_TYPE_PYTHON,    /* type */
    sizeof(bPythonConstraint), /* size */
    "Script",                  /* name */
    "bPythonConstraint",       /* struct name */
    pycon_free,                /* free data */
    pycon_id_looper,           /* id looper */
    pycon_copy,                /* copy data */
    pycon_new_data,            /* new data */
    pycon_get_tars,            /* get constraint targets */
    NULL,                      /* flush constraint targets */
    pycon_get_tarmat,          /* get target matrix */
    pycon_evaluate,            /* evaluate */
};

/* ----------- Armature Constraint -------------- */

static void armdef_free(bConstraint *con)
{
  bArmatureConstraint *data = con->data;

  /* Target list. */
  BLI_freelistN(&data->targets);
}

static void armdef_copy(bConstraint *con, bConstraint *srccon)
{
  bArmatureConstraint *pcon = (bArmatureConstraint *)con->data;
  bArmatureConstraint *opcon = (bArmatureConstraint *)srccon->data;

  BLI_duplicatelist(&pcon->targets, &opcon->targets);
}

static int armdef_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bArmatureConstraint *data = con->data;

    *list = data->targets;

    return BLI_listbase_count(&data->targets);
  }

  return 0;
}

static void armdef_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bArmatureConstraint *data = con->data;

  /* Target list. */
  LISTBASE_FOREACH (bConstraintTarget *, ct, &data->targets) {
    func(con, (ID **)&ct->tar, false, userdata);
  }
}

/* Compute the world space pose matrix of the target bone. */
static void armdef_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                              bConstraint *UNUSED(con),
                              bConstraintOb *UNUSED(cob),
                              bConstraintTarget *ct,
                              float UNUSED(ctime))
{
  if (ct != NULL) {
    if (ct->tar && ct->tar->type == OB_ARMATURE) {
      bPoseChannel *pchan = BKE_pose_channel_find_name(ct->tar->pose, ct->subtarget);

      if (pchan != NULL) {
        mul_m4_m4m4(ct->matrix, ct->tar->obmat, pchan->pose_mat);
        return;
      }
    }

    unit_m4(ct->matrix);
  }
}

static void armdef_accumulate_matrix(const float obmat[4][4],
                                     const float iobmat[4][4],
                                     const float basemat[4][4],
                                     const float bonemat[4][4],
                                     float weight,
                                     float r_sum_mat[4][4],
                                     DualQuat *r_sum_dq)
{
  if (weight == 0.0f) {
    return;
  }

  /* Convert the selected matrix into object space. */
  float mat[4][4];
  mul_m4_series(mat, obmat, bonemat, iobmat);

  /* Accumulate the transformation. */
  if (r_sum_dq != NULL) {
    float basemat_world[4][4];
    DualQuat tmpdq;

    /* Compute the orthonormal rest matrix in world space. */
    mul_m4_m4m4(basemat_world, obmat, basemat);
    orthogonalize_m4_stable(basemat_world, 1, true);

    mat4_to_dquat(&tmpdq, basemat_world, mat);
    add_weighted_dq_dq(r_sum_dq, &tmpdq, weight);
  }
  else {
    madd_m4_m4m4fl(r_sum_mat, r_sum_mat, mat, weight);
  }
}

/* Compute and accumulate transformation for a single target bone. */
static void armdef_accumulate_bone(bConstraintTarget *ct,
                                   bPoseChannel *pchan,
                                   const float wco[3],
                                   bool force_envelope,
                                   float *r_totweight,
                                   float r_sum_mat[4][4],
                                   DualQuat *r_sum_dq)
{
  float iobmat[4][4], co[3];
  Bone *bone = pchan->bone;
  float weight = ct->weight;

  /* Our object's location in target pose space. */
  invert_m4_m4(iobmat, ct->tar->obmat);
  mul_v3_m4v3(co, iobmat, wco);

  /* Multiply by the envelope weight when appropriate. */
  if (force_envelope || (bone->flag & BONE_MULT_VG_ENV)) {
    weight *= distfactor_to_bone(
        co, bone->arm_head, bone->arm_tail, bone->rad_head, bone->rad_tail, bone->dist);
  }

  /* Find the correct bone transform matrix in world space. */
  if (bone->segments > 1 && bone->segments == pchan->runtime.bbone_segments) {
    Mat4 *b_bone_mats = pchan->runtime.bbone_deform_mats;
    Mat4 *b_bone_rest_mats = pchan->runtime.bbone_rest_mats;
    float(*iamat)[4] = b_bone_mats[0].mat;
    float basemat[4][4];

    /* The target is a B-Bone:
     * FIRST: find the segment (see b_bone_deform in armature.c)
     * Need to transform co back to bone-space, only need y. */
    float y = iamat[0][1] * co[0] + iamat[1][1] * co[1] + iamat[2][1] * co[2] + iamat[3][1];

    /* Blend the matrix. */
    int index;
    float blend;
    BKE_pchan_bbone_deform_segment_index(pchan, y / bone->length, &index, &blend);

    if (r_sum_dq != NULL) {
      /* Compute the object space rest matrix of the segment. */
      mul_m4_m4m4(basemat, bone->arm_mat, b_bone_rest_mats[index].mat);
    }

    armdef_accumulate_matrix(ct->tar->obmat,
                             iobmat,
                             basemat,
                             b_bone_mats[index + 1].mat,
                             weight * (1.0f - blend),
                             r_sum_mat,
                             r_sum_dq);

    if (r_sum_dq != NULL) {
      /* Compute the object space rest matrix of the segment. */
      mul_m4_m4m4(basemat, bone->arm_mat, b_bone_rest_mats[index + 1].mat);
    }

    armdef_accumulate_matrix(ct->tar->obmat,
                             iobmat,
                             basemat,
                             b_bone_mats[index + 2].mat,
                             weight * blend,
                             r_sum_mat,
                             r_sum_dq);
  }
  else {
    /* Simple bone. This requires DEG_OPCODE_BONE_DONE dependency due to chan_mat. */
    armdef_accumulate_matrix(
        ct->tar->obmat, iobmat, bone->arm_mat, pchan->chan_mat, weight, r_sum_mat, r_sum_dq);
  }

  /* Accumulate the weight. */
  *r_totweight += weight;
}

static void armdef_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bArmatureConstraint *data = con->data;

  float sum_mat[4][4], input_co[3];
  DualQuat sum_dq;
  float weight = 0.0f;

  /* Prepare for blending. */
  zero_m4(sum_mat);
  memset(&sum_dq, 0, sizeof(sum_dq));

  DualQuat *pdq = (data->flag & CONSTRAINT_ARMATURE_QUATERNION) ? &sum_dq : NULL;
  bool use_envelopes = (data->flag & CONSTRAINT_ARMATURE_ENVELOPE) != 0;

  if (cob->pchan && cob->pchan->bone && !(data->flag & CONSTRAINT_ARMATURE_CUR_LOCATION)) {
    /* For constraints on bones, use the rest position to bind b-bone segments
     * and envelopes, to allow safely changing the bone location as if parented. */
    copy_v3_v3(input_co, cob->pchan->bone->arm_head);
    mul_m4_v3(cob->ob->obmat, input_co);
  }
  else {
    copy_v3_v3(input_co, cob->matrix[3]);
  }

  /* Process all targets. This can't use ct->matrix, as armdef_get_tarmat is not
   * called in solve for efficiency because the constraint needs bone data anyway. */
  LISTBASE_FOREACH (bConstraintTarget *, ct, targets) {
    if (ct->weight <= 0.0f) {
      continue;
    }

    /* Lookup the bone and abort if failed. */
    if (!VALID_CONS_TARGET(ct) || ct->tar->type != OB_ARMATURE) {
      return;
    }

    bPoseChannel *pchan = BKE_pose_channel_find_name(ct->tar->pose, ct->subtarget);

    if (pchan == NULL || pchan->bone == NULL) {
      return;
    }

    armdef_accumulate_bone(ct, pchan, input_co, use_envelopes, &weight, sum_mat, pdq);
  }

  /* Compute the final transform. */
  if (weight > 0.0f) {
    if (pdq != NULL) {
      normalize_dq(pdq, weight);
      dquat_to_mat4(sum_mat, pdq);
    }
    else {
      mul_m4_fl(sum_mat, 1.0f / weight);
    }

    /* Apply the transform to the result matrix. */
    mul_m4_m4m4(cob->matrix, sum_mat, cob->matrix);
  }
}

static bConstraintTypeInfo CTI_ARMATURE = {
    CONSTRAINT_TYPE_ARMATURE,    /* type */
    sizeof(bArmatureConstraint), /* size */
    "Armature",                  /* name */
    "bArmatureConstraint",       /* struct name */
    armdef_free,                 /* free data */
    armdef_id_looper,            /* id looper */
    armdef_copy,                 /* copy data */
    NULL,                        /* new data */
    armdef_get_tars,             /* get constraint targets */
    NULL,                        /* flush constraint targets */
    armdef_get_tarmat,           /* get target matrix */
    armdef_evaluate,             /* evaluate */
};

/* -------- Action Constraint ----------- */

static void actcon_new_data(void *cdata)
{
  bActionConstraint *data = (bActionConstraint *)cdata;

  /* set type to 20 (Loc X), as 0 is Rot X for backwards compatibility */
  data->type = 20;

  /* Set the mix mode to After Original with anti-shear scale handling. */
  data->mix_mode = ACTCON_MIX_AFTER;
}

static void actcon_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bActionConstraint *data = con->data;

  /* target */
  func(con, (ID **)&data->tar, false, userdata);

  /* action */
  func(con, (ID **)&data->act, true, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int actcon_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bActionConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void actcon_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bActionConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void actcon_get_tarmat(struct Depsgraph *depsgraph,
                              bConstraint *con,
                              bConstraintOb *cob,
                              bConstraintTarget *ct,
                              float UNUSED(ctime))
{
  bActionConstraint *data = con->data;

  if (VALID_CONS_TARGET(ct) || data->flag & ACTCON_USE_EVAL_TIME) {
    float tempmat[4][4], vec[3];
    float s, t;
    short axis;

    /* initialize return matrix */
    unit_m4(ct->matrix);

    /* Skip targets if we're using local float property to set action time */
    if (data->flag & ACTCON_USE_EVAL_TIME) {
      s = data->eval_time;
    }
    else {
      /* get the transform matrix of the target */
      constraint_target_to_mat4(ct->tar,
                                ct->subtarget,
                                cob,
                                tempmat,
                                CONSTRAINT_SPACE_WORLD,
                                ct->space,
                                con->flag,
                                con->headtail);

      /* determine where in transform range target is */
      /* data->type is mapped as follows for backwards compatibility:
       * 00,01,02 - rotation (it used to be like this)
       * 10,11,12 - scaling
       * 20,21,22 - location
       */
      if (data->type < 10) {
        /* extract rotation (is in whatever space target should be in) */
        mat4_to_eul(vec, tempmat);
        mul_v3_fl(vec, RAD2DEGF(1.0f)); /* rad -> deg */
        axis = data->type;
      }
      else if (data->type < 20) {
        /* extract scaling (is in whatever space target should be in) */
        mat4_to_size(vec, tempmat);
        axis = data->type - 10;
      }
      else {
        /* extract location */
        copy_v3_v3(vec, tempmat[3]);
        axis = data->type - 20;
      }

      BLI_assert((unsigned int)axis < 3);

      /* Target defines the animation */
      s = (vec[axis] - data->min) / (data->max - data->min);
    }

    CLAMP(s, 0, 1);
    t = (s * (data->end - data->start)) + data->start;
    const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(depsgraph,
                                                                                      t);

    if (G.debug & G_DEBUG) {
      printf("do Action Constraint %s - Ob %s Pchan %s\n",
             con->name,
             cob->ob->id.name + 2,
             (cob->pchan) ? cob->pchan->name : NULL);
    }

    /* Get the appropriate information from the action */
    if (cob->type == CONSTRAINT_OBTYPE_OBJECT || (data->flag & ACTCON_BONE_USE_OBJECT_ACTION)) {
      Object workob;

      /* evaluate using workob */
      /* FIXME: we don't have any consistent standards on limiting effects on object... */
      what_does_obaction(cob->ob, &workob, NULL, data->act, NULL, &anim_eval_context);
      BKE_object_to_mat4(&workob, ct->matrix);
    }
    else if (cob->type == CONSTRAINT_OBTYPE_BONE) {
      Object workob;
      bPose pose = {{0}};
      bPoseChannel *pchan, *tchan;

      /* make a copy of the bone of interest in the temp pose before evaluating action,
       * so that it can get set - we need to manually copy over a few settings,
       * including rotation order, otherwise this fails. */
      pchan = cob->pchan;

      tchan = BKE_pose_channel_ensure(&pose, pchan->name);
      tchan->rotmode = pchan->rotmode;

      /* evaluate action using workob (it will only set the PoseChannel in question) */
      what_does_obaction(cob->ob, &workob, &pose, data->act, pchan->name, &anim_eval_context);

      /* convert animation to matrices for use here */
      BKE_pchan_calc_mat(tchan);
      copy_m4_m4(ct->matrix, tchan->chan_mat);

      /* Clean up */
      BKE_pose_free_data(&pose);
    }
    else {
      /* behavior undefined... */
      puts("Error: unknown owner type for Action Constraint");
    }
  }
}

static void actcon_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bActionConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct) || data->flag & ACTCON_USE_EVAL_TIME) {
    switch (data->mix_mode) {
      /* Simple matrix multiplication. */
      case ACTCON_MIX_BEFORE_FULL:
        mul_m4_m4m4(cob->matrix, ct->matrix, cob->matrix);
        break;

      case ACTCON_MIX_AFTER_FULL:
        mul_m4_m4m4(cob->matrix, cob->matrix, ct->matrix);
        break;

      /* Aligned Inherit Scale emulation. */
      case ACTCON_MIX_BEFORE:
        mul_m4_m4m4_aligned_scale(cob->matrix, ct->matrix, cob->matrix);
        break;

      case ACTCON_MIX_AFTER:
        mul_m4_m4m4_aligned_scale(cob->matrix, cob->matrix, ct->matrix);
        break;

      /* Fully separate handling of channels. */
      case ACTCON_MIX_BEFORE_SPLIT:
        mul_m4_m4m4_split_channels(cob->matrix, ct->matrix, cob->matrix);
        break;

      case ACTCON_MIX_AFTER_SPLIT:
        mul_m4_m4m4_split_channels(cob->matrix, cob->matrix, ct->matrix);
        break;

      default:
        BLI_assert_msg(0, "Unknown Action mix mode");
    }
  }
}

static bConstraintTypeInfo CTI_ACTION = {
    CONSTRAINT_TYPE_ACTION,    /* type */
    sizeof(bActionConstraint), /* size */
    "Action",                  /* name */
    "bActionConstraint",       /* struct name */
    NULL,                      /* free data */
    actcon_id_looper,          /* id looper */
    NULL,                      /* copy data */
    actcon_new_data,           /* new data */
    actcon_get_tars,           /* get constraint targets */
    actcon_flush_tars,         /* flush constraint targets */
    actcon_get_tarmat,         /* get target matrix */
    actcon_evaluate,           /* evaluate */
};

/* --------- Locked Track ---------- */

static void locktrack_new_data(void *cdata)
{
  bLockTrackConstraint *data = (bLockTrackConstraint *)cdata;

  data->trackflag = TRACK_Y;
  data->lockflag = LOCK_Z;
}

static void locktrack_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bLockTrackConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int locktrack_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bLockTrackConstraint *data = con->data;
    bConstraintTarget *ct;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void locktrack_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bLockTrackConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

static void locktrack_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bLockTrackConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float vec[3], vec2[3];
    float totmat[3][3];
    float tmpmat[3][3];
    float invmat[3][3];
    float mdet;

    /* Vector object -> target */
    sub_v3_v3v3(vec, ct->matrix[3], cob->matrix[3]);
    switch (data->lockflag) {
      case LOCK_X: /* LOCK X */
      {
        switch (data->trackflag) {
          case TRACK_Y: /* LOCK X TRACK Y */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[0]);
            sub_v3_v3v3(totmat[1], vec, vec2);
            normalize_v3(totmat[1]);

            /* the x axis is fixed */
            normalize_v3_v3(totmat[0], cob->matrix[0]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[2], totmat[0], totmat[1]);
            break;
          }
          case TRACK_Z: /* LOCK X TRACK Z */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[0]);
            sub_v3_v3v3(totmat[2], vec, vec2);
            normalize_v3(totmat[2]);

            /* the x axis is fixed */
            normalize_v3_v3(totmat[0], cob->matrix[0]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[1], totmat[2], totmat[0]);
            break;
          }
          case TRACK_nY: /* LOCK X TRACK -Y */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[0]);
            sub_v3_v3v3(totmat[1], vec, vec2);
            normalize_v3(totmat[1]);
            negate_v3(totmat[1]);

            /* the x axis is fixed */
            normalize_v3_v3(totmat[0], cob->matrix[0]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[2], totmat[0], totmat[1]);
            break;
          }
          case TRACK_nZ: /* LOCK X TRACK -Z */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[0]);
            sub_v3_v3v3(totmat[2], vec, vec2);
            normalize_v3(totmat[2]);
            negate_v3(totmat[2]);

            /* the x axis is fixed */
            normalize_v3_v3(totmat[0], cob->matrix[0]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[1], totmat[2], totmat[0]);
            break;
          }
          default: {
            unit_m3(totmat);
            break;
          }
        }
        break;
      }
      case LOCK_Y: /* LOCK Y */
      {
        switch (data->trackflag) {
          case TRACK_X: /* LOCK Y TRACK X */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[1]);
            sub_v3_v3v3(totmat[0], vec, vec2);
            normalize_v3(totmat[0]);

            /* the y axis is fixed */
            normalize_v3_v3(totmat[1], cob->matrix[1]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[2], totmat[0], totmat[1]);
            break;
          }
          case TRACK_Z: /* LOCK Y TRACK Z */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[1]);
            sub_v3_v3v3(totmat[2], vec, vec2);
            normalize_v3(totmat[2]);

            /* the y axis is fixed */
            normalize_v3_v3(totmat[1], cob->matrix[1]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[0], totmat[1], totmat[2]);
            break;
          }
          case TRACK_nX: /* LOCK Y TRACK -X */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[1]);
            sub_v3_v3v3(totmat[0], vec, vec2);
            normalize_v3(totmat[0]);
            negate_v3(totmat[0]);

            /* the y axis is fixed */
            normalize_v3_v3(totmat[1], cob->matrix[1]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[2], totmat[0], totmat[1]);
            break;
          }
          case TRACK_nZ: /* LOCK Y TRACK -Z */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[1]);
            sub_v3_v3v3(totmat[2], vec, vec2);
            normalize_v3(totmat[2]);
            negate_v3(totmat[2]);

            /* the y axis is fixed */
            normalize_v3_v3(totmat[1], cob->matrix[1]);

            /* the z axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[0], totmat[1], totmat[2]);
            break;
          }
          default: {
            unit_m3(totmat);
            break;
          }
        }
        break;
      }
      case LOCK_Z: /* LOCK Z */
      {
        switch (data->trackflag) {
          case TRACK_X: /* LOCK Z TRACK X */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[2]);
            sub_v3_v3v3(totmat[0], vec, vec2);
            normalize_v3(totmat[0]);

            /* the z axis is fixed */
            normalize_v3_v3(totmat[2], cob->matrix[2]);

            /* the x axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[1], totmat[2], totmat[0]);
            break;
          }
          case TRACK_Y: /* LOCK Z TRACK Y */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[2]);
            sub_v3_v3v3(totmat[1], vec, vec2);
            normalize_v3(totmat[1]);

            /* the z axis is fixed */
            normalize_v3_v3(totmat[2], cob->matrix[2]);

            /* the x axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[0], totmat[1], totmat[2]);
            break;
          }
          case TRACK_nX: /* LOCK Z TRACK -X */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[2]);
            sub_v3_v3v3(totmat[0], vec, vec2);
            normalize_v3(totmat[0]);
            negate_v3(totmat[0]);

            /* the z axis is fixed */
            normalize_v3_v3(totmat[2], cob->matrix[2]);

            /* the x axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[1], totmat[2], totmat[0]);
            break;
          }
          case TRACK_nY: /* LOCK Z TRACK -Y */
          {
            /* Projection of Vector on the plane */
            project_v3_v3v3(vec2, vec, cob->matrix[2]);
            sub_v3_v3v3(totmat[1], vec, vec2);
            normalize_v3(totmat[1]);
            negate_v3(totmat[1]);

            /* the z axis is fixed */
            normalize_v3_v3(totmat[2], cob->matrix[2]);

            /* the x axis gets mapped onto a third orthogonal vector */
            cross_v3_v3v3(totmat[0], totmat[1], totmat[2]);
            break;
          }
          default: {
            unit_m3(totmat);
            break;
          }
        }
        break;
      }
      default: {
        unit_m3(totmat);
        break;
      }
    }
    /* Block to keep matrix heading */
    copy_m3_m4(tmpmat, cob->matrix);
    normalize_m3(tmpmat);
    invert_m3_m3(invmat, tmpmat);
    mul_m3_m3m3(tmpmat, totmat, invmat);
    totmat[0][0] = tmpmat[0][0];
    totmat[0][1] = tmpmat[0][1];
    totmat[0][2] = tmpmat[0][2];
    totmat[1][0] = tmpmat[1][0];
    totmat[1][1] = tmpmat[1][1];
    totmat[1][2] = tmpmat[1][2];
    totmat[2][0] = tmpmat[2][0];
    totmat[2][1] = tmpmat[2][1];
    totmat[2][2] = tmpmat[2][2];

    mdet = determinant_m3(totmat[0][0],
                          totmat[0][1],
                          totmat[0][2],
                          totmat[1][0],
                          totmat[1][1],
                          totmat[1][2],
                          totmat[2][0],
                          totmat[2][1],
                          totmat[2][2]);
    if (mdet == 0) {
      unit_m3(totmat);
    }

    /* apply out transformation to the object */
    mul_m4_m3m4(cob->matrix, totmat, cob->matrix);
  }
}

static bConstraintTypeInfo CTI_LOCKTRACK = {
    CONSTRAINT_TYPE_LOCKTRACK,    /* type */
    sizeof(bLockTrackConstraint), /* size */
    "Locked Track",               /* name */
    "bLockTrackConstraint",       /* struct name */
    NULL,                         /* free data */
    locktrack_id_looper,          /* id looper */
    NULL,                         /* copy data */
    locktrack_new_data,           /* new data */
    locktrack_get_tars,           /* get constraint targets */
    locktrack_flush_tars,         /* flush constraint targets */
    default_get_tarmat,           /* get target matrix */
    locktrack_evaluate,           /* evaluate */
};

/* ---------- Limit Distance Constraint ----------- */

static void distlimit_new_data(void *cdata)
{
  bDistLimitConstraint *data = (bDistLimitConstraint *)cdata;

  data->dist = 0.0f;
}

static void distlimit_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bDistLimitConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int distlimit_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bDistLimitConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void distlimit_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bDistLimitConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void distlimit_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bDistLimitConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float dvec[3], dist, sfac = 1.0f;
    short clamp_surf = 0;

    /* calculate our current distance from the target */
    dist = len_v3v3(cob->matrix[3], ct->matrix[3]);

    /* set distance (flag is only set when user demands it) */
    if (data->dist == 0) {
      data->dist = dist;

      /* Write the computed distance back to the master copy if in COW evaluation. */
      bConstraint *orig_con = constraint_find_original_for_update(cob, con);

      if (orig_con != NULL) {
        bDistLimitConstraint *orig_data = orig_con->data;

        orig_data->dist = data->dist;
      }
    }

    /* check if we're which way to clamp from, and calculate interpolation factor (if needed) */
    if (data->mode == LIMITDIST_OUTSIDE) {
      /* if inside, then move to surface */
      if (dist <= data->dist) {
        clamp_surf = 1;
        if (dist != 0.0f) {
          sfac = data->dist / dist;
        }
      }
      /* if soft-distance is enabled, start fading once owner is dist+softdist from the target */
      else if (data->flag & LIMITDIST_USESOFT) {
        if (dist <= (data->dist + data->soft)) {
          /* pass */
        }
      }
    }
    else if (data->mode == LIMITDIST_INSIDE) {
      /* if outside, then move to surface */
      if (dist >= data->dist) {
        clamp_surf = 1;
        if (dist != 0.0f) {
          sfac = data->dist / dist;
        }
      }
      /* if soft-distance is enabled, start fading once owner is dist-soft from the target */
      else if (data->flag & LIMITDIST_USESOFT) {
        /* FIXME: there's a problem with "jumping" when this kicks in */
        if (dist >= (data->dist - data->soft)) {
          sfac = (float)(data->soft * (1.0f - expf(-(dist - data->dist) / data->soft)) +
                         data->dist);
          if (dist != 0.0f) {
            sfac /= dist;
          }

          clamp_surf = 1;
        }
      }
    }
    else {
      if (IS_EQF(dist, data->dist) == 0) {
        clamp_surf = 1;
        if (dist != 0.0f) {
          sfac = data->dist / dist;
        }
      }
    }

    /* clamp to 'surface' (i.e. move owner so that dist == data->dist) */
    if (clamp_surf) {
      /* simply interpolate along line formed by target -> owner */
      interp_v3_v3v3(dvec, ct->matrix[3], cob->matrix[3], sfac);

      /* copy new vector onto owner */
      copy_v3_v3(cob->matrix[3], dvec);
    }
  }
}

static bConstraintTypeInfo CTI_DISTLIMIT = {
    CONSTRAINT_TYPE_DISTLIMIT,    /* type */
    sizeof(bDistLimitConstraint), /* size */
    "Limit Distance",             /* name */
    "bDistLimitConstraint",       /* struct name */
    NULL,                         /* free data */
    distlimit_id_looper,          /* id looper */
    NULL,                         /* copy data */
    distlimit_new_data,           /* new data */
    distlimit_get_tars,           /* get constraint targets */
    distlimit_flush_tars,         /* flush constraint targets */
    default_get_tarmat,           /* get a target matrix */
    distlimit_evaluate,           /* evaluate */
};

/* ---------- Stretch To ------------ */

static void stretchto_new_data(void *cdata)
{
  bStretchToConstraint *data = (bStretchToConstraint *)cdata;

  data->volmode = 0;
  data->plane = SWING_Y;
  data->orglength = 0.0;
  data->bulge = 1.0;
  data->bulge_max = 1.0f;
  data->bulge_min = 1.0f;
}

static void stretchto_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bStretchToConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int stretchto_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bStretchToConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void stretchto_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bStretchToConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

static void stretchto_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bStretchToConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float size[3], scale[3], vec[3], xx[3], zz[3], orth[3];
    float dist, bulge;

    /* Remove shear if using the Damped Track mode; the other modes
     * do it as a side effect, which is relied on by rigs. */
    if (data->plane == SWING_Y) {
      orthogonalize_m4_stable(cob->matrix, 1, false);
    }

    /* store scaling before destroying obmat */
    normalize_m4_ex(cob->matrix, size);

    /* store X orientation before destroying obmat */
    copy_v3_v3(xx, cob->matrix[0]);

    /* store Z orientation before destroying obmat */
    copy_v3_v3(zz, cob->matrix[2]);

    /* Compute distance and direction to target. */
    sub_v3_v3v3(vec, ct->matrix[3], cob->matrix[3]);

    dist = normalize_v3(vec);

    /* Only Y constrained object axis scale should be used, to keep same length when scaling it. */
    dist /= size[1];

    /* data->orglength==0 occurs on first run, and after 'R' button is clicked */
    if (data->orglength == 0) {
      data->orglength = dist;

      /* Write the computed length back to the master copy if in COW evaluation. */
      bConstraint *orig_con = constraint_find_original_for_update(cob, con);

      if (orig_con != NULL) {
        bStretchToConstraint *orig_data = orig_con->data;

        orig_data->orglength = data->orglength;
      }
    }

    scale[1] = dist / data->orglength;

    bulge = powf(data->orglength / dist, data->bulge);

    if (bulge > 1.0f) {
      if (data->flag & STRETCHTOCON_USE_BULGE_MAX) {
        float bulge_max = max_ff(data->bulge_max, 1.0f);
        float hard = min_ff(bulge, bulge_max);

        float range = bulge_max - 1.0f;
        float scale_fac = (range > 0.0f) ? 1.0f / range : 0.0f;
        float soft = 1.0f + range * atanf((bulge - 1.0f) * scale_fac) / (float)M_PI_2;

        bulge = interpf(soft, hard, data->bulge_smooth);
      }
    }
    if (bulge < 1.0f) {
      if (data->flag & STRETCHTOCON_USE_BULGE_MIN) {
        float bulge_min = CLAMPIS(data->bulge_min, 0.0f, 1.0f);
        float hard = max_ff(bulge, bulge_min);

        float range = 1.0f - bulge_min;
        float scale_fac = (range > 0.0f) ? 1.0f / range : 0.0f;
        float soft = 1.0f - range * atanf((1.0f - bulge) * scale_fac) / (float)M_PI_2;

        bulge = interpf(soft, hard, data->bulge_smooth);
      }
    }

    switch (data->volmode) {
      /* volume preserving scaling */
      case VOLUME_XZ:
        scale[0] = sqrtf(bulge);
        scale[2] = scale[0];
        break;
      case VOLUME_X:
        scale[0] = bulge;
        scale[2] = 1.0;
        break;
      case VOLUME_Z:
        scale[0] = 1.0;
        scale[2] = bulge;
        break;
      /* don't care for volume */
      case NO_VOLUME:
        scale[0] = 1.0;
        scale[2] = 1.0;
        break;
      default: /* Should not happen, but in case. */
        return;
    } /* switch (data->volmode) */

    /* Compute final scale. */
    mul_v3_v3(size, scale);

    switch (data->plane) {
      case SWING_Y:
        /* Point the Y axis using Damped Track math. */
        damptrack_do_transform(cob->matrix, vec, TRACK_Y);
        break;
      case PLANE_X:
        /* New Y aligns  object target connection. */
        copy_v3_v3(cob->matrix[1], vec);

        /* Build new Z vector. */
        /* Orthogonal to "new Y" "old X! plane. */
        cross_v3_v3v3(orth, xx, vec);
        normalize_v3(orth);

        /* New Z. */
        copy_v3_v3(cob->matrix[2], orth);

        /* We decided to keep X plane. */
        cross_v3_v3v3(xx, vec, orth);
        normalize_v3_v3(cob->matrix[0], xx);
        break;
      case PLANE_Z:
        /* New Y aligns  object target connection. */
        copy_v3_v3(cob->matrix[1], vec);

        /* Build new X vector. */
        /* Orthogonal to "new Y" "old Z! plane. */
        cross_v3_v3v3(orth, zz, vec);
        normalize_v3(orth);

        /* New X. */
        negate_v3_v3(cob->matrix[0], orth);

        /* We decided to keep Z. */
        cross_v3_v3v3(zz, vec, orth);
        normalize_v3_v3(cob->matrix[2], zz);
        break;
    } /* switch (data->plane) */

    rescale_m4(cob->matrix, size);
  }
}

static bConstraintTypeInfo CTI_STRETCHTO = {
    CONSTRAINT_TYPE_STRETCHTO,    /* type */
    sizeof(bStretchToConstraint), /* size */
    "Stretch To",                 /* name */
    "bStretchToConstraint",       /* struct name */
    NULL,                         /* free data */
    stretchto_id_looper,          /* id looper */
    NULL,                         /* copy data */
    stretchto_new_data,           /* new data */
    stretchto_get_tars,           /* get constraint targets */
    stretchto_flush_tars,         /* flush constraint targets */
    default_get_tarmat,           /* get target matrix */
    stretchto_evaluate,           /* evaluate */
};

/* ---------- Floor ------------ */

static void minmax_new_data(void *cdata)
{
  bMinMaxConstraint *data = (bMinMaxConstraint *)cdata;

  data->minmaxflag = TRACK_Z;
  data->offset = 0.0f;
  data->flag = 0;
}

static void minmax_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bMinMaxConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int minmax_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bMinMaxConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void minmax_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bMinMaxConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void minmax_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bMinMaxConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float obmat[4][4], imat[4][4], tarmat[4][4], tmat[4][4];
    float val1, val2;
    int index;

    copy_m4_m4(obmat, cob->matrix);
    copy_m4_m4(tarmat, ct->matrix);

    if (data->flag & MINMAX_USEROT) {
      /* Take rotation of target into account by doing the transaction in target's local-space. */
      invert_m4_m4(imat, tarmat);
      mul_m4_m4m4(tmat, imat, obmat);
      copy_m4_m4(obmat, tmat);
      unit_m4(tarmat);
    }

    switch (data->minmaxflag) {
      case TRACK_Z:
        val1 = tarmat[3][2];
        val2 = obmat[3][2] - data->offset;
        index = 2;
        break;
      case TRACK_Y:
        val1 = tarmat[3][1];
        val2 = obmat[3][1] - data->offset;
        index = 1;
        break;
      case TRACK_X:
        val1 = tarmat[3][0];
        val2 = obmat[3][0] - data->offset;
        index = 0;
        break;
      case TRACK_nZ:
        val2 = tarmat[3][2];
        val1 = obmat[3][2] - data->offset;
        index = 2;
        break;
      case TRACK_nY:
        val2 = tarmat[3][1];
        val1 = obmat[3][1] - data->offset;
        index = 1;
        break;
      case TRACK_nX:
        val2 = tarmat[3][0];
        val1 = obmat[3][0] - data->offset;
        index = 0;
        break;
      default:
        return;
    }

    if (val1 > val2) {
      obmat[3][index] = tarmat[3][index] + data->offset;
      if (data->flag & MINMAX_USEROT) {
        /* Get out of local-space. */
        mul_m4_m4m4(tmat, ct->matrix, obmat);
        copy_m4_m4(cob->matrix, tmat);
      }
      else {
        copy_v3_v3(cob->matrix[3], obmat[3]);
      }
    }
  }
}

static bConstraintTypeInfo CTI_MINMAX = {
    CONSTRAINT_TYPE_MINMAX,    /* type */
    sizeof(bMinMaxConstraint), /* size */
    "Floor",                   /* name */
    "bMinMaxConstraint",       /* struct name */
    NULL,                      /* free data */
    minmax_id_looper,          /* id looper */
    NULL,                      /* copy data */
    minmax_new_data,           /* new data */
    minmax_get_tars,           /* get constraint targets */
    minmax_flush_tars,         /* flush constraint targets */
    default_get_tarmat,        /* get target matrix */
    minmax_evaluate,           /* evaluate */
};

/* -------- Clamp To ---------- */

static void clampto_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bClampToConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int clampto_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bClampToConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints without subtargets */
    SINGLETARGETNS_GET_TARS(con, data->tar, ct, list);

    return 1;
  }

  return 0;
}

static void clampto_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bClampToConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGETNS_FLUSH_TARS(con, data->tar, ct, list, no_copy);
  }
}

static void clampto_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                               bConstraint *UNUSED(con),
                               bConstraintOb *UNUSED(cob),
                               bConstraintTarget *ct,
                               float UNUSED(ctime))
{
  /* technically, this isn't really needed for evaluation, but we don't know what else
   * might end up calling this...
   */
  if (ct) {
    unit_m4(ct->matrix);
  }
}

static void clampto_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bClampToConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target and it is a curve */
  if (VALID_CONS_TARGET(ct) && (ct->tar->type == OB_CURVES_LEGACY)) {
    float obmat[4][4], ownLoc[3];
    float curveMin[3], curveMax[3];
    float targetMatrix[4][4];

    copy_m4_m4(obmat, cob->matrix);
    copy_v3_v3(ownLoc, obmat[3]);

    unit_m4(targetMatrix);
    INIT_MINMAX(curveMin, curveMax);
    /* XXX(@campbellbarton): don't think this is good calling this here because
     * the other object's data is lazily initializing bounding-box information.
     * This could cause issues when evaluating from a thread.
     * If the depsgraph ensures the bound-box is always available, a code-path could
     * be used that doesn't lazy initialize to avoid thread safety issues in the future. */
    BKE_object_minmax(ct->tar, curveMin, curveMax, true);

    /* get targetmatrix */
    if (data->tar->runtime.curve_cache && data->tar->runtime.curve_cache->anim_path_accum_length) {
      float vec[4], dir[3], totmat[4][4];
      float curvetime;
      short clamp_axis;

      /* find best position on curve */
      /* 1. determine which axis to sample on? */
      if (data->flag == CLAMPTO_AUTO) {
        float size[3];
        sub_v3_v3v3(size, curveMax, curveMin);

        /* find axis along which the bounding box has the greatest
         * extent. Otherwise, default to the x-axis, as that is quite
         * frequently used.
         */
        if ((size[2] > size[0]) && (size[2] > size[1])) {
          clamp_axis = CLAMPTO_Z - 1;
        }
        else if ((size[1] > size[0]) && (size[1] > size[2])) {
          clamp_axis = CLAMPTO_Y - 1;
        }
        else {
          clamp_axis = CLAMPTO_X - 1;
        }
      }
      else {
        clamp_axis = data->flag - 1;
      }

      /* 2. determine position relative to curve on a 0-1 scale based on bounding box */
      if (data->flag2 & CLAMPTO_CYCLIC) {
        /* cyclic, so offset within relative bounding box is used */
        float len = (curveMax[clamp_axis] - curveMin[clamp_axis]);
        float offset;

        /* check to make sure len is not so close to zero that it'll cause errors */
        if (IS_EQF(len, 0.0f) == false) {
          /* find bounding-box range where target is located */
          if (ownLoc[clamp_axis] < curveMin[clamp_axis]) {
            /* bounding-box range is before */
            offset = curveMin[clamp_axis] -
                     ceilf((curveMin[clamp_axis] - ownLoc[clamp_axis]) / len) * len;

            /* Now, we calculate as per normal,
             * except using offset instead of curveMin[clamp_axis]. */
            curvetime = (ownLoc[clamp_axis] - offset) / (len);
          }
          else if (ownLoc[clamp_axis] > curveMax[clamp_axis]) {
            /* bounding-box range is after */
            offset = curveMax[clamp_axis] +
                     (int)((ownLoc[clamp_axis] - curveMax[clamp_axis]) / len) * len;

            /* Now, we calculate as per normal,
             * except using offset instead of curveMax[clamp_axis]. */
            curvetime = (ownLoc[clamp_axis] - offset) / (len);
          }
          else {
            /* as the location falls within bounds, just calculate */
            curvetime = (ownLoc[clamp_axis] - curveMin[clamp_axis]) / (len);
          }
        }
        else {
          /* as length is close to zero, curvetime by default should be 0 (i.e. the start) */
          curvetime = 0.0f;
        }
      }
      else {
        /* no cyclic, so position is clamped to within the bounding box */
        if (ownLoc[clamp_axis] <= curveMin[clamp_axis]) {
          curvetime = 0.0f;
        }
        else if (ownLoc[clamp_axis] >= curveMax[clamp_axis]) {
          curvetime = 1.0f;
        }
        else if (IS_EQF((curveMax[clamp_axis] - curveMin[clamp_axis]), 0.0f) == false) {
          curvetime = (ownLoc[clamp_axis] - curveMin[clamp_axis]) /
                      (curveMax[clamp_axis] - curveMin[clamp_axis]);
        }
        else {
          curvetime = 0.0f;
        }
      }

      /* 3. position on curve */
      if (BKE_where_on_path(ct->tar, curvetime, vec, dir, NULL, NULL, NULL)) {
        unit_m4(totmat);
        copy_v3_v3(totmat[3], vec);

        mul_m4_m4m4(targetMatrix, ct->tar->obmat, totmat);
      }
    }

    /* obtain final object position */
    copy_v3_v3(cob->matrix[3], targetMatrix[3]);
  }
}

static bConstraintTypeInfo CTI_CLAMPTO = {
    CONSTRAINT_TYPE_CLAMPTO,    /* type */
    sizeof(bClampToConstraint), /* size */
    "Clamp To",                 /* name */
    "bClampToConstraint",       /* struct name */
    NULL,                       /* free data */
    clampto_id_looper,          /* id looper */
    NULL,                       /* copy data */
    NULL,                       /* new data */
    clampto_get_tars,           /* get constraint targets */
    clampto_flush_tars,         /* flush constraint targets */
    clampto_get_tarmat,         /* get target matrix */
    clampto_evaluate,           /* evaluate */
};

/* ---------- Transform Constraint ----------- */

static void transform_new_data(void *cdata)
{
  bTransformConstraint *data = (bTransformConstraint *)cdata;

  data->map[0] = 0;
  data->map[1] = 1;
  data->map[2] = 2;

  for (int i = 0; i < 3; i++) {
    data->from_min_scale[i] = data->from_max_scale[i] = 1.0f;
    data->to_min_scale[i] = data->to_max_scale[i] = 1.0f;
  }
}

static void transform_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bTransformConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);

  custom_space_id_looper(con, func, userdata);
}

static int transform_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bTransformConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1 + get_space_tar(con, list);
  }

  return 0;
}

static void transform_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bTransformConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
    flush_space_tar(con, list, no_copy);
  }
}

static void transform_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bTransformConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    float *from_min, *from_max, *to_min, *to_max;
    float loc[3], rot[3][3], oldeul[3], size[3];
    float newloc[3], newrot[3][3], neweul[3], newsize[3];
    float dbuf[4], sval[3];
    float *const dvec = dbuf + 1;

    /* obtain target effect */
    switch (data->from) {
      case TRANS_SCALE:
        mat4_to_size(dvec, ct->matrix);

        if (is_negative_m4(ct->matrix)) {
          /* Bugfix T27886: (this is a limitation that riggers will have to live with for now).
           * We can't be sure which axis/axes are negative,
           * though we know that something is negative.
           * Assume we don't care about negativity of separate axes. */
          negate_v3(dvec);
        }
        from_min = data->from_min_scale;
        from_max = data->from_max_scale;
        break;
      case TRANS_ROTATION:
        BKE_driver_target_matrix_to_rot_channels(
            ct->matrix, cob->rotOrder, data->from_rotation_mode, -1, true, dbuf);
        from_min = data->from_min_rot;
        from_max = data->from_max_rot;
        break;
      case TRANS_LOCATION:
      default:
        copy_v3_v3(dvec, ct->matrix[3]);
        from_min = data->from_min;
        from_max = data->from_max;
        break;
    }

    /* Select the output Euler rotation order, defaulting to the owner. */
    short rot_order = cob->rotOrder;

    if (data->to == TRANS_ROTATION && data->to_euler_order != CONSTRAINT_EULER_AUTO) {
      rot_order = data->to_euler_order;
    }

    /* extract components of owner's matrix */
    mat4_to_loc_rot_size(loc, rot, size, cob->matrix);

    /* determine where in range current transforms lie */
    if (data->expo) {
      for (int i = 0; i < 3; i++) {
        if (from_max[i] - from_min[i]) {
          sval[i] = (dvec[i] - from_min[i]) / (from_max[i] - from_min[i]);
        }
        else {
          sval[i] = 0.0f;
        }
      }
    }
    else {
      /* clamp transforms out of range */
      for (int i = 0; i < 3; i++) {
        CLAMP(dvec[i], from_min[i], from_max[i]);
        if (from_max[i] - from_min[i]) {
          sval[i] = (dvec[i] - from_min[i]) / (from_max[i] - from_min[i]);
        }
        else {
          sval[i] = 0.0f;
        }
      }
    }

    /* apply transforms */
    switch (data->to) {
      case TRANS_SCALE:
        to_min = data->to_min_scale;
        to_max = data->to_max_scale;
        for (int i = 0; i < 3; i++) {
          newsize[i] = to_min[i] + (sval[(int)data->map[i]] * (to_max[i] - to_min[i]));
        }
        switch (data->mix_mode_scale) {
          case TRANS_MIXSCALE_MULTIPLY:
            mul_v3_v3(size, newsize);
            break;
          case TRANS_MIXSCALE_REPLACE:
          default:
            copy_v3_v3(size, newsize);
            break;
        }
        break;
      case TRANS_ROTATION:
        to_min = data->to_min_rot;
        to_max = data->to_max_rot;
        for (int i = 0; i < 3; i++) {
          neweul[i] = to_min[i] + (sval[(int)data->map[i]] * (to_max[i] - to_min[i]));
        }
        switch (data->mix_mode_rot) {
          case TRANS_MIXROT_REPLACE:
            eulO_to_mat3(rot, neweul, rot_order);
            break;
          case TRANS_MIXROT_BEFORE:
            eulO_to_mat3(newrot, neweul, rot_order);
            mul_m3_m3m3(rot, newrot, rot);
            break;
          case TRANS_MIXROT_AFTER:
            eulO_to_mat3(newrot, neweul, rot_order);
            mul_m3_m3m3(rot, rot, newrot);
            break;
          case TRANS_MIXROT_ADD:
          default:
            mat3_to_eulO(oldeul, rot_order, rot);
            add_v3_v3(neweul, oldeul);
            eulO_to_mat3(rot, neweul, rot_order);
            break;
        }
        break;
      case TRANS_LOCATION:
      default:
        to_min = data->to_min;
        to_max = data->to_max;
        for (int i = 0; i < 3; i++) {
          newloc[i] = (to_min[i] + (sval[(int)data->map[i]] * (to_max[i] - to_min[i])));
        }
        switch (data->mix_mode_loc) {
          case TRANS_MIXLOC_REPLACE:
            copy_v3_v3(loc, newloc);
            break;
          case TRANS_MIXLOC_ADD:
          default:
            add_v3_v3(loc, newloc);
            break;
        }
        break;
    }

    /* apply to matrix */
    loc_rot_size_to_mat4(cob->matrix, loc, rot, size);
  }
}

static bConstraintTypeInfo CTI_TRANSFORM = {
    CONSTRAINT_TYPE_TRANSFORM,    /* type */
    sizeof(bTransformConstraint), /* size */
    "Transformation",             /* name */
    "bTransformConstraint",       /* struct name */
    NULL,                         /* free data */
    transform_id_looper,          /* id looper */
    NULL,                         /* copy data */
    transform_new_data,           /* new data */
    transform_get_tars,           /* get constraint targets */
    transform_flush_tars,         /* flush constraint targets */
    default_get_tarmat,           /* get a target matrix */
    transform_evaluate,           /* evaluate */
};

/* ---------- Shrinkwrap Constraint ----------- */

static void shrinkwrap_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bShrinkwrapConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->target, false, userdata);
}

static void shrinkwrap_new_data(void *cdata)
{
  bShrinkwrapConstraint *data = (bShrinkwrapConstraint *)cdata;

  data->projAxis = OB_POSZ;
  data->projAxisSpace = CONSTRAINT_SPACE_LOCAL;
}

static int shrinkwrap_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bShrinkwrapConstraint *data = con->data;
    bConstraintTarget *ct;

    SINGLETARGETNS_GET_TARS(con, data->target, ct, list);

    return 1;
  }

  return 0;
}

static void shrinkwrap_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bShrinkwrapConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    SINGLETARGETNS_FLUSH_TARS(con, data->target, ct, list, no_copy);
  }
}

static void shrinkwrap_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                  bConstraint *con,
                                  bConstraintOb *cob,
                                  bConstraintTarget *ct,
                                  float UNUSED(ctime))
{
  bShrinkwrapConstraint *scon = (bShrinkwrapConstraint *)con->data;

  if (VALID_CONS_TARGET(ct) && (ct->tar->type == OB_MESH)) {

    bool fail = false;
    float co[3] = {0.0f, 0.0f, 0.0f};
    bool track_normal = false;
    float track_no[3] = {0.0f, 0.0f, 0.0f};

    SpaceTransform transform;
    Mesh *target_eval = BKE_object_get_evaluated_mesh(ct->tar);

    copy_m4_m4(ct->matrix, cob->matrix);

    bool do_track_normal = (scon->flag & CON_SHRINKWRAP_TRACK_NORMAL) != 0;
    ShrinkwrapTreeData tree;

    if (BKE_shrinkwrap_init_tree(
            &tree, target_eval, scon->shrinkType, scon->shrinkMode, do_track_normal)) {
      BLI_space_transform_from_matrices(&transform, cob->matrix, ct->tar->obmat);

      switch (scon->shrinkType) {
        case MOD_SHRINKWRAP_NEAREST_SURFACE:
        case MOD_SHRINKWRAP_NEAREST_VERTEX:
        case MOD_SHRINKWRAP_TARGET_PROJECT: {
          BVHTreeNearest nearest;

          nearest.index = -1;
          nearest.dist_sq = FLT_MAX;

          BLI_space_transform_apply(&transform, co);

          BKE_shrinkwrap_find_nearest_surface(&tree, &nearest, co, scon->shrinkType);

          if (nearest.index < 0) {
            fail = true;
            break;
          }

          if (scon->shrinkType != MOD_SHRINKWRAP_NEAREST_VERTEX) {
            if (do_track_normal) {
              track_normal = true;
              BKE_shrinkwrap_compute_smooth_normal(
                  &tree, NULL, nearest.index, nearest.co, nearest.no, track_no);
              BLI_space_transform_invert_normal(&transform, track_no);
            }

            BKE_shrinkwrap_snap_point_to_surface(&tree,
                                                 NULL,
                                                 scon->shrinkMode,
                                                 nearest.index,
                                                 nearest.co,
                                                 nearest.no,
                                                 scon->dist,
                                                 co,
                                                 co);
          }
          else {
            const float dist = len_v3v3(co, nearest.co);

            if (dist != 0.0f) {
              interp_v3_v3v3(
                  co, co, nearest.co, (dist - scon->dist) / dist); /* linear interpolation */
            }
          }

          BLI_space_transform_invert(&transform, co);
          break;
        }
        case MOD_SHRINKWRAP_PROJECT: {
          BVHTreeRayHit hit;

          float mat[4][4];
          float no[3] = {0.0f, 0.0f, 0.0f};

          /* TODO: should use FLT_MAX.. but normal projection doesn't yet supports it. */
          hit.index = -1;
          hit.dist = (scon->projLimit == 0.0f) ? BVH_RAYCAST_DIST_MAX : scon->projLimit;

          switch (scon->projAxis) {
            case OB_POSX:
            case OB_POSY:
            case OB_POSZ:
              no[scon->projAxis - OB_POSX] = 1.0f;
              break;
            case OB_NEGX:
            case OB_NEGY:
            case OB_NEGZ:
              no[scon->projAxis - OB_NEGX] = -1.0f;
              break;
          }

          /* Transform normal into requested space */
          /* Note that in this specific case, we need to keep scaling in non-parented 'local2world'
           * object case, because SpaceTransform also takes it into account when handling normals.
           * See T42447. */
          unit_m4(mat);
          BKE_constraint_mat_convertspace(
              cob->ob, cob->pchan, cob, mat, CONSTRAINT_SPACE_LOCAL, scon->projAxisSpace, true);
          invert_m4(mat);
          mul_mat3_m4_v3(mat, no);

          if (normalize_v3(no) < FLT_EPSILON) {
            fail = true;
            break;
          }

          char cull_mode = scon->flag & CON_SHRINKWRAP_PROJECT_CULL_MASK;

          BKE_shrinkwrap_project_normal(cull_mode, co, no, 0.0f, &transform, &tree, &hit);

          if (scon->flag & CON_SHRINKWRAP_PROJECT_OPPOSITE) {
            float inv_no[3];
            negate_v3_v3(inv_no, no);

            if ((scon->flag & CON_SHRINKWRAP_PROJECT_INVERT_CULL) && (cull_mode != 0)) {
              cull_mode ^= CON_SHRINKWRAP_PROJECT_CULL_MASK;
            }

            BKE_shrinkwrap_project_normal(cull_mode, co, inv_no, 0.0f, &transform, &tree, &hit);
          }

          if (hit.index < 0) {
            fail = true;
            break;
          }

          if (do_track_normal) {
            track_normal = true;
            BKE_shrinkwrap_compute_smooth_normal(
                &tree, &transform, hit.index, hit.co, hit.no, track_no);
          }

          BKE_shrinkwrap_snap_point_to_surface(
              &tree, &transform, scon->shrinkMode, hit.index, hit.co, hit.no, scon->dist, co, co);
          break;
        }
      }

      BKE_shrinkwrap_free_tree(&tree);

      if (fail == true) {
        /* Don't move the point */
        zero_v3(co);
      }

      /* co is in local object coordinates, change it to global and update target position */
      mul_m4_v3(cob->matrix, co);
      copy_v3_v3(ct->matrix[3], co);

      if (track_normal) {
        mul_mat3_m4_v3(cob->matrix, track_no);
        damptrack_do_transform(ct->matrix, track_no, scon->trackAxis);
      }
    }
  }
}

static void shrinkwrap_evaluate(bConstraint *UNUSED(con), bConstraintOb *cob, ListBase *targets)
{
  bConstraintTarget *ct = targets->first;

  /* only evaluate if there is a target */
  if (VALID_CONS_TARGET(ct)) {
    copy_m4_m4(cob->matrix, ct->matrix);
  }
}

static bConstraintTypeInfo CTI_SHRINKWRAP = {
    CONSTRAINT_TYPE_SHRINKWRAP,    /* type */
    sizeof(bShrinkwrapConstraint), /* size */
    "Shrinkwrap",                  /* name */
    "bShrinkwrapConstraint",       /* struct name */
    NULL,                          /* free data */
    shrinkwrap_id_looper,          /* id looper */
    NULL,                          /* copy data */
    shrinkwrap_new_data,           /* new data */
    shrinkwrap_get_tars,           /* get constraint targets */
    shrinkwrap_flush_tars,         /* flush constraint targets */
    shrinkwrap_get_tarmat,         /* get a target matrix */
    shrinkwrap_evaluate,           /* evaluate */
};

/* --------- Damped Track ---------- */

static void damptrack_new_data(void *cdata)
{
  bDampTrackConstraint *data = (bDampTrackConstraint *)cdata;

  data->trackflag = TRACK_Y;
}

static void damptrack_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bDampTrackConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int damptrack_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bDampTrackConstraint *data = con->data;
    bConstraintTarget *ct;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void damptrack_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bDampTrackConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}

/* array of direction vectors for the tracking flags */
static const float track_dir_vecs[6][3] = {
    {+1, 0, 0},
    {0, +1, 0},
    {0, 0, +1}, /* TRACK_X,  TRACK_Y,  TRACK_Z */
    {-1, 0, 0},
    {0, -1, 0},
    {0, 0, -1} /* TRACK_NX, TRACK_NY, TRACK_NZ */
};

static void damptrack_evaluate(bConstraint *con, bConstraintOb *cob, ListBase *targets)
{
  bDampTrackConstraint *data = con->data;
  bConstraintTarget *ct = targets->first;

  if (VALID_CONS_TARGET(ct)) {
    float tarvec[3];

    /* find the (unit) direction vector going from the owner to the target */
    sub_v3_v3v3(tarvec, ct->matrix[3], cob->matrix[3]);

    damptrack_do_transform(cob->matrix, tarvec, data->trackflag);
  }
}

static void damptrack_do_transform(float matrix[4][4], const float tarvec_in[3], int track_axis)
{
  /* find the (unit) direction vector going from the owner to the target */
  float tarvec[3];

  if (normalize_v3_v3(tarvec, tarvec_in) != 0.0f) {
    float obvec[3], obloc[3];
    float raxis[3], rangle;
    float rmat[3][3], tmat[4][4];

    /* find the (unit) direction that the axis we're interested in currently points
     * - mul_mat3_m4_v3() only takes the 3x3 (rotation+scaling) components of the 4x4 matrix
     * - the normalization step at the end should take care of any unwanted scaling
     *   left over in the 3x3 matrix we used
     */
    copy_v3_v3(obvec, track_dir_vecs[track_axis]);
    mul_mat3_m4_v3(matrix, obvec);

    if (normalize_v3(obvec) == 0.0f) {
      /* exceptional case - just use the track vector as appropriate */
      copy_v3_v3(obvec, track_dir_vecs[track_axis]);
    }

    copy_v3_v3(obloc, matrix[3]);

    /* determine the axis-angle rotation, which represents the smallest possible rotation
     * between the two rotation vectors (i.e. the 'damping' referred to in the name)
     * - we take this to be the rotation around the normal axis/vector to the plane defined
     *   by the current and destination vectors, which will 'map' the current axis to the
     *   destination vector
     * - the min/max wrappers around (obvec . tarvec) result (stored temporarily in rangle)
     *   are used to ensure that the smallest angle is chosen
     */
    cross_v3_v3v3_hi_prec(raxis, obvec, tarvec);

    rangle = dot_v3v3(obvec, tarvec);
    rangle = acosf(max_ff(-1.0f, min_ff(1.0f, rangle)));

    /* construct rotation matrix from the axis-angle rotation found above
     * - this call takes care to make sure that the axis provided is a unit vector first
     */
    float norm = normalize_v3(raxis);

    if (norm < FLT_EPSILON) {
      /* if dot product is nonzero, while cross is zero, we have two opposite vectors!
       * - this is an ambiguity in the math that needs to be resolved arbitrarily,
       *   or there will be a case where damped track strangely does nothing
       * - to do that, rotate around a different local axis
       */
      float tmpvec[3];

      if (fabsf(rangle) < M_PI - 0.01f) {
        return;
      }

      rangle = M_PI;
      copy_v3_v3(tmpvec, track_dir_vecs[(track_axis + 1) % 6]);
      mul_mat3_m4_v3(matrix, tmpvec);
      cross_v3_v3v3(raxis, obvec, tmpvec);

      if (normalize_v3(raxis) == 0.0f) {
        return;
      }
    }
    else if (norm < 0.1f) {
      /* near 0 and Pi arcsin has way better precision than arccos */
      rangle = (rangle > M_PI_2) ? M_PI - asinf(norm) : asinf(norm);
    }

    axis_angle_normalized_to_mat3(rmat, raxis, rangle);

    /* rotate the owner in the way defined by this rotation matrix, then reapply the location since
     * we may have destroyed that in the process of multiplying the matrix
     */
    unit_m4(tmat);
    mul_m4_m3m4(tmat, rmat, matrix); /* m1, m3, m2 */

    copy_m4_m4(matrix, tmat);
    copy_v3_v3(matrix[3], obloc);
  }
}

static bConstraintTypeInfo CTI_DAMPTRACK = {
    CONSTRAINT_TYPE_DAMPTRACK,    /* type */
    sizeof(bDampTrackConstraint), /* size */
    "Damped Track",               /* name */
    "bDampTrackConstraint",       /* struct name */
    NULL,                         /* free data */
    damptrack_id_looper,          /* id looper */
    NULL,                         /* copy data */
    damptrack_new_data,           /* new data */
    damptrack_get_tars,           /* get constraint targets */
    damptrack_flush_tars,         /* flush constraint targets */
    default_get_tarmat,           /* get target matrix */
    damptrack_evaluate,           /* evaluate */
};

/* ----------- Spline IK ------------ */

static void splineik_free(bConstraint *con)
{
  bSplineIKConstraint *data = con->data;

  /* binding array */
  MEM_SAFE_FREE(data->points);
}

static void splineik_copy(bConstraint *con, bConstraint *srccon)
{
  bSplineIKConstraint *src = srccon->data;
  bSplineIKConstraint *dst = con->data;

  /* copy the binding array */
  dst->points = MEM_dupallocN(src->points);
}

static void splineik_new_data(void *cdata)
{
  bSplineIKConstraint *data = (bSplineIKConstraint *)cdata;

  data->chainlen = 1;
  data->bulge = 1.0;
  data->bulge_max = 1.0f;
  data->bulge_min = 1.0f;

  data->yScaleMode = CONSTRAINT_SPLINEIK_YS_FIT_CURVE;
  data->flag = CONSTRAINT_SPLINEIK_USE_ORIGINAL_SCALE;
}

static void splineik_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bSplineIKConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int splineik_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bSplineIKConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints without subtargets */
    SINGLETARGETNS_GET_TARS(con, data->tar, ct, list);

    return 1;
  }

  return 0;
}

static void splineik_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bSplineIKConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGETNS_FLUSH_TARS(con, data->tar, ct, list, no_copy);
  }
}

static void splineik_get_tarmat(struct Depsgraph *UNUSED(depsgraph),
                                bConstraint *UNUSED(con),
                                bConstraintOb *UNUSED(cob),
                                bConstraintTarget *ct,
                                float UNUSED(ctime))
{
  /* technically, this isn't really needed for evaluation, but we don't know what else
   * might end up calling this...
   */
  if (ct) {
    unit_m4(ct->matrix);
  }
}

static bConstraintTypeInfo CTI_SPLINEIK = {
    CONSTRAINT_TYPE_SPLINEIK,    /* type */
    sizeof(bSplineIKConstraint), /* size */
    "Spline IK",                 /* name */
    "bSplineIKConstraint",       /* struct name */
    splineik_free,               /* free data */
    splineik_id_looper,          /* id looper */
    splineik_copy,               /* copy data */
    splineik_new_data,           /* new data */
    splineik_get_tars,           /* get constraint targets */
    splineik_flush_tars,         /* flush constraint targets */
    splineik_get_tarmat,         /* get target matrix */
    NULL,                        /* evaluate - solved as separate loop */
};

/* ----------- Pivot ------------- */

static void pivotcon_id_looper(bConstraint *con, ConstraintIDFunc func, void *userdata)
{
  bPivotConstraint *data = con->data;

  /* target only */
  func(con, (ID **)&data->tar, false, userdata);
}

static int pivotcon_get_tars(bConstraint *con, ListBase *list)
{
  if (con && list) {
    bPivotConstraint *data = con->data;
    bConstraintTarget *ct;

    /* standard target-getting macro for single-target constraints */
    SINGLETARGET_GET_TARS(con, data->tar, data->subtarget, ct, list);

    return 1;
  }

  return 0;
}

static void pivotcon_flush_tars(bConstraint *con, ListBase *list, bool no_copy)
{
  if (con && list) {
    bPivotConstraint *data = con->data;
    bConstraintTarget *ct = list->first;

    /* the following macro is used for all standard single-target constraints */
    SINGLETARGET_FLUSH_TARS(con, data->tar, data->subtarget, ct, list, no_copy);
  }
}
