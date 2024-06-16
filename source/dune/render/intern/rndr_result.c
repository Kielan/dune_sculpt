#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_ghash.h"
#include "lib_hash_md5.h"
#include "lib_list.h"
#include "lib_path_util.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_string_utils.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "dune_appdir.h"
#include "dune_camera.h"
#include "dune_global.h"
#include "dune_img.h"
#include "dune_img_format.h"
#include "dune_img_save.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "imbuf_colormanagement.h"
#include "imbuf.h"
#include "imbuf_types.h"
#include "imbuf_openexr.h"

#include "rndr_engine.h"

#include "rndr_result.h"
#include "rndr_types.h"

/* Free */

static void rndr_result_views_free(RndrResult *rr)
{
  while (rr->views.first) {
    RndrView *rv = rr->views.first;
    lib_remlink(&rr->views, rv);

    if (rv->rect32) {
      mem_freen(rv->rect32);
    }

    if (rv->rectz) {
      mem_freen(rv->rectz);
    }

    if (rv->rectf) {
      mem_freen(rv->rectf);
    }

    mem_freen(rv);
  }

  rr->have_combined = false;
}

void rndr_result_free(RndrResult *rr)
{
  if (rr == NULL) {
    return;
  }

  while (rr->layers.first) {
    RndrLayer *rl = rr->layers.first;

    while (rl->passes.first) {
      RndrPass *rpass = rl->passes.first;
      if (rpass->rect) {
        mem_freen(rpass->rect);
      }
      lib_remlink(&rl->passes, rpass);
      mem_freen(rpass);
    }
    lib_remlink(&rr->layers, rl);
    mem_freen(rl);
  }

  rndr_result_views_free(rr);

  if (rr->rect32) {
    mem_freen(rr->rect32);
  }
  if (rr->rectz) {
    mem_freen(rr->rectz);
  }
  if (rr->rectf) {
    mem_freen(rr->rectf);
  }
  if (rr->txt) {
    mem_freen(rr->txt);
  }
  if (rr->err) {
    mem_freen(rr->err);
  }

  dune_stamp_data_free(rr->stamp_data);

  mem_freen(rr);
}

void rndr_result_free_list(List *lb, RndrResult *rr)
{
  RndrResult *rrnext;

  for (; rr; rr = rrnext) {
    rrnext = rr->next;

    if (lb && lb->first) {
      lib_remlink(lb, rr);
    }

    rndr_result_free(rr);
  }
}

/* multiview */
void rndr_result_views_shallowcopy(RndrResult *dst, RndrResult *src)
{
  RndrView *rview;

  if (dst == NULL || src == NULL) {
    return;
  }

  for (rview = src->views.first; rview; rview = rview->next) {
    RndrView *rv;

    rv = mem_mallocn(sizeof(RndrView), "new render view");
    lib_addtail(&dst->views, rv);

    lib_strncpy(rv->name, rview->name, sizeof(rv->name));
    rv->rectf = rview->rectf;
    rv->rectz = rview->rectz;
    rv->rect32 = rview->rect32;
  }
}

void rndr_result_views_shallowdelete(RenderResult *rr)
{
  if (rr == NULL) {
    return;
  }

  while (rr->views.first) {
    RndrView *rv = rr->views.first;
    lib_remlink(&rr->views, rv);
    mem_freen(rv);
  }
}

/* New */
static void rndr_layer_alloc_pass(RndrResult *rr, RndrPass *rp)
{
  if (rp->rect != NULL) {
    return;
  }

  const size_t rectsize = ((size_t)rr->rectx) * rr->recty * rp->channels;
  rp->rect = mem_callocn(sizeof(float) * rectsize, rp->name);

  if (STREQ(rp->name, RE_PASSNAME_VECTOR)) {
    /* init to max speed */
    float *rect = rp->rect;
    for (int x = rectsize - 1; x >= 0; x--) {
      rect[x] = PASS_VECTOR_MAX;
    }
  }
  else if (STREQ(rp->name, RE_PASSNAME_Z)) {
    float *rect = rp->rect;
    for (int x = rectsize - 1; x >= 0; x--) {
      rect[x] = 10e10;
    }
  }
}

RenderPass *rndr_layer_add_pass(RndrResult *rr,
                                RenderLayer *rl,
                                int channels,
                                const char *name,
                                const char *viewname,
                                const char *chan_id,
                                const bool alloc)
{
  const int view_id = lib_findstringindex(&rr->views, viewname, offsetof(RenderView, name));
  RndrPass *rpass = mem_callocn(sizeof(RndrPass), name);

  rpass->channels = channels;
  rpass->rectx = rl->rectx;
  rpass->recty = rl->recty;
  rpass->view_id = view_id;

  lib_strncpy(rpass->name, name, sizeof(rpass->name));
  lib_strncpy(rpass->chan_id, chan_id, sizeof(rpass->chan_id));
  lib_strncpy(rpass->view, viewname, sizeof(rpass->view));
  render_result_full_channel_name(
      rpass->fullname, NULL, rpass->name, rpass->view, rpass->chan_id, -1);

  if (rl->exrhandle) {
    int a;
    for (a = 0; a < channels; a++) {
      char passname[EXR_PASS_MAXNAME];
      rndr_result_full_channel_name(passname, NULL, rpass->name, NULL, rpass->chan_id, a);
      imbuf_exr_add_channel(rl->exrhandle, rl->name, passname, viewname, 0, 0, NULL, false);
    }
  }

  lib_addtail(&rl->passes, rpass);

  if (alloc) {
    rndr_layer_allocate_pass(rr, rpass);
  }
  else {
    /* The result contains non-alloc'd pass now, so tag it as such. */
    rr->passes_allocated = false;
  }

  return rpass;
}

RndrResult *rndr_result_new(Rndr *re,
                            rcti *partrct,
                            const char *layername,
                            const char *viewname)
{
  RndrResult *rr;
  RndrLayer *rl;
  RndrView *rv;
  int rectx, recty;

  rectx = lib_rcti_size_x(partrct);
  recty = lib_rcti_size_y(partrct);

  if (rectx <= 0 || recty <= 0) {
    return NULL;
  }

  rr = mem_callocn(sizeof(RndrResult), "new render result");
  rr->rectx = rectx;
  rr->recty = recty;
  rr->renrect.xmin = 0;
  rr->renrect.xmax = rectx;

  /* tilerect is relative coords w/in render disprect. do not subtract crop yet */
  rr->tilerect.xmin = partrct->xmin - re->disprect.xmin;
  rr->tilerect.xmax = partrct->xmax - re->disprect.xmin;
  rr->tilerect.ymin = partrct->ymin - re->disprect.ymin;
  rr->tilerect.ymax = partrct->ymax - re->disprect.ymin;

  rr->passes_allocated = false;

  rndr_result_views_new(rr, &re->r);

  /* check renderdata for amount of layers */
  FOREACH_VIEW_LAYER_TO_RNDR_BEGIN (re, view_layer) {
    if (layername && layername[0]) {
      if (!STREQ(view_layer->name, layername)) {
        continue;
      }
    }

    rl = mem_callocn(sizeof(RndrLayer), "new render layer");
    lib_addtail(&rr->layers, rl);

    lib_strncpy(rl->name, view_layer->name, sizeof(rl->name));
    rl->layflag = view_layer->layflag;

    rl->passflag = view_layer->passflag;

    rl->rectx = rectx;
    rl->recty = recty;

    for (rv = rr->views.first; rv; rv = rv->next) {
      const char *view = rv->name;

      if (viewname && viewname[0]) {
        if (!STREQ(view, viewname)) {
          continue;
        }
      }

#define RNDR_LAYER_ADD_PASS_SAFE(rr, rl, channels, name, viewname, chan_id) \
  do { \
    if (rndr_layer_add_pass(rr, rl, channels, name, viewname, chan_id, false) == NULL) { \
      rndr_result_free(rr); \
      return NULL; \
    } \
  } while (false)

      /* A rndrlayer should always have a Combined pass. */
      rndr_layer_add_pass(rr, rl, 4, "Combined", view, "RGBA", false);

      if (view_layer->passflag & SCE_PASS_Z) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 1, RE_PASSNAME_Z, view, "Z");
      }
      if (view_layer->passflag & SCE_PASS_VECTOR) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 4, RE_PASSNAME_VECTOR, view, "XYZW");
      }
      if (view_layer->passflag & SCE_PASS_NORMAL) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_NORMAL, view, "XYZ");
      }
      if (view_layer->passflag & SCE_PASS_POSITION) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_POSITION, view, "XYZ");
      }
      if (view_layer->passflag & SCE_PASS_UV) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_UV, view, "UVA");
      }
      if (view_layer->passflag & SCE_PASS_EMIT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_EMIT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_AO) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_AO, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_ENVIRONMENT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_ENVIRONMENT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_SHADOW) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_SHADOW, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_INDEXOB) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 1, RE_PASSNAME_INDEXOB, view, "X");
      }
      if (view_layer->passflag & SCE_PASS_INDEXMA) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 1, RE_PASSNAME_INDEXMA, view, "X");
      }
      if (view_layer->passflag & SCE_PASS_MIST) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 1, RE_PASSNAME_MIST, view, "Z");
      }
      if (view_layer->passflag & SCE_PASS_DIFFUSE_DIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_DIFFUSE_DIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_DIFFUSE_INDIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_DIFFUSE_INDIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_DIFFUSE_COLOR) {
       RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_DIFFUSE_COLOR, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_GLOSSY_DIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_GLOSSY_DIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_GLOSSY_INDIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_GLOSSY_INDIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_GLOSSY_COLOR) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_GLOSSY_COLOR, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_TRANSM_DIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_TRANSM_DIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_TRANSM_INDIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_TRANSM_INDIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_TRANSM_COLOR) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_TRANSM_COLOR, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_SUBSURFACE_DIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_SUBSURFACE_DIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_SUBSURFACE_INDIRECT) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_SUBSURFACE_INDIRECT, view, "RGB");
      }
      if (view_layer->passflag & SCE_PASS_SUBSURFACE_COLOR) {
        RNDR_LAYER_ADD_PASS_SAFE(rr, rl, 3, RE_PASSNAME_SUBSURFACE_COLOR, view, "RGB");
      }
#undef RNDR_LAYER_ADD_PASS_SAFE
    }
  }
  FOREACH_VIEW_LAYER_TO_RENDER_END;

  /* Preview-render doesn't do layers, so we make a default one. */
  if (lib_list_is_empty(&rr->layers) && !(layername && layername[0])) {
    rl = mem_callocn(sizeof(RenderLayer), "new render layer");
    lib_addtail(&rr->layers, rl);

    rl->rectx = rectx;
    rl->recty = recty;

    for (rv = rr->views.first; rv; rv = rv->next) {
      const char *view = rv->name;

      if (viewname && viewname[0]) {
        if (!STREQ(view, viewname)) {
          continue;
        }
      }

      /* a rndrlayer should always have a Combined pass */
      rndr_layer_add_pass(rr, rl, 4, RE_PASSNAME_COMBINED, view, "RGBA", false);
    }

    /* NOTE: this has to be in sync with `scene.c`. */
    rl->layflag = SCE_LAY_FLAG_DEFAULT;
    rl->passflag = SCE_PASS_COMBINED;

    re->active_view_layer = 0;
  }

  /* Border render; calc offset for use in compositor. compo is centralized coords. */
  /* XXX: obsolete? I now use it for drwing border rndr offset. */
  rr->xof = re->disprect.xmin + lib_rcti_cent_x(&re->disprect) - (re->winx / 2);
  rr->yof = re->disprect.ymin + lib_rcti_cent_y(&re->disprect) - (re->winy / 2);

  /* Preview does not support deferred rndr result alloc. */
  if (re->r.scemode & R_BUTS_PREVIEW) {
    rndr_result_passes_alloc_ensure(rr);
  }

  return rr;
}

void rndr_result_passes_alloc_ensure(RndrResult *rr)
{
  if (rr == NULL) {
    /* Happens when the result was not yet alloc for the current scene or slot config. */
    return;
  }

  LIST_FOREACH (RndrLayer *, rl, &rr->layers) {
    LIST_FOREACH (RndrPass *, rp, &rl->passes) {
      if (rl->exrhandle != NULL && !STREQ(rp->name, RE_PASSNAME_COMBINED)) {
        continue;
      }

      rndr_layer_alloc_pass(rr, rp);
    }
  }

  rr->passes_alloc = true;
}

void rndr_result_clone_passes(Rndr *re, RndrResult *rr, const char *viewname)
{
  RndrLayer *rl;
  RndrPass *main_rp;

  for (rl = rr->layers.first; rl; rl = rl->next) {
    RndrLayer *main_rl = lib_findstring(
        &re->result->layers, rl->name, offsetof(RndrLayer, name));
    if (!main_rl) {
      continue;
    }

    for (main_rp = main_rl->passes.first; main_rp; main_rp = main_rp->next) {
      if (viewname && viewname[0] && !STREQ(main_rp->view, viewname)) {
        continue;
      }

      /* Compare fullname to make sure that the view also is equal. */
      RndrPass *rp = lib_findstring(
          &rl->passes, main_rp->fullname, offsetof(RndrPass, fullname));
      if (!rp) {
        rndr_layer_add_pass(
            rr, rl, main_rp->channels, main_rp->name, main_rp->view, main_rp->chan_id, false);
      }
    }
  }
}

void rndr_create_rndr_pass(RndrResult *rr,
                           const char *name,
                           int channels,
                           const char *chan_id,
                           const char *layername,
                           const char *viewname,
                           const bool alloc)
{
  RndrLayer *rl;
  RndrPass *rp;
  RndrView *rv;

  for (rl = rr->layers.first; rl; rl = rl->next) {
    if (layername && layername[0] && !STREQ(rl->name, layername)) {
      continue;
    }

    for (rv = rr->views.first; rv; rv = rv->next) {
      const char *view = rv->name;

      if (viewname && viewname[0] && !STREQ(view, viewname)) {
        continue;
      }

      /* Ensure that the pass doesn't exist yet. */
      for (rp = rl->passes.first; rp; rp = rp->next) {
        if (!STREQ(rp->name, name)) {
          continue;
        }
        if (!STREQ(rp->view, view)) {
          continue;
        }
        break;
      }

      if (!rp) {
        rndr_layer_add_pass(rr, rl, channels, name, view, chan_id, alloc);
      }
    }
  }
}

void rndr_result_full_channel_name(char *fullname,
                                   const char *layname,
                                   const char *passname,
                                   const char *viewname,
                                   const char *chan_id,
                                   const int channel)
{
  /* OpenEXR compatible full channel name. */
  const char *strings[4];
  int strings_len = 0;

  if (layname && layname[0]) {
    strings[strings_len++] = layname;
  }
  if (passname && passname[0]) {
    strings[strings_len++] = passname;
  }
  if (viewname && viewname[0]) {
    strings[strings_len++] = viewname;
  }

  char token[2];
  if (channel >= 0) {
    ARRAY_SET_ITEMS(token, chan_id[channel], '\0');
    strings[strings_len++] = token;
  }

  lib_string_join_array_by_sep_char(fullname, EXR_PASS_MAXNAME, '.', strings, strings_len);
}

static int passtype_from_name(const char *name)
{
  const char delim[] = {'.', '\0'};
  const char *sep, *suf;
  int len = lib_str_partition(name, delim, &sep, &suf);

#define CHECK_PASS(NAME) \
  if (STREQLEN(name, RE_PASSNAME_##NAME, len)) { \
    return SCE_PASS_##NAME; \
  } \
  ((void)0)

  CHECK_PASS(COMBINED);
  CHECK_PASS(Z);
  CHECK_PASS(VECTOR);
  CHECK_PASS(NORMAL);
  CHECK_PASS(UV);
  CHECK_PASS(EMIT);
  CHECK_PASS(SHADOW);
  CHECK_PASS(AO);
  CHECK_PASS(ENVIRONMENT);
  CHECK_PASS(INDEXOB);
  CHECK_PASS(INDEXMA);
  CHECK_PASS(MIST);
  CHECK_PASS(DIFFUSE_DIRECT);
  CHECK_PASS(DIFFUSE_INDIRECT);
  CHECK_PASS(DIFFUSE_COLOR);
  CHECK_PASS(GLOSSY_DIRECT);
  CHECK_PASS(GLOSSY_INDIRECT);
  CHECK_PASS(GLOSSY_COLOR);
  CHECK_PASS(TRANSM_DIRECT);
  CHECK_PASS(TRANSM_INDIRECT);
  CHECK_PASS(TRANSM_COLOR);
  CHECK_PASS(SUBSURFACE_DIRECT);
  CHECK_PASS(SUBSURFACE_INDIRECT);
  CHECK_PASS(SUBSURFACE_COLOR);

#undef CHECK_PASS
  return 0;
}

/* cbs for rndr_result_new_from_exr */
static void *ml_addlayer_cb(void *base, const char *str)
{
  RndrResult *rr = base;
  RndrLayer *rl;

  rl = mem_callocn(sizeof(RndrLayer), "new render layer");
  lib_addtail(&rr->layers, rl);

  lib_strncpy(rl->name, str, EXR_LAY_MAXNAME);
  return rl;
}

static void ml_addpass_cb(void *base,
                          void *lay,
                          const char *name,
                          float *rect,
                          int totchan,
                          const char *chan_id,
                          const char *view)
{
  RndrResult *rr = base;
  RndrLayer *rl = lay;
  RndrPass *rpass = mem_callocn(sizeof(RndrPass), "loaded pass");

  lib_addtail(&rl->passes, rpass);
  rpass->channels = totchan;
  rl->passflag |= passtype_from_name(name);

  /* channel id chars */
  lib_strncpy(rpass->chan_id, chan_id, sizeof(rpass->chan_id));

  rpass->rect = rect;
  lib_strncpy(rpass->name, name, EXR_PASS_MAXNAME);
  lib_strncpy(rpass->view, view, sizeof(rpass->view));
  rndr_result_full_channel_name(rpass->fullname, NULL, name, view, rpass->chan_id, -1);

  if (view[0] != '\0') {
    rpass->view_id = lib_findstringindex(&rr->views, view, offsetof(RndrView, name));
  }
  else {
    rpass->view_id = 0;
  }
}

static void *ml_addview_cb(void *base, const char *str)
{
  RndrResult *rr = base;
  RndrView *rv;

  rv = mem_callocn(sizeof(RndrView), "new render view");
  lib_strncpy(rv->name, str, EXR_VIEW_MAXNAME);

  /* For stereo drawing we need to ensure:
   * STEREO_LEFT_NAME  == STEREO_LEFT_ID and
   * STEREO_RIGHT_NAME == STEREO_RIGHT_ID */

  if (STREQ(str, STEREO_LEFT_NAME)) {
    lib_addhead(&rr->views, rv);
  }
  else if (STREQ(str, STEREO_RIGHT_NAME)) {
    RndrView *left_rv = lib_findstring(&rr->views, STEREO_LEFT_NAME, offsetof(RndrView, name));

    if (left_rv == NULL) {
      lib_addhead(&rr->views, rv);
    }
    else {
      lib_insertlinkafter(&rr->views, left_rv, rv);
    }
  }
  else {
    lib_addtail(&rr->views, rv);
  }

  return rv;
}

static int order_rndr_passes(const void *a, const void *b)
{
  /* 1 if `a` is after `b`. */
  RndrPass *rpa = (RndrPass *)a;
  RndrPass *rpb = (RndrPass *)b;
  unsigned int passtype_a = passtype_from_name(rpa->name);
  unsigned int passtype_b = passtype_from_name(rpb->name);

  /* Rndr passes with default type always go first. */
  if (passtype_b && !passtype_a) {
    return 1;
  }
  if (passtype_a && !passtype_b) {
    return 0;
  }

  if (passtype_a && passtype_b) {
    if (passtype_a > passtype_b) {
      return 1;
    }
    if (passtype_a < passtype_b) {
      return 0;
    }
  }
  else {
    int cmp = strncmp(rpa->name, rpb->name, EXR_PASS_MAXNAME);
    if (cmp > 0) {
      return 1;
    }
    if (cmp < 0) {
      return 0;
    }
  }

  /* they have the same type */
  /* left first */
  if (STREQ(rpa->view, STEREO_LEFT_NAME)) {
    return 0;
  }
  if (STREQ(rpb->view, STEREO_LEFT_NAME)) {
    return 1;
  }

  /* right second */
  if (STREQ(rpa->view, STEREO_RIGHT_NAME)) {
    return 0;
  }
  if (STREQ(rpb->view, STEREO_RIGHT_NAME)) {
    return 1;
  }

  /* remaining in ascending id order */
  return (rpa->view_id < rpb->view_id);
}

RndrResult *rndr_result_new_from_exr(
    void *exrhandle, const char *colorspace, bool predivide, int rectx, int recty)
{
  RndrResult *rr = mem_callocn(sizeof(RndrResult), __func__);
  RndrLayer *rl;
  RndrPass *rpass;
  const char *to_colorspace =imbuf_colormanagement_role_colorspace_name_get(
      COLOR_ROLE_SCENE_LINEAR);

  rr->rectx = rectx;
  rr->recty = recty;

  imbuf_exr_multilayer_convert(exrhandle, rr, ml_addview_cb, ml_addlayer_cb, ml_addpass_cb);

  for (rl = rr->layers.first; rl; rl = rl->next) {
    rl->rectx = rectx;
    rl->recty = recty;

    lib_list_sort(&rl->passes, order_render_passes);

    for (rpass = rl->passes.first; rpass; rpass = rpass->next) {
      rpass->rectx = rectx;
      rpass->recty = recty;

      if (rpass->channels >= 3) {
        imbuf_colormanagement_transform(rpass->rect,
                                        rpass->rectx,
                                        rpass->recty,
                                        rpass->channels,
                                      colorspace,
                                      to_colorspace,
                                      predivide);
      }
    }
  }

  return rr;
}

void rndr_result_view_new(RndrResult *rr, const char *viewname)
{
  RndrView *rv = mem_callocn(sizeof(RndrView), "new render view");
  lib_addtail(&rr->views, rv);
  lib_strncpy(rv->name, viewname, sizeof(rv->name));
}

void rndr_result_views_new(RndrResult *rr, const RndrData *rd)
{
  SceneRndrView *srv;

  /* clear prev existing views - for seq */
  rndr_result_views_free(rr);

  /* check rndrdata for amount of views */
  if (rd->scemode & R_MULTIVIEW) {
    for (srv = rd->views.first; srv; srv = srv->next) {
      if (dune_scene_multiview_is_rndr_view_active(rd, srv) == false) {
        continue;
      }
      rndr_result_view_new(rr, srv->name);
    }
  }

  /* we always need at least one view */
  if (lib_list_count_at_most(&rr->views, 1) == 0) {
    rndr_result_view_new(rr, "");
  }
}

/* Merge */

static void do_merge_tile(
    RndrResult *rr, RndrResult *rrpart, float *target, float *tile, int pixsize)
{
  int y, tilex, tiley;
  size_t ofs, copylen;

  copylen = tilex = rrpart->rectx;
  tiley = rrpart->recty;

  ofs = (((size_t)rrpart->tilerect.ymin) * rr->rectx + rrpart->tilerect.xmin);
  target += pixsize * ofs;

  copylen *= sizeof(float) * pixsize;
  tilex *= pixsize;
  ofs = pixsize * rr->rectx;

  for (y = 0; y < tiley; y++) {
    memcpy(target, tile, copylen);
    target += ofs;
    tile += tilex;
  }
}

void rndr_result_merge(RndrResult *rr, RndrResult *rrpart)
{
  RndrLayer *rl, *rlp;
  RndrPass *rpass, *rpassp;

  for (rl = rr->layers.first; rl; rl = rl->next) {
    rlp = rndr_GetRndrLayer(rrpart, rl->name);
    if (rlp) {
      /* Passes are alloc'd in sync. */
      for (rpass = rl->passes.first, rpassp = rlp->passes.first; rpass && rpassp;
           rpass = rpass->next) {
        /* For save bufs, skip any passes that are only saved to disk. */
        if (rpass->rect == NULL || rpassp->rect == NULL) {
          continue;
        }
        /* Rndrresult have all passes, rndrpart only the active view's passes. */
        if (!STREQ(rpassp->fullname, rpass->fullname)) {
          continue;
        }

        do_merge_tile(rr, rrpart, rpass->rect, rpassp->rect, rpass->channels);

        /* manually get next rndr pass */
        rpassp = rpassp->next;
      }
    }
  }
}

/* Single Layer Rendering */
void rndr_result_single_layer_begin(Rndr *re)
{
  /* all layers except the active one get tmp pushed away */

  /* officially pushed result should be NULL... error can happen w do_seq */
  rndr_FreeRndrResult(re->pushedresult);

  re->pushedresult = re->result;
  re->result = NULL;
}

void rndr_result_single_layer_end(Rndr *re)
{
  ViewLayer *view_layer;
  RndrLayer *rlpush;
  RndrLayer *rl;
  int nr;

  if (re->result == NULL) {
    printf("pop render result error; no current result!\n");
    return;
  }

  if (!re->pushedresult) {
    return;
  }

  if (re->pushedresult->rectx == re->result->rectx &&
      re->pushedresult->recty == re->result->recty) {
    /* find which layer in re->pushedresult should be replaced */
    rl = re->result->layers.first;

    /* rndr result should be empty after this */
    lib_remlink(&re->result->layers, rl);

    /* reconstruct rndr result layers */
    for (nr = 0, view_layer = re->view_layers.first; view_layer;
         view_layer = view_layer->next, nr++) {
      if (nr == re->active_view_layer) {
        lib_addtail(&re->result->layers, rl);
      }
      else {
        rlpush = rndr_GetRndrLayer(re->pushedresult, view_layer->name);
        if (rlpush) {
          lib_remlink(&re->pushedresult->layers, rlpush);
          lib_addtail(&re->result->layers, rlpush);
        }
      }
    }
  }

  rndr_FreeRndrResult(re->pushedresult);
  re->pushedresult = NULL;
}

int rndr_result_exr_file_read_path(RndrResult *rr,
                                   RndrLayer *rl_single,
                                   const char *filepath)
{
  RndrLayer *rl;
  RndrPass *rpass;
  void *exrhandle = imbuf_exr_get_handle();
  int rectx, recty;

  if (!imbuf_exr_begin_read(exrhandle, filepath, &rectx, &recty, false)) {
    printf("failed being read %s\n", filepath);
    imbuf_exr_close(exrhandle);
    return 0;
  }

  if (rr == NULL || rectx != rr->rectx || recty != rr->recty) {
    if (rr) {
      printf("err in reading render result: dimensions don't match\n");
    }
    else {
      printf("err in reading render result: NULL result ptr\n");
    }
    imbuf_exr_close(exrhandle);
    return 0;
  }

  for (rl = rr->layers.first; rl; rl = rl->next) {
    if (rl_single && rl_single != rl) {
      continue;
    }

    /* passes are alloc in sync */
    for (rpass = rl->passes.first; rpass; rpass = rpass->next) {
      const int xstride = rpass->channels;
      int a;
      char fullname[EXR_PASS_MAXNAME];

      for (a = 0; a < xstride; a++) {
        rndr_result_full_channel_name(
            fullname, NULL, rpass->name, rpass->view, rpass->chan_id, a);
        imbuf_exr_set_channel(
            exrhandle, rl->name, fullname, xstride, xstride * rectx, rpass->rect + a);
      }

      rndr_result_full_channel_name(
          rpass->fullname, NULL, rpass->name, rpass->view, rpass->chan_id, -1);
    }
  }

  imbuf_exr_read_channels(exrhandle);
  imbuf_exr_close(exrhandle);

  return 1;
}

static void rndr_result_exr_file_cache_path(Scene *sce, const char *root, char *r_path)
{
  char filename_full[FILE_MAX + MAX_ID_NAME + 100], filename[FILE_MAXFILE], dirname[FILE_MAXDIR];
  char path_digest[16] = {0};
  char path_hexdigest[33];

  /* If root is relative, use either current .dune file dir, or temp one if not saved. */
  const char *dunefile_path = dune_main_dunefile_path_from_global();
  if (dunefile_path[0] != '\0') {
    lib_split_dirfile(dunefile_path, dirname, filename, sizeof(dirname), sizeof(filename));
    lib_path_extension_replace(filename, sizeof(filename), ""); /* strip '.dune' */
    lib_hash_md5_buf(dunefile_path, strlen(dunefile_path), path_digest);
  }
  else {
    lib_strncpy(dirname, dune_tmpdir_base(), sizeof(dirname));
    lib_strncpy(filename, "UNSAVED", sizeof(filename));
  }
  lib_hash_md5_to_hexdigest(path_digest, path_hexdigest);

  /* Default to *non-volatile* tmp dir. */
  if (*root == '\0') {
    root = dune_tempdir_base();
  }

  lib_snprintf(filename_full,
               sizeof(filename_full),
               "cached_RR_%s_%s_%s.exr",
               filename,
               sce->id.name + 2,
               path_hexdigest);
  lib_make_file_string(dirname, r_path, root, filename_full);
}

void rndr_result_exr_file_cache_write(Rndr *re)
{
  RndrResult *rr = re->result;
  char str[FILE_MAXFILE + FILE_MAXFILE + MAX_ID_NAME + 100];
  char *root = U.rndr_cachedir;

  rndr_result_exr_file_cache_path(re->scene, root, str);
  printf("Caching exr file, %dx%d, %s\n", rr->rectx, rr->recty, str);

  dune_img_rndr_write_exr(NULL, rr, str, NULL, NULL, -1);
}

bool rndr_result_exr_file_cache_read(Rndr *re)
{
  /* File path to cache. */
  char filepath[FILE_MAXFILE + MAX_ID_NAME + MAX_ID_NAME + 100] = "";
  char *root = U.rndr_cachedir;
  rndr_result_exr_file_cache_path(re->scene, root, filepath);

  printf("read exr cache file: %s\n", filepath);

  /* Try opening the file. */
  void *exrhandle = imbuf_exr_get_handle();
  int rectx, recty;

  if (!imbuf_exr_begin_read(exrhandle, filepath, &rectx, &recty, true)) {
    printf("cannot read: %s\n", filepath);
    imbuf_exr_close(exrhandle);
    return false;
  }

  /* Read file contents into rndr result. */
  const char *colorspace = imbuf_colormanagement_role_colorspace_name_get(COLOR_ROLE_SCENE_LINEAR);
  rndr_FreeRndrResult(re->result);

  imbuf_exr_read_channels(exrhandle);
  re->result = rndr_result_new_from_exr(exrhandle, colorspace, false, rectx, recty);

  imbuf_exr_close(exrhandle);

  return true;
}

/* Combined Pixel Rect */

ImBuf *rndr_result_rect_to_ibuf(RndrResult *rr,
                                const ImgFormatData *imf,
                                const float dither,
                                const int view_id)
{
  ImBuf *ibuf = imbuf_allocImBuf(rr->rectx, rr->recty, imf->planes, 0);
  RndrView *rv = rndr_RndrViewGetById(rr, view_id);

  /* if not exists, dune_imbuf_write makes one */
  ibuf->rect = (unsigned int *)rv->rect32;
  ibuf->rect_float = rv->rectf;
  ibuf->zbuf_float = rv->rectz;

  /* float factor for random dither, imbuf takes care of it */
  ibuf->dither = dither;

  /* prepare to gamma correct to sRGB color space
   * note that seq editor can gen 8bpc rndr bufs */
  if (ibuf->rect) {
    if (dune_imtype_valid_depths(imf->imtype) &
        (R_IMF_CHAN_DEPTH_12 | R_IMF_CHAN_DEPTH_16 | R_IMF_CHAN_DEPTH_24 | R_IMF_CHAN_DEPTH_32)) {
      if (imf->depth == R_IMF_CHAN_DEPTH_8) {
        /* Higher depth bits are supported but not needed for current file output. */
        ibuf->rect_float = NULL;
      }
      else {
        imbuf_float_from_rect(ibuf);
      }
    }
    else {
      /* ensure no float buf remained from prev frame */
      ibuf->rect_float = NULL;
    }
  }

  /* Color -> gray-scale. */
  /* editing directly would alter the rndr view */
  if (imf->planes == R_IMF_PLANES_BW) {
    ImBuf *ibuf_bw = imbuf_dupImBuf(ibuf);
    imbuf_color_to_bw(ibuf_bw);
    imbuf_freeImBuf(ibuf);
    ibuf = ibuf_bw;
  }

  return ibuf;
}

void rndr_result_rect_from_ibuf(RndrResult *rr, const ImBuf *ibuf, const int view_id)
{
  RndrView *rv = rndr_RndrViewGetById(rr, view_id);

  if (ibuf->rect_float) {
    rr->have_combined = true;

    if (!rv->rectf) {
      rv->rectf = mem_mallocn(sizeof(float[4]) * rr->rectx * rr->recty, "rndr_seq rectf");
    }

    memcpy(rv->rectf, ibuf->rect_float, sizeof(float[4]) * rr->rectx * rr->recty);

    /* TSK! Since seq rndr doesn't free the *rr rndr result, the old rect32
     * can hang around when seq rndr has rndred a 32 bits one before */
    MEM_SAFE_FREE(rv->rect32);
  }
  else if (ibuf->rect) {
    rr->have_combined = true;

    if (!rv->rect32) {
      rv->rect32 = mem_malloc(sizeof(int) * rr->rectx * rr->recty, "rndr_seq rect");
    }

    memcpy(rv->rect32, ibuf->rect, 4 * rr->rectx * rr->recty);

    /* Same things as above, old rectf can hang around from prev rndr. */
    MEM_SAFE_FREE(rv->rectf);
  }
}
void rndr_result_rect_fill_zero(RndrResult *rr, const int view_id)
{
  RndrView *rv = rndr_RndrViewGetById(rr, view_id);

  if (rv->rectf) {
    memset(rv->rectf, 0, sizeof(float[4]) * rr->rectx * rr->recty);
  }
  else if (rv->rect32) {
    memset(rv->rect32, 0, 4 * rr->rectx * rr->recty);
  }
  else {
    rv->rect32 = mem_callocn(sizeof(int) * rr->rectx * rr->recty, "rndr_seq rect");
  }
}

void rndr_result_rect_get_pixels(RndrResult *rr,
                                 unsigned int *rect,
                                 int rectx,
                                 int recty,
                                 const ColorManagedViewSettings *view_settings,
                                 const ColorManagedDisplaySettings *display_settings,
                                 const int view_id)
{
  RndrView *rv = rndr_RenderViewGetById(rr, view_id);

  if (rv && rv->rect32) {
    memcpy(rect, rv->rect32, sizeof(int) * rr->rectx * rr->recty);
  }
  else if (rv && rv->rectf) {
    imbuf_display_buf_transform_apply((unsigned char *)rect,
                                       rv->rectf,
                                       rr->rectx,
                                       rr->recty,
                                       4,
                                       view_settings,
                                       display_settings,
                                       true);
  }
  else {
    /* else fill w black */
    memset(rect, 0, sizeof(int) * rectx * recty);
  }
}

/* multiview fns */
bool rndr_HasCombinedLayer(const RndrResult *rr)
{
  if (rr == NULL) {
    return false;
  }

  const RndrView *rv = rr->views.first;
  if (rv == NULL) {
    return false;
  }

  return (rv->rect32 || rv->rectf);
}

bool rndr_HasFloatPixels(const RndrResult *rr)
{
  for (const RndrView *rview = rr->views.first; rview; rview = rview->next) {
    if (rview->rect32 && !rview->rectf) {
      return false;
    }
  }

  return true;
}

bool rndr_RndrResult_is_stereo(const RndrResult *rr)
{
  if (!lib_findstring(&rr->views, STEREO_LEFT_NAME, offsetof(RndrView, name))) {
    return false;
  }

  if (!lib_findstring(&rr->views, STEREO_RIGHT_NAME, offsetof(RndrView, name))) {
    return false;
  }

  return true;
}

RndrView *rndr_RndrViewGetById(RndrResult *rr, const int view_id)
{
  RndrView *rv = lib_findlink(&rr->views, view_id);
  lib_assert(rr->views.first);
  return rv ? rv : rr->views.first;
}

RndrView *rndr_RndrViewGetByName(RndrResult *rr, const char *viewname)
{
  RndrView *rv = lib_findstring(&rr->views, viewname, offsetof(RndarView, name));
  lib_assert(rr->views.first);
  return rv ? rv : rr->views.first;
}

static RndrPass *dup_rndr_pass(RndrPass *rpass)
{
  RndrPass *new_rpass = mem_mallocn(sizeof(RndrPass), "new rndr pass");
  *new_rpass = *rpass;
  new_rpass->next = new_rpass->prev = NULL;
  if (new_rpass->rect != NULL) {
    new_rpass->rect = mem_dupallocn(new_rpass->rect);
  }
  return new_rpass;
}

static RndrLayer *dup_rndr_layer(RndrLayer *rl)
{
  RndrLayer *new_rl = mem_mallocn(sizeof(RndrLayer), "new rndr layer");
  *new_rl = *rl;
  new_rl->next = new_rl->prev = NULL;
  new_rl->passes.first = new_rl->passes.last = NULL;
  new_rl->exrhandle = NULL;
  for (RndrPass *rpass = rl->passes.first; rpass != NULL; rpass = rpass->next) {
    RndrPass *new_rpass = dup_rendr_pass(rpass);
    lib_addtail(&new_rl->passes, new_rpass);
  }
  return new_rl;
}

static RndrView *dup_rndr_view(RndrView *rview)
{
  RndrView *new_rview = mem_malloc(sizeof(RndrView), "new rndr view");
  *new_rview = *rview;
  if (new_rview->rectf != NULL) {
    new_rview->rectf = mem_dupalloc(new_rview->rectf);
  }
  if (new_rview->rectz != NULL) {
    new_rview->rectz = mem_dupalloc(new_rview->rectz);
  }
  if (new_rview->rect32 != NULL) {
    new_rview->rect32 = mem_dupalloc(new_rview->rect32);
  }
  return new_rview;
}

RndrResult *rndr_DupRndrResult(RndrResult *rr)
{
  RndrResult *new_rr = mem_mallocn(sizeof(RndrResult), "new dup'd rndr result");
  *new_rr = *rr;
  new_rr->next = new_rr->prev = NULL;
  new_rr->layers.first = new_rr->layers.last = NULL;
  new_rr->views.first = new_rr->views.last = NULL;
  for (RndrLayer *rl = rr->layers.first; rl != NULL; rl = rl->next) {
    RndrLayer *new_rl = dup_rndr_layer(rl);
    lib_addtail(&new_rr->layers, new_rl);
  }
  for (RndrView *rview = rr->views.first; rview != NULL; rview = rview->next) {
    RndrView *new_rview = dup_rndr_view(rview);
    lib_addtail(&new_rr->views, new_rview);
  }
  if (new_rr->rect32 != NULL) {
    new_rr->rect32 = mem_dupalloc(new_rr->rect32);
  }
  if (new_rr->rectf != NULL) {
    new_rr->rectf = mem_dupalloc(new_rr->rectf);
  }
  if (new_rr->rectz != NULL) {
    new_rr->rectz = mem_dupalloc(new_rr->rectz);
  }
  new_rr->stamp_data = dune_stamp_data_copy(new_rr->stamp_data);
  return new_rr;
}
