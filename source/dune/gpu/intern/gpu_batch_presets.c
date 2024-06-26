#include "lib_list.h"
#include "lib_math.h"
#include "lib_threads.h"
#include "lib_utildefines.h"
#include "mem_guardedalloc.h"

#include "types_userdef_types.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "gpu_batch.h"
#include "gpu_batch_presets.h" /* own include */
#include "gpu_batch_utils.h"


/* Local Structures */

/* Struct to store 3D Batches and their format */
static struct {
  struct {
    GPUBatch *sphere_high;
    GPUBatch *sphere_med;
    GPUBatch *sphere_low;
    GPUBatch *sphere_wire_low;
    GPUBatch *sphere_wire_med;
  } batch;

  GPUVertFormat format;

  struct {
    uint pos, nor;
  } attr_id;

  ThreadMutex mutex;
} g_presets_3d = {{0}};

static struct {
  struct {
    GPUBatch *panel_drag_widget;
    GPUBatch *quad;
  } batch;

  float panel_drag_widget_pixelsize;
  float panel_drag_widget_width;
  float panel_drag_widget_col_high[4];
  float panel_drag_widget_col_dark[4];

  GPUVertFormat format;

  struct {
    uint pos, col;
  } attr_id;
} g_presets_2d = {{0}};

static ListBase presets_list = {NULL, NULL};

/* 3D Primitives */
static GPUVertFormat *preset_3d_format(void)
{
  if (g_presets_3d.format.attr_len == 0) {
    GPUVertFormat *format = &g_presets_3d.format;
    g_presets_3d.attr_id.pos = gpu_vertformat_attr_add(
        format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    g_presets_3d.attr_id.nor = gpu_vertformat_attr_add(
        format, "nor", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  }
  return &g_presets_3d.format;
}

static GPUVertFormat *preset_2d_format(void)
{
  if (g_presets_2d.format.attr_len == 0) {
    GPUVertFormat *format = &g_presets_2d.format;
    g_presets_2d.attr_id.pos = gpu_vertformat_attr_add(
        format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    g_presets_2d.attr_id.col = gpu_vertformat_attr_add(
        format, "color", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  }
  return &g_presets_2d.format;
}

static void batch_sphere_lat_lon_vert(GPUVertBufRaw *pos_step,
                                      GPUVertBufRaw *nor_step,
                                      float lat,
                                      float lon)
{
  float pos[3];
  pos[0] = sinf(lat) * cosf(lon);
  pos[1] = cosf(lat);
  pos[2] = sinf(lat) * sinf(lon);
  copy_v3_v3(gpu_vertbuf_raw_step(pos_step), pos);
  copy_v3_v3(gpu_vertbuf_raw_step(nor_step), pos);
}
GPUBatch *gpu_batch_preset_sphere(int lod)
{
  lib_assert(lod >= 0 && lod <= 2);
  lib_assert(lib_thread_is_main());

  if (lod == 0) {
    return g_presets_3d.batch.sphere_low;
  }
  if (lod == 1) {
    return g_presets_3d.batch.sphere_med;
  }

  return g_presets_3d.batch.sphere_high;
}

GPUBatch *gpu_batch_preset_sphere_wire(int lod)
{
  lib_assert(lod >= 0 && lod <= 1);
  lib_assert(lib_thread_is_main());

  if (lod == 0) {
    return g_presets_3d.batch.sphere_wire_low;
  }

  return g_presets_3d.batch.sphere_wire_med;
}

/* Create Sphere (3D) */
GPUBatch *gpu_batch_sphere(int lat_res, int lon_res)
{
  const float lon_inc = 2 * M_PI / lon_res;
  const float lat_inc = M_PI / lat_res;
  float lon, lat;

  GPUVertBuf *vbo = gpu_vertbuf_create_with_format(preset_3d_format());
  const uint vbo_len = (lat_res - 1) * lon_res * 6;
  gpu_vertbuf_data_alloc(vbo, vbo_len);

  GPUVertBufRaw pos_step, nor_step;
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_3d.attr_id.pos, &pos_step);
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_3d.attr_id.nor, &nor_step);

  lon = 0.0f;
  for (int i = 0; i < lon_res; i++, lon += lon_inc) {
    lat = 0.0f;
    for (int j = 0; j < lat_res; j++, lat += lat_inc) {
      if (j != lat_res - 1) { /* Pole */
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon + lon_inc);
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon);
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat, lon);
      }

      if (j != 0) { /* Pole */
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat, lon + lon_inc);
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon + lon_inc);
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat, lon);
      }
    }
  }

  lib_assert(vbo_len == gpu_vertbuf_raw_used(&pos_step));
  lib_assert(vbo_len == gpu_vertbuf_raw_used(&nor_step));

  return gou_batch_create_ex(GPU_PRIM_TRIS, vbo, NULL, GPU_BATCH_OWNS_VBO);
}

static GPUBatch *batch_sphere_wire(int lat_res, int lon_res)
{
  const float lon_inc = 2 * M_PI / lon_res;
  const float lat_inc = M_PI / lat_res;
  float lon, lat;

  GPUVertBuf *vbo = gpu_vertbuf_create_with_format(preset_3d_format());
  const uint vbo_len = (lat_res * lon_res * 2) + ((lat_res - 1) * lon_res * 2);
  gpu_vertbuf_data_alloc(vbo, vbo_len);

  GPUVertBufRaw pos_step, nor_step;
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_3d.attr_id.pos, &pos_step);
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_3d.attr_id.nor, &nor_step);

  lon = 0.0f;
  for (int i = 0; i < lon_res; i++, lon += lon_inc) {
    lat = 0.0f;
    for (int j = 0; j < lat_res; j++, lat += lat_inc) {
      batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon);
      batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat, lon);

      if (j != lat_res - 1) { /* Pole */
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon + lon_inc);
        batch_sphere_lat_lon_vert(&pos_step, &nor_step, lat + lat_inc, lon);
      }
    }
  }

  lib_assert(vbo_len == gpu_vertbuf_raw_used(&pos_step));
  lib_assert(vbo_len == gpu_vertbuf_raw_used(&nor_step));

  return gpu_batch_create_ex(GPU_PRIM_LINES, vbo, NULL, GPU_BATCH_OWNS_VBO);
}

/* Panel Drag Widget */
static void gpu_batch_preset_rectf_tris_color_ex(GPUVertBufRaw *pos_step,
                                                 float x1,
                                                 float y1,
                                                 float x2,
                                                 float y2,
                                                 GPUVertBufRaw *col_step,
                                                 const float color[4])
{
  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x1, y1});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);

  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x2, y1});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);

  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x2, y2});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);

  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x1, y1});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);

  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x2, y2});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);

  copy_v2_v2(gpu_vertbuf_raw_step(pos_step), (const float[2]){x1, y2});
  copy_v4_v4(gpu_vertbuf_raw_step(col_step), color);
}

static GPUBatch *gpu_batch_preset_panel_drag_widget(float pixelsize,
                                                    const float col_high[4],
                                                    const float col_dark[4],
                                                    const float width)
{
  GPUVertBuf *vbo = gpu_vertbuf_create_with_format(preset_2d_format());
  const uint vbo_len = 4 * 2 * (6 * 2);
  gpu_vertbuf_data_alloc(vbo, vbo_len);

  GPUVertBufRaw pos_step, col_step;
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_2d.attr_id.pos, &pos_step);
  gpu_vertbuf_attr_get_raw_data(vbo, g_presets_2d.attr_id.col, &col_step);

  const int px = (int)pixelsize;
  const int px_zoom = max_ii(round_fl_to_int(width / 22.0f), 1);

  const int box_margin = max_ii(round_fl_to_int((float)(px_zoom * 2.0f)), px);
  const int box_size = max_ii(round_fl_to_int((width / 8.0f) - px), px);

  const int y_ofs = max_ii(round_fl_to_int(width / 2.5f), px);
  const int x_ofs = y_ofs;
  int i_x, i_y;

  for (i_x = 0; i_x < 4; i_x++) {
    for (i_y = 0; i_y < 2; i_y++) {
      const int x_co = (x_ofs) + (i_x * (box_size + box_margin));
      const int y_co = (y_ofs) + (i_y * (box_size + box_margin));

      gpu_batch_preset_rectf_tris_color_ex(&pos_step,
                                           x_co - box_size,
                                           y_co - px_zoom,
                                           x_co,
                                           (y_co + box_size) - px_zoom,
                                           &col_step,
                                           col_dark);
      gpu_batch_preset_rectf_tris_color_ex(
          &pos_step, x_co - box_size, y_co, x_co, y_co + box_size, &col_step, col_high);
    }
  }
  return gpu_batch_create_ex(GPU_PRIM_TRIS, vbo, NULL, GPU_BATCH_OWNS_VBO);
}

GPUBatch *gpu_batch_preset_panel_drag_widget(const float pixelsize,
                                             const float col_high[4],
                                             const float col_dark[4],
                                             const float width)
{
  const bool parameters_changed = (g_presets_2d.panel_drag_widget_pixelsize != pixelsize) ||
                                  (g_presets_2d.panel_drag_widget_width != width) ||
                                  !equals_v4v4(g_presets_2d.panel_drag_widget_col_high,
                                               col_high) ||
                                  !equals_v4v4(g_presets_2d.panel_drag_widget_col_dark, col_dark);

  if (g_presets_2d.batch.panel_drag_widget && parameters_changed) {
    gpu_batch_presets_unregister(g_presets_2d.batch.panel_drag_widget);
    gpu_batch_discard(g_presets_2d.batch.panel_drag_widget);
    g_presets_2d.batch.panel_drag_widget = NULL;
  }

  if (!g_presets_2d.batch.panel_drag_widget) {
    g_presets_2d.batch.panel_drag_widget = gpu_batch_preset_panel_drag_widget(
        pixelsize, col_high, col_dark, width);
    gpu_batch_presets_register(g_presets_2d.batch.panel_drag_widget);
    g_presets_2d.panel_drag_widget_pixelsize = pixelsize;
    g_presets_2d.panel_drag_widget_width = width;
    copy_v4_v4(g_presets_2d.panel_drag_widget_col_high, col_high);
    copy_v4_v4(g_presets_2d.panel_drag_widget_col_dark, col_dark);
  }
  return g_presets_2d.batch.panel_drag_widget;
}

GPUBatch *gpu_batch_preset_quad(void)
{
  if (!g_presets_2d.batch.quad) {
    GPUVertBuf *vbo = gpu_vertbuf_create_with_format(preset_2d_format());
    gpu_vertbuf_data_alloc(vbo, 4);

    float pos_data[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    gpu_vertbuf_attr_fill(vbo, g_presets_2d.attr_id.pos, pos_data);
    /* Don't fill the color. */

    g_presets_2d.batch.quad = gpu_batch_create_ex(GPU_PRIM_TRI_FAN, vbo, NULL, GPU_BATCH_OWNS_VBO);

    gpu_batch_presets_register(g_presets_2d.batch.quad);
  }
  return g_presets_2d.batch.quad;
}

/* Preset Registration Management */
void gpu_batch_presets_init(void)
{
  lib_mutex_init(&g_presets_3d.mutex);

  /* Hard coded resolution */
  g_presets_3d.batch.sphere_low = gpu_batch_sphere(8, 16);
  gpu_batch_presets_register(g_presets_3d.batch.sphere_low);

  g_presets_3d.batch.sphere_med = gpu_batch_sphere(16, 10);
  gpu_batch_presets_register(g_presets_3d.batch.sphere_med);

  g_presets_3d.batch.sphere_high = gpu_batch_sphere(32, 24);
  gpu_batch_presets_register(g_presets_3d.batch.sphere_high);

  g_presets_3d.batch.sphere_wire_low = batch_sphere_wire(6, 8);
  gpu_batch_presets_register(g_presets_3d.batch.sphere_wire_low);

  g_presets_3d.batch.sphere_wire_med = batch_sphere_wire(8, 16);
  gpu_batch_presets_register(g_presets_3d.batch.sphere_wire_med);
}

void gpu_batch_presets_register(GPUBatch *preset_batch)
{
  lib_mutex_lock(&g_presets_3d.mutex);
  lib_addtail(&presets_list, lib_genericnoden(preset_batch));
  lib_mutex_unlock(&g_presets_3d.mutex);
}

bool gpu_batch_presets_unregister(GPUBatch *preset_batch)
{
  lib_mutex_lock(&g_presets_3d.mutex);
  for (LinkData *link = presets_list.last; link; link = link->prev) {
    if (preset_batch == link->data) {
      lib_remlink(&presets_list, link);
      lib_mutex_unlock(&g_presets_3d.mutex);
      mem_freen(link);
      return true;
    }
  }
  lib_mutex_unlock(&g_presets_3d.mutex);
  return false;
}

void gpu_batch_presets_exit(void)
{
  LinkData *link;
  while ((link = lib_pophead(&presets_list))) {
    GPUBatch *preset = link->data;
    gpu_batch_discard(preset);
    men_freen(link);
  }

  lib_mutex_end(&g_presets_3d.mutex);
}
