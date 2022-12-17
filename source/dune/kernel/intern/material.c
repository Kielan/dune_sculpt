#include <math.h>
#include <stddef.h>
#include <string.h>

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_ID.h"
#include "DNA_anim_types.h"
#include "DNA_collection_types.h"
#include "DNA_curve_types.h"
#include "DNA_curves_types.h"
#include "DNA_customdata_types.h"
#include "DNA_defaults.h"
#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_meta_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_pointcloud_types.h"
#include "DNA_scene_types.h"
#include "DNA_volume_types.h"

#include "BLI_array_utils.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_brush.h"
#include "BKE_curve.h"
#include "BKE_displist.h"
#include "BKE_editmesh.h"
#include "BKE_gpencil.h"
#include "BKE_icons.h"
#include "BKE_idtype.h"
#include "BKE_image.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_scene.h"
#include "BKE_vfont.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "GPU_material.h"

#include "NOD_shader.h"

#include "BLO_read_write.h"

static CLG_LogRef LOG = {"bke.material"};

static void material_init_data(ID *id)
{
  Material *material = (Material *)id;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(material, id));

  MEMCPY_STRUCT_AFTER(material, DNA_struct_default_get(Material), id);
}

static void material_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Material *material_dst = (Material *)id_dst;
  const Material *material_src = (const Material *)id_src;

  const bool is_localized = (flag & LIB_ID_CREATE_LOCAL) != 0;
  /* We always need allocation of our private ID data. */
  const int flag_private_id_data = flag & ~LIB_ID_CREATE_NO_ALLOCATE;

  if (material_src->nodetree != NULL) {
    if (is_localized) {
      material_dst->nodetree = ntreeLocalize(material_src->nodetree);
    }
    else {
      BKE_id_copy_ex(bmain,
                     (ID *)material_src->nodetree,
                     (ID **)&material_dst->nodetree,
                     flag_private_id_data);
    }
  }

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    BKE_previewimg_id_copy(&material_dst->id, &material_src->id);
  }
  else {
    material_dst->preview = NULL;
  }

  if (material_src->texpaintslot != NULL) {
    /* TODO: Think we can also skip copying this data in the more generic `NO_MAIN` case? */
    material_dst->texpaintslot = is_localized ? NULL : MEM_dupallocN(material_src->texpaintslot);
  }

  if (material_src->gp_style != NULL) {
    material_dst->gp_style = MEM_dupallocN(material_src->gp_style);
  }

  BLI_listbase_clear(&material_dst->gpumaterial);

  /* TODO: Duplicate Engine Settings and set runtime to NULL. */
}

static void material_free_data(ID *id)
{
  Material *material = (Material *)id;

  /* Free gpu material before the ntree */
  GPU_material_free(&material->gpumaterial);

  /* is no lib link block, but material extension */
  if (material->nodetree) {
    ntreeFreeEmbeddedTree(material->nodetree);
    MEM_freeN(material->nodetree);
    material->nodetree = NULL;
  }

  MEM_SAFE_FREE(material->texpaintslot);

  MEM_SAFE_FREE(material->gp_style);

  BKE_icon_id_delete((ID *)material);
  BKE_previewimg_free(&material->preview);
}

static void material_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Material *material = (Material *)id;
  /* Nodetrees **are owned by IDs**, treat them as mere sub-data and not real ID! */
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_library_foreach_ID_embedded(data, (ID **)&material->nodetree));
  if (material->texpaintslot != NULL) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, material->texpaintslot->ima, IDWALK_CB_NOP);
  }
  if (material->gp_style != NULL) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, material->gp_style->sima, IDWALK_CB_USER);
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, material->gp_style->ima, IDWALK_CB_USER);
  }
}

static void material_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Material *ma = (Material *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  ma->texpaintslot = NULL;
  BLI_listbase_clear(&ma->gpumaterial);

  /* write LibData */
  BLO_write_id_struct(writer, Material, id_address, &ma->id);
  BKE_id_blend_write(writer, &ma->id);

  if (ma->adt) {
    BKE_animdata_blend_write(writer, ma->adt);
  }

  /* nodetree is integral part of material, no libdata */
  if (ma->nodetree) {
    BLO_write_struct(writer, bNodeTree, ma->nodetree);
    ntreeBlendWrite(writer, ma->nodetree);
  }

  BKE_previewimg_blend_write(writer, ma->preview);

  /* grease pencil settings */
  if (ma->gp_style) {
    BLO_write_struct(writer, MaterialGPencilStyle, ma->gp_style);
  }
}

static void material_blend_read_data(BlendDataReader *reader, ID *id)
{
  Material *ma = (Material *)id;
  BLO_read_data_address(reader, &ma->adt);
  BKE_animdata_blend_read_data(reader, ma->adt);

  ma->texpaintslot = NULL;

  BLO_read_data_address(reader, &ma->preview);
  BKE_previewimg_blend_read(reader, ma->preview);

  BLI_listbase_clear(&ma->gpumaterial);

  BLO_read_data_address(reader, &ma->gp_style);
}

static void material_blend_read_lib(BlendLibReader *reader, ID *id)
{
  Material *ma = (Material *)id;
  BLO_read_id_address(reader, ma->id.lib, &ma->ipo); /* XXX deprecated - old animation system */

  /* relink grease pencil settings */
  if (ma->gp_style != NULL) {
    MaterialGPencilStyle *gp_style = ma->gp_style;
    if (gp_style->sima != NULL) {
      BLO_read_id_address(reader, ma->id.lib, &gp_style->sima);
    }
    if (gp_style->ima != NULL) {
      BLO_read_id_address(reader, ma->id.lib, &gp_style->ima);
    }
  }
}

static void material_blend_read_expand(BlendExpander *expander, ID *id)
{
  Material *ma = (Material *)id;
  BLO_expand(expander, ma->ipo); /* XXX deprecated - old animation system */

  if (ma->gp_style) {
    MaterialGPencilStyle *gp_style = ma->gp_style;
    BLO_expand(expander, gp_style->sima);
    BLO_expand(expander, gp_style->ima);
  }
}

IDTypeInfo IDType_ID_MA = {
    .id_code = ID_MA,
    .id_filter = FILTER_ID_MA,
    .main_listbase_index = INDEX_ID_MA,
    .struct_size = sizeof(Material),
    .name = "Material",
    .name_plural = "materials",
    .translation_context = BLT_I18NCONTEXT_ID_MATERIAL,
    .flags = IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = material_init_data,
    .copy_data = material_copy_data,
    .free_data = material_free_data,
    .make_local = NULL,
    .foreach_id = material_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .blend_write = material_blend_write,
    .blend_read_data = material_blend_read_data,
    .blend_read_lib = material_blend_read_lib,
    .blend_read_expand = material_blend_read_expand,

    .blend_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

void BKE_gpencil_material_attr_init(Material *ma)
{
  if ((ma) && (ma->gp_style == NULL)) {
    ma->gp_style = MEM_callocN(sizeof(MaterialGPencilStyle), "Grease Pencil Material Settings");

    MaterialGPencilStyle *gp_style = ma->gp_style;
    /* set basic settings */
    gp_style->stroke_rgba[3] = 1.0f;
    gp_style->fill_rgba[3] = 1.0f;
    ARRAY_SET_ITEMS(gp_style->mix_rgba, 1.0f, 1.0f, 1.0f, 1.0f);
    ARRAY_SET_ITEMS(gp_style->texture_scale, 1.0f, 1.0f);
    gp_style->texture_offset[0] = -0.5f;
    gp_style->texture_pixsize = 100.0f;
    gp_style->mix_factor = 0.5f;

    gp_style->flag |= GP_MATERIAL_STROKE_SHOW;
  }
}

Material *BKE_material_add(Main *bmain, const char *name)
{
  Material *ma;

  ma = BKE_id_new(bmain, ID_MA, name);

  return ma;
}

Material *BKE_gpencil_material_add(Main *bmain, const char *name)
{
  Material *ma;

  ma = BKE_material_add(bmain, name);

  /* grease pencil settings */
  if (ma != NULL) {
    BKE_gpencil_material_attr_init(ma);
  }
  return ma;
}

Material ***BKE_object_material_array_p(Object *ob)
{
  if (ob->type == OB_MESH) {
    Mesh *me = ob->data;
    return &(me->mat);
  }
  if (ELEM(ob->type, OB_CURVES_LEGACY, OB_FONT, OB_SURF)) {
    Curve *cu = ob->data;
    return &(cu->mat);
  }
  if (ob->type == OB_MBALL) {
    MetaBall *mb = ob->data;
    return &(mb->mat);
  }
  if (ob->type == OB_GPENCIL) {
    bGPdata *gpd = ob->data;
    return &(gpd->mat);
  }
  if (ob->type == OB_CURVES) {
    Curves *curves = ob->data;
    return &(curves->mat);
  }
  if (ob->type == OB_POINTCLOUD) {
    PointCloud *pointcloud = ob->data;
    return &(pointcloud->mat);
  }
  if (ob->type == OB_VOLUME) {
    Volume *volume = ob->data;
    return &(volume->mat);
  }
  return NULL;
}

short *BKE_object_material_len_p(Object *ob)
{
  if (ob->type == OB_MESH) {
    Mesh *me = ob->data;
    return &(me->totcol);
  }
  if (ELEM(ob->type, OB_CURVES_LEGACY, OB_FONT, OB_SURF)) {
    Curve *cu = ob->data;
    return &(cu->totcol);
  }
  if (ob->type == OB_MBALL) {
    MetaBall *mb = ob->data;
    return &(mb->totcol);
  }
  if (ob->type == OB_GPENCIL) {
    bGPdata *gpd = ob->data;
    return &(gpd->totcol);
  }
  if (ob->type == OB_CURVES) {
    Curves *curves = ob->data;
    return &(curves->totcol);
  }
  if (ob->type == OB_POINTCLOUD) {
    PointCloud *pointcloud = ob->data;
    return &(pointcloud->totcol);
  }
  if (ob->type == OB_VOLUME) {
    Volume *volume = ob->data;
    return &(volume->totcol);
  }
  return NULL;
}

Material ***BKE_id_material_array_p(ID *id)
{
  /* ensure we don't try get materials from non-obdata */
  BLI_assert(OB_DATA_SUPPORT_ID(GS(id->name)));

  switch (GS(id->name)) {
    case ID_ME:
      return &(((Mesh *)id)->mat);
    case ID_CU_LEGACY:
      return &(((Curve *)id)->mat);
    case ID_MB:
      return &(((MetaBall *)id)->mat);
    case ID_GD:
      return &(((bGPdata *)id)->mat);
    case ID_CV:
      return &(((Curves *)id)->mat);
    case ID_PT:
      return &(((PointCloud *)id)->mat);
    case ID_VO:
      return &(((Volume *)id)->mat);
    default:
      break;
  }
  return NULL;
}

short *BKE_id_material_len_p(ID *id)
{
  /* ensure we don't try get materials from non-obdata */
  BLI_assert(OB_DATA_SUPPORT_ID(GS(id->name)));

  switch (GS(id->name)) {
    case ID_ME:
      return &(((Mesh *)id)->totcol);
    case ID_CU_LEGACY:
      return &(((Curve *)id)->totcol);
    case ID_MB:
      return &(((MetaBall *)id)->totcol);
    case ID_GD:
      return &(((bGPdata *)id)->totcol);
    case ID_CV:
      return &(((Curves *)id)->totcol);
    case ID_PT:
      return &(((PointCloud *)id)->totcol);
    case ID_VO:
      return &(((Volume *)id)->totcol);
    default:
      break;
  }
  return NULL;
}

static void material_data_index_remove_id(ID *id, short index)
{
  /* ensure we don't try get materials from non-obdata */
  BLI_assert(OB_DATA_SUPPORT_ID(GS(id->name)));

  switch (GS(id->name)) {
    case ID_ME:
      BKE_mesh_material_index_remove((Mesh *)id, index);
      break;
    case ID_CU_LEGACY:
      BKE_curve_material_index_remove((Curve *)id, index);
      break;
    case ID_MB:
    case ID_CV:
    case ID_PT:
    case ID_VO:
      /* No material indices for these object data types. */
      break;
    default:
      break;
  }
}

bool BKE_object_material_slot_used(Object *object, short actcol)
{
  if (!BKE_object_supports_material_slots(object)) {
    return false;
  }

  LISTBASE_FOREACH (ParticleSystem *, psys, &object->particlesystem) {
    if (psys->part->omat == actcol) {
      return true;
    }
  }

  ID *ob_data = object->data;
  if (ob_data == NULL || !OB_DATA_SUPPORT_ID(GS(ob_data->name))) {
    return false;
  }

  switch (GS(ob_data->name)) {
    case ID_ME:
      return BKE_mesh_material_index_used((Mesh *)ob_data, actcol - 1);
    case ID_CU_LEGACY:
      return BKE_curve_material_index_used((Curve *)ob_data, actcol - 1);
    case ID_MB:
      /* Meta-elements don't support materials at the moment. */
      return false;
    case ID_GD:
      return BKE_gpencil_material_index_used((bGPdata *)ob_data, actcol - 1);
    default:
      return false;
  }
}

static void material_data_index_clear_id(ID *id)
{
  /* ensure we don't try get materials from non-obdata */
  BLI_assert(OB_DATA_SUPPORT_ID(GS(id->name)));

  switch (GS(id->name)) {
    case ID_ME:
      BKE_mesh_material_index_clear((Mesh *)id);
      break;
    case ID_CU_LEGACY:
      BKE_curve_material_index_clear((Curve *)id);
      break;
    case ID_MB:
    case ID_CV:
    case ID_PT:
    case ID_VO:
      /* No material indices for these object data types. */
      break;
    default:
      break;
  }
}

void BKE_id_materials_copy(Main *bmain, ID *id_src, ID *id_dst)
{
  Material ***matar_src = BKE_id_material_array_p(id_src);
  const short *materials_len_p_src = BKE_id_material_len_p(id_src);

  Material ***matar_dst = BKE_id_material_array_p(id_dst);
  short *materials_len_p_dst = BKE_id_material_len_p(id_dst);

  *materials_len_p_dst = *materials_len_p_src;
  if (*materials_len_p_src != 0) {
    (*matar_dst) = MEM_dupallocN(*matar_src);

    for (int a = 0; a < *materials_len_p_src; a++) {
      id_us_plus((ID *)(*matar_dst)[a]);
    }

    DEG_id_tag_update(id_dst, ID_RECALC_COPY_ON_WRITE);
    DEG_relations_tag_update(bmain);
  }
}

void BKE_id_material_resize(Main *bmain, ID *id, short totcol, bool do_id_user)
{
  Material ***matar = BKE_id_material_array_p(id);
  short *totcolp = BKE_id_material_len_p(id);

  if (matar == NULL) {
    return;
  }

  if (do_id_user && totcol < (*totcolp)) {
    short i;
    for (i = totcol; i < (*totcolp); i++) {
      id_us_min((ID *)(*matar)[i]);
    }
  }

  if (totcol == 0) {
    if (*totcolp) {
      MEM_freeN(*matar);
      *matar = NULL;
    }
  }
  else {
    *matar = MEM_recallocN(*matar, sizeof(void *) * totcol);
  }
  *totcolp = totcol;

  DEG_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain);
}

void BKE_id_material_append(Main *bmain, ID *id, Material *ma)
{
  Material ***matar;
  if ((matar = BKE_id_material_array_p(id))) {
    short *totcol = BKE_id_material_len_p(id);
    Material **mat = MEM_callocN(sizeof(void *) * ((*totcol) + 1), "newmatar");
    if (*totcol) {
      memcpy(mat, *matar, sizeof(void *) * (*totcol));
    }
    if (*matar) {
      MEM_freeN(*matar);
    }

    *matar = mat;
    (*matar)[(*totcol)++] = ma;

    id_us_plus((ID *)ma);
    BKE_objects_materials_test_all(bmain, id);

    DEG_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
    DEG_relations_tag_update(bmain);
  }
}

Material *BKE_id_material_pop(Main *bmain, ID *id, int index_i)
{
  short index = (short)index_i;
  Material *ret = NULL;
  Material ***matar;
  if ((matar = BKE_id_material_array_p(id))) {
    short *totcol = BKE_id_material_len_p(id);
    if (index >= 0 && index < (*totcol)) {
      ret = (*matar)[index];
      id_us_min((ID *)ret);

      if (*totcol <= 1) {
        *totcol = 0;
        MEM_freeN(*matar);
        *matar = NULL;
      }
      else {
        if (index + 1 != (*totcol)) {
          memmove((*matar) + index,
                  (*matar) + (index + 1),
                  sizeof(void *) * ((*totcol) - (index + 1)));
        }

        (*totcol)--;
        *matar = MEM_reallocN(*matar, sizeof(void *) * (*totcol));
        BKE_objects_materials_test_all(bmain, id);
      }

      material_data_index_remove_id(id, index);

      DEG_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
      DEG_relations_tag_update(bmain);
    }
  }

  return ret;
}

void BKE_id_material_clear(Main *bmain, ID *id)
{
  Material ***matar;
  if ((matar = BKE_id_material_array_p(id))) {
    short *totcol = BKE_id_material_len_p(id);

    while ((*totcol)--) {
      id_us_min((ID *)((*matar)[*totcol]));
    }
    *totcol = 0;
    if (*matar) {
      MEM_freeN(*matar);
      *matar = NULL;
    }

    BKE_objects_materials_test_all(bmain, id);
    material_data_index_clear_id(id);

    DEG_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
    DEG_relations_tag_update(bmain);
  }
}

Material **BKE_object_material_get_p(Object *ob, short act)
{
  Material ***matarar, **ma_p;
  const short *totcolp;

  if (ob == NULL) {
    return NULL;
  }

  /* if object cannot have material, (totcolp == NULL) */
  totcolp = BKE_object_material_len_p(ob);
  if (totcolp == NULL || ob->totcol == 0) {
    return NULL;
  }

  /* return NULL for invalid 'act', can happen for mesh face indices */
  if (act > ob->totcol) {
    return NULL;
  }
  if (act <= 0) {
    if (act < 0) {
      CLOG_ERROR(&LOG, "Negative material index!");
    }
    return NULL;
  }

  if (ob->matbits && ob->matbits[act - 1]) { /* in object */
    ma_p = &ob->mat[act - 1];
  }
  else { /* in data */

    /* check for inconsistency */
    if (*totcolp < ob->totcol) {
      ob->totcol = *totcolp;
    }
    if (act > ob->totcol) {
      act = ob->totcol;
    }

    matarar = BKE_object_material_array_p(ob);

    if (matarar && *matarar) {
      ma_p = &(*matarar)[act - 1];
    }
    else {
      ma_p = NULL;
    }
  }

  return ma_p;
}

Material *BKE_object_material_get(Object *ob, short act)
{
  Material **ma_p = BKE_object_material_get_p(ob, act);
  return ma_p ? *ma_p : NULL;
}

static ID *get_evaluated_object_data_with_materials(Object *ob)
{
  ID *data = ob->data;
  /* Meshes in edit mode need special handling. */
  if (ob->type == OB_MESH && ob->mode == OB_MODE_EDIT) {
    Mesh *mesh = ob->data;
    Mesh *editmesh_eval_final = BKE_object_get_editmesh_eval_final(ob);
    if (mesh->edit_mesh && editmesh_eval_final) {
      data = &editmesh_eval_final->id;
    }
  }
  return data;
}

Material *BKE_object_material_get_eval(Object *ob, short act)
{
  BLI_assert(DEG_is_evaluated_object(ob));
  const int slot_index = act - 1;

  if (slot_index < 0) {
    return NULL;
  }
  ID *data = get_evaluated_object_data_with_materials(ob);
  const short *tot_slots_data_ptr = BKE_id_material_len_p(data);
  const int tot_slots_data = tot_slots_data_ptr ? *tot_slots_data_ptr : 0;
  if (slot_index >= tot_slots_data) {
    return NULL;
  }
  const int tot_slots_object = ob->totcol;

  Material ***materials_data_ptr = BKE_id_material_array_p(data);
  Material **materials_data = materials_data_ptr ? *materials_data_ptr : NULL;
  Material **materials_object = ob->mat;

  /* Check if slot is overwritten by object. */
  if (slot_index < tot_slots_object) {
    if (ob->matbits) {
      if (ob->matbits[slot_index]) {
        Material *material = materials_object[slot_index];
        if (material != NULL) {
          return material;
        }
      }
    }
  }
  /* Otherwise use data from object-data. */
  if (slot_index < tot_slots_data) {
    Material *material = materials_data[slot_index];
    return material;
  }
  return NULL;
}

int BKE_object_material_count_eval(Object *ob)
{
  BLI_assert(DEG_is_evaluated_object(ob));
  ID *id = get_evaluated_object_data_with_materials(ob);
  const short *len_p = BKE_id_material_len_p(id);
  return len_p ? *len_p : 0;
}

void BKE_id_material_eval_assign(ID *id, int slot, Material *material)
{
  BLI_assert(slot >= 1);
  Material ***materials_ptr = BKE_id_material_array_p(id);
  short *len_ptr = BKE_id_material_len_p(id);
  if (ELEM(NULL, materials_ptr, len_ptr)) {
    BLI_assert_unreachable();
    return;
  }

  const int slot_index = slot - 1;
  const int old_length = *len_ptr;

  if (slot_index >= old_length) {
    /* Need to grow slots array. */
    const int new_length = slot_index + 1;
    *materials_ptr = MEM_reallocN(*materials_ptr, sizeof(void *) * new_length);
    *len_ptr = new_length;
    for (int i = old_length; i < new_length; i++) {
      (*materials_ptr)[i] = NULL;
    }
  }

  (*materials_ptr)[slot_index] = material;
}

void BKE_id_material_eval_ensure_default_slot(ID *id)
{
  short *len_ptr = BKE_id_material_len_p(id);
  if (len_ptr == NULL) {
    return;
  }
  if (*len_ptr == 0) {
    BKE_id_material_eval_assign(id, 1, NULL);
  }
}

Material *BKE_gpencil_material(Object *ob, short act)
{
  Material *ma = BKE_object_material_get(ob, act);
  if (ma != NULL) {
    return ma;
  }

  return BKE_material_default_gpencil();
}

MaterialGPencilStyle *BKE_gpencil_material_settings(Object *ob, short act)
{
  Material *ma = BKE_object_material_get(ob, act);
  if (ma != NULL) {
    if (ma->gp_style == NULL) {
      BKE_gpencil_material_attr_init(ma);
    }

    return ma->gp_style;
  }

  return BKE_material_default_gpencil()->gp_style;
}

void BKE_object_material_resize(Main *bmain, Object *ob, const short totcol, bool do_id_user)
{
  Material **newmatar;
  char *newmatbits;

  if (do_id_user && totcol < ob->totcol) {
    for (int i = totcol; i < ob->totcol; i++) {
      id_us_min((ID *)ob->mat[i]);
    }
  }

  if (totcol == 0) {
    if (ob->totcol) {
      MEM_freeN(ob->mat);
      MEM_freeN(ob->matbits);
      ob->mat = NULL;
      ob->matbits = NULL;
    }
  }
  else if (ob->totcol < totcol) {
    newmatar = MEM_callocN(sizeof(void *) * totcol, "newmatar");
    newmatbits = MEM_callocN(sizeof(char) * totcol, "newmatbits");
    if (ob->totcol) {
      memcpy(newmatar, ob->mat, sizeof(void *) * ob->totcol);
      memcpy(newmatbits, ob->matbits, sizeof(char) * ob->totcol);
      MEM_freeN(ob->mat);
      MEM_freeN(ob->matbits);
    }
    ob->mat = newmatar;
    ob->matbits = newmatbits;
  }
  /* XXX(campbell): why not realloc on shrink? */

  ob->totcol = totcol;
  if (ob->totcol && ob->actcol == 0) {
    ob->actcol = 1;
  }
  if (ob->actcol > ob->totcol) {
    ob->actcol = ob->totcol;
  }

  DEG_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE | ID_RECALC_GEOMETRY);
  DEG_relations_tag_update(bmain);
}

void BKE_object_materials_test(Main *bmain, Object *ob, ID *id)
{
  /* make the ob mat-array same size as 'ob->data' mat-array */
  const short *totcol;

  if (id == NULL || (totcol = BKE_id_material_len_p(id)) == NULL) {
    return;
  }

  if ((ob->id.tag & LIB_TAG_MISSING) == 0 && (id->tag & LIB_TAG_MISSING) != 0) {
    /* Exception: In case the object is a valid data, but its obdata is an empty place-holder,
     * use object's material slots amount as reference.
     * This avoids losing materials in a local object when its linked obdata goes missing.
     * See T92780. */
    BKE_id_material_resize(bmain, id, (short)ob->totcol, false);
  }
  else {
    /* Normal case: the use the obdata amount of materials slots to update the object's one. */
    BKE_object_material_resize(bmain, ob, *totcol, false);
  }
}

void BKE_objects_materials_test_all(Main *bmain, ID *id)
{
  /* make the ob mat-array same size as 'ob->data' mat-array */
  Object *ob;
  const short *totcol;

  if (id == NULL || (totcol = BKE_id_material_len_p(id)) == NULL) {
    return;
  }

  BKE_main_lock(bmain);
  for (ob = bmain->objects.first; ob; ob = ob->id.next) {
    if (ob->data == id) {
      BKE_object_material_resize(bmain, ob, *totcol, false);
    }
  }
  BKE_main_unlock(bmain);
}

void BKE_id_material_assign(Main *bmain, ID *id, Material *ma, short act)
{
  Material *mao, **matar, ***matarar;
  short *totcolp;

  if (act > MAXMAT) {
    return;
  }
  if (act < 1) {
    act = 1;
  }

  /* test arraylens */

  totcolp = BKE_id_material_len_p(id);
  matarar = BKE_id_material_array_p(id);

  if (totcolp == NULL || matarar == NULL) {
    return;
  }

  if (act > *totcolp) {
    matar = MEM_callocN(sizeof(void *) * act, "matarray1");

    if (*totcolp) {
      memcpy(matar, *matarar, sizeof(void *) * (*totcolp));
      MEM_freeN(*matarar);
    }

    *matarar = matar;
    *totcolp = act;
  }

  /* in data */
  mao = (*matarar)[act - 1];
  if (mao) {
    id_us_min(&mao->id);
  }
  (*matarar)[act - 1] = ma;

  if (ma) {
    id_us_plus(&ma->id);
  }

  BKE_objects_materials_test_all(bmain, id);
}

void BKE_object_material_assign(Main *bmain, Object *ob, Material *ma, short act, int assign_type)
{
  Material *mao, **matar, ***matarar;
  short *totcolp;
  char bit = 0;

  if (act > MAXMAT) {
    return;
  }
  if (act < 1) {
    act = 1;
  }

  /* test arraylens */

  totcolp = BKE_object_material_len_p(ob);
  matarar = BKE_object_material_array_p(ob);

  if (totcolp == NULL || matarar == NULL) {
    return;
  }

  if (act > *totcolp) {
    matar = MEM_callocN(sizeof(void *) * act, "matarray1");

    if (*totcolp) {
      memcpy(matar, *matarar, sizeof(void *) * (*totcolp));
      MEM_freeN(*matarar);
    }

    *matarar = matar;
    *totcolp = act;
  }

  if (act > ob->totcol) {
    /* Need more space in the material arrays */
    ob->mat = MEM_recallocN_id(ob->mat, sizeof(void *) * act, "matarray2");
    ob->matbits = MEM_recallocN_id(ob->matbits, sizeof(char) * act, "matbits1");
    ob->totcol = act;
  }

  /* Determine the object/mesh linking */
  if (assign_type == BKE_MAT_ASSIGN_EXISTING) {
    /* keep existing option (avoid confusion in scripts),
     * intentionally ignore userpref (default to obdata). */
    bit = ob->matbits[act - 1];
  }
  else if (assign_type == BKE_MAT_ASSIGN_USERPREF && ob->totcol && ob->actcol) {
    /* copy from previous material */
    bit = ob->matbits[ob->actcol - 1];
  }
  else {
    switch (assign_type) {
      case BKE_MAT_ASSIGN_OBDATA:
        bit = 0;
        break;
      case BKE_MAT_ASSIGN_OBJECT:
        bit = 1;
        break;
      case BKE_MAT_ASSIGN_USERPREF:
      default:
        bit = (U.flag & USER_MAT_ON_OB) ? 1 : 0;
        break;
    }
  }

  /* do it */

  ob->matbits[act - 1] = bit;
  if (bit == 1) { /* in object */
    mao = ob->mat[act - 1];
    if (mao) {
      id_us_min(&mao->id);
    }
    ob->mat[act - 1] = ma;
    BKE_object_materials_test(bmain, ob, ob->data);
  }
  else { /* in data */
    mao = (*matarar)[act - 1];
    if (mao) {
      id_us_min(&mao->id);
    }
    (*matarar)[act - 1] = ma;
    BKE_objects_materials_test_all(bmain, ob->data); /* Data may be used by several objects... */
  }

  if (ma) {
    id_us_plus(&ma->id);
  }
}

void BKE_object_material_remap(Object *ob, const unsigned int *remap)
{
  Material ***matar = BKE_object_material_array_p(ob);
  const short *totcol_p = BKE_object_material_len_p(ob);

  BLI_array_permute(ob->mat, ob->totcol, remap);

  if (ob->matbits) {
    BLI_array_permute(ob->matbits, ob->totcol, remap);
  }

  if (matar) {
    BLI_array_permute(*matar, *totcol_p, remap);
  }

  if (ob->type == OB_MESH) {
    BKE_mesh_material_remap(ob->data, remap, ob->totcol);
  }
  else if (ELEM(ob->type, OB_CURVES_LEGACY, OB_SURF, OB_FONT)) {
    BKE_curve_material_remap(ob->data, remap, ob->totcol);
  }
  else if (ob->type == OB_GPENCIL) {
    BKE_gpencil_material_remap(ob->data, remap, ob->totcol);
  }
  else {
    /* add support for this object data! */
    BLI_assert(matar == NULL);
  }
}

void BKE_object_material_remap_calc(Object *ob_dst, Object *ob_src, short *remap_src_to_dst)
{
  if (ob_src->totcol == 0) {
    return;
  }

  GHash *gh_mat_map = BLI_ghash_ptr_new_ex(__func__, ob_src->totcol);

  for (int i = 0; i < ob_dst->totcol; i++) {
    Material *ma_src = BKE_object_material_get(ob_dst, i + 1);
    BLI_ghash_reinsert(gh_mat_map, ma_src, POINTER_FROM_INT(i), NULL, NULL);
  }

  /* setup default mapping (when materials don't match) */
  {
    int i = 0;
    if (ob_dst->totcol >= ob_src->totcol) {
      for (; i < ob_src->totcol; i++) {
        remap_src_to_dst[i] = i;
      }
    }
    else {
      for (; i < ob_dst->totcol; i++) {
        remap_src_to_dst[i] = i;
      }
      for (; i < ob_src->totcol; i++) {
        remap_src_to_dst[i] = 0;
      }
    }
  }

  for (int i = 0; i < ob_src->totcol; i++) {
    Material *ma_src = BKE_object_material_get(ob_src, i + 1);

    if ((i < ob_dst->totcol) && (ma_src == BKE_object_material_get(ob_dst, i + 1))) {
      /* when objects have exact matching materials - keep existing index */
    }
    else {
      void **index_src_p = BLI_ghash_lookup_p(gh_mat_map, ma_src);
      if (index_src_p) {
        remap_src_to_dst[i] = POINTER_AS_INT(*index_src_p);
      }
    }
  }

  LIB_ghash_free(gh_mat_map, NULL, NULL);
}
