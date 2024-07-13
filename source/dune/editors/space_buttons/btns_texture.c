#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_id.h"
#include "types_brush.h"
#include "types_linestyle.h"
#include "types_node.h"
#include "types_ob_force.h"
#include "types_ob.h"
#include "types_particle.h"
#include "types_scene.h"
#include "types_screa.h"
#include "types_space.h"
#include "types_win.h"

#include "dune_cxt.h"
#include "dune_pen_mod.h"
#include "dune_layer.h"
#include "dune_linestyle.h"
#include "dune_mod.h"
#include "dune_node.h"
#include "dune_paint.h"
#include "dune_particle.h"
#ifdef WITH_FREESTYLE
#endif

#include "api_access.h"
#include "api_prototypes.h"

#include "ui.h"
#include "ui_resources.h"

#include "ed_node.h"
#include "ed_screen.h"

#include "win_api.h"
#include "win_types.h"

#include "../ui/ui_intern.h"

#include "btns_intern.h" /* own include */

static ScrArea *find_area_props(const Cxt *C);
static SpaceProps *find_space_props(const Cxt *C);

/* Texture User */

static void btns_texture_user_socket_prop_add(List *users,
                                              Id *id,
                                              ApiPtr ptr,
                                              ApiProp *prop,
                                              NodeTree *ntree,
                                              Node *node,
                                              NodeSocket *socket,
                                              const char *category,
                                              int icon,
                                              const char *name)
{
  BtnsTextureUser *user = mem_callocn(sizeof(BtnsTextureUser), "BtnsTextureUser");

  user->id = id;
  user->ptr = ptr;
  user->prop = prop;
  user->ntree = ntree;
  user->node = node;
  user->socket = socket;
  user->category = category;
  user->icon = icon;
  user->name = name;
  user->index = lib_list_count(users);

  lib_addtail(users, user);
}

static void btns_texture_user_prop_add(List *users,
                                       Id *id,
                                       ApiPtr ptr,
                                       ApiProp *prop,
                                       const char *category,
                                       int icon,
                                       const char *name)
{
  BtnsTextureUser *user = mem_callocn(sizeof(BtnsTextureUser), "BtnsTextureUser");

  user->id = id;
  user->ptr = ptr;
  user->prop = prop;
  user->category = category;
  user->icon = icon;
  user->name = name;
  user->index = lib_list_count(users);

  lib_addtail(users, user);
}

static void btns_texture_user_node_add(List *users,
                                       Id *id,
                                       NodeTree *ntree,
                                       Node *node,
                                       const char *category,
                                       int icon,
                                       const char *name)
{
  BtnsTextureUser *user = mem_callocn(sizeof(BtnsTextureUser), "BtnsTextureUser");

  user->id = id;
  user->ntree = ntree;
  user->node = node;
  user->category = category;
  user->icon = icon;
  user->name = name;
  user->index = lib_list_count(users);

  lib_addtail(users, user);
}

static void btns_texture_users_find_nodetree(List *users,
                                             Id *id,
                                             NodeTree *ntree,
                                             const char *category)
{
  Node *node;

  if (ntree) {
    for (node = ntree->nodes.first; node; node = node->next) {
      if (node->typeinfo->nclass == NODE_CLASS_TEXTURE) {
        ApiPtr ptr;
        // ApiProp *prop; /* UNUSED */

        api_ptr_create(&ntree->id, &ApiNode, node, &ptr);
        // prop = api_struct_find_prop(&ptr, "texture"); /* UNUSED */

        btns_texture_user_node_add(
            users, id, ntree, node, category, api_struct_ui_icon(ptr.type), node->name);
      }
      else if (node->type == NODE_GROUP && node->id) {
        btns_texture_users_find_nodetree(users, id, (NodeTree *)node->id, category);
      }
    }
  }
}

static void btns_texture_mod_geonodes_users_add(Ob *ob,
                                                NodesModData *nmd,
                                                NodeTree *node_tree,
                                                List *users)
{
  ApiPtr ptr;
  ApiProp *prop;

  LIST_FOREACH (Node *, node, &node_tree->nodes) {
    if (node->type == NODE_GROUP && node->id) {
      /* Recurse into the node group */
      btns_texture_mod_geonodes_users_add(ob, nmd, (NodeTree *)node->id, users);
    }
    LIST_FOREACH (NodeSocket *, socket, &node->inputs) {
      if (socket->flag & SOCK_UNAVAIL) {
        continue;
      }
      if (socket->type != SOCK_TEXTURE) {
        continue;
      }
      api_ptr_create(&node_tree->id, &api_NodeSocket, socket, &ptr);
      prop = api_struct_find_prop(&ptr, "default_value");

      ApiPtr texptr = api_prop_ptr_get(&ptr, prop);
      Tex *tex = (api_struct_is_a(texptr.type, &api_Texture)) ? (Tex *)texptr.data : NULL;
      if (tex != NULL) {
        btns_texture_user_socket_prop_add(users,
                                          &ob->id,
                                          ptr,
                                          prop,
                                          node_tree,
                                          node,
                                          socket,
                                          N_("Geo Nodes"),
                                          api_struct_ui_icon(ptr.type),
                                          nmd->mod.name);
      }
    }
  }
}

static void btns_texture_mod_foreach(void *userData,
                                     Ob *ob,
                                     ModData *md,
                                     const char *propname)
{
  List *users = userData;

  if (md->type == eModType_Nodes) {
    NodesModData *nmd = (NodesModData *)md;
    if (nmd->node_group != NULL) {
      btns_texture_mod_geonodes_users_add(ob, nmd, nmd->node_group, users);
    }
  }
  else {
    ApiPtr ptr;
    ApiProp *prop;

    api_ptr_create(&ob->id, &api_Mod, md, &ptr);
    prop = api_struct_find_prop(&ptr, propname);

    btns_texture_user_prop_add(
        users, &ob->id, ptr, prop, N_("Mods"), api_struct_ui_icon(ptr.type), md->name);
  }
}

static void btns_texture_mod_pen_foreach(void *userData,
                                         Ob *ob,
                                         PenModData *md,
                                         const char *propname)
{
  ApiPtr ptr;
  PropApi *prop;
  List *users = userData;

  api_ptr_create(&ob->id, &ApiPenMod, md, &ptr);
  prop = api_struct_find_prop(&ptr, propname);

  btns_texture_user_prop_add(users,
                             &ob->id,
                             ptr,
                             prop,
                             N_("Pen Mods"),
                             api_struct_ui_icon(ptr.type),
                             md->name);
}

static void btns_texture_users_from_cxt(List *users,
                                        const Cxt *C,
                                        SpaceProps *sbtns)
{
  Scene *scene = NULL;
  Ob *ob = NULL;
  FreestyleLineStyle *linestyle = NULL;
  Brush *brush = NULL;
  Id *pinid = sbtns->pinid;
  bool limited_mode = (sbtns->flag & SB_TEX_USER_LIMITED) != 0;

  /* get data from context */
  if (pinid) {
    if (GS(pinid->name) == ID_SCE) {
      scene = (Scene *)pinid;
    }
    else if (GS(pinid->name) == ID_OB) {
      ob = (Ob *)pinid;
    }
    else if (GS(pinid->name) == ID_BR) {
      brush = (Brush *)pinid;
    }
    else if (GS(pinid->name) == ID_LS) {
      linestyle = (FreestyleLineStyle *)pinid;
    }
  }

  if (!scene) {
    scene = cxt_data_scene(C);
  }

  const ID_Type id_type = pinid != NULL ? GS(pinid->name) : -1;
  if (!pinid || id_type == ID_SCE) {
    Win *win = cxt_win(C);
    ViewLayer *view_layer = (win->scene == scene) ? win_get_active_view_layer(win) :
                                                    dune_view_layer_default_view(scene);

    brush = dune_paint_brush(dune_paint_get_active_from_cxt(C));
    linestyle = dune_linestyle_active_from_view_layer(view_layer);
    ob = obact(view_layer);
  }

  /* fill users */
  lib_list_clear(users);

  if (linestyle && !limited_mode) {
    btns_texture_users_find_nodetree(
        users, &linestyle->id, linestyle->nodetree, N_("Line Style"));
  }

  if (ob) {
    ParticleSys *psys = psys_get_current(ob);
    MTex *mtex;
    int a;

    /* mods */
    dune_mods_foreach_tex_link(ob, btns_texture_mod_foreach, users);

    /* pen mods */
    dune_pen_mods_foreach_tex_link(ob, btns_texture_mod_pen_foreach, users);

    /* particle systems */
    if (psys && !limited_mode) {
      for (a = 0; a < MAX_MTEX; a++) {
        mtex = psys->part->mtex[a];

        if (mtex) {
          ApiPtr ptr;
          ApiProp *prop;

          api_ptr_create(&psys->part->id, &api_ParticleSettingsTextureSlot, mtex, &ptr);
          prop = api_struct_find_prop(&ptr, "texture");

          btns_texture_user_prop_add(users,
                                     &psys->part->id,
                                     ptr,
                                     prop,
                                     N_("Particles"),
                                     api_struct_ui_icon(&api_ParticleSettings),
                                     psys->name);
        }
      }
    }

    /* field */
    if (ob->pd && ob->pd->forcefield == PFIELD_TEXTURE) {
      ApiPtr ptr;
      PropApi *prop;

      api_ptr_create(&ob->id, &api_FieldSettings, ob->pd, &ptr);
      prop = api_struct_find_prop(&ptr, "texture");

      btns_texture_user_prop_add(
          users, &ob->id, ptr, prop, N_("Fields"), ICON_FORCE_TEXTURE, IFACE_("Texture Field"));
    }
  }

  /* brush */
  if (brush) {
    ApiApi ptr;
    ApiProp *prop;

    /* texture */
    api_ptr_create(&brush->id, &api_BrushTextureSlot, &brush->mtex, &ptr);
    prop = api_struct_find_prop(&ptr, "texture");

    btns_texture_user_prop_add(
        users, &brush->id, ptr, prop, N_("Brush"), ICON_BRUSH_DATA, IFACE_("Brush"));

    /* mask texture */
    api_ptr_create(&brush->id, &api_BrushTextureSlot, &brush->mask_mtex, &ptr);
    prop = api_struct_find_prop(&ptr, "texture");

    btns_texture_user_prop_add(
        users, &brush->id, ptr, prop, N_("Brush"), ICON_BRUSH_DATA, IFACE_("Brush Mask"));
  }
}

void btns_texture_cxt_compute(const Cxt *C, SpaceProps *sbtns)
{
  /* gather available texture users in cxt. runs on every draw of
   * props editor, before the btns are created. */
  BtnsCxtTexture *ct = sbtns->texuser;
  Id *pinid = sbtns->pinid;

  if (!ct) {
    ct = mem_callocn(sizeof(BtnsCxtTexture), "BtnsCxtTexture");
    sbuts->texuser = ct;
  }
  else {
    lib_freelistn(&ct->users);
  }

  btns_texture_users_from_cxt(&ct->users, C, sbtns);

  if (pinid && GS(pinid->name) == ID_TE) {
    ct->user = NULL;
    ct->texture = (Tex *)pinid;
  }
  else {
    /* set one user as active based on active index */
    if (ct->index >= lib_list_count_at_most(&ct->users, ct->index + 1)) {
      ct->index = 0;
    }

    ct->user = lib_findlink(&ct->users, ct->index);
    ct->texture = NULL;

    if (ct->user) {
      if (ct->user->node != NULL) {
        /* Detect change of active texture node in same node tree, in that
         * case we also automatically switch to the other node. */
        if ((ct->user->node->flag & NODE_ACTIVE_TEXTURE) == 0) {
          BtnsTextureUser *user;
          for (user = ct->users.first; user; user = user->next) {
            if (user->ntree == ct->user->ntree && user->node != ct->user->node) {
              if (user->node->flag & NODE_ACTIVE_TEXTURE) {
                ct->user = user;
                ct->index = lib_findindex(&ct->users, user);
                break;
              }
            }
          }
        }
      }
      if (ct->user->ptr.data) {
        ApiPtr texptr;
        Tex *tex;

        /* Get texture datablock ptr if it's a prop. */
        texptr = api_prop_ptr_get(&ct->user->ptr, ct->user->prop);
        tex = (api_struct_is_a(texptr.type, &api_Texture)) ? texptr.data : NULL;

        ct->texture = tex;
      }
    }
  }
}

static void template_texture_sel(Cxt *C, void *user_p, void *UNUSED(arg))
{
  /* callback when sel a texture user in the menu */
  SpaceProps *sbtns = find_space_props(C);
  BtnsCxtTexture *ct = (sbtns) ? sbtns->texuser : NULL;
  BtnsTextureUser *user = (BtnsTextureUser *)user_p;
  ApiPtr texptr;
  Tex *tex;

  if (!ct) {
    return;
  }

  /* set user as active */
  if (user->node) {
    ed_node_set_active(cxt_data_main(C), NULL, user->ntree, user->node, NULL);
    ct->texture = NULL;

    /* Not totally sure if we should also change selection? */
    LIST_FOREACH (Node *, node, &user->ntree->nodes) {
      nodeSetSelected(node, false);
    }
    nodeSetSel(user->node, true);
    win_ev_add_notifier(C, NC_NODE | NA_SELECTED, NULL);
  }
  if (user->ptr.data) {
    texptr = api_prop_ptr_get(&user->ptr, user->prop);
    tex = (api_struct_is_a(texptr.type, &api_Texture)) ? texptr.data : NULL;

    ct->texture = tex;

    if (user->ptr.type == &api_ParticleSettingsTextureSlot) {
      /* stupid exception for particle systems which still uses influence
       * from the old texture system, set the active texture slots as well */
      ParticleSettings *part = (ParticleSettings *)user->ptr.owner_id;
      int a;

      for (a = 0; a < MAX_MTEX; a++) {
        if (user->ptr.data == part->mtex[a]) {
          part->texact = a;
        }
      }
    }

    if (sbtns && tex) {
      sbtns->preview = 1;
    }
  }

  ct->user = user;
  ct->index = user->index;
}

static void template_texture_user_menu(Cxt *C, uiLayout *layout, void *UNUSED(arg))
{
  /* callback when opening texture user sel menu, to create btns. */
  SpaceProps *sbtns = cxt_win_space_props(C);
  BtnsCtxTexture *ct = sbtns->texuser;
  BtnsTextureUser *user;
  uiBlock *block = uiLayoutGetBlock(layout);
  const char *last_category = NULL;

  for (user = ct->users.first; user; user = user->next) {
    uiBtn *btn;
    char name[UI_MAX_NAME_STR];

    /* add label per category */
    if (!last_category || !STREQ(last_category, user->category)) {
      uiItemL(layout, IFACE_(user->category), ICON_NONE);
      btn = block->btns.last;
      btn->drwflag = UI_BTN_TEXT_LEFT;
    }

    /* create btn */
    if (user->prop) {
      ApiPtr texptr = api_prop_ptr_get(&user->ptr, user->prop);
      Tex *tex = texptr.data;

      if (tex) {
        lib_snprintf(name, UI_MAX_NAME_STR, "  %s - %s", user->name, tex->id.name + 2);
      }
      else {
        lib_snprintf(name, UI_MAX_NAME_STR, "  %s", user->name);
      }
    }
    else {
      lib_snprintf(name, UI_MAX_NAME_STR, "  %s", user->name);
    }

    btn = uiDefIconTextBtn(block,
                           UI_BTYPE_BTN,
                           0,
                           user->icon,
                           name,
                           0,
                           0,
                           UI_UNIT_X * 4,
                           UI_UNIT_Y,
                           NULL,
                           0.0,
                           0.0,
                           0.0,
                           0.0,
                           "");
    ui_btn_fn_new_set(btn, template_texture_sel, mem_dupallocn(user), NULL);

    last_category = user->category;
  }

  ui_block_flag_enable(block, UI_BLOCK_NO_FLIP);
}

void uiTemplateTextureUser(uiLayout *layout, Cxt *C)
{
  /* Texture user selection drop-down menu. the available users have been
   * gathered before drawing in BtsnCxtTexture, we merely need to
   * display the current item. */
  SpaceProps *sbtns = cxt_win_space_props(C);
  BtnsCxtTexture *ct = (sbtns) ? sbtns->texuser : NULL;
  uiBlock *block = uiLayoutGetBlock(layout);
  uiBtn *btn;
  BtnsTextureUser *user;
  char name[UI_MAX_NAME_STR];

  if (!ct) {
    return;
  }

  /* get current user */
  user = ct->user;

  if (!user) {
    uiItemL(layout, TIP_("No textures in cxt"), ICON_NONE);
    return;
  }

  /* create btn */
  lib_strncpy(name, user->name, UI_MAX_NAME_STR);

  if (user->icon) {
    btn = uiDefIconTextMenuBtn(block,
                               template_texture_user_menu,
                               NULL,
                               user->icon,
                               name,
                               0,
                               0,
                               UI_UNIT_X * 4,
                               UI_UNIT_Y,
                               "");
  }
  else {
    btn = uiDefMenuBtn(
        block, template_texture_user_menu, NULL, name, 0, 0, UI_UNIT_X * 4, UI_UNIT_Y, "");
  }

  /* some cosmetic tweaks */
  ui_btn_type_set_menu_from_pulldown(btn);

  btn->flag &= ~UI_BTN_ICON_SUBMENU;
}

/* Texture Show **************************/
static ScrArea *find_area_props(const Cxt *C)
{
  Screen *screen = cxt_win_screen(C);
  Object *ob = cxt_data_active_ob(C);

  LIST_FOREACH (ScrArea *, area, &screen->areabase) {
    if (area->spacetype == SPACE_PROPS) {
      /* Only if unpinned, or if pinned object matches. */
      SpaceProps *sbtns = area->spacedata.first;
      Id *pinid = sbtns->pinid;
      if (pinid == NULL || ((GS(pinid->name) == ID_OB) && (Object *)pinid == ob)) {
        return area;
      }
    }
  }

  return NULL;
}

static SpaceProps *find_space_props(const Cxt *C)
{
  ScrArea *area = find_area_props(C);
  if (area != NULL) {
    return area->spacedata.first;
  }

  return NULL;
}

static void template_texture_show(Cxt *C, void *data_p, void *prop_p)
{
  if (data_p == NULL || prop_p == NULL) {
    return;
  }

  ScrArea *area = find_area_props(C);
  if (area == NULL) {
    return;
  }

  SpaceProps *sbtns = (SpaceProps *)area->spacedata.first;
  BtnsCxtTexture *ct = (sbtns) ? sbtns->texuser : NULL;
  if (!ct) {
    return;
  }

  BtnsTextureUser *user;
  for (user = ct->users.first; user; user = user->next) {
    if (user->ptr.data == data_p && user->prop == prop_p) {
      break;
    }
  }

  if (user) {
    /* sel texture */
    template_texture_sel(C, user, NULL);

    /* change cxt */
    sbtns->maind = CXT_TEXTURE;
    sbtns->mainbuser = sbtns->maind;
    sbtns->preview = 1;

    /* redrw editor */
    ed_area_tag_redrw(area);
  }
}

void uiTemplateTextureShow(uiLayout *layout, const Cxt *C, ApiPtr *ptr, ApiProp *prop)
{
  /* Only show the btn if there is actually a texture assigned. */
  Tex *texture = api_prop_ptr_get(ptr, prop).data;
  if (texture == NULL) {
    return;
  }

  /* Only show the btn if we are not in the Props Editor's texture tab. */
  SpaceProps *sbtns_cxt = cxt_win_space_props(C);
  if (sbtns_cxt != NULL && sbtns_cxt->maind == CXT_TEXTURE) {
    return;
  }

  SpaceProps *sbtns = find_space_prop(C);
  BtnsCxtTexture *ct = (sbtns) ? sbtns->texuser : NULL;

  /* find corresponding texture user */
  BtnsTextureUser *user;
  bool user_found = false;
  if (ct != NULL) {
    for (user = ct->users.first; user; user = user->next) {
      if (user->ptr.data == ptr->data && user->prop == prop) {
        user_found = true;
        break;
      }
    }
  }

  /* Draw btn (disabled if we cannot find a Props Editor to display this in). */
  uiBlock *block = uiLayoutGetBlock(layout);
  uiBtn *btn;
  btn = uiDefIconBtn(block,
                     UI_BTYPE_BTN,
                     0,
                     ICON_PROPS,
                     0,
                     0,
                     UI_UNIT_X,
                     UI_UNIT_Y,
                     NULL,
                     0.0,
                     0.0,
                     0.0,
                     0.0,
                     TIP_("Show texture in texture tab"));
  ui_btn_fn_set(btn,
                template_texture_show,
                user_found ? user->ptr.data : NULL,
                user_found ? user->prop : NULL);
  if (ct == NULL) {
    ui_btn_disable(btn, TIP_("No (unpinned) Props Editor found to display texture in"));
  }
  else if (!user_found) {
    ui_btn_disable(btn, TIP_("No texture user found"));
  }
}
