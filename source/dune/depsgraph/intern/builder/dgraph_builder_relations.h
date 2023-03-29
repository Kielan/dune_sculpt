#pragma once

#include <cstdio>
#include <cstring>

#include "intern/dgraph_type.h"

#include "types_id.h"

#include "api_path.h"

#include "lib_string.h"
#include "lib_utildefines.h"

#include "intern/builder/dgraph_builder.h"
#include "intern/builder/dgraph_builder_key.h"
#include "intern/builder/dgraph_builder_map.h"
#include "intern/builder/dgraph_builder_api.h"
#include "intern/builder/dgraph_builder_stack.h"
#include "intern/dgraph.h"
#include "intern/node/node.h"
#include "intern/node/node_component.h"
#include "intern/node/node_id.h"
#include "intern/node/node_operation.h"

struct CacheFile;
struct Camera;
struct Collection;
struct EffectorWeights;
struct FCurve;
struct FreestyleLineSet;
struct FreestyleLineStyle;
struct Id;
struct IdProp;
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
struct ParticleSystem;
struct Scene;
struct Simulation;
struct Speaker;
struct Tex;
struct VFont;
struct ViewLayer;
struct World;
struct Action;
struct Armature;
struct Constraint;
struct NodeSocket;
struct NodeTree;
struct PoseChannel;
struct Sound;

namespace dune::dgraph {

struct ComponentNode;
struct NodeHandle;
struct DGraph;
class DGraphBuilderCache;
struct IdNode;
struct Node;
struct OpNode;
struct Relation;
struct RootPChanMap;
struct TimeSourceNode;

class DGraphRelationBuilder : public DGraphBuilder {
 public:
  DGraphRelationBuilder(Main *dmain, DGraph *graph, DGraphBuilderCache *cache);

  void begin_build();

  template<typename KeyFrom, typename KeyTo>
  Relation *add_relation(const KeyFrom &key_from,
                         const KeyTo &key_to,
                         const char *description,
                         int flags = 0);

  template<typename KeyTo>
  Relation *add_relation(const TimeSourceKey &key_from,
                         const KeyTo &key_to,
                         const char *description,
                         int flags = 0);

  template<typename KeyType>
  Relation *add_node_handle_relation(const KeyType &key_from,
                                     const DNodeHandle *handle,
                                     const char *description,
                                     int flags = 0);

  template<typename KeyTo>
  Relation *add_depends_on_transform_relation(Id *id,
                                              const KeyTo &key_to,
                                              const char *description,
                                              int flags = 0);

  /* Adds relation from proper transformation operation to the modifier.
   * Takes care of checking for possible physics solvers modifying position
   * of this object. */
  void add_depends_on_transform_relation(const DNodeHandle *handle, const char *description);

  void add_customdata_mask(Object *object, const DGraphCustomDataMeshMasks &customdata_masks);
  void add_special_eval_flag(Id *id, uint32_t flag);

  virtual void build_id(Id *id);

  /* Build function for id types that do not need their own build_xxx() function. */
  virtual void build_generic_id(Id *id);

  virtual void build_idprops(IdProp *id_prop);

  virtual void build_scene_render(Scene *scene, ViewLayer *view_layer);
  virtual void build_scene_params(Scene *scene);
  virtual void build_scene_compositor(Scene *scene);

  virtual void build_layer_collections(ListBase *lb);
  virtual void build_view_layer(Scene *scene,
                                ViewLayer *view_layer,
                                eDGraphNode_LinkedState_Type linked_state);
  virtual void build_collection(LayerCollection *from_layer_collection,
                                Object *object,
                                Collection *collection);
  virtual void build_object(Object *object);
  virtual void build_object_from_view_layer_base(Object *object);
  virtual void build_object_layer_component_relations(Object *object);
  virtual void build_object_modifiers(Object *object);
  virtual void build_object_data(Object *object);
  virtual void build_object_data_camera(Object *object);
  virtual void build_object_data_geometry(Object *object);
  virtual void build_object_data_geometry_datablock(Id *obdata);
  virtual void build_object_data_light(Object *object);
  virtual void build_object_data_lightprobe(Object *object);
  virtual void build_object_data_speaker(Object *object);
  virtual void build_object_parent(Object *object);
  virtual void build_object_pointcache(Object *object);
  virtual void build_constraints(Id *id,
                                 NodeType component_type,
                                 const char *component_subdata,
                                 ListBase *constraints,
                                 RootPChanMap *root_map);
  virtual void build_animdata(Id *id);
  virtual void build_animdata_curves(Id *id);
  virtual void build_animdata_curves_targets(Id *id,
                                             ComponentKey &adt_key,
                                             OpNode *op_from,
                                             ListBase *curves);
  virtual void build_animdata_nlastrip_targets(Id *id,
                                               ComponentKey &adt_key,
                                               OpNode *op_from,
                                               ListBase *strips);
  virtual void build_animdata_drivers(Id *id);
  virtual void build_animdata_force(Id *id);
  virtual void build_animation_images(Id *id);
  virtual void build_action(DAction *action);
  virtual void build_driver(Id *id, FCurve *fcurve);
  virtual void build_driver_data(Id *id, FCurve *fcurve);
  virtual void build_driver_variables(Id *id, FCurve *fcurve);
  virtual void build_driver_id_prop(Id *id, const char *api_path);
  virtual void build_params(Id *id);
  virtual void build_dimensions(Object *object);
  virtual void build_world(World *world);
  virtual void build_rigidbody(Scene *scene);
  virtual void build_particle_systems(Object *object);
  virtual void build_particle_settings(ParticleSettings *part);
  virtual void build_particle_system_visualization_object(Object *object,
                                                          ParticleSystem *psys,
                                                          Object *draw_object);
  virtual void build_ik_pose(Object *object,
                             PoseChannel *pchan,
                             Constraint *con,
                             RootPChanMap *root_map);
  virtual void build_splineik_pose(Object *object,
                                   PoseChannel *pchan,
                                   Constraint *con,
                                   RootPChanMap *root_map);
  virtual void build_inter_ik_chains(Object *object,
                                     const OpKey &solver_key,
                                     const PoseChannel *rootchan,
                                     const RootPChanMap *root_map);
  virtual void build_rig(Object *object);
  virtual void build_shapekeys(Key *key);
  virtual void build_armature(Armature *armature);
  virtual void build_armature_bones(ListBase *bones);
  virtual void build_camera(Camera *camera);
  virtual void build_light(Light *lamp);
  virtual void build_nodetree(NodeTree *ntree);
  virtual void build_nodetree_socket(NodeSocket *socket);
  virtual void build_material(Material *ma);
  virtual void build_materials(Material **materials, int num_materials);
  virtual void build_freestyle_lineset(FreestyleLineSet *fls);
  virtual void build_freestyle_linestyle(FreestyleLineStyle *linestyle);
  virtual void build_texture(Tex *tex);
  virtual void build_image(Image *image);
  virtual void build_cachefile(CacheFile *cache_file);
  virtual void build_mask(Mask *mask);
  virtual void build_movieclip(MovieClip *clip);
  virtual void build_lightprobe(LightProbe *probe);
  virtual void build_speaker(Speaker *speaker);
  virtual void build_sound(Sound *sound);
  virtual void build_simulation(Simulation *simulation);
  virtual void build_scene_sequencer(Scene *scene);
  virtual void build_scene_audio(Scene *scene);
  virtual void build_scene_speakers(Scene *scene, ViewLayer *view_layer);
  virtual void build_vfont(VFont *vfont);

  virtual void build_nested_datablock(Id *owner, Id *id, bool flush_cow_changes);
  virtual void build_nested_nodetree(Id *owner, NodeTree *ntree);
  virtual void build_nested_shapekey(Id *owner, Key *key);

  void add_particle_collision_relations(const OpKey &key,
                                        Object *object,
                                        Collection *collection,
                                        const char *name);
  void add_particle_forcefield_relations(const OpKey &key,
                                         Object *object,
                                         ParticleSystem *psys,
                                         EffectorWeights *eff,
                                         bool add_absorption,
                                         const char *name);

  virtual void build_copy_on_write_relations();
  virtual void build_copy_on_write_relations(IdNode *id_node);
  virtual void build_driver_relations();
  virtual void build_driver_relations(IdNode *id_node);

  template<typename KeyType> OpNode *find_op_node(const KeyType &key);

  DGraph *getGraph();

 protected:
  TimeSourceNode *get_node(const TimeSourceKey &key) const;
  ComponentNode *get_node(const ComponentKey &key) const;
  OpNode *get_node(const OpKey &key) const;
  Node *get_node(const ApiPathKey &key);

  OpNode *find_node(const OpKey &key) const;
  bool has_node(const OpKey &key) const;

  Relation *add_time_relation(TimeSourceNode *timesrc,
                              Node *node_to,
                              const char *description,
                              int flags = 0);

  /* Add relation which ensures visibility of `id_from` when `id_to` is visible.
   * For the more detailed explanation see comment for `NodeType::VISIBILITY`. */
  void add_visibility_relation(Id *id_from, Id *id_to);

  Relation *add_op_relation(OpNode *node_from,
                            OpNode *node_to,
                            const char *description,
                            int flags = 0);

  template<typename KeyType>
  NodeHandle create_node_handle(const KeyType &key, const char *default_name = "");

  /* TODO: All those is_same* functions are to be generalized. */

  /* Check whether two keys corresponds to the same bone from same armature.
   *
   * This is used by drivers relations builder to avoid possible fake
   * dependency cycle when one bone property drives another property of the
   * same bone. */
  template<typename KeyFrom, typename KeyTo>
  bool is_same_bone_dependency(const KeyFrom &key_from, const KeyTo &key_to);

  /* Similar to above, but used to check whether driver is using node from
   * the same node tree as a driver variable. */
  template<typename KeyFrom, typename KeyTo>
  bool is_same_nodetree_node_dependency(const KeyFrom &key_from, const KeyTo &key_to);

 private:
  struct BuilderWalkUserData {
    GraphRelationBuilder *builder;
  };

  static void modifier_walk(void *user_data,
                            struct Object *object,
                            struct Id **idpoint,
                            int cb_flag);

  static void constraint_walk(Constraint *con, Id **idpoint, bool is_ref, void *user_data);

  /* State which demotes currently built entities. */
  Scene *scene_;

  BuilderMap built_map_;
  ApiNodeQuery api_node_query_;
  BuilderStack stack_;
};

struct DNodeHandle {
  NodeHandle(DGraphRelationBuilder *builder,
             OpNode *node,
             const char *default_name = "")
      : builder(builder), node(node), default_name(default_name)
  {
    lib_assert(node != nullptr);
  }

  DGraphRelationBuilder *builder;
  OpNode *node;
  const char *default_name;
};

}  // namespace dune::dgraph

#include "intern/builder/dgraph_builder_relations_impl.h"
