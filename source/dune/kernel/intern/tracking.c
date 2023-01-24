
{
  if (object->flag & TRACKING_OBJECT_CAMERA) {
    return &tracking->plane_tracks;
  }

  return &object->plane_tracks;
}

MovieTrackingReconstruction *KERNEL_tracking_object_get_reconstruction(MovieTracking *tracking,
                                                                    MovieTrackingObject *object)
{
  if (object->flag & TRACKING_OBJECT_CAMERA) {
    return &tracking->reconstruction;
  }

  return &object->reconstruction;
}

/*********************** Camera *************************/

static int reconstructed_camera_index_get(MovieTrackingReconstruction *reconstruction,
                                          int framenr,
                                          bool nearest)
{
  MovieReconstructedCamera *cameras = reconstruction->cameras;
  int a = 0, d = 1;

  if (!reconstruction->camnr) {
    return -1;
  }

  if (framenr < cameras[0].framenr) {
    if (nearest) {
      return 0;
    }

    return -1;
  }

  if (framenr > cameras[reconstruction->camnr - 1].framenr) {
    if (nearest) {
      return reconstruction->camnr - 1;
    }

    return -1;
  }

  if (reconstruction->last_camera < reconstruction->camnr) {
    a = reconstruction->last_camera;
  }

  if (cameras[a].framenr >= framenr) {
    d = -1;
  }

  while (a >= 0 && a < reconstruction->camnr) {
    int cfra = cameras[a].framenr;

    /* check if needed framenr was "skipped" -- no data for requested frame */

    if (d > 0 && cfra > framenr) {
      /* interpolate with previous position */
      if (nearest) {
        return a - 1;
      }

      break;
    }

    if (d < 0 && cfra < framenr) {
      /* interpolate with next position */
      if (nearest) {
        return a;
      }

      break;
    }

    if (cfra == framenr) {
      reconstruction->last_camera = a;

      return a;
    }

    a += d;
  }

  return -1;
}

static void reconstructed_camera_scale_set(MovieTrackingObject *object, float mat[4][4])
{
  if ((object->flag & TRACKING_OBJECT_CAMERA) == 0) {
    float smat[4][4];

    scale_m4_fl(smat, 1.0f / object->scale);
    mul_m4_m4m4(mat, mat, smat);
  }
}

void KERNEL_tracking_camera_shift_get(
    MovieTracking *tracking, int winx, int winy, float *shiftx, float *shifty)
{
  /* Indeed in both of cases it should be winx -
   * it's just how camera shift works for dune's camera. */
  *shiftx = (0.5f * winx - tracking->camera.principal[0]) / winx;
  *shifty = (0.5f * winy - tracking->camera.principal[1]) / winx;
}

void KERNEL_tracking_camera_to_dune(
    MovieTracking *tracking, Scene *scene, Camera *camera, int width, int height)
{
  float focal = tracking->camera.focal;

  camera->sensor_x = tracking->camera.sensor_width;
  camera->sensor_fit = CAMERA_SENSOR_FIT_AUTO;
  camera->lens = focal * camera->sensor_x / width;

  scene->r.xsch = width;
  scene->r.ysch = height;

  scene->r.xasp = tracking->camera.pixel_aspect;
  scene->r.yasp = 1.0f;

  KERNEL_tracking_camera_shift_get(tracking, width, height, &camera->shiftx, &camera->shifty);
}

MovieReconstructedCamera *KERNEL_tracking_camera_get_reconstructed(MovieTracking *tracking,
                                                                MovieTrackingObject *object,
                                                                int framenr)
{
  MovieTrackingReconstruction *reconstruction;
  int a;

  reconstruction = KERNEL_tracking_object_get_reconstruction(tracking, object);
  a = reconstructed_camera_index_get(reconstruction, framenr, false);

  if (a == -1) {
    return NULL;
  }

  return &reconstruction->cameras[a];
}

void KERNEL_tracking_camera_get_reconstructed_interpolate(MovieTracking *tracking,
                                                       MovieTrackingObject *object,
                                                       float framenr,
                                                       float mat[4][4])
{
  MovieTrackingReconstruction *reconstruction;
  MovieReconstructedCamera *cameras;
  int a;

  reconstruction = KERNEL_tracking_object_get_reconstruction(tracking, object);
  cameras = reconstruction->cameras;
  a = reconstructed_camera_index_get(reconstruction, (int)framenr, true);

  if (a == -1) {
    unit_m4(mat);
    return;
  }

  if (cameras[a].framenr != framenr && a < reconstruction->camnr - 1) {
    float t = ((float)framenr - cameras[a].framenr) /
              (cameras[a + 1].framenr - cameras[a].framenr);
    dune_m4_m4m4(mat, cameras[a].mat, cameras[a + 1].mat, t);
  }
  else {
    copy_m4_m4(mat, cameras[a].mat);
  }

  reconstructed_camera_scale_set(object, mat);
}

/*********************** Distortion/Undistortion *************************/

MovieDistortion *KERNEL_tracking_distortion_new(MovieTracking *tracking,
                                             int calibration_width,
                                             int calibration_height)
{
  MovieDistortion *distortion;
  libmv_CameraIntrinsicsOptions camera_intrinsics_options;

  tracking_cameraIntrinscisOptionsFromTracking(
      tracking, calibration_width, calibration_height, &camera_intrinsics_options);

  distortion = MEM_callocN(sizeof(MovieDistortion), "KERNEL_tracking_distortion_create");
  distortion->intrinsics = libmv_cameraIntrinsicsNew(&camera_intrinsics_options);

  const MovieTrackingCamera *camera = &tracking->camera;
  copy_v2_v2(distortion->principal, camera->principal);
  distortion->pixel_aspect = camera->pixel_aspect;
  distortion->focal = camera->focal;

  return distortion;
}

void KERNEL_tracking_distortion_update(MovieDistortion *distortion,
                                    MovieTracking *tracking,
                                    int calibration_width,
                                    int calibration_height)
{
  libmv_CameraIntrinsicsOptions camera_intrinsics_options;

  tracking_cameraIntrinscisOptionsFromTracking(
      tracking, calibration_width, calibration_height, &camera_intrinsics_options);

  const MovieTrackingCamera *camera = &tracking->camera;
  copy_v2_v2(distortion->principal, camera->principal);
  distortion->pixel_aspect = camera->pixel_aspect;
  distortion->focal = camera->focal;

  libmv_cameraIntrinsicsUpdate(&camera_intrinsics_options, distortion->intrinsics);
}

void KERNEL_tracking_distortion_set_threads(MovieDistortion *distortion, int threads)
{
  libmv_cameraIntrinsicsSetThreads(distortion->intrinsics, threads);
}

MovieDistortion *KERNEL_tracking_distortion_copy(MovieDistortion *distortion)
{
  MovieDistortion *new_distortion;

  new_distortion = MEM_callocN(sizeof(MovieDistortion), "BKE_tracking_distortion_create");
  *new_distortion = *distortion;
  new_distortion->intrinsics = libmv_cameraIntrinsicsCopy(distortion->intrinsics);

  return new_distortion;
}

ImBuf *KERNEL_tracking_distortion_exec(MovieDistortion *distortion,
                                    MovieTracking *tracking,
                                    ImBuf *ibuf,
                                    int calibration_width,
                                    int calibration_height,
                                    float overscan,
                                    bool undistort)
{
  ImBuf *resibuf;

  KERNEL_tracking_distortion_update(distortion, tracking, calibration_width, calibration_height);

  resibuf = IMB_dupImBuf(ibuf);

  if (ibuf->rect_float) {
    if (undistort) {
      libmv_cameraIntrinsicsUndistortFloat(distortion->intrinsics,
                                           ibuf->rect_float,
                                           ibuf->x,
                                           ibuf->y,
                                           overscan,
                                           ibuf->channels,
                                           resibuf->rect_float);
    }
    else {
      libmv_cameraIntrinsicsDistortFloat(distortion->intrinsics,
                                         ibuf->rect_float,
                                         ibuf->x,
                                         ibuf->y,
                                         overscan,
                                         ibuf->channels,
                                         resibuf->rect_float);
    }

    if (ibuf->rect) {
      imb_freerectImBuf(ibuf);
    }
  }
  else {
    if (undistort) {
      libmv_cameraIntrinsicsUndistortByte(distortion->intrinsics,
                                          (unsigned char *)ibuf->rect,
                                          ibuf->x,
                                          ibuf->y,
                                          overscan,
                                          ibuf->channels,
                                          (unsigned char *)resibuf->rect);
    }
    else {
      libmv_cameraIntrinsicsDistortByte(distortion->intrinsics,
                                        (unsigned char *)ibuf->rect,
                                        ibuf->x,
                                        ibuf->y,
                                        overscan,
                                        ibuf->channels,
                                        (unsigned char *)resibuf->rect);
    }
  }

  return resibuf;
}

void KERNEL_tracking_distortion_distort_v2(MovieDistortion *distortion,
                                        const float co[2],
                                        float r_co[2])
{
  const float aspy = 1.0f / distortion->pixel_aspect;

  /* Normalize coords. */
  float inv_focal = 1.0f / distortion->focal;
  double x = (co[0] - distortion->principal[0]) * inv_focal,
         y = (co[1] - distortion->principal[1] * aspy) * inv_focal;

  libmv_cameraIntrinsicsApply(distortion->intrinsics, x, y, &x, &y);

  /* Result is in image coords already. */
  r_co[0] = x;
  r_co[1] = y;
}

void KERNEL_tracking_distortion_undistort_v2(MovieDistortion *distortion,
                                          const float co[2],
                                          float r_co[2])
{
  double x = co[0], y = co[1];
  libmv_cameraIntrinsicsInvert(distortion->intrinsics, x, y, &x, &y);

  const float aspy = 1.0f / distortion->pixel_aspect;
  r_co[0] = (float)x * distortion->focal + distortion->principal[0];
  r_co[1] = (float)y * distortion->focal + distortion->principal[1] * aspy;
}

void KERNEL_tracking_distortion_free(MovieDistortion *distortion)
{
  libmv_cameraIntrinsicsDestroy(distortion->intrinsics);

  MEM_freeN(distortion);
}

void KERNEL_tracking_distort_v2(
    MovieTracking *tracking, int image_width, int image_height, const float co[2], float r_co[2])
{
  const MovieTrackingCamera *camera = &tracking->camera;
  const float aspy = 1.0f / tracking->camera.pixel_aspect;

  libmv_CameraIntrinsicsOptions camera_intrinsics_options;
  tracking_cameraIntrinscisOptionsFromTracking(
      tracking, image_width, image_height, &camera_intrinsics_options);
  libmv_CameraIntrinsics *intrinsics = libmv_cameraIntrinsicsNew(&camera_intrinsics_options);

  /* Normalize coordinates. */
  double x = (co[0] - camera->principal[0]) / camera->focal,
         y = (co[1] - camera->principal[1] * aspy) / camera->focal;

  libmv_cameraIntrinsicsApply(intrinsics, x, y, &x, &y);
  libmv_cameraIntrinsicsDestroy(intrinsics);

  /* Result is in image coords already. */
  r_co[0] = x;
  r_co[1] = y;
}

void KERNEL_tracking_undistort_v2(
    MovieTracking *tracking, int image_width, int image_height, const float co[2], float r_co[2])
{
  const MovieTrackingCamera *camera = &tracking->camera;
  const float aspy = 1.0f / tracking->camera.pixel_aspect;

  libmv_CameraIntrinsicsOptions camera_intrinsics_options;
  tracking_cameraIntrinscisOptionsFromTracking(
      tracking, image_width, image_height, &camera_intrinsics_options);
  libmv_CameraIntrinsics *intrinsics = libmv_cameraIntrinsicsNew(&camera_intrinsics_options);

  double x = co[0], y = co[1];
  libmv_cameraIntrinsicsInvert(intrinsics, x, y, &x, &y);
  libmv_cameraIntrinsicsDestroy(intrinsics);

  r_co[0] = (float)x * camera->focal + camera->principal[0];
  r_co[1] = (float)y * camera->focal + camera->principal[1] * aspy;
}

ImBuf *KERNEL_tracking_undistort_frame(MovieTracking *tracking,
                                    ImBuf *ibuf,
                                    int calibration_width,
                                    int calibration_height,
                                    float overscan)
{
  MovieTrackingCamera *camera = &tracking->camera;

  if (camera->intrinsics == NULL) {
    camera->intrinsics = KERNEL_tracking_distortion_new(
        tracking, calibration_width, calibration_height);
  }

  return KERNEL_tracking_distortion_exec(
      camera->intrinsics, tracking, ibuf, calibration_width, calibration_height, overscan, true);
}

ImBuf *KERNEL_tracking_distort_frame(MovieTracking *tracking,
                                  ImBuf *ibuf,
                                  int calibration_width,
                                  int calibration_height,
                                  float overscan)
{
  MovieTrackingCamera *camera = &tracking->camera;

  if (camera->intrinsics == NULL) {
    camera->intrinsics = KERNEL_tracking_distortion_new(
        tracking, calibration_width, calibration_height);
  }

  return KERNEL_tracking_distortion_exec(
      camera->intrinsics, tracking, ibuf, calibration_width, calibration_height, overscan, false);
}

void KERNEL_tracking_max_distortion_delta_across_bound(MovieTracking *tracking,
                                                    int image_width,
                                                    int image_height,
                                                    rcti *rect,
                                                    bool undistort,
                                                    float delta[2])
{
  float pos[2], warped_pos[2];
  const int coord_delta = 5;
  void (*apply_distortion)(MovieTracking * tracking,
                           int image_width,
                           int image_height,
                           const float pos[2],
                           float out[2]);

  if (undistort) {
    apply_distortion = KERNEL_tracking_undistort_v2;
  }
  else {
    apply_distortion = KERNEL_tracking_distort_v2;
  }

  delta[0] = delta[1] = -FLT_MAX;

  for (int a = rect->xmin; a <= rect->xmax + coord_delta; a += coord_delta) {
    if (a > rect->xmax) {
      a = rect->xmax;
    }

    /* bottom edge */
    pos[0] = a;
    pos[1] = rect->ymin;

    apply_distortion(tracking, image_width, image_height, pos, warped_pos);

    delta[0] = max_ff(delta[0], fabsf(pos[0] - warped_pos[0]));
    delta[1] = max_ff(delta[1], fabsf(pos[1] - warped_pos[1]));

    /* top edge */
    pos[0] = a;
    pos[1] = rect->ymax;

    apply_distortion(tracking, image_width, image_height, pos, warped_pos);

    delta[0] = max_ff(delta[0], fabsf(pos[0] - warped_pos[0]));
    delta[1] = max_ff(delta[1], fabsf(pos[1] - warped_pos[1]));

    if (a >= rect->xmax) {
      break;
    }
  }

  for (int a = rect->ymin; a <= rect->ymax + coord_delta; a += coord_delta) {
    if (a > rect->ymax) {
      a = rect->ymax;
    }

    /* left edge */
    pos[0] = rect->xmin;
    pos[1] = a;

    apply_distortion(tracking, image_width, image_height, pos, warped_pos);

    delta[0] = max_ff(delta[0], fabsf(pos[0] - warped_pos[0]));
    delta[1] = max_ff(delta[1], fabsf(pos[1] - warped_pos[1]));

    /* right edge */
    pos[0] = rect->xmax;
    pos[1] = a;

    apply_distortion(tracking, image_width, image_height, pos, warped_pos);

    delta[0] = max_ff(delta[0], fabsf(pos[0] - warped_pos[0]));
    delta[1] = max_ff(delta[1], fabsf(pos[1] - warped_pos[1]));

    if (a >= rect->ymax) {
      break;
    }
  }
}

/*********************** Image sampling *************************/

static void disable_imbuf_channels(ImBuf *ibuf, MovieTrackingTrack *track, bool grayscale)
{
  KERNEL_tracking_disable_channels(ibuf,
                                track->flag & TRACK_DISABLE_RED,
                                track->flag & TRACK_DISABLE_GREEN,
                                track->flag & TRACK_DISABLE_BLUE,
                                grayscale);
}

ImBuf *KERNEL_tracking_sample_pattern(int frame_width,
                                   int frame_height,
                                   ImBuf *search_ibuf,
                                   MovieTrackingTrack *track,
                                   MovieTrackingMarker *marker,
                                   bool from_anchor,
                                   bool use_mask,
                                   int num_samples_x,
                                   int num_samples_y,
                                   float pos[2])
{
  ImBuf *pattern_ibuf;
  double src_pixel_x[5], src_pixel_y[5];
  double warped_position_x, warped_position_y;
  float *mask = NULL;

  if (num_samples_x <= 0 || num_samples_y <= 0) {
    return NULL;
  }

  pattern_ibuf = IMB_allocImBuf(
      num_samples_x, num_samples_y, 32, search_ibuf->rect_float ? IB_rectfloat : IB_rect);

  tracking_get_marker_coords_for_tracking(
      frame_width, frame_height, marker, src_pixel_x, src_pixel_y);

  /* from_anchor means search buffer was obtained for an anchored position,
   * which means applying track offset rounded to pixel space (we could not
   * store search buffer with sub-pixel precision)
   *
   * in this case we need to alter coordinates a bit, to compensate rounded
   * fractional part of offset
   */
  if (from_anchor) {
    for (int a = 0; a < 5; a++) {
      src_pixel_x[a] += (double)((track->offset[0] * frame_width) -
                                 ((int)(track->offset[0] * frame_width)));
      src_pixel_y[a] += (double)((track->offset[1] * frame_height) -
                                 ((int)(track->offset[1] * frame_height)));

      /* when offset is negative, rounding happens in opposite direction */
      if (track->offset[0] < 0.0f) {
        src_pixel_x[a] += 1.0;
      }
      if (track->offset[1] < 0.0f) {
        src_pixel_y[a] += 1.0;
      }
    }
  }

  if (use_mask) {
    mask = KERNEL_tracking_track_get_mask(frame_width, frame_height, track, marker);
  }

  if (search_ibuf->rect_float) {
    libmv_samplePlanarPatchFloat(search_ibuf->rect_float,
                                 search_ibuf->x,
                                 search_ibuf->y,
                                 4,
                                 src_pixel_x,
                                 src_pixel_y,
                                 num_samples_x,
                                 num_samples_y,
                                 mask,
                                 pattern_ibuf->rect_float,
                                 &warped_position_x,
                                 &warped_position_y);
  }
  else {
    libmv_samplePlanarPatchByte((unsigned char *)search_ibuf->rect,
                                search_ibuf->x,
                                search_ibuf->y,
                                4,
                                src_pixel_x,
                                src_pixel_y,
                                num_samples_x,
                                num_samples_y,
                                mask,
                                (unsigned char *)pattern_ibuf->rect,
                                &warped_position_x,
                                &warped_position_y);
  }

  if (pos) {
    pos[0] = warped_position_x;
    pos[1] = warped_position_y;
  }

  if (mask) {
    MEM_freeN(mask);
  }

  return pattern_ibuf;
}

ImBuf *KERNEL_tracking_get_pattern_imbuf(ImBuf *ibuf,
                                      MovieTrackingTrack *track,
                                      MovieTrackingMarker *marker,
                                      bool anchored,
                                      bool disable_channels)
{
  ImBuf *pattern_ibuf, *search_ibuf;
  float pat_min[2], pat_max[2];
  int num_samples_x, num_samples_y;

  KERNEL_tracking_marker_pattern_minmax(marker, pat_min, pat_max);

  num_samples_x = (pat_max[0] - pat_min[0]) * ibuf->x;
  num_samples_y = (pat_max[1] - pat_min[1]) * ibuf->y;

  search_ibuf = KERNEL_tracking_get_search_imbuf(ibuf, track, marker, anchored, disable_channels);

  if (search_ibuf) {
    pattern_ibuf = KERNEL_tracking_sample_pattern(ibuf->x,
                                               ibuf->y,
                                               search_ibuf,
                                               track,
                                               marker,
                                               anchored,
                                               false,
                                               num_samples_x,
                                               num_samples_y,
                                               NULL);

    IMB_freeImBuf(search_ibuf);
  }
  else {
    pattern_ibuf = NULL;
  }

  return pattern_ibuf;
}

ImBuf *KERNEL_tracking_get_search_imbuf(ImBuf *ibuf,
                                     MovieTrackingTrack *track,
                                     MovieTrackingMarker *marker,
                                     bool anchored,
                                     bool disable_channels)
{
  ImBuf *searchibuf;
  int x, y, w, h;
  float search_origin[2];

  tracking_get_search_origin_frame_pixel(ibuf->x, ibuf->y, marker, search_origin);

  x = search_origin[0];
  y = search_origin[1];

  if (anchored) {
    x += track->offset[0] * ibuf->x;
    y += track->offset[1] * ibuf->y;
  }

  w = (marker->search_max[0] - marker->search_min[0]) * ibuf->x;
  h = (marker->search_max[1] - marker->search_min[1]) * ibuf->y;

  if (w <= 0 || h <= 0) {
    return NULL;
  }

  searchibuf = IMB_allocImBuf(w, h, 32, ibuf->rect_float ? IB_rectfloat : IB_rect);

  IMB_rectcpy(searchibuf, ibuf, 0, 0, x, y, w, h);

  if (disable_channels) {
    if ((track->flag & TRACK_PREVIEW_GRAYSCALE) || (track->flag & TRACK_DISABLE_RED) ||
        (track->flag & TRACK_DISABLE_GREEN) || (track->flag & TRACK_DISABLE_BLUE)) {
      disable_imbuf_channels(searchibuf, track, true);
    }
  }

  return searchibuf;
}

void KERNEL_tracking_disable_channels(
    ImBuf *ibuf, bool disable_red, bool disable_green, bool disable_blue, bool grayscale)
{
  if (!disable_red && !disable_green && !disable_blue && !grayscale) {
    return;
  }

  /* if only some components are selected, it's important to rescale the result
   * appropriately so that e.g. if only blue is selected, it's not zeroed out.
   */
  float scale = (disable_red ? 0.0f : 0.2126f) + (disable_green ? 0.0f : 0.7152f) +
                (disable_blue ? 0.0f : 0.0722f);

  for (int y = 0; y < ibuf->y; y++) {
    for (int x = 0; x < ibuf->x; x++) {
      int pixel = ibuf->x * y + x;

      if (ibuf->rect_float) {
        float *rrgbf = ibuf->rect_float + pixel * 4;
        float r = disable_red ? 0.0f : rrgbf[0];
        float g = disable_green ? 0.0f : rrgbf[1];
        float b = disable_blue ? 0.0f : rrgbf[2];

        if (grayscale) {
          float gray = (0.2126f * r + 0.7152f * g + 0.0722f * b) / scale;

          rrgbf[0] = rrgbf[1] = rrgbf[2] = gray;
        }
        else {
          rrgbf[0] = r;
          rrgbf[1] = g;
          rrgbf[2] = b;
        }
      }
      else {
        char *rrgb = (char *)ibuf->rect + pixel * 4;
        char r = disable_red ? 0 : rrgb[0];
        char g = disable_green ? 0 : rrgb[1];
        char b = disable_blue ? 0 : rrgb[2];

        if (grayscale) {
          float gray = (0.2126f * r + 0.7152f * g + 0.0722f * b) / scale;

          rrgb[0] = rrgb[1] = rrgb[2] = gray;
        }
        else {
          rrgb[0] = r;
          rrgb[1] = g;
          rrgb[2] = b;
        }
      }
    }
  }

  if (ibuf->rect_float) {
    ibuf->userflags |= IB_RECT_INVALID;
  }
}

/*********************** Dopesheet functions *************************/

/* ** Channels sort comparators ** */

static int channels_alpha_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  if (LIB_strcasecmp(channel_a->track->name, channel_b->track->name) > 0) {
    return 1;
  }

  return 0;
}

static int channels_total_track_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  if (channel_a->total_frames > channel_b->total_frames) {
    return 1;
  }

  return 0;
}

static int channels_longest_segment_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  if (channel_a->max_segment > channel_b->max_segment) {
    return 1;
  }

  return 0;
}

static int channels_average_error_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  if (channel_a->track->error > channel_b->track->error) {
    return 1;
  }

  return 0;
}

static int compare_firstlast_putting_undefined_first(
    bool inverse, bool a_markerless, int a_value, bool b_markerless, int b_value)
{
  if (a_markerless && b_markerless) {
    /* Neither channel has not-disabled markers, return whatever. */
    return 0;
  }
  if (a_markerless) {
    /* Put the markerless channel first. */
    return 0;
  }
  if (b_markerless) {
    /* Put the markerless channel first. */
    return 1;
  }

  /* Both channels have markers. */

  if (inverse) {
    if (a_value < b_value) {
      return 1;
    }
    return 0;
  }

  if (a_value > b_value) {
    return 1;
  }
  return 0;
}

static int channels_start_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  return compare_firstlast_putting_undefined_first(false,
                                                   channel_a->tot_segment == 0,
                                                   channel_a->first_not_disabled_marker_framenr,
                                                   channel_b->tot_segment == 0,
                                                   channel_b->first_not_disabled_marker_framenr);
}

static int channels_end_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  return compare_firstlast_putting_undefined_first(false,
                                                   channel_a->tot_segment == 0,
                                                   channel_a->last_not_disabled_marker_framenr,
                                                   channel_b->tot_segment == 0,
                                                   channel_b->last_not_disabled_marker_framenr);
}

static int channels_alpha_inverse_sort(const void *a, const void *b)
{
  if (channels_alpha_sort(a, b)) {
    return 0;
  }

  return 1;
}

static int channels_total_track_inverse_sort(const void *a, const void *b)
{
  if (channels_total_track_sort(a, b)) {
    return 0;
  }

  return 1;
}

static int channels_longest_segment_inverse_sort(const void *a, const void *b)
{
  if (channels_longest_segment_sort(a, b)) {
    return 0;
  }

  return 1;
}

static int channels_average_error_inverse_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  if (channel_a->track->error < channel_b->track->error) {
    return 1;
  }

  return 0;
}

static int channels_start_inverse_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  return compare_firstlast_putting_undefined_first(true,
                                                   channel_a->tot_segment == 0,
                                                   channel_a->first_not_disabled_marker_framenr,
                                                   channel_b->tot_segment == 0,
                                                   channel_b->first_not_disabled_marker_framenr);
}

static int channels_end_inverse_sort(const void *a, const void *b)
{
  const MovieTrackingDopesheetChannel *channel_a = a;
  const MovieTrackingDopesheetChannel *channel_b = b;

  return compare_firstlast_putting_undefined_first(true,
                                                   channel_a->tot_segment == 0,
                                                   channel_a->last_not_disabled_marker_framenr,
                                                   channel_b->tot_segment == 0,
                                                   channel_b->last_not_disabled_marker_framenr);
}

/* Calculate frames segments at which track is tracked continuously. */
static void tracking_dopesheet_channels_segments_calc(MovieTrackingDopesheetChannel *channel)
{
  MovieTrackingTrack *track = channel->track;
  int i, segment;
  bool first_not_disabled_marker_framenr_set;

  channel->tot_segment = 0;
  channel->max_segment = 0;
  channel->total_frames = 0;

  channel->first_not_disabled_marker_framenr = 0;
  channel->last_not_disabled_marker_framenr = 0;

  /* TODO(sergey): looks a bit code-duplicated, need to look into
   *               logic de-duplication here.
   */

  /* count */
  i = 0;
  first_not_disabled_marker_framenr_set = false;
  while (i < track->markersnr) {
    MovieTrackingMarker *marker = &track->markers[i];

    if ((marker->flag & MARKER_DISABLED) == 0) {
      int prev_fra = marker->framenr, len = 0;

      i++;
      while (i < track->markersnr) {
        marker = &track->markers[i];

        if (marker->framenr != prev_fra + 1) {
          break;
        }
        if (marker->flag & MARKER_DISABLED) {
          break;
        }

        if (!first_not_disabled_marker_framenr_set) {
          channel->first_not_disabled_marker_framenr = marker->framenr;
          first_not_disabled_marker_framenr_set = true;
        }
        channel->last_not_disabled_marker_framenr = marker->framenr;

        prev_fra = marker->framenr;
        len++;
        i++;
      }

      channel->tot_segment++;
    }

    i++;
  }

  if (!channel->tot_segment) {
    return;
  }

  channel->segments = MEM_callocN(sizeof(int[2]) * channel->tot_segment,
                                  "tracking channel segments");

  /* create segments */
  i = 0;
  segment = 0;
  while (i < track->markersnr) {
    MovieTrackingMarker *marker = &track->markers[i];

    if ((marker->flag & MARKER_DISABLED) == 0) {
      MovieTrackingMarker *start_marker = marker;
      int prev_fra = marker->framenr, len = 0;

      i++;
      while (i < track->markersnr) {
        marker = &track->markers[i];

        if (marker->framenr != prev_fra + 1) {
          break;
        }
        if (marker->flag & MARKER_DISABLED) {
          break;
        }

        prev_fra = marker->framenr;
        channel->total_frames++;
        len++;
        i++;
      }

      channel->segments[2 * segment] = start_marker->framenr;
      channel->segments[2 * segment + 1] = start_marker->framenr + len;

      channel->max_segment = max_ii(channel->max_segment, len);
      segment++;
    }

    i++;
  }
}

/* Create channels for tracks and calculate tracked segments for them. */
static void tracking_dopesheet_channels_calc(MovieTracking *tracking)
{
  MovieTrackingObject *object = KERNEL_tracking_object_get_active(tracking);
  MovieTrackingDopesheet *dopesheet = &tracking->dopesheet;
  MovieTrackingReconstruction *reconstruction = KERNEL_tracking_object_get_reconstruction(tracking,
                                                                                       object);
  ListBase *tracksbase = KERNEL_tracking_object_get_tracks(tracking, object);

  bool sel_only = (dopesheet->flag & TRACKING_DOPE_SELECTED_ONLY) != 0;
  bool show_hidden = (dopesheet->flag & TRACKING_DOPE_SHOW_HIDDEN) != 0;

  LISTBASE_FOREACH (MovieTrackingTrack *, track, tracksbase) {
    if (!show_hidden && (track->flag & TRACK_HIDDEN) != 0) {
      continue;
    }

    if (sel_only && !TRACK_SELECTED(track)) {
      continue;
    }

    MovieTrackingDopesheetChannel *channel = MEM_callocN(sizeof(MovieTrackingDopesheetChannel),
                                                         "tracking dopesheet channel");
    channel->track = track;

    if (reconstruction->flag & TRACKING_RECONSTRUCTED) {
      LIB_snprintf(channel->name, sizeof(channel->name), "%s (%.4f)", track->name, track->error);
    }
    else {
      LIB_strncpy(channel->name, track->name, sizeof(channel->name));
    }

    tracking_dopesheet_channels_segments_calc(channel);

    LIB_addtail(&dopesheet->channels, channel);
    dopesheet->tot_channel++;
  }
}

/* Sot dopesheet channels using given method (name, average error, total coverage,
 * longest tracked segment) and could also inverse the list if it's enabled.
 */
static void tracking_dopesheet_channels_sort(MovieTracking *tracking,
                                             int sort_method,
                                             bool inverse)
{
  MovieTrackingDopesheet *dopesheet = &tracking->dopesheet;

  if (inverse) {
    if (sort_method == TRACKING_DOPE_SORT_NAME) {
      LIB_listbase_sort(&dopesheet->channels, channels_alpha_inverse_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_LONGEST) {
      LIB_listbase_sort(&dopesheet->channels, channels_longest_segment_inverse_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_TOTAL) {
      LIB_listbase_sort(&dopesheet->channels, channels_total_track_inverse_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_AVERAGE_ERROR) {
      LIB_listbase_sort(&dopesheet->channels, channels_average_error_inverse_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_START) {
      LIB_listbase_sort(&dopesheet->channels, channels_start_inverse_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_END) {
      LIB_listbase_sort(&dopesheet->channels, channels_end_inverse_sort);
    }
  }
  else {
    if (sort_method == TRACKING_DOPE_SORT_NAME) {
      LIB_listbase_sort(&dopesheet->channels, channels_alpha_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_LONGEST) {
      LIB_listbase_sort(&dopesheet->channels, channels_longest_segment_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_TOTAL) {
      LIB_listbase_sort(&dopesheet->channels, channels_total_track_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_AVERAGE_ERROR) {
      LIB_listbase_sort(&dopesheet->channels, channels_average_error_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_START) {
      LIB_listbase_sort(&dopesheet->channels, channels_start_sort);
    }
    else if (sort_method == TRACKING_DOPE_SORT_END) {
      LIB_listbase_sort(&dopesheet->channels, channels_end_sort);
    }
  }
}

static int coverage_from_count(int count)
{
  /* Values are actually arbitrary here, probably need to be tweaked. */
  if (count < 8) {
    return TRACKING_COVERAGE_BAD;
  }
  if (count < 16) {
    return TRACKING_COVERAGE_ACCEPTABLE;
  }
  return TRACKING_COVERAGE_OK;
}

/* Calculate coverage of frames with tracks, this information
 * is used to highlight dopesheet background depending on how
 * many tracks exists on the frame.
 */
static void tracking_dopesheet_calc_coverage(MovieTracking *tracking)
{
  MovieTrackingDopesheet *dopesheet = &tracking->dopesheet;
  MovieTrackingObject *object = KERNEL_tracking_object_get_active(tracking);
  ListBase *tracksbase = KERNEL_tracking_object_get_tracks(tracking, object);
  int frames, start_frame = INT_MAX, end_frame = -INT_MAX;
  int *per_frame_counter;
  int prev_coverage, last_segment_frame;

  /* find frame boundaries */
  LISTBASE_FOREACH (MovieTrackingTrack *, track, tracksbase) {
    start_frame = min_ii(start_frame, track->markers[0].framenr);
    end_frame = max_ii(end_frame, track->markers[track->markersnr - 1].framenr);
  }

  if (start_frame > end_frame) {
    /* There are no markers at all, nothing to calculate coverage from. */
    return;
  }

  frames = end_frame - start_frame + 1;

  /* this is a per-frame counter of markers (how many markers belongs to the same frame) */
  per_frame_counter = MEM_callocN(sizeof(int) * frames, "per frame track counter");

  /* find per-frame markers count */
  LISTBASE_FOREACH (MovieTrackingTrack *, track, tracksbase) {
    for (int i = 0; i < track->markersnr; i++) {
      MovieTrackingMarker *marker = &track->markers[i];

      /* TODO: perhaps we need to add check for non-single-frame track here */
      if ((marker->flag & MARKER_DISABLED) == 0) {
        per_frame_counter[marker->framenr - start_frame]++;
      }
    }
  }

  /* convert markers count to coverage and detect segments with the same coverage */
  prev_coverage = coverage_from_count(per_frame_counter[0]);
  last_segment_frame = start_frame;

  /* means only disabled tracks in the beginning, could be ignored */
  if (!per_frame_counter[0]) {
    prev_coverage = TRACKING_COVERAGE_OK;
  }

  for (int i = 1; i < frames; i++) {
    int coverage = coverage_from_count(per_frame_counter[i]);

    /* means only disabled tracks in the end, could be ignored */
    if (i == frames - 1 && !per_frame_counter[i]) {
      coverage = TRACKING_COVERAGE_OK;
    }

    if (coverage != prev_coverage || i == frames - 1) {
      MovieTrackingDopesheetCoverageSegment *coverage_segment;
      int end_segment_frame = i - 1 + start_frame;

      if (end_segment_frame == last_segment_frame) {
        end_segment_frame++;
      }

      coverage_segment = MEM_callocN(sizeof(MovieTrackingDopesheetCoverageSegment),
                                     "tracking coverage segment");
      coverage_segment->coverage = prev_coverage;
      coverage_segment->start_frame = last_segment_frame;
      coverage_segment->end_frame = end_segment_frame;

      LIB_addtail(&dopesheet->coverage_segments, coverage_segment);

      last_segment_frame = end_segment_frame;
    }

    prev_coverage = coverage;
  }

  MEM_freeN(per_frame_counter);
}

void KERNEL_tracking_dopesheet_tag_update(MovieTracking *tracking)
{
  MovieTrackingDopesheet *dopesheet = &tracking->dopesheet;

  dopesheet->ok = false;
}

void KERNEL_tracking_dopesheet_update(MovieTracking *tracking)
{
  MovieTrackingDopesheet *dopesheet = &tracking->dopesheet;

  short sort_method = dopesheet->sort_method;
  bool inverse = (dopesheet->flag & TRACKING_DOPE_SORT_INVERSE) != 0;

  if (dopesheet->ok) {
    return;
  }

  tracking_dopesheet_free(dopesheet);

  /* channels */
  tracking_dopesheet_channels_calc(tracking);
  tracking_dopesheet_channels_sort(tracking, sort_method, inverse);

  /* frame coverage */
  tracking_dopesheet_calc_coverage(tracking);

  dopesheet->ok = true;
}

MovieTrackingObject *KERNEL_tracking_find_object_for_track(const MovieTracking *tracking,
                                                        const MovieTrackingTrack *track)
{
  const ListBase *tracksbase = &tracking->tracks;
  if (LIB_findindex(tracksbase, track) != -1) {
    return NULL;
  }
  MovieTrackingObject *object = tracking->objects.first;
  while (object != NULL) {
    if (LIB_findindex(&object->tracks, track) != -1) {
      return object;
    }
    object = object->next;
  }
  return NULL;
}

ListBase *KERNEL_tracking_find_tracks_list_for_track(MovieTracking *tracking,
                                                  const MovieTrackingTrack *track)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_track(tracking, track);
  if (object != NULL) {
    return &object->tracks;
  }
  return &tracking->tracks;
}

MovieTrackingObject *KERNEL_tracking_find_object_for_plane_track(
    const MovieTracking *tracking, const MovieTrackingPlaneTrack *plane_track)
{
  const ListBase *plane_tracks_base = &tracking->plane_tracks;
  if (LIB_findindex(plane_tracks_base, plane_track) != -1) {
    return NULL;
  }
  MovieTrackingObject *object = tracking->objects.first;
  while (object != NULL) {
    if (LIB_findindex(&object->plane_tracks, plane_track) != -1) {
      return object;
    }
    object = object->next;
  }
  return NULL;
}

ListBase *KERNEL_tracking_find_tracks_list_for_plane_track(MovieTracking *tracking,
                                                        const MovieTrackingPlaneTrack *plane_track)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_plane_track(tracking, plane_track);
  if (object != NULL) {
    return &object->plane_tracks;
  }
  return &tracking->plane_tracks;
}

void KERNEL_tracking_get_api_path_for_track(const struct MovieTracking *tracking,
                                         const struct MovieTrackingTrack *track,
                                         char *api_path,
                                         size_t api_path_len)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_track(tracking, track);
  char track_name_esc[MAX_NAME * 2];
  LIB_str_escape(track_name_esc, track->name, sizeof(track_name_esc));
  if (object == NULL) {
    LIB_snprintf(api_path, api_path_len, "tracking.tracks[\"%s\"]", track_name_esc);
  }
  else {
    char object_name_esc[MAX_NAME * 2];
    LIB_str_escape(object_name_esc, object->name, sizeof(object_name_esc));
    LIB_snprintf(api_path,
                 api_path_len,
                 "tracking.objects[\"%s\"].tracks[\"%s\"]",
                 object_name_esc,
                 track_name_esc);
  }
}

void KERNEL_tracking_get_api_path_prefix_for_track(const struct MovieTracking *tracking,
                                                const struct MovieTrackingTrack *track,
                                                char *api_path,
                                                size_t api_path_len)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_track(tracking, track);
  if (object == NULL) {
    LIB_strncpy(rna_path, "tracking.tracks", api_path_len);
  }
  else {
    char object_name_esc[MAX_NAME * 2];
    LIB_str_escape(object_name_esc, object->name, sizeof(object_name_esc));
    LIB_snprintf(api_path, api_path_len, "tracking.objects[\"%s\"]", object_name_esc);
  }
}

void KERNEL_tracking_get_api_path_for_plane_track(const struct MovieTracking *tracking,
                                               const struct MovieTrackingPlaneTrack *plane_track,
                                               char *api_path,
                                               size_t api_path_len)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_plane_track(tracking, plane_track);
  char track_name_esc[MAX_NAME * 2];
  LIB_str_escape(track_name_esc, plane_track->name, sizeof(track_name_esc));
  if (object == NULL) {
    LIB_snprintf(api_path, api_path_len, "tracking.plane_tracks[\"%s\"]", track_name_esc);
  }
  else {
    char object_name_esc[MAX_NAME * 2];
    LIB_str_escape(object_name_esc, object->name, sizeof(object_name_esc));
    LIB_snprintf(api_path,
                 api_path_len,
                 "tracking.objects[\"%s\"].plane_tracks[\"%s\"]",
                 object_name_esc,
                 track_name_esc);
  }
}

void KERNEL_tracking_get_api_path_prefix_for_plane_track(
    const struct MovieTracking *tracking,
    const struct MovieTrackingPlaneTrack *plane_track,
    char *api_path,
    size_t api_path_len)
{
  MovieTrackingObject *object = KERNEL_tracking_find_object_for_plane_track(tracking, plane_track);
  if (object == NULL) {
    LIB_strncpy(api_path, "tracking.plane_tracks", api_path_len);
  }
  else {
    char object_name_esc[MAX_NAME * 2];
    LIB_str_escape(object_name_esc, object->name, sizeof(object_name_esc));
    LIB_snprintf(api_path, api_path_len, "tracking.objects[\"%s\"].plane_tracks", object_name_esc);
  }
}
