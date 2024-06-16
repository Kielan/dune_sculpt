#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include "imbuf.h"
#include "imbuf_types.h"

#include "types_img.h"
#include "types_scene.h"
#include "types_texture.h"

#include "lib_dunelib.h"
#include "lib_math.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "dune_img.h"

#include "rndr_texture.h"

#include "rndr_types.h"
#include "texture_common.h"

static void boxsample(ImBuf *ibuf,
                      float minx,
                      float miny,
                      float maxx,
                      float maxy,
                      TexResult *texres,
                      const short imaprepeat,
                      const short imapextend);

/* IMGWRAPPING */
/* x and y have to be checked for image size */
static void ibuf_get_color(float col[4], struct ImBuf *ibuf, int x, int y)
{
  int ofs = y * ibuf->x + x;

  if (ibuf->rect_float) {
    if (ibuf->channels == 4) {
      const float *fp = ibuf->rect_float + 4 * ofs;
      copy_v4_v4(col, fp);
    }
    else if (ibuf->channels == 3) {
      const float *fp = ibuf->rect_float + 3 * ofs;
      copy_v3_v3(col, fp);
      col[3] = 1.0f;
    }
    else {
      const float *fp = ibuf->rect_float + ofs;
      col[0] = col[1] = col[2] = col[3] = *fp;
    }
  }
  else {
    const char *rect = (char *)(ibuf->rect + ofs);

    col[0] = ((float)rect[0]) * (1.0f / 255.0f);
    col[1] = ((float)rect[1]) * (1.0f / 255.0f);
    col[2] = ((float)rect[2]) * (1.0f / 255.0f);
    col[3] = ((float)rect[3]) * (1.0f / 255.0f);

    /* bytes are internally straight, however render pipeline seems to expect premul */
    col[0] *= col[3];
    col[1] *= col[3];
    col[2] *= col[3];
  }
}

int imgwrap(Tex *tex,
            Img *img,
            const float texvec[3],
            TexResult *texres,
            struct ImgPool *pool,
            const bool skip_load_img)
{
  float fx, fy, val1, val2, val3;
  int x, y, retval;
  int xi, yi; /* original values */

  texres->tin = texres->trgba[3] = texres->trgba[0] = texres->trgba[1] = texres->trgba[2] = 0.0f;

  /* we need to set retval OK, otherwise texture code generates normals itself... */
  retval = texres->nor ? (TEX_RGB | TEX_NOR) : TEX_RGB;

  /* quick tests */
  if (ima == NULL) {
    return retval;
  }

  /* hack for icon render */
  if (skip_load_image && !dune_img_has_loaded_ibuf(ima)) {
    return retval;
  }

  ImageUser *iuser = &tex->iuser;
  ImageUser local_iuser;
  if (ima->source == IMA_SRC_TILED) {
    /* tex->iuser might be shared by threads, so create a local copy. */
    local_iuser = tex->iuser;
    iuser = &local_iuser;

    float new_uv[2];
    iuser->tile = dune_image_get_tile_from_pos(ima, texvec, new_uv, NULL);
    fx = new_uv[0];
    fy = new_uv[1];
  }
  else {
    fx = texvec[0];
    fy = texvec[1];
  }

  ImBuf *ibuf = dune_img_pool_acquire_ibuf(ima, iuser, pool);

  ima->flag |= IMG_USED_FOR_RNDR;

  if (ibuf == NULL || (ibuf->rect == NULL && ibuf->rect_float == NULL)) {
    dune_img_pool_release_ibuf(img, ibuf, pool);
    return retval;
  }

  /* setup mapping */
  if (tex->imgflag & TEX_IMAROT) {
    SWAP(float, fx, fy);
  }

  if (tex->extend == TEX_CHECKER) {
    int xs, ys;

    xs = (int)floor(fx);
    ys = (int)floor(fy);
    fx -= xs;
    fy -= ys;

    if ((tex->flag & TEX_CHECKER_ODD) == 0) {
      if ((xs + ys) & 1) {
        /* pass */
      }
      else {
        if (img) {
          dune_img_pool_release_ibuf(ima, ibuf, pool);
        }
        return retval;
      }
    }
    if ((tex->flag & TEX_CHECKER_EVEN) == 0) {
      if ((xs + ys) & 1) {
        if (ima) {
          dune_img_pool_release_ibuf(ima, ibuf, pool);
        }
        return retval;
      }
    }
    /* scale around center, (0.5, 0.5) */
    if (tex->checkerdist < 1.0f) {
      fx = (fx - 0.5f) / (1.0f - tex->checkerdist) + 0.5f;
      fy = (fy - 0.5f) / (1.0f - tex->checkerdist) + 0.5f;
    }
  }

  x = xi = (int)floorf(fx * ibuf->x);
  y = yi = (int)floorf(fy * ibuf->y);

  if (tex->extend == TEX_CLIPCUBE) {
    if (x < 0 || y < 0 || x >= ibuf->x || y >= ibuf->y || texvec[2] < -1.0f || texvec[2] > 1.0f) {
      if (ima) {
        dune_img_pool_release_ibuf(img, ibuf, pool);
      }
      return retval;
    }
  }
  else if (ELEM(tex->extend, TEX_CLIP, TEX_CHECKER)) {
    if (x < 0 || y < 0 || x >= ibuf->x || y >= ibuf->y) {
      if (ima) {
        dune_img_pool_release_ibuf(img, ibuf, pool);
      }
      return retval;
    }
  }
  else {
    if (tex->extend == TEX_EXTEND) {
      if (x >= ibuf->x) {
        x = ibuf->x - 1;
      }
      else if (x < 0) {
        x = 0;
      }
    }
    else {
      x = x % ibuf->x;
      if (x < 0) {
        x += ibuf->x;
      }
    }
    if (tex->extend == TEX_EXTEND) {
      if (y >= ibuf->y) {
        y = ibuf->y - 1;
      }
      else if (y < 0) {
        y = 0;
      }
    }
    else {
      y = y % ibuf->y;
      if (y < 0) {
        y += ibuf->y;
      }
    }
  }

  /* Keep this before interpolation T29761. */
  if (ima) {
    if ((tex->imgflag & TEX_USEALPHA) && (ima->alpha_mode != IMA_ALPHA_IGNORE)) {
      if ((tex->imgflag & TEX_CALCALPHA) == 0) {
        texres->talpha = true;
      }
    }
  }

  /* interpolate */
  if (tex->imaflag & TEX_INTERPOL) {
    float filterx, filtery;
    filterx = (0.5f * tex->filtersize) / ibuf->x;
    filtery = (0.5f * tex->filtersize) / ibuf->y;

    /* Important that this val is wrapped T27782.
     * this applies the modifications made by the checks above,
     * back to the floating point vals */
    fx -= (float)(xi - x) / (float)ibuf->x;
    fy -= (float)(yi - y) / (float)ibuf->y;

    boxsample(ibuf,
              fx - filterx,
              fy - filtery,
              fx + filterx,
              fy + filtery,
              texres,
              (tex->extend == TEX_REPEAT),
              (tex->extend == TEX_EXTEND));
  }
  else { /* no filtering */
    ibuf_get_color(texres->trgba, ibuf, x, y);
  }

  if (texres->nor) {
    if (tex->imgflag & TEX_NORMALMAP) {
      /* Normal from color:
       * The invert of the red channel is to make
       * the normal map compliant with the outside world.
       * It needs to be done bc in Dube
       * the normal used in the renderer points inward. It is generated
       * this way in calc_vertexnormals(). Should this ever change
       * this negate must be removed. */
      texres->nor[0] = -2.0f * (texres->trgba[0] - 0.5f);
      texres->nor[1] = 2.0f * (texres->trgba[1] - 0.5f);
      texres->nor[2] = 2.0f * (texres->trgba[2] - 0.5f);
    }
    else {
      /* bump: take three samples */
      val1 = texres->trgba[0] + texres->trgba[1] + texres->trgba[2];

      if (x < ibuf->x - 1) {
        float col[4];
        ibuf_get_color(col, ibuf, x + 1, y);
        val2 = (col[0] + col[1] + col[2]);
      }
      else {
        val2 = val1;
      }

      if (y < ibuf->y - 1) {
        float col[4];
        ibuf_get_color(col, ibuf, x, y + 1);
        val3 = (col[0] + col[1] + col[2]);
      }
      else {
        val3 = val1;
      }

      /* do not mix up x and y here! */
      texres->nor[0] = (val1 - val2);
      texres->nor[1] = (val1 - val3);
    }
  }

  if (texres->talpha) {
    texres->tin = texres->trgba[3];
  }
  else if (tex->imgflag & TEX_CALCALPHA) {
    texres->trgba[3] = texres->tin = max_fff(texres->trgba[0], texres->trgba[1], texres->trgba[2]);
  }
  else {
    texres->trgba[3] = texres->tin = 1.0;
  }

  if (tex->flag & TEX_NEGALPHA) {
    texres->trgba[3] = 1.0f - texres->trgba[3];
  }

  /* de-premul, this is being pre-multiplied in shade_input_do_shade()
   * do not de-premul for generated alpha, it is alrdy in straight */
  if (texres->trgba[3] != 1.0f && texres->trgba[3] > 1e-4f && !(tex->imaflag & TEX_CALCALPHA)) {
    fx = 1.0f / texres->trgba[3];
    texres->trgba[0] *= fx;
    texres->trgba[1] *= fx;
    texres->trgba[2] *= fx;
  }

  if (ima) {
    dune_img_pool_release_ibuf(ima, ibuf, pool);
  }

  BRICONTRGB;

  return retval;
}

static void clipx_rctf_swap(rctf *stack, short *count, float x1, float x2)
{
  rctf *rf, *newrct;
  short a;

  a = *count;
  rf = stack;
  for (; a > 0; a--) {
    if (rf->xmin < x1) {
      if (rf->xmax < x1) {
        rf->xmin += (x2 - x1);
        rf->xmax += (x2 - x1);
      }
      else {
        if (rf->xmax > x2) {
          rf->xmax = x2;
        }
        newrct = stack + *count;
        (*count)++;

        newrct->xmax = x2;
        newrct->xmin = rf->xmin + (x2 - x1);
        newrct->ymin = rf->ymin;
        newrct->ymax = rf->ymax;

        if (newrct->xmin == newrct->xmax) {
          (*count)--;
        }

        rf->xmin = x1;
      }
    }
    else if (rf->xmax > x2) {
      if (rf->xmin > x2) {
        rf->xmin -= (x2 - x1);
        rf->xmax -= (x2 - x1);
      }
      else {
        if (rf->xmin < x1) {
          rf->xmin = x1;
        }
        newrct = stack + *count;
        (*count)++;

        newrct->xmin = x1;
        newrct->xmax = rf->xmax - (x2 - x1);
        newrct->ymin = rf->ymin;
        newrct->ymax = rf->ymax;

        if (newrct->xmin == newrct->xmax) {
          (*count)--;
        }

        rf->xmax = x2;
      }
    }
    rf++;
  }
}

static void clipy_rctf_swap(rctf *stack, short *count, float y1, float y2)
{
  rctf *rf, *newrct;
  short a;

  a = *count;
  rf = stack;
  for (; a > 0; a--) {
    if (rf->ymin < y1) {
      if (rf->ymax < y1) {
        rf->ymin += (y2 - y1);
        rf->ymax += (y2 - y1);
      }
      else {
        if (rf->ymax > y2) {
          rf->ymax = y2;
        }
        newrct = stack + *count;
        (*count)++;

        newrct->ymax = y2;
        newrct->ymin = rf->ymin + (y2 - y1);
        newrct->xmin = rf->xmin;
        newrct->xmax = rf->xmax;

        if (newrct->ymin == newrct->ymax) {
          (*count)--;
        }

        rf->ymin = y1;
      }
    }
    else if (rf->ymax > y2) {
      if (rf->ymin > y2) {
        rf->ymin -= (y2 - y1);
        rf->ymax -= (y2 - y1);
      }
      else {
        if (rf->ymin < y1) {
          rf->ymin = y1;
        }
        newrct = stack + *count;
        (*count)++;

        newrct->ymin = y1;
        newrct->ymax = rf->ymax - (y2 - y1);
        newrct->xmin = rf->xmin;
        newrct->xmax = rf->xmax;

        if (newrct->ymin == newrct->ymax) {
          (*count)--;
        }

        rf->ymax = y2;
      }
    }
    rf++;
  }
}

static float square_rctf(rctf *rf)
{
  float x, y;

  x = lib_rctf_size_x(rf);
  y = lib_rctf_size_y(rf);
  return x * y;
}

static float clipx_rctf(rctf *rf, float x1, float x2)
{
  float size;

  size = lib_rctf_size_x(rf);

  if (rf->xmin < x1) {
    rf->xmin = x1;
  }
  if (rf->xmax > x2) {
    rf->xmax = x2;
  }
  if (rf->xmin > rf->xmax) {
    rf->xmin = rf->xmax;
    return 0.0;
  }
  if (size != 0.0f) {
    return lib_rctf_size_x(rf) / size;
  }
  return 1.0;
}

static float clipy_rctf(rctf *rf, float y1, float y2)
{
  float size;

  size = lib_rctf_size_y(rf);

  if (rf->ymin < y1) {
    rf->ymin = y1;
  }
  if (rf->ymax > y2) {
    rf->ymax = y2;
  }

  if (rf->ymin > rf->ymax) {
    rf->ymin = rf->ymax;
    return 0.0;
  }
  if (size != 0.0f) {
    return lib_rctf_size_y(rf) / size;
  }
  return 1.0;
}

static void boxsampleclip(struct ImBuf *ibuf, rctf *rf, TexResult *texres)
{
  /* Sample box, is clipped alrdy, and minx etc. have been set at ibuf size.
   * Enlarge with anti-aliased edges of the pixels. */

  float muly, mulx, div, col[4];
  int x, y, startx, endx, starty, endy;

  startx = (int)floor(rf->xmin);
  endx = (int)floor(rf->xmax);
  starty = (int)floor(rf->ymin);
  endy = (int)floor(rf->ymax);

  if (startx < 0) {
    startx = 0;
  }
  if (starty < 0) {
    starty = 0;
  }
  if (endx >= ibuf->x) {
    endx = ibuf->x - 1;
  }
  if (endy >= ibuf->y) {
    endy = ibuf->y - 1;
  }

  if (starty == endy && startx == endx) {
    ibuf_get_color(texres->trgba, ibuf, startx, starty);
  }
  else {
    div = texres->trgba[0] = texres->trgba[1] = texres->trgba[2] = texres->trgba[3] = 0.0;
    for (y = starty; y <= endy; y++) {

      muly = 1.0;

      if (starty == endy) {
        /* pass */
      }
      else {
        if (y == starty) {
          muly = 1.0f - (rf->ymin - y);
        }
        if (y == endy) {
          muly = (rf->ymax - y);
        }
      }

      if (startx == endx) {
        mulx = muly;

        ibuf_get_color(col, ibuf, startx, y);
        madd_v4_v4fl(texres->trgba, col, mulx);
        div += mulx;
      }
      else {
        for (x = startx; x <= endx; x++) {
          mulx = muly;
          if (x == startx) {
            mulx *= 1.0f - (rf->xmin - x);
          }
          if (x == endx) {
            mulx *= (rf->xmax - x);
          }

          ibuf_get_color(col, ibuf, x, y);
          /* TODO: No need to do manual optimization. Branching is slower than multiplying
           * with 1. */
          if (mulx == 1.0f) {
            add_v4_v4(texres->trgba, col);
            div += 1.0f;
          }
          else {
            madd_v4_v4fl(texres->trgba, col, mulx);
            div += mulx;
          }
        }
      }
    }

    if (div != 0.0f) {
      div = 1.0f / div;
      mul_v4_fl(texres->trgba, div);
    }
    else {
      zero_v4(texres->trgba);
    }
  }
}

static void boxsample(ImBuf *ibuf,
                      float minx,
                      float miny,
                      float maxx,
                      float maxy,
                      TexResult *texres,
                      const short imgprepeat,
                      const short imgpextend)
{
  /* Sample box, performs clip. minx etc are in range 0.0 - 1.0 .
   * Enlarge with anti-aliased edges of pixels.
   * If variable 'imaprepeat' has been set, the
   * clipped-away parts are sampled as well. */
  /* Actually minx etc isn't in the proper range...
   *       this due to filter size and offset vectors for bump. */
  /* - talpha must be initialized. */
  /* - even when 'imgprepeat' is set, this can only repeat once in any direction.
   * the point which min/max is derived from is assumed to be wrapped. */
  TexResult texr;
  rctf *rf, stack[8];
  float opp, tot, alphaclip = 1.0;
  short count = 1;

  rf = stack;
  rf->xmin = minx * (ibuf->x);
  rf->xmax = maxx * (ibuf->x);
  rf->ymin = miny * (ibuf->y);
  rf->ymax = maxy * (ibuf->y);

  texr.talpha = texres->talpha; /* is read by boxsample_clip */

  if (imgpextend) {
    CLAMP(rf->xmin, 0.0f, ibuf->x - 1);
    CLAMP(rf->xmax, 0.0f, ibuf->x - 1);
  }
  else if (imgprepeat) {
    clipx_rctf_swap(stack, &count, 0.0, (float)(ibuf->x));
  }
  else {
    alphaclip = clipx_rctf(rf, 0.0, (float)(ibuf->x));

    if (alphaclip <= 0.0f) {
      texres->trgba[0] = texres->trgba[2] = texres->trgba[1] = texres->trgba[3] = 0.0;
      return;
    }
  }

  if (imgpextend) {
    CLAMP(rf->ymin, 0.0f, ibuf->y - 1);
    CLAMP(rf->ymax, 0.0f, ibuf->y - 1);
  }
  else if (imgprepeat) {
    clipy_rctf_swap(stack, &count, 0.0, (float)(ibuf->y));
  }
  else {
    alphaclip *= clipy_rctf(rf, 0.0, (float)(ibuf->y));

    if (alphaclip <= 0.0f) {
      texres->trgba[0] = texres->trgba[2] = texres->trgba[1] = texres->trgba[3] = 0.0;
      return;
    }
  }

  if (count > 1) {
    tot = texres->trgba[0] = texres->trgba[2] = texres->trgba[1] = texres->trgba[3] = 0.0;
    while (count--) {
      boxsampleclip(ibuf, rf, &texr);

      opp = square_rctf(rf);
      tot += opp;

      texres->trgba[0] += opp * texr.trgba[0];
      texres->trgba[1] += opp * texr.trgba[1];
      texres->trgba[2] += opp * texr.trgba[2];
      if (texres->talpha) {
        texres->trgba[3] += opp * texr.trgba[3];
      }
      rf++;
    }
    if (tot != 0.0f) {
      texres->trgba[0] /= tot;
      texres->trgba[1] /= tot;
      texres->trgba[2] /= tot;
      if (texres->talpha) {
        texres->trgba[3] /= tot;
      }
    }
  }
  else {
    boxsampleclip(ibuf, rf, texres);
  }

  if (texres->talpha == 0) {
    texres->trgba[3] = 1.0;
  }

  if (alphaclip != 1.0f) {
    /* premul it all */
    texres->trgba[0] *= alphaclip;
    texres->trgba[1] *= alphaclip;
    texres->trgba[2] *= alphaclip;
    texres->trgba[3] *= alphaclip;
  }
}

/* from here, some fns only used for the new filtering */

/* anisotropic filters, data struct used instead of long line of (possibly unused) func args */
typedef struct afdata_t {
  float dxt[2], dyt[2];
  int intpol, extflag;
  /* feline only */
  float majrad, minrad, theta;
  int iProbes;
  float dusc, dvsc;
} afdata_t;

/* this only used here to make it easier to pass extend flags as single int */
enum { TXC_XMIR = 1, TXC_YMIR, TXC_REPT, TXC_EXTD };

/* Similar to `ibuf_get_color()` but clips/wraps coords according to repeat/extend flags
 * returns true if out of range in clip-mode. */
static int ibuf_get_color_clip(float col[4], ImBuf *ibuf, int x, int y, int extflag)
{
  int clip = 0;
  switch (extflag) {
    case TXC_XMIR: /* y rep */
      x %= 2 * ibuf->x;
      x += x < 0 ? 2 * ibuf->x : 0;
      x = x >= ibuf->x ? 2 * ibuf->x - x - 1 : x;
      y %= ibuf->y;
      y += y < 0 ? ibuf->y : 0;
      break;
    case TXC_YMIR: /* x rep */
      x %= ibuf->x;
      x += x < 0 ? ibuf->x : 0;
      y %= 2 * ibuf->y;
      y += y < 0 ? 2 * ibuf->y : 0;
      y = y >= ibuf->y ? 2 * ibuf->y - y - 1 : y;
      break;
    case TXC_EXTD:
      x = (x < 0) ? 0 : ((x >= ibuf->x) ? (ibuf->x - 1) : x);
      y = (y < 0) ? 0 : ((y >= ibuf->y) ? (ibuf->y - 1) : y);
      break;
    case TXC_REPT:
      x %= ibuf->x;
      x += (x < 0) ? ibuf->x : 0;
      y %= ibuf->y;
      y += (y < 0) ? ibuf->y : 0;
      break;
    default: { /* as extend, if clipped, set alpha to 0.0 */
      if (x < 0) {
        x = 0;
      } /* TXF alpha: clip = 1; } */
      if (x >= ibuf->x) {
        x = ibuf->x - 1;
      } /* TXF alpha:  clip = 1; } */
      if (y < 0) {
        y = 0;
      } /* TXF alpha:  clip = 1; } */
      if (y >= ibuf->y) {
        y = ibuf->y - 1;
      } /* TXF alpha:  clip = 1; } */
    }
  }

  if (ibuf->rect_float) {
    const float *fp = ibuf->rect_float + (x + y * ibuf->x) * ibuf->channels;
    if (ibuf->channels == 1) {
      col[0] = col[1] = col[2] = col[3] = *fp;
    }
    else {
      col[0] = fp[0];
      col[1] = fp[1];
      col[2] = fp[2];
      col[3] = clip ? 0.0f : (ibuf->channels == 4 ? fp[3] : 1.0f);
    }
  }
  else {
    const char *rect = (char *)(ibuf->rect + x + y * ibuf->x);
    float inv_alpha_fac = (1.0f / 255.0f) * rect[3] * (1.0f / 255.0f);
    col[0] = rect[0] * inv_alpha_fac;
    col[1] = rect[1] * inv_alpha_fac;
    col[2] = rect[2] * inv_alpha_fac;
    col[3] = clip ? 0.0f : rect[3] * (1.0f / 255.0f);
  }
  return clip;
}

/* as above + bilerp */
static int ibuf_get_color_clip_bilerp(
    float col[4], ImBuf *ibuf, float u, float v, int intpol, int extflag)
{
  if (intpol) {
    float c00[4], c01[4], c10[4], c11[4];
    const float ufl = floorf(u -= 0.5f), vfl = floorf(v -= 0.5f);
    const float uf = u - ufl, vf = v - vfl;
    const float w00 = (1.0f - uf) * (1.0f - vf), w10 = uf * (1.0f - vf), w01 = (1.0f - uf) * vf,
                w11 = uf * vf;
    const int x1 = (int)ufl, y1 = (int)vfl, x2 = x1 + 1, y2 = y1 + 1;
    int clip = ibuf_get_color_clip(c00, ibuf, x1, y1, extflag);
    clip |= ibuf_get_color_clip(c10, ibuf, x2, y1, extflag);
    clip |= ibuf_get_color_clip(c01, ibuf, x1, y2, extflag);
    clip |= ibuf_get_color_clip(c11, ibuf, x2, y2, extflag);
    col[0] = w00 * c00[0] + w10 * c10[0] + w01 * c01[0] + w11 * c11[0];
    col[1] = w00 * c00[1] + w10 * c10[1] + w01 * c01[1] + w11 * c11[1];
    col[2] = w00 * c00[2] + w10 * c10[2] + w01 * c01[2] + w11 * c11[2];
    col[3] = clip ? 0.0f : w00 * c00[3] + w10 * c10[3] + w01 * c01[3] + w11 * c11[3];
    return clip;
  }
  return ibuf_get_color_clip(col, ibuf, (int)u, (int)v, extflag);
}

static void area_sample(TexResult *texr, ImBuf *ibuf, float fx, float fy, afdata_t *AFD)
{
  int xs, ys, clip = 0;
  float tc[4], xsd, ysd, cw = 0.0f;
  const float ux = ibuf->x * AFD->dxt[0], uy = ibuf->y * AFD->dxt[1];
  const float vx = ibuf->x * AFD->dyt[0], vy = ibuf->y * AFD->dyt[1];
  int xsam = (int)(0.5f * sqrtf(ux * ux + uy * uy) + 0.5f);
  int ysam = (int)(0.5f * sqrtf(vx * vx + vy * vy) + 0.5f);
  const int minsam = AFD->intpol ? 2 : 4;
  xsam = CLAMPIS(xsam, minsam, ibuf->x * 2);
  ysam = CLAMPIS(ysam, minsam, ibuf->y * 2);
  xsd = 1.0f / xsam;
  ysd = 1.0f / ysam;
  texr->trgba[0] = texr->trgba[1] = texr->trgba[2] = texr->trgba[3] = 0.0f;
  for (ys = 0; ys < ysam; ys++) {
    for (xs = 0; xs < xsam; xs++) {
      const float su = (xs + ((ys & 1) + 0.5f) * 0.5f) * xsd - 0.5f;
      const float sv = (ys + ((xs & 1) + 0.5f) * 0.5f) * ysd - 0.5f;
      const float pu = fx + su * AFD->dxt[0] + sv * AFD->dyt[0];
      const float pv = fy + su * AFD->dxt[1] + sv * AFD->dyt[1];
      const int out = ibuf_get_color_clip_bilerp(
          tc, ibuf, pu * ibuf->x, pv * ibuf->y, AFD->intpol, AFD->extflag);
      clip |= out;
      cw += out ? 0.0f : 1.0f;
      texr->trgba[0] += tc[0];
      texr->trgba[1] += tc[1];
      texr->trgba[2] += tc[2];
      texr->trgba[3] += texr->talpha ? tc[3] : 0.0f;
    }
  }
  xsd *= ysd;
  texr->trgba[0] *= xsd;
  texr->trgba[1] *= xsd;
  texr->trgba[2] *= xsd;
  /* clipping can be ignored if alpha used, texr->trgba[3] alrdy includes filtered edge */
  texr->trgba[3] = texr->talpha ? texr->trgba[3] * xsd : (clip ? cw * xsd : 1.0f);
}
