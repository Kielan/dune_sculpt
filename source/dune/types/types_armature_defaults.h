#pragma once

/* Struct members on own line. */
/* clang-format off */

/* -------------------------------------------------------------------- */
/** Armature Struct **/

#define _TYPES_DEFAULT_Armature \
  { \
    .deformflag = ARM_DEF_VGROUP | ARM_DEF_ENVELOPE, \
    .flag = ARM_COL_CUSTOM,  /* custom bone-group colors */ \
    .layer = 1, \
    .drawtype = ARM_OCTA, \
  }
