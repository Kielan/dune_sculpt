#include "IMB_colormanagement.h"
#include "IMB_colormanagement_intern.h"

#include <math.h>
#include <string.h>

#include "DNA_color_types.h"
#include "DNA_image_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "IMB_filetype.h"
#include "IMB_filter.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"
#include "IMB_moviecache.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_math_color.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_task.h"
#include "BLI_threads.h"

#include "BKE_appdir.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_main.h"

#include "RNA_define.h"

#include "SEQ_iterator.h"

#include <ocio_capi.h>

/* -------------------------------------------------------------------- */
/** \name Global declarations
 * \{ */

#define DISPLAY_BUFFER_CHANNELS 4

/* ** list of all supported color spaces, displays and views */
static char global_role_data[MAX_COLORSPACE_NAME];
static char global_role_scene_linear[MAX_COLORSPACE_NAME];
static char global_role_color_picking[MAX_COLORSPACE_NAME];
static char global_role_texture_painting[MAX_COLORSPACE_NAME];
static char global_role_default_byte[MAX_COLORSPACE_NAME];
static char global_role_default_float[MAX_COLORSPACE_NAME];
static char global_role_default_sequencer[MAX_COLORSPACE_NAME];

static ListBase global_colorspaces = {nullptr, nullptr};
static ListBase global_displays = {nullptr, nullptr};
static ListBase global_views = {nullptr, nullptr};
static ListBase global_looks = {nullptr, nullptr};

static int global_tot_colorspace = 0;
static int global_tot_display = 0;
static int global_tot_view = 0;
static int global_tot_looks = 0;

/* Luma coefficients and XYZ to RGB to be initialized by OCIO. */

float imbuf_luma_coefficients[3] = {0.0f};
float imbuf_scene_linear_to_xyz[3][3] = {{0.0f}};
float imbuf_xyz_to_scene_linear[3][3] = {{0.0f}};
float imbuf_scene_linear_to_rec709[3][3] = {{0.0f}};
float imbuf_rec709_to_scene_linear[3][3] = {{0.0f}};
float imbuf_scene_linear_to_aces[3][3] = {{0.0f}};
float imbuf_aces_to_scene_linear[3][3] = {{0.0f}};

/* lock used by pre-cached processors getters, so processor wouldn't
 * be created several times
 * LOCK_COLORMANAGE can not be used since this mutex could be needed to
 * be locked before pre-cached processor are creating
 */
static pthread_mutex_t processor_lock = BLI_MUTEX_INITIALIZER;

typedef struct ColormanageProcessor {
  OCIO_ConstCPUProcessorRcPtr *cpu_processor;
  CurveMapping *curve_mapping;
  bool is_data_result;
} ColormanageProcessor;

static struct global_gpu_state {
  /* GPU shader currently bound. */
  bool gpu_shader_bound;

  /* Curve mapping. */
  CurveMapping *curve_mapping, *orig_curve_mapping;
  bool use_curve_mapping;
  int curve_mapping_timestamp;
  OCIO_CurveMappingSettings curve_mapping_settings;
} global_gpu_state = {false};

static struct global_color_picking_state {
  /* Cached processor for color picking conversion. */
  OCIO_ConstCPUProcessorRcPtr *cpu_processor_to;
  OCIO_ConstCPUProcessorRcPtr *cpu_processor_from;
  bool failed;
} global_color_picking_state = {nullptr};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Color Managed Cache
 * \{ */

/**
 * Cache Implementation Notes
 * ==========================
 *
 * All color management cache stuff is stored in two properties of
 * image buffers:
 *
 *   1. display_buffer_flags
 *
 *      This is a bit field which used to mark calculated transformations
 *      for particular image buffer. Index inside of this array means index
 *      of a color managed display. Element with given index matches view
 *      transformations applied for a given display. So if bit B of array
 *      element B is set to 1, this means display buffer with display index
 *      of A and view transform of B was ever calculated for this imbuf.
 *
 *      In contrast with indices in global lists of displays and views this
 *      indices are 0-based, not 1-based. This is needed to save some bytes
 *      of memory.
 *
 *   2. colormanage_cache
 *
 *      This is a pointer to a structure which holds all data which is
 *      needed for color management cache to work.
 *
 *      It contains two parts:
 *        - data
 *        - moviecache
 *
 *      Data field is used to store additional information about cached
 *      buffers which affects on whether cached buffer could be used.
 *      This data can't go to cache key because changes in this data
 *      shouldn't lead extra buffers adding to cache, it shall
 *      invalidate cached images.
 *
 *      Currently such a data contains only exposure and gamma, but
 *      would likely extended further.
 *
 *      data field is not null only for elements of cache, not used for
 *      original image buffers.
 *
 *      Color management cache is using generic MovieCache implementation
 *      to make it easier to deal with memory limitation.
 *
 *      Currently color management is using the same memory limitation
 *      pool as sequencer and clip editor are using which means color
 *      managed buffers would be removed from the cache as soon as new
 *      frames are loading for the movie clip and there's no space in
 *      cache.
 *
 *      Every image buffer has got own movie cache instance, which
 *      means keys for color managed buffers could be really simple
 *      and look up in this cache would be fast and independent from
 *      overall amount of color managed images.
 */

/* NOTE: ColormanageCacheViewSettings and ColormanageCacheDisplaySettings are
 *       quite the same as ColorManagedViewSettings and ColorManageDisplaySettings
 *       but they holds indexes of all transformations and color spaces, not
 *       their names.
 *
 *       This helps avoid extra colorspace / display / view lookup without
 *       requiring to pass all variables which affects on display buffer
 *       to color management cache system and keeps calls small and nice.
 */
typedef struct ColormanageCacheViewSettings {
  int flag;
  int look;
  int view;
  float exposure;
  float gamma;
  float dither;
  CurveMapping *curve_mapping;
} ColormanageCacheViewSettings;

typedef struct ColormanageCacheDisplaySettings {
  int display;
} ColormanageCacheDisplaySettings;

typedef struct ColormanageCacheKey {
  int view;    /* view transformation used for display buffer */
  int display; /* display device name */
} ColormanageCacheKey;

typedef struct ColormanageCacheData {
  int flag;                    /* view flags of cached buffer */
  int look;                    /* Additional artistic transform. */
  float exposure;              /* exposure value cached buffer is calculated with */
  float gamma;                 /* gamma value cached buffer is calculated with */
  float dither;                /* dither value cached buffer is calculated with */
  CurveMapping *curve_mapping; /* curve mapping used for cached buffer */
  int curve_mapping_timestamp; /* time stamp of curve mapping used for cached buffer */
} ColormanageCacheData;

typedef struct ColormanageCache {
  struct MovieCache *moviecache;

  ColormanageCacheData *data;
} ColormanageCache;

static struct MovieCache *colormanage_moviecache_get(const ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    return nullptr;
  }

  return ibuf->colormanage_cache->moviecache;
}

static ColormanageCacheData *colormanage_cachedata_get(const ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    return nullptr;
  }

  return ibuf->colormanage_cache->data;
}

static uint colormanage_hashhash(const void *key_v)
{
  const ColormanageCacheKey *key = static_cast<const ColormanageCacheKey *>(key_v);

  uint rval = (key->display << 16) | (key->view % 0xffff);

  return rval;
}

static bool colormanage_hashcmp(const void *av, const void *bv)
{
  const ColormanageCacheKey *a = static_cast<const ColormanageCacheKey *>(av);
  const ColormanageCacheKey *b = static_cast<const ColormanageCacheKey *>(bv);

  return ((a->view != b->view) || (a->display != b->display));
}

static struct MovieCache *colormanage_moviecache_ensure(ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    ibuf->colormanage_cache = MEM_cnew<ColormanageCache>("imbuf colormanage cache");
  }

  if (!ibuf->colormanage_cache->moviecache) {
    struct MovieCache *moviecache;

    moviecache = IMB_moviecache_create("colormanage cache",
                                       sizeof(ColormanageCacheKey),
                                       colormanage_hashhash,
                                       colormanage_hashcmp);

    ibuf->colormanage_cache->moviecache = moviecache;
  }

  return ibuf->colormanage_cache->moviecache;
}

static void colormanage_cachedata_set(ImBuf *ibuf, ColormanageCacheData *data)
{
  if (!ibuf->colormanage_cache) {
    ibuf->colormanage_cache = MEM_cnew<ColormanageCache>("imbuf colormanage cache");
  }

  ibuf->colormanage_cache->data = data;
}

static void colormanage_view_settings_to_cache(ImBuf *ibuf,
                                               ColormanageCacheViewSettings *cache_view_settings,
                                               const ColorManagedViewSettings *view_settings)
{
  int look = IMB_colormanagement_look_get_named_index(view_settings->look);
  int view = IMB_colormanagement_view_get_named_index(view_settings->view_transform);

  cache_view_settings->look = look;
  cache_view_settings->view = view;
  cache_view_settings->exposure = view_settings->exposure;
  cache_view_settings->gamma = view_settings->gamma;
  cache_view_settings->dither = ibuf->dither;
  cache_view_settings->flag = view_settings->flag;
  cache_view_settings->curve_mapping = view_settings->curve_mapping;
}

static void colormanage_display_settings_to_cache(
    ColormanageCacheDisplaySettings *cache_display_settings,
    const ColorManagedDisplaySettings *display_settings)
{
  int display = IMB_colormanagement_display_get_named_index(display_settings->display_device);

  cache_display_settings->display = display;
}

static void colormanage_settings_to_key(ColormanageCacheKey *key,
                                        const ColormanageCacheViewSettings *view_settings,
                                        const ColormanageCacheDisplaySettings *display_settings)
{
  key->view = view_settings->view;
  key->display = display_settings->display;
}

static ImBuf *colormanage_cache_get_ibuf(ImBuf *ibuf,
                                         ColormanageCacheKey *key,
                                         void **cache_handle)
{
  ImBuf *cache_ibuf;
  struct MovieCache *moviecache = colormanage_moviecache_get(ibuf);

  if (!moviecache) {
    /* If there's no moviecache it means no color management was applied
     * on given image buffer before. */
    return nullptr;
  }

  *cache_handle = nullptr;

  cache_ibuf = IMB_moviecache_get(moviecache, key, nullptr);

  *cache_handle = cache_ibuf;

  return cache_ibuf;
}

static uchar *colormanage_cache_get(ImBuf *ibuf,
                                    const ColormanageCacheViewSettings *view_settings,
                                    const ColormanageCacheDisplaySettings *display_settings,
                                    void **cache_handle)
{
  ColormanageCacheKey key;
  ImBuf *cache_ibuf;
  int view_flag = 1 << (view_settings->view - 1);
  CurveMapping *curve_mapping = view_settings->curve_mapping;
  int curve_mapping_timestamp = curve_mapping ? curve_mapping->changed_timestamp : 0;

  colormanage_settings_to_key(&key, view_settings, display_settings);

  /* check whether image was marked as dirty for requested transform */
  if ((ibuf->display_buffer_flags[display_settings->display - 1] & view_flag) == 0) {
    return nullptr;
  }

  cache_ibuf = colormanage_cache_get_ibuf(ibuf, &key, cache_handle);

  if (cache_ibuf) {
    ColormanageCacheData *cache_data;

    BLI_assert(cache_ibuf->x == ibuf->x && cache_ibuf->y == ibuf->y);

    /* only buffers with different color space conversions are being stored
     * in cache separately. buffer which were used only different exposure/gamma
     * are re-suing the same cached buffer
     *
     * check here which exposure/gamma/curve was used for cached buffer and if they're
     * different from requested buffer should be re-generated
     */
    cache_data = colormanage_cachedata_get(cache_ibuf);

    if (cache_data->look != view_settings->look ||
        cache_data->exposure != view_settings->exposure ||
        cache_data->gamma != view_settings->gamma || cache_data->dither != view_settings->dither ||
        cache_data->flag != view_settings->flag || cache_data->curve_mapping != curve_mapping ||
        cache_data->curve_mapping_timestamp != curve_mapping_timestamp)
    {
      *cache_handle = nullptr;

      IMB_freeImBuf(cache_ibuf);

      return nullptr;
    }

    return (uchar *)cache_ibuf->rect;
  }

  return nullptr;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2012 Blender Foundation */

/** \file
 * \ingroup imbuf
 */

#include "IMB_colormanagement.h"
#include "IMB_colormanagement_intern.h"

#include <math.h>
#include <string.h>

#include "DNA_color_types.h"
#include "DNA_image_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "IMB_filetype.h"
#include "IMB_filter.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"
#include "IMB_moviecache.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_math_color.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_task.h"
#include "BLI_threads.h"

#include "BKE_appdir.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_main.h"

#include "RNA_define.h"

#include "SEQ_iterator.h"

#include <ocio_capi.h>

/* -------------------------------------------------------------------- */
/** \name Global declarations
 * \{ */

#define DISPLAY_BUFFER_CHANNELS 4

/* ** list of all supported color spaces, displays and views */
static char global_role_data[MAX_COLORSPACE_NAME];
static char global_role_scene_linear[MAX_COLORSPACE_NAME];
static char global_role_color_picking[MAX_COLORSPACE_NAME];
static char global_role_texture_painting[MAX_COLORSPACE_NAME];
static char global_role_default_byte[MAX_COLORSPACE_NAME];
static char global_role_default_float[MAX_COLORSPACE_NAME];
static char global_role_default_sequencer[MAX_COLORSPACE_NAME];

static ListBase global_colorspaces = {nullptr, nullptr};
static ListBase global_displays = {nullptr, nullptr};
static ListBase global_views = {nullptr, nullptr};
static ListBase global_looks = {nullptr, nullptr};

static int global_tot_colorspace = 0;
static int global_tot_display = 0;
static int global_tot_view = 0;
static int global_tot_looks = 0;

/* Luma coefficients and XYZ to RGB to be initialized by OCIO. */

float imbuf_luma_coefficients[3] = {0.0f};
float imbuf_scene_linear_to_xyz[3][3] = {{0.0f}};
float imbuf_xyz_to_scene_linear[3][3] = {{0.0f}};
float imbuf_scene_linear_to_rec709[3][3] = {{0.0f}};
float imbuf_rec709_to_scene_linear[3][3] = {{0.0f}};
float imbuf_scene_linear_to_aces[3][3] = {{0.0f}};
float imbuf_aces_to_scene_linear[3][3] = {{0.0f}};

/* lock used by pre-cached processors getters, so processor wouldn't
 * be created several times
 * LOCK_COLORMANAGE can not be used since this mutex could be needed to
 * be locked before pre-cached processor are creating
 */
static pthread_mutex_t processor_lock = BLI_MUTEX_INITIALIZER;

typedef struct ColormanageProcessor {
  OCIO_ConstCPUProcessorRcPtr *cpu_processor;
  CurveMapping *curve_mapping;
  bool is_data_result;
} ColormanageProcessor;

static struct global_gpu_state {
  /* GPU shader currently bound. */
  bool gpu_shader_bound;

  /* Curve mapping. */
  CurveMapping *curve_mapping, *orig_curve_mapping;
  bool use_curve_mapping;
  int curve_mapping_timestamp;
  OCIO_CurveMappingSettings curve_mapping_settings;
} global_gpu_state = {false};

static struct global_color_picking_state {
  /* Cached processor for color picking conversion. */
  OCIO_ConstCPUProcessorRcPtr *cpu_processor_to;
  OCIO_ConstCPUProcessorRcPtr *cpu_processor_from;
  bool failed;
} global_color_picking_state = {nullptr};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Color Managed Cache
 * \{ */

/**
 * Cache Implementation Notes
 * ==========================
 *
 * All color management cache stuff is stored in two properties of
 * image buffers:
 *
 *   1. display_buffer_flags
 *
 *      This is a bit field which used to mark calculated transformations
 *      for particular image buffer. Index inside of this array means index
 *      of a color managed display. Element with given index matches view
 *      transformations applied for a given display. So if bit B of array
 *      element B is set to 1, this means display buffer with display index
 *      of A and view transform of B was ever calculated for this imbuf.
 *
 *      In contrast with indices in global lists of displays and views this
 *      indices are 0-based, not 1-based. This is needed to save some bytes
 *      of memory.
 *
 *   2. colormanage_cache
 *
 *      This is a pointer to a structure which holds all data which is
 *      needed for color management cache to work.
 *
 *      It contains two parts:
 *        - data
 *        - moviecache
 *
 *      Data field is used to store additional information about cached
 *      buffers which affects on whether cached buffer could be used.
 *      This data can't go to cache key because changes in this data
 *      shouldn't lead extra buffers adding to cache, it shall
 *      invalidate cached images.
 *
 *      Currently such a data contains only exposure and gamma, but
 *      would likely extended further.
 *
 *      data field is not null only for elements of cache, not used for
 *      original image buffers.
 *
 *      Color management cache is using generic MovieCache implementation
 *      to make it easier to deal with memory limitation.
 *
 *      Currently color management is using the same memory limitation
 *      pool as sequencer and clip editor are using which means color
 *      managed buffers would be removed from the cache as soon as new
 *      frames are loading for the movie clip and there's no space in
 *      cache.
 *
 *      Every image buffer has got own movie cache instance, which
 *      means keys for color managed buffers could be really simple
 *      and look up in this cache would be fast and independent from
 *      overall amount of color managed images.
 */

/* NOTE: ColormanageCacheViewSettings and ColormanageCacheDisplaySettings are
 *       quite the same as ColorManagedViewSettings and ColorManageDisplaySettings
 *       but they holds indexes of all transformations and color spaces, not
 *       their names.
 *
 *       This helps avoid extra colorspace / display / view lookup without
 *       requiring to pass all variables which affects on display buffer
 *       to color management cache system and keeps calls small and nice.
 */
typedef struct ColormanageCacheViewSettings {
  int flag;
  int look;
  int view;
  float exposure;
  float gamma;
  float dither;
  CurveMapping *curve_mapping;
} ColormanageCacheViewSettings;

typedef struct ColormanageCacheDisplaySettings {
  int display;
} ColormanageCacheDisplaySettings;

typedef struct ColormanageCacheKey {
  int view;    /* view transformation used for display buffer */
  int display; /* display device name */
} ColormanageCacheKey;

typedef struct ColormanageCacheData {
  int flag;                    /* view flags of cached buffer */
  int look;                    /* Additional artistic transform. */
  float exposure;              /* exposure value cached buffer is calculated with */
  float gamma;                 /* gamma value cached buffer is calculated with */
  float dither;                /* dither value cached buffer is calculated with */
  CurveMapping *curve_mapping; /* curve mapping used for cached buffer */
  int curve_mapping_timestamp; /* time stamp of curve mapping used for cached buffer */
} ColormanageCacheData;

typedef struct ColormanageCache {
  struct MovieCache *moviecache;

  ColormanageCacheData *data;
} ColormanageCache;

static struct MovieCache *colormanage_moviecache_get(const ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    return nullptr;
  }

  return ibuf->colormanage_cache->moviecache;
}

static ColormanageCacheData *colormanage_cachedata_get(const ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    return nullptr;
  }

  return ibuf->colormanage_cache->data;
}

static uint colormanage_hashhash(const void *key_v)
{
  const ColormanageCacheKey *key = static_cast<const ColormanageCacheKey *>(key_v);

  uint rval = (key->display << 16) | (key->view % 0xffff);

  return rval;
}

static bool colormanage_hashcmp(const void *av, const void *bv)
{
  const ColormanageCacheKey *a = static_cast<const ColormanageCacheKey *>(av);
  const ColormanageCacheKey *b = static_cast<const ColormanageCacheKey *>(bv);

  return ((a->view != b->view) || (a->display != b->display));
}

static struct MovieCache *colormanage_moviecache_ensure(ImBuf *ibuf)
{
  if (!ibuf->colormanage_cache) {
    ibuf->colormanage_cache = MEM_cnew<ColormanageCache>("imbuf colormanage cache");
  }

  if (!ibuf->colormanage_cache->moviecache) {
    struct MovieCache *moviecache;

    moviecache = IMB_moviecache_create("colormanage cache",
                                       sizeof(ColormanageCacheKey),
                                       colormanage_hashhash,
                                       colormanage_hashcmp);

    ibuf->colormanage_cache->moviecache = moviecache;
  }

  return ibuf->colormanage_cache->moviecache;
}

static void colormanage_cachedata_set(ImBuf *ibuf, ColormanageCacheData *data)
{
  if (!ibuf->colormanage_cache) {
    ibuf->colormanage_cache = MEM_cnew<ColormanageCache>("imbuf colormanage cache");
  }

  ibuf->colormanage_cache->data = data;
}

static void colormanage_view_settings_to_cache(ImBuf *ibuf,
                                               ColormanageCacheViewSettings *cache_view_settings,
                                               const ColorManagedViewSettings *view_settings)
{
  int look = IMB_colormanagement_look_get_named_index(view_settings->look);
  int view = IMB_colormanagement_view_get_named_index(view_settings->view_transform);

  cache_view_settings->look = look;
  cache_view_settings->view = view;
  cache_view_settings->exposure = view_settings->exposure;
  cache_view_settings->gamma = view_settings->gamma;
  cache_view_settings->dither = ibuf->dither;
  cache_view_settings->flag = view_settings->flag;
  cache_view_settings->curve_mapping = view_settings->curve_mapping;
}

static void colormanage_display_settings_to_cache(
    ColormanageCacheDisplaySettings *cache_display_settings,
    const ColorManagedDisplaySettings *display_settings)
{
  int display = IMB_colormanagement_display_get_named_index(display_settings->display_device);

  cache_display_settings->display = display;
}

static void colormanage_settings_to_key(ColormanageCacheKey *key,
                                        const ColormanageCacheViewSettings *view_settings,
                                        const ColormanageCacheDisplaySettings *display_settings)
{
  key->view = view_settings->view;
  key->display = display_settings->display;
}

static ImBuf *colormanage_cache_get_ibuf(ImBuf *ibuf,
                                         ColormanageCacheKey *key,
                                         void **cache_handle)
{
  ImBuf *cache_ibuf;
  struct MovieCache *moviecache = colormanage_moviecache_get(ibuf);

  if (!moviecache) {
    /* If there's no moviecache it means no color management was applied
     * on given image buffer before. */
    return nullptr;
  }

  *cache_handle = nullptr;

  cache_ibuf = IMB_moviecache_get(moviecache, key, nullptr);

  *cache_handle = cache_ibuf;

  return cache_ibuf;
}

static uchar *colormanage_cache_get(ImBuf *ibuf,
                                    const ColormanageCacheViewSettings *view_settings,
                                    const ColormanageCacheDisplaySettings *display_settings,
                                    void **cache_handle)
{
  ColormanageCacheKey key;
  ImBuf *cache_ibuf;
  int view_flag = 1 << (view_settings->view - 1);
  CurveMapping *curve_mapping = view_settings->curve_mapping;
  int curve_mapping_timestamp = curve_mapping ? curve_mapping->changed_timestamp : 0;

  colormanage_settings_to_key(&key, view_settings, display_settings);

  /* check whether image was marked as dirty for requested transform */
  if ((ibuf->display_buffer_flags[display_settings->display - 1] & view_flag) == 0) {
    return nullptr;
  }

  cache_ibuf = colormanage_cache_get_ibuf(ibuf, &key, cache_handle);

  if (cache_ibuf) {
    ColormanageCacheData *cache_data;

    BLI_assert(cache_ibuf->x == ibuf->x && cache_ibuf->y == ibuf->y);

    /* only buffers with different color space conversions are being stored
     * in cache separately. buffer which were used only different exposure/gamma
     * are re-suing the same cached buffer
     *
     * check here which exposure/gamma/curve was used for cached buffer and if they're
     * different from requested buffer should be re-generated
     */
    cache_data = colormanage_cachedata_get(cache_ibuf);

    if (cache_data->look != view_settings->look ||
        cache_data->exposure != view_settings->exposure ||
        cache_data->gamma != view_settings->gamma || cache_data->dither != view_settings->dither ||
        cache_data->flag != view_settings->flag || cache_data->curve_mapping != curve_mapping ||
        cache_data->curve_mapping_timestamp != curve_mapping_timestamp)
    {
      *cache_handle = nullptr;

      IMB_freeImBuf(cache_ibuf);

      return nullptr;
    }

    return (uchar *)cache_ibuf->rect;
  }

  return nullptr;
}

static void colormanage_cache_put(ImBuf *ibuf,
                                  const ColormanageCacheViewSettings *view_settings,
                                  const ColormanageCacheDisplaySettings *display_settings,
                                  uchar *display_buffer,
                                  void **cache_handle)
{
  ColormanageCacheKey key;
  ImBuf *cache_ibuf;
  ColormanageCacheData *cache_data;
  int view_flag = 1 << (view_settings->view - 1);
  struct MovieCache *moviecache = colormanage_moviecache_ensure(ibuf);
  CurveMapping *curve_mapping = view_settings->curve_mapping;
  int curve_mapping_timestamp = curve_mapping ? curve_mapping->changed_timestamp : 0;

  colormanage_settings_to_key(&key, view_settings, display_settings);

  /* mark display buffer as valid */
  ibuf->display_buffer_flags[display_settings->display - 1] |= view_flag;

  /* buffer itself */
  cache_ibuf = IMB_allocImBuf(ibuf->x, ibuf->y, ibuf->planes, 0);
  cache_ibuf->rect = (uint *)display_buffer;

  cache_ibuf->mall |= IB_rect;
  cache_ibuf->flags |= IB_rect;

  /* Store data which is needed to check whether cached buffer
   * could be used for color managed display settings. */
  cache_data = MEM_cnew<ColormanageCacheData>("color manage cache imbuf data");
  cache_data->look = view_settings->look;
  cache_data->exposure = view_settings->exposure;
  cache_data->gamma = view_settings->gamma;
  cache_data->dither = view_settings->dither;
  cache_data->flag = view_settings->flag;
  cache_data->curve_mapping = curve_mapping;
  cache_data->curve_mapping_timestamp = curve_mapping_timestamp;

  colormanage_cachedata_set(cache_ibuf, cache_data);

  *cache_handle = cache_ibuf;

  IMB_moviecache_put(moviecache, &key, cache_ibuf);
}

static void colormanage_cache_handle_release(void *cache_handle)
{
  ImBuf *cache_ibuf = static_cast<ImBuf *>(cache_handle);

  IMB_freeImBuf(cache_ibuf);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Initialization / De-initialization
 * \{ */

static void colormanage_role_color_space_name_get(OCIO_ConstConfigRcPtr *config,
                                                  char *colorspace_name,
                                                  const char *role,
                                                  const char *backup_role)
{
  OCIO_ConstColorSpaceRcPtr *ociocs;

  ociocs = OCIO_configGetColorSpace(config, role);

  if (!ociocs && backup_role) {
    ociocs = OCIO_configGetColorSpace(config, backup_role);
  }

  if (ociocs) {
    const char *name = OCIO_colorSpaceGetName(ociocs);

    /* assume function was called with buffer properly allocated to MAX_COLORSPACE_NAME chars */
    BLI_strncpy(colorspace_name, name, MAX_COLORSPACE_NAME);
    OCIO_colorSpaceRelease(ociocs);
  }
  else {
    printf("Color management: Error could not find role %s role.\n", role);
  }
}

static void colormanage_load_config(OCIO_ConstConfigRcPtr *config)
{
  int tot_colorspace, tot_display, tot_display_view, tot_looks;
  int index, viewindex, viewindex2;
  const char *name;

  /* get roles */
  colormanage_role_color_space_name_get(config, global_role_data, OCIO_ROLE_DATA, nullptr);
  colormanage_role_color_space_name_get(
      config, global_role_scene_linear, OCIO_ROLE_SCENE_LINEAR, nullptr);
  colormanage_role_color_space_name_get(
      config, global_role_color_picking, OCIO_ROLE_COLOR_PICKING, nullptr);
  colormanage_role_color_space_name_get(
      config, global_role_texture_painting, OCIO_ROLE_TEXTURE_PAINT, nullptr);
  colormanage_role_color_space_name_get(
      config, global_role_default_sequencer, OCIO_ROLE_DEFAULT_SEQUENCER, OCIO_ROLE_SCENE_LINEAR);
  colormanage_role_color_space_name_get(
      config, global_role_default_byte, OCIO_ROLE_DEFAULT_BYTE, OCIO_ROLE_TEXTURE_PAINT);
  colormanage_role_color_space_name_get(
      config, global_role_default_float, OCIO_ROLE_DEFAULT_FLOAT, OCIO_ROLE_SCENE_LINEAR);

  /* load colorspaces */
  tot_colorspace = OCIO_configGetNumColorSpaces(config);
  for (index = 0; index < tot_colorspace; index++) {
    OCIO_ConstColorSpaceRcPtr *ocio_colorspace;
    const char *description;
    bool is_invertible, is_data;

    name = OCIO_configGetColorSpaceNameByIndex(config, index);

    ocio_colorspace = OCIO_configGetColorSpace(config, name);
    description = OCIO_colorSpaceGetDescription(ocio_colorspace);
    is_invertible = OCIO_colorSpaceIsInvertible(ocio_colorspace);
    is_data = OCIO_colorSpaceIsData(ocio_colorspace);

    ColorSpace *colorspace = colormanage_colorspace_add(name, description, is_invertible, is_data);

    colorspace->num_aliases = OCIO_colorSpaceGetNumAliases(ocio_colorspace);
    if (colorspace->num_aliases > 0) {
      colorspace->aliases = static_cast<char(*)[MAX_COLORSPACE_NAME]>(MEM_callocN(
          sizeof(*colorspace->aliases) * colorspace->num_aliases, "ColorSpace aliases"));
      for (int i = 0; i < colorspace->num_aliases; i++) {
        BLI_strncpy(colorspace->aliases[i],
                    OCIO_colorSpaceGetAlias(ocio_colorspace, i),
                    MAX_COLORSPACE_NAME);
      }
    }

    OCIO_colorSpaceRelease(ocio_colorspace);
  }

  /* load displays */
  viewindex2 = 0;
  tot_display = OCIO_configGetNumDisplays(config);

  for (index = 0; index < tot_display; index++) {
    const char *displayname;
    ColorManagedDisplay *display;

    displayname = OCIO_configGetDisplay(config, index);

    display = colormanage_display_add(displayname);

    /* load views */
    tot_display_view = OCIO_configGetNumViews(config, displayname);
    for (viewindex = 0; viewindex < tot_display_view; viewindex++, viewindex2++) {
      const char *viewname;
      ColorManagedView *view;
      LinkData *display_view;

      viewname = OCIO_configGetView(config, displayname, viewindex);

      /* first check if view transform with given name was already loaded */
      view = colormanage_view_get_named(viewname);

      if (!view) {
        view = colormanage_view_add(viewname);
      }

      display_view = BLI_genericNodeN(view);

      BLI_addtail(&display->views, display_view);
    }
  }

  global_tot_display = tot_display;

  /* load looks */
  tot_looks = OCIO_configGetNumLooks(config);
  colormanage_look_add("None", "", true);
  for (index = 0; index < tot_looks; index++) {
    OCIO_ConstLookRcPtr *ocio_look;
    const char *process_space;

    name = OCIO_configGetLookNameByIndex(config, index);
    ocio_look = OCIO_configGetLook(config, name);
    process_space = OCIO_lookGetProcessSpace(ocio_look);
    OCIO_lookRelease(ocio_look);

    colormanage_look_add(name, process_space, false);
  }

  /* Load luminance coefficients. */
  OCIO_configGetDefaultLumaCoefs(config, imbuf_luma_coefficients);

  /* Load standard color spaces. */
  OCIO_configGetXYZtoSceneLinear(config, imbuf_xyz_to_scene_linear);
  invert_m3_m3(imbuf_scene_linear_to_xyz, imbuf_xyz_to_scene_linear);

  mul_m3_m3m3(imbuf_scene_linear_to_rec709, OCIO_XYZ_TO_REC709, imbuf_scene_linear_to_xyz);
  invert_m3_m3(imbuf_rec709_to_scene_linear, imbuf_scene_linear_to_rec709);

  mul_m3_m3m3(imbuf_aces_to_scene_linear, imbuf_xyz_to_scene_linear, OCIO_ACES_TO_XYZ);
  invert_m3_m3(imbuf_scene_linear_to_aces, imbuf_aces_to_scene_linear);
}

static void colormanage_free_config(void)
{
  ColorSpace *colorspace;
  ColorManagedDisplay *display;

  /* free color spaces */
  colorspace = static_cast<ColorSpace *>(global_colorspaces.first);
  while (colorspace) {
    ColorSpace *colorspace_next = colorspace->next;

    /* Free precomputed processors. */
    if (colorspace->to_scene_linear) {
      OCIO_cpuProcessorRelease((OCIO_ConstCPUProcessorRcPtr *)colorspace->to_scene_linear);
    }
    if (colorspace->from_scene_linear) {
      OCIO_cpuProcessorRelease((OCIO_ConstCPUProcessorRcPtr *)colorspace->from_scene_linear);
    }

    /* free color space itself */
    MEM_SAFE_FREE(colorspace->aliases);
    MEM_freeN(colorspace);

    colorspace = colorspace_next;
  }
  BLI_listbase_clear(&global_colorspaces);
  global_tot_colorspace = 0;

  /* free displays */
  display = static_cast<ColorManagedDisplay *>(global_displays.first);
  while (display) {
    ColorManagedDisplay *display_next = display->next;

    /* free precomputer processors */
    if (display->to_scene_linear) {
      OCIO_cpuProcessorRelease((OCIO_ConstCPUProcessorRcPtr *)display->to_scene_linear);
    }
    if (display->from_scene_linear) {
      OCIO_cpuProcessorRelease((OCIO_ConstCPUProcessorRcPtr *)display->from_scene_linear);
    }

    /* free list of views */
    BLI_freelistN(&display->views);

    MEM_freeN(display);
    display = display_next;
  }
  BLI_listbase_clear(&global_displays);
  global_tot_display = 0;

  /* free views */
  BLI_freelistN(&global_views);
  global_tot_view = 0;

  /* free looks */
  BLI_freelistN(&global_looks);
  global_tot_looks = 0;

  OCIO_exit();
}

void colormanagement_init(void)
{
  const char *ocio_env;
  const char *configdir;
  char configfile[FILE_MAX];
  OCIO_ConstConfigRcPtr *config = nullptr;

  OCIO_init();

  ocio_env = BLI_getenv("OCIO");

  if (ocio_env && ocio_env[0] != '\0') {
    config = OCIO_configCreateFromEnv();
    if (config != nullptr) {
      printf("Color management: Using %s as a configuration file\n", ocio_env);
    }
  }

  if (config == nullptr) {
    configdir = BKE_appdir_folder_id(BLENDER_DATAFILES, "colormanagement");

    if (configdir) {
      BLI_path_join(configfile, sizeof(configfile), configdir, BCM_CONFIG_FILE);

      config = OCIO_configCreateFromFile(configfile);
    }
  }

  if (config == nullptr) {
    printf("Color management: using fallback mode for management\n");

    config = OCIO_configCreateFallback();
  }

  if (config) {
    OCIO_setCurrentConfig(config);

    colormanage_load_config(config);

    OCIO_configRelease(config);
  }

  /* If there are no valid display/views, use fallback mode. */
  if (global_tot_display == 0 || global_tot_view == 0) {
    printf("Color management: no displays/views in the config, using fallback mode instead\n");

    /* Free old config. */
    colormanage_free_config();

    /* Initialize fallback config. */
    config = OCIO_configCreateFallback();
    colormanage_load_config(config);
  }

  BLI_init_srgb_conversion();
}
void colormanagement_exit(void)
{
  OCIO_gpuCacheFree();

  if (global_gpu_state.curve_mapping) {
    BKE_curvemapping_free(global_gpu_state.curve_mapping);
  }

  if (global_gpu_state.curve_mapping_settings.lut) {
    MEM_freeN(global_gpu_state.curve_mapping_settings.lut);
  }

  if (global_color_picking_state.cpu_processor_to) {
    OCIO_cpuProcessorRelease(global_color_picking_state.cpu_processor_to);
  }

  if (global_color_picking_state.cpu_processor_from) {
    OCIO_cpuProcessorRelease(global_color_picking_state.cpu_processor_from);
  }

  memset(&global_gpu_state, 0, sizeof(global_gpu_state));
  memset(&global_color_picking_state, 0, sizeof(global_color_picking_state));

  colormanage_free_config();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal functions
 * \{ */

static bool colormanage_compatible_look(ColorManagedLook *look, const char *view_name)
{
  if (look->is_noop) {
    return true;
  }

  /* Skip looks only relevant to specific view transforms. */
  return (look->view[0] == 0 || (view_name && STREQ(look->view, view_name)));
}

static bool colormanage_use_look(const char *look, const char *view_name)
{
  ColorManagedLook *look_descr = colormanage_look_get_named(look);
  return (look_descr->is_noop == false && colormanage_compatible_look(look_descr, view_name));
}

void colormanage_cache_free(ImBuf *ibuf)
{
  MEM_SAFE_FREE(ibuf->display_buffer_flags);

  if (ibuf->colormanage_cache) {
    ColormanageCacheData *cache_data = colormanage_cachedata_get(ibuf);
    struct MovieCache *moviecache = colormanage_moviecache_get(ibuf);

    if (cache_data) {
      MEM_freeN(cache_data);
    }

    if (moviecache) {
      IMB_moviecache_free(moviecache);
    }

    MEM_freeN(ibuf->colormanage_cache);

    ibuf->colormanage_cache = nullptr;
  }
}

void IMB_colormanagement_display_settings_from_ctx(
    const bContext *C,
    ColorManagedViewSettings **r_view_settings,
    ColorManagedDisplaySettings **r_display_settings)
{
  Scene *scene = CTX_data_scene(C);
  SpaceImage *sima = CTX_wm_space_image(C);

  *r_view_settings = &scene->view_settings;
  *r_display_settings = &scene->display_settings;

  if (sima && sima->image) {
    if ((sima->image->flag & IMA_VIEW_AS_RENDER) == 0) {
      *r_view_settings = nullptr;
    }
  }
}

static const char *get_display_colorspace_name(const ColorManagedViewSettings *view_settings,
                                               const ColorManagedDisplaySettings *display_settings)
{
  OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();

  const char *display = display_settings->display_device;
  const char *view = view_settings->view_transform;
  const char *colorspace_name;

  colorspace_name = OCIO_configGetDisplayColorSpaceName(config, display, view);

  OCIO_configRelease(config);

  return colorspace_name;
}

static ColorSpace *display_transform_get_colorspace(
    const ColorManagedViewSettings *view_settings,
    const ColorManagedDisplaySettings *display_settings)
{
  const char *colorspace_name = get_display_colorspace_name(view_settings, display_settings);

  if (colorspace_name) {
    return colormanage_colorspace_get_named(colorspace_name);
  }

  return nullptr;
}

static OCIO_ConstCPUProcessorRcPtr *create_display_buffer_processor(const char *look,
                                                                    const char *view_transform,
                                                                    const char *display,
                                                                    float exposure,
                                                                    float gamma,
                                                                    const char *from_colorspace)
{
  OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
  const bool use_look = colormanage_use_look(look, view_transform);
  const float scale = (exposure == 0.0f) ? 1.0f : powf(2.0f, exposure);
  const float exponent = (gamma == 1.0f) ? 1.0f : 1.0f / max_ff(FLT_EPSILON, gamma);

  OCIO_ConstProcessorRcPtr *processor = OCIO_createDisplayProcessor(config,
                                                                    from_colorspace,
                                                                    view_transform,
                                                                    display,
                                                                    (use_look) ? look : "",
                                                                    scale,
                                                                    exponent,
                                                                    false);

  OCIO_configRelease(config);

  if (processor == nullptr) {
    return nullptr;
  }

  OCIO_ConstCPUProcessorRcPtr *cpu_processor = OCIO_processorGetCPUProcessor(processor);
  OCIO_processorRelease(processor);

  return cpu_processor;
}

static OCIO_ConstProcessorRcPtr *create_colorspace_transform_processor(const char *from_colorspace,
                                                                       const char *to_colorspace)
{
  OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
  OCIO_ConstProcessorRcPtr *processor;

  processor = OCIO_configGetProcessorWithNames(config, from_colorspace, to_colorspace);

  OCIO_configRelease(config);

  return processor;
}

static OCIO_ConstCPUProcessorRcPtr *colorspace_to_scene_linear_cpu_processor(
    ColorSpace *colorspace)
{
  if (colorspace->to_scene_linear == nullptr) {
    BLI_mutex_lock(&processor_lock);

    if (colorspace->to_scene_linear == nullptr) {
      OCIO_ConstProcessorRcPtr *processor = create_colorspace_transform_processor(
          colorspace->name, global_role_scene_linear);

      if (processor != nullptr) {
        colorspace->to_scene_linear = (OCIO_ConstCPUProcessorRcPtr *)OCIO_processorGetCPUProcessor(
            processor);
        OCIO_processorRelease(processor);
      }
    }

    BLI_mutex_unlock(&processor_lock);
  }

  return (OCIO_ConstCPUProcessorRcPtr *)colorspace->to_scene_linear;
}

static OCIO_ConstCPUProcessorRcPtr *colorspace_from_scene_linear_cpu_processor(
    ColorSpace *colorspace)
{
  if (colorspace->from_scene_linear == nullptr) {
    BLI_mutex_lock(&processor_lock);

    if (colorspace->from_scene_linear == nullptr) {
      OCIO_ConstProcessorRcPtr *processor = create_colorspace_transform_processor(
          global_role_scene_linear, colorspace->name);

      if (processor != nullptr) {
        colorspace->from_scene_linear = (OCIO_ConstCPUProcessorRcPtr *)
            OCIO_processorGetCPUProcessor(processor);
        OCIO_processorRelease(processor);
      }
    }

    BLI_mutex_unlock(&processor_lock);
  }

  return (OCIO_ConstCPUProcessorRcPtr *)colorspace->from_scene_linear;
}

static OCIO_ConstCPUProcessorRcPtr *display_from_scene_linear_processor(
    ColorManagedDisplay *display)
{
  if (display->from_scene_linear == nullptr) {
    BLI_mutex_lock(&processor_lock);

    if (display->from_scene_linear == nullptr) {
      const char *view_name = colormanage_view_get_default_name(display);
      OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
      OCIO_ConstProcessorRcPtr *processor = nullptr;

      if (view_name && config) {
        processor = OCIO_createDisplayProcessor(config,
                                                global_role_scene_linear,
                                                view_name,
                                                display->name,
                                                nullptr,
                                                1.0f,
                                                1.0f,
                                                false);

        OCIO_configRelease(config);
      }

      if (processor != nullptr) {
        display->from_scene_linear = (OCIO_ConstCPUProcessorRcPtr *)OCIO_processorGetCPUProcessor(
            processor);
        OCIO_processorRelease(processor);
      }
    }

    BLI_mutex_unlock(&processor_lock);
  }

  return (OCIO_ConstCPUProcessorRcPtr *)display->from_scene_linear;
}

static OCIO_ConstCPUProcessorRcPtr *display_to_scene_linear_processor(ColorManagedDisplay *display)
{
  if (display->to_scene_linear == nullptr) {
    BLI_mutex_lock(&processor_lock);

    if (display->to_scene_linear == nullptr) {
      const char *view_name = colormanage_view_get_default_name(display);
      OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
      OCIO_ConstProcessorRcPtr *processor = nullptr;

      if (view_name && config) {
        processor = OCIO_createDisplayProcessor(
            config, global_role_scene_linear, view_name, display->name, nullptr, 1.0f, 1.0f, true);

        OCIO_configRelease(config);
      }

      if (processor != nullptr) {
        display->to_scene_linear = (OCIO_ConstCPUProcessorRcPtr *)OCIO_processorGetCPUProcessor(
            processor);
        OCIO_processorRelease(processor);
      }
    }

    BLI_mutex_unlock(&processor_lock);
  }

  return (OCIO_ConstCPUProcessorRcPtr *)display->to_scene_linear;
}

void IMB_colormanagement_init_default_view_settings(
    ColorManagedViewSettings *view_settings, const ColorManagedDisplaySettings *display_settings)
{
  /* First, try use "Standard" view transform of the requested device. */
  ColorManagedView *default_view = colormanage_view_get_named_for_display(
      display_settings->display_device, "Standard");
  /* If that fails, we fall back to the default view transform of the display
   * as per OCIO configuration. */
  if (default_view == nullptr) {
    ColorManagedDisplay *display = colormanage_display_get_named(display_settings->display_device);
    if (display != nullptr) {
      default_view = colormanage_view_get_default(display);
    }
  }
  if (default_view != nullptr) {
    BLI_strncpy(
        view_settings->view_transform, default_view->name, sizeof(view_settings->view_transform));
  }
  else {
    view_settings->view_transform[0] = '\0';
  }
  /* TODO(sergey): Find a way to safely/reliable un-hardcode this. */
  BLI_strncpy(view_settings->look, "None", sizeof(view_settings->look));
  /* Initialize rest of the settings. */
  view_settings->flag = 0;
  view_settings->gamma = 1.0f;
  view_settings->exposure = 0.0f;
  view_settings->curve_mapping = nullptr;
}
static void curve_mapping_apply_pixel(CurveMapping *curve_mapping, float *pixel, int channels)
{
  if (channels == 1) {
    pixel[0] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[0]);
  }
  else if (channels == 2) {
    pixel[0] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[0]);
    pixel[1] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[1]);
  }
  else {
    BKE_curvemapping_evaluate_premulRGBF(curve_mapping, pixel, pixel);
  }
}

void colorspace_set_default_role(char *colorspace, int size, int role)
{
  if (colorspace && colorspace[0] == '\0') {
    const char *role_colorspace;

    role_colorspace = IMB_colormanagement_role_colorspace_name_get(role);

    BLI_strncpy(colorspace, role_colorspace, size);
  }
}

void colormanage_imbuf_set_default_spaces(ImBuf *ibuf)
{
  ibuf->rect_colorspace = colormanage_colorspace_get_named(global_role_default_byte);
}

void colormanage_imbuf_make_linear(ImBuf *ibuf, const char *from_colorspace)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(from_colorspace);

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
    return;
  }

  if (ibuf->rect_float) {
    const char *to_colorspace = global_role_scene_linear;
    const bool predivide = IMB_alpha_affects_rgb(ibuf);

    if (ibuf->rect) {
      imb_freerectImBuf(ibuf);
    }

    IMB_colormanagement_transform(ibuf->rect_float,
                                  ibuf->x,
                                  ibuf->y,
                                  ibuf->channels,
                                  from_colorspace,
                                  to_colorspace,
                                  predivide);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic Functions
 * \{ */

static void colormanage_check_display_settings(ColorManagedDisplaySettings *display_settings,
                                               const char *what,
                                               const ColorManagedDisplay *default_display)
{
  if (display_settings->display_device[0] == '\0') {
    BLI_strncpy(display_settings->display_device,
                default_display->name,
                sizeof(display_settings->display_device));
  }
  else {
    ColorManagedDisplay *display = colormanage_display_get_named(display_settings->display_device);

    if (!display) {
      printf(
          "Color management: display \"%s\" used by %s not found, setting to default (\"%s\").\n",
          display_settings->display_device,
          what,
          default_display->name);

      BLI_strncpy(display_settings->display_device,
                  default_display->name,
                  sizeof(display_settings->display_device));
    }
  }
}

static void colormanage_check_view_settings(ColorManagedDisplaySettings *display_settings,
                                            ColorManagedViewSettings *view_settings,
                                            const char *what)
{
  ColorManagedDisplay *display;
  ColorManagedView *default_view = nullptr;
  ColorManagedLook *default_look = (ColorManagedLook *)global_looks.first;

  if (view_settings->view_transform[0] == '\0') {
    display = colormanage_display_get_named(display_settings->display_device);

    if (display) {
      default_view = colormanage_view_get_default(display);
    }

    if (default_view) {
      BLI_strncpy(view_settings->view_transform,
                  default_view->name,
                  sizeof(view_settings->view_transform));
    }
  }
  else {
    ColorManagedView *view = colormanage_view_get_named(view_settings->view_transform);

    if (!view) {
      display = colormanage_display_get_named(display_settings->display_device);

      if (display) {
        default_view = colormanage_view_get_default(display);
      }

      if (default_view) {
        printf("Color management: %s view \"%s\" not found, setting default \"%s\".\n",
               what,
               view_settings->view_transform,
               default_view->name);

        BLI_strncpy(view_settings->view_transform,
                    default_view->name,
                    sizeof(view_settings->view_transform));
      }
    }
  }

  if (view_settings->look[0] == '\0') {
    BLI_strncpy(view_settings->look, default_look->name, sizeof(view_settings->look));
  }
  else {
    ColorManagedLook *look = colormanage_look_get_named(view_settings->look);
    if (look == nullptr) {
      printf("Color management: %s look \"%s\" not found, setting default \"%s\".\n",
             what,
             view_settings->look,
             default_look->name);

      BLI_strncpy(view_settings->look, default_look->name, sizeof(view_settings->look));
    }
  }

  /* OCIO_TODO: move to do_versions() */
  if (view_settings->exposure == 0.0f && view_settings->gamma == 0.0f) {
    view_settings->exposure = 0.0f;
    view_settings->gamma = 1.0f;
  }
}

static void colormanage_check_colorspace_settings(
    ColorManagedColorspaceSettings *colorspace_settings, const char *what)
{
  if (colorspace_settings->name[0] == '\0') {
    /* pass */
  }
  else {
    ColorSpace *colorspace = colormanage_colorspace_get_named(colorspace_settings->name);

    if (!colorspace) {
      printf("Color management: %s colorspace \"%s\" not found, will use default instead.\n",
             what,
             colorspace_settings->name);

      BLI_strncpy(colorspace_settings->name, "", sizeof(colorspace_settings->name));
    }
  }

  (void)what;
}

static bool seq_callback(Sequence *seq, void * /*user_data*/)
{
  if (seq->strip) {
    colormanage_check_colorspace_settings(&seq->strip->colorspace_settings, "sequencer strip");
  }
  return true;
}

void IMB_colormanagement_check_file_config(Main *bmain)
{
  ColorManagedDisplay *default_display = colormanage_display_get_default();
  if (!default_display) {
    /* happens when OCIO configuration is incorrect */
    return;
  }

  LISTBASE_FOREACH (Scene *, scene, &bmain->scenes) {
    ColorManagedColorspaceSettings *sequencer_colorspace_settings;

    /* check scene color management settings */
    colormanage_check_display_settings(&scene->display_settings, "scene", default_display);
    colormanage_check_view_settings(&scene->display_settings, &scene->view_settings, "scene");

    sequencer_colorspace_settings = &scene->sequencer_colorspace_settings;

    colormanage_check_colorspace_settings(sequencer_colorspace_settings, "sequencer");

    if (sequencer_colorspace_settings->name[0] == '\0') {
      BLI_strncpy(
          sequencer_colorspace_settings->name, global_role_default_sequencer, MAX_COLORSPACE_NAME);
    }

    /* check sequencer strip input color space settings */
    if (scene->ed != nullptr) {
      SEQ_for_each_callback(&scene->ed->seqbase, seq_callback, nullptr);
    }
  }

  /* ** check input color space settings ** */

  LISTBASE_FOREACH (Image *, image, &bmain->images) {
    colormanage_check_colorspace_settings(&image->colorspace_settings, "image");
  }

  LISTBASE_FOREACH (MovieClip *, clip, &bmain->movieclips) {
    colormanage_check_colorspace_settings(&clip->colorspace_settings, "clip");
  }
}

void IMB_colormanagement_validate_settings(const ColorManagedDisplaySettings *display_settings,
                                           ColorManagedViewSettings *view_settings)
{
  ColorManagedDisplay *display = colormanage_display_get_named(display_settings->display_device);
  ColorManagedView *default_view = colormanage_view_get_default(display);

  bool found = false;
  LISTBASE_FOREACH (LinkData *, view_link, &display->views) {
    ColorManagedView *view = static_cast<ColorManagedView *>(view_link->data);

    if (STREQ(view->name, view_settings->view_transform)) {
      found = true;
      break;
    }
  }

  if (!found && default_view) {
    BLI_strncpy(
        view_settings->view_transform, default_view->name, sizeof(view_settings->view_transform));
  }
}

const char *IMB_colormanagement_role_colorspace_name_get(int role)
{
  switch (role) {
    case COLOR_ROLE_DATA:
      return global_role_data;
    case COLOR_ROLE_SCENE_LINEAR:
      return global_role_scene_linear;
    case COLOR_ROLE_COLOR_PICKING:
      return global_role_color_picking;
    case COLOR_ROLE_TEXTURE_PAINTING:
      return global_role_texture_painting;
    case COLOR_ROLE_DEFAULT_SEQUENCER:
      return global_role_default_sequencer;
    case COLOR_ROLE_DEFAULT_FLOAT:
      return global_role_default_float;
    case COLOR_ROLE_DEFAULT_BYTE:
      return global_role_default_byte;
    default:
      printf("Unknown role was passed to %s\n", __func__);
      BLI_assert(0);
      break;
  }

  return nullptr;
}

void IMB_colormanagement_check_is_data(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

void IMB_colormanagegent_copy_settings(ImBuf *ibuf_src, ImBuf *ibuf_dst)
{
  IMB_colormanagement_assign_rect_colorspace(ibuf_dst,
                                             IMB_colormanagement_get_rect_colorspace(ibuf_src));
  IMB_colormanagement_assign_float_colorspace(ibuf_dst,
                                              IMB_colormanagement_get_float_colorspace(ibuf_src));
  if (ibuf_src->flags & IB_alphamode_premul) {
    ibuf_dst->flags |= IB_alphamode_premul;
  }
  else if (ibuf_src->flags & IB_alphamode_channel_packed) {
    ibuf_dst->flags |= IB_alphamode_channel_packed;
  }
  else if (ibuf_src->flags & IB_alphamode_ignore) {
    ibuf_dst->flags |= IB_alphamode_ignore;
  }
}

void IMB_colormanagement_assign_float_colorspace(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  ibuf->float_colorspace = colorspace;

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

void IMB_colormanagement_assign_rect_colorspace(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  ibuf->rect_colorspace = colorspace;

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

const char *IMB_colormanagement_get_float_colorspace(ImBuf *ibuf)
{
  if (ibuf->float_colorspace) {
    return ibuf->float_colorspace->name;
  }

  return IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_SCENE_LINEAR);
}

const char *IMB_colormanagement_get_rect_colorspace(ImBuf *ibuf)
{
  if (ibuf->rect_colorspace) {
    return ibuf->rect_colorspace->name;
  }

  return IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_BYTE);
}

bool IMB_colormanagement_space_is_data(ColorSpace *colorspace)
{
  return (colorspace && colorspace->is_data);
}

static void colormanage_ensure_srgb_scene_linear_info(ColorSpace *colorspace)
{
  if (!colorspace->info.cached) {
    OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
    OCIO_ConstColorSpaceRcPtr *ocio_colorspace = OCIO_configGetColorSpace(config,
                                                                          colorspace->name);

    bool is_scene_linear, is_srgb;
    OCIO_colorSpaceIsBuiltin(config, ocio_colorspace, &is_scene_linear, &is_srgb);

    OCIO_colorSpaceRelease(ocio_colorspace);
    OCIO_configRelease(config);

    colorspace->info.is_scene_linear = is_scene_linear;
    colorspace->info.is_srgb = is_srgb;
    colorspace->info.cached = true;
  }
}

bool IMB_colormanagement_space_is_scene_linear(ColorSpace *colorspace)
{
  colormanage_ensure_srgb_scene_linear_info(colorspace);
  return (colorspace && colorspace->info.is_scene_linear);
}

bool IMB_colormanagement_space_is_srgb(ColorSpace *colorspace)
{
  colormanage_ensure_srgb_scene_linear_info(colorspace);
  return (colorspace && colorspace->info.is_srgb);
}

bool IMB_colormanagement_space_name_is_data(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && colorspace->is_data);
}

bool IMB_colormanagement_space_name_is_scene_linear(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && IMB_colormanagement_space_is_scene_linear(colorspace));
}

bool IMB_colormanagement_space_name_is_srgb(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && IMB_colormanagement_space_is_srgb(colorspace));
}

const float *IMB_colormanagement_get_xyz_to_scene_linear(void)
{
  return &imbuf_xyz_to_scene_linear[0][0];
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Threaded Display Buffer Transform Routines
 * \{ */

typedef struct DisplayBufferThread {
  ColormanageProcessor *cm_processor;

  const float *buffer;
  uchar *byte_buffer;

  float *display_buffer;
  uchar *display_buffer_byte;

  int width;
  int start_line;
  int tot_line;

  int channels;
  float dither;
  bool is_data;
  bool predivide;

  const char *byte_colorspace;
  const char *float_colorspace;
} DisplayBufferThread;

typedef struct DisplayBufferInitData {
  ImBuf *ibuf;
  ColormanageProcessor *cm_processor;
  const float *buffer;
  uchar *byte_buffer;

  float *display_buffer;
  uchar *display_buffer_byte;

  int width;

  const char *byte_colorspace;
  const char *float_colorspace;
} DisplayBufferInitData;

static void display_buffer_init_handle(void *handle_v,
                                       int start_line,
                                       int tot_line,
                                       void *init_data_v)
{
  DisplayBufferThread *handle = (DisplayBufferThread *)handle_v;
  DisplayBufferInitData *init_data = (DisplayBufferInitData *)init_data_v;
  ImBuf *ibuf = init_data->ibuf;

  int channels = ibuf->channels;
  float dither = ibuf->dither;
  bool is_data = (ibuf->colormanage_flag & IMB_COLORMANAGE_IS_DATA) != 0;

  size_t offset = size_t(channels) * start_line * ibuf->x;
  size_t display_buffer_byte_offset = size_t(DISPLAY_BUFFER_CHANNELS) * start_line * ibuf->x;

  memset(handle, 0, sizeof(DisplayBufferThread));

  handle->cm_processor = init_data->cm_processor;

  if (init_data->buffer) {
    handle->buffer = init_data->buffer + offset;
  }

  if (init_data->byte_buffer) {
    handle->byte_buffer = init_data->byte_buffer + offset;
  }

  if (init_data->display_buffer) {
    handle->display_buffer = init_data->display_buffer + offset;
  }

  if (init_data->display_buffer_byte) {
    handle->display_buffer_byte = init_data->display_buffer_byte + display_buffer_byte_offset;
  }

  handle->width = ibuf->x;

  handle->start_line = start_line;
  handle->tot_line = tot_line;

  handle->channels = channels;
  handle->dither = dither;
  handle->is_data = is_data;
  handle->predivide = IMB_alpha_affects_rgb(ibuf);

  handle->byte_colorspace = init_data->byte_colorspace;
  handle->float_colorspace = init_data->float_colorspace;
}

static void display_buffer_apply_get_linear_buffer(DisplayBufferThread *handle,
                                                   int height,
                                                   float *linear_buffer,
                                                   bool *is_straight_alpha)
{
  int channels = handle->channels;
  int width = handle->width;

  size_t buffer_size = size_t(channels) * width * height;

  bool is_data = handle->is_data;
  bool is_data_display = handle->cm_processor->is_data_result;
  bool predivide = handle->predivide;

  if (!handle->buffer) {
    uchar *byte_buffer = handle->byte_buffer;

    const char *from_colorspace = handle->byte_colorspace;
    const char *to_colorspace = global_role_scene_linear;

    float *fp;
    uchar *cp;
    const size_t i_last = size_t(width) * height;
    size_t i;

    /* first convert byte buffer to float, keep in image space */
    for (i = 0, fp = linear_buffer, cp = byte_buffer; i != i_last;
         i++, fp += channels, cp += channels) {
      if (channels == 3) {
        rgb_uchar_to_float(fp, cp);
      }
      else if (channels == 4) {
        rgba_uchar_to_float(fp, cp);
      }
      else {
        BLI_assert_msg(0, "Buffers of 3 or 4 channels are only supported here");
      }
    }

    if (!is_data && !is_data_display) {
      /* convert float buffer to scene linear space */
      IMB_colormanagement_transform(
          linear_buffer, width, height, channels, from_colorspace, to_colorspace, false);
    }

    *is_straight_alpha = true;
  }
  else if (handle->float_colorspace) {
    /* currently float is non-linear only in sequencer, which is working
     * in its own color space even to handle float buffers.
     * This color space is the same for byte and float images.
     * Need to convert float buffer to linear space before applying display transform
     */

    const char *from_colorspace = handle->float_colorspace;
    const char *to_colorspace = global_role_scene_linear;

    memcpy(linear_buffer, handle->buffer, buffer_size * sizeof(float));

    if (!is_data && !is_data_display) {
      IMB_colormanagement_transform(
          linear_buffer, width, height, channels, from_colorspace, to_colorspace, predivide);
    }

    *is_straight_alpha = false;
  }
  else {
    /* some processors would want to modify float original buffer
     * before converting it into display byte buffer, so we need to
     * make sure original's ImBuf buffers wouldn't be modified by
     * using duplicated buffer here
     */

    memcpy(linear_buffer, handle->buffer, buffer_size * sizeof(float));

    *is_straight_alpha = false;
  }
}
static void curve_mapping_apply_pixel(CurveMapping *curve_mapping, float *pixel, int channels)
{
  if (channels == 1) {
    pixel[0] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[0]);
  }
  else if (channels == 2) {
    pixel[0] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[0]);
    pixel[1] = BKE_curvemap_evaluateF(curve_mapping, curve_mapping->cm, pixel[1]);
  }
  else {
    BKE_curvemapping_evaluate_premulRGBF(curve_mapping, pixel, pixel);
  }
}

void colorspace_set_default_role(char *colorspace, int size, int role)
{
  if (colorspace && colorspace[0] == '\0') {
    const char *role_colorspace;

    role_colorspace = IMB_colormanagement_role_colorspace_name_get(role);

    BLI_strncpy(colorspace, role_colorspace, size);
  }
}

void colormanage_imbuf_set_default_spaces(ImBuf *ibuf)
{
  ibuf->rect_colorspace = colormanage_colorspace_get_named(global_role_default_byte);
}

void colormanage_imbuf_make_linear(ImBuf *ibuf, const char *from_colorspace)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(from_colorspace);

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
    return;
  }

  if (ibuf->rect_float) {
    const char *to_colorspace = global_role_scene_linear;
    const bool predivide = IMB_alpha_affects_rgb(ibuf);

    if (ibuf->rect) {
      imb_freerectImBuf(ibuf);
    }

    IMB_colormanagement_transform(ibuf->rect_float,
                                  ibuf->x,
                                  ibuf->y,
                                  ibuf->channels,
                                  from_colorspace,
                                  to_colorspace,
                                  predivide);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic Functions
 * \{ */

static void colormanage_check_display_settings(ColorManagedDisplaySettings *display_settings,
                                               const char *what,
                                               const ColorManagedDisplay *default_display)
{
  if (display_settings->display_device[0] == '\0') {
    BLI_strncpy(display_settings->display_device,
                default_display->name,
                sizeof(display_settings->display_device));
  }
  else {
    ColorManagedDisplay *display = colormanage_display_get_named(display_settings->display_device);

    if (!display) {
      printf(
          "Color management: display \"%s\" used by %s not found, setting to default (\"%s\").\n",
          display_settings->display_device,
          what,
          default_display->name);

      BLI_strncpy(display_settings->display_device,
                  default_display->name,
                  sizeof(display_settings->display_device));
    }
  }
}

static void colormanage_check_view_settings(ColorManagedDisplaySettings *display_settings,
                                            ColorManagedViewSettings *view_settings,
                                            const char *what)
{
  ColorManagedDisplay *display;
  ColorManagedView *default_view = nullptr;
  ColorManagedLook *default_look = (ColorManagedLook *)global_looks.first;

  if (view_settings->view_transform[0] == '\0') {
    display = colormanage_display_get_named(display_settings->display_device);

    if (display) {
      default_view = colormanage_view_get_default(display);
    }

    if (default_view) {
      BLI_strncpy(view_settings->view_transform,
                  default_view->name,
                  sizeof(view_settings->view_transform));
    }
  }
  else {
    ColorManagedView *view = colormanage_view_get_named(view_settings->view_transform);

    if (!view) {
      display = colormanage_display_get_named(display_settings->display_device);

      if (display) {
        default_view = colormanage_view_get_default(display);
      }

      if (default_view) {
        printf("Color management: %s view \"%s\" not found, setting default \"%s\".\n",
               what,
               view_settings->view_transform,
               default_view->name);

        BLI_strncpy(view_settings->view_transform,
                    default_view->name,
                    sizeof(view_settings->view_transform));
      }
    }
  }

  if (view_settings->look[0] == '\0') {
    BLI_strncpy(view_settings->look, default_look->name, sizeof(view_settings->look));
  }
  else {
    ColorManagedLook *look = colormanage_look_get_named(view_settings->look);
    if (look == nullptr) {
      printf("Color management: %s look \"%s\" not found, setting default \"%s\".\n",
             what,
             view_settings->look,
             default_look->name);

      BLI_strncpy(view_settings->look, default_look->name, sizeof(view_settings->look));
    }
  }

  /* OCIO_TODO: move to do_versions() */
  if (view_settings->exposure == 0.0f && view_settings->gamma == 0.0f) {
    view_settings->exposure = 0.0f;
    view_settings->gamma = 1.0f;
  }
}

static void colormanage_check_colorspace_settings(
    ColorManagedColorspaceSettings *colorspace_settings, const char *what)
{
  if (colorspace_settings->name[0] == '\0') {
    /* pass */
  }
  else {
    ColorSpace *colorspace = colormanage_colorspace_get_named(colorspace_settings->name);

    if (!colorspace) {
      printf("Color management: %s colorspace \"%s\" not found, will use default instead.\n",
             what,
             colorspace_settings->name);

      BLI_strncpy(colorspace_settings->name, "", sizeof(colorspace_settings->name));
    }
  }

  (void)what;
}

static bool seq_callback(Sequence *seq, void * /*user_data*/)
{
  if (seq->strip) {
    colormanage_check_colorspace_settings(&seq->strip->colorspace_settings, "sequencer strip");
  }
  return true;
}

void IMB_colormanagement_check_file_config(Main *bmain)
{
  ColorManagedDisplay *default_display = colormanage_display_get_default();
  if (!default_display) {
    /* happens when OCIO configuration is incorrect */
    return;
  }

  LISTBASE_FOREACH (Scene *, scene, &bmain->scenes) {
    ColorManagedColorspaceSettings *sequencer_colorspace_settings;

    /* check scene color management settings */
    colormanage_check_display_settings(&scene->display_settings, "scene", default_display);
    colormanage_check_view_settings(&scene->display_settings, &scene->view_settings, "scene");

    sequencer_colorspace_settings = &scene->sequencer_colorspace_settings;

    colormanage_check_colorspace_settings(sequencer_colorspace_settings, "sequencer");

    if (sequencer_colorspace_settings->name[0] == '\0') {
      BLI_strncpy(
          sequencer_colorspace_settings->name, global_role_default_sequencer, MAX_COLORSPACE_NAME);
    }

    /* check sequencer strip input color space settings */
    if (scene->ed != nullptr) {
      SEQ_for_each_callback(&scene->ed->seqbase, seq_callback, nullptr);
    }
  }

  /* ** check input color space settings ** */

  LISTBASE_FOREACH (Image *, image, &bmain->images) {
    colormanage_check_colorspace_settings(&image->colorspace_settings, "image");
  }

  LISTBASE_FOREACH (MovieClip *, clip, &bmain->movieclips) {
    colormanage_check_colorspace_settings(&clip->colorspace_settings, "clip");
  }
}

void IMB_colormanagement_validate_settings(const ColorManagedDisplaySettings *display_settings,
                                           ColorManagedViewSettings *view_settings)
{
  ColorManagedDisplay *display = colormanage_display_get_named(display_settings->display_device);
  ColorManagedView *default_view = colormanage_view_get_default(display);

  bool found = false;
  LISTBASE_FOREACH (LinkData *, view_link, &display->views) {
    ColorManagedView *view = static_cast<ColorManagedView *>(view_link->data);

    if (STREQ(view->name, view_settings->view_transform)) {
      found = true;
      break;
    }
  }

  if (!found && default_view) {
    BLI_strncpy(
        view_settings->view_transform, default_view->name, sizeof(view_settings->view_transform));
  }
}

const char *IMB_colormanagement_role_colorspace_name_get(int role)
{
  switch (role) {
    case COLOR_ROLE_DATA:
      return global_role_data;
    case COLOR_ROLE_SCENE_LINEAR:
      return global_role_scene_linear;
    case COLOR_ROLE_COLOR_PICKING:
      return global_role_color_picking;
    case COLOR_ROLE_TEXTURE_PAINTING:
      return global_role_texture_painting;
    case COLOR_ROLE_DEFAULT_SEQUENCER:
      return global_role_default_sequencer;
    case COLOR_ROLE_DEFAULT_FLOAT:
      return global_role_default_float;
    case COLOR_ROLE_DEFAULT_BYTE:
      return global_role_default_byte;
    default:
      printf("Unknown role was passed to %s\n", __func__);
      BLI_assert(0);
      break;
  }

  return nullptr;
}

void IMB_colormanagement_check_is_data(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

void IMB_colormanagegent_copy_settings(ImBuf *ibuf_src, ImBuf *ibuf_dst)
{
  IMB_colormanagement_assign_rect_colorspace(ibuf_dst,
                                             IMB_colormanagement_get_rect_colorspace(ibuf_src));
  IMB_colormanagement_assign_float_colorspace(ibuf_dst,
                                              IMB_colormanagement_get_float_colorspace(ibuf_src));
  if (ibuf_src->flags & IB_alphamode_premul) {
    ibuf_dst->flags |= IB_alphamode_premul;
  }
  else if (ibuf_src->flags & IB_alphamode_channel_packed) {
    ibuf_dst->flags |= IB_alphamode_channel_packed;
  }
  else if (ibuf_src->flags & IB_alphamode_ignore) {
    ibuf_dst->flags |= IB_alphamode_ignore;
  }
}

void IMB_colormanagement_assign_float_colorspace(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  ibuf->float_colorspace = colorspace;

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

void IMB_colormanagement_assign_rect_colorspace(ImBuf *ibuf, const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);

  ibuf->rect_colorspace = colorspace;

  if (colorspace && colorspace->is_data) {
    ibuf->colormanage_flag |= IMB_COLORMANAGE_IS_DATA;
  }
  else {
    ibuf->colormanage_flag &= ~IMB_COLORMANAGE_IS_DATA;
  }
}

const char *IMB_colormanagement_get_float_colorspace(ImBuf *ibuf)
{
  if (ibuf->float_colorspace) {
    return ibuf->float_colorspace->name;
  }

  return IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_SCENE_LINEAR);
}

const char *IMB_colormanagement_get_rect_colorspace(ImBuf *ibuf)
{
  if (ibuf->rect_colorspace) {
    return ibuf->rect_colorspace->name;
  }

  return IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_BYTE);
}

bool IMB_colormanagement_space_is_data(ColorSpace *colorspace)
{
  return (colorspace && colorspace->is_data);
}

static void colormanage_ensure_srgb_scene_linear_info(ColorSpace *colorspace)
{
  if (!colorspace->info.cached) {
    OCIO_ConstConfigRcPtr *config = OCIO_getCurrentConfig();
    OCIO_ConstColorSpaceRcPtr *ocio_colorspace = OCIO_configGetColorSpace(config,
                                                                          colorspace->name);

    bool is_scene_linear, is_srgb;
    OCIO_colorSpaceIsBuiltin(config, ocio_colorspace, &is_scene_linear, &is_srgb);

    OCIO_colorSpaceRelease(ocio_colorspace);
    OCIO_configRelease(config);

    colorspace->info.is_scene_linear = is_scene_linear;
    colorspace->info.is_srgb = is_srgb;
    colorspace->info.cached = true;
  }
}

bool IMB_colormanagement_space_is_scene_linear(ColorSpace *colorspace)
{
  colormanage_ensure_srgb_scene_linear_info(colorspace);
  return (colorspace && colorspace->info.is_scene_linear);
}

bool IMB_colormanagement_space_is_srgb(ColorSpace *colorspace)
{
  colormanage_ensure_srgb_scene_linear_info(colorspace);
  return (colorspace && colorspace->info.is_srgb);
}

bool IMB_colormanagement_space_name_is_data(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && colorspace->is_data);
}

bool IMB_colormanagement_space_name_is_scene_linear(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && IMB_colormanagement_space_is_scene_linear(colorspace));
}

bool IMB_colormanagement_space_name_is_srgb(const char *name)
{
  ColorSpace *colorspace = colormanage_colorspace_get_named(name);
  return (colorspace && IMB_colormanagement_space_is_srgb(colorspace));
}

const float *IMB_colormanagement_get_xyz_to_scene_linear(void)
{
  return &imbuf_xyz_to_scene_linear[0][0];
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Threaded Display Buffer Transform Routines
 * \{ */

typedef struct DisplayBufferThread {
  ColormanageProcessor *cm_processor;

  const float *buffer;
  uchar *byte_buffer;

  float *display_buffer;
  uchar *display_buffer_byte;

  int width;
  int start_line;
  int tot_line;

  int channels;
  float dither;
  bool is_data;
  bool predivide;

  const char *byte_colorspace;
  const char *float_colorspace;
} DisplayBufferThread;

typedef struct DisplayBufferInitData {
  ImBuf *ibuf;
  ColormanageProcessor *cm_processor;
  const float *buffer;
  uchar *byte_buffer;

  float *display_buffer;
  uchar *display_buffer_byte;

  int width;

  const char *byte_colorspace;
  const char *float_colorspace;
} DisplayBufferInitData;

static void display_buffer_init_handle(void *handle_v,
                                       int start_line,
                                       int tot_line,
                                       void *init_data_v)
{
  DisplayBufferThread *handle = (DisplayBufferThread *)handle_v;
  DisplayBufferInitData *init_data = (DisplayBufferInitData *)init_data_v;
  ImBuf *ibuf = init_data->ibuf;

  int channels = ibuf->channels;
  float dither = ibuf->dither;
  bool is_data = (ibuf->colormanage_flag & IMB_COLORMANAGE_IS_DATA) != 0;

  size_t offset = size_t(channels) * start_line * ibuf->x;
  size_t display_buffer_byte_offset = size_t(DISPLAY_BUFFER_CHANNELS) * start_line * ibuf->x;

  memset(handle, 0, sizeof(DisplayBufferThread));

  handle->cm_processor = init_data->cm_processor;

  if (init_data->buffer) {
    handle->buffer = init_data->buffer + offset;
  }

  if (init_data->byte_buffer) {
    handle->byte_buffer = init_data->byte_buffer + offset;
  }

  if (init_data->display_buffer) {
    handle->display_buffer = init_data->display_buffer + offset;
  }

  if (init_data->display_buffer_byte) {
    handle->display_buffer_byte = init_data->display_buffer_byte + display_buffer_byte_offset;
  }

  handle->width = ibuf->x;

  handle->start_line = start_line;
  handle->tot_line = tot_line;

  handle->channels = channels;
  handle->dither = dither;
  handle->is_data = is_data;
  handle->predivide = IMB_alpha_affects_rgb(ibuf);

  handle->byte_colorspace = init_data->byte_colorspace;
  handle->float_colorspace = init_data->float_colorspace;
}

static void display_buffer_apply_get_linear_buffer(DisplayBufferThread *handle,
                                                   int height,
                                                   float *linear_buffer,
                                                   bool *is_straight_alpha)
{
  int channels = handle->channels;
  int width = handle->width;

  size_t buffer_size = size_t(channels) * width * height;

  bool is_data = handle->is_data;
  bool is_data_display = handle->cm_processor->is_data_result;
  bool predivide = handle->predivide;

  if (!handle->buffer) {
    uchar *byte_buffer = handle->byte_buffer;

    const char *from_colorspace = handle->byte_colorspace;
    const char *to_colorspace = global_role_scene_linear;

    float *fp;
    uchar *cp;
    const size_t i_last = size_t(width) * height;
    size_t i;

    /* first convert byte buffer to float, keep in image space */
    for (i = 0, fp = linear_buffer, cp = byte_buffer; i != i_last;
         i++, fp += channels, cp += channels) {
      if (channels == 3) {
        rgb_uchar_to_float(fp, cp);
      }
      else if (channels == 4) {
        rgba_uchar_to_float(fp, cp);
      }
      else {
        BLI_assert_msg(0, "Buffers of 3 or 4 channels are only supported here");
      }
    }

    if (!is_data && !is_data_display) {
      /* convert float buffer to scene linear space */
      IMB_colormanagement_transform(
          linear_buffer, width, height, channels, from_colorspace, to_colorspace, false);
    }

    *is_straight_alpha = true;
  }
  else if (handle->float_colorspace) {
    /* currently float is non-linear only in sequencer, which is working
     * in its own color space even to handle float buffers.
     * This color space is the same for byte and float images.
     * Need to convert float buffer to linear space before applying display transform
     */

    const char *from_colorspace = handle->float_colorspace;
    const char *to_colorspace = global_role_scene_linear;

    memcpy(linear_buffer, handle->buffer, buffer_size * sizeof(float));

    if (!is_data && !is_data_display) {
      IMB_colormanagement_transform(
          linear_buffer, width, height, channels, from_colorspace, to_colorspace, predivide);
    }

    *is_straight_alpha = false;
  }
  else {
    /* some processors would want to modify float original buffer
     * before converting it into display byte buffer, so we need to
     * make sure original's ImBuf buffers wouldn't be modified by
     * using duplicated buffer here
     */

    memcpy(linear_buffer, handle->buffer, buffer_size * sizeof(float));

    *is_straight_alpha = false;
  }
}

static void *do_display_buffer_apply_thread(void *handle_v)
{
  DisplayBufferThread *handle = (DisplayBufferThread *)handle_v;
  ColormanageProcessor *cm_processor = handle->cm_processor;
  float *display_buffer = handle->display_buffer;
  uchar *display_buffer_byte = handle->display_buffer_byte;
  int channels = handle->channels;
  int width = handle->width;
  int height = handle->tot_line;
  float dither = handle->dither;
  bool is_data = handle->is_data;

  if (cm_processor == nullptr) {
    if (display_buffer_byte && display_buffer_byte != handle->byte_buffer) {
      IMB_buffer_byte_from_byte(display_buffer_byte,
                                handle->byte_buffer,
                                IB_PROFILE_SRGB,
                                IB_PROFILE_SRGB,
                                false,
                                width,
                                height,
                                width,
                                width);
    }

    if (display_buffer) {
      IMB_buffer_float_from_byte(display_buffer,
                                 handle->byte_buffer,
                                 IB_PROFILE_SRGB,
                                 IB_PROFILE_SRGB,
                                 false,
                                 width,
                                 height,
                                 width,
                                 width);
    }
  }
  else {
    bool is_straight_alpha;
    float *linear_buffer = static_cast<float *>(MEM_mallocN(
        size_t(channels) * width * height * sizeof(float), "color conversion linear buffer"));

    display_buffer_apply_get_linear_buffer(handle, height, linear_buffer, &is_straight_alpha);

    bool predivide = handle->predivide && (is_straight_alpha == false);

    if (is_data) {
      /* special case for data buffers - no color space conversions,
       * only generate byte buffers
       */
    }
    else {
      /* apply processor */
      IMB_colormanagement_processor_apply(
          cm_processor, linear_buffer, width, height, channels, predivide);
    }

    /* copy result to output buffers */
    if (display_buffer_byte) {
      /* do conversion */
      IMB_buffer_byte_from_float(display_buffer_byte,
                                 linear_buffer,
                                 channels,
                                 dither,
                                 IB_PROFILE_SRGB,
                                 IB_PROFILE_SRGB,
                                 predivide,
                                 width,
                                 height,
                                 width,
                                 width);
    }

    if (display_buffer) {
      memcpy(display_buffer, linear_buffer, size_t(width) * height * channels * sizeof(float));

      if (is_straight_alpha && channels == 4) {
        const size_t i_last = size_t(width) * height;
        size_t i;
        float *fp;

        for (i = 0, fp = display_buffer; i != i_last; i++, fp += channels) {
          straight_to_premul_v4(fp);
        }
      }
    }

    MEM_freeN(linear_buffer);
  }

  return nullptr;
}

static void display_buffer_apply_threaded(ImBuf *ibuf,
                                          const float *buffer,
                                          uchar *byte_buffer,
                                          float *display_buffer,
                                          uchar *display_buffer_byte,
                                          ColormanageProcessor *cm_processor)
{
  DisplayBufferInitData init_data;

  init_data.ibuf = ibuf;
  init_data.cm_processor = cm_processor;
  init_data.buffer = buffer;
  init_data.byte_buffer = byte_buffer;
  init_data.display_buffer = display_buffer;
  init_data.display_buffer_byte = display_buffer_byte;

  if (ibuf->rect_colorspace != nullptr) {
    init_data.byte_colorspace = ibuf->rect_colorspace->name;
  }
  else {
    /* happens for viewer images, which are not so simple to determine where to
     * set image buffer's color spaces
     */
    init_data.byte_colorspace = global_role_default_byte;
  }

  if (ibuf->float_colorspace != nullptr) {
    /* sequencer stores float buffers in non-linear space */
    init_data.float_colorspace = ibuf->float_colorspace->name;
  }
  else {
    init_data.float_colorspace = nullptr;
  }

  IMB_processor_apply_threaded(ibuf->y,
                               sizeof(DisplayBufferThread),
                               &init_data,
                               display_buffer_init_handle,
                               do_display_buffer_apply_thread);
}

static bool is_ibuf_rect_in_display_space(ImBuf *ibuf,
                                          const ColorManagedViewSettings *view_settings,
                                          const ColorManagedDisplaySettings *display_settings)
{
  if ((view_settings->flag & COLORMANAGE_VIEW_USE_CURVES) == 0 &&
      view_settings->exposure == 0.0f && view_settings->gamma == 1.0f)
  {
    const char *from_colorspace = ibuf->rect_colorspace->name;
    const char *to_colorspace = get_display_colorspace_name(view_settings, display_settings);
    ColorManagedLook *look_descr = colormanage_look_get_named(view_settings->look);
    if (look_descr != nullptr && !STREQ(look_descr->process_space, "")) {
      return false;
    }

    if (to_colorspace && STREQ(from_colorspace, to_colorspace)) {
      return true;
    }
  }

  return false;
}

static void colormanage_display_buffer_process_ex(
    ImBuf *ibuf,
    float *display_buffer,
    uchar *display_buffer_byte,
    const ColorManagedViewSettings *view_settings,
    const ColorManagedDisplaySettings *display_settings)
{
  ColormanageProcessor *cm_processor = nullptr;
  bool skip_transform = false;

  /* if we're going to transform byte buffer, check whether transformation would
   * happen to the same color space as byte buffer itself is
   * this would save byte -> float -> byte conversions making display buffer
   * computation noticeable faster
   */
  if (ibuf->rect_float == nullptr && ibuf->rect_colorspace) {
    skip_transform = is_ibuf_rect_in_display_space(ibuf, view_settings, display_settings);
  }

  if (skip_transform == false) {
    cm_processor = IMB_colormanagement_display_processor_new(view_settings, display_settings);
  }

  display_buffer_apply_threaded(ibuf,
                                ibuf->rect_float,
                                (uchar *)ibuf->rect,
                                display_buffer,
                                display_buffer_byte,
                                cm_processor);

  if (cm_processor) {
    IMB_colormanagement_processor_free(cm_processor);
  }
}
