#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_brush_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "BLI_bitmap.h"
#include "BLI_listbase.h"
#include "BLI_math_color_blend.h"
#include "BLI_stack.h"
#include "BLI_task.h"

#include "BKE_brush.h"
#include "BKE_colorband.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_paint.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"

#include "ED_paint.h"
#include "ED_screen.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "UI_view2d.h"

#include "paint_intern.h"

/* Brush Painting for 2D image editor */

/* Defines and Structs */

typedef struct BrushPainterCache {
  bool use_float;            /* need float imbuf? */
  bool use_color_correction; /* use color correction for float */
  bool invert;

  bool is_texbrush;
  bool is_maskbrush;

  int lastdiameter;
  float last_tex_rotation;
  float last_mask_rotation;
  float last_pressure;

  ImBuf *ibuf;
  ImBuf *texibuf;
  ushort *tex_mask;
  ushort *tex_mask_old;
  uint tex_mask_old_w;
  uint tex_mask_old_h;

  CurveMaskCache curve_mask_cache;

  int image_size[2];
} BrushPainterCache;

typedef struct BrushPainter {
  Scene *scene;
  Brush *brush;

  bool firsttouch; /* first paint op */

  struct ImagePool *pool; /* image pool */
  rctf tex_mapping;       /* texture coordinate mapping */
  rctf mask_mapping;      /* mask texture coordinate mapping */

  bool cache_invert;
} BrushPainter;

typedef struct ImagePaintRegion {
  int destx, desty;
  int srcx, srcy;
  int width, height;
} ImagePaintRegion;

typedef enum ImagePaintTileState {
  PAINT2D_TILE_UNINITIALIZED = 0,
  PAINT2D_TILE_MISSING,
  PAINT2D_TILE_READY,
} ImagePaintTileState;

typedef struct ImagePaintTile {
  ImageUser iuser;
  ImBuf *canvas;
  float radius_fac;
  int size[2];
  float uv_origin[2]; /* Stores the position of this tile in UV space. */
  bool need_redraw;
  BrushPainterCache cache;

  ImagePaintTileState state;

  float last_paintpos[2];  /* position of last paint op */
  float start_paintpos[2]; /* position of first paint */
} ImagePaintTile;

typedef struct ImagePaintState {
  BrushPainter *painter;
  SpaceImage *sima;
  View2D *v2d;
  Scene *scene;

  Brush *brush;
  short tool, blend;
  Image *image;
  ImBuf *clonecanvas;

  bool do_masking;

  int symmetry;

  ImagePaintTile *tiles;
  int num_tiles;

  BlurKernel *blurkernel;
} ImagePaintState;

static BrushPainter *brush_painter_2d_new(Scene *scene, Brush *brush, bool invert)
{
  BrushPainter *painter = MEM_callocN(sizeof(BrushPainter), "BrushPainter");

  painter->brush = brush;
  painter->scene = scene;
  painter->firsttouch = true;
  painter->cache_invert = invert;

  return painter;
}

static void brush_painter_2d_require_imbuf(
    Brush *brush, ImagePaintTile *tile, bool use_float, bool use_color_correction, bool invert)
{
  BrushPainterCache *cache = &tile->cache;

  if ((cache->use_float != use_float)) {
    if (cache->ibuf) {
      IMB_freeImBuf(cache->ibuf);
    }
    if (cache->tex_mask) {
      MEM_freeN(cache->tex_mask);
    }
    if (cache->tex_mask_old) {
      MEM_freeN(cache->tex_mask_old);
    }
    cache->ibuf = NULL;
    cache->tex_mask = NULL;
    cache->lastdiameter = -1; /* force ibuf create in refresh */
    cache->invert = invert;
  }

  cache->use_float = use_float;
  cache->use_color_correction = use_float && use_color_correction;
  cache->is_texbrush = (brush->mtex.tex && brush->imagepaint_tool == PAINT_TOOL_DRAW) ? true :
                                                                                        false;
  cache->is_maskbrush = (brush->mask_mtex.tex) ? true : false;
}

static void brush_painter_cache_2d_free(BrushPainterCache *cache)
{
  if (cache->ibuf) {
    IMB_freeImBuf(cache->ibuf);
  }
  if (cache->texibuf) {
    IMB_freeImBuf(cache->texibuf);
  }
  paint_curve_mask_cache_free_data(&cache->curve_mask_cache);
  if (cache->tex_mask) {
    MEM_freeN(cache->tex_mask);
  }
  if (cache->tex_mask_old) {
    MEM_freeN(cache->tex_mask_old);
  }
}

static void brush_imbuf_tex_co(rctf *mapping, int x, int y, float texco[3])
{
  texco[0] = mapping->xmin + x * mapping->xmax;
  texco[1] = mapping->ymin + y * mapping->ymax;
  texco[2] = 0.0f;
}

/* create a mask with the mask texture */
static ushort *brush_painter_mask_ibuf_new(BrushPainter *painter, const int size)
{
  Scene *scene = painter->scene;
  Brush *brush = painter->brush;
  rctf mask_mapping = painter->mask_mapping;
  struct ImagePool *pool = painter->pool;

  float texco[3];
  ushort *mask, *m;
  int x, y, thread = 0;

  mask = MEM_mallocN(sizeof(ushort) * size * size, "brush_painter_mask");
  m = mask;

  for (y = 0; y < size; y++) {
    for (x = 0; x < size; x++, m++) {
      float res;
      brush_imbuf_tex_co(&mask_mapping, x, y, texco);
      res = BKE_brush_sample_masktex(scene, brush, texco, thread, pool);
      *m = (ushort)(65535.0f * res);
    }
  }

  return mask;
}

/* update rectangular section of the brush image */
static void brush_painter_mask_imbuf_update(BrushPainter *painter,
                                            ImagePaintTile *tile,
                                            const ushort *tex_mask_old,
                                            int origx,
                                            int origy,
                                            int w,
                                            int h,
                                            int xt,
                                            int yt,
                                            const int diameter)
{
  Scene *scene = painter->scene;
  Brush *brush = painter->brush;
  BrushPainterCache *cache = &tile->cache;
  rctf tex_mapping = painter->mask_mapping;
  struct ImagePool *pool = painter->pool;
  ushort res;

  bool use_texture_old = (tex_mask_old != NULL);

  int x, y, thread = 0;

  ushort *tex_mask = cache->tex_mask;
  ushort *tex_mask_cur = cache->tex_mask_old;

  /* fill pixels */
  for (y = origy; y < h; y++) {
    for (x = origx; x < w; x++) {
      /* sample texture */
      float texco[3];

      /* handle byte pixel */
      ushort *b = tex_mask + (y * diameter + x);
      ushort *t = tex_mask_cur + (y * diameter + x);

      if (!use_texture_old) {
        brush_imbuf_tex_co(&tex_mapping, x, y, texco);
        res = (ushort)(65535.0f * BKE_brush_sample_masktex(scene, brush, texco, thread, pool));
      }

      /* read from old texture buffer */
      if (use_texture_old) {
        res = *(tex_mask_old + ((y - origy + yt) * cache->tex_mask_old_w + (x - origx + xt)));
      }

      /* write to new texture mask */
      *t = res;
      /* write to mask image buffer */
      *b = res;
    }
  }
}

/**
 * Update the brush mask image by trying to reuse the cached texture result.
 * This can be considerably faster for brushes that change size due to pressure or
 * textures that stick to the surface where only part of the pixels are new
 */
static void brush_painter_mask_imbuf_partial_update(BrushPainter *painter,
                                                    ImagePaintTile *tile,
                                                    const float pos[2],
                                                    const int diameter)
{
  BrushPainterCache *cache = &tile->cache;
  ushort *tex_mask_old;
  int destx, desty, srcx, srcy, w, h, x1, y1, x2, y2;

  /* create brush image buffer if it didn't exist yet */
  if (!cache->tex_mask) {
    cache->tex_mask = MEM_mallocN(sizeof(ushort) * diameter * diameter, "brush_painter_mask");
  }

  /* create new texture image buffer with coordinates relative to old */
  tex_mask_old = cache->tex_mask_old;
  cache->tex_mask_old = MEM_mallocN(sizeof(ushort) * diameter * diameter, "brush_painter_mask");

  if (tex_mask_old) {
    ImBuf maskibuf;
    ImBuf maskibuf_old;
    maskibuf.x = diameter;
    maskibuf.y = diameter;
    maskibuf_old.x = cache->tex_mask_old_w;
    maskibuf_old.y = cache->tex_mask_old_h;

    srcx = srcy = 0;
    w = cache->tex_mask_old_w;
    h = cache->tex_mask_old_h;
    destx = (int)floorf(tile->last_paintpos[0]) - (int)floorf(pos[0]) + (diameter / 2 - w / 2);
    desty = (int)floorf(tile->last_paintpos[1]) - (int)floorf(pos[1]) + (diameter / 2 - h / 2);

    /* hack, use temporary rects so that clipping works */
    IMB_rectclip(&maskibuf, &maskibuf_old, &destx, &desty, &srcx, &srcy, &w, &h);
  }
  else {
    srcx = srcy = 0;
    destx = desty = 0;
    w = h = 0;
  }

  x1 = min_ii(destx, diameter);
  y1 = min_ii(desty, diameter);
  x2 = min_ii(destx + w, diameter);
  y2 = min_ii(desty + h, diameter);

  /* blend existing texture in new position */
  if ((x1 < x2) && (y1 < y2)) {
    brush_painter_mask_imbuf_update(
        painter, tile, tex_mask_old, x1, y1, x2, y2, srcx, srcy, diameter);
  }

  if (tex_mask_old) {
    MEM_freeN(tex_mask_old);
  }

  /* sample texture in new areas */
  if ((0 < x1) && (0 < diameter)) {
    brush_painter_mask_imbuf_update(painter, tile, NULL, 0, 0, x1, diameter, 0, 0, diameter);
  }
  if ((x2 < diameter) && (0 < diameter)) {
    brush_painter_mask_imbuf_update(
        painter, tile, NULL, x2, 0, diameter, diameter, 0, 0, diameter);
  }
  if ((x1 < x2) && (0 < y1)) {
    brush_painter_mask_imbuf_update(painter, tile, NULL, x1, 0, x2, y1, 0, 0, diameter);
  }
  if ((x1 < x2) && (y2 < diameter)) {
    brush_painter_mask_imbuf_update(painter, tile, NULL, x1, y2, x2, diameter, 0, 0, diameter);
  }

  /* through with sampling, now update sizes */
  cache->tex_mask_old_w = diameter;
  cache->tex_mask_old_h = diameter;
}

/* create imbuf with brush color */
static ImBuf *brush_painter_imbuf_new(
    BrushPainter *painter, ImagePaintTile *tile, const int size, float pressure, float distance)
{
  Scene *scene = painter->scene;
  Brush *brush = painter->brush;
  BrushPainterCache *cache = &tile->cache;

  const char *display_device = scene->display_settings.display_device;
  struct ColorManagedDisplay *display = IMB_colormanagement_display_get_named(display_device);

  rctf tex_mapping = painter->tex_mapping;
  struct ImagePool *pool = painter->pool;

  bool use_color_correction = cache->use_color_correction;
  bool use_float = cache->use_float;
  bool is_texbrush = cache->is_texbrush;

  int x, y, thread = 0;
  float brush_rgb[3];

  /* allocate image buffer */
  ImBuf *ibuf = IMB_allocImBuf(size, size, 32, (use_float) ? IB_rectfloat : IB_rect);

  /* get brush color */
  if (brush->imagepaint_tool == PAINT_TOOL_DRAW) {
    paint_brush_color_get(
        scene, brush, use_color_correction, cache->invert, distance, pressure, brush_rgb, display);
  }
  else {
    brush_rgb[0] = 1.0f;
    brush_rgb[1] = 1.0f;
    brush_rgb[2] = 1.0f;
  }

  /* fill image buffer */
  for (y = 0; y < size; y++) {
    for (x = 0; x < size; x++) {
      /* sample texture and multiply with brush color */
      float texco[3], rgba[4];

      if (is_texbrush) {
        brush_imbuf_tex_co(&tex_mapping, x, y, texco);
        BKE_brush_sample_tex_3d(scene, brush, texco, rgba, thread, pool);
        /* TODO(sergey): Support texture paint color space. */
        if (!use_float) {
          IMB_colormanagement_scene_linear_to_display_v3(rgba, display);
        }
        mul_v3_v3(rgba, brush_rgb);
      }
      else {
        copy_v3_v3(rgba, brush_rgb);
        rgba[3] = 1.0f;
      }

      if (use_float) {
        /* write to float pixel */
        float *dstf = ibuf->rect_float + (y * size + x) * 4;
        mul_v3_v3fl(dstf, rgba, rgba[3]); /* premultiply */
        dstf[3] = rgba[3];
      }
      else {
        /* write to byte pixel */
        uchar *dst = (uchar *)ibuf->rect + (y * size + x) * 4;

        rgb_float_to_uchar(dst, rgba);
        dst[3] = unit_float_to_uchar_clamp(rgba[3]);
      }
    }
  }

  return ibuf;
}

/* update rectangular section of the brush image */
static void brush_painter_imbuf_update(BrushPainter *painter,
                                       ImagePaintTile *tile,
                                       ImBuf *oldtexibuf,
                                       int origx,
                                       int origy,
                                       int w,
                                       int h,
                                       int xt,
                                       int yt)
{
  Scene *scene = painter->scene;
  Brush *brush = painter->brush;
  BrushPainterCache *cache = &tile->cache;

  const char *display_device = scene->display_settings.display_device;
  struct ColorManagedDisplay *display = IMB_colormanagement_display_get_named(display_device);

  rctf tex_mapping = painter->tex_mapping;
  struct ImagePool *pool = painter->pool;

  bool use_color_correction = cache->use_color_correction;
  bool use_float = cache->use_float;
  bool is_texbrush = cache->is_texbrush;
  bool use_texture_old = (oldtexibuf != NULL);

  int x, y, thread = 0;
  float brush_rgb[3];

  ImBuf *ibuf = cache->ibuf;
  ImBuf *texibuf = cache->texibuf;

  /* get brush color */
  if (brush->imagepaint_tool == PAINT_TOOL_DRAW) {
    paint_brush_color_get(
        scene, brush, use_color_correction, cache->invert, 0.0f, 1.0f, brush_rgb, display);
  }
  else {
    brush_rgb[0] = 1.0f;
    brush_rgb[1] = 1.0f;
    brush_rgb[2] = 1.0f;
  }

  /* fill pixels */
  for (y = origy; y < h; y++) {
    for (x = origx; x < w; x++) {
      /* sample texture and multiply with brush color */
      float texco[3], rgba[4];

      if (!use_texture_old) {
        if (is_texbrush) {
          brush_imbuf_tex_co(&tex_mapping, x, y, texco);
          BKE_brush_sample_tex_3d(scene, brush, texco, rgba, thread, pool);
          /* TODO(sergey): Support texture paint color space. */
          if (!use_float) {
            IMB_colormanagement_scene_linear_to_display_v3(rgba, display);
          }
          mul_v3_v3(rgba, brush_rgb);
        }
        else {
          copy_v3_v3(rgba, brush_rgb);
          rgba[3] = 1.0f;
        }
      }

      if (use_float) {
        /* handle float pixel */
        float *bf = ibuf->rect_float + (y * ibuf->x + x) * 4;
        float *tf = texibuf->rect_float + (y * texibuf->x + x) * 4;

        /* read from old texture buffer */
        if (use_texture_old) {
          const float *otf = oldtexibuf->rect_float +
                             ((y - origy + yt) * oldtexibuf->x + (x - origx + xt)) * 4;
          copy_v4_v4(rgba, otf);
        }

        /* write to new texture buffer */
        copy_v4_v4(tf, rgba);

        /* output premultiplied float image, mf was already premultiplied */
        mul_v3_v3fl(bf, rgba, rgba[3]);
        bf[3] = rgba[3];
      }
      else {
        uchar crgba[4];

        /* handle byte pixel */
        uchar *b = (uchar *)ibuf->rect + (y * ibuf->x + x) * 4;
        uchar *t = (uchar *)texibuf->rect + (y * texibuf->x + x) * 4;

        /* read from old texture buffer */
        if (use_texture_old) {
          uchar *ot = (uchar *)oldtexibuf->rect +
                      ((y - origy + yt) * oldtexibuf->x + (x - origx + xt)) * 4;
          crgba[0] = ot[0];
          crgba[1] = ot[1];
          crgba[2] = ot[2];
          crgba[3] = ot[3];
        }
        else {
          rgba_float_to_uchar(crgba, rgba);
        }

        /* write to new texture buffer */
        t[0] = crgba[0];
        t[1] = crgba[1];
        t[2] = crgba[2];
        t[3] = crgba[3];

        /* write to brush image buffer */
        b[0] = crgba[0];
        b[1] = crgba[1];
        b[2] = crgba[2];
        b[3] = crgba[3];
      }
    }
  }
}

/* update the brush image by trying to reuse the cached texture result. this
 * can be considerably faster for brushes that change size due to pressure or
 * textures that stick to the surface where only part of the pixels are new */
static void brush_painter_imbuf_partial_update(BrushPainter *painter,
                                               ImagePaintTile *tile,
                                               const float pos[2],
                                               const int diameter)
{
  BrushPainterCache *cache = &tile->cache;
  ImBuf *oldtexibuf, *ibuf;
  int imbflag, destx, desty, srcx, srcy, w, h, x1, y1, x2, y2;

  /* create brush image buffer if it didn't exist yet */
  imbflag = (cache->use_float) ? IB_rectfloat : IB_rect;
  if (!cache->ibuf) {
    cache->ibuf = IMB_allocImBuf(diameter, diameter, 32, imbflag);
  }
  ibuf = cache->ibuf;

  /* create new texture image buffer with coordinates relative to old */
  oldtexibuf = cache->texibuf;
  cache->texibuf = IMB_allocImBuf(diameter, diameter, 32, imbflag);

  if (oldtexibuf) {
    srcx = srcy = 0;
    w = oldtexibuf->x;
    h = oldtexibuf->y;
    destx = (int)floorf(tile->last_paintpos[0]) - (int)floorf(pos[0]) + (diameter / 2 - w / 2);
    desty = (int)floorf(tile->last_paintpos[1]) - (int)floorf(pos[1]) + (diameter / 2 - h / 2);

    IMB_rectclip(cache->texibuf, oldtexibuf, &destx, &desty, &srcx, &srcy, &w, &h);
  }
  else {
    srcx = srcy = 0;
    destx = desty = 0;
    w = h = 0;
  }

  x1 = min_ii(destx, ibuf->x);
  y1 = min_ii(desty, ibuf->y);
  x2 = min_ii(destx + w, ibuf->x);
  y2 = min_ii(desty + h, ibuf->y);

  /* blend existing texture in new position */
  if ((x1 < x2) && (y1 < y2)) {
    brush_painter_imbuf_update(painter, tile, oldtexibuf, x1, y1, x2, y2, srcx, srcy);
  }

  if (oldtexibuf) {
    IMB_freeImBuf(oldtexibuf);
  }

  /* sample texture in new areas */
  if ((0 < x1) && (0 < ibuf->y)) {
    brush_painter_imbuf_update(painter, tile, NULL, 0, 0, x1, ibuf->y, 0, 0);
  }
  if ((x2 < ibuf->x) && (0 < ibuf->y)) {
    brush_painter_imbuf_update(painter, tile, NULL, x2, 0, ibuf->x, ibuf->y, 0, 0);
  }
  if ((x1 < x2) && (0 < y1)) {
    brush_painter_imbuf_update(painter, tile, NULL, x1, 0, x2, y1, 0, 0);
  }
  if ((x1 < x2) && (y2 < ibuf->y)) {
    brush_painter_imbuf_update(painter, tile, NULL, x1, y2, x2, ibuf->y, 0, 0);
  }
}

static void brush_painter_2d_tex_mapping(ImagePaintState *s,
                                         ImBuf *canvas,
                                         const int diameter,
                                         const float startpos[2],
                                         const float pos[2],
                                         const float mouse[2],
                                         int mapmode,
                                         rctf *mapping)
{
  float invw = 1.0f / (float)canvas->x;
  float invh = 1.0f / (float)canvas->y;
  int xmin, ymin, xmax, ymax;
  int ipos[2];

  /* find start coordinate of brush in canvas */
  ipos[0] = (int)floorf((pos[0] - diameter / 2) + 1.0f);
  ipos[1] = (int)floorf((pos[1] - diameter / 2) + 1.0f);

  if (mapmode == MTEX_MAP_MODE_STENCIL) {
    /* map from view coordinates of brush to region coordinates */
    UI_view2d_view_to_region(s->v2d, ipos[0] * invw, ipos[1] * invh, &xmin, &ymin);
    UI_view2d_view_to_region(
        s->v2d, (ipos[0] + diameter) * invw, (ipos[1] + diameter) * invh, &xmax, &ymax);

    /* output mapping from brush ibuf x/y to region coordinates */
    mapping->xmin = xmin;
    mapping->ymin = ymin;
    mapping->xmax = (xmax - xmin) / (float)diameter;
    mapping->ymax = (ymax - ymin) / (float)diameter;
  }
  else if (mapmode == MTEX_MAP_MODE_3D) {
    /* 3D mapping, just mapping to canvas 0..1. */
    mapping->xmin = 2.0f * (ipos[0] * invw - 0.5f);
    mapping->ymin = 2.0f * (ipos[1] * invh - 0.5f);
    mapping->xmax = 2.0f * invw;
    mapping->ymax = 2.0f * invh;
  }
  else if (ELEM(mapmode, MTEX_MAP_MODE_VIEW, MTEX_MAP_MODE_RANDOM)) {
    /* view mapping */
    mapping->xmin = mouse[0] - diameter * 0.5f + 0.5f;
    mapping->ymin = mouse[1] - diameter * 0.5f + 0.5f;
    mapping->xmax = 1.0f;
    mapping->ymax = 1.0f;
  }
  else /* if (mapmode == MTEX_MAP_MODE_TILED) */ {
    mapping->xmin = (int)(-diameter * 0.5) + (int)floorf(pos[0]) - (int)floorf(startpos[0]);
    mapping->ymin = (int)(-diameter * 0.5) + (int)floorf(pos[1]) - (int)floorf(startpos[1]);
    mapping->xmax = 1.0f;
    mapping->ymax = 1.0f;
  }
}
