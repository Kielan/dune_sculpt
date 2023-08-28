#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_scene.h"
#include "types_userdef.h"

#include "lib_array.h"
#include "lib_easing.h"
#include "lib_link_utils.h"
#include "lib_list.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_timecode.h"
#include "lib_utildefines.h"

#include "dune_context.h"
#include "dune_global.h"
#include "dune_screen.h"

#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "wm_api.h"

#include "BLF_api.h"

#include "ed_screen.h"

#include "ui_interface.h"
#include "ui_view2d.h"

#include "interface_intern.h"

static void view2d_curRect_validate_resize(View2D *v2d, bool resize);

/** Internal Utilities **/
LIB_INLINE int clamp_float_to_int(const float f)
{
  const float min = (float)INT_MIN;
  const float max = (float)INT_MAX;

  if (UNLIKELY(f < min)) {
    return min;
  }
  if (UNLIKELY(f > max)) {
    return (int)max;
  }
  return (int)f;
}

/* use instead of lib_rcti_rctf_copy so we have consistent behavior
 * with users of clamp_float_to_int. */
LIB_INLINE void clamp_rctf_to_rcti(rcti *dst, const rctf *src)
{
  dst->xmin = clamp_float_to_int(src->xmin);
  dst->xmax = clamp_float_to_int(src->xmax);
  dst->ymin = clamp_float_to_int(src->ymin);
  dst->ymax = clamp_float_to_int(src->ymax);
}

/* still unresolved: scrolls hide/unhide vs region mask handling */
/* there's V2D_SCROLL_HORIZONTAL_HIDE and V2D_SCROLL_HORIZONTAL_FULLR ... */

/* Internal Scroll & Mask Utilities **/

/* helper to allow scrollbars to dynamically hide
 * - returns a copy of the scrollbar settings with the flags to display
 *   horizontal/vertical scrollbars removed
 * - input scroll value is the v2d->scroll var
 * - hide flags are set per region at drawtime */
static int view2d_scroll_mapped(int scroll)
{
  if (scroll & V2D_SCROLL_HORIZONTAL_FULLR) {
    scroll &= ~V2D_SCROLL_HORIZONTAL;
  }
  if (scroll & V2D_SCROLL_VERTICAL_FULLR) {
    scroll &= ~V2D_SCROLL_VERTICAL;
  }
  return scroll;
}

void view2d_mask_from_win(const View2D *v2d, rcti *r_mask)
{
  r_mask->xmin = 0;
  r_mask->ymin = 0;
  r_mask->xmax = v2d->winx - 1; /* -1 yes! masks are pixels */
  r_mask->ymax = v2d->winy - 1;
}

/* Called each time View2D.cur changes, to dynamically update masks.
 * param mask_scroll: Optionally clamp scrollbars by this region. */
static void view2d_masks(View2D *v2d, const rcti *mask_scroll)
{
  int scroll;

  /* mask - view frame */
  ui_view2d_mask_from_win(v2d, &v2d->mask);
  if (mask_scroll == NULL) {
    mask_scroll = &v2d->mask;
  }

  /* check size if hiding flag is set: */
  if (v2d->scroll & V2D_SCROLL_HORIZONTAL_HIDE) {
    if (!(v2d->scroll & V2D_SCROLL_HORIZONTAL_HANDLES)) {
      if (BLI_rctf_size_x(&v2d->tot) > BLI_rctf_size_x(&v2d->cur)) {
        v2d->scroll &= ~V2D_SCROLL_HORIZONTAL_FULLR;
      }
      else {
        v2d->scroll |= V2D_SCROLL_HORIZONTAL_FULLR;
      }
    }
  }
  if (v2d->scroll & V2D_SCROLL_VERTICAL_HIDE) {
    if (!(v2d->scroll & V2D_SCROLL_VERTICAL_HANDLES)) {
      if (lib_rctf_size_y(&v2d->tot) + 0.01f > BLI_rctf_size_y(&v2d->cur)) {
        v2d->scroll &= ~V2D_SCROLL_VERTICAL_FULLR;
      }
      else {
        v2d->scroll |= V2D_SCROLL_VERTICAL_FULLR;
      }
    }
  }

  scroll = view2d_scroll_mapped(v2d->scroll);

  /* Scrollers are based off region-size:
   * - they can only be on one to two edges of the region they define
   * - if they overlap, they must not occupy the corners (which are reserved for other widgets) */
  if (scroll) {
    float scroll_width, scroll_height;

    view2d_scroller_size_get(v2d, &scroll_width, &scroll_height);

    /* vertical scroller */
    if (scroll & V2D_SCROLL_LEFT) {
      /* on left-hand edge of region */
      v2d->vert = *mask_scroll;
      v2d->vert.xmax = scroll_width;
    }
    else if (scroll & V2D_SCROLL_RIGHT) {
      /* on right-hand edge of region */
      v2d->vert = *mask_scroll;
      v2d->vert.xmax++; /* one pixel extra... was leaving a minor gap... */
      v2d->vert.xmin = v2d->vert.xmax - scroll_width;
    }

    /* Currently, all regions that have vertical scale handles,
     * also have the scrubbing area at the top.
     * So the scrollbar has to move down a bit. */
    if (scroll & V2D_SCROLL_VERTICAL_HANDLES) {
      v2d->vert.ymax -= UI_TIME_SCRUB_MARGIN_Y;
    }

    /* horizontal scroller */
    if (scroll & V2D_SCROLL_BOTTOM) {
      /* on bottom edge of region */
      v2d->hor = *mask_scroll;
      v2d->hor.ymax = scroll_height;
    }
    else if (scroll & V2D_SCROLL_TOP) {
      /* on upper edge of region */
      v2d->hor = *mask_scroll;
      v2d->hor.ymin = v2d->hor.ymax - scroll_height;
    }

    /* adjust vertical scroller if there's a horizontal scroller, to leave corner free */
    if (scroll & V2D_SCROLL_VERTICAL) {
      if (scroll & V2D_SCROLL_BOTTOM) {
        /* on bottom edge of region */
        v2d->vert.ymin = v2d->hor.ymax;
      }
      else if (scroll & V2D_SCROLL_TOP) {
        /* on upper edge of region */
        v2d->vert.ymax = v2d->hor.ymin;
      }
    }
  }
}

/* View2D Refresh and Validation (Spatial) **/
void view2d_region_reinit(View2D *v2d, short type, int winx, int winy)
{
  bool tot_changed = false, do_init;
  const uiStyle *style = UI_style_get();

  do_init = (v2d->flag & V2D_IS_INIT) == 0;

  /* see eView2D_CommonViewTypes in ui_view2d.h for available view presets */
  switch (type) {
    /* 'standard view' - optimum setup for 'standard' view behavior,
     * that should be used new views as basis for their
     * own unique View2D settings, which should be used instead of this in most cases... */
    case V2D_COMMONVIEW_STANDARD: {
      /* for now, aspect ratio should be maintained,
       * and zoom is clamped within sane default limits */
      v2d->keepzoom = (V2D_KEEPASPECT | V2D_LIMITZOOM);
      v2d->minzoom = 0.01f;
      v2d->maxzoom = 1000.0f;

      /* View2D tot rect and cur should be same size,
       * and aligned using 'standard' OpenGL coordinates for now:
       * - region can resize 'tot' later to fit other data
       * - keeptot is only within bounds, as strict locking is not that critical
       * - view is aligned for (0,0) -> (winx-1, winy-1) setup */
      v2d->align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_NEG_Y);
      v2d->keeptot = V2D_KEEPTOT_BOUNDS;
      if (do_init) {
        v2d->tot.xmin = v2d->tot.ymin = 0.0f;
        v2d->tot.xmax = (float)(winx - 1);
        v2d->tot.ymax = (float)(winy - 1);

        v2d->cur = v2d->tot;
      }
      /* scrollers - should we have these by default? */
      /* for now, we don't override this, or set it either! */
      break;
    }
    /* 'list/channel view' - zoom, aspect ratio, and alignment restrictions are set here */
    case V2D_COMMONVIEW_LIST: {
      /* zoom + aspect ratio are locked */
      v2d->keepzoom = (V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y | V2D_LIMITZOOM | V2D_KEEPASPECT);
      v2d->minzoom = v2d->maxzoom = 1.0f;

      /* tot rect has strictly regulated placement, and must only occur in +/- quadrant */
      v2d->align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_POS_Y);
      v2d->keeptot = V2D_KEEPTOT_STRICT;
      tot_changed = do_init;

      /* scroller settings are currently not set here... that is left for regions... */
      break;
    }
    /* 'stack view' - practically the same as list/channel view,
     * except is located in the pos y half instead.
     * Zoom, aspect ratio, and alignment restrictions are set here. */
    case V2D_COMMONVIEW_STACK: {
      /* zoom + aspect ratio are locked */
      v2d->keepzoom = (V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y | V2D_LIMITZOOM | V2D_KEEPASPECT);
      v2d->minzoom = v2d->maxzoom = 1.0f;

      /* tot rect has strictly regulated placement, and must only occur in +/+ quadrant */
      v2d->align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_NEG_Y);
      v2d->keeptot = V2D_KEEPTOT_STRICT;
      tot_changed = do_init;

      /* scroller settings are currently not set here... that is left for regions... */
      break;
    }
    /* 'header' regions - zoom, aspect ratio,
     * alignment, and panning restrictions are set here */
    case V2D_COMMONVIEW_HEADER: {
      /* zoom + aspect ratio are locked */
      v2d->keepzoom = (V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y | V2D_LIMITZOOM | V2D_KEEPASPECT);
      v2d->minzoom = v2d->maxzoom = 1.0f;

      if (do_init) {
        v2d->tot.xmin = 0.0f;
        v2d->tot.xmax = winx;
        v2d->tot.ymin = 0.0f;
        v2d->tot.ymax = winy;
        v2d->cur = v2d->tot;

        v2d->min[0] = v2d->max[0] = (float)(winx - 1);
        v2d->min[1] = v2d->max[1] = (float)(winy - 1);
      }
      /* tot rect has strictly regulated placement, and must only occur in +/+ quadrant */
      v2d->align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_NEG_Y);
      v2d->keeptot = V2D_KEEPTOT_STRICT;
      tot_changed = do_init;

      /* panning in y-axis is prohibited */
      v2d->keepofs = V2D_LOCKOFS_Y;

      /* absolutely no scrollers allowed */
      v2d->scroll = 0;
      break;
    }
    /* panels view, with horizontal/vertical align */
    case V2D_COMMONVIEW_PANELS_UI: {

      /* for now, aspect ratio should be maintained,
       * and zoom is clamped within sane default limits */
      v2d->keepzoom = (V2D_KEEPASPECT | V2D_LIMITZOOM | V2D_KEEPZOOM);
      v2d->minzoom = 0.5f;
      v2d->maxzoom = 2.0f;

      v2d->align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_POS_Y);
      v2d->keeptot = V2D_KEEPTOT_BOUNDS;

      /* NOTE: scroll is being flipped in #ED_region_panels() drawing. */
      v2d->scroll |= (V2D_SCROLL_HORIZONTAL_HIDE | V2D_SCROLL_VERTICAL_HIDE);

      if (do_init) {
        const float panelzoom = (style) ? style->panelzoom : 1.0f;

        v2d->tot.xmin = 0.0f;
        v2d->tot.xmax = winx;

        v2d->tot.ymax = 0.0f;
        v2d->tot.ymin = -winy;

        v2d->cur.xmin = 0.0f;
        v2d->cur.xmax = (winx)*panelzoom;

        v2d->cur.ymax = 0.0f;
        v2d->cur.ymin = (-winy) * panelzoom;
      }
      break;
    }
    /* other view types are completely defined using their own settings already */
    default:
      /* we don't do anything here,
       * as settings should be fine, but just make sure that rect */
      break;
  }

  /* set initialized flag so that View2D doesn't get reinitialized next time again */
  v2d->flag |= V2D_IS_INIT;

  /* store view size */
  v2d->winx = winx;
  v2d->winy = winy;

  view2d_masks(v2d, NULL);

  if (do_init) {
    /* Visible by default. */
    v2d->alpha_hor = v2d->alpha_vert = 255;
  }

  /* set 'tot' rect before setting cur? */
  /* confusing stuff here still */
  if (tot_changed) {
    view2d_totRect_set_resize(v2d, winx, winy, !do_init);
  }
  else {
    view2d_curRect_validate_resize(v2d, !do_init);
  }
}

/* Ensure View2D rects remain in a viable configuration
 * 'cur' is not allowed to be: larger than max, smaller than min, or outside of 'tot' */
/* pre2.5 -> this used to be called  test_view2d() */
static void ui_view2d_curRect_validate_resize(View2D *v2d, bool resize)
{
  float totwidth, totheight, curwidth, curheight, width, height;
  float winx, winy;
  rctf *cur, *tot;

  /* use mask as size of region that View2D resides in, as it takes into account
   * scrollbars already - keep in sync with zoomx/zoomy in view_zoomstep_apply_ex! */
  winx = (float)(lib_rcti_size_x(&v2d->mask) + 1);
  winy = (float)(lib_rcti_size_y(&v2d->mask) + 1);

  /* get ptrs to rcts for less typing */
  cur = &v2d->cur;
  tot = &v2d->tot;

  /* we must satisfy the following constraints (in decreasing order of importance):
   * - alignment restrictions are respected
   * - cur must not fall outside of tot
   * - axis locks (zoom and offset) must be maintained
   * - zoom must not be excessive (check either sizes or zoom values)
   * - aspect ratio should be respected (NOTE: this is quite closely related to zoom too) */

  /* Step 1: if keepzoom, adjust the sizes of the rects only
   * - firstly, we calculate the sizes of the rects
   * - curwidth and curheight are saved as reference... modify width and height values here */
  totwidth = lib_rctf_size_x(tot);
  totheight = lib_rctf_size_y(tot);
  /* keep in sync with zoomx/zoomy in view_zoomstep_apply_ex! */
  curwidth = width = lib_rctf_size_x(cur);
  curheight = height = lib_rctf_size_y(cur);

  /* if zoom is locked, size on the appropriate axis is reset to mask size */
  if (v2d->keepzoom & V2D_LOCKZOOM_X) {
    width = winx;
  }
  if (v2d->keepzoom & V2D_LOCKZOOM_Y) {
    height = winy;
  }

  /* values used to divide, so make it safe
   * NOTE: width and height must use FLT_MIN instead of 1, otherwise it is impossible to
   *       get enough resolution in Graph Editor for editing some curves */
  if (width < FLT_MIN) {
    width = 1;
  }
  if (height < FLT_MIN) {
    height = 1;
  }
  if (winx < 1) {
    winx = 1;
  }
  if (winy < 1) {
    winy = 1;
  }

  /* V2D_LIMITZOOM indicates that zoom level should be preserved when the window size changes */
  if (resize && (v2d->keepzoom & V2D_KEEPZOOM)) {
    float zoom, oldzoom;

    if ((v2d->keepzoom & V2D_LOCKZOOM_X) == 0) {
      zoom = winx / width;
      oldzoom = v2d->oldwinx / curwidth;

      if (oldzoom != zoom) {
        width *= zoom / oldzoom;
      }
    }

    if ((v2d->keepzoom & V2D_LOCKZOOM_Y) == 0) {
      zoom = winy / height;
      oldzoom = v2d->oldwiny / curheight;

      if (oldzoom != zoom) {
        height *= zoom / oldzoom;
      }
    }
  }
  /* keepzoom (V2D_LIMITZOOM set), indicates that zoom level on each axis must not exceed limits
   * NOTE: in general, it is not expected that the lock-zoom will be used in conjunction with this */
  else if (v2d->keepzoom & V2D_LIMITZOOM) {

    /* check if excessive zoom on x-axis */
    if ((v2d->keepzoom & V2D_LOCKZOOM_X) == 0) {
      const float zoom = winx / width;
      if (zoom < v2d->minzoom) {
        width = winx / v2d->minzoom;
      }
      else if (zoom > v2d->maxzoom) {
        width = winx / v2d->maxzoom;
      }
    }

    /* check if excessive zoom on y-axis */
    if ((v2d->keepzoom & V2D_LOCKZOOM_Y) == 0) {
      const float zoom = winy / height;
      if (zoom < v2d->minzoom) {
        height = winy / v2d->minzoom;
      }
      else if (zoom > v2d->maxzoom) {
        height = winy / v2d->maxzoom;
      }
    }
  }
  else {
    /* make sure sizes don't exceed that of the min/max sizes
     * (even though we're not doing zoom clamping) */
    CLAMP(width, v2d->min[0], v2d->max[0]);
    CLAMP(height, v2d->min[1], v2d->max[1]);
  }

  /* check if we should restore aspect ratio (if view size changed) */
  if (v2d->keepzoom & V2D_KEEPASPECT) {
    bool do_x = false, do_y = false, do_cur /* , do_win */ /* UNUSED */;
    float curRatio, winRatio;

    /* when a window edge changes, the aspect ratio can't be used to
     * find which is the best new 'cur' rect. that's why it stores 'old' */
    if (winx != v2d->oldwinx) {
      do_x = true;
    }
    if (winy != v2d->oldwiny) {
      do_y = true;
    }

    curRatio = height / width;
    winRatio = winy / winx;

    /* Both sizes change (area/region maximized). */
    if (do_x == do_y) {
      if (do_x && do_y) {
        /* here is 1,1 case, so all others must be 0,0 */
        if (fabsf(winx - v2d->oldwinx) > fabsf(winy - v2d->oldwiny)) {
          do_y = false;
        }
        else {
          do_x = false;
        }
      }
      else if (winRatio > curRatio) {
        do_x = false;
      }
      else {
        do_x = true;
      }
    }
    do_cur = do_x;
    /* do_win = do_y; */ /* UNUSED */

    if (do_cur) {
      if ((v2d->keeptot == V2D_KEEPTOT_STRICT) && (winx != v2d->oldwinx)) {
        /* Special exception for Outliner (and later channel-lists):
         * - The view may be moved left to avoid contents
         *   being pushed out of view when view shrinks.
         * - The keeptot code will make sure cur->xmin will not be less than tot->xmin
         *   (which cannot be allowed).
         * - width is not adjusted for changed ratios here. */
        if (winx < v2d->oldwinx) {
          const float temp = v2d->oldwinx - winx;

          cur->xmin -= temp;
          cur->xmax -= temp;

          /* width does not get modified, as keepaspect here is just set to make
           * sure visible area adjusts to changing view shape! */
        }
      }
      else {
        /* portrait window: correct for x */
        width = height / winRatio;
      }
    }
    else {
      if ((v2d->keeptot == V2D_KEEPTOT_STRICT) && (winy != v2d->oldwiny)) {
        /* special exception for Outliner (and later channel-lists):
         * - Currently, no actions need to be taken here.. */

        if (winy < v2d->oldwiny) {
          const float temp = v2d->oldwiny - winy;

          if (v2d->align & V2D_ALIGN_NO_NEG_Y) {
            cur->ymin -= temp;
            cur->ymax -= temp;
          }
          else { /* Assume V2D_ALIGN_NO_POS_Y or combination */
            cur->ymin += temp;
            cur->ymax += temp;
          }
        }
      }
      else {
        /* landscape window: correct for y */
        height = width * winRatio;
      }
    }

    /* store region size for next time */
    v2d->oldwinx = (short)winx;
    v2d->oldwiny = (short)winy;
  }

  /* Step 2: apply new sizes to cur rect,
   * but need to take into account alignment settings here... */
  if ((width != curwidth) || (height != curheight)) {
    float temp, dh;

    /* Resize from center-point, unless otherwise specified. */
    if (width != curwidth) {
      if (v2d->keepofs & V2D_LOCKOFS_X) {
        cur->xmax += width - BLI_rctf_size_x(cur);
      }
      else if (v2d->keepofs & V2D_KEEPOFS_X) {
        if (v2d->align & V2D_ALIGN_NO_POS_X) {
          cur->xmin -= width - BLI_rctf_size_x(cur);
        }
        else {
          cur->xmax += width - BLI_rctf_size_x(cur);
        }
      }
      else {
        temp = BLI_rctf_cent_x(cur);
        dh = width * 0.5f;

        cur->xmin = temp - dh;
        cur->xmax = temp + dh;
      }
    }
    if (height != curheight) {
      if (v2d->keepofs & V2D_LOCKOFS_Y) {
        cur->ymax += height - BLI_rctf_size_y(cur);
      }
      else if (v2d->keepofs & V2D_KEEPOFS_Y) {
        if (v2d->align & V2D_ALIGN_NO_POS_Y) {
          cur->ymin -= height - BLI_rctf_size_y(cur);
        }
        else {
          cur->ymax += height - BLI_rctf_size_y(cur);
        }
      }
      else {
        temp = BLI_rctf_cent_y(cur);
        dh = height * 0.5f;

        cur->ymin = temp - dh;
        cur->ymax = temp + dh;
      }
    }
  }

  /* Step 3: adjust so that it doesn't fall outside of bounds of 'tot' */
  if (v2d->keeptot) {
    float temp, diff;

    /* recalculate extents of cur */
    curwidth = lib_rctf_size_x(cur);
    curheight = lib_rctf_size_y(cur);

    /* width */
    if ((curwidth > totwidth) &&
        !(v2d->keepzoom & (V2D_KEEPZOOM | V2D_LOCKZOOM_X | V2D_LIMITZOOM))) {
      /* if zoom doesn't have to be maintained, just clamp edges */
      if (cur->xmin < tot->xmin) {
        cur->xmin = tot->xmin;
      }
      if (cur->xmax > tot->xmax) {
        cur->xmax = tot->xmax;
      }
    }
    else if (v2d->keeptot == V2D_KEEPTOT_STRICT) {
      /* This is an exception for the outliner (and later channel-lists, headers)
       * - must clamp within tot rect (absolutely no excuses)
       * --> therefore, cur->xmin must not be less than tot->xmin */
      if (cur->xmin < tot->xmin) {
        /* move cur across so that it sits at minimum of tot */
        temp = tot->xmin - cur->xmin;

        cur->xmin += temp;
        cur->xmax += temp;
      }
      else if (cur->xmax > tot->xmax) {
        /* - only offset by difference of cur-xmax and tot-xmax if that would not move
         *   cur-xmin to lie past tot-xmin
         * - otherwise, simply shift to tot-xmin??? */
        temp = cur->xmax - tot->xmax;

        if ((cur->xmin - temp) < tot->xmin) {
          /* only offset by difference from cur-min and tot-min */
          temp = cur->xmin - tot->xmin;

          cur->xmin -= temp;
          cur->xmax -= temp;
        }
        else {
          cur->xmin -= temp;
          cur->xmax -= temp;
        }
      }
    }
    else {
      /* This here occurs when:
       * - width too big, but maintaining zoom (i.e. widths cannot be changed)
       * - width is OK, but need to check if outside of boundaries
       *
       * So, resolution is to just shift view by the gap between the extremities.
       * We favor moving the 'minimum' across, as that's origin for most things.
       * (XXX: in the past, max was favored... if there are bugs, swap!)  */
      if ((cur->xmin < tot->xmin) && (cur->xmax > tot->xmax)) {
        /* outside boundaries on both sides,
         * so take middle-point of tot, and place in balanced way */
        temp = lib_rctf_cent_x(tot);
        diff = curwidth * 0.5f;

        cur->xmin = temp - diff;
        cur->xmax = temp + diff;
      }
      else if (cur->xmin < tot->xmin) {
        /* move cur across so that it sits at minimum of tot */
        temp = tot->xmin - cur->xmin;

        cur->xmin += temp;
        cur->xmax += temp;
      }
      else if (cur->xmax > tot->xmax) {
        /* - only offset by difference of cur-xmax and tot-xmax if that would not move
         *   cur-xmin to lie past tot-xmin
         * - otherwise, simply shift to tot-xmin??? */
        temp = cur->xmax - tot->xmax;

        if ((cur->xmin - temp) < tot->xmin) {
          /* only offset by difference from cur-min and tot-min */
          temp = cur->xmin - tot->xmin;

          cur->xmin -= temp;
          cur->xmax -= temp;
        }
        else {
          cur->xmin -= temp;
          cur->xmax -= temp;
        }
      }
    }

    /* height */
    if ((curheight > totheight) &&
        !(v2d->keepzoom & (V2D_KEEPZOOM | V2D_LOCKZOOM_Y | V2D_LIMITZOOM))) {
      /* if zoom doesn't have to be maintained, just clamp edges */
      if (cur->ymin < tot->ymin) {
        cur->ymin = tot->ymin;
      }
      if (cur->ymax > tot->ymax) {
        cur->ymax = tot->ymax;
      }
    }
    else {
      /* This here occurs when:
       * - height too big, but maintaining zoom (i.e. heights cannot be changed)
       * - height is OK, but need to check if outside of boundaries
       *
       * So, resolution is to just shift view by the gap between the extremities.
       * We favor moving the 'minimum' across, as that's origin for most things */
      if ((cur->ymin < tot->ymin) && (cur->ymax > tot->ymax)) {
        /* outside boundaries on both sides,
         * so take middle-point of tot, and place in balanced way */
        temp = BLI_rctf_cent_y(tot);
        diff = curheight * 0.5f;

        cur->ymin = temp - diff;
        cur->ymax = temp + diff;
      }
      else if (cur->ymin < tot->ymin) {
        /* there's still space remaining, so shift up */
        temp = tot->ymin - cur->ymin;

        cur->ymin += temp;
        cur->ymax += temp;
      }
      else if (cur->ymax > tot->ymax) {
        /* there's still space remaining, so shift down */
        temp = cur->ymax - tot->ymax;

        cur->ymin -= temp;
        cur->ymax -= temp;
      }
    }
  }

  /* Step 4: Make sure alignment restrictions are respected */
  if (v2d->align) {
    /* If alignment flags are set (but keeptot is not), they must still be respected, as although
     * they don't specify any particular bounds to stay within, they do define ranges which are
     * invalid.
     *
     * Here, we only check to make sure that on each axis, the 'cur' rect doesn't stray into these
     * invalid zones, otherwise we offset  */

    /* handle width - posx and negx flags are mutually exclusive, so watch out */
    if ((v2d->align & V2D_ALIGN_NO_POS_X) && !(v2d->align & V2D_ALIGN_NO_NEG_X)) {
      /* width is in negative-x half */
      if (v2d->cur.xmax > 0) {
        v2d->cur.xmin -= v2d->cur.xmax;
        v2d->cur.xmax = 0.0f;
      }
    }
    else if ((v2d->align & V2D_ALIGN_NO_NEG_X) && !(v2d->align & V2D_ALIGN_NO_POS_X)) {
      /* width is in positive-x half */
      if (v2d->cur.xmin < 0) {
        v2d->cur.xmax -= v2d->cur.xmin;
        v2d->cur.xmin = 0.0f;
      }
    }

    /* handle height - posx and negx flags are mutually exclusive, so watch out */
    if ((v2d->align & V2D_ALIGN_NO_POS_Y) && !(v2d->align & V2D_ALIGN_NO_NEG_Y)) {
      /* height is in negative-y half */
      if (v2d->cur.ymax > 0) {
        v2d->cur.ymin -= v2d->cur.ymax;
        v2d->cur.ymax = 0.0f;
      }
    }
    else if ((v2d->align & V2D_ALIGN_NO_NEG_Y) && !(v2d->align & V2D_ALIGN_NO_POS_Y)) {
      /* height is in positive-y half */
      if (v2d->cur.ymin < 0) {
        v2d->cur.ymax -= v2d->cur.ymin;
        v2d->cur.ymin = 0.0f;
      }
    }
  }

  /* set masks */
  view2d_masks(v2d, NULL);
}

void view2d_curRect_validate(View2D *v2d)
{
  view2d_curRect_validate_resize(v2d, false);
}

void view2d_curRect_changed(const Cxt *C, View2D *v2d)
{
  view2d_curRect_validate(v2d);

  ARegion *region = cxt_wm_region(C);

  if (region->type->on_view2d_changed != NULL) {
    region->type->on_view2d_changed(C, region);
  }
}

bool view2d_area_supports_sync(ScrArea *area)
{
  return ELEM(area->spacetype, SPACE_ACTION, SPACE_NLA, SPACE_SEQ, SPACE_CLIP, SPACE_GRAPH);
}

void view2d_sync(Screen *screen, ScrArea *area, View2D *v2dcur, int flag)
{
  /* don't continue if no view syncing to be done */
  if ((v2dcur->flag & (V2D_VIEWSYNC_SCREEN_TIME | V2D_VIEWSYNC_AREA_VERTICAL)) == 0) {
    return;
  }

  /* check if doing within area syncing (i.e. channels/vertical) */
  if ((v2dcur->flag & V2D_VIEWSYNC_AREA_VERTICAL) && (area)) {
    LIST_FOREACH (ARegion *, region, &area->regionbase) {
      /* don't operate on self */
      if (v2dcur != &region->v2d) {
        /* only if view has vertical locks enabled */
        if (region->v2d.flag & V2D_VIEWSYNC_AREA_VERTICAL) {
          if (flag == V2D_LOCK_COPY) {
            /* other views with locks on must copy active */
            region->v2d.cur.ymin = v2dcur->cur.ymin;
            region->v2d.cur.ymax = v2dcur->cur.ymax;
          }
          else { /* V2D_LOCK_SET */
                 /* active must copy others */
            v2dcur->cur.ymin = region->v2d.cur.ymin;
            v2dcur->cur.ymax = region->v2d.cur.ymax;
          }

          /* region possibly changed, so refresh */
          ed_region_tag_redraw_no_rebuild(region);
        }
      }
    }
  }

  /* check if doing whole screen syncing (i.e. time/horizontal) */
  if ((v2dcur->flag & V2D_VIEWSYNC_SCREEN_TIME) && (screen)) {
    LIST_FOREACH (ScrArea *, area_iter, &screen->areabase) {
      if (!UI_view2d_area_supports_sync(area_iter)) {
        continue;
      }
      LIST_FOREACH (ARegion *, region, &area_iter->regionbase) {
        /* don't operate on self */
        if (v2dcur != &region->v2d) {
          /* only if view has horizontal locks enabled */
          if (region->v2d.flag & V2D_VIEWSYNC_SCREEN_TIME) {
            if (flag == V2D_LOCK_COPY) {
              /* other views with locks on must copy active */
              region->v2d.cur.xmin = v2dcur->cur.xmin;
              region->v2d.cur.xmax = v2dcur->cur.xmax;
            }
            else { /* V2D_LOCK_SET */
                   /* active must copy others */
              v2dcur->cur.xmin = region->v2d.cur.xmin;
              v2dcur->cur.xmax = region->v2d.cur.xmax;
            }

            /* region possibly changed, so refresh */
            ed_region_tag_redraw_no_rebuild(region);
          }
        }
      }
    }
  }
}

void view2d_curRect_reset(View2D *v2d)
{
  float width, height;

  /* assume width and height of 'cur' rect by default, should be same size as mask */
  width = (float)(lib_rcti_size_x(&v2d->mask) + 1);
  height = (float)(lib_rcti_size_y(&v2d->mask) + 1);

  /* handle width - posx and negx flags are mutually exclusive, so watch out */
  if ((v2d->align & V2D_ALIGN_NO_POS_X) && !(v2d->align & V2D_ALIGN_NO_NEG_X)) {
    /* width is in negative-x half */
    v2d->cur.xmin = -width;
    v2d->cur.xmax = 0.0f;
  }
  else if ((v2d->align & V2D_ALIGN_NO_NEG_X) && !(v2d->align & V2D_ALIGN_NO_POS_X)) {
    /* width is in positive-x half */
    v2d->cur.xmin = 0.0f;
    v2d->cur.xmax = width;
  }
  else {
    /* width is centered around (x == 0) */
    const float dx = width / 2.0f;

    v2d->cur.xmin = -dx;
    v2d->cur.xmax = dx;
  }

  /* handle height - posx and negx flags are mutually exclusive, so watch out */
  if ((v2d->align & V2D_ALIGN_NO_POS_Y) && !(v2d->align & V2D_ALIGN_NO_NEG_Y)) {
    /* height is in negative-y half */
    v2d->cur.ymin = -height;
    v2d->cur.ymax = 0.0f;
  }
  else if ((v2d->align & V2D_ALIGN_NO_NEG_Y) && !(v2d->align & V2D_ALIGN_NO_POS_Y)) {
    /* height is in positive-y half */
    v2d->cur.ymin = 0.0f;
    v2d->cur.ymax = height;
  }
  else {
    /* height is centered around (y == 0) */
    const float dy = height / 2.0f;

    v2d->cur.ymin = -dy;
    v2d->cur.ymax = dy;
  }
}

/* ------------------ */

void view2d_totRect_set_resize(View2D *v2d, int width, int height, bool resize)
{
  /* don't do anything if either value is 0 */
  width = abs(width);
  height = abs(height);

  if (ELEM(0, width, height)) {
    if (G.debug & G_DEBUG) {
      /* XXX: temp debug info. */
      printf("Error: View2D totRect set exiting: v2d=%p width=%d height=%d\n",
             (void *)v2d,
             width,
             height);
    }
    return;
  }

  /* handle width - posx and negx flags are mutually exclusive, so watch out */
  if ((v2d->align & V2D_ALIGN_NO_POS_X) && !(v2d->align & V2D_ALIGN_NO_NEG_X)) {
    /* width is in negative-x half */
    v2d->tot.xmin = (float)-width;
    v2d->tot.xmax = 0.0f;
  }
  else if ((v2d->align & V2D_ALIGN_NO_NEG_X) && !(v2d->align & V2D_ALIGN_NO_POS_X)) {
    /* width is in positive-x half */
    v2d->tot.xmin = 0.0f;
    v2d->tot.xmax = (float)width;
  }
  else {
    /* width is centered around (x == 0) */
    const float dx = (float)width / 2.0f;

    v2d->tot.xmin = -dx;
    v2d->tot.xmax = dx;
  }

  /* handle height - posx and negx flags are mutually exclusive, so watch out */
  if ((v2d->align & V2D_ALIGN_NO_POS_Y) && !(v2d->align & V2D_ALIGN_NO_NEG_Y)) {
    /* height is in negative-y half */
    v2d->tot.ymin = (float)-height;
    v2d->tot.ymax = 0.0f;
  }
  else if ((v2d->align & V2D_ALIGN_NO_NEG_Y) && !(v2d->align & V2D_ALIGN_NO_POS_Y)) {
    /* height is in positive-y half */
    v2d->tot.ymin = 0.0f;
    v2d->tot.ymax = (float)height;
  }
  else {
    /* height is centered around (y == 0) */
    const float dy = (float)height / 2.0f;

    v2d->tot.ymin = -dy;
    v2d->tot.ymax = dy;
  }

  /* make sure that 'cur' rect is in a valid state as a result of these changes */
  view2d_curRect_validate_resize(v2d, resize);
}

void view2d_totRect_set(View2D *v2d, int width, int height)
{
  view2d_totRect_set_resize(v2d, width, height, false);
}

void view2d_zoom_cache_reset(void)
{
  /* TODO: This way we avoid threading conflict with sequencer rendering
   * text strip. But ideally we want to make glyph cache to be fully safe
   * for threading.
   */
  if (G.is_rendering) {
    return;
  }
  /* While scaling we can accumulate fonts at many sizes (~20 or so).
   * Not an issue with embedded font, but can use over 500Mb with i18n ones! See T38244. */

  /* NOTE: only some views draw text, we could check for this case to avoid cleaning cache. */
  BLF_cache_clear();
}

/* -------------------------------------------------------------------- */
/** View2D Matrix Setup **/
