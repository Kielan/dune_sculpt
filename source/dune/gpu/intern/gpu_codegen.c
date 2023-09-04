/* Convert material node-trees to GLSL. */

#include "mem_guardedalloc.h"

#include "types_customdata_types.h"
#include "types_image_types.h"

#include "lib_dunelib.h"
#include "lib_dynstr.h"
#include "lib_ghash.h"
#include "lib_hash_mm2a.h"
#include "lib_link_utils.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "PIL_time.h"

#include "dune_material.h"

#include "gpu_capabilities.h"
#include "gpu_material.h"
#include "gpu_shader.h"
#include "gpu_uniform_buffer.h"
#include "gpu_vertex_format.h"

#include "lib_sys_types.h" /* for intptr_t support */

#include "gpu_codegen.h"
#include "gpu_material_lib.h"
#include "gpu_node_graph.h"

#include <stdarg.h>
#include <string.h>

extern char datatoc_gpu_shader_codegen_lib_glsl[];
extern char datatoc_gpu_shader_common_obinfos_lib_glsl[];

/* GPUPass Cache */
/* Internal shader cache: This prevent the shader recompilation / stall when
 * using undo/redo AND also allows for GPUPass reuse if the Shader code is the
 * same for 2 different Materials. Unused GPUPasses are free by Garbage collection */

/* Only use one linked-list that contains the GPUPasses grouped by hash. */
static GPUPass *pass_cache = NULL;
static SpinLock pass_cache_spin;

static uint32_t gpu_pass_hash(const char *frag_gen, const char *defs, ListBase *attributes)
{
  lib_HashMurmur2A hm2a;
  lib_hash_mm2a_init(&hm2a, 0);
  lib_hash_mm2a_add(&hm2a, (uchar *)frag_gen, strlen(frag_gen));
  LISTBASE_FOREACH (GPUMaterialAttribute *, attr, attributes) {
    lib_hash_mm2a_add(&hm2a, (uchar *)attr->name, strlen(attr->name));
  }
  if (defs) {
    lib_hash_mm2a_add(&hm2a, (uchar *)defs, strlen(defs));
  }

  return lib_hash_mm2a_end(&hm2a);
}

/* Search by hash only. Return first pass with the same hash.
 * There is hash collision if (pass->next && pass->next->hash == hash) */
static GPUPass *gpu_pass_cache_lookup(uint32_t hash)
{
  lib_spin_lock(&pass_cache_spin);
  /* Could be optimized with a Lookup table. */
  for (GPUPass *pass = pass_cache; pass; pass = pass->next) {
    if (pass->hash == hash) {
      lib_spin_unlock(&pass_cache_spin);
      return pass;
    }
  }
  lib_spin_unlock(&pass_cache_spin);
  return NULL;
}

/* Check all possible passes with the same hash. */
static GPUPass *gpu_pass_cache_resolve_collision(GPUPass *pass,
                                                 const char *vert,
                                                 const char *geom,
                                                 const char *frag,
                                                 const char *defs,
                                                 uint32_t hash)
{
  lib_spin_lock(&pass_cache_spin);
  /* Collision, need to `strcmp` the whole shader. */
  for (; pass && (pass->hash == hash); pass = pass->next) {
    if ((defs != NULL) && (!STREQ(pass->defines, defs))) { /* Pass */
    }
    else if ((geom != NULL) && (!STREQ(pass->geometrycode, geom))) { /* Pass */
    }
    else if ((!STREQ(pass->fragmentcode, frag) == 0) && (STREQ(pass->vertexcode, vert))) {
      lib_spin_unlock(&pass_cache_spin);
      return pass;
    }
  }
  lib_spin_unlock(&pass_cache_spin);
  return NULL;
}

/* GLSL code generation */
static void codegen_convert_datatype(DynStr *ds, int from, int to, const char *tmp, int id)
{
  char name[1024];

  lib_snprintf(name, sizeof(name), "%s%d", tmp, id);

  if (from == to) {
    lib_dynstr_append(ds, name);
  }
  else if (to == GPU_FLOAT) {
    if (from == GPU_VEC4) {
      lib_dynstr_appendf(ds, "dot(%s.rgb, vec3(0.2126, 0.7152, 0.0722))", name);
    }
    else if (from == GPU_VEC3) {
      lib_dynstr_appendf(ds, "(%s.r + %s.g + %s.b) / 3.0", name, name, name);
    }
    else if (from == GPU_VEC2) {
      lib_dynstr_appendf(ds, "%s.r", name);
    }
  }
  else if (to == GPU_VEC2) {
    if (from == GPU_VEC4) {
      lib_dynstr_appendf(ds, "vec2((%s.r + %s.g + %s.b) / 3.0, %s.a)", name, name, name, name);
    }
    else if (from == GPU_VEC3) {
      lib_dynstr_appendf(ds, "vec2((%s.r + %s.g + %s.b) / 3.0, 1.0)", name, name, name);
    }
    else if (from == GPU_FLOAT) {
      lib_dynstr_appendf(ds, "vec2(%s, 1.0)", name);
    }
  }
  else if (to == GPU_VEC3) {
    if (from == GPU_VEC4) {
      lib_dynstr_appendf(ds, "%s.rgb", name);
    }
    else if (from == GPU_VEC2) {
      lib_dynstr_appendf(ds, "vec3(%s.r, %s.r, %s.r)", name, name, name);
    }
    else if (from == GPU_FLOAT) {
      lib_dynstr_appendf(ds, "vec3(%s, %s, %s)", name, name, name);
    }
  }
  else if (to == GPU_VEC4) {
    if (from == GPU_VEC3) {
      lib_dynstr_appendf(ds, "vec4(%s, 1.0)", name);
    }
    else if (from == GPU_VEC2) {
      lib_dynstr_appendf(ds, "vec4(%s.r, %s.r, %s.r, %s.g)", name, name, name, name);
    }
    else if (from == GPU_FLOAT) {
      lib_dynstr_appendf(ds, "vec4(%s, %s, %s, 1.0)", name, name, name);
    }
  }
  else if (to == GPU_CLOSURE) {
    if (from == GPU_VEC4) {
      lib_dynstr_appendf(ds, "closure_emission(%s.rgb)", name);
    }
    else if (from == GPU_VEC3) {
      lib_dynstr_appendf(ds, "closure_emission(%s.rgb)", name);
    }
    else if (from == GPU_VEC2) {
      lib_dynstr_appendf(ds, "closure_emission(%s.rrr)", name);
    }
    else if (from == GPU_FLOAT) {
      lib_dynstr_appendf(ds, "closure_emission(vec3(%s, %s, %s))", name, name, name);
    }
  }
  else {
    lib_dynstr_append(ds, name);
  }
}

static void codegen_print_datatype(DynStr *ds, const eGPUType type, float *data)
{
  int i;

  lib_dynstr_appendf(ds, "%s(", gpu_data_type_to_string(type));

  for (i = 0; i < type; i++) {
    lib_dynstr_appendf(ds, "%.12f", data[i]);
    if (i == type - 1) {
      lib_dynstr_append(ds, ")");
    }
    else {
      lib_dynstr_append(ds, ", ");
    }
  }
}

static const char *gpu_builtin_name(eGPUBuiltin builtin)
{
  if (builtin == GPU_VIEW_MATRIX) {
    return "unfviewmat";
  }
  if (builtin == GPU_OBJECT_MATRIX) {
    return "unfobmat";
  }
  if (builtin == GPU_INVERSE_VIEW_MATRIX) {
    return "unfinvviewmat";
  }
  if (builtin == GPU_INVERSE_OBJECT_MATRIX) {
    return "unfinvobmat";
  }
  if (builtin == GPU_LOC_TO_VIEW_MATRIX) {
    return "unflocaltoviewmat";
  }
  if (builtin == GPU_INVERSE_LOC_TO_VIEW_MATRIX) {
    return "unfinvlocaltoviewmat";
  }
  if (builtin == GPU_VIEW_POSITION) {
    return "varposition";
  }
  if (builtin == GPU_WORLD_NORMAL) {
    return "varwnormal";
  }
  if (builtin == GPU_VIEW_NORMAL) {
    return "varnormal";
  }
  if (builtin == GPU_OBJECT_COLOR) {
    return "unfobjectcolor";
  }
  if (builtin == GPU_AUTO_BUMPSCALE) {
    return "unfobautobumpscale";
  }
  if (builtin == GPU_CAMERA_TEXCO_FACTORS) {
    return "unfcameratexfactors";
  }
  if (builtin == GPU_PARTICLE_SCALAR_PROPS) {
    return "unfparticlescalarprops";
  }
  if (builtin == GPU_PARTICLE_LOCATION) {
    return "unfparticleco";
  }
  if (builtin == GPU_PARTICLE_VELOCITY) {
    return "unfparticlevel";
  }
  if (builtin == GPU_PARTICLE_ANG_VELOCITY) {
    return "unfparticleangvel";
  }
  if (builtin == GPU_OBJECT_INFO) {
    return "unfobjectinfo";
  }
  if (builtin == GPU_BARYCENTRIC_TEXCO) {
    return "unfbarycentrictex";
  }
  if (builtin == GPU_BARYCENTRIC_DIST) {
    return "unfbarycentricdist";
  }
  return "";
}

static void codegen_set_unique_ids(GPUNodeGraph *graph)
{
  int id = 1;

  LIST_FOREACH (GPUNode *, node, &graph->nodes) {
    LIST_FOREACH (GPUInput *, input, &node->inputs) {
      /* set id for unique names of uniform variables */
      input->id = id++;
    }

    LIST_FOREACH (GPUOutput *, output, &node->outputs) {
      /* set id for unique names of tmp variables storing output */
      output->id = id++;
    }
  }
}

/* It will create an UBO for GPUMaterial if there is any GPU_DYNAMIC_UBO */
static int codegen_process_uniforms_functions(GPUMaterial *material,
                                              DynStr *ds,
                                              GPUNodeGraph *graph)
{
  const char *name;
  int builtins = 0;
  ListBase ubo_inputs = {NULL, NULL};

  /* Textures */
  LIST_FOREACH (GPUMaterialTexture *, tex, &graph->textures) {
    if (tex->colorband) {
      lib_dynstr_appendf(ds, "uniform sampler1DArray %s;\n", tex->sampler_name);
    }
    else if (tex->tiled_mapping_name[0]) {
      lib_dynstr_appendf(ds, "uniform sampler2DArray %s;\n", tex->sampler_name);
      lib_dynstr_appendf(ds, "uniform sampler1DArray %s;\n", tex->tiled_mapping_name);
    }
    else {
      lib_dynstr_appendf(ds, "uniform sampler2D %s;\n", tex->sampler_name);
    }
  }

  /* Volume Grids */
  LIST_FOREACH (GPUMaterialVolumeGrid *, grid, &graph->volume_grids) {
    lib_dynstr_appendf(ds, "uniform sampler3D %s;\n", grid->sampler_name);
    lib_dynstr_appendf(ds, "uniform mat4 %s = mat4(0.0);\n", grid->transform_name);
  }

  /* Print other uniforms */
  LIST_FOREACH (GPUNode *, node, &graph->nodes) {
    LIST_FOREACH (GPUInput *, input, &node->inputs) {
      if (input->source == GPU_SOURCE_BUILTIN) {
        /* only define each builtin uniform/varying once */
        if (!(builtins & input->builtin)) {
          builtins |= input->builtin;
          name = gpu_builtin_name(input->builtin);

          if (lib_str_startswith(name, "unf")) {
            lib_dynstr_appendf(ds, "uniform %s %s;\n", gpu_data_type_to_string(input->type), name);
          }
          else {
            lib_dynstr_appendf(ds, "in %s %s;\n", gpu_data_type_to_string(input->type), name);
          }
        }
      }
      else if (input->source == GPU_SOURCE_STRUCT) {
        /* Add other struct here if needed. */
        lib_dynstr_appendf(ds, "Closure strct%d = CLOSURE_DEFAULT;\n", input->id);
      }
      else if (input->source == GPU_SOURCE_UNIFORM) {
        if (!input->link) {
          /* We handle the UBOuniforms separately. */
          lib_addtail(&ubo_inputs, lib_genericNodeN(input));
        }
      }
      else if (input->source == GPU_SOURCE_CONSTANT) {
        lib_dynstr_appendf(
            ds, "const %s cons%d = ", gpu_data_type_to_string(input->type), input->id);
        codegen_print_datatype(ds, input->type, input->vec);
        lib_dynstr_append(ds, ";\n");
      }
    }
  }

  /* Handle the UBO block separately. */
  if ((material != NULL) && !lib_listbase_is_empty(&ubo_inputs)) {
    gpu_material_uniform_buffer_create(material, &ubo_inputs);

    /* Inputs are sorted */
    lib_dynstr_appendf(ds, "\nlayout (std140) uniform %s {\n", GPU_UBO_BLOCK_NAME);

    LIST_FOREACH (LinkData *, link, &ubo_inputs) {
      GPUInput *input = (GPUInput *)(link->data);
      lib_dynstr_appendf(ds, "  %s unf%d;\n", gpu_data_type_to_string(input->type), input->id);
    }
    lib_dynstr_append(ds, "};\n");
    lib_freelistN(&ubo_inputs);
  }

  /* Generate the uniform attribute UBO if necessary. */
  if (!lib_listbase_is_empty(&graph->uniform_attrs.list)) {
    lib_dynstr_append(ds, "\nstruct UniformAttributes {\n");
    LIST_FOREACH (GPUUniformAttr *, attr, &graph->uniform_attrs.list) {
      lib_dynstr_appendf(ds, "  vec4 attr%d;\n", attr->id);
    }
    lib_dynstr_append(ds, "};\n");
    lib_dynstr_appendf(ds, "layout (std140) uniform %s {\n", GPU_ATTRIBUTE_UBO_BLOCK_NAME);
    lib_dynstr_append(ds, "  UniformAttributes uniform_attrs[DRW_RESOURCE_CHUNK_LEN];\n");
    lib_dynstr_append(ds, "};\n");
    lib_dynstr_append(ds, "#define GET_UNIFORM_ATTR(name) (uniform_attrs[resource_id].name)\n");
  }

  lib_dynstr_append(ds, "\n");

  return builtins;
}

static void codegen_declare_tmps(DynStr *ds, GPUNodeGraph *graph)
{
  LIST_FOREACH (GPUNode *, node, &graph->nodes) {
    /* declare temporary variables for node output storage */
    LIST_FOREACH (GPUOutput *, output, &node->outputs) {
      if (output->type == GPU_CLOSURE) {
        lib_dynstr_appendf(ds, "  Closure tmp%d;\n", output->id);
      }
      else {
        lib_dynstr_appendf(ds, "  %s tmp%d;\n", gpu_data_type_to_string(output->type), output->id);
      }
    }
  }
  lib_dynstr_append(ds, "\n");
}

static void codegen_call_fns(DynStr *ds, GPUNodeGraph *graph)
{
  LIST_FOREACH (GPUNode *, node, &graph->nodes) {
    lib_dynstr_appendf(ds, "  %s(", node->name);

    LIST_FOREACH (GPUInput *, input, &node->inputs) {
      if (input->source == GPU_SOURCE_TEX) {
        lib_dynstr_append(ds, input->texture->sampler_name);
      }
      else if (input->source == GPU_SOURCE_TEX_TILED_MAPPING) {
        lib_dynstr_append(ds, input->texture->tiled_mapping_name);
      }
      else if (input->source == GPU_SOURCE_VOLUME_GRID) {
        lib_dynstr_append(ds, input->volume_grid->sampler_name);
      }
      else if (input->source == GPU_SOURCE_VOLUME_GRID_TRANSFORM) {
        lib_dynstr_append(ds, input->volume_grid->transform_name);
      }
      else if (input->source == GPU_SOURCE_OUTPUT) {
        codegen_convert_datatype(
            ds, input->link->output->type, input->type, "tmp", input->link->output->id);
      }
      else if (input->source == GPU_SOURCE_BUILTIN) {
        /* TODO: get rid of that. */
        if (input->builtin == GPU_INVERSE_VIEW_MATRIX) {
          lib_dynstr_append(ds, "viewinv");
        }
        else if (input->builtin == GPU_VIEW_MATRIX) {
          lib_dynstr_append(ds, "viewmat");
        }
        else if (input->builtin == GPU_CAMERA_TEXCO_FACTORS) {
          lib_dynstr_append(ds, "camtexfac");
        }
        else if (input->builtin == GPU_LOC_TO_VIEW_MATRIX) {
          lib_dynstr_append(ds, "localtoviewmat");
        }
        else if (input->builtin == GPU_INVERSE_LOC_TO_VIEW_MATRIX) {
          lib_dynstr_append(ds, "invlocaltoviewmat");
        }
        else if (input->builtin == GPU_BARYCENTRIC_DIST) {
          lib_dynstr_append(ds, "barycentricDist");
        }
        else if (input->builtin == GPU_BARYCENTRIC_TEXCO) {
          lib_dynstr_append(ds, "barytexco");
        }
        else if (input->builtin == GPU_OBJECT_MATRIX) {
          lib_dynstr_append(ds, "objmat");
        }
        else if (input->builtin == GPU_OBJECT_INFO) {
          lib_dynstr_append(ds, "ObjectInfo");
        }
        else if (input->builtin == GPU_OBJECT_COLOR) {
          lib_dynstr_append(ds, "ObjectColor");
        }
        else if (input->builtin == GPU_INVERSE_OBJECT_MATRIX) {
          lib_dynstr_append(ds, "objinv");
        }
        else if (input->builtin == GPU_VIEW_POSITION) {
          lib_dynstr_append(ds, "viewposition");
        }
        else if (input->builtin == GPU_VIEW_NORMAL) {
          lib_dynstr_append(ds, "facingnormal");
        }
        else if (input->builtin == GPU_WORLD_NORMAL) {
          lib_dynstr_append(ds, "facingwnormal");
        }
        else {
          lib_dynstr_append(ds, gpu_builtin_name(input->builtin));
        }
      }
      else if (input->source == GPU_SOURCE_STRUCT) {
        lib_dynstr_appendf(ds, "strct%d", input->id);
      }
      else if (input->source == GPU_SOURCE_UNIFORM) {
        lib_dynstr_appendf(ds, "unf%d", input->id);
      }
      else if (input->source == GPU_SOURCE_CONSTANT) {
        lib_dynstr_appendf(ds, "cons%d", input->id);
      }
      else if (input->source == GPU_SOURCE_ATTR) {
        codegen_convert_datatype(ds, input->attr->gputype, input->type, "var", input->attr->id);
      }
      else if (input->source == GPU_SOURCE_UNIFORM_ATTR) {
        lib_dynstr_appendf(ds, "GET_UNIFORM_ATTR(attr%d)", input->uniform_attr->id);
      }

      lib_dynstr_append(ds, ", ");
    }

    LIST_FOREACH (GPUOutput *, output, &node->outputs) {
      lib_dynstr_appendf(ds, "tmp%d", output->id);
      if (output->next) {
        lib_dynstr_append(ds, ", ");
      }
    }

    lib_dynstr_append(ds, ");\n");
  }
}

static void codegen_final_output(DynStr *ds, GPUOutput *finaloutput)
{
  lib_dynstr_appendf(ds, "return tmp%d;\n", finaloutput->id);
}

static char *code_generate_fragment(GPUMaterial *material,
                                    GPUNodeGraph *graph,
                                    const char *interface_str)
{
  DynStr *ds = lib_dynstr_new();
  char *code;
  int builtins;

  codegen_set_unique_ids(graph);

  /* Attributes, Shader stage interface. */
  if (interface_str) {
    lib_dynstr_appendf(ds, "in codegenInterface {%s};\n\n", interface_str);
  }

  builtins = codegen_process_uniforms_functions(material, ds, graph);

  if (builtins & (GPU_OBJECT_INFO | GPU_OBJECT_COLOR)) {
    lib_dynstr_append(ds, datatoc_gpu_shader_common_obinfos_lib_glsl);
  }

  if (builtins & GPU_BARYCENTRIC_TEXCO) {
    lib_dynstr_append(ds, datatoc_gpu_shader_codegen_lib_glsl);
  }

  lib_dynstr_append(ds, "Closure nodetree_exec(void)\n{\n");

  if (builtins & GPU_BARYCENTRIC_TEXCO) {
    lib_dynstr_append(ds, "  vec2 barytexco = barycentric_resolve(barycentricTexCo);\n");
  }
  /* TODO: get rid of that. */
  if (builtins & GPU_VIEW_MATRIX) {
    lib_dynstr_append(ds, "  #define viewmat ViewMatrix\n");
  }
  if (builtins & GPU_CAMERA_TEXCO_FACTORS) {
    lib_dynstr_append(ds, "  #define camtexfac CameraTexCoFactors\n");
  }
  if (builtins & GPU_OBJECT_MATRIX) {
    lib_dynstr_append(ds, "  #define objmat ModelMatrix\n");
  }
  if (builtins & GPU_INVERSE_OBJECT_MATRIX) {
    lib_dynstr_append(ds, "  #define objinv ModelMatrixInverse\n");
  }
  if (builtins & GPU_INVERSE_VIEW_MATRIX) {
    lib_dynstr_append(ds, "  #define viewinv ViewMatrixInverse\n");
  }
  if (builtins & GPU_LOC_TO_VIEW_MATRIX) {
    lib_dynstr_append(ds, "  #define localtoviewmat (ViewMatrix * ModelMatrix)\n");
  }
  if (builtins & GPU_INVERSE_LOC_TO_VIEW_MATRIX) {
    lib_dynstr_append(ds,
                      "  #define invlocaltoviewmat (ModelMatrixInverse * ViewMatrixInverse)\n");
  }
  if (builtins & GPU_VIEW_NORMAL) {
    lib_dynstr_append(ds, "#ifdef HAIR_SHADER\n");
    lib_dynstr_append(ds, "  vec3 n;\n");
    lib_dynstr_append(ds, "  world_normals_get(n);\n");
    lib_dynstr_append(ds, "  vec3 facingnormal = transform_direction(ViewMatrix, n);\n");
    lib_dynstr_append(ds, "#else\n");
    lib_dynstr_append(ds, "  vec3 facingnormal = gl_FrontFacing ? viewNormal: -viewNormal;\n");
    lib_dynstr_append(ds, "#endif\n");
  }
  if (builtins & GPU_WORLD_NORMAL) {
    lib_dynstr_append(ds, "  vec3 facingwnormal;\n");
    if (builtins & GPU_VIEW_NORMAL) {
      lib_dynstr_append(ds, "#ifdef HAIR_SHADER\n");
      lib_dynstr_append(ds, "  facingwnormal = n;\n");
      lib_dynstr_append(ds, "#else\n");
      lib_dynstr_append(ds, "  world_normals_get(facingwnormal);\n");
      lib_dynstr_append(ds, "#endif\n");
    }
    else {
      lib_dynstr_append(ds, "  world_normals_get(facingwnormal);\n");
    }
  }
  if (builtins & GPU_VIEW_POSITION) {
    lib_dynstr_append(ds, "  #define viewposition viewPosition\n");
  }

  codegen_declare_tmps(ds, graph);
  codegen_call_fns(ds, graph);

  lib_dynstr_append(ds, "  #ifndef VOLUMETRICS\n");
  lib_dynstr_append(ds, "  if (renderPassAOV) {\n");
  lib_dynstr_append(ds, "    switch (render_pass_aov_hash()) {\n");
  GSet *aovhashes_added = lib_gset_int_new(__func__);
  LIST_FOREACH (GPUNodeGraphOutputLink *, aovlink, &graph->outlink_aovs) {
    void *aov_key = PTR_FROM_INT(aovlink->hash);
    if (lib_gset_haskey(aovhashes_added, aov_key)) {
      continue;
    }
    lib_dynstr_appendf(ds, "      case %d: {\n        ", aovlink->hash);
    codegen_final_output(ds, aovlink->outlink->output);
    lib_dynstr_append(ds, "      }\n");
    lib_gset_add(aovhashes_added, aov_key);
  }
  lib_gset_free(aovhashes_added, NULL);
  lib_dynstr_append(ds, "      default: {\n");
  lib_dynstr_append(ds, "        Closure no_aov = CLOSURE_DEFAULT;\n");
  lib_dynstr_append(ds, "        no_aov.holdout = 1.0;\n");
  lib_dynstr_append(ds, "        return no_aov;\n");
  lib_dynstr_append(ds, "      }\n");
  lib_dynstr_append(ds, "    }\n");
  lib_dynstr_append(ds, "  } else {\n");
  lib_dynstr_append(ds, "  #else /* VOLUMETRICS */\n");
  lib_dynstr_append(ds, "  {\n");
  lib_dynstr_append(ds, "  #endif /* VOLUMETRICS */\n    ");
  codegen_final_output(ds, graph->outlink->output);
  lib_dynstr_append(ds, "  }\n");

  lib_dynstr_append(ds, "}\n");

  /* create shader */
  code = lib_dynstr_get_cstring(ds);
  lib_dynstr_free(ds);

#if 0
  if (G.debug & G_DEBUG) {
    printf("%s\n", code);
  }
#endif

  return code;
}

static const char *attr_prefix_get(CustomDataType type)
{
  switch (type) {
    case CD_ORCO:
      return "orco";
    case CD_MTFACE:
      return "u";
    case CD_TANGENT:
      return "t";
    case CD_MCOL:
      return "c";
    case CD_PROP_COLOR:
      return "c";
    case CD_AUTO_FROM_NAME:
      return "a";
    case CD_HAIRLENGTH:
      return "hl";
    default:
      lib_assert_msg(0, "GPUVertAttr Prefix type not found : This should not happen!");
      return "";
  }
}

/* We talk about shader stage interface, not to be mistaken with GPUShaderInterface. */
static char *code_generate_interface(GPUNodeGraph *graph, int builtins)
{
  if (lib_list_is_empty(&graph->attributes) &&
      (builtins & (GPU_BARYCENTRIC_DIST | GPU_BARYCENTRIC_TEXCO)) == 0) {
    return NULL;
  }

  DynStr *ds = lib_dynstr_new();

  lib_dynstr_append(ds, "\n");

  LIST_FOREACH (GPUMaterialAttribute *, attr, &graph->attributes) {
    if (attr->type == CD_HAIRLENGTH) {
      lib_dynstr_appendf(ds, "float var%d;\n", attr->id);
    }
    else {
      lib_dynstr_appendf(ds, "%s var%d;\n", gpu_data_type_to_string(attr->gputype), attr->id);
    }
  }
  if (builtins & GPU_BARYCENTRIC_TEXCO) {
    lib_dynstr_append(ds, "vec2 barycentricTexCo;\n");
  }
  if (builtins & GPU_BARYCENTRIC_DIST) {
    lib_dynstr_append(ds, "vec3 barycentricDist;\n");
  }

  char *code = lib_dynstr_get_cstring(ds);

  lib_dynstr_free(ds);

  return code;
}

static char *code_generate_vertex(GPUNodeGraph *graph,
                                  const char *interface_str,
                                  const char *vert_code,
                                  int builtins)
{
  DynStr *ds = lib_dynstr_new();

  lub_dynstr_append(ds, datatoc_gpu_shader_codegen_lib_glsl);

  /* Inputs */
  LIST_FOREACH (GPUMaterialAttribute *, attr, &graph->attributes) {
    const char *type_str = gpu_data_type_to_string(attr->gputype);
    const char *prefix = attr_prefix_get(attr->type);
    /* XXX FIXME: see notes in mesh_render_data_create() */
    /* NOTE: Replicate changes to mesh_render_data_create() in draw_cache_impl_mesh.c */
    if (attr->type == CD_ORCO) {
      /* OPTI: orco is computed from local positions, but only if no modifier is present. */
      lib_dynstr_append(ds, datatoc_gpu_shader_common_obinfos_lib_glsl);
      lib_dynstr_append(ds, "DEFINE_ATTR(vec4, orco);\n");
    }
    else if (attr->type == CD_HAIRLENGTH) {
      lib_dynstr_append(ds, datatoc_gpu_shader_common_obinfos_lib_glsl);
      lib_dynstr_append(ds, "DEFINE_ATTR(float, hairLen);\n");
    }
    else if (attr->name[0] == '\0') {
      lib_dynstr_appendf(ds, "DEFINE_ATTR(%s, %s);\n", type_str, prefix);
      lib_dynstr_appendf(ds, "#define att%d %s\n", attr->id, prefix);
    }
    else {
      char attr_safe_name[GPU_MAX_SAFE_ATTR_NAME];
      gpu_vertformat_safe_attr_name(attr->name, attr_safe_name, GPU_MAX_SAFE_ATTR_NAME);
      lib_dynstr_appendf(ds, "DEFINE_ATTR(%s, %s%s);\n", type_str, prefix, attr_safe_name);
      lib_dynstr_appendf(ds, "#define att%d %s%s\n", attr->id, prefix, attr_safe_name);
    }
  }

  /* Outputs interface */
  if (interface_str) {
    lib_dynstr_appendf(ds, "out codegenInterface {%s};\n\n", interface_str);
  }

  /* Prototype. Needed for hair functions. */
  lib_dynstr_append(ds, "void pass_attr(vec3 position, mat3 normalmat, mat4 modelmatinv);\n");
  lib_dynstr_append(ds, "#define USE_ATTR\n\n");

  lib_dynstr_append(ds, vert_code);
  lib_dynstr_append(ds, "\n\n");

  lib_dynstr_append(ds, "void pass_attr(vec3 position, mat3 normalmat, mat4 modelmatinv) {\n");

  /* GPU_BARYCENTRIC_TEXCO cannot be computed based on gl_VertexID
   * for MESH_SHADER because of indexed drawing. In this case a
   * geometry shader is needed. */
  if (builtins & GPU_BARYCENTRIC_TEXCO) {
    lib_dynstr_appendf(ds, "  barycentricTexCo = barycentric_get();\n");
  }
  if (builtins & GPU_BARYCENTRIC_DIST) {
    lib_dynstr_appendf(ds, "  barycentricDist = vec3(0);\n");
  }

  LIST_FOREACH (GPUMaterialAttribute *, attr, &graph->attributes) {
    if (attr->type == CD_TANGENT) { /* silly exception */
      lib_dynstr_appendf(ds, "  var%d = tangent_get(att%d, normalmat);\n", attr->id, attr->id);
    }
    else if (attr->type == CD_ORCO) {
      lib_dynstr_appendf(
          ds, "  var%d = orco_get(position, modelmatinv, OrcoTexCoFactors, orco);\n", attr->id);
    }
    else if (attr->type == CD_HAIRLENGTH) {
      lib_dynstr_appendf(ds, "  var%d = hair_len_get(hair_get_strand_id(), hairLen);\n", attr->id);
    }
    else {
      const char *type_str = gpu_data_type_to_string(attr->gputype);
      lib_dynstr_appendf(ds, "  var%d = GET_ATTR(%s, att%d);\n", attr->id, type_str, attr->id);
    }
  }

  lib_dynstr_append(ds, "}\n");

  char *code = lib_dynstr_get_cstring(ds);

  lib_dynstr_free(ds);

#if 0
  if (G.debug & G_DEBUG) {
    printf("%s\n", code);
  }
#endif

  return code;
}

static char *code_generate_geometry(GPUNodeGraph *graph,
                                    const char *interface_str,
                                    const char *geom_code,
                                    int builtins)
{
  if (!geom_code) {
    return NULL;
  }

  DynStr *ds = lib_dynstr_new();

  /* Attributes, Shader interface; */
  if (interface_str) {
    lib_dynstr_appendf(ds, "in codegenInterface {%s} dataAttrIn[];\n\n", interface_str);
    lib_dynstr_appendf(ds, "out codegenInterface {%s} dataAttrOut;\n\n", interface_str);
  }

  lib_dynstr_append(ds, datatoc_gpu_shader_codegen_lib_glsl);

  if (builtins & GPU_BARYCENTRIC_DIST) {
    /* geom_code should do something with this, but may not. */
    lib_dynstr_append(ds, "#define DO_BARYCENTRIC_DISTANCES\n");
  }

  /* Generate varying assignments. */
  lib_dynstr_append(ds, "#define USE_ATTR\n");
  /* This needs to be a define. Some drivers don't like variable vert index inside dataAttrIn. */
  lib_dynstr_append(ds, "#define pass_attr(vert) {\\\n");

  if (builtins & GPU_BARYCENTRIC_TEXCO) {
    lib_dynstr_append(ds, "dataAttrOut.barycentricTexCo = calc_barycentric_co(vert);\\\n");
  }

  LIST_FOREACH (GPUMaterialAttribute *, attr, &graph->attributes) {
    /* TODO: let shader choose what to do depending on what the attribute is. */
    lib_dynstr_appendf(ds, "dataAttrOut.var%d = dataAttrIn[vert].var%d;\\\n", attr->id, attr->id);
  }
  lib_dynstr_append(ds, "}\n\n");

  lib_dynstr_append(ds, geom_code);

  char *code = lib_dynstr_get_cstring(ds);
  lib_dynstr_free(ds);

  return code;
}

GPUShader *gpu_pass_shader_get(GPUPass *pass)
{
  return pass->shader;
}

/* Pass create/free */
static bool gpu_pass_is_valid(GPUPass *pass)
{
  /* Shader is not null if compilation is successful. */
  return (pass->compiled == false || pass->shader != NULL);
}

GPUPass *gpu_generate_pass(GPUMaterial *material,
                           GPUNodeGraph *graph,
                           const char *vert_code,
                           const char *geom_code,
                           const char *frag_lib,
                           const char *defines)
{
  /* Prune the unused nodes and extract attributes before compiling so the
   * generated VBOs are ready to accept the future shader. */
  gpu_node_graph_prune_unused(graph);
  gpu_node_graph_finalize_uniform_attrs(graph);

  int builtins = 0;
  LIST_FOREACH (GPUNode *, node, &graph->nodes) {
    LIST_FOREACH (GPUInput *, input, &node->inputs) {
      if (input->source == GPU_SOURCE_BUILTIN) {
        builtins |= input->builtin;
      }
    }
  }
  /* generate code */
  char *interface_str = code_generate_interface(graph, builtins);
  char *fragmentgen = code_generate_fragment(material, graph, interface_str);

  /* Cache lookup: Reuse shaders already compiled */
  uint32_t hash = gpu_pass_hash(fragmentgen, defines, &graph->attributes);
  GPUPass *pass_hash = gpu_pass_cache_lookup(hash);

  if (pass_hash && (pass_hash->next == NULL || pass_hash->next->hash != hash)) {
    /* No collision, just return the pass. */
    MEM_SAFE_FREE(interface_str);
    MEM_freeN(fragmentgen);
    if (!gpu_pass_is_valid(pass_hash)) {
      /* Shader has already been created but failed to compile. */
      return NULL;
    }
    pass_hash->refcount += 1;
    return pass_hash;
  }

  /* Either the shader is not compiled or there is a hash collision...
   * continue generating the shader strings. */
  GSet *used_libraries = gpu_material_used_libraries(material);
  char *tmp = gpu_material_library_generate_code(used_libraries, frag_lib);

  char *geometrycode = code_generate_geometry(graph, interface_str, geom_code, builtins);
  char *vertexcode = code_generate_vertex(graph, interface_str, vert_code, builtins);
  char *fragmentcode = lib_strdupcat(tmp, fragmentgen);

  MEM_SAFE_FREE(interface_str);
  mem_freen(fragmentgen);
  mem_freen(tmp);

  GPUPass *pass = NULL;
  if (pass_hash) {
    /* Cache lookup: Reuse shaders already compiled */
    pass = gpu_pass_cache_resolve_collision(
        pass_hash, vertexcode, geometrycode, fragmentcode, defines, hash);
  }

  if (pass) {
    MEM_SAFE_FREE(vertexcode);
    MEM_SAFE_FREE(fragmentcode);
    MEM_SAFE_FREE(geometrycode);

    /* Cache hit. Reuse the same GPUPass and GPUShader. */
    if (!gpu_pass_is_valid(pass)) {
      /* Shader has already been created but failed to compile. */
      return NULL;
    }

    pass->refcount += 1;
  }
  else {
    /* We still create a pass even if shader compilation
     * fails to avoid trying to compile again and again. */
    pass = mem_callocn(sizeof(GPUPass), "GPUPass");
    pass->shader = NULL;
    pass->refcount = 1;
    pass->hash = hash;
    pass->vertexcode = vertexcode;
    pass->fragmentcode = fragmentcode;
    pass->geometrycode = geometrycode;
    pass->defines = (defines) ? BLI_strdup(defines) : NULL;
    pass->compiled = false;

    lib_spin_lock(&pass_cache_spin);
    if (pass_hash != NULL) {
      /* Add after the first pass having the same hash. */
      pass->next = pass_hash->next;
      pass_hash->next = pass;
    }
    else {
      /* No other pass have same hash, just prepend to the list. */
      LIB_LINKS_PREPEND(pass_cache, pass);
    }
    lib_spin_unlock(&pass_cache_spin);
  }

  return pass;
}

static int count_active_texture_sampler(GPUShader *shader, const char *source)
{
  const char *code = source;

  /* Remember this is per stage. */
  GSet *sampler_ids = lib_gset_int_new(__func__);
  int num_samplers = 0;

  while ((code = strstr(code, "uniform "))) {
    /* Move past "uniform". */
    code += 7;
    /* Skip following spaces. */
    while (*code == ' ') {
      code++;
    }
    /* Skip "i" from potential isamplers. */
    if (*code == 'i') {
      code++;
    }
    /* Skip following spaces. */
    if (lib_str_startswith(code, "sampler")) {
      /* Move past "uniform". */
      code += 7;
      /* Skip sampler type suffix. */
      while (!ELEM(*code, ' ', '\0')) {
        code++;
      }
      /* Skip following spaces. */
      while (*code == ' ') {
        code++;
      }

      if (*code != '\0') {
        char sampler_name[64];
        code = gpu_str_skip_token(code, sampler_name, sizeof(sampler_name));
        int id = gpu_shader_get_uniform(shader, sampler_name);

        if (id == -1) {
          continue;
        }
        /* Catch duplicates. */
        if (lib_gset_add(sampler_ids, POINTER_FROM_INT(id))) {
          num_samplers++;
        }
      }
    }
  }

  lib_gset_free(sampler_ids, NULL);

  return num_samplers;
}

static bool gpu_pass_shader_validate(GPUPass *pass, GPUShader *shader)
{
  if (shader == NULL) {
    return false;
  }

  /* NOTE: The only drawback of this method is that it will count a sampler
   * used in the fragment shader and only declared (but not used) in the vertex
   * shader as used by both. But this corner case is not happening for now. */
  int vert_samplers_len = count_active_texture_sampler(shader, pass->vertexcode);
  int frag_samplers_len = count_active_texture_sampler(shader, pass->fragmentcode);

  int total_samplers_len = vert_samplers_len + frag_samplers_len;

  /* Validate against opengl limit. */
  if ((frag_samplers_len > gpu_max_textures_frag()) ||
      (vert_samplers_len > gpu_max_textures_vert())) {
    return false;
  }

  if (pass->geometrycode) {
    int geom_samplers_len = count_active_texture_sampler(shader, pass->geometrycode);
    total_samplers_len += geom_samplers_len;
    if (geom_samplers_len > gpu_max_textures_geom()) {
      return false;
    }
  }

  return (total_samplers_len <= gpu_max_textures());
}

bool gpu_pass_compile(GPUPass *pass, const char *shname)
{
  bool success = true;
  if (!pass->compiled) {
    GPUShader *shader = gpu_shader_create(
        pass->vertexcode, pass->fragmentcode, pass->geometrycode, NULL, pass->defines, shname);

    /* NOTE: Some drivers / gpu allows more active samplers than the opengl limit.
     * We need to make sure to count active samplers to avoid undefined behavior. */
    if (!gpu_pass_shader_validate(pass, shader)) {
      success = false;
      if (shader != NULL) {
        fprintf(stderr, "GPUShader: error: too many samplers in shader.\n");
        gpu_shader_free(shader);
        shader = NULL;
      }
    }
    pass->shader = shader;
    pass->compiled = true;
  }

  return success;
}

void gpu_pass_release(GPUPass *pass)
{
  lib_assert(pass->refcount > 0);
  pass->refcount--;
}

static void gpu_pass_free(GPUPass *pass)
{
  lib_assert(pass->refcount == 0);
  if (pass->shader) {
    gpu_shader_free(pass->shader);
  }
  MEM_SAFE_FREE(pass->fragmentcode);
  MEM_SAFE_FREE(pass->geometrycode);
  MEM_SAFE_FREE(pass->vertexcode);
  MEM_SAFE_FREE(pass->defines);
  mem_freen(pass);
}

void gpu_pass_cache_garbage_collect(void)
{
  static int lasttime = 0;
  const int shadercollectrate = 60; /* hardcoded for now. */
  int ctime = (int)PIL_check_seconds_timer();

  if (ctime < shadercollectrate + lasttime) {
    return;
  }

  lasttime = ctime;

  lib_spin_lock(&pass_cache_spin);
  GPUPass *next, **prev_pass = &pass_cache;
  for (GPUPass *pass = pass_cache; pass; pass = next) {
    next = pass->next;
    if (pass->refcount == 0) {
      /* Remove from list */
      *prev_pass = next;
      gpu_pass_free(pass);
    }
    else {
      prev_pass = &pass->next;
    }
  }
  lib_spin_unlock(&pass_cache_spin);
}

void gpu_pass_cache_init(void)
{
  lib_spin_init(&pass_cache_spin);
}

void gpu_pass_cache_free(void)
{
  lib_spin_lock(&pass_cache_spin);
  while (pass_cache) {
    GPUPass *next = pass_cache->next;
    gpu_pass_free(pass_cache);
    pass_cache = next;
  }
  lib_spin_unlock(&pass_cache_spin);

  lib_spin_end(&pass_cache_spin);
}

/* Module */
void gpu_codegen_init(void)
{
}

void gpu_codegen_exit(void)
{
  dune_material_defaults_free_gpu();
  gpu_shader_free_builtin_shaders();
}
