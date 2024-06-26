/* File contains a system used to provide a layer of abstraction between sources
 * of animation data and tools in Animation Editors. The method used here involves
 * generating a list of edit structures which enable tools to naively perform the actions
 * they require wo all the boiler-plate associated w loops w/in loops and checking
 * for cases to ignore.
 *
 * While this is primarily used for the Action/Dopesheet Editor (and its accessory modes),
 * the Graph Editor also uses this for its channel list and for determining which curves
 * are being edited. Likewise, the NLA Editor also uses this for its channel list and in
 * its ops.
 *
 * Much of the original system this was based on was built before the creation of the api
 * system. In future, it would be interesting to replace some parts of this code with api queries,
 * however, api does not eliminate some of the boiler-plate reduction benefits presented by this
 * system, so if any such work does occur, it should only be used for the internals used here...
 *
 * -- Joshua Leung, Dec 2008 (Last revision July 2009) */

#include <cstring>

#include "types_anim.h"
#include "types_armature.h"
#include "types_brush.h"
#include "types_cachefile.h"
#include "types_camera.h"
#include "types_curves.h"
#include "types_pen.h"
#include "types_pen.h"
#include "types_key.h"
#include "types_lattice.h"
#include "types_layer.h"
#include "types_light.h"
#include "types_linestyle.h"
#include "types_mask.h"
#include "types_material.h"
#include "types_mesh.h"
#include "types_meta.h"
#include "types_movieclip.h"
#include "types_node.h"
#include "types_ob_force.h"
#include "types_ob.h"
#include "types_particle.h"
#include "types_pointcloud.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_seq.h"
#include "types_space.h"
#include "types_speaker.h"
#include "types_userdef.h"
#include "types_volume.h"
#include "types_world.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_collection.h"
#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_fcurve_driver.h"
#include "dune_global.h"
#include "dune_pen.hh"
#include "dune_key.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_mask.h"
#include "dune_material.h"
#include "dune_mod.hh"
#include "dune_node.h"

#include "ed_anim_api.hh"
#include "ed_markers.hh"

#include "seq_seq.hh"
#include "seq_utils.hh"

#include "anim_bone_collections.hh"

#include "ui_resources.hh" /* for TH_KEYFRAME_SCALE lookup */

/* Blender Cxt <-> Anim Cxt mapping */
/* Private Stuff - Action Editor */

/* Get shapekey data being edited (for Action Editor -> ShapeKey mode) */
/* There's a similar fn in `key.cc` dune_key_from_ob. */
static Key *actedit_get_shapekeys(AnimCxt *ac)
{
  Scene *scene = ac->scene;
  ViewLayer *view_layer = ac->view_layer;
  Ob *ob;
  Key *key;

  dune_view_layer_synced_ensure(scene, view_layer);
  ob = dune_view_layer_active_ob_get(view_layer);
  if (ob == nullptr) {
    return nullptr;
  }

  /* pinning is not available in 'ShapeKey' mode... */
  // if (saction->pin) { return nullptr; }

  /* shapekey data is stored with geometry data */
  key = dune_key_from_ob(ob);

  if (key) {
    if (key->type == KEY_RELATIVE) {
      return key;
    }
  }

  return nullptr;
}

/* Get data being edited in Action Editor (depending on current 'mode') */
static bool actedit_get_context(AnimCxt *ac, SpaceAction *saction)
{
  /* get dopesheet */
  ac->ads = &saction->ads;

  /* sync settings with current view status, then return appropriate data */
  switch (saction->mode) {
    case SACTCONT_ACTION: /* 'Action Editor' */
      /* if not pinned, sync with active object */
      if (/* `saction->pin == 0` */ true) {
        if (ac->obact && ac->obact->adt) {
          saction->action = ac->obact->adt->action;
        }
        else {
          saction->action = nullptr;
        }
      }

      ac->datatype = ANIMCONT_ACTION;
      ac->data = saction->action;

      ac->mode = saction->mode;
      return true;

    case SACTCONT_SHAPEKEY: /* 'ShapeKey Editor' */
      ac->datatype = ANIMCONT_SHAPEKEY;
      ac->data = actedit_get_shapekeys(ac);

      /* if not pinned, sync with active object */
      if (/* `saction->pin == 0` */ true) {
        Key *key = (Key *)ac->data;

        if (key && key->adt) {
          saction->action = key->adt->action;
        }
        else {
          saction->action = nullptr;
        }
      }

      ac->mode = saction->mode;
      return true;

    case SACTCONT_PEN: /* Pen */ /* review how this mode is handled... */
      /* update scene-ptr (no need to check for pinning yet, as not implemented) */
      saction->ads.src = (Id *)ac->scene;

      ac->datatype = ANIMCONT_PEN;
      ac->data = &saction->ads;

      ac->mode = saction->mode;
      return true;

    case SACTCONT_CACHEFILE: /* Cache File */ /* XXX review how this mode is handled... */
      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      saction->ads.source = (Id *)ac->scene;

      ac->datatype = ANIMCONT_CHANNEL;
      ac->data = &saction->ads;

      ac->mode = saction->mode;
      return true;

    case SACTCONT_MASK: /* Mask */ /* XXX: review how this mode is handled. */
    {
      /* TODO: other methods to get the mask. */
#if 0
      Seq *seq = seq_sel_active_get(ac->scene);
      MovieClip *clip = ac->scene->clip;
      struct Mask *mask = seq ? seq->mask : nullptr;
#endif

      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      saction->ads.source = (ID *)ac->scene;

      ac->datatype = ANIMCONT_MASK;
      ac->data = &saction->ads;

      ac->mode = saction->mode;
      return true;
    }

    case SACTCONT_DOPESHEET: /* DopeSheet */
      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      saction->ads.source = (ID *)ac->scene;

      ac->datatype = ANIMCONT_DOPESHEET;
      ac->data = &saction->ads;

      ac->mode = saction->mode;
      return true;

    case SACTCONT_TIMELINE: /* Timeline */
      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      saction->ads.source = (ID *)ac->scene;

      /* sync scene's "selected keys only" flag with our "only selected" flag
       *
       * XXX: This is a workaround for #55525. We shouldn't really be syncing the flags like this,
       * but it's a simpler fix for now than also figuring out how the next/prev keyframe
       * tools should work in the 3D View if we allowed full access to the timeline's
       * dopesheet filters (i.e. we'd have to figure out where to host those settings,
       * to be on a scene level like this flag currently is, along with several other unknowns).
       */
      if (ac->scene->flag & SCE_KEYS_NO_SELONLY) {
        saction->ads.filterflag &= ~ADS_FILTER_ONLYSEL;
      }
      else {
        saction->ads.filterflag |= ADS_FILTER_ONLYSEL;
      }

      ac->datatype = ANIMCONT_TIMELINE;
      ac->data = &saction->ads;

      ac->mode = saction->mode;
      return true;

    default: /* unhandled yet */
      ac->datatype = ANIMCONT_NONE;
      ac->data = nullptr;

      ac->mode = -1;
      return false;
  }
}

/* ----------- Private Stuff - Graph Editor ------------- */

/* Get data being edited in Graph Editor (depending on current 'mode') */
static bool graphedit_get_context(bAnimContext *ac, SpaceGraph *sipo)
{
  /* init dopesheet data if non-existent (i.e. for old files) */
  if (sipo->ads == nullptr) {
    sipo->ads = static_cast<bDopeSheet *>(MEM_callocN(sizeof(bDopeSheet), "GraphEdit DopeSheet"));
    sipo->ads->source = (ID *)ac->scene;
  }
  ac->ads = sipo->ads;

  /* set settings for Graph Editor - "Selected = Editable" */
  if (U.animation_flag & USER_ANIM_ONLY_SHOW_SELECTED_CURVE_KEYS) {
    sipo->ads->filterflag |= ADS_FILTER_SELEDIT;
  }
  else {
    sipo->ads->filterflag &= ~ADS_FILTER_SELEDIT;
  }

  /* sync settings with current view status, then return appropriate data */
  switch (sipo->mode) {
    case SIPO_MODE_ANIMATION: /* Animation F-Curve Editor */
      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      sipo->ads->source = (ID *)ac->scene;
      sipo->ads->filterflag &= ~ADS_FILTER_ONLYDRIVERS;

      ac->datatype = ANIMCONT_FCURVES;
      ac->data = sipo->ads;

      ac->mode = sipo->mode;
      return true;

    case SIPO_MODE_DRIVERS: /* Driver F-Curve Editor */
      /* update scene-pointer (no need to check for pinning yet, as not implemented) */
      sipo->ads->source = (ID *)ac->scene;
      sipo->ads->filterflag |= ADS_FILTER_ONLYDRIVERS;

      ac->datatype = ANIMCONT_DRIVERS;
      ac->data = sipo->ads;

      ac->mode = sipo->mode;
      return true;

    default: /* unhandled yet */
      ac->datatype = ANIMCONT_NONE;
      ac->data = nullptr;

      ac->mode = -1;
      return false;
  }
}

/* Private Stuff - NLA Editor */

/* Get data being edited in Graph Editor (depending on current 'mode') */
static bool nlaedit_get_context(AnimCxt *ac, SpaceNla *snla)
{
  /* init dopesheet data if non-existent (i.e. for old files) */
  if (snla->ads == nullptr) {
    snla->ads = static_cast<DopeSheet *>(mem_calloc(sizeof(DopeSheet), "NlaEdit DopeSheet"));
  }
  ac->ads = snla->ads;

  /* sync settings with current view status, then return appropriate data */
  /* update scene-pointer (no need to check for pinning yet, as not implemented) */
  snla->ads->source = (Id *)ac->scene;
  snla->ads->filterflag |= ADS_FILTER_ONLYNLA;

  ac->datatype = ANIMCONT_NLA;
  ac->data = snla->ads;

  return true;
}

/* Public API */

bool animdata_cxt_getdata(AnimCxt *ac)
{
  SpaceLink *sl = ac->sl;
  bool ok = false;

  /* cxt depends on editor we are currently in */
  if (sl) {
    switch (ac->spacetype) {
      case SPACE_ACTION: {
        SpaceAction *saction = (SpaceAction *)sl;
        ok = actedit_get_cxt(ac, saction);
        break;
      }
      case SPACE_GRAPH: {
        SpaceGraph *sipo = (SpaceGraph *)sl;
        ok = graphedit_get_cxt(ac, sipo);
        break;
      }
      case SPACE_NLA: {
        SpaceNla *snla = (SpaceNla *)sl;
        ok = nlaedit_get_cxt(ac, snla);
        break;
      }
    }
  }

  /* check if there's any valid data */
  return (ok && ac->data);
}

bool animdata_get_cxt(const Cxt *C, AnimCxt *ac)
{
  Main *main = cxt_data_main(C);
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);
  SpaceLink *sl = cxt_win_space_data(C);
  Scene *scene = cxt_data_scene(C);

  /* clear old cxt info */
  if (ac == nullptr) {
    return false;
  }
  memset(ac, 0, sizeof(AnimCxt));

  /* get useful default cxt settings from context */
  ac->main = main;
  ac->scene = scene;
  ac->view_layer = cxt_data_view_layer(C);
  if (scene) {
    ac->markers = ed_cxt_get_markers(C);
    dune_view_layer_synced_ensure(ac->scene, ac->view_layer);
  }
  ac->graph = cxt_data_graph_ptr(C);
  ac->obact = dune_view_layer_active_ob_get(ac->view_layer);
  ac->area = area;
  ac->rgn = rgn;
  ac->sl = sl;
  ac->spacetype = (area) ? area->spacetype : 0;
  ac->rgntype = (rgn) ? rgn->rgntype : 0;

  /* get data cxt info */
  /* If the below fails, try to grab this info from context instead...
   * (to allow for scripting). */
  return animdata_cxt_getdata(ac);
}

bool animdata_can_have_pen(const eAnimContTypes type)
{
  return ELEM(type, ANIMCONT_PEN, ANIMCONT_DOPESHEET, ANIMCONT_TIMELINE);
}

/* Dune Data <-- Filter --> Channels to be operated on */
/* macros to use before/after getting the sub-channels of some channel,
 * to abstract away some of the tricky logic involved
 *
 * cases:
 * 1) Graph Edit main area (just data) OR channels visible in Channel List
 * 2) If not showing channels, we're only interested in the data (Action Editor's editing)
 * 3) We don't care what data, we just care there is some (so that a collapsed
 *    channel can be kept around). No need to clear channels-flag in order to
 *    keep expander channels with no sub-data out, as those cases should get
 *    dealt with by the recursive detection idiom in place.
 *
 * Implementation NOTE:
 *  YES the _doSubChannels variable is NOT read anywhere. BUT, this is NOT an excuse
 *  to go steamrolling the logic into a single-line expression as from experience,
 *  those are notoriously difficult to read + debug when extending later on. The code
 *  below is purposefully laid out so that each case noted above corresponds clearly to
 *  one case below. */
#define BEGIN_ANIMFILTER_SUBCHANNELS(expanded_check) \
  { \
    int _filter = filter_mode; \
    short _doSubChannels = 0; \
    if (!(filter_mode & ANIMFILTER_LIST_VISIBLE) || (expanded_check)) { \
      _doSubChannels = 1; \
    } \
    else if (!(filter_mode & ANIMFILTER_LIST_CHANNELS)) { \
      _doSubChannels = 2; \
    } \
    else { \
      filter_mode |= ANIMFILTER_TMP_PEEK; \
    } \
\
    { \
      (void)_doSubChannels; \
    }
/* ... standard sub-channel filtering can go on here now ... */
#define END_ANIMFILTER_SUBCHANNELS \
  filter_mode = _filter; \
  } \
  (void)0

/* quick macro to test if AnimData is usable */
#define ANIMDATA_HAS_KEYS(id) ((id)->adt && (id)->adt->action)

/* quick macro to test if AnimData is usable for drivers */
#define ANIMDATA_HAS_DRIVERS(id) ((id)->adt && (id)->adt->drivers.first)

/* quick macro to test if AnimData is usable for NLA */
#define ANIMDATA_HAS_NLA(id) ((id)->adt && (id)->adt->nla_tracks.first)

/* Quick macro to test for all three above usability tests, performing the appropriate provided
 * action for each when the AnimData context is appropriate.
 *
 * Priority order for this goes (most important, to least):
 * AnimData blocks, NLA, Drivers, Keyframes.
 *
 * For this to work correctly,
 * a standard set of data needs to be available within the scope that this
 *
 * Gets called in:
 * - List anim_data;
 * - DopeSheet *ads;
 * - AnimListElem *ale;
 * - size_t items;
 *
 * - id: Id block which should have an AnimData pointer following it immediately, to use
 * - adtOk: line or block of code to execute for AnimData-blocks case
 *   (usually ANIMDATA_ADD_ANIMDATA).
 * - nlaOk: line or block of code to execute for NLA tracks+strips case
 * - driversOk: line or block of code to execute for Drivers case
 * - nlaKeysOk: line or block of code for NLA Strip Keyframes case
 * - keysOk: line or block of code for Keyframes case
 *
 * The checks for the various cases are as follows:
 * 0) top level: checks for animdata and also that all the F-Curves for the block will be visible
 * 1) animdata check: for filtering animdata blocks only
 * 2A) nla tracks: include animdata block's data as there are NLA tracks+strips there
 * 2B) actions to convert to nla: include animdata block's data as there is an action that can be
 *     converted to a new NLA strip, and the filtering options allow this
 * 2C) allow non-animated data-blocks to be included so that data-blocks can be added
 * 3) drivers: include drivers from animdata block (for Drivers mode in Graph Editor)
 * 4A) nla strip keyframes: these are the per-strip controls for time and influence
 * 4B) normal keyframes: only when there is an active action */
#define ANIMDATA_FILTER_CASES(id, adtOk, nlaOk, driversOk, nlaKeysOk, keysOk) \
  { \
    if ((id)->adt) { \
      if (!(filter_mode & ANIMFILTER_CURVE_VISIBLE) || \
          !((id)->adt->flag & ADT_CURVES_NOT_VISIBLE)) { \
        if (filter_mode & ANIMFILTER_ANIMDATA) { \
          adtOk \
        } \
        else if (ads->filterflag & ADS_FILTER_ONLYNLA) { \
          if (ANIMDATA_HAS_NLA(id)) { \
            nlaOk \
          } \
          else if (!(ads->filterflag & ADS_FILTER_NLA_NOACT) || ANIMDATA_HAS_KEYS(id)) { \
            nlaOk \
          } \
        } \
        else if (ads->filterflag & ADS_FILTER_ONLYDRIVERS) { \
          if (ANIMDATA_HAS_DRIVERS(id)) { \
            driversOk \
          } \
        } \
        else { \
          if (ANIMDATA_HAS_NLA(id)) { \
            nlaKeysOk \
          } \
          if (ANIMDATA_HAS_KEYS(id)) { \
            keysOk \
          } \
        } \
      } \
    } \
  } \
  (void)0

/* Add a new anim channel, taking into account the "peek" flag, which is used to just check
 * whether any channels will be added (but without needing them to actually get created).
 *
 * warning This causes the calling fn to return early if we're only "peeking" for channels.
 *
 * ale_statement stuff is rly a hack for 1 special case. It shouldn't really be needed. */
#define ANIMCHANNEL_NEW_CHANNEL_FULL( \
    channel_data, channel_type, owner_id, fcurve_owner_id, ale_statement) \
  if (filter_mode & ANIMFILTER_TMP_PEEK) { \
    return 1; \
  } \
  { \
    AnimListElem *ale = make_new_animlistelem( \
        channel_data, channel_type, (Id *)owner_id, fcurve_owner_id); \
    if (ale) { \
      lib_addtail(anim_data, ale); \
      items++; \
      ale_statement \
    } \
  } \
  (void)0

#define ANIMCHANNEL_NEW_CHANNEL(channel_data, channel_type, owner_id, fcurve_owner_id) \
  ANIMCHANNEL_NEW_CHANNEL_FULL(channel_data, channel_type, owner_id, fcurve_owner_id, {})

/* quick macro to test if an anim-channel representing an AnimData block is suitably active */
#define ANIMCHANNEL_ACTIVEOK(ale) \
  (!(filter_mode & ANIMFILTER_ACTIVE) || !(ale->adt) || (ale->adt->flag & ADT_UI_ACTIVE))

/* Quick macro to test if an anim-channel (F-Curve, Group, etc.)
 * is selected in an acceptable way. */
#define ANIMCHANNEL_SELOK(test_fn) \
  (!(filter_mode & (ANIMFILTER_SEL | ANIMFILTER_UNSEL)) || \
   ((filter_mode & ANIMFILTER_SEL) && test_func) || \
   ((filter_mode & ANIMFILTER_UNSEL) && test_func == 0))

/* Quick macro to test if an anim-channel (F-Curve) is sel ok for editing purposes
 * - `*_SELEDIT` means that only sel curves will have visible+editable key-frames.
 *
 * checks here work as follows:
 * 1) SELEDIT off - don't need to consider the implications of this option.
 * 2) FOREDIT off - we're not considering editing, so channel is ok still.
 * 3) test_fn (i.e. sel test) - only if selected, this test will pass. */
#define ANIMCHANNEL_SELEDITOK(test_fn) \
  (!(filter_mode & ANIMFILTER_SELEDIT) || !(filter_mode & ANIMFILTER_FOREDIT) || (test_func))

/* 'Private' Stuff */

/* this fn allocs mem for a new AnimListElem struct for the
 * provided anim channel-data. */
static AnimListElem *make_new_animlistelem(void *data,
                                           short datatype,
                                           Id *owner_id,
                                           Id *fcurve_owner_id)
{
  AnimListElem *ale = nullptr;

  /* only alloc mem if there is data to convert */
  if (data) {
    /* alloc and set generic data */
    ale = static_cast<AnimListElem *>(mem_calloc(sizeof(AnimListElem), "AnimListElem"));

    ale->data = data;
    ale->type = datatype;

    ale->id = owner_id;
    ale->adt = dune_animdata_from_id(owner_id);
    ale->fcurve_owner_id = fcurve_owner_id;

    /* do specifics */
    switch (datatype) {
      case ANIMTYPE_SUMMARY: {
        /* Nothing to include for now... this is just a dummy wrapper around
         * all the other channels in the DopeSheet, and gets included at the start of the list. */
        ale->key_data = nullptr;
        ale->datatype = ALE_ALL;
        break;
      }
      case ANIMTYPE_SCENE: {
        Scene *sce = (Scene *)data;

        ale->flag = sce->flag;

        ale->key_data = sce;
        ale->datatype = ALE_SCE;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_OB: {
        Base *base = (Base *)data;
        Ob *ob = base->ob;

        ale->flag = ob->flag;

        ale->key_data = ob;
        ale->datatype = ALE_OB;

        ale->adt = dune_animdata_from_id(&ob->id);
        break;
      }
      case ANIMTYPE_FILLACTD: {
        Action *act = (Action *)data;

        ale->flag = act->flag;

        ale->key_data = act;
        ale->datatype = ALE_ACT;
        break;
      }
      case ANIMTYPE_FILLDRIVERS: {
        AnimData *adt = (AnimData *)data;

        ale->flag = adt->flag;

        /* drivers don't show summary for now. */
        ale->key_data = nullptr;
        ale->datatype = ALE_NONE;
        break;
      }
      case ANIMTYPE_DSMAT: {
        Material *ma = (Material *)data;
        AnimData *adt = ma->adt;

        ale->flag = FILTER_MAT_OBJD(ma);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSLAM: {
        Light *la = (Light *)data;
        AnimData *adt = la->adt;

        ale->flag = FILTER_LAM_OBJD(la);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_DSCAM: {
        Camera *ca = (Camera *)data;
        AnimData *adt = ca->adt;

        ale->flag = FILTER_CAM_OBJD(ca);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_DSCACHEFILE: {
        CacheFile *cache_file = (CacheFile *)data;
        AnimData *adt = cache_file->adt;

        ale->flag = FILTER_CACHEFILE_OBJD(cache_file);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_DSCUR: {
        Curve *cu = (Curve *)data;
        AnimData *adt = cu->adt;

        ale->flag = FILTER_CUR_OBJD(cu);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSARM: {
        Armature *arm = (Armature *)data;
        AnimData *adt = arm->adt;

        ale->flag = FILTER_ARM_OBJD(arm);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSMESH: {
        Mesh *me = (Mesh *)data;
        AnimData *adt = me->adt;

        ale->flag = FILTER_MESH_OBJD(me);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSLAT: {
        Lattice *lt = (Lattice *)data;
        AnimData *adt = lt->adt;

        ale->flag = FILTER_LATTICE_OBJD(lt);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSSPK: {
        Speaker *spk = (Speaker *)data;
        AnimData *adt = spk->adt;

        ale->flag = FILTER_SPK_OBJD(spk);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_DSHAIR: {
        Curves *curves = (Curves *)data;
        AnimData *adt = curves->adt;

        ale->flag = FILTER_CURVES_OBJD(curves);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<ID *>(data));
        break;
      }
      case ANIMTYPE_DSPOINTCLOUD: {
        PointCloud *pointcloud = (PointCloud *)data;
        AnimData *adt = pointcloud->adt;

        ale->flag = FILTER_POINTS_OBJD(pointcloud);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSVOLUME: {
        Volume *volume = (Volume *)data;
        AnimData *adt = volume->adt;

        ale->flag = FILTER_VOLUME_OBJD(volume);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSSKEY: {
        Key *key = (Key *)data;
        AnimData *adt = key->adt;

        ale->flag = FILTER_SKE_OBJD(key);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSWOR: {
        World *wo = (World *)data;
        AnimData *adt = wo->adt;

        ale->flag = FILTER_WOR_SCED(wo);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSNTREE: {
        NodeTree *ntree = (NodeTree *)data;
        AnimData *adt = ntree->adt;

        ale->flag = FILTER_NTREE_DATA(ntree);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSLINESTYLE: {
        FreestyleLineStyle *linestyle = (FreestyleLineStyle *)data;
        AnimData *adt = linestyle->adt;

        ale->flag = FILTER_LS_SCED(linestyle);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSPART: {
        ParticleSettings *part = (ParticleSettings *)ale->data;
        AnimData *adt = part->adt;

        ale->flag = FILTER_PART_OBJD(part);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSTEX: {
        Tex *tex = (Tex *)data;
        AnimData *adt = tex->adt;

        ale->flag = FILTER_TEX_DATA(tex);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSPEN: {
        PenData *pend = (PenData *)data;
        AnimData *adt = pend->adt;

        /* We reuse the same expand filter for this case */
        ale->flag = EXPANDED_PENDATA(pend);

        /* currently, this is only used for access to its anim data */
        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_DSMCLIP: {
        MovieClip *clip = (MovieClip *)data;
        AnimData *adt = clip->adt;

        ale->flag = EXPANDED_MCLIP(clip);

        ale->key_data = (adt) ? adt->action : nullptr;
        ale->datatype = ALE_ACT;

        ale->adt = dune_animdata_from_id(static_cast<Id *>(data));
        break;
      }
      case ANIMTYPE_NLACTRLS: {
        AnimData *adt = (AnimData *)data;

        ale->flag = adt->flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_NONE;
        break;
      }
      case ANIMTYPE_GROUP: {
        ActionGroup *agrp = (ActionGroup *)data;

        ale->flag = agrp->flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_GROUP;
        break;
      }
      case ANIMTYPE_FCURVE:
      case ANIMTYPE_NLACURVE: /* practically the same as ANIMTYPE_FCURVE.
                               * Differences are applied post-creation */
      {
        FCurve *fcu = (FCurve *)data;

        ale->flag = fcu->flag;

        ale->key_data = fcu;
        ale->datatype = ALE_FCURVE;
        break;
      }
      case ANIMTYPE_SHAPEKEY: {
        KeyBlock *kb = (KeyBlock *)data;
        Key *key = (Key *)ale->id;

        ale->flag = kb->flag;

        /* whether we have keyframes depends on whether there is a Key block to find it from */
        if (key) {
          /* index of shapekey is defined by place in key's list */
          ale->index = lib_findindex(&key->block, kb);

          /* the corresponding keyframes are from the animdata */
          if (ale->adt && ale->adt->action) {
            Action *act = ale->adt->action;
            char *api_path = dune_keyblock_curval_apipath_get(key, kb);

            /* try to find the F-Curve which corresponds to this exactly,
             * then free the mem_alloc'd string */
            if (rna_path) {
              ale->key_data = (void *)dune_fcurve_find(&act->curves, api_path, 0);
              mem_free(api_path);
            }
          }
          ale->datatype = (ale->key_data) ? ALE_FCURVE : ALE_NONE;
        }
        break;
      }
      case ANIMTYPE_PENLAYER: {
        PenLayer *penl = (PenLayer *)data;

        ale->flag = penl->flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_PENFRAME;
        break;
      }
      case ANIMTYPE_PEN_LAYER: {
        PenLayer *layer = static_cast<PenLayer *>(data);

        ale->flag = layer->base.flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_PEN_CEL;
        break;
      }
      case ANIMTYPE_PEN_LAYER_GROUP: {
        PenLayerTreeGroup *layer_group = static_cast<PenLayerTreeGroup *>(data);

        ale->flag = layer_group->base.flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_PEN_GROUP;
        break;
      }
      case ANIMTYPE_PEN_DATABLOCK: {
        Pen *pen = static_cast<Pen *>(data);

        ale->flag = pen->flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_PEN_DATA;
        break;
      }
      case ANIMTYPE_MASKLAYER: {
        MaskLayer *masklay = (MaskLayer *)data;

        ale->flag = masklay->flag;

        ale->key_data = nullptr;
        ale->datatype = ALE_MASKLAY;
        break;
      }
      case ANIMTYPE_NLATRACK: {
        NlaTrack *nlt = (NlaTrack *)data;

        ale->flag = nlt->flag;

        ale->key_data = &nlt->strips;
        ale->datatype = ALE_NLASTRIP;
        break;
      }
      case ANIMTYPE_NLAACTION: {
        /* nothing to include for now... nothing editable from NLA-perspective here */
        ale->key_data = nullptr;
        ale->datatype = ALE_NONE;
        break;
      }
    }
  }

  /* return created datatype */
  return ale;
}

/* 'Only Sel' sel data and/or 'Include Hidden' filtering
 * When this fn returns true, the F-Curve is to be skipped */
static bool skip_fcurve_sel_data(DopeSheet *ads, FCurve *fcu, Id *owner_id, int filter_mode)
{
  if (fcu->grp != nullptr && fcu->grp->flag & ADT_CURVES_ALWAYS_VISIBLE) {
    return false;
  }
  /* hidden items should be skipped if we only care about visible data,
   * but we aren't interested in hidden stuff */
  const bool skip_hidden = (filter_mode & ANIMFILTER_DATA_VISIBLE) &&
                           !(ads->filterflag & ADS_FILTER_INCL_HIDDEN);

  if (GS(owner_id->name) == ID_OB) {
    Ob *ob = (Ob *)owner_id;
    PoseChannel *pchan = nullptr;
    char bone_name[sizeof(pchan->name)];

    /* Only consider if F-Curve involves `pose.bones`. */
    if (fcu->api_path &&
        lib_str_quoted_substr(fcu->api_path, "pose.bones[", bone_name, sizeof(bone_name)))
    {
      /* Get bone-name, and check if this bone is sel. */
      pchan = dune_pose_channel_find_name(ob->pose, bone_name);

      /* check whether to continue or skip */
      if (pchan && pchan->bone) {
        /* If only visible channels,
         * skip if bone not visible unless user wants channels from hidden data too. */
        if (skip_hidden) {
          Armature *arm = (Armature *)ob->data;

          /* skipping - not visible on currently visible layers */
          if (!anim_bonecoll_is_visible_pchan(arm, pchan)) {
            return true;
          }
          /* skipping - is currently hidden */
          if (pchan->bone->flag & BONE_HIDDEN_P) {
            return true;
          }
        }

        /* can only add this F-Curve if it is sel */
        if (ads->filterflag & ADS_FILTER_ONLYSEL) {
          if ((pchan->bone->flag & BONE_SEL) == 0) {
            return true;
          }
        }
      }
    }
  }
  else if (GS(owner_id->name) == ID_SCE) {
    Scene *scene = (Scene *)owner_id;
    Seq *seq = nullptr;
    char seq_name[sizeof(seq->name)];

    /* Only consider if F-Curve involves `seq_editor.seqs`. */
    if (fcu->api_path &&
        lib_str_quoted_substr(fcu->api_path, "seqs_all[", seq_name, sizeof(seq_name)))
    {
      /* Get strip name, and check if this strip is sel. */
      Editing *ed = seq_editing_get(scene);
      if (ed) {
        seq = seq_get_seq_by_name(ed->seqbasep, seq_name, false);
      }

      /* Can only add this F-Curve if it is sel. */
      if (ads->filterflag & ADS_FILTER_ONLYSEL) {

        /* NOTE: The `seq == nullptr` check doesn't look right
         * (compared to other checks in this fn which skip data that can't be found).
         *
         * This is done since the search for seq strips doesn't use a global lookup:
         * - Nested meta-strips are excluded.
         * - When inside a meta-strip - strips outside the meta-strip excluded.
         *
         * Instead, only the strips directly visible to the user are considered for selection.
         * The nullptr check here means everything else is considered unselected and is not shown.
         *
         * There is a subtle diff between nodes, pose-bones ... etc
         * since data-paths that point to missing strips are not shown.
         * If this is an important difference, the nullptr case could perform a global lookup,
         * only returning `true` if the seq strip exists elsewhere
         * (ignoring it's selection state). */
        if (seq == nullptr) {
          return true;
        }

        if ((seq->flag & SEL) == 0) {
          return true;
        }
      }
    }
  }
  else if (GS(owner_id->name) == ID_NT) {
    NodeTree *ntree = (NodeTree *)owner_id;
    Node *node = nullptr;
    char node_name[sizeof(node->name)];

    /* Check for sel nodes. */
    if (fcu->api_path &&
        lib_str_quoted_substr(fcu->api_path, "nodes[", node_name, sizeof(node_name))) {
      /* Get strip name, and check if this strip is sel. */
      node = nodeFindNodebyName(ntree, node_name);

      /* Can only add this F-Curve if it is sel. */
      if (node) {
        if (ads->filterflag & ADS_FILTER_ONLYSEL) {
          if ((node->flag & NODE_SEL) == 0) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

/* Helper for name-based filtering - Perform "partial/fuzzy matches" (as in 80a7efd) */
static bool name_matches_dopesheet_filter(DopeSheet *ads, const char *name)
{
  if (ads->flag & ADS_FLAG_FUZZY_NAMES) {
    /* full fuzzy, multi-word, case insensitive matches */
    const size_t str_len = strlen(ads->searchstr);
    const int words_max = lib_string_max_possible_word_count(str_len);

    int(*words)[2] = static_cast<int(*)[2]>(BLI_array_alloca(words, words_max));
    const int words_len = lib_string_find_split_words(
        ads->searchstr, str_len, ' ', words, words_max);
    bool found = false;

    /* match name against all search words */
    for (int index = 0; index < words_len; index++) {
      if (lib_strncasestr(name, ads->searchstr + words[index][0], words[index][1])) {
        found = true;
        break;
      }
    }

    /* if we have a match somewhere, this returns true */
    return ((ads->flag & ADS_FLAG_INVERT_FILTER) == 0) ? found : !found;
  }
  /* fallback/default - just case insensitive, but starts from start of word */
  bool found = lib_strcasestr(name, ads->searchstr) != nullptr;
  return ((ads->flag & ADS_FLAG_INVERT_FILTER) == 0) ? found : !found;
}

/* (Display-)Name-based F-Curve filtering
 * When this fn returns true, the F-Curve is to be skipped */
static bool skip_fcurve_with_name(
    DopeSheet *ads, FCurve *fcu, eAnimChannelType channel_type, void *owner, Id *owner_id)
{
  AnimListElem ale_dummy = {nullptr};
  const AnimChannelType *acf;

  /* create a dummy wrapper for the F-Curve, so we can get typeinfo for it */
  ale_dummy.type = channel_type;
  ale_dummy.owner = owner;
  ale_dummy.id = owner_id;
  ale_dummy.data = fcu;

  /* get type info for channel */
  acf = anim_channel_get_typeinfo(&ale_dummy);
  if (acf && acf->name) {
    char name[256]; /* hopefully this will be enough! */

    /* get name */
    acf->name(&ale_dummy, name);

    /* check for partial match with the match string, assuming case insensitive filtering
     * if match, this channel shouldn't be ignored! */
    return !name_matches_dopesheet_filter(ads, name);
  }

  /* just let this go... */
  return true;
}

/* Check if F-Curve has errors and/or is disabled
 * return true if F-Curve has errors/is disabled */
static bool fcurve_has_errors(const FCurve *fcu)
{
  /* F-Curve disabled (path eval error). */
  if (fcu->flag & FCURVE_DISABLED) {
    return true;
  }

  /* driver? */
  if (fcu->driver) {
    const ChannelDriver *driver = fcu->driver;

    /* error flag on driver usually means that there is an error
     * BUT this may not hold with PyDrivers as this flag gets cleared
     *     if no critical errors prevent the driver from working... */
    if (driver->flag & DRIVER_FLAG_INVALID) {
      return true;
    }

    /* check vars for other things that need linting... */
    /* TODO: maybe it would be more efficient just to have a quick flag for this? */
    LIST_FOREACH (DriverVar *, dvar, &driver->vars) {
      DRIVER_TARGETS_USED_LOOPER_BEGIN (dvar) {
        if (dtar->flag & DTAR_FLAG_INVALID) {
          return true;
        }
      }
      DRIVER_TARGETS_LOOPER_END;
    }
  }

  /* no errors found */
  return false;
}

/* find the next F-Curve that is usable for inclusion */
static FCurve *animfilter_fcurve_next(DopeSheet *ads,
                                      FCurve *first,
                                      eAnimChannelType channel_type,
                                      int filter_mode,
                                      void *owner,
                                      Id *owner_id)
{
  ActionGroup *grp = (channel_type == ANIMTYPE_FCURVE) ? static_cast<ActionGroup *>(owner) :
                                                         nullptr;
  FCurve *fcu = nullptr;

  /* Loop over F-Curves - assume that the caller of this has alrdy checked
   * that these should be included.
   * We need to check if the F-Curves belong to the same group,
   * as this gets called for groups too... */
  for (fcu = first; ((fcu) && (fcu->grp == grp)); fcu = fcu->next) {
    /* special exception for Pose-Channel/Seq-Strip/Node Based F-Curves:
     * - The 'Only Sel' and 'Include Hidden' data filters should be applied to sub-Id data
     *   which can be independently sel/hidden, such as Pose-Channels, Seq Strips,
     *   and Nodes. Since these checks were traditionally done as first check for obs,
     *   we do the same here.
     * - We currently use an 'approximate' method for getting these F-Curves that doesn't require
     *   carefully checking the entire path.
     * - This will also affect things like Drivers, and also works for Bone Constraints.  */
    if (ads && owner_id) {
      if ((filter_mode & ANIMFILTER_TMP_IGNORE_ONLYSEL) == 0) {
        if ((ads->filterflag & ADS_FILTER_ONLYSEL) ||
            (ads->filterflag & ADS_FILTER_INCL_HIDDEN) == 0) {
          if (skip_fcurve_sel_data(ads, fcu, owner_id, filter_mode)) {
            continue;
          }
        }
      }
    }

    /* only include if visible (Graph Editor check, not channels check) */
    if (!(filter_mode & ANIMFILTER_CURVE_VISIBLE) || (fcu->flag & FCURVE_VISIBLE)) {
      /* only work with this channel and its subchannels if it is editable */
      if (!(filter_mode & ANIMFILTER_FOREDIT) || EDITABLE_FCU(fcu)) {
        /* Only include this curve if sel in a way consistent
         * with the filtering requirements. */
        if (ANIMCHANNEL_SELOK(SEL_FCU(fcu)) && ANIMCHANNEL_SELEDITOK(SEL_FCU(fcu))) {
          /* only include if this curve is active */
          if (!(filter_mode & ANIMFILTER_ACTIVE) || (fcu->flag & FCURVE_ACTIVE)) {
            /* name based filtering... */
            if (((ads) && (ads->searchstr[0] != '\0')) && (owner_id)) {
              if (skip_fcurve_with_name(ads, fcu, channel_type, owner, owner_id)) {
                continue;
              }
            }

            /* error-based filtering... */
            if ((ads) && (ads->filterflag & ADS_FILTER_ONLY_ERRORS)) {
              /* skip if no errors... */
              if (fcurve_has_errors(fcu) == false) {
                continue;
              }
            }

            /* this F-Curve can be used, so return it */
            return fcu;
          }
        }
      }
    }
  }

  /* no (more) F-Curves from the list are suitable... */
  return nullptr;
}

static size_t animfilter_fcurves(List *anim_data,
                                 DopeSheet *ads,
                                 FCurve *first,
                                 eAnimChannelType fcurve_type,
                                 int filter_mode,
                                 void *owner,
                                 Id *owner_id,
                                 Id *fcurve_owner_id)
{
  FCurve *fcu;
  size_t items = 0;

  /* Loop over every F-Curve able to be included.
   *
   * This for-loop works like this:
   * 1) The starting F-Curve is assigned to the fcu pointer
   *    so that we have a starting point to search from.
   * 2) The first valid F-Curve to start from (which may include the one given as 'first')
   *    in the remaining list of F-Curves is found, and verified to be non-null.
   * 3) The F-Curve referenced by fcu pointer is added to the list
   * 4) The fcu pointer is set to the F-Curve after the one we just added,
   *    so that we can keep going through the rest of the F-Curve list without an eternal loop.
   *    Back to step 2 : */
  for (fcu = first;
       (fcu = animfilter_fcurve_next(ads, fcu, fcurve_type, filter_mode, owner, owner_id));
       fcu = fcu->next)
  {
    if (UNLIKELY(fcurve_type == ANIMTYPE_NLACURVE)) {
      /* NLA Control Curve - Basically the same as normal F-Curves,
       * except we need to set some stuff differently */
      ANIMCHANNEL_NEW_CHANNEL_FULL(fcu, ANIMTYPE_NLACURVE, owner_id, fcurve_owner_id, {
        ale->owner = owner; /* strip */
        ale->adt = nullptr; /* to prevent time mapping from causing problems */
      });
    }
    else {
      /* Normal FCurve */
      ANIMCHANNEL_NEW_CHANNEL(fcu, ANIMTYPE_FCURVE, owner_id, fcurve_owner_id);
    }
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animfilter_act_group(AnimCxt *ac,
                                   List *anim_data,
                                   DopeSheet *ads,
                                   Action *act,
                                   ActionGroup *agrp,
                                   int filter_mode,
                                   Id *owner_id)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;
  // int ofilter = filter_mode;

  /* if we care about the sel status of the channels,
   * but the group isn't expanded (1)...
   * (1) this only matters if we actually care about the hierarchy though.
   *     - Hierarchy matters: this hack should be applied
   *     - Hierarchy ignored: cases like #21276 won't work properly, unless we skip this hack */
  if (
      /* Care about hierarchy but group isn't expanded. */
      ((filter_mode & ANIMFILTER_LIST_VISIBLE) && EXPANDED_AGRP(ac, agrp) == 0) &&
      /* Care about sel status. */
      (filter_mode & (ANIMFILTER_SEL | ANIMFILTER_UNSEL)))
  {
    /* If the group itself isn't sel appropriately,
     * we shouldn't consider its children either. */
    if (ANIMCHANNEL_SELOK(SEL_AGRP(agrp)) == 0) {
      return 0;
    }

    /* if we're still here,
     * then the sel status of the curves within this group should not matter,
     * since this creates too much overhead for animators (i.e. making a slow workflow).
     *
     * Tools affected by this at time of coding (2010 Feb 09):
     * - Inserting keyframes on sel channels only.
     * - Pasting keyframes.
     * - Creating ghost curves in Graph Editors */
    filter_mode &= ~(ANIMFILTER_SEL | ANIMFILTER_UNSEL | ANIMFILTER_LIST_VISIBLE);
  }

  /* add grouped F-Curves */
  BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_AGRP(ac, agrp)) {
    /* special filter so that we can get just the F-Curves within the active group */
    if (!(filter_mode & ANIMFILTER_ACTGROUPED) || (agrp->flag & AGRP_ACTIVE)) {
      /* for the Graph Editor, curves may be set to not be visible in the view to lessen
       * clutter, but to do this, we need to check that the group doesn't have its
       * not-visible flag set preventing all its sub-curves to be shown */
      if (!(filter_mode & ANIMFILTER_CURVE_VISIBLE) || !(agrp->flag & AGRP_NOTVISIBLE)) {
        /* group must be editable for its children to be editable (if we care about this) */
        if (!(filter_mode & ANIMFILTER_FOREDIT) || EDITABLE_AGRP(agrp)) {
          /* get first F-Curve which can be used here */
          FCurve *first_fcu = animfilter_fcurve_next(ads,
                                                     static_cast<FCurve *>(agrp->channels.first),
                                                     ANIMTYPE_FCURVE,
                                                     filter_mode,
                                                     agrp,
                                                     owner_id);

          /* filter list, starting from this F-Curve */
          tmp_items += animfilter_fcurves(
              &tmp_data, ads, first_fcu, ANIMTYPE_FCURVE, filter_mode, agrp, owner_id, &act->id);
        }
      }
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* add this group as a channel first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* restore original filter mode so that this next step works ok... */
      // filter_mode = ofilter;

      /* filter sel of channel specially here again,
       * since may be open and not subject to prev test */
      if (ANIMCHANNEL_SELOK(SEL_AGRP(agrp))) {
        ANIMCHANNEL_NEW_CHANNEL(agrp, ANIMTYPE_GROUP, owner_id, &act->id);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animfilter_action(AnimCxt *ac,
                                List *anim_data,
                                DopeSheet *ads,
                                Action *act,
                                int filter_mode,
                                Id *owner_id)
{
  FCurve *lastchan = nullptr;
  size_t items = 0;

  /* don't include anything from this action if it is linked in from another file,
   * and we're getting stuff for editing... */
  if ((filter_mode & ANIMFILTER_FOREDIT) && (ID_IS_LINKED(act) || ID_IS_OVERRIDE_LIB(act))) {
    return 0;
  }

  /* do groups */
  /* TODO: do nested groups? */
  LIST_FOREACH (ActionGroup *, agrp, &act->groups) {
    /* store ref to last channel of group */
    if (agrp->channels.last) {
      lastchan = static_cast<FCurve *>(agrp->channels.last);
    }

    /* action group's channels */
    items += animfilter_act_group(ac, anim_data, ads, act, agrp, filter_mode, owner_id);
  }

  /* un-grouped F-Curves (only if we're not only considering those channels in the active group) */
  if (!(filter_mode & ANIMFILTER_ACTGROUPED)) {
    FCurve *firstfcu = (lastchan) ? (lastchan->next) : static_cast<FCurve *>(act->curves.first);
    items += animfilter_fcurves(
        anim_data, ads, firstfcu, ANIMTYPE_FCURVE, filter_mode, nullptr, owner_id, &act->id);
  }

  /* return the number of items added to the list */
  return items;
}

/* Include NLA-Data for NLA-Editor:
 * - When ANIMFILTER_LIST_CHANNELS is used, that means we should be filtering the list for display
 *   Eval order is from 1st track to last and then apply the
 *   Action on top, we present this in the UI as the Active Action followed by the last track
 *   to the 1st to get the eval order presented as per a stack.
 * - For normal filtering (i.e. for editing),
 *   we only need the NLA-tracks but they can be in 'normal' eval order, i.e. first to last.
 *   Otherwise, some tools may get screwed up. */
static size_t animfilter_nla(AnimCxt * /*ac*/,
                             List *anim_data,
                             DopeSheet *ads,
                             AnimData *adt,
                             int filter_mode,
                             Id *owner_id)
{
  NlaTrack *nlt;
  NlaTrack *first = nullptr, *next = nullptr;
  size_t items = 0;

  /* if showing channels, include active action */
  if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
    /* if NLA action-line filtering is off, don't show unless there are keyframes,
     * in order to keep things more compact for doing transforms */
    if (!(ads->filterflag & ADS_FILTER_NLA_NOACT) || (adt->action)) {
      /* there isn't rly anything editable here, so skip if need editable */
      if ((filter_mode & ANIMFILTER_FOREDIT) == 0) {
        /* Just add the action track now (this MUST appear for drawing):
         * - As AnimData may not have an action,
         * we pass a dummy ptr just to get the list elem created,
         * then overwrite this with the real val - REVIEW THIS. */
        ANIMCHANNEL_NEW_CHANNEL_FULL(
            (void *)(&adt->action), ANIMTYPE_NLAACTION, owner_id, nullptr, {
              ale->data = adt->action ? adt->action : nullptr;
            });
      }
    }

    /* 1st track to include will be the last one if we're filtering by channels */
    first = static_cast<NlaTrack *>(adt->nla_tracks.last);
  }
  else {
    /* first track to include will the first one (as per normal) */
    first = static_cast<NlaTrack *>(adt->nla_tracks.first);
  }

  /* loop over NLA Tracks -
   * assume that the caller of this has alrdy checked that these should be included */
  for (nlt = first; nlt; nlt = next) {
    /* 'next' NLA-Track to use depends on whether we're filtering for drawing or not */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      next = nlt->prev;
    }
    else {
      next = nlt->next;
    }

    /* only work with this channel and its subchannels if it is editable */
    if (!(filter_mode & ANIMFILTER_FOREDIT) || EDITABLE_NLT(nlt)) {
      /* only include this track if selected in a way consistent with the filtering requirements */
      if (ANIMCHANNEL_SELOK(SEL_NLT(nlt))) {
        /* only include if this track is active */
        if (!(filter_mode & ANIMFILTER_ACTIVE) || (nlt->flag & NLATRACK_ACTIVE)) {
          /* name based filtering... */
          if (((ads) && (ads->searchstr[0] != '\0')) && (owner_id)) {
            bool track_ok = false, strip_ok = false;

            /* check if the name of the track, or the strips it has are ok... */
            track_ok = name_matches_dopesheet_filter(ads, nlt->name);

            if (track_ok == false) {
              LIST_FOREACH (NlaStrip *, strip, &nlt->strips) {
                if (name_matches_dopesheet_filter(ads, strip->name)) {
                  strip_ok = true;
                  break;
                }
              }
            }

            /* skip if both fail this test... */
            if (!track_ok && !strip_ok) {
              continue;
            }
          }

          /* add the track now that it has passed all our tests */
          ANIMCHANNEL_NEW_CHANNEL(nlt, ANIMTYPE_NLATRACK, owner_id, nullptr);
        }
      }
    }
  }

  /* return the number of items added to the list */
  return items;
}

/* Include the ctrl FCurves per NLA Strip in the channel list
 * This is includes the expander too.. */
static size_t animfilter_nla_ctrls(
    List *anim_data, DopeSheet *ads, AnimData *adt, int filter_mode, Id *owner_id)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add ctrl curves from each NLA strip... */
  /* NOTE: ANIMTYPE_FCURVES are created here, to avoid dup the code needed */
  BEGIN_ANIMFILTER_SUBCHANNELS ((adt->flag & ADT_NLA_SKEYS_COLLAPSED) == 0) {
    /* for now, we only go one level deep - so controls on grouped FCurves are not handled */
    LIST_FOREACH (NlaTrack *, nlt, &adt->nla_tracks) {
      LIST_FOREACH (NlaStrip *, strip, &nlt->strips) {
        /* pass strip as the "owner",
         * so that the name lookups (used while filtering) will resolve */
        /* NLA tracks are coming from AnimData, so owner of f-curves
         * is the same as owner of animation data. */
        tmp_items += animfilter_fcurves(&tmp_data,
                                        ads,
                                        static_cast<FCurve *>(strip->fcurves.first),
                                        ANIMTYPE_NLACURVE,
                                        filter_mode,
                                        strip,
                                        owner_id,
                                        owner_id);
      }
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* add the expander as a channel first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* currently these channels cannot be selected, so they should be skipped */
      if ((filter_mode & (ANIMFILTER_SEL | ANIMFILTER_UNSEL)) == 0) {
        ANIMCHANNEL_NEW_CHANNEL(adt, ANIMTYPE_NLACONTROLS, owner_id, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

/* determine what anim data from AnimData block should get displayed */
static size_t animfilter_block_data(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Id *id, int filter_mode)
{
  AnimData *adt = dune_animdata_from_id(id);
  size_t items = 0;

  /* img ob data-blocks have no anim-data so check for nullptr */
  if (adt) {
    IdAdtTemplate *iat = (IdAdtTemplate *)id;

    /* This macro is used instead of inlining the logic here,
     * since this sort of filtering is still needed in a few places in the rest of the code still -
     * notably for the few cases where special mode-based
     * different types of data expanders are required. */
    ANIMDATA_FILTER_CASES(
        iat,
        { /* AnimData */
          /* specifically filter animdata block */
          if (ANIMCHANNEL_SELOK(SEL_ANIMDATA(adt))) {
            ANIMCHANNEL_NEW_CHANNEL(adt, ANIMTYPE_ANIMDATA, id, nullptr);
          }
        },
        { /* NLA */
          items += animfilter_nla(ac, anim_data, ads, adt, filter_mode, id);
        },
        { /* Drivers */
          items += animfilter_fcurves(anim_data,
                                      ads,
                                      static_cast<FCurve *>(adt->drivers.first),
                                      ANIMTYPE_FCURVE,
                                      filter_mode,
                                      nullptr,
                                      id,
                                      id);
        },
        { /* NLA Ctrl Keyframes */
          items += animfilter_nla_ctrls(anim_data, ads, adt, filter_mode, id);
        },
        { /* Keyframes */
          items += animfilter_action(ac, anim_data, ads, adt->action, filter_mode, id);
        });
  }

  return items;
}

/* Include ShapeKey Data for ShapeKey Editor */
static size_t animdata_filter_shapekey(AnimCxt *ac,
                                       List *anim_data,
                                       Key *key,
                                       int filter_mode)
{
  size_t items = 0;

  /* check if channels or only F-Curves */
  if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
    DopeSheet *ads = ac->ads;

    /* loop through the channels adding ShapeKeys as appropriate */
    LIST_FOREACH (KeyBlock *, kb, &key->block) {
      /* skip the first one, since that's the non-animatable basis */
      if (kb == key->block.first) {
        continue;
      }

      /* Skip shapekey if the name doesn't match the filter string. */
      if (ads != nullptr && ads->searchstr[0] != '\0' &&
          name_matches_dopesheet_filter(ads, kb->name) == false)
      {
        continue;
      }

      /* only work with this channel and its subchannels if it is editable */
      if (!(filter_mode & ANIMFILTER_FOREDIT) || EDITABLE_SHAPEKEY(kb)) {
        /* Only include this track if sel in a way consistent
         * with the filtering requirements. */
        if (ANIMCHANNEL_SELOK(SEL_SHAPEKEY(kb))) {
          /* TODO: consider 'active' too? */

          /* owner-id here must be key so that the F-Curve can be resolved... */
          ANIMCHANNEL_NEW_CHANNEL(kb, ANIMTYPE_SHAPEKEY, key, nullptr);
        }
      }
    }
  }
  else {
    /* just use the action associated with the shapekey */
    /* TODO: somehow manage to pass dopesheet info down here too? */
    if (key->adt) {
      if (filter_mode & ANIMFILTER_ANIMDATA) {
        if (ANIMCHANNEL_SELOK(SEL_ANIMDATA(key->adt))) {
          ANIMCHANNEL_NEW_CHANNEL(key->adt, ANIMTYPE_ANIMDATA, key, nullptr);
        }
      }
      else if (key->adt->action) {
        items = animfilter_action(
            ac, anim_data, nullptr, key->adt->action, filter_mode, (ID *)key);
      }
    }
  }

  /* return the num of items added to the list */
  return items;
}

/* Helper for Pen - layers within a data-block. */
static size_t animdata_filter_pen_layer(List *anim_data,
                                                  DopeSheet * /*ads*/,
                                                  Pen *pen,
                                                  dune::pen::Layer &layer,
                                                  int filter_mode)
{

  size_t items = 0;

  /* Only if the layer is sel. */
  if (!ANIMCHANNEL_SELOK(layer.is_sel())) {
    return items;
  }

  /* Only if the layer is editable. */
  if ((filter_mode & ANIMFILTER_FOREDIT) && layer.is_locked()) {
    return items;
  }

  /* Only if the layer is active. */
  if ((filter_mode & ANIMFILTER_ACTIVE) && pen->is_layer_active(&layer)) {
    return items;
  }

  /* Skip empty layers. */
  if (layer.is_empty()) {
    return items;
  }

  /* Add layer channel. */
  ANIMCHANNEL_NEW_CHANNEL(
      static_cast<void *>(&layer), ANIMTYPE_PEN_LAYER, pen, nullptr);

  return items;
}

static size_t animdata_filter_grease_pencil_layer_node_recursive(
    List *anim_data,
    DopeSheet *ads,
    Pen *pen,
    dune::pen::TreeNode &node,
    int filter_mode)
{
  using namespace dune::pen;
  size_t items = 0;

  /* Skip node if the name doesn't match the filter string. */
  const bool name_search = (ads->searchstr[0] != '\0');
  const bool skip_node = name_search && !name_matches_dopesheet_filter(ads, node.name().c_str());

  if (node.is_layer() && !skip_node) {
    items += animdata_filter_pen_layer(
        anim_data, ads, pen, node.as_layer(), filter_mode);
  }
  else if (node.is_group()) {
    const LayerGroup &layer_group = node.as_group();

    List tmp_data = {nullptr, nullptr};
    size_t tmp_items = 0;

    /* Add grease pencil layer channels. */
    BEGIN_ANIMFILTER_SUBCHANNELS (layer_group.base.flag &PEN_LAYER_TREE_NODE_EXPANDED) {
      LIST_FOREACH_BACKWARD (PenLayerTreeNode *, node_, &layer_group.children) {
        tmp_items += animdata_filter_pen_layer_node_recursive(
            &tmp_data, ads, pen, node_->wrap(), filter_mode);
      }
    }
    END_ANIMFILTER_SUBCHANNELS;

    if ((tmp_items == 0) && !name_search) {
      /* If no sub-channels, return early.
       * Except if the search by name is on, because we might want to display the layer group alone
       * in that case. */
      return items;
    }

    if ((filter_mode & ANIMFILTER_LIST_CHANNELS) && !skip_node) {
      /* Add data block container (if for drawing, and it contains sub-channels). */
      ANIMCHANNEL_NEW_CHANNEL(
          static_cast<void *>(&node), ANIMTYPE_PEN_LAYER_GROUP, pen, nullptr);
    }

    /* Add the list of collected channels. */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }
  return items;
}

static size_t animdata_filter_pen_layers_data(List *anim_data,
                                              DopeSheet *ads,
                                              Pen *pen,
                                              int filter_mode)
{
  size_t items = 0;

  LIST_FOREACH_BACKWARD (
      PenLayerTreeNode *, node, &pen->root_group_ptr->children)
  {
    items += animdata_filter_pen_layer_node_recursive(
        anim_data, ads, pen, node->wrap(), filter_mode);
  }

  return items;
}

/* Helper for Pen - layers within a data-block. */
static size_t animdata_filter_pen_layers_data_legacy(List *anim_data,
                                                     DopeSheet *ads,
                                                     PenData *pend,
                                                     int filter_mode)
{
  size_t items = 0;

  /* loop over layers as the conditions are acceptable (top-Down order) */
  LIST_FOREACH_BACKWARD (PenLayer *, penl, &pend->layers) {
    /* only if selected */
    if (!ANIMCHANNEL_SELOK(SEL_GPL(gpl))) {
      continue;
    }

    /* only if editable */
    if ((filter_mode & ANIMFILTER_FOREDIT) && !EDITABLE_PENLAYER(penl)) {
      continue;
    }

    /* active... */
    if ((filter_mode & ANIMFILTER_ACTIVE) && (penl->flag & PEN_LAYER_ACTIVE) == 0) {
      continue;
    }

    /* skip layer if the name doesn't match the filter string */
    if (ads != nullptr && ads->searchstr[0] != '\0' &&
        name_matches_dopesheet_filter(ads, penl->info) == false)
    {
      continue;
    }

    /* Skip empty layers. */
    if (lib_list_is_empty(&penl->frames)) {
      continue;
    }

    /* add to list */
    ANIMCHANNEL_NEW_CHANNEL(penl, ANIMTYPE_PENLAYER, pend, nullptr);
  }

  return items;
}

static size_t animdata_filter_pen_data(List *anim_data,
                                       DopeSheet *ads,
                                       Pen *pen,
                                       int filter_mode)
{
  using namespace dune;

  size_t items = 0;

  /* When asked from "AnimData" blocks (i.e. the top-level containers for normal animation),
   * for convenience, this will return pen data-blocks instead.
   * This may cause issues down the track, but for now, this will do. */
  if (filter_mode & ANIMFILTER_ANIMDATA) {
    /* Just add data block container. */
    ANIMCHANNEL_NEW_CHANNEL(
        pen, ANIMTYPE_PEN_DATABLOCK, pen, nullptr);
  }
  else {
    List tmp_data = {nullptr, nullptr};
    size_t tmp_items = 0;

    if (!(filter_mode & ANIMFILTER_FCURVESONLY)) {
      /* Add pen layer channels. */
      BEGIN_ANIMFILTER_SUBCHANNELS (pen->flag &PEN_ANIM_CHANNEL_EXPANDED) {
        tmp_items += animdata_filter_pen_layers_data(
            &tmp_data, ads, pen, filter_mode);
      }
      END_ANIMFILTER_SUBCHANNELS;
    }

    if (tmp_items == 0) {
      /* If no sub-channels, return early. */
      return items;
    }

    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* Add data block container (if for drwing, and it contains sub-channels). */
      ANIMCHANNEL_NEW_CHANNEL(
          pen, ANIMTYPE_PEN_DATABLOCK, pen, nullptr);
    }

    /* Add the list of collected channels. */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  return items;
}

/* Helper for Pen - Pen data-block - Pen Frames. */
static size_t animdata_filter_pen_legacy_data(List *anim_data,
                                              DopeSheet *ads,
                                              PenData *pend,
                                              int filter_mode)
{
  size_t items = 0;

  /* When asked from "AnimData" blocks (i.e. the top-level containers for normal animation),
   * for convenience, this will return Pen Data-blocks instead.
   * This may cause issues down the track, but for now, this will do. */
  if (filter_mode & ANIMFILTER_ANIMDATA) {
    /* just add GPD as a channel - this will add everything needed */
    ANIMCHANNEL_NEW_CHANNEL(pend, ANIMTYPE_PENDATABLOCK, pend, nullptr);
  }
  else {
    List tmp_data = {nullptr, nullptr};
    size_t tmp_items = 0;

    if (!(filter_mode & ANIMFILTER_FCURVESONLY)) {
      /* add pen anim channels */
      BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_PENDATA(pend)) {
        tmp_items += animdata_filter_pen_layers_data_legacy(&tmp_data, ads, pend, filter_mode);
      }
      END_ANIMFILTER_SUBCHANNELS;
    }

    /* did we find anything? */
    if (tmp_items) {
      /* include data-expand widget first */
      if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
        /* add pend as channel too (if for drwing, and it has layers) */
        ANIMCHANNEL_NEW_CHANNEL(pend, ANIMTYPE_PENDATABLOCK, nullptr, nullptr);
      }

      /* now add the list of collected channels */
      lib_movelisttolist(anim_data, &tmp_data);
      lib_assert(lib_list_is_empty(&tmp_data));
      items += tmp_items;
    }
  }

  return items;
}

static size_t animdata_filter_pen(AnimCxt *ac, List *anim_data, int filter_mode)
{
  size_t items = 0;
  Scene *scene = ac->scene;
  ViewLayer *view_layer = (ViewLayer *)ac->view_layer;
  DopeSheet *ads = ac->ads;

  dune_view_layer_synced_ensure(scene, view_layer);
  LIST_FOREACH (Base *, base, dune_view_layer_ob_bases_get(view_layer)) {
    if (!base->ob || (base->ob->type != OB_PEN)) {
      continue;
    }
    Ob *ob = base->ob;

    if ((filter_mode & ANIMFILTER_DATA_VISIBLE) && !(ads->filterflag & ADS_FILTER_INCL_HIDDEN)) {
      /* Layer visibility - we check both object and base,
       * since these may not be in sync yet. */
      if ((base->flag & BASE_ENABLED_AND_MAYBE_VISIBLE_IN_VIEWPORT) == 0 ||
          (base->flag & BASE_ENABLED_AND_VISIBLE_IN_DEFAULT_VIEWPORT) == 0)
      {
        continue;
      }

      /* Outliner restrict-flag */
      if (ob->visibility_flag & OB_HIDE_VIEWPORT) {
        continue;
      }
    }

    /* Check sel and ob type filters */
    if ((ads->filterflag & ADS_FILTER_ONLYSEL) && !(base->flag & BASE_SEL)) {
      /* Only sel should be shown */
      continue;
    }

    if (ads->filter_grp != nullptr) {
      if (dune_collection_has_ob_recursive(ads->filter_grp, ob) == 0) {
        continue;
      }
    }

    items += animdata_filter_pen_data(
        anim_data, ads, static_cast<Pen *>(ob->data), filter_mode);
  }

  /* Return the num of items added to the list */
  return items;
}

/* Grab all Pen data-blocks in file.
 * Should this be amalgamated w the dope-sheet filtering code? */
static size_t animdata_filter_pen_legacy(AnimCxt *ac,
                                         List *anim_data,
                                         void * /*data*/,
                                         int filter_mode)
{
  DopeSheet *ads = ac->ads;
  size_t items = 0;

  Scene *scene = ac->scene;
  ViewLayer *view_layer = (ViewLayer *)ac->view_layer;

  /* Include all annotation datablocks. */
  if (((ads->filterflag & ADS_FILTER_ONLYSEL) == 0) || (ads->filterflag & ADS_FILTER_INCL_HIDDEN))
  {
    LIST_FOREACH (PenData *, pend, &ac->main->pens) {
      if (pend->flag & PEN_DATA_ANNOTATIONS) {
        items += animdata_filter_pen_legacy_data(anim_data, ads, pend, filter_mode);
      }
    }
  }
  /* Obs in the scene */
  dune_view_layer_synced_ensure(scene, view_layer);
  LIST_FOREACH (Base *, base, dune_view_layer_ob_bases_get(view_layer)) {
    /* Only consider this ob if it has got some Pen data (saving on all the other tests) */
    if (base->ob && (base->ob->type == OB_PEN_LEGACY)) {
      Ob *ob = base->ob;

      /* firstly, check if ob can be included, by the following factors:
       * - if only visible, must check for layer and also viewport visibility
       *   --> while tools may demand only visible, user setting takes priority
       *       as user option ctrls whether sets of channels get included while
       *       tool-flag takes into account collapsed/open channels too
       * - if only sel, must check if object is sel
       * - there must be animation data to edit (this is done recursively as we
       *   try to add the channels)
       */
      if ((filter_mode & ANIMFILTER_DATA_VISIBLE) && !(ads->filterflag & ADS_FILTER_INCL_HIDDEN)) {
        /* Layer visibility - we check both object and base,
         * since these may not be in sync yet. */
        if ((base->flag & BASE_ENABLED_AND_MAYBE_VISIBLE_IN_VIEWPORT) == 0 ||
            (base->flag & BASE_ENABLED_AND_VISIBLE_IN_DEFAULT_VIEWPORT) == 0)
        {
          continue;
        }

        /* outliner restrict-flag */
        if (ob->visibility_flag & OB_HIDE_VIEWPORT) {
          continue;
        }
      }

      /* check sel and ob type filters */
      if ((ads->filterflag & ADS_FILTER_ONLYSEL) && !(base->flag & BASE_SEL)) {
        /* only sel should be shown */
        continue;
      }

      /* check if ob belongs to the filtering group if option to filter
       * objects by the grouped status is on
       * - used to ease the process of doing multiple-char choreographies */
      if (ads->filter_grp != nullptr) {
        if (dune_collection_has_ob_recursive(ads->filter_grp, ob) == 0) {
          continue;
        }
      }

      /* finally, include this object's pen data-block. */
      /* Should we store these under expanders per item? */
      items += animdata_filter_pen_legacy_data(
          anim_data, ads, static_cast<PenData *>(ob->data), filter_mode);
    }
  }

  /* return the number of items added to the list */
  return items;
}

/* Helper for Pen data integrated with main DopeSheet */
static size_t animdata_filter_ds_pen(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, PenData *pend, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add relevant anim channels for Pen */
  BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_GPD(pend)) {
    /* add anim channels */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, &gpd->id, filter_mode);

    /* add Pen layers */
    if (!(filter_mode & ANIMFILTER_FCURVESONLY)) {
      tmp_items += animdata_filter_pen_layers_data_legacy(&tmp_data, ads, pend, filter_mode);
    }

    /* TODO: Do these need a separate expander?
     * What order should these go in? */
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      /* Active check here needs checking */
      if (ANIMCHANNEL_ACTIVEOK(pend)) {
        ANIMCHANNEL_NEW_CHANNEL(pend, ANIMTYPE_DSPEN, pend, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added to the list */
  return items;
}

/* Helper for Cache File data integrated with main DopeSheet */
static size_t animdata_filter_ds_cachefile(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, CacheFile *cache_file, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add relevant anim channels for Cache File */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_CACHEFILE_OBJD(cache_file)) {
    /* add animation channels */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, &cache_file->id, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      /* Active check here needs checking */
      if (ANIMCHANNEL_ACTIVEOK(cache_file)) {
        ANIMCHANNEL_NEW_CHANNEL(cache_file, ANIMTYPE_DSCACHEFILE, cache_file, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added to the list */
  return items;
}

/* Helper for Mask Editing - mask layers */
static size_t animdata_filter_mask_data(List *anim_data, Mask *mask, const int filter_mode)
{
  const MaskLayer *masklay_act = dune_mask_layer_active(mask);
  size_t items = 0;

  LIST_FOREACH (MaskLayer *, masklay, &mask->masklayers) {
    if (!ANIMCHANNEL_SELOK(SEL_MASKLAY(masklay))) {
      continue;
    }

    if ((filter_mode & ANIMFILTER_FOREDIT) && !EDITABLE_MASK(masklay)) {
      continue;
    }

    if ((filter_mode & ANIMFILTER_ACTIVE) & (masklay_act != masklay)) {
      continue;
    }

    ANIMCHANNEL_NEW_CHANNEL(masklay, ANIMTYPE_MASKLAYER, mask, nullptr);
  }

  return items;
}

/* Grab all mask data */
static size_t animdata_filter_mask(Main *bmain,
                                   ListBase *anim_data,
                                   void * /*data*/,
                                   int filter_mode)
{
  size_t items = 0;

  /* For now, grab mask data-blocks directly from main. */
  /* This is not good... */
  LIST_FOREACH (Mask *, mask, &main->masks) {
    List tmp_data = {nullptr, nullptr};
    size_t tmp_items = 0;

    /* only show if mask is used by something... */
    if (ID_REAL_USERS(mask) < 1) {
      continue;
    }

    /* add mask anim channels */
    if (!(filter_mode & ANIMFILTER_FCURVESONLY)) {
      BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_MASK(mask)) {
        tmp_items += animdata_filter_mask_data(&tmp_data, mask, filter_mode);
      }
      END_ANIMFILTER_SUBCHANNELS;
    }

    /* did we find anything? */
    if (!tmp_items) {
      continue;
    }

    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* add mask data-block as channel too (if for drwing, and it has layers) */
      ANIMCHANNEL_NEW_CHANNEL(mask, ANIMTYPE_MASKDATABLOCK, nullptr, nullptr);
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added to the list */
  return items;
}

/* Owner_id is scene, material, or texture block,
 * which is the direct owner of the node tree in question. */
static size_t animdata_filter_ds_nodetree_group(AnimCxt *ac,
                                                ListBase *anim_data,
                                                bDopeSheet *ads,
                                                ID *owner_id,
                                                bNodeTree *ntree,
                                                int filter_mode)
{
  ListBase tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add nodetree animation channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_NTREE_DATA(ntree)) {
    /* animation data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)ntree, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(ntree)) {
        ANIMCHANNEL_NEW_CHANNEL(ntree, ANIMTYPE_DSNTREE, owner_id, nullptr);
      }
    }

    /* now add the list of collected channels */
    BLI_movelisttolist(anim_data, &tmp_data);
    BLI_assert(BLI_listbase_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animdata_filter_ds_nodetree(bAnimContext *ac,
                                          ListBase *anim_data,
                                          bDopeSheet *ads,
                                          ID *owner_id,
                                          bNodeTree *ntree,
                                          int filter_mode)
{
  size_t items = 0;

  items += animdata_filter_ds_nodetree_group(ac, anim_data, ads, owner_id, ntree, filter_mode);

  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    if (node->type == NODE_GROUP) {
      if (node->id) {
        if ((ads->filterflag & ADS_FILTER_ONLYSEL) && (node->flag & NODE_SELECT) == 0) {
          continue;
        }
        /* Recurse into the node group */
        items += animdata_filter_ds_nodetree(ac,
                                             anim_data,
                                             ads,
                                             owner_id,
                                             (bNodeTree *)node->id,
                                             filter_mode | ANIMFILTER_TMP_IGNORE_ONLYSEL);
      }
    }
  }

  return items;
}

static size_t animdata_filter_ds_linestyle(
    bAnimContext *ac, ListBase *anim_data, bDopeSheet *ads, Scene *sce, int filter_mode)
{
  size_t items = 0;

  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    LISTBASE_FOREACH (FreestyleLineSet *, lineset, &view_layer->freestyle_config.linesets) {
      if (lineset->linestyle) {
        lineset->linestyle->id.tag |= LIB_TAG_DOIT;
      }
    }
  }

  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    /* skip render layers without Freestyle enabled */
    if ((view_layer->flag & VIEW_LAYER_FREESTYLE) == 0) {
      continue;
    }

    /* loop over linesets defined in the render layer */
    LISTBASE_FOREACH (FreestyleLineSet *, lineset, &view_layer->freestyle_config.linesets) {
      FreestyleLineStyle *linestyle = lineset->linestyle;
      ListBase tmp_data = {nullptr, nullptr};
      size_t tmp_items = 0;

      if ((linestyle == nullptr) || !(linestyle->id.tag & LIB_TAG_DOIT)) {
        continue;
      }
      linestyle->id.tag &= ~LIB_TAG_DOIT;

      /* add scene-level animation channels */
      BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_LS_SCED(linestyle)) {
        /* animation data filtering */
        tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)linestyle, filter_mode);
      }
      END_ANIMFILTER_SUBCHANNELS;

      /* did we find anything? */
      if (tmp_items) {
        /* include anim-expand widget first */
        if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
          /* check if filtering by active status */
          if (ANIMCHANNEL_ACTIVEOK(linestyle)) {
            ANIMCHANNEL_NEW_CHANNEL(linestyle, ANIMTYPE_DSLINESTYLE, sce, nullptr);
          }
        }

        /* now add the list of collected channels */
        lib_movelisttolist(anim_data, &tmp_data);
        lib_assert(lib_list_is_empty(&tmp_data));
        items += tmp_items;
      }
    }
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animdata_filter_ds_texture(AnimCxt *ac,
                                         List *anim_data,
                                         DopeSheet *ads,
                                         Tex *tex,
                                         Id *owner_id,
                                         int filter_mode)
{
  ListBase tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add texture's animation data to temp collection */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_TEX_DATA(tex)) {
    /* texture animdata */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)tex, filter_mode);

    /* nodes */
    if ((tex->nodetree) && !(ads->filterflag & ADS_FILTER_NONTREE)) {
      /* owner_id as id instead of texture,
       * since it'll otherwise be impossible to track the depth. */

      /* FIXME: perhaps as a result, textures should NOT be included under materials,
       * but under their own section instead so that free-floating textures can also be animated.*/
      tmp_items += animdata_filter_ds_nodetree(
          ac, &tmp_data, ads, (Id *)tex, tex->nodetree, filter_mode);
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include texture-expand widget? */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(tex)) {
        ANIMCHANNEL_NEW_CHANNEL(tex, ANIMTYPE_DSTEX, owner_id, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

/* Owner_id is the direct owner of the texture stack in question
 * Prev (legacy) was Material/Light/World before the Internal removal for 2. */
static size_t animdata_filter_ds_textures(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Id *owner_id, int filter_mode)
{
  MTex **mtex = nullptr;
  size_t items = 0;
  int a = 0;

  /* get datatype specific data first */
  if (owner_id == nullptr) {
    return 0;
  }

  switch (GS(owner_id->name)) {
    case ID_PA: {
      ParticleSettings *part = (ParticleSettings *)owner_id;
      mtex = (MTex **)(&part->mtex);
      break;
    }
    default: {
      /* invalid/unsupported option */
      if (G.debug & G_DEBUG) {
        printf("ERROR: Unsupported owner_id (i.e. texture stack) for filter textures - %s\n",
               owner_id->name);
      }
      return 0;
    }
  }

  /* Firstly check that we actually have some textures,
   * by gathering all textures in a tmp list. */
  for (a = 0; a < MAX_MTEX; a++) {
    Tex *tex = (mtex[a]) ? mtex[a]->tex : nullptr;

    /* for now, if no texture returned, skip (this shouldn't confuse the user I hope) */
    if (tex == nullptr) {
      continue;
    }

    /* add texture's anim channels */
    items += animdata_filter_ds_texture(ac, anim_data, ads, tex, owner_id, filter_mode);
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animdata_filter_ds_material(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Material *ma, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add material's animation data to temp collection */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_MAT_OBJD(ma)) {
    /* material's animation data */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)ma, filter_mode);

    /* nodes */
    if ((ma->nodetree) && !(ads->filterflag & ADS_FILTER_NONTREE)) {
      tmp_items += animdata_filter_ds_nodetree(
          ac, &tmp_data, ads, (ID *)ma, ma->nodetree, filter_mode);
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include material-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(ma)) {
        ANIMCHANNEL_NEW_CHANNEL(ma, ANIMTYPE_DSMAT, ma, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  return items;
}

static size_t animdata_filter_ds_materials(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Ob *ob, int filter_mode)
{
  size_t items = 0;
  int a = 0;

  /* First pass: take the materials referenced via the Material slots of the object. */
  for (a = 1; a <= ob->totcol; a++) {
    Material *ma = dune_ob_material_get(ob, a);

    /* if material is valid, try to add relevant contents from here */
    if (ma) {
      /* add channels */
      items += animdata_filter_ds_material(ac, anim_data, ads, ma, filter_mode);
    }
  }

  /* return the number of items added to the list */
  return items;
}

/* Tmp cxt for mod linked-data channel extraction */
struct tAnimFilterModsCxt {
  AnimCxt *ac; /* anim editor cxt */
  DopeSheet *ads;  /* dopesheet filtering settings */

  List tmp_data; /* list of channels created (but not yet added to the main list) */
  size_t items;      /* number of channels created */

  int filter_mode; /* flags for stuff we want to filter */
};

/* dependency walker cb for mod dependencies */
static void animfilter_mod_idpoin_cb(void *afm_ptr, Ob *ob, Id **idpoin, int /*cb_flag*/)
{
  tAnimFilterModsCxt *afm = (tAnimFilterModsCxt *)afm_ptr;
  Id *owner_id = &ob->id;
  Id *id = *idpoin;

  /* The walker only guarantees to give us all the ID-ptr *slots*,
   * not just the ones which are actually used, so be careful! */
  if (id == nullptr) {
    return;
  }

  /* check if this is something we're interested in... */
  switch (GS(id->name)) {
    case ID_TE: /* Textures */
    {
      Tex *tex = (Tex *)id;
      if (!(afm->ads->filterflag & ADS_FILTER_NOTEX)) {
        afm->items += animdata_filter_ds_texture(
            afm->ac, &afm->tmp_data, afm->ads, tex, owner_id, afm->filter_mode);
      }
      break;
    }
    case ID_NT: {
      NodeTree *node_tree = (NodeTree *)id;
      if (!(afm->ads->filterflag & ADS_FILTER_NONTREE)) {
        afm->items += animdata_filter_ds_nodetree(
            afm->ac, &afm->tmp_data, afm->ads, owner_id, node_tree, afm->filter_mode);
      }
    }

    /* TODO: images? */
    default:
      break;
  }
}

/* anim linked to data used by modifiers
 * Strictly speaking, mod anim is alrdy included under Ob level
 * but for some mods (e.g. Displace), there can be linked data that has settings
 * which would be nice to animate (i.e. texture params) but which are not actually
 * attached to any other obs/materials/etc. in the scene */
/* TODO: do we want an expander for this? */
static size_t animdata_filter_ds_mods(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Ob *ob, int filter_mode)
{
  tAnimFilterModsCxt afm = {nullptr};
  size_t items = 0;

  /* 1) create a tmp "cxt" containing all the info we have here to pass to the cbs
   * use to walk through the dependencies of the mods
   * Assumes all other unspecified values (i.e. accumulation buffs)
   * are zero'd out properly! */
  afm.ac = ac;
  afm.ads = ads;
  afm.filter_mode = filter_mode;

  /* 2) walk over dependencies */
  dune_mods_foreach_id_link(ob, animfilter_mod_idpoin_cb, &afm);

  /* 3) extract data from the cxt, merging it back into the standard list */
  if (afm.items) {
    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &afm.tmp_data);
    lib_assert(lib_list_is_empty(&afm.tmp_data));
    items += afm.items;
  }

  return items;
}

/* ............ */

static size_t animdata_filter_ds_particles(
    bAnimContext *ac, ListBase *anim_data, bDopeSheet *ads, Object *ob, int filter_mode)
{
  size_t items = 0;

  LISTBASE_FOREACH (ParticleSystem *, psys, &ob->particlesystem) {
    ListBase tmp_data = {nullptr, nullptr};
    size_t tmp_items = 0;

    /* Note that when psys->part->adt is nullptr the textures can still be
     * animated. */
    if (psys->part == nullptr) {
      continue;
    }

    /* add particle-system's animation data to temp collection */
    BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_PART_OBJD(psys->part)) {
      /* particle system's animation data */
      tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)psys->part, filter_mode);

      /* textures */
      if (!(ads->filterflag & ADS_FILTER_NOTEX)) {
        tmp_items += animdata_filter_ds_textures(
            ac, &tmp_data, ads, (ID *)psys->part, filter_mode);
      }
    }
    END_ANIMFILTER_SUBCHANNELS;

    /* did we find anything? */
    if (tmp_items) {
      /* include particle-expand widget first */
      if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
        /* check if filtering by active status */
        if (ANIMCHANNEL_ACTIVEOK(psys->part)) {
          ANIMCHANNEL_NEW_CHANNEL(psys->part, ANIMTYPE_DSPART, psys->part, nullptr);
        }
      }

      /* now add the list of collected channels */
      lib_movelisttolist(anim_data, &tmp_data);
      lib_assert(lib_list_is_empty(&tmp_data));
      items += tmp_items;
    }
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animdata_filter_ds_obdata(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Ob *ob, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  IdAdtTemplate *iat = static_cast<IdAdtTemplate *>(ob->data);
  short type = 0, expanded = 0;

  /* get settings based on data type */
  switch (ob->type) {
    case OB_CAMERA: /* Camera */
    {
      Camera *ca = (Camera *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOCAM) {
        return 0;
      }

      type = ANIMTYPE_DSCAM;
      expanded = FILTER_CAM_OBJD(ca);
      break;
    }
    case OB_LAMP: /* ---------- Light ----------- */
    {
      Light *la = (Light *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOLAM) {
        return 0;
      }

      type = ANIMTYPE_DSLAM;
      expanded = FILTER_LAM_OBJD(la);
      break;
    }
    case OB_CURVES_LEGACY: /* Curve */
    case OB_SURF:          /* Nurbs Surface */
    case OB_FONT:          /* Text Curve */
    {
      Curve *cu = (Curve *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOCUR) {
        return 0;
      }

      type = ANIMTYPE_DSCUR;
      expanded = FILTER_CUR_OBJD(cu);
      break;
    }
    case OB_MBALL: /* MetaBall */
    {
      MetaBall *mb = (MetaBall *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOMBA) {
        return 0;
      }

      type = ANIMTYPE_DSMBALL;
      expanded = FILTER_MBALL_OBJD(mb);
      break;
    }
    case OB_ARMATURE: /* Armature */
    {
      Armature *arm = (Armature *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOARM) {
        return 0;
      }

      type = ANIMTYPE_DSARM;
      expanded = FILTER_ARM_OBJD(arm);
      break;
    }
    case OB_MESH: /* Mesh */
    {
      Mesh *me = (Mesh *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOMESH) {
        return 0;
      }

      type = ANIMTYPE_DSMESH;
      expanded = FILTER_MESH_OBJD(me);
      break;
    }
    case OB_LATTICE: /* Lattice */
    {
      Lattice *lt = (Lattice *)ob->data;

      if (ads->filterflag & ADS_FILTER_NOLAT) {
        return 0;
      }

      type = ANIMTYPE_DSLAT;
      expanded = FILTER_LATTICE_OBJD(lt);
      break;
    }
    case OB_SPEAKER: /* Speaker */
    {
      Speaker *spk = (Speaker *)ob->data;

      type = ANIMTYPE_DSSPK;
      expanded = FILTER_SPK_OBJD(spk);
      break;
    }
    case OB_CURVES: /* Curves */
    {
      Curves *curves = (Curves *)ob->data;

      if (ads->filterflag2 & ADS_FILTER_NOHAIR) {
        return 0;
      }

      type = ANIMTYPE_DSHAIR;
      expanded = FILTER_CURVES_OBJD(curves);
      break;
    }
    case OB_POINTCLOUD: /* PointCloud */
    {
      PointCloud *pointcloud = (PointCloud *)ob->data;

      if (ads->filterflag2 & ADS_FILTER_NOPOINTCLOUD) {
        return 0;
      }

      type = ANIMTYPE_DSPOINTCLOUD;
      expanded = FILTER_POINTS_OBJD(pointcloud);
      break;
    }
    case OB_VOLUME: /* Volume */
    {
      Volume *volume = (Volume *)ob->data;

      if (ads->filterflag2 & ADS_FILTER_NOVOLUME) {
        return 0;
      }

      type = ANIMTYPE_DSVOLUME;
      expanded = FILTER_VOLUME_OBJD(volume);
      break;
    }
  }

  /* add ob data anim channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (expanded) {
    /* anim data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (Id *)iat, filter_mode);

    /* sub-data filtering... */
    switch (ob->type) {
      case OB_LAMP: /* light - textures + nodetree */
      {
        Light *la = static_cast<Light *>(ob->data);
        NodeTree *ntree = la->nodetree;

        /* nodetree */
        if ((ntree) && !(ads->filterflag & ADS_FILTER_NONTREE)) {
          tmp_items += animdata_filter_ds_nodetree(
              ac, &tmp_data, ads, &la->id, ntree, filter_mode);
        }
        break;
      }
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(iat)) {
        ANIMCHANNEL_NEW_CHANNEL(iat, type, iat, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

/* shapekey-level anim */
static size_t animdata_filter_ds_keyanim(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Ob *ob, Key *key, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add shapekey-level anim channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_SKE_OBJD(key)) {
    /* anim data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (Id *)key, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include key-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      if (ANIMCHANNEL_ACTIVEOK(key)) {
        ANIMCHANNEL_NEW_CHANNEL(key, ANIMTYPE_DSSKEY, ob, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

/* object-level animation */
static size_t animdata_filter_ds_obanim(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Ob *ob, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  AnimData *adt = ob->adt;
  short type = 0, expanded = 1;
  void *cdata = nullptr;

  /* determine the type of expander channels to use */
  /* this is the best way to do this for now... */
  ANIMDATA_FILTER_CASES(
      ob, /* Some useless long comment to prevent wrapping by old clang-format versions... */
      {/* AnimData - no channel, but consider data */},
      {/* NLA - no channel, but consider data */},
      { /* Drivers */
        type = ANIMTYPE_FILLDRIVERS;
        cdata = adt;
        expanded = EXPANDED_DRVD(adt);
      },
      {/* NLA Strip Controls - no dedicated channel for now (XXX) */},
      { /* Keyframes */
        type = ANIMTYPE_FILLACTD;
        cdata = adt->action;
        expanded = EXPANDED_ACTC(adt->action);
      });

  /* add ob-lvl anim channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (expanded) {
    /* anim data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (Id *)ob, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include anim-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      if (type != ANIMTYPE_NONE) {
        /* Active-status (and the assoc checks) don't apply here... */
        ANIMCHANNEL_NEW_CHANNEL(cdata, type, ob, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added to the list */
  return items;
}

/* get anim channels from ob2 */
static size_t animdata_filter_dopesheet_ob(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Base *base, int filter_mode)
{
  ListBase tmp_data = {nullptr, nullptr};
  Object *ob = base->object;
  size_t tmp_items = 0;
  size_t items = 0;

  /* filter data contained under ob first */
  BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_OBJC(ob)) {
    Key *key = dune_key_from_ob(ob);

    /* ob-level animation */
    if ((ob->adt) && !(ads->filterflag & ADS_FILTER_NOOBJ)) {
      tmp_items += animdata_filter_ds_obanim(ac, &tmp_data, ads, ob, filter_mode);
    }

    /* particle deflector textures */
    if (ob->pd != nullptr && ob->pd->tex != nullptr && !(ads->filterflag & ADS_FILTER_NOTEX)) {
      tmp_items += animdata_filter_ds_texture(
          ac, &tmp_data, ads, ob->pd->tex, &ob->id, filter_mode);
    }

    /* shape-key */
    if ((key && key->adt) && !(ads->filterflag & ADS_FILTER_NOSHAPEKEYS)) {
      tmp_items += animdata_filter_ds_keyanim(ac, &tmp_data, ads, ob, key, filter_mode);
    }

    /* mods */
    if ((ob->mods.first) && !(ads->filterflag & ADS_FILTER_NOMODS)) {
      tmp_items += animdata_filter_ds_mods(ac, &tmp_data, ads, ob, filter_mode);
    }

    /* materials */
    if ((ob->totcol) && !(ads->filterflag & ADS_FILTER_NOMAT)) {
      tmp_items += animdata_filter_ds_materials(ac, &tmp_data, ads, ob, filter_mode);
    }

    /* ob data */
    if ((ob->data) && (ob->type != OB_PEN_LEGACY)) {
      tmp_items += animdata_filter_ds_obdata(ac, &tmp_data, ads, ob, filter_mode);
    }

    /* particles */
    if ((ob->particlesys.first) && !(ads->filterflag & ADS_FILTER_NOPART)) {
      tmp_items += animdata_filter_ds_particles(ac, &tmp_data, ads, ob, filter_mode);
    }

    /* pen */
    if ((ob->type == OB_PEN_LEGACY) && (ob->data) && !(ads->filterflag & ADS_FILTER_NOPEN))
    {
      tmp_items += animdata_filter_ds_gpencil(
          ac, &tmp_data, ads, static_cast<bGPdata *>(ob->data), filter_mode);
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* if we collected some channels, add these to the new list... */
  if (tmp_items) {
    /* firstly add ob expander if required */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* Check if filtering by sel */
      /* double-check on this -
       * most of the time, a lot of tools need to filter out these channels! */
      if (ANIMCHANNEL_SELOK((base->flag & BASE_SEL))) {
        /* check if filtering by active status */
        if (ANIMCHANNEL_ACTIVEOK(ob)) {
          ANIMCHANNEL_NEW_CHANNEL(base, ANIMTYPE_OB, ob, nullptr);
        }
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added */
  return items;
}

static size_t animdata_filter_ds_world(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Scene *sce, World *wo, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* add world anim channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (FILTER_WOR_SCED(wo)) {
    /* anim data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (Id *)wo, filter_mode);

    /* nodes */
    if ((wo->nodetree) && !(ads->filterflag & ADS_FILTER_NONTREE)) {
      tmp_items += animdata_filter_ds_nodetree(
          ac, &tmp_data, ads, (Id *)wo, wo->nodetree, filter_mode);
    }
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(wo)) {
        ANIMCHANNEL_NEW_CHANNEL(wo, ANIMTYPE_DSWOR, sce, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added to the list */
  return items;
}

static size_t animdata_filter_ds_scene(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Scene *sce, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  AnimData *adt = sce->adt;
  short type = 0, expanded = 1;
  void *cdata = nullptr;

  /* determine the type of expander channels to use */
  /* this is the best way to do this for now... */
  ANIMDATA_FILTER_CASES(
      sce, /* Some useless long comment to prevent wrapping by old clang-format versions... */
      {/* AnimData - no channel, but consider data */},
      {/* NLA - no channel, but consider data */},
      { /* Drivers */
        type = ANIMTYPE_FILLDRIVERS;
        cdata = adt;
        expanded = EXPANDED_DRVD(adt);
      },
      {/* NLA Strip Ctrls - no dedicated channel for now (XXX) */},
      { /* Keyframes */
        type = ANIMTYPE_FILLACTD;
        cdata = adt->action;
        expanded = EXPANDED_ACTC(adt->action);
      });

  /* add scene-lvl anim channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (expanded) {
    /* anim data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (Id *)sce, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* did we find anything? */
  if (tmp_items) {
    /* include anim-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      if (type != ANIMTYPE_NONE) {
        /* NOTE: active-status (and the assoc checks) don't apply here... */
        ANIMCHANNEL_NEW_CHANNEL(cdata, type, sce, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the num of items added to the list */
  return items;
}

static size_t animdata_filter_dopesheet_scene(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, Scene *sce, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;

  /* filter data contained under object first */
  BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_SCEC(sce)) {
    NodeTree *ntree = sce->nodetree;
    PenData *pend = sce->pend;
    World *wo = sce->world;

    /* Action, Drivers, or NLA for Scene */
    if ((ads->filterflag & ADS_FILTER_NOSCE) == 0) {
      tmp_items += animdata_filter_ds_scene(ac, &tmp_data, ads, sce, filter_mode);
    }

    /* world */
    if ((wo) && !(ads->filterflag & ADS_FILTER_NOWOR)) {
      tmp_items += animdata_filter_ds_world(ac, &tmp_data, ads, sce, wo, filter_mode);
    }

    /* nodetree */
    if ((ntree) && !(ads->filterflag & ADS_FILTER_NONTREE)) {
      tmp_items += animdata_filter_ds_nodetree(ac, &tmp_data, ads, (Id *)sce, ntree, filter_mode);
    }

    /* line styles */
    if ((ads->filterflag & ADS_FILTER_NOLINESTYLE) == 0) {
      tmp_items += animdata_filter_ds_linestyle(ac, &tmp_data, ads, sce, filter_mode);
    }

    /* pen */
    if ((pend) && !(ads->filterflag & ADS_FILTER_NOPEN)) {
      tmp_items += animdata_filter_ds_pen(ac, &tmp_data, ads, pd, filter_mode);
    }

    /* TODO: one day, when seq becomes its own datatype,
     * perhaps it should be included here. */
  }
  END_ANIMFILTER_SUBCHANNELS;

  /* if we collected some channels, add these to the new list... */
  if (tmp_items) {
    /* firstly add object expander if required */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by sel */
      if (ANIMCHANNEL_SELOK((sce->flag & SCE_DS_SEL))) {
        /* NOTE: active-status doesn't matter for this! */
        ANIMCHANNEL_NEW_CHANNEL(sce, ANIMTYPE_SCENE, sce, nullptr);
      }
    }

    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }

  /* return the number of items added */
  return items;
}

static size_t animdata_filter_ds_movieclip(
    AnimCxt *ac, List *anim_data, DopeSheet *ads, MovieClip *clip, int filter_mode)
{
  List tmp_data = {nullptr, nullptr};
  size_t tmp_items = 0;
  size_t items = 0;
  /* add world animation channels */
  BEGIN_ANIMFILTER_SUBCHANNELS (EXPANDED_MCLIP(clip)) {
    /* animation data filtering */
    tmp_items += animfilter_block_data(ac, &tmp_data, ads, (ID *)clip, filter_mode);
  }
  END_ANIMFILTER_SUBCHANNELS;
  /* did we find anything? */
  if (tmp_items) {
    /* include data-expand widget first */
    if (filter_mode & ANIMFILTER_LIST_CHANNELS) {
      /* check if filtering by active status */
      if (ANIMCHANNEL_ACTIVEOK(clip)) {
        ANIMCHANNEL_NEW_CHANNEL(clip, ANIMTYPE_DSMCLIP, clip, nullptr);
      }
    }
    /* now add the list of collected channels */
    lib_movelisttolist(anim_data, &tmp_data);
    lib_assert(lib_list_is_empty(&tmp_data));
    items += tmp_items;
  }
  /* return the num of items added to the list */
  return items;
}

static size_t animdata_filter_dopesheet_movieclips(AnimCxt *ac,
                                                   List *anim_data,
                                                   DopeSheet *ads,
                                                   int filter_mode)
{
  size_t items = 0;
  LISTBASE_FOREACH (MovieClip *, clip, &ac->main->movieclips) {
    /* only show if gpd is used by something... */
    if (ID_REAL_USERS(clip) < 1) {
      continue;
    }
    items += animdata_filter_ds_movieclip(ac, anim_data, ads, clip, filter_mode);
  }
  /* return num of items added to the list */
  return items;
}

/* Helper for animdata_filter_dopesheet() - For checking if an ob should be included or not */
static bool animdata_filter_base_is_ok(DopeSheet *ads,
                                       Base *base,
                                       const eObMode ob_mode,
                                       int filter_mode)
{
  Ob *ob = base->ob;

  if (base->ob == nullptr) {
    return false;
  }

  /* 1st check if ob can be included by the following factors:
   * - if only visible, must check for layer and also viewport visibility
   *   --> while tools may demand only visible, user setting takes priority
   *       as user option ctrls whether sets of channels get included while
   *       tool-flag takes into account collapsed/open channels too
   * - if only sel, must check if object is selected
   * - there must be anim data to edit (this is done recursively as we
   *   try to add the channels)  */
  if ((filter_mode & ANIMFILTER_DATA_VISIBLE) && !(ads->filterflag & ADS_FILTER_INCL_HIDDEN)) {
    /* layer visibility - we check both object and base, since these may not be in sync yet */
    if ((base->flag & BASE_ENABLED_AND_MAYBE_VISIBLE_IN_VIEWPORT) == 0 ||
        (base->flag & BASE_ENABLED_AND_VISIBLE_IN_DEFAULT_VIEWPORT) == 0)
    {
      return false;
    }

    /* outliner restrict-flag */
    if (ob->visibility_flag & OB_HIDE_VIEWPORT) {
      return false;
    }
  }

  /* if only F-Curves with visible flags set can be shown, check that
   * data-block hasn't been set to invisible.
   */
  if (filter_mode & ANIMFILTER_CURVE_VISIBLE) {
    if ((ob->adt) && (ob->adt->flag & ADT_CURVES_NOT_VISIBLE)) {
      return false;
    }
  }

  /* Pinned curves are visible regardless of selection flags. */
  if ((ob->adt) && (ob->adt->flag & ADT_CURVES_ALWAYS_VISIBLE)) {
    return true;
  }

  /* Special case.
   * We don't do recursive checks for pin, but we need to deal with tricky
   * setup like anim camera lens wo anim camera location.
   * Wo such special handle here we wouldn't be able to bin such
   * camera data only anim to the editor. */
  if (ob->adt == nullptr && ob->data != nullptr) {
    AnimData *data_adt = dune_animdata_from_id(static_cast<ID *>(ob->data));
    if (data_adt != nullptr && (data_adt->flag & ADT_CURVES_ALWAYS_VISIBLE)) {
      return true;
    }
  }

  /* check selection and object type filters */
  if (ads->filterflag & ADS_FILTER_ONLYSEL) {
    if (ob_mode & OB_MODE_POSE) {
      /* When in pose-mode handle all pose-mode obs.
       * This avoids problems with pose-mode where objects may be unselected,
       * where a sel bone of an unsel ob would be hidden. see: #81922. */
      if (!(base->ob->mode & ob_mode)) {
        return false;
      }
    }
    else {
      /* only sel should be shown (ignore active) */
      if (!(base->flag & BASE_SEL)) {
        return false;
      }
    }
  }

  /* check if ob belongs to the filtering group if option to filter
   * objects by the grouped status is on
   * - used to ease the process of doing multiple-character choreographies */
  if (ads->filter_grp != nullptr) {
    if (dune_collection_has_ob_recursive(ads->filter_grp, ob) == 0) {
      return false;
    }
  }

  /* no reason to exclude this ob... */
  return true;
}

/* Helper for animdata_filter_ds_sorted_bases() - Comparison cb for two Base ptrs... */
static int ds_base_sorting_cmp(const void *base1_ptr, const void *base2_ptr)
{
  const Base *b1 = *((const Base **)base1_ptr);
  const Base *b2 = *((const Base **)base2_ptr);

  return strcmp(b1->ob->id.name + 2, b2->object->id.name + 2);
}

/* Get a sorted list of all the bases - for inclusion in dopesheet (when drawing channels) */
static Base **animdata_filter_ds_sorted_bases(DopeSheet *ads,
                                              const Scene *scene,
                                              ViewLayer *view_layer,
                                              int filter_mode,
                                              size_t *r_usable_bases)
{
  /* Create an array with space for all the bases, but only containing the usable ones */
  dune_view_layer_synced_ensure(scene, view_layer);
  List *ob_bases = dune_view_layer_ob_bases_get(view_layer);
  size_t tot_bases = lib_list_count(ob_bases);
  size_t num_bases = 0;

  Base **sorted_bases = mem_cnew_array<Base *>(tot_bases, "Dopesheet Usable Sorted Bases");
  LIST_FOREACH (Base *, base, ob_bases) {
    if (animdata_filter_base_is_ok(ads, base, OB_MODE_OB, filter_mode)) {
      sorted_bases[num_bases++] = base;
    }
  }

  /* Sort this list of ptrs (based on the names) */
  qsort(sorted_bases, num_bases, sizeof(Base *), ds_base_sorting_cmp);

  /* Return list of sorted bases */
  *r_usable_bases = num_bases;
  return sorted_bases;
}

/* TODO: implement pinning...
 * (if and when pinning is done, what we need to do is to provide freeing mechanisms -
 * to protect against data that was deleted). */
static size_t animdata_filter_dopesheet(AnimCxt *ac,
                                        List *anim_data,
                                        DopeSheet *ads,
                                        int filter_mode)
{
  Scene *scene = (Scene *)ads->src;
  ViewLayer *view_layer = (ViewLayer *)ac->view_layer;
  size_t items = 0;

  /* check that we do indeed have a scene */
  if ((ads->src == nullptr) || (GS(ads->src->name) != ID_SCE)) {
    printf("Dope Sheet Error: No scene!\n");
    if (G.debug & G_DEBUG) {
      printf("\tPtr = %p, Name = '%s'\n",
             (void *)ads->src,
             (ads->src) ? ads->sr ->name : nullptr);
    }
    return 0;
  }

  /* augment the filter-flags with settings based on the dopesheet filterflags
   * so that some tmp settings can get added automagically... */
  if (ads->filterflag & ADS_FILTER_SELEDIT) {
    /* only sel F-Curves should get their keyframes considered for editability */
    filter_mode |= ANIMFILTER_SELEDIT;
  }

  /* Cache files level animations (frame duration and such). */
  if (!(ads->filterflag2 & ADS_FILTER_NOCACHEFILES) && !(ads->filterflag & ADS_FILTER_ONLYSEL)) {
    LIST_FOREACH (CacheFile *, cache_file, &ac->main->cachefiles) {
      items += animdata_filter_ds_cachefile(ac, anim_data, ads, cache_file, filter_mode);
    }
  }

  /* movie clip's anim */
  if (!(ads->filterflag2 & ADS_FILTER_NOMOVIECLIPS) && !(ads->filterflag & ADS_FILTER_ONLYSEL)) {
    items += animdata_filter_dopesheet_movieclips(ac, anim_data, ads, filter_mode);
  }

  /* Scene-linked anim - e.g. world, compositing nodes, scene anim
   * (including seq currently). */
  items += animdata_filter_dopesheet_scene(ac, anim_data, ads, scene, filter_mode);

  /* If filtering for channel drwing, we want the obs in alphabetical order,
   * to make it easier to predict where items are in the hierarchy
   * - This order only rly matters
   *   if we need to show all channels in the list (e.g. for drawing).
   *   (What about lingering "active" flags? The order may now become unpredictable)
   * - Don't do this if this behavior has been turned off (i.e. due to it being too slow)
   * - Don't do this if there's just a single object
   */
  dune_view_layer_synced_ensure(scene, view_layer);
  List *ob_bases = dune_view_layer_ob_bases_get(view_layer);
  if ((filter_mode & ANIMFILTER_LIST_CHANNELS) && !(ads->flag & ADS_FLAG_NO_DB_SORT) &&
      (ob_bases->first != ob_bases->last))
  {
    /* Filter list of bases (i.e. obs), sort them, then add their contents normally... */
    /* TODO: Cache the old sorted order - if the set of bases hasn't changed, don't re-sort... */
    Base **sorted_bases;
    size_t num_bases;

    sorted_bases = animdata_filter_ds_sorted_bases(
        ads, scene, view_layer, filter_mode, &num_bases);
    if (sorted_bases) {
      /* Add the necessary channels for these bases... */
      for (size_t i = 0; i < num_bases; i++) {
        items += animdata_filter_dopesheet_ob(ac, anim_data, ads, sorted_bases[i], filter_mode);
      }

      /* TODO: store something to validate whether any changes are needed? */
      /* free tmp data */
      mem_free(sorted_bases);
    }
  }
  else {
    /* Filter and add contents of each base (i.e. object) without them sorting first
     * This saves performance in cases where order doesn't matter   */
    Ob *obact = dune_view_layer_active_ob_get(view_layer);
    const eObMode ob_mode = (obact != nullptr) ? eObMode(obact->mode) : OB_MODE_OB;
    LIST_FOREACH (Base *, base, ob_bases) {
      if (animdata_filter_base_is_ok(ads, base, object_mode, filter_mode)) {
        /* since we're still here, this ob should be usable */
        items += animdata_filter_dopesheet_ob(ac, anim_data, ads, base, filter_mode);
      }
    }
  }

  /* return num items in the list */
  return items;
}

/* Summary track for DopeSheet/Action Editor
 * - return code is whether the summary lets the other channels get drawn
 */
static short animdata_filter_dopesheet_summary(AnimCxt *ac,
                                               List *anim_data,
                                               int filter_mode,
                                               size_t *items)
{
  DopeSheet *ads = nullptr;

  /* get the DopeSheet information to use
   * - we should only need to deal with the DopeSheet/Action Editor,
   *   since all the other Anim Editors won't have this concept
   *   being applicable. */
  if ((ac && ac->sl) && (ac->spacetype == SPACE_ACTION)) {
    SpaceAction *saction = (SpaceAction *)ac->sl;
    ads = &saction->ads;
  }
  else {
    /* invalid space type - skip this summary channels */
    return 1;
  }

  /* dopesheet summary
   * - only for drwing and/or selecting keyframes in channels, but not for real editing
   * - only useful for DopeSheet/Action/etc. editors where it is actually useful  */
  if ((filter_mode & ANIMFILTER_LIST_CHANNELS) && (ads->filterflag & ADS_FILTER_SUMMARY)) {
    AnimListElem *ale = make_new_animlistelem(ac, ANIMTYPE_SUMMARY, nullptr, nullptr);
    if (ale) {
      lib_addtail(anim_data, ale);
      (*items)++;
    }

    /* If summary is collapsed, don't show other channels beneath this - this check is put inside
     * the summary check so that it doesn't interfere with normal op */
    if (ads->flag & ADS_FLAG_SUMMARY_COLLAPSED) {
      return 0;
    }
  }

  /* the other channels beneath this can be shown */
  return 1;
}

/* filter data assoc with a channel - usually for handling summary-channels in DopeSheet */
static size_t animdata_filter_animchan(AnimCxt *ac,
                                       List *anim_data,
                                       DopeSheet *ads,
                                       AnimListElem *channel,
                                       int filter_mode)
{
  size_t items = 0;

  /* data to filter depends on channel type */
  /* Only common channel-types have been handled for now. More can be added as necessary */
  switch (channel->type) {
    case ANIMTYPE_SUMMARY:
      items += animdata_filter_dopesheet(ac, anim_data, ads, filter_mode);
      break;

    case ANIMTYPE_SCENE:
      items += animdata_filter_dopesheet_scene(
          ac, anim_data, ads, static_cast<Scene *>(channel->data), filter_mode);
      break;

    case ANIMTYPE_OB:
      items += animdata_filter_dopesheet_ob(
          ac, anim_data, ads, static_cast<Base *>(channel->data), filter_mode);
      break;

    case ANIMTYPE_DSCACHEFILE:
      items += animdata_filter_ds_cachefile(
          ac, anim_data, ads, static_cast<CacheFile *>(channel->data), filter_mode);
      break;

    case ANIMTYPE_ANIMDATA:
      items += animfilter_block_data(ac, anim_data, ads, channel->id, filter_mode);
      break;

    default:
      printf("ERROR: Unsupported channel type (%d) in animdata_filter_animchan()\n",
             channel->type);
      break;
  }

  return items;
}

/* Cleanup API */

/* Remove entries with invalid types in animation channel list */
static size_t animdata_filter_remove_invalid(ListBase *anim_data)
{
  size_t items = 0;

  /* only keep entries with valid types */
  LISTBASE_FOREACH_MUTABLE (bAnimListElem *, ale, anim_data) {
    if (ale->type == ANIMTYPE_NONE) {
      BLI_freelinkN(anim_data, ale);
    }
    else {
      items++;
    }
  }

  return items;
}

/* Remove duplicate entries in animation channel list */
static size_t animdata_filter_remove_duplis(ListBase *anim_data)
{
  GSet *gs;
  size_t items = 0;

  /* Build new hash-table to efficiently store and retrieve which entries have been
   * encountered already while searching. */
  gs = BLI_gset_ptr_new(__func__);

  /* loop through items, removing them from the list if a similar item occurs already */
  LISTBASE_FOREACH_MUTABLE (bAnimListElem *, ale, anim_data) {
    /* check if hash has any record of an entry like this
     * - just use ale->data for now, though it would be nicer to involve
     *   ale->type in combination too to capture corner cases
     *   (where same data performs differently)
     */
    if (BLI_gset_add(gs, ale->data)) {
      /* this entry is 'unique' and can be kept */
      items++;
    }
    else {
      /* this entry isn't needed anymore */
      BLI_freelinkN(anim_data, ale);
    }
  }

  /* free the hash... */
  BLI_gset_free(gs, nullptr);

  /* return the number of items still in the list */
  return items;
}

/* ----------- Public API --------------- */

size_t ANIM_animdata_filter(bAnimContext *ac,
                            ListBase *anim_data,
                            eAnimFilter_Flags filter_mode,
                            void *data,
                            eAnimCont_Types datatype)
{
  size_t items = 0;

  /* only filter data if there's somewhere to put it */
  if (data && anim_data) {
    /* firstly filter the data */
    switch (datatype) {
      /* Action-Editing Modes */
      case ANIMCONT_ACTION: /* 'Action Editor' */
      {
        Object *obact = ac->obact;
        SpaceAction *saction = (SpaceAction *)ac->sl;
        bDopeSheet *ads = (saction) ? &saction->ads : nullptr;

        /* specially check for AnimData filter, see #36687. */
        if (UNLIKELY(filter_mode & ANIMFILTER_ANIMDATA)) {
          /* all channels here are within the same AnimData block, hence this special case */
          if (LIKELY(obact->adt)) {
            ANIMCHANNEL_NEW_CHANNEL(obact->adt, ANIMTYPE_ANIMDATA, (ID *)obact, nullptr);
          }
        }
        else {
          /* The check for the DopeSheet summary is included here
           * since the summary works here too. */
          if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
            items += animfilter_action(
                ac, anim_data, ads, static_cast<bAction *>(data), filter_mode, (ID *)obact);
          }
        }

        break;
      }
      case ANIMCONT_SHAPEKEY: /* 'ShapeKey Editor' */
      {
        Key *key = (Key *)data;

        /* specially check for AnimData filter, see #36687. */
        if (UNLIKELY(filter_mode & ANIMFILTER_ANIMDATA)) {
          /* all channels here are within the same AnimData block, hence this special case */
          if (LIKELY(key->adt)) {
            ANIMCHANNEL_NEW_CHANNEL(key->adt, ANIMTYPE_ANIMDATA, (ID *)key, nullptr);
          }
        }
        else {
          /* The check for the DopeSheet summary is included here
           * since the summary works here too. */
          if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
            items = animdata_filter_shapekey(ac, anim_data, key, filter_mode);
          }
        }

        break;
      }

      /* Modes for Specialty Data Types (i.e. not keyframes) */
      case ANIMCONT_PEN: {
        if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
          if (U.experimental.use_grease_pencil_version3) {
            items = animdata_filter_grease_pencil(ac, anim_data, filter_mode);
          }
          else {
            items = animdata_filter_gpencil_legacy(ac, anim_data, data, filter_mode);
          }
        }
        break;
      }
      case ANIMCONT_MASK: {
        if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
          items = animdata_filter_mask(ac->bmain, anim_data, data, filter_mode);
        }
        break;
      }

      /* DopeSheet Based Modes */
      case ANIMCONT_DOPESHEET: /* 'DopeSheet Editor' */
      {
        /* the DopeSheet editor is the primary place where the DopeSheet summaries are useful */
        if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
          items += animdata_filter_dopesheet(
              ac, anim_data, static_cast<bDopeSheet *>(data), filter_mode);
        }
        break;
      }
      case ANIMCONT_FCURVES: /* Graph Editor -> F-Curves/Animation Editing */
      case ANIMCONT_DRIVERS: /* Graph Editor -> Drivers Editing */
      case ANIMCONT_NLA:     /* NLA Editor */
      {
        /* all of these editors use the basic DopeSheet data for filtering options,
         * but don't have all the same features */
        items = animdata_filter_dopesheet(
            ac, anim_data, static_cast<bDopeSheet *>(data), filter_mode);
        break;
      }

      /* Timeline Mode - Basically the same as dopesheet,
       * except we only have the summary for now */
      case ANIMCONT_TIMELINE: {
        /* the DopeSheet editor is the primary place where the DopeSheet summaries are useful */
        if (animdata_filter_dopesheet_summary(ac, anim_data, filter_mode, &items)) {
          items += animdata_filter_dopesheet(
              ac, anim_data, static_cast<bDopeSheet *>(data), filter_mode);
        }
        break;
      }

      /* Special/Internal Use */
      case ANIMCONT_CHANNEL: /* animation channel */
      {
        DopeSheet *ads = ac->ads;

        /* based on the channel type, filter relevant data for this */
        items = animdata_filter_animchan(
            ac, anim_data, ads, static_cast<AnimListElem *>(data), filter_mode);
        break;
      }

      /* unhandled */
      default: {
        printf("ANIM_animdata_filter() - Invalid datatype argument %i\n", datatype);
        break;
      }
    }

    /* remove any 'weedy' entries */
    items = animdata_filter_remove_invalid(anim_data);

    /* remove duplicates (if required) */
    if (filter_mode & ANIMFILTER_NODUPLIS) {
      items = animdata_filter_remove_duplis(anim_data);
    }
  }

  /* return the number of items in the list */
  return items;
}

/* ************************************************************ */
