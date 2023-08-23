#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Keep for 'Camera' versioning. */
/*Props for DOF effect. */
typedef struct GPUDOFSettings {
  /**m Focal distance for depth of field. */
  float focus_distance;
  float fstop;
  float focal_length;
  float sensor;
  float rotation;
  float ratio;
  int num_blades;
  int high_quality;
} GPUDOFSettings;

#ifdef __cplusplus
}
#endif
