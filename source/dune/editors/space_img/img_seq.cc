#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_fileops.h"
#include "lib_fileops_types.h"
#include "lib_list.h"
#include "lib_math_base.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "types_img.h"
#include "types_win.h"

#include "api_access.hh"

#include "dune_img.h"
#include "dune_main.h"

#include "ed_img.hh"

struct ImgFrame {
  ImgFrame *next, *prev;
  int framenr;
};

/* Get a list of frames from the list of img files matching the first file name seq pattern.
 * The files and directory are read from standard file-sel op props.
 * The output is a list of frame ranges, each containing a list of frames with matching names. */
static void img_seq_get_frame_ranges(WinOp *op, List *ranges)
{
  char dir[FILE_MAXDIR];
  const bool do_frame_range = api_bool_get(op->ptr, "use_seq_detection");
  ImgFrameRange *range = nullptr;
  int range_first_frame = 0;
  /* Track when a new series of files are found that aren't compatible with the previous file. */
  char base_head[FILE_MAX], base_tail[FILE_MAX];

  api_string_get(op->ptr, "directory", dir);
  API_BEGIN (op->ptr, itemptr, "files") {
    char head[FILE_MAX], tail[FILE_MAX];
    ushort digits;
    char *filename = api_string_get_alloc(&itemptr, "name", nullptr, 0, nullptr);
    ImgFrame *frame = static_cast<ImgFrame *>(mem_calloc(sizeof(ImgFrame), "img_frame"));

    /* use the first file in the list as base filename */
    frame->framenr = lib_path_seq_decode(
        filename, head, sizeof(head), tail, sizeof(tail), &digits);

    /* still in the same seq */
    if (do_frame_range && (range != nullptr) && STREQLEN(base_head, head, FILE_MAX) &&
        STREQLEN(base_tail, tail, FILE_MAX))
    {
      /* Set filepath to first frame in the range. */
      if (frame->framenr < range_first_frame) {
        lib_path_join(range->filepath, sizeof(range->filepath), dir, filename);
        range_first_frame = frame->framenr;
      }
    }
    else {
      /* start a new frame range */
      range = static_cast<ImgFrameRange *>(mem_calloc(sizeof(*range), __func__));
      lib_path_join(range->filepath, sizeof(range->filepath), dir, filename);
      lib_addtail(ranges, range);

      STRNCPY(base_head, head);
      STRNCPY(base_tail, tail);

      range_first_frame = frame->framenr;
    }

    lib_addtail(&range->frames, frame);
    mem_free(filename);
  }
  API_END;
}

static int img_cmp_frame(const void *a, const void *b)
{
  const ImgFrame *frame_a = static_cast<const ImgFrame *>(a);
  const ImgFrame *frame_b = static_cast<const ImgFrame *>(b);

  if (frame_a->framenr < frame_b->framenr) {
    return -1;
  }
  if (frame_a->framenr > frame_b->framenr) {
    return 1;
  }
  return 0;
}

/* From a list of frames, compute the start (offset) and length of the sequence
 * of contiguous frames. If `detect_udim` is set, it will return UDIM tiles as well. */
static void img_detect_frame_range(ImgFrameRange *range, const bool detect_udim)
{
  /* UDIM */
  if (detect_udim) {
    int udim_start, udim_range;
    range->udims_detected = dune_img_get_tile_info(
        range->filepath, &range->udim_tiles, &udim_start, &udim_range);

    if (range->udims_detected) {
      range->offset = udim_start;
      range->length = udim_range;
      return;
    }
  }

  /* Img Seq */
  lib_list_sort(&range->frames, img_cmp_frame);

  ImgFrame *frame = static_cast<ImgFrame *>(range->frames.first);
  if (frame != nullptr) {
    int frame_curr = frame->framenr;
    range->offset = frame_curr;

    while (frame != nullptr && (frame->framenr == frame_curr)) {
      frame_curr++;
      frame = frame->next;
    }

    range->length = frame_curr - range->offset;
  }
  else {
    range->length = 1;
    range->offset = 0;
  }
}

List ed_img_filesel_detect_seqs(Main *main, WinOp *op, const bool detect_udim)
{
  List ranges;
  lib_list_clear(&ranges);

  char filepath[FILE_MAX];
  api_string_get(op->ptr, "filepath", filepath);

  /* File browser. */
  if (api_struct_prop_is_set(op->ptr, "directory") &&
      api_struct_prop_is_set(op->ptr, "files"))
  {
    const bool was_relative = lib_path_is_rel(filepath);

    img_seq_get_frame_ranges(op, &ranges);
    LIST_FOREACH (ImgFrameRange *, range, &ranges) {
      img_detect_frame_range(range, detect_udim);
      lib_freelist(&range->frames);

      if (was_relative) {
        lib_path_rel(range->filepath, dune_main_file_path(main));
      }
    }
  }
  /* Filepath prop for drag & drop etc. */
  else {
    ImgFrameRange *range = static_cast<ImgFrameRange *>(mem_calloc(sizeof(*range), __func__));
    lib_addtail(&ranges, range);

    STRNCPY(range->filepath, filepath);
    image_detect_frame_range(range, detect_udim);
  }

  return ranges;
}
