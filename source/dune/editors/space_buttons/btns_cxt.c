#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_armature.h"
#include "types_brush.h"
#include "types_collection.h"
#include "types_linestyle.h"
#include "types_material.h"
#include "types_node.h"
#include "types_scene.h"
#include "types_window.h"
#include "types_world.h"

#include "dune_act.h"
#include "dune_armature.h"
#include "dune_cxt.h"
#include "dune_layer.h"
#include "dune_linestyle.h"
#include "dune_material.h"
#include "dune_mod.h"
#include "dune_ob.h"
#include "dune_paint.h"
#include "dune_particle.h"
#include "dune_screen.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "ed_btns.h"
#include "ed_phys.h"
#include "ed_screen.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "win_api.h"

#include "btns_intern.h" /* own include */

static int set_ptr_type(BtnsCxtPath *path, CxtDataResult *result, ApiStruct *type)
{
  for (int i = 0; i < path->len; i++) {
    ApiPtr *ptr = &path->ptr[i];

    if (api_struct_is_a(ptr->type, type)) {
      cxt_data_ptr_set_ptr(result, ptr);
      return CXT_RESULT_OK;
    }
  }

  return CXT_RESULT_MEMBER_NOT_FOUND;
}

static ApiPtr *get_ptr_type(BtnsCxtPath *path, ApiStruct *type)
{
  for (int i = 0; i < path->len; i++) {
    ApiPtr *ptr = &path->ptr[i];

    if (api_struct_is_a(ptr->type, type)) {
      return ptr;
    }
  }

  return NULL;
}

/** Creating the Path */
static bool btns_cxt_path_scene(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* this one just verifies */
  return api_struct_is_a(ptr->type, &ApiScene);
}

static bool btns_cxt_path_view_layer(BtnsCxtPath *path, Window *win)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* View Layer may have already been resolved in a previous call
   * (e.g. in btns_cxt_path_linestyle). */
  if (api_struct_is_a(ptr->type, &ApiViewLayer)) {
    return true;
  }

  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;
    ViewLayer *view_layer = (win->scene == scene) ? wm_window_get_active_view_layer(win) :
                                                    dune_view_layer_default_view(scene);

    api_ptr_create(&scene->id, &ApiViewLayer, view_layer, &path->ptr[path->len]);
    path->len++;
    return true;
  }

  return false;
}

/* This fn can return true wo adding a world to the path
 * so the btns stay visible, but be sure to check the Id type if a ID_WO */
static bool btns_cxt_path_world(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we alrdy have a (pinned) world, we're done */
  if (api_struct_is_a(ptr->type, &ApiWorld)) {
    return true;
  }
  /* if we have a scene, use the scene's world */
  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;
    World *world = scene->world;

    if (world) {
      api_id_ptr_create(&scene->world->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }

    return true;
  }

  /* no path to a world possible */
  return false;
}

static bool btns_cxt_path_collection(const Cxt *C,
                                     BtnsCxtPath *path,
                                     Win *window)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) collection, we're done */
  if (api_struct_is_a(ptr->type, &ApiCollection)) {
    return true;
  }

  Scene *scene = cxt_data_scene(C);

  /* if we have a view layer, use the view layer's active collection */
  if (btns_cxt_path_view_layer(path, window)) {
    ViewLayer *view_layer = path->ptr[path->len - 1].data;
    Collection *c = view_layer->active_collection->collection;

    /* Do not show collection tab for master collection. */
    if (c == scene->master_collection) {
      return false;
    }

    if (c) {
      api_id_ptr_create(&c->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a collection possible */
  return false;
}

static bool btns_cxt_path_linestyle(BtnsCxtPath *path, Win *window)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) linestyle, we're done */
  if (api_struct_is_a(ptr->type, &ApiFreestyleLineStyle)) {
    return true;
  }
  /* if we have a view layer, use the lineset's linestyle */
  if (btns_cxt_path_view_layer(path, window)) {
    ViewLayer *view_layer = path->ptr[path->len - 1].data;
    FreestyleLineStyle *linestyle = dune_linestyle_active_from_view_layer(view_layer);
    if (linestyle) {
      api_id_ptr_create(&linestyle->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a linestyle possible */
  return false;
}

static bool btns_cxt_path_object(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we alrdy have a (pinned) ob, we're done */
  if (api_struct_is_a(ptr->type, &ApiOb)) {
    return true;
  }
  if (!api_struct_is_a(ptr->type, &ApiViewLayer)) {
    return false;
  }

  ViewLayer *view_layer = ptr->data;
  Ob *ob = (view_layer->basact) ? view_layer->basact->object : NULL;

  if (ob) {
    api_id_ptr_create(&ob->id, &path->ptr[path->len]);
    path->len++;

    return true;
  }

  /* no path to a ob possible */
  return false;
}

static bool btns_cxt_path_data(BtnsCxtPath *path, int type)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a data, we're done */
  if (api_struct_is_a(ptr->type, &ApiMesh) && (elem(type, -1, OB_MESH))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiCurve) &&
      (type == -1 || ELEM(type, OB_CURVES_LEGACY, OB_SURF, OB_FONT))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiArmature) && (elem(type, -1, OB_ARMATURE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiMetaBall) && (elem(type, -1, OB_MBALL))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLattice) && (elem(type, -1, OB_LATTICE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiCamera) && (elem(type, -1, OB_CAMERA))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLight) && (elem(type, -1, OB_LAMP))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiSpeaker) && (elem(type, -1, OB_SPEAKER))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLightProbe) && (elem(type, -1, OB_LIGHTPROBE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiPen) && (elem(type, -1, OB_PEN))) {
    return true;
  }
#ifdef WITH_NEW_CURVES_TYPE
  if (api_struct_is_a(ptr->type, &ApiCurves) && (elem(type, -1, OB_CURVES))) {
    return true;
  }
#endif
#ifdef WITH_POINT_CLOUD
  if (api_struct_is_a(ptr->type, &ApiPointCloud) && (elem(type, -1, OB_POINTCLOUD))) {
    return true;
  }
#endif
  if (api_struct_is_a(ptr->type, &ApiVolume) && (elem(type, -1, OB_VOLUME))) {
    return true;
  }
  /* try to get an object in the path, no pinning supported here */
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;

    if (ob && (elem(type, -1, ob->type))) {
      api_id_ptr_create(ob->data, &path->ptr[path->len]);
      path->len++;

      return true;
    }
  }

  /* no path to data possible */
  return false;
}
/* path mod */
static bool btns_cxt_path_mod(BtnsCxtPath *path)
{
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;

    if (elem(ob->type,
             OB_MESH,
             OB_CURVES_LEGACY,
             OB_FONT,
             OB_SURF,
             OB_LATTICE,
             OB_PEN,
             OB_CURVES,
             OB_POINTCLOUD,
             OB_VOLUME)) {
      ModData *md = dune_ob_active_mod(ob);
      if (md != NULL) {
        api_ptr_create(&ob->id, &ApiMod, md, &path->ptr[path->len]);
        path->len++;
      }

      return true;
    }
  }

  return false;
}

static bool btns_cxt_path_shaderfx(BtnsCxtPath *path)
{
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;

    if (ob && elem(ob->type, OB_PEN)) {
      return true;
    }
  }

  return false;
}

static bool btns_cxt_path_material(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we alrdy have a (pinned) material, we're done */
  if (api_struct_is_a(ptr->type, &ApiMaterial)) {
    return true;
  }
  /* if we have an ob, use the ob material slot */
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;

    if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type)) {
      Material *ma = dune_ob_material_get(ob, ob->actcol);
      if (ma != NULL) {
        api_id_ptr_create(&ma->id, &path->ptr[path->len]);
        path->len++;
      }
      return true;
    }
  }

  /* no path to a material possible */
  return false;
}

static bool btns_cxt_path_bone(BtnsCxtPath *path)
{
  /* if we have an armature, get the active bone */
  if (btns_cxt_path_data(path, OB_ARMATURE)) {
    Armature *arm = path->ptr[path->len - 1].data;

    if (arm->edbo) {
      if (arm->act_edbone) {
        EditBone *edbo = arm->act_edbone;
        api_ptr_create(&arm->id, &ApiEditBone, edbo, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
    else {
      if (arm->act_bone) {
        api_ptr_create(&arm->id, &ApiBone, arm->act_bone, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
  }

  /* no path to a bone possible */
  return false;
}

static bool btns_cxt_path_pose_bone(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we alrdy have a (pinned) PoseBone, we're done */
  if (api_struct_is_a(ptr->type, &ApiPoseBone)) {
    return true;
  }

  /* if we have an armature, get the active bone */
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;
    Armature *arm = ob->data; /* path->ptr[path->len-1].data - works too */

    if (ob->type != OB_ARMATURE || arm->edbo) {
      return false;
    }

    if (arm->act_bone) {
      PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, arm->act_bone->name);
      if (pchan) {
        api_ptr_create(&ob->id, &ApiPoseBone, pchan, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
  }

  /* no path to a bone possible */
  return false;
}

static bool btns_cxt_path_particle(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we alrdy have (pinned) particle settings, we're done */
  if (api_struct_is_a(ptr->type, &ApiParticleSettings)) {
    return true;
  }
  /* if we have an ob, get the active particle system */
  if (btns_cxt_path_ob(path)) {
    Ob *ob = path->ptr[path->len - 1].data;

    if (ob && ob->type == OB_MESH) {
      ParticleSys *psys = psys_get_current(ob);

      api_ptr_create(&ob->id, &ApiParticleSys, psys, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a particle sys possible */
  return false;
}

static bool btns_cxt_path_brush(const Cxt *C, BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) brush, we're done */
  if (api_struct_is_a(ptr->type, &ApiBrush)) {
    return true;
  }
  /* if we have a scene, use the toolsettings brushes */
  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;

    Brush *br = NULL;
    if (scene) {
      Win *window = cxt_win(C);
      ViewLayer *view_layer = win_get_active_view_layer(win);
      br = dune_paint_brush(dune_paint_get_active(scene, view_layer));
    }

    if (br) {
      api_id_ptr_create((Id *)br, &path->ptr[path->len]);
      path->len++;

      return true;
    }
  }

  /* no path to a brush possible */
  return false;
}

static bool btns_cxt_path_texture(const Cxt *C,
                                  BtnsCxtPath *path,
                                  BtnsCxtTexture *ct)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  if (!ct) {
    return false;
  }

  /* if we alrdy have a (pinned) texture, we're done */
  if (api_struct_is_a(ptr->type, &ApiTexture)) {
    return true;
  }

  if (!ct->user) {
    return false;
  }

  Id *id = ct->user->id;

  if (id) {
    if (GS(id->name) == ID_BR) {
      btns_cxt_path_brush(C, path);
    }
    else if (GS(id->name) == ID_PA) {
      btns_cxt_path_particle(path);
    }
    else if (GS(id->name) == ID_OB) {
      btns_cxt_path_ob(path);
    }
    else if (GS(id->name) == ID_LS) {
      btns_cxt_path_linestyle(path, cxt_wm_window(C));
    }
  }

  if (ct->texture) {
    api_id_ptr_create(&ct->texture->id, &path->ptr[path->len]);
    path->len++;
  }

  return true;
}

#ifdef WITH_FREESTYLE
static bool btns_cxt_linestyle_pinnable(const Cxt *C, ViewLayer *view_layer)
{
  Window *window = cxt_wm_window(C);
  Scene *scene = wm_window_get_active_scene(window);

  /* if Freestyle is disabled in the scene */
  if ((scene->r.mode & R_EDGE_FRS) == 0) {
    return false;
  }
  /* if Freestyle is not in the Parameter Editor mode */
  FreestyleConfig *config = &view_layer->freestyle_config;
  if (config->mode != FREESTYLE_CONTROL_EDITOR_MODE) {
    return false;
  }
  /* if the scene has already been pinned */
  SpaceProps *sbtns = cxt_win_space_props(C);
  if (sbtns->pinid && sbtns->pinid == &scene->id) {
    return false;
  }
  return true;
}
#endif

static bool btns_cxt_path(
    const Cxt *C, SpaceProps *sbtns, BtnsCxtPath *path, int mainb, int flag)
{
  /* We don't use cxt_data here, instead we get it from the win.
   * Otherwise there is a loop reading the cxt that we are setting. */
  Win *window = cxt_win(C);
  Scene *scene = win_get_active_scene(window);
  ViewLayer *view_layer = win_get_active_view_layer(window);

  memset(path, 0, sizeof(*path));
  path->flag = flag;

  /* If some ID datablock is pinned, set the root pointer. */
  if (sbuts->pinid) {
    Id *id = sbtns->pinid;

    api_id_ptr_create(id, &path->ptr[0]);
    path->len++;
  }
  /* No pinned root, use scene as initial root. */
  else if (mainb != CXT_TOOL) {
    api_id_ptr_create(&scene->id, &path->ptr[0]);
    path->len++;

    if (!elem(mainb,
              CXT_SCENE,
              CXT_RNDR,
              CXT_OUTPUT,
              CXT_VIEW_LAYER,
              CXT_WORLD)) {
      api_ptr_create(NULL, &ApiViewLayer, view_layer, &path->ptr[path->len]);
      path->len++;
    }
  }

  /* now for each btns cxt type, we try to construct a path,
   * tracing back recursively */
  bool found;
  switch (mainb) {
    case CXT_SCENE:
    case CXT_RNDR:
    case CXT_OUTPUT:
      found = btns_cxt_path_scene(path);
      break;
    case CXT_VIEW_LAYER:
#ifdef WITH_FREESTYLE
      if (btns_cxt_linestyle_pinnable(C, view_layer)) {
        found = btns_cxt_path_linestyle(path, window);
        if (found) {
          break;
        }
      }
#endif
      found = btns_cxt_path_view_layer(path, window);
      break;
    case CXT_WORLD:
      found = btns_cxt_path_world(path);
      break;
    case CXT_COLLECTION: /* This is for Line Art collection flags */
      found = btns_cxt_path_collection(C, path, window);
      break;
    case CXT_TOOL:
      found = true;
      break;
    case CXT_OB:
    case CXT_PHYS:
    case CXT_CONSTRAINT:
      found = btns_cxt_path_object(path);
      break;
    case CXT_MOD:
      found = btns_cxt_path_mod(path);
      break;
    case CXT_SHADERFX:
      found = btns_cxt_path_shaderfx(path);
      break;
    case CXT_DATA:
      found = btns_cxt_path_data(path, -1);
      break;
    case CXT_PARTICLE:
      found = btns_cxt_path_particle(path);
      break;
    case CXT_MATERIAL:
      found = btns_cxt_path_material(path);
      break;
    case CXT_TEXTURE:
      found = btns_cxt_path_texture(C, path, sbtns->texuser);
      break;
    case CXT_BONE:
      found = btns_cxt_path_bone(path);
      if (!found) {
        found = btns_cxt_path_data(path, OB_ARMATURE);
      }
      break;
    case CXT_BONE_CONSTRAINT:
      found = btns_cxt_path_pose_bone(path);
      break;
    default:
      found = false;
      break;
  }

  return found;
}

static bool btns_shading_cxt(const Cxt *C, int mainb)
{
  Win *window = cxt_win(C);
  ViewLayer *view_layer = win_get_active_view_layer(win);
  Ob *ob = OBACT(view_layer);

  if (elem(mainb, CXT_MATERIAL, CXT_WORLD, CXT_TEXTURE)) {
    return true;
  }
  if (mainb == CXT_DATA && ob && elem(ob->type, OB_LAMP, OB_CAMERA)) {
    return true;
  }

  return false;
}

static int btns_shading_new_cxt(const Cxt *C, int flag)
{
  Win *window = cxt_win(C);
  ViewLayer *view_layer = win_get_active_view_layer(window);
  Ob *ob = OBACT(view_layer);

  if (flag & (1 << CXT_MATERIAL)) {
    return CXT_MATERIAL;
  }
  if (ob && {elem(ob->type, OB_LAMP, OB_CAMERA) && (flag & (1 << BCONTEXT_DATA))) {
    return CXT_DATA;
  }
  if (flag & (1 << CXT_WORLD)) {
    return CXT_WORLD;
  }

  return CXT_RNDR;
}

void btns_cxt_compute(const Cxt *C, SpaceProps *sbtns)
{
  if (!sbtns->path) {
    sbtns->path = mem_callocn(sizeof(BtnsCxtPath), "BtnsCxtPath");
  }

  BtnsCxtPath *path = sbtns->path;

  int pflag = 0;
  int flag = 0;

  /* Set scene path. */
  btns_cxt_path(C, sbtns, path, CXT_SCENE, pflag);

  btns_texture_cxt_compute(C, sbtns);

  /* for each cxt, see if we can compute a valid path to it, if
   * this is the case, we know we have to display the button */
  for (int i = 0; i < CXT_TOT; i++) {
    if (btns_cxt_path(C, sbtns, path, i, pflag)) {
      flag |= (1 << i);

      /* setting icon for data cxt */
      if (i == CXT_DATA) {
        ApiPtr *ptr = &path->ptr[path->len - 1];

        if (ptr->type) {
          if (api_struct_is_a(ptr->type, &ApiLight)) {
            sbtns->dataicon = ICON_OUTLINER_DATA_LIGHT;
          }
          else {
            sbtns->dataicon = api_struct_ui_icon(ptr->type);
          }
        }
        else {
          sbtns->dataicon = ICON_EMPTY_DATA;
        }
      }
    }
  }

  /* always try to use the tab that was explicitly
   * set to the user, so that once that cxt comes
   * back, the tab is activated again */
  sbtns->mainb = sbtns->mainbuser;

  /* in case something becomes invalid, change */
  if ((flag & (1 << sbtns->mainb)) == 0) {
    if (sbtns->flag & SB_SHADING_CXT) {
      /* try to keep showing shading related btns */
      sbtns->mainb = btns_shading_new_cxt(C, flag);
    }
    else if (flag & CXT_OB) {
      sbtns->mainb = CXT_OB;
    }
    else {
      for (int i = 0; i < CXT_TOT; i++) {
        if (flag & (1 << i)) {
          sbtns->mainb = i;
          break;
        }
      }
    }
  }

  btns_cxt_path(C, sbtns, path, sbtns->mainb, pflag);

  if (!(flag & (1 << sbtns->mainb))) {
    if (flag & (1 << CXT_OB)) {
      sbtns->mainb = CXT_OB;
    }
    else {
      sbtns->mainb = CXT_SCENE;
    }
  }

  if (btns_shading_cxt(C, sbtns->mainb)) {
    sbtns->flag |= SB_SHADING_CXT;
  }
  else {
    sbtns->flag &= ~SB_SHADING_CXT;
  }

  sbtns->pathflag = flag;
}

static bool is_ptr_in_path(BtnsCxtPath *path, ApiPtr *ptr)
{
  for (int i = 0; i < path->len; ++i) {
    if (ptr->owner_id == path->ptr[i].owner_id) {
      return true;
    }
  }
  return false;
}

bool ed_btns_should_sync_with_outliner(const Cxt *C,
                                       const SpaceProps *sbtns,
                                       ScrArea *area)
{
  ScrArea *active_area = cxt_win_area(C);
  const bool auto_sync = ed_area_has_shared_border(active_area, area) &&
                         sbtns->outliner_sync == PROPS_SYNC_AUTO;
  return auto_sync || sbtns->outliner_sync == PROPS_SYNC_ALWAYS;
}

void ed_btns_set_cxt(const Cxt *C,
                    SpaceProps *sbtns,
                    ApiPtr *ptr,
                    const int cxt)
{
  BtnsCxtPath path;
  if (btns_cxt_path(C, sbtns, &path, cxt, 0) && is_ptr_in_path(&path, ptr)) {
    sbtns->mainbuser = cxt;
    sbtns->mainb = sbtns->mainbuser;
  }
}

/* Cxt Cb */
const char *btns_cxt_dir[] = {
    "texture_slot",
    "scene",
    "world",
    "ob",
    "mesh",
    "armature",
    "lattice",
    "curve",
    "meta_ball",
    "light",
    "speaker",
    "lightprobe",
    "camera",
    "material",
    "material_slot",
    "texture",
    "texture_user",
    "texture_user_prop",
    "bone",
    "edit_bone",
    "pose_bone",
    "particle_sys",
    "particle_sys_editable",
    "particle_settings",
    "cloth",
    "soft_body",
    "fluid",
    "collision",
    "brush",
    "dynamic_paint",
    "line_style",
    "collection",
    "pen",
#ifdef WITH_NEW_CURVES_TYPE
    "curves",
#endif
#ifdef WITH_POINT_CLOUD
    "pointcloud",
#endif
    "volume",
    NULL,
};

int /*eCxtResult*/ btns_cxt(const Cxt *C,
                            const char *member,
                            CxtDataResult *result)
{
  SpaceProps *sbtns = cxt_win_space_props(C);
  if (sbtns && sbtns->path == NULL) {
    /* path is cleared for SCREEN_OT_redo_last, when global undo does a file-read which clears the
     * path (see lib_link_workspace_layout_restore). */
    btns_cxt_compute(C, sbtns);
  }
  BtnsCxtPath *path = sbtns ? sbtns->path : NULL;

  if (!path) {
    return CXT_RESULT_MEMBER_NOT_FOUND;
  }

  if (sbtns->mainb == CXT_TOOL) {
    return CXT_RESULT_MEMBER_NOT_FOUND;
  }

  /* here we handle context, getting data from precomputed path */
  if (cxt_data_dir(member)) {
    /* in case of new shading system we skip texture_slot, complex python
     * UI script logic depends on checking if this is available */
    if (sbtns->texuser) {
      cxt_data_dir_set(result, btns_cxt_dir + 1);
    }
    else {
      cxt_data_dir_set(result, btns_cxt_dir);
    }
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "scene")) {
    /* Do not return one here if scene is not found in path,
     * in this case we want to get default cxt scene! */
    return set_ptr_type(path, result, &ApiScene);
  }
  if (cxt_data_equals(member, "world")) {
    set_ptr_type(path, result, &ApiWorld);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "collection")) {
    /* Do not return one here if collection is not found in path,
     * in this case we want to get default cxt collection! */
    return set_ptr_type(path, result, &ApiCollection);
  }
  if (cxt_data_equals(member, "ob")) {
    set_ptr_type(path, result, &ApiOb);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "mesh")) {
    set_ptr_type(path, result, &ApiMesh);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "armature")) {
    set_ptr_type(path, result, &ApiArmature);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "lattice")) {
    set_ptr_type(path, result, &ApiLattice);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "curve")) {
    set_ptr_type(path, result, &ApiCurve);
    return CXT_RESULT_OK;
  }
  if cxt_data_equals(member, "meta_ball")) {
    set_ptr_type(path, result, &ApiMetaBall);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "light")) {
    set_ptr_type(path, result, &ApiLight);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "camera")) {
    set_ptr_type(path, result, &ApiCamera);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "speaker")) {
    set_ptr_type(path, result, &ApiSpeaker);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "lightprobe")) {
    set_ptr_type(path, result, &ApiLightProbe);
    return CXT_RESULT_OK;
  }
#ifdef WITH_NEW_CURVES_TYPE
  if (cxt_data_equals(member, "curves")) {
    set_ptr_type(path, result, &ApiCurves);
    return CXT_RESULT_OK;
  }
#endif
#ifdef WITH_POINT_CLOUD
  if (cxt_data_equals(member, "pointcloud")) {
    set_ptr_type(path, result, &ApiPointCloud);
    return CXT_RESULT_OK;
  }
#endif
  if (cxt_data_equals(member, "volume")) {
    set_ptr_type(path, result, &ApiVolume);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "material")) {
    set_ptr_type(path, result, &ApiMaterial);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "texture")) {
    BtnsCxtTexture *ct = sbtns->texuser;

    if (ct) {
      if (ct->texture == NULL) {
        return CXT_RESULT_NO_DATA;
      }

      cxt_data_ptr_set(result, &ct->texture->id, &ApiTexture, ct->texture);
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "material_slot")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiObject);

    if (ptr) {
      Ob *ob = ptr->data;

      if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type) && ob->totcol) {
        /* a valid actcol isn't ensured T27526. */
        int matnr = ob->actcol - 1;
        if (matnr < 0) {
          matnr = 0;
        }
        /* Keep aligned with api_Object_material_slots_get. */
        cxt_data_ptr_set(
            result, &ob->id, &ApiMaterialSlot, (void *)(matnr + (uintptr_t)&ob->id));
      }
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "texture_user")) {
    BtnsCxtTexture *ct = sbtns->texuser;

    if (!ct) {
      return CXT_RESULT_NO_DATA;
    }

    if (ct->user && ct->user->ptr.data) {
      BtnsTextureUser *user = ct->user;
      cxt_data_ptr_set_ptr(result, &user->ptr);
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "texture_user_prop")) {
    BtnsCxtTexture *ct = sbtns->texuser;

    if (!ct) {
      return CXT_RESULT_NO_DATA;
    }

    if (ct->user && ct->user->ptr.data) {
      BtnsTextureUser *user = ct->user;
      cxt_data_ptr_set(result, NULL, &ApiProp, user->prop);
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "texture_node")) {
    BtnsCxtTexture *ct = sbtns->texuser;

    if (ct) {
      /* new shading system */
      if (ct->user && ct->user->node) {
        cxt_data_ptr_set(result, &ct->user->ntree->id, &Api_Node, ct->user->node);
      }

      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }
  if (cxt_data_equals(member, "texture_slot")) {
    BtnsCxtTexture *ct = sbtns->texuser;
    ApiPtr *ptr;

    /* Particles slots are used in both old and new textures handling. */
    if ((ptr = get_ptr_type(path, &ApiParticleSystem))) {
      ParticleSettings *part = ((ParticleSystem *)ptr->data)->part;

      if (part) {
        cxt_data_ptr_set(
            result, &part->id, &ApiParticleSettingsTextureSlot, part->mtex[(int)part->texact]);
      }
    }
    else if (ct) {
      return CXT_RESULT_MEMBER_NOT_FOUND; /* new shading system */
    }
    else if ((ptr = get_ptr_type(path, &ApiFreestyleLineStyle))) {
      FreestyleLineStyle *ls = ptr->data;

      if (ls) {
        cxt_data_ptr_set(
            result, &ls->id, &ApiLineStyleTextureSlot, ls->mtex[(int)ls->texact]);
      }
    }

    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "bone")) {
    set_ptr_type(path, result, &ApiBone);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "edit_bone")) {
    set_ptr_type(path, result, &ApiEditBone);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "pose_bone")) {
    set_ptr_type(path, result, &ApiPoseBone);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "particle_system")) {
    set_ptr_type(path, result, &ApiParticleSystem);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "particle_system_editable")) {
    if (pe_poll((Cxt *)C)) {
      set_ptr_type(path, result, &ApiParticleSystem);
    }
    else {
      cxt_data_ptr_set(result, NULL, &ApiParticleSystem, NULL);
    }
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "particle_settings")) {
    /* only available when pinned */
    ApiPtr *ptr = get_ptr_type(path, &ApiParticleSettings);

    if (ptr && ptr->data) {
      cxt_data_ptr_set_ptr(result, ptr);
      return CXT_RESULT_OK;
    }

    /* get settings from active particle system instead */
    ptr = get_ptr_type(path, &ApiParticleSystem);

    if (ptr && ptr->data) {
      ParticleSettings *part = ((ParticleSystem *)ptr->data)->part;
      cxt_data_ptr_set(result, ptr->owner_id, &ApiParticleSettings, part);
      return CXT_RESULT_OK;
    }

    set_ptr_type(path, result, &ApiParticleSettings);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "cloth")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiOb);

    if (ptr && ptr->data) {
      Ob *ob = ptr->data;
      ModData *md = dune_mods_findby_type(ob, eModType_Cloth);
      cxt_data_ptr_set(result, &ob->id, &ApiClothMod, md);
      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }
  if (cxt_data_equals(member, "soft_body")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiOb);

    if (ptr && ptr->data) {
      Ob *ob = ptr->data;
      ModData *md = dune_mods_findby_type(ob, eModType_Softbody);
      cxt_data_ptr_set(result, &ob->id, &ApiSoftBodyMod, md);
      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }

  if (cxt_data_equals(member, "fluid")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiObject);

    if (ptr && ptr->data) {
      Ob *ob = ptr->data;
      ModData *md = dune_mods_findby_type(ob, eModType_Fluid);
      cxt_data_ptr_set(result, &ob->id, &ApiFluidMod, md);
      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }
  if (cxt_data_equals(member, "collision")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiObject);

    if (ptr && ptr->data) {
      Ob *ob = ptr->data;
      ModData *md = dune_mods_findby_type(ob, eModType_Collision);
      cxt_data_ptr_set(result, &ob->id, &ApiCollisionMod, md);
      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }
  if (cxt_data_equals(member, "brush")) {
    set_ptr_type(path, result, &ApiBrush);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "dynamic_paint")) {
    ApiPtr *ptr = get_ptr_type(path, &ApiObject);

    if (ptr && ptr->data) {
      Ob *ob = ptr->data;
      ModData *md = dune_mods_findby_type(ob, eModType_DynamicPaint);
      cxt_data_ptr_set(result, &ob->id, &ApiDynamicPaintMod, md);
      return CXT_RESULT_OK;
    }
    return CXT_RESULT_NO_DATA;
  }
  if (cxt_data_equals(member, "line_style")) {
    set_ptr_type(path, result, &ApiFreestyleLineStyle);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "pen")) {
    set_ptr_type(path, result, &Api_Pen);
    return CXT_RESULT_OK;
  }
  return CXT_RESULT_MEMBER_NOT_FOUND;
}

/* Drawing the Path */
static bool btns_pnl_cxt_poll(const Cxt *C, PnlType *UNUSED(pt))
{
  SpaceProps *sbtns = cxt_win_space_props(C);
  return sbtns->mainb != CXT_TOOL;
}

static void btns_pnl_cxt_drw(const Cxt *C, Pnl *pnl)
{
  SpaceProps *sbtns = cxt_win_space_props(C);
  BtnsCxtPath *path = sbtns->path;

  if (!path) {
    return;
  }

  uiLayout *row = uiLayoutRow(panel->layout, true);
  uiLayoutSetAlignment(row, UI_LAYOUT_ALIGN_LEFT);

  bool first = true;
  for (int i = 0; i < path->len; i++) {
    ApiPtr *ptr = &path->ptr[i];

    /* Skip scene and view layer to save space. */
    if ((!elem(sbtns->mainb,
               CXT_RNDR,
               CXT_OUTPUT,
               CXT_SCENE,
               CXT_VIEW_LAYER,
               CXT_WORLD) &&
         ptr->type == &ApiScene)) {
      continue;
    }
    if ((!elem(sbtns->mainb,
               CXT_RNDR,
               CXT_OUTPUT,
               CXT_SCENE,
               CXT_VIEW_LAYER,
               CXT_WORLD) &&
         ptr->type == &Api_ViewLayer)) {
      continue;
    }

    /* Add > triangle. */
    if (!first) {
      uiItemL(row, "", ICON_RIGHTARROW);
    }

    if (ptr->data == NULL) {
      continue;
    }

    /* Add icon and name. */
    int icon = api_struct_ui_icon(ptr->type);
    char namebuf[128];
    char *name = api_struct_name_get_alloc(ptr, namebuf, sizeof(namebuf), NULL);

    if (name) {
      uiItemLDrag(row, ptr, name, icon);

      if (name != namebuf) {
        mem_freen(name);
      }
    }
    else {
      uiItemL(row, "", icon);
    }

    first = false;
  }

  uiLayout *pin_row = uiLayoutRow(row, false);
  uiLayoutSetAlignment(pin_row, UI_LAYOUT_ALIGN_RIGHT);
  uiItemSpacer(pin_row);
  uiLayoutSetEmboss(pin_row, UI_EMBOSS_NONE);
  uiItemO(pin_row,
          "",
          (sbuts->flag & SB_PIN_CXT) ? ICON_PINNED : ICON_UNPINNED,
          "btns_ot_toggle_pin");
}

void btns_cxt_register(ARgnType *art)
{
  PanelType *pt = mem_callocn(sizeof(PnlType), "spacetype btns panel cxt");
  strcpy(pt->idname, "props_pt_cxt");
  strcpy(pt->label, N_("Cxt")); /* XXX C panels unavailable through API bpy.types! */
  strcpy(pt->lang_cxt, LANG_CXT_DEFAULT_API);
  pt->poll = btns_pnl_cxt_poll;
  pt->drw = btns_pnl_cxt_draw;
  pt->flag = PANEL_TYPE_NO_HEADER | PNL_TYPE_NO_SEARCH;
  lib_addtail(&art->pnltypes, pt);
}

Id *btns_cxt_id_path(const Cxt *C)
{
  SpaceProps *sbtns = cxt_win_space_props(C);
  BtnsCxtPath *path = sbtns->path;

  if (path->len == 0) {
    return NULL;
  }

  for (int i = path->len - 1; i >= 0; i--) {
    ApiPtr *ptr = &path->ptr[i];

    /* Pin particle settings instead of system, since only settings are an idblock. */
    if (sbtns->mainb == CXT_PARTICLE && sbtns->flag & SB_PIN_CXT) {
      if (ptr->type == &Api_ParticleSys && ptr->data) {
        ParticleSys *psys = ptr->data;
        return &psys->part->id;
      }
    }

    /* There is no valid image ID panel, Image Empty objects need this workaround. */
    if (sbtns->mainb == CXT_DATA && sbtns->flag & SB_PIN_CXT) {
      if (ptr->type == &Api_Img && ptr->data) {
        continue;
      }
    }

    if (ptr->owner_id) {
      return ptr->owner_id;
    }
  }

  return NULL;
}
