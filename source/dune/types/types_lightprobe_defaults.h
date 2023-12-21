#pragma once

/* Struct members on own line. */
/* clang-format off */

/* LightProbe Struct */
#define _TYPES_DEFAULT_LightProbe \
  { \
    .grid_resolution_x = 4, \
    .grid_resolution_y = 4, \
    .grid_resolution_z = 4, \
    .distinf = 2.5f, \
    .distpar = 2.5f, \
    .falloff = 0.2f, \
    .clipsta = 0.8f, \
    .clipend = 40.0f, \
    .vis_bias = 1.0f, \
    .vis_blur = 0.2f, \
    .intensity = 1.0f, \
    .flag = LIGHTPROBE_FLAG_SHOW_INFLUENCE, \
  }

/* clang-format on */
