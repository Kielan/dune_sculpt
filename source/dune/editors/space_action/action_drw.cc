/* Sys includes */
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "lib_dunelib.h"
#include "lib_math_color.h"
#include "lib_utildefines.h"

/* Types */
#include "types_anim.h"
#include "types_cachefile.h"
#include "types_pen_legacy.h"
#include "types_mod.h"
#include "types_node.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_screen.h"

#include "dune_action.h"
#include "dune_bake_geo_nodes_mod.hh"
#include "dune_cxt.hh"
#include "dune_node_runtime.hh"
#include "dune_pointcache.h"

/* Everything from source (BIF, BDR, BSE) */
#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "ed_anim_api.hh"
#include "ed_keyframes_drw.hh"

#include "mod_nodes.hh"

#include "action_intern.hh"

/* Channel List */
void drw_channel_names(Cxt *C, AnimCxt *ac, ARtn *rgn)
{
  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  eAnimFilter_Flags filter;

  View2D *v2d = &rgn->v2d;
  size_t items;

  /* build list of channels to draw */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_LIST_CHANNELS);
  items = anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  const int height = anim_ui_get_channels_total_height(v2d, items);
  const float pad_bottom = lib_list_is_empty(ac->markers) ? 0 : UI_MARKER_MARGIN_Y;
  v2d->tot.ymin = -(height + pad_bottom);

  /* need to do a view-sync here, so that the keys area doesn't jump around (it must copy this) */
  ui_view2d_sync(nullptr, ac->area, v2d, V2D_LOCK_COPY);

  const float channel_step = anim_ui_get_channel_step();
  /* Loop through channels, and set up drawing depending on their type. */
  { /* first pass: just the standard GL-drawing for backdrop + text */
    size_t channel_index = 0;
    float ymax = anim_ui_get_first_channel_top(v2d);

    for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
         ale = ale->next, ymax -= channel_step, channel_index++)
    {
      const float ymin = ymax - ANIM_UI_get_channel_height();

      /* check if visible */
      if (IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)) {
        /* draw all channels using standard channel-drawing API */
        anim_channel_drw(ac, ale, ymin, ymax, channel_index);
      }
    }
  }
  { /* second pass: widgets */
    uiBlock *block = ui_block_begin(C, rgn, __func__, UI_EMBOSS);
    size_t channel_index = 0;
    float ymax = anim_ui_get_first_channel_top(v2d);

    for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
         ale = ale->next, ymax -= channel_step, channel_index++)
    {
      const float ymin = ymax - anim_ui_get_channel_height();

      /* check if visible */
      if (IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)) {
        /* draw all channels using standard channel-drawing API */
        rctf channel_rect;
        lib_rctf_init(&channel_rect, 0, v2d->cur.xmax, ymin, ymax);
        anim_channel_drw_widgets(C, ac, ale, block, &channel_rect, channel_index);
      }
    }

    ui_block_end(C, block);
    ui_block_drw(C, block);
  }

  /* Free temporary channels. */
  anim_animdata_freelist(&anim_data);
}

/* Keyframes */
/* extra padding for lengths (to go under scrollers) */
#define EXTRA_SCROLL_PAD 100.0f

/* Draw manually set intended playback frame ranges for actions. */
static void drw_channel_action_ranges(List *anim_data, View2D *v2d)
{
  /* Variables for coalescing the Y region of one action. */
  Action *cur_action = nullptr;
  AnimData *cur_adt = nullptr;
  float cur_ymax;

  /* Walk through channels, grouping contiguous spans referencing the same action. */
  float ymax = anim_ui_get_first_channel_top(v2d) + anim_ui_get_channel_skip() / 2;
  const float ystep = anim_ui_get_channel_step();
  float ymin = ymax - ystep;

  for (AnimListElem *ale = static_cast<AnimListElem *>(anim_data->first); ale;
       ale = ale->next, ymax = ymin, ymin -= ystep)
  {
    Action *action = nullptr;
    AnimData *adt = nullptr;

    /* check if visible */
    if (IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
        IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)) {
      /* check if anything to show for this channel */
      if (ale->datatype != ALE_NONE) {
        action = anim_channel_action_get(ale);

        if (action) {
          adt = ale->adt;
        }
      }
    }

    /* Extend the current rgn, or flush and restart. */
    if (action != cur_action || adt != cur_adt) {
      if (cur_action) {
        anim_drw_action_framerange(cur_adt, cur_action, v2d, ymax, cur_ymax);
      }

      cur_action = action;
      cur_adt = adt;
      cur_ymax = ymax;
    }
  }

  /* Flush the last rgn. */
  if (cur_action) {
    anim_drw_action_framerange(cur_adt, cur_action, v2d, ymax, cur_ymax);
  }
}

static void drw_backdrops(AnimCxt *ac, List &anim_data, View2D *v2d, uint pos)
{
  uchar col1[4], col2[4];
  uchar col1a[4], col2a[4];
  uchar col1b[4], col2b[4];
  uchar col_summary[4];

  /* get theme colors */
  ui_GetThemeColor4ubv(TH_SHADE2, col2);
  ui_GetThemeColor4ubv(TH_HILITE, col1);
  ui_GetThemeColor4ubv(TH_ANIM_ACTIVE, col_summary);

  ui_GetThemeColor4ubv(TH_GROUP, col2a);
  ui_GetThemeColor4ubv(TH_GROUP_ACTIVE, col1a);

  ui_GetThemeColor4ubv(TH_DOPESHEET_CHANNELOB, col1b);
  ui_GetThemeColor4ubv(TH_DOPESHEET_CHANNELSUBOB, col2b);

  float ymax = anim_ui_get_first_channel_top(v2d);
  const float channel_step = anim_ui_get_channel_step();
  AnimListElem *ale;
  for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
       ale = ale->next, ymax -= channel_step)
  {
    const float ymin = ymax - anim_ui_get_channel_height();

    /* check if visible */
    if (!(IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)))
    {
      continue;
    }
    const AnimChannelType *acf = anim_channel_get_typeinfo(ale);
    int sel = 0;

    /* determine if any need to draw channel */
    if (ale->datatype == ALE_NONE) {
      continue;
    }
    /* determine if channel is sel */
    if (acf->has_setting(ac, ale, ACHANNEL_SETTING_SEL)) {
      sel = anim_channel_setting_get(ac, ale, ACHANNEL_SETTING_SEL);
    }

    if (elem(ac->datatype, ANIMCONT_ACTION, ANIMCONT_DOPESHEET, ANIMCONT_SHAPEKEY)) {
      switch (ale->type) {
        case ANIMTYPE_SUMMARY: {
          /* reddish color from NLA */
          immUniformThemeColor(TH_ANIM_ACTIVE);
          break;
        }
        case ANIMTYPE_SCENE:
        case ANIMTYPE_OB: {
          immUniformColor3ubvAlpha(col1b, sel ? col1[3] : col1b[3]);
          break;
        }
        case ANIMTYPE_FILLACTD:
        case ANIMTYPE_DSSKEY:
        case ANIMTYPE_DSWOR: {
          immUniformColor3ubvAlpha(col2b, sel ? col1[3] : col2b[3]);
          break;
        }
        case ANIMTYPE_GROUP:
          immUniformColor4ubv(sel ? col1a : col2a);
          break;
        default: {
          immUniformColor4ubv(sel ? col1 : col2);
        }
      }

      /* drw rgn twice: firstly backdrop, then the current range */
      immRectf(pos, v2d->cur.xmin, ymin, v2d->cur.xmax + EXTRA_SCROLL_PAD, ymax);
    }
    else if (ac->datatype == ANIMCONT_PEN) {
      uchar *color;
      switch (ale->type) {
        case ANIMTYPE_SUMMARY:
          color = col_summary;
          break;

        case ANIMTYPE_PEN_LAYER_GROUP:
          color = sel ? col1a : col2a;
          break;

        case ANIMTYPE_PEN_DATABLOCK:
          color = col2b;
          color[3] = sel ? col1[3] : col2b[3];
          break;

        default:
          color = sel ? col1 : col2;
          break;
      }

      /* Color overlay on frames between the start/end frames. */
      immUniformColor4ubv(color);
      immRectf(pos, ac->scene->r.sfra, ymin, ac->scene->r.efra, ymax);

      /* Color overlay outside the start/end frame range get a more transparent overlay. */
      immUniformColor3ubvAlpha(color, std::min(255, color[3] / 2));
      immRectf(pos, v2d->cur.xmin, ymin, ac->scene->r.sfra, ymax);
      immRectf(pos, ac->scene->r.efra, ymin, v2d->cur.xmax + EXTRA_SCROLL_PAD, ymax);
    }
    else if (ac->datatype == ANIMCONT_MASK) {
      /* TODO: this is a copy of gpencil. */
      uchar *color;
      if (ale->type == ANIMTYPE_SUMMARY) {
        color = col_summary;
      }
      else {
        color = sel ? col1 : col2;
      }

      /* Color overlay on frames between the start/end frames. */
      immUniformColor4ubv(color);
      immRectf(pos, ac->scene->r.sfra, ymin, ac->scene->r.efra, ymax);

      /* Color overlay outside the start/end frame range get a more transparent overlay. */
      immUniformColor3ubvAlpha(color, std::min(255, color[3] / 2));
      immRectf(pos, v2d->cur.xmin, ymin, ac->scene->r.sfra, ymax);
      immRectf(pos, ac->scene->r.efra, ymin, v2d->cur.xmax + EXTRA_SCROLL_PAD, ymax);
    }

    /* Alpha-over the channel color, if it's there. */
    {
      const bool show_group_colors = U.anim_flag & USER_ANIM_SHOW_CHANNEL_GROUP_COLORS;
      uint8_t color[3];
      if (show_group_colors && acf->get_channel_color && acf->get_channel_color(ale, color)) {
        immUniformColor3ubvAlpha(color, 32);
        immRectf(pos, v2d->cur.xmin, ymin, v2d->cur.xmax + EXTRA_SCROLL_PAD, ymax);
      }
    }
  }
}

static void drw_keyframes(AnimCxt *ac,
                           View2D *v2d,
                           SpaceAction *saction,
                           List &anim_data)
{
  /* Draw keyframes
   * 1) Only channels that are visible in the Action Editor get drawn/evaluated.
   *    This is to try to optimize this for heavier data sets
   * 2) Keyframes which are out of view horizontally are disregarded */
  int action_flag = saction->flag;
  DopeSheet *ads = &saction->ads;

  if (saction->mode == SACTCONT_TIMELINE) {
    action_flag &= ~(SACTION_SHOW_INTERPOLATION | SACTION_SHOW_EXTREMES);
  }

  const float channel_step = anim_ui_get_channel_step();
  float ymax = anim_ui_get_first_channel_top(v2d);

  ChannelDrawList *draw_list = ed_channel_draw_list_create();

  const float scale_factor = anim_ui_get_keyframe_scale_factor();

  AnimListElem *ale;
  for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
       ale = ale->next, ymax -= channel_step)
  {
    const float ymin = ymax - anim_ui_get_channel_height();
    float ycenter = (ymin + ymax) / 2.0f;

    /* check if visible */
    if (!(IN_RANGE(ymin, v2d->cur.ymin, v2d->cur.ymax) ||
          IN_RANGE(ymax, v2d->cur.ymin, v2d->cur.ymax)))
    {
      continue;
    }

    /* check if anything to show for this channel */
    if (ale->datatype == ALE_NONE) {
      continue;
    }

    AnimData *adt = anim_nla_mapping_get(ac, ale);

    /* Add channels to list to draw later. */
    switch (ale->datatype) {
      case ALE_ALL:
        ed_add_summary_channel(
            drw_list, static_cast<AnimCxt *>(ale->data), ycenter, scale_factor, action_flag);
        break;
      case ALE_SCE:
        ed_add_scene_channel(drw_list,
                             ads,
                             static_cast<Scene *>(ale->key_data),
                             ycenter,
                             scale_factor,
                             action_flag);
        break;
      case ALE_OB:
        ed_add_ob_channel(drw_list,
                          ads,
                          static_cast<Ob *>(ale->key_data),
                          ycenter,
                          scale_factor,
                          action_flag);
        break;
      case ALE_ACT:
        ed_add_action_channel(drw_list,
                              adt,
                              static_cast<Action *>(ale->key_data),
                              ycenter,
                              scale_factor,
                              action_flag);
        break;
      case ALE_GROUP:
        ed_add_action_group_channel(drw_list,
                                    adt,
                                    static_cast<ActionGroup *>(ale->data),
                                    ycenter,
                                    scale_factor,
                                    action_flag);
        break;
      case ALE_FCURVE:
        ed_add_fcurve_channel(drw_list,
                              adt,
                              static_cast<FCurve *>(ale->key_data),
                              ycenter,
                              scale_factor,
                              action_flag);
        break;
      case ALE_PEN_CEL:
        ed_add_pen_cels_channel(drw_list,
                                ads,
                                static_cast<const PenLayer *>(ale->data),
                                ycenter,
                                scale_factor,
                                action_flag);
        break;
      case ALE_PEN_GROUP:
        ed_add_pen_layer_group_channel(
            drw_list,
            ads,
            static_cast<const PenLayerTreeGroup *>(ale->data),
            ycenter,
            scale_factor,
            action_flag);
        break;
      case ALE_PEN_DATA:
        ed_add_pen_datablock_channel(drw_list,
                                     ads,
                                     static_cast<const Pen *>(ale->data),
                                     ycenter,
                                     scale_factor,
                                     action_flag);
        break;
      case ALE_PENFRAME:
        ed_add_pen_layer_legacy_channel(drw_list,
                                        ads,
                                        static_cast<PenDataLayer *>(ale->data),
                                        ycenter,
                                        scale_factor,
                                        action_flag);
        break;
      case ALE_MASKLAY:
        ed_add_mask_layer_channel(drw_list,
                                  ads,
                                  static_cast<MaskLayer *>(ale->data),
                                  ycenter,
                                  scale_factor,
                                  action_flag);
        break;
    }
  }

  /* Drawing happens in here. */
  ed_channel_list_flush(drw_list, v2d);
  ed_channel_list_free(drw_list);
}

void drw_channel_strips(AnimCxt *ac, SpaceAction *saction, ARgn *rgn)
{
  List anim_data = {nullptr, nullptr};
  View2D *v2d = &rgn->v2d;

  /* build list of channels to draw */
  eAnimFilter_Flags filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE |
                              ANIMFILTER_LIST_CHANNELS);
  size_t items = anim_animdata_filter(
      ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  const int height = anim_ui_get_channels_total_height(v2d, items);
  const float pad_bottom = lib_list_is_empty(ac->markers) ? 0 : UI_MARKER_MARGIN_Y;
  v2d->tot.ymin = -(height + pad_bottom);

  /* Drw the manual frame ranges for actions in the background of the dopesheet.
   * The action editor has alrdy drawn the range for its action so it's not needed. */
  if (ac->datatype == ANIMCONT_DOPESHEET) {
    drw_channel_action_ranges(&anim_data, v2d);
  }

  /* Draw the background strips. */
  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  gpu_blend(GPU_BLEND_ALPHA);

  /* first backdrop strips */
  drw_backdrops(ac, anim_data, v2d, pos);

  gpu_blend(GPU_BLEND_NONE);

  /* black line marking 'current frame' for Time-Slide transform mode */
  if (saction->flag & SACTION_MOVING) {
    immUniformColor3f(0.0f, 0.0f, 0.0f);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex2f(pos, saction->timeslide, v2d->cur.ymin - EXTRA_SCROLL_PAD);
    immVertex2f(pos, saction->timeslide, v2d->cur.ymax);
    immEnd();
  }
  immUnbindProgram();

  draw_keyframes(ac, v2d, saction, anim_data);

  /* free tmp channels used for drawing */
  anim_animdata_freelist(&anim_data);
}


/* Timeline - Caches */
static bool timeline_cache_is_hidden_by_setting(const SpaceAction *saction, const PTCacheID *pid)
{
  switch (pid->type) {
    case PTCACHE_TYPE_SOFTBODY:
      if ((saction->cache_display & TIME_CACHE_SOFTBODY) == 0) {
        return true;
      }
      break;
    case PTCACHE_TYPE_PARTICLES:
      if ((saction->cache_display & TIME_CACHE_PARTICLES) == 0) {
        return true;
      }
      break;
    case PTCACHE_TYPE_CLOTH:
      if ((saction->cache_display & TIME_CACHE_CLOTH) == 0) {
        return true;
      }
      break;
    case PTCACHE_TYPE_SMOKE_DOMAIN:
    case PTCACHE_TYPE_SMOKE_HIGHRES:
      if ((saction->cache_display & TIME_CACHE_SMOKE) == 0) {
        return true;
      }
      break;
    case PTCACHE_TYPE_DYNAMICPAINT:
      if ((saction->cache_display & TIME_CACHE_DYNAMICPAINT) == 0) {
        return true;
      }
      break;
    case PTCACHE_TYPE_RIGIDBODY:
      if ((saction->cache_display & TIME_CACHE_RIGIDBODY) == 0) {
        return true;
      }
      break;
  }
  return false;
}

static void timeline_cache_color_get(PTCacheId *pid, float color[4])
{
  switch (pid->type) {
    case PTCACHE_TYPE_SOFTBODY:
      color[0] = 1.0;
      color[1] = 0.4;
      color[2] = 0.02;
      color[3] = 0.1;
      break;
    case PTCACHE_TYPE_PARTICLES:
      color[0] = 1.0;
      color[1] = 0.1;
      color[2] = 0.02;
      color[3] = 0.1;
      break;
    case PTCACHE_TYPE_CLOTH:
      color[0] = 0.1;
      color[1] = 0.1;
      color[2] = 0.75;
      color[3] = 0.1;
      break;
    case PTCACHE_TYPE_SMOKE_DOMAIN:
    case PTCACHE_TYPE_SMOKE_HIGHRES:
      color[0] = 0.2;
      color[1] = 0.2;
      color[2] = 0.2;
      color[3] = 0.1;
      break;
    case PTCACHE_TYPE_DYNAMICPAINT:
      color[0] = 1.0;
      color[1] = 0.1;
      color[2] = 0.75;
      color[3] = 0.1;
      break;
    case PTCACHE_TYPE_RIGIDBODY:
      color[0] = 1.0;
      color[1] = 0.6;
      color[2] = 0.0;
      color[3] = 0.1;
      break;
    default:
      color[0] = 1.0;
      color[1] = 0.0;
      color[2] = 1.0;
      color[3] = 0.1;
      lib_assert(0);
      break;
  }
}

static void timeline_cache_mod_color_based_on_state(PointCache *cache,
                                                    float color[4],
                                                    float color_state[4])
{
  if (cache->flag & PTCACHE_BAKED) {
    color[3] = color_state[3] = 1.0f;
  }
  else if (cache->flag & PTCACHE_OUTDATED) {
    color[3] = color_state[3] = 0.7f;
    mul_v3_fl(color_state, 0.5f);
  }
  else {
    color[3] = color_state[3] = 0.7f;
  }
}

static bool timeline_cache_find_next_cached_segment(PointCache *cache,
                                                    int search_start_frame,
                                                    int *r_segment_start,
                                                    int *r_segment_end)
{
  int offset = cache->startframe;
  int current = search_start_frame;

  /* Find segment start frame. */
  while (true) {
    if (current > cache->endframe) {
      return false;
    }
    if (cache->cached_frames[current - offset]) {
      *r_segment_start = current;
      break;
    }
    current++;
  }

  /* Find segment end frame. */
  while (true) {
    if (current > cache->endframe) {
      *r_segment_end = current - 1;
      return true;
    }
    if (!cache->cached_frames[current - offset]) {
      *r_segment_end = current - 1;
      return true;
    }
    current++;
  }
}

static uint timeline_cache_segments_count(PointCache *cache)
{
  uint count = 0;

  int current = cache->startframe;
  int segment_start;
  int segment_end;
  while (timeline_cache_find_next_cached_segment(cache, current, &segment_start, &segment_end)) {
    count++;
    current = segment_end + 1;
  }

  return count;
}

static void timeline_cache_drw_cached_segments(PointCache *cache, uint pos_id)
{
  uint segments_count = timeline_cache_segments_count(cache);
  if (segments_count == 0) {
    return;
  }

  immBeginAtMost(GPU_PRIM_TRIS, segments_count * 6);

  int current = cache->startframe;
  int segment_start;
  int segment_end;
  while (timeline_cache_find_next_cached_segment(cache, current, &segment_start, &segment_end)) {
    immRectf_fast(pos_id, segment_start, 0, segment_end + 1.0f, 1.0f);
    current = segment_end + 1;
  }

  immEnd();
}

static void timeline_cache_drw_single(PTCacheId *pid, float y_offset, float height, uint pos_id)
{
  gpu_matrix_push();
  gpu_matrix_translate_2f(0.0, float(V2D_SCROLL_HANDLE_HEIGHT) + y_offset);
  gpu_matrix_scale_2f(1.0, height);

  dune::ColorTheme4f color;
  timeline_cache_color_get(pid, color);

  /* Mix in the background color to tone it down a bit. */
  dune::ColorTheme4f background;
  ui_GetThemeColor4fv(TH_BACK, background);

  interp_v3_v3v3(color, color, background, 0.6f);

  /* Highlight the frame range of the simulation. */
  immUniform4fv("color1", color);
  immUniform4fv("color2", color);
  immRectf(pos_id, float(pid->cache->startframe), 0.0, float(pid->cache->endframe), 1.0);

  /* Now show the cached frames on top. */
  dune::ColorTheme4f color_state;
  copy_v4_v4(color_state, color);

  timeline_cache_mod_color_based_on_state(pid->cache, color, color_state);

  immUniform4fv("color1", color);
  immUniform4fv("color2", color_state);

  timeline_cache_drw_cached_segments(pid->cache, pos_id);

  gpu_matrix_pop();
}

struct SimRange {
  dune::IndexRange frames;
  dune::bake::CacheStatus status;
};

static void timeline_cache_drw_sim_nodes(
    const dune::Span<SimRange> sim_ranges,
    const bool all_sims_baked,
    float *y_offset,
    const float line_height,
    const uint pos_id)
{
  if (sim_ranges.is_empty()) {
    return;
  }

  bool has_bake = false;

  for (const SimRange &sim_range : sim_ranges) {
    switch (sim_range.status) {
      case dune::bake::CacheStatus::Invalid:
      case dune::bake::CacheStatus::Valid:
        break;
      case dune::bake::CacheStatus::Baked:
        has_bake = true;
        break;
    }
  }

  dune::Set<int> status_change_frames_set;
  for (const SimulationRange &sim_range : simulation_ranges) {
    status_change_frames_set.add(sim_range.frames.first());
    status_change_frames_set.add(sim_range.frames.one_after_last());
  }
  dune::Vector<int> status_change_frames;
  status_change_frames.extend(status_change_frames_set.begin(), status_change_frames_set.end());
  std::sort(status_change_frames.begin(), status_change_frames.end());
  const dune::OffsetIndices<int> frame_ranges = status_change_frames.as_span();

  gpu_matrix_push();
  gpu_matrix_translate_2f(0.0, float(V2D_SCROLL_HANDLE_HEIGHT) + *y_offset);
  gpu_matrix_scale_2f(1.0, line_height);

  dune::ColorTheme4f base_color;
  ui_GetThemeColor4fv(TH_SIMULATED_FRAMES, base_color);
  dune::ColorTheme4f invalid_color = base_color;
  mul_v3_fl(invalid_color, 0.5f);
  invalid_color.a *= 0.7f;
  dune::ColorTheme4f valid_color = base_color;
  valid_color.a *= 0.7f;
  dune::ColorTheme4f baked_color = base_color;

  float max_used_height = 1.0f;
  for (const int range_i : frame_ranges.index_range()) {
    const dune::IndexRange frame_range = frame_ranges[range_i];
    const int start_frame = frame_range.first();
    const int end_frame = frame_range.last();

    bool has_bake_at_frame = false;
    bool has_valid_at_frame = false;
    bool has_invalid_at_frame = false;
    for (const SimulationRange &sim_range : simulation_ranges) {
      if (sim_range.frames.contains(start_frame)) {
        switch (sim_range.status) {
          case dune::bake::CacheStatus::Invalid:
            has_invalid_at_frame = true;
            break;
          case dune::bake::CacheStatus::Valid:
            has_valid_at_frame = true;
            break;
          case dune::bake::CacheStatus::Baked:
            has_bake_at_frame = true;
            break;
        }
      }
    }
    if (!(has_bake_at_frame || has_valid_at_frame || has_invalid_at_frame)) {
      continue;
    }

    if (all_sims_baked) {
      immUniform4fv("color1", baked_color);
      immUniform4fv("color2", baked_color);
      immBeginAtMost(GPU_PRIM_TRIS, 6);
      immRectf_fast(pos_id, start_frame, 0, end_frame + 1.0f, 1.0f);
      immEnd();
    }
    else {
      if (has_valid_at_frame || has_invalid_at_frame) {
        immUniform4fv("color1", valid_color);
        immUniform4fv("color2", has_invalid_at_frame ? invalid_color : valid_color);
        immBeginAtMost(GPU_PRIM_TRIS, 6);
        const float top = has_bake ? 2.0f : 1.0f;
        immRectf_fast(pos_id, start_frame, 0.0f, end_frame + 1.0f, top);
        immEnd();
        max_used_height = top;
      }
      if (has_bake_at_frame) {
        immUniform4fv("color1", baked_color);
        immUniform4fv("color2", baked_color);
        immBeginAtMost(GPU_PRIM_TRIS, 6);
        immRectf_fast(pos_id, start_frame, 0, end_frame + 1.0f, 1.0f);
        immEnd();
      }
    }
  }
  gpu_matrix_pop();

  *y_offset += max_used_height * 2;
}

void timeline_drw_cache(const SpaceAction *saction, const Ob *ob, const Scene *scene)
{
  if ((saction->cache_display & TIME_CACHE_DISPLAY) == 0 || ob == nullptr) {
    return;
  }

  List pidlist;
  dune_ptcache_ids_from_ob(&pidlist, const_cast<Ob *>(ob), const_cast<Scene *>(scene), 0);

  uint pos_id = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_DIAG_STRIPES);

  gpu_blend(GPU_BLEND_ALPHA);

  /* Iter over point-caches on the active ob, and draw each one's range. */
  float y_offset = 0.0f;
  const float cache_drw_height = 4.0f * UI_SCALE_FAC * U.pixelsize;

  immUniform1i("size1", cache_drw_height * 2.0f);
  immUniform1i("size2", cache_drw_height);

  LIST_FOREACH (PTCacheId *, pid, &pidlist) {
    if (timeline_cache_is_hidden_by_setting(saction, pid)) {
      continue;
    }

    if (pid->cache->cached_frames == nullptr) {
      continue;
    }

    timeline_cache_drw_single(pid, y_offset, cache_drw_height, pos_id);

    y_offset += cache_drw_height;
  }
  if (saction->cache_display & TIME_CACHE_SIM_NODES) {
    dune::Vector<SimRange> sim_ranges;
    bool all_sims_baked = true;
    LIST_FOREACH (ModData *, md, &ob->mods) {
      if (md->type != eModTypeNodes) {
        continue;
      }
      const NodesModData *nmd = reinterpret_cast<NodesModData *>(md);
      if (nmd->node_group == nullptr) {
        continue;
      }
      if (!nmd->runtime->cache) {
        continue;
      }
      if ((nmd->node_group->runtime->runtime_flag & NTREE_RUNTIME_FLAG_HAS_SIMULATION_ZONE) == 0) {
        continue;
      }
      const dune::bake::ModCache &mod_cache = *nmd->runtime->cache;
      {
        std::lock_guard lock{modifier_cache.mutex};
        for (const std::unique_ptr<dune::bake::NodeCache> &node_cache_ptr :
             mod_cache.cache_by_id.values())
        {
          const dune::bake::NodeCache &node_cache = *node_cache_ptr;
          if (node_cache.frame_caches.is_empty()) {
            all_sims_baked = false;
            continue;
          }
          if (node_cache.cache_status !=dun::bake::CacheStatus::Baked) {
            all_sims_baked = false;
          }
          const int start_frame = node_cache.frame_caches.first()->frame.frame();
          const int end_frame = node_cache.frame_caches.last()->frame.frame();
          const dune::IndexRange frame_range{start_frame, end_frame - start_frame + 1};
          sim_ranges.append({frame_range, node_cache.cache_status});
        }
      }
    }
    timeline_cache_drw_sim_nodes(
        sim_ranges, all_sims_baked, &y_offset, cache_drw_height, pos_id);
  }

  gpu_blend(GPU_BLEND_NONE);
  immUnbindProgram();

  lib_freelist(&pidlist);
}
