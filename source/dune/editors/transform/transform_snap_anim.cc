#include "types_anim.h"

#include "lib_math_matrix_types.hh"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_nla.h"

#include "ed_markers.hh"
#include "ed_screen.hh"

#include "transform.hh"
#include "transform_snap.hh"

using namespace dune;

/* Snapping in Anim Editors */
void snapFrameTransform(TransInfo *t,
                        const eSnapMode snap_mode,
                        const float val_init,
                        const float val_final,
                        float *r_val_final)
{
  float deltax = val_final - val_init;
  /* Needed for the FPS macro. */
  const Scene *scene = t->scene;
  const eSnapFlag snap_flag = t->tsnap.flag;

  switch (snap_mode) {
    case SCE_SNAP_TO_FRAME: {
      if (snap_flag & SCE_SNAP_ABS_TIME_STEP) {
        *r_val_final = floorf(val_final + 0.5f);
      }
      else {
        deltax = floorf(deltax + 0.5f);
        *r_val_final = val_init + deltax;
      }
      break;
    }
    case SCE_SNAP_TO_SECOND: {
      if (snap_flag & SCE_SNAP_ABS_TIME_STEP) {
        *r_val_final = floorf((val_final / FPS) + 0.5) * FPS;
      }
      else {
        deltax = float(floor((deltax / FPS) + 0.5) * FPS);
        *r_val_final = val_initial + deltax;
      }
      break;
    }
    case SCE_SNAP_TO_MARKERS: {
      /* Snap to nearest marker. */
      /* TODO: need some more careful checks for where data comes from. */
      const float nearest_marker_time = float(
          ed_markers_find_nearest_marker_time(&t->scene->markers, val_final));
      *r_val_final = nearest_marker_time;
      break;
    }
    default: {
      *r_val_final = val_final;
      break;
    }
  }
}

static void transform_snap_anim_flush_data_ex(
    TransInfo *t, TransData *td, float val, const eSnapMode snap_mode, float *r_val_final)
{
  lib_assert(t->tsnap.flag);

  float ival = td->iloc[0];
  AnimData *adt = static_cast<AnimData *>(!ELEM(t->spacetype, SPACE_NLA, SPACE_SEQ) ? td->extra :
                                                                                      nullptr);

  /* Convert frame to nla-action time (if needed) */
  if (adt) {
    val = dune_nla_tweakedit_remap(adt, val, NLATIME_CONVERT_MAP);
    ival = dune_nla_tweakedit_remap(adt, ival, NLATIME_CONVERT_MAP);
  }

  snapFrameTransform(t, snap_mode, ival, val, &val);

  /* Convert frame out of nla-action time. */
  if (adt) {
    val = dune_nla_tweakedit_remap(adt, val, NLATIME_CONVERT_UNMAP);
  }

  *r_val_final = val;
}

void transform_snap_anim_flush_data(TransInfo *t,
                                    TransData *td,
                                    const eSnapMode snap_mode,
                                    float *r_val_final)
{
  transform_snap_anim_flush_data_ex(t, td, td->loc[0], snap_mode, r_val_final);
}

static void invert_snap(eSnapMode &snap_mode)
{
  if (snap_mode & SCE_SNAP_TO_FRAME) {
    snap_mode &= ~SCE_SNAP_TO_FRAME;
    snap_mode |= SCE_SNAP_TO_SECOND;
  }
  else if (snap_mode & SCE_SNAP_TO_SECOND) {
    snap_mode &= ~SCE_SNAP_TO_SECOND;
    snap_mode |= SCE_SNAP_TO_FRAME;
  }
}

/* WORKAROUND: The src position is based on the transformed elems.
 * However, at this stage transform has not yet been applied.
 * Apply transform here. */
static float2 nla_transform_apply(TransInfo *t, float *vec, float2 &ival)
{
  float4x4 mat = float4x4::identity();

  float vals_final_prev[4];
  const size_t vals_final_size = sizeof(*t->vals_final) * size_t(t->idx_max + 1);
  memcpy(vals_final_prev, t->vals_final, vals_final_size);
  memcpy(t->vals_final, vec, vals_final_size);

  mat[3][0] = ival[0];
  mat[3][1] = ival[1];
  transform_apply_matrix(t, mat.ptr());

  memcpy(t->vals_final, vals_final_prev, vals_final_size);

  return mat.location().xy();
}

bool transform_snap_nla_calc(TransInfo *t, float *vec)
{
  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  eSnapMode snap_mode = t->tsnap.mode;
  if (t->modifiers & MOD_SNAP_INVERT) {
    invert_snap(snap_mode);
  }

  float best_dist = FLT_MAX;
  float2 best_source = float2(0);
  float2 best_target = float2(0);
  bool found = false;

  for (int i = 0; i < tc->data_len; i++) {
    TransData *td = &tc->data[i];
    float2 snap_source = td->iloc;
    float2 snap_target = nla_transform_apply(t, vec, snap_source);

    transform_snap_anim_flush_data_ex(t, td, snap_target[0], snap_mode, &snap_target[0]);
    const int dist = abs(snap_target[0] - snap_source[0]);
    if (dist < best_dist) {
      if (dist != 0) {
        /* Prioritize non-zero dist for scale. */
        best_dist = dist;
      }
      else if (found) {
        continue;
      }
      best_source = snap_source;
      best_target = snap_target;
      found = true;
    }
  }

  copy_v2_v2(t->tsnap.snap_source, best_source);
  copy_v2_v2(t->tsnap.snap_target, best_target);
  return found;
}

/** \} */
