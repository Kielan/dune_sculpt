#include <stdio.h>
#include <stdlib.h>

#include "types_id.h"
#include "types_material.h"
#include "types_object.h"
#include "types_vfont.h"

#include "lib_utildefines.h"

#include "dune_icons.h"
#include "dune_lib_id.h"
#include "dune_object.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "wm_types.h"

#include "api_internal.h"

/* enum of ID-block types
 * NOTE: need to keep this in line with the other defines for these */
const EnumPropItem api_enum_id_type_items[] = {
    {ID_AC, "ACTION", ICON_ACTION, "Action", ""},
    {ID_AR, "ARMATURE", ICON_ARMATURE_DATA, "Armature", ""},
    {ID_BR, "BRUSH", ICON_BRUSH_DATA, "Brush", ""},
    {ID_CA, "CAMERA", ICON_CAMERA_DATA, "Camera", ""},
    {ID_CF, "CACHEFILE", ICON_FILE, "Cache File", ""},
    {ID_CU_LEGACY, "CURVE", ICON_CURVE_DATA, "Curve", ""},
    {ID_VF, "FONT", ICON_FONT_DATA, "Font", ""},
    {ID_GD, "PENCI", ICON_PEN, "Grease Pencil", ""},
    {ID_GR, "COLLECTION", ICON_OUTLINER_COLLECTION, "Collection", ""},
    {ID_IM, "IMAGE", ICON_IMAGE_DATA, "Image", ""},
    {ID_KE, "KEY", ICON_SHAPEKEY_DATA, "Key", ""},
    {ID_LA, "LIGHT", ICON_LIGHT_DATA, "Light", ""},
    {ID_LI, "LIB", ICON_LIBRARY_DATA_DIRECT, "Lib", ""},
    {ID_LS, "LINESTYLE", ICON_LINE_DATA, "Line Style", ""},
    {ID_LT, "LATTICE", ICON_LATTICE_DATA, "Lattice", ""},
    {ID_MSK, "MASK", ICON_MOD_MASK, "Mask", ""},
    {ID_MA, "MATERIAL", ICON_MATERIAL_DATA, "Material", ""},
    {ID_MB, "META", ICON_META_DATA, "Metaball", ""},
    {ID_ME, "MESH", ICON_MESH_DATA, "Mesh", ""},
    {ID_MC, "MOVIECLIP", ICON_TRACKER, "Movie Clip", ""},
    {ID_NT, "NODETREE", ICON_NODETREE, "Node Tree", ""},
    {ID_OB, "OBJECT", ICON_OBJECT_DATA, "Object", ""},
    {ID_PC, "PAINTCURVE", ICON_CURVE_BEZCURVE, "Paint Curve", ""},
    {ID_PAL, "PALETTE", ICON_COLOR, "Palette", ""},
    {ID_PA, "PARTICLE", ICON_PARTICLE_DATA, "Particle", ""},
    {ID_LP, "LIGHT_PROBE", ICON_LIGHTPROBE_CUBEMAP, "Light Probe", ""},
    {ID_SCE, "SCENE", ICON_SCENE_DATA, "Scene", ""},
    {ID_SIM, "SIMULATION", ICON_PHYSICS, "Simulation", ""}, /* TODO: Use correct icon. */
    {ID_SO, "SOUND", ICON_SOUND, "Sound", ""},
    {ID_SPK, "SPEAKER", ICON_SPEAKER, "Speaker", ""},
    {ID_TXT, "TEXT", ICON_TEXT, "Text", ""},
    {ID_TE, "TEXTURE", ICON_TEXTURE_DATA, "Texture", ""},
    {ID_CV, "CURVES", ICON_CURVES_DATA, "Hair Curves", ""},
    {ID_PT, "POINTCLOUD", ICON_POINTCLOUD_DATA, "Point Cloud", ""},
    {ID_VO, "VOLUME", ICON_VOLUME_DATA, "Volume", ""},
    {ID_WM, "WINDOWMANAGER", ICON_WINDOW, "Window Manager", ""},
    {ID_WO, "WORLD", ICON_WORLD_DATA, "World", ""},
    {ID_WS, "WORKSPACE", ICON_WORKSPACE, "Workspace", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_override_lib_prop_op_items[] = {
    {IDOVERRIDE_LIB_OP_NOOP,
     "NOOP",
     0,
     "No-Op",
     "Does nothing, prevents adding actual overrides (NOT USED)"},
    {IDOVERRIDE_LIB_OP_REPLACE,
     "REPLACE",
     0,
     "Replace",
     "Replace value of reference by overriding one"},
    {IDOVERRIDE_LIB_OP_ADD,
     "DIFF_ADD",
     0,
     "Differential",
     "Stores and apply difference between reference and local value (NOT USED)"},
    {IDOVERRIDE_LIB_OP_SUBTRACT,
     "DIFF_SUB",
     0,
     "Differential",
     "Stores and apply difference between reference and local value (NOT USED)"},
    {IDOVERRIDE_LIB_OP_MULTIPLY,
     "FACT_MULTIPLY",
     0,
     "Factor",
     "Stores and apply multiplication factor between reference and local value (NOT USED)"},
    {IDOVERRIDE_LIB_OP_INSERT_AFTER,
     "INSERT_AFTER",
     0,
     "Insert After",
     "Insert a new item into collection after the one referenced in subitem_reference_name or "
     "_index"},
    {IDOVERRIDE_LIB_OP_INSERT_BEFORE,
     "INSERT_BEFORE",
     0,
     "Insert Before",
     "Insert a new item into collection after the one referenced in subitem_reference_name or "
     "_index (NOT USED)"},
    {0, NULL, 0, NULL, NULL},
};

/**
 * \note Uses #IDFilterEnumPropertyItem, not EnumPropertyItem, to support 64 bit items.
 */
const struct IdFilterEnumPropItem api_enum_id_type_filter_items[] = {
    /* Datablocks */
    {FILTER_ID_AC, "filter_action", ICON_ACTION, "Actions", "Show Action data-blocks"},
    {FILTER_ID_AR,
     "filter_armature",
     ICON_ARMATURE_DATA,
     "Armatures",
     "Show Armature data-blocks"},
    {FILTER_ID_BR, "filter_brush", ICON_BRUSH_DATA, "Brushes", "Show Brushes data-blocks"},
    {FILTER_ID_CA, "filter_camera", ICON_CAMERA_DATA, "Cameras", "Show Camera data-blocks"},
    {FILTER_ID_CF, "filter_cachefile", ICON_FILE, "Cache Files", "Show Cache File data-blocks"},
    {FILTER_ID_CU_LEGACY, "filter_curve", ICON_CURVE_DATA, "Curves", "Show Curve data-blocks"},
    {FILTER_ID_GD,
     "filter_pen",
     ICON_PEN,
     "Pen",
     "Show peen data-blocks"},
    {FILTER_ID_GR,
     "filter_group",
     ICON_OUTLINER_COLLECTION,
     "Collections",
     "Show Collection data-blocks"},
    {FILTER_ID_CV, "filter_hair", ICON_CURVES_DATA, "Hairs", "Show/hide Hair data-blocks"},
    {FILTER_ID_IM, "filter_image", ICON_IMAGE_DATA, "Images", "Show Image data-blocks"},
    {FILTER_ID_LA, "filter_light", ICON_LIGHT_DATA, "Lights", "Show Light data-blocks"},
    {FILTER_ID_LP,
     "filter_light_probe",
     ICON_OUTLINER_DATA_LIGHTPROBE,
     "Light Probes",
     "Show Light Probe data-blocks"},
    {FILTER_ID_LS,
     "filter_linestyle",
     ICON_LINE_DATA,
     "Freestyle Linestyles",
     "Show Freestyle's Line Style data-blocks"},
    {FILTER_ID_LT, "filter_lattice", ICON_LATTICE_DATA, "Lattices", "Show Lattice data-blocks"},
    {FILTER_ID_MA,
     "filter_material",
     ICON_MATERIAL_DATA,
     "Materials",
     "Show Material data-blocks"},
    {FILTER_ID_MB, "filter_metaball", ICON_META_DATA, "Metaballs", "Show Metaball data-blocks"},
    {FILTER_ID_MC,
     "filter_movie_clip",
     ICON_TRACKER,
     "Movie Clips",
     "Show Movie Clip data-blocks"},
    {FILTER_ID_ME, "filter_mesh", ICON_MESH_DATA, "Meshes", "Show Mesh data-blocks"},
    {FILTER_ID_MSK, "filter_mask", ICON_MOD_MASK, "Masks", "Show Mask data-blocks"},
    {FILTER_ID_NT, "filter_node_tree", ICON_NODETREE, "Node Trees", "Show Node Tree data-blocks"},
    {FILTER_ID_OB, "filter_object", ICON_OBJECT_DATA, "Objects", "Show Object data-blocks"},
    {FILTER_ID_PA,
     "filter_particle_settings",
     ICON_PARTICLE_DATA,
     "Particles Settings",
     "Show Particle Settings data-blocks"},
    {FILTER_ID_PAL, "filter_palette", ICON_COLOR, "Palettes", "Show Palette data-blocks"},
    {FILTER_ID_PC,
     "filter_paint_curve",
     ICON_CURVE_BEZCURVE,
     "Paint Curves",
     "Show Paint Curve data-blocks"},
    {FILTER_ID_PT,
     "filter_pointcloud",
     ICON_POINTCLOUD_DATA,
     "Point Clouds",
     "Show/hide Point Cloud data-blocks"},
    {FILTER_ID_SCE, "filter_scene", ICON_SCENE_DATA, "Scenes", "Show Scene data-blocks"},
    {FILTER_ID_SIM,
     "filter_simulation",
     ICON_PHYS,
     "Simulations",
     "Show Simulation data-blocks"}, /* TODO: Use correct icon. */
    {FILTER_ID_SPK, "filter_speaker", ICON_SPEAKER, "Speakers", "Show Speaker data-blocks"},
    {FILTER_ID_SO, "filter_sound", ICON_SOUND, "Sounds", "Show Sound data-blocks"},
    {FILTER_ID_TE, "filter_texture", ICON_TEXTURE_DATA, "Textures", "Show Texture data-blocks"},
    {FILTER_ID_TXT, "filter_text", ICON_TEXT, "Texts", "Show Text data-blocks"},
    {FILTER_ID_VF, "filter_font", ICON_FONT_DATA, "Fonts", "Show Font data-blocks"},
    {FILTER_ID_VO, "filter_volume", ICON_VOLUME_DATA, "Volumes", "Show/hide Volume data-blocks"},
    {FILTER_ID_WO, "filter_world", ICON_WORLD_DATA, "Worlds", "Show World data-blocks"},
    {FILTER_ID_WS,
     "filter_work_space",
     ICON_WORKSPACE,
     "Workspaces",
     "Show workspace data-blocks"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "types_anim.h"

#  include "lib_list.h"
#  include "lib_math_base.h"

#  include "dune_anim_data.h"
#  include "dune_global.h" /* XXX, remove me */
#  include "dune_idprop.h"
#  include "dune_idtype.h"
#  include "dune_lib_override.h"
#  include "dune_lib_query.h"
#  include "dune_lib_remap.h"
#  include "dune_lib.h"
#  include "dune_material.h"
#  include "dune_vfont.h"

#  include "graph.h"
#  include "graph_build.h"
#  include "graph_query.h"

#  include "ed_asset.h"

#  include "wm_api.h"

void api_id_override_lib_prop_op_refname_get(ApiPtr *ptr, char *value)
{
  IdOverrideLibPropOp *opop = ptr->data;
  strcpy(value, (opop->subitem_ref_name == NULL) ? "" : opop->subitem_reference_name);
}

int api_id_override_lib_prop_oper_refname_length(ApiPtr *ptr)
{
  IdOverrideLibPropOp *opop = ptr->data;
  return (opop->subitem_ref_name == NULL) ? 0 : strlen(opop->subitem_reference_name);
}

void api_id_override_lib_prop_op_locname_get(PointerRNA *ptr, char *value)
{
  IDOverrideLibraryPropertyOperation *opop = ptr->data;
  strcpy(value, (opop->subitem_local_name == NULL) ? "" : opop->subitem_local_name);
}

int api_id_override_lib_prop_op_locname_length(ApiPtr *ptr)
{
  IdOverrideLibPropOp *opop = ptr->data;
  return (opop->subitem_local_name == NULL) ? 0 : strlen(opop->subitem_local_name);
}

/* name functions that ignore the first two Id characters */
void api_id_name_get(ApiPtr *ptr, char *value)
{
  Id *id = (Id *)ptr->data;
  lib_strncpy(value, id->name + 2, sizeof(id->name) - 2);
}

int api_id_name_length(Apitr *ptr)
{
  Id *id = (Id *)ptr->data;
  return strlen(id->name + 2);
}

void api_id_name_set(ApiPtr *ptr, const char *value)
{
  Id *id = (Id *)ptr->data;
  lib_strncpy_utf8(id->name + 2, value, sizeof(id->name) - 2);
  lib_assert(dune_id_is_in_global_main(id));
  lib_libblock_ensure_unique_name(G_MAIN, id->name);

  if (GS(id->name) == ID_OB) {
    Object *ob = (Object *)id;
    if (ob->type == OB_MBALL) {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }
}

static int api_id_name_editable(ApiPtr *ptr, const char **UNUSED(r_info)
{
  Id *id = (Id *)ptr->data;

  if (GS(id->name) == ID_VF) {
    VFont *vfont = (VFont *)id;
    if (dune_vfont_is_builtin(vfont)) {
      return 0;
    }
  }
  else if (!dune_id_is_in_global_main(id)) {
    return 0;
  }

  return PROP_EDITABLE;
}

void api_id_name_full_get(ApiPtr *ptr, char *value)
{
  Id *id = (Id *)ptr->data;
  dune_id_full_name_get(value, id, 0);
}

int api_id_name_full_length(ApiPtr *ptr)
{
  Id *id = (Id *)ptr->data;
  char name[MAX_ID_FULL_NAME];
  BKE_id_full_name_get(name, id, 0);
  return strlen(name);
}

static int rna_ID_is_evaluated_get(PointerRNA *ptr)
{
  ID *id = (ID *)ptr->data;

  return (DEG_get_original_id(id) != id);
}

static PointerRNA rna_ID_original_get(PointerRNA *ptr)
{
  ID *id = (ID *)ptr->data;

  return rna_pointer_inherit_refine(ptr, &RNA_ID, DEG_get_original_id(id));
}

short api_type_to_id_code(const ApiStruct *type)
{
  const ApiStruct *base_type = api_struct_base_child_of(type, &ApiId);
  if (UNLIKELY(base_type == NULL)) {
    return 0;
  }
  if (base_type == &ApiAction) {
    return ID_AC;
  }
  if (base_type == &ApiArmature) {
    return ID_AR;
  }
  if (base_type == &ApiBrush) {
    return ID_BR;
  }
  if (base_type == &ApiCacheFile) {
    return ID_CF;
  }
  if (base_type == &ApiCamera) {
    return ID_CA;
  }
  if (base_type == &ApiCurve) {
    return ID_CU_LEGACY;
  }
  if (base_type == &ApiPen) {
    return ID_GD;
  }
  if (base_type == &ApiCollection) {
    return ID_GR;
  }
  if (base_type == &ApiImage) {
    return ID_IM;
  }
  if (base_type == &ApiKey) {
    return ID_KE;
  }
  if (base_type == &ApiLight) {
    return ID_LA;
  }
  if (base_type == &ApiLib) {
    return ID_LI;
  }
  if (base_type == &ApiFreestyleLineStyle) {
    return ID_LS;
  }
#  ifdef WITH_NEW_CURVES_TYPE
  if (base_type == &ApiCurves) {
    return ID_CV;
  }
#  endif
  if (base_type == &ApiLattice) {
    return ID_LT;
  }
  if (base_type == &ApiMaterial) {
    return ID_MA;
  }
  if (base_type == &ApiMetaBall) {
    return ID_MB;
  }
  if (base_type == &ApiMovieClip) {
    return ID_MC;
  }
  if (base_type == &ApiMesh) {
    return ID_ME;
  }
  if (base_type == &ApiMask) {
    return ID_MSK;
  }
  if (base_type == &ApiNodeTree) {
    return ID_NT;
  }
  if (base_type == &ApiObject) {
    return ID_OB;
  }
  if (base_type == &ApiParticleSettings) {
    return ID_PA;
  }
  if (base_type == &ApiPalette) {
    return ID_PAL;
  }
  if (base_type == &ApiPaintCurve) {
    return ID_PC;
  }
  if (base_type == &ApiPointCloud) {
    return ID_PT;
  }
  if (base_type == &ApiLightProbe) {
    return ID_LP;
  }
  if (base_type == &ApiScene) {
    return ID_SCE;
  }
  if (base_type == &ApiScreen) {
    return ID_SCR;
  }
#  ifdef WITH_SIMULATION_DATABLOCK
  if (base_type == &ApiSimulation) {
    return ID_SIM;
  }
#  endif
  if (base_type == &ApiSound) {
    return ID_SO;
  }
  if (base_type == &ApiSpeaker) {
    return ID_SPK;
  }
  if (base_type == &ApiTexture) {
    return ID_TE;
  }
  if (base_type == &ApiText) {
    return ID_TXT;
  }
  if (base_type == &ApiVectorFont) {
    return ID_VF;
  }
  if (base_type == &ApiVolume) {
    return ID_VO;
  }
  if (base_type == &ApiWorkSpace) {
    return ID_WS;
  }
  if (base_type == &ApiWorld) {
    return ID_WO;
  }
  if (base_type == &ApiWindowManager) {
    return ID_WM;
  }

  return 0;
}

ApiStruct *id_code_to_api_type(short idcode)
{
  /* NOTE: this switch doesn't use a 'default',
   * so adding new ID's causes a warning. */
  switch ((ID_Type)idcode) {
    case ID_AC:
      return &ApiAction;
    case ID_AR:
      return ApiArmature;
    case ID_BR:
      return &ApiBrush;
    case ID_CA:
      return &ApiCamera;
    case ID_CF:
      return &ApiCacheFile;
    case ID_CU_LEGACY:
      return &ApiCurve;
    case ID_GD:
      return &ApiPen;
    case ID_GR:
      return &ApiCollection;
    case ID_CV:
#  ifdef WITH_NEW_CURVES_TYPE
      return &ApiCurves;
#  else
      return &ApiId;
#  endif
    case ID_IM:
      return &ApiImage;
    case ID_KE:
      return &ApiKey;
    case ID_LA:
      return &ApiLight;
    case ID_LI:
      return &ApiLib;
    case ID_LS:
      return &ApiFreestyleLineStyle;
    case ID_LT:
      return &ApiLattice;
    case ID_MA:
      return &ApiMaterial;
    case ID_MB:
      return &ApiMetaBall;
    case ID_MC:
      return &ApiMovieClip;
    case ID_ME:
      return &ApiMesh;
    case ID_MSK:
      return &ApiMask;
    case ID_NT:
      return &ApiNodeTree;
    case ID_OB:
      return &ApiObject;
    case ID_PA:
      return &ApiParticleSettings;
    case ID_PAL:
      return &ApiPalette;
    case ID_PC:
      return &ApiPaintCurve;
    case ID_PT:
      return &ApiPointCloud;
    case ID_LP:
      return &ApiLightProbe;
    case ID_SCE:
      return &ApiScene;
    case ID_SCR:
      return &ApiScreen;
    case ID_SIM:
#  ifdef WITH_SIMULATION_DATABLOCK
      return &ApiSimulation;
#  else
      return &ApiId;
#  endif
    case ID_SO:
      return &ApiSound;
    case ID_SPK:
      return &ApiSpeaker;
    case ID_TE:
      return &ApiTexture;
    case ID_TXT:
      return &ApiText;
    case ID_VF:
      return &ApiVectorFont;
    case ID_VO:
      return &ApiVolume;
    case ID_WM:
      return &ApiWindowManager;
    case ID_WO:
      return &ApiWorld;
    case ID_WS:
      return &ApiWorkSpace;

    /* deprecated */
    case ID_IP:
      break;
  }

  return &ApiId;
}

ApiStruct *api_id_refine(ApiPtr *ptr)
{
  Id *id = (Id *)ptr->data;

  return id_code_to_api_type(GS(id->name));
}

IdProp **api_id_idprops(ApiPtr *ptr)
{
  Id *id = (Id *)ptr->data;
  return &id->props;
}

void api_id_fake_user_set(ApiPtr *ptr, bool value)
{
  Id *id = (Id *)ptr->data;

  if (value) {
    id_fake_user_set(id);
  }
  else {
    id_fake_user_clear(id);
  }
}

IdProp **api_PropGroup_idprops(ApiPtr *ptr)
{
  return (IdProp **)&ptr->data;
}

void api_PropGroup_unregister(Main *UNUSED(main), ApiStruct *type)
{
  api_struct_free(&DuneApi, type);
}

ApiStruct *api_PropGroup_register(Main *UNUSED(main),
                                  ReportList *reports,
                                  void *data,
                                  const char *id,
                                  StructValidateFn validate,
                                  StructCbFn UNUSED(call),
                                  StructFreeFn UNUSED(free))
{
  ApiPtr dummyptr;

  /* create dummy pointer */
  api_ptr_create(NULL, &ApiPropGroup, NULL, &dummyptr);

  /* validate the python class */
  if (validate(&dummyptr, data, NULL) != 0) {
    return NULL;
  }

  /* NOTE: it looks like there is no length limit on the srna id since its
   * just a char pointer, but take care here, also be careful that python
   * owns the string pointer which it could potentially free while dune
   * is running. */
  if (lib_strnlen(id, MAX_IDPROP_NAME) == MAX_IDPROP_NAME) {
        dune_reportf(reports,
            RPT_ERROR,
            "Registering id prop class: '%s' is too long, maximum length is %d",
            id,
            MAX_IDPROP_NAME);
    return NULL;
  }

  return api_def_struct_ptr(&DuneApi, id, &ApiPropGroup); /* XXX */
}

ApiStruct *api_PropGroup_refine(ApiPtr *ptr)
{
  return ptr->type;
}

static Id *api_id_eval_get(Id *id, struct Graph *graph)
{
  return graph_get_evaluated_id(graph, id);
}

static Id *api_id_copy(Id *id, Main *main)
{
  Id *newid = dune_id_copy(main, id);

  if (newid != NULL) {
    id_us_min(newid);
  }

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return newid;
}

static void api_id_asset_mark(Id *id)
{
  if (ed_asset_mark_id(id)) {
    wm_main_add_notifier(NC_ID | NA_EDITED, NULL);
    wm_main_add_notifier(NC_ASSET | NA_ADDED, NULL);
  }
}

static void api_id_asset_generate_preview(Id *id, Cxt *C)
{
  ed_asset_generate_preview(C, id);

  wm_main_add_notifier(NC_ID | NA_EDITED, NULL);
  wm_main_add_notifier(NC_ASSET | NA_EDITED, NULL);
}

static void api_id_asset_clear(Id *id)
{
  if (ed_asset_clear_id(id)) {
    wm_main_add_notifier(NC_ID | NA_EDITED, NULL);
    wm_main_add_notifier(NC_ASSET | NA_REMOVED, NULL);
  }
}

static Id *api_id_override_create(Id *id, Main *main, bool remap_local_usages)
{
  if (!ID_IS_OVERRIDABLE_LIB(id)) {
    return NULL;
  }

  if (remap_local_usages) {
    dune_main_id_tag_all(main, LIB_TAG_DOIT, true);
  }

  Id *local_id = dune_lib_override_lib_create_from_id(main, id, remap_local_usages);

  if (remap_local_usages) {
    dune_main_id_tag_all(bmain, LIB_TAG_DOIT, false);
  }

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);
  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return local_id;
}

static Id *api_id_override_hierarchy_create(
    Id *id, Main *main, Scene *scene, ViewLayer *view_layer, Id *id_instance_hint)
{
  if (!ID_IS_OVERRIDABLE_LIB(id)) {
    return NULL;
  }

  dune_main_id_tag_all(main, LIB_TAG_DOIT, false);

  Id *id_root_override = NULL;
  dune_lib_override_lib_create(
      main, scene, view_layer, NULL, id, id, id_instance_hint, &id_root_override);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);
  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return id_root_override;
}

static void api_id_override_template_create(IDd*id, ReportList *reports)
{
  if (!U.experimental.use_override_templates) {
    dune_report(reports, RPT_ERROR, "Override template experimental feature is disabled");
    return;
  }
  if (ID_IS_LINKED(id)) {
    dune_report(reports, RPT_ERROR, "Unable to create override template for linked data-blocks");
    return;
  }
  if (ID_IS_OVERRIDE_LIB(id)) {
    dune_report(
        reports, RPT_ERROR, "Unable to create override template for overridden data-blocks");
    return;
  }
  dune_lib_override_lib_template_create(id);

  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static void api_id_override_lib_ops_update(Id *id,
                                           IdOverrideLib *UNUSED(override_lib),
                                           Main *main,
                                           ReportList *reports)
{
  if (!ID_IS_OVERRIDE_LIB_REAL(id)) {
    dune_reportf(reports, RPT_ERROR, "ID '%s' isn't an override", id->name);
    return;
  }

  if (ID_IS_LINKED(id)) {
    dune_reportf(reports, RPT_ERROR, "ID '%s' is linked, cannot edit its overrides", id->name);
    return;
  }

  dune_lib_override_lib_ops_create(main, id);

  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static void api_id_override_lib_reset(Id *id,
                                      IdOverrideLib *UNUSED(override_lib),
                                      Main *main,
                                      ReportList *reports,
                                      bool do_hierarchy)
{
  if (!ID_IS_OVERRIDE_LIB_REAL(id)) {
    dune_reportf(reports, RPT_ERROR, "ID '%s' isn't an override", id->name);
    return;
  }

  if (do_hierarchy) {
    dune_lib_override_lib_id_hierarchy_reset(main, id);
  }
  else {
    dune_lib_override_lib_id_reset(main, id);
  }

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static void rna_ID_override_library_destroy(ID *id,
                                            IDOverrideLibrary *UNUSED(override_library),
                                            Main *bmain,
                                            ReportList *reports,
                                            bool do_hierarchy)
{
  if (!ID_IS_OVERRIDE_LIBRARY_REAL(id)) {
    BKE_reportf(reports, RPT_ERROR, "ID '%s' isn't an override", id->name);
    return;
  }

  if (do_hierarchy) {
    BKE_lib_override_library_delete(bmain, id);
  }
  else {
    BKE_libblock_remap(bmain, id, id->override_library->reference, ID_REMAP_SKIP_INDIRECT_USAGE);
    BKE_id_delete(bmain, id);
  }

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static IDOverrideLibraryProperty *rna_ID_override_library_properties_add(
    IDOverrideLibrary *override_library, ReportList *reports, const char rna_path[])
{
  bool created;
  IDOverrideLibraryProperty *result = BKE_lib_override_library_property_get(
      override_library, rna_path, &created);

  if (!created) {
    BKE_report(reports, RPT_DEBUG, "No new override property created, property already exists");
  }

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
  return result;
}

static void rna_ID_override_library_properties_remove(IDOverrideLibrary *override_library,
                                                      ReportList *reports,
                                                      IDOverrideLibraryProperty *override_property)
{
  if (BLI_findindex(&override_library->properties, override_property) == -1) {
    BKE_report(reports, RPT_ERROR, "Override property cannot be removed");
    return;
  }

  BKE_lib_override_library_property_delete(override_library, override_property);

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static IDOverrideLibraryPropertyOperation *rna_ID_override_library_property_operations_add(
    IDOverrideLibraryProperty *override_property,
    ReportList *reports,
    int operation,
    const char *subitem_refname,
    const char *subitem_locname,
    int subitem_refindex,
    int subitem_locindex)
{
  bool created;
  bool strict;
  IDOverrideLibraryPropertyOperation *result = BKE_lib_override_library_property_operation_get(
      override_property,
      operation,
      subitem_refname,
      subitem_locname,
      subitem_refindex,
      subitem_locindex,
      false,
      &strict,
      &created);
  if (!created) {
    BKE_report(reports, RPT_DEBUG, "No new override operation created, operation already exists");
  }

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
  return result;
}

static void rna_ID_override_library_property_operations_remove(
    IDOverrideLibraryProperty *override_property,
    ReportList *reports,
    IDOverrideLibraryPropertyOperation *override_operation)
{
  if (BLI_findindex(&override_property->operations, override_operation) == -1) {
    BKE_report(reports, RPT_ERROR, "Override operation cannot be removed");
    return;
  }

  BKE_lib_override_library_property_operation_delete(override_property, override_operation);

  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
}

static void rna_ID_update_tag(ID *id, Main *bmain, ReportList *reports, int flag)
{
  /* XXX, new function for this! */
#  if 0
  if (ob->type == OB_FONT) {
    Curve *cu = ob->data;
    freedisplist(&cu->disp);
    BKE_vfont_to_curve(bmain, sce, ob, FO_EDIT, NULL);
  }
#  endif

  if (flag == 0) {
    /* pass */
  }
  else {
    int allow_flag = 0;

    /* ensure flag us correct for the type */
    switch (GS(id->name)) {
      case ID_OB:
        /* TODO(sergey): This is kind of difficult to predict since different
         * object types supports different flags. Maybe does not worth checking
         * for this at all. Or maybe let dependency graph to return whether
         * the tag was valid or not. */
        allow_flag = ID_RECALC_ALL;
        break;
        /* Could add particle updates later */
#  if 0
      case ID_PA:
        allow_flag = OB_RECALC_ALL | PSYS_RECALC;
        break;
#  endif
      case ID_AC:
        allow_flag = ID_RECALC_ANIMATION;
        break;
      default:
        if (id_can_have_animdata(id)) {
          allow_flag = ID_RECALC_ANIMATION;
        }
    }

    if (flag & ~allow_flag) {
      StructRNA *srna = ID_code_to_RNA_type(GS(id->name));
      BKE_reportf(reports,
                  RPT_ERROR,
                  "%s is not compatible with %s 'refresh' options",
                  RNA_struct_identifier(srna),
                  allow_flag ? "the specified" : "any");
      return;
    }
  }

  DEG_id_tag_update_ex(bmain, id, flag);
}

static void rna_ID_user_clear(ID *id)
{
  id_fake_user_clear(id);
  id->us = 0; /* don't save */
}

static void rna_ID_user_remap(ID *id, Main *bmain, ID *new_id)
{
  if ((GS(id->name) == GS(new_id->name)) && (id != new_id)) {
    /* For now, do not allow remapping data in linked data from here... */
    BKE_libblock_remap(
        bmain, id, new_id, ID_REMAP_SKIP_INDIRECT_USAGE | ID_REMAP_SKIP_NEVER_NULL_USAGE);
  }
}

static struct ID *rna_ID_make_local(struct ID *self, Main *bmain, bool UNUSED(clear_proxy))
{
  BKE_lib_id_make_local(bmain, self, 0);

  ID *ret_id = self->newid ? self->newid : self;
  BKE_id_newptr_and_tag_clear(self);
  return ret_id;
}

static AnimData *rna_ID_animation_data_create(ID *id, Main *bmain)
{
  AnimData *adt = BKE_animdata_ensure_id(id);
  DEG_relations_tag_update(bmain);
  return adt;
}

static void rna_ID_animation_data_free(ID *id, Main *bmain)
{
  BKE_animdata_free(id, true);
  DEG_relations_tag_update(bmain);
}

#  ifdef WITH_PYTHON
void **rna_ID_instance(PointerRNA *ptr)
{
  ID *id = (ID *)ptr->data;
  return &id->py_instance;
}
#  endif

static void rna_IDPArray_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  IDProperty *prop = (IDProperty *)ptr->data;
  rna_iterator_array_begin(iter, IDP_IDPArray(prop), sizeof(IDProperty), prop->len, 0, NULL);
}

static int rna_IDPArray_length(PointerRNA *ptr)
{
  IDProperty *prop = (IDProperty *)ptr->data;
  return prop->len;
}

int rna_IDMaterials_assign_int(PointerRNA *ptr, int key, const PointerRNA *assign_ptr)
{
  ID *id = ptr->owner_id;
  short *totcol = BKE_id_material_len_p(id);
  Material *mat_id = (Material *)assign_ptr->owner_id;
  if (totcol && (key >= 0 && key < *totcol)) {
    BLI_assert(BKE_id_is_in_global_main(id));
    BLI_assert(BKE_id_is_in_global_main(&mat_id->id));
    BKE_id_material_assign(G_MAIN, id, mat_id, key + 1);
    return 1;
  }
  else {
    return 0;
  }
}

static void rna_IDMaterials_append_id(ID *id, Main *bmain, Material *ma)
{
  BKE_id_material_append(bmain, id, ma);

  WM_main_add_notifier(NC_OBJECT | ND_DRAW, id);
  WM_main_add_notifier(NC_OBJECT | ND_OB_SHADING, id);
}

static Material *rna_IDMaterials_pop_id(ID *id, Main *bmain, ReportList *reports, int index_i)
{
  Material *ma;
  short *totcol = BKE_id_material_len_p(id);
  const short totcol_orig = *totcol;
  if (index_i < 0) {
    index_i += (*totcol);
  }

  if ((index_i < 0) || (index_i >= (*totcol))) {
    BKE_report(reports, RPT_ERROR, "Index out of range");
    return NULL;
  }

  ma = BKE_id_material_pop(bmain, id, index_i);

  if (*totcol == totcol_orig) {
    BKE_report(reports, RPT_ERROR, "No material to removed");
    return NULL;
  }

  DEG_id_tag_update(id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, id);
  WM_main_add_notifier(NC_OBJECT | ND_OB_SHADING, id);

  return ma;
}

static void rna_IDMaterials_clear_id(ID *id, Main *bmain)
{
  BKE_id_material_clear(bmain, id);

  DEG_id_tag_update(id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, id);
  WM_main_add_notifier(NC_OBJECT | ND_OB_SHADING, id);
}

static void rna_Library_filepath_set(PointerRNA *ptr, const char *value)
{
  Library *lib = (Library *)ptr->data;
  BLI_assert(BKE_id_is_in_global_main(&lib->id));
  BKE_library_filepath_set(G_MAIN, lib, value);
}

/* ***** ImagePreview ***** */

static void rna_ImagePreview_is_custom_set(PointerRNA *ptr, int value, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  if ((value && (prv_img->flag[size] & PRV_USER_EDITED)) ||
      (!value && !(prv_img->flag[size] & PRV_USER_EDITED))) {
    return;
  }

  if (value) {
    prv_img->flag[size] |= PRV_USER_EDITED;
  }
  else {
    prv_img->flag[size] &= ~PRV_USER_EDITED;
  }

  prv_img->flag[size] |= PRV_CHANGED;

  BKE_previewimg_clear_single(prv_img, size);
}

static void rna_ImagePreview_size_get(PointerRNA *ptr, int *values, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_ensure(prv_img, size);

  values[0] = prv_img->w[size];
  values[1] = prv_img->h[size];
}

static void rna_ImagePreview_size_set(PointerRNA *ptr, const int *values, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_clear_single(prv_img, size);

  if (values[0] && values[1]) {
    prv_img->rect[size] = MEM_callocN(values[0] * values[1] * sizeof(unsigned int), "prv_rect");

    prv_img->w[size] = values[0];
    prv_img->h[size] = values[1];
  }

  prv_img->flag[size] |= (PRV_CHANGED | PRV_USER_EDITED);
}

static int rna_ImagePreview_pixels_get_length(PointerRNA *ptr,
                                              int length[RNA_MAX_ARRAY_DIMENSION],
                                              enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_ensure(prv_img, size);

  length[0] = prv_img->w[size] * prv_img->h[size];

  return length[0];
}

static void rna_ImagePreview_pixels_get(PointerRNA *ptr, int *values, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_ensure(prv_img, size);

  memcpy(values, prv_img->rect[size], prv_img->w[size] * prv_img->h[size] * sizeof(unsigned int));
}

static void rna_ImagePreview_pixels_set(PointerRNA *ptr, const int *values, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  memcpy(prv_img->rect[size], values, prv_img->w[size] * prv_img->h[size] * sizeof(unsigned int));
  prv_img->flag[size] |= PRV_USER_EDITED;
}

static int rna_ImagePreview_pixels_float_get_length(PointerRNA *ptr,
                                                    int length[RNA_MAX_ARRAY_DIMENSION],
                                                    enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  BLI_assert(sizeof(unsigned int) == 4);

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_ensure(prv_img, size);

  length[0] = prv_img->w[size] * prv_img->h[size] * 4;

  return length[0];
}

static void rna_ImagePreview_pixels_float_get(PointerRNA *ptr, float *values, enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  unsigned char *data = (unsigned char *)prv_img->rect[size];
  const size_t len = prv_img->w[size] * prv_img->h[size] * 4;
  size_t i;

  BLI_assert(sizeof(unsigned int) == 4);

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  BKE_previewimg_ensure(prv_img, size);

  for (i = 0; i < len; i++) {
    values[i] = data[i] * (1.0f / 255.0f);
  }
}

static void rna_ImagePreview_pixels_float_set(PointerRNA *ptr,
                                              const float *values,
                                              enum eIconSizes size)
{
  ID *id = ptr->owner_id;
  PreviewImage *prv_img = (PreviewImage *)ptr->data;

  unsigned char *data = (unsigned char *)prv_img->rect[size];
  const size_t len = prv_img->w[size] * prv_img->h[size] * 4;
  size_t i;

  BLI_assert(sizeof(unsigned int) == 4);

  if (id != NULL) {
    BLI_assert(prv_img == BKE_previewimg_id_ensure(id));
  }

  for (i = 0; i < len; i++) {
    data[i] = unit_float_to_uchar_clamp(values[i]);
  }
  prv_img->flag[size] |= PRV_USER_EDITED;
}

static void rna_ImagePreview_is_image_custom_set(PointerRNA *ptr, bool value)
{
  rna_ImagePreview_is_custom_set(ptr, value, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_size_get(PointerRNA *ptr, int *values)
{
  rna_ImagePreview_size_get(ptr, values, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_size_set(PointerRNA *ptr, const int *values)
{
  rna_ImagePreview_size_set(ptr, values, ICON_SIZE_PREVIEW);
}

static int rna_ImagePreview_image_pixels_get_length(PointerRNA *ptr,
                                                    int length[RNA_MAX_ARRAY_DIMENSION])
{
  return rna_ImagePreview_pixels_get_length(ptr, length, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_pixels_get(PointerRNA *ptr, int *values)
{
  rna_ImagePreview_pixels_get(ptr, values, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_pixels_set(PointerRNA *ptr, const int *values)
{
  rna_ImagePreview_pixels_set(ptr, values, ICON_SIZE_PREVIEW);
}

static int rna_ImagePreview_image_pixels_float_get_length(PointerRNA *ptr,
                                                          int length[RNA_MAX_ARRAY_DIMENSION])
{
  return rna_ImagePreview_pixels_float_get_length(ptr, length, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_pixels_float_get(PointerRNA *ptr, float *values)
{
  rna_ImagePreview_pixels_float_get(ptr, values, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_image_pixels_float_set(PointerRNA *ptr, const float *values)
{
  rna_ImagePreview_pixels_float_set(ptr, values, ICON_SIZE_PREVIEW);
}

static void rna_ImagePreview_is_icon_custom_set(PointerRNA *ptr, bool value)
{
  rna_ImagePreview_is_custom_set(ptr, value, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_size_get(PointerRNA *ptr, int *values)
{
  rna_ImagePreview_size_get(ptr, values, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_size_set(PointerRNA *ptr, const int *values)
{
  rna_ImagePreview_size_set(ptr, values, ICON_SIZE_ICON);
}

static int rna_ImagePreview_icon_pixels_get_length(PointerRNA *ptr,
                                                   int length[RNA_MAX_ARRAY_DIMENSION])
{
  return rna_ImagePreview_pixels_get_length(ptr, length, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_pixels_get(PointerRNA *ptr, int *values)
{
  rna_ImagePreview_pixels_get(ptr, values, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_pixels_set(PointerRNA *ptr, const int *values)
{
  rna_ImagePreview_pixels_set(ptr, values, ICON_SIZE_ICON);
}

static int rna_ImagePreview_icon_pixels_float_get_length(PointerRNA *ptr,
                                                         int length[RNA_MAX_ARRAY_DIMENSION])
{
  return rna_ImagePreview_pixels_float_get_length(ptr, length, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_pixels_float_get(PointerRNA *ptr, float *values)
{
  rna_ImagePreview_pixels_float_get(ptr, values, ICON_SIZE_ICON);
}

static void rna_ImagePreview_icon_pixels_float_set(PointerRNA *ptr, const float *values)
{
  rna_ImagePreview_pixels_float_set(ptr, values, ICON_SIZE_ICON);
}

static int rna_ImagePreview_icon_id_get(PointerRNA *ptr)
{
  /* Using a callback here allows us to only generate icon matching
   * that preview when icon_id is requested. */
  return BKE_icon_preview_ensure(ptr->owner_id, (PreviewImage *)(ptr->data));
}
static void rna_ImagePreview_icon_reload(PreviewImage *prv)
{
  /* will lazy load on next use, but only in case icon is not user-modified! */
  if (!(prv->flag[ICON_SIZE_ICON] & PRV_USER_EDITED) &&
      !(prv->flag[ICON_SIZE_PREVIEW] & PRV_USER_EDITED)) {
    BKE_previewimg_clear(prv);
  }
}

static PointerRNA rna_IDPreview_get(PointerRNA *ptr)
{
  ID *id = (ID *)ptr->data;
  PreviewImage *prv_img = BKE_previewimg_id_get(id);

  return rna_pointer_inherit_refine(ptr, &RNA_ImagePreview, prv_img);
}

static IDProperty **rna_IDPropertyWrapPtr_idprops(PointerRNA *ptr)
{
  if (ptr == NULL) {
    return NULL;
  }
  return (IDProperty **)&ptr->data;
}

static void rna_Library_version_get(PointerRNA *ptr, int *value)
{
  Library *lib = (Library *)ptr->data;
  value[0] = lib->versionfile / 100;
  value[1] = lib->versionfile % 100;
  value[2] = lib->subversionfile;
}

#else

static void rna_def_ID_properties(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  /* this is struct is used for holding the virtual
   * PropertyRNA's for ID properties */
  srna = RNA_def_struct(brna, "PropertyGroupItem", NULL);
  RNA_def_struct_sdna(srna, "IDProperty");
  RNA_def_struct_ui_text(
      srna, "ID Property", "Property that stores arbitrary, user defined properties");

  /* IDP_STRING */
  prop = RNA_def_property(srna, "string", PROP_STRING, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);

  /* IDP_INT */
  prop = RNA_def_property(srna, "int", PROP_INT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);

  prop = RNA_def_property(srna, "int_array", PROP_INT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 1);

  /* IDP_FLOAT */
  prop = RNA_def_property(srna, "float", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);

  prop = RNA_def_property(srna, "float_array", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 1);

  /* IDP_DOUBLE */
  prop = RNA_def_property(srna, "double", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);

  prop = RNA_def_property(srna, "double_array", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_array(prop, 1);

  /* IDP_GROUP */
  prop = RNA_def_property(srna, "group", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "PropertyGroup");

  prop = RNA_def_property(srna, "collection", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_struct_type(prop, "PropertyGroup");

  prop = RNA_def_property(srna, "idp_array", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "PropertyGroup");
  RNA_def_property_collection_funcs(prop,
                                    "rna_IDPArray_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_IDPArray_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);

  /* never tested, maybe its useful to have this? */
#  if 0
  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Name", "Unique name used in the code and scripting");
  RNA_def_struct_name_property(srna, prop);
#  endif

  /* IDP_ID */
  prop = RNA_def_property(srna, "id", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY | PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "ID");

  /* ID property groups > level 0, since level 0 group is merged
   * with native RNA properties. the builtin_properties will take
   * care of the properties here */
  srna = RNA_def_struct(brna, "PropertyGroup", NULL);
  RNA_def_struct_sdna(srna, "IDPropertyGroup");
  RNA_def_struct_ui_text(srna, "ID Property Group", "Group of ID properties");
  RNA_def_struct_idprops_func(srna, "rna_PropertyGroup_idprops");
  RNA_def_struct_register_funcs(
      srna, "rna_PropertyGroup_register", "rna_PropertyGroup_unregister", NULL);
  RNA_def_struct_refine_func(srna, "rna_PropertyGroup_refine");

  /* important so python types can have their name used in list views
   * however this isn't perfect because it overrides how python would set the name
   * when we only really want this so RNA_def_struct_name_property() is set to something useful */
  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_flag(prop, PROP_IDPROPERTY);
  /*RNA_def_property_clear_flag(prop, PROP_EDITABLE); */
  RNA_def_property_ui_text(prop, "Name", "Unique name used in the code and scripting");
  RNA_def_struct_name_property(srna, prop);
}

static void rna_def_ID_materials(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  /* for mesh/mball/curve materials */
  srna = RNA_def_struct(brna, "IDMaterials", NULL);
  RNA_def_struct_sdna(srna, "ID");
  RNA_def_struct_ui_text(srna, "ID Materials", "Collection of materials");

  func = RNA_def_function(srna, "append", "rna_IDMaterials_append_id");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Add a new material to the data-block");
  parm = RNA_def_pointer(func, "material", "Material", "", "Material to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "pop", "rna_IDMaterials_pop_id");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Remove a material from the data-block");
  parm = RNA_def_int(
      func, "index", -1, -MAXMAT, MAXMAT, "", "Index of material to remove", 0, MAXMAT);
  parm = RNA_def_pointer(func, "material", "Material", "", "Material to remove");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "clear", "rna_IDMaterials_clear_id");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Remove all materials from the data-block");
}

static void rna_def_image_preview(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ImagePreview", NULL);
  RNA_def_struct_sdna(srna, "PreviewImage");
  RNA_def_struct_ui_text(srna, "Image Preview", "Preview image and icon");

  prop = RNA_def_property(srna, "is_image_custom", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag[ICON_SIZE_PREVIEW]", PRV_USER_EDITED);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_ImagePreview_is_image_custom_set");
  RNA_def_property_ui_text(prop,
                           "Custom Image",
                           "True if this preview image has been modified by py script,"
                           "and is no more auto-generated by Blender");

  prop = RNA_def_int_vector(
      srna, "image_size", 2, NULL, 0, 0, "Image Size", "Width and height in pixels", 0, 0);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  RNA_def_property_int_funcs(
      prop, "rna_ImagePreview_image_size_get", "rna_ImagePreview_image_size_set", NULL);

  prop = RNA_def_property(srna, "image_pixels", PROP_INT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_multi_array(prop, 1, NULL);
  RNA_def_property_ui_text(prop, "Image Pixels", "Image pixels, as bytes (always 32-bit RGBA)");
  RNA_def_property_dynamic_array_funcs(prop, "rna_ImagePreview_image_pixels_get_length");
  RNA_def_property_int_funcs(
      prop, "rna_ImagePreview_image_pixels_get", "rna_ImagePreview_image_pixels_set", NULL);

  prop = RNA_def_property(srna, "image_pixels_float", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_multi_array(prop, 1, NULL);
  RNA_def_property_ui_text(
      prop, "Float Image Pixels", "Image pixels components, as floats (RGBA concatenated values)");
  RNA_def_property_dynamic_array_funcs(prop, "rna_ImagePreview_image_pixels_float_get_length");
  RNA_def_property_float_funcs(prop,
                               "rna_ImagePreview_image_pixels_float_get",
                               "rna_ImagePreview_image_pixels_float_set",
                               NULL);

  prop = RNA_def_property(srna, "is_icon_custom", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag[ICON_SIZE_ICON]", PRV_USER_EDITED);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_ImagePreview_is_icon_custom_set");
  RNA_def_property_ui_text(prop,
                           "Custom Icon",
                           "True if this preview icon has been modified by py script,"
                           "and is no more auto-generated by Blender");

  prop = RNA_def_int_vector(
      srna, "icon_size", 2, NULL, 0, 0, "Icon Size", "Width and height in pixels", 0, 0);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  RNA_def_property_int_funcs(
      prop, "rna_ImagePreview_icon_size_get", "rna_ImagePreview_icon_size_set", NULL);

  prop = RNA_def_property(srna, "icon_pixels", PROP_INT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_multi_array(prop, 1, NULL);
  RNA_def_property_ui_text(prop, "Icon Pixels", "Icon pixels, as bytes (always 32-bit RGBA)");
  RNA_def_property_dynamic_array_funcs(prop, "rna_ImagePreview_icon_pixels_get_length");
  RNA_def_property_int_funcs(
      prop, "rna_ImagePreview_icon_pixels_get", "rna_ImagePreview_icon_pixels_set", NULL);

  prop = RNA_def_property(srna, "icon_pixels_float", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_multi_array(prop, 1, NULL);
  RNA_def_property_ui_text(
      prop, "Float Icon Pixels", "Icon pixels components, as floats (RGBA concatenated values)");
  RNA_def_property_dynamic_array_funcs(prop, "rna_ImagePreview_icon_pixels_float_get_length");
  RNA_def_property_float_funcs(prop,
                               "rna_ImagePreview_icon_pixels_float_get",
                               "rna_ImagePreview_icon_pixels_float_set",
                               NULL);

  prop = RNA_def_int(srna,
                     "icon_id",
                     0,
                     INT_MIN,
                     INT_MAX,
                     "Icon ID",
                     "Unique integer identifying this preview as an icon (zero means invalid)",
                     INT_MIN,
                     INT_MAX);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_ImagePreview_icon_id_get", NULL, NULL);

  func = RNA_def_function(srna, "reload", "rna_ImagePreview_icon_reload");
  RNA_def_function_ui_description(func, "Reload the preview from its source path");
}

static void rna_def_ID_override_library_property_operation(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem override_library_property_flag_items[] = {
      {IDOVERRIDE_LIBRARY_FLAG_MANDATORY,
       "MANDATORY",
       0,
       "Mandatory",
       "For templates, prevents the user from removing predefined operation (NOT USED)"},
      {IDOVERRIDE_LIBRARY_FLAG_LOCKED,
       "LOCKED",
       0,
       "Locked",
       "Prevents the user from modifying that override operation (NOT USED)"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "IDOverrideLibraryPropertyOperation", NULL);
  RNA_def_struct_ui_text(srna,
                         "ID Library Override Property Operation",
                         "Description of an override operation over an overridden property");

  prop = RNA_def_enum(srna,
                      "operation",
                      rna_enum_override_library_property_operation_items,
                      IDOVERRIDE_LIBRARY_OP_REPLACE,
                      "Operation",
                      "What override operation is performed");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */

  prop = RNA_def_enum(
      srna, "flag", override_library_property_flag_items, 0, "Flags", "Optional flags (NOT USED)");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */

  prop = RNA_def_string(srna,
                        "subitem_reference_name",
                        NULL,
                        INT_MAX,
                        "Subitem Reference Name",
                        "Used to handle insertions into collection");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */
  RNA_def_property_string_funcs(prop,
                                "rna_ID_override_library_property_operation_refname_get",
                                "rna_ID_override_library_property_operation_refname_length",
                                NULL);

  prop = RNA_def_string(srna,
                        "subitem_local_name",
                        NULL,
                        INT_MAX,
                        "Subitem Local Name",
                        "Used to handle insertions into collection");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */
  RNA_def_property_string_funcs(prop,
                                "rna_ID_override_library_property_operation_locname_get",
                                "rna_ID_override_library_property_operation_locname_length",
                                NULL);

  prop = RNA_def_int(srna,
                     "subitem_reference_index",
                     -1,
                     -1,
                     INT_MAX,
                     "Subitem Reference Index",
                     "Used to handle insertions into collection",
                     -1,
                     INT_MAX);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */

  prop = RNA_def_int(srna,
                     "subitem_local_index",
                     -1,
                     -1,
                     INT_MAX,
                     "Subitem Local Index",
                     "Used to handle insertions into collection",
                     -1,
                     INT_MAX);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */
}

static void rna_def_ID_override_library_property_operations(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "IDOverrideLibraryPropertyOperations");
  srna = RNA_def_struct(brna, "IDOverrideLibraryPropertyOperations", NULL);
  RNA_def_struct_sdna(srna, "IDOverrideLibraryProperty");
  RNA_def_struct_ui_text(srna, "Override Operations", "Collection of override operations");

  /* Add Property */
  func = RNA_def_function(srna, "add", "rna_ID_override_library_property_operations_add");
  RNA_def_function_ui_description(func, "Add a new operation");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_enum(func,
                      "operation",
                      rna_enum_override_library_property_operation_items,
                      IDOVERRIDE_LIBRARY_OP_REPLACE,
                      "Operation",
                      "What override operation is performed");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "subitem_reference_name",
                        NULL,
                        INT_MAX,
                        "Subitem Reference Name",
                        "Used to handle insertions into collection");
  parm = RNA_def_string(func,
                        "subitem_local_name",
                        NULL,
                        INT_MAX,
                        "Subitem Local Name",
                        "Used to handle insertions into collection");
  parm = RNA_def_int(func,
                     "subitem_reference_index",
                     -1,
                     -1,
                     INT_MAX,
                     "Subitem Reference Index",
                     "Used to handle insertions into collection",
                     -1,
                     INT_MAX);
  parm = RNA_def_int(func,
                     "subitem_local_index",
                     -1,
                     -1,
                     INT_MAX,
                     "Subitem Local Index",
                     "Used to handle insertions into collection",
                     -1,
                     INT_MAX);
  parm = RNA_def_pointer(func,
                         "property",
                         "IDOverrideLibraryPropertyOperation",
                         "New Operation",
                         "Created operation");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_ID_override_library_property_operations_remove");
  RNA_def_function_ui_description(func, "Remove and delete an operation");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func,
                         "operation",
                         "IDOverrideLibraryPropertyOperation",
                         "Operation",
                         "Override operation to be deleted");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

static void rna_def_ID_override_library_property(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "IDOverrideLibraryProperty", NULL);
  RNA_def_struct_ui_text(
      srna, "ID Library Override Property", "Description of an overridden property");

  /* String pointer, we *should* add get/set/etc.
   * But NULL rna_path would be a nasty bug anyway. */
  prop = RNA_def_string(srna,
                        "rna_path",
                        NULL,
                        INT_MAX,
                        "RNA Path",
                        "RNA path leading to that property, from owning ID");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* For now. */

  prop = RNA_def_collection(srna,
                            "operations",
                            "IDOverrideLibraryPropertyOperation",
                            "Operations",
                            "List of overriding operations for a property");
  RNA_def_property_update(prop, NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
  rna_def_ID_override_library_property_operations(brna, prop);

  rna_def_ID_override_library_property_operation(brna);
}

static void rna_def_ID_override_library_properties(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "IDOverrideLibraryProperties");
  srna = RNA_def_struct(brna, "IDOverrideLibraryProperties", NULL);
  RNA_def_struct_sdna(srna, "IDOverrideLibrary");
  RNA_def_struct_ui_text(srna, "Override Properties", "Collection of override properties");

  /* Add Property */
  func = RNA_def_function(srna, "add", "rna_ID_override_library_properties_add");
  RNA_def_function_ui_description(
      func, "Add a property to the override library when it doesn't exist yet");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func,
                         "property",
                         "IDOverrideLibraryProperty",
                         "New Property",
                         "Newly created override property or existing one");
  RNA_def_function_return(func, parm);
  parm = RNA_def_string(
      func, "rna_path", NULL, 256, "RNA Path", "RNA-Path of the property to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "remove", "rna_ID_override_library_properties_remove");
  RNA_def_function_ui_description(func, "Remove and delete a property");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func,
                         "property",
                         "IDOverrideLibraryProperty",
                         "Property",
                         "Override property to be deleted");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

static void rna_def_ID_override_library(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  FunctionRNA *func;

  srna = RNA_def_struct(brna, "IDOverrideLibrary", NULL);
  RNA_def_struct_ui_text(
      srna, "ID Library Override", "Struct gathering all data needed by overridden linked IDs");

  prop = RNA_def_pointer(
      srna, "reference", "ID", "Reference ID", "Linked ID used as reference by this override");
  RNA_def_property_update(prop, NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  RNA_def_pointer(
      srna,
      "hierarchy_root",
      "ID",
      "Hierarchy Root ID",
      "Library override ID used as root of the override hierarchy this ID is a member of");

  prop = RNA_def_boolean(srna,
                         "is_in_hierarchy",
                         true,
                         "Is In Hierarchy",
                         "Whether this library override is defined as part of a library "
                         "hierarchy, or as a single, isolated and autonomous override");
  RNA_def_property_update(prop, NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", IDOVERRIDE_LIBRARY_FLAG_NO_HIERARCHY);

  prop = RNA_def_collection(srna,
                            "properties",
                            "IDOverrideLibraryProperty",
                            "Properties",
                            "List of overridden properties");
  RNA_def_property_update(prop, NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);
  rna_def_ID_override_library_properties(brna, prop);

  /* Update function. */
  func = RNA_def_function(srna, "operations_update", "rna_ID_override_library_operations_update");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_SELF_ID | FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func,
                                  "Update the library override operations based on the "
                                  "differences between this override ID and its reference");

  func = RNA_def_function(srna, "reset", "rna_ID_override_library_reset");
  RNA_def_function_ui_description(func,
                                  "Reset this override to match again its linked reference ID");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_SELF_ID | FUNC_USE_REPORTS);
  RNA_def_boolean(
      func,
      "do_hierarchy",
      true,
      "",
      "Also reset all the dependencies of this override to match their reference linked IDs");

  func = RNA_def_function(srna, "destroy", "rna_ID_override_library_destroy");
  RNA_def_function_ui_description(
      func, "Delete this override ID and remap its usages to its linked reference ID instead");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_SELF_ID | FUNC_USE_REPORTS);
  RNA_def_boolean(func,
                  "do_hierarchy",
                  true,
                  "",
                  "Also delete all the dependencies of this override and remap their usages to "
                  "their reference linked IDs");

  rna_def_ID_override_library_property(brna);
}

static void rna_def_ID(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *prop, *parm;

  static const EnumPropertyItem update_flag_items[] = {
      {ID_RECALC_TRANSFORM, "OBJECT", 0, "Object", ""},
      {ID_RECALC_GEOMETRY, "DATA", 0, "Data", ""},
      {ID_RECALC_ANIMATION, "TIME", 0, "Time", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "ID", NULL);
  RNA_def_struct_ui_text(
      srna,
      "ID",
      "Base type for data-blocks, defining a unique name, linking from other libraries "
      "and garbage collection");
  RNA_def_struct_flag(srna, STRUCT_ID | STRUCT_ID_REFCOUNT);
  RNA_def_struct_refine_func(srna, "rna_ID_refine");
  RNA_def_struct_idprops_func(srna, "rna_ID_idprops");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Name", "Unique data-block ID name");
  RNA_def_property_string_funcs(prop, "rna_ID_name_get", "rna_ID_name_length", "rna_ID_name_set");
  RNA_def_property_string_maxlength(prop, MAX_ID_NAME - 2);
  RNA_def_property_editable_func(prop, "rna_ID_name_editable");
  RNA_def_property_update(prop, NC_ID | NA_RENAME, NULL);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "name_full", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Full Name", "Unique data-block ID name, including library one is any");
  RNA_def_property_string_funcs(prop, "rna_ID_name_full_get", "rna_ID_name_full_length", NULL);
  RNA_def_property_string_maxlength(prop, MAX_ID_FULL_NAME);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "is_evaluated", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Is Evaluated",
      "Whether this ID is runtime-only, evaluated data-block, or actual data from .blend file");
  RNA_def_property_boolean_funcs(prop, "rna_ID_is_evaluated_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "original", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "ID");
  RNA_def_property_ui_text(
      prop,
      "Original ID",
      "Actual data-block from .blend file (Main database) that generated that evaluated one");
  RNA_def_property_pointer_funcs(prop, "rna_ID_original_get", NULL, NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE | PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_flag(prop, PROP_HIDDEN);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);

  prop = RNA_def_property(srna, "users", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "us");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Users", "Number of times this data-block is referenced");

  prop = RNA_def_property(srna, "use_fake_user", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LIB_FAKEUSER);
  RNA_def_property_ui_text(prop, "Fake User", "Save this data-block even if it has no users");
  RNA_def_property_ui_icon(prop, ICON_FAKE_USER_OFF, true);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_ID_fake_user_set");

  prop = RNA_def_property(srna, "is_embedded_data", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LIB_EMBEDDED_DATA);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Embedded Data",
      "This data-block is not an independent one, but is actually a sub-data of another ID "
      "(typical example: root node trees or master collections)");

  prop = RNA_def_property(srna, "tag", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "tag", LIB_TAG_DOIT);
  RNA_def_property_flag(prop, PROP_LIB_EXCEPTION);
  RNA_def_property_ui_text(prop,
                           "Tag",
                           "Tools can use this to tag data for their own purposes "
                           "(initial state is undefined)");

  prop = RNA_def_property(srna, "is_library_indirect", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "tag", LIB_TAG_INDIRECT);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Is Indirect", "Is this ID block linked indirectly");

  prop = RNA_def_property(srna, "library", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "lib");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_ui_text(prop, "Library", "Library file the data-block is linked from");

  prop = RNA_def_pointer(srna,
                         "library_weak_reference",
                         "LibraryWeakReference",
                         "Library Weak Reference",
                         "Weak reference to a data-block in another library .blend file (used to "
                         "re-use already appended data instead of appending new copies)");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);

  prop = RNA_def_property(srna, "asset_data", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_ui_text(prop, "Asset Data", "Additional data for an asset data-block");

  prop = RNA_def_pointer(
      srna, "override_library", "IDOverrideLibrary", "Library Override", "Library override data");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_pointer(srna,
                         "preview",
                         "ImagePreview",
                         "Preview",
                         "Preview image and icon of this data-block (always None if not supported "
                         "for this type of data)");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_pointer_funcs(prop, "rna_IDPreview_get", NULL, NULL, NULL);

  /* functions */
  func = RNA_def_function(srna, "evaluated_get", "rna_ID_evaluated_get");
  RNA_def_function_ui_description(
      func, "Get corresponding evaluated ID from the given dependency graph");
  parm = RNA_def_pointer(
      func, "depsgraph", "Depsgraph", "", "Dependency graph to perform lookup in");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "id", "ID", "", "New copy of the ID");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "copy", "rna_ID_copy");
  RNA_def_function_ui_description(
      func, "Create a copy of this data-block (not supported for all data-blocks)");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "id", "ID", "", "New copy of the ID");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "asset_mark", "rna_ID_asset_mark");
  RNA_def_function_ui_description(
      func,
      "Enable easier reuse of the data-block through the Asset Browser, with the help of "
      "customizable metadata (like previews, descriptions and tags)");

  func = RNA_def_function(srna, "asset_clear", "rna_ID_asset_clear");
  RNA_def_function_ui_description(
      func,
      "Delete all asset metadata and turn the asset data-block back into a normal data-block");

  func = RNA_def_function(srna, "asset_generate_preview", "rna_ID_asset_generate_preview");
  RNA_def_function_ui_description(
      func, "Generate preview image (might be scheduled in a background thread)");
  RNA_def_function_flag(func, FUNC_USE_CONTEXT);

  func = RNA_def_function(srna, "override_create", "rna_ID_override_create");
  RNA_def_function_ui_description(func,
                                  "Create an overridden local copy of this linked data-block (not "
                                  "supported for all data-blocks)");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "id", "ID", "", "New overridden local copy of the ID");
  RNA_def_function_return(func, parm);
  RNA_def_boolean(func,
                  "remap_local_usages",
                  false,
                  "",
                  "Whether local usages of the linked ID should be remapped to the new "
                  "library override of it");

  func = RNA_def_function(srna, "override_hierarchy_create", "rna_ID_override_hierarchy_create");
  RNA_def_function_ui_description(
      func,
      "Create an overridden local copy of this linked data-block, and most of its dependencies "
      "when it is a Collection or and Object");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "id", "ID", "", "New overridden local copy of the root ID");
  RNA_def_function_return(func, parm);
  parm = RNA_def_pointer(
      func, "scene", "Scene", "", "In which scene the new overrides should be instantiated");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(func,
                         "view_layer",
                         "ViewLayer",
                         "",
                         "In which view layer the new overrides should be instantiated");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  RNA_def_pointer(func,
                  "reference",
                  "ID",
                  "",
                  "Another ID (usually an Object or Collection) used as a hint to decide where to "
                  "instantiate the new overrides");

  func = RNA_def_function(srna, "override_template_create", "rna_ID_override_template_create");
  RNA_def_function_ui_description(func, "Create an override template for this ID");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);

  func = RNA_def_function(srna, "user_clear", "rna_ID_user_clear");
  RNA_def_function_ui_description(func,
                                  "Clear the user count of a data-block so its not saved, "
                                  "on reload the data will be removed");

  func = RNA_def_function(srna, "user_remap", "rna_ID_user_remap");
  RNA_def_function_ui_description(
      func, "Replace all usage in the .blend file of this ID by new given one");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  parm = RNA_def_pointer(func, "new_id", "ID", "", "New ID to use");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(srna, "make_local", "rna_ID_make_local");
  RNA_def_function_ui_description(
      func,
      "Make this datablock local, return local one "
      "(may be a copy of the original, in case it is also indirectly used)");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  parm = RNA_def_boolean(func, "clear_proxy", true, "", "Deprecated, has no effect");
  parm = RNA_def_pointer(func, "id", "ID", "", "This ID, or the new ID if it was copied");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "user_of_id", "BKE_library_ID_use_ID");
  RNA_def_function_ui_description(func,
                                  "Count the number of times that ID uses/references given one");
  parm = RNA_def_pointer(func, "id", "ID", "", "ID to count usages");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func,
                     "count",
                     0,
                     0,
                     INT_MAX,
                     "",
                     "Number of usages/references of given id by current data-block",
                     0,
                     INT_MAX);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "animation_data_create", "rna_ID_animation_data_create");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  RNA_def_function_ui_description(
      func, "Create animation data to this ID, note that not all ID types support this");
  parm = RNA_def_pointer(func, "anim_data", "AnimData", "", "New animation data or NULL");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "animation_data_clear", "rna_ID_animation_data_free");
  RNA_def_function_flag(func, FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Clear animation on this this ID");

  func = RNA_def_function(srna, "update_tag", "rna_ID_update_tag");
  RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func,
                                  "Tag the ID to update its display data, "
                                  "e.g. when calling :class:`bpy.types.Scene.update`");
  RNA_def_enum_flag(func, "refresh", update_flag_items, 0, "", "Type of updates to perform");

  func = RNA_def_function(srna, "preview_ensure", "BKE_previewimg_id_ensure");
  RNA_def_function_ui_description(func,
                                  "Ensure that this ID has preview data (if ID type supports it)");
  parm = RNA_def_pointer(
      func, "preview_image", "ImagePreview", "", "The existing or created preview");
  RNA_def_function_return(func, parm);

#  ifdef WITH_PYTHON
  RNA_def_struct_register_funcs(srna, NULL, NULL, "rna_ID_instance");
#  endif
}

static void rna_def_library(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Library", "ID");
  RNA_def_struct_ui_text(srna, "Library", "External .blend file from which data is linked");
  RNA_def_struct_ui_icon(srna, ICON_LIBRARY_DATA_DIRECT);

  prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_string_sdna(prop, NULL, "filepath");
  RNA_def_property_ui_text(prop, "File Path", "Path to the library .blend file");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Library_filepath_set");

  prop = RNA_def_property(srna, "parent", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "Library");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_ui_text(prop, "Parent", "");

  prop = RNA_def_property(srna, "packed_file", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "packedfile");
  RNA_def_property_ui_text(prop, "Packed File", "");

  prop = RNA_def_int_vector(srna,
                            "version",
                            3,
                            NULL,
                            0,
                            INT_MAX,
                            "Version",
                            "Version of Blender the library .blend was saved with",
                            0,
                            INT_MAX);
  RNA_def_property_int_funcs(prop, "rna_Library_version_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_flag(prop, PROP_THICK_WRAP);

  func = RNA_def_function(srna, "reload", "WM_lib_reload");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_CONTEXT);
  RNA_def_function_ui_description(func, "Reload this library and all its linked data-blocks");
}

static void rna_def_library_weak_reference(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "LibraryWeakReference", NULL);
  RNA_def_struct_ui_text(
      srna,
      "LibraryWeakReference",
      "Read-only external reference to a linked data-block and its library file");

  prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_string_sdna(prop, NULL, "library_filepath");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "File Path", "Path to the library .blend file");

  prop = RNA_def_property(srna, "id_name", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_string_sdna(prop, NULL, "library_id_name");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "ID name",
      "Full ID name in the library .blend file (including the two leading 'id type' chars)");
}

/**
 * \attention This is separate from the above. It allows for RNA functions to
 * return an IDProperty *. See MovieClip.metadata for a usage example.
 */
static void rna_def_idproperty_wrap_ptr(BlenderRNA *brna)
{
  StructRNA *srna;

  srna = RNA_def_struct(brna, "IDPropertyWrapPtr", NULL);
  RNA_def_struct_idprops_func(srna, "rna_IDPropertyWrapPtr_idprops");
  RNA_def_struct_flag(srna, STRUCT_NO_DATABLOCK_IDPROPERTIES);
}

void RNA_def_ID(BlenderRNA *brna)
{
  StructRNA *srna;

  /* built-in unknown type */
  srna = RNA_def_struct(brna, "UnknownType", NULL);
  RNA_def_struct_ui_text(
      srna, "Unknown Type", "Stub RNA type used for pointers to unknown or internal data");

  /* built-in any type */
  srna = RNA_def_struct(brna, "AnyType", NULL);
  RNA_def_struct_ui_text(srna, "Any Type", "RNA type used for pointers to any possible data");

  rna_def_ID(brna);
  rna_def_ID_override_library(brna);
  rna_def_image_preview(brna);
  rna_def_ID_properties(brna);
  rna_def_ID_materials(brna);
  rna_def_library(brna);
  rna_def_library_weak_reference(brna);
  rna_def_idproperty_wrap_ptr(brna);
}

#endif
