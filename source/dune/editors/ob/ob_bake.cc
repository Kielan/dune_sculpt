#include <cstring>

#include "mem_guardedalloc.h"

#include "types_material.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_world.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_DerivedMesh.hh"
#include "dune.h"
#include "dune_cdderivedmesh.h"
#include "dune_cxt.hh"
#include "dune_global.h"
#include "dune_img.h"
#include "dune_material.h"
#include "dune_mesh.hh"
#include "dune_mod.hh"
#include "dune_multires.hh"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.hh"

#include "render_multires_bake.h"
#include "render_pipeline.h"
#include "render_texture.h"

#include "PIL_time.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_ob.hh"
#include "ed_screen.hh"
#include "ed_uvedit.hh"

#include "ob_intern.h"

static Img *bake_ob_img_get(Ob *ob, int mat_nr)
{
  Img *img = nullptr;
  ed_ob_get_active_img(ob, mat_nr + 1, &img, nullptr, nullptr, nullptr);
  return image;
}

static Img **bake_ob_img_get_array(Obg *ob)
{
  Img **img_array = static_cast<Img **>(
      mem_malloc(sizeof(Material *) * ob->totcol, __func__));
  for (int i = 0; i < ob->totcol; i++) {
    image_array[i] = bake_ob_img_get(ob, i);
  }
  return img_array;
}

/* multires BAKING */
/* holder of per-ob data needed for bake job
 * needed to make job totally thread-safe */
struct MultiresBakerJobData {
  MultiresBakerJobData *next, *prev;
  /* material aligned image array (for per-face bake img) */
  struct {
    Image **array;
    int len;
  } ob_image;
  DerivedMesh *lores_dm, *hires_dm;
  int lvl, tot_lvl;
  ListBase images;
};

/* data passing to multires-baker job */
struct MultiresBakeJob {
  Scene *scene;
  List data;
  /* Clear the imgs before baking */
  bool bake_clear;
  /* Margin size in pixels. */
  int bake_margin;
  /* margin type */
  char bake_margin_type;
  /* mode of baking (displacement, normals, AO) */
  short mode;
  /* Use low-resolution mesh when baking displacement maps */
  bool use_lores_mesh;
  /* Number of rays to be cast when doing AO baking */
  int number_of_rays;
  /* Bias between ob and start ray point when doing AO baking */
  float bias;
  /* Number of threads to be used for baking */
  int threads;
  /* User scale used to scale displacement when baking derivative map. */
  float user_scale;
};

static bool multiresbake_check(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);
  Object *ob;
  Mesh *me;
  MultiresModifierData *mmd;
  bool ok = true;
  int a;

  CXT_DATA_BEGIN (C, Base *, base, sel_editable_bases) {
    ob = base->ob;

    if (ob->type != OB_MESH) {
      dune_report(
          op->reports, RPT_ERROR, "Baking of multires data only works with an active mesh object");

      ok = false;
      break;
    }

    me = (Mesh *)ob->data;
    mmd = get_multires_mod(scene, ob, false);

    /* Multi-resolution should be and be last in the stack */
    if (ok && mmd) {
      ModData *md;

      ok = mmd->totlvl > 0;

      for (md = (ModData *)mmd->mod.next; md && ok; md = md->next) {
        if (dune_mod_is_enabled(scene, md, eModModeRealtime)) {
          ok = false;
        }
      }
    }
    else {
      ok = false;
    }

    if (!ok) {
      dune_report(op->reports, RPT_ERROR, "Multires data baking requires multi-resolution ob");

      break;
    }

    if (!CustomData_has_layer(&me->loop_data, CD_PROP_FLOAT2)) {
      dune_report(op->reports, RPT_ERROR, "Mesh should be unwrapped before multires data baking");

      ok = false;
    }
    else {
      const int *material_indices = dune_mesh_material_indices(me);
      a = me->faces_num;
      while (ok && a--) {
        Img *img = bake_ob_img_get(ob, material_indices ? material_indices[a] : 0);

        if (!img) {
          dune_report(
              op->reports, RPT_ERROR, "You should have active texture to use multires baker");

          ok = false;
        }
        else {
          LIST_FOREACH (ImgTile *, tile, &img->tiles) {
            ImgUser iuser;
            dune_imguser_default(&iuser);
            iuser.tile = tile->tile_number;

            ImBuf *ibuf = dune_img_acquire_ibuf(img, &iuser, nullptr);

            if (!ibuf) {
              dune_report(
                  op->reports, RPT_ERROR, "Baking should happen to img w img buf");

              ok = false;
            }
            else {
              if (ibuf->byte_buf.data == nullptr && ibuf->float_buf.data == nullptr) {
                ok = false;
              }

              if (ibuf->float_buf.data && !ELEM(ibuf->channels, 0, 4)) {
                ok = false;
              }

              if (!ok) {
                dune_report(op->reports, RPT_ERROR, "Baking to unsupported img type");
              }
            }

            dune_img_release_ibuf(img, ibuf, nullptr);
          }
        }
      }
    }

    if (!ok) {
      break;
    }
  }
  CXT_DATA_END;

  return ok;
}

static DerivedMesh *multiresbake_create_loresdm(Scene *scene, Ob *ob, int *lvl)
{
  DerivedMesh *dm;
  MultiresModData *mmd = get_multires_mod(scene, ob, false);
  Mesh *me = (Mesh *)ob->data;
  MultiresModData tmp_mmd = dune::types::shallow_copy(*mmd);

  *lvl = mmd->lvl;

  if (mmd->lvl == 0) {
    DerivedMesh *cddm = CDDM_from_mesh(me);
    DM_set_only_copy(cddm, &CD_MASK_BAREMESH);
    return cddm;
  }

  DerivedMesh *cddm = CDDM_from_mesh(me);
  DM_set_only_copy(cddm, &CD_MASK_BAREMESH);
  tmp_mmd.lvl = mmd->lvl;
  tmp_mmd.sculptlvl = mmd->lvl;
  dm = multires_make_derived_from_derived(cddm, &tmp_mmd, scene, ob, MultiresFlags(0));

  cddm->release(cddm);

  return dm;
}

static DerivedMesh *multiresbake_create_hiresdm(Scene *scene, Ob *ob, int *lvl)
{
  Mesh *me = (Mesh *)ob->data;
  MultiresModData *mmd = get_multires_mo(scene, ob, false);
  MultiresModData tmp_mmd = dune::types::shallow_copy(*mmd);
  DerivedMesh *cddm = CDDM_from_mesh(me);
  DerivedMesh *dm;

  DM_set_only_copy(cddm, &CD_MASK_BAREMESH);

  /* TODO: DM_set_only_copy wouldn't set mask for loop and poly data,
   * we rly need BAREMESH only to save lots of memory */
  CustomData_set_only_copy(&cddm->loopData, CD_MASK_BAREMESH.lmask);
  CustomData_set_only_copy(&cddm->polyData, CD_MASK_BAREMESH.pmask);

  *lvl = mmd->totlvl;

  tmp_mmd.lvl = mmd->totlvl;
  tmp_mmd.sculptlvl = mmd->totlvl;
  dm = multires_make_derived_from_derived(cddm, &tmp_mmd, scene, ob, MultiresFlags(0));
  cddm->release(cddm);

  return dm;
}

enum ClearFlag {
  CLEAR_TANGENT_NORMAL = 1,
  CLEAR_DISPLACEMENT = 2,
};

static void clear_single_img(Img *img, ClearFlag flag)
{
  const float vec_alpha[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float vec_solid[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const float nor_alpha[4] = {0.5f, 0.5f, 1.0f, 0.0f};
  const float nor_solid[4] = {0.5f, 0.5f, 1.0f, 1.0f};
  const float disp_alpha[4] = {0.5f, 0.5f, 0.5f, 0.0f};
  const float disp_solid[4] = {0.5f, 0.5f, 0.5f, 1.0f};

  if ((img->id.tag & LIB_TAG_DOIT) == 0) {
    LIST_FOREACH (ImgTile *, tile, &img->tiles) {
      ImgUser iuser;
      dune_imguser_default(&iuser);
      iuser.tile = tile->tile_number;

      ImBuf *ibuf = dune_img_acquire_ibuf(img, &iuser, nullptr);

      if (flag == CLEAR_TANGENT_NORMAL) {
        imbuf_rectfill(ibuf, (ibuf->planes == R_IMF_PLANES_RGBA) ? nor_alpha : nor_solid);
      }
      else if (flag == CLEAR_DISPLACEMENT) {
        imbuf_rectfill(ibuf, (ibuf->planes == R_IMF_PLANES_RGBA) ? disp_alpha : disp_solid);
      }
      else {
        imbuf_rectfill(ibuf, (ibuf->planes == R_IMF_PLANES_RGBA) ? vec_alpha : vec_solid);
      }

      img->id.tag |= LIB_TAG_DOIT;

      dune_img_release_ibuf(img, ibuf, nullptr);
    }
  }
}

static void clear_imgs_poly(Img **ob_img_array, int ob_img_array_len, ClearFlag flag)
{
  for (int i = 0; i < ob_img_array_len; i++) {
    Img *img = ob_img_array[i];
    if (img) {
      img->id.tag &= ~LIB_TAG_DOIT;
    }
  }

  for (int i = 0; i < ob_img_array_len; i++) {
    Img *img = ob_img_array[i];
    if (img) {
      clear_single_img(img, flag);
    }
  }

  for (int i = 0; i < ob_img_array_len; i++) {
    Img *img = ob_img_array[i];
    if (img) {
      img->id.tag &= ~LIB_TAG_DOIT;
    }
  }
}

static int multiresbake_img_ex_locked(Cxt *C, WinOp *op)
{
  Ob *ob;
  Scene *scene = cxt_data_scene(C);
  int obs_baked = 0;

  if (!multiresbake_check(C, op)) {
    return OP_CANCELLED;
  }

  if (scene->r.bake_flag & R_BAKE_CLEAR) { /* clear img */
    CXT_DATA_BEGIN (C, Base *, base, sel_editable_bases) {
      ClearFlag clear_flag = ClearFlag(0);

      ob = base->ob;
      // me = (Mesh *)ob->data;

      if (scene->r.bake_mode == RE_BAKE_NORMALS) {
        clear_flag = CLEAR_TANGENT_NORMAL;
      }
      else if (scene->r.bake_mode == RE_BAKE_DISPLACEMENT) {
        clear_flag = CLEAR_DISPLACEMENT;
      }

      {
        Img **ob_img_array = bake_ob_img_get_array(ob);
        clear_imgs_poly(ob_img_array, ob->totcol, clear_flag);
        mem_free(ob_img_array);
      }
    }
    CXT_DATA_END;
  }

  CXT_DATA_BEGIN (C, Base *, base, sel_editable_bases) {
    MultiresBakeRender bkr = {nullptr};

    ob = base->ob;

    multires_flush_sculpt_updates(ob);

    /* copy data stored in job descriptor */
    bkr.scene = scene;
    bkr.bake_margin = scene->r.bake_margin;
    if (scene->r.bake_mode == RE_BAKE_NORMALS) {
      bkr.bake_margin_type = R_BAKE_EXTEND;
    }
    else {
      bkr.bake_margin_type = scene->r.bake_margin_type;
    }
    bkr.mode = scene->r.bake_mode;
    bkr.use_lores_mesh = scene->r.bake_flag & R_BAKE_LORES_MESH;
    bkr.bias = scene->r.bake_biasdist;
    bkr.number_of_rays = scene->r.bake_samples;
    bkr.threads = dune_scene_num_threads(scene);
    bkr.user_scale = (scene->r.bake_flag & R_BAKE_USERSCALE) ? scene->r.bake_user_scale : -1.0f;
    // bkr.reports= op->reports;

    /* create low-resolution DM (to bake to) and hi-resolution DM (to bake from) */
    bkr.ob_img.array = bake_ob_img_get_array(ob);
    bkr.ob_img.len = ob->totcol;

    bkr.hires_dm = multiresbake_create_hiresdm(scene, ob, &bkr.tot_lvl);
    bkr.lores_dm = multiresbake_create_loresdm(scene, ob, &bkr.lvl);

    render_multires_bake_imgs(&bkr);

    mem_free(bkr.ob_img.array);

    lib_freelist(&bkr.img);

    bkr.lores_dm->release(bkr.lores_dm);
    bkr.hires_dm->release(bkr.hires_dm);

    obs_baked++;
  }
  CXT_DATA_END;

  if (!obs_baked) {
    dune_report(op->reports, RPT_ERROR, "No obs found to bake from");
  }

  return OP_FINISHED;
}

/* Multi-resolution-bake adopted for job-system ex. */
static void init_multiresbake_job(Cxt *C, MultiresBakeJob *bkj)
{
  Scene *scene = cxt_data_scene(C);
  Ob *ob;

  /* backup scene settings, so their changing in UI would take no effect on baker */
  bkj->scene = scene;
  bkj->bake_margin = scene->r.bake_margin;
  if (scene->r.bake_mode == RE_BAKE_NORMALS) {
    bkj->bake_margin_type = R_BAKE_EXTEND;
  }
  else {
    bkj->bake_margin_type = scene->r.bake_margin_type;
  }
  bkj->mode = scene->r.bake_mode;
  bkj->use_lores_mesh = scene->r.bake_flag & R_BAKE_LORES_MESH;
  bkj->bake_clear = scene->r.bake_flag & R_BAKE_CLEAR;
  bkj->bias = scene->r.bake_biasdist;
  bkj->number_of_rays = scene->r.bake_samples;
  bkj->threads = dune_scene_num_threads(scene);
  bkj->user_scale = (scene->r.bake_flag & R_BAKE_USERSCALE) ? scene->r.bake_user_scale : -1.0f;
  // bkj->reports = op->reports;

  CXT_DATA_BEGIN (C, Base *, base, sel_editable_bases) {
    int lvl;

    ob = base->ob;

    multires_flush_sculpt_updates(ob);

    MultiresBakerJobData *data = mem_cnew<MultiresBakerJobData>(__func__);

    data->ob_img.array = bake_ob_img_get_array(ob);
    data->ob_img.len = ob->totcol;

    /* create low-resolution DM (to bake to) and hi-resolution DM (to bake from) */
    data->hires_dm = multiresbake_create_hiresdm(scene, ob, &data->tot_lvl);
    data->lores_dm = multiresbake_create_loresdm(scene, ob, &lvl);
    data->lvl = lvl;

    lib_addtail(&bkj->data, data);
  }
  CXT_DATA_END;
}

static void multiresbake_startjob(void *bkv, WinJobWorkerStatus *worker_status)
{
  MultiresBakeJob *bkj = static_cast<MultiresBakeJob *>(bkv);
  int baked_obs = 0, tot_obj;

  tot_obj = lib_list_count(&bkj->data);

  if (bkj->bake_clear) { /* clear imgs */
    LIST_FOREACH (MultiresBakerJobData *, data, &bkj->data) {
      ClearFlag clear_flag = ClearFlag(0);

      if (bkj->mode == RE_BAKE_NORMALS) {
        clear_flag = CLEAR_TANGENT_NORMAL;
      }
      else if (bkj->mode == RE_BAKE_DISPLACEMENT) {
        clear_flag = CLEAR_DISPLACEMENT;
      }

      clear_imgs_poly(data->ob_img.array, data->ob_img.len, clear_flag);
    }
  }

  LIST_FOREACH (MultiresBakerJobData *, data, &bkj->data) {
    MultiresBakeRender bkr = {nullptr};

    /* copy data stored in job descriptor */
    bkr.scene = bkj->scene;
    bkr.bake_margin = bkj->bake_margin;
    bkr.bake_margin_type = bkj->bake_margin_type;
    bkr.mode = bkj->mode;
    bkr.use_lores_mesh = bkj->use_lores_mesh;
    bkr.user_scale = bkj->user_scale;
    // bkr.reports = bkj->reports;
    bkr.ob_img.array = data->ob_img.array;
    bkr.ob_img.len = data->ob_img.len;

    /* create low-resolution DM (to bake to) and hi-resolution DM (to bake from) */
    bkr.lores_dm = data->lores_dm;
    bkr.hires_dm = data->hires_dm;
    bkr.tot_lvl = data->tot_lvl;
    bkr.lvl = data->lvl;

    /* needed for proper progress bar */
    bkr.tot_ob = tot_ob;
    bkr.baked_obs = baked_obs;

    bkr.stop = &worker_status->stop;
    bkr.do_update = &worker_status->do_update;
    bkr.progress = &worker_status->progress;

    bkr.bias = bkj->bias;
    bkr.number_of_rays = bkj->number_of_rays;
    bkr.threads = bkj->threads;

    render_multires_bake_imgs(&bkr);

    data->imgs = bkr.img;

    baked_objects++;
  }
}

static void multiresbake_freejob(void *bkv)
{
  MultiresBakeJob *bkj = static_cast<MultiresBakeJob *>(bkv);
  MultiresBakerJobData *data, *next;

  data = static_cast<MultiresBakerJobData *>(bkj->data.first);
  while (data) {
    next = data->next;
    data->lores_dm->release(data->lores_dm);
    data->hires_dm->release(data->hires_dm);

    /* delete here, since this del will be called from main thread */
    LIST_FOREACH (LinkData *, link, &data->imgs) {
      Image *img = (Img *)link->data;
      dune_img_partial_update_mark_full_update(img);
    }

    mem_free(data->ob_img.array);

    lib_freelist(&data->imgs);

    mem_free(data);
    data = next;
  }

  mem_free(bkj);
}

static int multiresbake_img_ex(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);

  if (!multiresbake_check(C, op)) {
    return OP_CANCELLED;
  }

  MultiresBakeJob *bkr = mem_cnew<MultiresBakeJob>(__func__);
  init_multiresbake_job(C, bkr);

  if (!bkr->data.first) {
    dune_report(op->reports, RPT_ERROR, "No objects found to bake from");
    return OP_CANCELLED;
  }

  /* setup job */
  WinJob *win_job = win_jobs_get(cxt_wm(C),
                              cxt_win(C),
                              scene,
                              "Multires Bake",
                              WIN_JOB_EXCL_RENDER | WIN_JOB_PRIORITY | WM_JOB_PROGRESS,
                              WIN_JOB_TYPE_OB_BAKE_TEXTURE);
  win_jobs_customdata_set(win_job, bkr, multiresbake_freejob);
  win_jobs_timer(win_job, 0.5, NC_IMG, 0); /* TODO: only drw bake img, can we enforce this.
  win_jobs_cbs(win_job, multiresbake_startjob, nullptr, nullptr, nullptr);

  G.is_break = false;

  win_jobs_start(cxt_win(C), win_job);
  win_cursor_wait(false);

  /* add modal handler for ESC */
  win_ev_add_modal_handler(C, op);

  return OP_RUNNING_MODAL;
}

/* render BAKING */
/* Catch escape key to cancel. */
static int obs_bake_render_modal(Cxt *C, WinOp * /*op*/, const WinEv *ev)
{
  /* no running dune, remove handler and pass thru */
  if (0 == win_jobs_test(cxt_win(C), cxt_data_scene(C), WIN_JOB_TYPE_OB_BAKE_TEXTURE)) {
    return OP_FINISHED | OP_PASS_THROUGH;
  }

  /* running render */
  switch (ev->type) {
    case EV_ESCKEY:
      return OP_RUNNING_MODAL;
  }
  return OP_PASS_THROUGH;
}

static bool is_multires_bake(Scene *scene)
{
  if (ELEM(scene->r.bake_mode, RE_BAKE_NORMALS, RE_BAKE_DISPLACEMENT, RE_BAKE_AO)) {
    return scene->r.bake_flag & R_BAKE_MULTIRES;
  }

  return false;
}

static int obs_bake_render_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  Scene *scene = cxt_data_scene(C);
  int result = OP_CANCELLED;

  result = multiresbake_img_ex(C, op);

  win_ev_add_notifier(C, NC_SCENE | ND_RENDER_RESULT, scene);

  return result;
}

static int bake_image_ex(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);
  int result = OP_CANCELLED;

  if (!is_multires_bake(scene)) {
    lib_assert(0);
    return result;
  }

  result = multiresbake_img_ex_locked(C, op);

  win_ev_add_notifier(C, NC_SCENE | ND_RENDER_RESULT, scene);

  return result;
}

void OB_OT_bake_img(WinOpType *ot)
{
  /* ids */
  ot->name = "Bake";
  ot->description = "Bake img textures of sel obs";
  ot->idname = "OB_OT_bake_img";

  /* api cbs */
  ot->ex = bake_img_ex;
  ot->invoke = obs_bake_render_invoke;
  ot->modal = obs_bake_render_modal;
  ot->poll = es_ob_ob_active;
}
