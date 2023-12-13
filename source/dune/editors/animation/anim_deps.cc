#include <cstring>

#include "mem_guardedalloc.h"

#include "types_anim.h"
#include "types_armature_types.h"
#include "types_gpencil_legacy_types.h"
#include "types_pen.h"
#include "types_mask.h"
#include "types_node.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_seq.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_pen_legacy.h"
#include "dune_pen.hh"
#include "dune_main.hh"
#include "dune_node.h"

#include "graph.hh"

#include "api_access.hh"
#include "api_path.hh"

#include "seq.hh"
#include "seq_utils.hh"

#include "ed_anim_api.hh"

/* graph tagging */
void anim_list_elem_update(Main *main, Scene *scene, AnimListElem *ale)
{
  Id *id;
  FCurve *fcu;
  AnimData *adt;

  id = ale->id;
  if (!id) {
    return;
  }

  /* tag AnimData for refresh so that other views will update in realtime with these changes */
  adt = dune_animdata_from_id(id);
  if (adt) {
    graph_id_tag_update(id, ID_RECALC_ANIM);
    if (adt->action != nullptr) {
      graph_id_tag_update(&adt->action->id, ID_RECALC_ANIM);
    }
  }

  /* Tag copy on the main ob if updating anything directly inside AnimData */
  if (ELEM(ale->type, ANIMTYPE_ANIMDATA, ANIMTYPE_NLAACTION, ANIMTYPE_NLATRACK, ANIMTYPE_NLACURVE))
  {
    graph_id_tag_update(id, ID_RECALC_ANIM);
    return;
  }

  /* update data */
  fcu = static_cast<FCurve *>((ale->datatype == ALE_FCURVE) ? ale->key_data : nullptr);

  if (fcu && fcu->api_path) {
    /* If we have an fcurve, call the update for the prop we
     * are editing, this is then expected to do the proper redrws
     * and graph updates. */
    ApiPtr ptr;
    ApiProp *prop;

    ApiPtr id_ptr = api_id_ptr_create(id);

    if (api_path_resolve_prop(&id_ptr, fcu->api_path, &ptr, &prop)) {
      api_prop_update_main(main, scene, &ptr, prop);
    }
  }
  else {
    /* in other case we do standard depsgraph update, ideally
     * we'd be calling prop update functions here too ... */
    graph_id_tag_update(id, /* or do we want something more restrictive? */
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION);
  }
}

void anim_id_update(Main *main, Id *id)
{
  if (id) {
    graph_id_tag_update_ex(main,
                         id, /* Or do we want something more restrictive? */
                         ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION);
  }
}

/* anim data <-> data syncing */
/* Code here is used to sync the
 * - sel (to find sel data easier)
 * - ... (insert other relevant items here later)
 * status in relevant Dune data with the status stored in anim channels.
 *
 * Should be called in the refresh() cbs for various editors in
 * response to appropriate notifiers. */

/* perform sync updates for Action Groups */
static void animchan_sync_group(AnimCxt *ac, AnimListElem *ale, ActionGroup **active_agrp)
{
  ActionGroup *agrp = (ActionGroup *)ale->data;
  Id *owner_id = ale->id;

  /* major priority is sel status
   * so we need both a group and an owner  */
  if (ELEM(nullptr, agrp, owner_id)) {
    return;
  }

  /* for standard Obs, check if group is the name of some bone */
  if (GS(owner_id->name) == ID_OB) {
    Ob *ob = (Ob *)owner_id;

    /* check if there are bones, and whether the name matches any
     * This feature will only work if groups by default contain the F-Curves
     * for a single bone.  */
    if (ob->pose) {
      PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, agrp->name);
      Armature *arm = static_cast<Armature *>(ob->data);

      if (pchan) {
        /* if one matches, sync the sel status */
        if ((pchan->bone) && (pchan->bone->flag & BONE_SEL)) {
          agrp->flag |= AGRP_SEL;
        }
        else {
          agrp->flag &= ~AGRP_SEL;
        }

        /* also sync active group status */
        if ((ob == ac->obact) && (pchan->bone == arm->act_bone)) {
          /* if no previous F-Curve has active flag, then we're the first and only one to get it */
          if (*active_agrp == nullptr) {
            agrp->flag |= AGRP_ACTIVE;
            *active_agrp = agrp;
          }
          else {
            /* someone else has alrdy taken it - set as not active */
            agrp->flag &= ~AGRP_ACTIVE;
          }
        }
        else {
          /* this can't possibly be active now */
          agrp->flag &= ~AGRP_ACTIVE;
        }

        /* sync bone color */
        action_group_colors_set_from_posebone(agrp, pchan);
      }
    }
  }
}

static void animchan_sync_fcurve_scene(AnimListElem *ale)
{
  Id *owner_id = ale->id;
  lib_assert(GS(owner_id->name) == ID_SCE);
  Scene *scene = (Scene *)owner_id;
  FCurve *fcu = (FCurve *)ale->data;
  Seq *seq = nullptr;

  /* Only affect if F-Curve involves seq_editor.seqs. */
  char seq_name[sizeof(seq->name)];
  if (!lib_str_quoted_substr(fcu->api_path, "seqs_all[", seq_name, sizeof(seq_name))) {
    return;
  }

  /* Check if this strip is sel. */
  Editing *ed = seq_editing_get(scene);
  seq = seq_get_seq_by_name(ed->seqbasep, seq_name, false);
  if (seq == nullptr) {
    return;
  }

  /* update sel status */
  if (seq->flag & SEL) {
    fcu->flag |= FCURVE_SEL;
  }
  else {
    fcu->flag &= ~FCURVE_SEL;
  }
}

/* perform syncing updates for F-Curves */
static void animchan_sync_fcurve(AnimListElem *ale)
{
  FCurve *fcu = (FCurve *)ale->data;
  Id *owner_id = ale->id;

  /* major priority is sel status, so refer to the checks done in `anim_filter.cc`
   * skip_fcurve_sel_data() for ref about what's going on here.  */
  if (ELEM(nullptr, fcu, fcu->api_path, owner_id)) {
    return;
  }

  switch (GS(owner_id->name)) {
    case ID_SCE:
      animchan_sync_fcurve_scene(ale);
      break;
    default:
      break;
  }
}

/* perform syncing updates for Pen Layers */
static void animchan_sync_player(AnimListElem *ale)
{
  PenLayer *penl = (PenLayer *)ale->data;

  /* Ensure sel flags agree with the "active" flag.
   * Sel flags are used in the Dopesheet only, whereas
   * the active flag is used everywhere else. Try to
   * sync these here so that it all seems to be have as the user
   * expects - #50184
   * Assume that we only do this when the active status changes.
   * This may prove annoying if it means sel is always lost)  */
  if (penl->flag & PEN_LAYER_ACTIVE) {
    penl->flag |= PEN_LAYER_SEL;
  }
  else {
    penl->flag &= ~PEN_LAYER_SEL;
  }
}

void anim_sync_animchannels_to_data(const Cxt *C)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  int filter;

  ActionGroup *active_agrp = nullptr;

  /* get anim cxt info for filtering the channels */
  if (animdata_get_cxt(C, &ac) == 0) {
    return;
  }

  /* filter data */
  /* We want all channels, since we want to be able to set sel status on some of them
   * even when collapsed... however,
   * don't include dups so that sel statuses don't override each other. */
  filter = ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_CHANNELS | ANIMFILTER_NODUPLIS;
  animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

  /* flush settings as appropriate depending on the types of the channels */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->type) {
      case ANIMTYPE_GROUP:
        animchan_sync_group(&ac, ale, &active_agrp);
        break;

      case ANIMTYPE_FCURVE:
        animchan_sync_fcurve(ale);
        break;

      case ANIMTYPE_PLAYER:
        animchan_sync_player(ale);
        break;
      case ANIMTYPE_PEN_LAYER:
        using namespace dunee::pen;
        Pen *pen = reinterpret_cast<Pen *>(ale->id);
        Layer *layer = static_cast<Layer *>(ale->data);
        layer->set_sel(pen->is_layer_active(layer));
        break;
    }
  }

  animdata_freelist(&anim_data);
}

void animdata_update(AnimCxt *ac, List *anim_data)
{
  LIST_FOREACH (AnimListElem *, ale, anim_data) {
    if (ale->type == ANIMTYPE_PLAYER) {
      PenLayer *penl = static_cast<PenLayer *>(ale->data);

      if (ale->update & ANIM_UPDATE_ORDER) {
        ale->update &= ~ANIM_UPDATE_ORDER;
        if (penl) {
          dune_pen_layer_frames_sort(penl, nullptr);
        }
      }

      if (ale->update & ANIM_UPDATE_DEPS) {
        ale->update &= ~ANIM_UPDATE_DEPS;
        anim_list_elem_update(ac->main, ac->scene, ale);
      }
      /* disable handles to avoid crash */
      if (ale->update & ANIM_UPDATE_HANDLES) {
        ale->update &= ~ANIM_UPDATE_HANDLES;
      }
    }
    else if (ale->datatype == ALE_MASKLAY) {
      MaskLayer *masklay = static_cast<MaskLayer *>(ale->data);

      if (ale->update & ANIM_UPDATE_ORDER) {
        ale->update &= ~ANIM_UPDATE_ORDER;
        if (masklay) {
          /* While correct & we could enable it: 'posttrans_mask_clean' currently
           * both sorts and removes doubles, so this is not necessary here. */
          // dune_mask_layer_shape_sort(masklay);
        }
      }

      if (ale->update & ANIM_UPDATE_DEPS) {
        ale->update &= ~ANIM_UPDATE_DEPS;
        anim_list_elem_update(ac->bmain, ac->scene, ale);
      }
      /* Disable handles to avoid assert. */
      if (ale->update & ANIM_UPDATE_HANDLES) {
        ale->update &= ~ANIM_UPDATE_HANDLES;
      }
    }
    else if (ale->datatype == ALE_FCURVE) {
      FCurve *fcu = static_cast<FCurve *>(ale->key_data);

      if (ale->update & ANIM_UPDATE_ORDER) {
        ale->update &= ~ANIM_UPDATE_ORDER;
        if (fcu) {
          sort_time_fcurve(fcu);
        }
      }

      if (ale->update & ANIM_UPDATE_HANDLES) {
        ale->update &= ~ANIM_UPDATE_HANDLES;
        if (fcu) {
          dune_fcurve_handles_recalc(fcu);
        }
      }

      if (ale->update & ANIM_UPDATE_DEPS) {
        ale->update &= ~ANIM_UPDATE_DEPS;
        anim_list_elem_update(ac->main, ac->scene, ale);
      }
    }
    else if (ELEM(ale->type,
                  ANIMTYPE_ANIMDATA,
                  ANIMTYPE_NLAACTION,
                  ANIMTYPE_NLATRACK,
                  ANIMTYPE_NLACURVE))
    {
      if (ale->update & ANIM_UPDATE_DEPS) {
        ale->update &= ~ANIM_UPDATE_DEPS;
        anim_list_elem_update(ac->bmain, ac->scene, ale);
      }
    }
    else if (ale->update) {
#if 0
      if (G.debug & G_DEBUG) {
        printf("%s: Unhandled animchannel updates (%d) for type=%d (%p)\n",
               __func__,
               ale->update,
               ale->type,
               ale->data);
      }
#endif
      /* Prevent crashes in cases where it can't be handled */
      ale->update = 0;
    }

    lib_assert(ale->update == 0);
  }
}

void animdata_freelist(List *anim_data)
{
#ifndef NDEBUG
  AnimListElem *ale, *ale_next;
  for (ale = static_cast<AnimListElem *>(anim_data->first); ale; ale = ale_next) {
    ale_next = ale->next;
    lib_assert(ale->update == 0);
    mem_free(ale);
  }
  lib_list_clear(anim_data);
#else
  lib_freelist(anim_data);
#endif
}
