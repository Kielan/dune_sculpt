#pragma once

#include "BLI_utildefines.h"

#include "BLI_compiler_attrs.h"

#include "rna_internal_types.h"

#include "UI_resources.h"

#define RNA_MAGIC ((int)~0)

struct AssetLibraryReference;
struct FreestyleSettings;
struct ID;
struct IDOverrideLibrary;
struct IDOverrideLibraryPropertyOperation;
struct IDProperty;
struct Main;
struct Object;
struct ReportList;
struct SDNA;
struct ViewLayer;

/* Data structures used during define */

typedef struct ContainerDefRNA {
  void *next, *prev;

  ContainerRNA *cont;
  ListBase properties;
} ContainerDefRNA;

typedef struct FunctionDefRNA {
  ContainerDefRNA cont;

  FunctionRNA *func;
  const char *srna;
  const char *call;
  const char *gencall;
} FunctionDefRNA;

typedef struct PropertyDefRNA {
  struct PropertyDefRNA *next, *prev;

  struct ContainerRNA *cont;
  struct PropertyRNA *prop;

  /* struct */
  const char *dnastructname;
  const char *dnastructfromname;
  const char *dnastructfromprop;

  /* property */
  const char *dnaname;
  const char *dnatype;
  int dnaarraylength;
  int dnapointerlevel;
  /**
   * Offset in bytes within `dnastructname`.
   * -1 when unusable (follows pointer for e.g.). */
  int dnaoffset;
  int dnasize;

  /* for finding length of array collections */
  const char *dnalengthstructname;
  const char *dnalengthname;
  int dnalengthfixed;

  int64_t booleanbit;
  bool booleannegative;

  /* not to be confused with PROP_ENUM_FLAG
   * this only allows one of the flags to be set at a time, clearing all others */
  int enumbitflags;
} PropertyDefRNA;

typedef struct StructDefRNA {
  ContainerDefRNA cont;

  struct StructRNA *srna;
  const char *filename;

  const char *dnaname;

  /* for derived structs to find data in some property */
  const char *dnafromname;
  const char *dnafromprop;

  ListBase functions;
} StructDefRNA;

typedef struct AllocDefRNA {
  struct AllocDefRNA *next, *prev;
  void *mem;
} AllocDefRNA;

typedef struct BlenderDefRNA {
  struct SDNA *sdna;
  ListBase structs;
  ListBase allocs;
  struct StructRNA *laststruct;
  bool error;
  bool silent;
  bool preprocess;
  bool verify;
  bool animate;
  /** Whether RNA properties defined should be overridable or not by default. */
  bool make_overridable;

  /* Keep last. */
#ifndef RNA_RUNTIME
  struct {
    /** #RNA_def_property_update */
    struct {
      int noteflag;
      const char *updatefunc;
    } property_update;
  } fallback;
#endif
} BlenderDefRNA;

extern BlenderDefRNA DefRNA;

/* Define functions for all types */
#ifndef __RNA_ACCESS_H__
extern BlenderRNA BLENDER_RNA;
#endif

void RNA_def_ID(struct BlenderRNA *brna);
void RNA_def_action(struct BlenderRNA *brna);
void RNA_def_animation(struct BlenderRNA *brna);
void RNA_def_animviz(struct BlenderRNA *brna);
void RNA_def_armature(struct BlenderRNA *brna);
void RNA_def_attribute(struct BlenderRNA *brna);
void RNA_def_asset(struct BlenderRNA *brna);
void RNA_def_boid(struct BlenderRNA *brna);
void RNA_def_brush(struct BlenderRNA *brna);
void RNA_def_cachefile(struct BlenderRNA *brna);
void RNA_def_camera(struct BlenderRNA *brna);
void RNA_def_cloth(struct BlenderRNA *brna);
void RNA_def_collections(struct BlenderRNA *brna);
void RNA_def_color(struct BlenderRNA *brna);
void RNA_def_constraint(struct BlenderRNA *brna);
void RNA_def_context(struct BlenderRNA *brna);
void RNA_def_curve(struct BlenderRNA *brna);
void RNA_def_depsgraph(struct BlenderRNA *brna);
void RNA_def_dynamic_paint(struct BlenderRNA *brna);
void RNA_def_fcurve(struct BlenderRNA *brna);
void RNA_def_gpencil(struct BlenderRNA *brna);
void RNA_def_greasepencil_modifier(struct BlenderRNA *brna);
void RNA_def_shader_fx(struct BlenderRNA *brna);
void RNA_def_curves(struct BlenderRNA *brna);
void RNA_def_image(struct BlenderRNA *brna);
void RNA_def_key(struct BlenderRNA *brna);
void RNA_def_light(struct BlenderRNA *brna);
void RNA_def_lattice(struct BlenderRNA *brna);
void RNA_def_linestyle(struct BlenderRNA *brna);
void RNA_def_main(struct BlenderRNA *brna);
void RNA_def_material(struct BlenderRNA *brna);
void RNA_def_mesh(struct BlenderRNA *brna);
void RNA_def_meta(struct BlenderRNA *brna);
void RNA_def_modifier(struct BlenderRNA *brna);
void RNA_def_nla(struct BlenderRNA *brna);
void RNA_def_nodetree(struct BlenderRNA *brna);
void RNA_def_object(struct BlenderRNA *brna);
void RNA_def_object_force(struct BlenderRNA *brna);
void RNA_def_packedfile(struct BlenderRNA *brna);
void RNA_def_palette(struct BlenderRNA *brna);
void RNA_def_particle(struct BlenderRNA *brna);
void RNA_def_pointcloud(struct BlenderRNA *brna);
void RNA_def_pose(struct BlenderRNA *brna);
void RNA_def_profile(struct BlenderRNA *brna);
void RNA_def_lightprobe(struct BlenderRNA *brna);
void RNA_def_render(struct BlenderRNA *brna);
void RNA_def_rigidbody(struct BlenderRNA *brna);
void RNA_def_rna(struct BlenderRNA *brna);
void RNA_def_scene(struct BlenderRNA *brna);
void RNA_def_simulation(struct BlenderRNA *brna);
void RNA_def_view_layer(struct BlenderRNA *brna);
void RNA_def_screen(struct BlenderRNA *brna);
void RNA_def_sculpt_paint(struct BlenderRNA *brna);
void RNA_def_sequencer(struct BlenderRNA *brna);
void RNA_def_fluid(struct BlenderRNA *brna);
void RNA_def_space(struct BlenderRNA *brna);
void RNA_def_speaker(struct BlenderRNA *brna);
void RNA_def_test(struct BlenderRNA *brna);
void RNA_def_text(struct BlenderRNA *brna);
void RNA_def_texture(struct BlenderRNA *brna);
void RNA_def_timeline_marker(struct BlenderRNA *brna);
void RNA_def_sound(struct BlenderRNA *brna);
void RNA_def_ui(struct BlenderRNA *brna);
void RNA_def_userdef(struct BlenderRNA *brna);
void RNA_def_vfont(struct BlenderRNA *brna);
void RNA_def_volume(struct BlenderRNA *brna);
void RNA_def_wm(struct BlenderRNA *brna);
void RNA_def_wm_gizmo(struct BlenderRNA *brna);
void RNA_def_workspace(struct BlenderRNA *brna);
void RNA_def_world(struct BlenderRNA *brna);
void RNA_def_movieclip(struct BlenderRNA *brna);
void RNA_def_tracking(struct BlenderRNA *brna);
void RNA_def_mask(struct BlenderRNA *brna);
void RNA_def_xr(struct BlenderRNA *brna);

/* Common Define functions */

void rna_def_attributes_common(struct StructRNA *srna);

void rna_AttributeGroup_iterator_begin(CollectionPropertyIterator *iter, PointerRNA *ptr);
void rna_AttributeGroup_iterator_next(CollectionPropertyIterator *iter);
PointerRNA rna_AttributeGroup_iterator_get(CollectionPropertyIterator *iter);
int rna_AttributeGroup_length(PointerRNA *ptr);

void rna_def_animdata_common(struct StructRNA *srna);

bool rna_AnimaData_override_apply(struct Main *bmain,
                                  struct PointerRNA *ptr_local,
                                  struct PointerRNA *ptr_reference,
                                  struct PointerRNA *ptr_storage,
                                  struct PropertyRNA *prop_local,
                                  struct PropertyRNA *prop_reference,
                                  struct PropertyRNA *prop_storage,
                                  int len_local,
                                  int len_reference,
                                  int len_storage,
                                  struct PointerRNA *ptr_item_local,
                                  struct PointerRNA *ptr_item_reference,
                                  struct PointerRNA *ptr_item_storage,
                                  struct IDOverrideLibraryPropertyOperation *opop);

void rna_def_animviz_common(struct StructRNA *srna);
void rna_def_motionpath_common(struct StructRNA *srna);

/**
 * Settings for curved bbone settings.
 */
void rna_def_bone_curved_common(struct StructRNA *srna, bool is_posebone, bool is_editbone);

void rna_def_texmat_common(struct StructRNA *srna, const char *texspace_editable);
void rna_def_mtex_common(struct BlenderRNA *brna,
                         struct StructRNA *srna,
                         const char *begin,
                         const char *activeget,
                         const char *activeset,
                         const char *activeeditable,
                         const char *structname,
                         const char *structname_slots,
                         const char *update,
                         const char *update_index);
void rna_def_texpaint_slots(struct BlenderRNA *brna, struct StructRNA *srna);
void rna_def_view_layer_common(struct BlenderRNA *brna, struct StructRNA *srna, bool scene);

int rna_AssetMetaData_editable(struct PointerRNA *ptr, const char **r_info);
/**
 * \note the UI text and updating has to be set by the caller.
 */
PropertyRNA *rna_def_asset_library_reference_common(struct StructRNA *srna,
                                                    const char *get,
                                                    const char *set);
const EnumPropertyItem *rna_asset_library_reference_itemf(struct bContext *C,
                                                          struct PointerRNA *ptr,
                                                          struct PropertyRNA *prop,
                                                          bool *r_free);

/**
 * Common properties for Action/Bone Groups - related to color.
 */
void rna_def_actionbone_group_common(struct StructRNA *srna,
                                     int update_flag,
                                     const char *update_cb);
void rna_ActionGroup_colorset_set(struct PointerRNA *ptr, int value);
bool rna_ActionGroup_is_custom_colorset_get(struct PointerRNA *ptr);

void rna_ID_name_get(struct PointerRNA *ptr, char *value);
int rna_ID_name_length(struct PointerRNA *ptr);
void rna_ID_name_set(struct PointerRNA *ptr, const char *value);
struct StructRNA *rna_ID_refine(struct PointerRNA *ptr);
struct IDProperty **rna_ID_idprops(struct PointerRNA *ptr);
void rna_ID_fake_user_set(struct PointerRNA *ptr, bool value);
void **rna_ID_instance(PointerRNA *ptr);
struct IDProperty **rna_PropertyGroup_idprops(struct PointerRNA *ptr);
void rna_PropertyGroup_unregister(struct Main *bmain, struct StructRNA *type);
struct StructRNA *rna_PropertyGroup_register(struct Main *bmain,
                                             struct ReportList *reports,
                                             void *data,
                                             const char *identifier,
                                             StructValidateFunc validate,
                                             StructCallbackFunc call,
                                             StructFreeFunc free);
struct StructRNA *rna_PropertyGroup_refine(struct PointerRNA *ptr);

void rna_object_vgroup_name_index_get(struct PointerRNA *ptr, char *value, int index);
int rna_object_vgroup_name_index_length(struct PointerRNA *ptr, int index);
void rna_object_vgroup_name_index_set(struct PointerRNA *ptr, const char *value, short *index);
void rna_object_vgroup_name_set(struct PointerRNA *ptr,
                                const char *value,
                                char *result,
                                int maxlen);
void rna_object_uvlayer_name_set(struct PointerRNA *ptr,
                                 const char *value,
                                 char *result,
                                 int maxlen);
void rna_object_vcollayer_name_set(struct PointerRNA *ptr,
                                   const char *value,
                                   char *result,
                                   int maxlen);
PointerRNA rna_object_shapekey_index_get(struct ID *id, int value);
int rna_object_shapekey_index_set(struct ID *id, PointerRNA value, int current);

/* ViewLayer related functions defined in rna_scene.c but required in rna_layer.c */
void rna_def_freestyle_settings(struct BlenderRNA *brna);
struct PointerRNA rna_FreestyleLineSet_linestyle_get(struct PointerRNA *ptr);
void rna_FreestyleLineSet_linestyle_set(struct PointerRNA *ptr,
                                        struct PointerRNA value,
                                        struct ReportList *reports);
struct FreestyleLineSet *rna_FreestyleSettings_lineset_add(struct ID *id,
                                                           struct FreestyleSettings *config,
                                                           struct Main *bmain,
                                                           const char *name);
void rna_FreestyleSettings_lineset_remove(struct ID *id,
                                          struct FreestyleSettings *config,
                                          struct ReportList *reports,
                                          struct PointerRNA *lineset_ptr);
struct PointerRNA rna_FreestyleSettings_active_lineset_get(struct PointerRNA *ptr);
void rna_FreestyleSettings_active_lineset_index_range(
    struct PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax);
int rna_FreestyleSettings_active_lineset_index_get(struct PointerRNA *ptr);
void rna_FreestyleSettings_active_lineset_index_set(struct PointerRNA *ptr, int value);
struct FreestyleModuleConfig *rna_FreestyleSettings_module_add(struct ID *id,
                                                               struct FreestyleSettings *config);
void rna_FreestyleSettings_module_remove(struct ID *id,
                                         struct FreestyleSettings *config,
                                         struct ReportList *reports,
                                         struct PointerRNA *module_ptr);

void rna_Scene_use_view_map_cache_update(struct Main *bmain,
                                         struct Scene *scene,
                                         struct PointerRNA *ptr);
void rna_Scene_glsl_update(struct Main *bmain, struct Scene *scene, struct PointerRNA *ptr);
void rna_Scene_freestyle_update(struct Main *bmain, struct Scene *scene, struct PointerRNA *ptr);
void rna_ViewLayer_name_set(struct PointerRNA *ptr, const char *value);
void rna_ViewLayer_material_override_update(struct Main *bmain,
                                            struct Scene *activescene,
                                            struct PointerRNA *ptr);
void rna_ViewLayer_pass_update(struct Main *bmain,
                               struct Scene *activescene,
                               struct PointerRNA *ptr);
void rna_ViewLayer_active_aov_index_range(
    PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax);
int rna_ViewLayer_active_aov_index_get(PointerRNA *ptr);
void rna_ViewLayer_active_aov_index_set(PointerRNA *ptr, int value);
/**
 *  Set `r_rna_path` with the base view-layer path.
 *  `rna_path_buffer_size` should be at least `sizeof(ViewLayer.name) * 3`.
 *  \return actual length of the generated RNA path.
 */
