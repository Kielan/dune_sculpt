#pragma once

/* Struct members on own line. */
/* clang-format off */

/* -------------------------------------------------------------------- */
/* Asset Struct */

#define _TYPES_DEFAULT_AssetMetaData \
  { \
    0 \
  }

#define _TYPES_DEFAULT_AssetLibRef \
  { \
    .type = ASSET_LIB_LOCAL, \
    /* Not needed really (should be ignored for ASSET_LIB_LOCAL), but helps debugging. */ \
    .custom_lib_index = -1, \
  }

/* clang-format on */
