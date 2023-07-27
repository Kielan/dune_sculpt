#include <stdlib.h>

#include "types_node.h"
#include "type_object.h"
#include "types_scene.h"

#include "lib_path_util.h"
#include "lib_utildefines.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#include "graph.h"

#include "dune_image.h"
#include "dune_scene.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "render_engine.h"
#include "render_pipeline.h"

#include "ed_render.h"

/* Deprecated, only provided for API compatibility. */
const EnumPropItem api_enum_render_pass_type_items[] = {
    {SCE_PASS_COMBINED, "COMBINED", 0, "Combined", ""},
    {SCE_PASS_Z, "Z", 0, "Z", ""},
    {SCE_PASS_SHADOW, "SHADOW", 0, "Shadow", ""},
    {SCE_PASS_AO, "AO", 0, "Ambient Occlusion", ""},
    {SCE_PASS_POSITION, "POSITION", 0, "Position", ""},
    {SCE_PASS_NORMAL, "NORMAL", 0, "Normal", ""},
    {SCE_PASS_VECTOR, "VECTOR", 0, "Vector", ""},
    {SCE_PASS_INDEXOB, "OBJECT_INDEX", 0, "Object Index", ""},
    {SCE_PASS_UV, "UV", 0, "UV", ""},
    {SCE_PASS_MIST, "MIST", 0, "Mist", ""},
    {SCE_PASS_EMIT, "EMIT", 0, "Emit", ""},
    {SCE_PASS_ENVIRONMENT, "ENVIRONMENT", 0, "Environment", ""},
    {SCE_PASS_INDEXMA, "MATERIAL_INDEX", 0, "Material Index", ""},
    {SCE_PASS_DIFFUSE_DIRECT, "DIFFUSE_DIRECT", 0, "Diffuse Direct", ""},
    {SCE_PASS_DIFFUSE_INDIRECT, "DIFFUSE_INDIRECT", 0, "Diffuse Indirect", ""},
    {SCE_PASS_DIFFUSE_COLOR, "DIFFUSE_COLOR", 0, "Diffuse Color", ""},
    {SCE_PASS_GLOSSY_DIRECT, "GLOSSY_DIRECT", 0, "Glossy Direct", ""},
    {SCE_PASS_GLOSSY_INDIRECT, "GLOSSY_INDIRECT", 0, "Glossy Indirect", ""},
    {SCE_PASS_GLOSSY_COLOR, "GLOSSY_COLOR", 0, "Glossy Color", ""},
    {SCE_PASS_TRANSM_DIRECT, "TRANSMISSION_DIRECT", 0, "Transmission Direct", ""},
    {SCE_PASS_TRANSM_INDIRECT, "TRANSMISSION_INDIRECT", 0, "Transmission Indirect", ""},
    {SCE_PASS_TRANSM_COLOR, "TRANSMISSION_COLOR", 0, "Transmission Color", ""},
    {SCE_PASS_SUBSURFACE_DIRECT, "SUBSURFACE_DIRECT", 0, "Subsurface Direct", ""},
    {SCE_PASS_SUBSURFACE_INDIRECT, "SUBSURFACE_INDIRECT", 0, "Subsurface Indirect", ""},
    {SCE_PASS_SUBSURFACE_COLOR, "SUBSURFACE_COLOR", 0, "Subsurface Color", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_pass_type_items[] = {
    {SCE_PASS_COMBINED, "COMBINED", 0, "Combined", ""},
    {SCE_PASS_AO, "AO", 0, "Ambient Occlusion", ""},
    {SCE_PASS_SHADOW, "SHADOW", 0, "Shadow", ""},
    {SCE_PASS_POSITION, "POSITION", 0, "Position", ""},
    {SCE_PASS_NORMAL, "NORMAL", 0, "Normal", ""},
    {SCE_PASS_UV, "UV", 0, "UV", ""},
    {SCE_PASS_ROUGHNESS, "ROUGHNESS", 0, "ROUGHNESS", ""},
    {SCE_PASS_EMIT, "EMIT", 0, "Emit", ""},
    {SCE_PASS_ENVIRONMENT, "ENVIRONMENT", 0, "Environment", ""},
    {SCE_PASS_DIFFUSE_COLOR, "DIFFUSE", 0, "Diffuse", ""},
    {SCE_PASS_GLOSSY_COLOR, "GLOSSY", 0, "Glossy", ""},
    {SCE_PASS_TRANSM_COLOR, "TRANSMISSION", 0, "Transmission", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "api_access.h"

#  include "dune_appdir.h"
#  include "dune_context.h"
#  include "dune_report.h"

#  include "gpu_capabilities.h"
#  include "gpu_shader.h"
#  include "imb_colormanagement.h"

#  include "graph_query.h"

/* RenderEngine Callbacks */
static void engine_tag_redraw(RenderEngine *engine)
{
  engine->flag |= RE_ENGINE_DO_DRAW;
}

static void engine_tag_update(RenderEngine *engine)
{
  engine->flag |= RE_ENGINE_DO_UPDATE;
}

static bool engine_support_display_space_shader(RenderEngine *UNUSED(engine), Scene *scene)
{
  return imb_colormanagement_support_glsl_draw(&scene->view_settings);
}

static int engine_get_preview_pixel_size(RenderEngine *UNUSED(engine), Scene *scene)
{
  return dune_render_preview_pixel_size(&scene->r);
}

static void engine_bind_display_space_shader(RenderEngine *UNUSED(engine), Scene *UNUSED(scene))
{
  GPUShader *shader = gpu_shader_get_builtin_shader(GPU_SHADER_2D_IMAGE);
  gpu_shader_bind(shader);

  int img_loc = gpu_shader_get_uniform(shader, "image");

  gpu_shader_uniform_int(shader, img_loc, 0);
}

static void engine_unbind_display_space_shader(RenderEngine *UNUSED(engine))
{
  gpu_shader_unbind();
}

static void engine_update(RenderEngine *engine, Main *main, Graph *graph)
{
  extern ApiFn api_RenderEngine_update_func;
  ApiPtr ptr;
  ApiParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fb = &api_RenderEngine_update_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "data", main);
  apu_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_render(RenderEngine *engine, Graph *graph)
{
  extern ApiFn api_RenderEngine_render_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_render_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_render_frame_finish(RenderEngine *engine)
{
  extern ApiFn api_RenderEngine_render_frame_finish_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_render_frame_finish_fn;

  api_param_list_create(&list, &ptr, fn);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_draw(RenderEngine *engine, const struct Cxt *cxt, Graph *graph)
{
  extern ApiFn api_RenderEngine_draw_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_draw_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "cxt", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_bake(RenderEngine *engine,
                        struct Graph *graph,
                        struct Object *object,
                        const int pass_type,
                        const int pass_filter,
                        const int width,
                        const int height)
{
  extern ApiFn api_RenderEngine_bake_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_bake_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "depsgraph", &graph);
  api_param_set_lookup(&list, "object", &object);
  api_param_set_lookup(&list, "pass_type", &pass_type);
  api_param_set_lookup(&list, "pass_filter", &pass_filter);
  api_param_set_lookup(&list, "width", &width);
  api_param_set_lookup(&list, "height", &height);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_view_update(RenderEngine *engine,
                               const struct Cxt *cxt,
                               Graph *graph)
{
  extern ApiFn api_RenderEngine_view_update_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_view_update_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_view_draw(RenderEngine *engine,
                             const struct Cxt *cxt,
                             Graph *graph)
{
  extern ApiFn api_RenderEngine_view_draw_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_view_draw_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "cxt", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_update_script_node(RenderEngine *engine,
                                      struct NodeTree *ntree,
                                      struct Node *node)
{
  extern ApiFn api_RenderEngine_update_script_node_fn;
  ApiPtr ptr, nodeptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  api_ptr_create((Id *)ntree, &ApiNode, node, &nodeptr);
  fn = &api_RenderEngine_update_script_node_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "node", &nodeptr);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_update_render_passes(RenderEngine *engine,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer)
{
  extern ApiFn api_RenderEngine_update_render_passes_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->rna_ext.srna, engine, &ptr);
  fn = &api_RenderEngine_update_render_passes_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "scene", &scene)
  apu_param_set_lookup(&list, "renderlayer", &view_layer);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

/* RenderEngine registration */
static void api_RenderEngine_unregister(Main *main, ApiStruct *type)
{
  RenderEngineType *et = api_struct_dune_type_get(type);

  if (!et) {
    return;
  }

  /* Stop all renders in case we were using this one. */
  ed_render_engine_changed(main, false);
  render_FreeAllPersistentData();

  api_struct_free_extension(type, &et->api_ext);
  api_struct_free(&DUNE_API, type);
  lib_freelinkn(&R_engines, et);
}

static ApiStruct *api_RenderEngine_register(Main *main,
                                            ReportList *reports,
                                            void *data,
                                            const char *id,
                                            StructValidateFn validate,
                                            StructCbFn call,
                                            StructFreeFn free)
{
  RenderEngineType *et, dummyet = {NULL};
  RenderEngine dummyengine = {NULL};
  ApiPtr dummyptr;
  int have_fn[9];

  /* setup dummy engine & engine type to store static properties in */
  dummyengine.type = &dummyet;
  dummyet.flag |= RE_USE_SHADING_NODES_CUSTOM;
  api_ptr_create(NULL, &ApiRenderEngine, &dummyengine, &dummyptr);

  /* validate the python class */
  if (validate(&dummyptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummyet.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Registering render engine class: '%s' is too long, maximum length is %d",
                id,
                (int)sizeof(dummyet.idname));
    return NULL;
  }

  /* check if we have registered this engine type before, and remove it */
  for (et = R_engines.first; et; et = et->next) {
    if (STREQ(et->idname, dummyet.idname)) {
      if (et->api_ext.sapi) {
        api_RenderEngine_unregister(main, et->api_ext.sapi);
      }
      break;
    }
  }

  /* create a new engine type */
  et = mem_mallocn(sizeof(RenderEngineType), "python render engine");
  memcpy(et, &dummyet, sizeof(dummyet));

  et->api_ext.sapi = api_def_struct_ptr(&DUNE_API, et->idname, &ApiRenderEngine);
  et->api_ext.data = data;
  et->api_ext.call = call;
  et->api_ext.free = free;
  api_struct_dune_type_set(et->api_ext.sapi, et);

  et->update = (have_fn[0]) ? engine_update : NULL;
  et->render = (have_fn[1]) ? engine_render : NULL;
  et->render_frame_finish = (have_fn[2]) ? engine_render_frame_finish : NULL;
  et->draw = (have_fn[3]) ? engine_draw : NULL;
  et->bake = (have_fn[4]) ? engine_bake : NULL;
  et->view_update = (have_fn[5]) ? engine_view_update : NULL;
  et->view_draw = (have_fn[6]) ? engine_view_draw : NULL;
  et->update_script_node = (have_fn[7]) ? engine_update_script_node : NULL;
  et->update_render_passes = (have_fn[8]) ? engine_update_render_passes : NULL;

  render_engines_register(et);

  return et->api_ext.sapi;
}

static void **api_RenderEngine_instance(ApiPtr *ptr)
{
  RenderEngine *engine = ptr->data;
  return &engine->py_instance;
}

static ApiStruct *api_RenderEngine_refine(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;
  return (engine->type && engine->type->api_ext.sapi) ? engine->type->api_ext.sapi :
                                                        &ApiRenderEngine;
}

static void api_RenderEngine_tempdir_get(ApiPtr *UNUSED(ptr), char *value)
{
  lib_strncpy(value, dune_tempdir_session(), FILE_MAX);
}

static int api_RenderEngine_tempdir_length(ApiPtr *UNUSED(ptr))
{
  return strlen(dune_tempdir_session());
}

static ApiPtr api_RenderEngine_render_get(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;

  if (engine->re) {
    RenderData *r = render_engine_get_render_data(engine->re);

    return api_ptr_inherit_refine(ptr, &ApiRenderSettings, r);
  } else {
    return api_ptr_inherit_refine(ptr, &ApiRenderSettings, NULL);
  }
}

static ApiPtr api_RenderEngine_camera_override_get(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;
  /* TODO: Shouldn't engine point to an evaluated datablocks already? */
  if (engine->re) {
    Object *cam = render_GetCamera(engine->re);
    Object *cam_eval = graph_get_evaluated_object(engine->graph, cam);
    return api_ptr_inherit_refine(ptr, &ApiObject, cam_eval);
  }
  else {
    return api_ptr_inherit_refine(ptr, &ApiObject, engine->camera_override);
  }
}

static void api_RenderEngine_engine_frame_set(RenderEngine *engine, int frame, float subframe)
{
#  ifdef WITH_PYTHON
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  render_engine_frame_set(engine, frame, subframe);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif
}

static void api_RenderResult_views_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  RenderResult *rr = (RenderResult *)ptr->data;
  api_iter_list_begin(iter, &rr->views, NULL);
}

static void api_RenderResult_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  RenderResult *rr = (RenderResult *)ptr->data;
  api_iter_list_begin(iter, &rr->layers, NULL);
}

static void api_RenderResult_stamp_data_add_field(RenderResult *rr,
                                                  const char *field,
                                                  const char *value)
{
  dune_render_result_stamp_data(rr, field, value);
}

static void api_RenderLayer_passes_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  RenderLayer *rl = (RenderLayer *)ptr->data;
  api_iter_list_begin(iter, &rl->passes, NULL);
}

static int api_RenderPass_rect_get_length(ApiPtr *ptr, int length[API_MAX_ARRAY_DIMENSION])
{
  RenderPass *rpass = (RenderPass *)ptr->data;

  length[0] = rpass->rectx * rpass->recty;
  length[1] = rpass->channels;

  return length[0] * length[1];
}

static void api_RenderPass_rect_get(ApiPtr *ptr, float *values)
{
  RenderPass *rpass = (RenderPass *)ptr->data;
  memcpy(values, rpass->rect, sizeof(float) * rpass->rectx * rpass->recty * rpass->channels);
}

void api_RenderPass_rect_set(ApiPtr *ptr, const float *values)
{
  RenderPass *rpass = (RenderPass *)ptr->data;
  memcpy(rpass->rect, values, sizeof(float) * rpass->rectx * rpass->recty * rpass->channels);
}

static RenderPass *api_RenderPass_find_by_type(RenderLayer *rl, int passtype, const char *view)
{
  return render_pass_find_by_type(rl, passtype, view);
}

static RenderPass *api_RenderPass_find_by_name(RenderLayer *rl, const char *name, const char *view)
{
  return render_pass_find_by_name(rl, name, view);
}

#else /* API_RUNTIME */

static void api_def_render_engine(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem render_pass_type_items[] = {
      {SOCK_FLOAT, "VALUE", 0, "Value", ""},
      {SOCK_VECTOR, "VECTOR", 0, "Vector", ""},
      {SOCK_RGBA, "COLOR", 0, "Color", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "RenderEngine", NUL
  api_def_struct_stype(sapi, "RenderEngine");
  api_def_struct_ui_text(sapi, "Render Engine", "Render engine");
  api_def_struct_refine_fn(sapi, "api_RenderEngine_refine");
  api_def_struct_register_fns(sapi,
                              "api_RenderEngine_register",
                              "api_RenderEngine_unregister",
                              "api_RenderEngine_instance");

  /* final render callbacks */
  fn = api_def_fn(sapi, "update", NULL);
  api_def_fn_ui_description(fn, "Export scene data for render");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  api_def_ptr(fn, "data", "DuneData", "", "");
  api_def_ptr(fn, "graph", "Graph", "", "");

  func = api_def_fn(sapi, "render", NULL);
  api_def_fn_ui_description(fb, "Render scene into an image");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "graph", "Graph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "render_frame_finish", NULL);
  api_def_fn_ui_description(
      fn, "Perform finishing operations after all view layers in a frame were rendered");
  apo_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);

  fn = api_def_fn(sapi, "draw", NULL);
  api_def_fn_ui_description(fn, "Draw render image");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "graph", "Graph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "bake", NULL);
  api_def_fn_ui_description(fn, "Bake passes");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "graph", "Graph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "object", "Object", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fb, "pass_type", api_enum_bake_pass_type_items, 0, "Pass", "Pass to bake");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "pass_filter",
                     0,
                     0,
                     INT_MAX,
                     "Pass Filter",
                     "Filter to combined, diffuse, glossy and transmission passes",
                     0,
                     INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "width", 0, 0, INT_MAX, "Width", "Image width", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "height", 0, 0, INT_MAX, "Height", "Image height", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* viewport render callbacks */
  fn = api_def_fn(sapi, "view_update", NULL);
  api_def_fn_ui_description(fn, "Update on data changes for viewport render");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "depsgraph", "Depsgraph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "view_draw", NULL);
  api_def_fn_ui_description(fn, "Draw viewport render");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "cxt", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = apu_def_ptr(fn, "graph", "Graph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* shader script callbacks */
  fn = api_def_fn(sapi, "update_script_node", NULL);
  api_def_fn_ui_description(fn, "Compile shader script node");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "node", "Node", "", "");
  api_def_param_flags(parm, 0, PARM_RNAPTR);

  fn = api_def_fn(sapi, "update_render_passes", NULL);
  api_def_fn_ui_description(fn, "Update the render passes that will be generated");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  parm = api_def_ptr(fn, "renderlayer", "ViewLayer", "", "");

  /* tag for redraw */
  fn = api_def_fn(sapi, "tag_redraw", "engine_tag_redraw");
  api_def_fn_ui_description(fn, "Request redraw for viewport rendering");

  /* tag for update */
  fn = api_def_fn(sapi, "tag_update", "engine_tag_update");
  api_def_fn_ui_description(fn, "Request update call for viewport rendering");

  fn = api_def_fn(sapi, "begin_result", "render_engine_begin_result");
  api_def_fn_ui_description(
      fn, "Create render result to write linear floating-point render layers and passes");
  parm = api_def_int(fn, "x", 0, 0, INT_MAX, "X", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "y", 0, 0, INT_MAX, "Y", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "w", 0, 0, INT_MAX, "Width", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "h", 0, 0, INT_MAX, "Height", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(
      fn, "layer", NULL, 0, "Layer", "Single layer to get render result for"); /* NULL ok here */
  api_def_string(
      fn, "view", NULL, 0, "View", "Single view to get render result for"); /* NULL ok here */
  parm = api_def_ptr(fn, "result", "RenderResult", "Result", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "update_result", "RE_engine_update_result");
  api_def_fn_ui_description(
      fn, "Signal that pixels have been updated and can be redrawn in the user interface");
  parm = api_def_pr(fn, "result", "RenderResult", "Result", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "end_result", "RE_engine_end_result");
  api_def_fn_ui_description(fn,
                            "All pixels in the render result have been set and are final");
  parm = api_def_ptr(fn, "result", "RenderResult", "Result", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(
      fn, "cancel", 0, "Cancel", "Don't mark tile as done, don't merge results unless forced");
  api_def_bool(fn, "highlight", 0, "Highlight", "Don't mark tile as done yet");
  api_def_bool(
      fn, "do_merge_results", 0, "Merge Results", "Merge results even if cancel=true");

  fn = api_def_fn(sapi, "add_pass", "RE_engine_add_pass");
  api_def_fn_ui_description(fn, "Add a pass to the render layer");
  parm = api_def_string(
      fn, "name", NULL, 0, "Name", "Name of the Pass, without view or channel tag");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "channels", 0, 0, INT_MAX, "Channels", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(
      fn, "chan_id", NULL, 0, "Channel IDs", "Channel names, one character per channel");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(
      fn, "layer", NULL, 0, "Layer", "Single layer to add render pass to"); /* NULL ok here */

  fn = api_def_fn(sapi, "get_result", "RE_engine_get_result");
  api_def_fn_ui_description(fn, "Get final result for non-pixel operations");
  parm = api_def_ptr(fn, "result", "RenderResult", "Result", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "test_break", "RE_engine_test_break");
  api_def_fn_ui_description(fn,
                            "Test if the render operation should been canceled, this is a "
                            "fast call that should be used regularly for responsiveness");
  parm = api_def_bool(fn, "do_break", 0, "Break", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "pass_by_index_get", "RE_engine_pass_by_index_get");
  parm = api_def_string(fn, "layer", NULL, 0, "Layer", "Name of render layer to get pass for");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Index of pass to get", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "render_pass", "RenderPass", "Index", "Index of pass to get");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "active_view_get", "RE_engine_active_view_get");
  parm = api_def_string(fn, "view", NULL, 0, "View", "Single view active");
  api_def_fn_return(fb, parm);

  fn = api_def_fn(sapi, "active_view_set", "RE_engine_active_view_set");
  parm = api_def_string(
      fn, "view", NULL, 0, "View", "Single view to set as active"); /* NULL ok here */
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "camera_shift_x", "RE_engine_get_camera_shift_x");
  parm = api_def_ptr(fn, "camera", "Object", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "use_spherical_stereo", 0, "Spherical Stereo", "");
  parm = api_def_float(fn, "shift_x", 0.0f, 0.0f, FLT_MAX, "Shift X", "", 0.0f, FLT_MAX);
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "camera_model_matrix", "render_engine_get_camera_model_matrix");
  parm = api_def_ptr(fn, "camera", "Object", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "use_spherical_stereo", 0, "Spherical Stereo", "");
  parm = api_def_float_matrix(fn,
                              "r_model_matrix",
                              4,
                              4,
                              NULL,
                              0.0f,
                              0.0f,
                              "Model Matrix",
                              "Normalized camera model matrix",
                              0.0f,
                              0.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_output(fn, parm);

  fn = api_def_fn(sapi, "use_spherical_stereo", "render_engine_get_spherical_stereo");
  parm = api_def_ptr(fn, "camera", "Object", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "use_spherical_stereo", 0, "Spherical Stereo", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "update_stats", "render_engine_update_stats");
  api_def_fn_ui_description(fn, "Update and signal to redraw render status text");
  parm = api_def_string(fn, "stats", NULL, 0, "Stats", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "info", NULL, 0, "Info", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "frame_set", "api_RenderEngine_engine_frame_set");
  api_def_fn_ui_description(fn, "Evaluate scene at a different frame (for motion blur)");
  parm = api_def_int(fn, "frame", 0, INT_MIN, INT_MAX, "Frame", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(fn, "subframe", 0.0f, 0.0f, 1.0f, "Subframe", "", 0.0f, 1.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "update_progress", "RE_engine_update_progress");
  api_def_fn_ui_description(fn, "Update progress percentage of render");
  parm = api_def_float(
      fn, "progress", 0, 0.0f, 1.0f, "", "Percentage of render that's done", 0.0f, 1.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "update_memory_stats", "RE_engine_update_memory_stats");
  api_def_fn_ui_description(fn, "Update memory usage statistics");
  api_def_float(fn,
                "memory_used",
                0,
                0.0f,
                FLT_MAX,
                "",
                "Current memory usage in megabytes",
                0.0f,
                FLT_MAX);
  api_def_float(
      fn, "memory_peak", 0, 0.0f, FLT_MAX, "", "Peak memory usage in megabytes", 0.0f, FLT_MAX);

  fn = api_def_fn(sapi, "report", "RE_engine_report");
  api_def_fn_ui_description(fn, "Report info, warning or error messages");
  parm = api_def_enum_flag(fn, "type", api_enum_wm_report_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "message", NULL, 0, "Report Message", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "error_set", "render_engine_set_error_message");
  api_def_fn_ui_description(fn,
                            "Set error message displaying after the render is finished");
  parm = api_def_string(fn, "message", NULL, 0, "Report Message", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = apu_def_fn(sapi, "bind_display_space_shader", "engine_bind_display_space_shader");
  api_def_fn_ui_description(fn,
                            "Bind GLSL fragment shader that converts linear colors to "
                            "display space colors using scene color management settings");
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(
      srna, "unbind_display_space_shader", "engine_unbind_display_space_shader");
  api_def_fn_ui_description(
      fn, "Unbind GLSL display space shader, must always be called after binding the shader");

  fn = api_def_fn(
      sapi, "support_display_space_shader", "engine_support_display_space_shader");
  api_def_fn_ui_description(fn,
                            "Test if GLSL display space shader is supported for the "
                            "combination of graphics card and scene settings");
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "supported", 0, "Supported", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "get_preview_pixel_size", "engine_get_preview_pixel_size");
  api_def_fn_ui_description(fn,
                            "Get the pixel size that should be used for preview rendering");
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "pixel_size", 0, 1, 8, "Pixel Size", "", 1, 8);
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "free_dune_memory", "render_engine_free_dune_memory");
  api_def_fn_ui_description(fn, "Free Dune side memory of render engine");

  fn = api_def_fn(sapi, "tile_highlight_set", "RE_engine_tile_highlight_set");
  api_def_fn_ui_description(func, "Set highlighted state of the given tile");
  parm = api_def_int(fn, "x", 0, 0, INT_MAX, "X", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fb, "y", 0, 0, INT_MAX, "Y", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "width", 0, 0, INT_MAX, "Width", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "height", 0, 0, INT_MAX, "Height", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "highlight", 0, "Highlight", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "tile_highlight_clear_all", "render_engine_tile_highlight_clear_all");
  api_def_fn_ui_description(fn, "Clear highlight from all tiles");

  api_define_verify_stype(0);

  prop = api_def_prop(sapi, "is_animation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sdtype(prop, NULL, "flag", RE_ENGINE_ANIMATION);

  prop = api_def_prop(sapi, "is_preview", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RE_ENGINE_PREVIEW);

  prop = api_def_prop(sapi, "camera_override", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_fns(prop, "api_RenderEngine_camera_override_get", NULL, NULL, NULL);
  api_def_prop_struct_type(prop, "Object");

  prop = api_def_prop(sapi, "layer_override", PROP_BOOLEAN, PROP_LAYER_MEMBER);
  api_def_prop_bool_stype(prop, NULL, "layer_override", 1);
  api_def_prop_array(prop, 20);

  prop = api_def_prop(sapi, "resolution_x", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "resolution_x");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "resolution_y", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "resolution_y");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "temp_dir", PROP_STRING, PROP_NONE);
  api_def_fn_ui_description(fn, "The temp directory used by Dune");
  api_def_prop_string_fns(
      prop, "api_RenderEngine_tempdir_get", "rna_RenderEngine_tempdir_length", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  /* Render Data */
  prop = api_def_prop(sapi, "render", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "RenderSettings");
  api_def_prop_ptr_fns(prop, "api_RenderEngine_render_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Render Data", "");

  prop = api_def_prop(sapi, "use_highlight_tiles", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RE_ENGINE_HIGHLIGHT_TILES);

  fn = api_def_fn(sapi, "register_pass", "RE_engine_register_pass");
  api_def_fn_ui_description(
      fn, "Register a render pass that will be part of the render with the current settings");
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "view_layer", "ViewLayer", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "channels", 1, 1, 8, "Channels", "", 1, 4);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "chanid", NULL, 8, "Channel IDs", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "type", render_pass_type_items, SOCK_FLOAT, "Type", "");
  api_def_prop_enum_native_type(parm, "eNodeSocketDatatype");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* registration */
  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->idname");
  api_def_prop_flag(prop, PROP_REGISTER);

  prop = api_def_prop(sapi, "bl_label", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->name");
  api_def_prop_flag(prop, PROP_REGISTER);

  prop = api_def_prop(sapi, "bl_use_preview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_PREVIEW);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop,
      "Use Preview Render",
      "Render engine supports being used for rendering previews of materials, lights and worlds");

  prop = api_def_prop(sapi, "bl_use_postprocess", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "type->flag", RE_USE_POSTPROCESS);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(prop, "Use Post Processing", "Apply compositing on render results");

  prop = api_def_prop(sapi, "bl_use_eevee_viewport", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_EEVEE_VIEWPORT);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop, "Use Eevee Viewport", "Uses Eevee for viewport shading in LookDev shading mode");

  prop = api_def_prop(sapi, "bl_use_custom_freestyle", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_CUSTOM_FREESTYLE);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop,
      "Use Custom Freestyle",
      "Handles freestyle rendering on its own, instead of delegating it to EEVEE");

  prop = api_def_prop(sapi, "bl_use_image_save", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "type->flag", RE_USE_NO_IMAGE_SAVE);
  api_def_prop_bool_default(prop, true);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop,
      "Use Image Save",
      "Save images/movie to disk while rendering an animation. "
      "Disabling image saving is only supported when bl_use_postprocess is also disabled");

  prop = api_def_prop(sapi, "bl_use_gpu_context", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_GPU_CONTEXT);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop,
      "Use GPU Context",
      "Enable OpenGL context for the render method, for engines that render using OpenGL");

  prop = api_def_prop(sapi, "bl_use_shading_nodes_custom", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_SHADING_NODES_CUSTOM);
  api_def_prop_bool_default(prop, true);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(prop,
                       "Use Custom Shading Nodes",
                       "Don't expose Cycles and Eevee shading nodes in the node editor user "
                       "interface, so own nodes can be used instead");

  prop = api_def_prop(sapi, "bl_use_spherical_stereo", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_SPHERICAL_STEREO);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(prop, "Use Spherical Stereo", "Support spherical stereo camera models");

  prop = api_def_prop(sapi, "bl_use_stereo_viewport", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_STEREO_VIEWPORT);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(prop, "Use Stereo Viewport", "Support rendering stereo 3D viewport");

  prop = api_def_prop(sapi, "bl_use_alembic_procedural", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "type->flag", RE_USE_ALEMBIC_PROCEDURAL);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop, "Use Alembic Procedural", "Support loading Alembic data at render time");

  api_define_verify_stype(1);
}

static void api_def_render_result(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "RenderResult", NULL);
  api_def_struct_ui_text(
      sapi, "Render Result", "Result of rendering, including all layers and passes");

  fn = api_def_fn(sapi, "load_from_file", "RE_result_load_from_file");
  api_def_fn_ui_description(fn,
                            "Copies the pixels of this render result from an image file");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string_file_name(
      func,
      "filename",
      NULL,
      FILE_MAX,
      "File Name",
      "Filename to load into this render tile, must be no smaller than "
      "the render result");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "stamp_data_add_field", "rna_RenderResult_stamp_data_add_field");
  api_def_fn_ui_description(fn, "Add engine-specific stamp data to the result");
  parm = api_def_string(fn, "field", NULL, 1024, "Field", "Name of the stamp field to add");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "value", NULL, 0, "Value", "Value of the stamp data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  api_define_verify_stype(0);

  prop = api_def_prop(sapi, "resolution_x", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "rectx");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "resolution_y", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "recty");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "layers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "RenderLayer");
  api_def_prop_collection_fns(prop,
                              "api_RenderResult_layers_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);

  prop = api_def_prop(sapi, "views", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "RenderView");
  api_def_prop_collection_fns(prop,
                              "api_RenderResult_views_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);

  api_define_verify_stype(1);
}

static void api_def_render_view(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "RenderView", NULL);
  api_def_struct_ui_text(sapi, "Render View", "");

  api_define_verify_stype(0);

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "name");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_struct_name_prop(sapi, prop);

  api_define_verify_stype(1);
}

static void api_def_render_passes(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "RenderPasses");
  sapi = api_def_struct(dapi, "RenderPasses", NULL);
  api_def_struct_stype(sapi, "RenderLayer");
  api_def_struct_ui_text(sapi, "Render Passes", "Collection of render passes");

  fn = api_def_fn(sapi, "find_by_type", "api_RenderPass_find_by_type");
  api_def_function_ui_description(func, "Get the render pass for a given type and view");
  parm = api_def_enum(
      fn, "pass_type", api_enum_render_pass_type_items, SCE_PASS_COMBINED, "Pass", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(
      fn, "view", NULL, 0, "View", "Render view to get pass from"); /* NULL ok here */
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "render_pass", "RenderPass", "", "The matching render pass");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "find_by_name", "api_RenderPass_find_by_name");
  api_def_fn_ui_description(fn, "Get the render pass for a given name and view");
  parm = api_def_string(fn, "name", RE_PASSNAME_COMBINED, 0, "Pass", "");
  apu_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(
      fn, "view", NULL, 0, "View", "Render view to get pass from"); /* NULL ok here */
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "render_pass", "RenderPass", "", "The matching render pass");
  api_def_fn_return(fn, parm);
}

static void api_def_render_layer(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "RenderLayer", NULL);
  api_def_struct_ui_text(sapi, "Render Layer", "");

  fn = api_def_fn(sapi, "load_from_file", "render_layer_load_from_file");
  api_def_fn_ui_description(fn,
                            "Copies the pixels of this renderlayer from an image file");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(
      fn,
      "filename",
      NULL,
      0,
      "Filename",
      "Filename to load into this render tile, must be no smaller than the renderlayer");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_int(fn,
              "x",
              0,
              0,
              INT_MAX,
              "Offset X",
              "Offset the position to copy from if the image is larger than the render layer",
              0,
              INT_MAX);
  api_def_int(fn,
              "y",
              0,
              0,
              INT_MAX,
              "Offset Y",
              "Offset the position to copy from if the image is larger than the render layer",
              0,
              INT_MAX);

  api_define_verify_sdna(0);

  api_def_view_layer_common(dapi, sapi, false);

  prop = api_def_prop(sapi, "passes", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "RenderPass");
  api_def_prop_collection_fns(prop,
                              "api_RenderLayer_passes_begin",
                              "api_iter_list_next",
                                    "api_iter_list_end",
                                    "api_iter_list_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  rna_def_render_passes(brna, prop);

  RNA_define_verify_sdna(1);
}

static void rna_def_render_pass(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "RenderPass", NULL);
  RNA_def_struct_ui_text(srna, "Render Pass", "");

  RNA_define_verify_sdna(0);

  prop = RNA_def_property(srna, "fullname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "fullname");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "name");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "channel_id", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "chan_id");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "channels", PROP_INT, PROP_NONE);
  RNA_def_prop_int_sdna(prop, NULL, "channels");
  RNA_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "rect", PROP_FLOAT, PROP_NONE);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_multi_array(prop, 2, NULL);
  api_def_prop_dynamic_array_funcs(prop, "rna_RenderPass_rect_get_length");
  api_def_prop_float_fns(prop, "rna_RenderPass_rect_get", "rna_RenderPass_rect_set", NULL);

  prop = api_def_prop(srna, "view_id", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "view_id");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  api_define_verify_sdna(1);
}

void api_def_render(BlenderRNA *brna)
{
  a_def_render_engine(brna);
  a_def_render_result(brna);
  a_def_render_view(brna);
  a_def_render_layer(brna);
  api_def_render_pass(brna);
}

#endif /* RNA_RUNTIME */
