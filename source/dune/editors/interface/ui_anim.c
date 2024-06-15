#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_anim.h"
#include "types_scene.h"
#include "types_screen.h"

#include "lib_list.h"
#include "lib_string.h"
#include "lib_string_utf8.h"
#include "lib_utildefines.h"

#include "dune_animsys.h"
#include "dune_cxt.h"
#include "dune_fcurve.h"
#include "dune_fcurve_driver.h"
#include "dune_global.h"
#include "dune_main.h"
#include "dune_nla.h"

#include "graph.h"
#include "graph_build.h"

#include "ed_keyframing.h"

#include "ui.h"

#include "api_access.h"

#include "win_api.h"
#include "win_types.h"

#include "interface_intern.h"

static FCurve *btn_get_fcurve(
    Btn *btn, AnimData **adt, Action **action, bool *r_driven, bool *r_special)
{
  /* for entire array btns we check the first component, it's not perfect
   * but works well enough in typical cases */
  const int index = (btn->apiindex == -1) ? 0 : btn->apiindex;

  return dune_fcurve_find_by_api_cxt_ui(
      btn->block->evil_C, &btn->apiptr, btn->apiprop, apiindex, adt, action, r_driven, r_special);
}

void btn_anim_flag(Btn *btn, const AnimEvalCxt *anim_eval_cxt)
{
  AnimData *adt;
  Action *act;
  FCurve *fcu;
  bool driven;
  bool special;

  btn->flag &= ~(UI_BTN_ANIM | UI_BTN_ANIM_KEY | UI_BTN_DRIVEN);
  btn->drawflag &= ~UI_BTN_ANIM_CHANGED;

  /* NOTE: "special" is reserved for special F-Curves stored on the animation data
   *        itself (which are used to animate properties of the animation data).
   *        We count those as "animated" too for now  */
  fcu = btn_get_fcurve(btn, &adt, &act, &driven, &special);

  if (fcu) {
    if (!driven) {
      /* Empty curves are ignored by the animation evaluation system. */
      if (dune_fcurve_is_empty(fcu)) {
        return;
      }

      btn->flag |= UI_BTN_ANIM;

      /* T41525 - When the active action is a NLA strip being edited,
       * we need to correct the frame number to "look inside" the
       * remapped action  */
      float cfra = anim_eval_cxt->eval_time;
      if (adt) {
        cfra = dune_nla_tweakedit_remap(adt, cfra, NLATIME_CONVERT_UNMAP);
      }

      if (fcurve_frame_has_keyframe(fcu, cfra, 0)) {
        btn->flag |= UI_BTN_ANIMATED_KEY;
      }

      /* this feature is totally broken and useless with NLA */
      if (adt == NULL || adt->nla_tracks.first == NULL) {
        const AnimEvalCxt remapped_cxt = dune_animsys_eval_cxt_construct_at(
            anim_eval_cxt, cfra);
        if (fcurve_is_changed(btn->apiptr, btn->apiprop, fcu, &remapped_cxt)) {
          btn->drawflag |= UI_BTN_ANIM_CHANGED;
        }
      }
    }
    else {
      btn->flag |= UI_BTN_DRIVEN;
    }
  }
}

static Btn *btn_anim_decorate_find_attached_btn(BtnDecorator *btn_decorate)
{
  Btn *btn_iter = NULL;

  lib_assert(btn_is_decorator(&btn_decorate->btn));
  lib_assert(btn_decorate->apiptr.data && btn_decorate->apiprop);

  LIST_CIRCULAR_BACKWARD_BEGIN (
      &btn_decorate->btn.block->btns, btn_iter, btn_decorate->btn.prev) {
    if (btn_iter != (Btn *)btn_decorate &&
        btn_api_equals_ex(
            btn_iter, &btn_decorate->apiptr, btn_decorate->apiprop, btn_decorate->apiindex)) {
      return btn_iter;
    }
  }
  LIST_CIRCULAR_BACKWARD_END(
      &btn_decorate->btn.block->btns, btn_iter, btn_decorate->btn.prev);

  return NULL;
}

void btn_anim_decorate_update_from_flag(BtnDecorator *decorator_btn)
{
  if (!decorator_btn->apiptr.data || !decorator_btn->apiprop) {
    /* Nothing to do. */
    return;
  }

  const Btn *btn_anim = btn_anim_decorate_find_attached_btn(decorator_btn);
  Btn *btn = &decorator_btn->btn;

  if (!btn_anim) {
    printf("Could not find btn with matching prop to decorate (%s.%s)\n",
           api_struct_id(decorator_btn->apiptr.type),
           api__prop_id(decorator_btn->apiprop));
    return;
  }

  const int flag = btn_anim->flag;

  if (flag & UI_BTN_DRIVEN) {
    btn->icon = ICON_DECORATE_DRIVER;
  }
  else if (flag & UI_BTN_ANIMATED_KEY) {
    btn->icon = ICON_DECORATE_KEYFRAME;
  }
  else if (flag & UI_BTN_ANIMATED) {
    btn->icon = ICON_DECORATE_ANIMATE;
  }
  else if (flag & UI_BTN_OVERRIDDEN) {
    btn->icon = ICON_DECORATE_OVERRIDE;
  }
  else {
    btn->icon = ICON_DECORATE;
  }

  const int flag_copy = (UI_BTN_DISABLED | UI_BTN_INACTIVE);
  btn->flag = (btn->flag & ~flag_copy) | (flag & flag_copy);
}

bool btn_anim_expression_get(Btn *btn, char *str, size_t maxlen)
{
  FCurve *fcu;
  ChannelDriver *driver;
  bool driven, special;

  fcu = btn_get_fcurve(but, NULL, NULL, &driven, &special);

  if (fcu && driven) {
    driver = fcu->driver;

    if (driver && driver->type == DRIVER_TYPE_PYTHON) {
      if (str) {
        lib_strncpy(str, driver->expression, maxlen);
      }
      return true;
    }
  }

  return false;
}

bool btn_anim_expression_set(Btn *btn, const char *str)
{
  FCurve *fcu;
  ChannelDriver *driver;
  bool driven, special;

  fcu = btn_get_fcurve(btn, NULL, NULL, &driven, &special);

  if (fcu && driven) {
    driver = fcu->driver;

    if (driver && (driver->type == DRIVER_TYPE_PYTHON)) {
      Cxt *C = btn->block->evil_C;

      lib_strncpy_utf8(driver->expression, str, sizeof(driver->expression));

      /* tag driver as needing to be recompiled */
      dune_driver_invalidate_expression(driver, true, false);

      /* clear invalid flags which may prevent this from working */
      driver->flag &= ~DRIVER_FLAG_INVALID;
      fcu->flag &= ~FCURVE_DISABLED;

      /* this notifier should update the Graph Editor and trigger graph refresh? */
      win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME, NULL);

      graph_relations_tag_update(cxt_data_main(C));

      return true;
    }
  }

  return false;
}

bool btn_anim_expression_create(Btn *btn, const char *str)
{
  Cxt *C = btn->block->evil_C;
  Id *id;
  FCurve *fcu;
  char *path;
  bool ok = false;

  /* btn must have api-ptr to a numeric-capable prop */
  if (ELEM(NULL, btn->apiptr.data, btn->apiprop)) {
    if (G.debug & G_DEBUG) {
      printf("ERROR: create expression failed - btn has no api info attached\n");
    }
    return false;
  }

  if (api_prop_arr_check(btn->apiprop) != 0) {
    if (btn->apiindex == -1) {
      if (G.debug & G_DEBUG) {
        printf("ERROR: create expression failed - can't create expression for entire array\n");
      }
      return false;
    }
  }

  /* make sure we have animdata for this */
  /* FIXME: until materials can be handled by depsgraph,
   * don't allow drivers to be created for them */
  id = btn->apiptr.owner_id;
  if ((id == NULL) || (GS(id->name) == ID_MA) || (GS(id->name) == ID_TE)) {
    if (G.debug & G_DEBUG) {
      printf("ERROR: create expression failed - invalid data-block for adding drivers (%p)\n", id);
    }
    return false;
  }

  /* get path */
  path = api_path_from_id_to_prop(&btn->apiptr, btn->apiprop);
  if (path == NULL) {
    return false;
  }

  /* create driver */
  fcu = verify_driver_fcurve(id, path, btn->apiindex, DRIVER_FCURVE_KEYFRAMES);
  if (fcu) {
    ChannelDriver *driver = fcu->driver;

    if (driver) {
      /* set type of driver */
      driver->type = DRIVER_TYPE_PYTHON;

      /* set the expression */
      /* TODO: need some way of identifying variables used */
      lib_strncpy_utf8(driver->expression, str, sizeof(driver->expression));

      /* updates */
      dune_driver_invalidate_expression(driver, true, false);
      graph_relations_tag_update(cxt_data_main(C));
      win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME, NULL);
      ok = true;
    }
  }

  mem_free(path);

  return ok;
}

void btn_anim_autokey(Cxt *C, Btn *btn, Scene *scene, float cfra)
{
  ed_autokeyframe_prop(C, scene, &btn->apiptr, btn->apiprop, btn->apiindex, cfra);
}

void btn_anim_copy_driver(Cxt *C)
{
  /* this op calls ui_cxt_active_btn_prop_get */
  win_op_name_call(C, "anim_ot_copy_driver_btn", WIN_OP_INVOKE_DEFAULT, NULL, NULL);
}

void btn_anim_paste_driver(Cxt *C)
{
  /* this op calls ui_cxt_active_btn_prop_get */
  win_op_name_call(C, "anim_ot_paste_driver_btn", WIN_OP_INVOKE_DEFAULT, NULL, NULL);
}

void btn_anim_decorate_cb(Cxt *C, void *arg_btn, void *UNUSED(arg_dummy))
{
  WinManager *wm = cxt_wm(C);
  BtnDecorator *btn_decorate = arg_btn;
  Btn *btn_anim = btn_anim_decorate_find_attached_btn(btn_decorate);

  if (!btn_anim) {
    return;
  }

  /* FIXME, swapping active ptr is weak. */
  SWAP(struct uiHandleBtnData *, btn_anim->active, btn_decorate->btn.active);
  win->op_undo_depth++;

  if (btn_anim->flag & UI_BTN_DRIVEN) {
    /* pass */
    /* TODO: report? */
  }
  else if (btn_anim->flag & UI_BTN_ANIMATED_KEY) {
    ApiPtr props_ptr;
    WinOpType *ot = win_optype_find("anim_ot_keyframe_del_btn", false);
    win_op_props_create_ptr(&props_ptr, ot);
    api_bool_set(&props_ptr, "all", btn_anim->apiindex == -1);
    win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &props_ptr, NULL);
    win_op_props_free(&props_ptr);
  }
  else {
    ApiPtr props_ptr;
    WinOpType *ot = win_optype_find("anim_ot_keyframe_insert_btn", false);
    win_op_props_create_ptr(&props_ptr, ot);
    api_bool_set(&props_ptr, "all", but_anim->apiindex == -1);
    win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &props_ptr, NULL);
    win_op_props_free(&props_ptr);
  }

  SWAP(struct uiHandleBtnData *, btn_anim->active, btn_decorate->btn.active);
  win->op_undo_depth--;
}
