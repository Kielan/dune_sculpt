#include <cmath> /* floor */
#include <cstdlib>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_endian_switch.h"
#include "BLI_ghash.h"
#include "BLI_index_range.hh"
#include "BLI_math.h"
#include "BLI_math_vec_types.hh"
#include "BLI_string.h"
#include "BLI_utildefines.h"
#include "BLT_translation.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_anim_types.h"
#include "DNA_curve_types.h"
#include "DNA_defaults.h"
#include "DNA_material_types.h"

/* For dereferencing pointers. */
#include "DNA_key_types.h"
#include "DNA_object_types.h"
#include "DNA_vfont_types.h"

#include "BKE_anim_data.h"
#include "BKE_curve.h"
#include "BKE_curveprofile.h"
#include "BKE_displist.h"
#include "BKE_idtype.h"
#include "BKE_key.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_spline.hh"
#include "BKE_vfont.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "CLG_log.h"

#include "BLO_read_write.h"

using blender::float3;
using blender::IndexRange;

/* globals */

/* local */
// static CLG_LogRef LOG = {"bke.curve"};

enum class NURBSValidationStatus {
  Valid,
  AtLeastTwoPointsRequired,
  MorePointsThanOrderRequired,
  MoreRowsForBezierRequired,
  MorePointsForBezierRequired
};

static void curve_init_data(ID *id)
{
  Curve *curve = (Curve *)id;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(curve, id));

  MEMCPY_STRUCT_AFTER(curve, DNA_struct_default_get(Curve), id);
}

static void curve_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Curve *curve_dst = (Curve *)id_dst;
  const Curve *curve_src = (const Curve *)id_src;

  BLI_listbase_clear(&curve_dst->nurb);
  BKE_nurbList_duplicate(&(curve_dst->nurb), &(curve_src->nurb));

  curve_dst->mat = (Material **)MEM_dupallocN(curve_src->mat);

  curve_dst->str = (char *)MEM_dupallocN(curve_src->str);
  curve_dst->strinfo = (CharInfo *)MEM_dupallocN(curve_src->strinfo);
  curve_dst->tb = (TextBox *)MEM_dupallocN(curve_src->tb);
  curve_dst->batch_cache = nullptr;

  curve_dst->bevel_profile = BKE_curveprofile_copy(curve_src->bevel_profile);

  if (curve_src->key && (flag & LIB_ID_COPY_SHAPEKEY)) {
    BKE_id_copy_ex(bmain, &curve_src->key->id, (ID **)&curve_dst->key, flag);
    /* XXX This is not nice, we need to make BKE_id_copy_ex fully re-entrant... */
    curve_dst->key->from = &curve_dst->id;
  }

  curve_dst->editnurb = nullptr;
  curve_dst->editfont = nullptr;
}

static void curve_free_data(ID *id)
{
  Curve *curve = (Curve *)id;

  BKE_curve_batch_cache_free(curve);

  BKE_nurbList_free(&curve->nurb);
  BKE_curve_editfont_free(curve);

  BKE_curve_editNurb_free(curve);

  BKE_curveprofile_free(curve->bevel_profile);

  MEM_SAFE_FREE(curve->mat);
  MEM_SAFE_FREE(curve->str);
  MEM_SAFE_FREE(curve->strinfo);
  MEM_SAFE_FREE(curve->tb);

  delete curve->curve_eval;
}

static void curve_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Curve *curve = (Curve *)id;
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->bevobj, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->taperobj, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->textoncurve, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->key, IDWALK_CB_USER);
  for (int i = 0; i < curve->totcol; i++) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->mat[i], IDWALK_CB_USER);
  }
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->vfont, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->vfontb, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->vfonti, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, curve->vfontbi, IDWALK_CB_USER);
}

static void curve_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Curve *cu = (Curve *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  cu->editnurb = nullptr;
  cu->editfont = nullptr;
  cu->batch_cache = nullptr;

  /* write LibData */
  BLO_write_id_struct(writer, Curve, id_address, &cu->id);
  BKE_id_blend_write(writer, &cu->id);

  /* direct data */
  BLO_write_pointer_array(writer, cu->totcol, cu->mat);
  if (cu->adt) {
    BKE_animdata_blend_write(writer, cu->adt);
  }

  if (cu->vfont) {
    BLO_write_raw(writer, cu->len + 1, cu->str);
    BLO_write_struct_array(writer, CharInfo, cu->len_char32 + 1, cu->strinfo);
    BLO_write_struct_array(writer, TextBox, cu->totbox, cu->tb);
  }
  else {
    /* is also the order of reading */
    LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
      BLO_write_struct(writer, Nurb, nu);
    }
    LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
      if (nu->type == CU_BEZIER) {
        BLO_write_struct_array(writer, BezTriple, nu->pntsu, nu->bezt);
      }
      else {
        BLO_write_struct_array(writer, BPoint, nu->pntsu * nu->pntsv, nu->bp);
        if (nu->knotsu) {
          BLO_write_float_array(writer, KNOTSU(nu), nu->knotsu);
        }
        if (nu->knotsv) {
          BLO_write_float_array(writer, KNOTSV(nu), nu->knotsv);
        }
      }
    }
  }

  if (cu->bevel_profile != nullptr) {
    BKE_curveprofile_blend_write(writer, cu->bevel_profile);
  }
}

static void switch_endian_knots(Nurb *nu)
{
  if (nu->knotsu) {
    BLI_endian_switch_float_array(nu->knotsu, KNOTSU(nu));
  }
  if (nu->knotsv) {
    BLI_endian_switch_float_array(nu->knotsv, KNOTSV(nu));
  }
}

static void curve_blend_read_data(BlendDataReader *reader, ID *id)
{
  Curve *cu = (Curve *)id;
  BLO_read_data_address(reader, &cu->adt);
  BKE_animdata_blend_read_data(reader, cu->adt);

  /* Protect against integer overflow vulnerability. */
  CLAMP(cu->len_char32, 0, INT_MAX - 4);

  BLO_read_pointer_array(reader, (void **)&cu->mat);

  BLO_read_data_address(reader, &cu->str);
  BLO_read_data_address(reader, &cu->strinfo);
  BLO_read_data_address(reader, &cu->tb);

  if (cu->vfont == nullptr) {
    BLO_read_list(reader, &(cu->nurb));
  }
  else {
    cu->nurb.first = cu->nurb.last = nullptr;

    TextBox *tb = (TextBox *)MEM_calloc_arrayN(MAXTEXTBOX, sizeof(TextBox), "TextBoxread");
    if (cu->tb) {
      memcpy(tb, cu->tb, cu->totbox * sizeof(TextBox));
      MEM_freeN(cu->tb);
      cu->tb = tb;
    }
    else {
      cu->totbox = 1;
      cu->actbox = 1;
      cu->tb = tb;
      cu->tb[0].w = cu->linewidth;
    }
    if (cu->wordspace == 0.0f) {
      cu->wordspace = 1.0f;
    }
  }

  cu->editnurb = nullptr;
  cu->editfont = nullptr;
  cu->batch_cache = nullptr;

  LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
    BLO_read_data_address(reader, &nu->bezt);
    BLO_read_data_address(reader, &nu->bp);
    BLO_read_data_address(reader, &nu->knotsu);
    BLO_read_data_address(reader, &nu->knotsv);
    if (cu->vfont == nullptr) {
      nu->charidx = 0;
    }

    if (BLO_read_requires_endian_switch(reader)) {
      switch_endian_knots(nu);
    }
  }
  cu->texflag &= ~CU_AUTOSPACE_EVALUATED;

  BLO_read_data_address(reader, &cu->bevel_profile);
  if (cu->bevel_profile != nullptr) {
    BKE_curveprofile_blend_read(reader, cu->bevel_profile);
  }
}

static void curve_blend_read_lib(BlendLibReader *reader, ID *id)
{
  Curve *cu = (Curve *)id;
  for (int a = 0; a < cu->totcol; a++) {
    BLO_read_id_address(reader, cu->id.lib, &cu->mat[a]);
  }

  BLO_read_id_address(reader, cu->id.lib, &cu->bevobj);
  BLO_read_id_address(reader, cu->id.lib, &cu->taperobj);
  BLO_read_id_address(reader, cu->id.lib, &cu->textoncurve);
  BLO_read_id_address(reader, cu->id.lib, &cu->vfont);
  BLO_read_id_address(reader, cu->id.lib, &cu->vfontb);
  BLO_read_id_address(reader, cu->id.lib, &cu->vfonti);
  BLO_read_id_address(reader, cu->id.lib, &cu->vfontbi);

  BLO_read_id_address(reader, cu->id.lib, &cu->ipo); /* XXX deprecated - old animation system */
  BLO_read_id_address(reader, cu->id.lib, &cu->key);
}

static void curve_blend_read_expand(BlendExpander *expander, ID *id)
{
  Curve *cu = (Curve *)id;
  for (int a = 0; a < cu->totcol; a++) {
    BLO_expand(expander, cu->mat[a]);
  }

  BLO_expand(expander, cu->vfont);
  BLO_expand(expander, cu->vfontb);
  BLO_expand(expander, cu->vfonti);
  BLO_expand(expander, cu->vfontbi);
  BLO_expand(expander, cu->key);
  BLO_expand(expander, cu->ipo); /* XXX deprecated - old animation system */
  BLO_expand(expander, cu->bevobj);
  BLO_expand(expander, cu->taperobj);
  BLO_expand(expander, cu->textoncurve);
}

IDTypeInfo IDType_ID_CU_LEGACY = {
    /* id_code */ ID_CU_LEGACY,
    /* id_filter */ FILTER_ID_CU_LEGACY,
    /* main_listbase_index */ INDEX_ID_CU_LEGACY,
    /* struct_size */ sizeof(Curve),
    /* name */ "Curve",
    /* name_plural */ "curves",
    /* translation_context */ BLT_I18NCONTEXT_ID_CURVE_LEGACY,
    /* flags */ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /* asset_type_info */ nullptr,

    /* init_data */ curve_init_data,
    /* copy_data */ curve_copy_data,
    /* free_data */ curve_free_data,
    /* make_local */ nullptr,
    /* foreach_id */ curve_foreach_id,
    /* foreach_cache */ nullptr,
    /* foreach_path */ nullptr,
    /* owner_get */ nullptr,

    /* blend_write */ curve_blend_write,
    /* blend_read_data */ curve_blend_read_data,
    /* blend_read_lib */ curve_blend_read_lib,
    /* blend_read_expand */ curve_blend_read_expand,

    /* blend_read_undo_preserve */ nullptr,

    /* lib_override_apply_post */ nullptr,
};

void BKE_curve_editfont_free(Curve *cu)
{
  if (cu->editfont) {
    EditFont *ef = cu->editfont;

    if (ef->textbuf) {
      MEM_freeN(ef->textbuf);
    }
    if (ef->textbufinfo) {
      MEM_freeN(ef->textbufinfo);
    }
    if (ef->selboxes) {
      MEM_freeN(ef->selboxes);
    }

    MEM_freeN(ef);
    cu->editfont = nullptr;
  }
}

static void curve_editNurb_keyIndex_cv_free_cb(void *val)
{
  CVKeyIndex *index = (CVKeyIndex *)val;
  MEM_freeN(index->orig_cv);
  MEM_freeN(val);
}

void BKE_curve_editNurb_keyIndex_delCV(GHash *keyindex, const void *cv)
{
  BLI_assert(keyindex != nullptr);
  BLI_ghash_remove(keyindex, cv, nullptr, curve_editNurb_keyIndex_cv_free_cb);
}

void BKE_curve_editNurb_keyIndex_free(GHash **keyindex)
{
  if (!(*keyindex)) {
    return;
  }
  BLI_ghash_free(*keyindex, nullptr, curve_editNurb_keyIndex_cv_free_cb);
  *keyindex = nullptr;
}

void BKE_curve_editNurb_free(Curve *cu)
{
  if (cu->editnurb) {
    BKE_nurbList_free(&cu->editnurb->nurbs);
    BKE_curve_editNurb_keyIndex_free(&cu->editnurb->keyindex);
    MEM_freeN(cu->editnurb);
    cu->editnurb = nullptr;
  }
}

void BKE_curve_init(Curve *cu, const short curve_type)
{
  curve_init_data(&cu->id);

  cu->type = curve_type;

  if (cu->type == OB_FONT) {
    cu->flag |= CU_FRONT | CU_BACK;
    cu->vfont = cu->vfontb = cu->vfonti = cu->vfontbi = BKE_vfont_builtin_get();
    cu->vfont->id.us += 4;
    cu->str = (char *)MEM_malloc_arrayN(12, sizeof(unsigned char), "str");
    BLI_strncpy(cu->str, "Text", 12);
    cu->len = cu->len_char32 = cu->pos = 4;
    cu->strinfo = (CharInfo *)MEM_calloc_arrayN(12, sizeof(CharInfo), "strinfo new");
    cu->totbox = cu->actbox = 1;
    cu->tb = (TextBox *)MEM_calloc_arrayN(MAXTEXTBOX, sizeof(TextBox), "textbox");
    cu->tb[0].w = cu->tb[0].h = 0.0;
  }
  else if (cu->type == OB_SURF) {
    cu->flag |= CU_3D;
    cu->resolu = 4;
    cu->resolv = 4;
  }
  cu->bevel_profile = nullptr;
}

Curve *BKE_curve_add(Main *bmain, const char *name, int type)
{
  Curve *cu;

  /* We cannot use #BKE_id_new here as we need some custom initialization code. */
  cu = (Curve *)BKE_libblock_alloc(bmain, ID_CU_LEGACY, name, 0);

  BKE_curve_init(cu, type);

  return cu;
}

ListBase *BKE_curve_editNurbs_get(Curve *cu)
{
  if (cu->editnurb) {
    return &cu->editnurb->nurbs;
  }

  return nullptr;
}

const ListBase *BKE_curve_editNurbs_get_for_read(const Curve *cu)
{
  if (cu->editnurb) {
    return &cu->editnurb->nurbs;
  }

  return nullptr;
}

short BKE_curve_type_get(const Curve *cu)
{
  int type = cu->type;

  if (cu->vfont) {
    return OB_FONT;
  }

  if (!cu->type) {
    type = OB_CURVES_LEGACY;

    LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
      if (nu->pntsv > 1) {
        type = OB_SURF;
      }
    }
  }

  return type;
}

void BKE_curve_dimension_update(Curve *cu)
{
  ListBase *nurbs = BKE_curve_nurbs_get(cu);
  bool is_2d = CU_IS_2D(cu);

  LISTBASE_FOREACH (Nurb *, nu, nurbs) {
    if (is_2d) {
      BKE_nurb_project_2d(nu);
    }

    /* since the handles are moved they need to be auto-located again */
    if (nu->type == CU_BEZIER) {
      BKE_nurb_handles_calc(nu);
    }
  }
}

void BKE_curve_type_test(Object *ob)
{
  ob->type = BKE_curve_type_get((Curve *)ob->data);

  if (ob->type == OB_CURVES_LEGACY) {
    Curve *cu = (Curve *)ob->data;
    if (CU_IS_2D(cu)) {
      BKE_curve_dimension_update(cu);
    }
  }
}

BoundBox *BKE_curve_boundbox_get(Object *ob)
{
  /* This is Object-level data access,
   * DO NOT touch to Mesh's bb, would be totally thread-unsafe. */
  if (ob->runtime.bb == nullptr || ob->runtime.bb->flag & BOUNDBOX_DIRTY) {
    Curve *cu = (Curve *)ob->data;
    float min[3], max[3];

    INIT_MINMAX(min, max);
    if (!BKE_curve_minmax(cu, true, min, max)) {
      copy_v3_fl(min, -1.0f);
      copy_v3_fl(max, 1.0f);
    }

    if (ob->runtime.bb == nullptr) {
      ob->runtime.bb = (BoundBox *)MEM_mallocN(sizeof(*ob->runtime.bb), __func__);
    }
    BKE_boundbox_init_from_minmax(ob->runtime.bb, min, max);
    ob->runtime.bb->flag &= ~BOUNDBOX_DIRTY;
  }

  return ob->runtime.bb;
}

void BKE_curve_texspace_calc(Curve *cu)
{
  if (cu->texflag & CU_AUTOSPACE) {
    float min[3], max[3];

    INIT_MINMAX(min, max);
    if (!BKE_curve_minmax(cu, true, min, max)) {
      min[0] = min[1] = min[2] = -1.0f;
      max[0] = max[1] = max[2] = 1.0f;
    }

    float loc[3], size[3];
    mid_v3_v3v3(loc, min, max);

    size[0] = (max[0] - min[0]) / 2.0f;
    size[1] = (max[1] - min[1]) / 2.0f;
    size[2] = (max[2] - min[2]) / 2.0f;

    for (int a = 0; a < 3; a++) {
      if (size[a] == 0.0f) {
        size[a] = 1.0f;
      }
      else if (size[a] > 0.0f && size[a] < 0.00001f) {
        size[a] = 0.00001f;
      }
      else if (size[a] < 0.0f && size[a] > -0.00001f) {
        size[a] = -0.00001f;
      }
    }

    copy_v3_v3(cu->loc, loc);
    copy_v3_v3(cu->size, size);

    cu->texflag |= CU_AUTOSPACE_EVALUATED;
  }
}

void BKE_curve_texspace_ensure(Curve *cu)
{
  if ((cu->texflag & CU_AUTOSPACE) && !(cu->texflag & CU_AUTOSPACE_EVALUATED)) {
    BKE_curve_texspace_calc(cu);
  }
}

bool BKE_nurbList_index_get_co(ListBase *nurb, const int index, float r_co[3])
{
  int tot = 0;

  LISTBASE_FOREACH (Nurb *, nu, nurb) {
    int tot_nu;
    if (nu->type == CU_BEZIER) {
      tot_nu = nu->pntsu;
      if (index - tot < tot_nu) {
        copy_v3_v3(r_co, nu->bezt[index - tot].vec[1]);
        return true;
      }
    }
    else {
      tot_nu = nu->pntsu * nu->pntsv;
      if (index - tot < tot_nu) {
        copy_v3_v3(r_co, nu->bp[index - tot].vec);
        return true;
      }
    }
    tot += tot_nu;
  }

  return false;
}

int BKE_nurbList_verts_count(const ListBase *nurb)
{
  int tot = 0;

  LISTBASE_FOREACH (const Nurb *, nu, nurb) {
    if (nu->bezt) {
      tot += 3 * nu->pntsu;
    }
    else if (nu->bp) {
      tot += nu->pntsu * nu->pntsv;
    }
  }

  return tot;
}

int BKE_nurbList_verts_count_without_handles(const ListBase *nurb)
{
  int tot = 0;

  LISTBASE_FOREACH (Nurb *, nu, nurb) {
    if (nu->bezt) {
      tot += nu->pntsu;
    }
    else if (nu->bp) {
      tot += nu->pntsu * nu->pntsv;
    }
  }

  return tot;
}

/* **************** NURBS ROUTINES ******************** */

void BKE_nurb_free(Nurb *nu)
{
  if (nu == nullptr) {
    return;
  }

  if (nu->bezt) {
    MEM_freeN(nu->bezt);
  }
  nu->bezt = nullptr;
  if (nu->bp) {
    MEM_freeN(nu->bp);
  }
  nu->bp = nullptr;
  if (nu->knotsu) {
    MEM_freeN(nu->knotsu);
  }
  nu->knotsu = nullptr;
  if (nu->knotsv) {
    MEM_freeN(nu->knotsv);
  }
  nu->knotsv = nullptr;
  // if (nu->trim.first) freeNurblist(&(nu->trim));

  MEM_freeN(nu);
}

void BKE_nurbList_free(ListBase *lb)
{
  if (lb == nullptr) {
    return;
  }

  LISTBASE_FOREACH_MUTABLE (Nurb *, nu, lb) {
    BKE_nurb_free(nu);
  }
  BLI_listbase_clear(lb);
}

Nurb *BKE_nurb_duplicate(const Nurb *nu)
{
  Nurb *newnu;
  int len;

  newnu = (Nurb *)MEM_mallocN(sizeof(Nurb), "duplicateNurb");
  if (newnu == nullptr) {
    return nullptr;
  }
  memcpy(newnu, nu, sizeof(Nurb));

  if (nu->bezt) {
    newnu->bezt = (BezTriple *)MEM_malloc_arrayN(nu->pntsu, sizeof(BezTriple), "duplicateNurb2");
    memcpy(newnu->bezt, nu->bezt, nu->pntsu * sizeof(BezTriple));
  }
  else {
    len = nu->pntsu * nu->pntsv;
    newnu->bp = (BPoint *)MEM_malloc_arrayN(len, sizeof(BPoint), "duplicateNurb3");
    memcpy(newnu->bp, nu->bp, len * sizeof(BPoint));

    newnu->knotsu = newnu->knotsv = nullptr;

    if (nu->knotsu) {
      len = KNOTSU(nu);
      if (len) {
        newnu->knotsu = (float *)MEM_malloc_arrayN(len, sizeof(float), "duplicateNurb4");
        memcpy(newnu->knotsu, nu->knotsu, sizeof(float) * len);
      }
    }
    if (nu->pntsv > 1 && nu->knotsv) {
      len = KNOTSV(nu);
      if (len) {
        newnu->knotsv = (float *)MEM_malloc_arrayN(len, sizeof(float), "duplicateNurb5");
        memcpy(newnu->knotsv, nu->knotsv, sizeof(float) * len);
      }
    }
  }
  return newnu;
}

Nurb *BKE_nurb_copy(Nurb *src, int pntsu, int pntsv)
{
  Nurb *newnu = (Nurb *)MEM_mallocN(sizeof(Nurb), "copyNurb");
  memcpy(newnu, src, sizeof(Nurb));

  if (pntsu == 1) {
    SWAP(int, pntsu, pntsv);
  }
  newnu->pntsu = pntsu;
  newnu->pntsv = pntsv;

  /* caller can manually handle these arrays */
  newnu->knotsu = nullptr;
  newnu->knotsv = nullptr;

  if (src->bezt) {
    newnu->bezt = (BezTriple *)MEM_malloc_arrayN(pntsu * pntsv, sizeof(BezTriple), "copyNurb2");
  }
  else {
    newnu->bp = (BPoint *)MEM_malloc_arrayN(pntsu * pntsv, sizeof(BPoint), "copyNurb3");
  }

  return newnu;
}

void BKE_nurbList_duplicate(ListBase *lb1, const ListBase *lb2)
{
  BKE_nurbList_free(lb1);

  LISTBASE_FOREACH (const Nurb *, nu, lb2) {
    Nurb *nurb_new = BKE_nurb_duplicate(nu);
    BLI_addtail(lb1, nurb_new);
  }
}

void BKE_nurb_project_2d(Nurb *nu)
{
  BezTriple *bezt;
  BPoint *bp;
  int a;

  if (nu->type == CU_BEZIER) {
    a = nu->pntsu;
    bezt = nu->bezt;
    while (a--) {
      bezt->vec[0][2] = 0.0;
      bezt->vec[1][2] = 0.0;
      bezt->vec[2][2] = 0.0;
      bezt++;
    }
  }
  else {
    a = nu->pntsu * nu->pntsv;
    bp = nu->bp;
    while (a--) {
      bp->vec[2] = 0.0;
      bp++;
    }
  }
}

void BKE_nurb_minmax(const Nurb *nu, bool use_radius, float min[3], float max[3])
{
  BezTriple *bezt;
  BPoint *bp;
  int a;
  float point[3];

  if (nu->type == CU_BEZIER) {
    a = nu->pntsu;
    bezt = nu->bezt;
    while (a--) {
      if (use_radius) {
        float radius_vector[3];
        radius_vector[0] = radius_vector[1] = radius_vector[2] = bezt->radius;

        add_v3_v3v3(point, bezt->vec[1], radius_vector);
        minmax_v3v3_v3(min, max, point);

        sub_v3_v3v3(point, bezt->vec[1], radius_vector);
        minmax_v3v3_v3(min, max, point);
      }
      else {
        minmax_v3v3_v3(min, max, bezt->vec[1]);
      }
      minmax_v3v3_v3(min, max, bezt->vec[0]);
      minmax_v3v3_v3(min, max, bezt->vec[2]);
      bezt++;
    }
  }
  else {
    a = nu->pntsu * nu->pntsv;
    bp = nu->bp;
    while (a--) {
      if (nu->pntsv == 1 && use_radius) {
        float radius_vector[3];
        radius_vector[0] = radius_vector[1] = radius_vector[2] = bp->radius;

        add_v3_v3v3(point, bp->vec, radius_vector);
        minmax_v3v3_v3(min, max, point);

        sub_v3_v3v3(point, bp->vec, radius_vector);
        minmax_v3v3_v3(min, max, point);
      }
      else {
        /* Surfaces doesn't use bevel, so no need to take radius into account. */
        minmax_v3v3_v3(min, max, bp->vec);
      }
      bp++;
    }
  }
}

float BKE_nurb_calc_length(const Nurb *nu, int resolution)
{
  BezTriple *bezt, *prevbezt;
  BPoint *bp, *prevbp;
  int a, b;
  float length = 0.0f;
  int resolu = resolution ? resolution : nu->resolu;
  int pntsu = nu->pntsu;
  float *points, *pntsit, *prevpntsit;

  if (nu->type == CU_POLY) {
    a = nu->pntsu - 1;
    bp = nu->bp;
    if (nu->flagu & CU_NURB_CYCLIC) {
      a++;
      prevbp = nu->bp + (nu->pntsu - 1);
    }
    else {
      prevbp = bp;
      bp++;
    }

    while (a--) {
      length += len_v3v3(prevbp->vec, bp->vec);
      prevbp = bp;
      bp++;
    }
  }
  else if (nu->type == CU_BEZIER) {
    points = (float *)MEM_mallocN(sizeof(float[3]) * (resolu + 1), "getLength_bezier");
    a = nu->pntsu - 1;
    bezt = nu->bezt;
    if (nu->flagu & CU_NURB_CYCLIC) {
      a++;
      prevbezt = nu->bezt + (nu->pntsu - 1);
    }
    else {
      prevbezt = bezt;
      bezt++;
    }

    while (a--) {
      if (prevbezt->h2 == HD_VECT && bezt->h1 == HD_VECT) {
        length += len_v3v3(prevbezt->vec[1], bezt->vec[1]);
      }
      else {
        for (int j = 0; j < 3; j++) {
          BKE_curve_forward_diff_bezier(prevbezt->vec[1][j],
                                        prevbezt->vec[2][j],
                                        bezt->vec[0][j],
                                        bezt->vec[1][j],
                                        points + j,
                                        resolu,
                                        sizeof(float[3]));
        }

        prevpntsit = pntsit = points;
        b = resolu;
        while (b--) {
          pntsit += 3;
          length += len_v3v3(prevpntsit, pntsit);
          prevpntsit = pntsit;
        }
      }
      prevbezt = bezt;
      bezt++;
    }

    MEM_freeN(points);
  }
  else if (nu->type == CU_NURBS) {
    if (nu->pntsv == 1) {
      /* important to zero for BKE_nurb_makeCurve. */
      points = (float *)MEM_callocN(sizeof(float[3]) * pntsu * resolu, "getLength_nurbs");

      BKE_nurb_makeCurve(nu, points, nullptr, nullptr, nullptr, resolu, sizeof(float[3]));

      if (nu->flagu & CU_NURB_CYCLIC) {
        b = pntsu * resolu + 1;
        prevpntsit = points + 3 * (pntsu * resolu - 1);
        pntsit = points;
      }
      else {
        b = (pntsu - 1) * resolu;
        prevpntsit = points;
        pntsit = points + 3;
      }

      while (--b > 0) {
        length += len_v3v3(prevpntsit, pntsit);
        prevpntsit = pntsit;
        pntsit += 3;
      }

      MEM_freeN(points);
    }
  }

  return length;
}

void BKE_nurb_points_add(Nurb *nu, int number)
{
  nu->bp = (BPoint *)MEM_recallocN(nu->bp, (nu->pntsu + number) * sizeof(BPoint));

  BPoint *bp;
  int i;
  for (i = 0, bp = &nu->bp[nu->pntsu]; i < number; i++, bp++) {
    bp->radius = 1.0f;
  }

  nu->pntsu += number;
}

void BKE_nurb_bezierPoints_add(Nurb *nu, int number)
{
  BezTriple *bezt;
  int i;

  nu->bezt = (BezTriple *)MEM_recallocN(nu->bezt, (nu->pntsu + number) * sizeof(BezTriple));

  for (i = 0, bezt = &nu->bezt[nu->pntsu]; i < number; i++, bezt++) {
    bezt->radius = 1.0f;
  }

  nu->pntsu += number;
}

int BKE_nurb_index_from_uv(Nurb *nu, int u, int v)
{
  const int totu = nu->pntsu;
  const int totv = nu->pntsv;

  if (nu->flagu & CU_NURB_CYCLIC) {
    u = mod_i(u, totu);
  }
  else if (u < 0 || u >= totu) {
    return -1;
  }

  if (nu->flagv & CU_NURB_CYCLIC) {
    v = mod_i(v, totv);
  }
  else if (v < 0 || v >= totv) {
    return -1;
  }

  return (v * totu) + u;
}
