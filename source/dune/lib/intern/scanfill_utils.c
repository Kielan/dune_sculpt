#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_ghash.h"
#include "lib_list.h"
#include "lib_math_geom.h"
#include "lib_math_vector.h"
#include "lib_utildefines.h"

#include "lib_scanfill.h" /* own include */

#include "lib_strict_flags.h"

typedef struct PolyInfo {
  ScanFillEdge *edge_first, *edge_last;
  ScanFillVert *vert_outer;
} PolyInfo;

typedef struct ScanFillIsect {
  struct ScanFillIsect *next, *prev;
  float co[3];

  /* newly created vert */
  ScanFillVert *v;
} ScanFillIsect;

#define V_ISISECT 1
#define E_ISISECT 1
#define E_ISDELETE 2

#define EFLAG_SET(eed, val) \
  { \
    CHECK_TYPE(eed, ScanFillEdge *); \
    (eed)->user_flag = (eed)->user_flag | (uint)val; \
  } \
  (void)0
#if 0
#  define EFLAG_CLEAR(eed, val) \
    { \
      CHECK_TYPE(eed, ScanFillEdge *); \
      (eed)->user_flag = (eed)->user_flag & ~(uint)val; \
    } \
    (void)0
#endif

#define VFLAG_SET(eve, val) \
  { \
    CHECK_TYPE(eve, ScanFillVert *); \
    (eve)->user_flag = (eve)->user_flag | (uint)val; \
  } \
  (void)0
#if 0
#  define VFLAG_CLEAR(eve, val) \
    { \
      CHECK_TYPE(eve, ScanFillVert *); \
      (eve)->user_flags = (eve)->user_flag & ~(uint)val; \
    } \
    (void)0
#endif

#if 0
void lib_scanfill_ob_dump(ScanFillCxt *sf_cxt)
{
  FILE *f = fopen("test.ob", "w");
  uint i = 1;

  ScanFillVert *eve;
  ScanFillEdge *eed;

  for (eve = sf_ctx->fillvertbase.first; eve; eve = eve->next, i++) {
    fprintf(f, "v %f %f %f\n", UNPACK3(eve->co));
    eve->keyindex = i;
  }
  for (eed = sf_ctx->filledgebase.first; eed; eed = eed->next) {
    fprintf(f, "f %d %d\n", eed->v1->keyindex, eed->v2->keyindex);
  }
  fclose(f);
}
#endif

static List *edge_isect_ls_ensure(GHash *isect_hash, ScanFillEdge *eed)
{
  List *e_ls;
  void **val_p;

  if (!lib_ghash_ensure_p(isect_hash, eed, &val_p)) {
    *val_p = mem_calloc(sizeof(List), __func__);
  }
  e_ls = *val_p;

  return e_ls;
}

static List *edge_isect_ls_add(GHash *isect_hash, ScanFillEdge *eed, ScanFillIsect *isect)
{
  List *e_ls;
  LinkData *isect_link;
  e_ls = edge_isect_ls_ensure(isect_hash, eed);
  isect_link = mem_calloc(sizeof(*isect_link), __func__);
  isect_link->data = isect;
  EFLAG_SET(eed, E_ISISECT);
  lib_addtail(e_ls, isect_link);
  return e_ls;
}

static int edge_isect_ls_sort_cb(void *thunk, const void *def_a_ptr, const void *def_b_ptr)
{
  const float *co = thunk;

  const ScanFillIsect *i_a = ((const LinkData *)def_a_ptr)->data;
  const ScanFillIsect *i_b = ((const LinkData *)def_b_ptr)->data;
  const float a = len_squared_v2v2(co, i_a->co);
  const float b = len_squared_v2v2(co, i_b->co);

  if (a > b) {
    return -1;
  }

  return (a < b);
}

static ScanFillEdge *edge_step(PolyInfo *poly_info,
                               const ushort poly_nr,
                               ScanFillVert *v_prev,
                               ScanFillVert *v_curr,
                               ScanFillEdge *e_curr)
{
  ScanFillEdge *eed;

  lib_assert(ELEM(v_prev, e_curr->v1, e_curr->v2));
  lib_assert(ELEM(v_curr, e_curr->v1, e_curr->v2));

  eed = (e_curr->next && e_curr != poly_info[poly_nr].edge_last) ? e_curr->next :
                                                                   poly_info[poly_nr].edge_first;
  if (ELEM(v_curr, eed->v1, eed->v2) == true && ELEM(v_prev, eed->v1, eed->v2) == false) {
    return eed;
  }

  eed = (e_curr->prev && e_curr != poly_info[poly_nr].edge_first) ? e_curr->prev :
                                                                    poly_info[poly_nr].edge_last;
  if (ELEM(v_curr, eed->v1, eed->v2) == true && ELEM(v_prev, eed->v1, eed->v2) == false) {
    return eed;
  }

  lib_assert(0);
  return NULL;
}

static bool scanfill_preprocess_self_isect(ScanFillCxt *sf_cxt,
                                           PolyInfo *poly_info,
                                           const ushort poly_nr,
                                           List *filledgebase)
{
  PolyInfo *pi = &poly_info[poly_nr];
  GHash *isect_hash = NULL;
  List isect_lb = {NULL};

  /* warning, O(n2) check here, should use spatial lookup */
  {
    ScanFillEdge *eed;

    for (eed = pi->edge_first; eed; eed = (eed == pi->edge_last) ? NULL : eed->next) {
      ScanFillEdge *eed_other;

      for (eed_other = eed->next; eed_other;
           eed_other = (eed_other == pi->edge_last) ? NULL : eed_other->next)
      {
        if (!ELEM(eed->v1, eed_other->v1, eed_other->v2) &&
            !ELEM(eed->v2, eed_other->v1, eed_other->v2) && (eed != eed_other))
        {
          /* check isect */
          float pt[2];
          lib_assert(eed != eed_other);

          if (isect_seg_seg_v2_point(
                  eed->v1->co, eed->v2->co, eed_other->v1->co, eed_other->v2->co, pt) == 1)
          {
            ScanFillIsect *isect;

            if (UNLIKELY(isect_hash == NULL)) {
              isect_hash = lib_ghash_ptr_new(__func__);
            }

            isect = mem_malloc(sizeof(ScanFillIsect), __func__);

            lib_addtail(&isect_lb, isect);

            copy_v2_v2(isect->co, pt);
            isect->co[2] = eed->v1->co[2];
            isect->v = lib_scanfill_vert_add(sf_cxt, isect->co);

            /* Vert may belong to 2 polys now */
            isect->v->poly_nr = eed->v1->poly_nr;

            VFLAG_SET(isect->v, V_ISISECT);
            edge_isect_ls_add(isect_hash, eed, isect);
            edge_isect_ls_add(isect_hash, eed_other, isect);
          }
        }
      }
    }
  }

  if (isect_hash == NULL) {
    return false;
  }

  /* now subdiv the edges */
  {
    ScanFillEdge *eed;

    for (eed = pi->edge_first; eed; eed = (eed == pi->edge_last) ? NULL : eed->next) {
      if (eed->user_flag & E_ISISECT) {
        List *e_ls = lib_ghash_lookup(isect_hash, eed);

        LinkData *isect_link;

        if (UNLIKELY(e_ls == NULL)) {
          /* only happens in very rare cases (entirely overlapping splines).
           * in this case we can't do much useful. but at least don't crash */
          continue;
        }

        /* Maintain correct terminating edge. */
        if (pi->edge_last == eed) {
          pi->edge_last = NULL;
        }

        if (lib_list_is_single(e_ls) == false) {
          lib_list_sort_r(e_ls, edge_isect_ls_sort_cb, eed->v2->co);
        }

        /* move original edge to filledgebase and add replacement
         * (which gets subdivided next) */
        {
          ScanFillEdge *eed_tmp;
          eed_tmp = lib_scanfill_edge_add(sf_ctx, eed->v1, eed->v2);
          lib_remlink(&sf_cxt->filledgebase, eed_tmp);
          lib_insertlinkafter(&sf_cxt->filledgebase, eed, eed_tmp);
          lib_remlink(&sf_cxt->filledgebase, eed);
          lib_addtail(filledgebase, eed);
          if (pi->edge_first == eed) {
            pi->edge_first = eed_tmp;
          }
          eed = eed_tmp;
        }

        for (isect_link = e_ls->first; isect_link; isect_link = isect_link->next) {
          ScanFillIsect *isect = isect_link->data;
          ScanFillEdge *eed_subd;

          eed_subd = lib_scanfill_edge_add(sf_cxt, isect->v, eed->v2);
          eed_subd->poly_nr = poly_nr;
          eed->v2 = isect->v;

          lib_remlink(&sf_cxt->filledgebase, eed_subd);
          lib_insertlinkafter(&sf_cxt->filledgebase, eed, eed_subd);

          /* step to the next edge and continue dividing */
          eed = eed_subd;
        }

        lib_freelist(e_ls);
        mem_free(e_ls);

        if (pi->edge_last == NULL) {
          pi->edge_last = eed;
        }
      }
    }
  }

  lib_freelistN(&isect_lb);
  lib_ghash_free(isect_hash, NULL, NULL);

  {
    ScanFillEdge *e_init;
    ScanFillEdge *e_curr;
    ScanFillEdge *e_next;

    ScanFillVert *v_prev;
    ScanFillVert *v_curr;

    bool inside = false;

    /* first vert */
#if 0
    e_init = pi->edge_last;
    e_curr = e_init;
    e_next = pi->edge_first;

    v_prev = e_curr->v1;
    v_curr = e_curr->v2;
#else

    /* find outside vert */
    {
      ScanFillEdge *eed;
      ScanFillEdge *eed_prev;
      float min_x = FLT_MAX;

      e_curr = pi->edge_last;
      e_next = pi->edge_first;

      eed_prev = pi->edge_last;
      for (eed = pi->edge_first; eed; eed = (eed == pi->edge_last) ? NULL : eed->next) {
        if (eed->v2->co[0] < min_x) {
          min_x = eed->v2->co[0];
          e_curr = eed_prev;
          e_next = eed;
        }
        eed_prev = eed;
      }

      e_init = e_curr;
      v_prev = e_curr->v1;
      v_curr = e_curr->v2;
    }
#endif

    lib_assert(e_curr->poly_nr == poly_nr);
    lib_assert(pi->edge_last->poly_nr == poly_nr);

    do {
      ScanFillVert *v_next;

      v_next = (e_next->v1 == v_curr) ? e_next->v2 : e_next->v1;
      lib_assert(ELEM(v_curr, e_next->v1, e_next->v2));

      /* track intersections */
      if (inside) {
        EFLAG_SET(e_next, E_ISDELETE);
      }
      if (v_next->user_flag & V_ISISECT) {
        inside = !inside;
      }
      /* now step... */

      v_prev = v_curr;
      v_curr = v_next;
      e_curr = e_next;

      e_next = edge_step(poly_info, poly_nr, v_prev, v_curr, e_curr);

    } while (e_curr != e_init);
  }

  return true;
}

bool lib_scanfill_calc_self_isect(ScanFillCxt *sf_cxt,
                                  List *remvertbase,
                                  List *remedgebase)
{
  const uint poly_num = (uint)sf_cxt->poly_nr + 1;
  uint eed_index = 0;
  bool changed = false;

  PolyInfo *poly_info;

  if (UNLIKELY(sf_cxt->poly_nr == SF_POLY_UNSET)) {
    return false;
  }

  poly_info = mem_calloc(sizeof(*poly_info) * poly_num, __func__);

  /* get the polygon span */
  if (sf_cxt->poly_nr == 0) {
    poly_info->edge_first = sf_cxt->filledgebase.first;
    poly_info->edge_last = sf_cxt->filledgebase.last;
  }
  else {
    ushort poly_nr;
    ScanFillEdge *eed;

    poly_nr = 0;

    for (eed = sf_ctx->filledgebase.first; eed; eed = eed->next, eed_index++) {

      lib_assert(eed->poly_nr == eed->v1->poly_nr);
      lib_assert(eed->poly_nr == eed->v2->poly_nr);

      if ((poly_info[poly_nr].edge_last != NULL) &&
          (poly_info[poly_nr].edge_last->poly_nr != eed->poly_nr))
      {
        poly_nr++;
      }

      if (poly_info[poly_nr].edge_first == NULL) {
        poly_info[poly_nr].edge_first = eed;
        poly_info[poly_nr].edge_last = eed;
      }
      else if (poly_info[poly_nr].edge_last->poly_nr == eed->poly_nr) {
        poly_info[poly_nr].edge_last = eed;
      }

      lib_assert(poly_info[poly_nr].edge_first->poly_nr == poly_info[poly_nr].edge_last->poly_nr);
    }
  }

  /* self-intersect each polygon */
  {
    ushort poly_nr;
    for (poly_nr = 0; poly_nr < poly_num; poly_nr++) {
      changed |= scanfill_preprocess_self_isect(sf_cxt, poly_info, poly_nr, remedgebase);
    }
  }

  mem_free(poly_info);

  if (changed == false) {
    return false;
  }

  /* move free edges into own list */
  {
    ScanFillEdge *eed;
    ScanFillEdge *eed_next;
    for (eed = sf_cxt->filledgebase.first; eed; eed = eed_next) {
      eed_next = eed->next;
      if (eed->user_flag & E_ISDELETE) {
        lib_remlink(&sf_cxt->filledgebase, eed);
        lib_addtail(remedgebase, eed);
      }
    }
  }

  /* move free verts into own list */
  {
    ScanFillEdge *eed;
    ScanFillVert *eve;
    ScanFillVert *eve_next;

    for (eve = sf_cxt->fillvertbase.first; eve; eve = eve->next) {
      eve->user_flag = 0;
      eve->poly_nr = SF_POLY_UNSET;
    }
    for (eed = sf_cxt->filledgebase.first; eed; eed = eed->next) {
      eed->v1->user_flag = 1;
      eed->v2->user_flag = 1;
      eed->poly_nr = SF_POLY_UNSET;
    }

    for (eve = sf_cxt->fillvertbase.first; eve; eve = eve_next) {
      eve_next = eve->next;
      if (eve->user_flag != 1) {
        lib_remlink(&sf_cxt->fillvertbase, eve);
        lib_addtail(remvertbase, eve);
      }
      else {
        eve->user_flag = 0;
      }
    }
  }

  /* polygon id's are no longer meaningful,
   * when removing self intersections we may have created new isolated polys */
  sf_cxt->poly_nr = SF_POLY_UNSET;

#if 0
  lib_scanfill_view3d_dump(sf_cxt);
  lib_scanfill_ob_dump(sf_cxt);
#endif

  return changed;
}
