/* Overview
 * - Each undo step is a ImgUndoStep
 * - Each ImgUndoStep stores a list of UndoImgHandle
 *   - Each UndoImgHandle stores a list of UndoImgBuf
 *     (this is the undo systems equivalent of an ImBuf).
 *     - Each UndoImgBuf stores an array of UndoImgTile
 *       The tiles are shared between UndoImgBuf's to avoid dup.
 *
 * When the undo sys manages an img, there will always be a full copy (as a UndoImgBuf)
 * each new undo step only stores mod tiles. */

#include "CLG_log.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_map.hh"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "types_img.h"
#include "types_ob.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_winmngr.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "dune_cxt.hh"
#include "dune_img.h"
#include "dune_paint.hh"
#include "dune_undo_system.h"

#include "graph.hh"

#include "ed_ob.hh"
#include "ed_paint.hh"
#include "ed_undo.hh"
#include "ed_util.hh"

#include "win_api.hh"

static CLG_LogRef LOG = {"ed.img.undo"};

/* Thread Locking */
/* This is a non-global static resource,
 * Maybe it should be exposed as part of the
 * paint op, but for now just give a public interface */
static SpinLock paint_tiles_lock;

void ed_img_paint_tile_lock_init()
{
  lib_spin_init(&paint_tiles_lock);
}

void ed_img_paint_tile_lock_end()
{
  lib_spin_end(&paint_tiles_lock);
}

/* Paint Tiles
 * Created on demand while painting,
 * use to access the previous state for some paint operations.
 *
 * These bufs are also used for undo when available. */

static ImBuf *imbuf_alloc_tmp_tile()
{
  return imbuf_alloc(
      ED_IMG_UNDO_TILE_SIZE, ED_IMG_UNDO_TILE_SIZE, 32, IB_rectfloat | IB_rect);
}

struct PaintTileKey {
  int x_tile, y_tile;
  Img *img;
  ImBuf *ibuf;
  /* Copied from iuser.tile in PaintTile. */
  int iuser_tile;

  uint64_t hash() const
  {
    return dune::get_default_hash_4(x_tile, y_tile, img, ibuf);
  }
  bool operator==(const PaintTileKey &other) const
  {
    return x_tile == other.x_tile && y_tile == other.y_tile && img == other.img &&
           ibuf == other.ibuf && iuser_tile == other.iuser_tile;
  }
};

struct PaintTile {
  Img *img;
  ImBuf *ibuf;
  /* For 2D img painting the ImgUser uses most of the vals.
   * Even though views and passes are stored they are currently not supported for painting.
   * For 3D projection painting this only uses a tile & frame number.
   * The scene ptr must be cleared (or mp set it as needed, but leave cleared). */
  ImgUser iuser;
  union {
    float *fp;
    uint8_t *byte_ptr;
    void *pt;
  } rect;
  uint16_t *mask;
  bool valid;
  bool use_float;
  int x_tile, y_tile;
};

static void ptile_free(PaintTile *ptile)
{
  if (ptile->rect.pt) {
   mem_free(ptile->rect.pt);
  }
  if (ptile->mask) {
    mem_free(ptile->mask);
  }
  mem_free(ptile);
}

struct PaintTileMap {
  dune::Map<PaintTileKey, PaintTile *> map;

  ~PaintTileMap()
  {
    for (PaintTile *ptile : map.vals()) {
      ptile_free(ptile);
    }
  }
};

static void ptile_invalidate_map(PaintTileMap *paint_tile_map)
{
  for (PaintTile *ptile : paint_tile_map->map.vals()) {
    ptile->valid = false;
  }
}

void *ed_img_paint_tile_find(PaintTileMap *paint_tile_map,
                             Img *img,
                             ImBuf *ibuf,
                             ImgUser *iuser,
                             int x_tile,
                             int y_tile,
                             ushort **r_mask,
                             bool validate)
{
  PaintTileKey key;
  key.ibuf = ibuf;
  key.img = img;
  key.iuser_tile = iuser->tile;
  key.x_tile = x_tile;
  key.y_tile = y_tile;
  PaintTile **pptile = paint_tile_map->map.lookup_ptr(key);
  if (pptile == nullptr) {
    return nullptr;
  }
  PaintTile *ptile = *pptile;
  if (r_mask) {
    /* allocate mask if requested. */
    if (!ptile->mask) {
      ptile->mask = static_cast<uint16_t *>(
          mem_calloc(sizeof(uint16_t) * square_i(ED_IMG_UNDO_TILE_SIZE), "UndoImgTile.mask"));
    }
    *r_mask = ptile->mask;
  }
  if (validate) {
    ptile->valid = true;
  }
  return ptile->rect.pt;
}

/* Set the given buf data as an owning data of the imbuf's buf.
 * Returns the data ptr which was stolen from the imbuf before assignment. */
static uint8_t *img_undo_steal_and_assign_byte_buffer(ImBuf *ibuf, uint8_t *new_buf_data)
{
  uint8_t *old_buf_data = imbuf_steal_byte_buf(ibuf);
  imbuf_assign_byte_buf(ibuf, new_buf_data, IB_TAKE_OWNERSHIP);
  return old_buffer_data;
}
static float *img_undo_steal_and_assign_float_buf(ImBuf *ibuf, float *new_buf_data)
{
  float *old_buf_data = imbuf_steal_float_buf(ibuf);
  imbuf_assign_float_buf(ibuf, new_buf_data, IB_TAKE_OWNERSHIP);
  return old_buf_data;
}

void *ed_img_paint_tile_push(PaintTileMap *paint_tile_map,
                             Img *img,
                             ImBuf *ibuf,
                             ImBuf **tmpibuf,
                             ImgUser *iuser,
                             int x_tile,
                             int y_tile,
                             ushort **r_mask,
                             bool **r_valid,
                             bool use_thread_lock,
                             bool find_prev)
{
  if (use_thread_lock) {
    lib_spin_lock(&paint_tiles_lock);
  }
  const bool has_float = (ibuf->float_buf.data != nullptr);

  /* check if tile is alrdy pushed */
  /* in projective painting we keep accounting of tiles, so if we need one pushed, just push! */
  if (find_prev) {
    void *data = ed_img_paint_tile_find(
        paint_tile_map, img, ibuf, iuser, x_tile, y_tile, r_mask, true);
    if (data) {
      if (use_thread_lock) {
        lib_spin_unlock(&paint_tiles_lock);
      }
      return data;
    }
  }

  if (*tmpibuf == nullptr) {
    *tmpibuf = imbuf_alloc_tmp_tile();
  }

  PaintTile *ptile = static_cast<PaintTile *>(mem_calloc(sizeof(PaintTile), "PaintTile"));

  ptile->img = img;
  ptile->ibuf = ibuf;
  ptile->iuser = *iuser;
  ptile->iuser.scene = nullptr;

  ptile->x_tile = x_tile;
  ptile->y_tile = y_tile;

  /* add mask explicitly here */
  if (r_mask) {
    *r_mask = ptile->mask = static_cast<uint16_t *>(
        mem_calloc(sizeof(uint16_t) * square_i(ED_IMG_UNDO_TILE_SIZE), "PaintTile.mask"));
  }

  ptile->rect.pt = mem_calloc((ibuf->float_buf.data ? sizeof(float[4]) : sizeof(char[4])) *
                               square_i(ED_IMG_UNDO_TILE_SIZE),
                               "PaintTile.rect");

  ptile->use_float = has_float;
  ptile->valid = true;

  if (r_valid) {
    *r_valid = &ptile->valid;
  }

  imbuf_rectcpy(*tmpibuf,
              ibuf,
              0,
              0,
              x_tile * ED_IMG_UNDO_TILE_SIZE,
              y_tile * ED_IMG_UNDO_TILE_SIZE,
              ED_IMG_UNDO_TILE_SIZE,
              ED_IMG_UNDO_TILE_SIZE);

  if (has_float) {
    ptile->rect.fp = img_undo_steal_and_assign_float_buf(*tmpibuf, ptile->rect.fp);
  }
  else {
    ptile->rect.byte_ptr = img_undo_steal_and_assign_byte_buf(*tmpibuf, ptile->rect.byte_ptr);
  }

  PaintTileKey key = {};
  key.ibuf = ibuf;
  key.img = img;
  key.iuser_tile = iuser->tile;
  key.x_tile = x_tile;
  key.y_tile = y_tile;
  PaintTile *existing_tile = nullptr;
  paint_tile_map->map.add_or_modify(
      key,
      [&](PaintTile **pptile) { *pptile = ptile; },
      [&](PaintTile **pptile) { existing_tile = *pptile; });
  if (existing_tile) {
    ptile_free(ptile);
    ptile = existing_tile;
  }

  if (use_thread_lock) {
    lib_spin_unlock(&paint_tiles_lock);
  }
  return ptile->rect.pt;
}

static void ptile_restore_runtime_map(PaintTileMap *paint_tile_map)
{
  ImBuf *tmpibuf = imbuf_alloc_tmp_tile();

  for (PaintTile *ptile : paint_tile_map->map.vals()) {
    Img *img = ptile->img;
    ImBuf *ibuf = dune_img_acquire_ibuf(img, &ptile->iuser, nullptr);
    const bool has_float = (ibuf->float_buf.data != nullptr);

    if (has_float) {
      ptile->rect.fp = img_undo_steal_and_assign_float_buffer(tmpibuf, ptile->rect.fp);
    }
    else {
      ptile->rect.byte_ptr = img_undo_steal_and_assign_byte_buf(tmpibuf,
                                                                ptile->rect.byte_ptr);
    }

    /* TODO: Look into implementing API which does not require such temp buf
     * assignment. */
    imbuf_rectcpy(ibuf,
                tmpibuf,
                ptile->x_tile * ED_IMG_UNDO_TILE_SIZE,
                ptile->y_tile * ED_IMG_UNDO_TILE_SIZE,
                0,
                0,
                ED_IMG_UNDO_TILE_SIZE,
                ED_IMG_UNDO_TILE_SIZE);

    if (has_float) {
      ptile->rect.fp = img_undo_steal_and_assign_float_buf(tmpibuf, ptile->rect.fp);
    }
    else {
      ptile->rect.byte_ptr = img_undo_steal_and_assign_byte_buf(tmpibuf,
                                                                     ptile->rect.byte_ptr);
    }

    /* Force OpenGL reload (maybe partial update will operate better?) */
    dune_img_free_gputextures(img);

    if (ibuf->float_buf.data) {
      ibuf->userflags |= IB_RECT_INVALID; /* force recreate of char rect */
    }
    if (ibuf->mipmap[0]) {
      ibuf->userflags |= IB_MIPMAP_INVALID; /* Force MIP-MAP recreation. */
    }
    ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;

    dune_img_release_ibuf(img, ibuf, nullptr);
  }

  imbuf_free(tmpibuf);
}

/* Img Undo Tile */
static uint32_t index_from_xy(uint32_t tile_x, uint32_t tile_y, const uint32_t tiles_dims[2])
{
  lib_assert(tile_x < tiles_dims[0] && tile_y < tiles_dims[1]);
  return (tile_y * tiles_dims[0]) + tile_x;
}

struct UndoImgTile {
  union {
    float *fp;
    uint8_t *byte_ptr;
    void *pt;
  } rect;
  int users;
};

static UndoImgTile *utile_alloc(bool has_float)
{
  UndoImgTile *utile = static_cast<UndoImgTile *>(
      mem_calloc(sizeof(*utile), "ImgUndoTile"));
  if (has_float) {
    utile->rect.fp = static_cast<float *>(
        mem_malloc(sizeof(float[4]) * square_i(ED_IMG_UNDO_TILE_SIZE), __func__));
  }
  else {
    utile->rect.byte_ptr = static_cast<uint8_t *>(
        mem_malloc(sizeof(uint32_t) * square_i(ED_IMG_UNDO_TILE_SIZE), __func__));
  }
  return utile;
}

static void utile_init_from_imbuf(
    UndoImgTile *utile, const uint32_t x, const uint32_t y, const ImBuf *ibuf, ImBuf *tmpibuf)
{
  const bool has_float = ibuf->float_buf.data;

  if (has_float) {
    utile->rect.fp = img_undo_steal_and_assign_float_buf(tmpibuf, utile->rect.fp);
  }
  else {
    utile->rect.byte_ptr = image_undo_steal_and_assign_byte_buf(tmpibuf, utile->rect.byte_ptr);
  }

  /* TODO: Look into implementing API which does not require such tmp buf
   * assignment. */
  imbuf_rectcpy(tmpibuf, ibuf, 0, 0, x, y, ED_IMG_UNDO_TILE_SIZE, ED_IMG_UNDO_TILE_SIZE);

  if (has_float) {
    utile->rect.fp = img_undo_steal_and_assign_float_buf(tmpibuf, utile->rect.fp);
  }
  else {
    utile->rect.byte_ptr = img_undo_steal_and_assign_byte_buf(tmpibuf, utile->rect.byte_ptr);
  }
}

static void utile_restore(
    const UndoImgTile *utile, const uint x, const uint y, ImBuf *ibuf, ImBuf *tmpibuf)
{
  const bool has_float = ibuf->float_buf.data;
  float *prev_rect_float = tmpibuf->float_buf.data;
  uint8_t *prev_rect = tmpibuf->byte_buf.data;

  if (has_float) {
    tmpibuf->float_buf.data = utile->rect.fp;
  }
  else {
    tmpibuf->byte_buf.data = utile->rect.byte_ptr;
  }
  
  /* TODO: Look into implementing API which does not require such tmp buf
   * assignment. */
  imbuf_rectcpy(ibuf, tmpibuf, x, y, 0, 0, ED_IMG_UNDO_TILE_SIZE, ED_IMG_UNDO_TILE_SIZE);

  tmpibuf->float_buf.data = prev_rect_float;
  tmpibuf->byte_buf.data = prev_rect;
}

static void utile_decref(UndoImgTile *utile)
{
  utile->users -= 1;
  lib_assert(utile->users >= 0);
  if (utile->users == 0) {
    mem_free(utile->rect.pt);
    mem_delete(utile);
  }
}

/* Img Undo Buf */
struct UndoImgBuf {
  UndoImgBuf *next, *prev;

  /* The buf after the undo step has ex */
  UndoImgBuf *post;

  char ibuf_filepath[IMB_FILEPATH_SIZE];

  UndoImgTile **tiles;

  /* Can calc these from dims, just for convenience. */
  uint32_t tiles_len;
  uint32_t tiles_dims[2];

  uint32_t image_dims[2];

  /* Store vars from the img. */
  struct {
    short source;
    bool use_float;
  } img_state;
};

static UndoImgBuf *ubuf_from_img_no_tiles(Img *img, const ImBuf *ibuf)
{
  UndoImageBuf *ubuf = static_cast<UndoImgBuf *>(mem_calloc(sizeof(*ubuf), __func__));

  ubuf->image_dims[0] = ibuf->x;
  ubuf->image_dims[1] = ibuf->y;

  ubuf->tiles_dims[0] = ED_IMG_UNDO_TILE_NUMBER(ubuf->img_dims[0]);
  ubuf->tiles_dims[1] = ED_IMG_UNDO_TILE_NUMBER(ubuf->img_dims[1]);

  ubuf->tiles_len = ubuf->tiles_dims[0] * ubuf->tiles_dims[1];
  ubuf->tiles = static_cast<UndoImgTile **>(
      mem_calloc(sizeof(*ubuf->tiles) * ubuf->tiles_len, __func__));

  STRNCPY(ubuf->ibuf_filepath, ibuf->filepath);
  ubuf->img_state.source = img->source;
  ubuf->img_state.use_float = ibuf->float_buffer.data != nullptr;

  return ubuf;
}

static void ubuf_from_img_all_tiles(UndoImgBuf *ubuf, const ImBuf *ibuf)
{
  ImBuf *tmpibuf = imbuf_alloc_temp_tile();

  const bool has_float = ibuf->float_buffer.data;
  int i = 0;
  for (uint y_tile = 0; y_tile < ubuf->tiles_dims[1]; y_tile += 1) {
    uint y = y_tile << ED_IMG_UNDO_TILE_BITS;
    for (uint x_tile = 0; x_tile < ubuf->tiles_dims[0]; x_tile += 1) {
      uint x = x_tile << ED_IMG_UNDO_TILE_BITS;

      lib_assert(ubuf->tiles[i] == nullptr);
      UndoImgTile *utile = utile_alloc(has_float);
      utile->users = 1;
      utile_init_from_imbuf(utile, x, y, ibuf, tmpibuf);
      ubuf->tiles[i] = utile;

      i += 1;
    }
  }

  lib_assert(i == ubuf->tiles_len);

  imbuf_free(tmpibuf);
}

/* Ensure we can copy the ubuf into the ibuf. */
static void ubuf_ensure_compat_ibuf(const UndoImgBuf *ubuf, ImBuf *ibuf)
{
  /* We could have both float and rect buffers,
   * in this case free the float buff if it's unused. */
  if ((ibuf->float_buf.data != nullptr) && (ubuf->img_state.use_float == false)) {
    imb_freerectfloatImBuf(ibuf);
  }

  if (ibuf->x == ubuf->img_dims[0] && ibuf->y == ubuf->img_dims[1] &&
      (ubuf->img_state.use_float ? (void *)ibuf->float_buf.data :
                                   (void *)ibuf->byte_buf.data))
  {
    return;
  }

  imbuf_freerect_all(ibuf);
  imbuf_rect_size_set(ibuf, ubuf->img_dims);

  if (ubuf->img_state.use_float) {
    imb_addrectfloatImBuf(ibuf, 4);
  }
  else {
    imb_addrectImBuf(ibuf);
  }
}

static void ubuf_free(UndoImgBuf *ubuf)
{
  UndoImgBuf *ubuf_post = ubuf->post;
  for (uint i = 0; i < ubuf->tiles_len; i++) {
    UndoImgTile *utile = ubuf->tiles[i];
    utile_decref(utile);
  }
  mem_free(ubuf->tiles);
  MEM_freeN(ubuf);
  if (ubuf_post) {
    ubuf_free(ubuf_post);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Image Undo Handle
 * \{ */

struct UndoImageHandle {
  UndoImageHandle *next, *prev;

  /** Each undo handle refers to a single image which may have multiple buffers. */
  UndoRefID_Image image_ref;

  /**
   * Each tile of a tiled image has its own UndoImageHandle.
   * The tile number of this IUser is used to distinguish them.
   */
  ImageUser iuser;

  /**
   * List of #UndoImageBuf's to support multiple buffers per image.
   */
  ListBase buffers;
};

static void uhandle_restore_list(ListBase *undo_handles, bool use_init)
{
  ImBuf *tmpibuf = imbuf_alloc_temp_tile();

  LISTBASE_FOREACH (UndoImageHandle *, uh, undo_handles) {
    /* Tiles only added to second set of tiles. */
    Image *image = uh->image_ref.ptr;

    ImBuf *ibuf = BKE_image_acquire_ibuf(image, &uh->iuser, nullptr);
    if (UNLIKELY(ibuf == nullptr)) {
      CLOG_ERROR(&LOG, "Unable to get buffer for image '%s'", image->id.name + 2);
      continue;
    }
    bool changed = false;
    LISTBASE_FOREACH (UndoImageBuf *, ubuf_iter, &uh->buffers) {
      UndoImageBuf *ubuf = use_init ? ubuf_iter : ubuf_iter->post;
      ubuf_ensure_compat_ibuf(ubuf, ibuf);

      int i = 0;
      for (uint y_tile = 0; y_tile < ubuf->tiles_dims[1]; y_tile += 1) {
        uint y = y_tile << ED_IMAGE_UNDO_TILE_BITS;
        for (uint x_tile = 0; x_tile < ubuf->tiles_dims[0]; x_tile += 1) {
          uint x = x_tile << ED_IMAGE_UNDO_TILE_BITS;
          utile_restore(ubuf->tiles[i], x, y, ibuf, tmpibuf);
          changed = true;
          i += 1;
        }
      }
    }

    if (changed) {
      BKE_image_mark_dirty(image, ibuf);
      /* TODO(@jbakker): only mark areas that are actually updated to improve performance. */
      BKE_image_partial_update_mark_full_update(image);

      if (ibuf->float_buffer.data) {
        ibuf->userflags |= IB_RECT_INVALID; /* Force recreate of char `rect` */
      }
      if (ibuf->mipmap[0]) {
        ibuf->userflags |= IB_MIPMAP_INVALID; /* Force MIP-MAP recreation. */
      }
      ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;

      DEG_id_tag_update(&image->id, 0);
    }
    BKE_image_release_ibuf(image, ibuf, nullptr);
  }

  IMB_freeImBuf(tmpibuf);
}

static void uhandle_free_list(ListBase *undo_handles)
{
  LISTBASE_FOREACH_MUTABLE (UndoImageHandle *, uh, undo_handles) {
    LISTBASE_FOREACH_MUTABLE (UndoImageBuf *, ubuf, &uh->buffers) {
      ubuf_free(ubuf);
    }
    MEM_freeN(uh);
  }
  BLI_listbase_clear(undo_handles);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Image Undo Internal Utilities
 * \{ */

/** #UndoImageHandle utilities */

static UndoImageBuf *uhandle_lookup_ubuf(UndoImageHandle *uh,
                                         const Image * /*image*/,
                                         const char *ibuf_filepath)
{
  LISTBASE_FOREACH (UndoImageBuf *, ubuf, &uh->buffers) {
    if (STREQ(ubuf->ibuf_filepath, ibuf_filepath)) {
      return ubuf;
    }
  }
  return nullptr;
}

static UndoImageBuf *uhandle_add_ubuf(UndoImageHandle *uh, Image *image, ImBuf *ibuf)
{
  BLI_assert(uhandle_lookup_ubuf(uh, image, ibuf->filepath) == nullptr);
  UndoImageBuf *ubuf = ubuf_from_image_no_tiles(image, ibuf);
  BLI_addtail(&uh->buffers, ubuf);

  ubuf->post = nullptr;

  return ubuf;
}

static UndoImageBuf *uhandle_ensure_ubuf(UndoImageHandle *uh, Image *image, ImBuf *ibuf)
{
  UndoImageBuf *ubuf = uhandle_lookup_ubuf(uh, image, ibuf->filepath);
  if (ubuf == nullptr) {
    ubuf = uhandle_add_ubuf(uh, image, ibuf);
  }
  return ubuf;
}

static UndoImageHandle *uhandle_lookup_by_name(ListBase *undo_handles,
                                               const Image *image,
                                               int tile_number)
{
  LISTBASE_FOREACH (UndoImageHandle *, uh, undo_handles) {
    if (STREQ(image->id.name + 2, uh->image_ref.name + 2) && uh->iuser.tile == tile_number) {
      return uh;
    }
  }
  return nullptr;
}

static UndoImageHandle *uhandle_lookup(ListBase *undo_handles, const Image *image, int tile_number)
{
  LISTBASE_FOREACH (UndoImageHandle *, uh, undo_handles) {
    if (image == uh->image_ref.ptr && uh->iuser.tile == tile_number) {
      return uh;
    }
  }
  return nullptr;
}

static UndoImageHandle *uhandle_add(ListBase *undo_handles, Image *image, ImageUser *iuser)
{
  BLI_assert(uhandle_lookup(undo_handles, image, iuser->tile) == nullptr);
  UndoImageHandle *uh = static_cast<UndoImageHandle *>(MEM_callocN(sizeof(*uh), __func__));
  uh->image_ref.ptr = image;
  uh->iuser = *iuser;
  uh->iuser.scene = nullptr;
  BLI_addtail(undo_handles, uh);
  return uh;
}

static UndoImageHandle *uhandle_ensure(ListBase *undo_handles, Image *image, ImageUser *iuser)
{
  UndoImageHandle *uh = uhandle_lookup(undo_handles, image, iuser->tile);
  if (uh == nullptr) {
    uh = uhandle_add(undo_handles, image, iuser);
  }
  return uh;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Implements ED Undo System
 * \{ */

struct ImageUndoStep {
  UndoStep step;

  /** #UndoImageHandle */
  ListBase handles;

  /**
   * #PaintTile
   * Run-time only data (active during a paint stroke).
   */
  PaintTileMap *paint_tile_map;

  bool is_encode_init;
  ePaintMode paint_mode;
};

/**
 * Find the previous undo buffer from this one.
 * \note We could look into undo steps even further back.
 */
static UndoImageBuf *ubuf_lookup_from_reference(ImageUndoStep *us_prev,
                                                const Image *image,
                                                int tile_number,
                                                const UndoImageBuf *ubuf)
{
  /* Use name lookup because the pointer is cleared for previous steps. */
  UndoImageHandle *uh_prev = uhandle_lookup_by_name(&us_prev->handles, image, tile_number);
  if (uh_prev != nullptr) {
    UndoImageBuf *ubuf_reference = uhandle_lookup_ubuf(uh_prev, image, ubuf->ibuf_filepath);
    if (ubuf_reference) {
      ubuf_reference = ubuf_reference->post;
      if ((ubuf_reference->image_dims[0] == ubuf->image_dims[0]) &&
          (ubuf_reference->image_dims[1] == ubuf->image_dims[1]))
      {
        return ubuf_reference;
      }
    }
  }
  return nullptr;
}

static bool image_undosys_poll(bContext *C)
{
  Object *obact = CTX_data_active_object(C);

  ScrArea *area = CTX_wm_area(C);
  if (area && (area->spacetype == SPACE_IMAGE)) {
    SpaceImage *sima = (SpaceImage *)area->spacedata.first;
    if ((obact && (obact->mode & OB_MODE_TEXTURE_PAINT)) || (sima->mode == SI_MODE_PAINT)) {
      return true;
    }
  }
  else {
    if (obact && (obact->mode & OB_MODE_TEXTURE_PAINT)) {
      return true;
    }
  }
  return false;
}

static void image_undosys_step_encode_init(bContext * /*C*/, UndoStep *us_p)
{
  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);
  /* dummy, memory is cleared anyway. */
  us->is_encode_init = true;
  BLI_listbase_clear(&us->handles);
  us->paint_tile_map = MEM_new<PaintTileMap>(__func__);
}

static bool image_undosys_step_encode(bContext *C, Main * /*bmain*/, UndoStep *us_p)
{
  /* Encoding is done along the way by adding tiles
   * to the current 'ImageUndoStep' added by encode_init.
   *
   * This function ensures there are previous and current states of the image in the undo buffer.
   */
  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);

  BLI_assert(us->step.data_size == 0);

  if (us->is_encode_init) {

    ImBuf *tmpibuf = imbuf_alloc_temp_tile();

    ImageUndoStep *us_reference = reinterpret_cast<ImageUndoStep *>(
        ED_undo_stack_get()->step_active);
    while (us_reference && us_reference->step.type != BKE_UNDOSYS_TYPE_IMAGE) {
      us_reference = reinterpret_cast<ImageUndoStep *>(us_reference->step.prev);
    }

    /* Initialize undo tiles from paint-tiles (if they exist). */
    for (PaintTile *ptile : us->paint_tile_map->map.values()) {
      if (ptile->valid) {
        UndoImageHandle *uh = uhandle_ensure(&us->handles, ptile->image, &ptile->iuser);
        UndoImageBuf *ubuf_pre = uhandle_ensure_ubuf(uh, ptile->image, ptile->ibuf);

        UndoImageTile *utile = static_cast<UndoImageTile *>(
            MEM_callocN(sizeof(*utile), "UndoImageTile"));
        utile->users = 1;
        utile->rect.pt = ptile->rect.pt;
        ptile->rect.pt = nullptr;
        const uint tile_index = index_from_xy(ptile->x_tile, ptile->y_tile, ubuf_pre->tiles_dims);

        BLI_assert(ubuf_pre->tiles[tile_index] == nullptr);
        ubuf_pre->tiles[tile_index] = utile;
      }
      ptile_free(ptile);
    }
    us->paint_tile_map->map.clear();

    LISTBASE_FOREACH (UndoImageHandle *, uh, &us->handles) {
      LISTBASE_FOREACH (UndoImageBuf *, ubuf_pre, &uh->buffers) {

        ImBuf *ibuf = BKE_image_acquire_ibuf(uh->image_ref.ptr, &uh->iuser, nullptr);

        const bool has_float = ibuf->float_buffer.data;

        BLI_assert(ubuf_pre->post == nullptr);
        ubuf_pre->post = ubuf_from_image_no_tiles(uh->image_ref.ptr, ibuf);
        UndoImageBuf *ubuf_post = ubuf_pre->post;

        if (ubuf_pre->image_dims[0] != ubuf_post->image_dims[0] ||
            ubuf_pre->image_dims[1] != ubuf_post->image_dims[1])
        {
          ubuf_from_image_all_tiles(ubuf_post, ibuf);
        }
        else {
          /* Search for the previous buffer. */
          UndoImageBuf *ubuf_reference =
              (us_reference ? ubuf_lookup_from_reference(
                                  us_reference, uh->image_ref.ptr, uh->iuser.tile, ubuf_post) :
                              nullptr);

          int i = 0;
          for (uint y_tile = 0; y_tile < ubuf_pre->tiles_dims[1]; y_tile += 1) {
            uint y = y_tile << ED_IMAGE_UNDO_TILE_BITS;
            for (uint x_tile = 0; x_tile < ubuf_pre->tiles_dims[0]; x_tile += 1) {
              uint x = x_tile << ED_IMAGE_UNDO_TILE_BITS;

              if ((ubuf_reference != nullptr) &&
                  ((ubuf_pre->tiles[i] == nullptr) ||
                   /* In this case the paint stroke as has added a tile
                    * which we have a duplicate reference available. */
                   (ubuf_pre->tiles[i]->users == 1)))
              {
                if (ubuf_pre->tiles[i] != nullptr) {
                  /* If we have a reference, re-use this single use tile for the post state. */
                  BLI_assert(ubuf_pre->tiles[i]->users == 1);
                  ubuf_post->tiles[i] = ubuf_pre->tiles[i];
                  ubuf_pre->tiles[i] = nullptr;
                  utile_init_from_imbuf(ubuf_post->tiles[i], x, y, ibuf, tmpibuf);
                }
                else {
                  BLI_assert(ubuf_post->tiles[i] == nullptr);
                  ubuf_post->tiles[i] = ubuf_reference->tiles[i];
                  ubuf_post->tiles[i]->users += 1;
                }
                BLI_assert(ubuf_pre->tiles[i] == nullptr);
                ubuf_pre->tiles[i] = ubuf_reference->tiles[i];
                ubuf_pre->tiles[i]->users += 1;

                BLI_assert(ubuf_pre->tiles[i] != nullptr);
                BLI_assert(ubuf_post->tiles[i] != nullptr);
              }
              else {
                UndoImageTile *utile = utile_alloc(has_float);
                utile_init_from_imbuf(utile, x, y, ibuf, tmpibuf);

                if (ubuf_pre->tiles[i] != nullptr) {
                  ubuf_post->tiles[i] = utile;
                  utile->users = 1;
                }
                else {
                  ubuf_pre->tiles[i] = utile;
                  ubuf_post->tiles[i] = utile;
                  utile->users = 2;
                }
              }
              BLI_assert(ubuf_pre->tiles[i] != nullptr);
              BLI_assert(ubuf_post->tiles[i] != nullptr);
              i += 1;
            }
          }
          BLI_assert(i == ubuf_pre->tiles_len);
          BLI_assert(i == ubuf_post->tiles_len);
        }
        BKE_image_release_ibuf(uh->image_ref.ptr, ibuf, nullptr);
      }
    }

    IMB_freeImBuf(tmpibuf);

    /* Useful to debug tiles are stored correctly. */
    if (false) {
      uhandle_restore_list(&us->handles, false);
    }
  }
  else {
    BLI_assert(C != nullptr);
    /* Happens when switching modes. */
    ePaintMode paint_mode = BKE_paintmode_get_active_from_context(C);
    BLI_assert(ELEM(paint_mode, PAINT_MODE_TEXTURE_2D, PAINT_MODE_TEXTURE_3D));
    us->paint_mode = paint_mode;
  }

  us_p->is_applied = true;

  return true;
}

static void image_undosys_step_decode_undo_impl(ImageUndoStep *us, bool is_final)
{
  BLI_assert(us->step.is_applied == true);
  uhandle_restore_list(&us->handles, !is_final);
  us->step.is_applied = false;
}

static void image_undosys_step_decode_redo_impl(ImageUndoStep *us)
{
  BLI_assert(us->step.is_applied == false);
  uhandle_restore_list(&us->handles, false);
  us->step.is_applied = true;
}

static void image_undosys_step_decode_undo(ImageUndoStep *us, bool is_final)
{
  /* Walk forward over any applied steps of same type,
   * then walk back in the next loop, un-applying them. */
  ImageUndoStep *us_iter = us;
  while (us_iter->step.next && (us_iter->step.next->type == us_iter->step.type)) {
    if (us_iter->step.next->is_applied == false) {
      break;
    }
    us_iter = (ImageUndoStep *)us_iter->step.next;
  }
  while (us_iter != us || (!is_final && us_iter == us)) {
    BLI_assert(us_iter->step.type == us->step.type); /* Previous loop ensures this. */
    image_undosys_step_decode_undo_impl(us_iter, is_final);
    if (us_iter == us) {
      break;
    }
    us_iter = (ImageUndoStep *)us_iter->step.prev;
  }
}

static void image_undosys_step_decode_redo(ImageUndoStep *us)
{
  ImageUndoStep *us_iter = us;
  while (us_iter->step.prev && (us_iter->step.prev->type == us_iter->step.type)) {
    if (us_iter->step.prev->is_applied == true) {
      break;
    }
    us_iter = (ImageUndoStep *)us_iter->step.prev;
  }
  while (us_iter && (us_iter->step.is_applied == false)) {
    image_undosys_step_decode_redo_impl(us_iter);
    if (us_iter == us) {
      break;
    }
    us_iter = (ImageUndoStep *)us_iter->step.next;
  }
}

static void image_undosys_step_decode(
    bContext *C, Main *bmain, UndoStep *us_p, const eUndoStepDir dir, bool is_final)
{
  /* NOTE: behavior for undo/redo closely matches sculpt undo. */
  BLI_assert(dir != STEP_INVALID);

  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);
  if (dir == STEP_UNDO) {
    image_undosys_step_decode_undo(us, is_final);
  }
  else if (dir == STEP_REDO) {
    image_undosys_step_decode_redo(us);
  }

  if (us->paint_mode == PAINT_MODE_TEXTURE_3D) {
    ED_object_mode_set_ex(C, OB_MODE_TEXTURE_PAINT, false, nullptr);
  }

  /* Refresh texture slots. */
  ED_editors_init_for_undo(bmain);
}

static void image_undosys_step_free(UndoStep *us_p)
{
  ImageUndoStep *us = (ImageUndoStep *)us_p;
  uhandle_free_list(&us->handles);

  /* Typically this map will have been cleared. */
  MEM_delete(us->paint_tile_map);
  us->paint_tile_map = nullptr;
}

static void image_undosys_foreach_ID_ref(UndoStep *us_p,
                                         UndoTypeForEachIDRefFn foreach_ID_ref_fn,
                                         void *user_data)
{
  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);
  LISTBASE_FOREACH (UndoImageHandle *, uh, &us->handles) {
    foreach_ID_ref_fn(user_data, ((UndoRefID *)&uh->image_ref));
  }
}

void ED_image_undosys_type(UndoType *ut)
{
  ut->name = "Image";
  ut->poll = image_undosys_poll;
  ut->step_encode_init = image_undosys_step_encode_init;
  ut->step_encode = image_undosys_step_encode;
  ut->step_decode = image_undosys_step_decode;
  ut->step_free = image_undosys_step_free;

  ut->step_foreach_ID_ref = image_undosys_foreach_ID_ref;

  /* NOTE: this is actually a confusing case, since it expects a valid context, but only in a
   * specific case, see `image_undosys_step_encode` code. We cannot specify
   * `UNDOTYPE_FLAG_NEED_CONTEXT_FOR_ENCODE` though, as it can be called with a null context by
   * current code. */
  ut->flags = UNDOTYPE_FLAG_DECODE_ACTIVE_STEP;

  ut->step_size = sizeof(ImageUndoStep);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Utilities
 *
 * \note image undo exposes #ED_image_undo_push_begin, #ED_image_undo_push_end
 * which must be called by the operator directly.
 *
 * Unlike most other undo stacks this is needed:
 * - So we can always access the state before the image was painted onto,
 *   which is needed if previous undo states aren't image-type.
 * - So operators can access the pixel-data before the stroke was applied, at run-time.
 * \{ */

PaintTileMap *ED_image_paint_tile_map_get()
{
  UndoStack *ustack = ED_undo_stack_get();
  UndoStep *us_prev = ustack->step_init;
  UndoStep *us_p = BKE_undosys_stack_init_or_active_with_type(ustack, BKE_UNDOSYS_TYPE_IMAGE);
  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);
  /* We should always have an undo push started when accessing tiles,
   * not doing this means we won't have paint_mode correctly set. */
  BLI_assert(us_p == us_prev);
  if (us_p != us_prev) {
    /* Fallback value until we can be sure this never happens. */
    us->paint_mode = PAINT_MODE_TEXTURE_2D;
  }
  return us->paint_tile_map;
}

void ED_image_undo_restore(UndoStep *us)
{
  PaintTileMap *paint_tile_map = reinterpret_cast<ImageUndoStep *>(us)->paint_tile_map;
  ptile_restore_runtime_map(paint_tile_map);
  ptile_invalidate_map(paint_tile_map);
}

static ImageUndoStep *image_undo_push_begin(const char *name, int paint_mode)
{
  UndoStack *ustack = ED_undo_stack_get();
  bContext *C = nullptr; /* special case, we never read from this. */
  UndoStep *us_p = BKE_undosys_step_push_init_with_type(ustack, C, name, BKE_UNDOSYS_TYPE_IMAGE);
  ImageUndoStep *us = reinterpret_cast<ImageUndoStep *>(us_p);
  BLI_assert(ELEM(paint_mode, PAINT_MODE_TEXTURE_2D, PAINT_MODE_TEXTURE_3D, PAINT_MODE_SCULPT));
  us->paint_mode = (ePaintMode)paint_mode;
  return us;
}

void ED_image_undo_push_begin(const char *name, int paint_mode)
{
  image_undo_push_begin(name, paint_mode);
}

void ED_image_undo_push_begin_with_image(const char *name,
                                         Image *image,
                                         ImBuf *ibuf,
                                         ImageUser *iuser)
{
  ImageUndoStep *us = image_undo_push_begin(name, PAINT_MODE_TEXTURE_2D);

  BLI_assert(BKE_image_get_tile(image, iuser->tile));
  UndoImageHandle *uh = uhandle_ensure(&us->handles, image, iuser);
  UndoImageBuf *ubuf_pre = uhandle_ensure_ubuf(uh, image, ibuf);
  BLI_assert(ubuf_pre->post == nullptr);

  ImageUndoStep *us_reference = reinterpret_cast<ImageUndoStep *>(
      ED_undo_stack_get()->step_active);
  while (us_reference && us_reference->step.type != BKE_UNDOSYS_TYPE_IMAGE) {
    us_reference = reinterpret_cast<ImageUndoStep *>(us_reference->step.prev);
  }
  UndoImageBuf *ubuf_reference = (us_reference ? ubuf_lookup_from_reference(
                                                     us_reference, image, iuser->tile, ubuf_pre) :
                                                 nullptr);

  if (ubuf_reference) {
    memcpy(ubuf_pre->tiles, ubuf_reference->tiles, sizeof(*ubuf_pre->tiles) * ubuf_pre->tiles_len);
    for (uint32_t i = 0; i < ubuf_pre->tiles_len; i++) {
      UndoImageTile *utile = ubuf_pre->tiles[i];
      utile->users += 1;
    }
  }
  else {
    ubuf_from_image_all_tiles(ubuf_pre, ibuf);
  }
}

void ED_image_undo_push_end()
{
  UndoStack *ustack = ED_undo_stack_get();
  BKE_undosys_step_push(ustack, nullptr, nullptr);
  BKE_undosys_stack_limit_steps_and_memory_defaults(ustack);
  WM_file_tag_modified();
}
