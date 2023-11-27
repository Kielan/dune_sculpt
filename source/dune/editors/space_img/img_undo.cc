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
    utile->rect.byte_ptr = img_undo_steal_and_assign_byte_buf(tmpibuf, utile->rect.byte_ptr);
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

  uint32_t img_dims[2];

  /* Store vars from the img. */
  struct {
    short source;
    bool use_float;
  } img_state;
};

static UndoImgBuf *ubuf_from_img_no_tiles(Img *img, const ImBuf *ibuf)
{
  UndoImgBuf *ubuf = static_cast<UndoImgBuf *>(mem_calloc(sizeof(*ubuf), __func__));

  ubuf->img_dims[0] = ibuf->x;
  ubuf->img_dims[1] = ibuf->y;

  ubuf->tiles_dims[0] = ED_IMG_UNDO_TILE_NUMBER(ubuf->img_dims[0]);
  ubuf->tiles_dims[1] = ED_IMG_UNDO_TILE_NUMBER(ubuf->img_dims[1]);

  ubuf->tiles_len = ubuf->tiles_dims[0] * ubuf->tiles_dims[1];
  ubuf->tiles = static_cast<UndoImgTile **>(
      mem_calloc(sizeof(*ubuf->tiles) * ubuf->tiles_len, __func__));

  STRNCPY(ubuf->ibuf_filepath, ibuf->filepath);
  ubuf->img_state.src = img->sr ;
  ubuf->img_state.use_float = ibuf->float_buf.data != nullptr;

  return ubuf;
}

static void ubuf_from_img_all_tiles(UndoImgBuf *ubuf, const ImBuf *ibuf)
{
  ImBuf *tmpibuf = imbuf_alloc_tmp_tile();

  const bool has_float = ibuf->float_buf.data;
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
  mem_free(ubuf);
  if (ubuf_post) {
    ubuf_free(ubuf_post);
  }
}

/* Img Undo Handle */
struct UndoImgHandle {
  UndoImgHandle *next, *prev;

  /* Each undo handle refers to a single image which may have multiple buffers. */
  UndoRefIdImg img_ref;

  /* Each tile of a tiled img has its own UndoImgHandle.
   * The tile number of this IUser is used to distinguish them. */
  ImageUser iuser;

  /* List of UndoImgBuf's to support multiple buffers per image.  */
  List buffers;
};

static void uhandle_restore_list(List *undo_handles, bool use_init)
{
  ImBuf *tmpibuf = imbuf_alloc_tmp_tile();

  LIST_FOREACH (UndoImgHandle *, uh, undo_handles) {
    /* Tiles only added to second set of tiles. */
    Img *img = uh->img_ref.ptr;

    ImBuf *ibuf = dune_img_acquire_ibuf(img, &uh->iuser, nullptr);
    if (UNLIKELY(ibuf == nullptr)) {
      CLOG_ERROR(&LOG, "Unable to get buf for img '%s'", img->id.name + 2);
      continue;
    }
    bool changed = false;
    LIST_FOREACH (UndoImgBuf *, ubuf_iter, &uh->buffers) {
      UndoImgBuf *ubuf = use_init ? ubuf_iter : ubuf_iter->post;
      ubuf_ensure_compat_ibuf(ubuf, ibuf);

      int i = 0;
      for (uint y_tile = 0; y_tile < ubuf->tiles_dims[1]; y_tile += 1) {
        uint y = y_tile << ED_IMG_UNDO_TILE_BITS;
        for (uint x_tile = 0; x_tile < ubuf->tiles_dims[0]; x_tile += 1) {
          uint x = x_tile << ED_IMG_UNDO_TILE_BITS;
          utile_restore(ubuf->tiles[i], x, y, ibuf, tmpibuf);
          changed = true;
          i += 1;
        }
      }
    }

    if (changed) {
      dune_img_mark_dirty(img, ibuf);
      /* TODO: only mark areas that are actually updated to improve performance. */
      dune_img_partial_update_mark_full_update(img);

      if (ibuf->float_buf.data) {
        ibuf->userflags |= IB_RECT_INVALID; /* Force recreate of char `rect` */
      }
      if (ibuf->mipmap[0]) {
        ibuf->userflags |= IB_MIPMAP_INVALID; /* Force MIP-MAP recreation. */
      }
      ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;

      graph_id_tag_update(&img->id, 0);
    }
    dune_img_release_ibuf(img, ibuf, nullptr);
  }

  imbuf_free(tmpibuf);
}

static void uhandle_free_list(List *undo_handles)
{
  LIST_FOREACH_MUTABLE (UndoImgHandle *, uh, undo_handles) {
    LIST_FOREACH_MUTABLE (UndoImgBuf *, ubuf, &uh->buffers) {
      ubuf_free(ubuf);
    }
    mem_free(uh);
  }
  lib_list_clear(undo_handles);
}

/* Img Undo Internal Utils */
/* UndoImgHandle utils */
static UndoImgBuf *uhandle_lookup_ubuf(UndoImgHandle *uh,
                                       const Img * /*img*/,
                                       

const char *ibuf_filepath)
{
  LIST_FOREACH (UndoImgBuf *, ubuf, &uh->bufs) {
    if (STREQ(ubuf->ibuf_filepath, ibuf_filepath)) {
      return ubuf;
    }
  }
  return nullptr;
}

static UndoImgBuf *uhandle_add_ubuf(UndoImgHandle *uh, Img *img, ImBuf *ibuf)
{
  lib_assert(uhandle_lookup_ubuf(uh, img, ibuf->filepath) == nullptr);
  UndoImgBuf *ubuf = ubuf_from_img_no_tiles(img, ibuf);
  lib_addtail(&uh->buffers, ubuf);

  ubuf->post = nullptr;

  return ubuf;
}

static UndoImgBuf *uhandle_ensure_ubuf(UndoImgHandle *uh, Img *img, ImBuf *ibuf)
{
  UndoImgBuf *ubuf = uhandle_lookup_ubuf(uh, img, ibuf->filepath);
  if (ubuf == nullptr) {
    ubuf = uhandle_add_ubuf(uh, img, ibuf);
  }
  return ubuf;
}

static UndoImgHandle *uhandle_lookup_by_name(List *undo_handles,
                                             const Img *img,
                                             int tile_number)
{
  LIST_FOREACH (UndoImgHandle *, uh, undo_handles) {
    if (STREQ(img->id.name + 2, uh->img_ref.name + 2) && uh->iuser.tile == tile_number) {
      return uh;
    }
  }
  return nullptr;
}

static UndoImgHandle *uhandle_lookup(List *undo_handles, const Img *img, int tile_number)
{
  LIST_FOREACH (UndoImgHandle *, uh, undo_handles) {
    if (img == uh->img_ref.ptr && uh->iuser.tile == tile_number) {
      return uh;
    }
  }
  return nullptr;
}

static UndoImgHandle *uhandle_add(List *undo_handles, Img *img, ImgUser *iuser)
{
  lib_assert(uhandle_lookup(undo_handles, image, iuser->tile) == nullptr);
  UndoImgHandle *uh = static_cast<UndoImgHandle *>(mem_calloc(sizeof(*uh), __func__));
  uh->img_ref.ptr = img;
  uh->iuser = *iuser;
  uh->iuser.scene = nullptr;
  lib_addtail(undo_handles, uh);
  return uh;
}

static UndoImgHandle *uhandle_ensure(List *undo_handles, Imb *img, ImgUser *iuser)
{
  UndoImgHandle *uh = uhandle_lookup(undo_handles, img, iuser->tile);
  if (uh == nullptr) {
    uh = uhandle_add(undo_handles, img, iuser);
  }
  return uh;
}

/* Implements ed Undo System */
struct ImgUndoStep {
  UndoStep step;

  /* UndoImgHandle */
  List handles;

  /* PaintTile
   * Run-time only data (active during a paint stroke).   */
  PaintTileMap *paint_tile_map;

  bool is_encode_init;
  ePaintMode paint_mode;
};

/* Find the previous undo buffer from this one.
 * We could look into undo steps even further back. */
static UndoImgBuf *ubuf_lookup_from_ref(ImgUndoStep *us_prev,
                                        const Img *img,
                                        int tile_number,
                                        const UndoImgBuf *ubuf)
{
  /* Use name lookup because the pointer is cleared for previous steps. */
  UndoImgHandle *uh_prev = uhandle_lookup_by_name(&us_prev->handles, img, tile_number);
  if (uh_prev != nullptr) {
    UndoImgBuf *ubuf_ref = uhandle_lookup_ubuf(uh_prev, img, ubuf->ibuf_filepath);
    if (ubuf_ref) {
      ubuf_ref = ubuf_ref->post;
      if ((ubuf_ref->img_dims[0] == ubuf->img_dims[0]) &&
          (ubuf_ref->img_dims[1] == ubuf->img_dims[1]))
      {
        return ubuf_ref;
      }
    }
  }
  return nullptr;
}

static bool img_undosys_poll(Cxt *C)
{
  Ob *obact = cxt_data_active_ob(C);

  ScrArea *area = cxt_win_area(C);
  if (area && (area->spacetype == SPACE_IMG)) {
    SpaceImg *simg = (SpaceImg *)area->spacedata.first;
    if ((obact && (obact->mode & OB_MODE_TEXTURE_PAINT)) || (simg->mode == SI_MODE_PAINT)) {
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

static void img_undosys_step_encode_init(Cxt * /*C*/, UndoStep *us_p)
{
  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);
  /* dummy, memory is cleared anyway. */
  us->is_encode_init = true;
  lib_list_clear(&us->handles);
  us->paint_tile_map = mem_new<PaintTileMap>(__func__);
}

static bool img_undosys_step_encode(Cxt *C, Main * /*main*/, UndoStep *us_p)
{
  /* Encoding is done along the way by adding tiles
   * to the current 'ImgUndoStep' added by encode_init.
   * This fn ensures there are previous and current states of the image in the undo buffer  */
  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);

  lib_assert(us->step.data_size == 0);

  if (us->is_encode_init) {

    ImBuf *tmpibuf = imbuf_alloc_tmp_tile();

    ImgUndoStep *us_ref = reinterpret_cast<ImgUndoStep *>(
        ed_undo_stack_get()->step_active);
    while (us_ref && us_ref->step.type != DUNE_UNDOSYS_TYPE_IMG) {
      us_ref = reinterpret_cast<ImgUndoStep *>(us_ref->step.prev);
    }

    /* Init undo tiles from paint-tiles (if they exist). */
    for (PaintTile *ptile : us->paint_tile_map->map.vals()) {
      if (ptile->valid) {
        UndoImgHandle *uh = uhandle_ensure(&us->handles, ptile->img, &ptile->iuser);
        UndoImgBuf *ubuf_pre = uhandle_ensure_ubuf(uh, ptile->img, ptile->ibuf);

        UndoImgTile *utile = static_cast<UndoImageTile *>(
            mem_calloc(sizeof(*utile), "UndoImgTile"));
        utile->users = 1;
        utile->rect.pt = ptile->rect.pt;
        ptile->rect.pt = nullptr;
        const uint tile_index = index_from_xy(ptile->x_tile, ptile->y_tile, ubuf_pre->tiles_dims);

        lib_assert(ubuf_pre->tiles[tile_index] == nullptr);
        ubuf_pre->tiles[tile_index] = utile;
      }
      ptile_free(ptile);
    }
    us->paint_tile_map->map.clear();

    LIST_FOREACH (UndoImgHandle *, uh, &us->handles) {
      LIST_FOREACH (UndoImgBuf *, ubuf_pre, &uh->buffers) {

        ImBuf *ibuf = dune_img_acquire_ibuf(uh->img_ref.ptr, &uh->iuser, nullptr);

        const bool has_float = ibuf->float_buf.data;

        lib_assert(ubuf_pre->post == nullptr);
        ubuf_pre->post = ubuf_from_img_no_tiles(uh->img_ref.ptr, ibuf);
        UndoImgBuf *ubuf_post = ubuf_pre->post;

        if (ubuf_pre->img_dims[0] != ubuf_post->img_dims[0] ||
            ubuf_pre->img_dims[1] != ubuf_post->img_dims[1])
        {
          ubuf_from_img_all_tiles(ubuf_post, ibuf);
        }
        else {
          /* Search for the previous buf. */
          UndoImgBuf *ubuf_ref =
              (us_ref ? ubuf_lookup_from_ref(
                                  us_ref, uh->img_ref.ptr, uh->iuser.tile, ubuf_post) :
                              nullptr);

          int i = 0;
          for (uint y_tile = 0; y_tile < ubuf_pre->tiles_dims[1]; y_tile += 1) {
            uint y = y_tile << ED_IMG_UNDO_TILE_BITS;
            for (uint x_tile = 0; x_tile < ubuf_pre->tiles_dims[0]; x_tile += 1) {
              uint x = x_tile << ED_IMG_UNDO_TILE_BITS;

              if ((ubuf_ref != nullptr) &&
                  ((ubuf_pre->tiles[i] == nullptr) ||
                   /* In this case the paint stroke as has added a tile
                    * which we have a dup ref available. */
                   (ubuf_pre->tiles[i]->users == 1)))
              {
                if (ubuf_pre->tiles[i] != nullptr) {
                  /* If we have a reference, re-use this single use tile for the post state. */
                  lib_assert(ubuf_pre->tiles[i]->users == 1);
                  ubuf_post->tiles[i] = ubuf_pre->tiles[i];
                  ubuf_pre->tiles[i] = nullptr;
                  utile_init_from_imbuf(ubuf_post->tiles[i], x, y, ibuf, tmpibuf);
                }
                else {
                  lib_assert(ubuf_post->tiles[i] == nullptr);
                  ubuf_post->tiles[i] = ubuf_ref->tiles[i];
                  ubuf_post->tiles[i]->users += 1;
                }
                lib_assert(ubuf_pre->tiles[i] == nullptr);
                ubuf_pre->tiles[i] = ubuf_ref->tiles[i];
                ubuf_pre->tiles[i]->users += 1;

                lib_assert(ubuf_pre->tiles[i] != nullptr);
                lib_assert(ubuf_post->tiles[i] != nullptr);
              }
              else {
                UndoImgTile *utile = utile_alloc(has_float);
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
              lib_assert(ubuf_pre->tiles[i] != nullptr);
              lib_assert(ubuf_post->tiles[i] != nullptr);
              i += 1;
            }
          }
          lib_assert(i == ubuf_pre->tiles_len);
         lib_assert(i == ubuf_post->tiles_len);
        }
        dune_img_release_ibuf(uh->img_ref.ptr, ibuf, nullptr);
      }
    }

    imbuf_free(tmpibuf);

    /* Useful to debug tiles are stored correctly. */
    if (false) {
      uhandle_restore_list(&us->handles, false);
    }
  }
  else {
    lib_assert(C != nullptr);
    /* Happens when switching modes. */
    ePaintMode paint_mode = dune_paintmode_get_active_from_cxt(C);
    lib_assert(ELEM(paint_mode, PAINT_MODE_TEXTURE_2D, PAINT_MODE_TEXTURE_3D));
    us->paint_mode = paint_mode;
  }

  us_p->is_applied = true;

  return true;
}

static void img_undosys_step_decode_undo_impl(ImgUndoStep *us, bool is_final)
{
  lib_assert(us->step.is_applied == true);
  uhandle_restore_list(&us->handles, !is_final);
  us->step.is_applied = false;
}

static void img_undosys_step_decode_redo_impl(ImgUndoStep *us)
{
  lib_assert(us->step.is_applied == false);
  uhandle_restore_list(&us->handles, false);
  us->step.is_applied = true;
}

static void img_undosys_step_decode_undo(ImgUndoStep *us, bool is_final)
{
  /* Walk forward over any applied steps of same type,
   * then walk back in the next loop, un-applying them. */
  ImgUndoStep *us_iter = us;
  while (us_iter->step.next && (us_iter->step.next->type == us_iter->step.type)) {
    if (us_iter->step.next->is_applied == false) {
      break;
    }
    us_iter = (ImgUndoStep *)us_iter->step.next;
  }
  while (us_iter != us || (!is_final && us_iter == us)) {
    lib_assert(us_iter->step.type == us->step.type); /* Previous loop ensures this. */
    img_undosys_step_decode_undo_impl(us_iter, is_final);
    if (us_iter == us) {
      break;
    }
    us_iter = (ImgUndoStep *)us_iter->step.prev;
  }
}

static void img_undosys_step_decode_redo(ImgUndoStep *us)
{
  ImgUndoStep *us_iter = us;
  while (us_iter->step.prev && (us_iter->step.prev->type == us_iter->step.type)) {
    if (us_iter->step.prev->is_applied == true) {
      break;
    }
    us_iter = (ImgUndoStep *)us_iter->step.prev;
  }
  while (us_iter && (us_iter->step.is_applied == false)) {
    img_undosys_step_decode_redo_impl(us_iter);
    if (us_iter == us) {
      break;
    }
    us_iter = (ImgUndoStep *)us_iter->step.next;
  }
}

static void img_undosys_step_decode(
    Cxt *C, Main *main, UndoStep *us_p, const eUndoStepDir dir, bool is_final)
{
  /* NOTE: behavior for undo/redo closely matches sculpt undo. */
  lib_assert(dir != STEP_INVALID);

  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);
  if (dir == STEP_UNDO) {
    img_undosys_step_decode_undo(us, is_final);
  }
  else if (dir == STEP_REDO) {
    img_undosys_step_decode_redo(us);
  }

  if (us->paint_mode == PAINT_MODE_TEXTURE_3D) {
    ed_ob_mode_set_ex(C, OB_MODE_TEXTURE_PAINT, false, nullptr);
  }

  /* Refresh texture slots. */
  ed_editors_init_for_undo(main);
}

static void img_undosys_step_free(UndoStep *us_p)
{
  ImgUndoStep *us = (ImgUndoStep *)us_p;
  uhandle_free_list(&us->handles);

  /* Typically this map will have been cleared. */
  mem_delete(us->paint_tile_map);
  us->paint_tile_map = nullptr;
}

static void img_undosys_foreach_id_ref(UndoStep *us_p,
                                         UndoTypeForEachIdRefFn foreach_id_ref_fn,
                                         void *user_data)
{
  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);
  LIST_FOREACH (UndoImgHandle *, uh, &us->handles) {
    foreach_id_ref_fn(user_data, ((UndoRefId *)&uh->img_ref));
  }
}

void ed_img_undosys_type(UndoType *ut)
{
  ut->name = "Image";
  ut->poll = img_undosys_poll;
  ut->step_encode_init = img_undosys_step_encode_init;
  ut->step_encode = img_undosys_step_encode;
  ut->step_decode = img_undosys_step_decode;
  ut->step_free = img_undosys_step_free;

  ut->step_foreach_id_ref = img_undosys_foreach_id_ref;

  /* This is a confusing case, expects a valid cxt, but only in a
   * specific case, see `img_undosys_step_encode` code. We cannot specify
   * `UNDOTYPE_FLAG_NEED_CXT_FOR_ENCODE` though, as it can be called with a null cxt by
   * current code. */
  ut->flags = UNDOTYPE_FLAG_DECODE_ACTIVE_STEP;

  ut->step_size = sizeof(ImgUndoStep);
}

/* Utils
 *
 * img undo exposes ed_img_undo_push_begin, ed_img_undo_push_end
 * which must be called by the op directly.
 *
 * Unlike most other undo stacks this is needed:
 * - Can always access the state before the img was painted onto,
 *   which is needed if previous undo states aren't img-type.
 * - Ops can access the pixel-data before the stroke was applied, at run-time. */

PaintTileMap *ed_img_paint_tile_map_get()
{
  UndoStack *ustack = ed_undo_stack_get();
  UndoStep *us_prev = ustack->step_init;
  UndoStep *us_p = dune_undosys_stack_init_or_active_with_type(ustack, DUNE_UNDOSYS_TYPE_IMG);
  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);
  /* We should always have an undo push started when accessing tiles,
   * not doing this means we won't have paint_mode correctly set. */
  lib_assert(us_p == us_prev);
  if (us_p != us_prev) {
    /* Fallback val until we can be sure this never happens. */
    us->paint_mode = PAINT_MODE_TEXTURE_2D;
  }
  return us->paint_tile_map;
}

void ed_img_undo_restore(UndoStep *us)
{
  PaintTileMap *paint_tile_map = reinterpret_cast<ImageUndoStep *>(us)->paint_tile_map;
  ptile_restore_runtime_map(paint_tile_map);
  ptile_invalidate_map(paint_tile_map);
}

static ImgUndoStep *img_undo_push_begin(const char *name, int paint_mode)
{
  UndoStack *ustack = ed_undo_stack_get();
  Cxt *C = nullptr; /* special case, we never read from this. */
  UndoStep *us_p = dune_undosys_step_push_init_with_type(ustack, C, name, DUNE_UNDOSYS_TYPE_IMAGE);
  ImgUndoStep *us = reinterpret_cast<ImgUndoStep *>(us_p);
  lib_assert(ELEM(paint_mode, PAINT_MODE_TEXTURE_2D, PAINT_MODE_TEXTURE_3D, PAINT_MODE_SCULPT));
  us->paint_mode = (ePaintMode)paint_mode;
  return us;
}

void ed_img_undo_push_begin(const char *name, int paint_mode)
{
  img_undo_push_begin(name, paint_mode);
}

void ed_img_undo_push_begin_with_img(const char *name,
                                     Img *img,
                                     ImBuf *ibuf,
                                     ImgUser *iuser)
{
  ImgUndoStep *us = img_undo_push_begin(name, PAINT_MODE_TEXTURE_2D);

  lib_assert(dune_img_get_tile(img, iuser->tile));
  UndoImgHandle *uh = uhandle_ensure(&us->handles, img, iuser);
  UndoImgBuf *ubuf_pre = uhandle_ensure_ubuf(uh, img, ibuf);
  lib_assert(ubuf_pre->post == nullptr);

  ImgUndoStep *us_ref = reinterpret_cast<ImgUndoStep *>(
      ed_undo_stack_get()->step_active);
  while (us_ref && us_ref->step.type != DUNE_UNDOSYS_TYPE_IMG) {
    us_ref = reinterpret_cast<ImgUndoStep *>(us_ref->step.prev);
  }
  UndoImgBuf *ubuf_ref = (us_ref ? ubuf_lookup_from_ref(
                                                     us_ref, img, iuser->tile, ubuf_pre) :
                                                 nullptr);

  if (ubuf_ref) {
    memcpy(ubuf_pre->tiles, ubuf_ref->tiles, sizeof(*ubuf_pre->tiles) * ubuf_pre->tiles_len);
    for (uint32_t i = 0; i < ubuf_pre->tiles_len; i++) {
      UndoImgTile *utile = ubuf_pre->tiles[i];
      utile->users += 1;
    }
  }
  else {
    ubuf_from_img_all_tiles(ubuf_pre, ibuf);
  }
}

void ed_img_undo_push_end()
{
  UndoStack *ustack = ed_undo_stack_get();
  dune_undosys_step_push(ustack, nullptr, nullptr);
  dune_undosys_stack_limit_steps_and_memory_defaults(ustack);
  win_file_tag_modified();
}
