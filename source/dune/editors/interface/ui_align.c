#include "types_screen.h"
#include "types_userdef.h"

#include "list_list.h"
#include "lib_math.h"
#include "lib_rect.h"

#include "interface.h"

#include "interface_intern.h"

#include "mem_guardedalloc.h"

#ifdef USE_UIBTN_SPATIAL_ALIGN

/* This struct stores a (simplified) 2D representation of all btns of a same align group,
 * with their immediate neighbors (if found),
 * and needed value to compute 'stitching' of aligned btns.
 *
 * note: This simplistic struct cannot fully represent complex layouts where btns share some
 *       'align space' with several others (see schema below), we'd need linked list and more
 *       complex code to handle that. However, looks like we can do without that for now,
 *       which is rather lucky!
 *
 *       <pre>
 *       +--------+-------+
 *       | BTN 1  | BTN 2 |      BTN 3 has two 'top' neighbors...
 *       |----------------|  =>  In practice, we only store one of BTN 1 or 2 (which ones is not
 *       |      BTN 3     |      really deterministic), and assume the other stores a ref to BTN 3.
 *       +----------------+
 *       </pre>
 *
 *       This will probably not work in all possible cases,
 *       but not sure we want to support such exotic cases anyway. */
typedef struct BtnAlign {
  uiBtn *btn;

  /* Neighbor btns */
  struct BtnAlign *neighbors[4];

  /* Pointers to coordinates (rctf values) of the btn. */
  float *borders[4];

  /* Distances to the neighbors. */
  float dists[4];

  /* Flags, used to mark whether we should 'stitch'
   * the corners of this btn with its neighbors' ones. */
  char flags[4];
} BtnAlign;

/* Side-related enums and flags. */
enum {
  /* Sides (used as indices, order is **crucial**,
   * this allows us to factorize code in a loop over the four sides). */
  LEFT = 0,
  TOP = 1,
  RIGHT = 2,
  DOWN = 3,
  TOTSIDES = 4,

  /* Stitch flags, built from sides values. */
  STITCH_LEFT = 1 << LEFT,
  STITCH_TOP = 1 << TOP,
  STITCH_RIGHT = 1 << RIGHT,
  STITCH_DOWN = 1 << DOWN,
};

/* Mapping between 'our' sides and 'public' UI_BTN_ALIGN flags, order must match enum above. */
#  define SIDE_TO_UI_BTN_ALIGN \
    { \
      UI_BTN_ALIGN_LEFT, UI_BTN_ALIGN_TOP, UI_BTN_ALIGN_RIGHT, UI_BTN_ALIGN_DOWN \
    }

/* Given one side, compute the three other ones */
#  define SIDE1(_s) (((_s) + 1) % TOTSIDES)
#  define OPPOSITE(_s) (((_s) + 2) % TOTSIDES)
#  define SIDE2(_s) (((_s) + 3) % TOTSIDES)

/* 0: LEFT/RIGHT sides; 1 = TOP/DOWN sides. */
#  define IS_COLUMN(_s) ((_s) % 2)

/* Stitch flag from side value. */
#  define STITCH(_s) (1 << (_s))

/* Max distance between to btns for them to be 'mergeable'. */
#  define MAX_DELTA 0.45f * max_ii(UI_UNIT_Y, UI_UNIT_X)

bool btn_can_align(const uiBtn *btn)
{
  const bool btype_can_align = !ELEM(btn->type,
                                     UI_BTYPE_LABEL,
                                     UI_BTYPE_CHECKBOX,
                                     UI_BTYPE_CHECKBOX_N,
                                     UI_BTYPE_TAB,
                                     UI_BTYPE_SEPR,
                                     UI_BTYPE_SEPR_LINE,
                                     UI_BTYPE_SEPR_SPACER);
  return (btype_can_align && (lib_rctf_size_x(&btn->rect) > 0.0f) &&
          (lib_rctf_size_y(&btn->rect) > 0.0f));
}

/* This fn checks a pair of btns (assumed in a same align group),
 * and if they are neighbors, set needed data accordingly.
 *
 * note It is designed to be called in total random order of btns.
 * Order-based optimizations are done by caller. */
static void block_align_proximity_compute(BtnAlign *btnal, BtnAlign *btnal_other)
{
  /* That's the biggest gap between two borders to consider them 'alignable'. */
  const float max_delta = MAX_DELTA;
  float delta, delta_side_opp;
  int side, side_opp;

  const bool btnal_can_align = ui_btn_can_align(btnal->btn);
  const bool btnal_other_can_align = ui_btn_can_align(btnal_other->btn);

  const bool btns_share[2] = {
      /* Sharing same line? */
      !((*btnal->borders[DOWN] >= *btnal_other->borders[TOP]) ||
        (*btnal->borders[TOP] <= *btnal_other->borders[DOWN])),
      /* Sharing same column? */
      !((*btnal->borders[LEFT] >= *btnal_other->borders[RIGHT]) ||
        (*btnal->borders[RIGHT] <= *btnal_other->borders[LEFT])),
  };

  /* Early out in case btns share no column or line, or if none can align... */
  if (!(btns_share[0] || btns_share[1]) || !(btnal_can_align || btnal_other_can_align)) {
    return;
  }

  for (side = 0; side < RIGHT; side++) {
    /* We are only interested in btns which share a same line
     * (LEFT/RIGHT sides) or column (TOP/DOWN sides). */
    if (btns_share[IS_COLUMN(side)]) {
      side_opp = OPPOSITE(side);

      /* We check both opposite sides at once, because with very small btns,
       * delta could be below max_delta for the wrong side
       * (that is, in horizontal case, the total width of two btns can be below max_delta).
       * We rely on exact zero value here as an 'already processed' flag,
       * so ensure we never actually set a zero value at this stage.
       * FLT_MIN is zero-enough for UI position computing. ;) */
      delta = max_ff(fabsf(*btnal->borders[side] - *btnal_other->borders[side_opp]), FLT_MIN);
      delta_side_opp = max_ff(fabsf(*btnal->borders[side_opp] - *btnal_other->borders[side]),
                              FLT_MIN);
      if (delta_side_opp < delta) {
        SWAP(int, side, side_opp);
        delta = delta_side_opp;
      }

      if (delta < max_delta) {
        /* We are only interested in neighbors that are
         * at least as close as already found ones. */
        if (delta <= btnal->dists[side]) {
          {
            /* We found an as close or closer neighbor.
             * If both btns are alignable, we set them as each other neighbors.
             * Else, we have an unalignable one, we need to reset the others matching
             * neighbor to NULL if its 'proximity distance'
             * is really lower with current one.
             *
             * NOTE: We cannot only execute that piece of code in case we found a
             *       **closer** neighbor, due to the limited way we represent neighbors
             *       (btns only know **one** neighbor on each side, when they can
             *       actually have several ones), it would prevent some btns to be
             *       properly 'neighborly-initialized'. */
            if (btnal_can_align && btnal_other_can_align) {
              btnal->neighbors[side] = btnal_other;
              btnal_other->neighbors[side_opp] = btnal;
            }
            else if (btnal_can_align && (delta < btnal->dists[side])) {
              btnal->neighbors[side] = NULL;
            }
            else if (btnal_other_can_align && (delta < btnal_other->dists[side_opp])) {
              btnal_other->neighbors[side_opp] = NULL;
            }
            btnal->dists[side] = btnal_other->dists[side_opp] = delta;
          }

          if (btnal_can_align && btnal_other_can_align) {
            const int side_s1 = SIDE1(side);
            const int side_s2 = SIDE2(side);

            const int stitch = STITCH(side);
            const int stitch_opp = STITCH(side_opp);

            if (btnal->neighbors[side] == NULL) {
              btnal->neighbors[side] = btnal_other;
            }
            if (btnal_other->neighbors[side_opp] == NULL) {
              btnal_other->neighbors[side_opp] = btnal;
            }

            /* We have a pair of neighbors, we have to check whether we
             *   can stitch their matching corners.
             *   E.g. if btnal_other is on the left of btnal (that is, side == LEFT),
             *        if both TOP (side_s1) coordinates of btns are close enough,
             *        we can stitch their upper matching corners,
             *        and same for DOWN (side_s2) side. */
            delta = fabsf(*btnal->borders[side_s1] - *btnal_other->borders[side_s1]);
            if (delta < max_delta) {
              btnal->flags[side_s1] |= stitch;
              btnal_other->flags[side_s1] |= stitch_opp;
            }
            delta = fabsf(*btnal->borders[side_s2] - *btnal_other->borders[side_s2]);
            if (delta < max_delta) {
              btnal->flags[side_s2] |= stitch;
              btnal_other->flags[side_s2] |= stitch_opp;
            }
          }
        }
        /* We assume two btns can only share one side at most - for until
         * we have spherical UI. */
        return;
      }
    }
  }
}

/* This fn takes care of case described in this schema:
 *
 * +-----------+-----------+
 * |   BTN 1   |   BTN 2   |
 * |-----------------------+
 * |   BTN 3   |
 * +-----------+
 *
 * Here, BTN 3 RIGHT side would not get 'dragged' to align with BTN 1 RIGHT side,
 * since BTN 3 has not RIGHT neighbor.
 * So, this fn, when called with BTN 1, will 'walk' the whole column in side_s1 direction
 * (TOP or DOWN when called for RIGHT side), and force btns like BTN 3 to align as needed,
 * if BTN 1 and BTN 3 were detected as needing top-right corner stitching in
 * block_align_proximity_compute() step.
 *
 * To avoid doing this twice, some stitching flags are cleared to break the
 * 'stitching connection' between neighbors. */
static void block_align_stitch_neighbors(BtnAlign *btnal,
                                         const int side,
                                         const int side_opp,
                                         const int side_s1,
                                         const int side_s2,
                                         const int align,
                                         const int align_opp,
                                         const float co)
{
  BtnAlign *btnal_neighbor;

  const int stitch_s1 = STITCH(side_s1);
  const int stitch_s2 = STITCH(side_s2);

  /* We have to check stitching flags on both sides of the stitching,
   * since we only clear one of them flags to break any future loop on same 'columns/side' case.
   * Also, if btnal is spanning over several rows or columns of neighbors,
   * it may have both of its stitching flags
   * set, but would not be the case of its immediate neighbor! */
  while ((btnal->flags[side] & stitch_s1) && (btnal = btnal->neighbors[side_s1]) &&
         (btnal->flags[side] & stitch_s2)) {
    btnal_neighbor = btnal->neighbors[side];

    /* If we actually do have a neighbor, we directly set its values accordingly,
     * and clear its matching 'dist' to prevent it being set again later... */
    if (btnal_neighbor) {
      btnal->btn->drawflag |= align;
      btnal_neighbor->btn->drawflag |= align_opp;
      *btnal_neighbor->borders[side_opp] = co;
      btnal_neighbor->dists[side_opp] = 0.0f;
    }
    /* See definition of UI_BTN_ALIGN_STITCH_LEFT/TOP for reason of this... */
    else if (side == LEFT) {
      btnal->btn->drawflag |= UI_BTN_ALIGN_STITCH_LEFT;
    }
    else if (side == TOP) {
      btnal->btn->drawflag |= UI_BTN_ALIGN_STITCH_TOP;
    }
    *btnal->borders[side] = co;
    btnal->dists[side] = 0.0f;
    /* Clearing one of the 'flags pair' here is enough to prevent this loop running on
     * the same column, side and direction again. */
    btnal->flags[side] &= ~stitch_s2;
  }
}

/* Helper to sort BtnAlign items by:
 *   - Their align group.
 *   - Their vertical position.
 *   - Their horizontal position.
 */
static int ui_block_align_btnal_cmp(const void *a, const void *b)
{
  const BtnAlign *btnal = a;
  const BtnAlign *btnal_other = b;

  /* Sort by align group. */
  if (btnal->btn->alignnr != btnal_other->btn->alignnr) {
    return btnal->btn->alignnr - btnal_other->btn->alignnr;
  }

  /* Sort vertically.
   * Note that Y of btns is decreasing (first btns have higher Y value than later ones). */
  if (*btnal->borders[TOP] != *btnal_other->borders[TOP]) {
    return (*btnal_other->borders[TOP] > *btnal->borders[TOP]) ? 1 : -1;
  }

  /* Sort horizontally. */
  if (*btnal->borders[LEFT] != *btnal_other->borders[LEFT]) {
    return (*btnal->borders[LEFT] > *btnal_other->borders[LEFT]) ? 1 : -1;
  }

  /* We cannot actually assert here, since in some very compressed space cases,
   * stupid UI code produces widgets which have the same TOP and LEFT positions...
   * We do not care really,
   * because this happens when UI is way too small to be usable anyway. */
  // lib_assert(0);
  return 0;
}

static void blockalign_btn_to_region(uiBtn *btn, const ARegion *region)
{
  rctf *rect = &btn->rect;
  const float btn_width = lib_rctf_size_x(rect);
  const float btn_height = lib_rctf_size_y(rect);
  const float outline_px = U.pixelsize; /* This may have to be made more variable. */

  switch (btn->drawflag & UI_BTN_ALIGN) {
    case UI_BTN_ALIGN_TOP:
      rect->ymax = region->winy + outline_px;
      rect->ymin = btn->rect.ymax - btn_height;
      break;
    case UI_BTN_ALIGN_DOWN:
      rect->ymin = -outline_px;
      rect->ymax = rect->ymin + btn_height;
      break;
    case UI_BTN_ALIGN_LEFT:
      rect->xmin = -outline_px;
      rect->xmax = rect->xmin + btn_width;
      break;
    case UI_BTN_ALIGN_RIGHT:
      rect->xmax = region->winx + outline_px;
      rect->xmin = rect->xmax - btn_width;
      break;
    default:
      /* Tabs may be shown in unaligned regions too, they just appear as regular btns then. */
      break;
  }
}

void blockalign_calc(uiBlock *block, const ARegion *region)
{
  int num_btns = 0;

  const int sides_to_ui_btn_align_flags[4] = SIDE_TO_UI_BTN_ALIGN;

  BtnAlign *btnal_array;
  BtnAlign *btnal, *btnal_other;
  int side;

  /* First loop: we count number of btns belonging to an align group,
   * and clear their align flag.
   * Tabs get some special treatment here, they get aligned to region border. */
  LIST_FOREACH (uiBtn *, btn, &block->btns) {
    /* special case: tabs need to be aligned to a region border, drawflag tells which one */
    if (btn->type == UI_BTYPE_TAB) {
      ui_block_align_btn_to_region(btn, region);
    }
    else {
      /* Clear old align flags. */
      btn->drawflag &= ~UI_BTN_ALIGN_ALL;
    }

    if (btn->alignnr != 0) {
      num_btns++;
    }
  }

  if (num_btns < 2) {
    /* No need to go further if we have nothing to align... */
    return;
  }

  /* Note that this is typically less than ~20, and almost always under ~100.
   * Even so, we can't ensure this value won't exceed available stack memory.
   * Fallback to allocation instead of using #alloca, see: T78636. */
  BtnAlign btnal_array_buf[256];
  if (num_btns <= ARRAY_SIZE(btnal_array_buf)) {
    btnal_array = btnal_array_buf;
  }
  else {
    btnal_array = _mallocN(sizeof(*btnal_array) * num_btns, __func__);
  }
  memset(btnal_array, 0, sizeof(*btnal_array) * (size_t)num_btns);

  /* Second loop: we initialize our BtnAlign data for each btn. */
  btnal = btnal_array;
  LIST_FOREACH (uiBtn *, btn, &block->btns) {
    if (btn->alignnr != 0) {
      btnal->btn = btn;
      btnal->borders[LEFT] = &btn->rect.xmin;
      btnal->borders[RIGHT] = &btn->rect.xmax;
      btnal->borders[DOWN] = &btn->rect.ymin;
      btnal->borders[TOP] = &btn->rect.ymax;
      copy_v4_fl(btnal->dists, FLT_MAX);
      btnal++;
    }
  }

  /* This will give us BtnAlign items regrouped by align group, vertical and horizontal location.
   * Note that, given how btns are defined in UI code,
   * btnal_array shall already be "nearly sorted"... */
  qsort(btnal_array, (size_t)num_btns, sizeof(*btnal_array), ui_block_align_btnal_cmp);

  /* Third loop: for each pair of btns in the same align group,
   * we compute their potential proximity. Note that each pair is checked only once, and that we
   * break early in case we know all remaining pairs will always be too far away. */
  int i;
  for (i = 0, btnal = btnal_array; i < num_btns; i++, btnal++) {
    const short alignnr = btnal->btn->alignnr;

    int j;
    for (j = i + 1, btnal_other = &btnal_array[i + 1]; j < num_btns; j++, btnal_other++) {
      const float max_delta = MAX_DELTA;

      /* Since they are sorted, btns after current btnal can only be of same or higher
       * group, and once they are not of same group, we know we can break this sub-loop and
       * start checking with next btnal. */
      if (btnal_other->btn->alignnr != alignnr) {
        break;
      }

      /* Since they are sorted vertically first, btns after current btnal can only be at
       * same or lower height, and once they are lower than a given threshold, we know we can
       * break this sub-loop and start checking with next btnal. */
      if ((*btnal->borders[DOWN] - *btnal_other->borders[TOP]) > max_delta) {
        break;
      }

      block_align_proximity_compute(btnal, btnal_other);
    }
  }

  /* Fourth loop: we have all our 'aligned' btns as a 'map' in btnal_array. We need to:
   *     - update their relevant coordinates to stitch them.
   *     - assign them valid flags */
  for (i = 0; i < num_btns; i++) {
    btnal = &btnal_array[i];

    for (side = 0; side < TOTSIDES; side++) {
      btnal_other = btnal->neighbors[side];

      if (btnal_other) {
        const int side_opp = OPPOSITE(side);
        const int side_s1 = SIDE1(side);
        const int side_s2 = SIDE2(side);

        const int align = sides_to_ui_btn_align_flags[side];
        const int align_opp = sides_to_ui_btn_align_flags[side_opp];

        float co;

        btnal->btn->drawflag |= align;
        btnal_other->btn->drawflag |= align_opp;
        if (!IS_EQF(btnal->dists[side], 0.0f)) {
          float *delta = &btnal->dists[side];

          if (*btnal->borders[side] < *btnal_other->borders[side_opp]) {
            *delta *= 0.5f;
          }
          else {
            *delta *= -0.5f;
          }
          co = (*btnal->borders[side] += *delta);

          if (!IS_EQF(btnal_other->dists[side_opp], 0.0f)) {
            lib_assert(btnal_other->dists[side_opp] * 0.5f == fabsf(*delta));
            *btnal_other->borders[side_opp] = co;
            btnal_other->dists[side_opp] = 0.0f;
          }
          *delta = 0.0f;
        }
        else {
          co = *btnal->borders[side];
        }

        blockalign_stitch_neighbors(
            btnal, side, side_opp, side_s1, side_s2, align, align_opp, co);
        blockalign_stitch_neighbors(
            btnal, side, side_opp, side_s2, side_s1, align, align_opp, co);
      }
    }
  }
  if (btnal_array_buf != btnal_array) {
    mem_freen(btnal_array);
  }
}

#  undef SIDE_TO_UI_BTN_ALIGN
#  undef SIDE1
#  undef OPPOSITE
#  undef SIDE2
#  undef IS_COLUMN
#  undef STITCH
#  undef MAX_DELTA

#else /* !USE_UIBTN_SPATIAL_ALIGN */

bool btnalign_can(const uiBtn *btn)
{
  return !ELEM(btn->type,
               UI_BTYPE_LABEL,
               UI_BTYPE_CHECKBOX,
               UI_BTYPE_CHECKBOX_N,
               UI_BTYPE_SEPR,
               UI_BTYPE_SEPR_LINE,
               UI_BTYPE_SEPR_SPACER);
}

static bool btnlist_is_horiz(uiBtn *btn1, uiBtn *btn2)
{
  float dx, dy;

  /* simple case which can fail if btns shift apart
   * with proportional layouts, see: T38602. */
  if ((btn1->rect.ymin == btn2->rect.ymin) && (btn1->rect.xmin != btn2->rect.xmin)) {
    return true;
  }

  dx = fabsf(btn1->rect.xmax - btn2->rect.xmin);
  dy = fabsf(btn1->rect.ymin - btn2->rect.ymax);

  return (dx <= dy);
}

static void blockalign_btn_calc(uiBtn *first, short nr)
{
  uiBtn *prev, *btn = NULL, *next;
  int flag = 0, cols = 0, rows = 0;

  /* auto align */

  for (btn = first; btn && btn->alignnr == nr; btn = btn->next) {
    if (btn->next && btn->next->alignnr == nr) {
      if (btnlist_is_horiz(btn, btn->next)) {
        cols++;
      }
      else {
        rows++;
      }
    }
  }

  /* rows == 0: 1 row, cols == 0: 1 column */

  /* NOTE: manipulation of 'flag' in the loop below is confusing.
   * In some cases it's assigned, other times OR is used. */
  for (btn = first, prev = NULL; brn && btn->alignnr == nr; prev = btn, btn = btn->next) {
    next = btn->next;
    if (next && next->alignnr != nr) {
      next = NULL;
    }

    /* clear old flag */
    btn->drawflag &= ~UI_BTN_ALIGN;

    if (flag == 0) { /* first case */
      if (next) {
        if (btns_are_horiz(btn, next)) {
          if (rows == 0) {
            flag = UI_BTN_ALIGN_RIGHT;
          }
          else {
            flag = UI_BTN_ALIGN_DOWN | UI_BTN_ALIGN_RIGHT;
          }
        }
        else {
          flag = UI_BTN_ALIGN_DOWN;
        }
      }
    }
    else if (next == NULL) { /* last case */
      if (prev) {
        if (btns_are_horiz(prev, btn)) {
          if (rows == 0) {
            flag = UI_BTN_ALIGN_LEFT;
          }
          else {
            flag = UI_BTN_ALIGN_TOP | UI_BTN_ALIGN_LEFT;
          }
        }
        else {
          flag = UI_BTN_ALIGN_TOP;
        }
      }
    }
    else if (btns_are_horiz(btn, next)) {
      /* check if this is already second row */
      if (prev && btns_are_horiz(prev, btn) == 0) {
        flag &= ~UI_BTN_ALIGN_LEFT;
        flag |= UI_BTN_ALIGN_TOP;
        /* exception case: bottom row */
        if (rows > 0) {
          uiBtn *bt = btn;
          while (bt && bt->alignnr == nr) {
            if (bt->next && bt->next->alignnr == nr && btns_are_horiz(bt, bt->next) == 0) {
              break;
            }
            bt = bt->next;
          }
          if (bt == NULL || bt->alignnr != nr) {
            flag = UI_BTN_ALIGN_TOP | UI_BTN_ALIGN_RIGHT;
          }
        }
      }
      else {
        flag |= UI_BTN_ALIGN_LEFT;
      }
    }
    else {
      if (cols == 0) {
        flag |= UI_BTN_ALIGN_TOP;
      }
      else { /* next btn switches to new row */

        if (prev && btnlist_is_horiz(prev, btn)) {
          flag |= UI_BTN_ALIGN_LEFT;
        }
        else {
          flag &= ~UI_BTN_ALIGN_LEFT;
          flag |= UI_BTN_ALIGN_TOP;
        }

        if ((flag & UI_BTN_ALIGN_TOP) == 0) { /* still top row */
          if (prev) {
            if (next && flex_is_horiz(btn, next)) {
              flag = UI_BTN_ALIGN_DOWN | UI_BTN_ALIGN_LEFT | UI_BTN_ALIGN_RIGHT;
            }
            else {
              /* last btn in top row */
              flag = UI_BTN_ALIGN_DOWN | UI_BTN_ALIGN_LEFT;
            }
          }
          else {
            flag |= UI_BTN_ALIGN_DOWN;
          }
        }
        else {
          flag |= UI_BTN_ALIGN_TOP;
        }
      }
    }

    btn->drawflag |= flag;

    /* merge coordinates */
    if (prev) {
      /* simple cases */
      if (rows == 0) {
        btn->rect.xmin = (prev->rect.xmax + btn->rect.xmin) / 2.0f;
        prev->rect.xmax = btn->rect.xmin;
      }
      else if (cols == 0) {
        btn->rect.ymax = (prev->rect.ymin + btn->rect.ymax) / 2.0f;
        prev->rect.ymin = btn->rect.ymax;
      }
      else {
        if (flex_is_horiz(prev, btn)) {
          btn->rect.xmin = (prev->rect.xmax + btn->rect.xmin) / 2.0f;
          prev->rect.xmax = btn->rect.xmin;
          /* copy height too */
          btn->rect.ymax = prev->rect.ymax;
        }
        else if (prev->prev && flex_is_horiz(prev->prev, prev) == 0) {
          /* the previous btn is a single one in its row */
          btn->rect.ymax = (prev->rect.ymin + btn->rect.ymax) / 2.0f;
          prev->rect.ymin = btn->rect.ymax;

          btn->rect.xmin = prev->rect.xmin;
          if (next && flex_is_horiz(btn, next) == 0) {
            btn->rect.xmax = prev->rect.xmax;
          }
        }
        else {
          /* the previous btn is not a single one in its row */
          btn->rect.ymax = prev->rect.ymin;
        }
      }
    }
  }
}

void blockalign_calc(uiBlock *block, const struct ARegion *UNUSED(region))
{
  short nr;

  /* align btns with same align nr */
  LIST_FOREACH (uiBtn *, btn, &block->btnlist) {
    if (btn->alignnr) {
      nr = btn->alignnr;
      flex_btnalign_calc(btn, nr);

      /* skip with same number */
      for (; btn && btn->alignnr == nr; btn = btn->next) {
        /* pass */
      }

      if (!btn) {
        break;
      }
    }
    else {
      btn = btn->next;
    }
  }
}

#endif /* !USE_UIBTN_SPATIAL_ALIGN */

int btnalign_opposite_to_areaalign_get(const ARegion *region)
{
  const ARegion *align_region = (region->alignment & RGN_SPLIT_PREV && region->prev) ?
                                    region->prev :
                                    region;

  switch (RGN_ALIGN_ENUM_FROM_MASK(align_region->alignment)) {
    case RGN_ALIGN_TOP:
      return UI_BTN_ALIGN_DOWN;
    case RGN_ALIGN_BOTTOM:
      return UI_BTN_ALIGN_TOP;
    case RGN_ALIGN_LEFT:
      return UI_BTN_ALIGN_RIGHT;
    case RGN_ALIGN_RIGHT:
      return UI_BTN_ALIGN_LEFT;
  }

  return 0;
}
