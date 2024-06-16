#pragma once

/* exposed internal in render module */
#include "types_ob.h"
#include "types_scene.h"

#include "lib_threads.h"

#include "dune_main.h"

#include "render_pipeline.h"

struct GHash;
struct GSet;
struct Main;
struct Ob;
struct RndrEngine;
struct ReportList;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HighlightedTile {
  rcti rect;
} HighlightedTile;

/* ctrls state of rndr, everything that's read-only during rndr stage */
struct Rndr {
  struct Rndr *next, *prev;
  char name[RE_MAXNAME];
  int slot;

  /* state settings */
  short flag, ok, result_ok;

  /* result of rndring */
  RndrResult *result;
  /* if render with single-layer option, other rendered layers are stored here */
  RndrResult *pushedresult;
  /* A list of RndrResults, for full-samples. */
  List fullresult;
  /* read/write mutex, all internal code that writes to re->result must use a
   * write lock, all external code must use a read lock. internal code is assumed
   * to not conflict with writes, so no lock used for that */
  ThreadRWMutex resultmutex;

  /* Guard for drwing rndr result using engine's `draw()` cb. */
  ThreadMutex engine_drw_mutex;

  /* Win size, display rect, viewplane.
   * Buf width and height w percentage applied
   * wo border & crop. convert to long before multiplying together to avoid overflow. */
  int winx, winy;
  rcti disprect;  /* part within winx winy */
  rctf viewplane; /* mapped on winx winy */

  /* final picture width and height (within disprect) */
  int rectx, recty;

  /* Camera transform, only used by Freestyle. */
  float winmat[4][4];

  /* Clipping. */
  float clip_start;
  float clip_end;

  /* main, scene, and its full copy of rndrdata and world */
  struct Main *main;
  Scene *scene;
  RndrData r;
  List view_layers;
  int active_view_layer;
  struct Ob *camera_override;

  ThreadMutex highlighted_tiles_mutex;
  struct GSet *highlighted_tiles;

  /* render engine */
  struct RndrEngine *engine;

  /* NOTE: This is a minimal dep graph and evald scene which is enough to access view
   * layer visibility and use for postprocessing (compositor and sequencer). */
  Graph *pipeline_graph;
  Scene *pipeline_scene_eval;

  /* cbs */
  void (*display_init)(void *handle, RndrResult *rr);
  void *dih;
  void (*display_clear)(void *handle, RndrResult *rr);
  void *dch;
  void (*display_update)(void *handle, RndrResult *rr, rcti *rect);
  void *duh;
  void (*current_scene_update)(void *handle, struct Scene *scene);
  void *suh;

  void (*stats_drw)(void *handle, RndrStats *ri);
  void *sdh;
  void (*progress)(void *handle, float i);
  void *prh;

  void (*drw_lock)(void *handle, bool lock);
  void *dlh;
  int (*test_break)(void *handle);
  void *tbh;

  RndrStats i;

  struct ReportList *reports;

  void **movie_ctx_arr;
  char viewname[MAX_NAME];

  /* TODO: replace by a whole drw manager. */
  void *gl_cxt;
  void *gpu_cxt;
};

/* defines */

/* R.flag */
#define R_ANIMATION 1

#ifdef __cplusplus
}
#endif
