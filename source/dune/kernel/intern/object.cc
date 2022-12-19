
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2001-2002 NaN Holding BV. All rights reserved. */

/** \file
 * \ingroup bke
 */

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include <cmath>
#include <cstdio>
#include <cstring>

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_camera_types.h"
#include "DNA_collection_types.h"
#include "DNA_constraint_types.h"
#include "DNA_defaults.h"
#include "DNA_dynamicpaint_types.h"
#include "DNA_effect_types.h"
#include "DNA_fluid_types.h"
#include "DNA_gpencil_modifier_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_key_types.h"
#include "DNA_lattice_types.h"
#include "DNA_light_types.h"
#include "DNA_lightprobe_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_meta_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_nla_types.h"
#include "DNA_object_fluidsim_types.h"
#include "DNA_object_types.h"
#include "DNA_pointcloud_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_sequence_types.h"
#include "DNA_shader_fx_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"

#include "BLI_blenlib.h"
#include "BLI_kdtree.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_DerivedMesh.h"
#include "BKE_action.h"
#include "BKE_anim_data.h"
#include "BKE_anim_path.h"
#include "BKE_anim_visualization.h"
#include "BKE_animsys.h"
#include "BKE_armature.h"
#include "BKE_asset.h"
#include "BKE_bpath.h"
#include "BKE_camera.h"
#include "BKE_collection.h"
#include "BKE_constraint.h"
#include "BKE_crazyspace.h"
#include "BKE_curve.h"
#include "BKE_curves.hh"
#include "BKE_deform.h"
#include "BKE_displist.h"
#include "BKE_duplilist.h"
#include "BKE_editmesh.h"
#include "BKE_editmesh_cache.h"
#include "BKE_effect.h"
#include "BKE_fcurve.h"
#include "BKE_fcurve_driver.h"
#include "BKE_geometry_set.h"
#include "BKE_geometry_set.hh"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_icons.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_image.h"
#include "BKE_key.h"
#include "BKE_lattice.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_lib_remap.h"
#include "BKE_light.h"
#include "BKE_lightprobe.h"
#include "BKE_linestyle.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mball.h"
#include "BKE_mesh.h"
#include "BKE_mesh_wrapper.h"
#include "BKE_modifier.h"
#include "BKE_multires.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_object_facemap.h"
#include "BKE_paint.h"
#include "BKE_particle.h"
#include "BKE_pbvh.h"
#include "BKE_pointcache.h"
#include "BKE_pointcloud.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_shader_fx.h"
#include "BKE_softbody.h"
#include "BKE_speaker.h"
#include "BKE_subdiv_ccg.h"
#include "BKE_subsurf.h"
#include "BKE_vfont.h"
#include "BKE_volume.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DRW_engine.h"

#include "BLO_read_write.h"
#include "BLO_readfile.h"

#include "SEQ_sequencer.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#include "CCGSubSurf.h"
#include "atomic_ops.h"

static CLG_LogRef LOG = {"bke.object"};

/**
 * NOTE(@sergey): Vertex parent modifies original #BMesh which is not safe for threading.
 * Ideally such a modification should be handled as a separate DAG update
 * callback for mesh data-block, but for until it is actually supported use
 * simpler solution with a mutex lock.
 */
#define VPARENT_THREADING_HACK

#ifdef VPARENT_THREADING_HACK
static ThreadMutex vparent_lock = BLI_MUTEX_INITIALIZER;
#endif

static void copy_object_pose(Object *obn, const Object *ob, const int flag);

static void object_init_data(ID *id)
{
  Object *ob = (Object *)id;
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(ob, id));

  MEMCPY_STRUCT_AFTER(ob, DNA_struct_default_get(Object), id);

  ob->type = OB_EMPTY;

  ob->trackflag = OB_POSY;
  ob->upflag = OB_POSZ;

  /* Animation Visualization defaults */
  animviz_settings_init(&ob->avs);
}

static void object_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Object *ob_dst = (Object *)id_dst;
  const Object *ob_src = (const Object *)id_src;

  /* Do not copy runtime data. */
  BKE_object_runtime_reset_on_copy(ob_dst, flag);

  /* We never handle usercount here for own data. */
  const int flag_subdata = flag | LIB_ID_CREATE_NO_USER_REFCOUNT;

  if (ob_src->totcol) {
    ob_dst->mat = (Material **)MEM_dupallocN(ob_src->mat);
    ob_dst->matbits = (char *)MEM_dupallocN(ob_src->matbits);
    ob_dst->totcol = ob_src->totcol;
  }
  else if (ob_dst->mat != nullptr || ob_dst->matbits != nullptr) {
    /* This shall not be needed, but better be safe than sorry. */
    BLI_assert_msg(
        0, "Object copy: non-nullptr material pointers with zero counter, should not happen.");
    ob_dst->mat = nullptr;
    ob_dst->matbits = nullptr;
  }

  if (ob_src->iuser) {
    ob_dst->iuser = (ImageUser *)MEM_dupallocN(ob_src->iuser);
  }

  if (ob_src->runtime.bb) {
    ob_dst->runtime.bb = (BoundBox *)MEM_dupallocN(ob_src->runtime.bb);
  }

  BLI_listbase_clear(&ob_dst->shader_fx);
  LISTBASE_FOREACH (ShaderFxData *, fx, &ob_src->shader_fx) {
    ShaderFxData *nfx = BKE_shaderfx_new(fx->type);
    BLI_strncpy(nfx->name, fx->name, sizeof(nfx->name));
    BKE_shaderfx_copydata_ex(fx, nfx, flag_subdata);
    BLI_addtail(&ob_dst->shader_fx, nfx);
  }

  if (ob_src->pose) {
    copy_object_pose(ob_dst, ob_src, flag_subdata);
    /* backwards compat... non-armatures can get poses in older files? */
    if (ob_src->type == OB_ARMATURE) {
      const bool do_pose_id_user = (flag & LIB_ID_CREATE_NO_USER_REFCOUNT) == 0;
      BKE_pose_rebuild(bmain, ob_dst, (bArmature *)ob_dst->data, do_pose_id_user);
    }
  }

  BKE_object_facemap_copy_list(&ob_dst->fmaps, &ob_src->fmaps);
  BKE_constraints_copy_ex(&ob_dst->constraints, &ob_src->constraints, flag_subdata, true);

  ob_dst->mode = ob_dst->type != OB_GPENCIL ? OB_MODE_OBJECT : ob_dst->mode;
  ob_dst->sculpt = nullptr;

  if (ob_src->pd) {
    ob_dst->pd = (PartDeflect *)MEM_dupallocN(ob_src->pd);
    if (ob_dst->pd->rng) {
      ob_dst->pd->rng = (RNG *)MEM_dupallocN(ob_src->pd->rng);
    }
  }
  BKE_rigidbody_object_copy(bmain, ob_dst, ob_src, flag_subdata);

  BLI_listbase_clear(&ob_dst->modifiers);
  BLI_listbase_clear(&ob_dst->greasepencil_modifiers);
  /* NOTE: Also takes care of softbody and particle systems copying. */
  BKE_object_modifier_stack_copy(ob_dst, ob_src, true, flag_subdata);

  BLI_listbase_clear((ListBase *)&ob_dst->drawdata);
  BLI_listbase_clear(&ob_dst->pc_ids);

  ob_dst->avs = ob_src->avs;
  ob_dst->mpath = animviz_copy_motionpath(ob_src->mpath);

  /* Do not copy object's preview
   * (mostly due to the fact renderers create temp copy of objects). */
  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0 && false) { /* XXX TODO: temp hack. */
    BKE_previewimg_id_copy(&ob_dst->id, &ob_src->id);
  }
  else {
    ob_dst->preview = nullptr;
  }
}

static void object_free_data(ID *id)
{
  Object *ob = (Object *)id;

  DRW_drawdata_free((ID *)ob);

  /* BKE_<id>_free shall never touch to ID->us. Never ever. */
  BKE_object_free_modifiers(ob, LIB_ID_CREATE_NO_USER_REFCOUNT);
  BKE_object_free_shaderfx(ob, LIB_ID_CREATE_NO_USER_REFCOUNT);

  MEM_SAFE_FREE(ob->mat);
  MEM_SAFE_FREE(ob->matbits);
  MEM_SAFE_FREE(ob->iuser);
  MEM_SAFE_FREE(ob->runtime.bb);

  BLI_freelistN(&ob->fmaps);
  if (ob->pose) {
    BKE_pose_free_ex(ob->pose, false);
    ob->pose = nullptr;
  }
  if (ob->mpath) {
    animviz_free_motionpath(ob->mpath);
    ob->mpath = nullptr;
  }

  BKE_constraints_free_ex(&ob->constraints, false);

  BKE_partdeflect_free(ob->pd);
  BKE_rigidbody_free_object(ob, nullptr);
  BKE_rigidbody_free_constraint(ob);

  sbFree(ob);

  BKE_sculptsession_free(ob);

  BLI_freelistN(&ob->pc_ids);

  /* Free runtime curves data. */
  if (ob->runtime.curve_cache) {
    BKE_curve_bevelList_free(&ob->runtime.curve_cache->bev);
    if (ob->runtime.curve_cache->anim_path_accum_length) {
      MEM_freeN((void *)ob->runtime.curve_cache->anim_path_accum_length);
    }
    MEM_freeN(ob->runtime.curve_cache);
    ob->runtime.curve_cache = nullptr;
  }

  BKE_previewimg_free(&ob->preview);
}

static void library_foreach_modifiersForeachIDLink(void *user_data,
                                                   Object *UNUSED(object),
                                                   ID **id_pointer,
                                                   int cb_flag)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

static void library_foreach_gpencil_modifiersForeachIDLink(void *user_data,
                                                           Object *UNUSED(object),
                                                           ID **id_pointer,
                                                           int cb_flag)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

static void library_foreach_shaderfxForeachIDLink(void *user_data,
                                                  Object *UNUSED(object),
                                                  ID **id_pointer,
                                                  int cb_flag)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

static void library_foreach_constraintObjectLooper(bConstraint *UNUSED(con),
                                                   ID **id_pointer,
                                                   bool is_reference,
                                                   void *user_data)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  const int cb_flag = is_reference ? IDWALK_CB_USER : IDWALK_CB_NOP;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

static void library_foreach_particlesystemsObjectLooper(ParticleSystem *UNUSED(psys),
                                                        ID **id_pointer,
                                                        void *user_data,
                                                        int cb_flag)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

static void object_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Object *object = (Object *)id;

  /* object data special case */
  if (object->type == OB_EMPTY) {
    /* empty can have nullptr or Image */
    BKE_LIB_FOREACHID_PROCESS_ID(data, object->data, IDWALK_CB_USER);
  }
  else {
    /* when set, this can't be nullptr */
    if (object->data) {
      BKE_LIB_FOREACHID_PROCESS_ID(data, object->data, IDWALK_CB_USER | IDWALK_CB_NEVER_NULL);
    }
  }

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->parent, IDWALK_CB_NEVER_SELF);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->track, IDWALK_CB_NEVER_SELF);

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->poselib, IDWALK_CB_USER);

  for (int i = 0; i < object->totcol; i++) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->mat[i], IDWALK_CB_USER);
  }

  /* Note that ob->gpd is deprecated, so no need to handle it here. */
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->instance_collection, IDWALK_CB_USER);

  if (object->pd) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->pd->tex, IDWALK_CB_USER);
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->pd->f_source, IDWALK_CB_NOP);
  }
  /* Note that ob->effect is deprecated, so no need to handle it here. */

  if (object->pose) {
    LISTBASE_FOREACH (bPoseChannel *, pchan, &object->pose->chanbase) {
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
          data,
          IDP_foreach_property(pchan->prop,
                               IDP_TYPE_FILTER_ID,
                               BKE_lib_query_idpropertiesForeachIDLink_callback,
                               data));

      BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, pchan->custom, IDWALK_CB_USER);
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
          data,
          BKE_constraints_id_loop(
              &pchan->constraints, library_foreach_constraintObjectLooper, data));
    }
  }

  if (object->rigidbody_constraint) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(
        data, object->rigidbody_constraint->ob1, IDWALK_CB_NEVER_SELF);
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(
        data, object->rigidbody_constraint->ob2, IDWALK_CB_NEVER_SELF);
  }

  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_modifiers_foreach_ID_link(object, library_foreach_modifiersForeachIDLink, data));
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data,
      BKE_gpencil_modifiers_foreach_ID_link(
          object, library_foreach_gpencil_modifiersForeachIDLink, data));
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data,
      BKE_constraints_id_loop(&object->constraints, library_foreach_constraintObjectLooper, data));
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_shaderfx_foreach_ID_link(object, library_foreach_shaderfxForeachIDLink, data));

  LISTBASE_FOREACH (ParticleSystem *, psys, &object->particlesystem) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, BKE_particlesystem_id_loop(psys, library_foreach_particlesystemsObjectLooper, data));
  }

  if (object->soft) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, object->soft->collision_group, IDWALK_CB_NOP);

    if (object->soft->effector_weights) {
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(
          data, object->soft->effector_weights->group, IDWALK_CB_NOP);
    }
  }
}

static void object_foreach_path_pointcache(ListBase *ptcache_list,
                                           BPathForeachPathData *bpath_data)
{
  for (PointCache *cache = (PointCache *)ptcache_list->first; cache != nullptr;
       cache = cache->next) {
    if (cache->flag & PTCACHE_DISK_CACHE) {
      BKE_bpath_foreach_path_fixed_process(bpath_data, cache->path);
    }
  }
}

static void object_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Object *ob = reinterpret_cast<Object *>(id);

  LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
    /* TODO: Move that to #ModifierTypeInfo. */
    switch (md->type) {
      case eModifierType_Fluidsim: {
        FluidsimModifierData *fluidmd = reinterpret_cast<FluidsimModifierData *>(md);
        if (fluidmd->fss) {
          BKE_bpath_foreach_path_fixed_process(bpath_data, fluidmd->fss->surfdataPath);
        }
        break;
      }
      case eModifierType_Fluid: {
        FluidModifierData *fmd = reinterpret_cast<FluidModifierData *>(md);
        if (fmd->type & MOD_FLUID_TYPE_DOMAIN && fmd->domain) {
          BKE_bpath_foreach_path_fixed_process(bpath_data, fmd->domain->cache_directory);
        }
        break;
      }
      case eModifierType_Cloth: {
        ClothModifierData *clmd = reinterpret_cast<ClothModifierData *>(md);
        object_foreach_path_pointcache(&clmd->ptcaches, bpath_data);
        break;
      }
      case eModifierType_Ocean: {
        OceanModifierData *omd = reinterpret_cast<OceanModifierData *>(md);
        BKE_bpath_foreach_path_fixed_process(bpath_data, omd->cachepath);
        break;
      }
      case eModifierType_MeshCache: {
        MeshCacheModifierData *mcmd = reinterpret_cast<MeshCacheModifierData *>(md);
        BKE_bpath_foreach_path_fixed_process(bpath_data, mcmd->filepath);
        break;
      }
      default:
        break;
    }
  }

  if (ob->soft != nullptr) {
    object_foreach_path_pointcache(&ob->soft->shared->ptcaches, bpath_data);
  }

  LISTBASE_FOREACH (ParticleSystem *, psys, &ob->particlesystem) {
    object_foreach_path_pointcache(&psys->ptcaches, bpath_data);
  }
}

static void write_fmaps(BlendWriter *writer, ListBase *fbase)
{
  LISTBASE_FOREACH (bFaceMap *, fmap, fbase) {
    BLO_write_struct(writer, bFaceMap, fmap);
  }
}

static void object_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Object *ob = (Object *)id;

  const bool is_undo = BLO_write_is_undo(writer);

  /* Clean up, important in undo case to reduce false detection of changed data-blocks. */
  BKE_object_runtime_reset(ob);

  if (is_undo) {
    /* For undo we stay in object mode during undo presses, so keep edit-mode disabled on save as
     * well, can help reducing false detection of changed data-blocks. */
    ob->mode &= ~OB_MODE_EDIT;
  }

  /* write LibData */
  BLO_write_id_struct(writer, Object, id_address, &ob->id);
  BKE_id_blend_write(writer, &ob->id);

  if (ob->adt) {
    BKE_animdata_blend_write(writer, ob->adt);
  }

  /* direct data */
  BLO_write_pointer_array(writer, ob->totcol, ob->mat);
  BLO_write_raw(writer, sizeof(char) * ob->totcol, ob->matbits);

  bArmature *arm = nullptr;
  if (ob->type == OB_ARMATURE) {
    arm = (bArmature *)ob->data;
  }

  BKE_pose_blend_write(writer, ob->pose, arm);
  write_fmaps(writer, &ob->fmaps);
  BKE_constraint_blend_write(writer, &ob->constraints);
  animviz_motionpath_blend_write(writer, ob->mpath);

  BLO_write_struct(writer, PartDeflect, ob->pd);
  if (ob->soft) {
    /* Set deprecated pointers to prevent crashes of older Blenders */
    ob->soft->pointcache = ob->soft->shared->pointcache;
    ob->soft->ptcaches = ob->soft->shared->ptcaches;
    BLO_write_struct(writer, SoftBody, ob->soft);
    BLO_write_struct(writer, SoftBody_Shared, ob->soft->shared);
    BKE_ptcache_blend_write(writer, &(ob->soft->shared->ptcaches));
    BLO_write_struct(writer, EffectorWeights, ob->soft->effector_weights);
  }

  if (ob->rigidbody_object) {
    /* TODO: if any extra data is added to handle duplis, will need separate function then */
    BLO_write_struct(writer, RigidBodyOb, ob->rigidbody_object);
  }
  if (ob->rigidbody_constraint) {
    BLO_write_struct(writer, RigidBodyCon, ob->rigidbody_constraint);
  }

  if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE) {
    BLO_write_struct(writer, ImageUser, ob->iuser);
  }

  BKE_particle_system_blend_write(writer, &ob->particlesystem);
  BKE_modifier_blend_write(writer, &ob->modifiers);
  BKE_gpencil_modifier_blend_write(writer, &ob->greasepencil_modifiers);
  BKE_shaderfx_blend_write(writer, &ob->shader_fx);

  BLO_write_struct_list(writer, LinkData, &ob->pc_ids);

  BKE_previewimg_blend_write(writer, ob->preview);
}

/* XXX deprecated - old animation system */
static void direct_link_nlastrips(BlendDataReader *reader, ListBase *strips)
{
  BLO_read_list(reader, strips);

  LISTBASE_FOREACH (bActionStrip *, strip, strips) {
    BLO_read_list(reader, &strip->modifiers);
  }
}

static void object_blend_read_data(BlendDataReader *reader, ID *id)
{
  Object *ob = (Object *)id;

  PartEff *paf;

  /* XXX This should not be needed - but seems like it can happen in some cases,
   * so for now play safe. */
  ob->proxy_from = nullptr;

  const bool is_undo = BLO_read_data_is_undo(reader);
  if (ob->id.tag & (LIB_TAG_EXTERN | LIB_TAG_INDIRECT)) {
    /* Do not allow any non-object mode for linked data.
     * See T34776, T42780, T81027 for more information. */
    ob->mode &= ~OB_MODE_ALL_MODE_DATA;
  }
  else if (is_undo) {
    /* For undo we want to stay in object mode during undo presses, so keep some edit modes
     * disabled.
     * TODO: Check if we should not disable more edit modes here? */
    ob->mode &= ~(OB_MODE_EDIT | OB_MODE_PARTICLE_EDIT);
  }

  BLO_read_data_address(reader, &ob->adt);
  BKE_animdata_blend_read_data(reader, ob->adt);

  BLO_read_data_address(reader, &ob->pose);
  BKE_pose_blend_read_data(reader, ob->pose);

  BLO_read_data_address(reader, &ob->mpath);
  if (ob->mpath) {
    animviz_motionpath_blend_read_data(reader, ob->mpath);
  }

  /* Only for versioning, vertex group names are now stored on object data. */
  BLO_read_list(reader, &ob->defbase);

  BLO_read_list(reader, &ob->fmaps);
  /* XXX deprecated - old animation system <<< */
  direct_link_nlastrips(reader, &ob->nlastrips);
  BLO_read_list(reader, &ob->constraintChannels);
  /* >>> XXX deprecated - old animation system */

  BLO_read_pointer_array(reader, (void **)&ob->mat);
  BLO_read_data_address(reader, &ob->matbits);

  /* do it here, below old data gets converted */
  BKE_modifier_blend_read_data(reader, &ob->modifiers, ob);
  BKE_gpencil_modifier_blend_read_data(reader, &ob->greasepencil_modifiers);
  BKE_shaderfx_blend_read_data(reader, &ob->shader_fx);

  BLO_read_list(reader, &ob->effect);
  paf = (PartEff *)ob->effect.first;
  while (paf) {
    if (paf->type == EFF_PARTICLE) {
      paf->keys = nullptr;
    }
    if (paf->type == EFF_WAVE) {
      WaveEff *wav = (WaveEff *)paf;
      PartEff *next = paf->next;
      WaveModifierData *wmd = (WaveModifierData *)BKE_modifier_new(eModifierType_Wave);

      wmd->damp = wav->damp;
      wmd->flag = wav->flag;
      wmd->height = wav->height;
      wmd->lifetime = wav->lifetime;
      wmd->narrow = wav->narrow;
      wmd->speed = wav->speed;
      wmd->startx = wav->startx;
      wmd->starty = wav->startx;
      wmd->timeoffs = wav->timeoffs;
      wmd->width = wav->width;

      BLI_addtail(&ob->modifiers, wmd);

      BLI_remlink(&ob->effect, paf);
      MEM_freeN(paf);

      paf = next;
      continue;
    }
    if (paf->type == EFF_BUILD) {
      BuildEff *baf = (BuildEff *)paf;
      PartEff *next = paf->next;
      BuildModifierData *bmd = (BuildModifierData *)BKE_modifier_new(eModifierType_Build);

      bmd->start = baf->sfra;
      bmd->length = baf->len;
      bmd->randomize = 0;
      bmd->seed = 1;

      BLI_addtail(&ob->modifiers, bmd);

      BLI_remlink(&ob->effect, paf);
      MEM_freeN(paf);

      paf = next;
      continue;
    }
    paf = paf->next;
  }

  BLO_read_data_address(reader, &ob->pd);
  BKE_particle_partdeflect_blend_read_data(reader, ob->pd);
  BLO_read_data_address(reader, &ob->soft);
  if (ob->soft) {
    SoftBody *sb = ob->soft;

    sb->bpoint = nullptr; /* init pointers so it gets rebuilt nicely */
    sb->bspring = nullptr;
    sb->scratch = nullptr;
    /* although not used anymore */
    /* still have to be loaded to be compatible with old files */
    BLO_read_pointer_array(reader, (void **)&sb->keys);
    if (sb->keys) {
      for (int a = 0; a < sb->totkey; a++) {
        BLO_read_data_address(reader, &sb->keys[a]);
      }
    }

    BLO_read_data_address(reader, &sb->effector_weights);
    if (!sb->effector_weights) {
      sb->effector_weights = BKE_effector_add_weights(nullptr);
    }

    BLO_read_data_address(reader, &sb->shared);
    if (sb->shared == nullptr) {
      /* Link deprecated caches if they exist, so we can use them for versioning.
       * We should only do this when sb->shared == nullptr, because those pointers
       * are always set (for compatibility with older Blenders). We mustn't link
       * the same pointcache twice. */
      BKE_ptcache_blend_read_data(reader, &sb->ptcaches, &sb->pointcache, false);
    }
    else {
      /* link caches */
      BKE_ptcache_blend_read_data(reader, &sb->shared->ptcaches, &sb->shared->pointcache, false);
    }
  }
  BLO_read_data_address(reader, &ob->fluidsimSettings); /* NT */

  BLO_read_data_address(reader, &ob->rigidbody_object);
  if (ob->rigidbody_object) {
    RigidBodyOb *rbo = ob->rigidbody_object;
    /* Allocate runtime-only struct */
    rbo->shared = (RigidBodyOb_Shared *)MEM_callocN(sizeof(*rbo->shared), "RigidBodyObShared");
  }
  BLO_read_data_address(reader, &ob->rigidbody_constraint);
  if (ob->rigidbody_constraint) {
    ob->rigidbody_constraint->physics_constraint = nullptr;
  }

  BLO_read_list(reader, &ob->particlesystem);
  BKE_particle_system_blend_read_data(reader, &ob->particlesystem);

  BKE_constraint_blend_read_data(reader, &ob->constraints);

  BLO_read_list(reader, &ob->hooks);
  while (ob->hooks.first) {
    ObHook *hook = (ObHook *)ob->hooks.first;
    HookModifierData *hmd = (HookModifierData *)BKE_modifier_new(eModifierType_Hook);

    BLO_read_int32_array(reader, hook->totindex, &hook->indexar);

    /* Do conversion here because if we have loaded
     * a hook we need to make sure it gets converted
     * and freed, regardless of version.
     */
    copy_v3_v3(hmd->cent, hook->cent);
    hmd->falloff = hook->falloff;
    hmd->force = hook->force;
    hmd->indexar = hook->indexar;
    hmd->object = hook->parent;
    memcpy(hmd->parentinv, hook->parentinv, sizeof(hmd->parentinv));
    hmd->totindex = hook->totindex;

    BLI_addhead(&ob->modifiers, hmd);
    BLI_remlink(&ob->hooks, hook);

    BKE_modifier_unique_name(&ob->modifiers, (ModifierData *)hmd);

    MEM_freeN(hook);
  }

  BLO_read_data_address(reader, &ob->iuser);
  if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE && !ob->iuser) {
    BKE_object_empty_draw_type_set(ob, ob->empty_drawtype);
  }

  BKE_object_runtime_reset(ob);
  BLO_read_list(reader, &ob->pc_ids);

  /* in case this value changes in future, clamp else we get undefined behavior */
  CLAMP(ob->rotmode, ROT_MODE_MIN, ROT_MODE_MAX);

  if (ob->sculpt) {
    ob->sculpt = nullptr;
    /* Only create data on undo, otherwise rely on editor mode switching. */
    if (BLO_read_data_is_undo(reader) && (ob->mode & OB_MODE_ALL_SCULPT)) {
      BKE_object_sculpt_data_create(ob);
    }
  }

  BLO_read_data_address(reader, &ob->preview);
  BKE_previewimg_blend_read(reader, ob->preview);
}

/* XXX deprecated - old animation system */
static void lib_link_nlastrips(BlendLibReader *reader, ID *id, ListBase *striplist)
{
  LISTBASE_FOREACH (bActionStrip *, strip, striplist) {
    BLO_read_id_address(reader, id->lib, &strip->object);
    BLO_read_id_address(reader, id->lib, &strip->act);
    BLO_read_id_address(reader, id->lib, &strip->ipo);
    LISTBASE_FOREACH (bActionModifier *, amod, &strip->modifiers) {
      BLO_read_id_address(reader, id->lib, &amod->ob);
    }
  }
}

/* XXX deprecated - old animation system */
static void lib_link_constraint_channels(BlendLibReader *reader, ID *id, ListBase *chanbase)
{
  LISTBASE_FOREACH (bConstraintChannel *, chan, chanbase) {
    BLO_read_id_address(reader, id->lib, &chan->ipo);
  }
}
