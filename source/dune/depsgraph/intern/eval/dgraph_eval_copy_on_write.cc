/** dgraph **/

/* Enable special trickery to treat nested owned ids (such as nodetree of
 * material) to be handled in same way as "real" data-blocks, even tho some
 * internal dune routines doesn't treat them like that.
 *
 * TODO: Re-evaluate that after new id handling is in place. */
#define NESTED_ID_NASTY_WORKAROUND

/* Silence warnings from copying deprecated fields. */
#define TYPES_DEPRECATED_ALLOW

#include "intern/eval/dgraph_eval_copy_on_write.h"

#include <cstring>

#include "lib_listbase.h"
#include "lib_string.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "dune_curve.h"
#include "dune_global.h"
#include "dune_dpen.h"
#include "dune_dpen_update_cache.h"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_scene.h"

#include "dgraph.h"
#include "dgraph_query.h"

#include "mem_guardedalloc.h"

#include "types_id.h"
#include "types_anim.h"
#include "types_armature.h"
#include "types_dpen.h"
#include "types_mesh.h"
#include "types_modifier.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_rigidbody.h"
#include "types_scene.h"
#include "types_sequence.h"
#include "types_simulation.h"
#include "types_sound.h"

#include "draw_engine.h"

#ifdef NESTED_ID_NASTY_WORKAROUND
#  include "types_curve.h"
#  include "types_key.h"
#  include "types_lattice.h"
#  include "types_light.h"
#  include "types_linestyle.h"
#  include "types_material.h"
#  include "types_meta.h"
#  include "types_node.h"
#  include "types_texture.h"
#  include "types_world.h"
#endif

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_animsys.h"
#include "dune_armature.h"
#include "dune_editmesh.h"
#include "dune_lib_query.h"
#include "dune_modifier.h"
#include "dune_object.h"
#include "dune_pointcache.h"
#include "dune_sound.h"

#include "seq_relations.h"

#include "intern/builder/dgraph_builder.h"
#include "intern/builder/dgraph_builder_nodes.h"
#include "intern/dgraph.h"
#include "intern/eval/dgraph_eval_runtime_backup.h"
#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_id.h"

namespace dune::dgraph {

#define DEBUG_PRINT \
  if (G.debug & G_DEBUG_DEPSGRAPH_EVAL) \
  printf

namespace {

#ifdef NESTED_ID_NASTY_WORKAROUND
union NestedIdHackTempStorage {
  Curve curve;
  FreestyleLineStyle linestyle;
  Light lamp;
  Lattice lattice;
  Material material;
  Mesh mesh;
  Scene scene;
  Tex tex;
  World world;
  Simulation simulation;
};

/* Set nested owned id pointers to nullptr. */
void nested_id_hack_discard_ptrs(Id *id_cow)
{
  switch (GS(id_cow->name)) {
#  define SPECIAL_CASE(id_type, types_type, field) \
    case id_type: { \
      ((types_type *)id_cow)->field = nullptr; \
      break; \
    }

    SPECIAL_CASE(ID_LS, FreestyleLineStyle, nodetree)
    SPECIAL_CASE(ID_LA, Light, nodetree)
    SPECIAL_CASE(ID_MA, Material, nodetree)
    SPECIAL_CASE(ID_TE, Tex, nodetree)
    SPECIAL_CASE(ID_WO, World, nodetree)
    SPECIAL_CASE(ID_SIM, Simulation, nodetree)

    SPECIAL_CASE(ID_CU_LEGACY, Curve, key)
    SPECIAL_CASE(ID_LT, Lattice, key)
    SPECIAL_CASE(ID_ME, Mesh, key)

    case ID_SCE: {
      Scene *scene_cow = (Scene *)id_cow;
      /* Node trees always have their own id node in the graph, and are
       * being copied as part of their copy-on-write process. */
      scene_cow->nodetree = nullptr;
      /* Tool settings pointer is shared with the original scene. */
      scene_cow->toolsettings = nullptr;
      break;
    }

    case ID_OB: {
      /* Clear the ParticleSettings pointer to prevent doubly-freeing it. */
      Object *ob = (Object *)id_cow;
      LISTBASE_FOREACH (ParticleSystem *, psys, &ob->particlesystem) {
        psys->part = nullptr;
      }
      break;
    }
#  undef SPECIAL_CASE

    default:
      break;
  }
}

/* Set id pointer of nested owned ids (nodetree, key) to nullptr.
 *
 * Return pointer to a new id to be used. */
const Id *nested_id_hack_get_discarded_ptrs(NestedIdHackTempStorage *storage, const ID *id)
{
  switch (GS(id->name)) {
#  define SPECIAL_CASE(id_type, types_type, field, variable) \
    case id_type: { \
      storage->variable = types::shallow_copy(*(types_type *)id); \
      storage->variable.field = nullptr; \
      return &storage->variable.id; \
    }

    SPECIAL_CASE(ID_LS, FreestyleLineStyle, nodetree, linestyle)
    SPECIAL_CASE(ID_LA, Light, nodetree, lamp)
    SPECIAL_CASE(ID_MA, Material, nodetree, material)
    SPECIAL_CASE(ID_TE, Tex, nodetree, tex)
    SPECIAL_CASE(ID_WO, World, nodetree, world)
    SPECIAL_CASE(ID_SIM, Simulation, nodetree, simulation)

    SPECIAL_CASE(ID_CU_LEGACY, Curve, key, curve)
    SPECIAL_CASE(ID_LT, Lattice, key, lattice)
    SPECIAL_CASE(ID_ME, Mesh, key, mesh)

    case ID_SCE: {
      storage->scene = *(Scene *)id;
      storage->scene.toolsettings = nullptr;
      storage->scene.nodetree = nullptr;
      return &storage->scene.id;
    }

#  undef SPECIAL_CASE

    default:
      break;
  }
  return id;
}

/* Set id pointer of nested owned ids (nodetree, key) to the original value. */
void nested_id_hack_restore_ptrs(const Id *old_id, Id *new_id)
{
  if (new_id == nullptr) {
    return;
  }
  switch (GS(old_id->name)) {
#  define SPECIAL_CASE(id_type, types_type, field) \
    case id_type: { \
      ((types_type *)(new_id))->field = ((types_type *)(old_id))->field; \
      break; \
    }

    SPECIAL_CASE(ID_LS, FreestyleLineStyle, nodetree)
    SPECIAL_CASE(ID_LA, Light, nodetree)
    SPECIAL_CASE(ID_MA, Material, nodetree)
    SPECIAL_CASE(ID_SCE, Scene, nodetree)
    SPECIAL_CASE(ID_TE, Tex, nodetree)
    SPECIAL_CASE(ID_WO, World, nodetree)
    SPECIAL_CASE(ID_SIM, Simulation, nodetree)

    SPECIAL_CASE(ID_CU_LEGACY, Curve, key)
    SPECIAL_CASE(ID_LT, Lattice, key)
    SPECIAL_CASE(ID_ME, Mesh, key)

#  undef SPECIAL_CASE
    default:
      break;
  }
}

/* Remap pointer of nested owned ids (nodetree. key) to the new id values. */
void ntree_hack_remap_ptrs(const DGraph *dgraph, Id *id_cow)
{
  switch (GS(id_cow->name)) {
#  define SPECIAL_CASE(id_type, types_type, field, field_type) \
    case id_type: { \
      types_type *data = (types_type *)id_cow; \
      if (data->field != nullptr) { \
        Id *ntree_id_cow = dgraph->get_cow_id(&data->field->id); \
        if (ntree_id_cow != nullptr) { \
          DEG_COW_PRINT("    Remapping datablock for %s: id_orig=%p id_cow=%p\n", \
                        data->field->id.name, \
                        data->field, \
                        ntree_id_cow); \
          data->field = (field_type *)ntree_id_cow; \
        } \
      } \
      break; \
    }

    SPECIAL_CASE(ID_LS, FreestyleLineStyle, nodetree, bNodeTree)
    SPECIAL_CASE(ID_LA, Light, nodetree, bNodeTree)
    SPECIAL_CASE(ID_MA, Material, nodetree, bNodeTree)
    SPECIAL_CASE(ID_SCE, Scene, nodetree, bNodeTree)
    SPECIAL_CASE(ID_TE, Tex, nodetree, bNodeTree)
    SPECIAL_CASE(ID_WO, World, nodetree, bNodeTree)
    SPECIAL_CASE(ID_SIM, Simulation, nodetree, bNodeTree)

    SPECIAL_CASE(ID_CU_LEGACY, Curve, key, Key)
    SPECIAL_CASE(ID_LT, Lattice, key, Key)
    SPECIAL_CASE(ID_ME, Mesh, key, Key)

#  undef SPECIAL_CASE
    default:
      break;
  }
}
#endif /* NODETREE_NASTY_WORKAROUND */

struct ValidateData {
  bool is_valid;
};

/* Similar to generic dune_id_copy() but does not require main and assumes pointer
 * is already allocated. */
bool id_copy_inplace_no_main(const Id *id, Id *newid)
{
  const Id *id_for_copy = id;

  if (G.debug & G_DEBUG_DEPSGRAPH_UUID) {
    const IdType id_type = GS(id_for_copy->name);
    if (id_type == ID_OB) {
      const Object *object = reinterpret_cast<const Object *>(id_for_copy);
      dune_object_check_uuids_unique_and_report(object);
    }
  }

#ifdef NESTED_ID_NASTY_WORKAROUND
  NestedIdHackTempStorage id_hack_storage;
  id_for_copy = nested_id_hack_get_discarded_ptrs(&id_hack_storage, id);
#endif

  bool result = (dune_id_copy_ex(nullptr,
                                (Id *)id_for_copy,
                                &newid,
                                (LIB_ID_COPY_LOCALIZE | LIB_ID_CREATE_NO_ALLOCATE |
                                 LIB_ID_COPY_SET_COPIED_ON_WRITE)) != nullptr);

#ifdef NESTED_ID_NASTY_WORKAROUND
  if (result) {
    nested_id_hack_restore_ptrs(id, newid);
  }
#endif

  return result;
}

/* Similar to dune_scene_copy() but does not require main and assumes pointer
 * is already allocated. */
bool scene_copy_inplace_no_main(const Scene *scene, Scene *new_scene)
{

  if (G.debug & G_DEBUG_DEPSGRAPH_UUID) {
    seq_relations_check_uuids_unique_and_report(scene);
  }

#ifdef NESTED_ID_NASTY_WORKAROUND
  NestedIdHackTempStorage id_hack_storage;
  const Id *id_for_copy = nested_id_hack_get_discarded_ptrs(&id_hack_storage, &scene->id);
#else
  const Id *id_for_copy = &scene->id;
#endif
  bool result = (dune_id_copy_ex(nullptr,
                                id_for_copy,
                                (Id **)&new_scene,
                                (LIB_ID_COPY_LOCALIZE | LIB_ID_CREATE_NO_ALLOCATE |
                                 LIB_ID_COPY_SET_COPIED_ON_WRITE)) != nullptr);

#ifdef NESTED_ID_NASTY_WORKAROUND
  if (result) {
    nested_id_hack_restore_ptrs(&scene->id, &new_scene->id);
  }
#endif

  return result;
}

/* For the given scene get view layer which corresponds to an original for the
 * scene's evaluated one. This depends on how the scene is pulled into the
 * dependency graph. */
ViewLayer *get_original_view_layer(const DGraph *dgraph, const IdNode *id_node)
{
  if (id_node->linked_state == DGRAPH_ID_LINKED_DIRECTLY) {
    return dgraph->view_layer;
  }
  if (id_node->linked_state == DGRAPH_ID_LINKED_VIA_SET) {
    Scene *scene_orig = reinterpret_cast<Scene *>(id_node->id_orig);
    return dune_view_layer_default_render(scene_orig);
  }
  /* Is possible to have scene linked indirectly (i.e. via the driver) which
   * we need to support. Currently there are issues somewhere else, which
   * makes testing hard. This is a reported problem, so will eventually be
   * properly fixed.
   *
   * TODO: Support indirectly linked scene. */
  return nullptr;
}

/* Remove all view layers but the one which corresponds to an input one. */
void scene_remove_unused_view_layers(const DGraph *dgraph,
                                     const IdNode *id_node,
                                     Scene *scene_cow)
{
  const ViewLayer *view_layer_input;
  if (dgraph->is_render_pipeline_dgraph) {
    /* If the dependency graph is used for post-processing (such as compositor) we do need to
     * have access to its view layer names so can not remove any view layers.
     * On a more positive side we can remove all the bases from all the view layers.
     *
     * NOTE: Need to clear pointers which might be pointing to original on freed (due to being
     * unused) data.
     *
     * NOTE: Need to keep view layers for all scenes, even indirect ones. This is because of
     * render layer node possibly pointing to another scene. */
    LISTBASE_FOREACH (ViewLayer *, view_layer, &scene_cow->view_layers) {
      view_layer->basact = nullptr;
    }
    return;
  }
  if (id_node->linked_state == DGRAPH m_ID_LINKED_INDIRECTLY) {
    /* Indirectly linked scenes means it's not an input scene and not a set scene, and is pulled
     * via some driver. Such scenes should not have view layers after copy. */
    view_layer_input = nullptr;
  }
  else {
    view_layer_input = get_original_view_layer(dgraph, id_node);
  }
  ViewLayer *view_layer_eval = nullptr;
  /* Find evaluated view layer. At the same time we free memory used by
   * all other of the view layers. */
  for (ViewLayer *view_layer_cow = reinterpret_cast<ViewLayer *>(scene_cow->view_layers.first),
                 *view_layer_next;
       view_layer_cow != nullptr;
       view_layer_cow = view_layer_next) {
    view_layer_next = view_layer_cow->next;
    if (view_layer_input != nullptr && streq(view_layer_input->name, view_layer_cow->name)) {
      view_layer_eval = view_layer_cow;
    }
    else {
      dune_view_layer_free_ex(view_layer_cow, false);
    }
  }
  /* Make evaluated view layer the only one in the evaluated scene (if it exists). */
  if (view_layer_eval != nullptr) {
    view_layer_eval->prev = view_layer_eval->next = nullptr;
  }
  scene_cow->view_layers.first = view_layer_eval;
  scene_cow->view_layers.last = view_layer_eval;
}

void scene_remove_all_bases(Scene *scene_cow)
{
  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene_cow->view_layers) {
    lib_freelistN(&view_layer->object_bases);
  }
}

/* Makes it so given view layer only has bases corresponding to enabled
 * objects. */
void view_layer_remove_disabled_bases(const DGraph *dgraph,
                                      const Scene *scene,
                                      ViewLayer *view_layer)
{
  if (view_layer == nullptr) {
    return;
  }
  ListBase enabled_bases = {nullptr, nullptr};
  dune_view_layer_synced_ensure(scene, view_layer);
  LISTBASE_FOREACH_MUTABLE (Base *, base, dune_view_layer_object_bases_get(view_layer)) {
    /* TODO: Would be cool to optimize this somehow, or make it so
     * builder tags bases.
     *
     * NOTE: The idea of using id's tag and check whether its copied ot not
     * is not reliable, since object might be indirectly linked into the
     * graph.
     *
     * NOTE: We are using original base since the object which evaluated base
     * points to is not yet copied. This is dangerous access from evaluated
     * domain to original one, but this is how the entire copy-on-write works:
     * it does need to access original for an initial copy. */
    const bool is_object_enabled = deg_check_base_in_dgraph(dgraph, base);
    if (is_object_enabled) {
      lib_addtail(&enabled_bases, base);
    }
    else {
      if (base == view_layer->basact) {
        view_layer->basact = nullptr;
      }
      mem_freen(base);
    }
  }
  view_layer->object_bases = enabled_bases;
}

void view_layer_update_orig_base_pointers(const ViewLayer *view_layer_orig,
                                          ViewLayer *view_layer_eval)
{
  if (view_layer_orig == nullptr || view_layer_eval == nullptr) {
    /* Happens when scene is only used for parameters or compositor/sequencer. */
    return;
  }
  Base *base_orig = reinterpret_cast<Base *>(view_layer_orig->object_bases.first);
  LISTBASE_FOREACH (Base *, base_eval, &view_layer_eval->object_bases) {
    base_eval->base_orig = base_orig;
    base_orig = base_orig->next;
  }
}

void scene_setup_view_layers_before_remap(const DGraph *dgraph,
                                          const IdNode *id_node,
                                          Scene *scene_cow)
{
  scene_remove_unused_view_layers(dgraph, id_node, scene_cow);
  /* If dependency graph is used for post-processing we don't need any bases and can free of them.
   * Do it before re-mapping to make that process faster. */
  if (dgraph->is_render_pipeline_dgraph) {
    scene_remove_all_bases(scene_cow);
  }
}

void scene_setup_view_layers_after_remap(const DGraph *dgraph,
                                         const IdNode *id_node,
                                         Scene *scene_cow)
{
  const ViewLayer *view_layer_orig = get_original_view_layer(dgraph, id_node);
  ViewLayer *view_layer_eval = reinterpret_cast<ViewLayer *>(scene_cow->view_layers.first);
  view_layer_update_orig_base_ptrs(view_layer_orig, view_layer_eval);
  view_layer_remove_disabled_bases(dgraph, scene_cow, view_layer_eval);
  /* TODO: Remove objects from collections as well.
   * Not a HUGE deal for now, nobody is looking into those CURRENTLY.
   * Still not an excuse to have those. */
}

/* Check whether given id is expanded or still a shallow copy. */
inline bool check_datablock_expanded(const Id *id_cow)
{
  return (id_cow->name[0] != '\0');
}

/* Callback for dune_lib_foreach_id_link which remaps original id pointer
 * with the one created by CoW system. */

struct RemapCbUserData {
  /* Dependency graph for which remapping is happening. */
  const DGraph *dGraph;
};

int foreach_libblock_remap_cb(LibIdLinkCbData *cb_data)
{
  Id **id_p = cb_data->id_pyr;
  if (*id_p == nullptr) {
    return IDWALK_RET_NOP;
  }

  RemapCbUserData *user_data = (RemapCbUserData *)cb_data->user_data;
  const DGraph *dgraph = user_data->dgraph;
  Id *id_orig = *id_p;
  if (dgraph_copy_on_write_is_needed(id_orig)) {
    Id *id_cow = dgraph->get_cow_id(id_orig);
    lib_assert(id_cow != nullptr);
    DGRAPH_COW_PRINT(
        "    Remapping datablock for %s: id_orig=%p id_cow=%p\n", id_orig->name, id_orig, id_cow);
    *id_p = id_cow;
  }
  return IDWALK_RET_NOP;
}

void update_armature_edit_mode_ptrs(const DGraph * /*dgraph*/,
                                        const Id *id_orig,
                                        Id *id_cow)
{
  const DArmature *armature_orig = (const DArmature *)id_orig;
  DArmature *armature_cow = (DArmature *)id_cow;
  armature_cow->edbo = armature_orig->edbo;
  armature_cow->act_edbone = armature_orig->act_edbone;
}

void update_curve_edit_mode_ptrs(const DGraph * /*dgraph*/,
                                 const Id *id_orig,
                                 Id *id_cow)
{
  const Curve *curve_orig = (const Curve *)id_orig;
  Curve *curve_cow = (Curve *)id_cow;
  curve_cow->editnurb = curve_orig->editnurb;
  curve_cow->editfont = curve_orig->editfont;
}

void update_mball_edit_mode_ptrs(const DGraph * /*dgraph*/,
                                     const Id *id_orig,
                                     Id *id_cow)
{
  const MetaBall *mball_orig = (const MetaBall *)id_orig;
  MetaBall *mball_cow = (MetaBall *)id_cow;
  mball_cow->editelems = mball_orig->editelems;
}

void update_lattice_edit_mode_ptrs(const DGraph * /*depsgraph*/,
                                       const Id *id_orig,
                                       Id *id_cow)
{
  const Lattice *lt_orig = (const Lattice *)id_orig;
  Lattice *lt_cow = (Lattice *)id_cow;
  lt_cow->editlatt = lt_orig->editlatt;
}

void update_mesh_edit_mode_pointers(const Id *id_orig, Id *id_cow)
{
  const Mesh *mesh_orig = (const Mesh *)id_orig;
  Mesh *mesh_cow = (Mesh *)id_cow;
  if (mesh_orig->edit_mesh == nullptr) {
    return;
  }
  mesh_cow->edit_mesh = mesh_orig->edit_mesh;
}

/* Edit data is stored and owned by original datablocks, copied ones
 * are simply referencing to them. */
void update_edit_mode_pointers(const Depsgraph *depsgraph, const ID *id_orig, ID *id_cow)
{
  const ID_Type type = GS(id_orig->name);
  switch (type) {
    case ID_AR:
      update_armature_edit_mode_pointers(depsgraph, id_orig, id_cow);
      break;
    case ID_ME:
      update_mesh_edit_mode_pointers(id_orig, id_cow);
      break;
    case ID_CU_LEGACY:
      update_curve_edit_mode_pointers(depsgraph, id_orig, id_cow);
      break;
    case ID_MB:
      update_mball_edit_mode_pointers(depsgraph, id_orig, id_cow);
      break;
    case ID_LT:
      update_lattice_edit_mode_pointers(depsgraph, id_orig, id_cow);
      break;
    default:
      break;
  }
}

template<typename T>
void update_list_orig_ptrs(const ListBase *listbase_orig,
                               ListBase *listbase,
                               T *T::*orig_field)
{
  T *element_orig = reinterpret_cast<T *>(listbase_orig->first);
  T *element_cow = reinterpret_cast<T *>(listbase->first);

  /* Both lists should have the same number of elements, so the check on
   * `element_cow` is just to prevent a crash if this is not the case. */
  while (element_orig != nullptr && element_cow != nullptr) {
    element_cow->*orig_field = element_orig;
    element_cow = element_cow->next;
    element_orig = element_orig->next;
  }

  lib_assert((element_orig == nullptr && element_cow == nullptr) ||
             !"list of pointers of different sizes, unable to reliably set orig pointer");
}

void update_particle_system_orig_ptrs(const Object *object_orig, Object *object_cow)
{
  update_list_orig_ptrs(
      &object_orig->particlesystem, &object_cow->particlesystem, &ParticleSystem::orig_psys);
}

void set_particle_system_modifiers_loaded(Object *object_cow)
{
  LISTBASE_FOREACH (ModifierData *, md, &object_cow->modifiers) {
    if (md->type != eModifierType_ParticleSystem) {
      continue;
    }
    ParticleSystemModifierData *psmd = reinterpret_cast<ParticleSystemModifierData *>(md);
    psmd->flag |= eParticleSystemFlag_file_loaded;
  }
}

void reset_particle_system_edit_eval(const DGraph *dgraph, Object *object_cow)
{
  /* Inactive (and render) dependency graphs are living in their own little bubble, should not care
   * about edit mode at all. */
  if (!dgraph_is_active(reinterpret_cast<const ::DGraph *>(dgraph))) {
    return;
  }
  LISTBASE_FOREACH (ParticleSystem *, psys, &object_cow->particlesystem) {
    ParticleSystem *orig_psys = psys->orig_psys;
    if (orig_psys->edit != nullptr) {
      orig_psys->edit->psys_eval = nullptr;
      orig_psys->edit->psmd_eval = nullptr;
    }
  }
}

void update_particles_after_copy(const DGraph *dgraph,
                                 const Object *object_orig,
                                 Object *object_cow)
{
  update_particle_system_orig_ptrs(object_orig, object_cow);
  set_particle_system_modifiers_loaded(object_cow);
  reset_particle_system_edit_eval(dgraph, object_cow);
}

void update_pose_orig_ptrs(const DPose *pose_orig, DPose *pose_cow)
{
  update_list_orig_ptrs(&pose_orig->chanbase, &pose_cow->chanbase, &DPoseChannel::orig_pchan);
}

void update_nla_strips_orig_ptrs(const ListBase *strips_orig, ListBase *strips_cow)
{
  NlaStrip *strip_orig = reinterpret_cast<NlaStrip *>(strips_orig->first);
  NlaStrip *strip_cow = reinterpret_cast<NlaStrip *>(strips_cow->first);
  while (strip_orig != nullptr) {
    strip_cow->orig_strip = strip_orig;
    update_nla_strips_orig_ptrs(&strip_orig->strips, &strip_cow->strips);
    strip_cow = strip_cow->next;
    strip_orig = strip_orig->next;
  }
}

void update_nla_tracks_orig_ptrs(const ListBase *tracks_orig, ListBase *tracks_cow)
{
  NlaTrack *track_orig = reinterpret_cast<NlaTrack *>(tracks_orig->first);
  NlaTrack *track_cow = reinterpret_cast<NlaTrack *>(tracks_cow->first);
  while (track_orig != nullptr) {
    update_nla_strips_orig_pointers(&track_orig->strips, &track_cow->strips);
    track_cow = track_cow->next;
    track_orig = track_orig->next;
  }
}

void update_animation_data_after_copy(const Id *id_orig, Id *id_cow)
{
  const AnimData *anim_data_orig = dune_animdata_from_id(const_cast<Id *>(id_orig));
  if (anim_data_orig == nullptr) {
    return;
  }
  AnimData *anim_data_cow = dune_animdata_from_id(id_cow);
  lib_assert(anim_data_cow != nullptr);
  update_nla_tracks_orig_pointers(&anim_data_orig->nla_tracks, &anim_data_cow->nla_tracks);
}

/* Do some special treatment of data transfer from original id to its
 * CoW complementary part.
 *
 * Only use for the newly created CoW data-blocks. */
void update_id_after_copy(const DGraph *dgraph,
                          const IdNode *id_node,
                          const Id *id_orig,
                          Id *id_cow)
{
  const IdType type = GS(id_orig->name);
  update_animation_data_after_copy(id_orig, id_cow);
  switch (type) {
    case ID_OB: {
      /* Ensure we don't drag someone's else derived mesh to the
       * new copy of the object. */
      Object *object_cow = (Object *)id_cow;
      const Object *object_orig = (const Object *)id_orig;
      object_cow->mode = object_orig->mode;
      object_cow->sculpt = object_orig->sculpt;
      object_cow->runtime.data_orig = (Id *)object_cow->data;
      if (object_cow->type == OB_ARMATURE) {
        const DArmature *armature_orig = (DArmature *)object_orig->data;
        DArmature *armature_cow = (DArmature *)object_cow->data;
        dune_pose_remap_bone_ptrs(armature_cow, object_cow->pose);
        if (armature_orig->edbo == nullptr) {
          update_pose_orig_pointers(object_orig->pose, object_cow->pose);
        }
        dune_pose_pchan_index_rebuild(object_cow->pose);
      }
      update_particles_after_copy(dgraph, object_orig, object_cow);
      break;
    }
    case ID_SCE: {
      Scene *scene_cow = (Scene *)id_cow;
      const Scene *scene_orig = (const Scene *)id_orig;
      scene_cow->toolsettings = scene_orig->toolsettings;
      scene_cow->eevee.light_cache_data = scene_orig->eevee.light_cache_data;
      scene_setup_view_layers_after_remap(depsgraph, id_node, reinterpret_cast<Scene *>(id_cow));
      break;
    }
    /* FIXME: This is a temporary fix to update the runtime pointers properly, see #96216. Should
     * be removed at some point. */
    case ID_GD: {
      DPenData *dpd_cow = (DPenData *)id_cow;
      DPenDataLayer *dpl = (DPenDataLayer *)(dpd_cow->layers.first);
      if (dpl != nullptr && dpl->runtime.dpl_orig == nullptr) {
        dune_dpen_data_update_orig_ptrs((DPenData *)id_orig, dpd_cow);
      }
      break;
    }
    default:
      break;
  }
  update_edit_mode_ptrs(dgraph, id_orig, id_cow);
  dune_animsys_update_driver_array(id_cow);
}

/* This callback is used to validate that all nested id data-blocks are
 * properly expanded. */
int foreach_libblock_validate_callback(LibIdLinkCbData *cb_data)
{
  ValidateData *data = (ValidateData *)cb_data->user_data;
  Id **id_p = cb_data->id_ptr;

  if (*id_p != nullptr) {
    if (!check_datablock_expanded(*id_p)) {
      data->is_valid = false;
      /* TODO: Store which is not valid? */
    }
  }
  return IDWALK_RET_NOP;
}

/* Actual implementation of logic which "expands" all the data which was not
 * yet copied-on-write.
 *
 * NOTE: Expects that CoW datablock is empty. */
Id *dgraph_expand_copy_on_write_datablock(const DGraph *dgraph, const IdNode *id_node)
{
  const Id *id_orig = id_node->id_orig;
  Id *id_cow = id_node->id_cow;
  const int id_cow_recalc = id_cow->recalc;

  /* No need to expand such datablocks, their copied id is same as original
   * one already. */
  if (!dgraph_copy_on_write_is_needed(id_orig)) {
    return id_cow;
  }

  DEG_COW_PRINT(
      "Expanding datablock for %s: id_orig=%p id_cow=%p\n", id_orig->name, id_orig, id_cow);

  /* Sanity checks. */
  lib_assert(check_datablock_expanded(id_cow) == false);
  lib_assert(id_cow->py_instance == nullptr);

  /* Copy data from original id to a copied version. */
  /* TODO: Avoid doing full id copy somehow, make Mesh to reference
   * original geometry arrays for until those are modified. */
  /* TODO: We do some trickery with temp bmain and extra id pointer
   * just to be able to use existing API. Ideally we need to replace this with
   * in-place copy from existing datablock to a prepared memory.
   *
   * NOTE: We don't use dune_main_{new,free} because:
   * - We don't want heap-allocations here.
   * - We don't want bmain's content to be freed when main is freed. */
  bool done = false;
  /* First we handle special cases which are not covered by BKE_id_copy() yet.
   * or cases where we want to do something smarter than simple datablock
   * copy. */
  const IdType id_type = GS(id_orig->name);
  switch (id_type) {
    case ID_SCE: {
      done = scene_copy_inplace_no_main((Scene *)id_orig, (Scene *)id_cow);
      if (done) {
        /* NOTE: This is important to do before remap, because this
         * function will make it so less ids are to be remapped. */
        scene_setup_view_layers_before_remap(dgraph, id_node, (Scene *)id_cow);
      }
      break;
    }
    case ID_ME: {
      /* TODO: Ideally we want to handle meshes in a special
       * manner here to avoid initial copy of all the geometry arrays. */
      break;
    }
    default:
      break;
  }
  if (!done) {
    done = id_copy_inplace_no_main(id_orig, id_cow);
  }
  if (!done) {
    lib_assert_msg(0, "No idea how to perform CoW on datablock");
  }
  /* Update pointers to nested id datablocks. */
  DGRAPH_COW_PRINT(
      "  Remapping id links for %s: id_orig=%p id_cow=%p\n", id_orig->name, id_orig, id_cow);

#ifdef NESTED_ID_NASTY_WORKAROUND
  ntree_hack_remap_ptrs(dgraph, id_cow);
#endif
  /* Do it now, so remapping will understand that possibly remapped self id
   * is not to be remapped again. */
  dgraph_tag_copy_on_write_id(id_cow, id_orig);
  /* Perform remapping of the nodes. */
  RemapCbUserData user_data = {nullptr};
  user_data.dgraph = dgraph;
  dune_lib_foreach_id_link(nullptr,
                              id_cow,
                              foreach_libblock_remap_cb,
                              (void *)&user_data,
                              IDWALK_IGNORE_EMBEDDED_ID);
  /* Correct or tweak some pointers which are not taken care by foreach
   * from above. */
  update_id_after_copy(dgraph, id_node, id_orig, id_cow);
  id_cow->recalc = id_cow_recalc;
  return id_cow;
}

}  // namespace

Id *dgraph_update_copy_on_write_datablock(const DGraph *dgraph, const IdNode *id_node)
{
  const Id *id_orig = id_node->id_orig;
  Id *id_cow = id_node->id_cow;
  /* Similar to expansion, no need to do anything here. */
  if (!dgraph_copy_on_write_is_needed(id_orig)) {
    return id_cow;
  }

  /* When updating object data in edit-mode, don't request COW update since this will duplicate
   * all object data which is unnecessary when the edit-mode data is used for calculating
   * modifiers.
   *
   * TODO: Investigate modes besides edit-mode. */
  if (check_datablock_expanded(id_cow) && !id_node->is_cow_explicitly_tagged) {
    const IdType id_type = GS(id_orig->name);
    /* Pass nullptr as the object is only needed for Curves which do not have edit mode pointers.
     */
    if (OB_DATA_SUPPORT_EDITMODE(id_type) && dune_object_data_is_in_editmode(nullptr, id_orig)) {
      /* Make sure pointers in the edit mode data are updated in the copy.
       * This allows dgraph to pick up changes made in another context after it has been
       * evaluated. Consider the following scenario:
       *
       *  - ObjectA in SceneA is using Mesh.
       *  - ObjectB in SceneB is using Mesh (same exact datablock).
       *  - Depsgraph of SceneA is evaluated.
       *  - Depsgraph of SceneB is evaluated.
       *  - User enters edit mode of ObjectA in SceneA. */
      update_edit_mode_ptrs(dgraph, id_orig, id_cow);
      return id_cow;
    }
    /* In case we don't need to do a copy-on-write, we can use the update cache of the grease
     * pencil data to do an update-on-write. */
    if (id_type == ID_GD && BKE_gpencil_can_avoid_full_copy_on_write(
                                (const ::Depsgraph *)depsgraph, (bGPdata *)id_orig)) {
      dune_dpen_update_on_write((bGPdata *)id_orig, (bGPdata *)id_cow);
      return id_cow;
    }
  }

  RuntimeBackup backup(dgraph);
  backup.init_from_id(id_cow);
  dgraph_free_copy_on_write_datablock(id_cow);
  dgraph_expand_copy_on_write_datablock(dgraph, id_node);
  backup.restore_to_id(id_cow);
  return id_cow;
}

/**
 * dgraph is supposed to have ID node already.
 */
Id *dgraph_update_copy_on_write_datablock(const DGraph *dgraph, Id *id_orig)
{
  IdNode *id_node = dgraph->find_id_node(id_orig);
  lib_assert(id_node != nullptr);
  return dgraph_update_copy_on_write_datablock(dgraph, id_node);
}

namespace {

void discard_armature_edit_mode_pointers(Id *id_cow)
{
  DArmature *armature_cow = (DArmature *)id_cow;
  armature_cow->edbo = nullptr;
}

void discard_curve_edit_mode_pointers(Id *id_cow)
{
  Curve *curve_cow = (Curve *)id_cow;
  curve_cow->editnurb = nullptr;
  curve_cow->editfont = nullptr;
}

void discard_mball_edit_mode_pointers(ID *id_cow)
{
  MetaBall *mball_cow = (MetaBall *)id_cow;
  mball_cow->editelems = nullptr;
}

void discard_lattice_edit_mode_pointers(ID *id_cow)
{
  Lattice *lt_cow = (Lattice *)id_cow;
  lt_cow->editlatt = nullptr;
}

void discard_mesh_edit_mode_pointers(ID *id_cow)
{
  Mesh *mesh_cow = (Mesh *)id_cow;
  mesh_cow->edit_mesh = nullptr;
}

void discard_scene_pointers(ID *id_cow)
{
  Scene *scene_cow = (Scene *)id_cow;
  scene_cow->toolsettings = nullptr;
  scene_cow->eevee.light_cache_data = nullptr;
}

/* nullptr-ify all edit mode pointers which points to data from
 * original object. */
void discard_edit_mode_pointers(ID *id_cow)
{
  const ID_Type type = GS(id_cow->name);
  switch (type) {
    case ID_AR:
      discard_armature_edit_mode_pointers(id_cow);
      break;
    case ID_ME:
      discard_mesh_edit_mode_pointers(id_cow);
      break;
    case ID_CU_LEGACY:
      discard_curve_edit_mode_pointers(id_cow);
      break;
    case ID_MB:
      discard_mball_edit_mode_pointers(id_cow);
      break;
    case ID_LT:
      discard_lattice_edit_mode_pointers(id_cow);
      break;
    case ID_SCE:
      /* Not really edit mode but still needs to run before
       * dune_libblock_free_datablock() */
      discard_scene_pointers(id_cow);
      break;
    default:
      break;
  }
}

}  // namespace

/**
 *  Free content of the CoW data-block.
 * Notes:
 * - Does not recurse into nested ID data-blocks.
 * - Does not free data-block itself.
 */
void deg_free_copy_on_write_datablock(ID *id_cow)
{
  if (!check_datablock_expanded(id_cow)) {
    /* Actual content was never copied on top of CoW block, we have
     * nothing to free. */
    return;
  }
  const ID_Type type = GS(id_cow->name);
#ifdef NESTED_ID_NASTY_WORKAROUND
  nested_id_hack_discard_pointers(id_cow);
#endif
  switch (type) {
    case ID_OB: {
      /* TODO(sergey): This workaround is only to prevent free derived
       * caches from modifying object->data. This is currently happening
       * due to mesh/curve data-block bound-box tagging dirty. */
      Object *ob_cow = (Object *)id_cow;
      ob_cow->data = nullptr;
      ob_cow->sculpt = nullptr;
      break;
    }
    default:
      break;
  }
  discard_edit_mode_pointers(id_cow);
  dune_libblock_free_data_py(id_cow);
  dune_libblock_free_datablock(id_cow, 0);
  dune_libblock_free_data(id_cow, false);
  /* Signal datablock as not being expanded. */
  id_cow->name[0] = '\0';
}

void dgraph_evaluate_copy_on_write(struct ::DGraph *graph, const IdNode *id_node)
{
  const DGraph *dgraph = reinterpret_cast<const DGraph *>(graph);
  dgraph_debug_print_eval(graph, __func__, id_node->id_orig->name, id_node->id_cow);
  if (id_node->id_orig == &dgraph->scene->id) {
    /* NOTE: This is handled by eval_ctx setup routines, which
     * ensures scene and view layer pointers are valid. */
    return;
  }
  dgraph_update_copy_on_write_datablock(dgraph, id_node);
}

bool dgraph_validate_copy_on_write_datablock(Id *id_cow)
{
  if (id_cow == nullptr) {
    return false;
  }
  ValidateData data;
  data.is_valid = true;
  dune_lib_foreach_id_link(
      nullptr, id_cow, foreach_libblock_validate_cb, &data, IDWALK_NOP);
  return data.is_valid;
}

void dgraph_tag_copy_on_write_id(If *id_cow, const Id *id_orig)
{
  lib_assert(id_cow != id_orig);
  lib_assert((id_orig->tag & LIB_TAG_COPIED_ON_WRITE) == 0);
  id_cow->tag |= LIB_TAG_COPIED_ON_WRITE;
  /* This ID is no longer localized, is a self-sustaining copy now. */
  id_cow->tag &= ~LIB_TAG_LOCALIZED;
  id_cow->orig_id = (ID *)id_orig;
}

bool deg_copy_on_write_is_expanded(const ID *id_cow)
{
  return check_datablock_expanded(id_cow);
}

bool deg_copy_on_write_is_needed(const ID *id_orig)
{
  const ID_Type id_type = GS(id_orig->name);
  return deg_copy_on_write_is_needed(id_type);
}

bool deg_copy_on_write_is_needed(const ID_Type id_type)
{
  return ID_TYPE_IS_COW(id_type);
}

}  // namespace blender::deg
