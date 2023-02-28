#pragma once

#include "intern/builder/dgraph_builder.h"
#include "intern/builder/dgraph_builder_key.h"
#include "intern/builder/dgraph_builder_map.h"
#include "intern/dgraph_type.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_op.h"

#include "dgraph.h"

struct CacheFile;
struct Camera;
struct Collection;
struct FCurve;
struct FreestyleLineSet;
struct FreestyleLineStyle;
struct Id;
struct IdPropl;
struct Image;
struct Key;
struct LayerCollection;
struct Light;
struct LightProbe;
struct ListBase;
struct Main;
struct Mask;
struct Material;
struct MovieClip;
struct Object;
struct ParticleSettings;
struct Scene;
struct Simulation;
struct Speaker;
struct Tex;
struct VFont;
struct World;
struct DAction;
struct DArmature;
struct DConstraint;
struct DNodeSocket;
struct DNodeTree;
struct DPoseChannel;
struct DSound;

namespace dune::dgraph {

struct ComponentNode;
struct DGraph;
class DGraphBuilderCache;
struct IdNode;
struct OpKey;
struct OpNode;
struct TimeSourceNode;

class DGraphNodeBuilder : public DGraphBuilder {
 public:
  DepsgraphNodeBuilder(Main *Dmain, DGraph *graph, DGraphBuilderCache *cache);
  ~DepsgraphNodeBuilder();

  /* For given original Id get Id which is created by CoW system. */
  Id *get_cow_id(const Id *id_orig) const;
  /* Similar to above, but for the cases when there is no ID node we create
   * one. */
  Id *ensure_cow_id(Id *id_orig);

  /* Helper wrapper function which wraps get_cow_id with a needed type cast. */
  template<typename T> T *get_cow_datablock(const T *orig) const
  {
    return (T *)get_cow_id(&orig->id);
  }

  /* For a given COW datablock get corresponding original one. */
  template<typename T> T *get_orig_datablock(const T *cow) const
  {
    return (T *)cow->id.orig_id;
  }

  virtual void begin_build();
  virtual void end_build();

  /**
   * `id_cow_self` is the user of `id_ptr`,
   * see also `LibIdLinkCbData` struct definition.
   */
  int foreach_id_cow_detect_need_for_update_cb(Id *id_cow_self, Id *id_ptr);

  IdNode *add_id_node(Id *id);
  IdNode *find_id_node(const Id *id);
  TimeSourceNode *add_time_source();

  ComponentNode *add_component_node(Id *id, NodeType comp_type, const char *comp_name = "");
  ComponentNode *find_component_node(const Id *id, NodeType comp_type, const char *comp_name = "");

  OpNode *add_op_node(ComponentNode *comp_node,
                                    OpCode opcode,
                                    const DepsEvalOpCb &op = nullptr,
                                    const char *name = "",
                                    int name_tag = -1);
  OpNode *add_op_node(Id *id,
                                    NodeType comp_type,
                                    const char *comp_name,
                                    OpCode opcode,
                                    const DepsEvalOpCb &op = nullptr,
                                    const char *name = "",
                                    int name_tag = -1);
  OpNode *add_op_node(Id *id,
                                    NodeType comp_type,
                                    OpCode opcode,
                                    const DepsEvalOpCb &op = nullptr,
                                    const char *name = "",
                                    int name_tag = -1);

  OpNode *ensure_op_node(Id *id,
                                       NodeType comp_type,
                                       const char *comp_name,
                                       OperationCode opcode,
                                       const DepsEvalOpCb &op = nullptr,
                                       const char *name = "",
                                       int name_tag = -1);
  OperationNode *ensure_op_node(Id *id,
                                NodeType comp_type,
                                OpCode opcode,
                                const DepsEvalOpCb &op = nullptr,
                                const char *name = "",
                                int name_tag = -1);

  bool has_op_node(Id *id,
                   NodeType comp_type,
                   const char *comp_name,
                   OpCode opcode,
                   const char *name = "",
                   int name_tag = -1);

  OpNode *find_op_node(const Id *id,
                       NodeType comp_type,
                       const char *comp_name,
                       OpCode opcode,
                       const char *name = "",
                       int name_tag = -1);

  OpNode *find_op_node(const Id *id,
                       NodeType comp_type,
                       OpCode opcode,
                       const char *name = "",
                       int name_tag = -1);

  OpNode *find_op_node(const OpKey &key);

  virtual void build_id(Id *id);

  /* Build function for ID types that do not need their own build_xxx() function. */
  virtual void build_generic_id(Id *id);

  virtual void build_idprops(IdProp *id_prop);

  virtual void build_scene_render(Scene *scene, ViewLayer *view_layer);
  virtual void build_scene_params(Scene *scene);
  virtual void build_scene_compositor(Scene *scene);

  virtual void build_layer_collections(ListBase *lb);
  virtual void build_view_layer(Scene *scene,
                                ViewLayer *view_layer,
                                eDepsNode_LinkedState_Type linked_state);
  virtual void build_collection(LayerCollection *from_layer_collection, Collection *collection);
  virtual void build_object(int base_index,
                            Object *object,
                            eDepsNode_LinkedState_Type linked_state,
                            bool is_visible);
  virtual void build_object_instance_collection(Object *object, bool is_object_visible);
  virtual void build_object_from_layer(int base_index,
                                       Object *object,
                                       eDepsNode_LinkedState_Type linked_state);
  virtual void build_object_flags(int base_index,
                                  Object *object,
                                  eDepsNode_LinkedState_Type linked_state);
  virtual void build_object_modifiers(Object *object);
  virtual void build_object_data(Object *object);
  virtual void build_object_data_camera(Object *object);
  virtual void build_object_data_geometry(Object *object);
  virtual void build_object_data_geometry_datablock(ID *obdata);
  virtual void build_object_data_light(Object *object);
  virtual void build_object_data_lightprobe(Object *object);
  virtual void build_object_data_speaker(Object *object);
  virtual void build_object_transform(Object *object);
  virtual void build_object_constraints(Object *object);
  virtual void build_object_pointcache(Object *object);
  virtual void build_pose_constraints(Object *object, bPoseChannel *pchan, int pchan_index);
  virtual void build_rigidbody(Scene *scene);
  virtual void build_particle_systems(Object *object, bool is_object_visible);
  virtual void build_particle_settings(ParticleSettings *part);
  /**
   * Build graph nodes for #AnimData block and any animated images used.
   * \param id: ID-Block which hosts the #AnimData
   */
  virtual void build_animdata(ID *id);
  virtual void build_animdata_nlastrip_targets(ListBase *strips);
  /**
   * Build graph nodes to update the current frame in image users.
   */
  virtual void build_animation_images(ID *id);
  virtual void build_action(bAction *action);
  /**
   * Build graph node(s) for Driver
   * \param id: ID-Block that driver is attached to
   * \param fcurve: Driver-FCurve
   * \param driver_index: Index in animation data drivers list
   */
  virtual void build_driver(Id *id, FCurve *fcurve, int driver_index);
  virtual void build_driver_variables(Id *id, FCurve *fcurve);
  virtual void build_driver_id_prop(Id *id, const char *rna_path);
  virtual void build_params(Id *id);
  virtual void build_dimensions(Object *object);
  virtual void build_ik_pose(Object *object, bPoseChannel *pchan, bConstraint *con);
  virtual void build_splineik_pose(Object *object, bPoseChannel *pchan, bConstraint *con);
  virtual void build_rig(Object *object);
  virtual void build_armature(DArmature *armature);
  virtual void build_armature_bones(ListBase *bones);
  virtual void build_shapekeys(Key *key);
  virtual void build_camera(Camera *camera);
  virtual void build_light(Light *lamp);
  virtual void build_nodetree(DNodeTree *ntree);
  virtual void build_nodetree_socket(bNodeSocket *socket);
  virtual void build_material(Material *ma);
  virtual void build_materials(Material **materials, int num_materials);
  virtual void build_freestyle_lineset(FreestyleLineSet *fls);
  virtual void build_freestyle_linestyle(FreestyleLineStyle *linestyle);
  virtual void build_texture(Tex *tex);
  virtual void build_image(Image *image);
  virtual void build_world(World *world);
  virtual void build_cachefile(CacheFile *cache_file);
  virtual void build_mask(Mask *mask);
  virtual void build_movieclip(MovieClip *clip);
  virtual void build_lightprobe(LightProbe *probe);
  virtual void build_speaker(Speaker *speaker);
  virtual void build_sound(DSound *sound);
  virtual void build_simulation(Simulation *simulation);
  virtual void build_scene_sequencer(Scene *scene);
  virtual void build_scene_audio(Scene *scene);
  virtual void build_scene_speakers(Scene *scene, ViewLayer *view_layer);
  virtual void build_vfont(VFont *vfont);

  /* Per-ID information about what was already in the dependency graph.
   * Allows to re-use certain values, to speed up following evaluation. */
  struct IdInfo {
    /* Copy-on-written pointer of the corresponding ID. */
    Id *id_cow;
    /* Mask of visible components from previous state of the
     * dependency graph. */
    IdComponentsMask previously_visible_components_mask;
    /* Special evaluation flag mask from the previous depsgraph. */
    uint32_t previous_eval_flags;
    /* Mesh CustomData mask from the previous depsgraph. */
    DGraphCustomDataMeshMasks previous_customdata_masks;
  };

 protected:
  /* Entry tags from the previous state of the dependency graph.
   * Stored before the graph is re-created so that they can be transferred over. */
  Vector<PersistentOpKey> saved_entry_tags_;

  struct BuilderWalkUserData {
    DGraphNodeBuilder *builder;
  };
  static void modifier_walk(void *user_data,
                            struct Object *object,
                            struct Id **idpoint,
                            int cb_flag);
  static void constraint_walk(DConstraint *constraint,
                              Id **idpoint,
                              bool is_ref,
                              void *user_data);

  void tag_previously_tagged_nodes();
  /**
   * Check for Ids that need to be flushed (COW-updated)
   * because the depsgraph itself created or removed some of their evaluated dependencies.
   */
  void update_invalid_cow_ptrs();

  /* State which demotes currently built entities. */
  Scene *scene_;
  ViewLayer *view_layer_;
  int view_layer_index_;
  /* NOTE: Collection are possibly built recursively, so be careful when
   * setting the current state. */
  Collection *collection_;
  /* Accumulated flag over the hierarchy of currently building collections.
   * Denotes whether all the hierarchy from parent of `collection_` to the
   * very root is visible (aka not restricted.). */
  bool is_parent_collection_visible_;

  /* Indexed by original Id.session_uuid, values are IDInfo. */
  Map<uint, IdInfo *> id_info_hash_;

  /* Set of IDs which were already build. Makes it easier to keep track of
   * what was already built and what was not. */
  BuilderMap built_map_;
};

}  // namespace dune::deg
