#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* internal exports only */
struct WinOpType;

struct Base;
struct GPUSelResult;
struct Ob;
struct Scene;
struct Cxt;
struct PoseChannel;

struct Bone;
struct EditBone;
struct Armature;

struct LinkData;
struct List;

/* Armature EditMode Ops */

void ARMATURE_OT_bone_primitive_add(struct WinOpType *ot);

void ARMATURE_OT_align(struct WinOpType *ot);
void ARMATURE_OT_calculate_roll(struct WinOpType *ot);
void ARMATURE_OT_roll_clear(struct WinOpType *ot);
void ARMATURE_OT_switch_direction(struct WinOpType *ot);

void ARMATURE_OT_subdivide(struct WinOpType *ot);

void ARMATURE_OT_parent_set(struct WinOpType *ot);
void ARMATURE_OT_parent_clear(struct WinOpType *ot);

void ARMATURE_OT_sel_all(struct WinOpType *ot);
void ARMATURE_OT_sel_mirror(struct WinOpType *ot);
void ARMATURE_OT_sel_more(struct WinOpType *ot);
void ARMATURE_OT_sel_less(struct WinOpType *ot);
void ARMATURE_OT_sel_hierarchy(struct WinOpType *ot);
void ARMATURE_OT_sel_linked_pick(struct WinOpType *ot);
void ARMATURE_OT_sel_linked(struct WinOpType *ot);
void ARMATURE_OT_sel_similar(struct WinOpType *ot);
void ARMATURE_OT_shortest_path_pick(struct WinOpType *ot);

void ARMATURE_OT_delete(struct WinOpType *ot);
void ARMATURE_OT_dissolve(struct WinOpType *ot);
void ARMATURE_OT_duplicate(struct WinOpType *ot);
void ARMATURE_OT_symmetrize(struct WinOpType *ot);
void ARMATURE_OT_extrude(struct WinOpType *ot);
void ARMATURE_OT_hide(struct WinOpType *ot);
void ARMATURE_OT_reveal(struct WinOpType *ot);
void ARMATURE_OT_click_extrude(struct WinOpType *ot);
void ARMATURE_OT_fill(struct WinOpType *ot);
void ARMATURE_OT_separate(struct WinOpType *ot);
void ARMATURE_OT_split(struct WinOpType *ot);

void ARMATURE_OT_autoside_names(struct WinOpType *ot);
void ARMATURE_OT_flip_names(struct WinOpType *ot);

void ARMATURE_OT_collection_add(struct WinOpType *ot);
void ARMATURE_OT_collection_remove(struct WinOpType *ot);
void ARMATURE_OT_collection_move(struct WinOpType *ot);
void ARMATURE_OT_collection_assign(struct WinOpType *ot);
void ARMATURE_OT_collection_unassign(struct WinOpType *ot);
void ARMATURE_OT_collection_unassign_named(struct WinOpType *ot);
void ARMATURE_OT_collection_sel(struct WinOpType *ot);
void ARMATURE_OT_collection_desel(struct WinOpType *ot);

void ARMATURE_OT_move_to_collection(struct WinOpType *ot);
void ARMATURE_OT_assign_to_collection(struct WinOpType *ot);

/* Pose-Mode Op */
void POSE_OT_hide(struct WinOpType *ot);
void POSE_OT_reveal(struct WinOpType *ot);

void POSE_OT_armature_apply(struct WinOpType *ot);
void POSE_OT_visual_transform_apply(struct WinOpType *ot);

void POSE_OT_rot_clear(struct WinOpType *ot);
void POSE_OT_loc_clear(struct WinOpType *ot);
void POSE_OT_scale_clear(struct WinOpType *ot);
void POSE_OT_transforms_clear(struct WinOpType *ot);
void POSE_OT_user_transforms_clear(struct WinOpType *ot);

void POSE_OT_copy(struct WinOpType *ot);
void POSE_OT_paste(struct WinOpType *ot);

void POSE_OT_sel_all(struct WinOpType *ot);
void POSE_OT_sel_parent(struct WinOpType *ot);
void POSE_OT_sel_hierarchy(struct WinOpType *ot);
void POSE_OT_sel_linked(struct WinOpType *ot);
void POSE_OT_sel_linked_pick(struct WinOpType *ot);
void POSE_OT_sel_constraint_target(struct WinOpType *ot);
void POSE_OT_sel_grouped(struct WinOpType *ot);
void POSE_OT_sel_mirror(struct WinOpType *ot);

void POSE_OT_paths_calc(struct WinOpType *ot);
void POSE_OT_paths_update(struct WinOpType *ot);
void POSE_OT_paths_clear(struct WinOpType *ot);
void POSE_OT_paths_range_update(struct WinOpType *ot);

void POSE_OT_autoside_names(struct WinOpType *ot);
void POSE_OT_flip_names(struct WinOpType *ot);

void POSE_OT_rotation_mode_set(struct WinOpType *ot);

void POSE_OT_quaternions_flip(struct WinOpType *ot);

/* Pose Tool Utils (for PoseLib, Pose Sliding, etc.) */

/* `pose_utils.cc` */

/* Tmp data linking PoseChannels with the F-Curves they affect */
typedef struct tPChanFCurveLink {
  struct tPChanFCurveLink *next, *prev;

  /* Ob this Pose Channel belongs to. */
  struct Ob *ob;

  /* F-Curves for this PoseChannel (wrapped with LinkData) */
  List fcurves;
  /* Pose Channel which data is attached to */
  struct PoseChannel *pchan;

  /* Api Path to this Pose Channel (needs to be freed when we're done) */
  char *pchan_path;

  /* transform vals at start of op (to be restored before each modal step) */
  float oldloc[3];
  float oldrot[3];
  float oldscale[3];
  float oldquat[4];
  float oldangle;
  float oldaxis[3];

  /** old bbone values (to be restored along with the transform properties) */
  float roll1, roll2;
  /** (NOTE: we haven't renamed these this time, as their names are already long enough) */
  float curve_in_x, curve_in_z;
  float curve_out_x, curve_out_z;
  float ease1, ease2;
  float scale_in[3];
  float scale_out[3];

  /** copy of custom properties at start of operator (to be restored before each modal step) */
  struct IDProperty *oldprops;
} tPChanFCurveLink;

/* ----------- */

/** Returns a valid pose armature for this object, else returns NULL. */
struct Object *poseAnim_object_get(struct Object *ob_);
/** Get sets of F-Curves providing transforms for the bones in the Pose. */
void poseAnim_mapping_get(struct bContext *C, ListBase *pfLinks);
/** Free F-Curve <-> PoseChannel links. */
void poseAnim_mapping_free(ListBase *pfLinks);

/**
 * Helper for apply() / reset() - refresh the data.
 */
void poseAnim_mapping_refresh(struct bContext *C, struct Scene *scene, struct Object *ob);
/**
 * Reset changes made to current pose.
 */
void poseAnim_mapping_reset(ListBase *pfLinks);
/** Perform auto-key-framing after changes were made + confirmed. */
void poseAnim_mapping_autoKeyframe(struct bContext *C,
                                   struct Scene *scene,
                                   ListBase *pfLinks,
                                   float cframe);

/**
 * Find the next F-Curve for a PoseChannel with matching path.
 * - `path` is not just the #tPChanFCurveLink (`pfl`) rna_path,
 *   since that path doesn't have property info yet.
 */
LinkData *poseAnim_mapping_getNextFCurve(ListBase *fcuLinks, LinkData *prev, const char *path);

/** \} */

/* -------------------------------------------------------------------- */
/** \name PoseLib
 * \{ */

/* `pose_lib_2.cc` */

void POSELIB_OT_apply_pose_asset(struct WinOpType *ot);
void POSELIB_OT_blend_pose_asset(struct WinOpType *ot);
/* Pose Sliding Tool */
/* `pose_slide.cc` */
void POSE_OT_push(struct WinOpType *ot);
void POSE_OT_relax(struct WinOpType *ot);
void POSE_OT_blend_with_rest(struct WinOpType *ot);
void POSE_OT_breakdown(struct WinOpType *ot);
void POSE_OT_blend_to_neighbors(struct WinOpType *ot);

void POSE_OT_propagate(struct WinOpType *ot);

/* Various Armature Edit/Pose Editing API' */
/* Ideally, many of these defines would not be needed as everything would be strictly
 * self-contained within each file,
 * but some tools still have a bit of overlap which makes things messy -- Feb 2013 */

struct EditBone *make_boneList(struct List *edbo,
                               struct List *bones,
                               struct Bone *actBone);

/* Dup method. */
/* Call this before doing any dups. */
void preEditBoneDup(struct List *editbones);
void postEditBoneDup(struct List *editbones, struct Ob *ob);
struct EditBone *dupEditBone(struct EditBone *cur_bone,
                                   const char *name,
                                   struct List *editbones,
                                   struct Ob *ob);

/* Dup method (cross obs). */
/* param editbones: The target list. */
struct EditBone *dupEditBoneObs(struct EditBone *cur_bone,
                                const char *name,
                                struct List *editbones,
                                struct Ob *src_ob,
                                struct O *dst_ob);

/* Adds an EditBone between the nominated locations (should be in the right space). */
struct EditBone *add_points_bone(struct Ob *obedit, float head[3], float tail[3]);
void bone_free(struct Armature *arm, struct EditBone *bone);

void armature_tag_sel_mirrored(struct Armature *arm);
/* Helper fn for tools to work on mirrored parts.
 * it leaves mirrored bones sel then too, which is a good indication of what happened */
void armature_sel_mirrored_ex(struct Armature *arm, int flag);
void armature_sel_mirrored(struct Armature *arm);
/* Only works when tagged. */
void armature_tag_unsel(struct Armature *arm);

/* Sel Picking */
struct EditBone *ed_armature_pick_ebone(struct Cxt *C,
                                        const int xy[2],
                                        bool findunsel,
                                        struct Base **r_base);
struct PoseChannel *ed_armature_pick_pchan(struct Cxt *C,
                                            const int xy[2],
                                            bool findunsel,
                                            struct Base **r_base);
struct Bone *ed_armature_pick_bone(struct Cxt *C,
                                   const int xy[2],
                                   bool findunsel,
                                   struct Base **r_base);

struct EditBone *ed_armature_pick_ebone_from_selbuf(
    struct Base **bases,
    uint bases_len,
    const struct GPUSelResult *hit_results,
    int hits,
    bool findunsel,
    bool do_nearest,
    struct Base **r_base);
struct PoseChannel *ed_armature_pick_pchan_from_selbuf(
    struct Base **bases,
    uint bases_len,
    const struct GPUSelResult *hit_results,
    int hits,
    bool findunsel,
    bool do_nearest,
    struct Base **r_base);
struct Bone *ed_armature_pick_bone_from_selbuf(struct Base **bases,
                                                     uint bases_len,
                                                     const struct GPUSelResult *hit_results,
                                                     int hits,
                                                     bool findunsel,
                                                     bool do_nearest,
                                                     struct Base **r_base);

/* -------------------------------------------------------------------- */
/** \name Iteration
 * \{ */

/**
 * XXX: bone_looper is only to be used when we want to access settings
 * (i.e. editability/visibility/selected) that context doesn't offer.
 */
int bone_looper(struct Object *ob,
                struct Bone *bone,
                void *data,
                int (*bone_func)(struct Object *, struct Bone *, void *));

/** \} */

#ifdef __cplusplus
}
#endif
