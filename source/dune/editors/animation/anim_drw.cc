#include "lib_sys_types.h"

#include "types_anim.h"
#include "types_pen_legacy.h"
#include "types_mask.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_userdef.h"

#include "lib_dlrbTree.h"
#include "lib_math_rotation.h"
#include "lib_rect.h"
#include "lib_timecode.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_curve.hh"
#include "dune_fcurve.h"
#include "dune_global.h"
#include "dune_mask.h"
#include "dune_nla.h"

#include "ed_anim_api.hh"
#include "ed_keyframes_drw.hh"
#include "ed_keyframes_edit.hh"
#include "ed_keyframes_keylist.hh"

#include "api_access.hh"
#include "api_path.hh"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

/* CURRENT FRAME DRAWING */
void anim_drw_cfra(const Cxt *C, View2D *v2d, short flag)
{
  Scene *scene = cxt_data_scene(C);

  const float time = scene->r.cfra + scene->r.subframe;
  const float x = float(time * scene->r.framelen);

  gpu_line_width((flag & DRWCFRA_WIDE) ? 3.0 : 2.0);

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  /* Drw a light green line to indicate current frame */
  immUniformThemeColor(TH_CFRAME);

  immBegin(GPU_PRIM_LINES, 2);
  immVertex2f(pos, x, v2d->cur.ymin - 500.0f); /* arbitrary... want it go to bottom */
  immVertex2f(pos, x, v2d->cur.ymax);
  immEnd();
  immUnbindProgram();
}

/* PREVIEW RANGE 'CURTAINS' */
/* 'Preview Range' tools are defined in `anim_ops.cc`. */
void anim_drw_previewrange(const Cxt *C, View2D *v2d, int end_frame_width)
{
  Scene *scene = cxt_data_scene(C);

  /* only drw this if preview range is set */
  if (PRVRANGEON) {
    gpu_blend(GPU_BLEND_ALPHA);

    GPUVertFormat *format = immVertexFormat();
    uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

    immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
    immUniformThemeColorShadeAlpha(TH_ANIM_PREVIEW_RANGE, -25, -30);
    /* Fix this hardcoded color (anim_active) */
    // immUniformColor4f(0.8f, 0.44f, 0.1f, 0.2f);

    /* only drw 2 separate 'curtains' if there's no overlap between them */
    if (PSFRA < PEFRA + end_frame_width) {
      immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, float(PSFRA), v2d->cur.ymax);
      immRectf(pos, float(PEFRA + end_frame_width), v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
    }
    else {
      immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
    }

    immUnbindProgram();

    gpu_blend(GPU_BLEND_NONE);
  }
}

/* SCENE FRAME RANGE */
void anim_drw_framerange(Scene *scene, View2D *v2d)
{
  /* drw darkened area outside of active timeline frame range */
  gpu_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformThemeColorShadeAlpha(TH_BACK, -25, -100);

  if (scene->r.sfra < scene->r.efra) {
    immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, float(scene->r.sfra), v2d->cur.ymax);
    immRectf(pos, float(scene->r.efra), v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
  }
  else {
    immRectf(pos, v2d->cur.xmin, v2d->cur.ymin, v2d->cur.xmax, v2d->cur.ymax);
  }

  gpu_blend(GPU_BLEND_NONE);

  /* thin lines where the actual frames are */
  immUniformThemeColorShade(TH_BACK, -60);

  immBegin(GPU_PRIM_LINES, 4);

  immVertex2f(pos, float(scene->r.sfra), v2d->cur.ymin);
  immVertex2f(pos, float(scene->r.sfra), v2d->cur.ymax);

  immVertex2f(pos, float(scene->r.efra), v2d->cur.ymin);
  immVertex2f(pos, float(scene->r.efra), v2d->cur.ymax);

  immEnd();
  immUnbindProgram();
}

void anim_drw_action_framerange(
    AnimData *adt, Action *action, View2D *v2d, float ymin, float ymax)
{
  if ((action->flag & ACT_FRAME_RANGE) == 0) {
    return;
  }

  /* Compute the dimensions. */
  CLAMP_MIN(ymin, v2d->cur.ymin);
  CLAMP_MAX(ymax, v2d->cur.ymax);

  if (ymin > ymax) {
    return;
  }

  const float sfra = dune_nla_tweakedit_remap(adt, action->frame_start, NLATIME_CONVERT_MAP);
  const float efra = dune_nla_tweakedit_remap(adt, action->frame_end, NLATIME_CONVERT_MAP);

  /* Diagonal stripe filled area outside of the frame range. */
  gpu_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_2D_DIAG_STRIPES);

  float color[4];
  ui_GetThemeColorShadeAlpha4fv(TH_BACK, -40, -50, color);

  immUniform4f("color1", color[0], color[1], color[2], color[3]);
  immUniform4f("color2", 0.0f, 0.0f, 0.0f, 0.0f);
  immUniform1i("size1", 2 * UI_SCALE_FAC);
  immUniform1i("size2", 4 * UI_SCALE_FAC);

  if (sfra < efra) {
    immRectf(pos, v2d->cur.xmin, ymin, sfra, ymax);
    immRectf(pos, efra, ymin, v2d->cur.xmax, ymax);
  }
  else {
    immRectf(pos, v2d->cur.xmin, ymin, v2d->cur.xmax, ymax);
  }

  immUnbindProgram();

  gpu_blend(GPU_BLEND_NONE);

  /* Thin lines where the actual frames are. */
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformThemeColorShade(TH_BACK, -60);

  gpu_line_width(1.0f);

  immBegin(GPU_PRIM_LINES, 4);

  immVertex2f(pos, sfra, ymin);
  immVertex2f(pos, sfra, ymax);

  immVertex2f(pos, efra, ymin);
  immVertex2f(pos, efra, ymax);

  immEnd();
  immUnbindProgram();
}

/* NLA-MAPPING UTILS (required for drwing and also editing keyframes). */
AnimData *anim_nla_mapping_get(AnimCxt *ac, AnimListElem *ale)
{
  /* sanity checks */
  if (ac == nullptr) {
    return nullptr;
  }

  /* abort if rendering - we may get some race condition issues... */
  if (G.is_rendering) {
    return nullptr;
  }

  /* apart from strictly keyframe-related contexts, this shouldn't even happen */
  /* nla and channel here may not be necessary... */
  if (ELEM(ac->datatype,
           ANIMCONT_ACTION,
           ANIMCONT_SHAPEKEY,
           ANIMCONT_DOPESHEET,
           ANIMCONT_FCURVES,
           ANIMCONT_NLA,
           ANIMCONT_CHANNEL,
           ANIMCONT_TIMELINE))
  {
    /* handling depends on the type of anim-cxt we've got */
    if (ale) {
      /* NLA Ctrl Curves occur on NLA strips,
       * and shouldn't be subjected to this kind of mapping. */
      if (ale->type != ANIMTYPE_NLACURVE) {
        return ale->adt;
      }
    }
  }

  /* cannot handle... */
  return nullptr;
}

/* Helper fn for anim_nla_mapping_apply_fcurve() -> "restore",
 * i.e. mapping points back to action-time. */
static short bezt_nlamapping_restore(KeyframeEditData *ked, BezTriple *bezt)
{
  /* AnimData block providing scaling is stored in 'data', only_keys option is stored in i1 */
  AnimData *adt = (AnimData *)ked->data;
  short only_keys = short(ked->i1);

  /* adjust BezTriple handles only if allowed to */
  if (only_keys == 0) {
    bezt->vec[0][0] = dune_nla_tweakedit_remap(adt, bezt->vec[0][0], NLATIME_CONVERT_UNMAP);
    bezt->vec[2][0] = dune_nla_tweakedit_remap(adt, bezt->vec[2][0], NLATIME_CONVERT_UNMAP);
  }

  bezt->vec[1][0] = dune_nla_tweakedit_remap(adt, bezt->vec[1][0], NLATIME_CONVERT_UNMAP);

  return 0;
}

/* helper fn for anim_nla_mapping_apply_fcurve() -> "apply",
 * i.e. mapping points to NLA-mapped global time */
static short bezt_nlamapping_apply(KeyframeEditData *ked, BezTriple *bezt)
{
  /* AnimData block providing scaling is stored in 'data', only_keys option is stored in i1 */
  AnimData *adt = (AnimData *)ked->data;
  short only_keys = short(ked->i1);

  /* adjust BezTriple handles only if allowed to */
  if (only_keys == 0) {
    bezt->vec[0][0] = dune_nla_tweakedit_remap(adt, bezt->vec[0][0], NLATIME_CONVERT_MAP);
    bezt->vec[2][0] = dune_nla_tweakedit_remap(adt, bezt->vec[2][0], NLATIME_CONVERT_MAP);
  }

  bezt->vec[1][0] = dune_nla_tweakedit_remap(adt, bezt->vec[1][0], NLATIME_CONVERT_MAP);

  return 0;
}

void anim_nla_mapping_apply_fcurve(AnimData *adt, FCurve *fcu, bool restore, bool only_keys)
{
  if (adt == nullptr || lib_list_is_empty(&adt->nla_tracks)) {
    return;
  }
  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn map_cb;

  /* init edit data
   * - AnimData is stored in 'data'
   * - only_keys is stored in 'i1' */
  ked.data = (void *)adt;
  ked.i1 = int(only_keys);

  /* get editing cb */
  if (restore) {
    map_cb = bezt_nlamapping_restore;
  }
  else {
    map_cb = bezt_nlamapping_apply;
  }

  /* apply to F-Curve */
  anim_fcurve_keyframes_loop(&ked, fcu, nullptr, map_cb, nullptr);
}

/* UNITS CONVERSION MAPPING (required for drawing and editing keyframes) */
short anim_get_normalization_flags(SpaceLink *space_link)
{
  if (space_link->spacetype == SPACE_GRAPH) {
    SpaceGraph *sipo = (SpaceGraph *)space_link;
    bool use_normalization = (sipo->flag & SIPO_NORMALIZE) != 0;
    bool freeze_normalization = (sipo->flag & SIPO_NORMALIZE_FREEZE) != 0;
    return use_normalization ? (ANIM_UNITCONV_NORMALIZE |
                                (freeze_normalization ? ANIM_UNITCONV_NORMALIZE_FREEZE : 0)) :
                               0;
  }

  return 0;
}

static void fcurve_scene_coord_range_get(Scene *scene,
                                         FCurve *fcu,
                                         float *r_min_coord,
                                         float *r_max_coord)
{
  float min_coord = FLT_MAX;
  float max_coord = -FLT_MAX;
  const bool use_preview_only = PRVRANGEON;

  if (fcu->bezt || fcu->fpt) {
    int start = 0;
    int end = fcu->totvert;

    if (use_preview_only) {
      if (fcu->bezt) {
        /* Preview frame ranges need to be converted to bezt array indices. */
        bool replace = false;
        start = dune_fcurve_bezt_binarysearch_index(
            fcu->bezt, scene->r.psfra, fcu->totvert, &replace);

        end = dune_fcurve_bezt_binarysearch_index(
            fcu->bezt, scene->r.pefra + 1, fcu->totvert, &replace);
      }
      else if (fcu->fpt) {
        const int unclamped_start = int(scene->r.psfra - fcu->fpt[0].vec[0]);
        start = max_ii(unclamped_start, 0);
        end = min_ii(unclamped_start + (scene->r.pefra - scene->r.psfra) + 1, fcu->totvert);
      }
    }

    if (fcu->bezt) {
      const BezTriple *bezt = fcu->bezt + start;
      for (int i = start; i < end; i++, bezt++) {

        if (i == 0) {
          /* We ignore extrapolation flags and handle here, and use the
           * control point position only. so we normalize "interesting"
           * part of the curve.
           *
           * Here we handle left extrapolation. */
          max_coord = max_ff(max_coord, bezt->vec[1][1]);
          min_coord = min_ff(min_coord, bezt->vec[1][1]);
        }
        else {
          const BezTriple *prev_bezt = bezt - 1;
          if (!ELEM(prev_bezt->ipo, BEZT_IPO_BEZ, BEZT_IPO_BACK, BEZT_IPO_ELASTIC)) {
            /* The points on the curve will lie inside the start and end points.
             * Calc min/max using both previous and current CV. */
            max_coord = max_ff(max_coord, bezt->vec[1][1]);
            min_coord = min_ff(min_coord, bezt->vec[1][1]);
            max_coord = max_ff(max_coord, prev_bezt->vec[1][1]);
            min_coord = min_ff(min_coord, prev_bezt->vec[1][1]);
          }
          else {
            const int resol = fcu->driver ?
                                  32 :
                                  min_ii(int(5.0f * len_v2v2(bezt->vec[1], prev_bezt->vec[1])),
                                         32);
            if (resol < 2) {
              max_coord = max_ff(max_coord, prev_bezt->vec[1][1]);
              min_coord = min_ff(min_coord, prev_bezt->vec[1][1]);
            }
            else {
              if (!ELEM(prev_bezt->ipo, BEZT_IPO_BACK, BEZT_IPO_ELASTIC)) {
                /* Calc min/max using bezier forward differencing. */
                float data[120];
                float v1[2], v2[2], v3[2], v4[2];

                v1[0] = prev_bezt->vec[1][0];
                v1[1] = prev_bezt->vec[1][1];
                v2[0] = prev_bezt->vec[2][0];
                v2[1] = prev_bezt->vec[2][1];

                v3[0] = bezt->vec[0][0];
                v3[1] = bezt->vec[0][1];
                v4[0] = bezt->vec[1][0];
                v4[1] = bezt->vec[1][1];

                dune_fcurve_correct_bezpart(v1, v2, v3, v4);

                dune_curve_forward_diff_bezier(
                    v1[0], v2[0], v3[0], v4[0], data, resol, sizeof(float[3]));
                dune_curve_forward_diff_bezier(
                    v1[1], v2[1], v3[1], v4[1], data + 1, resol, sizeof(float[3]));

                for (int j = 0; j <= resol; ++j) {
                  const float *fp = &data[j * 3];
                  max_coord = max_ff(max_coord, fp[1]);
                  min_coord = min_ff(min_coord, fp[1]);
                }
              }
              else {
                /* Calc min/max using full fcurve evaluation.
                 * [slower than bezier forward differencing but evaluates Back/Elastic
                 * interpolation as well]. */
                float step_size = (bezt->vec[1][0] - prev_bezt->vec[1][0]) / resol;
                for (int j = 0; j <= resol; j++) {
                  float eval_time = prev_bezt->vec[1][0] + step_size * j;
                  float eval_val = eval_fcurve_only_curve(fcu, eval_time);
                  max_coord = max_ff(max_coord, eval_val);
                  min_coord = min_ff(min_coord, eval_val);
                }
              }
            }
          }
        }
      }
    }
    else if (fcu->fpt) {
      const FPoint *fpt = fcu->fpt + start;
      for (int i = start; i < end; ++i, ++fpt) {
        min_coord = min_ff(min_coord, fpt->vec[1]);
        max_coord = max_ff(max_coord, fpt->vec[1]);
      }
    }
  }

  if (r_min_coord) {
    *r_min_coord = min_coord;
  }
  if (r_max_coord) {
    *r_max_coord = max_coord;
  }
}

static float normalization_factor_get(Scene *scene, FCurve *fcu, short flag, float *r_offset)
{
  float factor = 1.0f, offset = 0.0f;

  if (flag & ANIM_UNITCONV_RESTORE) {
    if (r_offset) {
      *r_offset = fcu->prev_offset;
    }

    return 1.0f / fcu->prev_norm_factor;
  }

  if (flag & ANIM_UNITCONV_NORMALIZE_FREEZE) {
    if (r_offset) {
      *r_offset = fcu->prev_offset;
    }
    if (fcu->prev_norm_factor == 0.0f) {
      /* Happens when Auto Normalize was disabled before
       * any curves were displayed. */
      return 1.0f;
    }
    return fcu->prev_norm_factor;
  }

  if (G.moving & G_TRANSFORM_FCURVES) {
    if (r_offset) {
      *r_offset = fcu->prev_offset;
    }
    if (fcu->prev_norm_factor == 0.0f) {
      /* Same as above. */
      return 1.0f;
    }
    return fcu->prev_norm_factor;
  }

  fcu->prev_norm_factor = 1.0f;

  float max_coord = -FLT_MAX;
  float min_coord = FLT_MAX;
  fcurve_scene_coord_range_get(scene, fcu, &min_coord, &max_coord);

  /* We use an ULPS-based floating point comparison here, w the
   * rationale that if there are too few possible vals between
   * `min_coord` and `max_coord`, then after display normalization it
   * will certainly be a weird quantized experience for the user anyway. */
  if (min_coord < max_coord && ulp_diff_ff(min_coord, max_coord) > 256) {
    /* Normalize. */
    const float range = max_coord - min_coord;
    factor = 2.0f / range;
    offset = -min_coord - range / 2.0f;
  }
  else {
    /* Skip normalization. */
    factor = 1.0f;
    offset = -min_coord;
  }

  BLI_assert(factor != 0.0f);
  if (r_offset) {
    *r_offset = offset;
  }

  fcu->prev_norm_factor = factor;
  fcu->prev_offset = offset;
  return factor;
}

float anim_unit_mapping_get_factor(Scene *scene, Id *id, FCurve *fcu, short flag, float *r_offset)
{
  if (flag & ANIM_UNITCONV_NORMALIZE) {
    return normalization_factor_get(scene, fcu, flag, r_offset);
  }

  if (r_offset) {
    *r_offset = 0.0f;
  }

  /* sanity checks */
  if (id && fcu && fcu->api_path) {
    ApiPtr ptr;
    ApiProp *prop;

    /* get api prop that F-Curve affects */
    ApiPtr id_ptr = api_id_ptr_create(id);
    if (api_path_resolve_prop(&id_ptr, fcu->api_path, &ptr, &prop)) {
      /* rotations: radians <-> degrees? */
      if (API_SUBTYPE_UNIT(api_prop_subtype(prop)) == PROP_UNIT_ROTATION) {
        /* if the radians flag is not set, default to using degrees which need conversions */
        if ((scene) && (scene->unit.sys_rotation == USER_UNIT_ROT_RADIANS) == 0) {
          if (flag & ANIM_UNITCONV_RESTORE) {
            return DEG2RADF(1.0f); /* degrees to radians */
          }
          return RAD2DEGF(1.0f); /* radians to degrees */
        }
      }

      /* TODO: other rotation types here as necessary */
    }
  }

  /* no mapping needs to occur... */
  return 1.0f;
}

static bool find_prev_next_keyframes(Cxt *C, int *r_nextfra, int *r_prevfra)
{
  Scene *scene = cxt_data_scene(C);
  Ob *ob = cxt_data_active_ob(C);
  Mask *mask = cxt_data_edit_mask(C);
  DopeSheet ads = {nullptr};
  AnimKeylist *keylist = ed_keylist_create();
  const ActKeyColumn *aknext, *akprev;
  float cfranext, cfraprev;
  bool donenext = false, doneprev = false;
  int nextcount = 0, prevcount = 0;

  cfranext = cfraprev = float(scene->r.cfra);

  /* seed up dummy dopesheet cxt with flags to perform necessary filtering */
  if ((scene->flag & SCE_KEYS_NO_SELONLY) == 0) {
    /* only selected channels are included */
    ads.filterflag |= ADS_FILTER_ONLYSEL;
  }

  /* populate tree with keyframe nodes */
  scene_to_keylist(&ads, scene, keylist, 0, {-FLT_MAX, FLT_MAX});
  gpencil_to_keylist(&ads, scene->gpd, keylist, false);

  if (ob) {
    ob_to_keylist(&ads, ob, keylist, 0, {-FLT_MAX, FLT_MAX});
    pen_to_keylist(&ads, static_cast<PenData *>(ob->data), keylist, false);
  }

  if (mask) {
    MaskLayer *masklay = dune_mask_layer_active(mask);
    mask_to_keylist(&ads, masklay, keylist);
  }
  ed_keylist_prepare_for_direct_access(keylist);

  /* TODO: Keylists are ordered, no need to do any searching at all. */
  /* find matching keyframe in the right direction */
  do {
    aknext = ed_keylist_find_next(keylist, cfranext);

    if (aknext) {
      if (scene->r.cfra == int(aknext->cfra)) {
        /* make this the new starting point for the search and ignore */
        cfranext = aknext->cfra;
      }
      else {
        /* this changes the frame, so set the frame and we're done */
        if (++nextcount == U.view_frame_keyframes) {
          donenext = true;
        }
      }
      cfranext = aknext->cfra;
    }
  } while ((aknext != nullptr) && (donenext == false));

  do {
    akprev = ed_keylist_find_prev(keylist, cfraprev);

    if (akprev) {
      if (scene->r.cfra == int(akprev->cfra)) {
        /* make this the new starting point for the search */
      }
      else {
        /* this changes the frame, so set the frame and we're done */
        if (++prevcount == U.view_frame_keyframes) {
          doneprev = true;
        }
      }
      cfraprev = akprev->cfra;
    }
  } while ((akprev != nullptr) && (doneprev == false));

  /* free tmp stuff */
  ed_keylist_free(keylist);

  /* any success? */
  if (doneprev || donenext) {
    if (doneprev) {
      *r_prevfra = cfraprev;
    }
    else {
      *r_prevfra = scene->r.cfra - (cfranext - scene->r.cfra);
    }

    if (donenext) {
      *r_nextfra = cfranext;
    }
    else {
      *r_nextfra = scene->r.cfra + (scene->r.cfra - cfraprev);
    }

    return true;
  }

  return false;
}

void anim_center_frame(Cxt *C, int smooth_viewtx)
{
  ARgn *rgn = cxt_win_rgn(C);
  Scene *scene = cxt_data_scene(C);
  float w = lib_rctf_size_x(&rgn->v2d.cur);
  rctf newrct;
  int nextfra, prevfra;

  switch (U.view_frame_type) {
    case ZOOM_FRAME_MODE_SECONDS: {
      const float fps = FPS;
      newrct.xmax = scene->r.cfra + U.view_frame_seconds * fps + 1;
      newrct.xmin = scene->r.cfra - U.view_frame_seconds * fps - 1;
      newrct.ymax = rgn->v2d.cur.ymax;
      newrct.ymin = rgn->v2d.cur.ymin;
      break;
    }

    /* hardest case of all, look for all keyframes around frame and display those */
    case ZOOM_FRAME_MODE_KEYFRAMES:
      if (find_prev_next_keyframes(C, &nextfra, &prevfra)) {
        newrct.xmax = nextfra;
        newrct.xmin = prevfra;
        newrct.ymax = rgn->v2d.cur.ymax;
        newrct.ymin = rgn->v2d.cur.ymin;
        break;
      }
      /* else drop through, keep range instead */
      ATTR_FALLTHROUGH;

    case ZOOM_FRAME_MODE_KEEP_RANGE:
    default:
      newrct.xmax = scene->r.cfra + (w / 2);
      newrct.xmin = scene->r.cfra - (w / 2);
      newrct.ymax = rgn->v2d.cur.ymax;
      newrct.ymin = rgn->v2d.cur.ymin;
      break;
  }

  ui_view2d_smooth_view(C, rgn, &newrct, smooth_viewtx);
}
