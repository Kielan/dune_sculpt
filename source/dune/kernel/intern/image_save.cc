#include <cerrno>
#include <cstring>

#include "LIB_listbase.h"
#include "LIB_path_util.h"
#include "LIB_string.h"

#include "structs_image_types.h"

#include "MEM_guardedalloc.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_openexr.h"

#include "KERNEL_colortools.h"
#include "KERNEL_image.h"
#include "KERNEL_image_format.h"
#include "KERNEL_image_save.h"
#include "KERNEL_main.h"
#include "KERNEL_report.h"
#include "KERNEL_scene.h"

#include "RE_pipeline.h"

void KERNEL_image_save_options_init(ImageSaveOptions *opts, Main *dunemain, Scene *scene)
{
  memset(opts, 0, sizeof(*opts));

  opts->dunemain = dunemain;
  opts->scene = scene;

  KERNEL_image_format_init(&opts->im_format, false);
}

void KERNEL_image_save_options_free(ImageSaveOptions *opts)
{
  KERNEL_image_format_free(&opts->im_format);
}

static void image_save_post(ReportList *reports,
                            Image *ima,
                            ImBuf *ibuf,
                            int ok,
                            ImageSaveOptions *opts,
                            int save_copy,
                            const char *filepath,
                            bool *r_colorspace_changed)
{
  if (!ok) {
    KERNEL_reportf(reports, RPT_ERROR, "Could not write image: %s", strerror(errno));
    return;
  }

  if (save_copy) {
    return;
  }

  if (opts->do_newpath) {
    LIB_strncpy(ibuf->name, filepath, sizeof(ibuf->name));
    LIB_strncpy(ima->filepath, filepath, sizeof(ima->filepath));
  }

  ibuf->userflags &= ~IB_BITMAPDIRTY;

  /* change type? */
  if (ima->type == IMA_TYPE_R_RESULT) {
    ima->type = IMA_TYPE_IMAGE;

    /* workaround to ensure the render result buffer is no longer used
     * by this image, otherwise can crash when a new render result is
     * created. */
    if (ibuf->rect && !(ibuf->mall & IB_rect)) {
      imb_freerectImBuf(ibuf);
    }
    if (ibuf->rect_float && !(ibuf->mall & IB_rectfloat)) {
      imb_freerectfloatImBuf(ibuf);
    }
    if (ibuf->zbuf && !(ibuf->mall & IB_zbuf)) {
      IMB_freezbufImBuf(ibuf);
    }
    if (ibuf->zbuf_float && !(ibuf->mall & IB_zbuffloat)) {
      IMB_freezbuffloatImBuf(ibuf);
    }
  }
  if (ELEM(ima->source, IMA_SRC_GENERATED, IMA_SRC_VIEWER)) {
    ima->source = IMA_SRC_FILE;
    ima->type = IMA_TYPE_IMAGE;
  }

  /* only image path, never ibuf */
  if (opts->relative) {
    const char *relbase = ID_DUNE_PATH(opts->dunemain, &ima->id);
    LIB_path_rel(ima->filepath, relbase); /* only after saving */
  }

  ColorManagedColorspaceSettings old_colorspace_settings;
  KERNEL m_color_managed_colorspace_settings_copy(&old_colorspace_settings, &ima->colorspace_settings);
  IMB_colormanagement_colorspace_from_ibuf_ftype(&ima->colorspace_settings, ibuf);
  if (!KERNEL_color_managed_colorspace_settings_equals(&old_colorspace_settings,
                                                    &ima->colorspace_settings)) {
    *r_colorspace_changed = true;
  }
}

static void imbuf_save_post(ImBuf *ibuf, ImBuf *colormanaged_ibuf)
{
  if (colormanaged_ibuf != ibuf) {
    /* This guys might be modified by image buffer write functions,
     * need to copy them back from color managed image buffer to an
     * original one, so file type of image is being properly updated.
     */
    ibuf->ftype = colormanaged_ibuf->ftype;
    ibuf->foptions = colormanaged_ibuf->foptions;
    ibuf->planes = colormanaged_ibuf->planes;

    IMB_freeImBuf(colormanaged_ibuf);
  }
}

/**
 * return success.
 * note `ima->filepath` and `ibuf->name` should end up the same.
 * note for multi-view the first `ibuf` is important to get the settings.
 */
static bool image_save_single(ReportList *reports,
                              Image *ima,
                              ImageUser *iuser,
                              ImageSaveOptions *opts,
                              bool *r_colorspace_changed)
{
  void *lock;
  ImBuf *ibuf = KERNEL_image_acquire_ibuf(ima, iuser, &lock);
  RenderResult *rr = nullptr;
  bool ok = false;

  if (ibuf == nullptr || (ibuf->rect == nullptr && ibuf->rect_float == nullptr)) {
    KERNEL_image_release_ibuf(ima, ibuf, lock);
    return ok;
  }

  ImBuf *colormanaged_ibuf = nullptr;
  const bool save_copy = opts->save_copy;
  const bool save_as_render = opts->save_as_render;
  ImageFormatData *imf = &opts->im_format;

  if (ima->type == IMA_TYPE_R_RESULT) {
    /* enforce user setting for RGB or RGBA, but skip BW */
    if (opts->im_format.planes == R_IMF_PLANES_RGBA) {
      ibuf->planes = R_IMF_PLANES_RGBA;
    }
    else if (opts->im_format.planes == R_IMF_PLANES_RGB) {
      ibuf->planes = R_IMF_PLANES_RGB;
    }
  }
  else {
    /* TODO: better solution, if a 24bit image is painted onto it may contain alpha. */
    if ((opts->im_format.planes == R_IMF_PLANES_RGBA) &&
        /* it has been painted onto */
        (ibuf->userflags & IB_BITMAPDIRTY)) {
      /* checks each pixel, not ideal */
      ibuf->planes = KERNEL_imbuf_alpha_test(ibuf) ? R_IMF_PLANES_RGBA : R_IMF_PLANES_RGB;
    }
  }

  /* we need renderresult for exr and rendered multiview */
  rr = KERNEL_image_acquire_renderresult(opts->scene, ima);
  bool is_mono = rr ? LIB_listbase_count_at_most(&rr->views, 2) < 2 :
                      LIB_listbase_count_at_most(&ima->views, 2) < 2;
  bool is_exr_rr = rr && ELEM(imf->imtype, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER) &&
                   RE_HasFloatPixels(rr);
  bool is_multilayer = is_exr_rr && (imf->imtype == R_IMF_IMTYPE_MULTILAYER);
  int layer = (iuser && !is_multilayer) ? iuser->layer : -1;

  /* error handling */
  if (rr == nullptr) {
    if (imf->imtype == R_IMF_IMTYPE_MULTILAYER) {
      KERNEL_report(reports, RPT_ERROR, "Did not write, no Multilayer Image");
      KERNEL_image_release_ibuf(ima, ibuf, lock);
      return ok;
    }
  }
  else {
    if (imf->views_format == R_IMF_VIEWS_STEREO_3D) {
      if (!KERNEL_image_is_stereo(ima)) {
        KERNEL_reportf(reports,
                    RPT_ERROR,
                    R"(Did not write, the image doesn't have a "%s" and "%s" views)",
                    STEREO_LEFT_NAME,
                    STEREO_RIGHT_NAME);
        KERNEL_image_release_ibuf(ima, ibuf, lock);
        KERNEL_image_release_renderresult(opts->scene, ima);
        return ok;
      }

      /* It shouldn't ever happen. */
      if ((LIB_findstring(&rr->views, STEREO_LEFT_NAME, offsetof(RenderView, name)) == nullptr) ||
          (LIB_findstring(&rr->views, STEREO_RIGHT_NAME, offsetof(RenderView, name)) == nullptr)) {
        KERNEL_reportf(reports,
                    RPT_ERROR,
                    R"(Did not write, the image doesn't have a "%s" and "%s" views)",
                    STEREO_LEFT_NAME,
                    STEREO_RIGHT_NAME);
        KERNEL_image_release_ibuf(ima, ibuf, lock);
        KERNEL_image_release_renderresult(opts->scene, ima);
        return ok;
      }
    }
    KERNEL_imbuf_stamp_info(rr, ibuf);
  }

  /* fancy multiview OpenEXR */
  if (imf->views_format == R_IMF_VIEWS_MULTIVIEW && is_exr_rr) {
    /* save render result */
    ok = KERNEL_image_render_write_exr(reports, rr, opts->filepath, imf, nullptr, layer);
    image_save_post(reports, ima, ibuf, ok, opts, true, opts->filepath, r_colorspace_changed);
    KERNEL_image_release_ibuf(ima, ibuf, lock);
  }
  /* regular mono pipeline */
  else if (is_mono) {
    if (is_exr_rr) {
      ok = KERNEL_image_render_write_exr(reports, rr, opts->filepath, imf, nullptr, layer);
    }
    else {
      colormanaged_ibuf = IMB_colormanagement_imbuf_for_write(ibuf, save_as_render, true, imf);
      ok = KERNEL_imbuf_write_as(colormanaged_ibuf, opts->filepath, imf, save_copy);
      imbuf_save_post(ibuf, colormanaged_ibuf);
    }
    image_save_post(reports,
                    ima,
                    ibuf,
                    ok,
                    opts,
                    (is_exr_rr ? true : save_copy),
                    opts->filepath,
                    r_colorspace_changed);
    KERNEL_image_release_ibuf(ima, ibuf, lock);
  }
  /* individual multiview images */
  else if (imf->views_format == R_IMF_VIEWS_INDIVIDUAL) {
    unsigned char planes = ibuf->planes;
    const int totviews = (rr ? LIB_listbase_count(&rr->views) : BLI_listbase_count(&ima->views));

    if (!is_exr_rr) {
      KERNEL_image_release_ibuf(ima, ibuf, lock);
    }

    for (int i = 0; i < totviews; i++) {
      char filepath[FILE_MAX];
      bool ok_view = false;
      const char *view = rr ? ((RenderView *)LIB_findlink(&rr->views, i))->name :
                              ((ImageView *)LIB_findlink(&ima->views, i))->name;

      if (is_exr_rr) {
        KERNEL_scene_multiview_view_filepath_get(&opts->scene->r, opts->filepath, view, filepath);
        ok_view = KERNEL_image_render_write_exr(reports, rr, filepath, imf, view, layer);
        image_save_post(reports, ima, ibuf, ok_view, opts, true, filepath, r_colorspace_changed);
      }
      else {
        /* copy iuser to get the correct ibuf for this view */
        ImageUser view_iuser;

        if (iuser) {
          /* copy iuser to get the correct ibuf for this view */
          view_iuser = *iuser;
        }
        else {
          KERNEL_imageuser_default(&view_iuser);
        }

        view_iuser.view = i;
        view_iuser.flag &= ~IMA_SHOW_STEREO;

        if (rr) {
          KERNEL_image_multilayer_index(rr, &view_iuser);
        }
        else {
          KERNEL_image_multiview_index(ima, &view_iuser);
        }

        ibuf = KERNEL_image_acquire_ibuf(ima, &view_iuser, &lock);
        ibuf->planes = planes;

        KERNEL_scene_multiview_view_filepath_get(&opts->scene->r, opts->filepath, view, filepath);

        colormanaged_ibuf = IMB_colormanagement_imbuf_for_write(ibuf, save_as_render, true, imf);
        ok_view = KERNEL_imbuf_write_as(colormanaged_ibuf, filepath, &opts->im_format, save_copy);
        imbuf_save_post(ibuf, colormanaged_ibuf);
        image_save_post(reports, ima, ibuf, ok_view, opts, true, filepath, r_colorspace_changed);
        KERNEL_image_release_ibuf(ima, ibuf, lock);
      }
      ok &= ok_view;
    }

    if (is_exr_rr) {
      KERNEL_image_release_ibuf(ima, ibuf, lock);
    }
  }
  /* stereo (multiview) images */
  else if (opts->im_format.views_format == R_IMF_VIEWS_STEREO_3D) {
    if (imf->imtype == R_IMF_IMTYPE_MULTILAYER) {
      ok = KERNEL_image_render_write_exr(reports, rr, opts->filepath, imf, nullptr, layer);
      image_save_post(reports, ima, ibuf, ok, opts, true, opts->filepath, r_colorspace_changed);
      KERNEL_image_release_ibuf(ima, ibuf, lock);
    }
    else {
      ImBuf *ibuf_stereo[2] = {nullptr};

      unsigned char planes = ibuf->planes;
      const char *names[2] = {STEREO_LEFT_NAME, STEREO_RIGHT_NAME};

      /* we need to get the specific per-view buffers */
      KERNEL_image_release_ibuf(ima, ibuf, lock);
      bool stereo_ok = true;

      for (int i = 0; i < 2; i++) {
        ImageUser view_iuser;

        if (iuser) {
          view_iuser = *iuser;
        }
        else {
          KERNEL_imageuser_default(&view_iuser);
        }

        view_iuser.flag &= ~IMA_SHOW_STEREO;

        if (rr) {
          int id = LIB_findstringindex(&rr->views, names[i], offsetof(RenderView, name));
          view_iuser.view = id;
          KERNEL_image_multilayer_index(rr, &view_iuser);
        }
        else {
          view_iuser.view = i;
          KERNEL_image_multiview_index(ima, &view_iuser);
        }

        ibuf = KERNEL_image_acquire_ibuf(ima, &view_iuser, &lock);

        if (ibuf == nullptr) {
          KERNEL_report(
              reports, RPT_ERROR, "Did not write, unexpected error when saving stereo image");
          KERNEL_image_release_ibuf(ima, ibuf, lock);
          stereo_ok = false;
          break;
        }

        ibuf->planes = planes;

        /* color manage the ImBuf leaving it ready for saving */
        colormanaged_ibuf = IMB_colormanagement_imbuf_for_write(ibuf, save_as_render, true, imf);

        KERNEL_image_format_to_imbuf(colormanaged_ibuf, imf);
        IMB_prepare_write_ImBuf(IMB_isfloat(colormanaged_ibuf), colormanaged_ibuf);

        /* duplicate buffer to prevent locker issue when using render result */
        ibuf_stereo[i] = IMB_dupImBuf(colormanaged_ibuf);

        imbuf_save_post(ibuf, colormanaged_ibuf);

        KERNEL_image_release_ibuf(ima, ibuf, lock);
      }

      if (stereo_ok) {
        ibuf = IMB_stereo3d_ImBuf(imf, ibuf_stereo[0], ibuf_stereo[1]);

        /* save via traditional path */
        ok = KERNEL_imbuf_write_as(ibuf, opts->filepath, imf, save_copy);

        IMB_freeImBuf(ibuf);
      }

      for (int i = 0; i < 2; i++) {
        IMB_freeImBuf(ibuf_stereo[i]);
      }
    }
  }

  return ok;
}

bool KERNEL_image_save(
    ReportList *reports, Main *bmain, Image *ima, ImageUser *iuser, ImageSaveOptions *opts)
{
  ImageUser save_iuser;
  KERNEL_imageuser_default(&save_iuser);

  bool colorspace_changed = false;

  eUDIM_TILE_FORMAT tile_format;
  char *udim_pattern = nullptr;

  if (ima->source == IMA_SRC_TILED) {
    /* Verify filepath for tiled images contains a valid UDIM marker. */
    udim_pattern = KERNEL_image_get_tile_strformat(opts->filepath, &tile_format);
    if (tile_format == UDIM_TILE_FORMAT_NONE) {
      KERNEL_reportf(reports,
                  RPT_ERROR,
                  "When saving a tiled image, the path '%s' must contain a valid UDIM marker",
                  opts->filepath);
      return false;
    }

    /* For saving a tiled image we need an iuser, so use a local one if there isn't already one.
     */
    if (iuser == nullptr) {
      iuser = &save_iuser;
    }
  }

  /* Save images */
  bool ok = false;
  if (ima->source != IMA_SRC_TILED) {
    ok = image_save_single(reports, ima, iuser, opts, &colorspace_changed);
  }
  else {
    char filepath[FILE_MAX];
    LIB_strncpy(filepath, opts->filepath, sizeof(filepath));

    /* Save all the tiles. */
    LISTBASE_FOREACH (ImageTile *, tile, &ima->tiles) {
      KERNEL_image_set_filepath_from_tile_number(
          opts->filepath, udim_pattern, tile_format, tile->tile_number);

      iuser->tile = tile->tile_number;
      ok = image_save_single(reports, ima, iuser, opts, &colorspace_changed);
      if (!ok) {
        break;
      }
    }
    LIB_strncpy(ima->filepath, filepath, sizeof(ima->filepath));
    LIB_strncpy(opts->filepath, filepath, sizeof(opts->filepath));
    MEM_freeN(udim_pattern);
  }

  if (colorspace_changed) {
    KERNEL_image_signal(dunemain, ima, nullptr, IMA_SIGNAL_COLORMANAGE);
  }

  return ok;
}

/* OpenEXR saving, single and multilayer. */

bool KERNEL_image_render_write_exr(ReportList *reports,
                                const RenderResult *rr,
                                const char *filename,
                                const ImageFormatData *imf,
                                const char *view,
                                int layer)
{
  void *exrhandle = IMB_exr_get_handle();
  const bool half_float = (imf && imf->depth == R_IMF_CHAN_DEPTH_16);
  const bool multi_layer = !(imf && imf->imtype == R_IMF_IMTYPE_OPENEXR);
  const bool write_z = !multi_layer && (imf && (imf->flag & R_IMF_FLAG_ZBUF));

  /* Write first layer if not multilayer and no layer was specified. */
  if (!multi_layer && layer == -1) {
    layer = 0;
  }

  /* First add views since IMB_exr_add_channel checks number of views. */
  const RenderView *first_rview = (const RenderView *)rr->views.first;
  if (first_rview && (first_rview->next || first_rview->name[0])) {
    LISTBASE_FOREACH (RenderView *, rview, &rr->views) {
      if (!view || STREQ(view, rview->name)) {
        IMB_exr_add_view(exrhandle, rview->name);
      }
    }
  }

  /* Compositing result. */
  if (rr->have_combined) {
    LISTBASE_FOREACH (RenderView *, rview, &rr->views) {
      if (!rview->rectf) {
        continue;
      }

      const char *viewname = rview->name;
      if (view) {
        if (!STREQ(view, viewname)) {
          continue;
        }

        viewname = "";
      }

      /* Skip compositing if only a single other layer is requested. */
      if (!multi_layer && layer != 0) {
        continue;
      }

      float *output_rect = rview->rectf;

      for (int a = 0; a < 4; a++) {
        char passname[EXR_PASS_MAXNAME];
        char layname[EXR_PASS_MAXNAME];
        const char *chan_id = "RGBA";

        if (multi_layer) {
          RE_render_result_full_channel_name(passname, nullptr, "Combined", nullptr, chan_id, a);
          LIB_strncpy(layname, "Composite", sizeof(layname));
        }
        else {
          passname[0] = chan_id[a];
          passname[1] = '\0';
          layname[0] = '\0';
        }

        IMB_exr_add_channel(
            exrhandle, layname, passname, viewname, 4, 4 * rr->rectx, output_rect + a, half_float);
      }

      if (write_z && rview->rectz) {
        const char *layname = (multi_layer) ? "Composite" : "";
        IMB_exr_add_channel(exrhandle, layname, "Z", viewname, 1, rr->rectx, rview->rectz, false);
      }
    }
  }

  /* Other render layers. */
  int nr = (rr->have_combined) ? 1 : 0;
  LISTBASE_FOREACH (RenderLayer *, rl, &rr->layers) {
    /* Skip other render layers if requested. */
    if (!multi_layer && nr != layer) {
      continue;
    }

    LISTBASE_FOREACH (RenderPass *, rp, &rl->passes) {
      /* Skip non-RGBA and Z passes if not using multi layer. */
      if (!multi_layer && !(STREQ(rp->name, RE_PASSNAME_COMBINED) || STREQ(rp->name, "") ||
                            (STREQ(rp->name, RE_PASSNAME_Z) && write_z))) {
        continue;
      }

      /* Skip pass if it does not match the requested view(s). */
      const char *viewname = rp->view;
      if (view) {
        if (!STREQ(view, viewname))      continue;
        }

        viewname = "";
      }

      /* We only store RGBA passes as half float, for
       * others precision loss can be problematic. */
      const bool pass_RGBA = (STR_ELEM(rp->chan_id, "RGB", "RGBA", "R", "G", "B", "A"));
      const bool pass_half_float = half_float && pass_RGBA;

      float *output_rect = rp->rect;

      for (int a = 0; a < rp->channels; a++) {
        /* Save Combined as RGBA if single layer save. */
        char passname[EXR_PASS_MAXNAME];
        char layname[EXR_PASS_MAXNAME];

        if (multi_layer) {
          RE_render_result_full_channel_name(passname, nullptr, rp->name, nullptr, rp->chan_id, a);
          LIB_strncpy(layname, rl->name, sizeof(layname));
        }
        else {
          passname[0] = rp->chan_id[a];
          passname[1] = '\0';
          layname[0] = '\0';
        }

        IMB_exr_add_channel(exrhandle,
                            layname,
                            passname,
                            viewname,
                            rp->channels,
                            rp->channels * rr->rectx,
                            output_rect + a,
                            pass_half_float);
      }
    }
  }

  errno = 0;

  LIB_make_existing_file(filename);

  int compress = (imf ? imf->exr_codec : 0);
  bool success = IMB_exr_begin_write(
      exrhandle, filename, rr->rectx, rr->recty, compress, rr->stamp_data);
  if (success) {
    IMB_exr_write_channels(exrhandle);
  }
  else {
    /* TODO: get the error from openexr's exception. */
    KERNEL_reportf(
        reports, RPT_ERROR, "Error writing render result, %s (see console)", strerror(errno));
  }

  IMB_exr_close(exrhandle);
  return success;
}

/* Render output. */

static void image_render_print_save_message(ReportList *reports, const char *name, int ok, int err)
{
  if (ok) {
    /* no need to report, just some helpful console info */
    printf("Saved: '%s'\n", name);
  }
  else {
    /* report on error since users will want to know what failed */
    KERNEL_reportf(reports, RPT_ERROR, "Render error (%s) cannot save: '%s'", strerror(err), name);
  }
}

static int image_render_write_stamp_test(ReportList *reports,
                                         const Scene *scene,
                                         const RenderResult *rr,
                                         ImBuf *ibuf,
                                         const char *name,
                                         const ImageFormatData *imf,
                                         const bool stamp)
{
  int ok;

  if (stamp) {
    /* writes the name of the individual cameras */
    ok = KERNEL_imbuf_write_stamp(scene, rr, ibuf, name, imf);
  }
  else {
    ok = KERNEL_imbuf_write(ibuf, name, imf);
  }

  image_render_print_save_message(reports, name, ok, errno);

  return ok;
}

bool KERNEL_image_render_write(ReportList *reports,
                            RenderResult *rr,
                            const Scene *scene,
                            const bool stamp,
                            const char *filename)
{
  bool ok = true;

  if (!rr) {
    return false;
  }

  ImageFormatData image_format;
  KERNEL_image_format_init_for_write(&image_format, scene, nullptr);

  const bool is_mono = LIB_listbase_count_at_most(&rr->views, 2) < 2;
  const bool is_exr_rr = ELEM(
                             image_format.imtype, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER) &&
                         RE_HasFloatPixels(rr);
  const float dither = scene->r.dither_intensity;

  if (image_format.views_format == R_IMF_VIEWS_MULTIVIEW && is_exr_rr) {
    ok = KERNEL_image_render_write_exr(reports, rr, filename, &image_format, nullptr, -1);
    image_render_print_save_message(reports, filename, ok, errno);
  }

  /* mono, legacy code */
  else if (is_mono || (image_format.views_format == R_IMF_VIEWS_INDIVIDUAL)) {
    int view_id = 0;
    for (const RenderView *rv = (const RenderView *)rr->views.first; rv;
         rv = rv->next, view_id++) {
      char filepath[FILE_MAX];
      if (is_mono) {
        STRNCPY(filepath, filename);
      }
      else {
        KERNEL_scene_multiview_view_filepath_get(&scene->r, filename, rv->name, filepath);
      }

      if (is_exr_rr) {
        ok = KERNEL_image_render_write_exr(reports, rr, filepath, &image_format, rv->name, -1);
        image_render_print_save_message(reports, filepath, ok, errno);

        /* optional preview images for exr */
        if (ok && (image_format.flag & R_IMF_FLAG_PREVIEW_JPG)) {
          image_format.imtype = R_IMF_IMTYPE_JPEG90;

          if (LIB_path_extension_check(filepath, ".exr")) {
            filepath[strlen(filepath) - 4] = 0;
          }
          KERNEL_image_path_ensure_ext_from_imformat(filepath, &image_format);

          ImBuf *ibuf = RE_render_result_rect_to_ibuf(rr, &image_format, dither, view_id);
          ibuf->planes = 24;
          IMB_colormanagement_imbuf_for_write(ibuf, true, false, &image_format);

          ok = image_render_write_stamp_test(
              reports, scene, rr, ibuf, filepath, &image_format, stamp);

          IMB_freeImBuf(ibuf);
        }
      }
      else {
        ImBuf *ibuf = RE_render_result_rect_to_ibuf(rr, &image_format, dither, view_id);

        IMB_colormanagement_imbuf_for_write(ibuf, true, false, &image_format);

        ok = image_render_write_stamp_test(
            reports, scene, rr, ibuf, filepath, &image_format, stamp);

        /* imbuf knows which rects are not part of ibuf */
        IMB_freeImBuf(ibuf);
      }
    }
  }
  else { /* R_IMF_VIEWS_STEREO_3D */
    LIB_assert(image_format.views_format == R_IMF_VIEWS_STEREO_3D);

    char filepath[FILE_MAX];
    STRNCPY(filepath, filename);

    if (image_format.imtype == R_IMF_IMTYPE_MULTILAYER) {
      printf("Stereo 3D not supported for MultiLayer image: %s\n", filepath);
    }
    else {
      ImBuf *ibuf_arr[3] = {nullptr};
      const char *names[2] = {STEREO_LEFT_NAME, STEREO_RIGHT_NAME};
      int i;

      for (i = 0; i < 2; i++) {
        int view_id = LIB_findstringindex(&rr->views, names[i], offsetof(RenderView, name));
        ibuf_arr[i] = RE_render_result_rect_to_ibuf(rr, &image_format, dither, view_id);
        IMB_colormanagement_imbuf_for_write(ibuf_arr[i], true, false, &image_format);
        IMB_prepare_write_ImBuf(IMB_isfloat(ibuf_arr[i]), ibuf_arr[i]);
      }

      ibuf_arr[2] = IMB_stereo3d_ImBuf(&image_format, ibuf_arr[0], ibuf_arr[1]);

      ok = image_render_write_stamp_test(
          reports, scene, rr, ibuf_arr[2], filepath, &image_format, stamp);

      /* optional preview images for exr */
      if (ok && is_exr_rr && (image_format.flag & R_IMF_FLAG_PREVIEW_JPG)) {
        image_format.imtype = R_IMF_IMTYPE_JPEG90;

        if (LIB_path_extension_check(filepath, ".exr")) {
          filepath[strlen(filepath) - 4] = 0;
        }

        KERNEL_image_path_ensure_ext_from_imformat(filepath, &image_format);
        ibuf_arr[2]->planes = 24;

        ok = image_render_write_stamp_test(
            reports, scene, rr, ibuf_arr[2], filepath, &image_format, stamp);
      }

      /* imbuf knows which rects are not part of ibuf */
      for (i = 0; i < 3; i++) {
        IMB_freeImBuf(ibuf_arr[i]);
      }
    }
  }

  KERNEL_image_format_free(&image_format);

  return ok;
}
