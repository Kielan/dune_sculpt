#include <cstdio>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "BLI_blenlib.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_editmesh.hh"
#include "BKE_layer.h"
#include "BKE_screen.hh"

#include "DEG_depsgraph.hh"

#include "ED_image.hh"
#include "ED_uvedit.hh"

#include "UI_interface.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#define B_UVEDIT_VERTEX 3

/* UV Utilities */

static int uvedit_center(Scene *scene, Object **objects, uint objects_len, float center[2])
{
  BMFace *f;
  BMLoop *l;
  BMIter iter, liter;
  float *luv;
  int tot = 0;

  zero_v2(center);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    const BMUVOffsets offsets = BM_uv_map_get_offsets(em->bm);

    BM_ITER_MESH (f, &iter, em->bm, BM_FACES_OF_MESH) {
      if (!uvedit_face_visible_test(scene, f)) {
        continue;
      }

      BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
        if (uvedit_uv_select_test(scene, l, offsets)) {
          luv = BM_ELEM_CD_GET_FLOAT_P(l, offsets.uv);
          add_v2_v2(center, luv);
          tot++;
        }
      }
    }
  }

  if (tot > 0) {
    center[0] /= tot;
    center[1] /= tot;
  }

  return tot;
}

static void uvedit_translate(Scene *scene,
                             Object **objects,
                             uint objects_len,
                             const float delta[2])
{
  BMFace *f;
  BMLoop *l;
  BMIter iter, liter;
  float *luv;

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    const BMUVOffsets offsets = BM_uv_map_get_offsets(em->bm);

    BM_ITER_MESH (f, &iter, em->bm, BM_FACES_OF_MESH) {
      if (!uvedit_face_visible_test(scene, f)) {
        continue;
      }

      BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
        if (uvedit_uv_select_test(scene, l, offsets)) {
          luv = BM_ELEM_CD_GET_FLOAT_P(l, offsets.uv);
          add_v2_v2(luv, delta);
        }
      }
    }
  }
}

/* Button Functions, using an evil static variable */

static float uvedit_old_center[2];

static void uvedit_vertex_buttons(const bContext *C, uiBlock *block)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  Scene *scene = CTX_data_scene(C);
  float center[2];
  int imx, imy, step, digits;
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data_with_uvs(
      scene, CTX_data_view_layer(C), CTX_wm_view3d(C), &objects_len);

  ED_space_image_get_size(sima, &imx, &imy);

  if (uvedit_center(scene, objects, objects_len, center)) {
    float range_xy[2][2] = {
        {-10.0f, 10.0f},
        {-10.0f, 10.0f},
    };

    copy_v2_v2(uvedit_old_center, center);

    /* expand UI range by center */
    CLAMP_MAX(range_xy[0][0], uvedit_old_center[0]);
    CLAMP_MIN(range_xy[0][1], uvedit_old_center[0]);
    CLAMP_MAX(range_xy[1][0], uvedit_old_center[1]);
    CLAMP_MIN(range_xy[1][1], uvedit_old_center[1]);

    if (!(sima->flag & SI_COORDFLOATS)) {
      uvedit_old_center[0] *= imx;
      uvedit_old_center[1] *= imy;

      mul_v2_fl(range_xy[0], imx);
      mul_v2_fl(range_xy[1], imy);
    }

    if (sima->flag & SI_COORDFLOATS) {
      step = 1;
      digits = 3;
    }
    else {
      step = 100;
      digits = 2;
    }

    uiBut *but;

    int y = 0;
    UI_block_align_begin(block);
    but = uiDefButF(block,
                    UI_BTYPE_NUM,
                    B_UVEDIT_VERTEX,
                    IFACE_("X:"),
                    0,
                    y -= UI_UNIT_Y,
                    200,
                    UI_UNIT_Y,
                    &uvedit_old_center[0],
                    UNPACK2(range_xy[0]),
                    0,
                    0,
                    "");
    ui_btn_num_step_size_set(btn, step);
    ui_btn_num_precision_set(btn, digits);
    but = uiDefBtnF(block,
                    UI_BTYPE_NUM,
                    B_UVEDIT_VERTEX,
                    IFACE_("Y:"),
                    0,
                    y -= UI_UNIT_Y,
                    200,
                    UI_UNIT_Y,
                    &uvedit_old_center[1],
                    UNPACK2(range_xy[1]),
                    0,
                    0,
                    "");
    ui_btn_num_step_size_set(btn, step);
    ui_btn_num_precision_set(btn, digits);
    ui_block_align_end(block);
  }

  mem_free(obs);
}

static void do_uvedit_vertex(Cxt *C, void * /*arg*/, int event)
{
  SpaceImg *simg = cxt_win_space_img(C);
  Scene *scene = cxt_data_scene(C);
  float center[2], delta[2];
  int imx, imy;

  if (event != B_UVEDIT_VERTEX) {
    return;
  }

  uint obs_len = 0;
  Ob **obs = dune_view_layer_arr_from_obs_in_edit_mode_unique_data_w_uvs(
      scene, cxt_data_view_layer(C), cxt_win_view3d(C), &obs_len);

  ed_space_img_get_size(simg, &imx, &imy);
  uvedit_center(scene, obs, obs_len, center);

  if (sima->flag & SI_COORDFLOATS) {
    delta[0] = uvedit_old_center[0] - center[0];
    delta[1] = uvedit_old_center[1] - center[1];
  }
  else {
    delta[0] = uvedit_old_center[0] / imx - center[0];
    delta[1] = uvedit_old_center[1] / imy - center[1];
  }

  uvedit_translate(scene, obs, objects_len, delta);

  win_ev_add_notifier(C, NC_IMG, simg->imh);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *obedit = objects[ob_index];
    graph_id_tag_update((Id *)obedit->data, ID_RECALC_GEOMETRY);
  }

  mem_free(obs);
}

/* Panels */
static bool img_pnl_uv_poll(const Cxt *C, PnlType * /*pt*/)
{
  SpaceImg *simg = cxt_win_space_img(C);
  if (sima->mode != SI_MODE_UV) {
    return false;
  }
  Ob *obedit = cxt_data_edit_ob(C);
  return ed_uvedit_test(obedit);
}

static void img_pnl_uv(const Cxt *C, Pnl *pnl)
{
  uiBlock *block;

  block = uiLayoutAbsoluteBlock(pnl->layout);
  ui_block_fn_handle_set(block, do_uvedit_vertex, nullptr);

  uvedit_vertex_btns(C, block);
}

void ed_uvedit_btns_register(ARgnType *art)
{
  PnlType *pt = mem_cnew<PnlType>(__func__);

  STRNCPY(pt->idname, "IMG_PT_uv");
  STRNCPY(pt->label, N_("UV Vertex")); /* C pnls unavailable through api bpy.types! */
  /* Could be 'Item' matching 3D view, avoid new tab for 2 btns. */
  STRNCPY(pt->category, "Img");
  pt->drw = img_pnl_uv;
  pt->poll = img_pnl_uv_poll;
  lib_addtail(&art->pnltypes, pt);
}
