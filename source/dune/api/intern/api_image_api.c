#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "types_packedFile.h"

#include "lib_path_util.h"
#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "dune_packedFile.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_image.h"
#  include "dune_image_format.h"
#  include "dune_main.h"
#  include "dune_scene.h"
#  include <errno.h>

#  include "imbuf_colormanagement.h"
#  include "imbuf.h"

#  include "types_image.h"
#  include "types_scene.h"

#  include "mem_guardedalloc.h"

static void api_ImagePackedFile_save(ImagePackedFile *imapf, Main *main, ReportList *reports)
{
  if (dune_packedfile_write_to_file(
          reports, dune_main_dunefile_path(main), imapf->filepath, imapf->packedfile, 0) !=
      RET_OK) {
    dune_reportf(reports, RPT_ERROR, "Could not save packed file to disk as '%s'", imapf->filepath);
  }
}

static void api_image_save_render(
    Image *image, Cxt *C, ReportList *reports, const char *path, Scene *scene)
{
  ImBuf *ibuf;

  if (scene == NULL) {
    scene = cxt_data_scene(C);
  }

  if (scene) {
    ImageUser iuser = {NULL};
    void *lock;

    iuser.scene = scene;

    ibuf = dune_image_acquire_ibuf(image, &iuser, &lock);

    if (ibuf == NULL) {
      dune_report(reports, RPT_ERROR, "Could not acquire buffer from image");
    }
    else {
      ImBuf *write_ibuf;

      ImageFormatData image_format;
      dune_image_format_init_for_write(&image_format, scene, NULL);

      write_ibuf = imbuf_colormanagement_imbuf_for_write(ibuf, true, true, &image_format);

      write_ibuf->planes = image_format.planes;
      write_ibuf->dither = scene->r.dither_intensity;

      if (!dune_imbuf_write(write_ibuf, path, &image_format)) {
        dune_reportf(reports, RPT_ERROR, "Could not write image: %s, '%s'", strerror(errno), path);
      }

      if (write_ibuf != ibuf) {
        imbuf_freeImBuf(write_ibuf);
      }

      dune_image_format_free(&image_format);
    }

    dune_image_release_ibuf(image, ibuf, lock);
  }
  else {
    dune_report(reports, RPT_ERROR, "Scene not in context, could not get save parameters");
  }
}

static void api_Image_save(Image *image, Main *main, Cxt *C, ReportList *reports)
{
  void *lock;

  ImBuf *ibuf = dune_image_acquire_ibuf(image, NULL, &lock);
  if (ibuf) {
    char filename[FILE_MAX];
    lib_strncpy(filename, image->filepath, sizeof(filename));
    lib_path_abs(filename, ID_DUNE_PATH(main, &image->id));

    /* NOTE: we purposefully ignore packed files here,
     * developers need to explicitly write them via 'packed_files' */
    if (imbuf_saveiff(ibuf, filename, ibuf->flags)) {
      image->type = IMA_TYPE_IMAGE;

      if (image->source == IMA_SRC_GENERATED) {
        image->source = IMA_SRC_FILE;
      }

      imbuf_colormanagement_colorspace_from_ibuf_ftype(&image->colorspace_settings, ibuf);

      ibuf->userflags &= ~IB_BITMAPDIRTY;
    }
    else {
      dune_reportf(reports,
                  RPT_ERROR,
                  "Image '%s' could not be saved to '%s'",
                  image->id.name + 2,
                  image->filepath);
    }
  }
  else {
    dune_reportf(reports, RPT_ERROR, "Image '%s' does not have any image data", image->id.name + 2);
  }

  dune_image_release_ibuf(image, ibuf, lock);
  wm_event_add_notifier(C, NC_IMAGE | NA_EDITED, image);
}

static void api_image_pack(
    Image *image, Main *main, Cxt *C, ReportList *reports, const char *data, int data_len)
{
  dune_image_free_packedfiles(image);

  if (data) {
    char *data_dup = mem_mallocn(sizeof(*data_dup) * (size_t)data_len, __func__);
    memcpy(data_dup, data, (size_t)data_len);
    dune_image_packfiles_from_mem(reports, image, data_dup, (size_t)data_len);
  }
  else if (dune_image_is_dirty(image)) {
    dune_image_memorypack(image);
  }
  else {
    dune_image_packfiles(reports, image, ID_DUNE_PATH(main, &image->id));
  }

  wm_event_add_notifier(C, NC_IMAGE | NA_EDITED, image);
}

static void api_image_unpack(Image *image, Main *bmain, ReportList *reports, int method)
{
  if (!dune_image_has_packedfile(image)) {
    dune_report(reports, RPT_ERROR, "Image not packed");
  }
  else if (dune_image_has_multiple_ibufs(image)) {
    dune_report(
        reports, RPT_ERROR, "Unpacking movies, image sequences or tiled images not supported");
    return;
  }
  else {
    /* reports its own error on failure */
    dune_packedfile_unpack_image(main, reports, image, method);
  }
}

static void api_image_reload(Image *image, Main *main)
{
  dune_image_signal(main, image, NULL, IMA_SIGNAL_RELOAD);
  wm_main_add_notifier(NC_IMAGE | NA_EDITED, image);
}

static void api_image_update(Image *image, ReportList *reports)
{
  ImBuf *ibuf = dune_image_acquire_ibuf(image, NULL, NULL);

  if (ibuf == NULL) {
    dune_reportf(reports, RPT_ERROR, "Image '%s' does not have any image data", image->id.name + 2);
    return;
  }

  if (ibuf->rect) {
    imbuf_rect_from_float(ibuf);
  }

  ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;

  dune_image_release_ibuf(image, ibuf, NULL);
}

static void api_image_scale(Image *image, ReportList *reports, int width, int height)
{
  if (!dune_image_scale(image, width, height)) {
    dune_reportf(reports, RPT_ERROR, "Image '%s' does not have any image data", image->id.name + 2);
  }
}

static int api_image_gl_load(
    Image *image, ReportList *reports, int frame, int layer_index, int pass_index)
{
  ImageUser iuser;
  dune_imageuser_default(&iuser);
  iuser.framenr = frame;
  iuser.layer = layer_index;
  iuser.pass = pass_index;
  if (image->rr != NULL) {
    dune_image_multilayer_index(image->rr, &iuser);
  }

  GPUTexture *tex = dune_image_get_gpu_texture(image, &iuser, NULL);

  if (tex == NULL) {
    dune_reportf(reports, RPT_ERROR, "Failed to load image texture '%s'", image->id.name + 2);
    /* TODO: this error code makes no sense for vulkan. */
    return 0x0502; /* GL_INVALID_OPERATION */
  }

  return 0; /* GL_NO_ERROR */
}

static int api_image_gl_touch(
    Image *image, ReportList *reports, int frame, int layer_index, int pass_index)
{
  int error = 0; /* GL_NO_ERROR */

  dune_image_tag_time(image);

  if (image->gputexture[TEXTARGET_2D][0][IMA_TEXTURE_RESOLUTION_FULL] == NULL) {
    error = api_image_gl_load(image, reports, frame, layer_index, pass_index);
  }

  return error;
}

static void api_image_gl_free(Image *image)
{
  dune_image_free_gputextures(image);

  /* remove the nocollect flag, image is available for garbage collection again */
  image->flag &= ~IMA_NOCOLLECT;
}

static void api_image_filepath_from_user(Image *image, ImageUser *image_user, char *filepath)
{
  dune_image_user_file_path(image_user, image, filepath);
}

static void api_image_buffers_free(Image *image)
{
  dune_image_free_buffers_ex(image, true);
}

#else

void api_image_packed_file(ApiStruct *sapi)
{
  ApiFn *fn;

  fn = api_def_fn(sapi, "save", "api_ImagePackedFile_save");
  api_def_fn_ui_description(fn, "Save the packed file to its filepath");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);
}

void api_image(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_function(srna, "save_render", "api_image_save_render");
  api_def_fn_ui_description(fn,
                            "Save image to a specific path using a scenes render settings");
  api_def_fn_flag(fn, FN_USE_CXT | FN_USE_REPORTS);
  parm = api_def_string_file_path(fn, "filepath", NULL, 0, "", "Save path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_ptr(fn, "scene", "Scene", "", "Scene to take image parameters from");

  fn = api_def_fn(sapi, "save", "api_image_save");
  api_def_fn_ui_description(fn, "Save image to its source path");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_CXT | FN_USE_REPORTS);

  fn = api_def_fn(sapi, "pack", "api_image_pack");
  api_def_fn_ui_description(fn, "Pack an image as embedded data into the .blend file");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_CXT | FN_USE_REPORTS);
  parm = api_def_prop(fn, "data", PROP_STRING, PROP_BYTESTRING);
  api_def_prop_ui_text(parm, "data", "Raw data (bytes, exact content of the embedded file)");
  api_def_int(fn,
              "data_len",
              0,
              0,
              INT_MAX,
              "data_len",
              "length of given data (mandatory if data is provided)",
              0,
              INT_MAX);

  fn = api_def_fn(sapi, "unpack", "api_Image_unpack");
  api_def_fn_ui_description(fn, "Save an image packed in the .dune file to disk");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);
  api_def_enum(
      fn, "method", api_enum_unpack_method_items, PF_USE_LOCAL, "method", "How to unpack");

  fn = api_def_fn(sapi, "reload", "api_image_reload");
  api_def_fn_flag(fn, FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Reload the image from its source path");

  fn = api_def_fn(sapi, "update", "api_image_update");
  api_def_fn_ui_description(fn, "Update the display image from the floating-point buffer");
  api_def_fn_flag(fn, FN_USE_REPORTS);

  fn = api_def_fn(sapi, "scale", "api_Image_scale");
  api_def_fn_ui_description(fn, "Scale the image in pixels");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_int(fn, "width", 1, 1, INT_MAX, "", "Width", 1, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "height", 1, 1, INT_MAX, "", "Height", 1, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "gl_touch", "api_Image_gl_touch");
  api_def_fn_ui_description(
      func, "Delay the image from being cleaned from the cache due inactivity");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_int(
      fn, "frame", 0, 0, INT_MAX, "Frame", "Frame of image sequence or movie", 0, INT_MAX);
  api_def_int(fn,
              "layer_index",
              0,
              0,
              INT_MAX,
              "Layer",
              "Index of layer that should be loaded",
              0,
              INT_MAX);
  api_def_int(fn,
              "pass_index",
              0,
              0,
              INT_MAX,
              "Pass",
              "Index of pass that should be loaded",
              0,
              INT_MAX);
  /* return value */
  parm = api_def_int(
      func, "error", 0, -INT_MAX, INT_MAX, "Error", "OpenGL error value", -INT_MAX, INT_MAX);
  apu_def_fn_return(fn, parm);

  func = RNA_def_function(srna, "gl_load", "rna_Image_gl_load");
  RNA_def_function_ui_description(
      func,
      "Load the image into an OpenGL texture. On success, image.bindcode will contain the "
      "OpenGL texture bindcode. Colors read from the texture will be in scene linear color space "
      "and have premultiplied or straight alpha matching the image alpha mode");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_int(
      func, "frame", 0, 0, INT_MAX, "Frame", "Frame of image sequence or movie", 0, INT_MAX);
  RNA_def_int(func,
              "layer_index",
              0,
              0,
              INT_MAX,
              "Layer",
              "Index of layer that should be loaded",
              0,
              INT_MAX);
  RNA_def_int(func,
              "pass_index",
              0,
              0,
              INT_MAX,
              "Pass",
              "Index of pass that should be loaded",
              0,
              INT_MAX);
  /* return value */
  parm = RNA_def_int(
      func, "error", 0, -INT_MAX, INT_MAX, "Error", "OpenGL error value", -INT_MAX, INT_MAX);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "gl_free", "rna_Image_gl_free");
  RNA_def_function_ui_description(func, "Free the image from OpenGL graphics memory");

  /* path to an frame specified by image user */
  func = RNA_def_function(srna, "filepath_from_user", "rna_Image_filepath_from_user");
  RNA_def_function_ui_description(
      func,
      "Return the absolute path to the filepath of an image frame specified by the image user");
  RNA_def_pointer(
      func, "image_user", "ImageUser", "", "Image user of the image to get filepath for");
  parm = RNA_def_string_file_path(func,
                                  "filepath",
                                  NULL,
                                  FILE_MAX,
                                  "File Path",
                                  "The resulting filepath from the image and its user");
  RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0); /* needed for string return value */
  RNA_def_function_output(func, parm);

  func = RNA_def_function(srna, "buffers_free", "rna_Image_buffers_free");
  RNA_def_function_ui_description(func, "Free the image buffers from memory");

  /* TODO: pack/unpack, maybe should be generic functions? */
}

#endif
