#pragma once

/* Struct members on own line. */
/* clang-format off */

/* SpaceClip Struct */
#define _TYPES_DEFAULT_MaskSpaceInfo \
  { \
    .drw_flag = 0, \
    .drw_type = MASK_DT_OUTLINE, \
    .overlay_mode = MASK_OVERLAY_ALPHACHANNEL, \
  }

#define _TYPES_DEFAULT_SpaceClip \
  { \
    .spacetype = SPACE_CLIP, \
    .link_flag = 0, \
    .xof = 0, \
    .yof = 0, \
    .xlockof = 0, \
    .ylockof = 0, \
    .zoom = 1.0f, \
    .user = _TYPES_DEFAULT_MovieClipUser, \
    .scopes = _TYPES_DEFAULT_MovieClipScopes, \
    .flag = SC_SHOW_MARKER_PATTERN | SC_SHOW_TRACK_PATH | SC_SHOW_GRAPH_TRACKS_MOTION | \
                 SC_SHOW_GRAPH_FRAMES | SC_SHOW_ANNOTATION, \
    .mode = SC_MODE_TRACKING, \
    .view = SC_VIEW_CLIP, \
    .path_length = 20, \
    .loc = {0, 0}, \
    .scale = 0, \
    .angle = 0, \
    .stabmat = _TYPES_DEFAULT_UNIT_M4, \
    .unistabmat = _TYPES_DEFAULT_UNIT_M4, \
    .postproc_flag = 0, \
    .pen_src = SC_PEN_SRC_CLIP, \
    .around = V3D_AROUND_CENTER_MEDIAN, \
    .cursor = {0, 0}, \
    .mask_info = _TYPES_DEFAULT_MaskSpaceInfo, \
  }
