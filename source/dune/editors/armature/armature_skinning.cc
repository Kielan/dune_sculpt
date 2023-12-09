/* API's for creating vertex groups from bones
 * - Interfaces with heat weighting in meshlaplacian. */
#include "types_armature.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"
#include "lib_string_utils.hh"

#include "dune_action.h"
#include "dune_armature.hh"
#include "dune_deform.h"
#include "dune_mesh.hh"
#include "dune_mesh_iters.hh"
#include "dune_mesh_runtime.hh"
#include "dune_mod.hh"
#include "dune_ob.hh"
#include "dune_ob_deform.h"
#include "dune_report.h"
#include "dune_subsurf.hh"

#include "graph.hh"
#include "graph_query.hh"

#include "ed_armature.hh"
#include "ed_mesh.hh"

#include "anim_bone_collections.hh"

#include "armature_intern.h"
#include "meshlaplacian.h"

/* Bone Skinning */
static int bone_skinnable_cb(Ob * /*ob*/, Bone *bone, void *datap)
{
  /* Bones that are deforming
   * are regarded to be "skinnable" and are eligible for
   * auto-skinning.
   *
   * This fn performs 2 fns:
   *   a) It returns 1 if the bone is skinnable.
   *      If we loop over all bones with this
   *      fns, we can count the number of
   *      skinnable bones.
   *   b) If the ptr data is non null,
   *      it is treated like a handle to a
   *      bone ptr the bone ptr
   *      is set to point at this bone, and
   *      the ptr the handle points to
   *      is incremented to point to the
   *      next member of an array of ptrs
   *      to bones. This way we can loop using
   *      this fn to construct an array of
   *      ptrs to bones that point to all
   *      skinnable bones. */
  Bone ***hbone;
  int a, segments;
  struct Arg {
    Ob *armob;
    void *list;
    int heat;
    bool is_weight_paint;
  } *data = static_cast<Arg *>(datap);

  if (!(data->is_weight_paint) || !(bone->flag & BONE_HIDDEN_P)) {
    if (!(bone->flag & BONE_NO_DEFORM)) {
      if (data->heat && data->armob->pose &&
          dune_pose_channel_find_name(data->armob->pose, bone->name)) {
        segments = bone->segments;
      }
      else {
        segments = 1;
      }

      if (data->list != nullptr) {
        hbone = (Bone ***)&data->list;

        for (a = 0; a < segments; a++) {
          **hbone = bone;
          (*hbone)++;
        }
      }
      return segments;
    }
  }
  return 0;
}

static int vgroup_add_unique_bone_cb(Object *ob, Bone *bone, void * /*ptr*/)
{
  /* This group creates a vertex group to ob that has the
   * same name as bone (provided the bone is skinnable).
   * If such a vertex group alrdy exist the routine exits */
  if (!(bone->flag & BONE_NO_DEFORM)) {
    if (!dune_ob_defgroup_find_name(ob, bone->name)) {
      dune_ob_defgroup_add_name(ob, bone->name);
      return 1;
    }
  }
  return 0;
}

static int dgroup_skinnable_cb(Ob *ob, Bone *bone, void *datap)
{
  /* Bones that are deforming
   * are regarded to be "skinnable" and are eligible for
   * auto-skinning.
   * This fn performs 2 functions:
   *
   *   a) If the bone is skinnable, it creates
   *      a vertex group for ob that has
   *      the name of the skinnable bone
   *      (if one doesn't exist alrdy).
   *   b) If the ptr data is non null,
   *      it is treated like a handle to a
   *      DeformGroup ptr -- the
   *      DeformGroup ptr is set to point
   *      to the deform group with the bone's
   *      name, and the ptr the handle
   *      points to is incremented to point to the
   *      next member of an arr of ptrs
   *      to DeformGroups. This way we can loop using
   *      this fn to construct an array of
   *      ptrs to DeformGroups, all with names
   *      of skinnable bones. */
  DeformGroup ***hgroup, *defgroup = nullptr;
  int a, segments;
  struct Arg {
    Object *armob;
    void *list;
    int heat;
    bool is_weight_paint;
  } *data = static_cast<Arg *>(datap);
  Armature *arm = static_cast<Armature *>(data->armob->data);

  if (!data->is_weight_paint || !(bone->flag & BONE_HIDDEN_P)) {
    if (!(bone->flag & BONE_NO_DEFORM)) {
      if (data->heat && data->armob->pose &&
          dune_pose_channel_find_name(data->armob->pose, bone->name)) {
        segments = bone->segments;
      }
      else {
        segments = 1;
      }

      if (!data->is_weight_paint ||
          (anim_bonecoll_is_visible(arm, bone) && (bone->flag & BONE_SEL)))
      {
        if (!(defgroup = dune_ob_defgroup_find_name(ob, bone->name))) {
          defgroup = dune_ob_defgroup_add_name(ob, bone->name);
        }
        else if (defgroup->flag & DG_LOCK_WEIGHT) {
          /* In case vgroup alrdy exists and is locked, do not modify it here. See #43814. */
          defgroup = nullptr;
        }
      }

      if (data->list != nullptr) {
        hgroup = (DeformGroup ***)&data->list;

        for (a = 0; a < segments; a++) {
          **hgroup = defgroup;
          (*hgroup)++;
        }
      }
      return segments;
    }
  }
  return 0;
}

static void envelope_bone_weighting(Ob *ob,
                                    Mesh *mesh,
                                    float (*verts)[3],
                                    int numbones,
                                    Bone **bonelist,
                                    DeformGroup **dgrouplist,
                                    DeformGroup **dgroupflip,
                                    float (*root)[3],
                                    float (*tip)[3],
                                    const int *sel,
                                    float scale)
{
  /* Create vertex group weights from envelopes */

  bool use_topology = (mesh->editflag & ME_EDIT_MIRROR_TOPO) != 0;
  bool use_mask = false;

  if ((ob->mode & OB_MODE_WEIGHT_PAINT) &&
      (mesh->editflag & (ME_EDIT_PAINT_FACE_SEL | ME_EDIT_PAINT_VERT_SEL)))
  {
    use_mask = true;
  }

  const bool *sel_vert = (const bool *)CustomData_get_layer_named(
      &mesh->vert_data, CD_PROP_BOOL, ".sel_vert");

  /* foreach vert in the mesh */
  for (int i = 0; i < mesh->totvert; i++) {

    if (use_mask && !(sel_vert && sel_vert[i])) {
      continue;
    }

    int iflip = (dgroupflip) ? mesh_get_x_mirror_vert(ob, nullptr, i, use_topology) : -1;

    /* for each skinnable bone */
    for (int j = 0; j < numbones; j++) {
      if (!sel[j]) {
        continue;
      }

      Bone *bone = bonelist[j];
      DeformGroup *dgroup = dgrouplist[j];

      /* store the distance-factor from the vert to the bone */
      float distance = distfactor_to_bone(verts[i],
                                          root[j],
                                          tip[j],
                                          bone->rad_head * scale,
                                          bone->rad_tail * scale,
                                          bone->dist * scale);

      /* add the vert to the deform group if (weight != 0.0) */
      if (distance != 0.0f) {
        ed_vgroup_vert_add(ob, dgroup, i, distance, WEIGHT_REPLACE);
      }
      else {
        ed_vgroup_vert_remove(ob, dgroup, i);
      }

      /* do same for mirror */
      if (dgroupflip && dgroupflip[j] && iflip != -1) {
        if (distance != 0.0f) {
          ed_vgroup_vert_add(ob, dgroupflip[j], iflip, distance, WEIGHT_REPLACE);
        }
        else {
          ed_vgroup_vert_remove(ob, dgroupflip[j], iflip);
        }
      }
    }
  }
}

static void add_verts_to_dgroups(ReportList *reports,
                                 Graph *graph,
                                 Scene * /*scene*/,
                                 Ob *ob,
                                 Ob *par,
                                 int heat,
                                 const bool mirror)
{
  /* This fns implements the automatic computation of vertex group
   * weights, either through envelopes or using a heat equilibrium.
   *
   * This fn can be called both when parenting a mesh to an armature,
   * or in weight-paint + pose-mode. In the latter case sel is taken
   * into account and vertex weights can be mirrored.
   *
   * The mesh vertex positions used are either the final deformed coords
   * from the eval mesh in weight-paint mode, the final sub-surface coords
   * when parenting, or simply the original mesh coords. */

  Armature *arm = static_cast<Armature *>(par->data);
  Bone **bonelist, *bone;
  DeformGroup **dgrouplist, **dgroupflip;
  DeformGroup *dgroup;
  PoseChannel *pchan;
  Mesh *mesh;
  Mat4 bbone_array[MAX_BBONE_SUBDIV], *bbone = nullptr;
  float(*root)[3], (*tip)[3], (*verts)[3];
  int *sel;
  int numbones, vertsfilled = 0, segments = 0;
  const bool wpmode = (ob->mode & OB_MODE_WEIGHT_PAINT);
  struct {
    Ob *armob;
    void *list;
    int heat;
    bool is_weight_paint;
  } looper_data;

  looper_data.armob = par;
  looper_data.heat = heat;
  looper_data.list = nullptr;
  looper_data.is_weight_paint = wpmode;

  /* count the number of skinnable bones */
  numbones = bone_looper(
      ob, static_cast<Bone *>(arm->bonebase.first), &looper_data, bone_skinnable_cb);

  if (numbones == 0) {
    return;
  }

  if (dune_ob_defgroup_data_create(static_cast<Id *>(ob->data)) == nullptr) {
    return;
  }

  /* create an array of ptr to bones that are skinnable
   * and fill it with all of the skinnable bones */
  bonelist = static_cast<Bone **>(mem_calloc(numbones * sizeof(Bone *), "bonelist"));
  looper_data.list = bonelist;
  bone_looper(ob, static_cast<Bone *>(arm->bonebase.first), &looper_data, bone_skinnable_cb);

  /* create an array of ptrs to the deform groups that
   * correspond to the skinnable bones (creating them
   * as necessary. */
  dgrouplist = static_cast<DeformGroup **>(
      mem_calloc(numbones * sizeof(DeformGroup *), "dgrouplist"));
  dgroupflip = static_cast<DeformGroup **>(
      mem_calloc(numbones * sizeof(DeformGroup *), "dgroupflip"));

  looper_data.list = dgrouplist;
  bone_looper(ob, static_cast<Bone *>(arm->bonebase.first), &looper_data, dgroup_skinnable_cb);

  /* create an arr of root and tip positions transformed into
   * global coords */
  root = static_cast<float(*)[3]>(mem_calloc(sizeof(float[3]) * numbones, "root"));
  tip = static_cast<float(*)[3]>(mem_calloc(sizeof(float[3]) * numbones, "tip"));
  sel = static_cast<int *>(mem_calloc(sizeof(int) * numbones, "sel"));

  for (int j = 0; j < numbones; j++) {
    bone = bonelist[j];
    dgroup = dgrouplist[j];

    /* handle bbone */
    if (heat) {
      if (segments == 0) {
        segments = 1;
        bbone = nullptr;

        if ((par->pose) && (pchan = dune_pose_channel_find_name(par->pose, bone->name))) {
          if (bone->segments > 1) {
            segments = bone->segments;
            dune_pchan_bbone_spline_setup(pchan, true, false, bbone_array);
            bbone = bbone_array;
          }
        }
      }

      segments--;
    }

    /* compute root and tip */
    if (bbone) {
      mul_v3_m4v3(root[j], bone->arm_mat, bbone[segments].mat[3]);
      if ((segments + 1) < bone->segments) {
        mul_v3_m4v3(tip[j], bone->arm_mat, bbone[segments + 1].mat[3]);
      }
      else {
        copy_v3_v3(tip[j], bone->arm_tail);
      }
    }
    else {
      copy_v3_v3(root[j], bone->arm_head);
      copy_v3_v3(tip[j], bone->arm_tail);
    }

    mul_m4_v3(par->ob_to_world, root[j]);
    mul_m4_v3(par->ob_to_world, tip[j]);

    /* set sel */
    if (wpmode) {
      if (anim_bonecoll_is_visible(arm, bone) && (bone->flag & BONE_SEL)) {
        sel[j] = 1;
      }
    }
    else {
      sel[j] = 1;
    }

    /* find flipped group */
    if (dgroup && mirror) {
      char name_flip[MAXBONENAME];

      lib_string_flip_side_name(name_flip, dgroup->name, false, sizeof(name_flip));
      dgroupflip[j] = dune_ob_defgroup_find_name(ob, name_flip);
    }
  }

  /* create verts */
  mesh = (Mesh *)ob->data;
  verts = static_cast<float(*)[3]>(
      mem_calloc(mesh->totvert * sizeof(*verts), "closestboneverts"));

  if (wpmode) {
    /* if in weight paint mode, use final verts from eval mesh */
    const Ob *ob_eval = graph_get_eval_ob(graph, ob);
    const Mesh *me_eval = dune_ob_get_eval_mesh(ob_eval);
    if (me_eval) {
      dune_mesh_foreach_mapped_vert_coords_get(me_eval, verts, mesh->totvert);
      vertsfilled = 1;
    }
  }
  else if (dune_mods_findby_type(ob, eModTypeSubsurf)) {
    /* Is subdivision-surface on? Lets use the verts on the limit surface then.
     * = same amount of vertices as mesh, but verts  moved to the
     * subdivision-surfaced position, like for 'optimal'. */
    subsurf_calc_limit_positions(mesh, verts);
    vertsfilled = 1;
  }

  /* transform verts to global space */
  const dune::Span<dune::float3> positions = mesh->vert_positions();
  for (int i = 0; i < mesh->totvert; i++) {
    if (!vertsfilled) {
      copy_v3_v3(verts[i], positions[i]);
    }
    mul_m4_v3(ob->ob_to_world, verts[i]);
  }

  /* compute the weights based on gathered verts and bones */
  if (heat) {
    const char *error = nullptr;

    heat_bone_weighting(
        ob, mesh, verts, numbones, dgrouplist, dgroupflip, root, tip, sel, &error);
    if (error) {
      dune_report(reports, RPT_WARNING, error);
    }
  }
  else {
    envelope_bone_weighting(ob,
                            mesh,
                            verts,
                            numbones,
                            bonelist,
                            dgrouplist,
                            dgroupflip,
                            root,
                            tip,
                            sel,
                            mat4_to_scale(par->ob_to_world));
  }

  /* only generated in some cases but can call anyway */
  ed_mesh_mirror_spatial_table_end(ob);

  /* free the mem allocated */
  mem_free(bonelist);
  mem_free(dgrouplist);
  mem_free(dgroupflip);
  mem_free(root);
  mem_free(tip);
  mem_free(sel);
  mem_free(verts);
}

void ed_ob_vgroup_calc_from_armature(ReportList *reports,
                                     Graph *graph,
                                     Scene *scene,
                                     Ob *ob,
                                     Ob *par,
                                     const int mode,
                                     const bool mirror)
{
  /* Lets try to create some vert groups
   * based on the bones of the parent armature.  */
  Armature *arm = static_cast<Armature *>(par->data);

  if (mode == ARM_GROUPS_NAME) {
    const int defbase_tot = dune_ob_defgroup_count(ob);
    int defbase_add;
    /* Traverse the bone list, trying to create empty vert
     * groups corresponding to the bone. */
    defbase_add = bone_looper(
        ob, static_cast<Bone *>(arm->bonebase.first), nullptr, vgroup_add_unique_bone_cb);

    if (defbase_add) {
      /* It's possible there are DWeights outside the range of the current
       * on's deform groups. In this case the new groups won't be empty #33889. */
      ed_vgroup_data_clamp_range(static_cast<Id *>(ob->data), defbase_tot);
    }
  }
  else if (ELEM(mode, ARM_GROUPS_ENVELOPE, ARM_GROUPS_AUTO)) {
    /* Traverse the bone list, trying to create vert groups
     * that are populated w the verts for which the
     * bone is closest. */
    add_verts_to_dgroups(reports, graph, scene, ob, par, (mode == ARM_GROUPS_AUTO), mirror);
  }
}
