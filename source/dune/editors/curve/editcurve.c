#include "DNA_anim_types.h"
#include "DNA_key_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_array_utils.h"
#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "BKE_action.h"
#include "BKE_anim_data.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_displist.h"
#include "BKE_fcurve.h"
#include "BKE_global.h"
#include "BKE_key.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_modifier.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_curve.h"
#include "ED_object.h"
#include "ED_outliner.h"
#include "ED_screen.h"
#include "ED_select_utils.h"
#include "ED_transform.h"
#include "ED_transform_snap_object_context.h"
#include "ED_types.h"
#include "ED_view3d.h"

#include "curve_intern.h"

#include "curve_fit_nd.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

void selectend_nurb(Object *obedit, enum eEndPoint_Types selfirst, bool doswap, bool selstatus);
static void adduplicateflagNurb(
    Object *obedit, View3D *v3d, ListBase *newnurb, const uint8_t flag, const bool split);
static bool curve_delete_segments(Object *obedit, View3D *v3d, const bool split);
static bool curve_delete_vertices(Object *obedit, View3D *v3d);

/* -------------------------------------------------------------------- */
/** \name Utility Functions
 * \{ */

ListBase *object_editcurve_get(Object *ob)
{
  if (ob && ELEM(ob->type, OB_CURVES_LEGACY, OB_SURF)) {
    Curve *cu = ob->data;
    return &cu->editnurb->nurbs;
  }
  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Debug Printing
 * \{ */

#if 0
void printknots(Object *obedit)
{
  ListBase *editnurb = object_editcurve_get(obedit);
  Nurb *nu;
  int a, num;

  for (nu = editnurb->first; nu; nu = nu->next) {
    if (ED_curve_nurb_select_check(nu) && nu->type == CU_NURBS) {
      if (nu->knotsu) {
        num = KNOTSU(nu);
        for (a = 0; a < num; a++) {
          printf("knotu %d: %f\n", a, nu->knotsu[a]);
        }
      }
      if (nu->knotsv) {
        num = KNOTSV(nu);
        for (a = 0; a < num; a++) {
          printf("knotv %d: %f\n", a, nu->knotsv[a]);
        }
      }
    }
  }
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shape keys
 * \{ */

static CVKeyIndex *init_cvKeyIndex(
    void *cv, int key_index, int nu_index, int pt_index, int vertex_index)
{
  CVKeyIndex *cvIndex = MEM_callocN(sizeof(CVKeyIndex), __func__);

  cvIndex->orig_cv = cv;
  cvIndex->key_index = key_index;
  cvIndex->nu_index = nu_index;
  cvIndex->pt_index = pt_index;
  cvIndex->vertex_index = vertex_index;
  cvIndex->switched = false;

  return cvIndex;
}

static void init_editNurb_keyIndex(EditNurb *editnurb, ListBase *origBase)
{
  Nurb *nu = editnurb->nurbs.first;
  Nurb *orignu = origBase->first;
  GHash *gh;
  BezTriple *bezt, *origbezt;
  BPoint *bp, *origbp;
  CVKeyIndex *keyIndex;
  int a, key_index = 0, nu_index = 0, pt_index = 0, vertex_index = 0;

  if (editnurb->keyindex) {
    return;
  }

  gh = BLI_ghash_ptr_new("editNurb keyIndex");

  while (orignu) {
    if (orignu->bezt) {
      a = orignu->pntsu;
      bezt = nu->bezt;
      origbezt = orignu->bezt;
      pt_index = 0;
      while (a--) {
        /* We cannot keep *any* reference to curve obdata,
         * it might be replaced and freed while editcurve remain in use
         * (in viewport render case e.g.). Note that we could use a pool to avoid
         * lots of malloc's here, but... not really a problem for now. */
        BezTriple *origbezt_cpy = MEM_mallocN(sizeof(*origbezt), __func__);
        *origbezt_cpy = *origbezt;
        keyIndex = init_cvKeyIndex(origbezt_cpy, key_index, nu_index, pt_index, vertex_index);
        BLI_ghash_insert(gh, bezt, keyIndex);
        key_index += KEYELEM_FLOAT_LEN_BEZTRIPLE;
        vertex_index += 3;
        bezt++;
        origbezt++;
        pt_index++;
      }
    }
    else {
      a = orignu->pntsu * orignu->pntsv;
      bp = nu->bp;
      origbp = orignu->bp;
      pt_index = 0;
      while (a--) {
        /* We cannot keep *any* reference to curve obdata,
         * it might be replaced and freed while editcurve remain in use
         * (in viewport render case e.g.). Note that we could use a pool to avoid
         * lots of malloc's here, but... not really a problem for now. */
        BPoint *origbp_cpy = MEM_mallocN(sizeof(*origbp_cpy), __func__);
        *origbp_cpy = *origbp;
        keyIndex = init_cvKeyIndex(origbp_cpy, key_index, nu_index, pt_index, vertex_index);
        BLI_ghash_insert(gh, bp, keyIndex);
        key_index += KEYELEM_FLOAT_LEN_BPOINT;
        bp++;
        origbp++;
        pt_index++;
        vertex_index++;
      }
    }

    nu = nu->next;
    orignu = orignu->next;
    nu_index++;
  }

  editnurb->keyindex = gh;
}

static CVKeyIndex *getCVKeyIndex(EditNurb *editnurb, const void *cv)
{
  return BLI_ghash_lookup(editnurb->keyindex, cv);
}

static CVKeyIndex *popCVKeyIndex(EditNurb *editnurb, const void *cv)
{
  return BLI_ghash_popkey(editnurb->keyindex, cv, NULL);
}

static BezTriple *getKeyIndexOrig_bezt(EditNurb *editnurb, const BezTriple *bezt)
{
  CVKeyIndex *index = getCVKeyIndex(editnurb, bezt);

  if (!index) {
    return NULL;
  }

  return (BezTriple *)index->orig_cv;
}

static BPoint *getKeyIndexOrig_bp(EditNurb *editnurb, BPoint *bp)
{
  CVKeyIndex *index = getCVKeyIndex(editnurb, bp);

  if (!index) {
    return NULL;
  }

  return (BPoint *)index->orig_cv;
}

static int getKeyIndexOrig_keyIndex(EditNurb *editnurb, void *cv)
{
  CVKeyIndex *index = getCVKeyIndex(editnurb, cv);

  if (!index) {
    return -1;
  }

  return index->key_index;
}

static void keyIndex_delBezt(EditNurb *editnurb, BezTriple *bezt)
{
  if (!editnurb->keyindex) {
    return;
  }

  BKE_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bezt);
}

static void keyIndex_delBP(EditNurb *editnurb, BPoint *bp)
{
  if (!editnurb->keyindex) {
    return;
  }

  BKE_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bp);
}

static void keyIndex_delNurb(EditNurb *editnurb, Nurb *nu)
{
  int a;

  if (!editnurb->keyindex) {
    return;
  }

  if (nu->bezt) {
    const BezTriple *bezt = nu->bezt;
    a = nu->pntsu;

    while (a--) {
      BKE_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bezt);
      bezt++;
    }
  }
  else {
    const BPoint *bp = nu->bp;
    a = nu->pntsu * nu->pntsv;

    while (a--) {
      BKE_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bp);
      bp++;
    }
  }
}

static void keyIndex_delNurbList(EditNurb *editnurb, ListBase *nubase)
{
  LISTBASE_FOREACH (Nurb *, nu, nubase) {
    keyIndex_delNurb(editnurb, nu);
  }
}

static void keyIndex_updateCV(EditNurb *editnurb, char *cv, char *newcv, int count, int size)
{
  int i;
  CVKeyIndex *index;

  if (editnurb->keyindex == NULL) {
    /* No shape keys - updating not needed */
    return;
  }

  for (i = 0; i < count; i++) {
    index = popCVKeyIndex(editnurb, cv);

    if (index) {
      BLI_ghash_insert(editnurb->keyindex, newcv, index);
    }

    newcv += size;
    cv += size;
  }
}

static void keyIndex_updateBezt(EditNurb *editnurb, BezTriple *bezt, BezTriple *newbezt, int count)
{
  keyIndex_updateCV(editnurb, (char *)bezt, (char *)newbezt, count, sizeof(BezTriple));
}

static void keyIndex_updateBP(EditNurb *editnurb, BPoint *bp, BPoint *newbp, int count)
{
  keyIndex_updateCV(editnurb, (char *)bp, (char *)newbp, count, sizeof(BPoint));
}

void ED_curve_keyindex_update_nurb(EditNurb *editnurb, Nurb *nu, Nurb *newnu)
{
  if (nu->bezt) {
    keyIndex_updateBezt(editnurb, nu->bezt, newnu->bezt, newnu->pntsu);
  }
  else {
    keyIndex_updateBP(editnurb, nu->bp, newnu->bp, newnu->pntsu * newnu->pntsv);
  }
}

static void keyIndex_swap(EditNurb *editnurb, void *a, void *b)
{
  CVKeyIndex *index1 = popCVKeyIndex(editnurb, a);
  CVKeyIndex *index2 = popCVKeyIndex(editnurb, b);

  if (index2) {
    BLI_ghash_insert(editnurb->keyindex, a, index2);
  }
  if (index1) {
    BLI_ghash_insert(editnurb->keyindex, b, index1);
  }
}

static void keyIndex_switchDirection(EditNurb *editnurb, Nurb *nu)
{
  int a;
  CVKeyIndex *index1, *index2;

  if (nu->bezt) {
    BezTriple *bezt1, *bezt2;

    a = nu->pntsu;

    bezt1 = nu->bezt;
    bezt2 = bezt1 + (a - 1);

    if (a & 1) {
      a++;
    }

    a /= 2;

    while (a--) {
      index1 = getCVKeyIndex(editnurb, bezt1);
      index2 = getCVKeyIndex(editnurb, bezt2);

      if (index1) {
        index1->switched = !index1->switched;
      }

      if (bezt1 != bezt2) {
        keyIndex_swap(editnurb, bezt1, bezt2);

        if (index2) {
          index2->switched = !index2->switched;
        }
      }

      bezt1++;
      bezt2--;
    }
  }
  else {
    BPoint *bp1, *bp2;

    if (nu->pntsv == 1) {
      a = nu->pntsu;
      bp1 = nu->bp;
      bp2 = bp1 + (a - 1);
      a /= 2;
      while (bp1 != bp2 && a > 0) {
        index1 = getCVKeyIndex(editnurb, bp1);
        index2 = getCVKeyIndex(editnurb, bp2);

        if (index1) {
          index1->switched = !index1->switched;
        }

        if (bp1 != bp2) {
          if (index2) {
            index2->switched = !index2->switched;
          }

          keyIndex_swap(editnurb, bp1, bp2);
        }

        a--;
        bp1++;
        bp2--;
      }
    }
    else {
      int b;

      for (b = 0; b < nu->pntsv; b++) {

        bp1 = &nu->bp[b * nu->pntsu];
        a = nu->pntsu;
        bp2 = bp1 + (a - 1);
        a /= 2;

        while (bp1 != bp2 && a > 0) {
          index1 = getCVKeyIndex(editnurb, bp1);
          index2 = getCVKeyIndex(editnurb, bp2);

          if (index1) {
            index1->switched = !index1->switched;
          }

          if (bp1 != bp2) {
            if (index2) {
              index2->switched = !index2->switched;
            }

            keyIndex_swap(editnurb, bp1, bp2);
          }

          a--;
          bp1++;
          bp2--;
        }
      }
    }
  }
}

static void switch_keys_direction(Curve *cu, Nurb *actnu)
{
  EditNurb *editnurb = cu->editnurb;
  ListBase *nubase = &editnurb->nurbs;
  float *fp;
  int a;

  LISTBASE_FOREACH (KeyBlock *, currkey, &cu->key->block) {
    fp = currkey->data;

    LISTBASE_FOREACH (Nurb *, nu, nubase) {
      if (nu->bezt) {
        BezTriple *bezt = nu->bezt;
        a = nu->pntsu;
        if (nu == actnu) {
          while (a--) {
            if (getKeyIndexOrig_bezt(editnurb, bezt)) {
              swap_v3_v3(fp, fp + 6);
              *(fp + 9) = -*(fp + 9);
              fp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
            }
            bezt++;
          }
        }
        else {
          fp += a * KEYELEM_FLOAT_LEN_BEZTRIPLE;
        }
      }
      else {
        BPoint *bp = nu->bp;
        a = nu->pntsu * nu->pntsv;
        if (nu == actnu) {
          while (a--) {
            if (getKeyIndexOrig_bp(editnurb, bp)) {
              *(fp + 3) = -*(fp + 3);
              fp += KEYELEM_FLOAT_LEN_BPOINT;
            }
            bp++;
          }
        }
        else {
          fp += a * KEYELEM_FLOAT_LEN_BPOINT;
        }
      }
    }
  }
}

static void keyData_switchDirectionNurb(Curve *cu, Nurb *nu)
{
  EditNurb *editnurb = cu->editnurb;

  if (!editnurb->keyindex) {
    /* no shape keys - nothing to do */
    return;
  }

  keyIndex_switchDirection(editnurb, nu);
  if (cu->key) {
    switch_keys_direction(cu, nu);
  }
}

GHash *ED_curve_keyindex_hash_duplicate(GHash *keyindex)
{
  GHash *gh;
  GHashIterator gh_iter;

  gh = BLI_ghash_ptr_new_ex("dupli_keyIndex gh", BLI_ghash_len(keyindex));

  GHASH_ITER (gh_iter, keyindex) {
    void *cv = BLI_ghashIterator_getKey(&gh_iter);
    CVKeyIndex *index = BLI_ghashIterator_getValue(&gh_iter);
    CVKeyIndex *newIndex = MEM_mallocN(sizeof(CVKeyIndex), "dupli_keyIndexHash index");

    memcpy(newIndex, index, sizeof(CVKeyIndex));
    newIndex->orig_cv = MEM_dupallocN(index->orig_cv);

    BLI_ghash_insert(gh, cv, newIndex);
  }

  return gh;
}

static void key_to_bezt(float *key, BezTriple *basebezt, BezTriple *bezt)
{
  memcpy(bezt, basebezt, sizeof(BezTriple));
  memcpy(bezt->vec, key, sizeof(float[9]));
  bezt->tilt = key[9];
  bezt->radius = key[10];
}

static void bezt_to_key(BezTriple *bezt, float *key)
{
  memcpy(key, bezt->vec, sizeof(float[9]));
  key[9] = bezt->tilt;
  key[10] = bezt->radius;
}

static void calc_keyHandles(ListBase *nurb, float *key)
{
  int a;
  float *fp = key;
  BezTriple *bezt;

  LISTBASE_FOREACH (Nurb *, nu, nurb) {
    if (nu->bezt) {
      BezTriple *prevp, *nextp;
      BezTriple cur, prev, next;
      float *startfp, *prevfp, *nextfp;

      bezt = nu->bezt;
      a = nu->pntsu;
      startfp = fp;

      if (nu->flagu & CU_NURB_CYCLIC) {
        prevp = bezt + (a - 1);
        prevfp = fp + (KEYELEM_FLOAT_LEN_BEZTRIPLE * (a - 1));
      }
      else {
        prevp = NULL;
        prevfp = NULL;
      }

      nextp = bezt + 1;
      nextfp = fp + KEYELEM_FLOAT_LEN_BEZTRIPLE;

      while (a--) {
        key_to_bezt(fp, bezt, &cur);

        if (nextp) {
          key_to_bezt(nextfp, nextp, &next);
        }
        if (prevp) {
          key_to_bezt(prevfp, prevp, &prev);
        }

        BKE_nurb_handle_calc(&cur, prevp ? &prev : NULL, nextp ? &next : NULL, 0, 0);
        bezt_to_key(&cur, fp);

        prevp = bezt;
        prevfp = fp;
        if (a == 1) {
          if (nu->flagu & CU_NURB_CYCLIC) {
            nextp = nu->bezt;
            nextfp = startfp;
          }
          else {
            nextp = NULL;
            nextfp = NULL;
          }
        }
        else {
          nextp++;
          nextfp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
        }

        bezt++;
        fp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
      }
    }
    else {
      a = nu->pntsu * nu->pntsv;
      fp += a * KEYELEM_FLOAT_LEN_BPOINT;
    }
  }
}
