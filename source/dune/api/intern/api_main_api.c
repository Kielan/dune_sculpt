#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "types_id.h"
#include "types_mod.h"
#include "types_object.h"
#include "types_space.h"

#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "dune_action.h"
#  include "dune_armature.h"
#  include "dune_brush.h"
#  include "dune_camera.h"
#  include "dune_collection.h"
#  include "dune_curve.h"
#  include "dune_curves.h"
#  include "dune_displist.h"
#  include "dune_pen.h"
#  include "dune_icons.h"
#  include "dune_idtype.h"
#  include "dune_image.h"
#  include "dune_lattice.h"
#  include "dune_lib_remap.h"
#  include "dune_light.h"
#  include "dune_lightprobe.h"
#  include "dune_linestyle.h"
#  include "dune_mask.h"
#  include "dune_material.h"
#  include "dune_mball.h"
#  include "dune_mesh.h"
#  include "dune_movieclip.h"
#  include "dune_node.h"
#  include "dune_object.h"
#  include "dune_paint.h"
#  include "dune_particle.h"
#  include "dune_pointcloud.h"
#  include "dune_scene.h"
#  include "dune_simulation.h"
#  include "dune_sound.h"
#  include "dune_speaker.h"
#  include "dune_text.h"
#  include "dune_texture.h"
#  include "dune_vfont.h"
#  include "dune_volume.h"
#  include "dune_workspace.h"
#  include "dune_world.h"

#  include "graph_build.h"
#  include "graph_query.h"

#  include "types_armature.h"
#  include "types_brush.h"
#  include "types_camera.h"
#  include "types_collection.h"
#  include "types_curve.h"
#  include "types_curves.h"
#  include "types_pen.h"
#  include "types_lattice.h"
#  include "types_light.h"
#  include "types_lightprobe.h"
#  include "types_mask.h"
#  include "types_material.h"
#  include "types_mesh.h"
#  include "types_meta.h"
#  include "types_movieclip.h"
#  include "types_node.h"
#  include "types_particle.h"
#  include "types_pointcloud_.h"
#  include "types_simulation.h"
#  include "types_sound.h"
#  include "types_speaker.h"
#  include "types_text.h"
#  include "types_texture.h"
#  include "types_vfont.h"
#  include "types_volume.h"
#  include "types_world.h"

#  include "ed_screen.h"

#  include "lang_translation.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

#  include "wm_api.h"
#  include "wm_types.h"

static void api_idname_validate(const char *name, char *r_name)
{
  lib_strncpy(r_name, name, MAX_ID_NAME - 2);
  lib_str_utf8_invalid_strip(r_name, strlen(r_name));
}

static void api_Main_id_remove(Main *main,
                               ReportList *reports,
                               ApiPtr *id_ptr,
                               bool do_unlink,
                               bool do_id_user,
                               bool do_ui_user)
{
  Id *id = id_ptr->data;
  if (id->tag & LIB_TAG_NO_MAIN) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is outside of main database and can not be removed from it",
                dune_idtype_idcode_to_name(GS(id->name)),
                id->name + 2);
    return;
  }
  if (do_unlink) {
    dune_id_delete(main, id);
    API_PTR_INVALIDATE(id_ptr);
    /* Force full redraw, mandatory to avoid crashes when running this from UI... */
    wm_main_add_notifier(NC_WINDOW, NULL);
  }
  else if (ID_REAL_USERS(id) <= 0) {
    const int flag = (do_id_user ? 0 : LIB_ID_FREE_NO_USER_REFCOUNT) |
                     (do_ui_user ? 0 : LIB_ID_FREE_NO_UI_USER);
    /* Still using id flags here, this is in-between commit anyway... */
    dune_id_free_ex(main, id, flag, true);
    API_PTR_INVALIDATE(id_ptr);
  }
  else {
    dune_reportf(
        reports,
        RPT_ERROR,
        "%s '%s' must have zero users to be removed, found %d (try with do_unlink=True parameter)",
        dune_idtype_idcode_to_name(GS(id->name)),
        id->name + 2,
        ID_REAL_USERS(id));
  }
}

static Camera *api_Main_cameras_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Id *id = dune_camera_add(main, safe_name);
  id_us_min(id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return (Camera *)id;
}

static Scene *api_Main_scenes_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Scene *scene = dune_scene_add(main, safe_name);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return scene;
}
static void api_Main_scenes_remove(
    Main *main, Cxt *C, ReportList *reports, ApiPtr *scene_ptr, bool do_unlink)
{
  /* don't call BKE_id_free(...) directly */
  Scene *scene = scene_ptr->data;

  if (dune_scene_can_be_removed(main, scene)) {
    Scene *scene_new = scene->id.prev ? scene->id.prev : scene->id.next;
    if (do_unlink) {
      wmWindow *win = cxt_wm_window(C);

      if (wm_window_get_active_scene(win) == scene) {

#  ifdef WITH_PYTHON
        BPy_BEGIN_ALLOW_THREADS;
#  endif

        wm_window_set_active_scene(bmain, C, win, scene_new);

#  ifdef WITH_PYTHON
        BPy_END_ALLOW_THREADS;
#  endif
      }
    }
    api_Main_id_remove(main, reports, scene_ptr, do_unlink, true, true);
  }
  else {
    dune_reportf(reports,
                RPT_ERROR,
                "Scene '%s' is the last local one, cannot be removed",
                scene->id.name + 2);
  }
}

static Object *api_Main_objects_new(Main *bmain, ReportList *reports, const char *name, ID *data)
{
  if (data != NULL && (data->tag & LIB_TAG_NO_MAIN)) {
    dune_report(reports,
               RPT_ERROR,
               "Can not create object in main database with an evaluated data data-block");
    return NULL;
  }

  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Object *ob;
  int type = OB_EMPTY;

  if (data) {
    type = dune_object_obdata_to_type(data);
    if (type == -1) {
      const char *idname;
      if (api_enum_id_from_value(api_enum_id_type_items, GS(data->name), &idname) == 0) {
        idname = "UNKNOWN";
      }

      dune_reportf(reports, RPT_ERROR, "id type '%s' is not valid for an object", idname);
      return NULL;
    }

    id_us_plus(data);
  }

  ob = dune_object_add_only_object(main, type, safe_name);

  ob->data = data;
  dune_object_materials_test(main, ob, ob->data);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return ob;
}

static Material *api_Main_materials_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Id *id = (Id *)dune_material_add(main, safe_name);
  id_us_min(id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return (Material *)id;
}

static void api_Main_materials_pen_data(Main *UNUSED(main), ApiPtr *id_ptr)
{
  Id *id = id_ptr->data;
  Material *ma = (Material *)id;
  dune_pen_material_attr_init(ma);
}

static void api_Main_materials_pen_remove(Main *UNUSED(main), ApiPtr *id_ptr)
{
  Id *id = id_ptr->data;
  Material *ma = (Material *)id;
  if (ma->pen_style) {
    MEM_SAFE_FREE(ma->pen_style);
  }
}

static const EnumPropItem *api_Main_nodetree_type_itemf(Cxt *UNUSED(C),
                                                        ApiPtr *UNUSED(ptr),
                                                        ApiProp *UNUSED(prop),
                                                        bool *r_free)
{
  return api_node_tree_type_itemf(NULL, NULL, r_free);
}
static struct NodeTree *api_Main_nodetree_new(Main *main, const char *name, int type)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  NodeTreeType *typeinfo = api_node_tree_type_from_enum(type);
  if (typeinfo) {
    NodeTree *ntree = ntreeAddTree(main, safe_name, typeinfo->idname);

    id_us_min(&ntree->id);
    return ntree;
  } else {
    return NULL;
  }
}

static Mesh *api_Main_meshes_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Mesh *me = dune_mesh_add(main, safe_name);
  id_us_min(&me->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return me;
}

/* copied from Mesh_getFromObject and adapted to api interface */
static Mesh *api_Main_meshes_new_from_object(Main *main,
                                             ReportList *reports,
                                             Object *object,
                                             bool preserve_all_data_layers,
                                             Graph *graph)
{
  switch (object->type) {
    case OB_FONT:
    case OB_CURVES_LEGACY:
    case OB_SURF:
    case OB_MBALL:
    case OB_MESH:
      break;
    default:
      dune_report(reports, RPT_ERROR, "Object does not have geometry data");
      return NULL;
  }

  Mesh *mesh = dune_mesh_new_from_object_to_bmain(
      main, graph, object, preserve_all_data_layers);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return mesh;
}

static Light *api_Main_lights_new(Main *main, const char *name, int type)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Light *lamp = dune_light_add(main, safe_name);
  lamp->type = type;
  id_us_min(&lamp->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return lamp;
}

static Image *api_Main_images_new(Main *main,
                                  const char *name,
                                  int width,
                                  int height,
                                  bool alpha,
                                  bool float_buffer,
                                  bool stereo3d,
                                  bool is_data,
                                  bool tiled)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  float color[4] = {0.0, 0.0, 0.0, 1.0};
  Image *image = dune_image_add_generated(main,
                                         width,
                                         height,
                                         safe_name,
                                         alpha ? 32 : 24,
                                         float_buffer,
                                         0,
                                         color,
                                         stereo3d,
                                         is_data,
                                         tiled);
  id_us_min(&image->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return image;
}
static Image *api_Main_images_load(Main *main,
                                   ReportList *reports,
                                   const char *filepath,
                                   bool check_existing)
{
  Image *ima;

  errno = 0;
  if (check_existing) {
    ima = dune_image_load_exists(bmain, filepath);
  }
  else {
    ima = dune_image_load(bmain, filepath);
  }

  if (!ima) {
    dune_reportf(reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unsupported image format"));
  }

  id_us_min((Id *)ima);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return ima;
}

static Lattice *api_Main_lattices_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Lattice *lt = dune_lattice_add(main, safe_name);
  id_us_min(&lt->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return lt;
}

static Curve *api_Main_curves_new(Main *main, const char *name, int type)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Curve *cu = dune_curve_add(main, safe_name, type);
  id_us_min(&cu->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return cu;
}

static MetaBall *api_Main_metaballs_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  MetaBall *mb = dune_mball_add(main, safe_name);
  id_us_min(&mb->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return mb;
}

static VFont *api_Main_fonts_load(Main *main,
                                  ReportList *reports,
                                  const char *filepath,
                                  bool check_existing)
{
  VFont *font;
  errno = 0;

  if (check_existing) {
    font = dune_vfont_load_exists(main, filepath);
  } else {
    font = dune_vfont_load(main, filepath);
  }

  if (!font) {
    dune_reportf(reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unsupported font format"));
  }

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return font;
}

static Tex *api_Main_textures_new(Main *main, const char *name, int type)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Tex *tex = dune_texture_add(main, safe_name);
  dune_texture_type_set(tex, type);
  id_us_min(&tex->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return tex;
}

static Brush *api_Main_brushes_new(Main *main, const char *name, int mode)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Brush *brush = dune_brush_add(main, safe_name, mode);
  id_us_min(&brush->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return brush;
}

static void api_Main_brush_pen_data(Main *UNUSED(main), ApiPtr *id_ptr)
{
  Id *id = id_ptr->data;
  Brush *brush = (Brush *)id;
  dune_brush_init_pen_settings(brush);
}

static World *api_Main_worlds_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  World *world = dune_world_add(main, safe_name);
  id_us_min(&world->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return world;
}

static Collection *api_Main_collections_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Collection *collection = dune_collection_add(main, NULL, safe_name);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return collection;
}

static Speaker *api_Main_speakers_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Speaker *speaker = dune_speaker_add(main, safe_name);
  id_us_min(&speaker->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return speaker;
}

static Sound *dune_Main_sounds_load(Main *main, const char *name, bool check_existing)
{
  Sound *sound;

  if (check_existing) {
    sound = dune_sound_new_file_exists(main, name);
  } else {
    sound = dune_sound_new_file(bmain, name);
  }

  id_us_min(&sound->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return sound;
}

static Text *api_Main_texts_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Text *text = dune_text_add(main, safe_name);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return text;
}

static Text *api_Main_texts_load(Main *main,
                                 ReportList *reports,
                                 const char *filepath,
                                 bool is_internal)
{
  Text *txt;

  errno = 0;
  txt = dune_text_load_ex(main, filepath, BKE_main_blendfile_path(bmain), is_internal);

  if (!txt) {
    dune_reportf(reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unable to load text"));
  }

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return txt;
}

static Armature *api_Main_armatures_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Armature *arm = dune_armature_add(bmain, safe_name);
  id_us_min(&arm->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return arm;
}

static Action *api_Main_actions_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  rna_idname_validate(name, safe_name);

  Action *act = dune_action_add(main, safe_name);
  id_fake_user_clear(&act->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return act;
}

static ParticleSettings *api_Main_particles_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  ParticleSettings *part = new_particlesettings_add(bmain, safe_name);
  id_us_min(&part->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return part;
}

static Palette *api_Main_palettes_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Palette *palette = dune_palette_add(main, safe_name);
  id_us_min(&palette->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return (Palette *)palette;
}

static MovieClip *api_Main_movieclip_load(Main *main,
                                          ReportList *reports,
                                          const char *filepath,
                                          bool check_existing)
{
  MovieClip *clip;

  errno = 0;

  if (check_existing) {
    clip = dune_movieclip_file_add_exists(bmain, filepath);
  }
  else {
    clip = dune_movieclip_file_add(bmain, filepath);
  }

  if (clip != NULL) {
    graph_relations_tag_update(bmain);
  }
  else {
    dune_reportf(reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unable to load movie clip"));
  }

  id_us_min((Id *)clip);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return clip;
}

static Mask *api_Main_mask_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Mask *mask = dune_mask_new(main, safe_name);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return mask;
}

static FreestyleLineStyle *api_Main_linestyles_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  FreestyleLineStyle *linestyle = BKE_linestyle_new(bmain, safe_name);
  id_us_min(&linestyle->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return linestyle;
}

static LightProbe *api_Main_lightprobe_new(Main *bmain, const char *name, int type)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  LightProbe *probe = dune_lightprobe_add(main, safe_name);

  dune_lightprobe_type_set(probe, type);

  id_us_min(&probe->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return probe;
}

static PenData *api_Main_pens_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  PenData *pd = dune_pen_data_addnew(main, safe_name);
  id_us_min(&pd->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return pd;
}

#  ifdef WITH_NEW_CURVES_TYPE
static Curves *api_Main_hair_curves_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Curves *curves = dune_curves_add(main, safe_name);
  id_us_min(&curves->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return curves;
}
#  endif

static PointCloud *api_Main_pointclouds_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  PointCloud *pointcloud = dune_pointcloud_add(main, safe_name);
  id_us_min(&pointcloud->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return pointcloud;
}

static Volume *api_Main_volumes_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Volume *volume = dune_volume_add(main, safe_name);
  id_us_min(&volume->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return volume;
}

#  ifdef WITH_SIMULATION_DATABLOCK
static Simulation *api_Main_simulations_new(Main *main, const char *name)
{
  char safe_name[MAX_ID_NAME - 2];
  api_idname_validate(name, safe_name);

  Simulation *simulation = dune_simulation_add(main, safe_name);
  id_us_min(&simulation->id);

  wm_main_add_notifier(NC_ID | NA_ADDED, NULL);

  return simulation;
}
#  endif

/* tag functions, all the same */
#  define API_MAIN_ID_TAG_FNS_DEF(_fn_name, _list_name, _id_type) \
    static void api_Main_##_fn_name##_tag(Main *main, bool value) \
    { \
      dune_main_id_tag_list(&main->_list_name, LIB_TAG_DOIT, value); \
    }

API_MAIN_ID_TAG_FNS_DEF(cameras, cameras, ID_CA)
API_MAIN_ID_TAG_FNS_DEF(scenes, scenes, ID_SCE)
API_MAIN_ID_TAG_FNS_DEF(objects, objects, ID_OB)
API_MAIN_ID_TAG_FNS_DEF(materials, materials, ID_MA)
API_MAIN_ID_TAG_FNS_DEF(node_groups, nodetrees, ID_NT)
API_MAIN_ID_TAG_FNS_DEF(meshes, meshes, ID_ME)
API_MAIN_ID_TAG_FNS_DEF(lights, lights, ID_LA)
API_MAIN_ID_TAG_FNS_DEF(libraries, libraries, ID_LI)
API_MAIN_ID_TAG_FNS_DEF(screens, screens, ID_SCR)
API_MAIN_ID_TAG_FNS_DEF(window_managers, wm, ID_WM)
API_MAIN_ID_TAG_FNS_DEF(images, images, ID_IM)
API_MAIN_ID_TAG_FNS_DEF(lattices, lattices, ID_LT)
API_MAIN_ID_TAG_FNS_DEF(curves, curves, ID_CU_LEGACY)
API_MAIN_ID_TAG_FNS_DEF(metaballs, metaballs, ID_MB)
API_MAIN_ID_TAG_FNS_DEF(fonts, fonts, ID_VF)
API_MAIN_ID_TAG_FNS_DEF(textures, textures, ID_TE)
API_MAIN_ID_TAG_FNS_DEF(brushes, brushes, ID_BR)
API_MAIN_ID_TAG_FNS_DEF(worlds, worlds, ID_WO)
API_MAIN_ID_TAG_FUNCS_DEF(collections, collections, ID_GR)
// API_MAIN_ID_TAG_FNS_DEF(shape_keys, key, ID_KE)
API_MAIN_ID_TAG_FNS_DEF(texts, texts, ID_TXT)
API_MAIN_ID_TAG_FNS_DEF(speakers, speakers, ID_SPK)
API_MAIN_ID_TAG_FNS_DEF(sounds, sounds, ID_SO)
API_MAIN_ID_TAG_FNS_DEF(armatures, armatures, ID_AR)
API_MAIN_ID_TAG_FNS_DEF(actions, actions, ID_AC)
API_MAIN_ID_TAG_FNS_DEF(particles, particles, ID_PA)
API_MAIN_ID_TAG_FNS_DEF(palettes, palettes, ID_PAL)
API_MAIN_ID_TAG_FNS_DEF(pen, pens, ID_GD)
API_MAIN_ID_TAG_FNS_DEF(movieclips, movieclips, ID_MC)
API_MAIN_ID_TAG_FNS_DEF(masks, masks, ID_MSK)
API_MAIN_ID_TAG_FNS_DEF(linestyle, linestyles, ID_LS)
API_MAIN_ID_TAG_FNS_DEF(cachefiles, cachefiles, ID_CF)
API_MAIN_ID_TAG_FNS_DEF(paintcurves, paintcurves, ID_PC)
API_MAIN_ID_TAG_FNS_DEF(workspaces, workspaces, ID_WS)
API_MAIN_ID_TAG_FNS_DEF(lightprobes, lightprobes, ID_LP)
#  ifdef WITH_NEW_CURVES_TYPE
API_MAIN_ID_TAG_FNS_DEF(hair_curves, hair_curves, ID_CV)
#  endif
API_MAIN_ID_TAG_FNS_DEF(pointclouds, pointclouds, ID_PT)
API_MAIN_ID_TAG_FNS_DEF(volumes, volumes, ID_VO)
#  ifdef WITH_SIMULATION_DATABLOCK
API_MAIN_ID_TAG_FNS_DEF(simulations, simulations, ID_SIM)
#  endif

#  undef API_MAIN_ID_TAG_FNS_DEF

#else

void api_main(ApiStruct *UNUSED(sapi))
{
#  if 0
  ApiFn *fn;
  ApiProp *parm;

  /* maybe we want to add functions in 'bpy.data' still?
   * for now they are all in collections bpy.data.images.new(...) */
  fn = api_def_fn(sapi, "add_image", "api_Main_add_image");
  api_def_fn_ui_description(fn, "Add a new image");
  parm = api_def_string_file_path(fn, "filepath", NULL, 0, "", "File path to load image from");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "image", "Image", "", "New image");
  api_def_fn_return(fn, parm);
#  endif
}

void api_def_main_cameras(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataCameras");
  sapi = api_def_struct(dapi, "BlendDataCameras", NULL);
  api_def_struct_sdna(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Cameras", "Collection of cameras");

  fn = api_def_fn(sapi, "new", "api_Main_cameras_new");
  api_def_fn_ui_description(fn, "Add a new camera to the main database");
  parm = api_def_string(fn, "name", "Camera", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "camera", "Camera", "", "New camera data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_id_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a camera from the current dunefile");
  parm = api_def_ptr(fn, "camera", "Camera", "", "Camera to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(fn,
               "do_unlink",
               true,
               "",
               "Unlink all usages of this camera before deleting it "
               "(WARNING: will also delete objects instancing that camera data)");
  api_def_bool(fn,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this camera");
  api_def_bool(
      func, "do_ui_user", true, "", "Make sure interface does not reference this camera");

  fn = api_def_fn(sapi, "tag", "api_Main_cameras_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_scenes(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataScenes");
  sapi = api_def_struct(dapi, "BlendDataScenes", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Scenes", "Collection of scenes");

  fn = api_def_fn(sapi, "new", "api_Main_scenes_new");
  api_def_fn_ui_description(fn, "Add a new scene to the main database");
  parm = api_def_string(fn, "name", "Scene", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "scene", "Scene", "", "New scene data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_scenes_remove");
  api_def_fn_flag(fn, FN_USE_CXT | FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a scene from the current blendfile");
  parm = api_def_ptr(fn, "scene", "Scene", "", "Scene to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(
      fn, "do_unlink", true, "", "Unlink all usages of this scene before deleting it");

  fn = api_def_fn(sapi, "tag", "api_Main_scenes_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_objects(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataObjects");
  sapi = api_def_struct(dapi, "BlendDataObjects", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Objects", "Collection of objects");

  fn = api_def_fn(sapi, "new", "api_Main_objects_new");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Add a new object to the main database");
  parm = api_def_string(fn, "name", "Object", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "object_data", "ID", "", "Object data or None for an empty object");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* return type */
  parm = api_def_ptr(fn, "object", "Object", "", "New object data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_id_remove");
  api_def_fn_ui_description(fn, "Remove an object from the current blendfile");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "object", "Object", "", "Object to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(
      fn, "do_unlink", true, "", "Unlink all usages of this object before deleting it");
  api_def_bool(fb,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this object");
   api_def_bool(
      fn, "do_ui_user", true, "", "Make sure interface does not reference this object");

  fn = api_def_fn(sapi, "tag", "api_Main_objects_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_materials(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataMaterials");
  sapi = api_def_struct(dapi, "BlendDataMaterials", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Materials", "Collection of materials");

  fn = api_def_fn(sapi, "new", "api_Main_materials_new");
  api_def_fn_ui_description(fn, "Add a new material to the main database");
  parm = api_def_string(fn, "name", "Material", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "material", "Material", "", "New material data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "create_pen_data", "api_Main_materials_pen_data");
  api_def_fn_ui_description(fn, "Add pen material settings");
  parm = api_def_ptr(fn, "material", "Material", "", "Material");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "remove_pen_data", "api_Main_materials_pen_remove");
  api_def_fn_ui_description(fn, "Remove pen material settings");
  parm = api_def_ptr(fn, "material", "Material", "", "Material");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "remove", "api_Main_id_remove")
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a material from the current dunefile");
  parm = api_def_ptr(fn, "material", "Material", "", "Material to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(
      fn, "do_unlink", true, "", "Unlink all usages of this material before deleting it");
  api_def_bool(fn,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this material");
  api_def_bool(
      func, "do_ui_user", true, "", "Make sure interface does not reference this material");

  fn = api_def_fn(sapi, "tag", "api_Main_materials_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}
void api_def_main_node_groups(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem dummy_items[] = {
      {0, "DUMMY", 0, "", ""},
      {0, NULL, 0, NULL, NULL},
  };

  api_def_prop_sapi(cprop, "BlendDataNodeTrees");
  sapi = api_def_struct(dapi, "BlendDataNodeTrees", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Node Trees", "Collection of node trees");

  fn = api_def_fn(sapi, "new", "api_Main_nodetree_new");
  api_def_fn_ui_description(fn, "Add a new node tree to the main");
  parm = api_def_string(fn, "name", "NodeGroup", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "type", dummy_items, 0, "Type", "The type of node_group to add");
  api_def_prop_enum_fns(parm, NULL, NULL, "api_Main_nodetree_type_itemf");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "tree", "NodeTree", "", "New node tree data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_id_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a node tree from the current blendfile");
  parm = apo_def_ptr(fn, "tree", "NodeTree", "", "Node tree to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(
      fn, "do_unlink", true, "", "Unlink all usages of this node tree before deleting it");
  api_def_bool(fn,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this node tree");
  api_def_bool(
      fn, "do_ui_user", true, "", "Make sure interface does not reference this node tree");

  fn = api_def_fn(sapi, "tag", "api_Main_node_groups_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}
void api_def_main_meshes(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *func;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataMeshes");
  sapi = api_def_struct(brna, "BlendDataMeshes", NULL);
  api_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Meshes", "Collection of meshes");

  func = RNA_def_function(srna, "new", "rna_Main_meshes_new");
  RNA_def_function_ui_description(func, "Add a new mesh to the main database");
  parm = RNA_def_string(func, "name", "Mesh", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "mesh", "Mesh", "", "New mesh data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_from_object", "api_Main_meshes_new_from_object");
  api_def_fn_ui_description(
      fn,
      "Add a new mesh created from given object (undeformed geometry if object is original, and "
      "final evaluated geometry, with all modifiers etc., if object is evaluated)");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fb, "object", "Object", "", "Object to create mesh from");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  api_def_bool(fn,
               "preserve_all_data_layers",
               false,
               "",
               "Preserve all data layers in the mesh, like UV maps and vertex groups. "
               "By default Blender only computes the subset of data layers needed for viewport "
               "display and rendering, for better performance");
  api_def_ptr(
      fn,
      "graph",
      "Graph",
      "Graph",
      "Evald dep graph which is required when preserve_all_data_layers is true");
  parm = api_def_ptr(fn,
                         "mesh",
                         "Mesh",
                         "",
                         "Mesh created from object, remove it if it is only used for export");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_id_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a mesh from the current blendfile");
  parm = api_def_ptr(fn, "mesh", "Mesh", "", "Mesh to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(fn,
               "do_unlink",
               true,
               "",
               "Unlink all usages of this mesh before deleting it "
               "(WARNING: will also delete objects instancing that mesh data)");
  api_def_bool(fb,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this mesh data");
  api_def_bool(
      fn, "do_ui_user", true, "", "Make sure interface does not reference this mesh data");

  fn = api_def_fn(sapi, "tag", "api_Main_meshes_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_lights(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp prop;

  apo_def_prop_srna(cprop, "BlendDataLights");
  sapi = api_def_struct(dapi, "BlendDataLights", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Lights", "Collection of lights");

  fn = api_def_fn(sapi, "new", "api_Main_lights_new");
  api_def_fn_ui_description(fn, "Add a new light to the main database");
  parm = api_def_string(fn, "name", "Light", 0, "", "New name for the data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(
      fn, "type", api_enum_light_type_items, 0, "Type", "The type of light to add");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "light", "Light", "", "New light data-block");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Main_ID_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a light from the current blendfile");
  parm = api_def_ptr(fn, "light", "Light", "", "Light to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  api_def_bool(fn,
               "do_unlink",
               true,
               "",
               "Unlink all usages of this light before deleting it "
               "(WARNING: will also delete objects instancing that light data)");
  api_def_bool(fn,
               "do_id_user",
               true,
               "",
               "Decrement user counter of all datablocks used by this light data");
  api_def_bool(
      func, "do_ui_user", true, "", "Make sure interface does not reference this light data");

  fn = api_def_fn(sapi, "tag", "api_Main_lights_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_libs(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataLibraries");
  sapi = api_def_struct(brna, "BlendDataLibraries", NULL);
  api_def_struct_sdna(srna, "Main");
  api_def_struct_ui_text(srna, "Main Libraries", "Collection of libraries");

  func = RNA_def_function(srna, "tag", "rna_Main_libraries_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a library from the current blendfile");
  parm = RNA_def_pointer(func, "library", "Library", "", "Library to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this library before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this library");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this library");
}

void api_def_main_screens(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataScreens");
  sapi = api_def_struct(dapi, "BlendDataScreens", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Screens", "Collection of screens");

  fn = api_def_fn(sapi, "tag", "api_Main_screens_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_main_window_managers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fm;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BlendDataWindowManagers");
  sapi = api_def_struct(dapi, "BlendDataWindowManagers", NULL);
  api_def_struct_stype(sapi, "Main");
  api_def_struct_ui_text(sapi, "Main Window Managers", "Collection of window managers");

  fn = api_def_fn(sapi, "tag", "api_Main_window_managers_tag");
  parm = api_def_bool(fn, "value", 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}
void api_def_main_images(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataImages");
  srna = RNA_def_struct(brna, "BlendDataImages", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Images", "Collection of images");

  func = RNA_def_function(srna, "new", "rna_Main_images_new");
  RNA_def_function_ui_description(func, "Add a new image to the main database");
  parm = RNA_def_string(func, "name", "Image", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "width", 1024, 1, INT_MAX, "", "Width of the image", 1, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "height", 1024, 1, INT_MAX, "", "Height of the image", 1, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func, "alpha", 0, "Alpha", "Use alpha channel");
  RNA_def_boolean(
      func, "float_buffer", 0, "Float Buffer", "Create an image with floating-point color");
  RNA_def_boolean(func, "stereo3d", 0, "Stereo 3D", "Create left and right views");
  RNA_def_boolean(func, "is_data", 0, "Is Data", "Create image with non-color data color space");
  RNA_def_boolean(func, "tiled", 0, "Tiled", "Create a tiled image");
  /* return type */
  parm = RNA_def_pointer(func, "image", "Image", "", "New image data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "load", "rna_Main_images_load");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Load a new image into the main database");
  parm = RNA_def_string_file_path(
      func, "filepath", "File Path", 0, "", "Path of the file to load");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func,
                  "check_existing",
                  false,
                  "",
                  "Using existing data-block if this file is already loaded");
  /* return type */
  parm = RNA_def_pointer(func, "image", "Image", "", "New image data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove an image from the current blendfile");
  parm = RNA_def_pointer(func, "image", "Image", "", "Image to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this image before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this image");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this image");

  func = RNA_def_function(srna, "tag", "rna_Main_images_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_lattices(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataLattices");
  srna = RNA_def_struct(brna, "BlendDataLattices", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Lattices", "Collection of lattices");

  func = RNA_def_function(srna, "new", "rna_Main_lattices_new");
  RNA_def_function_ui_description(func, "Add a new lattice to the main database");
  parm = RNA_def_string(func, "name", "Lattice", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "lattice", "Lattice", "", "New lattice data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a lattice from the current blendfile");
  parm = RNA_def_pointer(func, "lattice", "Lattice", "", "Lattice to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this lattice before deleting it "
                  "(WARNING: will also delete objects instancing that lattice data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this lattice data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this lattice data");

  func = RNA_def_function(srna, "tag", "rna_Main_lattices_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_curves(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataCurves");
  srna = RNA_def_struct(brna, "BlendDataCurves", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Curves", "Collection of curves");

  func = RNA_def_function(srna, "new", "rna_Main_curves_new");
  RNA_def_function_ui_description(func, "Add a new curve to the main database");
  parm = RNA_def_string(func, "name", "Curve", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(
      func, "type", rna_enum_object_type_curve_items, 0, "Type", "The type of curve to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "curve", "Curve", "", "New curve data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a curve from the current blendfile");
  parm = RNA_def_pointer(func, "curve", "Curve", "", "Curve to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this curve before deleting it "
                  "(WARNING: will also delete objects instancing that curve data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this curve data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this curve data");

  func = RNA_def_function(srna, "tag", "rna_Main_curves_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_metaballs(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataMetaBalls");
  srna = RNA_def_struct(brna, "BlendDataMetaBalls", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Metaballs", "Collection of metaballs");

  func = RNA_def_function(srna, "new", "rna_Main_metaballs_new");
  RNA_def_function_ui_description(func, "Add a new metaball to the main database");
  parm = RNA_def_string(func, "name", "MetaBall", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "metaball", "MetaBall", "", "New metaball data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a metaball from the current blendfile");
  parm = RNA_def_pointer(func, "metaball", "MetaBall", "", "Metaball to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this metaball before deleting it "
                  "(WARNING: will also delete objects instancing that metaball data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this metaball data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this metaball data");

  func = RNA_def_function(srna, "tag", "rna_Main_metaballs_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_fonts(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataFonts");
  srna = RNA_def_struct(brna, "BlendDataFonts", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Fonts", "Collection of fonts");

  func = RNA_def_function(srna, "load", "rna_Main_fonts_load");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Load a new font into the main database");
  parm = RNA_def_string_file_path(
      func, "filepath", "File Path", 0, "", "path of the font to load");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func,
                  "check_existing",
                  false,
                  "",
                  "Using existing data-block if this file is already loaded");
  /* return type */
  parm = RNA_def_pointer(func, "vfont", "VectorFont", "", "New font data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a font from the current blendfile");
  parm = RNA_def_pointer(func, "vfont", "VectorFont", "", "Font to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this font before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this font");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this font");

  func = RNA_def_function(srna, "tag", "rna_Main_fonts_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_textures(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataTextures");
  srna = RNA_def_struct(brna, "BlendDataTextures", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Textures", "Collection of textures");

  func = RNA_def_function(srna, "new", "rna_Main_textures_new");
  RNA_def_function_ui_description(func, "Add a new texture to the main database");
  parm = RNA_def_string(func, "name", "Texture", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(
      func, "type", rna_enum_texture_type_items, 0, "Type", "The type of texture to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "texture", "Texture", "", "New texture data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a texture from the current blendfile");
  parm = RNA_def_pointer(func, "texture", "Texture", "", "Texture to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this texture before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this texture");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this texture");

  func = RNA_def_function(srna, "tag", "rna_Main_textures_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_brushes(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataBrushes");
  srna = RNA_def_struct(brna, "BlendDataBrushes", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Brushes", "Collection of brushes");

  func = RNA_def_function(srna, "new", "rna_Main_brushes_new");
  RNA_def_function_ui_description(func, "Add a new brush to the main database");
  parm = RNA_def_string(func, "name", "Brush", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "mode",
                      rna_enum_object_mode_items,
                      OB_MODE_TEXTURE_PAINT,
                      "",
                      "Paint Mode for the new brush");
  /* return type */
  parm = RNA_def_pointer(func, "brush", "Brush", "", "New brush data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a brush from the current blendfile");
  parm = RNA_def_pointer(func, "brush", "Brush", "", "Brush to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this brush before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this brush");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this brush");

  func = RNA_def_function(srna, "tag", "rna_Main_brushes_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "create_gpencil_data", "rna_Main_brush_gpencil_data");
  RNA_def_function_ui_description(func, "Add grease pencil brush settings");
  parm = RNA_def_pointer(func, "brush", "Brush", "", "Brush");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
}

void RNA_def_main_worlds(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataWorlds");
  srna = RNA_def_struct(brna, "BlendDataWorlds", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Worlds", "Collection of worlds");

  func = RNA_def_function(srna, "new", "rna_Main_worlds_new");
  RNA_def_function_ui_description(func, "Add a new world to the main database");
  parm = RNA_def_string(func, "name", "World", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "world", "World", "", "New world data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a world from the current blendfile");
  parm = RNA_def_pointer(func, "world", "World", "", "World to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this world before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this world");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this world");

  func = RNA_def_function(srna, "tag", "rna_Main_worlds_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_collections(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataCollections");
  srna = RNA_def_struct(brna, "BlendDataCollections", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Collections", "Collection of collections");

  func = RNA_def_function(srna, "new", "rna_Main_collections_new");
  RNA_def_function_ui_description(func, "Add a new collection to the main database");
  parm = RNA_def_string(func, "name", "Collection", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "collection", "Collection", "", "New collection data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_ui_description(func, "Remove a collection from the current blendfile");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "collection", "Collection", "", "Collection to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this collection before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this collection");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this collection");

  func = RNA_def_function(srna, "tag", "rna_Main_collections_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_speakers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataSpeakers");
  srna = RNA_def_struct(brna, "BlendDataSpeakers", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Speakers", "Collection of speakers");

  func = RNA_def_function(srna, "new", "rna_Main_speakers_new");
  RNA_def_function_ui_description(func, "Add a new speaker to the main database");
  parm = RNA_def_string(func, "name", "Speaker", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "speaker", "Speaker", "", "New speaker data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a speaker from the current blendfile");
  parm = RNA_def_pointer(func, "speaker", "Speaker", "", "Speaker to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this speaker before deleting it "
                  "(WARNING: will also delete objects instancing that speaker data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this speaker data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this speaker data");

  func = RNA_def_function(srna, "tag", "rna_Main_speakers_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_texts(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataTexts");
  srna = RNA_def_struct(brna, "BlendDataTexts", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Texts", "Collection of texts");

  func = RNA_def_function(srna, "new", "rna_Main_texts_new");
  RNA_def_function_ui_description(func, "Add a new text to the main database");
  parm = RNA_def_string(func, "name", "Text", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "text", "Text", "", "New text data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_ui_description(func, "Remove a text from the current blendfile");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "text", "Text", "", "Text to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this text before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this text");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this text");

  /* load func */
  func = RNA_def_function(srna, "load", "rna_Main_texts_load");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Add a new text to the main database from a file");
  parm = RNA_def_string_file_path(
      func, "filepath", "Path", FILE_MAX, "", "path for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(
      func, "internal", 0, "Make internal", "Make text file internal after loading");
  /* return type */
  parm = RNA_def_pointer(func, "text", "Text", "", "New text data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "tag", "rna_Main_texts_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_sounds(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataSounds");
  srna = RNA_def_struct(brna, "BlendDataSounds", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Sounds", "Collection of sounds");

  /* load func */
  func = RNA_def_function(srna, "load", "rna_Main_sounds_load");
  RNA_def_function_ui_description(func, "Add a new sound to the main database from a file");
  parm = RNA_def_string_file_path(
      func, "filepath", "Path", FILE_MAX, "", "path for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func,
                  "check_existing",
                  false,
                  "",
                  "Using existing data-block if this file is already loaded");
  /* return type */
  parm = RNA_def_pointer(func, "sound", "Sound", "", "New text data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a sound from the current blendfile");
  parm = RNA_def_pointer(func, "sound", "Sound", "", "Sound to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this sound before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this sound");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this sound");

  func = RNA_def_function(srna, "tag", "rna_Main_sounds_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_armatures(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataArmatures");
  srna = RNA_def_struct(brna, "BlendDataArmatures", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Armatures", "Collection of armatures");

  func = RNA_def_function(srna, "new", "rna_Main_armatures_new");
  RNA_def_function_ui_description(func, "Add a new armature to the main database");
  parm = RNA_def_string(func, "name", "Armature", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "armature", "Armature", "", "New armature data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove an armature from the current blendfile");
  parm = RNA_def_pointer(func, "armature", "Armature", "", "Armature to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this armature before deleting it "
                  "(WARNING: will also delete objects instancing that armature data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this armature data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this armature data");

  func = RNA_def_function(srna, "tag", "rna_Main_armatures_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_actions(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataActions");
  srna = RNA_def_struct(brna, "BlendDataActions", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Actions", "Collection of actions");

  func = RNA_def_function(srna, "new", "rna_Main_actions_new");
  RNA_def_function_ui_description(func, "Add a new action to the main database");
  parm = RNA_def_string(func, "name", "Action", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "action", "Action", "", "New action data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove an action from the current blendfile");
  parm = RNA_def_pointer(func, "action", "Action", "", "Action to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this action before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this action");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this action");

  func = RNA_def_function(srna, "tag", "rna_Main_actions_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_particles(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataParticles");
  srna = RNA_def_struct(brna, "BlendDataParticles", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Particle Settings", "Collection of particle settings");

  func = RNA_def_function(srna, "new", "rna_Main_particles_new");
  RNA_def_function_ui_description(func,
                                  "Add a new particle settings instance to the main database");
  parm = RNA_def_string(func, "name", "ParticleSettings", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(
      func, "particle", "ParticleSettings", "", "New particle settings data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(
      func, "Remove a particle settings instance from the current blendfile");
  parm = RNA_def_pointer(func, "particle", "ParticleSettings", "", "Particle Settings to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of those particle settings before deleting them");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this particle settings");
  RNA_def_boolean(func,
                  "do_ui_user",
                  true,
                  "",
                  "Make sure interface does not reference this particle settings");

  func = RNA_def_function(srna, "tag", "rna_Main_particles_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_palettes(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataPalettes");
  srna = RNA_def_struct(brna, "BlendDataPalettes", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Palettes", "Collection of palettes");

  func = RNA_def_function(srna, "new", "rna_Main_palettes_new");
  RNA_def_function_ui_description(func, "Add a new palette to the main database");
  parm = RNA_def_string(func, "name", "Palette", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "palette", "Palette", "", "New palette data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a palette from the current blendfile");
  parm = RNA_def_pointer(func, "palette", "Palette", "", "Palette to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this palette before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this palette");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this palette");

  func = RNA_def_function(srna, "tag", "rna_Main_palettes_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_cachefiles(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataCacheFiles");
  srna = RNA_def_struct(brna, "BlendDataCacheFiles", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Cache Files", "Collection of cache files");

  func = RNA_def_function(srna, "tag", "rna_Main_cachefiles_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_paintcurves(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataPaintCurves");
  srna = RNA_def_struct(brna, "BlendDataPaintCurves", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Paint Curves", "Collection of paint curves");

  func = RNA_def_function(srna, "tag", "rna_Main_paintcurves_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
void RNA_def_main_gpencil(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataGreasePencils");
  srna = RNA_def_struct(brna, "BlendDataGreasePencils", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Grease Pencils", "Collection of grease pencils");

  func = RNA_def_function(srna, "tag", "rna_Main_gpencils_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "new", "rna_Main_gpencils_new");
  RNA_def_function_ui_description(func, "Add a new grease pencil datablock to the main database");
  parm = RNA_def_string(func, "name", "GreasePencil", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(
      func, "grease_pencil", "GreasePencil", "", "New grease pencil data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func,
                                  "Remove a grease pencil instance from the current blendfile");
  parm = RNA_def_pointer(func, "grease_pencil", "GreasePencil", "", "Grease Pencil to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this grease pencil before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this grease pencil");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this grease pencil");
}

void RNA_def_main_movieclips(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataMovieClips");
  srna = RNA_def_struct(brna, "BlendDataMovieClips", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Movie Clips", "Collection of movie clips");

  func = RNA_def_function(srna, "tag", "rna_Main_movieclips_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a movie clip from the current blendfile.");
  parm = RNA_def_pointer(func, "clip", "MovieClip", "", "Movie clip to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this movie clip before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this movie clip");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this movie clip");

  /* load func */
  func = RNA_def_function(srna, "load", "rna_Main_movieclip_load");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(
      func,
      "Add a new movie clip to the main database from a file "
      "(while ``check_existing`` is disabled for consistency with other load functions, "
      "behavior with multiple movie-clips using the same file may incorrectly generate proxies)");
  parm = RNA_def_string_file_path(
      func, "filepath", "Path", FILE_MAX, "", "path for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func,
                  "check_existing",
                  false,
                  "",
                  "Using existing data-block if this file is already loaded");
  /* return type */
  parm = RNA_def_pointer(func, "clip", "MovieClip", "", "New movie clip data-block");
  RNA_def_function_return(func, parm);
}

void RNA_def_main_masks(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataMasks");
  srna = RNA_def_struct(brna, "BlendDataMasks", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Masks", "Collection of masks");

  func = RNA_def_function(srna, "tag", "rna_Main_masks_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  /* new func */
  func = RNA_def_function(srna, "new", "rna_Main_mask_new");
  RNA_def_function_ui_description(func, "Add a new mask with a given name to the main database");
  parm = RNA_def_string(
      func, "name", NULL, MAX_ID_NAME - 2, "Mask", "Name of new mask data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "mask", "Mask", "", "New mask data-block");
  RNA_def_function_return(func, parm);

  /* remove func */
  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a mask from the current blendfile");
  parm = RNA_def_pointer(func, "mask", "Mask", "", "Mask to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this mask before deleting it");
  RNA_def_boolean(
      func, "do_id_user", true, "", "Decrement user counter of all datablocks used by this mask");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this mask");
}

void RNA_def_main_linestyles(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataLineStyles");
  srna = RNA_def_struct(brna, "BlendDataLineStyles", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Line Styles", "Collection of line styles");

  func = RNA_def_function(srna, "tag", "rna_Main_linestyle_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "new", "rna_Main_linestyles_new");
  RNA_def_function_ui_description(func, "Add a new line style instance to the main database");
  parm = RNA_def_string(func, "name", "FreestyleLineStyle", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "linestyle", "FreestyleLineStyle", "", "New line style data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a line style instance from the current blendfile");
  parm = RNA_def_pointer(func, "linestyle", "FreestyleLineStyle", "", "Line style to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this line style before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this line style");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this line style");
}

void RNA_def_main_workspaces(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataWorkSpaces");
  srna = RNA_def_struct(brna, "BlendDataWorkSpaces", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Workspaces", "Collection of workspaces");

  func = RNA_def_function(srna, "tag", "rna_Main_workspaces_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_lightprobes(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataProbes");
  srna = RNA_def_struct(brna, "BlendDataProbes", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Light Probes", "Collection of light probes");

  func = RNA_def_function(srna, "new", "rna_Main_lightprobe_new");
  RNA_def_function_ui_description(func, "Add a new light probe to the main database");
  parm = RNA_def_string(func, "name", "Probe", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(
      func, "type", rna_enum_lightprobes_type_items, 0, "Type", "The type of light probe to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "lightprobe", "LightProbe", "", "New light probe data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a light probe from the current blendfile");
  parm = RNA_def_pointer(func, "lightprobe", "LightProbe", "", "Light probe to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this light probe before deleting it "
                  "(WARNING: will also delete objects instancing that light probe data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this light probe");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this light probe");

  func = RNA_def_function(srna, "tag", "rna_Main_lightprobes_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

#  ifdef WITH_NEW_CURVES_TYPE
void RNA_def_main_hair_curves(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataHairCurves");
  srna = RNA_def_struct(brna, "BlendDataHairCurves", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Hair Curves", "Collection of hair curves");

  func = RNA_def_function(srna, "new", "rna_Main_hair_curves_new");
  RNA_def_function_ui_description(func, "Add a new hair to the main database");
  parm = RNA_def_string(func, "name", "Curves", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "curves", "Curves", "", "New curves data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a curves data-block from the current blendfile");
  parm = RNA_def_pointer(func, "curves", "Curves", "", "Curves data-block to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this curves before deleting it "
                  "(WARNING: will also delete objects instancing that curves data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this curves data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this curves data");

  func = RNA_def_function(srna, "tag", "rna_Main_hair_curves_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
#  endif

void RNA_def_main_pointclouds(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataPointClouds");
  srna = RNA_def_struct(brna, "BlendDataPointClouds", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Point Clouds", "Collection of point clouds");

  func = RNA_def_function(srna, "new", "rna_Main_pointclouds_new");
  RNA_def_function_ui_description(func, "Add a new point cloud to the main database");
  parm = RNA_def_string(func, "name", "PointCloud", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "pointcloud", "PointCloud", "", "New point cloud data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a point cloud from the current blendfile");
  parm = RNA_def_pointer(func, "pointcloud", "PointCloud", "", "Point cloud to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this point cloud before deleting it "
                  "(WARNING: will also delete objects instancing that point cloud data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this point cloud data");
  RNA_def_boolean(func,
                  "do_ui_user",
                  true,
                  "",
                  "Make sure interface does not reference this point cloud data");

  func = RNA_def_function(srna, "tag", "rna_Main_pointclouds_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

void RNA_def_main_volumes(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataVolumes");
  srna = RNA_def_struct(brna, "BlendDataVolumes", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Volumes", "Collection of volumes");

  func = RNA_def_function(srna, "new", "rna_Main_volumes_new");
  RNA_def_function_ui_description(func, "Add a new volume to the main database");
  parm = RNA_def_string(func, "name", "Volume", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "volume", "Volume", "", "New volume data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a volume from the current blendfile");
  parm = RNA_def_pointer(func, "volume", "Volume", "", "Volume to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(func,
                  "do_unlink",
                  true,
                  "",
                  "Unlink all usages of this volume before deleting it "
                  "(WARNING: will also delete objects instancing that volume data)");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this volume data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this volume data");

  func = RNA_def_function(srna, "tag", "rna_Main_volumes_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

#  ifdef WITH_SIMULATION_DATABLOCK
void RNA_def_main_simulations(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "BlendDataSimulations");
  srna = RNA_def_struct(brna, "BlendDataSimulations", NULL);
  RNA_def_struct_sdna(srna, "Main");
  RNA_def_struct_ui_text(srna, "Main Simulations", "Collection of simulations");

  func = RNA_def_function(srna, "new", "rna_Main_simulations_new");
  RNA_def_function_ui_description(func, "Add a new simulation to the main database");
  parm = RNA_def_string(func, "name", "Simulation", 0, "", "New name for the data-block");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "simulation", "Simulation", "", "New simulation data-block");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Main_ID_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a simulation from the current blendfile");
  parm = RNA_def_pointer(func, "simulation", "Simulation", "", "Simulation to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_boolean(
      func, "do_unlink", true, "", "Unlink all usages of this simulation before deleting it");
  RNA_def_boolean(func,
                  "do_id_user",
                  true,
                  "",
                  "Decrement user counter of all datablocks used by this simulation data");
  RNA_def_boolean(
      func, "do_ui_user", true, "", "Make sure interface does not reference this simulation data");

  func = RNA_def_function(srna, "tag", "rna_Main_simulations_tag");
  parm = RNA_def_boolean(func, "value", 0, "Value", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}
#  endif

#endif
