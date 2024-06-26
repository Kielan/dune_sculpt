/**
 *
 * MetaBalls are created from a single Object (with a name without number in it),
 * here the DispList and BoundBox also is located.
 * All objects with the same name (but with a number in it) are added to this.
 *
 * texture coordinates are patched within the displist
 */

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define STRUCTS_DEPRECATED_ALLOW

#include "STRUCTS_defaults.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_meta_types.h"
#include "STRUCTS_object_types.h"
#include "STRUCTS_scene_types.h"

#include "LIB_blenlib.h"
#include "LIB_math.h"
#include "LIB_string_utils.h"
#include "LIB_utildefines.h"

#include "TRANSLATION_translation.h"

#include "KERNEL_main.h"

#include "KERNEL_anim_data.h"
#include "KERNEL_curve.h"
#include "KERNEL_displist.h"
#include "KERNEL_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_material.h"
#include "BKE_mball.h"
#include "BKE_object.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"

#include "LOADER_read_write.h"

static void metaball_init_data(ID *id)
{
  MetaBall *metaball = (MetaBall *)id;

  LIB_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(metaball, id));

  MEMCPY_STRUCT_AFTER(metaball, DNA_struct_default_get(MetaBall), id);
}

static void metaball_copy_data(Main *UNUSED(bmain),
                               ID *id_dst,
                               const ID *id_src,
                               const int UNUSED(flag))
{
  MetaBall *metaball_dst = (MetaBall *)id_dst;
  const MetaBall *metaball_src = (const MetaBall *)id_src;

  LIB_duplicatelist(&metaball_dst->elems, &metaball_src->elems);

  metaball_dst->mat = MEM_dupallocN(metaball_src->mat);

  metaball_dst->editelems = NULL;
  metaball_dst->lastelem = NULL;
  metaball_dst->batch_cache = NULL;
}

static void metaball_free_data(ID *id)
{
  MetaBall *metaball = (MetaBall *)id;

  KERNEL_mball_batch_cache_free(metaball);

  MEM_SAFE_FREE(metaball->mat);

  LIB_freelistN(&metaball->elems);
  if (metaball->disp.first) {
    KERNEL_displist_free(&metaball->disp);
  }
}

static void metaball_foreach_id(ID *id, LibraryForeachIDData *data)
{
  MetaBall *metaball = (MetaBall *)id;
  for (int i = 0; i < metaball->totcol; i++) {
    KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, metaball->mat[i], IDWALK_CB_USER);
  }
}

static void metaball_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  MetaBall *mb = (MetaBall *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  LIB_listbase_clear(&mb->disp);
  mb->editelems = NULL;
  /* Must always be cleared (meta's don't have their own edit-data). */
  mb->needs_flush_to_id = 0;
  mb->lastelem = NULL;
  mb->batch_cache = NULL;

  /* write LibData */
  LOADER_write_id_struct(writer, MetaBall, id_address, &mb->id);
  KERNEL_id_blend_write(writer, &mb->id);

  /* direct data */
  LOADER_write_pointer_array(writer, mb->totcol, mb->mat);
  if (mb->adt) {
    KERNEL_animdata_blend_write(writer, mb->adt);
  }

  LISTBASE_FOREACH (MetaElem *, ml, &mb->elems) {
    LOADER_write_struct(writer, MetaElem, ml);
  }
}

static void metaball_dune_read_data(DuneDataReader *reader, ID *id)
{
  MetaBall *mb = (MetaBall *)id;
  LOADER_read_data_address(reader, &mb->adt);
  KERNEL_animdata_dune_read_data(reader, mb->adt);

  LOADER_read_pointer_array(reader, (void **)&mb->mat);

  LOADER_read_list(reader, &(mb->elems));

  LIB_listbase_clear(&mb->disp);
  mb->editelems = NULL;
  /* Must always be cleared (meta's don't have their own edit-data). */
  mb->needs_flush_to_id = 0;
  // mb->edit_elems.first = mb->edit_elems.last = NULL;
  mb->lastelem = NULL;
  mb->batch_cache = NULL;
}

static void metaball_dune_read_lib(BlendLibReader *reader, ID *id)
{
  MetaBall *mb = (MetaBall *)id;
  for (int a = 0; a < mb->totcol; a++) {
    LOADER_read_id_address(reader, mb->id.lib, &mb->mat[a]);
  }

  LOADER_read_id_address(reader, mb->id.lib, &mb->ipo);  // XXX deprecated - old animation system
}

static void metaball_dune_read_expand(DuneExpander *expander, ID *id)
{
  MetaBall *mb = (MetaBall *)id;
  for (int a = 0; a < mb->totcol; a++) {
    LOADER_expand(expander, mb->mat[a]);
  }
}

IDTypeInfo IDType_ID_MB = {
    .id_code = ID_MB,
    .id_filter = FILTER_ID_MB,
    .main_listbase_index = INDEX_ID_MB,
    .struct_size = sizeof(MetaBall),
    .name = "Metaball",
    .name_plural = "metaballs",
    .translation_context = BLT_I18NCONTEXT_ID_METABALL,
    .flags = IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = metaball_init_data,
    .copy_data = metaball_copy_data,
    .free_data = metaball_free_data,
    .make_local = NULL,
    .foreach_id = metaball_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .dune_write = metaball_dune_write,
    .dune_read_data = metaball_dune_read_data,
    .dune_read_lib = metaball_dune_read_lib,
    .dune_read_expand = metaball_dune_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* Functions */

MetaBall *KERNEL_mball_add(Main *bmain, const char *name)
{
  MetaBall *mb;

  mb = KERNEL_id_new(bmain, ID_MB, name);

  return mb;
}

MetaElem *KERNEL_mball_element_add(MetaBall *mb, const int type)
{
  MetaElem *ml = MEM_callocN(sizeof(MetaElem), "metaelem");

  unit_qt(ml->quat);

  ml->rad = 2.0;
  ml->s = 2.0;
  ml->flag = MB_SCALE_RAD;

  switch (type) {
    case MB_BALL:
      ml->type = MB_BALL;
      ml->expx = ml->expy = ml->expz = 1.0;

      break;
    case MB_TUBE:
      ml->type = MB_TUBE;
      ml->expx = ml->expy = ml->expz = 1.0;

      break;
    case MB_PLANE:
      ml->type = MB_PLANE;
      ml->expx = ml->expy = ml->expz = 1.0;

      break;
    case MB_ELIPSOID:
      ml->type = MB_ELIPSOID;
      ml->expx = 1.2f;
      ml->expy = 0.8f;
      ml->expz = 1.0;

      break;
    case MB_CUBE:
      ml->type = MB_CUBE;
      ml->expx = ml->expy = ml->expz = 1.0;

      break;
    default:
      break;
  }

  LIB_addtail(&mb->elems, ml);

  return ml;
}
void KERNEL_mball_texspace_calc(Object *ob)
{
  DispList *dl;
  BoundBox *bb;
  float *data, min[3], max[3] /*, loc[3], size[3] */;
  int tot;
  bool do_it = false;

  if (ob->runtime.bb == NULL) {
    ob->runtime.bb = MEM_callocN(sizeof(BoundBox), "mb boundbox");
  }
  bb = ob->runtime.bb;

  /* Weird one, this. */
  // INIT_MINMAX(min, max);
  (min)[0] = (min)[1] = (min)[2] = 1.0e30f;
  (max)[0] = (max)[1] = (max)[2] = -1.0e30f;

  dl = ob->runtime.curve_cache->disp.first;
  while (dl) {
    tot = dl->nr;
    if (tot) {
      do_it = true;
    }
    data = dl->verts;
    while (tot--) {
      /* Also weird... but longer. From utildefines. */
      minmax_v3v3_v3(min, max, data);
      data += 3;
    }
    dl = dl->next;
  }

  if (!do_it) {
    min[0] = min[1] = min[2] = -1.0f;
    max[0] = max[1] = max[2] = 1.0f;
  }

  KERNEL_boundbox_init_from_minmax(bb, min, max);

  bb->flag &= ~BOUNDBOX_DIRTY;
}

BoundBox *KERNEL_mball_boundbox_get(Object *ob)
{
  LIB_assert(ob->type == OB_MBALL);

  if (ob->runtime.bb != NULL && (ob->runtime.bb->flag & BOUNDBOX_DIRTY) == 0) {
    return ob->runtime.bb;
  }

  /* This should always only be called with evaluated objects,
   * but currently RNA is a problem here... */
  if (ob->runtime.curve_cache != NULL) {
    KERNEL_mball_texspace_calc(ob);
  }

  return ob->runtime.bb;
}

float *KERNEL_mball_make_orco(Object *ob, ListBase *dispbase)
{
  BoundBox *bb;
  DispList *dl;
  float *data, *orco, *orcodata;
  float loc[3], size[3];
  int a;

  /* restore size and loc */
  bb = ob->runtime.bb;
  loc[0] = (bb->vec[0][0] + bb->vec[4][0]) / 2.0f;
  size[0] = bb->vec[4][0] - loc[0];
  loc[1] = (bb->vec[0][1] + bb->vec[2][1]) / 2.0f;
  size[1] = bb->vec[2][1] - loc[1];
  loc[2] = (bb->vec[0][2] + bb->vec[1][2]) / 2.0f;
  size[2] = bb->vec[1][2] - loc[2];

  dl = dispbase->first;
  orcodata = MEM_mallocN(sizeof(float[3]) * dl->nr, "MballOrco");

  data = dl->verts;
  orco = orcodata;
  a = dl->nr;
  while (a--) {
    orco[0] = (data[0] - loc[0]) / size[0];
    orco[1] = (data[1] - loc[1]) / size[1];
    orco[2] = (data[2] - loc[2]) / size[2];

    data += 3;
    orco += 3;
  }

  return orcodata;
}

bool KERNEL_mball_is_basis(Object *ob)
{
  /* Meta-Ball Basis Notes from Blender-2.5x
   * =======================================
   *
   * NOTE(@campbellbarton): This is a can of worms.
   *
   * This really needs a rewrite/refactor its totally broken in anything other than basic cases
   * Multiple Scenes + Set Scenes & mixing meta-ball basis _should_ work but fails to update the
   * depsgraph on rename and linking into scenes or removal of basis meta-ball.
   * So take care when changing this code.
   *
   * Main idiot thing here is that the system returns #BKE_mball_basis_find()
   * objects which fail a #BKE_mball_is_basis() test.
   *
   * Not only that but the depsgraph and their areas depend on this behavior,
   * so making small fixes here isn't worth it. */

  /* Just a quick test. */
  const int len = strlen(ob->id.name);
  return (!isdigit(ob->id.name[len - 1]));
}

bool KERNEL_mball_is_basis_for(Object *ob1, Object *ob2)
{
  int basis1nr, basis2nr;
  char basis1name[MAX_ID_NAME], basis2name[MAX_ID_NAME];

  if (ob1->id.name[2] != ob2->id.name[2]) {
    /* Quick return in case first char of both ID's names is not the same... */
    return false;
  }

  LIB_split_name_num(basis1name, &basis1nr, ob1->id.name + 2, '.');
  LIB_split_name_num(basis2name, &basis2nr, ob2->id.name + 2, '.');

  if (STREQ(basis1name, basis2name)) {
    return KERNEL_mball_is_basis(ob1);
  }

  return false;
}

bool KERNEL_mball_is_any_selected(const MetaBall *mb)
{
  for (const MetaElem *ml = mb->editelems->first; ml != NULL; ml = ml->next) {
    if (ml->flag & SELECT) {
      return true;
    }
  }
  return false;
}

bool KERNEL_mball_is_any_selected_multi(Base **bases, int bases_len)
{
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Object *obedit = bases[base_index]->object;
    MetaBall *mb = (MetaBall *)obedit->data;
    if (KERNEL_mball_is_any_selected(mb)) {
      return true;
    }
  }
  return false;
}

bool KERNEL_mball_is_any_unselected(const MetaBall *mb)
{
  for (const MetaElem *ml = mb->editelems->first; ml != NULL; ml = ml->next) {
    if ((ml->flag & SELECT) == 0) {
      return true;
    }
  }
  return false;
}

void KERNEL_mball_properties_copy(Scene *scene, Object *active_object)
{
  Scene *sce_iter = scene;
  Base *base;
  Object *ob;
  MetaBall *active_mball = (MetaBall *)active_object->data;
  int basisnr, obnr;
  char basisname[MAX_ID_NAME], obname[MAX_ID_NAME];
  SceneBaseIter iter;

  LIB_split_name_num(basisname, &basisnr, active_object->id.name + 2, '.');

  /* Pass depsgraph as NULL, which means we will not expand into
   * duplis unlike when we generate the meta-ball. Expanding duplis
   * would not be compatible when editing multiple view layers. */
  KERNEL_scene_base_iter_next(NULL, &iter, &sce_iter, 0, NULL, NULL);
  while (KERNEL_scene_base_iter_next(NULL, &iter, &sce_iter, 1, &base, &ob)) {
    if (ob->type == OB_MBALL) {
      if (ob != active_object) {
        LIB_split_name_num(obname, &obnr, ob->id.name + 2, '.');

        /* Object ob has to be in same "group" ... it means, that it has to have
         * same base of its name */
        if (STREQ(obname, basisname)) {
          MetaBall *mb = ob->data;

          /* Copy properties from selected/edited metaball */
          mb->wiresize = active_mball->wiresize;
          mb->rendersize = active_mball->rendersize;
          mb->thresh = active_mball->thresh;
          mb->flag = active_mball->flag;
          DEG_id_tag_update(&mb->id, 0);
        }
      }
    }
  }
}

Object *KERNEL_mball_basis_find(Scene *scene, Object *object)
{
  Object *bob = object;
  int basisnr, obnr;
  char basisname[MAX_ID_NAME], obname[MAX_ID_NAME];

  LIB_split_name_num(basisname, &basisnr, object->id.name + 2, '.');

  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
      Object *ob = base->object;
      if ((ob->type == OB_MBALL) && !(base->flag & BASE_FROM_DUPLI)) {
        if (ob != bob) {
          LIB_split_name_num(obname, &obnr, ob->id.name + 2, '.');

          /* Object ob has to be in same "group" ... it means,
           * that it has to have same base of its name. */
          if (STREQ(obname, basisname)) {
            if (obnr < basisnr) {
              object = ob;
              basisnr = obnr;
            }
          }
        }
      }
    }
  }

  return object;
}

bool KERNEL_mball_minmax_ex(
    const MetaBall *mb, float min[3], float max[3], const float obmat[4][4], const short flag)
{
  const float scale = obmat ? mat4_to_scale(obmat) : 1.0f;
  bool changed = false;
  float centroid[3], vec[3];

  INIT_MINMAX(min, max);

  LISTBASE_FOREACH (const MetaElem *, ml, &mb->elems) {
    if ((ml->flag & flag) == flag) {
      const float scale_mb = (ml->rad * 0.5f) * scale;

      if (obmat) {
        mul_v3_m4v3(centroid, obmat, &ml->x);
      }
      else {
        copy_v3_v3(centroid, &ml->x);
      }

      /* TODO(campbell): non circle shapes cubes etc, probably nobody notices. */
      for (int i = -1; i != 3; i += 2) {
        copy_v3_v3(vec, centroid);
        add_v3_fl(vec, scale_mb * i);
        minmax_v3v3_v3(min, max, vec);
      }
      changed = true;
    }
  }

  return changed;
}

bool KERNEL_mball_minmax(const MetaBall *mb, float min[3], float max[3])
{
  INIT_MINMAX(min, max);

  LISTBASE_FOREACH (const MetaElem *, ml, &mb->elems) {
    minmax_v3v3_v3(min, max, &ml->x);
  }

  return (LIB_listbase_is_empty(&mb->elems) == false);
}

bool KERNEL_mball_center_median(const MetaBall *mb, float r_cent[3])
{
  int total = 0;

  zero_v3(r_cent);

  LISTBASE_FOREACH (const MetaElem *, ml, &mb->elems) {
    add_v3_v3(r_cent, &ml->x);
    total++;
  }

  if (total) {
    mul_v3_fl(r_cent, 1.0f / (float)total);
  }

  return (total != 0);
}

bool KERNEL_mball_center_bounds(const MetaBall *mb, float r_cent[3])
{
  float min[3], max[3];

  if (KERNEL_mball_minmax(mb, min, max)) {
    mid_v3_v3v3(r_cent, min, max);
    return true;
  }

  return false;
}

void KERNEL_mball_transform(MetaBall *mb, const float mat[4][4], const bool do_props)
{
  float quat[4];
  const float scale = mat4_to_scale(mat);
  const float scale_sqrt = sqrtf(scale);

  mat4_to_quat(quat, mat);

  LISTBASE_FOREACH (MetaElem *, ml, &mb->elems) {
    mul_m4_v3(mat, &ml->x);
    mul_qt_qtqt(ml->quat, quat, ml->quat);

    if (do_props) {
      ml->rad *= scale;
      /* hrmf, probably elems shouldn't be
       * treating scale differently - campbell */
      if (!MB_TYPE_SIZE_SQUARED(ml->type)) {
        mul_v3_fl(&ml->expx, scale);
      }
      else {
        mul_v3_fl(&ml->expx, scale_sqrt);
      }
    }
  }
}

void KERNEL_mball_translate(MetaBall *mb, const float offset[3])
{
  LISTBASE_FOREACH (MetaElem *, ml, &mb->elems) {
    add_v3_v3(&ml->x, offset);
  }
}

int KERNEL_mball_select_count(const MetaBall *mb)
{
  int sel = 0;
  LISTBASE_FOREACH (const MetaElem *, ml, mb->editelems) {
    if (ml->flag & SELECT) {
      sel++;
    }
  }
  return sel;
}

int KERNEL_mball_select_count_multi(Base **bases, int bases_len)
{
  int sel = 0;
  for (uint ob_index = 0; ob_index < bases_len; ob_index++) {
    const Object *obedit = bases[ob_index]->object;
    const MetaBall *mb = (MetaBall *)obedit->data;
    sel += KERNEL_mball_select_count(mb);
  }
  return sel;
}

bool KERNEL_mball_select_all(MetaBall *mb)
{
  bool changed = false;
  LISTBASE_FOREACH (MetaElem *, ml, mb->editelems) {
    if ((ml->flag & SELECT) == 0) {
      ml->flag |= SELECT;
      changed = true;
    }
  }
  return changed;
}

bool KERNEL_mball_select_all_multi_ex(Base **bases, int bases_len)
{
  bool changed_multi = false;
  for (uint ob_index = 0; ob_index < bases_len; ob_index++) {
    Object *obedit = bases[ob_index]->object;
    MetaBall *mb = obedit->data;
    changed_multi |= KERNEL_mball_select_all(mb);
  }
  return changed_multi;
}

bool KERNEL_mball_deselect_all(MetaBall *mb)
{
  bool changed = false;
  LISTBASE_FOREACH (MetaElem *, ml, mb->editelems) {
    if ((ml->flag & SELECT) != 0) {
      ml->flag &= ~SELECT;
      changed = true;
    }
  }
  return changed;
}

bool KERNEL_mball_deselect_all_multi_ex(Base **bases, int bases_len)
{
  bool changed_multi = false;
  for (uint ob_index = 0; ob_index < bases_len; ob_index++) {
    Object *obedit = bases[ob_index]->object;
    MetaBall *mb = obedit->data;
    changed_multi |= KERNEL_mball_deselect_all(mb);
    DEG_id_tag_update(&mb->id, ID_RECALC_SELECT);
  }
  return changed_multi;
}

bool KERNEL_mball_select_swap(MetaBall *mb)
{
  bool changed = false;
  LISTBASE_FOREACH (MetaElem *, ml, mb->editelems) {
    ml->flag ^= SELECT;
    changed = true;
  }
  return changed;
}

bool KERNEL_mball_select_swap_multi_ex(Base **bases, int bases_len)
{
  bool changed_multi = false;
  for (uint ob_index = 0; ob_index < bases_len; ob_index++) {
    Object *obedit = bases[ob_index]->object;
    MetaBall *mb = (MetaBall *)obedit->data;
    changed_multi |= KERNEL_mball_select_swap(mb);
  }
  return changed_multi;
}

/* **** Depsgraph evaluation **** */

/* Draw Engine */

void (*KERNEL_mball_batch_cache_dirty_tag_cb)(MetaBall *mb, int mode) = NULL;
void (*KERNEL_mball_batch_cache_free_cb)(MetaBall *mb) = NULL;

void KERNEL_mball_batch_cache_dirty_tag(MetaBall *mb, int mode)
{
  if (mb->batch_cache) {
    KERNEL_mball_batch_cache_dirty_tag_cb(mb, mode);
  }
}
void KERNEL_mball_batch_cache_free(MetaBall *mb)
{
  if (mb->batch_cache) {
    KERNEL_mball_batch_cache_free_cb(mb);
  }
}
