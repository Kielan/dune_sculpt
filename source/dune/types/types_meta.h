#pragma once

#include "types_id.h"
#include "types_defs.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AnimData;
struct BoundBox;
struct Ipo;
struct Material;

typedef struct MetaElem {
  struct MetaElem *next, *prev;

  /* Bound Box of MetaElem. */
  struct BoundBox *bb;

  short type, flag;
  char _pad[4];
  /* Position of center of MetaElem. */
  float x, y, z;
  /* Rotation of MetaElem (MUST be kept normalized). */
  float quat[4];
  /* Dimension params, used for some types like cubes. */
  float expx;
  float expy;
  float expz;
  /* Radius of the meta element. */
  float rad;
  /* Tmp field, used only while processing. */
  float rad2;
  /* Stiffness, how much of the element to fill. */
  float s;
  /* Old, only used for backwards compat. use dimensions now. */
  float len;

  /* Matrix and inverted matrix. */
  float *mat, *imat;
} MetaElem;

typedef struct MetaBall {
  Id id;
  struct AnimData *adt;

  List elems;
  List disp;
  /* Not saved in files, note we use pointer for editmode check. */
  List *editelems;
  /* Old anim sys, deprecated for 2.5. */
  struct Ipo *ipo TYPES_DEPRECATED;

  /* material of the mother ball will define the material used of all others */
  struct Material **mat;

  /* Flag is enum for updates, flag2 is bitflags for settings. */
  char flag, flag2;
  short totcol;
  /* Used to store MB_AUTOSPACE. */
  char texflag;
  char _pad[2];

  /* Id data is older than edit-mode data (TODO: move to edit-mode struct).
   * Set Main.is_memfile_undo_flush_needed when enabling. */
  char needs_flush_to_id;

  /* texture space, copied as one block in editobject.c */
  float loc[3];
  float size[3];
  float rot[3];

  /* Display and render res. */
  float wiresize, rendersize;

  /* bias elements to have an offset volume.
   * mother ball changes will effect other objects thresholds,
   * but these may also have their own thresh as an offset */
  float thresh;

  /* used in editmode */
  // List edit_elems;
  MetaElem *lastelem;

  void *batch_cache;
} MetaBall;

/* METABALL */

/* texflag */
#define MB_AUTOSPACE 1

/* mb->flag */
#define MB_UPDATE_ALWAYS 0
#define MB_UPDATE_HALFRES 1
#define MB_UPDATE_FAST 2
#define MB_UPDATE_NEVER 3

/* mb->flag2 */
#define MB_DS_EXPAND (1 << 0)

/* ml->type */
#define MB_BALL 0
#define MB_TUBEX 1 /* deprecated. */
#define MB_TUBEY 2 /* deprecated. */
#define MB_TUBEZ 3 /* deprecated. */
#define MB_TUBE 4
#define MB_PLANE 5
#define MB_ELIPSOID 6
#define MB_CUBE 7

#define MB_TYPE_SIZE_SQUARED(type) (type == MB_ELIPSOID)

/* ml->flag */
#define MB_NEGATIVE 2
#define MB_HIDE 8
#define MB_SCALE_RAD 16

#ifdef __cplusplus
}
#endif
