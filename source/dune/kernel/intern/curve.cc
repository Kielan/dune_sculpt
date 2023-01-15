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

void BKE_nurb_index_to_uv(Nurb *nu, int index, int *r_u, int *r_v)
{
  const int totu = nu->pntsu;
  const int totv = nu->pntsv;
  BLI_assert(index >= 0 && index < (nu->pntsu * nu->pntsv));
  *r_u = (index % totu);
  *r_v = (index / totu) % totv;
}

BezTriple *BKE_nurb_bezt_get_next(Nurb *nu, BezTriple *bezt)
{
  BezTriple *bezt_next;

  BLI_assert(ARRAY_HAS_ITEM(bezt, nu->bezt, nu->pntsu));

  if (bezt == &nu->bezt[nu->pntsu - 1]) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      bezt_next = nu->bezt;
    }
    else {
      bezt_next = nullptr;
    }
  }
  else {
    bezt_next = bezt + 1;
  }

  return bezt_next;
}

BPoint *BKE_nurb_bpoint_get_next(Nurb *nu, BPoint *bp)
{
  BPoint *bp_next;

  BLI_assert(ARRAY_HAS_ITEM(bp, nu->bp, nu->pntsu));

  if (bp == &nu->bp[nu->pntsu - 1]) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      bp_next = nu->bp;
    }
    else {
      bp_next = nullptr;
    }
  }
  else {
    bp_next = bp + 1;
  }

  return bp_next;
}

BezTriple *BKE_nurb_bezt_get_prev(Nurb *nu, BezTriple *bezt)
{
  BezTriple *bezt_prev;

  BLI_assert(ARRAY_HAS_ITEM(bezt, nu->bezt, nu->pntsu));
  BLI_assert(nu->pntsv <= 1);

  if (bezt == nu->bezt) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      bezt_prev = &nu->bezt[nu->pntsu - 1];
    }
    else {
      bezt_prev = nullptr;
    }
  }
  else {
    bezt_prev = bezt - 1;
  }

  return bezt_prev;
}

BPoint *BKE_nurb_bpoint_get_prev(Nurb *nu, BPoint *bp)
{
  BPoint *bp_prev;

  BLI_assert(ARRAY_HAS_ITEM(bp, nu->bp, nu->pntsu));
  BLI_assert(nu->pntsv == 1);

  if (bp == nu->bp) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      bp_prev = &nu->bp[nu->pntsu - 1];
    }
    else {
      bp_prev = nullptr;
    }
  }
  else {
    bp_prev = bp - 1;
  }

  return bp_prev;
}

void BKE_nurb_bezt_calc_normal(struct Nurb *UNUSED(nu), BezTriple *bezt, float r_normal[3])
{
  /* calculate the axis matrix from the spline */
  float dir_prev[3], dir_next[3];

  sub_v3_v3v3(dir_prev, bezt->vec[0], bezt->vec[1]);
  sub_v3_v3v3(dir_next, bezt->vec[1], bezt->vec[2]);

  normalize_v3(dir_prev);
  normalize_v3(dir_next);

  add_v3_v3v3(r_normal, dir_prev, dir_next);
  normalize_v3(r_normal);
}

void BKE_nurb_bezt_calc_plane(struct Nurb *nu, BezTriple *bezt, float r_plane[3])
{
  float dir_prev[3], dir_next[3];

  sub_v3_v3v3(dir_prev, bezt->vec[0], bezt->vec[1]);
  sub_v3_v3v3(dir_next, bezt->vec[1], bezt->vec[2]);

  normalize_v3(dir_prev);
  normalize_v3(dir_next);

  cross_v3_v3v3(r_plane, dir_prev, dir_next);
  if (normalize_v3(r_plane) < FLT_EPSILON) {
    BezTriple *bezt_prev = BKE_nurb_bezt_get_prev(nu, bezt);
    BezTriple *bezt_next = BKE_nurb_bezt_get_next(nu, bezt);

    if (bezt_prev) {
      sub_v3_v3v3(dir_prev, bezt_prev->vec[1], bezt->vec[1]);
      normalize_v3(dir_prev);
    }
    if (bezt_next) {
      sub_v3_v3v3(dir_next, bezt->vec[1], bezt_next->vec[1]);
      normalize_v3(dir_next);
    }
    cross_v3_v3v3(r_plane, dir_prev, dir_next);
  }

  /* matches with bones more closely */
  {
    float dir_mid[3], tvec[3];
    add_v3_v3v3(dir_mid, dir_prev, dir_next);
    cross_v3_v3v3(tvec, r_plane, dir_mid);
    copy_v3_v3(r_plane, tvec);
  }

  normalize_v3(r_plane);
}

void BKE_nurb_bpoint_calc_normal(struct Nurb *nu, BPoint *bp, float r_normal[3])
{
  BPoint *bp_prev = BKE_nurb_bpoint_get_prev(nu, bp);
  BPoint *bp_next = BKE_nurb_bpoint_get_next(nu, bp);

  zero_v3(r_normal);

  if (bp_prev) {
    float dir_prev[3];
    sub_v3_v3v3(dir_prev, bp_prev->vec, bp->vec);
    normalize_v3(dir_prev);
    add_v3_v3(r_normal, dir_prev);
  }
  if (bp_next) {
    float dir_next[3];
    sub_v3_v3v3(dir_next, bp->vec, bp_next->vec);
    normalize_v3(dir_next);
    add_v3_v3(r_normal, dir_next);
  }

  normalize_v3(r_normal);
}

void BKE_nurb_bpoint_calc_plane(struct Nurb *nu, BPoint *bp, float r_plane[3])
{
  BPoint *bp_prev = BKE_nurb_bpoint_get_prev(nu, bp);
  BPoint *bp_next = BKE_nurb_bpoint_get_next(nu, bp);

  float dir_prev[3] = {0.0f}, dir_next[3] = {0.0f};

  if (bp_prev) {
    sub_v3_v3v3(dir_prev, bp_prev->vec, bp->vec);
    normalize_v3(dir_prev);
  }
  if (bp_next) {
    sub_v3_v3v3(dir_next, bp->vec, bp_next->vec);
    normalize_v3(dir_next);
  }
  cross_v3_v3v3(r_plane, dir_prev, dir_next);

  /* matches with bones more closely */
  {
    float dir_mid[3], tvec[3];
    add_v3_v3v3(dir_mid, dir_prev, dir_next);
    cross_v3_v3v3(tvec, r_plane, dir_mid);
    copy_v3_v3(r_plane, tvec);
  }

  normalize_v3(r_plane);
}

/* ~~~~~~~~~~~~~~~~~~~~Non Uniform Rational B Spline calculations ~~~~~~~~~~~ */

static void calcknots(float *knots, const int pnts, const short order, const short flag)
{
  const bool is_cyclic = flag & CU_NURB_CYCLIC;
  const bool is_bezier = flag & CU_NURB_BEZIER;
  const bool is_end_point = flag & CU_NURB_ENDPOINT;
  /* Inner knots are always repeated once except on Bezier case. */
  const int repeat_inner = is_bezier ? order - 1 : 1;
  /* How many times to repeat 0.0 at the beginning of knot. */
  const int head = is_end_point ? (order - (is_cyclic ? 1 : 0)) :
                                  (is_bezier ? min_ii(2, repeat_inner) : 1);
  /* Number of knots replicating widths of the starting knots.
   * Covers both Cyclic and EndPoint cases. */
  const int tail = is_cyclic ? 2 * order - 1 : (is_end_point ? order : 0);

  const int knot_count = pnts + order + (is_cyclic ? order - 1 : 0);

  int r = head;
  float current = 0.0f;

  const int offset = is_end_point && is_cyclic ? 1 : 0;
  if (offset) {
    knots[0] = current;
    current += 1.0f;
  }

  for (const int i : IndexRange(offset, knot_count - offset - tail)) {
    knots[i] = current;
    r--;
    if (r == 0) {
      current += 1.0f;
      r = repeat_inner;
    }
  }

  const int tail_index = knot_count - tail;
  for (const int i : IndexRange(tail)) {
    knots[tail_index + i] = current + (knots[i] - knots[0]);
  }
}

static void makeknots(Nurb *nu, short uv)
{
  if (nu->type == CU_NURBS) {
    if (uv == 1) {
      if (nu->knotsu) {
        MEM_freeN(nu->knotsu);
      }
      if (BKE_nurb_check_valid_u(nu)) {
        nu->knotsu = (float *)MEM_calloc_arrayN(KNOTSU(nu) + 1, sizeof(float), "makeknots");
        calcknots(nu->knotsu, nu->pntsu, nu->orderu, nu->flagu);
      }
      else {
        nu->knotsu = nullptr;
      }
    }
    else if (uv == 2) {
      if (nu->knotsv) {
        MEM_freeN(nu->knotsv);
      }
      if (BKE_nurb_check_valid_v(nu)) {
        nu->knotsv = (float *)MEM_calloc_arrayN(KNOTSV(nu) + 1, sizeof(float), "makeknots");
        calcknots(nu->knotsv, nu->pntsv, nu->orderv, nu->flagv);
      }
      else {
        nu->knotsv = nullptr;
      }
    }
  }
}

void BKE_nurb_knot_calc_u(Nurb *nu)
{
  makeknots(nu, 1);
}

void BKE_nurb_knot_calc_v(Nurb *nu)
{
  makeknots(nu, 2);
}

static void basisNurb(
    float t, short order, int pnts, const float *knots, float *basis, int *start, int *end)
{
  float d, e;
  int i, i1 = 0, i2 = 0, j, orderpluspnts, opp2, o2;

  orderpluspnts = order + pnts;
  opp2 = orderpluspnts - 1;

  /* this is for float inaccuracy */
  if (t < knots[0]) {
    t = knots[0];
  }
  else if (t > knots[opp2]) {
    t = knots[opp2];
  }

  /* this part is order '1' */
  o2 = order + 1;
  for (i = 0; i < opp2; i++) {
    if (knots[i] != knots[i + 1] && t >= knots[i] && t <= knots[i + 1]) {
      basis[i] = 1.0;
      i1 = i - o2;
      if (i1 < 0) {
        i1 = 0;
      }
      i2 = i;
      i++;
      while (i < opp2) {
        basis[i] = 0.0;
        i++;
      }
      break;
    }

    basis[i] = 0.0;
  }
  basis[i] = 0.0;

  /* this is order 2, 3, ... */
  for (j = 2; j <= order; j++) {

    if (i2 + j >= orderpluspnts) {
      i2 = opp2 - j;
    }

    for (i = i1; i <= i2; i++) {
      if (basis[i] != 0.0f) {
        d = ((t - knots[i]) * basis[i]) / (knots[i + j - 1] - knots[i]);
      }
      else {
        d = 0.0f;
      }

      if (basis[i + 1] != 0.0f) {
        e = ((knots[i + j] - t) * basis[i + 1]) / (knots[i + j] - knots[i + 1]);
      }
      else {
        e = 0.0;
      }

      basis[i] = d + e;
    }
  }

  *start = 1000;
  *end = 0;

  for (i = i1; i <= i2; i++) {
    if (basis[i] > 0.0f) {
      *end = i;
      if (*start == 1000) {
        *start = i;
      }
    }
  }
}

void BKE_nurb_makeFaces(const Nurb *nu, float *coord_array, int rowstride, int resolu, int resolv)
{
  BPoint *bp;
  float *basisu, *basis, *basisv, *sum, *fp, *in;
  float u, v, ustart, uend, ustep, vstart, vend, vstep, sumdiv;
  int i, j, iofs, jofs, cycl, len, curu, curv;
  int istart, iend, jsta, jen, *jstart, *jend, ratcomp;

  int totu = nu->pntsu * resolu, totv = nu->pntsv * resolv;

  if (nu->knotsu == nullptr || nu->knotsv == nullptr) {
    return;
  }
  if (nu->orderu > nu->pntsu) {
    return;
  }
  if (nu->orderv > nu->pntsv) {
    return;
  }
  if (coord_array == nullptr) {
    return;
  }

  /* allocate and initialize */
  len = totu * totv;
  if (len == 0) {
    return;
  }

  sum = (float *)MEM_calloc_arrayN(len, sizeof(float), "makeNurbfaces1");

  bp = nu->bp;
  i = nu->pntsu * nu->pntsv;
  ratcomp = 0;
  while (i--) {
    if (bp->vec[3] != 1.0f) {
      ratcomp = 1;
      break;
    }
    bp++;
  }

  fp = nu->knotsu;
  ustart = fp[nu->orderu - 1];
  if (nu->flagu & CU_NURB_CYCLIC) {
    uend = fp[nu->pntsu + nu->orderu - 1];
  }
  else {
    uend = fp[nu->pntsu];
  }
  ustep = (uend - ustart) / ((nu->flagu & CU_NURB_CYCLIC) ? totu : totu - 1);

  basisu = (float *)MEM_malloc_arrayN(KNOTSU(nu), sizeof(float), "makeNurbfaces3");

  fp = nu->knotsv;
  vstart = fp[nu->orderv - 1];

  if (nu->flagv & CU_NURB_CYCLIC) {
    vend = fp[nu->pntsv + nu->orderv - 1];
  }
  else {
    vend = fp[nu->pntsv];
  }
  vstep = (vend - vstart) / ((nu->flagv & CU_NURB_CYCLIC) ? totv : totv - 1);

  len = KNOTSV(nu);
  basisv = (float *)MEM_malloc_arrayN(len * totv, sizeof(float), "makeNurbfaces3");
  jstart = (int *)MEM_malloc_arrayN(totv, sizeof(float), "makeNurbfaces4");
  jend = (int *)MEM_malloc_arrayN(totv, sizeof(float), "makeNurbfaces5");

  /* Pre-calculation of `basisv` and `jstart`, `jend`. */
  if (nu->flagv & CU_NURB_CYCLIC) {
    cycl = nu->orderv - 1;
  }
  else {
    cycl = 0;
  }
  v = vstart;
  basis = basisv;
  curv = totv;
  while (curv--) {
    basisNurb(v, nu->orderv, nu->pntsv + cycl, nu->knotsv, basis, jstart + curv, jend + curv);
    basis += KNOTSV(nu);
    v += vstep;
  }

  if (nu->flagu & CU_NURB_CYCLIC) {
    cycl = nu->orderu - 1;
  }
  else {
    cycl = 0;
  }
  in = coord_array;
  u = ustart;
  curu = totu;
  while (curu--) {
    basisNurb(u, nu->orderu, nu->pntsu + cycl, nu->knotsu, basisu, &istart, &iend);

    basis = basisv;
    curv = totv;
    while (curv--) {
      jsta = jstart[curv];
      jen = jend[curv];

      /* calculate sum */
      sumdiv = 0.0;
      fp = sum;

      for (j = jsta; j <= jen; j++) {

        if (j >= nu->pntsv) {
          jofs = (j - nu->pntsv);
        }
        else {
          jofs = j;
        }
        bp = nu->bp + nu->pntsu * jofs + istart - 1;

        for (i = istart; i <= iend; i++, fp++) {
          if (i >= nu->pntsu) {
            iofs = i - nu->pntsu;
            bp = nu->bp + nu->pntsu * jofs + iofs;
          }
          else {
            bp++;
          }

          if (ratcomp) {
            *fp = basisu[i] * basis[j] * bp->vec[3];
            sumdiv += *fp;
          }
          else {
            *fp = basisu[i] * basis[j];
          }
        }
      }

      if (ratcomp) {
        fp = sum;
        for (j = jsta; j <= jen; j++) {
          for (i = istart; i <= iend; i++, fp++) {
            *fp /= sumdiv;
          }
        }
      }

      zero_v3(in);

      /* one! (1.0) real point now */
      fp = sum;
      for (j = jsta; j <= jen; j++) {

        if (j >= nu->pntsv) {
          jofs = (j - nu->pntsv);
        }
        else {
          jofs = j;
        }
        bp = nu->bp + nu->pntsu * jofs + istart - 1;

        for (i = istart; i <= iend; i++, fp++) {
          if (i >= nu->pntsu) {
            iofs = i - nu->pntsu;
            bp = nu->bp + nu->pntsu * jofs + iofs;
          }
          else {
            bp++;
          }

          if (*fp != 0.0f) {
            madd_v3_v3fl(in, bp->vec, *fp);
          }
        }
      }

      in += 3;
      basis += KNOTSV(nu);
    }
    u += ustep;
    if (rowstride != 0) {
      in = (float *)(((unsigned char *)in) + (rowstride - 3 * totv * sizeof(*in)));
    }
  }

  /* free */
  MEM_freeN(sum);
  MEM_freeN(basisu);
  MEM_freeN(basisv);
  MEM_freeN(jstart);
  MEM_freeN(jend);
}

void BKE_nurb_makeCurve(const Nurb *nu,
                        float *coord_array,
                        float *tilt_array,
                        float *radius_array,
                        float *weight_array,
                        int resolu,
                        int stride)
{
  const float eps = 1e-6f;
  BPoint *bp;
  float u, ustart, uend, ustep, sumdiv;
  float *basisu, *sum, *fp;
  float *coord_fp = coord_array, *tilt_fp = tilt_array, *radius_fp = radius_array,
        *weight_fp = weight_array;
  int i, len, istart, iend, cycl;

  if (nu->knotsu == nullptr) {
    return;
  }
  if (nu->orderu > nu->pntsu) {
    return;
  }
  if (coord_array == nullptr) {
    return;
  }

  /* allocate and initialize */
  len = nu->pntsu;
  if (len == 0) {
    return;
  }
  sum = (float *)MEM_calloc_arrayN(len, sizeof(float), "makeNurbcurve1");

  resolu = (resolu * SEGMENTSU(nu));

  if (resolu == 0) {
    MEM_freeN(sum);
    return;
  }

  fp = nu->knotsu;
  ustart = fp[nu->orderu - 1];
  if (nu->flagu & CU_NURB_CYCLIC) {
    uend = fp[nu->pntsu + nu->orderu - 1];
  }
  else {
    uend = fp[nu->pntsu];
  }
  ustep = (uend - ustart) / (resolu - ((nu->flagu & CU_NURB_CYCLIC) ? 0 : 1));

  basisu = (float *)MEM_malloc_arrayN(KNOTSU(nu), sizeof(float), "makeNurbcurve3");

  if (nu->flagu & CU_NURB_CYCLIC) {
    cycl = nu->orderu - 1;
  }
  else {
    cycl = 0;
  }

  u = ustart;
  while (resolu--) {
    basisNurb(u, nu->orderu, nu->pntsu + cycl, nu->knotsu, basisu, &istart, &iend);

    /* calc sum */
    sumdiv = 0.0;
    fp = sum;
    bp = nu->bp + istart - 1;
    for (i = istart; i <= iend; i++, fp++) {
      if (i >= nu->pntsu) {
        bp = nu->bp + (i - nu->pntsu);
      }
      else {
        bp++;
      }

      *fp = basisu[i] * bp->vec[3];
      sumdiv += *fp;
    }
    if ((sumdiv != 0.0f) && (sumdiv < 1.0f - eps || sumdiv > 1.0f + eps)) {
      /* is normalizing needed? */
      fp = sum;
      for (i = istart; i <= iend; i++, fp++) {
        *fp /= sumdiv;
      }
    }

    zero_v3(coord_fp);

    /* one! (1.0) real point */
    fp = sum;
    bp = nu->bp + istart - 1;
    for (i = istart; i <= iend; i++, fp++) {
      if (i >= nu->pntsu) {
        bp = nu->bp + (i - nu->pntsu);
      }
      else {
        bp++;
      }

      if (*fp != 0.0f) {
        madd_v3_v3fl(coord_fp, bp->vec, *fp);

        if (tilt_fp) {
          (*tilt_fp) += (*fp) * bp->tilt;
        }

        if (radius_fp) {
          (*radius_fp) += (*fp) * bp->radius;
        }

        if (weight_fp) {
          (*weight_fp) += (*fp) * bp->weight;
        }
      }
    }

    coord_fp = (float *)POINTER_OFFSET(coord_fp, stride);

    if (tilt_fp) {
      tilt_fp = (float *)POINTER_OFFSET(tilt_fp, stride);
    }
    if (radius_fp) {
      radius_fp = (float *)POINTER_OFFSET(radius_fp, stride);
    }
    if (weight_fp) {
      weight_fp = (float *)POINTER_OFFSET(weight_fp, stride);
    }

    u += ustep;
  }

  /* free */
  MEM_freeN(sum);
  MEM_freeN(basisu);
}

unsigned int BKE_curve_calc_coords_axis_len(const unsigned int bezt_array_len,
                                            const unsigned int resolu,
                                            const bool is_cyclic,
                                            const bool use_cyclic_duplicate_endpoint)
{
  const unsigned int segments = bezt_array_len - (is_cyclic ? 0 : 1);
  const unsigned int points_len = (segments * resolu) +
                                  (is_cyclic ? (use_cyclic_duplicate_endpoint) : 1);
  return points_len;
}

void BKE_curve_calc_coords_axis(const BezTriple *bezt_array,
                                const unsigned int bezt_array_len,
                                const unsigned int resolu,
                                const bool is_cyclic,
                                const bool use_cyclic_duplicate_endpoint,
                                /* array params */
                                const unsigned int axis,
                                const unsigned int stride,
                                float *r_points)
{
  const unsigned int points_len = BKE_curve_calc_coords_axis_len(
      bezt_array_len, resolu, is_cyclic, use_cyclic_duplicate_endpoint);
  float *r_points_offset = r_points;

  const unsigned int resolu_stride = resolu * stride;
  const unsigned int bezt_array_last = bezt_array_len - 1;

  for (unsigned int i = 0; i < bezt_array_last; i++) {
    const BezTriple *bezt_curr = &bezt_array[i];
    const BezTriple *bezt_next = &bezt_array[i + 1];
    BKE_curve_forward_diff_bezier(bezt_curr->vec[1][axis],
                                  bezt_curr->vec[2][axis],
                                  bezt_next->vec[0][axis],
                                  bezt_next->vec[1][axis],
                                  r_points_offset,
                                  (int)resolu,
                                  stride);
    r_points_offset = (float *)POINTER_OFFSET(r_points_offset, resolu_stride);
  }

  if (is_cyclic) {
    const BezTriple *bezt_curr = &bezt_array[bezt_array_last];
    const BezTriple *bezt_next = &bezt_array[0];
    BKE_curve_forward_diff_bezier(bezt_curr->vec[1][axis],
                                  bezt_curr->vec[2][axis],
                                  bezt_next->vec[0][axis],
                                  bezt_next->vec[1][axis],
                                  r_points_offset,
                                  (int)resolu,
                                  stride);
    r_points_offset = (float *)POINTER_OFFSET(r_points_offset, resolu_stride);
    if (use_cyclic_duplicate_endpoint) {
      *r_points_offset = *r_points;
      r_points_offset = (float *)POINTER_OFFSET(r_points_offset, stride);
    }
  }
  else {
    float *r_points_last = (float *)POINTER_OFFSET(r_points, bezt_array_last * resolu_stride);
    *r_points_last = bezt_array[bezt_array_last].vec[1][axis];
    r_points_offset = (float *)POINTER_OFFSET(r_points_offset, stride);
  }

  BLI_assert((float *)POINTER_OFFSET(r_points, points_len * stride) == r_points_offset);
  UNUSED_VARS_NDEBUG(points_len);
}

void BKE_curve_forward_diff_bezier(
    float q0, float q1, float q2, float q3, float *p, int it, int stride)
{
  float rt0, rt1, rt2, rt3, f;
  int a;

  f = (float)it;
  rt0 = q0;
  rt1 = 3.0f * (q1 - q0) / f;
  f *= f;
  rt2 = 3.0f * (q0 - 2.0f * q1 + q2) / f;
  f *= it;
  rt3 = (q3 - q0 + 3.0f * (q1 - q2)) / f;

  q0 = rt0;
  q1 = rt1 + rt2 + rt3;
  q2 = 2 * rt2 + 6 * rt3;
  q3 = 6 * rt3;

  for (a = 0; a <= it; a++) {
    *p = q0;
    p = (float *)POINTER_OFFSET(p, stride);
    q0 += q1;
    q1 += q2;
    q2 += q3;
  }
}

void BKE_curve_forward_diff_tangent_bezier(
    float q0, float q1, float q2, float q3, float *p, int it, int stride)
{
  float rt0, rt1, rt2, f;
  int a;

  f = 1.0f / (float)it;

  rt0 = 3.0f * (q1 - q0);
  rt1 = f * (3.0f * (q3 - q0) + 9.0f * (q1 - q2));
  rt2 = 6.0f * (q0 + q2) - 12.0f * q1;

  q0 = rt0;
  q1 = f * (rt1 + rt2);
  q2 = 2.0f * f * rt1;

  for (a = 0; a <= it; a++) {
    *p = q0;
    p = (float *)POINTER_OFFSET(p, stride);
    q0 += q1;
    q1 += q2;
  }
}

static void forward_diff_bezier_cotangent(const float p0[3],
                                          const float p1[3],
                                          const float p2[3],
                                          const float p3[3],
                                          float p[3],
                                          int it,
                                          int stride)
{
  /* note that these are not perpendicular to the curve
   * they need to be rotated for this,
   *
   * This could also be optimized like BKE_curve_forward_diff_bezier */
  for (int a = 0; a <= it; a++) {
    float t = (float)a / (float)it;

    for (int i = 0; i < 3; i++) {
      p[i] = (-6.0f * t + 6.0f) * p0[i] + (18.0f * t - 12.0f) * p1[i] +
             (-18.0f * t + 6.0f) * p2[i] + (6.0f * t) * p3[i];
    }
    normalize_v3(p);
    p = (float *)POINTER_OFFSET(p, stride);
  }
}

static int cu_isectLL(const float v1[3],
                      const float v2[3],
                      const float v3[3],
                      const float v4[3],
                      short cox,
                      short coy,
                      float *lambda,
                      float *mu,
                      float vec[3])
{
  /* return:
   * -1: collinear
   *  0: no intersection of segments
   *  1: exact intersection of segments
   *  2: cross-intersection of segments
   */
  float deler;

  deler = (v1[cox] - v2[cox]) * (v3[coy] - v4[coy]) - (v3[cox] - v4[cox]) * (v1[coy] - v2[coy]);
  if (deler == 0.0f) {
    return -1;
  }

  *lambda = (v1[coy] - v3[coy]) * (v3[cox] - v4[cox]) - (v1[cox] - v3[cox]) * (v3[coy] - v4[coy]);
  *lambda = -(*lambda / deler);

  deler = v3[coy] - v4[coy];
  if (deler == 0) {
    deler = v3[cox] - v4[cox];
    *mu = -(*lambda * (v2[cox] - v1[cox]) + v1[cox] - v3[cox]) / deler;
  }
  else {
    *mu = -(*lambda * (v2[coy] - v1[coy]) + v1[coy] - v3[coy]) / deler;
  }
  vec[cox] = *lambda * (v2[cox] - v1[cox]) + v1[cox];
  vec[coy] = *lambda * (v2[coy] - v1[coy]) + v1[coy];

  if (*lambda >= 0.0f && *lambda <= 1.0f && *mu >= 0.0f && *mu <= 1.0f) {
    if (*lambda == 0.0f || *lambda == 1.0f || *mu == 0.0f || *mu == 1.0f) {
      return 1;
    }
    return 2;
  }
  return 0;
}

static bool bevelinside(const BevList *bl1, const BevList *bl2)
{
  /* is bl2 INSIDE bl1 ? with left-right method and "lambda's" */
  /* returns '1' if correct hole. */
  BevPoint *bevp, *prevbevp;
  float min, max, vec[3], hvec1[3], hvec2[3], lab, mu;
  int nr, links = 0, rechts = 0, mode;

  /* take first vertex of possible hole */

  bevp = bl2->bevpoints;
  hvec1[0] = bevp->vec[0];
  hvec1[1] = bevp->vec[1];
  hvec1[2] = 0.0;
  copy_v3_v3(hvec2, hvec1);
  hvec2[0] += 1000;

  /* test it with all edges of potential surrounding poly */
  /* count number of transitions left-right. */

  bevp = bl1->bevpoints;
  nr = bl1->nr;
  prevbevp = bevp + (nr - 1);

  while (nr--) {
    min = prevbevp->vec[1];
    max = bevp->vec[1];
    if (max < min) {
      min = max;
      max = prevbevp->vec[1];
    }
    if (min != max) {
      if (min <= hvec1[1] && max >= hvec1[1]) {
        /* there's a transition, calc intersection point */
        mode = cu_isectLL(prevbevp->vec, bevp->vec, hvec1, hvec2, 0, 1, &lab, &mu, vec);
        /* if lab==0.0 or lab==1.0 then the edge intersects exactly a transition
         * only allow for one situation: we choose lab= 1.0
         */
        if (mode >= 0 && lab != 0.0f) {
          if (vec[0] < hvec1[0]) {
            links++;
          }
          else {
            rechts++;
          }
        }
      }
    }
    prevbevp = bevp;
    bevp++;
  }

  return (links & 1) && (rechts & 1);
}

struct BevelSort {
  BevList *bl;
  float left;
  int dir;
};

static int vergxcobev(const void *a1, const void *a2)
{
  const struct BevelSort *x1 = (BevelSort *)a1, *x2 = (BevelSort *)a2;

  if (x1->left > x2->left) {
    return 1;
  }
  if (x1->left < x2->left) {
    return -1;
  }
  return 0;
}

/* this function cannot be replaced with atan2, but why? */

static void calc_bevel_sin_cos(
    float x1, float y1, float x2, float y2, float *r_sina, float *r_cosa)
{
  float t01, t02, x3, y3;

  t01 = sqrtf(x1 * x1 + y1 * y1);
  t02 = sqrtf(x2 * x2 + y2 * y2);
  if (t01 == 0.0f) {
    t01 = 1.0f;
  }
  if (t02 == 0.0f) {
    t02 = 1.0f;
  }

  x1 /= t01;
  y1 /= t01;
  x2 /= t02;
  y2 /= t02;

  t02 = x1 * x2 + y1 * y2;
  if (fabsf(t02) >= 1.0f) {
    t02 = M_PI_2;
  }
  else {
    t02 = (saacos(t02)) / 2.0f;
  }

  t02 = sinf(t02);
  if (t02 == 0.0f) {
    t02 = 1.0f;
  }

  x3 = x1 - x2;
  y3 = y1 - y2;
  if (x3 == 0 && y3 == 0) {
    x3 = y1;
    y3 = -x1;
  }
  else {
    t01 = sqrtf(x3 * x3 + y3 * y3);
    x3 /= t01;
    y3 /= t01;
  }

  *r_sina = -y3 / t02;
  *r_cosa = x3 / t02;
}

static void tilt_bezpart(const BezTriple *prevbezt,
                         const BezTriple *bezt,
                         const Nurb *nu,
                         float *tilt_array,
                         float *radius_array,
                         float *weight_array,
                         int resolu,
                         int stride)
{
  const BezTriple *pprev, *next, *last;
  float fac, dfac, t[4];
  int a;

  if (tilt_array == nullptr && radius_array == nullptr) {
    return;
  }

  last = nu->bezt + (nu->pntsu - 1);

  /* returns a point */
  if (prevbezt == nu->bezt) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      pprev = last;
    }
    else {
      pprev = prevbezt;
    }
  }
  else {
    pprev = prevbezt - 1;
  }

  /* next point */
  if (bezt == last) {
    if (nu->flagu & CU_NURB_CYCLIC) {
      next = nu->bezt;
    }
    else {
      next = bezt;
    }
  }
  else {
    next = bezt + 1;
  }

  fac = 0.0;
  dfac = 1.0f / (float)resolu;

  for (a = 0; a < resolu; a++, fac += dfac) {
    if (tilt_array) {
      if (nu->tilt_interp == KEY_CU_EASE) {
        /* May as well support for tilt also 2.47 ease interp. */
        *tilt_array = prevbezt->tilt +
                      (bezt->tilt - prevbezt->tilt) * (3.0f * fac * fac - 2.0f * fac * fac * fac);
      }
      else {
        key_curve_position_weights(fac, t, nu->tilt_interp);
        *tilt_array = t[0] * pprev->tilt + t[1] * prevbezt->tilt + t[2] * bezt->tilt +
                      t[3] * next->tilt;
      }

      tilt_array = (float *)POINTER_OFFSET(tilt_array, stride);
    }

    if (radius_array) {
      if (nu->radius_interp == KEY_CU_EASE) {
        /* Support 2.47 ease interp
         * NOTE: this only takes the 2 points into account,
         * giving much more localized results to changes in radius, sometimes you want that. */
        *radius_array = prevbezt->radius + (bezt->radius - prevbezt->radius) *
                                               (3.0f * fac * fac - 2.0f * fac * fac * fac);
      }
      else {

        /* reuse interpolation from tilt if we can */
        if (tilt_array == nullptr || nu->tilt_interp != nu->radius_interp) {
          key_curve_position_weights(fac, t, nu->radius_interp);
        }
        *radius_array = t[0] * pprev->radius + t[1] * prevbezt->radius + t[2] * bezt->radius +
                        t[3] * next->radius;
      }

      radius_array = (float *)POINTER_OFFSET(radius_array, stride);
    }

    if (weight_array) {
      /* Basic interpolation for now, could copy tilt interp too. */
      *weight_array = prevbezt->weight + (bezt->weight - prevbezt->weight) *
                                             (3.0f * fac * fac - 2.0f * fac * fac * fac);

      weight_array = (float *)POINTER_OFFSET(weight_array, stride);
    }
  }
}

/* `make_bevel_list_3D_*` functions, at a minimum these must
 * fill in the #BevPoint.quat and #BevPoint.dir values. */

/** Utility for `make_bevel_list_3D_*` functions. */
static void bevel_list_calc_bisect(BevList *bl)
{
  BevPoint *bevp2, *bevp1, *bevp0;
  int nr;
  bool is_cyclic = bl->poly != -1;

  if (is_cyclic) {
    bevp2 = bl->bevpoints;
    bevp1 = bevp2 + (bl->nr - 1);
    bevp0 = bevp1 - 1;
    nr = bl->nr;
  }
  else {
    /* If spline is not cyclic, direction of first and
     * last bevel points matches direction of CV handle.
     *
     * This is getting calculated earlier when we know
     * CV's handles and here we might simply skip evaluation
     * of direction for this guys.
     */

    bevp0 = bl->bevpoints;
    bevp1 = bevp0 + 1;
    bevp2 = bevp1 + 1;

    nr = bl->nr - 2;
  }

  while (nr--) {
    /* totally simple */
    bisect_v3_v3v3v3(bevp1->dir, bevp0->vec, bevp1->vec, bevp2->vec);

    bevp0 = bevp1;
    bevp1 = bevp2;
    bevp2++;
  }

  /* In the unlikely situation that handles define a zeroed direction,
   * calculate it from the adjacent points, see T80742.
   *
   * Only do this as a fallback since we typically want the end-point directions
   * to be exactly aligned with the handles at the end-point, see T83117. */
  if (is_cyclic == false) {
    bevp0 = &bl->bevpoints[0];
    bevp1 = &bl->bevpoints[1];
    if (UNLIKELY(is_zero_v3(bevp0->dir))) {
      sub_v3_v3v3(bevp0->dir, bevp1->vec, bevp0->vec);
      if (normalize_v3(bevp0->dir) == 0.0f) {
        copy_v3_v3(bevp0->dir, bevp1->dir);
      }
    }

    bevp0 = &bl->bevpoints[bl->nr - 2];
    bevp1 = &bl->bevpoints[bl->nr - 1];
    if (UNLIKELY(is_zero_v3(bevp1->dir))) {
      sub_v3_v3v3(bevp1->dir, bevp1->vec, bevp0->vec);
      if (normalize_v3(bevp1->dir) == 0.0f) {
        copy_v3_v3(bevp1->dir, bevp0->dir);
      }
    }
  }
}
static void bevel_list_flip_tangents(BevList *bl)
{
  BevPoint *bevp2, *bevp1, *bevp0;
  int nr;

  bevp2 = bl->bevpoints;
  bevp1 = bevp2 + (bl->nr - 1);
  bevp0 = bevp1 - 1;

  nr = bl->nr;
  while (nr--) {
    if (angle_normalized_v3v3(bevp0->tan, bevp1->tan) > DEG2RADF(90.0f)) {
      negate_v3(bevp1->tan);
    }

    bevp0 = bevp1;
    bevp1 = bevp2;
    bevp2++;
  }
}
/* apply user tilt */
static void bevel_list_apply_tilt(BevList *bl)
{
  BevPoint *bevp2, *bevp1;
  int nr;
  float q[4];

  bevp2 = bl->bevpoints;
  bevp1 = bevp2 + (bl->nr - 1);

  nr = bl->nr;
  while (nr--) {
    axis_angle_to_quat(q, bevp1->dir, bevp1->tilt);
    mul_qt_qtqt(bevp1->quat, q, bevp1->quat);
    normalize_qt(bevp1->quat);

    bevp1 = bevp2;
    bevp2++;
  }
}
/* smooth quats, this function should be optimized, it can get slow with many iterations. */
static void bevel_list_smooth(BevList *bl, int smooth_iter)
{
  BevPoint *bevp2, *bevp1, *bevp0;
  int nr;

  float q[4];
  float bevp0_quat[4];
  int a;

  for (a = 0; a < smooth_iter; a++) {
    bevp2 = bl->bevpoints;
    bevp1 = bevp2 + (bl->nr - 1);
    bevp0 = bevp1 - 1;

    nr = bl->nr;

    if (bl->poly == -1) { /* check its not cyclic */
      /* skip the first point */
      /* bevp0 = bevp1; */
      bevp1 = bevp2;
      bevp2++;
      nr--;

      bevp0 = bevp1;
      bevp1 = bevp2;
      bevp2++;
      nr--;
    }

    copy_qt_qt(bevp0_quat, bevp0->quat);

    while (nr--) {
      /* interpolate quats */
      float zaxis[3] = {0, 0, 1}, cross[3], q2[4];
      interp_qt_qtqt(q, bevp0_quat, bevp2->quat, 0.5);
      normalize_qt(q);

      mul_qt_v3(q, zaxis);
      cross_v3_v3v3(cross, zaxis, bevp1->dir);
      axis_angle_to_quat(q2, cross, angle_normalized_v3v3(zaxis, bevp1->dir));
      normalize_qt(q2);

      copy_qt_qt(bevp0_quat, bevp1->quat);
      mul_qt_qtqt(q, q2, q);
      interp_qt_qtqt(bevp1->quat, bevp1->quat, q, 0.5);
      normalize_qt(bevp1->quat);

      /* bevp0 = bevp1; */ /* UNUSED */
      bevp1 = bevp2;
      bevp2++;
    }
  }
}

static void make_bevel_list_3D_zup(BevList *bl)
{
  BevPoint *bevp = bl->bevpoints;
  int nr = bl->nr;

  bevel_list_calc_bisect(bl);

  while (nr--) {
    vec_to_quat(bevp->quat, bevp->dir, 5, 1);
    bevp++;
  }
}

static void minimum_twist_between_two_points(BevPoint *current_point, BevPoint *previous_point)
{
  float angle = angle_normalized_v3v3(previous_point->dir, current_point->dir);
  float q[4];

  if (angle > 0.0f) { /* otherwise we can keep as is */
    float cross_tmp[3];
    cross_v3_v3v3(cross_tmp, previous_point->dir, current_point->dir);
    axis_angle_to_quat(q, cross_tmp, angle);
    mul_qt_qtqt(current_point->quat, q, previous_point->quat);
  }
  else {
    copy_qt_qt(current_point->quat, previous_point->quat);
  }
}

static void make_bevel_list_3D_minimum_twist(BevList *bl)
{
  BevPoint *bevp2, *bevp1, *bevp0; /* standard for all make_bevel_list_3D_* funcs */
  int nr;
  float q[4];

  bevel_list_calc_bisect(bl);

  bevp2 = bl->bevpoints;
  bevp1 = bevp2 + (bl->nr - 1);
  bevp0 = bevp1 - 1;

  /* The ordinal of the point being adjusted (bevp2). First point is 1. */

  /* First point is the reference, don't adjust.
   * Skip this point in the following loop. */
  if (bl->nr > 0) {
    vec_to_quat(bevp2->quat, bevp2->dir, 5, 1);

    bevp0 = bevp1; /* bevp0 is unused */
    bevp1 = bevp2;
    bevp2++;
  }
  for (nr = 1; nr < bl->nr; nr++) {
    minimum_twist_between_two_points(bevp2, bevp1);

    bevp0 = bevp1; /* bevp0 is unused */
    bevp1 = bevp2;
    bevp2++;
  }

  if (bl->poly != -1) { /* check for cyclic */

    /* Need to correct for the start/end points not matching
     * do this by calculating the tilt angle difference, then apply
     * the rotation gradually over the entire curve.
     *
     * Note that the split is between last and second last, rather than first/last as you'd expect.
     *
     * real order is like this
     * 0,1,2,3,4 --> 1,2,3,4,0
     *
     * This is why we compare last with second last.
     */
    float vec_1[3] = {0, 1, 0}, vec_2[3] = {0, 1, 0}, angle, ang_fac, cross_tmp[3];

    BevPoint *bevp_first;
    BevPoint *bevp_last;

    bevp_first = bl->bevpoints;
    bevp_first += bl->nr - 1;
    bevp_last = bevp_first;
    bevp_last--;

    /* quats and vec's are normalized, should not need to re-normalize */
    mul_qt_v3(bevp_first->quat, vec_1);
    mul_qt_v3(bevp_last->quat, vec_2);
    normalize_v3(vec_1);
    normalize_v3(vec_2);

    /* align the vector, can avoid this and it looks 98% OK but
     * better to align the angle quat roll's before comparing */
    {
      cross_v3_v3v3(cross_tmp, bevp_last->dir, bevp_first->dir);
      angle = angle_normalized_v3v3(bevp_first->dir, bevp_last->dir);
      axis_angle_to_quat(q, cross_tmp, angle);
      mul_qt_v3(q, vec_2);
    }

    angle = angle_normalized_v3v3(vec_1, vec_2);

    /* flip rotation if needs be */
    cross_v3_v3v3(cross_tmp, vec_1, vec_2);
    normalize_v3(cross_tmp);
    if (angle_normalized_v3v3(bevp_first->dir, cross_tmp) < DEG2RADF(90.0f)) {
      angle = -angle;
    }

    bevp2 = bl->bevpoints;
    bevp1 = bevp2 + (bl->nr - 1);
    bevp0 = bevp1 - 1;

    nr = bl->nr;
    while (nr--) {
      ang_fac = angle * (1.0f - ((float)nr / bl->nr)); /* also works */

      axis_angle_to_quat(q, bevp1->dir, ang_fac);
      mul_qt_qtqt(bevp1->quat, q, bevp1->quat);

      bevp0 = bevp1;
      bevp1 = bevp2;
      bevp2++;
    }
  }
  else {
    /* Need to correct quat for the first/last point,
     * this is so because previously it was only calculated
     * using its own direction, which might not correspond
     * the twist of neighbor point.
     */
    bevp1 = bl->bevpoints;
    bevp0 = bevp1 + 1;
    minimum_twist_between_two_points(bevp1, bevp0);

    bevp2 = bl->bevpoints;
    bevp1 = bevp2 + (bl->nr - 1);
    bevp0 = bevp1 - 1;
    minimum_twist_between_two_points(bevp1, bevp0);
  }
}

static void make_bevel_list_3D_tangent(BevList *bl)
{
  BevPoint *bevp2, *bevp1, *bevp0; /* standard for all make_bevel_list_3D_* funcs */
  int nr;

  float bevp0_tan[3];

  bevel_list_calc_bisect(bl);
  bevel_list_flip_tangents(bl);

  /* correct the tangents */
  bevp2 = bl->bevpoints;
  bevp1 = bevp2 + (bl->nr - 1);
  bevp0 = bevp1 - 1;

  nr = bl->nr;
  while (nr--) {
    float cross_tmp[3];
    cross_v3_v3v3(cross_tmp, bevp1->tan, bevp1->dir);
    cross_v3_v3v3(bevp1->tan, cross_tmp, bevp1->dir);
    normalize_v3(bevp1->tan);

    bevp0 = bevp1;
    bevp1 = bevp2;
    bevp2++;
  }

  /* now for the real twist calc */
  bevp2 = bl->bevpoints;
  bevp1 = bevp2 + (bl->nr - 1);
  bevp0 = bevp1 - 1;

  copy_v3_v3(bevp0_tan, bevp0->tan);

  nr = bl->nr;
  while (nr--) {
    /* make perpendicular, modify tan in place, is ok */
    float cross_tmp[3];
    const float zero[3] = {0, 0, 0};

    cross_v3_v3v3(cross_tmp, bevp1->tan, bevp1->dir);
    normalize_v3(cross_tmp);
    tri_to_quat(bevp1->quat, zero, cross_tmp, bevp1->tan); /* XXX: could be faster. */

    /* bevp0 = bevp1; */ /* UNUSED */
    bevp1 = bevp2;
    bevp2++;
  }
}

static void make_bevel_list_3D(BevList *bl, int smooth_iter, int twist_mode)
{
  switch (twist_mode) {
    case CU_TWIST_TANGENT:
      make_bevel_list_3D_tangent(bl);
      break;
    case CU_TWIST_MINIMUM:
      make_bevel_list_3D_minimum_twist(bl);
      break;
    default: /* CU_TWIST_Z_UP default, pre 2.49c */
      make_bevel_list_3D_zup(bl);
      break;
  }

  if (smooth_iter) {
    bevel_list_smooth(bl, smooth_iter);
  }

  bevel_list_apply_tilt(bl);
}

/* only for 2 points */
static void make_bevel_list_segment_3D(BevList *bl)
{
  float q[4];

  BevPoint *bevp2 = bl->bevpoints;
  BevPoint *bevp1 = bevp2 + 1;

  /* simple quat/dir */
  sub_v3_v3v3(bevp1->dir, bevp1->vec, bevp2->vec);
  normalize_v3(bevp1->dir);

  vec_to_quat(bevp1->quat, bevp1->dir, 5, 1);
  axis_angle_to_quat(q, bevp1->dir, bevp1->tilt);
  mul_qt_qtqt(bevp1->quat, q, bevp1->quat);
  normalize_qt(bevp1->quat);

  copy_v3_v3(bevp2->dir, bevp1->dir);
  vec_to_quat(bevp2->quat, bevp2->dir, 5, 1);
  axis_angle_to_quat(q, bevp2->dir, bevp2->tilt);
  mul_qt_qtqt(bevp2->quat, q, bevp2->quat);
  normalize_qt(bevp2->quat);
}

/* only for 2 points */
static void make_bevel_list_segment_2D(BevList *bl)
{
  BevPoint *bevp2 = bl->bevpoints;
  BevPoint *bevp1 = bevp2 + 1;

  const float x1 = bevp1->vec[0] - bevp2->vec[0];
  const float y1 = bevp1->vec[1] - bevp2->vec[1];

  calc_bevel_sin_cos(x1, y1, -x1, -y1, &(bevp1->sina), &(bevp1->cosa));
  bevp2->sina = bevp1->sina;
  bevp2->cosa = bevp1->cosa;

  /* fill in dir & quat */
  make_bevel_list_segment_3D(bl);
}

static void make_bevel_list_2D(BevList *bl)
{
  /* NOTE(campbell): `bevp->dir` and `bevp->quat` are not needed for beveling but are
   * used when making a path from a 2D curve, therefore they need to be set. */

  BevPoint *bevp0, *bevp1, *bevp2;
  int nr;

  if (bl->poly != -1) {
    bevp2 = bl->bevpoints;
    bevp1 = bevp2 + (bl->nr - 1);
    bevp0 = bevp1 - 1;
    nr = bl->nr;
  }
  else {
    bevp0 = bl->bevpoints;
    bevp1 = bevp0 + 1;
    bevp2 = bevp1 + 1;

    nr = bl->nr - 2;
  }

  while (nr--) {
    const float x1 = bevp1->vec[0] - bevp0->vec[0];
    const float x2 = bevp1->vec[0] - bevp2->vec[0];
    const float y1 = bevp1->vec[1] - bevp0->vec[1];
    const float y2 = bevp1->vec[1] - bevp2->vec[1];

    calc_bevel_sin_cos(x1, y1, x2, y2, &(bevp1->sina), &(bevp1->cosa));

    /* from: make_bevel_list_3D_zup, could call but avoid a second loop.
     * no need for tricky tilt calculation as with 3D curves */
    bisect_v3_v3v3v3(bevp1->dir, bevp0->vec, bevp1->vec, bevp2->vec);
    vec_to_quat(bevp1->quat, bevp1->dir, 5, 1);
    /* done with inline make_bevel_list_3D_zup */

    bevp0 = bevp1;
    bevp1 = bevp2;
    bevp2++;
  }

  /* correct non-cyclic cases */
  if (bl->poly == -1) {
    BevPoint *bevp;
    float angle;

    /* first */
    bevp = bl->bevpoints;
    angle = atan2f(bevp->dir[0], bevp->dir[1]) - (float)M_PI_2;
    bevp->sina = sinf(angle);
    bevp->cosa = cosf(angle);
    vec_to_quat(bevp->quat, bevp->dir, 5, 1);

    /* last */
    bevp = bl->bevpoints;
    bevp += (bl->nr - 1);
    angle = atan2f(bevp->dir[0], bevp->dir[1]) - (float)M_PI_2;
    bevp->sina = sinf(angle);
    bevp->cosa = cosf(angle);
    vec_to_quat(bevp->quat, bevp->dir, 5, 1);
  }
}

static void bevlist_firstlast_direction_calc_from_bpoint(const Nurb *nu, BevList *bl)
{
  if (nu->pntsu > 1) {
    BPoint *first_bp = nu->bp, *last_bp = nu->bp + (nu->pntsu - 1);
    BevPoint *first_bevp, *last_bevp;

    first_bevp = bl->bevpoints;
    last_bevp = first_bevp + (bl->nr - 1);

    sub_v3_v3v3(first_bevp->dir, (first_bp + 1)->vec, first_bp->vec);
    normalize_v3(first_bevp->dir);

    sub_v3_v3v3(last_bevp->dir, last_bp->vec, (last_bp - 1)->vec);
    normalize_v3(last_bevp->dir);
  }
}

void BKE_curve_bevelList_free(ListBase *bev)
{
  LISTBASE_FOREACH_MUTABLE (BevList *, bl, bev) {
    if (bl->seglen != nullptr) {
      MEM_freeN(bl->seglen);
    }
    if (bl->segbevcount != nullptr) {
      MEM_freeN(bl->segbevcount);
    }
    if (bl->bevpoints != nullptr) {
      MEM_freeN(bl->bevpoints);
    }
    MEM_freeN(bl);
  }

  BLI_listbase_clear(bev);
}

void BKE_curve_bevelList_make(Object *ob, const ListBase *nurbs, const bool for_render)
{
  /* - Convert all curves to polys, with indication of resolution and flags for double-vertices.
   * - Possibly; do a smart vertex removal (in case #Nurb).
   * - Separate in individual blocks with #BoundBox.
   * - Auto-hole detection.
   */

  /* This function needs an object, because of `tflag` and `upflag`. */
  Curve *cu = (Curve *)ob->data;
  BezTriple *bezt, *prevbezt;
  BPoint *bp;
  BevList *blnew;
  BevPoint *bevp2, *bevp1 = nullptr, *bevp0;
  const float threshold = 0.00001f;
  float min, inp;
  float *seglen = nullptr;
  struct BevelSort *sortdata, *sd, *sd1;
  int a, b, nr, poly, resolu = 0, len = 0, segcount;
  int *segbevcount;
  bool do_tilt, do_radius, do_weight;
  bool is_editmode = false;
  ListBase *bev;

  /* segbevcount also requires seglen. */
  const bool need_seglen = ELEM(
                               cu->bevfac1_mapping, CU_BEVFAC_MAP_SEGMENT, CU_BEVFAC_MAP_SPLINE) ||
                           ELEM(cu->bevfac2_mapping, CU_BEVFAC_MAP_SEGMENT, CU_BEVFAC_MAP_SPLINE);

  bev = &ob->runtime.curve_cache->bev;

#if 0
  /* do we need to calculate the radius for each point? */
  do_radius = (cu->bevobj || cu->taperobj || (cu->flag & CU_FRONT) || (cu->flag & CU_BACK)) ? 0 :
                                                                                              1;
#endif

  /* STEP 1: MAKE POLYS */

  BKE_curve_bevelList_free(&ob->runtime.curve_cache->bev);
  if (cu->editnurb && ob->type != OB_FONT) {
    is_editmode = true;
  }

  LISTBASE_FOREACH (const Nurb *, nu, nurbs) {
    if (nu->hide && is_editmode) {
      continue;
    }

    /* check we are a single point? also check we are not a surface and that the orderu is sane,
     * enforced in the UI but can go wrong possibly */
    if (!BKE_nurb_check_valid_u(nu)) {
      BevList *bl = (BevList *)MEM_callocN(sizeof(BevList), "makeBevelList1");
      bl->bevpoints = (BevPoint *)MEM_calloc_arrayN(1, sizeof(BevPoint), "makeBevelPoints1");
      BLI_addtail(bev, bl);
      bl->nr = 0;
      bl->charidx = nu->charidx;
      continue;
    }

    /* Tilt, as the rotation angle of curve control points, is only calculated for 3D curves,
     * (since this transformation affects the 3D space). */
    do_tilt = (cu->flag & CU_3D) != 0;

    /* Normal display uses the radius, better just to calculate them. */
    do_radius = CU_DO_RADIUS(cu, nu);

    do_weight = true;

    BevPoint *bevp;

    if (for_render && cu->resolu_ren != 0) {
      resolu = cu->resolu_ren;
    }
    else {
      resolu = nu->resolu;
    }

    segcount = SEGMENTSU(nu);

    if (nu->type == CU_POLY) {
      len = nu->pntsu;
      BevList *bl = MEM_cnew<BevList>(__func__);
      bl->bevpoints = (BevPoint *)MEM_calloc_arrayN(len, sizeof(BevPoint), __func__);
      if (need_seglen && (nu->flagu & CU_NURB_CYCLIC) == 0) {
        bl->seglen = (float *)MEM_malloc_arrayN(segcount, sizeof(float), __func__);
        bl->segbevcount = (int *)MEM_malloc_arrayN(segcount, sizeof(int), __func__);
      }
      BLI_addtail(bev, bl);

      bl->poly = (nu->flagu & CU_NURB_CYCLIC) ? 0 : -1;
      bl->nr = len;
      bl->dupe_nr = 0;
      bl->charidx = nu->charidx;
      bevp = bl->bevpoints;
      bevp->offset = 0;
      bp = nu->bp;
      seglen = bl->seglen;
      segbevcount = bl->segbevcount;

      while (len--) {
        copy_v3_v3(bevp->vec, bp->vec);
        bevp->tilt = bp->tilt;
        bevp->radius = bp->radius;
        bevp->weight = bp->weight;
        bp++;
        if (seglen != nullptr && len != 0) {
          *seglen = len_v3v3(bevp->vec, bp->vec);
          bevp++;
          bevp->offset = *seglen;
          if (*seglen > threshold) {
            *segbevcount = 1;
          }
          else {
            *segbevcount = 0;
          }
          seglen++;
          segbevcount++;
        }
        else {
          bevp++;
        }
      }

      if ((nu->flagu & CU_NURB_CYCLIC) == 0) {
        bevlist_firstlast_direction_calc_from_bpoint(nu, bl);
      }
    }
    else if (nu->type == CU_BEZIER) {
      /* in case last point is not cyclic */
      len = segcount * resolu + 1;

      BevList *bl = MEM_cnew<BevList>(__func__);
      bl->bevpoints = (BevPoint *)MEM_calloc_arrayN(len, sizeof(BevPoint), __func__);
      if (need_seglen && (nu->flagu & CU_NURB_CYCLIC) == 0) {
        bl->seglen = (float *)MEM_malloc_arrayN(segcount, sizeof(float), __func__);
        bl->segbevcount = (int *)MEM_malloc_arrayN(segcount, sizeof(int), __func__);
      }
      BLI_addtail(bev, bl);

      bl->poly = (nu->flagu & CU_NURB_CYCLIC) ? 0 : -1;
      bl->charidx = nu->charidx;

      bevp = bl->bevpoints;
      seglen = bl->seglen;
      segbevcount = bl->segbevcount;

      bevp->offset = 0;
      if (seglen != nullptr) {
        *seglen = 0;
        *segbevcount = 0;
      }

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

      sub_v3_v3v3(bevp->dir, prevbezt->vec[2], prevbezt->vec[1]);
      normalize_v3(bevp->dir);

      BLI_assert(segcount >= a);

      while (a--) {
        if (prevbezt->h2 == HD_VECT && bezt->h1 == HD_VECT) {

          copy_v3_v3(bevp->vec, prevbezt->vec[1]);
          bevp->tilt = prevbezt->tilt;
          bevp->radius = prevbezt->radius;
          bevp->weight = prevbezt->weight;
          bevp->dupe_tag = false;
          bevp++;
          bl->nr++;
          bl->dupe_nr = 1;
          if (seglen != nullptr) {
            *seglen = len_v3v3(prevbezt->vec[1], bezt->vec[1]);
            bevp->offset = *seglen;
            seglen++;
            /* match segbevcount to the cleaned up bevel lists (see STEP 2) */
            if (bevp->offset > threshold) {
              *segbevcount = 1;
            }
            segbevcount++;
          }
        }
        else {
          /* Always do all three, to prevent data hanging around. */
          int j;

          /* #BevPoint must stay aligned to 4 so `sizeof(BevPoint) / sizeof(float)` works. */
          for (j = 0; j < 3; j++) {
            BKE_curve_forward_diff_bezier(prevbezt->vec[1][j],
                                          prevbezt->vec[2][j],
                                          bezt->vec[0][j],
                                          bezt->vec[1][j],
                                          &(bevp->vec[j]),
                                          resolu,
                                          sizeof(BevPoint));
          }

          /* If both arrays are `nullptr` do nothing. */
          tilt_bezpart(prevbezt,
                       bezt,
                       nu,
                       do_tilt ? &bevp->tilt : nullptr,
                       do_radius ? &bevp->radius : nullptr,
                       do_weight ? &bevp->weight : nullptr,
                       resolu,
                       sizeof(BevPoint));

          if (cu->twist_mode == CU_TWIST_TANGENT) {
            forward_diff_bezier_cotangent(prevbezt->vec[1],
                                          prevbezt->vec[2],
                                          bezt->vec[0],
                                          bezt->vec[1],
                                          bevp->tan,
                                          resolu,
                                          sizeof(BevPoint));
          }

          /* `seglen`. */
          if (seglen != nullptr) {
            *seglen = 0;
            *segbevcount = 0;
            for (j = 0; j < resolu; j++) {
              bevp0 = bevp;
              bevp++;
              bevp->offset = len_v3v3(bevp0->vec, bevp->vec);
              /* Match `seglen` and `segbevcount` to the cleaned up bevel lists (see STEP 2). */
              if (bevp->offset > threshold) {
                *seglen += bevp->offset;
                *segbevcount += 1;
              }
            }
            seglen++;
            segbevcount++;
          }
          else {
            bevp += resolu;
          }
          bl->nr += resolu;
        }
        prevbezt = bezt;
        bezt++;
      }

      if ((nu->flagu & CU_NURB_CYCLIC) == 0) { /* not cyclic: endpoint */
        copy_v3_v3(bevp->vec, prevbezt->vec[1]);
        bevp->tilt = prevbezt->tilt;
        bevp->radius = prevbezt->radius;
        bevp->weight = prevbezt->weight;

        sub_v3_v3v3(bevp->dir, prevbezt->vec[1], prevbezt->vec[0]);
        normalize_v3(bevp->dir);

        bl->nr++;
      }
    }
    else if (nu->type == CU_NURBS) {
      if (nu->pntsv == 1) {
        len = (resolu * segcount);

        BevList *bl = MEM_cnew<BevList>(__func__);
        bl->bevpoints = (BevPoint *)MEM_calloc_arrayN(len, sizeof(BevPoint), __func__);
        if (need_seglen && (nu->flagu & CU_NURB_CYCLIC) == 0) {
          bl->seglen = (float *)MEM_malloc_arrayN(segcount, sizeof(float), __func__);
          bl->segbevcount = (int *)MEM_malloc_arrayN(segcount, sizeof(int), __func__);
        }
        BLI_addtail(bev, bl);
        bl->nr = len;
        bl->dupe_nr = 0;
        bl->poly = (nu->flagu & CU_NURB_CYCLIC) ? 0 : -1;
        bl->charidx = nu->charidx;

        bevp = bl->bevpoints;
        seglen = bl->seglen;
        segbevcount = bl->segbevcount;

        BKE_nurb_makeCurve(nu,
                           &bevp->vec[0],
                           do_tilt ? &bevp->tilt : nullptr,
                           do_radius ? &bevp->radius : nullptr,
                           do_weight ? &bevp->weight : nullptr,
                           resolu,
                           sizeof(BevPoint));

        /* match seglen and segbevcount to the cleaned up bevel lists (see STEP 2) */
        if (seglen != nullptr) {
          nr = segcount;
          bevp0 = bevp;
          bevp++;
          while (nr) {
            int j;
            *seglen = 0;
            *segbevcount = 0;
            /* We keep last bevel segment zero-length. */
            for (j = 0; j < ((nr == 1) ? (resolu - 1) : resolu); j++) {
              bevp->offset = len_v3v3(bevp0->vec, bevp->vec);
              if (bevp->offset > threshold) {
                *seglen += bevp->offset;
                *segbevcount += 1;
              }
              bevp0 = bevp;
              bevp++;
            }
            seglen++;
            segbevcount++;
            nr--;
          }
        }

        if ((nu->flagu & CU_NURB_CYCLIC) == 0) {
          bevlist_firstlast_direction_calc_from_bpoint(nu, bl);
        }
      }
    }
  }

  /* STEP 2: DOUBLE POINTS AND AUTOMATIC RESOLUTION, REDUCE DATABLOCKS */
  LISTBASE_FOREACH (BevList *, bl, bev) {
    if (bl->nr == 0) { /* null bevel items come from single points */
      continue;
    }

    /* Scale the threshold so high resolution shapes don't get over reduced, see: T49850. */
    const float threshold_resolu = 0.00001f / resolu;
    bool is_cyclic = bl->poly != -1;
    nr = bl->nr;
    if (is_cyclic) {
      bevp1 = bl->bevpoints;
      bevp0 = bevp1 + (nr - 1);
    }
    else {
      bevp0 = bl->bevpoints;
      bevp0->offset = 0;
      bevp1 = bevp0 + 1;
    }
    nr--;
    while (nr--) {
      if (seglen != nullptr) {
        if (fabsf(bevp1->offset) < threshold) {
          bevp0->dupe_tag = true;
          bl->dupe_nr++;
        }
      }
      else {
        if (compare_v3v3(bevp0->vec, bevp1->vec, threshold_resolu)) {
          bevp0->dupe_tag = true;
          bl->dupe_nr++;
        }
      }
      bevp0 = bevp1;
      bevp1++;
    }
  }

  LISTBASE_FOREACH_MUTABLE (BevList *, bl, bev) {
    if (bl->nr == 0 || bl->dupe_nr == 0) {
      continue;
    }

    nr = bl->nr - bl->dupe_nr + 1; /* +1 because vector-bezier sets flag too. */
    blnew = (BevList *)MEM_mallocN(sizeof(BevList), "makeBevelList4");
    memcpy(blnew, bl, sizeof(BevList));
    blnew->bevpoints = (BevPoint *)MEM_calloc_arrayN(nr, sizeof(BevPoint), "makeBevelPoints4");
    if (!blnew->bevpoints) {
      MEM_freeN(blnew);
      break;
    }
    blnew->segbevcount = bl->segbevcount;
    blnew->seglen = bl->seglen;
    blnew->nr = 0;
    BLI_remlink(bev, bl);
    BLI_insertlinkbefore(bev, bl->next, blnew); /* Ensure `bevlist` is tuned with `nurblist`. */
    bevp0 = bl->bevpoints;
    bevp1 = blnew->bevpoints;
    nr = bl->nr;
    while (nr--) {
      if (bevp0->dupe_tag == 0) {
        memcpy(bevp1, bevp0, sizeof(BevPoint));
        bevp1++;
        blnew->nr++;
      }
      bevp0++;
    }
    if (bl->bevpoints != nullptr) {
      MEM_freeN(bl->bevpoints);
    }
    MEM_freeN(bl);
    blnew->dupe_nr = 0;
  }

  /* STEP 3: POLYS COUNT AND AUTOHOLE */
  poly = 0;
  LISTBASE_FOREACH (BevList *, bl, bev) {
    if (bl->nr && bl->poly >= 0) {
      poly++;
      bl->poly = poly;
      bl->hole = 0;
    }
  }

  /* find extreme left points, also test (turning) direction */
  if (poly > 0) {
    sd = sortdata = (BevelSort *)MEM_malloc_arrayN(poly, sizeof(struct BevelSort), __func__);
    LISTBASE_FOREACH (BevList *, bl, bev) {
      if (bl->poly > 0) {
        BevPoint *bevp;

        bevp = bl->bevpoints;
        bevp1 = bl->bevpoints;
        min = bevp1->vec[0];
        nr = bl->nr;
        while (nr--) {
          if (min > bevp->vec[0]) {
            min = bevp->vec[0];
            bevp1 = bevp;
          }
          bevp++;
        }
        sd->bl = bl;
        sd->left = min;

        bevp = bl->bevpoints;
        if (bevp1 == bevp) {
          bevp0 = bevp + (bl->nr - 1);
        }
        else {
          bevp0 = bevp1 - 1;
        }
        bevp = bevp + (bl->nr - 1);
        if (bevp1 == bevp) {
          bevp2 = bl->bevpoints;
        }
        else {
          bevp2 = bevp1 + 1;
        }

        inp = ((bevp1->vec[0] - bevp0->vec[0]) * (bevp0->vec[1] - bevp2->vec[1]) +
               (bevp0->vec[1] - bevp1->vec[1]) * (bevp0->vec[0] - bevp2->vec[0]));

        if (inp > 0.0f) {
          sd->dir = 1;
        }
        else {
          sd->dir = 0;
        }

        sd++;
      }
    }
    qsort(sortdata, poly, sizeof(struct BevelSort), vergxcobev);

    sd = sortdata + 1;
    for (a = 1; a < poly; a++, sd++) {
      BevList *bl = sd->bl; /* is bl a hole? */
      sd1 = sortdata + (a - 1);
      for (b = a - 1; b >= 0; b--, sd1--) {    /* all polys to the left */
        if (sd1->bl->charidx == bl->charidx) { /* for text, only check matching char */
          if (bevelinside(sd1->bl, bl)) {
            bl->hole = 1 - sd1->bl->hole;
            break;
          }
        }
      }
    }

    /* turning direction */
    if (CU_IS_2D(cu)) {
      sd = sortdata;
      for (a = 0; a < poly; a++, sd++) {
        if (sd->bl->hole == sd->dir) {
          BevList *bl = sd->bl;
          bevp1 = bl->bevpoints;
          bevp2 = bevp1 + (bl->nr - 1);
          nr = bl->nr / 2;
          while (nr--) {
            SWAP(BevPoint, *bevp1, *bevp2);
            bevp1++;
            bevp2--;
          }
        }
      }
    }
    MEM_freeN(sortdata);
  }

  /* STEP 4: 2D-COSINES or 3D ORIENTATION */
  if (CU_IS_2D(cu)) {
    /* 2D Curves */
    LISTBASE_FOREACH (BevList *, bl, bev) {
      if (bl->nr < 2) {
        BevPoint *bevp = bl->bevpoints;
        unit_qt(bevp->quat);
      }
      else if (bl->nr == 2) { /* 2 points, treat separately. */
        make_bevel_list_segment_2D(bl);
      }
      else {
        make_bevel_list_2D(bl);
      }
    }
  }
  else {
    /* 3D Curves */
    LISTBASE_FOREACH (BevList *, bl, bev) {
      if (bl->nr < 2) {
        BevPoint *bevp = bl->bevpoints;
        unit_qt(bevp->quat);
      }
      else if (bl->nr == 2) { /* 2 points, treat separately. */
        make_bevel_list_segment_3D(bl);
      }
      else {
        make_bevel_list_3D(bl, (int)(resolu * cu->twist_smooth), cu->twist_mode);
      }
    }
  }
}

/* ****************** HANDLES ************** */

static void calchandleNurb_intern(BezTriple *bezt,
                                  const BezTriple *prev,
                                  const BezTriple *next,
                                  eBezTriple_Flag handle_sel_flag,
                                  bool is_fcurve,
                                  bool skip_align,
                                  char fcurve_smoothing)
{
  /* defines to avoid confusion */
#define p2_h1 ((p2)-3)
#define p2_h2 ((p2) + 3)

  const float *p1, *p3;
  float *p2;
  float pt[3];
  float dvec_a[3], dvec_b[3];
  float len, len_a, len_b;
  const float eps = 1e-5;

  /* assume normal handle until we check */
  bezt->auto_handle_type = HD_AUTOTYPE_NORMAL;

  if (bezt->h1 == 0 && bezt->h2 == 0) {
    return;
  }

  p2 = bezt->vec[1];

  if (prev == nullptr) {
    p3 = next->vec[1];
    pt[0] = 2.0f * p2[0] - p3[0];
    pt[1] = 2.0f * p2[1] - p3[1];
    pt[2] = 2.0f * p2[2] - p3[2];
    p1 = pt;
  }
  else {
    p1 = prev->vec[1];
  }

  if (next == nullptr) {
    pt[0] = 2.0f * p2[0] - p1[0];
    pt[1] = 2.0f * p2[1] - p1[1];
    pt[2] = 2.0f * p2[2] - p1[2];
    p3 = pt;
  }
  else {
    p3 = next->vec[1];
  }

  sub_v3_v3v3(dvec_a, p2, p1);
  sub_v3_v3v3(dvec_b, p3, p2);

  if (is_fcurve) {
    len_a = dvec_a[0];
    len_b = dvec_b[0];
  }
  else {
    len_a = len_v3(dvec_a);
    len_b = len_v3(dvec_b);
  }

  if (len_a == 0.0f) {
    len_a = 1.0f;
  }
  if (len_b == 0.0f) {
    len_b = 1.0f;
  }

  if (ELEM(bezt->h1, HD_AUTO, HD_AUTO_ANIM) || ELEM(bezt->h2, HD_AUTO, HD_AUTO_ANIM)) { /* auto */
    float tvec[3];
    tvec[0] = dvec_b[0] / len_b + dvec_a[0] / len_a;
    tvec[1] = dvec_b[1] / len_b + dvec_a[1] / len_a;
    tvec[2] = dvec_b[2] / len_b + dvec_a[2] / len_a;

    if (is_fcurve) {
      if (fcurve_smoothing != FCURVE_SMOOTH_NONE) {
        /* force the horizontal handle size to be 1/3 of the key interval so that
         * the X component of the parametric bezier curve is a linear spline */
        len = 6.0f / 2.5614f;
      }
      else {
        len = tvec[0];
      }
    }
    else {
      len = len_v3(tvec);
    }
    len *= 2.5614f;

    if (len != 0.0f) {
      /* only for fcurves */
      bool leftviolate = false, rightviolate = false;

      if (!is_fcurve || fcurve_smoothing == FCURVE_SMOOTH_NONE) {
        if (len_a > 5.0f * len_b) {
          len_a = 5.0f * len_b;
        }
        if (len_b > 5.0f * len_a) {
          len_b = 5.0f * len_a;
        }
      }

      if (ELEM(bezt->h1, HD_AUTO, HD_AUTO_ANIM)) {
        len_a /= len;
        madd_v3_v3v3fl(p2_h1, p2, tvec, -len_a);

        if ((bezt->h1 == HD_AUTO_ANIM) && next && prev) { /* keep horizontal if extrema */
          float ydiff1 = prev->vec[1][1] - bezt->vec[1][1];
          float ydiff2 = next->vec[1][1] - bezt->vec[1][1];
          if ((ydiff1 <= 0.0f && ydiff2 <= 0.0f) || (ydiff1 >= 0.0f && ydiff2 >= 0.0f)) {
            bezt->vec[0][1] = bezt->vec[1][1];
            bezt->auto_handle_type = HD_AUTOTYPE_LOCKED_FINAL;
          }
          else { /* handles should not be beyond y coord of two others */
            if (ydiff1 <= 0.0f) {
              if (prev->vec[1][1] > bezt->vec[0][1]) {
                bezt->vec[0][1] = prev->vec[1][1];
                leftviolate = true;
              }
            }
            else {
              if (prev->vec[1][1] < bezt->vec[0][1]) {
                bezt->vec[0][1] = prev->vec[1][1];
                leftviolate = true;
              }
            }
          }
        }
      }
      if (ELEM(bezt->h2, HD_AUTO, HD_AUTO_ANIM)) {
        len_b /= len;
        madd_v3_v3v3fl(p2_h2, p2, tvec, len_b);

        if ((bezt->h2 == HD_AUTO_ANIM) && next && prev) { /* keep horizontal if extrema */
          float ydiff1 = prev->vec[1][1] - bezt->vec[1][1];
          float ydiff2 = next->vec[1][1] - bezt->vec[1][1];
          if ((ydiff1 <= 0.0f && ydiff2 <= 0.0f) || (ydiff1 >= 0.0f && ydiff2 >= 0.0f)) {
            bezt->vec[2][1] = bezt->vec[1][1];
            bezt->auto_handle_type = HD_AUTOTYPE_LOCKED_FINAL;
          }
          else { /* handles should not be beyond y coord of two others */
            if (ydiff1 <= 0.0f) {
              if (next->vec[1][1] < bezt->vec[2][1]) {
                bezt->vec[2][1] = next->vec[1][1];
                rightviolate = true;
              }
            }
            else {
              if (next->vec[1][1] > bezt->vec[2][1]) {
                bezt->vec[2][1] = next->vec[1][1];
                rightviolate = true;
              }
            }
          }
        }
      }
      if (leftviolate || rightviolate) { /* align left handle */
        BLI_assert(is_fcurve);
        /* simple 2d calculation */
        float h1_x = p2_h1[0] - p2[0];
        float h2_x = p2[0] - p2_h2[0];

        if (leftviolate) {
          p2_h2[1] = p2[1] + ((p2[1] - p2_h1[1]) / h1_x) * h2_x;
        }
        else {
          p2_h1[1] = p2[1] + ((p2[1] - p2_h2[1]) / h2_x) * h1_x;
        }
      }
    }
  }

  if (bezt->h1 == HD_VECT) { /* vector */
    madd_v3_v3v3fl(p2_h1, p2, dvec_a, -1.0f / 3.0f);
  }
  if (bezt->h2 == HD_VECT) {
    madd_v3_v3v3fl(p2_h2, p2, dvec_b, 1.0f / 3.0f);
  }

  if (skip_align ||
      /* When one handle is free, aligning makes no sense, see: T35952 */
      ELEM(HD_FREE, bezt->h1, bezt->h2) ||
      /* Also when no handles are aligned, skip this step. */
      (!ELEM(HD_ALIGN, bezt->h1, bezt->h2) && !ELEM(HD_ALIGN_DOUBLESIDE, bezt->h1, bezt->h2))) {
    /* Handles need to be updated during animation and applying stuff like hooks,
     * but in such situations it's quite difficult to distinguish in which order
     * align handles should be aligned so skip them for now. */
    return;
  }

  len_a = len_v3v3(p2, p2_h1);
  len_b = len_v3v3(p2, p2_h2);

  if (len_a == 0.0f) {
    len_a = 1.0f;
  }
  if (len_b == 0.0f) {
    len_b = 1.0f;
  }

  const float len_ratio = len_a / len_b;

  if (bezt->f1 & handle_sel_flag) {                      /* order of calculation */
    if (ELEM(bezt->h2, HD_ALIGN, HD_ALIGN_DOUBLESIDE)) { /* aligned */
      if (len_a > eps) {
        len = 1.0f / len_ratio;
        p2_h2[0] = p2[0] + len * (p2[0] - p2_h1[0]);
        p2_h2[1] = p2[1] + len * (p2[1] - p2_h1[1]);
        p2_h2[2] = p2[2] + len * (p2[2] - p2_h1[2]);
      }
    }
    if (ELEM(bezt->h1, HD_ALIGN, HD_ALIGN_DOUBLESIDE)) {
      if (len_b > eps) {
        len = len_ratio;
        p2_h1[0] = p2[0] + len * (p2[0] - p2_h2[0]);
        p2_h1[1] = p2[1] + len * (p2[1] - p2_h2[1]);
        p2_h1[2] = p2[2] + len * (p2[2] - p2_h2[2]);
      }
    }
  }
  else {
    if (ELEM(bezt->h1, HD_ALIGN, HD_ALIGN_DOUBLESIDE)) {
      if (len_b > eps) {
        len = len_ratio;
        p2_h1[0] = p2[0] + len * (p2[0] - p2_h2[0]);
        p2_h1[1] = p2[1] + len * (p2[1] - p2_h2[1]);
        p2_h1[2] = p2[2] + len * (p2[2] - p2_h2[2]);
      }
    }
    if (ELEM(bezt->h2, HD_ALIGN, HD_ALIGN_DOUBLESIDE)) { /* aligned */
      if (len_a > eps) {
        len = 1.0f / len_ratio;
        p2_h2[0] = p2[0] + len * (p2[0] - p2_h1[0]);
        p2_h2[1] = p2[1] + len * (p2[1] - p2_h1[1]);
        p2_h2[2] = p2[2] + len * (p2[2] - p2_h1[2]);
      }
    }
  }

#undef p2_h1
#undef p2_h2
}
