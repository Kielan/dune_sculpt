#include <stdlib.h>
#include <string.h>

#include "lib_path_util.h"
#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "dune_global.h"
#  include "dune_main.h"
#  include "dune_mesh.h"

/* all the list begin functions are added manually here, Main is not in stype */
static bool api_Main_use_autopack_get(ApiPtr *UNUSED(ptr))
{
  if (G.fileflags & G_FILE_AUTOPACK) {
    return 1;
  }

  return 0;
}

static void api_Main_use_autopack_set(ApiPtr *UNUSED(ptr), bool val)
{
  if (value) {
    G.fileflags |= G_FILE_AUTOPACK;
  }
  else {
    G.fileflags &= ~G_FILE_AUTOPACK;
  }
}

static bool api_Main_is_saved_get(ApiPtr *ptr)
{
  const Main *main = (Main *)ptr->data;
  return (main->filepath[0] != '\0');
}

static bool api_Main_is_dirty_get(ApiPtr *ptr)
{
  /* not totally nice to do it this way, should store in main ? */
  Main *main = (Main *)ptr->data;
  wmWindowManager *wm;
  if ((wm = main->wm.first)) {
    return !wm->file_saved;
  }

  return true;
}

static void api_Main_filepath_get(ApiPtr *ptr, char *val)
{
  Main *main = (Main *)ptr->data;
  lib_strncpy(val, main->filepath, sizeof(main->filepath));
}

static int api_Main_filepath_length(ApiPtr *ptr)
{
  Main *main = (Main *)ptr->data;
  return strlen(main->filepath);
}

#  if 0
static void api_Main_filepath_set(ApiPtr *ptr, const char *val)
{
  Main *main = (Main *)ptr->data;
  STRNCPY(main->filepath, val);
}
#  endif

#  define API_MAIN_LIST_FNS_DEF(_list_name) \
    static void api_Main_##_list_name##_begin(CollectionPropIter *iter, \
                                              ApiPtr *ptr) \
  
      api_iter_list_begin(iter, &((Main *)ptr->data)->_list_name, NULL); \
    }

API_MAIN_LIST_FNS_DEF(actions)
API_MAIN_LIST_FNS_DEF(armatures)
API_MAIN_LIST_FNS_DEF(brushes)
API_MAIN_LIST_FNS_DEF(cachefiles)
API_MAIN_LIST_FNS_DEF(cameras)
API_MAIN_LIST_FNS_DEF(collections)
API_MAIN_LIST_FNS_DEF(curves)
API_MAIN_LIST_FNS_DEF(fonts)
API_MAIN_LIST_FNS_DEF(pens)
#  ifdef WITH_NEW_CURVES_TYPE
API_MAIN_LIST_FNS_DEF(hair_curves)
#  endif
API_MAIN_LIST_FNS_DEF(images)
API_MAIN_LIST_FNS_DEF(lattices)
API_MAIN_LIST_FNS_DEF(libs)
API_MAIN_LIST_FNS_DEF(lightprobes)
API_MAIN_LIST_FNS_DEF(lights)
API_MAIN_LIST_FNS_DEF(linestyles)
API_MAIN_LIST_FNS_DEF(masks)
API_MAIN_LIST_FNS_DEF(materials)
API_MAIN_LIST_FNS_DEF(meshes)
API_MAIN_LIST_FNS_DEF(metaballs)
API_MAIN_LIST_FNS_DEF(movieclips)
API_MAIN_LIST_FNS_DEF(nodetrees)
API_MAIN_LIST_FNS_DEF(objects)
API_MAIN_LIST_FNS_DEF(paintcurves)
API_MAIN_LIST_FNS_DEF(palettes)
RNA_MAIN_LIST_FNS_DEF(particles)
API_MAIN_LIST_FNS_DEF(pointclouds)
API_MAIN_LIST_FNS_DEF(scenes)
API_MAIN_LIST_FNS_DEF(screens)
API_MAIN_LIST_FNS_DEF(shapekeys)
#  ifdef WITH_SIMULATION_DATABLOCK
API_MAIN_LIST_FNS_DEF(simulations)
#  endif
API_MAIN_LIST_FNS_DEF(sounds)
API_MAIN_LIST_FNS_DEF(speakers)
API_MAIN_LIST_FNS_DEF(texts)
API_MAIN_LIST_FNS_DEF(textures)
API_MAIN_LIST_FNS_DEF(volumes)
API_MAIN_LIST_FNS_DEF(wm)
API_MAIN_LIST_FNS_DEF(workspaces)
API_MAIN_LIST_FNS_DEF(worlds)

#  undef API_MAIN_LIST_FNS_DEF

static void api_Main_version_get(ApiPtr *ptr, int *value)
{
  Main *main = (Main *)ptr->data;
  value[0] = main->versionfile / 100;
  value[1] = main->versionfile % 100;
  value[2] =main->subversionfile;
}

#  ifdef UNIT_TEST

static ApiPtr api_Test_test_get(ApiPtr *ptr)
{
  ApiPtr ret = *ptr;
  ret.type = &Api_Test;

  return ret;
}

#  endif

#else

/* local convenience types */
typedef void(CollectionDefFn)(struct DuneApi *dapi, struct ApiProp *cprop);

typedef struct MainCollectionDef {
  const char *id;
  const char *type;
  const char *iter_begin;
  const char *name;
  const char *description;
  CollectionDefFn *fb;
} MainCollectionDef;

void api_def_main(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiPrip *prop;
  CollectionDefFn fn;

  /* plural must match idtypes in readblenentry.c */
  MainCollectionDef lists[] = {
      {"cameras",
       "Camera",
       "api_Main_cameras_begin",
       "Cameras",
       "Camera data-blocks",
       api_def_main_cameras},
      {"scenes",
       "Scene",
       "api_Main_scenes_begin",
       "Scenes",
       "Scene data-blocks",
       api_def_main_scenes},
      {"objects",
       "Object",
       "api_Main_objects_begin",
       "Objects",
       "Object data-blocks",
       api_def_main_objects},
      {"materials",
       "Material",
       "api_Main_materials_begin",
       "Materials",
       "Material data-blocks",
       api_def_main_materials},
      {"node_groups",
       "NodeTree",
       "api_Main_nodetrees_begin",
       "Node Groups",
       "Node group data-blocks",
       api_def_main_node_groups},
      {"meshes",
       "Mesh",
       "api_Main_meshes_begin",
       "Meshes",
       "Mesh data-blocks",
       api_def_main_meshes},
      {"lights",
       "Light",
       "api_Main_lights_begin",
       "Lights",
       "Light data-blocks",
       api_def_main_lights},
      {"libs",
       "Lib",
       "api_Main_libs_begin",
       "Libs",
       "Lib data-blocks",
       api_def_main_libs},
      {"screens",
       "Screen",
       "api_Main_screens_begin",
       "Screens",
       "Screen data-blocks",
       api_def_main_screens},
      {"window_managers",
       "WindowManager",
       "rna_Main_wm_begin",
       "Window Managers",
       "Window manager data-blocks",
       api_def_main_window_managers},
      {"images",
       "Image",
       "api_Main_images_begin",
       "Images",
       "Image data-blocks",
       api_def_main_images},
      {"lattices",
       "Lattice",
       "api_Main_lattices_begin",
       "Lattices",
       "Lattice data-blocks",
       api_def_main_lattices},
      {"curves",
       "Curve",
       "rna_Main_curves_begin",
       "Curves",
       "Curve data-blocks",
       api_def_main_curves},
      {"metaballs",
       "MetaBall",
       "api_Main_metaballs_begin",
       "Metaballs",
       "Metaball data-blocks",
       api_def_main_metaballs},
      {"fonts",
       "VectorFont",
       "api_Main_fonts_begin",
       "Vector Fonts",
       "Vector font data-blocks",
       api_def_main_fonts},
      {"textures",
       "Texture",
       "api_Main_textures_begin",
       "Textures",
       "Texture data-blocks",
       api_def_main_textures},
      {"brushes",
       "Brush",
       "api_Main_brushes_begin",
       "Brushes",
       "Brush data-blocks",
       api_def_main_brushes},
      {"worlds",
       "World",
       "api_Main_worlds_begin",
       "Worlds",
       "World data-blocks",
       api_def_main_worlds},
      {"collections",
       "Collection",
       "api_Main_collections_begin",
       "Collections",
       "Collection data-blocks",
       api_def_main_collections},
      {"shape_keys",
       "Key",
       "api_Main_shapekeys_begin",
       "Shape Keys",
       "Shape Key data-blocks",
       NULL},
      {"texts", "Text", "api_Main_texts_begin", "Texts", "Text data-blocks", RNA_def_main_texts},
      {"speakers",
       "Speaker",
       "api_Main_speakers_begin",
       "Speakers",
       "Speaker data-blocks",
       api_def_main_speakers},
      {"sounds",
       "Sound",
       "api_Main_sounds_begin",
       "Sounds",
       "Sound data-blocks",
       api_def_main_sounds},
      {"armatures",
       "Armature",
       "api_Main_armatures_begin",
       "Armatures",
       "Armature data-blocks",
       api_def_main_armatures},
      {"actions",
       "Action",
       "api_Main_actions_begin",
       "Actions",
       "Action data-blocks",
       api_def_main_actions},
      {"particles",
       "ParticleSettings",
       "api_Main_particles_begin",
       "Particles",
       "Particle data-blocks",
       api_def_main_particles},
      {"palettes",
       "Palette",
       "api_Main_palettes_begin",
       "Palettes",
       "Palette data-blocks",
       api_def_main_palettes},
      {"pen",
       "ePen",
       "api_Main_pen_begin",
       "Pen",
       "Pen data-blocks",
       api_def_main_pen},
      {"movieclips",
       "MovieClip",
       "api_Main_movieclips_begin",
       "Movie Clips",
       "Movie Clip data-blocks",
       api_def_main_movieclips},
      {"masks", "Mask", "rna_Main_masks_begin", "Masks", "Masks data-blocks", RNA_def_main_masks},
      {"linestyles",
       "FreestyleLineStyle",
       "rna_Main_linestyles_begin",
       "Line Styles",
       "Line Style data-blocks",
       api_def_main_linestyles},
      {"cache_files",
       "CacheFile",
       "rna_Main_cachefiles_begin",
       "Cache Files",
       "Cache Files data-blocks",
       api_def_main_cachefiles},
      {"paint_curves",
       "PaintCurve",
       "api_Main_paintcurves_begin",
       "Paint Curves",
       "Paint Curves data-blocks",
       api_def_main_paintcurves},
      {"workspaces",
       "WorkSpace",
       "api_Main_workspaces_begin",
       "Workspaces",
       "Workspace data-blocks",
       api_def_main_workspaces},
      {"lightprobes",
       "LightProbe",
       "api_Main_lightprobes_begin",
       "Light Probes",
       "Light Probe data-blocks",
       api_def_main_lightprobes},
#  ifdef WITH_NEW_CURVES_TYPE
      /** The name `hair_curves` is chosen to be different than `curves`,
       * but they are generic curve data-blocks, not just for hair. */
      {"hair_curves",
       "Curves",
       "api_Main_hair_curves_begin",
       "Hair Curves",
       "Hair curve data-blocks",
       api_def_main_hair_curves},
#  endif
      {"pointclouds",
       "PointCloud",
       "api_Main_pointclouds_begin",
       "Point Clouds",
       "Point cloud data-blocks",
       api_def_main_pointclouds},
      {"volumes",
       "Volume",
       "api_Main_volumes_begin",
       "Volumes",
       "Volume data-blocks",
       api_def_main_volumes},
#  ifdef WITH_SIMULATION_DATABLOCK
      {"simulations",
       "Simulation",
       "api_Main_simulations_begin",
       "Simulations",
       "Simulation data-blocks",
       api_def_main_simulations},
#  endif
      {NULL, NULL, NULL, NULL, NULL, NULL},
  };

  int i;

  sapi = api_def_struct(dapi, "DuneData", NULL);
  api_def_struct_ui_text(sapi,
                         "Dune-File Data",
                         "Main data structure representing a .dune file and all its data-blocks");
  api_def_struct_ui_icon(sapi, ICON_DUNE);

  prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_maxlength(prop, FILE_MAX);
  api_def_prop_string_fns(
      prop, "api_Main_filepath_get", "api_Main_filepath_length", "api_Main_filepath_set");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Filename", "Path to the .dune file");

  prop = api_def_prop(sapi, "is_dirty", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Main_is_dirty_get", NULL);
  api_def_prop_ui_text(
      prop, "File Has Unsaved Changes", "Have recent edits been saved to disk");

  prop = api_def_prop(sapi, "is_saved", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Main_is_saved_get", NULL);
  api_def_prop_ui_text(
      prop, "File is Saved", "Has the current session been saved to disk as a .blend file");

  prop = api_def_prop(sapi, "use_autopack", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_Main_use_autopack_get", "rna_Main_use_autopack_set");
  api_def_prop_ui_text(
      prop, "Use Auto-Pack", "Automatically pack all external data into .blend file");

  prop = api_def_int_vector(sapi,
                            "version",
                            3,
                            NULL,
                            0,
                            INT_MAX,
                            "Version",
                            "File format version the .blend file was saved with",
                            0,
                            INT_MAX);
  api_def_prop_int_fns(prop, "api_Main_version_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_THICK_WRAP);

  for (i = 0; lists[i].name; i++) {
    prop = api_def_prop(sapi, lists[i].id, PROP_COLLECTION, PROP_NONE);
    api_def_prop_struct_type(prop, lists[i].type);
    api_def_prop_collection_fn(prop,
                                lists[i].iter_begin,
                                "api_iter_list_next",
                                "api_iter_list_end",
                                "api_iter_list_get",
                                NULL,
                                NULL,
                                NULL,
                                NULL);
    api_def_prop_ui_txt(prop, lists[i].name, lists[i].description);

    /* collection fns */
    fn = lists[i].fn;
    if (fn) {
      fn(dapi, prop);
    }
  }

  api_main(sapi);

#  ifdef UNIT_TEST

  api_define_verify_stype(0);

  prop = api_def_prop(sapi, "test", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Test");
  api_def_prop_ptr_fns(prop, "api_Test_test_get", NULL, NULL, NULL);

  api_define_verify_stype(1);

#  endif
}

#endif
