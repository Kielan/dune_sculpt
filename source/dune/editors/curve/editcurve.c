#include "TYPES_anim_types.h"
#include "TYPES_key_types.h"
#include "TYPES_object_types.h"
#include "TYPES_scene_types.h"

#include "MEM_guardedalloc.h"

#include "LIB_array_utils.h"
#include "LIB_blenlib.h"
#include "LIB_ghash.h"
#include "LIB_math.h"

#include "I18N_translation.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_context.h"
#include "dune_curve.h"
#include "dune_displist.h"
#include "dune_fcurve.h"
#include "dune_global.h"
#include "dune_key.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_modifier.h"
#include "dune_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "wm_api.h"
#include "wm_types.h"

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

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

void selectend_nurb(Object *obedit, enum eEndPoint_Types selfirst, bool doswap, bool selstatus);
static void adduplicateflagNurb(
    Object *obedit, View3D *v3d, ListBase *newnurb, const uint8_t flag, const bool split);
static bool curve_delete_segments(Object *obedit, View3D *v3d, const bool split);
static bool curve_delete_vertices(Object *obedit, View3D *v3d);

/* -------------------------------------------------------------------- */
/** Utility Functions **/

ListBase *object_editcurve_get(Object *ob)
{
  if (ob && ELEM(ob->type, OB_CURVES_LEGACY, OB_SURF)) {
    Curve *cu = ob->data;
    return &cu->editnurb->nurbs;
  }
  return NULL;
}

/* -------------------------------------------------------------------- */
/** Debug Printing **/

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

/* -------------------------------------------------------------------- */
/** Shape keys **/

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
  DPoint *bp, *origbp;
  CVKeyIndex *keyIndex;
  int a, key_index = 0, nu_index = 0, pt_index = 0, vertex_index = 0;

  if (editnurb->keyindex) {
    return;
  }

  gh = lib_ghash_ptr_new("editNurb keyIndex");

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
        lib_ghash_insert(gh, bezt, keyIndex);
        key_index += KEYELEM_FLOAT_LEN_BEZTRIPLE;
        vertex_index += 3;
        bezt++;
        origbezt++;
        pt_index++;
      }
    }
    else {
      a = orignu->pntsu * orignu->pntsv;
      dp = nu->dp;
      origdp = orignu->dp;
      pt_index = 0;
      while (a--) {
        /* We cannot keep *any* reference to curve obdata,
         * it might be replaced and freed while editcurve remain in use
         * (in viewport render case e.g.). Note that we could use a pool to avoid
         * lots of malloc's here, but... not really a problem for now. */
        DPoint *origbp_cpy = MEM_mallocN(sizeof(*origbp_cpy), __func__);
        *origbp_cpy = *origbp;
        keyIndex = init_cvKeyIndex(origbp_cpy, key_index, nu_index, pt_index, vertex_index);
        lib_ghash_insert(gh, bp, keyIndex);
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
  return lib_ghash_lookup(editnurb->keyindex, cv);
}

static CVKeyIndex *popCVKeyIndex(EditNurb *editnurb, const void *cv)
{
  return lib_ghash_popkey(editnurb->keyindex, cv, NULL);
}

static BezTriple *getKeyIndexOrig_bezt(EditNurb *editnurb, const BezTriple *bezt)
{
  CVKeyIndex *index = getCVKeyIndex(editnurb, bezt);

  if (!index) {
    return NULL;
  }

  return (BezTriple *)index->orig_cv;
}

static DPoint *getKeyIndexOrig_dp(EditNurb *editnurb, DPoint *dp)
{
  CVKeyIndex *index = getCVKeyIndex(editnurb, dp);

  if (!index) {
    return NULL;
  }

  return (DPoint *)index->orig_cv;
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

  dune_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bezt);
}

static void keyIndex_delBP(EditNurb *editnurb, DPoint *bp)
{
  if (!editnurb->keyindex) {
    return;
  }

  dune_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bp);
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
      dune_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bezt);
      bezt++;
    }
  }
  else {
    const DPoint *bp = nu->bp;
    a = nu->pntsu * nu->pntsv;

    while (a--) {
      dune_curve_editNurb_keyIndex_delCV(editnurb->keyindex, bp);
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
      lib_ghash_insert(editnurb->keyindex, newcv, index);
    }

    newcv += size;
    cv += size;
  }
}

static void keyIndex_updateBezt(EditNurb *editnurb, BezTriple *bezt, BezTriple *newbezt, int count)
{
  keyIndex_updateCV(editnurb, (char *)bezt, (char *)newbezt, count, sizeof(BezTriple));
}

static void keyIndex_updateDP(EditNurb *editnurb, DPoint *dp, DPoint *newdp, int count)
{
  keyIndex_updateCV(editnurb, (char *)dp, (char *)newdp, count, sizeof(DPoint));
}

void ED_curve_keyindex_update_nurb(EditNurb *editnurb, Nurb *nu, Nurb *newnu)
{
  if (nu->bezt) {
    keyIndex_updateBezt(editnurb, nu->bezt, newnu->bezt, newnu->pntsu);
  }
  else {
    keyIndex_updateBP(editnurb, nu->dp, newnu->dp, newnu->pntsu * newnu->pntsv);
  }
}

static void keyIndex_swap(EditNurb *editnurb, void *a, void *b)
{
  CVKeyIndex *index1 = popCVKeyIndex(editnurb, a);
  CVKeyIndex *index2 = popCVKeyIndex(editnurb, b);

  if (index2) {
    lib_ghash_insert(editnurb->keyindex, a, index2);
  }
  if (index1) {
    lib_ghash_insert(editnurb->keyindex, b, index1);
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
    DPoint *dp1, *dp2;

    if (nu->pntsv == 1) {
      a = nu->pntsu;
      bp1 = nu->bp;
      bp2 = bp1 + (a - 1);
      a /= 2;
      while (bp1 != bp2 && a > 0) {
        index1 = getCVKeyIndex(editnurb, dp1);
        index2 = getCVKeyIndex(editnurb, dp2);

        if (index1) {
          index1->switched = !index1->switched;
        }

        if (dp1 != dp2) {
          if (index2) {
            index2->switched = !index2->switched;
          }

          keyIndex_swap(editnurb, dp1, dp2);
        }

        a--;
        dp1++;
        dp2--;
      }
    }
    else {
      int b;

      for (b = 0; b < nu->pntsv; b++) {

        dp1 = &nu->dp[b * nu->pntsu];
        a = nu->pntsu;
        dp2 = dp1 + (a - 1);
        a /= 2;

        while (dp1 != dp2 && a > 0) {
          index1 = getCVKeyIndex(editnurb, dp1);
          index2 = getCVKeyIndex(editnurb, dp2);

          if (index1) {
            index1->switched = !index1->switched;
          }

          if (dp1 != dp2) {
            if (index2) {
              index2->switched = !index2->switched;
            }

            keyIndex_swap(editnurb, dp1, dp2);
          }

          a--;
          dp1++;
          dp2--;
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
        DPoint *dp = nu->dp;
        a = nu->pntsu * nu->pntsv;
        if (nu == actnu) {
          while (a--) {
            if (getKeyIndexOrig_bp(editnurb, dp)) {
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

  gh = lib_ghash_ptr_new_ex("dupli_keyIndex gh", lib_ghash_len(keyindex));

  GHASH_ITER (gh_iter, keyindex) {
    void *cv = lib_ghashIterator_getKey(&gh_iter);
    CVKeyIndex *index = lib_ghashIterator_getValue(&gh_iter);
    CVKeyIndex *newIndex = MEM_mallocN(sizeof(CVKeyIndex), "dupli_keyIndexHash index");

    memcpy(newIndex, index, sizeof(CVKeyIndex));
    newIndex->orig_cv = MEM_dupallocN(index->orig_cv);

    lib_ghash_insert(gh, cv, newIndex);
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

        dune_nurb_handle_calc(&cur, prevp ? &prev : NULL, nextp ? &next : NULL, 0, 0);
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
static void calc_shapeKeys(Object *obedit, ListBase *newnurbs)
{
  Curve *cu = (Curve *)obedit->data;

  if (cu->key == NULL) {
    return;
  }

  int a, i;
  EditNurb *editnurb = cu->editnurb;
  KeyBlock *actkey = lib_findlink(&cu->key->block, editnurb->shapenr - 1);
  BezTriple *bezt, *oldbezt;
  DPoint *dp, *olddp;
  Nurb *newnu;
  int totvert = dune_keyblock_curve_element_count(&editnurb->nurbs);

  float(*ofs)[3] = NULL;
  float *oldkey, *newkey, *ofp;

  /* editing the base key should update others */
  if (cu->key->type == KEY_RELATIVE) {
    if (dune_keyblock_is_basis(cu->key, editnurb->shapenr - 1)) { /* active key is a base */
      int totvec = 0;

      /* Calculate needed memory to store offset */
      LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {

        if (nu->bezt) {
          /* Three vects to store handles and one for tilt. */
          totvec += nu->pntsu * 4;
        }
        else {
          totvec += 2 * nu->pntsu * nu->pntsv;
        }
      }

      ofs = MEM_callocN(sizeof(float[3]) * totvec, "currkey->data");
      i = 0;
      LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
        if (nu->bezt) {
          bezt = nu->bezt;
          a = nu->pntsu;
          while (a--) {
            oldbezt = getKeyIndexOrig_bezt(editnurb, bezt);

            if (oldbezt) {
              int j;
              for (j = 0; j < 3; j++) {
                sub_v3_v3v3(ofs[i], bezt->vec[j], oldbezt->vec[j]);
                i++;
              }
              ofs[i][0] = bezt->tilt - oldbezt->tilt;
              ofs[i][1] = bezt->radius - oldbezt->radius;
              i++;
            }
            else {
              i += 4;
            }
            bezt++;
          }
        }
        else {
          dp = nu->dp;
          a = nu->pntsu * nu->pntsv;
          while (a--) {
            oldbp = getKeyIndexOrig_dp(editnurb, dp);
            if (oldbp) {
              sub_v3_v3v3(ofs[i], dp->vec, olddp->vec);
              ofs[i + 1][0] = dp->tilt - olddp->tilt;
              ofs[i + 1][1] = dp->radius - olddp->radius;
            }
            i += 2;
            dp++;
          }
        }
      }
    }
  }

  LISTBASE_FOREACH (KeyBlock *, currkey, &cu->key->block) {
    const bool apply_offset = (ofs && (currkey != actkey) &&
                               (editnurb->shapenr - 1 == currkey->relative));

    float *fp = newkey = MEM_callocN(cu->key->elemsize * totvert, "currkey->data");
    ofp = oldkey = currkey->data;

    Nurb *nu = editnurb->nurbs.first;
    /* We need to restore to original curve into newnurb, *not* editcurve's nurbs.
     * Otherwise, in case we update obdata *without* leaving editmode (e.g. viewport render),
     * we would invalidate editcurve. */
    newnu = newnurbs->first;
    i = 0;
    while (nu) {
      if (currkey == actkey) {
        const bool restore = actkey != cu->key->refkey;

        if (nu->bezt) {
          bezt = nu->bezt;
          a = nu->pntsu;
          BezTriple *newbezt = newnu->bezt;
          while (a--) {
            int j;
            oldbezt = getKeyIndexOrig_bezt(editnurb, bezt);

            for (j = 0; j < 3; j++, i++) {
              copy_v3_v3(&fp[j * 3], bezt->vec[j]);

              if (restore && oldbezt) {
                copy_v3_v3(newbezt->vec[j], oldbezt->vec[j]);
              }
            }
            fp[9] = bezt->tilt;
            fp[10] = bezt->radius;

            if (restore && oldbezt) {
              newbezt->tilt = oldbezt->tilt;
              newbezt->radius = oldbezt->radius;
            }

            fp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
            i++;
            bezt++;
            newbezt++;
          }
        }
        else {
          dp = nu->dp;
          a = nu->pntsu * nu->pntsv;
          DPoint *newdp = newnu->dp;
          while (a--) {
            oldbp = getKeyIndexOrig_bp(editnurb, dp);

            copy_v3_v3(fp, dp->vec);

            fp[3] = dp->tilt;
            fp[4] = dp->radius;

            if (restore && olddp) {
              copy_v3_v3(newdp->vec, olddp->vec);
              newbp->tilt = olddp->tilt;
              newbp->radius = olddp->radius;
            }

            fp += KEYELEM_FLOAT_LEN_BPOINT;
            dp++;
            newdp++;
            i += 2;
          }
        }
      }
      else {
        int index;
        const float *curofp;

        if (oldkey) {
          if (nu->bezt) {
            bezt = nu->bezt;
            a = nu->pntsu;

            while (a--) {
              index = getKeyIndexOrig_keyIndex(editnurb, bezt);
              if (index >= 0) {
                int j;
                curofp = ofp + index;

                for (j = 0; j < 3; j++, i++) {
                  copy_v3_v3(&fp[j * 3], &curofp[j * 3]);

                  if (apply_offset) {
                    add_v3_v3(&fp[j * 3], ofs[i]);
                  }
                }
                fp[9] = curofp[9];
                fp[10] = curofp[10];

                if (apply_offset) {
                  /* Apply tilt offsets. */
                  add_v3_v3(fp + 9, ofs[i]);
                  i++;
                }

                fp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
              }
              else {
                int j;
                for (j = 0; j < 3; j++, i++) {
                  copy_v3_v3(&fp[j * 3], bezt->vec[j]);
                }
                fp[9] = bezt->tilt;
                fp[10] = bezt->radius;

                fp += KEYELEM_FLOAT_LEN_BEZTRIPLE;
              }
              bezt++;
            }
          }
          else {
            dp = nu->dp;
            a = nu->pntsu * nu->pntsv;
            while (a--) {
              index = getKeyIndexOrig_keyIndex(editnurb, bp);

              if (index >= 0) {
                curofp = ofp + index;
                copy_v3_v3(fp, curofp);
                fp[3] = curofp[3];
                fp[4] = curofp[4];

                if (apply_offset) {
                  add_v3_v3(fp, ofs[i]);
                  add_v3_v3(&fp[3], ofs[i + 1]);
                }
              }
              else {
                copy_v3_v3(fp, bp->vec);
                fp[3] = bp->tilt;
                fp[4] = bp->radius;
              }

              fp += KEYELEM_FLOAT_LEN_BPOINT;
              dp++;
              i += 2;
            }
          }
        }
      }

      nu = nu->next;
      newnu = newnu->next;
    }

    if (apply_offset) {
      /* handles could become malicious after offsets applying */
      calc_keyHandles(&editnurb->nurbs, newkey);
    }

    currkey->totelem = totvert;
    if (currkey->data) {
      MEM_freeN(currkey->data);
    }
    currkey->data = newkey;
  }

  if (ofs) {
    MEM_freeN(ofs);
  }
}

/* -------------------------------------------------------------------- */
/** Animation Data **/

static bool curve_is_animated(Curve *cu)
{
  AnimData *ad = dune_animdata_from_id(&cu->id);

  return ad && (ad->action || ad->drivers.first);
}

static void fcurve_path_rename(AnimData *adt,
                               const char *orig_api_path,
                               const char *api_path,
                               ListBase *orig_curves,
                               ListBase *curves)
{
  FCurve *nfcu;
  int len = strlen(orig_api_path);

  LISTBASE_FOREACH_MUTABLE (FCurve *, fcu, orig_curves) {
    if (STREQLEN(fcu->api_path, orig_api_path, len)) {
      char *spath, *suffix = fcu->api_path + len;
      nfcu = dune_fcurve_copy(fcu);
      spath = nfcu->rna_path;
      nfcu->rna_path = lib_sprintfN("%s%s", rna_path, suffix);

      /* dune_fcurve_copy() sets nfcu->grp to NULL. To maintain the groups, we need to keep the
       * pointer. As a result, the group's 'channels' pointers will be wrong, which is fixed by
       * calling `action_groups_reconstruct(action)` later, after all fcurves have been renamed. */
      nfcu->grp = fcu->grp;
      lib_addtail(curves, nfcu);

      if (fcu->grp) {
        action_groups_remove_channel(adt->action, fcu);
      }
      else if ((adt->action) && (&adt->action->curves == orig_curves)) {
        lib_remlink(&adt->action->curves, fcu);
      }
      else {
        lib_remlink(&adt->drivers, fcu);
      }

      dune_fcurve_free(fcu);

      MEM_freeN(spath);
    }
  }
}

static void fcurve_remove(AnimData *adt, ListBase *orig_curves, FCurve *fcu)
{
  if (orig_curves == &adt->drivers) {
    lib_remlink(&adt->drivers, fcu);
  }
  else {
    action_groups_remove_channel(adt->action, fcu);
  }

  dune_fcurve_free(fcu);
}

static void curve_rename_fcurves(Curve *cu, ListBase *orig_curves)
{
  int a, pt_index;
  EditNurb *editnurb = cu->editnurb;
  CVKeyIndex *keyIndex;
  char rna_path[64], orig_api_path[64];
  AnimData *adt = dune_animdata_from_id(&cu->id);
  ListBase curves = {NULL, NULL};

  int nu_index = 0;
  LISTBASE_FOREACH_INDEX (Nurb *, nu, &editnurb->nurbs, nu_index) {
    if (nu->bezt) {
      BezTriple *bezt = nu->bezt;
      a = nu->pntsu;
      pt_index = 0;

      while (a--) {
        keyIndex = getCVKeyIndex(editnurb, bezt);
        if (keyIndex) {
          lib_snprintf(
              api_path, sizeof(api_path), "splines[%d].bezier_points[%d]", nu_index, pt_index);
          lib_snprintf(orig_api_path,
                       sizeof(orig_api_path),
                       "splines[%d].bezier_points[%d]",
                       keyIndex->nu_index,
                       keyIndex->pt_index);

          if (keyIndex->switched) {
            char handle_path[64], orig_handle_path[64];
            lib_snprintf(orig_handle_path, sizeof(orig_api_path), "%s.handle_left", orig_rna_path);
            lib_snprintf(handle_path, sizeof(api_path), "%s.handle_right", rna_path);
            fcurve_path_rename(adt, orig_handle_path, handle_path, orig_curves, &curves);

            lib_snprintf(
                orig_handle_path, sizeof(orig_api_path), "%s.handle_right", orig_api_path);
            lib_snprintf(handle_path, sizeof(api_path), "%s.handle_left", api_path);
            fcurve_path_rename(adt, orig_handle_path, handle_path, orig_curves, &curves);
          }

          fcurve_path_rename(adt, orig_rna_path, rna_path, orig_curves, &curves);

          keyIndex->nu_index = nu_index;
          keyIndex->pt_index = pt_index;
        }

        bezt++;
        pt_index++;
      }
    }
    else {
      DPoint *dp = nu->dp;
      a = nu->pntsu * nu->pntsv;
      pt_index = 0;

      while (a--) {
        keyIndex = getCVKeyIndex(editnurb, bp);
        if (keyIndex) {
          lib_snprintf(api_path, sizeof(api_path), "splines[%d].points[%d]", nu_index, pt_index);
          lib_snprintf(orig_api_path,
                       sizeof(orig_api_path),
                       "splines[%d].points[%d]",
                       keyIndex->nu_index,
                       keyIndex->pt_index);
          fcurve_path_rename(adt, orig_api_path, api_path, orig_curves, &curves);

          keyIndex->nu_index = nu_index;
          keyIndex->pt_index = pt_index;
        }

        bp++;
        pt_index++;
      }
    }
  }

  /* remove paths for removed control points
   * need this to make further step with copying non-cv related curves copying
   * not touching cv's f-curves */
  LISTBASE_FOREACH_MUTABLE (FCurve *, fcu, orig_curves) {
    if (STRPREFIX(fcu->api_path, "splines")) {
      const char *ch = strchr(fcu->api_path, '.');

      if (ch && (STRPREFIX(ch, ".bezier_points") || STRPREFIX(ch, ".points"))) {
        fcurve_remove(adt, orig_curves, fcu);
      }
    }
  }

  nu_index = 0;
  LISTBASE_FOREACH_INDEX (Nurb *, nu, &editnurb->nurbs, nu_index) {
    keyIndex = NULL;
    if (nu->pntsu) {
      if (nu->bezt) {
        keyIndex = getCVKeyIndex(editnurb, &nu->bezt[0]);
      }
      else {
        keyIndex = getCVKeyIndex(editnurb, &nu->bp[0]);
      }
    }

    if (keyIndex) {
      lib_snprintf(api_path, sizeof(api_path), "splines[%d]", nu_index);
      lib_snprintf(orig_api_path, sizeof(orig_rna_path), "splines[%d]", keyIndex->nu_index);
      fcurve_path_rename(adt, orig_api_path, api_path, orig_curves, &curves);
    }
  }

  /* the remainders in orig_curves can be copied back (like follow path) */
  /* (if it's not path to spline) */
  LISTBASE_FOREACH_MUTABLE (FCurve *, fcu, orig_curves) {
    if (STRPREFIX(fcu->api_path, "splines")) {
      fcurve_remove(adt, orig_curves, fcu);
    }
    else {
      lib_addtail(&curves, fcu);
    }
  }

  *orig_curves = curves;
  if (adt != NULL) {
    dune_action_groups_reconstruct(adt->action);
  }
}

int ED_curve_updateAnimPaths(Main *duneMain, Curve *cu)
{
  AnimData *adt = dune_animdata_from_id(&cu->id);
  EditNurb *editnurb = cu->editnurb;

  if (!editnurb->keyindex) {
    return 0;
  }

  if (!curve_is_animated(cu)) {
    return 0;
  }

  if (adt->action != NULL) {
    curve_rename_fcurves(cu, &adt->action->curves);
    DEG_id_tag_update(&adt->action->id, ID_RECALC_COPY_ON_WRITE);
  }

  curve_rename_fcurves(cu, &adt->drivers);
  DEG_id_tag_update(&cu->id, ID_RECALC_COPY_ON_WRITE);

  /* TODO(sergey): Only update if something actually changed. */
  DEG_relations_tag_update(bmain);

  return 1;
}

/* -------------------------------------------------------------------- */
/** Edit Mode Conversion (Make & Load) **/

static int *init_index_map(Object *obedit, int *r_old_totvert)
{
  Curve *curve = (Curve *)obedit->data;
  EditNurb *editnurb = curve->editnurb;
  CVKeyIndex *keyIndex;
  int *old_to_new_map;

  int old_totvert = 0;
  LISTBASE_FOREACH (Nurb *, nu, &curve->nurb) {
    if (nu->bezt) {
      old_totvert += nu->pntsu * 3;
    }
    else {
      old_totvert += nu->pntsu * nu->pntsv;
    }
  }

  old_to_new_map = MEM_mallocN(old_totvert * sizeof(int), "curve old to new index map");
  for (int i = 0; i < old_totvert; i++) {
    old_to_new_map[i] = -1;
  }

  int vertex_index = 0;
  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    if (nu->bezt) {
      BezTriple *bezt = nu->bezt;
      int a = nu->pntsu;

      while (a--) {
        keyIndex = getCVKeyIndex(editnurb, bezt);
        if (keyIndex && keyIndex->vertex_index + 2 < old_totvert) {
          if (keyIndex->switched) {
            old_to_new_map[keyIndex->vertex_index] = vertex_index + 2;
            old_to_new_map[keyIndex->vertex_index + 1] = vertex_index + 1;
            old_to_new_map[keyIndex->vertex_index + 2] = vertex_index;
          }
          else {
            old_to_new_map[keyIndex->vertex_index] = vertex_index;
            old_to_new_map[keyIndex->vertex_index + 1] = vertex_index + 1;
            old_to_new_map[keyIndex->vertex_index + 2] = vertex_index + 2;
          }
        }
        vertex_index += 3;
        bezt++;
      }
    }
    else {
      DPoint *dp = nu->dp;
      int a = nu->pntsu * nu->pntsv;

      while (a--) {
        keyIndex = getCVKeyIndex(editnurb, bp);
        if (keyIndex) {
          old_to_new_map[keyIndex->vertex_index] = vertex_index;
        }
        vertex_index++;
        bp++;
      }
    }
  }

  *r_old_totvert = old_totvert;
  return old_to_new_map;
}

static void remap_hooks_and_vertex_parents(Main *duneMain, Object *obedit)
{
  Curve *curve = (Curve *)obedit->data;
  EditNurb *editnurb = curve->editnurb;
  int *old_to_new_map = NULL;
  int old_totvert;

  if (editnurb->keyindex == NULL) {
    /* TODO: Happens when separating curves, this would lead to
     * the wrong indices in the hook modifier, address this together with
     * other indices issues.
     */
    return;
  }

  LISTBASE_FOREACH (Object *, object, &dmain->objects) {
    int index;
    if ((object->parent) && (object->parent->data == curve) &&
        ELEM(object->partype, PARVERT1, PARVERT3)) {
      if (old_to_new_map == NULL) {
        old_to_new_map = init_index_map(obedit, &old_totvert);
      }

      if (object->par1 < old_totvert) {
        index = old_to_new_map[object->par1];
        if (index != -1) {
          object->par1 = index;
        }
      }
      if (object->par2 < old_totvert) {
        index = old_to_new_map[object->par2];
        if (index != -1) {
          object->par2 = index;
        }
      }
      if (object->par3 < old_totvert) {
        index = old_to_new_map[object->par3];
        if (index != -1) {
          object->par3 = index;
        }
      }
    }
    if (object->data == curve) {
      LISTBASE_FOREACH (ModifierData *, md, &object->modifiers) {
        if (md->type == eModifierType_Hook) {
          HookModifierData *hmd = (HookModifierData *)md;
          int i, j;

          if (old_to_new_map == NULL) {
            old_to_new_map = init_index_map(obedit, &old_totvert);
          }

          for (i = j = 0; i < hmd->totindex; i++) {
            if (hmd->indexar[i] < old_totvert) {
              index = old_to_new_map[hmd->indexar[i]];
              if (index != -1) {
                hmd->indexar[j++] = index;
              }
            }
            else {
              j++;
            }
          }

          hmd->totindex = j;
        }
      }
    }
  }
  if (old_to_new_map != NULL) {
    MEM_freeN(old_to_new_map);
  }
}

void ED_curve_editnurb_load(Main *dmain, Object *obedit)
{
  ListBase *editnurb = object_editcurve_get(obedit);

  if (obedit == NULL) {
    return;
  }

  if (ELEM(obedit->type, OB_CURVES_LEGACY, OB_SURF)) {
    Curve *cu = obedit->data;
    ListBase newnurb = {NULL, NULL}, oldnurb = cu->nurb;

    remap_hooks_and_vertex_parents(dmain, obedit);

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      Nurb *newnu = dune_nurb_duplicate(nu);
      lib_addtail(&newnurb, newnu);

      if (nu->type == CU_NURBS) {
        dune_nurb_order_clamp_u(nu);
      }
    }

    /* We have to pass also new copied nurbs, since we want to restore original curve
     * (without edited shapekey) on obdata, but *not* on editcurve itself
     * (ED_curve_editnurb_load call does not always implies freeing
     * of editcurve, e.g. when called to generate render data). */
    calc_shapeKeys(obedit, &newnurb);

    cu->nurb = newnurb;

    ED_curve_updateAnimPaths(dmain, obedit->data);

    dune_nurbList_free(&oldnurb);
  }
}

void ED_curve_editnurb_make(Object *obedit)
{
  Curve *cu = (Curve *)obedit->data;
  EditNurb *editnurb = cu->editnurb;
  KeyBlock *actkey;

  if (ELEM(obedit->type, OB_CURVES_LEGACY, OB_SURF)) {
    actkey = dune_keyblock_from_object(obedit);

    if (actkey) {
      // XXX strcpy(G.editModeTitleExtra, "(Key) ");
      /* TODO: undo_system: investigate why this was needed. */
#if 0
      undo_editmode_clear();
#endif
    }

    if (editnurb) {
      dune_nurbList_free(&editnurb->nurbs);
      dune_curve_editNurb_keyIndex_free(&editnurb->keyindex);
    }
    else {
      editnurb = MEM_callocN(sizeof(EditNurb), "editnurb");
      cu->editnurb = editnurb;
    }

    LISTBASE_FOREACH (Nurb *, nu, &cu->nurb) {
      Nurb *newnu = dune_nurb_duplicate(nu);
      lib_addtail(&editnurb->nurbs, newnu);
    }

    /* animation could be added in editmode even if there was no animdata in
     * object mode hence we always need CVs index be created */
    init_editNurb_keyIndex(editnurb, &cu->nurb);

    if (actkey) {
      editnurb->shapenr = obedit->shapenr;
      /* Apply shapekey to new nurbs of editnurb, not those of original curve
       * (and *after* we generated keyIndex), else we do not have valid 'original' data
       * to properly restore curve when leaving editmode. */
      dune_keyblock_convert_to_curve(actkey, cu, &editnurb->nurbs);
    }
  }
}

void ED_curve_editnurb_free(Object *obedit)
{
  Curve *cu = obedit->data;

  dune_curve_editNurb_free(cu);
}

/* -------------------------------------------------------------------- */
/** Separate Operator **/

static int separate_exec(dContext *C, wmOperator *op)
{
  Main *dmain = ctx_data_main(C);
  Scene *scene = ctx_data_scene(C);
  ViewLayer *view_layer = ctx_data_view_layer(C);
  View3D *v3d = ctx_wm_view3d(C);

  struct {
    int changed;
    int unselected;
    int error_vertex_keys;
    int error_generic;
  } status = {0};

  WM_cursor_wait(true);

  uint bases_len = 0;
  Base **bases = dune_view_layer_array_from_bases_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &bases_len);
  for (uint b_index = 0; b_index < bases_len; b_index++) {
    Base *oldbase = bases[b_index];
    Base *newbase;
    Object *oldob, *newob;
    Curve *oldcu, *newcu;
    EditNurb *newedit;
    ListBase newnurb = {NULL, NULL};

    oldob = oldbase->object;
    oldcu = oldob->data;

    if (oldcu->key) {
      status.error_vertex_keys++;
      continue;
    }

    if (!ED_curve_select_check(v3d, oldcu->editnurb)) {
      status.unselected++;
      continue;
    }

    /* 1. Duplicate geometry and check for valid selection for separate. */
    adduplicateflagNurb(oldob, v3d, &newnurb, SELECT, true);

    if (lib_listbase_is_empty(&newnurb)) {
      status.error_generic++;
      continue;
    }

    /* 2. Duplicate the object and data. */

    /* Take into account user preferences for duplicating actions. */
    const eDupli_ID_Flags dupflag = (U.dupflag & USER_DUP_ACT);

    newbase = ED_object_add_duplicate(dmain, scene, view_layer, oldbase, dupflag);
    DEG_relations_tag_update(dmain);

    newob = newbase->object;
    newcu = newob->data = dune_id_copy(dmain, &oldcu->id);
    newcu->editnurb = NULL;
    id_us_min(&oldcu->id); /* Because new curve is a copy: reduce user count. */

    /* 3. Put new object in editmode, clear it and set separated nurbs. */
    ED_curve_editnurb_make(newob);
    newedit = newcu->editnurb;
    dune_nurbList_free(&newedit->nurbs);
    dune_curve_editNurb_keyIndex_free(&newedit->keyindex);
    lib_movelisttolist(&newedit->nurbs, &newnurb);

    /* 4. Put old object out of editmode and delete separated geometry. */
    ED_curve_editnurb_load(dmain, newob);
    ED_curve_editnurb_free(newob);
    curve_delete_segments(oldob, v3d, true);

    DEG_id_tag_update(&oldob->id, ID_RECALC_GEOMETRY); /* This is the original one. */
    DEG_id_tag_update(&newob->id, ID_RECALC_GEOMETRY); /* This is the separated one. */

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, oldob->data);
    WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, newob);
    status.changed++;
  }
  MEM_freeN(bases);
  WM_cursor_wait(false);

  if (status.unselected == bases_len) {
    dune_report(op->reports, RPT_ERROR, "No point was selected");
    return OPERATOR_CANCELLED;
  }

  const int tot_errors = status.error_vertex_keys + status.error_generic;
  if (tot_errors > 0) {

    /* Some curves changed, but some curves failed: don't explain why it failed. */
    if (status.changed) {
      dune_reportf(op->reports, RPT_INFO, "%d curve(s) could not be separated", tot_errors);
      return OPERATOR_FINISHED;
    }

    /* All curves failed: If there is more than one error give a generic error report. */
    if (((status.error_vertex_keys ? 1 : 0) + (status.error_generic ? 1 : 0)) > 1) {
      dune_report(op->reports, RPT_ERROR, "Could not separate selected curve(s)");
    }

    /* All curves failed due to the same error. */
    if (status.error_vertex_keys) {
      dune_report(op->reports, RPT_ERROR, "Cannot separate curves with vertex keys");
    }
    else {
      lib_assert(status.error_generic);
      dune_report(op->reports, RPT_ERROR, "Cannot separate current selection");
    }
    return OPERATOR_CANCELLED;
  }

  ED_outliner_select_sync_from_object_tag(C);

  return OPERATOR_FINISHED;
}

void CURVE_OT_separate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Separate";
  ot->idname = "CURVE_OT_separate";
  ot->description = "Separate selected points from connected unselected points into a new object";

  /* api callbacks */
  ot->invoke = WM_operator_confirm;
  ot->exec = separate_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Split Operator **/

static int curve_split_exec(dContext *C, wmOperator *op)
{
  Main *dmain = ctx_data_main(C);
  ViewLayer *view_layer = ctx_data_view_layer(C);
  View3D *v3d = ctx_wm_view3d(C);
  int ok = -1;

  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!ED_curve_select_check(v3d, cu->editnurb)) {
      continue;
    }

    ListBase newnurb = {NULL, NULL};

    adduplicateflagNurb(obedit, v3d, &newnurb, SELECT, true);

    if (lib_listbase_is_empty(&newnurb)) {
      ok = MAX2(ok, 0);
      continue;
    }

    ListBase *editnurb = object_editcurve_get(obedit);
    const int len_orig = lib_listbase_count(editnurb);

    curve_delete_segments(obedit, v3d, true);
    cu->actnu -= len_orig - lib_listbase_count(editnurb);
    lib_movelisttolist(editnurb, &newnurb);

    if (ED_curve_updateAnimPaths(dmain, obedit->data)) {
      WM_event_add_notifier(C, NC_OBJECT | ND_KEYS, obedit);
    }

    ok = 1;
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }
  MEM_freeN(objects);

  if (ok == 0) {
    dune_report(op->reports, RPT_ERROR, "Cannot split current selection");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void CURVE_OT_split(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Split";
  ot->idname = "CURVE_OT_split";
  ot->description = "Split off selected points from connected unselected points";

  /* api callbacks */
  ot->exec = curve_split_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Flag Utility Functions **/

static bool isNurbselUV(const Nurb *nu, uint8_t flag, int *r_u, int *r_v)
{
  /* return (u != -1): 1 row in u-direction selected. U has value between 0-pntsv
   * return (v != -1): 1 column in v-direction selected. V has value between 0-pntsu
   */
  DPoint *dp;
  int a, b, sel;

  *r_u = *r_v = -1;

  dp = nu->dp;
  for (b = 0; b < nu->pntsv; b++) {
    sel = 0;
    for (a = 0; a < nu->pntsu; a++, bp++) {
      if (dp->f1 & flag) {
        sel++;
      }
    }
    if (sel == nu->pntsu) {
      if (*r_u == -1) {
        *r_u = b;
      }
      else {
        return 0;
      }
    }
    else if (sel > 1) {
      return 0; /* because sel == 1 is still ok */
    }
  }

  for (a = 0; a < nu->pntsu; a++) {
    sel = 0;
    dp = &nu->bp[a];
    for (b = 0; b < nu->pntsv; b++, bp += nu->pntsu) {
      if (dp->f1 & flag) {
        sel++;
      }
    }
    if (sel == nu->pntsv) {
      if (*r_v == -1) {
        *r_v = a;
      }
      else {
        return 0;
      }
    }
    else if (sel > 1) {
      return 0;
    }
  }

  if (*r_u == -1 && *r_v > -1) {
    return 1;
  }
  if (*r_v == -1 && *r_u > -1) {
    return 1;
  }
  return 0;
}

/* return true if U direction is selected and number of selected columns v */
static bool isNurbselU(Nurb *nu, int *v, int flag)
{
  DPoint *dp;
  int a, b, sel;

  *v = 0;

  for (b = 0, bp = nu->bp; b < nu->pntsv; b++) {
    sel = 0;
    for (a = 0; a < nu->pntsu; a++, bp++) {
      if (bp->f1 & flag) {
        sel++;
      }
    }
    if (sel == nu->pntsu) {
      (*v)++;
    }
    else if (sel >= 1) {
      *v = 0;
      return 0;
    }
  }

  return 1;
}

/* return true if V direction is selected and number of selected rows u */
static bool isNurbselV(Nurb *nu, int *u, int flag)
{
  DPoint *dp;
  int a, b, sel;

  *u = 0;

  for (a = 0; a < nu->pntsu; a++) {
    bp = &nu->dp[a];
    sel = 0;
    for (b = 0; b < nu->pntsv; b++, bp += nu->pntsu) {
      if (dp->f1 & flag) {
        sel++;
      }
    }
    if (sel == nu->pntsv) {
      (*u)++;
    }
    else if (sel >= 1) {
      *u = 0;
      return 0;
    }
  }

  return 1;
}

static void rotateflagNurb(ListBase *editnurb,
                           short flag,
                           const float cent[3],
                           const float rotmat[3][3])
{
  /* all verts with (flag & 'flag') rotate */
  DPoint *dp;
  int a;

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    if (nu->type == CU_NURBS) {
      dp = nu->dp;
      a = nu->pntsu * nu->pntsv;

      while (a--) {
        if (bp->f1 & flag) {
          sub_v3_v3(bp->vec, cent);
          mul_m3_v3(rotmat, bp->vec);
          add_v3_v3(bp->vec, cent);
        }
        dp++;
      }
    }
  }
}

void ed_editnurb_translate_flag(ListBase *editnurb, uint8_t flag, const float vec[3], bool is_2d)
{
  /* all verts with ('flag' & flag) translate */
  BezTriple *bezt;
  DPoint *dp;
  int a;

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    if (nu->type == CU_BEZIER) {
      a = nu->pntsu;
      bezt = nu->bezt;
      while (a--) {
        if (bezt->f1 & flag) {
          add_v3_v3(bezt->vec[0], vec);
        }
        if (bezt->f2 & flag) {
          add_v3_v3(bezt->vec[1], vec);
        }
        if (bezt->f3 & flag) {
          add_v3_v3(bezt->vec[2], vec);
        }
        bezt++;
      }
    }
    else {
      a = nu->pntsu * nu->pntsv;
      dp = nu->dp;
      while (a--) {
        if (dp->f1 & flag) {
          add_v3_v3(dp->vec, vec);
        }
        dp++;
      }
    }

    if (is_2d) {
      dune_nurb_project_2d(nu);
    }
  }
}
static void weightflagNurb(ListBase *editnurb, short flag, float w)
{
  DPoint *dp;
  int a;

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    if (nu->type == CU_NURBS) {
      a = nu->pntsu * nu->pntsv;
      dp = nu->dp;
      while (a--) {
        if (dp->f1 & flag) {
          /* a mode used to exist for replace/multiple but is was unused */
          dp->vec[3] *= w;
        }
        dp++;
      }
    }
  }
}

static void ed_surf_delete_selected(Object *obedit)
{
  Curve *cu = obedit->data;
  ListBase *editnurb = object_editcurve_get(obedit);
  DPoint *dp, *dpn, *newdp;
  int a, b, newu, newv;

  lib_assert(obedit->type == OB_SURF);

  LISTBASE_FOREACH_MUTABLE (Nurb *, nu, editnurb) {
    /* is entire nurb selected */
    dp = nu->dp;
    a = nu->pntsu * nu->pntsv;
    while (a) {
      a--;
      if (dp->f1 & SELECT) {
        /* pass */
      }
      else {
        break;
      }
      dp++;
    }
    if (a == 0) {
      lib_remlink(editnurb, nu);
      keyIndex_delNurb(cu->editnurb, nu);
      dune_nurb_free(nu);
      nu = NULL;
    }
    else {
      if (isNurbselU(nu, &newv, SELECT)) {
        /* U direction selected */
        newv = nu->pntsv - newv;
        if (newv != nu->pntsv) {
          /* delete */
          dp = nu->dp;
          dpn = newdp = (DPoint *)MEM_mallocN(newv * nu->pntsu * sizeof(DPoint), "deleteNurb");
          for (b = 0; b < nu->pntsv; b++) {
            if ((dp->f1 & SELECT) == 0) {
              memcpy(dpn, dp, nu->pntsu * sizeof(DPoint));
              keyIndex_updateDP(cu->editnurb, dp, dpn, nu->pntsu);
              dpn += nu->pntsu;
            }
            else {
              keyIndex_delDP(cu->editnurb, bp);
            }
            dp += nu->pntsu;
          }
          nu->pntsv = newv;
          MEM_freeN(nu->dp);
          nu->bp = newbp;
          dune_nurb_order_clamp_v(nu);

          dune_nurb_knot_calc_v(nu);
        }
      }
      else if (isNurbselV(nu, &newu, SELECT)) {
        /* V direction selected */
        newu = nu->pntsu - newu;
        if (newu != nu->pntsu) {
          /* delete */
          dp = nu->dp;
          dpn = newdp = (DPoint *)MEM_mallocN(newu * nu->pntsv * sizeof(BPoint), "deleteNurb");
          for (b = 0; b < nu->pntsv; b++) {
            for (a = 0; a < nu->pntsu; a++, bp++) {
              if ((bp->f1 & SELECT) == 0) {
                *dpn = *dp;
                keyIndex_updateBP(cu->editnurb, dp, dpn, 1);
                bpn++;
              }
              else {
                keyIndex_delBP(cu->editnurb, bp);
              }
            }
          }
          MEM_freeN(nu->dp);
          nu->dp = newdp;
          if (newu == 1 && nu->pntsv > 1) { /* make a U spline */
            nu->pntsu = nu->pntsv;
            nu->pntsv = 1;
            SWAP(short, nu->orderu, nu->orderv);
            dune_nurb_order_clamp_u(nu);
            MEM_SAFE_FREE(nu->knotsv);
          }
          else {
            nu->pntsu = newu;
            dune_nurb_order_clamp_u(nu);
          }
          dune_nurb_knot_calc_u(nu);
        }
      }
    }
  }
}

static void ed_curve_delete_selected(Object *obedit, View3D *v3d)
{
  Curve *cu = obedit->data;
  EditNurb *editnurb = cu->editnurb;
  ListBase *nubase = &editnurb->nurbs;
  BezTriple *bezt, *bezt1;
  DPoint *bp, *bp1;
  int a, type, nuindex = 0;

  /* first loop, can we remove entire pieces? */
  LISTBASE_FOREACH_MUTABLE (Nurb *, nu, nubase) {
    if (nu->type == CU_BEZIER) {
      bezt = nu->bezt;
      a = nu->pntsu;
      if (a) {
        while (a) {
          if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
            /* pass */
          }
          else {
            break;
          }
          a--;
          bezt++;
        }
        if (a == 0) {
          if (cu->actnu == nuindex) {
            cu->actnu = CU_ACT_NONE;
          }

          lib_remlink(nubase, nu);
          keyIndex_delNurb(editnurb, nu);
          lib_nurb_free(nu);
          nu = NULL;
        }
      }
    }
    else {
      bp = nu->dp;
      a = nu->pntsu * nu->pntsv;
      if (a) {
        while (a) {
          if (dp->f1 & SELECT) {
            /* pass */
          }
          else {
            break;
          }
          a--;
          dp++;
        }
        if (a == 0) {
          if (cu->actnu == nuindex) {
            cu->actnu = CU_ACT_NONE;
          }

          lib_remlink(nubase, nu);
          keyIndex_delNurb(editnurb, nu);
          dune_nurb_free(nu);
          nu = NULL;
        }
      }
    }

    /* Never allow the order to exceed the number of points
     * NOTE: this is ok but changes unselected nurbs, disable for now. */
#if 0
    if ((nu != NULL) && (nu->type == CU_NURBS)) {
      clamp_nurb_order_u(nu);
    }
#endif
    nuindex++;
  }
  /* 2nd loop, delete small pieces: just for curves */
  LISTBASE_FOREACH_MUTABLE (Nurb *, nu, nubase) {
    type = 0;
    if (nu->type == CU_BEZIER) {
      bezt = nu->bezt;
      for (a = 0; a < nu->pntsu; a++) {
        if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
          memmove(bezt, bezt + 1, (nu->pntsu - a - 1) * sizeof(BezTriple));
          keyIndex_delBezt(editnurb, bezt);
          keyIndex_updateBezt(editnurb, bezt + 1, bezt, nu->pntsu - a - 1);
          nu->pntsu--;
          a--;
          type = 1;
        }
        else {
          bezt++;
        }
      }
      if (type) {
        bezt1 = (BezTriple *)MEM_mallocN((nu->pntsu) * sizeof(BezTriple), "delNurb");
        memcpy(bezt1, nu->bezt, (nu->pntsu) * sizeof(BezTriple));
        keyIndex_updateBezt(editnurb, nu->bezt, bezt1, nu->pntsu);
        MEM_freeN(nu->bezt);
        nu->bezt = bezt1;
        dune_nurb_handles_calc(nu);
      }
    }
    else if (nu->pntsv == 1) {
      dp = nu->dp;

      for (a = 0; a < nu->pntsu; a++) {
        if (dp->f1 & SELECT) {
          memmove(dp, dp + 1, (nu->pntsu - a - 1) * sizeof(DPoint));
          keyIndex_delBP(editnurb, dp);
          keyIndex_updateBP(editnurb, dp + 1, dp, nu->pntsu - a - 1);
          nu->pntsu--;
          a--;
          type = 1;
        }
        else {
          dp++;
        }
      }
      if (type) {
        dp1 = (DPoint *)MEM_mallocN(nu->pntsu * sizeof(DPoint), "delNurb2");
        memcpy(dp1, nu->dp, (nu->pntsu) * sizeof(DPoint));
        keyIndex_updateDP(editnurb, nu->dp, dp1, nu->pntsu);
        MEM_freeN(nu->bp);
        nu->bp = bp1;

        /* Never allow the order to exceed the number of points
         * NOTE: this is ok but changes unselected nurbs, disable for now. */
#if 0
        if (nu->type == CU_NURBS) {
          clamp_nurb_order_u(nu);
        }
#endif
      }
      dune_nurb_order_clamp_u(nu);
      dune_nurb_knot_calc_u(nu);
    }
  }
}

bool ed_editnurb_extrude_flag(EditNurb *editnurb, const uint8_t flag)
{
  DPoint *dp, *dpn, *newdp;
  int a, u, v, len;
  bool ok = false;

  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    if (nu->pntsv == 1) {
      bp = nu->dp;
      a = nu->pntsu;
      while (a) {
        if (dp->f1 & flag) {
          /* pass */
        }
        else {
          break;
        }
        dp++;
        a--;
      }
      if (a == 0) {
        ok = true;
        newdp = (DPoint *)MEM_mallocN(2 * nu->pntsu * sizeof(DPoint), "extrudeNurb1");
        ED_curve_dpcpy(editnurb, newdp, nu->dp, nu->pntsu);
        dp = newdp + nu->pntsu;
        ED_curve_dpcpy(editnurb, dp, nu->dp, nu->pntsu);
        MEM_freeN(nu->dp);
        nu->dp = newdp;
        a = nu->pntsu;
        while (a--) {
          select_dpoint(dp, SELECT, flag, HIDDEN);
          select_dpoint(newdp, DESELECT, flag, HIDDEN);
          dp++;
          newdp++;
        }

        nu->pntsv = 2;
        nu->orderv = 2;
        dune_nurb_knot_calc_v(nu);
      }
    }
    else {
      /* which row or column is selected */

      if (isNurbselUV(nu, flag, &u, &v)) {

        /* deselect all */
        dp = nu->dp;
        a = nu->pntsu * nu->pntsv;
        while (a--) {
          select_dpoint(dp, DESELECT, flag, HIDDEN);
          bp++;
        }

        if (ELEM(u, 0, nu->pntsv - 1)) { /* row in u-direction selected */
          ok = true;
          newbp = (DPoint *)MEM_mallocN(nu->pntsu * (nu->pntsv + 1) * sizeof(DPoint),
                                        "extrudeNurb1");
          if (u == 0) {
            len = nu->pntsv * nu->pntsu;
            ED_curve_dpcpy(editnurb, newdp + nu->pntsu, nu->dp, len);
            ED_curve_dpcpy(editnurb, newdp, nu->dp, nu->pntsu);
            dp = newdp;
          }
          else {
            len = nu->pntsv * nu->pntsu;
            ED_curve_dpcpy(editnurb, newdp, nu->dp, len);
            ED_curve_dpcpy(editnurb, newdp + len, &nu->dp[len - nu->pntsu], nu->pntsu);
            dp = newdp + len;
          }

          a = nu->pntsu;
          while (a--) {
            select_dpoint(dp, SELECT, flag, HIDDEN);
            dp++;
          }

          MEM_freeN(nu->bp);
          nu->bp = newbp;
          nu->pntsv++;
          dune_nurb_knot_calc_v(nu);
        }
        else if (ELEM(v, 0, nu->pntsu - 1)) { /* column in v-direction selected */
          ok = true;
          dpn = newbp = (DPoint *)MEM_mallocN((nu->pntsu + 1) * nu->pntsv * sizeof(DPoint),
                                              "extrudeNurb1");
          dp = nu->dp;

          for (a = 0; a < nu->pntsv; a++) {
            if (v == 0) {
              *dpn = *dp;
              dpn->f1 |= flag;
              dpn++;
            }
            ED_curve_bpcpy(editnurb, dpn, dp, nu->pntsu);
            dp += nu->pntsu;
            dpn += nu->pntsu;
            if (v == nu->pntsu - 1) {
              *dpn = *(dp - 1);
              dpn->f1 |= flag;
              dpn++;
            }
          }

          MEM_freeN(nu->dp);
          nu->dp = newdp;
          nu->pntsu++;
          dune_nurb_knot_calc_u(nu);
        }
      }
    }
  }

  return ok;
}

static void calc_duplicate_actnurb(const ListBase *editnurb, const ListBase *newnurb, Curve *cu)
{
  cu->actnu = lib_listbase_count(editnurb) + lib_listbase_count(newnurb);
}

static bool calc_duplicate_actvert(
    const ListBase *editnurb, const ListBase *newnurb, Curve *cu, int start, int end, int vert)
{
  if (cu->actvert == -1) {
    calc_duplicate_actnurb(editnurb, newnurb, cu);
    return true;
  }

  if ((start <= cu->actvert) && (end > cu->actvert)) {
    calc_duplicate_actnurb(editnurb, newnurb, cu);
    cu->actvert = vert;
    return true;
  }
  return false;
}

static void adduplicateflagNurb(
    Object *obedit, View3D *v3d, ListBase *newnurb, const uint8_t flag, const bool split)
{
  ListBase *editnurb = object_editcurve_get(obedit);
  Nurb *newnu;
  BezTriple *bezt, *bezt1;
  DPoint *dp, *dp1, *dp2, *dp3;
  Curve *cu = (Curve *)obedit->data;
  int a, b, c, starta, enda, diffa, cyclicu, cyclicv, newu, newv;
  char *usel;

  int i = 0;
  LISTBASE_FOREACH_INDEX (Nurb *, nu, editnurb, i) {
    cyclicu = cyclicv = 0;
    if (nu->type == CU_BEZIER) {
      for (a = 0, bezt = nu->bezt; a < nu->pntsu; a++, bezt++) {
        enda = -1;
        starta = a;
        while ((bezt->f1 & flag) || (bezt->f2 & flag) || (bezt->f3 & flag)) {
          if (!split) {
            select_beztriple(bezt, DESELECT, flag, HIDDEN);
          }
          enda = a;
          if (a >= nu->pntsu - 1) {
            break;
          }
          a++;
          bezt++;
        }
        if (enda >= starta) {
          newu = diffa = enda - starta + 1; /* set newu and diffa, may use both */

          if (starta == 0 && newu != nu->pntsu && (nu->flagu & CU_NURB_CYCLIC)) {
            cyclicu = newu;
          }
          else {
            if (enda == nu->pntsu - 1) {
              newu += cyclicu;
            }
            if (i == cu->actnu) {
              calc_duplicate_actvert(
                  editnurb, newnurb, cu, starta, starta + diffa, cu->actvert - starta);
            }

            newnu = dune_nurb_copy(nu, newu, 1);
            memcpy(newnu->bezt, &nu->bezt[starta], diffa * sizeof(BezTriple));
            if (newu != diffa) {
              memcpy(&newnu->bezt[diffa], nu->bezt, cyclicu * sizeof(BezTriple));
              if (i == cu->actnu) {
                calc_duplicate_actvert(
                    editnurb, newnurb, cu, 0, cyclicu, newu - cyclicu + cu->actvert);
              }
              cyclicu = 0;
            }

            if (newu != nu->pntsu) {
              newnu->flagu &= ~CU_NURB_CYCLIC;
            }

            for (b = 0, bezt1 = newnu->bezt; b < newnu->pntsu; b++, bezt1++) {
              select_beztriple(bezt1, SELECT, flag, HIDDEN);
            }

            lib_addtail(newnurb, newnu);
          }
        }
      }

      if (cyclicu != 0) {
        if (i == cu->actnu) {
          calc_duplicate_actvert(editnurb, newnurb, cu, 0, cyclicu, cu->actvert);
        }

        newnu = dune_nurb_copy(nu, cyclicu, 1);
        memcpy(newnu->bezt, nu->bezt, cyclicu * sizeof(BezTriple));
        newnu->flagu &= ~CU_NURB_CYCLIC;

        for (b = 0, bezt1 = newnu->bezt; b < newnu->pntsu; b++, bezt1++) {
          select_beztriple(bezt1, SELECT, flag, HIDDEN);
        }

        lib_addtail(newnurb, newnu);
      }
    }
    else if (nu->pntsv == 1) { /* because UV Nurb has a different method for dupli */
      for (a = 0, dp = nu->dp; a < nu->pntsu; a++, dp++) {
        enda = -1;
        starta = a;
        while (dp->f1 & flag) {
          if (!split) {
            select_dpoint(dp, DESELECT, flag, HIDDEN);
          }
          enda = a;
          if (a >= nu->pntsu - 1) {
            break;
          }
          a++;
          dp++;
        }
        if (enda >= starta) {
          newu = diffa = enda - starta + 1; /* set newu and diffa, may use both */

          if (starta == 0 && newu != nu->pntsu && (nu->flagu & CU_NURB_CYCLIC)) {
            cyclicu = newu;
          }
          else {
            if (enda == nu->pntsu - 1) {
              newu += cyclicu;
            }
            if (i == cu->actnu) {
              calc_duplicate_actvert(
                  editnurb, newnurb, cu, starta, starta + diffa, cu->actvert - starta);
            }

            newnu = dune_nurb_copy(nu, newu, 1);
            memcpy(newnu->dp, &nu->bp[starta], diffa * sizeof(DPoint));
            if (newu != diffa) {
              memcpy(&newnu->dp[diffa], nu->dp, cyclicu * sizeof(DPoint));
              if (i == cu->actnu) {
                calc_duplicate_actvert(
                    editnurb, newnurb, cu, 0, cyclicu, newu - cyclicu + cu->actvert);
              }
              cyclicu = 0;
            }

            if (newu != nu->pntsu) {
              newnu->flagu &= ~CU_NURB_CYCLIC;
            }

            for (b = 0, dp1 = newnu->dp; b < newnu->pntsu; b++, dp1++) {
              select_dpoint(dp1, SELECT, flag, HIDDEN);
            }

            lib_addtail(newnurb, newnu);
          }
        }
      }

      if (cyclicu != 0) {
        if (i == cu->actnu) {
          calc_duplicate_actvert(editnurb, newnurb, cu, 0, cyclicu, cu->actvert);
        }

        newnu = dune_nurb_copy(nu, cyclicu, 1);
        memcpy(newnu->bp, nu->dp, cyclicu * sizeof(DPoint));
        newnu->flagu &= ~CU_NURB_CYCLIC;

        for (b = 0, bp1 = newnu->dp; b < newnu->pntsu; b++, dp1++) {
          select_dpoint(dp1, SELECT, flag, HIDDEN);
        }

        lib_addtail(newnurb, newnu);
      }
    }
    else {
      if (ED_curve_nurb_select_check(v3d, nu)) {
        /* A rectangular area in nurb has to be selected and if splitting
         * must be in U or V direction. */
        usel = MEM_callocN(nu->pntsu, "adduplicateN3");
        dp = nu->dp;
        for (a = 0; a < nu->pntsv; a++) {
          for (b = 0; b < nu->pntsu; b++, dp++) {
            if (dp->f1 & flag) {
              usel[b]++;
            }
          }
        }
        newu = 0;
        newv = 0;
        for (a = 0; a < nu->pntsu; a++) {
          if (usel[a]) {
            if (ELEM(newv, 0, usel[a])) {
              newv = usel[a];
              newu++;
            }
            else {
              newv = 0;
              break;
            }
          }
        }
        MEM_freeN(usel);

        if ((newu == 0 || newv == 0) ||
            (split && !isNurbselU(nu, &newv, SELECT) && !isNurbselV(nu, &newu, SELECT))) {
          if (G.debug & G_DEBUG) {
            printf("Can't duplicate Nurb\n");
          }
        }
        else {
          for (a = 0, bp1 = nu->bp; a < nu->pntsu * nu->pntsv; a++, bp1++) {
            newv = newu = 0;

            if ((dp1->f1 & flag) && !(dp1->f1 & SURF_SEEN)) {
              /* point selected, now loop over points in U and V directions */
              for (b = a % nu->pntsu, dp2 = dp1; b < nu->pntsu; b++, dp2++) {
                if (dp2->f1 & flag) {
                  newu++;
                  for (c = a / nu->pntsu, dp3 = dp2; c < nu->pntsv; c++, dp3 += nu->pntsu) {
                    if (dp3->f1 & flag) {
                      /* flag as seen so skipped on future iterations */
                      dp3->f1 |= SURF_SEEN;
                      if (newu == 1) {
                        newv++;
                      }
                    }
                    else {
                      break;
                    }
                  }
                }
                else {
                  break;
                }
              }
            }

            if ((newu + newv) > 2) {
              /* ignore single points */
              if (a == 0) {
                /* check if need to save cyclic selection and continue if so */
                if (newu == nu->pntsu && (nu->flagv & CU_NURB_CYCLIC)) {
                  cyclicv = newv;
                }
                if (newv == nu->pntsv && (nu->flagu & CU_NURB_CYCLIC)) {
                  cyclicu = newu;
                }
                if (cyclicu != 0 || cyclicv != 0) {
                  continue;
                }
              }

              if (a + newu == nu->pntsu && cyclicu != 0) {
                /* cyclic in U direction */
                newnu = dune_nurb_copy(nu, newu + cyclicu, newv);
                for (b = 0; b < newv; b++) {
                  memcpy(&newnu->dp[b * newnu->pntsu],
                         &nu->dp[b * nu->pntsu + a],
                         newu * sizeof(DPoint));
                  memcpy(&newnu->dp[b * newnu->pntsu + newu],
                         &nu->dp[b * nu->pntsu],
                         cyclicu * sizeof(DPoint));
                }

                if (cu->actnu == i) {
                  if (cu->actvert == -1) {
                    calc_duplicate_actnurb(editnurb, newnurb, cu);
                  }
                  else {
                    for (b = 0, diffa = 0; b < newv; b++, diffa += nu->pntsu - newu) {
                      starta = b * nu->pntsu + a;
                      if (calc_duplicate_actvert(editnurb,
                                                 newnurb,
                                                 cu,
                                                 cu->actvert,
                                                 starta,
                                                 cu->actvert % nu->pntsu + newu +
                                                     b * newnu->pntsu)) {
                        /* actvert in cyclicu selection */
                        break;
                      }
                      if (calc_duplicate_actvert(editnurb,
                                                 newnurb,
                                                 cu,
                                                 starta,
                                                 starta + newu,
                                                 cu->actvert - starta + b * newnu->pntsu)) {
                        /* actvert in 'current' iteration selection */
                        break;
                      }
                    }
                  }
                }
                cyclicu = cyclicv = 0;
              }
              else if ((a / nu->pntsu) + newv == nu->pntsv && cyclicv != 0) {
                /* cyclic in V direction */
                newnu = dune_nurb_copy(nu, newu, newv + cyclicv);
                memcpy(newnu->dp, &nu->dp[a], newu * newv * sizeof(DPoint));
                memcpy(&newnu->dp[newu * newv], nu->bp, newu * cyclicv * sizeof(DPoint));

                /* check for actvert in cyclicv selection */
                if (cu->actnu == i) {
                  calc_duplicate_actvert(
                      editnurb, newnurb, cu, cu->actvert, a, (newu * newv) + cu->actvert);
                }
                cyclicu = cyclicv = 0;
              }
              else {
                newnu = dune_nurb_copy(nu, newu, newv);
                for (b = 0; b < newv; b++) {
                  memcpy(&newnu->fp[b * newu], &nu->dp[b * nu->pntsu + a], newu * sizeof(DPoint));
                }
              }

              /* general case if not handled by cyclicu or cyclicv */
              if (cu->actnu == i) {
                if (cu->actvert == -1) {
                  calc_duplicate_actnurb(editnurb, newnurb, cu);
                }
                else {
                  for (b = 0, diffa = 0; b < newv; b++, diffa += nu->pntsu - newu) {
                    starta = b * nu->pntsu + a;
                    if (calc_duplicate_actvert(editnurb,
                                               newnurb,
                                               cu,
                                               starta,
                                               starta + newu,
                                               cu->actvert - (a / nu->pntsu * nu->pntsu + diffa +
                                                              (starta % nu->pntsu)))) {
                      break;
                    }
                  }
                }
              }
              lib_addtail(newnurb, newnu);

              if (newu != nu->pntsu) {
                newnu->flagu &= ~CU_NURB_CYCLIC;
              }
              if (newv != nu->pntsv) {
                newnu->flagv &= ~CU_NURB_CYCLIC;
              }
            }
          }

          if (cyclicu != 0 || cyclicv != 0) {
            /* copy start of a cyclic surface, or copying all selected points */
            newu = cyclicu == 0 ? nu->pntsu : cyclicu;
            newv = cyclicv == 0 ? nu->pntsv : cyclicv;

            newnu = dune_nurb_copy(nu, newu, newv);
            for (b = 0; b < newv; b++) {
              memcpy(&newnu->dp[b * newu], &nu->dp[b * nu->pntsu], newu * sizeof(DPoint));
            }

            /* Check for `actvert` in the unused cyclic-UV selection. */
            if (cu->actnu == i) {
              if (cu->actvert == -1) {
                calc_duplicate_actnurb(editnurb, newnurb, cu);
              }
              else {
                for (b = 0, diffa = 0; b < newv; b++, diffa += nu->pntsu - newu) {
                  starta = b * nu->pntsu;
                  if (calc_duplicate_actvert(editnurb,
                                             newnurb,
                                             cu,
                                             starta,
                                             starta + newu,
                                             cu->actvert - (diffa + (starta % nu->pntsu)))) {
                    break;
                  }
                }
              }
            }
            BLI_addtail(newnurb, newnu);

            if (newu != nu->pntsu) {
              newnu->flagu &= ~CU_NURB_CYCLIC;
            }
            if (newv != nu->pntsv) {
              newnu->flagv &= ~CU_NURB_CYCLIC;
            }
          }

          for (b = 0, bp1 = nu->bp; b < nu->pntsu * nu->pntsv; b++, bp1++) {
            bp1->f1 &= ~SURF_SEEN;
            if (!split) {
              select_bpoint(bp1, DESELECT, flag, HIDDEN);
            }
          }
        }
      }
    }
  }

  if (BLI_listbase_is_empty(newnurb) == false) {
    LISTBASE_FOREACH (Nurb *, nu, newnurb) {
      if (nu->type == CU_BEZIER) {
        if (split) {
          /* recalc first and last */
          BKE_nurb_handle_calc_simple(nu, &nu->bezt[0]);
          BKE_nurb_handle_calc_simple(nu, &nu->bezt[nu->pntsu - 1]);
        }
      }
      else {
        /* knots done after duplicate as pntsu may change */
        BKE_nurb_order_clamp_u(nu);
        BKE_nurb_knot_calc_u(nu);

        if (obedit->type == OB_SURF) {
          for (a = 0, bp = nu->bp; a < nu->pntsu * nu->pntsv; a++, bp++) {
            bp->f1 &= ~SURF_SEEN;
          }

          BKE_nurb_order_clamp_v(nu);
          BKE_nurb_knot_calc_v(nu);
        }
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Switch Direction Operator
 * \{ */

static int switch_direction_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);

  uint objects_len;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!ED_curve_select_check(v3d, cu->editnurb)) {
      continue;
    }

    EditNurb *editnurb = cu->editnurb;

    int i = 0;
    LISTBASE_FOREACH_INDEX (Nurb *, nu, &editnurb->nurbs, i) {
      if (ED_curve_nurb_select_check(v3d, nu)) {
        BKE_nurb_direction_switch(nu);
        keyData_switchDirectionNurb(cu, nu);
        if ((i == cu->actnu) && (cu->actvert != CU_ACT_NONE)) {
          cu->actvert = (nu->pntsu - 1) - cu->actvert;
        }
      }
    }

    if (ED_curve_updateAnimPaths(bmain, obedit->data)) {
      WM_event_add_notifier(C, NC_OBJECT | ND_KEYS, obedit);
    }

    DEG_id_tag_update(obedit->data, 0);
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_switch_direction(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Switch Direction";
  ot->description = "Switch direction of selected splines";
  ot->idname = "CURVE_OT_switch_direction";

  /* api callbacks */
  ot->exec = switch_direction_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Set Weight Operator
 * \{ */

static int set_goal_weight_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);
    BezTriple *bezt;
    BPoint *bp;
    float weight = RNA_float_get(op->ptr, "weight");
    int a;

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->bezt) {
        for (bezt = nu->bezt, a = 0; a < nu->pntsu; a++, bezt++) {
          if (bezt->f2 & SELECT) {
            bezt->weight = weight;
          }
        }
      }
      else if (nu->bp) {
        for (bp = nu->bp, a = 0; a < nu->pntsu * nu->pntsv; a++, bp++) {
          if (bp->f1 & SELECT) {
            bp->weight = weight;
          }
        }
      }
    }

    DEG_id_tag_update(obedit->data, 0);
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_spline_weight_set(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Set Goal Weight";
  ot->description = "Set softbody goal weight for selected points";
  ot->idname = "CURVE_OT_spline_weight_set";

  /* api callbacks */
  ot->exec = set_goal_weight_exec;
  ot->invoke = WM_operator_props_popup;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_float_factor(ot->srna, "weight", 1.0f, 0.0f, 1.0f, "Weight", "", 0.0f, 1.0f);
}

/* -------------------------------------------------------------------- */
/** Set Radius Operator */

static int set_radius_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);
    BezTriple *bezt;
    DPoint *bp;
    float radius = api_float_get(op->ptr, "radius");
    int a;

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->bezt) {
        for (bezt = nu->bezt, a = 0; a < nu->pntsu; a++, bezt++) {
          if (bezt->f2 & SELECT) {
            bezt->radius = radius;
          }
        }
      }
      else if (nu->bp) {
        for (bp = nu->bp, a = 0; a < nu->pntsu * nu->pntsv; a++, bp++) {
          if (bp->f1 & SELECT) {
            bp->radius = radius;
          }
        }
      }
    }

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_radius_set(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Set Curve Radius";
  ot->description = "Set per-point radius which is used for bevel tapering";
  ot->idname = "CURVE_OT_radius_set";

  /* api callbacks */
  ot->exec = set_radius_exec;
  ot->invoke = WM_operator_props_popup;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  api_def_float(
      ot->srna, "radius", 1.0f, 0.0f, OBJECT_ADD_SIZE_MAXF, "Radius", "", 0.0001f, 10.0f);
}

/* -------------------------------------------------------------------- */
/**  Smooth Vertices Operator **/

static void smooth_single_bezt(BezTriple *bezt,
                               const BezTriple *bezt_orig_prev,
                               const BezTriple *bezt_orig_next,
                               float factor)
{
  BLI_assert(IN_RANGE_INCL(factor, 0.0f, 1.0f));

  for (int i = 0; i < 3; i++) {
    /* get single dimension pos of the mid handle */
    float val_old = bezt->vec[1][i];

    /* get the weights of the previous/next mid handles and calc offset */
    float val_new = (bezt_orig_prev->vec[1][i] * 0.5f) + (bezt_orig_next->vec[1][i] * 0.5f);
    float offset = (val_old * (1.0f - factor)) + (val_new * factor) - val_old;

    /* offset midpoint and 2 handles */
    bezt->vec[1][i] += offset;
    bezt->vec[0][i] += offset;
    bezt->vec[2][i] += offset;
  }
}

/**
 * Same as #smooth_single_bezt(), keep in sync.
 */
static void smooth_single_bp(BPoint *bp,
                             const BPoint *bp_orig_prev,
                             const BPoint *bp_orig_next,
                             float factor)
{
  BLI_assert(IN_RANGE_INCL(factor, 0.0f, 1.0f));

  for (int i = 0; i < 3; i++) {
    float val_old, val_new, offset;

    val_old = bp->vec[i];
    val_new = (bp_orig_prev->vec[i] * 0.5f) + (bp_orig_next->vec[i] * 0.5f);
    offset = (val_old * (1.0f - factor)) + (val_new * factor) - val_old;

    bp->vec[i] += offset;
  }
}

static int smooth_exec(bContext *C, wmOperator *UNUSED(op))
{
  const float factor = 1.0f / 6.0f;
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);

    int a, a_end;
    bool changed = false;

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->bezt) {
        /* duplicate the curve to use in weight calculation */
        const BezTriple *bezt_orig = MEM_dupallocN(nu->bezt);
        BezTriple *bezt;
        changed = false;

        /* check whether its cyclic or not, and set initial & final conditions */
        if (nu->flagu & CU_NURB_CYCLIC) {
          a = 0;
          a_end = nu->pntsu;
        }
        else {
          a = 1;
          a_end = nu->pntsu - 1;
        }

        /* for all the curve points */
        for (; a < a_end; a++) {
          /* respect selection */
          bezt = &nu->bezt[a];
          if (bezt->f2 & SELECT) {
            const BezTriple *bezt_orig_prev, *bezt_orig_next;

            bezt_orig_prev = &bezt_orig[mod_i(a - 1, nu->pntsu)];
            bezt_orig_next = &bezt_orig[mod_i(a + 1, nu->pntsu)];

            smooth_single_bezt(bezt, bezt_orig_prev, bezt_orig_next, factor);

            changed = true;
          }
        }
        MEM_freeN((void *)bezt_orig);
        if (changed) {
          BKE_nurb_handles_calc(nu);
        }
      }
      else if (nu->bp) {
        /* Same as above, keep these the same! */
        const BPoint *bp_orig = MEM_dupallocN(nu->bp);
        BPoint *bp;

        if (nu->flagu & CU_NURB_CYCLIC) {
          a = 0;
          a_end = nu->pntsu;
        }
        else {
          a = 1;
          a_end = nu->pntsu - 1;
        }

        for (; a < a_end; a++) {
          bp = &nu->bp[a];
          if (bp->f1 & SELECT) {
            const BPoint *bp_orig_prev, *bp_orig_next;

            bp_orig_prev = &bp_orig[mod_i(a - 1, nu->pntsu)];
            bp_orig_next = &bp_orig[mod_i(a + 1, nu->pntsu)];

            smooth_single_bp(bp, bp_orig_prev, bp_orig_next, factor);
          }
        }
        MEM_freeN((void *)bp_orig);
      }
    }

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_smooth(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Smooth";
  ot->description = "Flatten angles of selected points";
  ot->idname = "CURVE_OT_smooth";

  /* api callbacks */
  ot->exec = smooth_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Smooth Operator (Radius/Weight/Tilt) Utilities
 *
 * To do:
 * - Make smoothing distance based.
 * - Support cyclic curves.
 **/

static void curve_smooth_value(ListBase *editnurb, const int bezt_offsetof, const int bp_offset)
{
  BezTriple *bezt;
  BPoint *bp;
  int a;

  /* use for smoothing */
  int last_sel;
  int start_sel, end_sel; /* selection indices, inclusive */
  float start_rad, end_rad, fac, range;

  LISTBASE_FOREACH (Nurb *, nu, editnurb) {
    if (nu->bezt) {
#define BEZT_VALUE(bezt) (*((float *)((char *)(bezt) + bezt_offsetof)))

      for (last_sel = 0; last_sel < nu->pntsu; last_sel++) {
        /* loop over selection segments of a curve, smooth each */

        /* Start BezTriple code,
         * this is duplicated below for points, make sure these functions stay in sync */
        start_sel = -1;
        for (bezt = &nu->bezt[last_sel], a = last_sel; a < nu->pntsu; a++, bezt++) {
          if (bezt->f2 & SELECT) {
            start_sel = a;
            break;
          }
        }
        /* in case there are no other selected verts */
        end_sel = start_sel;
        for (bezt = &nu->bezt[start_sel + 1], a = start_sel + 1; a < nu->pntsu; a++, bezt++) {
          if ((bezt->f2 & SELECT) == 0) {
            break;
          }
          end_sel = a;
        }

        if (start_sel == -1) {
          last_sel = nu->pntsu; /* next... */
        }
        else {
          last_sel = end_sel; /* before we modify it */

          /* now blend between start and end sel */
          start_rad = end_rad = FLT_MAX;

          if (start_sel == end_sel) {
            /* simple, only 1 point selected */
            if (start_sel > 0) {
              start_rad = BEZT_VALUE(&nu->bezt[start_sel - 1]);
            }
            if (end_sel != -1 && end_sel < nu->pntsu) {
              end_rad = BEZT_VALUE(&nu->bezt[start_sel + 1]);
            }

            if (start_rad != FLT_MAX && end_rad >= FLT_MAX) {
              BEZT_VALUE(&nu->bezt[start_sel]) = (start_rad + end_rad) / 2.0f;
            }
            else if (start_rad != FLT_MAX) {
              BEZT_VALUE(&nu->bezt[start_sel]) = start_rad;
            }
            else if (end_rad != FLT_MAX) {
              BEZT_VALUE(&nu->bezt[start_sel]) = end_rad;
            }
          }
          else {
            /* if endpoints selected, then use them */
            if (start_sel == 0) {
              start_rad = BEZT_VALUE(&nu->bezt[start_sel]);
              start_sel++; /* we don't want to edit the selected endpoint */
            }
            else {
              start_rad = BEZT_VALUE(&nu->bezt[start_sel - 1]);
            }
            if (end_sel == nu->pntsu - 1) {
              end_rad = BEZT_VALUE(&nu->bezt[end_sel]);
              end_sel--; /* we don't want to edit the selected endpoint */
            }
            else {
              end_rad = BEZT_VALUE(&nu->bezt[end_sel + 1]);
            }

            /* Now Blend between the points */
            range = (float)(end_sel - start_sel) + 2.0f;
            for (bezt = &nu->bezt[start_sel], a = start_sel; a <= end_sel; a++, bezt++) {
              fac = (float)(1 + a - start_sel) / range;
              BEZT_VALUE(bezt) = start_rad * (1.0f - fac) + end_rad * fac;
            }
          }
        }
      }
#undef BEZT_VALUE
    }
    else if (nu->bp) {
#define BP_VALUE(bp) (*((float *)((char *)(bp) + bp_offset)))

      /* Same as above, keep these the same! */
      for (last_sel = 0; last_sel < nu->pntsu; last_sel++) {
        /* loop over selection segments of a curve, smooth each */

        /* Start BezTriple code,
         * this is duplicated below for points, make sure these functions stay in sync */
        start_sel = -1;
        for (bp = &nu->bp[last_sel], a = last_sel; a < nu->pntsu; a++, bp++) {
          if (bp->f1 & SELECT) {
            start_sel = a;
            break;
          }
        }
        /* in case there are no other selected verts */
        end_sel = start_sel;
        for (bp = &nu->bp[start_sel + 1], a = start_sel + 1; a < nu->pntsu; a++, bp++) {
          if ((bp->f1 & SELECT) == 0) {
            break;
          }
          end_sel = a;
        }

        if (start_sel == -1) {
          last_sel = nu->pntsu; /* next... */
        }
        else {
          last_sel = end_sel; /* before we modify it */

          /* now blend between start and end sel */
          start_rad = end_rad = FLT_MAX;

          if (start_sel == end_sel) {
            /* simple, only 1 point selected */
            if (start_sel > 0) {
              start_rad = BP_VALUE(&nu->bp[start_sel - 1]);
            }
            if (end_sel != -1 && end_sel < nu->pntsu) {
              end_rad = BP_VALUE(&nu->bp[start_sel + 1]);
            }

            if (start_rad != FLT_MAX && end_rad != FLT_MAX) {
              BP_VALUE(&nu->bp[start_sel]) = (start_rad + end_rad) / 2;
            }
            else if (start_rad != FLT_MAX) {
              BP_VALUE(&nu->bp[start_sel]) = start_rad;
            }
            else if (end_rad != FLT_MAX) {
              BP_VALUE(&nu->bp[start_sel]) = end_rad;
            }
          }
          else {
            /* if endpoints selected, then use them */
            if (start_sel == 0) {
              start_rad = BP_VALUE(&nu->bp[start_sel]);
              start_sel++; /* we don't want to edit the selected endpoint */
            }
            else {
              start_rad = BP_VALUE(&nu->bp[start_sel - 1]);
            }
            if (end_sel == nu->pntsu - 1) {
              end_rad = BP_VALUE(&nu->bp[end_sel]);
              end_sel--; /* we don't want to edit the selected endpoint */
            }
            else {
              end_rad = BP_VALUE(&nu->bp[end_sel + 1]);
            }

            /* Now Blend between the points */
            range = (float)(end_sel - start_sel) + 2.0f;
            for (bp = &nu->bp[start_sel], a = start_sel; a <= end_sel; a++, bp++) {
              fac = (float)(1 + a - start_sel) / range;
              BP_VALUE(bp) = start_rad * (1.0f - fac) + end_rad * fac;
            }
          }
        }
      }
#undef BP_VALUE
    }
  }
}

/* -------------------------------------------------------------------- */
/** Smooth Weight Operator */

static int curve_smooth_weight_exec(dContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);

    curve_smooth_value(editnurb, offsetof(BezTriple, weight), offsetof(DPoint, weight));

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_smooth_weight(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Smooth Curve Weight";
  ot->description = "Interpolate weight of selected points";
  ot->idname = "CURVE_OT_smooth_weight";

  /* api callbacks */
  ot->exec = curve_smooth_weight_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER |;
}

/* -------------------------------------------------------------------- */
/* Smooth Radius Operator */

static int curve_smooth_radius_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);

    curve_smooth_value(editnurb, offsetof(BezTriple, radius), offsetof(BPoint, radius));

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_smooth_radius(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Smooth Curve Radius";
  ot->description = "Interpolate radii of selected points";
  ot->idname = "CURVE_OT_smooth_radius";

  /* api callbacks */
  ot->exec = curve_smooth_radius_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Smooth Tilt Operator */

static int curve_smooth_tilt_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);

    curve_smooth_value(editnurb, offsetof(BezTriple, tilt), offsetof(BPoint, tilt));

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_smooth_tilt(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Smooth Curve Tilt";
  ot->description = "Interpolate tilt of selected points";
  ot->idname = "CURVE_OT_smooth_tilt";

  /* api callbacks */
  ot->exec = curve_smooth_tilt_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Hide Operator **/

static int hide_exec(dContext *C, wmOperator *op)
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  View3D *v3d = ctx_wm_view3d(C);

  const bool invert = api_bool_get(op->ptr, "unselected");

  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, dune_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!(invert || ED_curve_select_check(v3d, cu->editnurb))) {
      continue;
    }

    ListBase *editnurb = object_editcurve_get(obedit);
    DPoint *bp;
    BezTriple *bezt;
    int a, sel;

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (nu->type == CU_BEZIER) {
        bezt = nu->bezt;
        a = nu->pntsu;
        sel = 0;
        while (a--) {
          if (invert == 0 && BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
            select_beztriple(bezt, DESELECT, SELECT, HIDDEN);
            bezt->hide = 1;
          }
          else if (invert && !BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt)) {
            select_beztriple(bezt, DESELECT, SELECT, HIDDEN);
            bezt->hide = 1;
          }
          if (bezt->hide) {
            sel++;
          }
          bezt++;
        }
        if (sel == nu->pntsu) {
          nu->hide = 1;
        }
      }
      else {
        bp = nu->bp;
        a = nu->pntsu * nu->pntsv;
        sel = 0;
        while (a--) {
          if (invert == 0 && (bp->f1 & SELECT)) {
            select_bpoint(bp, DESELECT, SELECT, HIDDEN);
            bp->hide = 1;
          }
          else if (invert && (bp->f1 & SELECT) == 0) {
            select_bpoint(bp, DESELECT, SELECT, HIDDEN);
            bp->hide = 1;
          }
          if (bp->hide) {
            sel++;
          }
          bp++;
        }
        if (sel == nu->pntsu * nu->pntsv) {
          nu->hide = 1;
        }
      }
    }

    DEG_id_tag_update(obedit->data, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
    dune_curve_nurb_vert_active_validate(obedit->data);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_hide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Hide Selected";
  ot->idname = "CURVE_OT_hide";
  ot->description = "Hide (un)selected control points";

  /* api callbacks */
  ot->exec = hide_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->srna, "unselected", 0, "Unselected", "Hide unselected rather than selected");
}
/* -------------------------------------------------------------------- */
/** Reveal Operator */

static int reveal_exec(dContext *C, wmOperator *op)
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  const bool select = api_bool_get(op->ptr, "select");
  bool changed_multi = false;

  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    ListBase *editnurb = object_editcurve_get(obedit);
    DPoint *bp;
    BezTriple *bezt;
    int a;
    bool changed = false;

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      nu->hide = 0;
      if (nu->type == CU_BEZIER) {
        bezt = nu->bezt;
        a = nu->pntsu;
        while (a--) {
          if (bezt->hide) {
            select_beztriple(bezt, select, SELECT, HIDDEN);
            bezt->hide = 0;
            changed = true;
          }
          bezt++;
        }
      }
      else {
        bp = nu->bp;
        a = nu->pntsu * nu->pntsv;
        while (a--) {
          if (bp->hide) {
            select_bpoint(bp, select, SELECT, HIDDEN);
            bp->hide = 0;
            changed = true;
          }
          bp++;
        }
      }
    }

    if (changed) {
      DEG_id_tag_update(obedit->data,
                        ID_RECALC_COPY_ON_WRITE | ID_RECALC_SELECT | ID_RECALC_GEOMETRY);
      wm_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
      changed_multi = true;
    }
  }
  MEM_freeN(objects);
  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void CURVE_OT_reveal(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reveal Hidden";
  ot->idname = "CURVE_OT_reveal";
  ot->description = "Reveal hidden control points";

  /* api callbacks */
  ot->exec = reveal_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->srna, "select", true, "")
}

/* -------------------------------------------------------------------- */
/** Subdivide Operator */

/**
 * Divide the line segments associated with the currently selected
 * curve nodes (Bezier or NURB). If there are no valid segment
 * selections within the current selection, nothing happens.
 */
static void subdividenurb(Object *obedit, View3D *v3d, int number_cuts)
{
  Curve *cu = obedit->data;
  EditNurb *editnurb = cu->editnurb;
  BezTriple *bezt, *beztnew, *beztn;
  BPoint *bp, *prevbp, *bpnew, *bpn;
  float vec[15];
  int a, b, sel, amount, *usel, *vsel;
  float factor;

  // printf("*** subdivideNurb: entering subdivide\n");

  LISTBASE_FOREACH (Nurb *, nu, &editnurb->nurbs) {
    amount = 0;
    if (nu->type == CU_BEZIER) {
      BezTriple *nextbezt;

      /*
       * Insert a point into a 2D Bezier curve.
       * Endpoints are preserved. Otherwise, all selected and inserted points are
       * newly created. Old points are discarded.
       */
      /* count */
      a = nu->pntsu;
      bezt = nu->bezt;
      while (a--) {
        nextbezt = dune_nurb_bezt_get_next(nu, bezt);
        if (nextbezt == NULL) {
          break;
        }

        if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt) &&
            BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, nextbezt)) {
          amount += number_cuts;
        }
        bezt++;
      }

      if (amount) {
        /* insert */
        beztnew = (BezTriple *)MEM_mallocN((amount + nu->pntsu) * sizeof(BezTriple), "subdivNurb");
        beztn = beztnew;
        a = nu->pntsu;
        bezt = nu->bezt;
        while (a--) {
          memcpy(beztn, bezt, sizeof(BezTriple));
          keyIndex_updateBezt(editnurb, bezt, beztn, 1);
          beztn++;

          nextbezt = BKE_nurb_bezt_get_next(nu, bezt);
          if (nextbezt == NULL) {
            break;
          }

          if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt) &&
              BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, nextbezt)) {
            float prevvec[3][3];

            memcpy(prevvec, bezt->vec, sizeof(float[9]));

            for (int i = 0; i < number_cuts; i++) {
              factor = 1.0f / (number_cuts + 1 - i);

              memcpy(beztn, nextbezt, sizeof(BezTriple));

              /* midpoint subdividing */
              interp_v3_v3v3(vec, prevvec[1], prevvec[2], factor);
              interp_v3_v3v3(vec + 3, prevvec[2], nextbezt->vec[0], factor);
              interp_v3_v3v3(vec + 6, nextbezt->vec[0], nextbezt->vec[1], factor);

              interp_v3_v3v3(vec + 9, vec, vec + 3, factor);
              interp_v3_v3v3(vec + 12, vec + 3, vec + 6, factor);

              /* change handle of prev beztn */
              copy_v3_v3((beztn - 1)->vec[2], vec);
              /* new point */
              copy_v3_v3(beztn->vec[0], vec + 9);
              interp_v3_v3v3(beztn->vec[1], vec + 9, vec + 12, factor);
              copy_v3_v3(beztn->vec[2], vec + 12);
              /* handle of next bezt */
              if (a == 0 && i == number_cuts - 1 && (nu->flagu & CU_NURB_CYCLIC)) {
                copy_v3_v3(beztnew->vec[0], vec + 6);
              }
              else {
                copy_v3_v3(nextbezt->vec[0], vec + 6);
              }

              beztn->radius = (bezt->radius + nextbezt->radius) / 2;
              beztn->weight = (bezt->weight + nextbezt->weight) / 2;

              memcpy(prevvec, beztn->vec, sizeof(float[9]));
              beztn++;
            }
          }

          bezt++;
        }

        MEM_freeN(nu->bezt);
        nu->bezt = beztnew;
        nu->pntsu += amount;

        dune_nurb_handles_calc(nu);
      }
    } /* End of 'if (nu->type == CU_BEZIER)' */
    else if (nu->pntsv == 1) {
      DPoint *nextbp;

      /*
       * All flat lines (ie. co-planar), except flat Nurbs. Flat NURB curves
       * are handled together with the regular NURB plane division, as it
       * should be. I split it off just now, let's see if it is
       * stable... nzc 30-5-'00
       */
      /* count */
      a = nu->pntsu;
      bp = nu->bp;
      while (a--) {
        nextbp = dune_nurb_bpoint_get_next(nu, bp);
        if (nextbp == NULL) {
          break;
        }

        if ((bp->f1 & SELECT) && (nextbp->f1 & SELECT)) {
          amount += number_cuts;
        }
        bp++;
      }

      if (amount) {
        /* insert */
        bpnew = (DPoint *)MEM_mallocN((amount + nu->pntsu) * sizeof(BPoint), "subdivNurb2");
        bpn = bpnew;

        a = nu->pntsu;
        bp = nu->bp;

        while (a--) {
          /* Copy "old" point. */
          memcpy(bpn, bp, sizeof(DPoint));
          keyIndex_updateBP(editnurb, bp, bpn, 1);
          bpn++;

          nextbp = dune_nurb_bpoint_get_next(nu, bp);
          if (nextbp == NULL) {
            break;
          }

          if ((bp->f1 & SELECT) && (nextbp->f1 & SELECT)) {
            // printf("*** subdivideNurb: insert 'linear' point\n");
            for (int i = 0; i < number_cuts; i++) {
              factor = (float)(i + 1) / (number_cuts + 1);

              memcpy(bpn, nextbp, sizeof(BPoint));
              interp_v4_v4v4(bpn->vec, bp->vec, nextbp->vec, factor);
              bpn->radius = interpf(bp->radius, nextbp->radius, factor);
              bpn++;
            }
          }
          bp++;
        }

        MEM_freeN(nu->bp);
        nu->bp = bpnew;
        nu->pntsu += amount;

        if (nu->type & CU_NURBS) {
          dune_nurb_knot_calc_u(nu);
        }
      }
    } /* End of 'else if (nu->pntsv == 1)' */
    else if (nu->type == CU_NURBS) {
      /* This is a very strange test ... */
      /**
       * Subdivide NURB surfaces - nzc 30-5-'00 -
       *
       * Subdivision of a NURB curve can be effected by adding a
       * control point (insertion of a knot), or by raising the
       * degree of the functions used to build the NURB. The
       * expression
       *
       *     `degree = knots - controlpoints + 1` (J Walter piece)
       *     `degree = knots - controlpoints` (Blender implementation)
       *       ( this is confusing.... what is true? Another concern
       *       is that the JW piece allows the curve to become
       *       explicitly 1st order derivative discontinuous, while
       *       this is not what we want here... )
       *
       * is an invariant for a single NURB curve. Raising the degree
       * of the NURB is done elsewhere; the degree is assumed
       * constant during this operation. Degree is a property shared
       * by all control-points in a curve (even though it is stored
       * per control point - this can be misleading).
       * Adding a knot is done by searching for the place in the
       * knot vector where a certain knot value must be inserted, or
       * by picking an appropriate knot value between two existing
       * ones. The number of control-points that is influenced by the
       * insertion depends on the order of the curve. A certain
       * minimum number of knots is needed to form high-order
       * curves, as can be seen from the equation above. In Blender,
       * currently NURBs may be up to 6th order, so we modify at
       * most 6 points. One point is added. For an n-degree curve,
       * n points are discarded, and n+1 points inserted
       * (so effectively, n points are modified).  (that holds for
       * the JW piece, but it seems not for our NURBs)
       * In practice, the knot spacing is copied, but the tail
       * (the points following the insertion point) need to be
       * offset to keep the knot series ascending. The knot series
       * is always a series of monotonically ascending integers in
       * Dune. When not enough control points are available to
       * fit the order, duplicates of the endpoints are added as
       * needed.
       */
      /* selection-arrays */
      usel = MEM_callocN(sizeof(int) * nu->pntsu, "subivideNurb3");
      vsel = MEM_callocN(sizeof(int) * nu->pntsv, "subivideNurb3");
      sel = 0;

      /* Count the number of selected points. */
      bp = nu->bp;
      for (a = 0; a < nu->pntsv; a++) {
        for (b = 0; b < nu->pntsu; b++) {
          if (bp->f1 & SELECT) {
            usel[b]++;
            vsel[a]++;
            sel++;
          }
          bp++;
        }
      }
      if (sel == (nu->pntsu * nu->pntsv)) { /* subdivide entire nurb */
        /* Global subdivision is a special case of partial
         * subdivision. Strange it is considered separately... */

        /* count of nodes (after subdivision) along U axis */
        int countu = nu->pntsu + (nu->pntsu - 1) * number_cuts;

        /* total count of nodes after subdivision */
        int tot = ((number_cuts + 1) * nu->pntsu - number_cuts) *
                  ((number_cuts + 1) * nu->pntsv - number_cuts);

        bpn = bpnew = MEM_mallocN(tot * sizeof(BPoint), "subdivideNurb4");
        bp = nu->bp;
        /* first subdivide rows */
        for (a = 0; a < nu->pntsv; a++) {
          for (b = 0; b < nu->pntsu; b++) {
            *bpn = *bp;
            keyIndex_updateBP(editnurb, bp, bpn, 1);
            bpn++;
            bp++;
            if (b < nu->pntsu - 1) {
              prevbp = bp - 1;
              for (int i = 0; i < number_cuts; i++) {
                factor = (float)(i + 1) / (number_cuts + 1);
                *bpn = *bp;
                interp_v4_v4v4(bpn->vec, prevbp->vec, bp->vec, factor);
                bpn++;
              }
            }
          }
          bpn += number_cuts * countu;
        }
        /* now insert new */
        bpn = bpnew + ((number_cuts + 1) * nu->pntsu - number_cuts);
        bp = bpnew + (number_cuts + 1) * ((number_cuts + 1) * nu->pntsu - number_cuts);
        prevbp = bpnew;
        for (a = 1; a < nu->pntsv; a++) {

          for (b = 0; b < (number_cuts + 1) * nu->pntsu - number_cuts; b++) {
            DPoint *tmp = bpn;
            for (int i = 0; i < number_cuts; i++) {
              factor = (float)(i + 1) / (number_cuts + 1);
              *tmp = *bp;
              interp_v4_v4v4(tmp->vec, prevbp->vec, bp->vec, factor);
              tmp += countu;
            }
            bp++;
            prevbp++;
            bpn++;
          }
          bp += number_cuts * countu;
          bpn += number_cuts * countu;
          prevbp += number_cuts * countu;
        }
        MEM_freeN(nu->bp);
        nu->bp = bpnew;
        nu->pntsu = (number_cuts + 1) * nu->pntsu - number_cuts;
        nu->pntsv = (number_cuts + 1) * nu->pntsv - number_cuts;
        dune_nurb_knot_calc_u(nu);
        dune_nurb_knot_calc_v(nu);
      } /* End of 'if (sel == nu->pntsu * nu->pntsv)' (subdivide entire NURB) */
      else {
        /* subdivide in v direction? */
        sel = 0;
        for (a = 0; a < nu->pntsv - 1; a++) {
          if (vsel[a] == nu->pntsu && vsel[a + 1] == nu->pntsu) {
            sel += number_cuts;
          }
        }

        if (sel) { /* V ! */
          bpn = bpnew = MEM_mallocN((sel + nu->pntsv) * nu->pntsu * sizeof(BPoint),
                                    "subdivideNurb4");
          bp = nu->bp;
          for (a = 0; a < nu->pntsv; a++) {
            for (b = 0; b < nu->pntsu; b++) {
              *bpn = *bp;
              keyIndex_updateBP(editnurb, bp, bpn, 1);
              bpn++;
              bp++;
            }
            if ((a < nu->pntsv - 1) && vsel[a] == nu->pntsu && vsel[a + 1] == nu->pntsu) {
              for (int i = 0; i < number_cuts; i++) {
                factor = (float)(i + 1) / (number_cuts + 1);
                prevbp = bp - nu->pntsu;
                for (b = 0; b < nu->pntsu; b++) {
                  /*
                   * This simple bisection must be replaces by a
                   * subtle resampling of a number of points. Our
                   * task is made slightly easier because each
                   * point in our curve is a separate data
                   * node. (is it?)
                   */
                  *bpn = *prevbp;
                  interp_v4_v4v4(bpn->vec, prevbp->vec, bp->vec, factor);
                  bpn++;

                  prevbp++;
                  bp++;
                }
                bp -= nu->pntsu;
              }
            }
          }
          MEM_freeN(nu->bp);
          nu->bp = bpnew;
          nu->pntsv += sel;
          dune_nurb_knot_calc_v(nu);
        }
        else {
          /* or in u direction? */
          sel = 0;
          for (a = 0; a < nu->pntsu - 1; a++) {
            if (usel[a] == nu->pntsv && usel[a + 1] == nu->pntsv) {
              sel += number_cuts;
            }
          }

          if (sel) { /* U ! */
            /* Inserting U points is sort of 'default' Flat curves only get
             * U points inserted in them. */
            bpn = bpnew = MEM_mallocN((sel + nu->pntsu) * nu->pntsv * sizeof(DPoint),
                                      "subdivideNurb4");
            bp = nu->bp;
            for (a = 0; a < nu->pntsv; a++) {
              for (b = 0; b < nu->pntsu; b++) {
                *bpn = *bp;
                keyIndex_updateBP(editnurb, bp, bpn, 1);
                bpn++;
                bp++;
                if ((b < nu->pntsu - 1) && usel[b] == nu->pntsv && usel[b + 1] == nu->pntsv) {
                  /*
                   * One thing that bugs me here is that the
                   * orders of things are not the same as in
                   * the JW piece. Also, this implies that we
                   * handle at most 3rd order curves? I miss
                   * some symmetry here...
                   */
                  for (int i = 0; i < number_cuts; i++) {
                    factor = (float)(i + 1) / (number_cuts + 1);
                    prevbp = bp - 1;
                    *bpn = *prevbp;
                    interp_v4_v4v4(bpn->vec, prevbp->vec, bp->vec, factor);
                    bpn++;
                  }
                }
              }
            }
            MEM_freeN(nu->bp);
            nu->bp = bpnew;
            nu->pntsu += sel;
            dube_nurb_knot_calc_u(nu); /* shift knots forward */
          }
        }
      }
      MEM_freeN(usel);
      MEM_freeN(vsel);

    } /* End of `if (nu->type == CU_NURBS)`. */
  }
}

static int subdivide_exec(dContext *C, wmOperator *op)
{
  const int number_cuts = api_int_get(op->ptr, "number_cuts");

  Main *dmain = ctx_data_main(C);
  ViewLayer *view_layer = ctx_data_view_layer(C);
  View3D *v3d = ctx_wm_view3d(C);

  uint objects_len = 0;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!ED_curve_select_check(v3d, cu->editnurb)) {
      continue;
    }

    subdividenurb(obedit, v3d, number_cuts);

    if (ED_curve_updateAnimPaths(bmain, cu)) {
      WM_event_add_notifier(C, NC_OBJECT | ND_KEYS, obedit);
    }

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, cu);
    DEG_id_tag_update(obedit->data, 0);
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void CURVE_OT_subdivide(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Subdivide";
  ot->description = "Subdivide selected segments";
  ot->idname = "CURVE_OT_subdivide";

  /* api callbacks */
  ot->exec = subdivide_exec;
  ot->poll = ED_operator_editsurfcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  prop = api_def_int(ot->srna, "number_cuts", 1, 1, 1000, "Number of Cuts", "", 1, 10);
  /* Avoid re-using last var because it can cause _very_ high poly meshes
   * and annoy users (or worse crash). */
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/** Set Spline Type Operator */

static int set_spline_type_exec(dContext *C, wmOperator *op)
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  int ret_value = OPERATOR_CANCELLED;

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Main *duneMain = ctx_data_main(C);
    View3D *v3d = ctx_wm_view3d(C);
    ListBase *editnurb = object_editcurve_get(obedit);
    bool changed = false;
    bool changed_size = false;
    const bool use_handles = api_boolean_get(op->ptr, "use_handles");
    const int type = api_enum_get(op->ptr, "type");

    LISTBASE_FOREACH (Nurb *, nu, editnurb) {
      if (ED_curve_nurb_select_check(v3d, nu)) {
        const int pntsu_prev = nu->pntsu;
        const char *err_msg = NULL;
        if (dune_nurb_type_convert(nu, type, use_handles, &err_msg)) {
          changed = true;
          if (pntsu_prev != nu->pntsu) {
            changed_size = true;
          }
        }
        else {
          dune_report(op->reports, RPT_ERROR, err_msg);
        }
      }
    }

    if (changed) {
      if (ED_curve_updateAnimPaths(bmain, obedit->data)) {
        em_event_add_notifier(C, NC_OBJECT | ND_KEYS, obedit);
      }

      DEG_id_tag_update(obedit->data, 0);
      wm_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);

      if (changed_size) {
        Curve *cu = obedit->data;
        cu->actvert = CU_ACT_NONE;
      }

      ret_value = OPERATOR_FINISHED;
    }
  }

  MEM_freeN(objects);

  return ret_value;
}

void CURVE_OT_spline_type_set(wmOperatorType *ot)
{
  static const EnumPropertyItem type_items[] = {
      {CU_POLY, "POLY", 0, "Poly", ""},
      {CU_BEZIER, "BEZIER", 0, "Bezier", ""},
      {CU_NURBS, "NURBS", 0, "NURBS", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Set Spline Type";
  ot->description = "Set type of active spline";
  ot->idname = "CURVE_OT_spline_type_set";

  /* api callbacks */
  ot->exec = set_spline_type_exec;
  ot->invoke = wm_menu_invoke;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = api_def_enum(ot->srna, "type", type_items, CU_POLY, "Type", "Spline type");
  api_def_bool(ot->srna,
                  "use_handles",
                  0,
                  "Handles",
                  "Use handles when converting bezier curves into polygons");
}

/* -------------------------------------------------------------------- */
/* Set Handle Type Operator */

static int set_handle_type_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  const int handle_type = RNA_enum_get(op->ptr, "type");

  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!ED_curve_select_check(v3d, cu->editnurb)) {
      continue;
    }

    ListBase *editnurb = object_editcurve_get(obedit);
    dune_nurbList_handles_set(editnurb, handle_type);

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_handle_type_set(wmOperatorType *ot)
{
  /* keep in sync with graphkeys_handle_type_items */
  static const EnumPropertyItem editcurve_handle_type_items[] = {
      {HD_AUTO, "AUTOMATIC", 0, "Automatic", ""},
      {HD_VECT, "VECTOR", 0, "Vector", ""},
      {5, "ALIGNED", 0, "Aligned", ""},
      {6, "FREE_ALIGN", 0, "Free", ""},
      {3, "TOGGLE_FREE_ALIGN", 0, "Toggle Free/Align", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Set Handle Type";
  ot->description = "Set type of handles for selected control points";
  ot->idname = "CURVE_OT_handle_type_set";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = set_handle_type_exec;
  ot->poll = ED_operator_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = api_def_enum(ot->srna, "type", editcurve_handle_type_items, 1, "Type", "Spline type")

/* -------------------------------------------------------------------- */
/** Recalculate Handles Operator **/

static int curve_normals_make_consistent_exec(duneContext *C, wmOperator *op)
{
  ViewLayer *view_layer = ctx_data_view_layer(C);
  View3D *v3d = ctx_wm_view3d(C);

  const bool calc_length = api_bool_get(op->ptr, "calc_length");

  uint objects_len;
  Object **objects = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, ctx_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Curve *cu = obedit->data;

    if (!ED_curve_select_check(v3d, cu->editnurb)) {
      continue;
    }

    ListBase *editnurb = object_editcurve_get(obedit);
    dune_nurbList_handles_recalculate(editnurb, calc_length, SELECT);

    WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    DEG_id_tag_update(obedit->data, 0);
  }
  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void CURVE_OT_normals_make_consistent(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Recalculate Handles";
  ot->description = "Recalculate the direction of selected handles";
  ot->idname = "CURVE_OT_normals_make_consistent";

  /* api callbacks */
  ot->exec = curve_normals_make_consistent_exec;
  ot->poll = ED_op_editcurve;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->srna, "calc_length", false, "Length", "Recalculate handle length");
}
