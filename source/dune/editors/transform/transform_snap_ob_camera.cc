#include "lib_math_matrix.hh"

#include "dune_bvhutils.hh"
#include "dune_mesh.hh"
#include "dune_ob.hh"
#include "dune_tracking.h"

#include "ed_transform_snap_ob_cxt.hh"

#include "transform_snap_obt.hh"

using namespace dune;

eSnapMode snapCamera(SnapObCxt *scxt,
                     Ob *ob,
                     const float4x4 &obmat,
                     eSnapMode snap_to_flag)
{
  eSnapMode retval = SCE_SNAP_TO_NONE;

  if (!(scxt->runtime.snap_to_flag & SCE_SNAP_TO_POINT)) {
    return retval;
  }

  Scene *scene = sctx->scene;

  MovieClip *clip = dune_ob_movieclip_get(scene, ob, false);
  if (clip == nullptr) {
    return snap_ob_center(scxt, ob, obmat, snap_to_flag);
  }

  if (ob->transflag & OB_DUP) {
    return retval;
  }

  float4x4 orig_camera_mat;
  dune_tracking_get_camera_ob_matrix(ob, orig_camera_mat.ptr());

  SnapData nearest2d(scxt);
  nearest2d.clip_planes_enable(sctx, ob);

  MovieTracking *tracking = &clip->tracking;
  LIST_FOREACH (MovieTrackingOb *, tracking_ob, &tracking->obs) {
    float4x4 reconstructed_camera_imat;

    if ((tracking_ob->flag & TRACKING_OB_CAMERA) == 0) {
      float4x4 reconstructed_camera_mat;
      dune_tracking_camera_get_reconstructed_interpolate(
          tracking, tracking_ob, scene->r.cfra, reconstructed_camera_mat.ptr());

      reconstructed_camera_imat = math::invert(reconstructed_camera_mat) * obmat;
    }

    LIST_FOREACH (MovieTrackingTrack *, track, &tracking_ob->tracks) {
      float3 bundle_pos;

      if ((track->flag & TRACK_HAS_BUNDLE) == 0) {
        continue;
      }

      if (tracking_ob->flag & TRACKING_OB_CAMERA) {
        bundle_pos = math::transform_point(orig_camera_mat, float3(track->bundle_pos));
      }
      else {
        bundle_pos = math::transform_point(reconstructed_camera_imat, float3(track->bundle_pos));
      }

      if (nearest2d.snap_point(bundle_pos)) {
        retval = SCE_SNAP_TO_POINT;
      }
    }
  }

  if (retval) {
    nearest2d.register_result(scxt, ob, static_cast<const Id *>(ob->data));
  }
  return retval;
}
