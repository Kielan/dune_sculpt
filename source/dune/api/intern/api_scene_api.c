#include <stdio.h>
#include <stdlib.h>

#include "lib_kdopbvh.h"
#include "lib_path_util.h"
#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "types_anim.h"
#include "types_object.h"
#include "types_scene.h"

#include "api_internal.h" /* own include */

#ifdef WITH_ALEMBIC
#  include "ABC_alembic.h"
#endif

#ifdef API_RUNTIME

#  include "dune_editmesh.h"
#  include "dune_global.h"
#  include "dune_image.h"
#  include "dune_scene.h"
#  include "dune_writeavi.h"

#  include "graph_query.h"

#  include "ed_transform.h"
#  include "ed_transform_snap_object_context.h"
#  include "ed_uvedit.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

static void api_Scene_frame_set(Scene *scene, Main *main, int frame, float subframe)
{
  double cfra = (double)frame + (double)subframe;

  CLAMP(cfra, MINAFRAME, MAXFRAME);
  dune_scene_frame_set(scene, cfra);

#  ifdef WITH_PYTHON
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  for (ViewLayer *view_layer = scene->view_layers.first; view_layer != NULL;
       view_layer = view_layer->next)
  {
    Graph *graph = dune_scene_ensure_graph(main, scene, view_layer);
    dune_scene_graph_update_for_newframe(graph);
  }

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif

  if (dune_scene_camera_switch_update(scene)) {
    for (Screen *screen = main->screens.first; screen; screen = screen->id.next) {
      dune_screen_view3d_scene_sync(screen, scene);
    }
  }

  /* don't do notifier when we're rendering, avoid some viewport crashes
   * redrawing while the data is being modified for render */
  if (!G.is_rendering) {
    /* can't use NC_SCENE|ND_FRAME because this causes wm_event_do_notifiers to call
     * dune_scene_graph_update_for_newframe which will lose any un-keyed changes #24690. */
    // wm_main_add_notifier(NC_SCENE|ND_FRAME, scene);

    /* instead just redraw the views */
    wm_main_add_notifier(NC_WINDOW, NULL);
  }
}

static void api_Scene_uvedit_aspect(Scene *UNUSED(scene), Object *ob, float aspect[2])
{
  if ((ob->type == OB_MESH) && (ob->mode == OB_MODE_EDIT)) {
    MeshEditMesh *em;
    em = dune_editmesh_from_object(ob);
    if (EDBM_uv_check(em)) {
      ed_uvedit_get_aspect(ob, aspect, aspect + 1);
      return;
    }
  }

  aspect[0] = aspect[1] = 1.0f;
}

static void api_SceneRender_get_frame_path(
    RenderData *rd, Main *main, int frame, bool preview, const char *view, char *filepath)
{
  const char *suffix = dune_scene_multiview_view_suffix_get(rd, view);

  /* avoid NULL pointer */
  if (!suffix) {
    suffix = "";
  }

  if (dune_imtype_is_movie(rd->im_format.imtype)) {
    dune_movie_filepath_get(filepath, rd, preview != 0, suffix);
  } else {
    dune_image_path_from_imformat(filepath,
                                 rd->pic,
                                 dune_main_dunefile_path(main),
                                 (frame == INT_MIN) ? rd->cfra : frame,
                                 &rd->im_format,
                                 (rd->scemode & R_EXTENSION) != 0,
                                 true,
                                 suffix);
  }
}

static void api_Scene_ray_cast(Scene *scene,
                               Graph *graph,
                               float origin[3],
                               float direction[3],
                               float ray_dist,
                               bool *r_success,
                               float r_location[3],
                               float r_normal[3],
                               int *r_index,
                               Object **r_ob,
                               float r_obmat[16])
{
  normalize_v3(direction);
  SnapObjectCxt *scxt = ed_transform_snap_object_context_create(scene, 0);

  bool ret = ed_transform_snap_object_project_ray_ex(sctx,
                                                     graph,
                                                     NULL,
                                                     &(const struct SnapObjectParams){
                                                         .snap_target_select = SCE_SNAP_TARGET_ALL,
                                                     },
                                                     origin,
                                                     direction,
                                                     &ray_dist,
                                                     r_location,
                                                     r_normal,
                                                     r_index,
                                                     r_ob,
                                                     (float(*)[4])r_obmat);

  ed_transform_snap_object_cxt_destroy(scxt);

  if (r_ob != NULL && *r_ob != NULL) {
    *r_ob = graph_get_original_object(*r_ob);
  }

  if (ret) {
    *r_success = true;
  } else {
    *r_success = false;

    unit_m4((float(*)[4])r_obmat);
    zero_v3(r_location);
    zero_v3(r_normal);
  }
}

static void api_Scene_seq_editing_free(Scene *scene)
{
  seq_editing_free(scene, true);
}

#  ifdef WITH_ALEMBIC

static void api_Scene_alembic_export(Scene *scene,
                                     Cxt *C,
                                     const char *filepath,
                                     int frame_start,
                                     int frame_end,
                                     int xform_samples,
                                     int geom_samples,
                                     float shutter_open,
                                     float shutter_close,
                                     bool selected_only,
                                     bool uvs,
                                     bool normals,
                                     bool vcolors,
                                     bool apply_subdiv,
                                     bool flatten_hierarchy,
                                     bool visible_objects_only,
                                     bool face_sets,
                                     bool use_subdiv_schema,
                                     bool export_hair,
                                     bool export_particles,
                                     bool packuv,
                                     float scale,
                                     bool triangulate,
                                     int quad_method,
                                     int ngon_method)
{
/* We have to enable allow_threads, because we may change scene frame number
 * during export. */
#    ifdef WITH_PYTHON
  BPy_BEGIN_ALLOW_THREADS;
#    endif

  const struct AlembicExportParams params = {
      .frame_start = frame_start,
      .frame_end = frame_end,

      .frame_samples_xform = xform_samples,
      .frame_samples_shape = geom_samples,

      .shutter_open = shutter_open,
      .shutter_close = shutter_close,

      .selected_only = selected_only,
      .uvs = uvs,
      .normals = normals,
      .vcolors = vcolors,
      .apply_subdiv = apply_subdiv,
      .flatten_hierarchy = flatten_hierarchy,
      .visible_objects_only = visible_objects_only,
      .face_sets = face_sets,
      .use_subdiv_schema = use_subdiv_schema,
      .export_hair = export_hair,
      .export_particles = export_particles,
      .packuv = packuv,
      .triangulate = triangulate,
      .quad_method = quad_method,
      .ngon_method = ngon_method,

      .global_scale = scale,
  };

  ABC_export(scene, C, filepath, &params, true);

#    ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#    endif
}

#  endif

#else

void api_scene(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "frame_set", "api_Scene_frame_set");
  api_def_fn_ui_description(fn, "Set scene frame updating all objects immediately");
  parm = api_def_int(
      fn, "frame", 0, MINAFRAME, MAXFRAME, "", "Frame number to set", MINAFRAME, MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_float(
      fn, "subframe", 0.0, 0.0, 1.0, "", "Subframe time, between 0.0 and 1.0", 0.0, 1.0);
  api_def_fn_flag(fn, FN_USE_MAIN);

  fn = apj_def_fn(sapi, "uvedit_aspect", "rna_Scene_uvedit_aspect");
  api_def_fn_ui_description(fn, "Get uv aspect for current object");
  parm = apj_def_ptr(fn, "object", "Object", "", "Object");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_float_vector(fn, "result", 2, NULL, 0.0f, FLT_MAX, "", "aspect", 0.0f, FLT_MAX);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);

  /* Ray Cast */
  fn = api_def_fn(sapi, "ray_cast", "rna_Scene_ray_cast");
  api_def_fn_ui_description(fn, "Cast a ray onto in object space");

  parm = api_def_ptr(fn, "graph", "Graph", "", "The current dependency graph");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  /* ray start and end */
  parm = api_def_float_vector(fn, "origin", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float_vector(fn, "direction", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_float(fn,
                "distance",
                BVH_RAYCAST_DIST_MAX,
                0.0,
                BVH_RAYCAST_DIST_MAX,
                "",
                "Maximum distance",
                0.0,
                BVH_RAYCAST_DIST_MAX);
  /* return location and normal */
  parm = api_def_bool(fn, "result", 0, "", "");
  api_def_fn_output(fn, parm);
  parm = api_def_float_vector(fn,
                              "location",
                              3,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "The hit location of this ray cast",
                              -1e4,
                              1e4);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_float_vector(fn,
                              "normal",
                              3,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Normal",
                              "The face normal at the ray cast hit location",
                              -1e4,
                              1e4);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_int(
      fn, "index", 0, 0, 0, "", "The face index, -1 when original data isn't available", 0, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_ptr(fn, "object", "Object", "", "Ray cast object");
  api_def_fn_output(fn, parm);
  parm = api_def_float_matrix(fn, "matrix", 4, 4, NULL, 0.0f, 0.0f, "", "Matrix", 0.0f, 0.0f);
  api_def_fn_output(fn, parm);

  /* Sequencer. */
  fn = api_def_fn(sapi, "seq_editor_create", "seq_editing_ensure");
  api_def_fn_ui_description(fn, "Ensure sequence editor is valid in this scene");
  parm = api_def_ptr(
      fn, "seq_editor", "SeqEditor", "", "New sequence editor data or NULL");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "seq_editor_clear", "api_Scene_seq_editing_free");
  api_def_fn_ui_description(fn, "Clear sequence editor in this scene");

#  ifdef WITH_ALEMBIC
  /* XXX Deprecated, will be removed in 2.8 in favor of calling the export operator. */
  func = api_def_function(sapi, "alembic_export", "api_Scene_alembic_export");
  apu_def_fn_ui_description(
      fn, "Export to Alembic file (deprecated, use the Alembic export operator)");

  parm = api_def_string(
      fn, "filepath", NULL, FILE_MAX, "File Path", "File path to write Alembic file");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_subtype(parm, PROP_FILEPATH); /* allow non utf8 */

  api_def_int(fn, "frame_start", 1, INT_MIN, INT_MAX, "Start", "Start Frame", INT_MIN, INT_MAX);
  api_def_int(fn, "frame_end", 1, INT_MIN, INT_MAX, "End", "End Frame", INT_MIN, INT_MAX);
  api_def_int(
      fn, "xform_samples", 1, 1, 128, "Xform samples", "Transform samples per frame", 1, 128);
  api_def_int(
      fn, "geom_samples", 1, 1, 128, "Geom samples", "Geometry samples per frame", 1, 128);
  api_def_float(fn, "shutter_open", 0.0f, -1.0f, 1.0f, "Shutter open", "", -1.0f, 1.0f);
  api_def_float(fn, "shutter_close", 1.0f, -1.0f, 1.0f, "Shutter close", "", -1.0f, 1.0f);
  api_def_bool(fn, "selected_only", 0, "Selected only", "Export only selected objects");
  api_def_bool(fn, "uvs", 1, "UVs", "Export UVs");
  api_def_bool(fn, "normals", 1, "Normals", "Export normals");
  api_def_bool(fn, "vcolors", 0, "Color Attributes", "Export color attributes");
  api_def_bool(
      fn, "apply_subdiv", 1, "Subsurfs as meshes", "Export subdivision surfaces as meshes");
  api_def_bool(fn, "flatten", 0, "Flatten hierarchy", "Flatten hierarchy");
  api_def_bool(fn,
                  "visible_objects_only",
                  0,
                  "Visible layers only",
                  "Export only objects in visible layers");
  api_def_bool(fn, "face_sets", 0, "Facesets", "Export face sets");
  api_def_bool(fn,
               "subdiv_schema",
               0,
               "Use Alembic subdivision Schema",
               "Use Alembic subdivision Schema");
  api_def_bool(
      fn, "export_hair", 1, "Export Hair", "Exports hair particle systems as animated curves");
  api_def_bool(
      fn, "export_particles", 1, "Export Particles", "Exports non-hair particle systems");
  api_def_bool(
      fn, "packuv", 0, "Export with packed UV islands", "Export with packed UV islands");
  api_def_float(
      fn,
      "scale",
      1.0f,
      0.0001f,
      1000.0f,
      "Scale",
      "Value by which to enlarge or shrink the objects with respect to the world's origin",
      0.0001f,
      1000.0f);
  api_def_bool(
      fn, "triangulate", 0, "Triangulate", "Export polygons (quads and n-gons) as triangles");
  api_def_enum(fn,
               "quad_method",
               api_enum_mod_triangulate_quad_method_items,
               0,
               "Quad Method",
               "Method for splitting the quads into triangles");
  api_def_enum(fn,
               "ngon_method",
               api_enum_mod_triangulate_ngon_method_items,
               0,
               "N-gon Method",
               "Method for splitting the n-gons into triangles");

  api_def_fn_flag(fb, FN_USE_CXT);
#  endif
}

void api_scene_render(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "frame_path", "api_SceneRender_get_frame_path
  api_def_fn_flag(fn, FN_USE_MAIN);
  api_def_fn_ui_description(
      fn, "Return the absolute path to the filename to be written for a given frame");
  api_def_int(fn,
              "frame",
              INT_MIN,
              INT_MIN,
              INT_MAX,
              "",
              "Frame number to use, if unset the current frame will be used",
              MINAFRAME,
              MAXFRAME);
  api_def_bool(fn, "preview", 0, "Preview", "Use preview range");
  api_def_string_file_path(fn,
                           "view",
                           NULL,
                           FILE_MAX,
                           "View",
                           "The name of the view to use to replace the \"%\" chars");
  parm = api_def_string_file_path(fn,
                                  "filepath",
                                  NULL,
                                  FILE_MAX,
                                  "File Path",
                                  "The resulting filepath from the scenes render settings");
  api_def_param_flags(parm, PROP_THICK_WRAP, 0); /* needed for string return value */
  api_def_fn_output(fn, parm);
}

#endif
