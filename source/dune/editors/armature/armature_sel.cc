#include "mem_guardedalloc.h"

#include "types_armature.h"
#include "types_ob.h"
#include "types_scene.h"

#include "lib_blenlib.h"
#include "lib_math_matrix.h"
#include "lib_math_vector.h"
#include "lib_rect.h"
#include "lib_string_utils.hh"

#include "dune_action.h"
#include "dune_armature.hh"
#include "dune_cxt.hh"
#include "dune_layer.h"
#include "dune_ob.hh"
#include "dune_ob_types.hh"
#include "dune_report.h"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_ob.hh"
#include "ed_outliner.hh"
#include "ed_screen.hh"
#include "ed_sel_utils.hh"
#include "ed_view3d.hh"

#include "graph.hh"

#include "gpu_sel.hh"

#include "anim_bone_collections.hh"
#include "anim_bonecolor.hh"

#include "armature_intern.h"

/* util macros for storing a tmp int in the bone (sel flag) */
#define EBONE_PREV_FLAG_GET(ebone) ((void)0, (ebone)->tmp.i)
#define EBONE_PREV_FLAG_SET(ebone, val) ((ebone)->tmp.i = val)


/* Sel Buf Queries for PoseMode & EditMode*/
Base *ed_armature_base_and_ebone_from_sel_buf(Base **bases,
                                              uint bases_len,
                                              const uint sel_id,
                                              EditBone **r_ebone)
{
  const uint hit_ob = sel_id & 0xFFFF;
  Base *base = nullptr;
  EditBone *ebone = nullptr;
  /* TODO: optimize, eg: sort & binary search. */
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    if (bases[base_index]->ob->runtime->select_id == hit_ob) {
      base = bases[base_index];
      break;
    }
  }
  if (base != nullptr) {
    const uint hit_bone = (sel_id & ~BONESEL_ANY) >> 16;
    Armature *arm = static_cast<Armature *>(base->ob->data);
    ebone = static_cast<EditBone *>(lin_findlink(arm->edbo, hit_bone));
  }
  *r_ebone = ebone;
  return base;
}

Ob *ed_armature_ob_and_ebone_from_sel_buf(Ob **obs,
                                          uint obs_len,
                                          const uint sel_id,
                                          EditBone **r_ebone)
{
  const uint hit_ob = sel_id & 0xFFFF;
  Ob *ob = nullptr;
  EditBone *ebone = nullptr;
  /* TODO: optimize, eg: sort & binary search. */
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    if (obs[ob_index]->runtime->sel_id == hit_ob) {
      ob = obs[ob_index];
      break;
    }
  }
  if (ob != nullptr) {
    const uint hit_bone = (select_id & ~BONESEL_ANY) >> 16;
    Armature *arm = static_cast<Armature *>(ob->data);
    ebone = static_cast<EditBone *>(lib_findlink(arm->edbo, hit_bone));
  }
  *r_ebone = ebone;
  return ob;
}

Base *ed_armature_base_and_pchan_from_sel_buf(Base **bases,
                                              uint bases_len,
                                              const uint sel_id,
                                              PoseChannel **r_pchan)
{
  const uint hit_ob = sel_id & 0xFFFF;
  Base *base = nullptr;
  PoseChannel *pchan = nullptr;
  /* TODO: optimize, eg: sort & binary search. */
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    if (bases[base_index]->ob->runtime->sel_id == hit_ob) {
      base = bases[base_index];
      break;
    }
  }
  if (base != nullptr) {
    if (base->ob->pose != nullptr) {
      const uint hit_bone = (sel_id & ~BONESEL_ANY) >> 16;
      /* pchan may be nullptr. */
      pchan = static_cast<PoseChannel *>(lib_findlink(&base->ob->pose->chanbase, hit_bone));
    }
  }
  *r_pchan = pchan;
  return base;
}

Base *ed_armature_base_and_bone_from_sel_buf(Base **bases,
                                             uint bases_len,
                                             const uint sel_id,
                                             Bone **r_bone)
{
  PoseChannel *pchan = nullptr;
  Base *base = ed_armature_base_and_pchan_from_sel_buf(bases, bases_len, sel_id, &pchan);
  *r_bone = pchan ? pchan->bone : nullptr;
  return base;
}

/* Cursor Pick from Sel Buf API
 * Internal ed_armature_pick_bone_from_selbuf_impl is exposed as:
 *-ed_armature_pick_ebone_from_selbuf
 *-ed_armature_pick_pchan_from_selbuf
 *-ed_armature_pick_bone_from_selbuf */

/* check if sel bones in this buf */
/* only bones from base are checked on */
static void *ed_armature_pick_bone_from_selbuf_impl(
    const bool is_editmode,
    Base **bases,
    uint bases_len,
    const dune::Span<GPUSelResult> hit_results,
    bool findunsel,
    bool do_nearest,
    Base **r_base)
{
  PoseChannel *pchan;
  EditBone *ebone;
  void *firstunSel = nullptr, *firstSel = nullptr, *data;
  Base *firstunSel_base = nullptr, *firstSel_base = nullptr;
  bool takeNext = false;
  int minsel = 0xffffffff, minunsel = 0xffffffff;

  for (const GPUSelResult &hit_result : hit_results) {
    uint hit_id = hit_result.id;

    if (hit_id & BONESEL_ANY) { /* to avoid including obs in sel */
      Base *base = nullptr;
      bool sel;

      hit_id &= ~BONESEL_ANY;
      /* Determine what the current bone is */
      if (is_editmode == false) {
        base = ed_armature_base_and_pchan_from_sel_buf(bases, bases_len, hit_id, &pchan);
        if (pchan != nullptr) {
          if (findunsel) {
            sel = (pchan->bone->flag & BONE_SEL);
          }
          else {
            sel = !(pchan->bone->flag & BONE_SELECTED);
          }

          data = pchan;
        }
        else {
          data = nullptr;
          sel = false;
        }
      }
      else {
        base = ed_armature_base_and_ebone_from_sel_buf(bases, bases_len, hit_id, &ebone);
        if (findunsel) {
          sel = (ebone->flag & BONE_SEL);
        }
        else {
          sel = !(ebone->flag & BONE_SEL);
        }

        data = ebone;
      }

      if (data) {
        if (sel) {
          if (do_nearest) {
            if (minsel > hit_result.depth) {
              firstSel = data;
              firstSel_base = base;
              minsel = hit_result.depth;
            }
          }
          else {
            if (!firstSel) {
              firstSel = data;
              firstSel_base = base;
            }
            takeNext = true;
          }
        }
        else {
          if (do_nearest) {
            if (minunsel > hit_result.depth) {
              firstunSel = data;
              firstunSel_base = base;
              minunsel = hit_result.depth;
            }
          }
          else {
            if (!firstunSel) {
              firstunSel = data;
              firstunSel_base = base;
            }
            if (takeNext) {
              *r_base = base;
              return data;
            }
          }
        }
      }
    }
  }

  if (firstunSel) {
    *r_base = firstunSel_base;
    return firstunSel;
  }
  *r_base = firstSel_base;
  return firstSel;
}

EditBone *ed_armature_pick_ebone_from_selbuf(Base **bases,
                                             uint bases_len,
                                             const GPUSelResult *hit_results,
                                             const int hits,
                                             bool findunsel,
                                             bool do_nearest,
                                             Base **r_base)
{
  const bool is_editmode = true;
  return static_cast<EditBone *>(ed_armature_pick_bone_from_selbuf_impl(
      is_editmode, bases, bases_len, {hit_results, hits}, findunsel, do_nearest, r_base));
}

PoseChannel *ed_armature_pick_pchan_from_selbuf(Base **bases,
                                                uint bases_len,
                                                const GPUSelResult *hit_results,
                                                const int hits,
                                                bool findunsel,
                                                bool do_nearest,
                                                Base **r_base)
{
  const bool is_editmode = false;
  return static_cast<PoseChannel *>(ed_armature_pick_bone_from_selbuf_impl(
      is_editmode, bases, bases_len, {hit_results, hits}, findunsel, do_nearest, r_base));
}

Bone *ed_armature_pick_bone_from_selbuf(Base **bases,
                                              uint bases_len,
                                              const GPUSelResult *hit_results,
                                              const int hits,
                                              bool findunsel,
                                              bool do_nearest,
                                              Base **r_base)
{
  PoseChannel *pchan = ed_armature_pick_pchan_from_selbuf(
      bases, bases_len, hit_results, hits, findunsel, do_nearest, r_base);
  return pchan ? pchan->bone : nullptr;
}

/* Cursor Pick API
 * Internal ed_armature_pick_bone_impl is exposed as:
 * - ed_armature_pick_ebone
 * - ed_armature_pick_pchan
 * - ed_armature_pick_bonr */

/* param xy: Cursor coords (area space).
 * return An EditBone when is_editmode, otherwise a PoseChannel.
 * Only checks obs in the current mode (edit-mode or pose-mode) */
static void *ed_armature_pick_bone_impl(
    const bool is_editmode, Cxt *C, const int xy[2], bool findunsel, Base **r_base)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  rcti rect;
  GPUSelBuf buf;
  int hits;

  ViewCxt vc = ed_view3d_viewcxt_init(C, graph);
  lib_assert((vc.obedit != nullptr) == is_editmode);

  lib_rcti_init_pt_radius(&rect, xy, 0);

  /* Don't use hits with this id, (armature drwing uses this). */
  const int select_id_ignore = -1;

  hits = view3d_opengl_sel_w_id_filter(
      &vc, &buf, &rect, VIEW3D_SEL_PICK_NEAREST, VIEW3D_SEL_FILTER_NOP, sel_id_ignore);

  *r_base = nullptr;

  if (hits > 0) {
    uint bases_len = 0;
    Base **bases;

    if (vc.obedit != nullptr) {
      bases = dune_view_layer_array_from_bases_in_edit_mode(
          vc.scene, vc.view_layer, vc.v3d, &bases_len);
    }
    else {
      bases = dune_ob_pose_base_array_get(vc.scene, vc.view_layer, vc.v3d, &bases_len);
    }

    void *bone = ed_armature_pick_bone_from_selbuf_impl(
        is_editmode,
        bases,
        bases_len,
        buf.storage.as_span().take_front(hits),
        findunsel,
        true,
        r_base);

    mem_free(bases);

    return bone;
  }
  return nullptr;
}

EditBone *ed_armature_pick_ebone(Cxt *C, const int xy[2], bool findunsel, Base **r_base)
{
  const bool is_editmode = true;
  return static_cast<EditBone *>(
      ed_armature_pick_bone_impl(is_editmode, C, xy, findunsel, r_base));
}

PoseChannel *ed_armature_pick_pchan(Cxt *C, const int xy[2], bool findunsel, Base **r_base)
{
  const bool is_editmode = false;
  return static_cast<PoseChannel *>(
      ed_armature_pick_bone_impl(is_editmode, C, xy, findunsel, r_base));
}

Bone *ed_armature_pick_bone(Cxt *C, const int xy[2], bool findunsel, Base **r_base)
{
  PoseChannel *pchan = ed_armature_pick_pchan(C, xy, findunsel, r_base);
  return pchan ? pchan->bone : nullptr;
}

/* Sel Linked Implementation
 * Shared logic for sel linked all/pick.
 * Use BONE_DONE flag to sel linked. */

/* param all_forks: Ctrl how chains are stepped over.
 * true: sep all connected bones traveling up & down forks.
 * false: sel all parents and all children, but not the children of the root bone */
static bool armature_sel_linked_impl(Ob *ob, const bool sel, const bool all_forks)
{
  bool changed = false;
  Armature *arm = static_cast<Armature *>(ob->data);

  /* Implementation note, this flood-fills sel bones with the 'TOUCH' flag,
   * even tho this is a loop-within a loop, walking up the parent chain only touches new bones.
   * Bones that have been touched are skipped, so the complexity is OK. */

  enum {
    /* Bone has been walked over, its LINK val can be read. */
    TOUCH = (1 << 0),
    /* When TOUCH has been set, this flag can be checked to see if the bone is connected. */
    LINK = (1 << 1),
  };

#define CHECK_PARENT(ebone) \
\
  (((ebone)->flag & BONE_CONNECTED) && \
   ((ebone)->parent ? EBONE_SELECTABLE(arm, (ebone)->parent) : false))

  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    ebone->tmp.i = 0;
  }

  /* Sel parents. */
  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    if (ebone_iter->temp.i & TOUCH) {
      continue;
    }
    if ((ebone_iter->flag & BONE_DONE) == 0) {
      continue;
    }

    ebone_iter->tmp.i |= TOUCH | LINK;

    /* We have an un-touched link. */
    for (EditBone *ebone = ebone_iter; ebone;
         ebone = CHECK_PARENT(ebone) ? ebone->parent : nullptr) {
      ed_armature_ebone_sel_set(ebone, sel);
      changed = true;

      if (all_forks) {
        ebone->tmp.i |= (TOUCH | LINK);
      }
      else {
        ebone->tmp.i |= TOUCH;
      }
      /* Don't walk onto links (messes up 'all_forks' logic). */
      if (ebone->parent && ebone->parent->tmp.i & LINK) {
        break;
      }
    }
  }

  /* Sel children. */
  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    /* No need to 'touch' this bone as it won't be walked over when scanning up the chain. */
    if (!CHECK_PARENT(ebone_iter)) {
      continue;
    }
    if (ebone_iter->tmp.i & TOUCH) {
      continue;
    }

    /* Fl1st check if we're marked. */
    EditBone *ebone_touched_parent = nullptr;
    for (EditBone *ebone = ebone_iter; ebone;
         ebone = CHECK_PARENT(ebone) ? ebone->parent : nullptr) {
      if (ebone->tmp.i & TOUCH) {
        ebone_touched_parent = ebone;
        break;
      }
      ebone->tmp.i |= TOUCH;
    }

    if ((ebone_touched_parent != nullptr) && (ebone_touched_parent->tmp.i & LINK)) {
      for (EditBone *ebone = ebone_iter; ebone != ebone_touched_parent; ebone = ebone->parent) {
        if ((ebone->tmp.i & LINK) == 0) {
          ebone->tmp.i |= LINK;
          ed_armature_ebone_sel_set(ebone, sel);
          changed = true;
        }
      }
    }
  }

#undef CHECK_PARENT

  if (changed) {
    ed_armature_edit_sync_sel(arm->edbo);
    graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    win_main_add_notifier(NC_PEN | ND_DATA | NA_EDITED, ob);
  }

  return changed;
}

/* Sel Linked Op */
static int armature_sel_linked_ex(Cxt *C, WinOp *op)
{
  const bool all_forks = api_bool_get(op->ptr, "all_forks");

  bool changed_multi = false;
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);

    bool found = false;
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_VISIBLE(arm, ebone) &&
          (ebone->flag & (BONE_SEL | BONE_ROOTSEL | BONE_TIPSEL))) {
        ebone->flag |= BONE_DONE;
        found = true;
      }
      else {
        ebone->flag &= ~BONE_DONE;
      }
    }

    if (found) {
      if (armature_sel_linked_impl(ob, true, all_forks)) {
        changed_multi = true;
      }
    }
  }
  mem_free(obs);

  if (changed_multi) {
    ed_outliner_sel_sync_from_edit_bone_tag(C);
  }
  return OP_FINISHED;
}

void ARMATURE_OT_sel_linked(WinOpType *ot)
{
  /* ids */
  ot->name = "Select Linked All";
  ot->idname = "ARMATURE_OT_sel_linked";
  ot->description = "Se all bones linked by parent/child connections to the current sel";

  /* api cbs */
  ot->ex = armature_sel_linked_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Leave disabled by default as this matches pose mode. */
  api_def_bool(ot->sapi, "all_forks", false, "All Forks", "Follow forks in the parents chain");
}

/* Sel Linked (Cursor Pick) Op */
static int armature_sel_linked_pick_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  const bool sel = !api_bool_get(op->ptr, "desel");
  const bool all_forks = api_bool_get(op->ptr, "all_forks");

  view3d_op_needs_opengl(C);
  dune_ob_update_sel_id(cxt_data_main(C));

  Base *base = nullptr;
  EditBone *ebone_active = ed_armature_pick_ebone(C, ev->mval, true, &base);

  if (ebone_active == nullptr) {
    return OP_CANCELLED;
  }

  Armature *arm = static_cast<Armature *>(base->ob->data);
  if (!EBONE_SELECTABLE(arm, ebone_active)) {
    return OP_CANCELLED;
  }

  /* Init flags. */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    ebone->flag &= ~BONE_DONE;
  }
  ebone_active->flag |= BONE_DONE;

  if (armature_sel_linked_impl(base->ob, sel, all_forks)) {
    ed_outliner_sel_sync_from_edit_bone_tag(C);
  }

  return OP_FINISHED;
}

static bool armature_sel_linked_pick_poll(Cxt *C)
{
  return (ed_op_view3d_active(C) && ed_op_editarmature(C));
}

void ARMATURE_OT_sel_linked_pick(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Linked";
  ot->idname = "ARMATURE_OT_sel_linked_pick";
  ot->description = "(De)sel bones linked by parent/child connections under the mouse cursor";

  /* api cbs */
  /* leave 'ex' unset */
  ot->invoke = armature_sel_linked_pick_invoke;
  ot->poll = armature_sel_linked_pick_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi, "desel", false, "Desel", "");
  /* Leave disabled by default as this matches pose mode. */
  api_def_bool(ot->sapi, "all_forks", false, "All Forks", "Follow forks in the parents chain");
}
l

/* Sel Buf Queries EditMode*/
/* util fn for get_nearest_editbonepoint */
static int selbuf_ret_hits_12(dune::MutableSpan<GPUSelResult> /*hit_results*/,
                                    const int hits12)
{
  return hits12;
}

static int selbuf_ret_hits_5(dune::MutableSpan<GPUSelResult> hit_results,
                             const int hits12,
                             const int hits5)
{
  const int ofs = hits12;
  /* Shift results to beginning. */
  hit_results.slice(0, hits5).copy_from(hit_results.slice(ofs, hits5));
  return hits5;
}

/* does bones and points */
/* note that BONE ROOT only gets drwn for root bones (or without IK) */
static EditBone *get_nearest_editbonepoint(
    ViewCxt *vc, bool findunsel, bool use_cycle, Base **r_base, int *r_selmask)
{
  GPUSelBuf buf;
  struct Result {
    uint hitresult;
    Base *base;
    EditBone *ebone;
  };
  Result *result = nullptr;
  Result result_cycle{};
  result_cycle.hitresult = -1;
  result_cycle.base = nullptr;
  result_cycle.ebone = nullptr;
  Result result_bias{};
  result_bias.hitresult = -1;
  result_bias.base = nullptr;
  result_bias.ebone = nullptr;

  /* find the bone after the current active bone, so as to bump up its chances in selection.
   * this way overlapping bones will cycle sel state as with obs. */
  Ob *obedit_orig = vc->obedit;
  EditBone *ebone_active_orig = ((Armature *)obedit_orig->data)->act_edbone;
  if (ebone_active_orig == nullptr) {
    use_cycle = false;
  }

  if (use_cycle) {
    use_cycle = !win_cursor_test_motion_and_update(vc->mval);
  }

  const bool do_nearest = !(XRAY_ACTIVE(vc->v3d) || use_cycle);

  /* matching logic from 'mixed_bones_ob_selbuf' */
  int hits = 0;
  /* Don't use hits with this Id, (armature drwing uses this). */
  const int select_id_ignore = -1;

  /* we _must_ end cache before return, use 'goto cache_end' */
  view3d_opengl_sel_cache_begin();

  {
    const int sel_mode = (do_nearest ? VIEW3D_SEL_PICK_NEAREST : VIEW3D_SEL_PICK_ALL);
    const eV3DSelObFilter sel_filter = VIEW3D_SEL_FILTER_NOP;

    GPUSelStorage &storage = buf.storage;
    rcti rect;
    lib_rcti_init_pt_radius(&rect, vc->mval, 12);
    const int hits12 = view3d_opengl_sel_with_id_filter(
        vc, &buffer, &rect, eV3DSelMode(sel_mode), sel_filter, sel_id_ignore);

    if (hits12 == 1) {
      hits = selbuf_ret_hits_12(storage.as_mutable_span(), hits12);
      goto cache_end;
    }
    else if (hits12 > 0) {
      lob_rcti_init_pt_radius(&rect, vc->mval, 5);
      const int hits5 = view3d_opengl_sel_with_id_filter(
          vc, &buf, &rect, eV3DSelMode(sel_mode), sel_filter, sel_id_ignore);

      if (hits5 == 1) {
        hits = selbuf_ret_hits_5(storage.as_mutable_span(), hits12, hits5);
        goto cache_end;
      }

      if (hits5 > 0) {
        hits = selbuf_ret_hits_5(storage.as_mutable_span(), hits12, hits5);
        goto cache_end;
      }
      else {
        hits = selbuf_ret_hits_12(storage.as_mutable_span(), hits12);
        goto cache_end;
      }
    }
  }

cache_end:
  view3d_opengl_sel_cache_end();

  uint bases_len;
  Base **bases = dune_view_layer_array_from_bases_in_edit_mode_unique_data(
      vc->scene, vc->view_layer, vc->v3d, &bases_len);

  /* See if there are any sel bones in this group */
  if (hits > 0) {
    if (hits == 1) {
      result_bias.hitresult = buf.storage[0].id;
      result_bias.base = ed_armature_base_and_ebone_from_sel_buf(
          bases, bases_len, result_bias.hitresult, &result_bias.ebone);
    }
    else {
      int bias_max = INT_MIN;

      /* Track Cycle Vars
       * - Offset is always set to the active bone.
       * - The ob & bone indices subtracted by the 'offset.as_u32' vals
       *   Unsigned subtraction wrapping: always sel the next bone in the cycle. */
      struct {
        union {
          uint32_t as_u32;
          struct {
#ifdef __BIG_ENDIAN__
            uint16_t ob;
            uint16_t bone;
#else
            uint16_t bone;
            uint16_t ob;
#endif
          };
        } offset, test, best;
      } cycle_order;

      if (use_cycle) {
        Armature *arm = static_cast<Armature *>(obedit_orig->data);
        int ob_index = obedit_orig->runtime->sel_id & 0xFFFF;
        int bone_index = lib_findindex(arm->edbo, ebone_active_orig);
        /* Offset from the current active bone, so we cycle onto the next. */
        cycle_order.offset.ob = ob_index;
        cycle_order.offset.bone = bone_index;
        /* Val of active bone w offset subtracted signaling always overwrite. */
        cycle_order.best.as_u32 = 0;
      }

      for (int i = 0; i < hits; i++) {
        const uint hitresult = buf.storage[i].id;

        Base *base = nullptr;
        EditBone *ebone;
        base = ed_armature_base_and_ebone_from_sel_buf(bases, bases_len, hitresult, &ebone);
        /* If this fails, sel code is setting the sel Id's incorrectly. */
        lib_assert(base && ebone);

        /* Prioritized sel */
        {
          int bias;
          /* clicks on bone points get advantage */
          if (hitresult & (BONESEL_ROOT | BONESEL_TIP)) {
            /* but also the unsel one */
            if (findunsel) {
              if ((hitresult & BONESEL_ROOT) && (ebone->flag & BONE_ROOTSEL) == 0) {
                bias = 4;
              }
              else if ((hitresult & BONESEL_TIP) && (ebone->flag & BONE_TIPSEL) == 0) {
                bias = 4;
              }
              else {
                bias = 3;
              }
            }
            else {
              bias = 4;
            }
          }
          else {
            /* bone found */
            if (findunsel) {
              if ((ebone->flag & BONE_SELECTED) == 0) {
                bias = 2;
              }
              else {
                bias = 1;
              }
            }
            else {
              bias = 2;
            }
          }

          if (bias > bias_max) {
            bias_max = bias;

            result_bias.hitresult = hitresult;
            result_bias.base = base;
            result_bias.ebone = ebone;
          }
        }

        /* Cycle sel items (obs & bones). */
        if (use_cycle) {
          cycle_order.test.ob = hitresult & 0xFFFF;
          cycle_order.test.bone = (hitresult & ~BONESEL_ANY) >> 16;
          if (ebone == ebone_active_orig) {
            lib_assert(cycle_order.test.ob == cycle_order.offset.ob);
            lob_assert(cycle_order.test.bone == cycle_order.offset.bone);
          }
          /* Subtraction as a single val is needed to support cycling through bones
           * from multiple obs. Once the last bone is sel,
           * the bits for the bone index wrap into the ob,
           * causing the next ob to be stepped onto. */
          cycle_order.test.as_u32 -= cycle_order.offset.as_u32;

          /* Tho logic avoids stepping onto the active bone,
           * always set the 'best' val for the first time.
           * Else ensure val is the smallest it can be,
           * relative to the active bone, as long as it's not the active bone. */
          if ((cycle_order.best.as_u32 == 0) ||
              (cycle_order.test.as_u32 && (cycle_order.test.as_u32 < cycle_order.best.as_u32)))
          {
            cycle_order.best = cycle_order.test;
            result_cycle.hitresult = hitresult;
            result_cycle.base = base;
            result_cycle.ebone = ebone;
          }
        }
      }
    }

    result = (use_cycle && result_cycle.ebone) ? &result_cycle : &result_bias;

    if (result->hitresult != -1) {
      *r_base = result->base;

      *r_selmask = 0;
      if (result->hitresult & BONESEL_ROOT) {
        *r_selmask |= BONE_ROOTSEL;
      }
      if (result->hitresult & BONESEL_TIP) {
        *r_selmask |= BONE_TIPSEL;
      }
      if (result->hitresult & BONESEL_BONE) {
        *r_selmask |= BONE_SEL;
      }
      mem_free(bases);
      return result->ebone;
    }
  }
  *r_selmask = 0;
  *r_base = nullptr;
  mem_free(bases);
  return nullptr;
}

/* Sel Util Fns */

bool ed_armature_edit_desel_all(Ob *obedit)
{
  Armature *arm = static_cast<Armature *>(obedit->data);
  bool changed = false;
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (ebone->flag & (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL)) {
      ebone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
      changed = true;
    }
  }
  return changed;
}

bool ed_armature_edit_desel_all_visible(Ob *obedit)
{
  Armature *arm = static_cast<Armature *>(obedit->data);
  bool changed = false;
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    /* 1st, bone must be vis and sel */
    if (EBONE_VISIBLE(arm, ebone)) {
      if (ebone->flag & (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL)) {
        ebone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        changed = true;
      }
    }
  }

  if (changed) {
    ed_armature_edit_sync_sel(arm->edbo);
  }
  return changed;
}

bool ed_armature_edit_desel_all_multi_ex(Base **bases, uint bases_len)
{
  bool changed_multi = false;
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *obedit = bases[base_index]->ob;
    changed_multi |= ed_armature_edit_desel_all(obedit);
  }
  return changed_multi;
}

bool ed_armature_edit_desel_all_visible_multi_ex(Base **bases, uint bases_len)
{
  bool changed_multi = false;
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *obedit = bases[base_index]->ob;
    changed_multi |= ed_armature_edit_desel_all_visible(obedit);
  }
  return changed_multi;
}

bool ed_armature_edit_desel_all_visible_multi(Cxt *C)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewCxt vc = ed_view3d_viewcxt_init(C, graph);
  uint bases_len = 0;
  Base **bases = dune_view_layer_array_from_bases_in_edit_mode_unique_data(
      vc.scene, vc.view_layer, vc.v3d, &bases_len);
  bool changed_multi = ed_armature_edit_desel_all_multi_ex(bases, bases_len);
  mem_free(bases);
  return changed_multi;
}

/* Sel Cursor Pick API */
bool ed_armature_edit_sel_pick_bone(
    Cxt *C, Base *basact, EditBone *ebone, const int selmask, const SelectPickParams *params)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);
  bool changed = false;
  bool found = false;

  if (ebone) {
    Armature *arm = static_cast<Armature *>(basact->ob->data);
    if (EBONE_SELECTABLE(arm, ebone)) {
      found = true;
    }
  }

  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->sel_passthrough) &&
        (ed_armature_ebone_selflag_get(ebone) & selmask)) {
      found = false;
    }
    else if (found || params->desel_all) {
      /* Desel everything. */
      uint bases_len = 0;
      Base **bases = dune_view_layer_arr_from_bases_in_edit_mode_unique_data(
          scene, view_layer, v3d, &bases_len);
      ed_armature_edit_desel_all_multi_ex(bases, bases_len);
      mem_free(bases);
      changed = true;
    }
  }

  if (found) {
    lib_assert(dune_ob_is_in_editmode(basact->ob));
    Armature *arm = static_cast<Armature *>(basact->ob->data);

    /* By definition the non-root connected bones have no root point drawn,
     * so a root sel needs to be delivered to the parent tip. */

    if (selmask & BONE_SEL) {
      if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {

        /* Bone is in a chain. */
        switch (params->sel_op) {
          case SEL_OP_ADD: {
            /* Select this bone. */
            ebone->flag |= BONE_TIPSEL;
            ebone->parent->flag |= BONE_TIPSEL;
            break;
          }
          case SEL_OP_SUB: {
            /* Desel this bone. */
            ebone->flag &= ~(BONE_TIPSEL | BONE_SEL);
            /* Only desel parent tip if it is not sel. */
            if (!(ebone->parent->flag & BONE_SEL)) {
              ebone->parent->flag &= ~BONE_TIPSEL;
            }
            break;
          }
          case SEL_OP_XOR: {
            /* Toggle inverts this bone's sel. */
            if (ebone->flag & BONE_SEL) {
              /* Desel this bone. */
              ebone->flag &= ~(BONE_TIPSEL | BONE_SEL);
              /* Only desel parent tip if it is not sel. */
              if (!(ebone->parent->flag & BONE_SEL)) {
                ebone->parent->flag &= ~BONE_TIPSEL;
              }
            }
            else {
              /* Sel this bone. */
              ebone->flag |= BONE_TIPSEL;
              ebone->parent->flag |= BONE_TIPSEL;
            }
            break;
          }
          case SEL_OP_SET: {
            /* Sel this bone. */
            ebone->flag |= BONE_TIPSEL;
            ebone->parent->flag |= BONE_TIPSEL;
            break;
          }
          case SEL_OP_AND: {
            lib_assert_unreachable(); /* Doesn't make sense for picking. */
            break;
          }
        }
      }
      else {
        switch (params->sel_op) {
          case SEL_OP_ADD: {
            ebone->flag |= (BONE_TIPSEL | BONE_ROOTSEL);
            break;
          }
          case SEL_OP_SUB: {
            ebone->flag &= ~(BONE_TIPSEL | BONE_ROOTSEL);
            break;
          }
          case SEL_OP_XOR: {
            /* Toggle inverts this bone's sel. */
            if (ebone->flag & BONE_SEL) {
              ebone->flag &= ~(BONE_TIPSEL | BONE_ROOTSEL);
            }
            else {
              ebone->flag |= (BONE_TIPSEL | BONE_ROOTSEL);
            }
            break;
          }
          case SEL_OP_SET: {
            ebone->flag |= (BONE_TIPSEL | BONE_ROOTSEL);
            break;
          }
          case SEL_OP_AND: {
            lib_assert_unreachable(); /* Doesn't make sense for picking. */
            break;
          }
        }
      }
    }
    else {
      switch (params->sel_op) {
        case SEL_OP_ADD: {
          ebone->flag |= selmask;
          break;
        }
        case SEL_OP_SUB: {
          ebone->flag &= ~selmask;
          break;
        }
        case SEL_OP_XOR: {
          if (ebone->flag & selmask) {
            ebone->flag &= ~selmask;
          }
          else {
            ebone->flag |= selmask;
          }
          break;
        }
        case SEL_OP_SET: {
          ebone->flag |= selmask;
          break;
        }
        case SEL_OP_AND: {
          lib_assert_unreachable(); /* Doesn't make sense for picking. */
          break;
        }
      }
    }

    ed_armature_edit_sync_sel(arm->edbo);

    /* Then now check for active status. */
    if (ed_armature_ebone_selflag_get(ebone)) {
      arm->act_edbone = ebone;
    }

    dune_view_layer_synced_ensure(scene, view_layer);
    if (dune_view_layer_active_base_get(view_layer) != basact) {
      ed_ob_base_activate(C, basact);
    }

    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, basact->ob);
    graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    changed = true;
  }

  if (changed) {
    ed_outliner_sel_sync_from_edit_bone_tag(C);
  }

  return changed || found;
}

bool ed_armature_edit_sel_pick(Cxt *C, const int mval[2], const SelPickParams *params)

{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  EditBone *nearBone = nullptr;
  int selmask;
  Base *basact = nullptr;

  ViewCxt vc = ed_view3d_viewcxt_init(C, graph);
  vc.mval[0] = mval[0];
  vc.mval[1] = mval[1];

  nearBone = get_nearest_editbonepoint(&vc, true, true, &basact, &selmask);
  return ed_armature_edit_sel_pick_bone(C, basact, nearBone, selmask, params);
}

/* Sel Op From Tagged
 * Implements ed_armature_edit_sel_op_from_tagged */
static bool armature_edit_sel_op_apply(Armature *arm,
                                       EditBone *ebone,
                                       const eSelOp sel_op,
                                       int is_ignore_flag,
                                       int is_inside_flag)
{
  lib_assert(!(is_ignore_flag & ~(BONESEL_ROOT | BONESEL_TIP)));
  lib_assert(!(is_inside_flag & ~(BONESEL_ROOT | BONESEL_TIP | BONESEL_BONE)));
  lib_assert(EBONE_VISIBLE(arm, ebone));
  bool changed = false;
  bool is_point_done = false;
  int points_proj_tot = 0;
  lib_assert(ebone->flag == ebone->tmp.i);
  const int ebone_flag_prev = ebone->flag;

  if ((is_ignore_flag & BONE_ROOTSEL) == 0) {
    points_proj_tot++;
    const bool is_sel = ebone->flag & BONE_ROOTSEL;
    const bool is_inside = is_inside_flag & BONESEL_ROOT;
    const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_select, is_inside);
    if (sel_op_result != -1) {
      if (sel_op_result == 0 || EBONE_SELECTABLE(arm, ebone)) {
        SET_FLAG_FROM_TEST(ebone->flag, sel_op_result, BONE_ROOTSEL);
      }
    }
    is_point_done |= is_inside;
  }

  if ((is_ignore_flag & BONE_TIPSEL) == 0) {
    points_proj_tot++;
    const bool is_select = ebone->flag & BONE_TIPSEL;
    const bool is_inside = is_inside_flag & BONESEL_TIP;
    const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
    if (sel_op_result != -1) {
      if (sel_op_result == 0 || EBONE_SELECTABLE(arm, ebone)) {
        SET_FLAG_FROM_TEST(ebone->flag, sel_op_result, BONE_TIPSEL);
      }
    }
    is_point_done |= is_inside;
  }

  /* if one of points sel, we skip the bone itself */
  if ((is_point_done == false) && (points_proj_tot == 2)) {
    const bool is_sel = ebone->flag & BONE_SEL;
    {
      const bool is_inside = is_inside_flag & BONESEL_BONE;
      const int sel_op_result = es_sel_op_action_deselected(sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        if (sel_op_result == 0 || EBONE_SELECTABLE(arm, ebone)) {
          SET_FLAG_FROM_TEST(
              ebone->flag, sel_op_result, BONE_SEL | BONE_ROOTSEL | BONE_TIPSEL);
        }
      }
    }

    changed = true;
  }
  changed |= is_point_done;

  if (ebone_flag_prev != ebone->flag) {
    ebone->tmp.i = ebone->flag;
    ebone->flag = ebone_flag_prev;
    ebone->flag = ebone_flag_prev | BONE_DONE;
    changed = true;
  }

  return changed;
}

bool ed_armature_edit_sel_op_from_tagged(Armature *arm, const int sel_op)
{
  bool changed = false;

  /* Init flags. */
  {
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {

      /* Flush the parent flag to this bone
       * so we don't need to check the parent when adjusting the sel. */
      if ((ebone->flag & BONE_CONNECTED) && ebone->parent) {
        if (ebone->parent->flag & BONE_TIPSEL) {
          ebone->flag |= BONE_ROOTSEL;
        }
        else {
          ebone->flag &= ~BONE_ROOTSEL;
        }

        /* Flush the 'tmp.i' flag. */
        if (ebone->parent->tmp.i & BONESEL_TIP) {
          ebone->temp.i |= BONESEL_ROOT;
        }
      }
      ebone->flag &= ~BONE_DONE;
    }
  }

  /* Apply sel from bone sel flags. */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (ebone->tmp.i != 0) {
      int is_ignore_flag = ((ebone->tmp.i << 16) & (BONESEL_ROOT | BONESEL_TIP));
      int is_inside_flag = (ebone->tmp.i & (BONESEL_ROOT | BONESEL_TIP | BONESEL_BONE));

      /* Use as prev bone flag from now on. */
      ebone->tmp.i = ebone->flag;

      /* When there is a partial sel wo both endpoints, only sel an endpoint. */
      if ((is_inside_flag & BONESEL_BONE) &&
          ELEM(is_inside_flag & (BONESEL_ROOT | BONESEL_TIP), BONESEL_ROOT, BONESEL_TIP))
      {
        is_inside_flag &= ~BONESEL_BONE;
      }

      changed |= armature_edit_sel_op_apply(
          arm, ebone, eSelOp(sel_op), is_ignore_flag, is_inside_flag);
    }
  }

  if (changed) {
    /* Cleanup flags. */
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (ebone->flag & BONE_DONE) {
        SWAP(int, ebone->tmp.i, ebone->flag);
        ebone->flag |= BONE_DONE;
        if ((ebone->flag & BONE_CONNECTED) && ebone->parent) {
          if ((ebone->parent->flag & BONE_DONE) == 0) {
            /* Checked below. */
            ebone->parent->temp.i = ebone->parent->flag;
          }
        }
      }
    }

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (ebone->flag & BONE_DONE) {
        if ((ebone->flag & BONE_CONNECTED) && ebone->parent) {
          bool is_parent_tip_changed = (ebone->parent->flag & BONE_TIPSEL) !=
                                       (ebone->parent->tmp.i & BONE_TIPSEL);
          if ((ebone->tmp.i & BONE_ROOTSEL) == 0) {
            if ((ebone->flag & BONE_ROOTSEL) != 0) {
              ebone->parent->flag |= BONE_TIPSEL;
            }
          }
          else {
            if ((ebone->flag & BONE_ROOTSEL) == 0) {
              ebone->parent->flag &= ~BONE_TIPSEL;
            }
          }

          if (is_parent_tip_changed == false) {
            /* Keep tip sel if the parent remains sel. */
            if (ebone->parent->flag & BONE_SEL) {
              ebone->parent->flag |= BONE_TIPSEL;
            }
          }
        }
        ebone->flag &= ~BONE_DONE;
      }
    }

    ed_armature_edit_sync_sel(arm->edbo);
    ed_armature_edit_validate_active(arm);
  }

  return changed;
}

/* (De)Sel All Op */
static int armature_de_sel_all_ex(Cxt *C, WinOp *op)
{
  int action = api_enum_get(op->ptr, "action");

  if (action == SEL_TOGGLE) {
    /* Determine if there are any sel bones
     * Therefore whether we are sel or desel */
    action = SEL_SEL;
    CXT_DATA_BEGIN (C, EditBone *, ebone, visible_bones) {
      if (ebone->flag & (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL)) {
        action = SEL_DESEL;
        break;
      }
    }
    CXT_DATA_END;
  }

  /* Set the flags. */
  CXT_DATA_BEGIN (C, EditBone *, ebone, visible_bones) {
    /* ignore bone if sel can't change */
    switch (action) {
      case SEL_SEL:
        if ((ebone->flag & BONE_UNSEL) == 0) {
          ebone->flag |= (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
          if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
            ebone->parent->flag |= BONE_TIPSEL;
          }
        }
        break;
      case SEL_DESEL:
        ebone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        break;
      case SEL_INVERT:
        if (ebone->flag & BONE_SEL) {
          ebone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        }
        else {
          if ((ebone->flag & BONE_UNSELECTABLE) == 0) {
            ebone->flag |= (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
            if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
              ebone->parent->flag |= BONE_TIPSEL;
            }
          }
        }
        break;
    }
  }
  CXT_DATA_END;

  ed_outliner_sel_sync_from_edit_bone_tag(C);

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, nullptr);

  /* Tagging only one ob to refresh drwing. */
  Ob *obedit = cxt_data_edit_ob(C);
  graph_id_tag_update(&obedit->id, ID_RECALC_SEL);

  return OP_FINISHED;
}

void ARMATURE_OT_sel_all(WinOpType *ot)
{
  /* ids */
  ot->name = "(De)sel All";
  ot->idname = "ARMATURE_OT_sel_all";
  ot->description = "Toggle sel status of all bones";

  /* api cbs */
  ot->ex = armature_desel_all_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  win_op_props_sel_all(ot);
}

/* More/Less Implementation */
static void armature_sel_more(Armature *arm, EditBone *ebone)
{
  if ((EBONE_PREV_FLAG_GET(ebone) & (BONE_ROOTSEL | BONE_TIPSEL)) != 0) {
    if (EBONE_SELECTABLE(arm, ebone)) {
      ed_armature_ebone_sel_set(ebone, true);
    }
  }

  if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
    /* to parent */
    if ((EBONE_PREV_FLAG_GET(ebone) & BONE_ROOTSEL) != 0) {
      if (EBONE_SELECTABLE(arm, ebone->parent)) {
        ED_armature_ebone_selflag_enable(ebone->parent,
                                        (BONE_SEL | BONE_ROOTSEL | BONE_TIPSEL));
      }
    }

    /* from parent (diff from sel less) */
    if ((EBONE_PREV_FLAG_GET(ebone->parent) & BONE_TIPSEL) != 0) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        ed_armature_ebone_selflag_enable(ebone, (BONE_SEL | BONE_ROOTSEL));
      }
    }
  }
}

static void armature_sel_less(Armature * /*arm*/, EditBone *ebone)
{
  if ((EBONE_PREV_FLAG_GET(ebone) & (BONE_ROOTSEL | BONE_TIPSEL)) != (BONE_ROOTSEL | BONE_TIPSEL))
  {
    ed_armature_ebone_sel_set(ebone, false);
  }

  if (ebone->parent && (ebone->flag & BONE_CONNECTED)) {
    /* to parent */
    if ((EBONE_PREV_FLAG_GET(ebone) & BONE_SEL) == 0) {
      ed_armature_ebone_selflag_disable(ebone->parent, (BONE_SEL | BONE_TIPSEL));
    }

    /* from parent (diff from sel more) */
    if ((EBONE_PREV_FLAG_GET(ebone->parent) & BONE_SEL) == 0) {
      ed_armature_ebone_selflag_disable(ebone, (BONE_SEL | BONE_ROOTSEL));
    }
  }
}

static void armature_sel_more_less(Ob *ob, bool more)
{
  Armature *arm = (Armature *)ob->data;

  /* eventually we shouldn't need this. */
  ed_armature_edit_sync_sel(arm->edbo);

  /* count bones & store sel state */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    EBONE_PREV_FLAG_SET(ebone,ed_armature_ebone_selflag_get(ebone));
  }

  /* do sel */
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (EBONE_VISIBLE(arm, ebone)) {
      if (more) {
        armature_sel_more(arm, ebone);
      }
      else {
        armature_sel_less(arm, ebone);
      }
    }
  }

  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (EBONE_VISIBLE(arm, ebone)) {
      if (more == false) {
        if (ebone->flag & BONE_SEL) {
          ed_armature_ebone_sel_set(ebone, true);
        }
      }
    }
    ebone->tmp.p = nullptr;
  }

 ed_armature_edit_sync_sel(arm->edbo);
}

/* Sel More Op */

static int armature_de_sel_more_ex(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    armature_sel_more_less(ob, true);
    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
    graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  }
  mem_free(obs);

  ed_outliner_sel_sync_from_edit_bone_tag(C);
  return OP_FINISHED;
}

void ARMATURE_OT_sel_more(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel More";
  ot->idname = "ARMATURE_OT_sel_more";
  ot->description = "Sel those bones connected to the initial sel";

  /* api cbs */
  ot->ex = armature_de_sel_more_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Sel Less Op */
static int armature_de_sel_less_ex(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    armature_sel_more_less(ob, false);
    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
    graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  }
  mem_free(obs);
  
  _outliner_sel_sync_from_edit_bone_tag(C);
  return OP_FINISHED;
}

void ARMATURE_OT_sel_less(WinOprType *ot)
{
  /* ids */
  ot->name = "Sel Less";
  ot->idname = "ARMATURE_OT_sel_less";
  ot->description = "Des those bones at the boundary of each sel rgn";

  /* api cbs */
  ot->ex = armature_desel_less_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Sel Similar */
enum {
  SIMEDBONE_CHILDREN = 1,
  SIMEDBONE_CHILDREN_IMMEDIATE,
  SIMEDBONE_SIBLINGS,
  SIMEDBONE_LENGTH,
  SIMEDBONE_DIRECTION,
  SIMEDBONE_PREFIX,
  SIMEDBONE_SUFFIX,
  SIMEDBONE_COLLECTION,
  SIMEDBONE_COLOR,
  SIMEDBONE_SHAPE,
};

static const EnumPropItem prop_similar_types[] = {
    {SIMEDBONE_CHILDREN, "CHILDREN", 0, "Children", ""},
    {SIMEDBONE_CHILDREN_IMMEDIATE, "CHILDREN_IMMEDIATE", 0, "Immediate Children", ""},
    {SIMEDBONE_SIBLINGS, "SIBLINGS", 0, "Siblings", ""},
    {SIMEDBONE_LENGTH, "LENGTH", 0, "Length", ""},
    {SIMEDBONE_DIRECTION, "DIRECTION", 0, "Direction (Y Axis)", ""},
    {SIMEDBONE_PREFIX, "PREFIX", 0, "Prefix", ""},
    {SIMEDBONE_SUFFIX, "SUFFIX", 0, "Suffix", ""},
    {SIMEDBONE_COLLECTION, "BONE_COLLECTION", 0, "Bone Collection", ""},
    {SIMEDBONE_COLOR, "COLOR", 0, "Color", ""},
    {SIMEDBONE_SHAPE, "SHAPE", 0, "Shape", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static float bone_length_squared_worldspace_get(Ob *ob, EditBone *ebone)
{
  float v1[3], v2[3];
  mul_v3_mat3_m4v3(v1, ob->ob_to_world, ebone->head);
  mul_v3_mat3_m4v3(v2, ob->ob_to_world, ebone->tail);
  return len_squared_v3v3(v1, v2);
}

static void sel_similar_length(Cxt *C, const float thresh)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_act = cxt_data_edit_ob(C);
  EditBone *ebone_act = cxt_data_active_bone(C);

  /* Thresh is always relative to current length. */
  const float len = bone_length_squared_worldspace_get(ob_act, ebone_act);
  const float len_min = len / (1.0f + (thresh - FLT_EPSILON));
  const float len_max = len * (1.0f + (thresh + FLT_EPSILON));

  uint obs_len = 0;
  Ob **obs =  dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        const float len_iter = bone_length_squared_worldspace_get(ob, ebone);
        if ((len_iter > len_min) && (len_iter < len_max)) {
          ed_armature_ebone_sel_set(ebone, true);
          changed = true;
        }
      }
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}

static void bone_direction_worldspace_get(Ob *ob, EditBone *ebone, float *r_dir)
{
  float v1[3], v2[3];
  copy_v3_v3(v1, ebone->head);
  copy_v3_v3(v2, ebone->tail);

  mul_m4_v3(ob->ob_to_world, v1);
  mul_m4_v3(ob->ob_to_world, v2);

  sub_v3_v3v3(r_dir, v1, v2);
  normalize_v3(r_dir);
}

static void sel_similar_direction(Cxt *C, const float thresh)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_act = cxt_data_edit_ob(C);
  EditBone *ebone_act = cxt_data_active_bone(C);

  float dir_act[3];
  bone_direction_worldspace_get(ob_act, ebone_act, dir_act);

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        float dir[3];
        bone_direction_worldspace_get(ob, ebone, dir);

        if (angle_v3v3(dir_act, dir) / float(M_PI) < (thresh + FLT_EPSILON)) {
          ed_armature_ebone_sel_set(ebone, true);
          changed = true;
        }
      }
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}

static void sel_similar_bone_collection(Cxt *C)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  EditBone *ebone_act = cxt_data_active_bone(C);

  /* Build a set of bone collection names, to allow cross-Armature sel. */
  dune::Set<std::string> collection_names;
  LIST_FOREACH (BoneCollectionRef *, bcoll_ref, &ebone_act->bone_collections) {
    collection_names.add(bcoll_ref->bcoll->name);
  }

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (!EBONE_SELECTABLE(arm, ebone)) {
        continue;
      }

      LIST_FOREACH (BoneCollectionRef *, bcoll_ref, &ebone->bone_collections) {
        if (!collection_names.contains(bcoll_ref->bcoll->name)) {
          continue;
        }

        ed_armature_ebone_sel_set(ebone, true);
        changed = true;
        break;
      }
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}
static void sel_similar_bone_color(Cxt *C)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  EditBone *ebone_act = czt_data_active_bone(C);

  const dune::animrig::BoneColor &active_bone_color = ebone_act->color.wrap();

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < ons_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (!EBONE_SELECTABLE(arm, ebone)) {
        continue;
      }

      const dune::animrig::BoneColor &bone_color = ebone->color.wrap();
      if (bone_color != active_bone_color) {
        continue;
      }

      win_armature_ebone_sel_set(ebone, true);
      changed = true;
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}

static void sel_similar_prefix(Cxt *C)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  EditBone *ebone_act = cxt_data_active_bone(C);

  char body_tmp[MAXBONENAME];
  char prefix_act[MAXBONENAME];

  lib_string_split_prefix(ebone_act->name, sizeof(ebone_act->name), prefix_act, body_tmp);

  if (prefix_act[0] == '\0') {
    return;
  }

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data)
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    /* Find matches */
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        char prefix_other[MAXBONENAME];
        lib_string_split_prefix(ebone->name, sizeof(ebone->name), prefix_other, body_tmp);
        if (STREQ(prefix_act, prefix_other)) {
          ed_armature_ebone_sel_set(ebone, true);
          changed = true;
        }
      }
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}

static void sel_similar_suffix(Cxt *C)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  EditBone *ebone_act = cxt_data_active_bone(C);

  char body_tmp[MAXBONENAME];
  char suffix_act[MAXBONENAME];

  lib_string_split_suffix(ebone_act->name, sizeof(ebone_act->name), body_tmp, suffix_act);

  if (suffix_act[0] == '\0') {
    return;
  }

  uint obs_len = 0;
  Ob **obe = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < os_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    /* Find matches */
    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        char suffix_other[MAXBONENAME];
        lib_string_split_suffix(ebone->name, sizeof(ebone->name), body_tmp, suffix_other);
        if (STREQ(suffix_act, suffix_other)) {
          ed_armature_ebone_sel_set(ebone, true);
          changed = true;
        }
      }
    }

    if (changed) {
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);
}

/* Use for matching any pose channel data. */
static void sel_similar_data_pchan(Cxt *C, const size_t bytes_size, const int offset)
{
  Ob *obedit = cxt_data_edit_ob(C);
  Armature *arm = static_cast<Armature *>(obedit->data);
  EditBone *ebone_act = cxt_data_active_bone(C);

  const PoseChannel *pchan_active = dune_pose_channel_find_name(obedit->pose, ebone_act->name);

  /* This will mostly happen for corner cases where the user tried to access this
   * before having any valid pose data for the armature. */
  if (pchan_active == nullptr) {
    return;
  }

  const char *data_active = (const char *)PTR_OFFSET(pchan_active, offset);
  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (EBONE_SELECTABLE(arm, ebone)) {
      const PoseChannel *pchan = dune_pose_channel_find_name(obedit->pose, ebone->name);
      if (pchan) {
        const char *data_test = (const char *)PTR_OFFSET(pchan, offset);
        if (memcmp(data_active, data_test, bytes_size) == 0) {
         ed_armature_ebone_sel_set(ebone, true);
        }
      }
    }
  }

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
  graph_id_tag_update(&obedit->id, ID_RECALC_COPY_ON_WRITE);
}

static void is_ancestor(EditBone *bone, EditBone *ancestor)
{
  if (ELEM(bone->temp.ebone, ancestor, nullptr)) {
    return;
  }

  if (!ELEM(bone->tmp.ebone->tmp.ebone, nullptr, ancestor)) {
    is_ancestor(bone->tmp.ebone, ancestor);
  }

  bone->tmp.ebone = bone->tmp.ebone->tmp.ebone;
}

static void sel_similar_children(Cxt *C)
{
  Ob *obedit = cxt_data_edit_ob(C);
  Armature *arm = static_cast<Armature *>(obedit->data);
  EditBone *ebone_act = cxt_data_active_bone(C);

  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    ebone_iter->tmp.ebone = ebone_iter->parent;
  }

  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    is_ancestor(ebone_iter, ebone_act);

    if (ebone_iter->tmp.ebone == ebone_act && EBONE_SELECTABLE(arm, ebone_iter)) {
      ed_armature_ebone_sel_set(ebone_iter, true);
    }
  }

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
  graph_id_tag_update(&obedit->id, ID_RECALC_COPY_ON_WRITE);
}

static void sel_similar_children_immediate(Cxt *C)
{
  Ob *obedit = cxt_data_edit_ob(C);
  Armature *arm = static_cast<Armature *>(obedit->data);
  EditBone *ebone_act = cxt_data_active_bone(C);

  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    if (ebone_iter->parent == ebone_act && EBONE_SELECTABLE(arm, ebone_iter)) {
      ed_armature_ebone_sel_set(ebone_iter, true);
    }
  }

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
  graph_id_tag_update(&obedit->id, ID_RECALC_COPY_ON_WRITE);
}

static void sel_similar_siblings(Cxt *C)
{
  Ob *obedit = cxt_data_edit_ob(C);
  Armature *arm = static_cast<Armature *>(obedit->data);
  EditBone *ebone_act = cxt_data_active_bone(C);

  if (ebone_act->parent == nullptr) {
    return;
  }

  LIST_FOREACH (EditBone *, ebone_iter, arm->edbo) {
    if (ebone_iter->parent == ebone_act->parent && EBONE_SELECTABLE(arm, ebone_iter)) {
     ed_armature_ebone_sel_set(ebone_iter, true);
    }
  }

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
  graph_id_tag_update(&obedit->id, ID_RECALC_COPY_ON_WRITE);
}

static int armature_sel_similar_ex(Cxt *C, WinOp *op)
{
  /* Get props */
  int type = api_enum_get(op->ptr, "type");
  float thresh = api_float_get(op->ptr, "threshold");

  /* Check for active bone */
  if (cxt_data_active_bone(C) == nullptr) {
    dune_report(op->reports, RPT_ERROR, "Op requires an active bone");
    return OP_CANCELLED;
  }

#define STRUCT_SIZE_AND_OFFSET(_struct, _member) \
  sizeof(_struct::_member), offsetof(_struct, _member)

  switch (type) {
    case SIMEDBONE_CHILDREN:
      sel_similar_children(C);
      break;
    case SIMEDBONE_CHILDREN_IMMEDIATE:
      sel_similar_children_immediate(C);
      break;
    case SIMEDBONE_SIBLINGS:
      sel_similar_siblings(C);
      break;
    case SIMEDBONE_LENGTH:
      sel_similar_length(C, thresh);
      break;
    case SIMEDBONE_DIRECTION:
      sel_similar_direction(C, thresh);
      break;
    case SIMEDBONE_PREFIX:
      sel_similar_prefix(C);
      break;
    case SIMEDBONE_SUFFIX:
      sel_similar_suffix(C);
      break;
    case SIMEDBONE_COLLECTION:
      sel_similar_bone_collection(C);
      break;
    case SIMEDBONE_COLOR:
      sel_similar_bone_color(C);
      break;
    case SIMEDBONE_SHAPE:
      sel_similar_data_pchan(C, STRUCT_SIZE_AND_OFFSET(PoseChannel, custom));
      break;
  }

#undef STRUCT_SIZE_AND_OFFSET

  ed_outliner_sel_sync_from_edit_bone_tag(C);

  return OP_FINISHED;
}

void ARMATURE_OT_sel_similar(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Similar";
  ot->idname = "ARMATURE_OT_sel_similar";

  /* cb fns */
  ot->invoke = win_menu_invoke;
  ot->ex = armature_sel_similar_ex;
  ot->poll = ed_op_editarmature;
  ot->description = "Sel similar bones by prop types";

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_similar_types, SIMEDBONE_LENGTH, "Type", "");
  api_def_float(ot->sapi, "threshold", 0.1f, 0.0f, 1.0f, "Threshold", "", 0.0f, 1.0f);
}

/* Sel Hierarchy Op */
/* No need to convert to multi-obs. Just like we keep the non-active bones
 * sel we then keep the non-active obs untouched (sel/unsel). */
static int armature_sel_hierarchy_ex(Cxt *C, WinOp *op)
{
  Ob *ob = cxt_data_edit_ob(C);
  EditBone *ebone_active;
  int direction = api_enum_get(op->ptr, "direction");
  const bool add_to_sel = api_bool_get(op->ptr, "extend");
  bool changed = false;
  Armature *arm = (Armature *)ob->data;

  ebone_active = arm->act_edbone;
  if (ebone_active == nullptr) {
    return OP_CANCELLED;
  }

  if (direction == BONE_SEL_PARENT) {
    if (ebone_active->parent) {
      EditBone *ebone_parent;

      ebone_parent = ebone_active->parent;

      if (EBONE_SELECTABLE(arm, ebone_parent)) {
        arm->act_edbone = ebone_parent;

        if (!add_to_sel) {
          ed_armature_ebone_sel_set(ebone_active, false);
        }
        ed_armature_ebone_sel_set(ebone_parent, true);

        changed = true;
      }
    }
  }
  else { /* BONE_SELECT_CHILD */
    EditBone *ebone_child = nullptr;
    int pass;

    /* first pass, only connected bones (the logical direct child) */
    for (pass = 0; pass < 2 && (ebone_child == nullptr); pass++) {
      LIS_FOREACH (EditBone *, ebone_iter, arm->edbo) {
        /* possible we have multiple children, some invisible */
        if (EBONE_SELECTABLE(arm, ebone_iter)) {
          if (ebone_iter->parent == ebone_active) {
            if ((pass == 1) || (ebone_iter->flag & BONE_CONNECTED)) {
              ebone_child = ebone_iter;
              break;
            }
          }
        }
      }
    }

    if (ebone_child) {
      arm->act_edbone = ebone_child;

      if (!add_to_sel) {
        ed_armature_ebone_sel_set(ebone_active, false);
      }
      ed_armature_ebone_sel_set(ebone_child, true);

      changed = true;
    }
  }

  if (changed == false) {
    return OP_CANCELLED;
  }

  ed_outliner_sel_sync_from_edit_bone_tag(C);

  ed_armature_edit_sync_sel(arm->edbo);

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
  graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);

  return OP_FINISHED;
}

void ARMATURE_OT_sel_hierarchy(WinOpType *ot)
{
  static const EnumPropItem direction_items[] = {
      {BONE_SEL_PARENT, "PARENT", 0, "Sel Parent", ""},
      {BONE_SEL_CHILD, "CHILD", 0, "Select Child", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Sel Hierarchy";
  ot->idname = "ARMATURE_OT_sel_hierarchy";
  ot->description = "Sel immediate parent/children of sel bones";

  /* api cbs */
  ot->ex = armature_sel_hierarchy_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_enum(ot->sapi, "direction", direction_items, BONE_SEL_PARENT, "Direction", "");
  api_def_bool(ot->sapi, "extend", false, "Extend", "Extend the sel");
}

/* Sel Mirror Op */

/* clone of pose_sel_mirror_ex keep in sync */
static int armature_sel_mirror_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  const bool active_only = api_bool_get(op->ptr, "only_active");
  const bool extend = api_bool_get(op->ptr, "extend");

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);

    EditBone *ebone_mirror_act = nullptr;

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      const int flag = ed_armature_ebone_selflag_get(ebone);
      EBONE_PREV_FLAG_SET(ebone, flag);
    }

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_SELECTABLE(arm, ebone)) {
        EditBone *ebone_mirror;
        int flag_new = extend ? EBONE_PREV_FLAG_GET(ebone) : 0;

        if ((ebone_mirror = ed_armature_ebone_get_mirrored(arm->edbo, ebone)) &&
            EBONE_VISIBLE(arm, ebone_mirror))
        {
          const int flag_mirror = EBONE_PREV_FLAG_GET(ebone_mirror);
          flag_new |= flag_mirror;

          if (ebone == arm->act_edbone) {
            ebone_mirror_act = ebone_mirror;
          }

          /* skip all but the active or its mirror */
          if (active_only && !ELEM(arm->act_edbone, ebone, ebone_mirror)) {
            continue;
          }
        }

        ed_armature_ebone_selflag_set(ebone, flag_new);
      }
    }

    if (ebone_mirror_act) {
      arm->act_edbone = ebone_mirror_act;
    }

    ed_outliner_sel_sync_from_edit_bone_tag(C);

    ed_armature_edit_sync_sel(arm->edbo);

    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
    graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  }
  mem_free(obs);

  return OP_FINISHED;
}

void ARMATURE_OT_sel_mirror(WimOpType *ot)
{
  /* ids */
  ot->name = "Sel Mirror";
  ot->idname = "ARMATURE_OT_sel_mirror";
  ot->description = "Mirror the bone sel";

  /* api cbs */
  ot->ex = armature_sel_mirror_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(
      ot->sapi, "only_active", false, "Active Only", "Only op on the active bone");
  api_def_bool(ot->sapi, "extend", false, "Extend", "Extend the sel");
}

/* Sel Path Op */
static bool armature_shortest_path_sel(
    Armature *arm, EditBone *ebone_parent, EditBone *ebone_child, bool use_parent, bool is_test)
{
  do {

    if (!use_parent && (ebone_child == ebone_parent)) {
      break;
    }

    if (is_test) {
      if (!EBONE_SELECTABLE(arm, ebone_child)) {
        return false;
      }
    }
    else {
      ed_armature_ebone_selflag_set(ebone_child, (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL));
    }

    if (ebone_child == ebone_parent) {
      break;
    }

    ebone_child = ebone_child->parent;
  } while (true);

  return true;
}

static int armature_shortest_path_pick_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Ob *obedit = cxt_data_edit_ob(C);
  Armature *arm = static_cast<Armature *>(obedit->data);
  EditBone *ebone_src, *ebone_dst;
  EditBone *ebone_isect_parent = nullptr;
  EditBone *ebone_isect_child[2];
  bool changed;
  Base *base_dst = nullptr;

  view3d_op_needs_opengl(C);
  dune_ob_update_sel_id(cxt_data_main(C));

  ebone_src = arm->act_edbone;
  ebone_dst = ed_armature_pick_ebone(C, ev->mval, false, &base_dst);

  /* fallback to ob sel */
  if (ELEM(nullptr, ebone_src, ebone_dst) || (ebone_src == ebone_dst)) {
    return OPERATOR_PASS_THROUGH;
  }

  if (base_dst && base_dst->ob != obedit) {
    /* Disconnected, ignore. */
    return OP_CANCELLED;
  }

  ebone_isect_child[0] = ebone_src;
  ebone_isect_child[1] = ebone_dst;

  /* ensure 'ebone_src' is the parent of 'ebone_dst', or set 'ebone_isect_parent' */
  if (ed_armature_ebone_is_child_recursive(ebone_src, ebone_dst)) {
    /* pass */
  }
  else if (ed_armature_ebone_is_child_recursive(ebone_dst, ebone_src)) {
    SWAP(EditBone *, ebone_src, ebone_dst);
  }
  else if ((ebone_isect_parent = ED_armature_ebone_find_shared_parent(ebone_isect_child, 2))) {
    /* pass */
  }
  else {
    /* disconnected bones */
    return OP_CANCELLED;
  }

  if (ebone_isect_parent) {
    if (armature_shortest_path_sel(arm, ebone_isect_parent, ebone_src, false, true) &&
        armature_shortest_path_sel(arm, ebone_isect_parent, ebone_dst, false, true))
    {
      armature_shortest_path_sel(arm, ebone_isect_parent, ebone_src, false, false);
      armature_shortest_path_sel(arm, ebone_isect_parent, ebone_dst, false, false);
      changed = true;
    }
    else {
      /* unselectable */
      changed = false;
    }
  }
  else {
    if (armature_shortest_path_sel(arm, ebone_src, ebone_dst, true, true)) {
      armature_shortest_path_sel(arm, ebone_src, ebone_dst, true, false);
      changed = true;
    }
    else {
      /* unselectable */
      changed = false;
    }
  }

  if (changed) {
    arm->act_edbone = ebone_dst;
    ed_outliner_select_sync_from_edit_bone_tag(C);
    ed_armature_edit_sync_sel(arm->edbo);
    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, obedit);
    graph_id_tag_update(&obedit->id, ID_RECALC_COPY_ON_WRITE);

    return OP_FINISHED;
  }

  BKE_report(op->reports, RPT_WARNING, "Unselectable bone in chain");
  return OPERATOR_CANCELLED;
}

void ARMATURE_OT_shortest_path_pick(WinOpType *ot)
{
  /* ids */
  ot->name = "Pick Shortest Path";
  ot->idname = "ARMATURE_OT_shortest_path_pick";
  ot->description = "Sel shortest path between two bones";

  /* api cbs */
  ot->invoke = armature_shortest_path_pick_invoke;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
