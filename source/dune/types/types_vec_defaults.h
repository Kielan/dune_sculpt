#pragma once

/* Struct members on own line. */
/* clang-format off */

/* -------------------------------------------------------------------- */
/** Generic Defaults **/

/** See #unit_m4. */
#define _TYPES_DEFAULT_UNIT_M4 \
  { \
    {1, 0, 0, 0}, \
    {0, 1, 0, 0}, \
    {0, 0, 1, 0}, \
    {0, 0, 0, 1}, \
  }

#define _TYPES_DEFAULT_UNIT_M3 \
  { \
    {1, 0, 0}, \
    {0, 1, 0}, \
    {0, 0, 1}, \
  }

/** See #unit_qt. */
#define _TYPES_DEFAULT_UNIT_QT \
  {1, 0, 0, 0}

/* clang-format on */
