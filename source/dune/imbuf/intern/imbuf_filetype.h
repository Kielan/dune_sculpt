#pragma once

#include "imbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/** Generic File Type **/

struct ImBuf;

#define IM_FTYPE_FLOAT 1

typedef struct ImFileType {
  /** Optional, called once when initializing. */
  void (*init)(void);
  /** Optional, called once when exiting. */
  void (*exit)(void);

  /**
   * Check if the data matches this file types 'magic',
   * note that this may only read in a small part of the files header,
   * see: imbuf_ispic_type for details.
   */
  bool (*is_a)(const unsigned char *buf, size_t size);

  /** Load an image from memory. */
  struct ImBuf *(*load)(const unsigned char *mem,
                        size_t size,
                        int flags,
                        char colorspace[IM_MAX_SPACE]);
  /** Load an image from a file. */
  struct ImBuf *(*load_filepath)(const char *filepath, int flags, char colorspace[IM_MAX_SPACE]);
  /**
   * Load/Create a thumbnail image from a filepath. `max_thumb_size` is maximum size of either
   * dimension, so can return less on either or both. Should, if possible and performant, return
   * dimensions of the full-size image in r_width & r_height.
   */
  struct ImBuf *(*load_filepath_thumbnail)(const char *filepath,
                                           int flags,
                                           size_t max_thumb_size,
                                           char colorspace[IM_MAX_SPACE],
                                           size_t *r_width,
                                           size_t *r_height);
  /** Save to a file (or memory if #IB_mem is set in `flags` and the format supports it). */
  bool (*save)(struct ImBuf *ibuf, const char *filepath, int flags);

  int flag;

  /** #eImbFileType */
  int filetype;

  int default_save_role;
} ImFileType;

extern const ImFileType IMB_FILE_TYPES[];
extern const ImFileType *IMB_FILE_TYPES_LAST;

const ImFileType *imbuf_file_type_from_ftype(int ftype);
const ImFileType *imbuf_file_type_from_ibuf(const struct ImBuf *ibuf);

void imbuf_filetypes_init(void);
void imbuf_filetypes_exit(void);

/* Type Specific Functions */

/* -------------------------------------------------------------------- */
/** Format: PNG (IMB_FTYPE_PNG) * */

bool imbuf_is_a_png(const unsigned char *mem, size_t size);
struct ImBuf *imb_load_png(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
bool imbuf_save_png(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: TARGA (IMB_FTYPE_TGA) **/

bool imbuf_is_a_tga(const unsigned char *mem, size_t size);
struct ImBuf *imb_load_tga(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
bool imbuf_save_tga(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: IRIS (IMB_FTYPE_IMAGIC) **/

bool imb_is_a_iris(const unsigned char *mem, size_t size);
/** Read in a B/W RGB or RGBA iris image file and return an image buffer. */
struct ImBuf *imbuf_loadiris(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
bool imbuf_saveiris(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: JP2 (IMB_FTYPE_JP2) **/

bool imbuf_is_a_jp2(const unsigned char *buf, size_t size);
struct ImBuf *imbuf_load_jp2(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
struct ImBuf *imbuf_load_jp2_filepath(const char *filepath,
                                    int flags,
                                    char colorspace[IM_MAX_SPACE]);
bool imbuf_save_jp2(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: JPEG (IMB_FTYPE_JPG) */

bool imbuf_is_a_jpeg(const unsigned char *mem, size_t size);
bool imbuf_savejpeg(struct ImBuf *ibuf, const char *filepath, int flags);
struct ImBuf *imbuf_load_jpeg(const unsigned char *buffer,
                            size_t size,
                            int flags,
                            char colorspace[IM_MAX_SPACE]);
struct ImBuf *imbuf_thumbnail_jpeg(const char *filepath,
                                 int flags,
                                 size_t max_thumb_size,
                                 char colorspace[IM_MAX_SPACE],
                                 size_t *r_width,
                                 size_t *r_height);


/* -------------------------------------------------------------------- */
/** Format: BMP (IMB_FTYPE_BMP) **/

bool imbuf_is_a_bmp(const unsigned char *buf, size_t size);
struct ImBuf *imbuf_load_bmp(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
/* Found write info at http://users.ece.gatech.edu/~slabaugh/personal/c/bitmapUnix.c */
bool imb_save_bmp(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: CINEON (IMB_FTYPE_CINEON) **/

bool imbuf_is_a_cineon(const unsigned char *buf, size_t size);
bool imbuf_save_cineon(struct ImBuf *buf, const char *filepath, int flags);
struct ImBuf *imbuf_load_cineon(const unsigned char *mem,
                              size_t size,
                              int flags,
                              char colorspace[IM_MAX_SPACE]);

/* -------------------------------------------------------------------- */
/** Format: DPX (IMB_FTYPE_DPX) **/

bool imbuf_is_a_dpx(const unsigned char *buf, size_t size);
bool imbuf_save_dpx(struct ImBuf *ibuf, const char *filepath, int flags);
struct ImBuf *imbuf_load_dpx(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);

/* -------------------------------------------------------------------- */
/** Format: HDR (IMB_FTYPE_RADHDR) **/

bool imbuf_is_a_hdr(const unsigned char *buf, size_t size);
struct ImBuf *imb_load_hdr(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
bool imbuf_save_hdr(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: TIFF (IMB_FTYPE_TIF) **/

bool imbuf_is_a_tiff(const unsigned char *buf, size_t size);
/**
 * Loads a TIFF file.
 * param mem: Memory containing the TIFF file.
 * param size: Size of the mem buffer.
 * param flags: If flags has IB_test set then the file is not actually loaded,
 * but all other operations take place.
 *
 * return A newly allocated #ImBuf structure if successful, otherwise NULL.
 */
struct ImBuf *imbuf_load_tiff(const unsigned char *mem,
                            size_t size,
                            int flags,
                            char colorspace[IM_MAX_SPACE]);
/**
 * Saves a TIFF file.
 *
 * #ImBuf structures with 1, 3 or 4 bytes per pixel (GRAY, RGB, RGBA respectively)
 * are accepted, and interpreted correctly. Note that the TIFF convention is to use
 * pre-multiplied alpha, which can be achieved within Blender by setting `premul` alpha handling.
 * Other alpha conventions are not strictly correct, but are permitted anyhow.
 *
 * param ibuf: Image buffer.
 * param filepath: Name of the TIFF file to create.
 * param flags: Currently largely ignored.
 *
 * return 1 if the function is successful, 0 on failure.
 */
bool imbuf_save_tiff(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: WEBP (IMB_FTYPE_WEBP) **/

bool imbuf_is_a_webp(const unsigned char *buf, size_t size);
struct ImBuf *imbuf_loadwebp(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);
struct ImBuf *imbuf_load_filepath_thumbnail_webp(const char *filepath,
                                               const int flags,
                                               const size_t max_thumb_size,
                                               char colorspace[],
                                               size_t *r_width,
                                               size_t *r_height);
bool imb_savewebp(struct ImBuf *ibuf, const char *filepath, int flags);

/* -------------------------------------------------------------------- */
/** Format: DDS (IMB_FTYPE_DDS) **/

bool imbuf_is_a_dds(const unsigned char *buf, size_t size);

struct ImBuf *imbuf_load_dds(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);

/* -------------------------------------------------------------------- */
/** Format: PSD (#IMB_FTYPE_PSD) **/

bool imbuf_is_a_psd(const unsigned char *buf, size_t size);

struct ImBuf *imbuf_load_psd(const unsigned char *mem,
                           size_t size,
                           int flags,
                           char colorspace[IM_MAX_SPACE]);

#ifdef __cplusplus
};
#endif
