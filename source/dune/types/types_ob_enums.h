/* Enums typedef's for use in public headers. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Ob.mode */
typedef enum eObMode {
  OB_MODE_OB = 0,
  OB_MODE_EDIT = 1 << 0,
  OB_MODE_SCULPT = 1 << 1,
  OB_MODE_VERT_PAINT = 1 << 2,
  OB_MODE_WEIGHT_PAINT = 1 << 3,
  OB_MODE_TEXTURE_PAINT = 1 << 4,
  OB_MODE_PARTICLE_EDIT = 1 << 5,
  OB_MODE_POSE = 1 << 6,
  OB_MODE_EDIT_PEN = 1 << 7,
  OB_MODE_PAINT_PEN = 1 << 8,
  OB_MODE_SCULPT_PEN = 1 << 9,
  OB_MODE_WEIGHT_PEN = 1 << 10,
  OB_MODE_VERT_PEN = 1 << 11,
  OB_MODE_SCULPT_CURVES = 1 << 12,
} eObMode;

/* Ob.dt, View3DShading.type */
typedef enum eDrwType {
  OB_BOUNDBOX = 1,
  OB_WIRE = 2,
  OB_SOLID = 3,
  OB_MATERIAL = 4,
  OB_TEXTURE = 5,
  OB_RENDER = 6,
} eDrwType;

/* Any mode where the brush system is used. */
#define OB_MODE_ALL_PAINT \
  (OB_MODE_SCULPT | OB_MODE_VERT_PAINT | OB_MODE_WEIGHT_PAINT | OB_MODE_TEXTURE_PAINT)

#define OB_MODE_ALL_PAINT_PEN \
  (OB_MODE_PAINT_PEN | OB_MODE_SCULPT_PEN | OB_MODE_WEIGHT_PEN | \
   OB_MODE_VERT_PEN)

/* Any mode that uses Ob.sculpt. */
#define OB_MODE_ALL_SCULPT (OB_MODE_SCULPT | OB_MODE_VERT_PAINT | OB_MODE_WEIGHT_PAINT)

/* Any mode that uses weightpaint. */
#define OB_MODE_ALL_WEIGHT_PAINT (OB_MODE_WEIGHT_PAINT | OB_MODE_WEIGHT_PEN)

/* Any mode that has data or for Pen modes, we need to free when switching modes,
 * see: ed_ob_mode_generic_exit */
#define OB_MODE_ALL_MODE_DATA \
  (OB_MODE_EDIT | OB_MODE_VERT_PAINT | OB_MODE_WEIGHT_PAINT | OB_MODE_SCULPT | OB_MODE_POSE | \
   OB_MODE_PAINT_PEN | OB_MODE_EDIT_PEN | OB_MODE_SCULPT_PEN | \
   OB_MODE_WEIGHT_PEN | OB_MODE_VERT_PEN | OB_MODE_SCULPT_CURVES)

#ifdef __cplusplus
}
#endif
