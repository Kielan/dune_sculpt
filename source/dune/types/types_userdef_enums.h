#pragma once

/* UserDef.dupflag
 *
 * The flag tells dune_ob_dup() whether to copy data linked to the ob,
 * or to ref the existing data.
 * U.dupflag should be used for default ops or you can construct a flag as Python does.
 * If eDupIdFlags is 0 then no data will be copied (linked dup). */
typedef enum eDupIdFlags {
  USER_DUP_MESH = (1 << 0),
  USER_DUP_CURVE = (1 << 1),
  USER_DUP_SURF = (1 << 2),
  USER_DUP_FONT = (1 << 3),
  USER_DUP_MBALL = (1 << 4),
  USER_DUP_LAMP = (1 << 5),
  /* USER_DUP_FCURVE = (1 << 6), */ /* UNUSED, keep bc we may implement. */
  USER_DUP_MAT = (1 << 7),
  /* USER_DUP_TEX = (1 << 8), */ /* UNUSED, keep bc we may implement. */
  USER_DUP_ARM = (1 << 9),
  USER_DUP_ACT = (1 << 10),
  USER_DUP_PSYS = (1 << 11),
  USER_DUP_LIGHTPROBE = (1 << 12),
  USER_DUP_PEN = (1 << 13),
  USER_DUP_CURVES = (1 << 14),
  USER_DUP_POINTCLOUD = (1 << 15),
  USER_DUP_VOLUME = (1 << 16),
  USER_DUP_LATTICE = (1 << 17),
  USER_DUP_CAMERA = (1 << 18),
  USER_DUP_SPEAKER = (1 << 19),
  USER_DUP_OBDATA = (~0) & ((1 << 24) - 1),
  /* Those are not exposed as user prefs, only used internally. */
  USER_DUP_OB = (1 << 24),
  /* USER_DUP_COLLECTION = (1 << 25), */ /* UNUSED, keep bc we may implement. */
  /* Dup (and hence make local) linked data. */
  USER_DUP_LINKED_ID = (1 << 30),
} eDupIdFlags;
