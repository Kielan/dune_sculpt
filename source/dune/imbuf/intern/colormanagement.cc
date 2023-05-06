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
