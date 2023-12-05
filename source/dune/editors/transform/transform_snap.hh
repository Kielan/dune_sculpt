#pragma once

#define SNAP_MIN_DISTANCE 30

/* For enum. */
#include "types_scene.h"
#include "types_space.h"

bool peelObsTransform(TransInfo *t,
                      const float mval[2],
                      bool use_peel_ob,
                      /* return args */
                      float r_loc[3],
                      float r_no[3],
                      float *r_thickness);

eSnapMode snapObsTransform(TransInfo *t,
                           const float mval[2],
                           float *dist_px,
                           /* return args */
                           float r_loc[3],
                           float r_no[3]);
bool snapNodesTransform(TransInfo *t,
                        const dune::float2 &mval,
                        /* return args */
                        float r_loc[2],
                        float *r_dist_px,
                        char *r_node_border);

bool transformModeUseSnap(const TransInfo *t);

void tranform_snap_target_median_calc(const TransInfo *t, float r_median[3]);
bool transform_snap_increment_ex(const TransInfo *t, bool use_local_space, float *r_val);
bool transform_snap_increment(const TransInfo *t, float *val);
float transform_snap_increment_get(const TransInfo *t);

void tranform_snap_src_restore_cxt(TransInfo *t);

void transform_snap_flag_from_mods_set(TransInfo *t);
bool transform_snap_is_active(const TransInfo *t);

bool validSnap(const TransInfo *t);

void initSnapping(TransInfo *t, WinOp *op);
void freeSnapping(TransInfo *t);
bool transform_snap_project_individual_is_active(const TransInfo *t);
void transform_snap_project_individual_apply(TransInfo *t);
void transform_snap_mixed_apply(TransInfo *t, float *vec);
void resetSnapping(TransInfo *t);
eRedrwFlag handleSnapping(TransInfo *t, const WinEv *ev);
void drwSnapping(TransInfo *t);
bool usingSnappingNormal(const TransInfo *t);
bool validSnappingNormal(const TransInfo *t);

void getSnapPoint(const TransInfo *t, float vec[3]);
void addSnapPoint(TransInfo *t);
eRedrwFlag updateSelSnapPoint(TransInfo *t);
void removeSnapPoint(TransInfo *t);

float transform_snap_distance_len_squared_fn(TransInfo *t, const float p1[3], const float p2[3]);

/* `transform_snap_seq.cc` */
TransSeqSnapData *transform_snap_seq_data_alloc(const TransInfo *t);
void transform_snap_seq_data_free(TransSeqSnapData *data);
bool transform_snap_seq_calc(TransInfo *t);
void transform_snap_seq_apply_translate(TransInfo *t, float *vec);

/* transform_snap_anim.cc */
void snapFrameTransform(
    TransInfo *t, eSnapMode autosnap, float val_init, float val_final, float *r_val_final);
/* Fn is used by Ani Editor specific transform fns to do
 * the Snap Keyframe to Nearest Frame/Marker */
void transform_snap_anim_flush_data(TransInfo *t,
                                    TransData *td,
                                    eSnapMode autosnap,
                                    float *r_val_final);
bool transform_snap_nla_calc(TransInfo *t, float *vec);
