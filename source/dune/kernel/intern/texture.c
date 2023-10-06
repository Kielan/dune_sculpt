#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_kdopbvh.h"
#include "lib_list.h"
#include "lib_math.h"
#include "lib_math_color.h"
#include "lib_utildefines.h"

#include "lang.h"

/* Allow using deprecated functionality for .dune file I/O. */
#define TYPES_DEPRECATED_ALLOW

#include "types_brush.h"
#include "types_color.h"
#include "types_defaults.h"
#include "types_key.h"
#include "types_linestyle.h"
#include "types_material.h"
#include "types_node.h"
#include "types_object.h"
#include "types_particle.h"

#include "imbuf.h"

#include "dune_main.h"

#include "dune_anim_data.h"
#include "dune_colorband.h"
#include "dune_colortools.h"
#include "dune_icons.h"
#include "dune_idtype.h"
#include "dune_image.h"
#include "dune_key.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_material.h"
#include "dune_node.h"
#include "dune_scene.h"
#include "dune_texture.h"

#include "node_texture.h"

#include "render_texture.h"

#include "loader_read_write.h"

static void texture_init_data(Id *id)
{
  Tex *texture = (Tex *)id;

  lib_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(texture, id));

  MEMCPY_STRUCT_AFTER(texture, structs_struct_default_get(Tex), id);

  imageuser_default(&texture->iuser);
}

static void texture_copy_data(Main *main, Id *id_dst, const Id *id_src, const int flag)
{
  Tex *texture_dst = (Tex *)id_dst;
  const Tex *texture_src = (const Tex *)id_src;

  const bool is_localized = (flag & LIB_ID_CREATE_LOCAL) != 0;
  /* We always need allocation of our private Id data. */
  const int flag_private_id_data = flag & ~LIB_ID_CREATE_NO_ALLOCATE;

  if (!texture_is_image_user(texture_src)) {
    texture_dst->ima = NULL;
  }

  if (texture_dst->coba) {
    texture_dst->coba = mem_dupallocn(texture_dst->coba);
  }
  if (texture_src->nodetree) {
    if (texture_src->nodetree->exdata) {
      ntreeTexEndExTree(texture_src->nodetree->exdata);
    }

    if (is_localized) {
      texture_dst->nodetree = ntreeLocalize(texture_src->nodetree);
    }
    else {
      id_copy_ex(
          main, (Id *)texture_src->nodetree, (Id **)&texture_dst->nodetree, flag_private_id_data);
    }
  }

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    previewimg_id_copy(&texture_dst->id, &texture_src->id);
  }
  else {
    texture_dst->preview = NULL;
  }
}

static void texture_free_data(Id *id)
{
  Tex *texture = (Tex *)id;

  /* is no lib link block, but texture extension */
  if (texture->nodetree) {
    ntreeFreeEmbeddedTree(texture->nodetree);
    mem_freen(texture->nodetree);
    texture->nodetree = NULL;
  }

  MEM_SAFE_FREE(texture->coba);

  icon_id_delete((Id *)texture);
  previewimg_free(&texture->preview);
}

static void texture_foreach_id(Id *id, ForeachIdData *data)
{
  Tex *texture = (Tex *)id;
  if (texture->nodetree) {
    /* nodetree **are owned by Ids**, treat them as mere sub-data and not real ID! */
    FOREACHID_PROCESS_FN_CALL(
        data, lib_foreach_id_embedded(data, (Id **)&texture->nodetree));
  }
  FOREACHID_PROCESS_IDSUPER(data, texture->ima, IDWALK_CB_USER);
}

static void texture_write(Writer *writer, Id *id, const void *id_address)
{
  Tex *tex = (Tex *)id;

  /* write LibData */
  loader_write_id_struct(writer, Tex, id_address, &tex->id);
  id_write(writer, &tex->id);

  if (tex->adt) {
    animdata_write(writer, tex->adt);
  }

  /* direct data */
  if (tex->coba) {
    loader_write_struct(writer, ColorBand, tex->coba);
  }

  /* nodetree is integral part of texture, no libdata */
  if (tex->nodetree) {
    loader_write_struct(writer, NodeTree, tex->nodetree);
    ntreeWrite(writer, tex->nodetree);
  }

  previewimg_write(writer, tex->preview);
}

static void texture_read_data(DataReader *reader, Id *id)
{
  Tex *tex = (Tex *)id;
  loader_read_data_address(reader, &tex->adt);
  animdata_read_data(reader, tex->adt);

  loader_read_data_address(reader, &tex->coba);

  loader_read_data_address(reader, &tex->preview);
  previewimg_read(reader, tex->preview);

  tex->iuser.scene = NULL;
}

static void texture_read_lib(LibReader *reader, Id *id)
{
  Tex *tex = (Tex *)id;
  loader_read_id_address(reader, tex->id.lib, &tex->ima);
  loader_read_id_address(reader, tex->id.lib, &tex->ipo); /* deprecated - old animation system */
}

static void texture_read_expand(Expander *expander, Id *id)
{
  Tex *tex = (Tex *)id;
  loader_expand(expander, tex->ima);
  loader_expand(expander, tex->ipo); /* deprecated - old animation system */
}

IdTypeInfo IdType_TE = {
    .id_code = ID_TE,
    .id_filter = FILTER_ID_TE,
    .main_list_index = INDEX_ID_TE,
    .struct_size = sizeof(Tex),
    .name = "Texture",
    .name_plural = "textures",
    .lang_cxt = LANG_CXT_ID_TEXTURE,
    .flags = IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = texture_init_data,
    .copy_data = texture_copy_data,
    .free_data = texture_free_data,
    .make_local = NULL,
    .foreach_id = texture_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .dune_write = texture_write,
    .dune_read_data = texture_read_data,
    .dune_read_lib = texture_read_lib,
    .dune_read_expand = texture_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

void texture_mtex_foreach_id(ForeachIdData *data, MTex *mtex)
{
  FOREACHID_PROCESS_IDSUPER(data, mtex->object, IDWALK_CB_NOP);
  FOREACHID_PROCESS_IDSUPER(data, mtex->tex, IDWALK_CB_USER);
}

/* Mapping */
TexMapping *texture_mapping_add(int type)
{
  TexMapping *texmap = mem_callocn(sizeof(TexMapping), "TexMapping");

  texture_mapping_default(texmap, type);

  return texmap;
}

void texture_mapping_default(TexMapping *texmap, int type)
{
  memset(texmap, 0, sizeof(TexMapping));

  texmap->size[0] = texmap->size[1] = texmap->size[2] = 1.0f;
  texmap->max[0] = texmap->max[1] = texmap->max[2] = 1.0f;
  unit_m4(texmap->mat);

  texmap->projx = PROJ_X;
  texmap->projy = PROJ_Y;
  texmap->projz = PROJ_Z;
  texmap->mapping = MTEX_FLAT;
  texmap->type = type;
}

void texture_mapping_init(TexMapping *texmap)
{
  float smat[4][4], rmat[4][4], tmat[4][4], proj[4][4], size[3];

  if (texmap->projx == PROJ_X && texmap->projy == PROJ_Y && texmap->projz == PROJ_Z &&
      is_zero_v3(texmap->loc) && is_zero_v3(texmap->rot) && is_one_v3(texmap->size)) {
    unit_m4(texmap->mat);

    texmap->flag |= TEXMAP_UNIT_MATRIX;
  }
  else {
    /* axis projection */
    zero_m4(proj);
    proj[3][3] = 1.0f;

    if (texmap->projx != PROJ_N) {
      proj[texmap->projx - 1][0] = 1.0f;
    }
    if (texmap->projy != PROJ_N) {
      proj[texmap->projy - 1][1] = 1.0f;
    }
    if (texmap->projz != PROJ_N) {
      proj[texmap->projz - 1][2] = 1.0f;
    }

    /* scale */
    copy_v3_v3(size, texmap->size);

    if (ELEM(texmap->type, TEXMAP_TYPE_TEXTURE, TEXMAP_TYPE_NORMAL)) {
      /* keep matrix invertible */
      if (fabsf(size[0]) < 1e-5f) {
        size[0] = signf(size[0]) * 1e-5f;
      }
      if (fabsf(size[1]) < 1e-5f) {
        size[1] = signf(size[1]) * 1e-5f;
      }
      if (fabsf(size[2]) < 1e-5f) {
        size[2] = signf(size[2]) * 1e-5f;
      }
    }

    size_to_mat4(smat, texmap->size);

    /* rotation */
    eul_to_mat4(rmat, texmap->rot);

    /* translation */
    unit_m4(tmat);
    copy_v3_v3(tmat[3], texmap->loc);

    if (texmap->type == TEXMAP_TYPE_TEXTURE) {
      /* to transform a texture, the inverse transform needs
       * to be applied to the texture coordinate */
      mul_m4_series(texmap->mat, tmat, rmat, smat);
      invert_m4(texmap->mat);
    }
    else if (texmap->type == TEXMAP_TYPE_POINT) {
      /* forward transform */
      mul_m4_series(texmap->mat, tmat, rmat, smat);
    }
    else if (texmap->type == TEXMAP_TYPE_VECTOR) {
      /* no translation for vectors */
      mul_m4_m4m4(texmap->mat, rmat, smat);
    }
    else if (texmap->type == TEXMAP_TYPE_NORMAL) {
      /* no translation for normals, and inverse transpose */
      mul_m4_m4m4(texmap->mat, rmat, smat);
      invert_m4(texmap->mat);
      transpose_m4(texmap->mat);
    }

    /* projection last */
    mul_m4_m4m4(texmap->mat, texmap->mat, proj);

    texmap->flag &= ~TEXMAP_UNIT_MATRIX;
  }
}

ColorMapping *texture_colormapping_add(void)
{
  ColorMapping *colormap = mem_callocn(sizeof(ColorMapping), "ColorMapping");

  texture_colormapping_default(colormap);

  return colormap;
}

void texture_colormapping_default(ColorMapping *colormap)
{
  memset(colormap, 0, sizeof(ColorMapping));

  colorband_init(&colormap->coba, true);

  colormap->bright = 1.0;
  colormap->contrast = 1.0;
  colormap->saturation = 1.0;

  colormap->dune_color[0] = 0.8f;
  colormap->dune_color[1] = 0.8f;
  colormap->dune_color[2] = 0.8f;
  colormap->dune_type = MA_RAMP_BLEND;
  colormap->dune_factor = 0.0f;
}

/* TEX */
void texture_default(Tex *tex)
{
  texture_init_data(&tex->id);
}

void texture_type_set(Tex *tex, int type)
{
  tex->type = type;
}

Tex *texture_add(Main *main, const char *name)
{
  Tex *tex;

  tex = id_new(main, IdTE, name);

  return tex;
}

void texture_mtex_default(MTex *mtex)
{
  memcpy(mtex, structs_struct_default_get(MTex), sizeof(*mtex));
}

MTex *texture_mtex_add(void)
{
  MTex *mtex;

  mtex = mem_callocn(sizeof(MTex), "texture_mtex_add");

  texture_mtex_default(mtex);

  return mtex;
}

MTex *texture_mtex_add_id(Id *id, int slot)
{
  MTex **mtex_ar;
  short act;

  give_active_mtex(id, &mtex_ar, &act);

  if (mtex_ar == NULL) {
    return NULL;
  }

  if (slot == -1) {
    /* find first free */
    int i;
    for (i = 0; i < MAX_MTEX; i++) {
      if (!mtex_ar[i]) {
        slot = i;
        break;
      }
    }
    if (slot == -1) {
      return NULL;
    }
  }
  else {
    /* make sure slot is valid */
    if (slot < 0 || slot >= MAX_MTEX) {
      return NULL;
    }
  }

  if (mtex_ar[slot]) {
    id_us_min((Id *)mtex_ar[slot]->tex);
    mem_freen(mtex_ar[slot]);
    mtex_ar[slot] = NULL;
  }

  mtex_ar[slot] = texture_mtex_add();

  return mtex_ar[slot];
}

Tex *give_current_linestyle_texture(FreestyleLineStyle *linestyle)
{
  MTex *mtex = NULL;
  Tex *tex = NULL;

  if (linestyle) {
    mtex = linestyle->mtex[(int)(linestyle->texact)];
    if (mtex) {
      tex = mtex->tex;
    }
  }

  return tex;
}

void set_current_linestyle_texture(FreestyleLineStyle *linestyle, Tex *newtex)
{
  int act = linestyle->texact;

  if (linestyle->mtex[act] && linestyle->mtex[act]->tex) {
    id_us_min(&linestyle->mtex[act]->tex->id);
  }

  if (newtex) {
    if (!linestyle->mtex[act]) {
      linestyle->mtex[act] = texture_mtex_add();
      linestyle->mtex[act]->texco = TEXCO_STROKE;
    }

    linestyle->mtex[act]->tex = newtex;
    id_us_plus(&newtex->id);
  }
  else {
    MEM_SAFE_FREE(linestyle->mtex[act]);
  }
}

bool give_active_mtex(Id *id, MTex ***mtex_ar, short *act)
{
  switch (GS(id->name)) {
    case ID_LS:
      *mtex_ar = ((FreestyleLineStyle *)id)->mtex;
      if (act) {
        *act = (((FreestyleLineStyle *)id)->texact);
      }
      break;
    case ID_PA:
      *mtex_ar = ((ParticleSettings *)id)->mtex;
      if (act) {
        *act = (((ParticleSettings *)id)->texact);
      }
      break;
    default:
      *mtex_ar = NULL;
      if (act) {
        *act = 0;
      }
      return false;
  }

  return true;
}

void set_active_mtex(Id *id, short act)
{
  if (act < 0) {
    act = 0;
  }
  else if (act >= MAX_MTEX) {
    act = MAX_MTEX - 1;
  }

  switch (GS(id->name)) {
    case ID_LS:
      ((FreestyleLineStyle *)id)->texact = act;
      break;
    case ID_PA:
      ((ParticleSettings *)id)->texact = act;
      break;
    default:
      break;
  }
}

Tex *give_current_brush_texture(Brush *br)
{
  return br->mtex.tex;
}

void set_current_brush_texture(Brush *br, Tex *newtex)
{
  if (br->mtex.tex) {
    id_us_min(&br->mtex.tex->id);
  }

  if (newtex) {
    br->mtex.tex = newtex;
    id_us_plus(&newtex->id);
  }
}

Tex *give_current_particle_texture(ParticleSettings *part)
{
  MTex *mtex = NULL;
  Tex *tex = NULL;

  if (!part) {
    return NULL;
  }

  mtex = part->mtex[(int)(part->texact)];
  if (mtex) {
    tex = mtex->tex;
  }

  return tex;
}

void set_current_particle_texture(ParticleSettings *part, Tex *newtex)
{
  int act = part->texact;

  if (part->mtex[act] && part->mtex[act]->tex) {
    id_us_min(&part->mtex[act]->tex->id);
  }

  if (newtex) {
    if (!part->mtex[act]) {
      part->mtex[act] = texture_mtex_add();
      part->mtex[act]->texco = TEXCO_ORCO;
      part->mtex[act]->blendtype = MTEX_MUL;
    }

    part->mtex[act]->tex = newtex;
    id_us_plus(&newtex->id);
  }
  else {
    MEM_SAFE_FREE(part->mtex[act]);
  }
}

void texture_pointdensity_init_data(PointDensity *pd)
{
  pd->flag = 0;
  pd->radius = 0.3f;
  pd->falloff_type = TEX_PD_FALLOFF_STD;
  pd->falloff_softness = 2.0;
  pd->source = TEX_PD_PSYS;
  pd->point_tree = NULL;
  pd->point_data = NULL;
  pd->noise_size = 0.5f;
  pd->noise_depth = 1;
  pd->noise_fac = 1.0f;
  pd->noise_influence = TEX_PD_NOISE_STATIC;
  pd->coba = colorband_add(true);
  pd->speed_scale = 1.0f;
  pd->totpoints = 0;
  pd->object = NULL;
  pd->psys = 0;
  pd->psys_cache_space = TEX_PD_WORLDSPACE;
  pd->falloff_curve = curvemapping_add(1, 0, 0, 1, 1);

  pd->falloff_curve->preset = CURVE_PRESET_LINE;
  pd->falloff_curve->flag &= ~CUMA_EXTEND_EXTRAPOLATE;
  curvemap_reset(pd->falloff_curve->cm,
                     &pd->falloff_curve->clipr,
                     pd->falloff_curve->preset,
                     CURVEMAP_SLOPE_POSITIVE);
  curvemapping_changed(pd->falloff_curve, false);
}

PointDensity *texture_pointdensity_add(void)
{
  PointDensity *pd = mem_callocn(sizeof(PointDensity), "pointdensity");
  texture_pointdensity_init_data(pd);
  return pd;
}

PointDensity *texture_pointdensity_copy(const PointDensity *pd, const int UNUSED(flag))
{
  PointDensity *pdn;

  pdn = mem_dupallocn(pd);
  pdn->point_tree = NULL;
  pdn->point_data = NULL;
  if (pdn->coba) {
    pdn->coba = mem_dupallocn(pdn->coba);
  }
  pdn->falloff_curve = curvemapping_copy(pdn->falloff_curve); /* can be NULL */
  return pdn;
}

void texture_pointdensity_free_data(PointDensity *pd)
{
  if (pd->point_tree) {
    lib_bvhtree_free(pd->point_tree);
    pd->point_tree = NULL;
  }
  MEM_SAFE_FREE(pd->point_data);
  MEM_SAFE_FREE(pd->coba);

  curvemapping_free(pd->falloff_curve); /* can be NULL */
}

void texture_pointdensity_free(PointDensity *pd)
{
  texture_pointdensity_free_data(pd);
  mem_freen(pd);
}

bool texture_is_image_user(const struct Tex *tex)
{
  switch (tex->type) {
    case TEX_IMAGE: {
      return true;
    }
  }

  return false;
}

bool texture_dependsOnTime(const struct Tex *texture)
{
  if (texture->ima && image_is_animated(texture->ima)) {
    return true;
  }
  if (texture->adt) {
    /* assume anything in adt means the texture is animated */
    return true;
  }
  if (texture->type == TEX_NOISE) {
    /* noise always varies with time */
    return true;
  }
  return false;
}

void texture_get_value_ex(const Scene *scene,
                          Tex *texture,
                          const float *tex_co,
                          TexResult *texres,
                          struct ImagePool *pool,
                          bool use_color_management)
{
  int result_type;
  bool do_color_manage = false;

  if (scene && use_color_management) {
    do_color_manage = scene_check_color_management_enabled(scene);
  }

  /* no node textures for now */
  result_type = multitex_ext_safe(texture, tex_co, texres, pool, do_color_manage, false);

  /* if the texture gave an RGB value, we assume it didn't give a valid
   * intensity, since this is in the context of mods don't use perceptual color conversion.
   * if the texture didn't give an RGB value, copy the intensity across */
  if (result_type & TEX_RGB) {
    texres->tin = (1.0f / 3.0f) * (texres->trgba[0] + texres->trgba[1] + texres->trgba[2]);
  }
  else {
    copy_v3_fl(texres->trgba, texres->tin);
  }
}

void texture_get_value(const Scene *scene,
                       Tex *texture,
                       const float *tex_co,
                       TexResult *texres,
                       bool use_color_management)
{
  texture_get_value_ex(scene, texture, tex_co, texres, NULL, use_color_management);
}

static void texture_nodes_fetch_images_for_pool(Tex *texture,
                                                NodeTree *ntree,
                                                struct ImagePool *pool)
{
  LIST_FOREACH (Node *, node, &ntree->nodes) {
    if (node->type == SH_NODE_TEX_IMAGE && node->id != NULL) {
      Image *image = (Image *)node->id;
      image_pool_acquire_ibuf(image, &texture->iuser, pool);
    }
    else if (node->type == NODE_GROUP && node->id != NULL) {
      /* Do we need to control recursion here? */
      NodeTree *nested_tree = (NodeTree *)node->id;
      texture_nodes_fetch_images_for_pool(texture, nested_tree, pool);
    }
  }
}

void texture_fetch_images_for_pool(Tex *texture, struct ImagePool *pool)
{
  if (texture->nodetree != NULL) {
    texture_nodes_fetch_images_for_pool(texture, texture->nodetree, pool);
  }
  else {
    if (texture->type == TEX_IMAGE) {
      if (texture->ima != NULL) {
        image_pool_acquire_ibuf(texture->ima, &texture->iuser, pool);
      }
    }
  }
}
