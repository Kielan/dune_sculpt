#pragma once

#include "lib_utildefines.h"

#include "lib_compiler_attrs.h"

#include "api_internal_types.h"

#include "ui_resources.h"

#define API_MAGIC ((int)~0)

struct AssetLibRef;
struct FreestyleSettings;
struct Id;
struct IdOverrideLib;
struct IdOverrideLibPropOp;
struct IdProp;
struct Main;
struct Object;
struct ReportList;
struct SType;
struct ViewLayer;

/* Data structures used during define */

typedef struct ApiContainerDef {
  void *next, *prev;

  ApiContainer *cont;
  List props;
} ApiContainerDef;

typedef struct ApiFnDef { 
  ApiContainerDef cont;
  ApiFn *fn;
  const char *sapi;
  const char *call;
  const char *gencall;
} ApiFnDef;

typedef struct ApiPropDef {
  struct ApiPropDef *next, *prev;
  struct ApiContainer *cont;
  struct ApiProp *prop;

  /* struct */
  const char *stypestructname;
  const char *stypestructfromname;
  const char *stypestructfromprop;

  /* property */
  const char *stypename;
  const char *stype;
  int stylearraylength;
  int stypeptrlevel;
  /**
   * Offset in bytes within `dnastructname`.
   * -1 when unusable (follows pointer for e.g.). */
  int stypeoffset;
  int stypesize;

  /* for finding length of array collections */
  const char *stypelengthstructname;
  const char *stypelengthname;
  int stypelengthfixed;

  int64_t boolbit;
  bool boolnegative;

  /* not to be confused with PROP_ENUM_FLAG
   * this only allows one of the flags to be set at a time, clearing all others */
  int enumbitflags;
} ApiPropDef;

typedef struct ApiStructDef {
  ApiContainerDef cont;
  struct ApiStruct *sapi;
  const char *filename;
  const char *stypename;
  /* for derived structs to find data in some property */
  const char *stypefromname;
  const char *stypefromprop;
  List fns;
} ApiStructDef;

typedef struct ApiAllocDef {
  struct ApiAllocDef *next, *prev;
  void *mem;
} ApiAllocDef;

typedef struct DuneApiDef {
  struct SType *stype;
  List structs;
  List allocs;
  struct ApiStruct *laststruct;
  bool error;
  bool silent;
  bool preprocess;
  bool verify;
  bool animate;
  /** Whether RNA properties defined should be overridable or not by default. */
  bool make_overridable;

  /* Keep last. */
#ifndef API_RUNTIME
  struct {
    /** #api_def_prop_update */
    struct {
      int noteflag;
      const char *updatefn;
    } prop_update;
  } fallback;
#endif
} DuneApiDef;

extern DuneApiDef ApiDef;

/* Define functions for all types */
#ifndef __API_ACCESS_H__
extern DuneApi DUNE_API;
#endif

void api_def_id(struct DuneApiDef *dapi);
void api_def_action(struct DuneApi *dapi);
void api_def_anim(struct DuneApi *dapi);
void api_def_animviz(struct DuneApi *dapi);
void api_def_armature(struct DuneApi *dapi);
void api_def_attribute(struct DuneApi *dapi);
void api_def_asset(struct DuneApi *dapi);
void api_def_boid(struct DuneApi *dapi);
void api_def_brush(struct DuneApi *dapi);
void api_def_cachefile(struct DuneApi *dapi);
void api_def_camera(struct DuneApi *dapi);
void api_def_cloth(struct DuneApi *dapi);
void api_def_collections(struct DuneApi *dapi);
void api_def_color(struct DuneApi *dapi);
void api_def_constraint(struct DuneApi *dapi);
void api_def_cxt(struct DuneApi *dapi);
void api_def_curve(struct DuneApi *dapi);
void api_def_graph(struct DuneApi *dapi);
void api_def_dynamic_paint(struct DuneApi *dapi);
void api_def_fcurve(struct DuneApi *dapi);
void api_def_pen(struct DuneApi *dapi);
void api_def_pen_mod(struct DuneApi *dapi);
void api_def_shader_fx(struct DuneApi *dapi);
void api_def_curves(struct DuneApi *dapi);
void api_def_image(struct DuneApi *dapi);
void api_def_key(struct DuneApi *dapi);
void api_def_light(struct DuneApi *dapi);
void api_def_lattice(struct DuneApi *dapi);
void api_def_linestyle(struct DuneApi *dapi);
void api_def_main(struct DuneApi *dapi);
void api_def_material(struct DuneApi *dapi);
void api_def_mesh(struct DuneApi *dapi);
void api_def_meta(struct DuneApi *dapi);
void api_def_mod(struct DuneApi *dapi);
void api_def_nla(struct DuneApi *dapi);
void api_def_nodetree(struct DuneApi *dapi);
void api_def_object(struct DuneApi *dapi);
void api_def_object_force(struct DuneApi *dapi);
void api_def_packedfile(struct DuneApi *dapi);
void api_def_palette(struct DuneApi *dapi);
void api_def_particle(struct DuneApi *dapi);
void api_def_pointcloud(struct DuneApi *dapi);
void api_def_pose(struct DuneApi *dapi);
void api_def_profile(struct DuneApi *dapi);
void api_def_lightprobe(struct DuneApi *dapi);
void api_def_render(struct DuneApi *dapi);
void api_def_rigidbody(struct DuneApi *dapi);
void api_def_api(struct DuneApi *dapi);
void api_def_scene(struct DuneApi *dapi);
void api_def_simulation(struct DuneApi *dapi);
void api_def_view_layer(struct DuneApi *dapi);
void api_def_screen(struct DuneApi *dapi);
void api_def_sculpt_paint(struct DuneApi *dapi);
void api_def_sequencer(struct DuneApi *dapi);
void api_def_fluid(struct DuneApi *dapi);
void api_def_space(struct DuneApi *dapi);
void api_def_speaker(struct DuneApi *dapi);
void api_def_test(struct DuneApi *dapi);
void api_def_text(struct DuneApi *dapi);
void api_def_texture(struct DuneApi *dapi);
void api_def_timeline_marker(struct DuneApi *dapi);
void api_def_sound(struct DundApi *dapi);
void api_def_ui(struct DuneApi *dapi);
void api_def_userdef(struct DuneApi *dapi);
void api_def_vfont(struct DuneApi *dapi);
void api_def_volume(struct DuneApi *dapi);
void api_def_wm(struct DuneApi *dapi);
void api_def_wm_gizmo(struct DuneApi *dapi);
void api_def_workspace(struct DuneApi *dapi);
void api_def_world(struct DuneApi *dapi);
void api_def_movieclip(struct DuneApi *dapu);
void api_def_tracking(struct DuneApi *dapi);
void api_def_mask(struct DuneApi *dapi);
void api_def_xr(struct DuneApi *dapi);

/* Common Define functions */
void api_def_attributes_common(struct ApiStruct *sapi);

void api_AttributeGroup_iter_begin(CollectionPropIter *iter, ApiPtr *ptr);
void api_AttributeGroup_iter_next(CollectionPropIter *iter);
ApiPtr api_AttributeGroup_iter_get(CollectionPropIter *iter);
int api_AttributeGroup_length(ApiPtr *ptr);

void api_def_AnimData_common(struct ApiStruct *sapi);

bool api_AnimaData_override_apply(struct Main *main,
                                  struct ApiPtr *ptr_local,
                                  struct ApiPtr *ptr_ref,
                                  struct ApiPtr *ptr_storage,
                                  struct ApiProp *prop_local,
                                  struct ApiProp *prop_ref,
                                  struct ApiProp *prop_storage,
                                  int len_local,
                                  int len_ref,
                                  int len_storage,
                                  struct ApiPtr *ptr_item_local,
                                  struct ApiPtr *ptr_item_ref,
                                  struct ApiPtr *ptr_item_storage,
                                  struct IdOverrideLibPropOp *opop);

void api_def_animviz_common(struct ApiStruct *sapi)
void api_def_motionpath_common(struct ApiSruct *sapi);

/** Settings for curved bbone settings. */
void api_def_bone_curved_common(struct ApiStruct *sapi, bool is_posebone, bool is_editbone);

void api_def_texmat_common(struct ApiStruct *sapi, const char *texspace_editable);
void api_def_mtex_common(struct DuneApi *dapi,
                         struct ApiStruct *sapi,
                         const char *begin,
                         const char *activeget,
                         const char *activeset,
                         const char *activeeditable,
                         const char *structname,
                         const char *structname_slots,
                         const char *update,
                         const char *update_index);
void api_def_texpaint_slots(struct DuneApi *dapi, struct ApiStruct *sapi);
void api_def_view_layer_common(struct DuneApi *dapi, struct ApStruct *sapi, bool scene);

int api_AssetMetaData_editable(struct ApiPtr *ptr, const char **r_info);
/** the UI text and updating has to be set by the caller. */
ApiProp *api_def_asset_lib_ref_common(struct ApiStruct *sapi,
                                      const char *get,
                                      const char *set);
const EnumPropItem *api_asset_lib_ref_itemf(struct Cxt *C,
                                            struct ApiPtr *ptr,
                                            struct ApiProp *prop,
                                            bool *r_free);

/** Common properties for Action/Bone Groups - related to color. */
void api_def_actionbone_group_common(struct ApiStruct *sapi,
                                     int update_flag,
                                     const char *update_cb);
void api_ActionGroup_colorset_set(struct ApiPtr *ptr, int value);
bool api_ActionGroup_is_custom_colorset_get(struct ApiPtr *ptr);

void api_id_name_get(struct ApiPtr *ptr, char *value);
int api_id_name_length(struct ApiPtr *ptr);
void api_id_name_set(struct ApiPtr *ptr, const char *value);
struct ApiStruct *api_id_refine(struct ApiPtr *ptr);
struct IdProp **api_id_idprops(struct ApiPtr *ptr);
void api_id_fake_user_set(struct ApiPtr *ptr, bool value);
void **api_id_instance(ApiPtr *ptr);
struct IDProperty **rna_PropGroup_idprops(struct ApiPtr *ptr);
void api_PropGroup_unregister(struct Main *main, struct ApiStruct *type);
struct StructRNA *api_PropGroup_register(struct Main *main,
                                         struct ReportList *reports,
                                         void *data,
                                         const char *id,
                                         StructValidateFn validate,
                                         StructCallbackFn call,
                                         StructFreeFn free);
struct ApiStruct *api_PropGroup_refine(struct ApiPtr *ptr);

void api_object_vgroup_name_index_get(struct ApiPtr *ptr, char *value, int index);
int api_object_vgroup_name_index_length(struct ApiPtr *ptr, int index);
void api_object_vgroup_name_index_set(struct ApiPtr *ptr, const char *value, short *index);
void api_object_vgroup_name_set(struct ApiPtr *ptr,
                                const char *value,
                                char *result,
                                int maxlen);
void api_object_uvlayer_name_set(struct ApiPtr *ptr,
                                 const char *value,
                                 char *result,
                                 int maxlen);
void api_object_vcollayer_name_set(struct Apitr *ptr,
                                   const char *value,
                                   char *result,
                                   int maxlen);
ApiPtr api_object_shapekey_index_get(struct Id *id, int value);
int api_object_shapekey_index_set(struct Id *id, ApiPtr value, int current);

/* ViewLayer related functions defined in rna_scene.c but required in api_layer.c */
void api_def_freestyle_settings(struct ApiDune *dapi);
struct ApiPtr api_FreestyleLineSet_linestyle_get(struct ApiPtr *ptr);
void api_FreestyleLineSet_linestyle_set(struct ApiPtr *ptr,
                                        struct ApiPtr value,
                                        struct ReportList *reports);
struct FreestyleLineSet *api_FreestyleSettings_lineset_add(struct Id *id,
                                                           struct FreestyleSettings *config,
                                                           struct Main *main,
                                                           const char *name);
void api_FreestyleSettings_lineset_remove(struct Id *id,
                                          struct FreestyleSettings *config,
                                          struct ReportList *reports,
                                          struct ApiPtr *lineset_ptr);
struct ApiPtr api_FreestyleSettings_active_lineset_get(struct ApiPtr *ptr);
void api_FreestyleSettings_active_lineset_index_range(
    struct ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax);
int api_FreestyleSettings_active_lineset_index_get(struct PointerRNA *ptr);
void api_FreestyleSettings_active_lineset_index_set(struct PointerRNA *ptr, int value);
struct FreestyleModuleConfig *rna_FreestyleSettings_module_add(struct Id *id,
                                                               struct FreestyleSettings *config);
void api_FreestyleSettings_module_remove(struct Id *id,
                                         struct FreestyleSettings *config,
                                         struct ReportList *reports,
                                         struct ApiPtr *module_ptr);

void api_Scene_use_view_map_cache_update(struct Main *main,
                                         struct Scene *scene,
                                         struct ApiPtr *ptr);
void api_Scene_glsl_update(struct Main *main, struct Scene *scene, struct PointerRNA *ptr);
void api_Scene_freestyle_update(struct Main *main, struct Scene *scene, struct PointerRNA *ptr);
void api_ViewLayer_name_set(struct ApiPtr *ptr, const char *value);
void api_ViewLayer_material_override_update(struct Main *main,
                                            struct Scene *activescene,
                                            struct ApiPtr *ptr);
void api_ViewLayer_pass_update(struct Main *main,
                               struct Scene *activescene,
                               struct ApiPtr *ptr);
void api_ViewLayer_active_aov_index_range(
    ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax);
int api_ViewLayer_active_aov_index_get(ApiPtr *ptr);
void api_ViewLayer_active_aov_index_set(Apitr *ptr, int value);
/** Set `r_api_path` with the base view-layer path.
 *  `api_path_buffer_size` should be at least `sizeof(ViewLayer.name) * 3`.
 *  return actual length of the generated api path */
size_t api_ViewLayer_path_buffer_get(struct ViewLayer *view_layer,
                                     char *r_api_path,
                                     const size_t rna_path_buffer_size);

/* named internal so as not to conflict with obj.update() rna func */
void api_Object_internal_update_data(struct Main *main,
                                     struct Scene *scene,
                                     struct ApiPtr *ptr);
void api_Mesh_update_draw(struct Main *main, struct Scene *scene, struct ApiPtr *ptr);
void api_TextureSlot_update(struct Cxt *C, struct ApiPtr *ptr);

/* basic poll functions for object types */
bool api_Armature_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Camera_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Curve_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Pen_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Light_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Lattice_object_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Mesh_object_poll(struct ApiPtr *ptr, struct ApiPtr value);

/* basic poll functions for actions (to prevent actions getting set in wrong places) */
bool api_Action_id_poll(struct ApiPtr *ptr, struct ApiPtr value);
bool api_Action_actedit_assign_poll(struct ApiPtr *ptr, struct ApiPtr value);

/* Pen datablock polling functions - for filtering GP Object vs Annotation datablocks */
bool api_Pen_datablocks_annotations_poll(struct ApiPtr *ptr,
                                         const struct ApiPtr value);
bool api_Pen_datablocks_obdata_poll(struct ApiPtr *ptr, const struct ApiPtr value);

char *api_TextureSlot_path(struct ApiPtr *ptr);
char *api_Node_ImageUser_path(struct ApiPtr *ptr);

/* Set U.is_dirty and redraw. */
void api_userdef_is_dirty_update_impl(void);
void api_userdef_is_dirty_update(struct Main *main, struct Scene *scene, struct ApiPtr *ptr);

/* API functions */
void api_action(ApiStruct *sapi);
void api_animdata(struct ApiStruct *sapi);
void api_armature_edit_bone(ApiStruct *sapi);
void api_bone(ApiStruct *sapi);
void api_camera(ApiStruct *sapi);
void api_curve(ApiStruct *sapi);
void api_curve_nurb(ApiStruct *sapi);
void api_fcurves(ApiStruct *sapi);
void api_drivers(ApiStruct *sapi);
void api_image_packed_file(struct ApiStruct *sapi);
void api_image(struct ApiStruct *sapi);
void api_lattice(struct ApiStruct *sapi);
void api_op(struct ApiStruct *sapi);
void api_macro(struct ApiStruct *sapi);
void api_gizmo(struct ApiStruct *sapi);
void api_gizmogroup(struct ApiStruct *sapi);
void api_keyconfig(struct ApiStructRNA *srna);
void api_keyconfigs(struct ApiStructRNA *srna);
void api_keyingset(struct ApiStructRNA *srna);
void api_keymap(struct ApiStructRNA *srna);
void api_keymaps(struct ApiStructRNA *srna);
void api_keymapitem(struct ApiStructRNA *srna);
void api_keymapitems(struct ApiStructRNA *srna);
void api_main(struct ApiStructRNA *srna);
void api_material(ApiStructRNA *srna);
void api_mesh(struct ApiStructRNA *srna);
void api_meta(struct ApiStructRNA *srna);
void api_object(struct ApiStructRNA *srna);
void api_pose(struct ApiStruct *srna);
void api_pose_channel(struct ApiStructRNA *srna);
void api_scene(struct ApiStruct *srna);
void api_scene_render(struct ApiStructRNA *srna);
void api_sequence_strip(ApiStructRNA *srna);
void api_text(struct ApiStructRNA *srna);
void api_ui_layout(struct ApiStructRNA *srna);
void api_window(struct ApiStructRNA *srna);
void api_wm(struct ApiStructRNA *srna);
void api_space_node(struct ApiStruct *sapi);
void api_space_text(struct ApiStruct *sapi);
void api_space_filebrowser(struct ApiStruct *sapi);
void api_region_view3d(struct ApiStruct *sapi);
void api_texture(struct ApiStruct *sapi);
void api_sequences(DuneApi *dapi, ApiProp *cprop, bool metastrip);
void api_seq_elements(DuneA *dapi, ApiProp *cprop);
void api_sound(struct StructRNA *srna);
void api_api_vfont(struct StructRNA *srna);
void api_api_workspace(struct StructRNA *srna);
void api_api_workspace_tool(struct StructRNA *srna);

/* main collection functions */
void apk_def_main_cameras(DuneApi *dapi, ApiProp *cprop);
void api_def_main_scenes(DuneApi *dapi, ApiProp *cprop);
void api_def_main_objects(DuneApi *dapi, ApiProp *cprop);
void api_def_main_materials(DuneApi *dapi, ApiProp *cprop);
void api_def_main_node_groups(DuneApi *api, ApiProp *cprop);
void api_def_main_meshes(BlenderRNA *dapi, Prop *cprop);
void api_def_main_lights(BlenderRNA *dapi, PropertyRNA *cprop);
void api_def_main_libs(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_screens(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_window_managers(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_images(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_lattices(DuneApi *dapi, ApiProp *cprop);
void api_def_main_curves(DuneApi *dapi, ApiProp *cprop);
void api_def_main_metaballs(DuneApi *dapi, ApiProp *cprop);
void api_def_main_fonts(DuneApi *dapi, PropertyRNA *cprop);
void api_def_main_textures(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_brushes(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_worlds(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_collections(DuneA *brna, PropertyRNA *cprop);
void api_def_main_texts(DuneA *brna, ApiProp *cprop);
void api_def_main_speakers(DuneA *dapi, ApiProp *cprop);
void api_def_main_sounds(DunrA *dapi, ApiProp *cprop);
void api_def_main_armatures(DuneA *dapi, ApiProp *cprop);
void spi_def_main_actions(DuneA *dapi, ApiProp *cprop);
void api_def_main_particles(DunrA *brna, PropertyRNA *cprop);
void api_def_main_palettes(DuneA *brna, PropertyRNA *cprop);
void api_def_main_gpencil(DuneA *brna, PropertyRNA *cprop);
void api_def_main_movieclips(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_masks(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_linestyles(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_cachefiles(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_paintcurves(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_workspaces(BlenderRNA *brna, PropertyRNA *cprop);
void api_def_main_lightprobes(BlenderRNA *brna, PropertyRNA *cprop);
#ifdef WITH_NEW_CURVES_TYPE
void api_def_main_hair_curves(DuneApi *dapi, PropertyRNA *cprop);
#endif
void api_def_main_pointclouds(DuneA *dapi, PropertyRNA *cprop);
void RNA_def_main_volumes(BlenderRNA *brna, PropertyRNA *cprop);
#ifdef WITH_SIMULATION_DATABLOCK
void RNA_def_main_simulations(BlenderRNA *brna, PropertyRNA *cprop);
#endif

/* ID Properties */

#ifndef __RNA_ACCESS_H__
extern StructRNA RNA_PropertyGroupItem;
extern StructRNA RNA_PropertyGroup;
#endif

/**
 * This function only returns an #IDProperty,
 * or NULL (in case IDProp could not be found, or prop is a real RNA property).
 */
struct IDProperty *rna_idproperty_check(struct PropertyRNA **prop,
                                        struct PointerRNA *ptr) ATTR_WARN_UNUSED_RESULT;
/** This fn always return the valid, real data pointer, be it a regular api prop one,
 * or an IdProp one. */
struct ApiProp api_ensure_prop_realdata(struct ApiProp **prop,
                                                 struct ApiPtr *ptr) ATTR_WARN_UNUSED_RESULT;
struct PropertyRNA *rna_ensure_property(struct ApiProp *prop) ATTR_WARN_UNUSED_RESULT;

/* Override default callbacks. */
/* Default override callbacks for all types. */
/* TODO: Maybe at some point we'll want to write that in direct RNA-generated code instead
 *       (like we do for default get/set/etc.)?
 *       Not obvious though, those are fairly more complicated than basic SDNA access.
 */
int api_prop_override_diff_default(struct Main *main,
                                   struct ApiOrIdProp *prop_a,
                                   struct ApiOrIdProp *prop_b,
                                       int mode,
                                       struct IdOverrideLib *override,
                                       const char *api_path,
                                       size_t api_path_len,
                                       int flags,
                                       bool *r_override_changed);

bool api_prop_override_store_default(struct Main *main,
                                         struct ApiPointerRNA *ptr_local,
                                         struct ApiPointerRNA *ptr_reference,
                                         struct PointerRNA *ptr_storage,
                                         struct PropertyRNA *prop_local,
                                         struct PropertyRNA *prop_reference,
                                         struct PropertyRNA *prop_storage,
                                         int len_local,
                                         int len_reference,
                                         int len_storage,
                                         struct IdOverrideLibPropOp *opop);

bool api_prop_override_apply_default(struct Main *main,
                                     struct ApiPtr *ptr_dst,
                                     struct ApiPtr *ptr_src,
                                     struct ApiPtr *ptr_storage,
                                         struct PropertyRNA *prop_dst,
                                         struct PropertyRNA *prop_src,
                                         struct PropertyRNA *prop_storage,
                                         int len_dst,
                                         int len_src,
                                         int len_storage,
                                         struct PointerRNA *ptr_item_dst,
                                         struct PointerRNA *ptr_item_src,
                                         struct PointerRNA *ptr_item_storage,
                                         struct IDOverrideLibraryPropertyOperation *opop);

/* Builtin Property Callbacks */

void api_builtin_props_begin(struct CollectionPropIter *iter, struct ApiPtr *ptr);
void api_builtin_props_next(struct CollectionPropIter *iter);
ApiPtr api_builtin_props_get(struct CollectionPropIter *iter);
ApiPtr api_builtin_type_get(struct ApiPtr *ptr);
int api_builtin_props_lookup_string(ApiPtr *ptr, const char *key, PointerRNA *r_ptr);

/* Iterators */

void rna_iterator_listbase_begin(struct CollectionPropertyIterator *iter,
                                 struct ListBase *lb,
                                 IteratorSkipFunc skip);
void rna_iterator_listbase_next(struct CollectionPropertyIterator *iter);
void *rna_iterator_listbase_get(struct CollectionPropertyIterator *iter);
void rna_iterator_listbase_end(struct CollectionPropertyIterator *iter);
PointerRNA rna_listbase_lookup_int(PointerRNA *ptr,
                                   StructRNA *type,
                                   struct ListBase *lb,
                                   int index);

void rna_iterator_array_begin(struct CollectionPropertyIterator *iter,
                              void *ptr,
                              int itemsize,
                              int length,
                              bool free_ptr,
                              IteratorSkipFunc skip);
void rna_iterator_array_next(struct CollectionPropertyIterator *iter);
void *rna_iterator_array_get(struct CollectionPropertyIterator *iter);
void *rna_iterator_array_dereference_get(struct CollectionPropertyIterator *iter);
void rna_iterator_array_end(struct CollectionPropertyIterator *iter);
PointerRNA rna_array_lookup_int(
    PointerRNA *ptr, StructRNA *type, void *data, int itemsize, int length, int index);

/* Duplicated code since we can't link in blenlib */

#ifndef RNA_RUNTIME
void *rna_alloc_from_buffer(const char *buffer, int buffer_len);
void *rna_calloc(int buffer_len);
#endif

void rna_addtail(struct List *listbase, void *vlink);
void api_freelinkN(struct List *listbase, void *vlink);
void api_freelistN(struct List *listbase);
ApiPropDef *api_findlink(List *listbase, const char *identifier);

StructDefRNA *rna_find_struct_def(StructRNA *srna);
FunctionDefRNA *rna_find_function_def(FunctionRNA *func);
PropertyDefRNA *rna_find_parameter_def(PropertyRNA *parm);
PropertyDefRNA *rna_find_struct_property_def(StructRNA *srna, PropertyRNA *prop);

/* Pointer Handling */

PointerRNA rna_pointer_inherit_refine(struct PointerRNA *ptr, struct StructRNA *type, void *data);

/* Functions */

int rna_parameter_size(struct PropertyRNA *parm);

/* XXX, these should not need to be defined here~! */
struct MTex *rna_mtex_texture_slots_add(struct ID *self,
                                        struct bContext *C,
                                        struct ReportList *reports);
struct MTex *rna_mtex_texture_slots_create(struct ID *self,
                                           struct bContext *C,
                                           struct ReportList *reports,
                                           int index);
void rna_mtex_texture_slots_clear(struct ID *self,
                                  struct bContext *C,
                                  struct ReportList *reports,
                                  int index);

int rna_IDMaterials_assign_int(struct PointerRNA *ptr,
                               int key,
                               const struct PointerRNA *assign_ptr);

const char *rna_translate_ui_text(const char *text,
                                  const char *text_ctxt,
                                  struct StructRNA *type,
                                  struct PropertyRNA *prop,
                                  bool translate);

/* Internal functions that cycles uses so we need to declare (tsk!). */
void rna_RenderPass_rect_set(PointerRNA *ptr, const float *values);

#ifdef RNA_RUNTIME
#  ifdef __GNUC__
#    pragma GCC diagnostic ignored "-Wredundant-decls"
#  endif
#endif

/* C11 for compile time range checks */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define USE_RNA_RANGE_CHECK
#  define TYPEOF_MAX(x) \
    _Generic((x), bool : 1, char \
             : CHAR_MAX, signed char \
             : SCHAR_MAX, unsigned char \
             : UCHAR_MAX, signed short \
             : SHRT_MAX, unsigned short \
             : USHRT_MAX, signed int \
             : INT_MAX, unsigned int \
             : UINT_MAX, float \
             : FLT_MAX, double \
             : DBL_MAX)

#  define TYPEOF_MIN(x) \
    _Generic((x), bool : 0, char \
             : CHAR_MIN, signed char \
             : SCHAR_MIN, unsigned char : 0, signed short \
             : SHRT_MIN, unsigned short : 0, signed int \
             : INT_MIN, unsigned int : 0, float \
             : -FLT_MAX, double \
             : -DBL_MAX)
#endif
