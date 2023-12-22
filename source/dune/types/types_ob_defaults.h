#pragma once

#include "types_vec_defaults.h"

/* Struct members on own line. */
/* clang-format off */

/* Ob Struct */
#define _TYPES_DEFAULT_Ob \
  { \
    /* Type is not very meaningful as a default, normally changed. */ \
    .type = OB_EMPTY, \
    .color = {1, 1, 1, 1}, \
 \
    .constinv = _TYPES_DEFAULT_UNIT_M4, \
    .parentinv = _TYPES_DEFAULT_UNIT_M4, \
    .obmat = _TYPES_DEFAULT_UNIT_M4, \
 \
    .scale = {1, 1, 1}, \
    .dscale = {1, 1, 1}, \
    /* Obs should default to having Euler XYZ rotations, \
     * but rotations default to quaternions. */ \
    .rotmode = ROT_MODE_EUL, \
    /* See unit_axis_angle. */ \
    .rotAxis = {0, 1, 0}, \
    .rotAngle = 0, \
    .drotAxis = {0, 1, 0}, \
    .drotAngle = 0, \
    .quat = _TYPES_DEFAULT_UNIT_QT, \
    .dquat = _TYPES_DEFAULT_UNIT_QT, \
    .protectflag = OB_LOCK_ROT4D, \
 \
    .dt = OB_TEXTURE, \
 \
    .empty_drwtype = OB_PLAINAXES, \
    .empty_drwsize = 1.0, \
    .empty_img_depth = OB_EMPTY_IMG_DEPTH_DEFAULT, \
    .ima_ofs = {-0.5, -0.5}, \
 \
    .instance_faces_scale = 1, \
    .col_group = 0x01,  \
    .col_mask = 0xffff, \
    .preview = NULL, \
    .dup_visibility_flag = OB_DUP_FLAG_VIEWPORT | OB_DUP_FLAG_RENDER, \
    .pc_ids = {NULL, NULL}, \
    .lineart = { .crease_threshold = DEG2RAD(140.0f) }, \
  }

/* clang-format on */
