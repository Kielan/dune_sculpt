#pragma once

#include "types_ob_enums.h"

#include "types_customdata.h"
#include "types_defs.h"
#include "types_lineart.h"
#include "types_list.h"

#include "types_id.h"
#include "types_action.h" /* AnimVizSettings */
#include "types_customdata.h"
#include "types_defs.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AnimData;
struct BoundBox;
struct Curve;
struct FluidsimSettings;
struct GeometrySet;
struct Ipo;
struct Material;
struct Mesh;
struct Ob;
struct PartDeflect;
struct Path;
struct RigidBodyOb;
struct SculptSession;
struct SoftBody;
struct PenData;

/* Vert Groups - Name Info */
typedef struct DeformGroup {
  struct DeformGroup *next, *prev;
  /* MAX_VGROUP_NAME. */
  char name[64];
  /* need this flag for locking weights */
  char flag, _pad0[7];
} DeformGroup;

/* Face Maps. */
typedef struct FaceMap {
  struct FaceMap *next, *prev;
  /* MAX_VGROUP_NAME. */
  char name[64];
  char flag;
  char _pad0[7];
} FaceMap;

#define MAX_VGROUP_NAME 64

/* DeformGroup->flag */
#define DG_LOCK_WEIGHT 1

/* The following illustrates the orientation of the
 * bounding box in local space
 *
 * Z  Y
 * | /
 * |/
 * .-----X
 *     2----------6
 *    /|         /|
 *   / |        / |
 *  1----------5  |
 *  |  |       |  |
 *  |  3-------|--7
 *  | /        | /
 *  |/         |/
 *  0----------4
 */
typedef struct BoundBox {
  float vec[8][3];
  int flag;
  char _pad0[4];
} BoundBox;

/* BoundBox.flag */
enum {
  BOUNDBOX_DISABLED = (1 << 0),
  BOUNDBOX_DIRTY = (1 << 1),
};

struct CustomData_MeshMasks;

/* Not saved in file! */
typedef struct ObRuntime {
  /* The custom data layer mask that was last used
   * to calculate data_eval and mesh_deform_eval. */
  CustomData_MeshMasks last_data_mask;

  /* Did last mod stack generation need mapping support? */
  char last_need_mapping;

  /* Opaque data reserved for management of objects in collection cxt.
   *  E.g. used currently to check for potential duplicates of objects in a collection, after
   * remapping process. */
  char collection_management;

  char _pad0[2];

  /* Only used for drawing the parent/child help-line. */
  float parent_display_origin[3];

  /* Sel id of this ob. It might differ between an evaluated and its original object,
   * when the ob is being instanced.  */
  int sel_id;
  char _pad1[3];

  /* Denotes whether the eval data is owned by this ob or is ref and owned by
   * somebody else. */
  char is_data_eval_owned;

  /* Start time of the mode transfer overlay anim. */
  double overlay_mode_transfer_start_time;

  /* Axis aligned bound-box (in local-space). */
  struct BoundBox *bb;

  /* Original data ptr, before ob->data was changed to point
   * to data_eval.
   * Is assigned by graph's copy-on-write eval. */
  struct Id *data_orig;
  /* Ob data struct created during ob eval. It has all mods applied.
   * The type is determined by the type of the original ob. */
  struct Id *data_eval;

  /* Obs can eval to a geometry set instead of a single Id. In those cases, the evaluated
   * geometry set will be stored here. An Id of the correct type is still stored in data_eval.
   * geometry_set_eval might ref the Id pointed to by data_eval as well, but does not own
   * the data. */
  struct GeometrySet *geometry_set_eval;

  /* Mesh struct created during ob eval.
   * It has deformation only mods applied on it. */
  struct Mesh *mesh_deform_eval;

  /* Eval'd mesh cage in edit mode. */
  struct Mesh *meshedit_eval_cage;

  /* Cached cage bounding box of `editmesh_eval_cage` for selection. */
  struct BoundBox *meshedit_bb_cage;

  /* Original pen PenData ptr, before object->data was changed to point
   * to pd_eval.
   * Is assigned by graph's copy-on-write evaluation. */
  struct PenData *pd_orig;
  /* PenData struct created during object evaluation.
   * It has all mods applied. */
  struct PenData *pd_eval;

  /* This is a mesh representation of corresponding object.
   * It created when Python calls `object.to_mesh()`.  */
  struct Mesh *ob_as_tmp_mesh;

  /* This is a curve representation of corresponding object.
   * It created when Python calls `object.to_curve()`. */
  struct Curve *ob_as_tmp_curve;

  /* Runtime eval curve-specific data, not stored in the file. */
  struct CurveCache *curve_cache;

  unsigned short local_collections_bits;
  short _pad2[3];

  float (*crazyspace_deform_imats)[3][3];
  float (*crazyspace_deform_cos)[3];
  int crazyspace_num_verts;

  int _pad3[3];
} ObRuntime;

typedef struct ObLineArt {
  short usage;
  short flags;

  /* if OB_LRT_OWN_CREASE is set */
  float crease_threshold;
} ObLineArt;

/* warning while the values seem to be flags, they aren't treated as flags. */
enum eObLineArtUsage {
  OB_LRT_INHERIT = 0,
  OB_LRT_INCLUDE = (1 << 0),
  OB_LRT_OCCLUSION_ONLY = (1 << 1),
  OBJECT_LRT_EXCLUDE = (1 << 2),
  OB_LRT_INTERSECTION_ONLY = (1 << 3),
  OB_LRT_NO_INTERSECTION = (1 << 4),
};

enum eObLineArtFlags {
  OB_LRT_OWN_CREASE = (1 << 0),
};

typedef struct Ob {
  Id id;
  /* Anim data (must be immediately after id for utilities to use it). */
  struct AnimData *adt;
  /* Runtime (must be immediately after id for utilities to use it). */
  struct DrwDataList drwdata;

  struct SculptSession *sculpt;

  short type, partype;
  /* Can be vertexnrs. */
  int par1, par2, par3;
  /* String describing subob info, MAX_ID_NAME-2. */
  char parsubstr[64];
  struct Ob *parent, *track;
  /* Proxy ptr are deprecated, only kept for conversion to liboverrides. */
  struct Ob *proxy TYPES_DEPRECATED;
  struct Object *proxy_group TYPES_DEPRECATED;
  struct Object *proxy_from TYPES_DEPRECATED;
  /* Old animation system, deprecated for 2.5. */
  struct Ipo *ipo TYPES_DEPRECATED;
  /* struct Path *path; */
  struct Action *action TYPES_DEPRECATED; /* XXX deprecated... old animation system */
  struct Action *poselib;
  /* Pose data, armature obs only. */
  struct Pose *pose;
  /* Ptr to objects data - an 'Id' or NULL. */
  void *data;

  /* Pen data. */
  struct PenData *pd
      TYPES_DEPRECATED; /* XXX deprecated... replaced by Pen object, keep for readfile */

  /* Settings for visualization of object-transform animation. */
  AnimVizSettings avs;
  /* Motion path cache for this object. */
  MotionPath *mpath;
  void *_pad0;

  List constraintChannels TYPES_DEPRECATED; /* XXX deprecated... old animation system */
  List effect TYPES_DEPRECATED;             /* XXX deprecated... keep for readfile */
  List defbase TYPES_DEPRECATED;            /* Only for versioning, moved to object data. */
  /* List of ModData structures. */
  List mods;
  /* List of PenModData structures. */
  List pen_mods;
  /* List of facemaps. */
  List fmaps;
  /* List of viewport effects. Actually only used by grease pencil. */
  List shader_fx;

  /* Local ob mode. */
  int mode;
  int restore_mode;

  /* materials */
  /* Material slots. */
  struct Material **mat;
  /* A bool field, with each byte 1 if corresponding material is linked to object. */
  char *matbits;
  /* Copy of mesh, curve & meta struct member of same name (keep in sync). */
  int totcol;
  /* Currently sel material in the UI. */
  int actcol;

  /* rot en drot have to be together! (transform('r' en 's')) */
  float loc[3], dloc[3];
  /* Scale (can be negative). */
  float scale[3];
  /* DEPRECATED, 2.60 and older only. */
  float dsize[3] TYPES_DEPRECATED;
  /* Ack!, changing. */
  float dscale[3];
  /* Euler rotation. */
  float rot[3], drot[3];
  /* Quaternion rotation. */
  float quat[4], dquat[4];
  /* Axis angle rotation - axis part. */
  float rotAxis[3], drotAxis[3];
  /* Axis angle rotation - angle part. */
  float rotAngle, drotAngle;
  /* Final world-space matrix with constraints & animsys applied. */
  float obmat[4][4];
  /* Inverse result of parent, so that object doesn't 'stick' to parent. */
  float parentinv[4][4];
  /* Inverse result of constraints.
   * doesn't include effect of parent or object local transform. */
  float constinv[4][4];
  /* Inverse matrix of 'obmat' for any other use than rendering!
   * note this isn't assured to be valid as with 'obmat',
   * before using this value you should do: `invert_m4_m4(ob->imat, ob->obmat) */
  float imat[4][4];

  /* Copy of Base's layer in the scene. */
  unsigned int lay TYPES_DEPRECATED;

  /* Copy of Base. */
  short flag;
  /* Deprecated, use 'matbits'. */
  short colbits TYPES_DEPRECATED;

  /* Transformation settings and transform locks. */
  short transflag, protectflag;
  short trackflag, upflag;
  /* Used for DopeSheet filtering settings (expanded/collapsed). */
  short nlaflag;

  char _pad1;
  char duplicator_visibility_flag;

  /* Graph */
  /* Used by graph, flushed from base. */
  short base_flag;
  /* Used by viewport, synced from base. */
  unsigned short base_local_view_bits;

  /* Collision mask settings */
  unsigned short col_group, col_mask;

  /* Rotation mode - uses defines set out in types_action.h for PoseChannel rotations.... */
  short rotmode;

  /* Bounding box use for drawing. */
  char boundtype;
  /* Bounding box type used for collision. */
  char collision_boundtype;

  /* Viewport drw extra settings. */
  short dtx;
  /* Viewport drw type. */
  char dt;
  char empty_drwtype;
  float empty_drwsize;
  /* Dupface scale. */
  float instance_faces_scale;

  /* Custom index, for renderpasses. */
  short index;
  /* Current deformation group. Index starts at 1. */
  unsigned short actdef TYPES_DEPRECATED;
  /* Current face map. Index starts at 1. */
  unsigned short actfmap;
  char _pad2[2];
  /* Ob color (in most cases the material color is used for drawing). */
  float color[4];

  /* Softbody settings. */
  short softflag;

  /* For restricting view, select, render etc. accessible in outliner. */
  short visibility_flag;

  /* Current shape key for menu or pinned. */
  short shapenr;
  /* Flag for pinning. */
  char shapeflag;

  char _pad3[1];

  /* Ob constraints. */
  List constraints;
  List nlastrips TYPES_DEPRECATED; /* deprecated... old anim system */
  List hooks TYPES_DEPRECATED;     /* deprecated... old anim system */
  /* Particle sys. */
  List particlesys;

  /* Particle deflector/attractor/collision data. */
  struct PartDeflect *pd;
  /* If exists, saved in file. */
  struct SoftBody *soft;
  /* Ob duplicator for group. */
  struct Collection *instance_collection;

  /* If fluidsim enabled, store additional settings. */
  struct FluidsimSettings *fluidsimSettings
      TYPES_DEPRECATED; /* deprecated... replaced by mantaflow, keep for readfile */

  List pc_ids;

  /* Settings for Bullet rigid body. */
  struct RigidBodyOb *rigidbody_ob;
  /* Settings for Bullet constraint. */
  struct RigidBodyCon *rigidbody_constraint;

  /* Offset for img empties. */
  float ima_ofs[2];
  /* Must be non-null when object is an empty image. */
  ImgUser *iuser;
  char empty_img_visibility_flag;
  char empty_img_depth;
  char empty_img_flag;
  char _pad8[5];

  struct PreviewImg *preview;

  ObLineArt lineart;

  /* Runtime evaluation data (keep last). */
  void *_pad9;
  ObRuntime runtime;
} Ob;

/* DEPRECATED: this is not used anymore bc hooks are now mods. */
typedef struct ObHook {
  struct ObHook *next, *prev;

  struct Ob *parent;
  /* Matrix making current transform unmodified. */
  float parentinv[4][4];
  /* Temp matrix while hooking. */
  float mat[4][4];
  /* Visualization of hook. */
  float cent[3];
  /* If not zero, falloff is distance where influence zero. */
  float falloff;

  /* MAX_NAME. */
  char name[64];

  int *indexar;
  /* Curindex is cache for fast lookup. */
  int totindex, curindex;
  /* Active is only first hook, for btn menu. */
  short type, active;
  float force;
} ObHook;

/* OB */

/* used many places, should be specialized. */
#define SEL 1

/* Ob.type */
enum {
  OB_EMPTY = 0,
  OB_MESH = 1,
  /* Curve object is still used but replaced by "Curves" for the future (see T95355). */
  OB_CURVES_LEGACY = 2,
  OB_SURF = 3,
  OB_FONT = 4,
  OB_MBALL = 5,

  OB_LAMP = 10,
  OB_CAMERA = 11,

  OB_SPEAKER = 12,
  OB_LIGHTPROBE = 13,

  OB_LATTICE = 22,

  OB_ARMATURE = 25,

  /* Pen ob used in 3D view but not used for annotation in 2D. */
  OB_PEN = 26,

  OB_CURVES = 27,

  OB_POINTCLOUD = 28,

  OB_VOLUME = 29,

  /* Keep last. */
  OB_TYPE_MAX,
};

/* check if the ob type supports materials */
#define OB_TYPE_SUPPORT_MATERIAL(_type) \
  (((_type) >= OB_MESH && (_type) <= OB_MBALL) || ((_type) >= OB_PEN && (_type) <= OB_VOLUME))
/* Does the object have some render-able geometry (unlike empties, cameras, etc.). */
#define OB_TYPE_IS_GEOMETRY(_type) \
  (ELEM(_type, \
        OB_MESH, \
        OB_SURF, \
        OB_FONT, \
        OB_MBALL, \
        OB_PEN, \
        OB_CURVES, \
        OB_POINTCLOUD, \
        OB_VOLUME))
#define OB_TYPE_SUPPORT_VGROUP(_type) (ELEM(_type, OB_MESH, OB_LATTICE, OB_PEN))
#define OB_TYPE_SUPPORT_EDITMODE(_type) \
  (ELEM(_type, \
        OB_MESH, \
        OB_FONT, \
        OB_CURVES_LEGACY, \
        OB_SURF, \
        OB_MBALL, \
        OB_LATTICE, \
        OB_ARMATURE, \
        OB_CURVES))
#define OB_TYPE_SUPPORT_PARVERT(_type) \
  (ELEM(_type, OB_MESH, OB_SURF, OB_CURVES_LEGACY, OB_LATTICE))

/* Matches OB_TYPE_SUPPORT_EDITMODE. */
#define OB_DATA_SUPPORT_EDITMODE(_type) (ELEM(_type, ID_ME, ID_CU_LEGACY, ID_MB, ID_LT, ID_AR))

/* is this Id type used as ob data */
#define OB_DATA_SUPPORT_ID(_id_type) \
  (ELEM(_id_type, \
        ID_ME, \
        ID_CU_LEGACY, \
        ID_MB, \
        ID_LA, \
        ID_SPK, \
        ID_LP, \
        ID_CA, \
        ID_LT, \
        ID_GD, \
        ID_AR, \
        ID_CV, \
        ID_PT, \
        ID_VO))

#define OB_DATA_SUPPORT_ID_CASE \
  ID_ME: \
  case ID_CU_LEGACY: \
  case ID_MB: \
  case ID_LA: \
  case ID_SPK: \
  case ID_LP: \
  case ID_CA: \
  case ID_LT: \
  case ID_GD: \
  case ID_AR: \
  case ID_CV: \
  case ID_PT: \
  case ID_VO

/* Ob.partype: first 4 bits: type. */
enum {
  PARTYPE = (1 << 4) - 1,
  PAROB = 0,
  PARSKEL = 4,
  PARVERT1 = 5,
  PARVERT3 = 6,
  PARBONE = 7,

};

/* Object.transflag (short) */
enum {
  OB_TRANSFORM_ADJUST_ROOT_PARENT_FOR_VIEW_LOCK = 1 << 0,
  OB_TRANSFLAG_UNUSED_1 = 1 << 1, /* cleared */
  OB_NEG_SCALE = 1 << 2,
  OB_TRANSFLAG_UNUSED_3 = 1 << 3, /* cleared */
  OB_DUPLIVERTS = 1 << 4,
  OB_DUPLIROT = 1 << 5,
  OB_TRANSFLAG_UNUSED_6 = 1 << 6, /* cleared */
  /* runtime, calculate derivedmesh for dup before it's used */
  OB_TRANSFLAG_UNUSED_7 = 1 << 7, /* dirty */
  OB_DUPCOLLECTION = 1 << 8,
  OB_DUPFACES = 1 << 9,
  OB_DUPFACES_SCALE = 1 << 10,
  OB_DUPPARTS = 1 << 11,
  OB_TRANSFLAG_UNUSED_12 = 1 << 12, /* cleared */
  /* runtime constraints disable */
  OB_NO_CONSTRAINTS = 1 << 13,

  OB_DUP = OB_DUPVERTS | OB_DUPCOLLECTION | OB_DUPFACES | OB_DUPPARTS,
};

/* Obtrackflag/Ob.upflag (short) */
enum {
  OB_POSX = 0,
  OB_POSY = 1,
  OB_POSZ = 2,
  OB_NEGX = 3,
  OB_NEGY = 4,
  OB_NEGZ = 5,
};

/* Ob.dtx drw type extra flags (short) */
enum {
  OB_DRAWBOUNDOX = 1 << 0,
  OB_AXIS = 1 << 1,
  OB_TEXSPACE = 1 << 2,
  OB_DRWNAME = 1 << 3,
  /* OB_DRWIMG = 1 << 4, */ /* UNUSED */
  /* for solid+wire display */
  OB_DRWWIRE = 1 << 5,
  /* For overdrawing. */
  OB_DRW_IN_FRONT = 1 << 6,
  /* Enable transparent draw. */
  OB_DRWTRANSP = 1 << 7,
  OB_DRW_ALL_EDGES = 1 << 8, /* only for meshes currently */
  OB_DRW_NO_SHADOW_CAST = 1 << 9,
  /* Enable lights for grease pencil. */
  OB_USE_PEN_LIGHTS = 1 << 10,
};

/* Ob.empty_drwtype: no flags */
enum {
  OB_ARROWS = 1,
  OB_PLAINAXES = 2,
  OB_CIRCLE = 3,
  OB_SINGLE_ARROW = 4,
  OB_CUBE = 5,
  OB_EMPTY_SPHERE = 6,
  OB_EMPTY_CONE = 7,
  OB_EMPTY_IMG = 8,
};

/* Pen add types.
 * TODO: doesn't need to be TYPES, local to `OBJECT_OT_pen_add`. */
enum {
  P_EMPTY = 0,
  P_STROKE = 1,
  P_MONKEY = 2,
  P_LRT_SCENE = 3,
  P_LRT_OB = 4,
  P_LRT_COLLECTION = 5,
};

/* Ob.boundtype */
enum {
  OB_BOUND_BOX = 0,
  OB_BOUND_SPHERE = 1,
  OB_BOUND_CYLINDER = 2,
  OB_BOUND_CONE = 3,
  // OB_BOUND_TRIANGLE_MESH = 4, /* UNUSED */
  // OB_BOUND_CONVEX_HULL = 5,   /* UNUSED */
  // OB_BOUND_DYN_MESH = 6,      /* UNUSED */
  OB_BOUND_CAPSULE = 7,
};

/* BASE */
/* Base.flag_legacy */
enum {
  BA_WAS_SEL = (1 << 1),
  /* NOTE: BA_HAS_RECALC_DATA can be re-used later if freed in readfile.c. */
  // BA_HAS_RECALC_OB = (1 << 2),  /* DEPRECATED */
  // BA_HAS_RECALC_DATA =  (1 << 3),  /* DEPRECATED */
  /* DEPRECATED, was runtime only, but was reusing an older flag. */
  BA_SNAP_FIX_DEPS_FIASCO = (1 << 2),
};

/* This was used as a proper setting in past, so nullify before using */
#define BA_TMP_TAG (1 << 5)

/* Even if this is tagged for transform, this flag means it's being locked in place.
 * Use for SCE_XFORM_SKIP_CHILDREN. */
#define BA_TRANSFORM_LOCKED_IN_PLACE (1 << 7)

#define BA_TRANSFORM_CHILD (1 << 8)   /* child of a transformed object */
#define BA_TRANSFORM_PARENT (1 << 13) /* parent of a transformed object */

#define OB_FROMDUPLI (1 << 9)
#define OB_DONE (1 << 10) /* unknown state, clear before use */
#ifdef TYPES_DEPRECATED_ALLOW
#  define OB_FLAG_UNUSED_11 (1 << 11) /* cleared */
#  define OB_FLAG_UNUSED_12 (1 << 12) /* cleared */
#endif

/* Ob.visibility_flag */
enum {
  OB_HIDE_VIEWPORT = 1 << 0,
  OB_HIDE_SEL = 1 << 1,
  OB_HIDE_RENDER = 1 << 2,
  OB_HIDE_CAMERA = 1 << 3,
  OB_HIDE_DIFFUSE = 1 << 4,
  OB_HIDE_GLOSSY = 1 << 5,
  OB_HIDE_TRANSMISSION = 1 << 6,
  OB_HIDE_VOLUME_SCATTER = 1 << 7,
  OB_HIDE_SHADOW = 1 << 8,
  OB_HOLDOUT = 1 << 9,
  OB_SHADOW_CATCHER = 1 << 10
};

/* Ob.shapeflag */
enum {
  OB_SHAPE_LOCK = 1 << 0,
#ifdef TYPES_DEPRECATED_ALLOW
  OB_SHAPE_FLAG_UNUSED_1 = 1 << 1, /* cleared */
#endif
  OB_SHAPE_EDIT_MODE = 1 << 2,
};

/* Ob.nlaflag */
enum {
  OB_ADS_UNUSED_1 = 1 << 0, /* cleared */
  OB_ADS_UNUSED_2 = 1 << 1, /* cleared */
  /* ob-channel expanded status */
  OB_ADS_COLLAPSED = 1 << 10,
  /* ob's ipo-block */
  /* OB_ADS_SHOWIPO = 1 << 11, */ /* UNUSED */
  /* object's constraint channels */
  /* OB_ADS_SHOWCONS = 1 << 12, */ /* UNUSED */
  /* ob's material channels */
  /* OB_ADS_SHOWMATS = 1 << 13, */ /* UNUSED */
  /* ob's particle channels */
  /* OB_ADS_SHOWPARTS = 1 << 14, */ /* UNUSED */
};

/* Ob.protectflag */
enum {
  OB_LOCK_LOCX = 1 << 0,
  OB_LOCK_LOCY = 1 << 1,
  OB_LOCK_LOCZ = 1 << 2,
  OB_LOCK_LOC = OB_LOCK_LOCX | OB_LOCK_LOCY | OB_LOCK_LOCZ,
  OB_LOCK_ROTX = 1 << 3,
  OB_LOCK_ROTY = 1 << 4,
  OB_LOCK_ROTZ = 1 << 5,
  OB_LOCK_ROT = OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ,
  OB_LOCK_SCALEX = 1 << 6,
  OB_LOCK_SCALEY = 1 << 7,
  OB_LOCK_SCALEZ = 1 << 8,
  OB_LOCK_SCALE = OB_LOCK_SCALEX | OB_LOCK_SCALEY | OB_LOCK_SCALEZ,
  OB_LOCK_ROTW = 1 << 9,
  OB_LOCK_ROT4D = 1 << 10,
};

/* Ob.duplicator_visibility_flag */
enum {
  OB_DUP_FLAG_VIEWPORT = 1 << 0,
  OB_DUP_FLAG_RENDER = 1 << 1,
};

/* Ob.empty_img_depth */
#define OB_EMPTY_IMG_DEPTH_DEFAULT 0
#define OB_EMPTY_IMG_DEPTH_FRONT 1
#define OB_EMPTY_IMG_DEPTH_BACK 2

/* Ob.empty_img_visibility_flag */
enum {
  OB_EMPTY_IMG_HIDE_PERSPECTIVE = 1 << 0,
  OB_EMPTY_IMG_HIDE_ORTHOGRAPHIC = 1 << 1,
  OB_EMPTY_IMG_HIDE_BACK = 1 << 2,
  OB_EMPTY_IMG_HIDE_FRONT = 1 << 3,
  OB_EMPTY_IMG_HIDE_NON_AXIS_ALIGNED = 1 << 4,
};

/* Ob.empty_img_flag */
enum {
  OB_EMPTY_IMG_USE_ALPHA_BLEND = 1 << 0,
};

#define MAX_DUP_RECUR 8

#ifdef __cplusplus
}
#endif
