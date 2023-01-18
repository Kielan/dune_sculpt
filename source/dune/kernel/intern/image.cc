#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#ifndef WIN32
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include <ctime>

#include "BLI_array.hh"

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"
#include "IMB_moviecache.h"
#include "IMB_openexr.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_brush_types.h"
#include "DNA_camera_types.h"
#include "DNA_defaults.h"
#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_simulation_types.h"
#include "DNA_world_types.h"

#include "BLI_blenlib.h"
#include "BLI_math_vector.h"
#include "BLI_mempool.h"
#include "BLI_system.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_timecode.h" /* For stamp time-code format. */
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_bpath.h"
#include "BKE_colortools.h"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_idtype.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_node_tree_update.h"
#include "BKE_packedFile.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_workspace.h"

#include "BLF_api.h"

#include "PIL_time.h"

#include "RE_pipeline.h"

#include "SEQ_utils.h" /* SEQ_get_topmost_sequence() */

#include "GPU_material.h"
#include "GPU_texture.h"

#include "BLI_sys_types.h" /* for intptr_t support */

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "BLO_read_write.h"

/* for image user iteration */
#include "DNA_node_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

using blender::Array;

static CLG_LogRef LOG = {"bke.image"};

static void image_init(Image *ima, short source, short type);
static void image_free_packedfiles(Image *ima);
static void copy_image_packedfiles(ListBase *lb_dst, const ListBase *lb_src);

/* -------------------------------------------------------------------- */
/** \name Image #IDTypeInfo API
 * \{ */

/** Reset runtime image fields when data-block is being initialized. */
static void image_runtime_reset(struct Image *image)
{
  memset(&image->runtime, 0, sizeof(image->runtime));
  image->runtime.cache_mutex = MEM_mallocN(sizeof(ThreadMutex), "image runtime cache_mutex");
  BLI_mutex_init(static_cast<ThreadMutex *>(image->runtime.cache_mutex));
}

/** Reset runtime image fields when data-block is being copied. */
static void image_runtime_reset_on_copy(struct Image *image)
{
  image->runtime.cache_mutex = MEM_mallocN(sizeof(ThreadMutex), "image runtime cache_mutex");
  BLI_mutex_init(static_cast<ThreadMutex *>(image->runtime.cache_mutex));

  image->runtime.partial_update_register = nullptr;
  image->runtime.partial_update_user = nullptr;
}

static void image_runtime_free_data(struct Image *image)
{
  BLI_mutex_end(static_cast<ThreadMutex *>(image->runtime.cache_mutex));
  MEM_freeN(image->runtime.cache_mutex);
  image->runtime.cache_mutex = nullptr;

  if (image->runtime.partial_update_user != nullptr) {
    BKE_image_partial_update_free(image->runtime.partial_update_user);
    image->runtime.partial_update_user = nullptr;
  }
  BKE_image_partial_update_register_free(image);
}

static void image_init_data(ID *id)
{
  Image *image = (Image *)id;

  if (image != nullptr) {
    image_init(image, IMA_SRC_GENERATED, IMA_TYPE_UV_TEST);
  }
}

static void image_copy_data(Main *UNUSED(bmain), ID *id_dst, const ID *id_src, const int flag)
{
  Image *image_dst = (Image *)id_dst;
  const Image *image_src = (const Image *)id_src;

  BKE_color_managed_colorspace_settings_copy(&image_dst->colorspace_settings,
                                             &image_src->colorspace_settings);

  copy_image_packedfiles(&image_dst->packedfiles, &image_src->packedfiles);

  image_dst->stereo3d_format = static_cast<Stereo3dFormat *>(
      MEM_dupallocN(image_src->stereo3d_format));
  BLI_duplicatelist(&image_dst->views, &image_src->views);

  /* Cleanup stuff that cannot be copied. */
  image_dst->cache = nullptr;
  image_dst->rr = nullptr;

  BLI_duplicatelist(&image_dst->renderslots, &image_src->renderslots);
  LISTBASE_FOREACH (RenderSlot *, slot, &image_dst->renderslots) {
    slot->render = nullptr;
  }

  BLI_listbase_clear(&image_dst->anims);

  BLI_duplicatelist(&image_dst->tiles, &image_src->tiles);

  for (int eye = 0; eye < 2; eye++) {
    for (int i = 0; i < TEXTARGET_COUNT; i++) {
      for (int resolution = 0; resolution < IMA_TEXTURE_RESOLUTION_LEN; resolution++) {
        image_dst->gputexture[i][eye][resolution] = nullptr;
      }
    }
  }

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    BKE_previewimg_id_copy(&image_dst->id, &image_src->id);
  }
  else {
    image_dst->preview = nullptr;
  }

  image_runtime_reset_on_copy(image_dst);
}

static void image_free_data(ID *id)
{
  Image *image = (Image *)id;

  /* Also frees animations (#Image.anims list). */
  BKE_image_free_buffers(image);

  image_free_packedfiles(image);

  LISTBASE_FOREACH (RenderSlot *, slot, &image->renderslots) {
    if (slot->render) {
      RE_FreeRenderResult(slot->render);
      slot->render = nullptr;
    }
  }
  BLI_freelistN(&image->renderslots);

  BKE_image_free_views(image);
  MEM_SAFE_FREE(image->stereo3d_format);

  BKE_icon_id_delete(&image->id);
  BKE_previewimg_free(&image->preview);

  BLI_freelistN(&image->tiles);

  image_runtime_free_data(image);
}

static void image_foreach_cache(ID *id,
                                IDTypeForeachCacheFunctionCallback function_callback,
                                void *user_data)
{
  Image *image = (Image *)id;
  IDCacheKey key;
  key.id_session_uuid = id->session_uuid;
  key.offset_in_ID = offsetof(Image, cache);
  key.cache_v = image->cache;
  function_callback(id, &key, (void **)&image->cache, 0, user_data);

  auto gputexture_offset = [image](int target, int eye, int resolution) {
    constexpr size_t base_offset = offsetof(Image, gputexture);
    const auto first = &image->gputexture[0][0][0];
    const size_t array_offset = sizeof(*first) *
                                (&image->gputexture[target][eye][resolution] - first);
    return base_offset + array_offset;
  };

  for (int eye = 0; eye < 2; eye++) {
    for (int a = 0; a < TEXTARGET_COUNT; a++) {
      for (int resolution = 0; resolution < IMA_TEXTURE_RESOLUTION_LEN; resolution++) {
        GPUTexture *texture = image->gputexture[a][eye][resolution];
        if (texture == nullptr) {
          continue;
        }
        key.offset_in_ID = gputexture_offset(a, eye, resolution);
        key.cache_v = texture;
        function_callback(id, &key, (void **)&image->gputexture[a][eye][resolution], 0, user_data);
      }
    }
  }

  key.offset_in_ID = offsetof(Image, rr);
  key.cache_v = image->rr;
  function_callback(id, &key, (void **)&image->rr, 0, user_data);

  LISTBASE_FOREACH (RenderSlot *, slot, &image->renderslots) {
    key.offset_in_ID = (size_t)BLI_ghashutil_strhash_p(slot->name);
    key.cache_v = slot->render;
    function_callback(id, &key, (void **)&slot->render, 0, user_data);
  }
}

static void image_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Image *ima = (Image *)id;
  const eBPathForeachFlag flag = bpath_data->flag;

  if (BKE_image_has_packedfile(ima) && (flag & BKE_BPATH_FOREACH_PATH_SKIP_PACKED) != 0) {
    return;
  }
  /* Skip empty file paths, these are typically from generated images and
   * don't make sense to add directories to until the image has been saved
   * once to give it a meaningful value. */
  /* TODO re-assess whether this behavior is desired in the new generic code context. */
  if (!ELEM(ima->source, IMA_SRC_FILE, IMA_SRC_MOVIE, IMA_SRC_SEQUENCE, IMA_SRC_TILED) ||
      ima->filepath[0] == '\0') {
    return;
  }

  /* If this is a tiled image, and we're asked to resolve the tokens in the virtual
   * filepath, use the first tile to generate a concrete path for use during processing. */
  bool result = false;
  if (ima->source == IMA_SRC_TILED && (flag & BKE_BPATH_FOREACH_PATH_RESOLVE_TOKEN) != 0) {
    char temp_path[FILE_MAX], orig_file[FILE_MAXFILE];
    BLI_strncpy(temp_path, ima->filepath, sizeof(temp_path));
    BLI_split_file_part(temp_path, orig_file, sizeof(orig_file));

    eUDIM_TILE_FORMAT tile_format;
    char *udim_pattern = BKE_image_get_tile_strformat(temp_path, &tile_format);
    BKE_image_set_filepath_from_tile_number(
        temp_path, udim_pattern, tile_format, ((ImageTile *)ima->tiles.first)->tile_number);
    MEM_SAFE_FREE(udim_pattern);

    result = BKE_bpath_foreach_path_fixed_process(bpath_data, temp_path);
    if (result) {
      /* Put the filepath back together using the new directory and the original file name. */
      char new_dir[FILE_MAXDIR];
      BLI_split_dir_part(temp_path, new_dir, sizeof(new_dir));
      BLI_join_dirfile(ima->filepath, sizeof(ima->filepath), new_dir, orig_file);
    }
  }
  else {
    result = BKE_bpath_foreach_path_fixed_process(bpath_data, ima->filepath);
  }

  if (result) {
    if (flag & BKE_BPATH_FOREACH_PATH_RELOAD_EDITED) {
      if (!BKE_image_has_packedfile(ima) &&
          /* Image may have been painted onto (and not saved, T44543). */
          !BKE_image_is_dirty(ima)) {
        BKE_image_signal(bpath_data->bmain, ima, nullptr, IMA_SIGNAL_RELOAD);
      }
    }
  }
}

static void image_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Image *ima = (Image *)id;
  const bool is_undo = BLO_write_is_undo(writer);

  /* Clear all data that isn't read to reduce false detection of changed image during memfile undo.
   */
  ima->lastused = 0;
  ima->cache = nullptr;
  ima->gpuflag = 0;
  BLI_listbase_clear(&ima->anims);
  ima->runtime.partial_update_register = nullptr;
  ima->runtime.partial_update_user = nullptr;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      for (int resolution = 0; resolution < IMA_TEXTURE_RESOLUTION_LEN; resolution++) {
        ima->gputexture[i][j][resolution] = nullptr;
      }
    }
  }

  ImagePackedFile *imapf;

  BLI_assert(ima->packedfile == nullptr);
  if (!is_undo) {
    /* Do not store packed files in case this is a library override ID. */
    if (ID_IS_OVERRIDE_LIBRARY(ima)) {
      BLI_listbase_clear(&ima->packedfiles);
    }
    else {
      /* Some trickery to keep forward compatibility of packed images. */
      if (ima->packedfiles.first != nullptr) {
        imapf = static_cast<ImagePackedFile *>(ima->packedfiles.first);
        ima->packedfile = imapf->packedfile;
      }
    }
  }

  /* write LibData */
  BLO_write_id_struct(writer, Image, id_address, &ima->id);
  BKE_id_blend_write(writer, &ima->id);

  for (imapf = static_cast<ImagePackedFile *>(ima->packedfiles.first); imapf;
       imapf = imapf->next) {
    BLO_write_struct(writer, ImagePackedFile, imapf);
    BKE_packedfile_blend_write(writer, imapf->packedfile);
  }

  BKE_previewimg_blend_write(writer, ima->preview);

  LISTBASE_FOREACH (ImageView *, iv, &ima->views) {
    BLO_write_struct(writer, ImageView, iv);
  }
  BLO_write_struct(writer, Stereo3dFormat, ima->stereo3d_format);

  BLO_write_struct_list(writer, ImageTile, &ima->tiles);

  ima->packedfile = nullptr;

  BLO_write_struct_list(writer, RenderSlot, &ima->renderslots);
}

static void image_blend_read_data(BlendDataReader *reader, ID *id)
{
  Image *ima = (Image *)id;
  BLO_read_list(reader, &ima->tiles);

  BLO_read_list(reader, &(ima->renderslots));
  if (!BLO_read_data_is_undo(reader)) {
    /* We reset this last render slot index only when actually reading a file, not for undo. */
    ima->last_render_slot = ima->render_slot;
  }

  BLO_read_list(reader, &(ima->views));
  BLO_read_list(reader, &(ima->packedfiles));

  if (ima->packedfiles.first) {
    LISTBASE_FOREACH (ImagePackedFile *, imapf, &ima->packedfiles) {
      BKE_packedfile_blend_read(reader, &imapf->packedfile);
    }
    ima->packedfile = nullptr;
  }
  else {
    BKE_packedfile_blend_read(reader, &ima->packedfile);
  }

  BLI_listbase_clear(&ima->anims);
  BLO_read_data_address(reader, &ima->preview);
  BKE_previewimg_blend_read(reader, ima->preview);
  BLO_read_data_address(reader, &ima->stereo3d_format);

  ima->lastused = 0;
  ima->gpuflag = 0;

  image_runtime_reset(ima);
}

static void image_blend_read_lib(BlendLibReader *UNUSED(reader), ID *id)
{
  Image *ima = (Image *)id;
  /* Images have some kind of 'main' cache, when null we should also clear all others. */
  /* Needs to be done *after* cache pointers are restored (call to
   * `foreach_cache`/`blo_cache_storage_entry_restore_in_new`), easier for now to do it in
   * lib_link... */
  if (ima->cache == nullptr) {
    BKE_image_free_buffers(ima);
  }
}

constexpr IDTypeInfo get_type_info()
{
  IDTypeInfo info{};
  info.id_code = ID_IM;
  info.id_filter = FILTER_ID_IM;
  info.main_listbase_index = INDEX_ID_IM;
  info.struct_size = sizeof(Image);
  info.name = "Image";
  info.name_plural = "images";
  info.translation_context = BLT_I18NCONTEXT_ID_IMAGE;
  info.flags = IDTYPE_FLAGS_NO_ANIMDATA | IDTYPE_FLAGS_APPEND_IS_REUSABLE;
  info.asset_type_info = nullptr;

  info.init_data = image_init_data;
  info.copy_data = image_copy_data;
  info.free_data = image_free_data;
  info.make_local = nullptr;
  info.foreach_id = nullptr;
  info.foreach_cache = image_foreach_cache;
  info.foreach_path = image_foreach_path;
  info.owner_get = nullptr;

  info.blend_write = image_blend_write;
  info.blend_read_data = image_blend_read_data;
  info.blend_read_lib = image_blend_read_lib;
  info.blend_read_expand = nullptr;

  info.blend_read_undo_preserve = nullptr;

  info.lib_override_apply_post = nullptr;
  return info;
}
IDTypeInfo IDType_ID_IM = get_type_info();

/* prototypes */
static int image_num_files(struct Image *ima);
static ImBuf *image_acquire_ibuf(Image *ima, ImageUser *iuser, void **r_lock);
static void image_update_views_format(Image *ima, ImageUser *iuser);
static void image_add_view(Image *ima, const char *viewname, const char *filepath);

/* max int, to indicate we don't store sequences in ibuf */
#define IMA_NO_INDEX 0x7FEFEFEF

/* quick lookup: supports 1 million entries, thousand passes */
#define IMA_MAKE_INDEX(entry, index) (((entry) << 10) + (index))
#define IMA_INDEX_ENTRY(index) ((index) >> 10)
#if 0
#  define IMA_INDEX_PASS(index) (index & ~1023)
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Image Cache
 * \{ */

typedef struct ImageCacheKey {
  int index;
} ImageCacheKey;

static unsigned int imagecache_hashhash(const void *key_v)
{
  const ImageCacheKey *key = static_cast<const ImageCacheKey *>(key_v);
  return key->index;
}

static bool imagecache_hashcmp(const void *a_v, const void *b_v)
{
  const ImageCacheKey *a = static_cast<const ImageCacheKey *>(a_v);
  const ImageCacheKey *b = static_cast<const ImageCacheKey *>(b_v);

  return (a->index != b->index);
}

static void imagecache_keydata(void *userkey, int *framenr, int *proxy, int *render_flags)
{
  ImageCacheKey *key = static_cast<ImageCacheKey *>(userkey);

  *framenr = IMA_INDEX_ENTRY(key->index);
  *proxy = IMB_PROXY_NONE;
  *render_flags = 0;
}

static void imagecache_put(Image *image, int index, ImBuf *ibuf)
{
  ImageCacheKey key;

  if (image->cache == nullptr) {
    // char cache_name[64];
    // SNPRINTF(cache_name, "Image Datablock %s", image->id.name);

    image->cache = IMB_moviecache_create(
        "Image Datablock Cache", sizeof(ImageCacheKey), imagecache_hashhash, imagecache_hashcmp);
    IMB_moviecache_set_getdata_callback(image->cache, imagecache_keydata);
  }

  key.index = index;

  IMB_moviecache_put(image->cache, &key, ibuf);
}

static void imagecache_remove(Image *image, int index)
{
  if (image->cache == nullptr) {
    return;
  }

  ImageCacheKey key;
  key.index = index;
  IMB_moviecache_remove(image->cache, &key);
}

static struct ImBuf *imagecache_get(Image *image, int index, bool *r_is_cached_empty)
{
  if (image->cache) {
    ImageCacheKey key;
    key.index = index;
    return IMB_moviecache_get(image->cache, &key, r_is_cached_empty);
  }

  return nullptr;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Allocate & Free, Data Managing
 * \{ */

static void image_free_cached_frames(Image *image)
{
  if (image->cache) {
    IMB_moviecache_free(image->cache);
    image->cache = nullptr;
  }
}

static void image_free_packedfiles(Image *ima)
{
  while (ima->packedfiles.last) {
    ImagePackedFile *imapf = static_cast<ImagePackedFile *>(ima->packedfiles.last);
    if (imapf->packedfile) {
      BKE_packedfile_free(imapf->packedfile);
    }
    BLI_remlink(&ima->packedfiles, imapf);
    MEM_freeN(imapf);
  }
}

void BKE_image_free_packedfiles(Image *ima)
{
  image_free_packedfiles(ima);
}

void BKE_image_free_views(Image *image)
{
  BLI_freelistN(&image->views);
}

static void image_free_anims(Image *ima)
{
  while (ima->anims.last) {
    ImageAnim *ia = static_cast<ImageAnim *>(ima->anims.last);
    if (ia->anim) {
      IMB_free_anim(ia->anim);
      ia->anim = nullptr;
    }
    BLI_remlink(&ima->anims, ia);
    MEM_freeN(ia);
  }
}

void BKE_image_free_buffers_ex(Image *ima, bool do_lock)
{
  if (do_lock) {
    BLI_mutex_lock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));
  }
  image_free_cached_frames(ima);

  image_free_anims(ima);

  if (ima->rr) {
    RE_FreeRenderResult(ima->rr);
    ima->rr = nullptr;
  }

  BKE_image_free_gputextures(ima);

  if (do_lock) {
    BLI_mutex_unlock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));
  }
}

void BKE_image_free_buffers(Image *ima)
{
  BKE_image_free_buffers_ex(ima, false);
}

void BKE_image_free_data(Image *ima)
{
  image_free_data(&ima->id);
}

/* only image block itself */
static void image_init(Image *ima, short source, short type)
{
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(ima, id));

  MEMCPY_STRUCT_AFTER(ima, DNA_struct_default_get(Image), id);

  ima->source = source;
  ima->type = type;

  if (source == IMA_SRC_VIEWER) {
    ima->flag |= IMA_VIEW_AS_RENDER;
  }

  ImageTile *tile = MEM_cnew<ImageTile>("Image Tiles");
  tile->tile_number = 1001;
  BLI_addtail(&ima->tiles, tile);

  if (type == IMA_TYPE_R_RESULT) {
    for (int i = 0; i < 8; i++) {
      BKE_image_add_renderslot(ima, nullptr);
    }
  }

  image_runtime_reset(ima);

  BKE_color_managed_colorspace_settings_init(&ima->colorspace_settings);
  ima->stereo3d_format = MEM_cnew<Stereo3dFormat>("Image Stereo Format");
}

static Image *image_alloc(Main *bmain, const char *name, short source, short type)
{
  Image *ima;

  ima = static_cast<Image *>(BKE_libblock_alloc(bmain, ID_IM, name, 0));
  if (ima) {
    image_init(ima, source, type);
  }

  return ima;
}

/**
 * Get the ibuf from an image cache by its index and entry.
 * Local use here only.
 *
 * \returns referenced image buffer if it exists, callee is to call #IMB_freeImBuf
 * to de-reference the image buffer after it's done handling it.
 */
static ImBuf *image_get_cached_ibuf_for_index_entry(Image *ima,
                                                    int index,
                                                    int entry,
                                                    bool *r_is_cached_empty)
{
  if (index != IMA_NO_INDEX) {
    index = IMA_MAKE_INDEX(entry, index);
  }

  return imagecache_get(ima, index, r_is_cached_empty);
}

static void image_assign_ibuf(Image *ima, ImBuf *ibuf, int index, int entry)
{
  if (index != IMA_NO_INDEX) {
    index = IMA_MAKE_INDEX(entry, index);
  }

  imagecache_put(ima, index, ibuf);
}

static void image_remove_ibuf(Image *ima, int index, int entry)
{
  if (index != IMA_NO_INDEX) {
    index = IMA_MAKE_INDEX(entry, index);
  }
  imagecache_remove(ima, index);
}

static void copy_image_packedfiles(ListBase *lb_dst, const ListBase *lb_src)
{
  const ImagePackedFile *imapf_src;

  BLI_listbase_clear(lb_dst);
  for (imapf_src = static_cast<const ImagePackedFile *>(lb_src->first); imapf_src;
       imapf_src = imapf_src->next) {
    ImagePackedFile *imapf_dst = static_cast<ImagePackedFile *>(
        MEM_mallocN(sizeof(ImagePackedFile), "Image Packed Files (copy)"));
    STRNCPY(imapf_dst->filepath, imapf_src->filepath);

    if (imapf_src->packedfile) {
      imapf_dst->packedfile = BKE_packedfile_duplicate(imapf_src->packedfile);
    }

    BLI_addtail(lb_dst, imapf_dst);
  }
}

void BKE_image_merge(Main *bmain, Image *dest, Image *source)
{
  /* sanity check */
  if (dest && source && dest != source) {
    BLI_mutex_lock(static_cast<ThreadMutex *>(source->runtime.cache_mutex));
    BLI_mutex_lock(static_cast<ThreadMutex *>(dest->runtime.cache_mutex));

    if (source->cache != nullptr) {
      struct MovieCacheIter *iter;
      iter = IMB_moviecacheIter_new(source->cache);
      while (!IMB_moviecacheIter_done(iter)) {
        ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
        ImageCacheKey *key = static_cast<ImageCacheKey *>(IMB_moviecacheIter_getUserKey(iter));
        imagecache_put(dest, key->index, ibuf);
        IMB_moviecacheIter_step(iter);
      }
      IMB_moviecacheIter_free(iter);
    }

    BLI_mutex_unlock(static_cast<ThreadMutex *>(dest->runtime.cache_mutex));
    BLI_mutex_unlock(static_cast<ThreadMutex *>(source->runtime.cache_mutex));

    BKE_id_free(bmain, source);
  }
}

bool BKE_image_scale(Image *image, int width, int height)
{
  /* NOTE: We could be clever and scale all imbuf's
   * but since some are mipmaps its not so simple. */

  ImBuf *ibuf;
  void *lock;

  ibuf = BKE_image_acquire_ibuf(image, nullptr, &lock);

  if (ibuf) {
    IMB_scaleImBuf(ibuf, width, height);
    BKE_image_mark_dirty(image, ibuf);
  }

  BKE_image_release_ibuf(image, ibuf, lock);

  return (ibuf != nullptr);
}

bool BKE_image_has_opengl_texture(Image *ima)
{
  for (int eye = 0; eye < 2; eye++) {
    for (int i = 0; i < TEXTARGET_COUNT; i++) {
      for (int resolution = 0; resolution < IMA_TEXTURE_RESOLUTION_LEN; resolution++) {
        if (ima->gputexture[i][eye][resolution] != nullptr) {
          return true;
        }
      }
    }
  }
  return false;
}

static int image_get_tile_number_from_iuser(Image *ima, const ImageUser *iuser)
{
  BLI_assert(ima != nullptr && ima->tiles.first);
  ImageTile *tile = static_cast<ImageTile *>(ima->tiles.first);
  return (iuser && iuser->tile) ? iuser->tile : tile->tile_number;
}

ImageTile *BKE_image_get_tile(Image *ima, int tile_number)
{
  if (ima == nullptr) {
    return nullptr;
  }

  /* Tiles 0 and 1001 are a special case and refer to the first tile, typically
   * coming from non-UDIM-aware code. */
  if (ELEM(tile_number, 0, 1001)) {
    return static_cast<ImageTile *>(ima->tiles.first);
  }

  /* Must have a tiled image and a valid tile number at this point. */
  if (ima->source != IMA_SRC_TILED || tile_number < 1001 || tile_number > IMA_UDIM_MAX) {
    return nullptr;
  }

  LISTBASE_FOREACH (ImageTile *, tile, &ima->tiles) {
    if (tile->tile_number == tile_number) {
      return tile;
    }
  }

  return nullptr;
}

ImageTile *BKE_image_get_tile_from_iuser(Image *ima, const ImageUser *iuser)
{
  return BKE_image_get_tile(ima, image_get_tile_number_from_iuser(ima, iuser));
}

int BKE_image_get_tile_from_pos(struct Image *ima,
                                const float uv[2],
                                float r_uv[2],
                                float r_ofs[2])
{
  float local_ofs[2];
  if (r_ofs == nullptr) {
    r_ofs = local_ofs;
  }

  copy_v2_v2(r_uv, uv);
  zero_v2(r_ofs);

  if ((ima->source != IMA_SRC_TILED) || uv[0] < 0.0f || uv[1] < 0.0f || uv[0] >= 10.0f) {
    return 0;
  }

  int ix = (int)uv[0];
  int iy = (int)uv[1];
  int tile_number = 1001 + 10 * iy + ix;

  if (BKE_image_get_tile(ima, tile_number) == nullptr) {
    return 0;
  }
  r_ofs[0] = ix;
  r_ofs[1] = iy;
  sub_v2_v2(r_uv, r_ofs);

  return tile_number;
}

int BKE_image_find_nearest_tile(const Image *image, const float co[2])
{
  const float co_floor[2] = {floorf(co[0]), floorf(co[1])};
  /* Distance to the closest UDIM tile. */
  float dist_best_sq = FLT_MAX;
  int tile_number_best = -1;

  LISTBASE_FOREACH (const ImageTile *, tile, &image->tiles) {
    const int tile_index = tile->tile_number - 1001;
    /* Coordinates of the current tile. */
    const float tile_index_co[2] = {static_cast<float>(tile_index % 10),
                                    static_cast<float>(tile_index / 10)};

    if (equals_v2v2(co_floor, tile_index_co)) {
      return tile->tile_number;
    }

    /* Distance between co[2] and UDIM tile. */
    const float dist_sq = len_squared_v2v2(tile_index_co, co);

    if (dist_sq < dist_best_sq) {
      dist_best_sq = dist_sq;
      tile_number_best = tile->tile_number;
    }
  }

  return tile_number_best;
}

static void image_init_color_management(Image *ima)
{
  ImBuf *ibuf;
  char name[FILE_MAX];

  BKE_image_user_file_path(nullptr, ima, name);

  /* will set input color space to image format default's */
  ibuf = IMB_loadiffname(name, IB_test | IB_alphamode_detect, ima->colorspace_settings.name);

  if (ibuf) {
    if (ibuf->flags & IB_alphamode_premul) {
      ima->alpha_mode = IMA_ALPHA_PREMUL;
    }
    else if (ibuf->flags & IB_alphamode_channel_packed) {
      ima->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    }
    else if (ibuf->flags & IB_alphamode_ignore) {
      ima->alpha_mode = IMA_ALPHA_IGNORE;
    }
    else {
      ima->alpha_mode = IMA_ALPHA_STRAIGHT;
    }

    IMB_freeImBuf(ibuf);
  }
}

char BKE_image_alpha_mode_from_extension_ex(const char *filepath)
{
  if (BLI_path_extension_check_n(filepath, ".exr", ".cin", ".dpx", ".hdr", nullptr)) {
    return IMA_ALPHA_PREMUL;
  }

  return IMA_ALPHA_STRAIGHT;
}

void BKE_image_alpha_mode_from_extension(Image *image)
{
  image->alpha_mode = BKE_image_alpha_mode_from_extension_ex(image->filepath);
}

Image *BKE_image_load(Main *bmain, const char *filepath)
{
  Image *ima;
  int file;
  char str[FILE_MAX];

  STRNCPY(str, filepath);
  BLI_path_abs(str, BKE_main_blendfile_path(bmain));

  /* exists? */
  file = BLI_open(str, O_BINARY | O_RDONLY, 0);
  if (file == -1) {
    if (!BKE_image_tile_filepath_exists(str)) {
      return nullptr;
    }
  }
  else {
    close(file);
  }

  ima = image_alloc(bmain, BLI_path_basename(filepath), IMA_SRC_FILE, IMA_TYPE_IMAGE);
  STRNCPY(ima->filepath, filepath);

  if (BLI_path_extension_check_array(filepath, imb_ext_movie)) {
    ima->source = IMA_SRC_MOVIE;
  }

  image_init_color_management(ima);

  return ima;
}

Image *BKE_image_load_exists_ex(Main *bmain, const char *filepath, bool *r_exists)
{
  Image *ima;
  char str[FILE_MAX], strtest[FILE_MAX];

  STRNCPY(str, filepath);
  BLI_path_abs(str, bmain->filepath);

  /* first search an identical filepath */
  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    if (!ELEM(ima->source, IMA_SRC_VIEWER, IMA_SRC_GENERATED)) {
      STRNCPY(strtest, ima->filepath);
      BLI_path_abs(strtest, ID_BLEND_PATH(bmain, &ima->id));

      if (BLI_path_cmp(strtest, str) == 0) {
        if ((BKE_image_has_anim(ima) == false) || (ima->id.us == 0)) {
          id_us_plus(&ima->id); /* officially should not, it doesn't link here! */
          if (r_exists) {
            *r_exists = true;
          }
          return ima;
        }
      }
    }
  }

  if (r_exists) {
    *r_exists = false;
  }
  return BKE_image_load(bmain, filepath);
}

Image *BKE_image_load_exists(Main *bmain, const char *filepath)
{
  return BKE_image_load_exists_ex(bmain, filepath, nullptr);
}

struct ImageFillData {
  short gen_type;
  uint width;
  uint height;
  unsigned char *rect;
  float *rect_float;
  float fill_color[4];
};

static void image_buf_fill_isolated(void *usersata_v)
{
  ImageFillData *usersata = static_cast<ImageFillData *>(usersata_v);

  const short gen_type = usersata->gen_type;
  const uint width = usersata->width;
  const uint height = usersata->height;

  unsigned char *rect = usersata->rect;
  float *rect_float = usersata->rect_float;

  switch (gen_type) {
    case IMA_GENTYPE_GRID:
      BKE_image_buf_fill_checker(rect, rect_float, width, height);
      break;
    case IMA_GENTYPE_GRID_COLOR:
      BKE_image_buf_fill_checker_color(rect, rect_float, width, height);
      break;
    default:
      BKE_image_buf_fill_color(rect, rect_float, width, height, usersata->fill_color);
      break;
  }
}

static ImBuf *add_ibuf_size(unsigned int width,
                            unsigned int height,
                            const char *name,
                            int depth,
                            int floatbuf,
                            short gen_type,
                            const float color[4],
                            ColorManagedColorspaceSettings *colorspace_settings)
{
  ImBuf *ibuf;
  unsigned char *rect = nullptr;
  float *rect_float = nullptr;
  float fill_color[4];

  if (floatbuf) {
    ibuf = IMB_allocImBuf(width, height, depth, IB_rectfloat);

    if (colorspace_settings->name[0] == '\0') {
      const char *colorspace = IMB_colormanagement_role_colorspace_name_get(
          COLOR_ROLE_DEFAULT_FLOAT);

      STRNCPY(colorspace_settings->name, colorspace);
    }

    if (ibuf != nullptr) {
      rect_float = ibuf->rect_float;
      IMB_colormanagement_check_is_data(ibuf, colorspace_settings->name);
    }

    if (IMB_colormanagement_space_name_is_data(colorspace_settings->name)) {
      copy_v4_v4(fill_color, color);
    }
    else {
      /* The input color here should ideally be linear already, but for now
       * we just convert and postpone breaking the API for later. */
      srgb_to_linearrgb_v4(fill_color, color);
    }
  }
  else {
    ibuf = IMB_allocImBuf(width, height, depth, IB_rect);

    if (colorspace_settings->name[0] == '\0') {
      const char *colorspace = IMB_colormanagement_role_colorspace_name_get(
          COLOR_ROLE_DEFAULT_BYTE);

      STRNCPY(colorspace_settings->name, colorspace);
    }

    if (ibuf != nullptr) {
      rect = (unsigned char *)ibuf->rect;
      IMB_colormanagement_assign_rect_colorspace(ibuf, colorspace_settings->name);
    }

    copy_v4_v4(fill_color, color);
  }

  if (!ibuf) {
    return nullptr;
  }

  STRNCPY(ibuf->name, name);

  ImageFillData data;

  data.gen_type = gen_type;
  data.width = width;
  data.height = height;
  data.rect = rect;
  data.rect_float = rect_float;
  copy_v4_v4(data.fill_color, fill_color);

  BLI_task_isolate(image_buf_fill_isolated, &data);

  return ibuf;
}

Image *BKE_image_add_generated(Main *bmain,
                               unsigned int width,
                               unsigned int height,
                               const char *name,
                               int depth,
                               int floatbuf,
                               short gen_type,
                               const float color[4],
                               const bool stereo3d,
                               const bool is_data,
                               const bool tiled)
{
  /* Saving the image changes it's #Image.source to #IMA_SRC_FILE (leave as generated here). */
  Image *ima;
  if (tiled) {
    ima = image_alloc(bmain, name, IMA_SRC_TILED, IMA_TYPE_IMAGE);
  }
  else {
    ima = image_alloc(bmain, name, IMA_SRC_GENERATED, IMA_TYPE_UV_TEST);
  }
  if (ima == nullptr) {
    return nullptr;
  }

  int view_id;
  const char *names[2] = {STEREO_LEFT_NAME, STEREO_RIGHT_NAME};

  /* NOTE: leave `ima->filepath` unset,
   * setting it to a dummy value may write to an invalid file-path. */
  ima->gen_x = width;
  ima->gen_y = height;
  ima->gen_type = gen_type;
  ima->gen_flag |= (floatbuf ? IMA_GEN_FLOAT : 0);
  ima->gen_depth = depth;
  copy_v4_v4(ima->gen_color, color);

  if (is_data) {
    STRNCPY(ima->colorspace_settings.name,
            IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
  }

  for (view_id = 0; view_id < 2; view_id++) {
    ImBuf *ibuf;
    ibuf = add_ibuf_size(
        width, height, ima->filepath, depth, floatbuf, gen_type, color, &ima->colorspace_settings);
    int index = tiled ? 0 : IMA_NO_INDEX;
    int entry = tiled ? 1001 : 0;
    image_assign_ibuf(ima, ibuf, stereo3d ? view_id : index, entry);

    /* #image_assign_ibuf puts buffer to the cache, which increments user counter. */
    IMB_freeImBuf(ibuf);
    if (!stereo3d) {
      break;
    }

    image_add_view(ima, names[view_id], "");
  }

  return ima;
}

Image *BKE_image_add_from_imbuf(Main *bmain, ImBuf *ibuf, const char *name)
{
  Image *ima;

  if (name == nullptr) {
    name = BLI_path_basename(ibuf->name);
  }

  ima = image_alloc(bmain, name, IMA_SRC_FILE, IMA_TYPE_IMAGE);

  if (ima) {
    STRNCPY(ima->filepath, ibuf->name);
    image_assign_ibuf(ima, ibuf, IMA_NO_INDEX, 0);
  }

  return ima;
}

/** Pack image buffer to memory as PNG or EXR. */
static bool image_memorypack_imbuf(Image *ima, ImBuf *ibuf, const char *filepath)
{
  ibuf->ftype = (ibuf->rect_float) ? IMB_FTYPE_OPENEXR : IMB_FTYPE_PNG;

  IMB_saveiff(ibuf, filepath, IB_rect | IB_mem);

  if (ibuf->encodedbuffer == nullptr) {
    CLOG_STR_ERROR(&LOG, "memory save for pack error");
    IMB_freeImBuf(ibuf);
    image_free_packedfiles(ima);
    return false;
  }

  ImagePackedFile *imapf;
  PackedFile *pf = MEM_cnew<PackedFile>("PackedFile");

  pf->data = ibuf->encodedbuffer;
  pf->size = ibuf->encodedsize;

  imapf = static_cast<ImagePackedFile *>(MEM_mallocN(sizeof(ImagePackedFile), "Image PackedFile"));
  STRNCPY(imapf->filepath, filepath);
  imapf->packedfile = pf;
  BLI_addtail(&ima->packedfiles, imapf);

  ibuf->encodedbuffer = nullptr;
  ibuf->encodedsize = 0;
  ibuf->userflags &= ~IB_BITMAPDIRTY;

  return true;
}

bool BKE_image_memorypack(Image *ima)
{
  bool ok = true;

  image_free_packedfiles(ima);

  if (BKE_image_is_multiview(ima)) {
    /* Store each view as a separate packed files with R_IMF_VIEWS_INDIVIDUAL. */
    ImageView *iv;
    int i;

    for (i = 0, iv = static_cast<ImageView *>(ima->views.first); iv;
         iv = static_cast<ImageView *>(iv->next), i++) {
      ImBuf *ibuf = image_get_cached_ibuf_for_index_entry(ima, i, 0, nullptr);

      if (!ibuf) {
        ok = false;
        break;
      }

      /* if the image was a R_IMF_VIEWS_STEREO_3D we force _L, _R suffices */
      if (ima->views_format == R_IMF_VIEWS_STEREO_3D) {
        const char *suffix[2] = {STEREO_LEFT_SUFFIX, STEREO_RIGHT_SUFFIX};
        BLI_path_suffix(iv->filepath, FILE_MAX, suffix[i], "");
      }

      ok = ok && image_memorypack_imbuf(ima, ibuf, iv->filepath);
      IMB_freeImBuf(ibuf);
    }

    ima->views_format = R_IMF_VIEWS_INDIVIDUAL;
  }
  else {
    ImBuf *ibuf = image_get_cached_ibuf_for_index_entry(ima, IMA_NO_INDEX, 0, nullptr);

    if (ibuf) {
      ok = ok && image_memorypack_imbuf(ima, ibuf, ibuf->name);
      IMB_freeImBuf(ibuf);
    }
    else {
      ok = false;
    }
  }

  if (ok && ima->source == IMA_SRC_GENERATED) {
    ima->source = IMA_SRC_FILE;
    ima->type = IMA_TYPE_IMAGE;
  }

  return ok;
}

void BKE_image_packfiles(ReportList *reports, Image *ima, const char *basepath)
{
  const int totfiles = image_num_files(ima);

  if (totfiles == 1) {
    ImagePackedFile *imapf = static_cast<ImagePackedFile *>(
        MEM_mallocN(sizeof(ImagePackedFile), "Image packed file"));
    BLI_addtail(&ima->packedfiles, imapf);
    imapf->packedfile = BKE_packedfile_new(reports, ima->filepath, basepath);
    if (imapf->packedfile) {
      STRNCPY(imapf->filepath, ima->filepath);
    }
    else {
      BLI_freelinkN(&ima->packedfiles, imapf);
    }
  }
  else {
    for (ImageView *iv = static_cast<ImageView *>(ima->views.first); iv; iv = iv->next) {
      ImagePackedFile *imapf = static_cast<ImagePackedFile *>(
          MEM_mallocN(sizeof(ImagePackedFile), "Image packed file"));
      BLI_addtail(&ima->packedfiles, imapf);

      imapf->packedfile = BKE_packedfile_new(reports, iv->filepath, basepath);
      if (imapf->packedfile) {
        STRNCPY(imapf->filepath, iv->filepath);
      }
      else {
        BLI_freelinkN(&ima->packedfiles, imapf);
      }
    }
  }
}

void BKE_image_packfiles_from_mem(ReportList *reports,
                                  Image *ima,
                                  char *data,
                                  const size_t data_len)
{
  const int totfiles = image_num_files(ima);

  if (totfiles != 1) {
    BKE_report(reports, RPT_ERROR, "Cannot pack multiview images from raw data currently...");
  }
  else {
    ImagePackedFile *imapf = static_cast<ImagePackedFile *>(
        MEM_mallocN(sizeof(ImagePackedFile), __func__));
    BLI_addtail(&ima->packedfiles, imapf);
    imapf->packedfile = BKE_packedfile_new_from_memory(data, data_len);
    STRNCPY(imapf->filepath, ima->filepath);
  }
}

void BKE_image_tag_time(Image *ima)
{
  ima->lastused = PIL_check_seconds_timer_i();
}

static uintptr_t image_mem_size(Image *image)
{
  uintptr_t size = 0;

  /* viewers have memory depending on other rules, has no valid rect pointer */
  if (image->source == IMA_SRC_VIEWER) {
    return 0;
  }

  BLI_mutex_lock(static_cast<ThreadMutex *>(image->runtime.cache_mutex));

  if (image->cache != nullptr) {
    struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

    while (!IMB_moviecacheIter_done(iter)) {
      ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
      IMB_moviecacheIter_step(iter);
      if (ibuf == nullptr) {
        continue;
      }
      ImBuf *ibufm;
      int level;

      if (ibuf->rect) {
        size += MEM_allocN_len(ibuf->rect);
      }
      if (ibuf->rect_float) {
        size += MEM_allocN_len(ibuf->rect_float);
      }

      for (level = 0; level < IMB_MIPMAP_LEVELS; level++) {
        ibufm = ibuf->mipmap[level];
        if (ibufm) {
          if (ibufm->rect) {
            size += MEM_allocN_len(ibufm->rect);
          }
          if (ibufm->rect_float) {
            size += MEM_allocN_len(ibufm->rect_float);
          }
        }
      }
    }
    IMB_moviecacheIter_free(iter);
  }

  BLI_mutex_unlock(static_cast<ThreadMutex *>(image->runtime.cache_mutex));

  return size;
}

void BKE_image_print_memlist(Main *bmain)
{
  Image *ima;
  uintptr_t size, totsize = 0;

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    totsize += image_mem_size(ima);
  }

  printf("\ntotal image memory len: %.3f MB\n", (double)totsize / (double)(1024 * 1024));

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    size = image_mem_size(ima);

    if (size) {
      printf("%s len: %.3f MB\n", ima->id.name + 2, (double)size / (double)(1024 * 1024));
    }
  }
}

static bool imagecache_check_dirty(ImBuf *ibuf, void *UNUSED(userkey), void *UNUSED(userdata))
{
  if (ibuf == nullptr) {
    return false;
  }
  return (ibuf->userflags & IB_BITMAPDIRTY) == 0;
}

void BKE_image_free_all_textures(Main *bmain)
{
#undef CHECK_FREED_SIZE

  Tex *tex;
  Image *ima;
#ifdef CHECK_FREED_SIZE
  uintptr_t tot_freed_size = 0;
#endif

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    ima->id.tag &= ~LIB_TAG_DOIT;
  }

  for (tex = static_cast<Tex *>(bmain->textures.first); tex;
       tex = static_cast<Tex *>(tex->id.next)) {
    if (tex->ima) {
      tex->ima->id.tag |= LIB_TAG_DOIT;
    }
  }

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    if (ima->cache && (ima->id.tag & LIB_TAG_DOIT)) {
#ifdef CHECK_FREED_SIZE
      uintptr_t old_size = image_mem_size(ima);
#endif

      IMB_moviecache_cleanup(ima->cache, imagecache_check_dirty, nullptr);

#ifdef CHECK_FREED_SIZE
      tot_freed_size += old_size - image_mem_size(ima);
#endif
    }
  }
#ifdef CHECK_FREED_SIZE
  printf("%s: freed total %lu MB\n", __func__, tot_freed_size / (1024 * 1024));
#endif
}

static bool imagecache_check_free_anim(ImBuf *ibuf, void *UNUSED(userkey), void *userdata)
{
  if (ibuf == nullptr) {
    return true;
  }
  int except_frame = *(int *)userdata;
  return (ibuf->userflags & IB_BITMAPDIRTY) == 0 && (ibuf->index != IMA_NO_INDEX) &&
         (except_frame != IMA_INDEX_ENTRY(ibuf->index));
}

void BKE_image_free_anim_ibufs(Image *ima, int except_frame)
{
  BLI_mutex_lock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));
  if (ima->cache != nullptr) {
    IMB_moviecache_cleanup(ima->cache, imagecache_check_free_anim, &except_frame);
  }
  BLI_mutex_unlock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));
}

void BKE_image_all_free_anim_ibufs(Main *bmain, int cfra)
{
  Image *ima;

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    if (BKE_image_is_animated(ima)) {
      BKE_image_free_anim_ibufs(ima, cfra);
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Read and Write
 * \{ */

#define STAMP_NAME_SIZE ((MAX_ID_NAME - 2) + 16)
/* could allow access externally - 512 is for long names,
 * STAMP_NAME_SIZE is for id names, allowing them some room for description */
struct StampDataCustomField {
  struct StampDataCustomField *next, *prev;
  /* TODO(sergey): Think of better size here, maybe dynamically allocated even. */
  char key[512];
  char *value;
  /* TODO(sergey): Support non-string values. */
};

struct StampData {
  char file[512];
  char note[512];
  char date[512];
  char marker[512];
  char time[512];
  char frame[512];
  char frame_range[512];
  char camera[STAMP_NAME_SIZE];
  char cameralens[STAMP_NAME_SIZE];
  char scene[STAMP_NAME_SIZE];
  char strip[STAMP_NAME_SIZE];
  char rendertime[STAMP_NAME_SIZE];
  char memory[STAMP_NAME_SIZE];
  char hostname[512];

  /* Custom fields are used to put extra meta information header from render
   * engine to the result image.
   *
   * NOTE: This fields are not stamped onto the image. At least for now.
   */
  ListBase custom_fields;
};
#undef STAMP_NAME_SIZE

/**
 * \param do_prefix: Include a label like "File ", "Date ", etc. in the stamp data strings.
 * \param use_dynamic: Also include data that can change on a per-frame basis.
 */
static void stampdata(
    const Scene *scene, Object *camera, StampData *stamp_data, int do_prefix, bool use_dynamic)
{
  char text[256];
  struct tm *tl;
  time_t t;

  if (scene->r.stamp & R_STAMP_FILENAME) {
    const char *blendfile_path = BKE_main_blendfile_path_from_global();
    SNPRINTF(stamp_data->file,
             do_prefix ? "File %s" : "%s",
             (blendfile_path[0] != '\0') ? blendfile_path : "<untitled>");
  }
  else {
    stamp_data->file[0] = '\0';
  }

  if (scene->r.stamp & R_STAMP_NOTE) {
    /* Never do prefix for Note */
    SNPRINTF(stamp_data->note, "%s", scene->r.stamp_udata);
  }
  else {
    stamp_data->note[0] = '\0';
  }

  if (scene->r.stamp & R_STAMP_DATE) {
    t = time(nullptr);
    tl = localtime(&t);
    SNPRINTF(text,
             "%04d/%02d/%02d %02d:%02d:%02d",
             tl->tm_year + 1900,
             tl->tm_mon + 1,
             tl->tm_mday,
             tl->tm_hour,
             tl->tm_min,
             tl->tm_sec);
    SNPRINTF(stamp_data->date, do_prefix ? "Date %s" : "%s", text);
  }
  else {
    stamp_data->date[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_MARKER) {
    const char *name = BKE_scene_find_last_marker_name(scene, CFRA);

    if (name) {
      STRNCPY(text, name);
    }
    else {
      STRNCPY(text, "<none>");
    }

    SNPRINTF(stamp_data->marker, do_prefix ? "Marker %s" : "%s", text);
  }
  else {
    stamp_data->marker[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_TIME) {
    const short timecode_style = USER_TIMECODE_SMPTE_FULL;
    BLI_timecode_string_from_time(
        text, sizeof(text), 0, FRA2TIME(scene->r.cfra), FPS, timecode_style);
    SNPRINTF(stamp_data->time, do_prefix ? "Timecode %s" : "%s", text);
  }
  else {
    stamp_data->time[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_FRAME) {
    char fmtstr[32];
    int digits = 1;

    if (scene->r.efra > 9) {
      digits = integer_digits_i(scene->r.efra);
    }

    SNPRINTF(fmtstr, do_prefix ? "Frame %%0%di" : "%%0%di", digits);
    SNPRINTF(stamp_data->frame, fmtstr, scene->r.cfra);
  }
  else {
    stamp_data->frame[0] = '\0';
  }

  if (scene->r.stamp & R_STAMP_FRAME_RANGE) {
    SNPRINTF(stamp_data->frame_range,
             do_prefix ? "Frame Range %d:%d" : "%d:%d",
             scene->r.sfra,
             scene->r.efra);
  }
  else {
    stamp_data->frame_range[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_CAMERA) {
    SNPRINTF(stamp_data->camera,
             do_prefix ? "Camera %s" : "%s",
             camera ? camera->id.name + 2 : "<none>");
  }
  else {
    stamp_data->camera[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_CAMERALENS) {
    if (camera && camera->type == OB_CAMERA) {
      SNPRINTF(text, "%.2f", ((Camera *)camera->data)->lens);
    }
    else {
      STRNCPY(text, "<none>");
    }

    SNPRINTF(stamp_data->cameralens, do_prefix ? "Lens %s" : "%s", text);
  }
  else {
    stamp_data->cameralens[0] = '\0';
  }

  if (scene->r.stamp & R_STAMP_SCENE) {
    SNPRINTF(stamp_data->scene, do_prefix ? "Scene %s" : "%s", scene->id.name + 2);
  }
  else {
    stamp_data->scene[0] = '\0';
  }

  if (use_dynamic && scene->r.stamp & R_STAMP_SEQSTRIP) {
    const Sequence *seq = SEQ_get_topmost_sequence(scene, scene->r.cfra);

    if (seq) {
      STRNCPY(text, seq->name + 2);
    }
    else {
      STRNCPY(text, "<none>");
    }

    SNPRINTF(stamp_data->strip, do_prefix ? "Strip %s" : "%s", text);
  }
  else {
    stamp_data->strip[0] = '\0';
  }

  {
    Render *re = RE_GetSceneRender(scene);
    RenderStats *stats = re ? RE_GetStats(re) : nullptr;

    if (use_dynamic && stats && (scene->r.stamp & R_STAMP_RENDERTIME)) {
      BLI_timecode_string_from_time_simple(text, sizeof(text), stats->lastframetime);

      SNPRINTF(stamp_data->rendertime, do_prefix ? "RenderTime %s" : "%s", text);
    }
    else {
      stamp_data->rendertime[0] = '\0';
    }

    if (use_dynamic && stats && (scene->r.stamp & R_STAMP_MEMORY)) {
      SNPRINTF(stamp_data->memory, do_prefix ? "Peak Memory %.2fM" : "%.2fM", stats->mem_peak);
    }
    else {
      stamp_data->memory[0] = '\0';
    }
  }
  if (scene->r.stamp & R_STAMP_FRAME_RANGE) {
    SNPRINTF(stamp_data->frame_range,
             do_prefix ? "Frame Range %d:%d" : "%d:%d",
             scene->r.sfra,
             scene->r.efra);
  }
  else {
    stamp_data->frame_range[0] = '\0';
  }

  if (scene->r.stamp & R_STAMP_HOSTNAME) {
    char hostname[500]; /* sizeof(stamp_data->hostname) minus some bytes for a label. */
    BLI_hostname_get(hostname, sizeof(hostname));
    SNPRINTF(stamp_data->hostname, do_prefix ? "Hostname %s" : "%s", hostname);
  }
  else {
    stamp_data->hostname[0] = '\0';
  }
}

static void stampdata_from_template(StampData *stamp_data,
                                    const Scene *scene,
                                    const StampData *stamp_data_template,
                                    bool do_prefix)
{
  if (scene->r.stamp & R_STAMP_FILENAME) {
    SNPRINTF(stamp_data->file, do_prefix ? "File %s" : "%s", stamp_data_template->file);
  }
  else {
    stamp_data->file[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_NOTE) {
    SNPRINTF(stamp_data->note, "%s", stamp_data_template->note);
  }
  else {
    stamp_data->note[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_DATE) {
    SNPRINTF(stamp_data->date, do_prefix ? "Date %s" : "%s", stamp_data_template->date);
  }
  else {
    stamp_data->date[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_MARKER) {
    SNPRINTF(stamp_data->marker, do_prefix ? "Marker %s" : "%s", stamp_data_template->marker);
  }
  else {
    stamp_data->marker[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_TIME) {
    SNPRINTF(stamp_data->time, do_prefix ? "Timecode %s" : "%s", stamp_data_template->time);
  }
  else {
    stamp_data->time[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_FRAME) {
    SNPRINTF(stamp_data->frame, do_prefix ? "Frame %s" : "%s", stamp_data_template->frame);
  }
  else {
    stamp_data->frame[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_CAMERA) {
    SNPRINTF(stamp_data->camera, do_prefix ? "Camera %s" : "%s", stamp_data_template->camera);
  }
  else {
    stamp_data->camera[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_CAMERALENS) {
    SNPRINTF(
        stamp_data->cameralens, do_prefix ? "Lens %s" : "%s", stamp_data_template->cameralens);
  }
  else {
    stamp_data->cameralens[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_SCENE) {
    SNPRINTF(stamp_data->scene, do_prefix ? "Scene %s" : "%s", stamp_data_template->scene);
  }
  else {
    stamp_data->scene[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_SEQSTRIP) {
    SNPRINTF(stamp_data->strip, do_prefix ? "Strip %s" : "%s", stamp_data_template->strip);
  }
  else {
    stamp_data->strip[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_RENDERTIME) {
    SNPRINTF(stamp_data->rendertime,
             do_prefix ? "RenderTime %s" : "%s",
             stamp_data_template->rendertime);
  }
  else {
    stamp_data->rendertime[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_MEMORY) {
    SNPRINTF(stamp_data->memory, do_prefix ? "Peak Memory %s" : "%s", stamp_data_template->memory);
  }
  else {
    stamp_data->memory[0] = '\0';
  }
  if (scene->r.stamp & R_STAMP_HOSTNAME) {
    SNPRINTF(
        stamp_data->hostname, do_prefix ? "Hostname %s" : "%s", stamp_data_template->hostname);
  }
  else {
    stamp_data->hostname[0] = '\0';
  }
}

void BKE_image_stamp_buf(Scene *scene,
                         Object *camera,
                         const StampData *stamp_data_template,
                         unsigned char *rect,
                         float *rectf,
                         int width,
                         int height,
                         int channels)
{
  struct StampData stamp_data;
  float w, h, pad;
  int x, y, y_ofs;
  float h_fixed;
  const int mono = blf_mono_font_render; /* XXX */
  struct ColorManagedDisplay *display;
  const char *display_device;

  /* vars for calculating wordwrap */
  struct {
    struct ResultBLF info;
    rctf rect;
  } wrap;

  /* this could be an argument if we want to operate on non linear float imbuf's
   * for now though this is only used for renders which use scene settings */

#define TEXT_SIZE_CHECK(str, w, h) \
  ((str[0]) && ((void)(h = h_fixed), (w = BLF_width(mono, str, sizeof(str)))))

  /* must enable BLF_WORD_WRAP before using */
#define TEXT_SIZE_CHECK_WORD_WRAP(str, w, h) \
  ((str[0]) && (BLF_boundbox_ex(mono, str, sizeof(str), &wrap.rect, &wrap.info), \
                (void)(h = h_fixed * wrap.info.lines), \
                (w = BLI_rctf_size_x(&wrap.rect))))

#define BUFF_MARGIN_X 2
#define BUFF_MARGIN_Y 1

  if (!rect && !rectf) {
    return;
  }

  display_device = scene->display_settings.display_device;
  display = IMB_colormanagement_display_get_named(display_device);

  bool do_prefix = (scene->r.stamp & R_STAMP_HIDE_LABELS) == 0;
  if (stamp_data_template == nullptr) {
    stampdata(scene, camera, &stamp_data, do_prefix, true);
  }
  else {
    stampdata_from_template(&stamp_data, scene, stamp_data_template, do_prefix);
  }

  /* TODO: do_versions. */
  if (scene->r.stamp_font_id < 8) {
    scene->r.stamp_font_id = 12;
  }

  /* set before return */
  BLF_size(mono, scene->r.stamp_font_id, 72);
  BLF_wordwrap(mono, width - (BUFF_MARGIN_X * 2));

  BLF_buffer(mono, rectf, rect, width, height, channels, display);
  BLF_buffer_col(mono, scene->r.fg_stamp);
  pad = BLF_width_max(mono);

  /* use 'h_fixed' rather than 'h', aligns better */
  h_fixed = BLF_height_max(mono);
  y_ofs = -BLF_descender(mono);

  x = 0;
  y = height;

  if (TEXT_SIZE_CHECK(stamp_data.file, w, h)) {
    /* Top left corner */
    y -= h;

    /* also a little of space to the background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    /* and draw the text. */
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.file, sizeof(stamp_data.file));

    /* the extra pixel for background. */
    y -= BUFF_MARGIN_Y * 2;
  }

  /* Top left corner, below File */
  if (TEXT_SIZE_CHECK(stamp_data.date, w, h)) {
    y -= h;

    /* and space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      0,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.date, sizeof(stamp_data.date));

    /* the extra pixel for background. */
    y -= BUFF_MARGIN_Y * 2;
  }

  /* Top left corner, below File, Date */
  if (TEXT_SIZE_CHECK(stamp_data.rendertime, w, h)) {
    y -= h;

    /* and space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      0,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.rendertime, sizeof(stamp_data.rendertime));

    /* the extra pixel for background. */
    y -= BUFF_MARGIN_Y * 2;
  }

  /* Top left corner, below File, Date, Rendertime */
  if (TEXT_SIZE_CHECK(stamp_data.memory, w, h)) {
    y -= h;

    /* and space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      0,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.memory, sizeof(stamp_data.memory));

    /* the extra pixel for background. */
    y -= BUFF_MARGIN_Y * 2;
  }

  /* Top left corner, below File, Date, Rendertime, Memory */
  if (TEXT_SIZE_CHECK(stamp_data.hostname, w, h)) {
    y -= h;

    /* and space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      0,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.hostname, sizeof(stamp_data.hostname));

    /* the extra pixel for background. */
    y -= BUFF_MARGIN_Y * 2;
  }

  /* Top left corner, below File, Date, Memory, Rendertime, Hostname */
  BLF_enable(mono, BLF_WORD_WRAP);
  if (TEXT_SIZE_CHECK_WORD_WRAP(stamp_data.note, w, h)) {
    y -= h;

    /* and space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      0,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs + (h - h_fixed), 0.0);
    BLF_draw_buffer(mono, stamp_data.note, sizeof(stamp_data.note));
  }
  BLF_disable(mono, BLF_WORD_WRAP);

  x = 0;
  y = 0;

  /* Bottom left corner, leaving space for timing */
  if (TEXT_SIZE_CHECK(stamp_data.marker, w, h)) {

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    /* and pad the text. */
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.marker, sizeof(stamp_data.marker));

    /* space width. */
    x += w + pad;
  }

  /* Left bottom corner */
  if (TEXT_SIZE_CHECK(stamp_data.time, w, h)) {

    /* extra space for background */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    /* and pad the text. */
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.time, sizeof(stamp_data.time));

    /* space width. */
    x += w + pad;
  }

  if (TEXT_SIZE_CHECK(stamp_data.frame, w, h)) {

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    /* and pad the text. */
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.frame, sizeof(stamp_data.frame));

    /* space width. */
    x += w + pad;
  }

  if (TEXT_SIZE_CHECK(stamp_data.camera, w, h)) {

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.camera, sizeof(stamp_data.camera));

    /* space width. */
    x += w + pad;
  }

  if (TEXT_SIZE_CHECK(stamp_data.cameralens, w, h)) {

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.cameralens, sizeof(stamp_data.cameralens));
  }

  if (TEXT_SIZE_CHECK(stamp_data.scene, w, h)) {

    /* Bottom right corner, with an extra space because the BLF API is too strict! */
    x = width - w - 2;

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    /* and pad the text. */
    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.scene, sizeof(stamp_data.scene));
  }

  if (TEXT_SIZE_CHECK(stamp_data.strip, w, h)) {

    /* Top right corner, with an extra space because the BLF API is too strict! */
    x = width - w - pad;
    y = height - h;

    /* extra space for background. */
    buf_rectfill_area(rect,
                      rectf,
                      width,
                      height,
                      scene->r.bg_stamp,
                      display,
                      x - BUFF_MARGIN_X,
                      y - BUFF_MARGIN_Y,
                      x + w + BUFF_MARGIN_X,
                      y + h + BUFF_MARGIN_Y);

    BLF_position(mono, x, y + y_ofs, 0.0);
    BLF_draw_buffer(mono, stamp_data.strip, sizeof(stamp_data.strip));
  }

  /* cleanup the buffer. */
  BLF_buffer(mono, nullptr, nullptr, 0, 0, 0, nullptr);
  BLF_wordwrap(mono, 0);

#undef TEXT_SIZE_CHECK
#undef TEXT_SIZE_CHECK_WORD_WRAP
#undef BUFF_MARGIN_X
#undef BUFF_MARGIN_Y
}

void BKE_render_result_stamp_info(Scene *scene,
                                  Object *camera,
                                  struct RenderResult *rr,
                                  bool allocate_only)
{
  struct StampData *stamp_data;

  if (!(scene && (scene->r.stamp & R_STAMP_ALL)) && !allocate_only) {
    return;
  }

  if (!rr->stamp_data) {
    stamp_data = MEM_cnew<StampData>("RenderResult.stamp_data");
  }
  else {
    stamp_data = rr->stamp_data;
  }

  if (!allocate_only) {
    stampdata(scene, camera, stamp_data, 0, true);
  }

  if (!rr->stamp_data) {
    rr->stamp_data = stamp_data;
  }
}

struct StampData *BKE_stamp_info_from_scene_static(const Scene *scene)
{
  struct StampData *stamp_data;

  if (!(scene && (scene->r.stamp & R_STAMP_ALL))) {
    return nullptr;
  }

  /* Memory is allocated here (instead of by the caller) so that the caller
   * doesn't have to know the size of the StampData struct. */
  stamp_data = MEM_cnew<StampData>(__func__);
  stampdata(scene, nullptr, stamp_data, 0, false);

  return stamp_data;
}

static const char *stamp_metadata_fields[] = {
    "File",
    "Note",
    "Date",
    "Marker",
    "Time",
    "Frame",
    "FrameRange",
    "Camera",
    "Lens",
    "Scene",
    "Strip",
    "RenderTime",
    "Memory",
    "Hostname",
    nullptr,
};

bool BKE_stamp_is_known_field(const char *field_name)
{
  int i = 0;
  while (stamp_metadata_fields[i] != nullptr) {
    if (STREQ(field_name, stamp_metadata_fields[i])) {
      return true;
    }
    i++;
  }
  return false;
}

void BKE_stamp_info_callback(void *data,
                             struct StampData *stamp_data,
                             StampCallback callback,
                             bool noskip)
{
  if ((callback == nullptr) || (stamp_data == nullptr)) {
    return;
  }

#define CALL(member, value_str) \
  if (noskip || stamp_data->member[0]) { \
    callback(data, value_str, stamp_data->member, sizeof(stamp_data->member)); \
  } \
  ((void)0)

  /* TODO(sergey): Use stamp_metadata_fields somehow, or make it more generic
   * meta information to avoid duplication. */
  CALL(file, "File");
  CALL(note, "Note");
  CALL(date, "Date");
  CALL(marker, "Marker");
  CALL(time, "Time");
  CALL(frame, "Frame");
  CALL(frame_range, "FrameRange");
  CALL(camera, "Camera");
  CALL(cameralens, "Lens");
  CALL(scene, "Scene");
  CALL(strip, "Strip");
  CALL(rendertime, "RenderTime");
  CALL(memory, "Memory");
  CALL(hostname, "Hostname");

  LISTBASE_FOREACH (StampDataCustomField *, custom_field, &stamp_data->custom_fields) {
    if (noskip || custom_field->value[0]) {
      callback(data, custom_field->key, custom_field->value, strlen(custom_field->value) + 1);
    }
  }

#undef CALL
}

void BKE_render_result_stamp_data(RenderResult *rr, const char *key, const char *value)
{
  StampData *stamp_data;
  if (rr->stamp_data == nullptr) {
    rr->stamp_data = MEM_cnew<StampData>("RenderResult.stamp_data");
  }
  stamp_data = rr->stamp_data;
  StampDataCustomField *field = static_cast<StampDataCustomField *>(
      MEM_mallocN(sizeof(StampDataCustomField), "StampData Custom Field"));
  STRNCPY(field->key, key);
  field->value = BLI_strdup(value);
  BLI_addtail(&stamp_data->custom_fields, field);
}

StampData *BKE_stamp_data_copy(const StampData *stamp_data)
{
  if (stamp_data == nullptr) {
    return nullptr;
  }

  StampData *stamp_datan = static_cast<StampData *>(MEM_dupallocN(stamp_data));
  BLI_duplicatelist(&stamp_datan->custom_fields, &stamp_data->custom_fields);

  LISTBASE_FOREACH (StampDataCustomField *, custom_fieldn, &stamp_datan->custom_fields) {
    custom_fieldn->value = static_cast<char *>(MEM_dupallocN(custom_fieldn->value));
  }

  return stamp_datan;
}

void BKE_stamp_data_free(StampData *stamp_data)
{
  if (stamp_data == nullptr) {
    return;
  }
  LISTBASE_FOREACH (StampDataCustomField *, custom_field, &stamp_data->custom_fields) {
    MEM_freeN(custom_field->value);
  }
  BLI_freelistN(&stamp_data->custom_fields);
  MEM_freeN(stamp_data);
}

/* wrap for callback only */
static void metadata_set_field(void *data, const char *propname, char *propvalue, int UNUSED(len))
{
  /* We know it is an ImBuf* because that's what we pass to BKE_stamp_info_callback. */
  ImBuf *imbuf = static_cast<ImBuf *>(data);
  IMB_metadata_set_field(imbuf->metadata, propname, propvalue);
}

static void metadata_get_field(void *data, const char *propname, char *propvalue, int len)
{
  /* We know it is an ImBuf* because that's what we pass to BKE_stamp_info_callback. */
  ImBuf *imbuf = static_cast<ImBuf *>(data);
  IMB_metadata_get_field(imbuf->metadata, propname, propvalue, len);
}

void BKE_imbuf_stamp_info(const RenderResult *rr, ImBuf *ibuf)
{
  StampData *stamp_data = const_cast<StampData *>(rr->stamp_data);
  IMB_metadata_ensure(&ibuf->metadata);
  BKE_stamp_info_callback(ibuf, stamp_data, metadata_set_field, false);
}

static void metadata_copy_custom_fields(const char *field, const char *value, void *rr_v)
{
  if (BKE_stamp_is_known_field(field)) {
    return;
  }
  RenderResult *rr = (RenderResult *)rr_v;
  BKE_render_result_stamp_data(rr, field, value);
}

void BKE_stamp_info_from_imbuf(RenderResult *rr, ImBuf *ibuf)
{
  if (rr->stamp_data == nullptr) {
    rr->stamp_data = MEM_cnew<StampData>("RenderResult.stamp_data");
  }
  StampData *stamp_data = rr->stamp_data;
  IMB_metadata_ensure(&ibuf->metadata);
  BKE_stamp_info_callback(ibuf, stamp_data, metadata_get_field, true);
  /* Copy render engine specific settings. */
  IMB_metadata_foreach(ibuf, metadata_copy_custom_fields, rr);
}

bool BKE_imbuf_alpha_test(ImBuf *ibuf)
{
  int tot;
  if (ibuf->rect_float) {
    const float *buf = ibuf->rect_float;
    for (tot = ibuf->x * ibuf->y; tot--; buf += 4) {
      if (buf[3] < 1.0f) {
        return true;
      }
    }
  }
  else if (ibuf->rect) {
    unsigned char *buf = (unsigned char *)ibuf->rect;
    for (tot = ibuf->x * ibuf->y; tot--; buf += 4) {
      if (buf[3] != 255) {
        return true;
      }
    }
  }

  return false;
}

int BKE_imbuf_write(ImBuf *ibuf, const char *name, const ImageFormatData *imf)
{
  BKE_image_format_to_imbuf(ibuf, imf);

  BLI_make_existing_file(name);

  const bool ok = IMB_saveiff(ibuf, name, IB_rect | IB_zbuf | IB_zbuffloat);
  if (ok == 0) {
    perror(name);
  }

  return ok;
}

int BKE_imbuf_write_as(ImBuf *ibuf, const char *name, ImageFormatData *imf, const bool save_copy)
{
  ImBuf ibuf_back = *ibuf;
  int ok;

  /* All data is RGBA anyway, this just controls how to save for some formats. */
  ibuf->planes = imf->planes;

  ok = BKE_imbuf_write(ibuf, name, imf);

  if (save_copy) {
    /* note that we are not restoring _all_ settings */
    ibuf->planes = ibuf_back.planes;
    ibuf->ftype = ibuf_back.ftype;
    ibuf->foptions = ibuf_back.foptions;
  }

  return ok;
}

int BKE_imbuf_write_stamp(const Scene *scene,
                          const struct RenderResult *rr,
                          ImBuf *ibuf,
                          const char *name,
                          const struct ImageFormatData *imf)
{
  if (scene && scene->r.stamp & R_STAMP_ALL) {
    BKE_imbuf_stamp_info(rr, ibuf);
  }

  return BKE_imbuf_write(ibuf, name, imf);
}

struct anim *openanim_noload(const char *name,
                             int flags,
                             int streamindex,
                             char colorspace[IMA_MAX_SPACE])
{
  struct anim *anim;

  anim = IMB_open_anim(name, flags, streamindex, colorspace);
  return anim;
}

struct anim *openanim(const char *name, int flags, int streamindex, char colorspace[IMA_MAX_SPACE])
{
  struct anim *anim;
  struct ImBuf *ibuf;

  anim = IMB_open_anim(name, flags, streamindex, colorspace);
  if (anim == nullptr) {
    return nullptr;
  }

  ibuf = IMB_anim_absolute(anim, 0, IMB_TC_NONE, IMB_PROXY_NONE);
  if (ibuf == nullptr) {
    if (BLI_exists(name)) {
      printf("not an anim: %s\n", name);
    }
    else {
      printf("anim file doesn't exist: %s\n", name);
    }
    IMB_free_anim(anim);
    return nullptr;
  }
  IMB_freeImBuf(ibuf);

  return anim;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name New Image API
 * \{ */

/* Notes about Image storage
 * - packedfile
 *   -> written in .blend
 * - filename
 *   -> written in .blend
 * - movie
 *   -> comes from packedfile or filename
 * - renderresult
 *   -> comes from packedfile or filename
 * - listbase
 *   -> ibufs from EXR-handle.
 * - flip-book array
 *   -> ibufs come from movie, temporary renderresult or sequence
 * - ibuf
 *   -> comes from packedfile or filename or generated
 */

Image *BKE_image_ensure_viewer(Main *bmain, int type, const char *name)
{
  Image *ima;

  for (ima = static_cast<Image *>(bmain->images.first); ima;
       ima = static_cast<Image *>(ima->id.next)) {
    if (ima->source == IMA_SRC_VIEWER) {
      if (ima->type == type) {
        break;
      }
    }
  }

  if (ima == nullptr) {
    ima = image_alloc(bmain, name, IMA_SRC_VIEWER, type);
  }

  /* Happens on reload, imagewindow cannot be image user when hidden. */
  if (ima->id.us == 0) {
    id_us_ensure_real(&ima->id);
  }

  return ima;
}

static void image_viewer_create_views(const RenderData *rd, Image *ima)
{
  if ((rd->scemode & R_MULTIVIEW) == 0) {
    image_add_view(ima, "", "");
  }
  else {
    for (SceneRenderView *srv = static_cast<SceneRenderView *>(rd->views.first); srv;
         srv = srv->next) {
      if (BKE_scene_multiview_is_render_view_active(rd, srv) == false) {
        continue;
      }
      image_add_view(ima, srv->name, "");
    }
  }
}

void BKE_image_ensure_viewer_views(const RenderData *rd, Image *ima, ImageUser *iuser)
{
  bool do_reset;
  const bool is_multiview = (rd->scemode & R_MULTIVIEW) != 0;

  BLI_thread_lock(LOCK_DRAW_IMAGE);

  if (!BKE_scene_multiview_is_stereo3d(rd)) {
    iuser->flag &= ~IMA_SHOW_STEREO;
  }

  /* see if all scene render views are in the image view list */
  do_reset = (BKE_scene_multiview_num_views_get(rd) != BLI_listbase_count(&ima->views));

  /* multiview also needs to be sure all the views are synced */
  if (is_multiview && !do_reset) {
    SceneRenderView *srv;
    ImageView *iv;

    for (iv = static_cast<ImageView *>(ima->views.first); iv; iv = iv->next) {
      srv = static_cast<SceneRenderView *>(
          BLI_findstring(&rd->views, iv->name, offsetof(SceneRenderView, name)));
      if ((srv == nullptr) || (BKE_scene_multiview_is_render_view_active(rd, srv) == false)) {
        do_reset = true;
        break;
      }
    }
  }

  if (do_reset) {
    BLI_mutex_lock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));

    image_free_cached_frames(ima);
    BKE_image_free_views(ima);

    /* add new views */
    image_viewer_create_views(rd, ima);

    BLI_mutex_unlock(static_cast<ThreadMutex *>(ima->runtime.cache_mutex));
  }

  BLI_thread_unlock(LOCK_DRAW_IMAGE);
}

static void image_walk_ntree_all_users(
    bNodeTree *ntree,
    ID *id,
    void *customdata,
    void callback(Image *ima, ID *iuser_id, ImageUser *iuser, void *customdata))
{
  switch (ntree->type) {
    case NTREE_SHADER:
      LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
        if (node->id) {
          if (node->type == SH_NODE_TEX_IMAGE) {
            NodeTexImage *tex = static_cast<NodeTexImage *>(node->storage);
            Image *ima = (Image *)node->id;
            callback(ima, id, &tex->iuser, customdata);
          }
          if (node->type == SH_NODE_TEX_ENVIRONMENT) {
            NodeTexImage *tex = static_cast<NodeTexImage *>(node->storage);
            Image *ima = (Image *)node->id;
            callback(ima, id, &tex->iuser, customdata);
          }
        }
      }
      break;
    case NTREE_TEXTURE:
      LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
        if (node->id && node->type == TEX_NODE_IMAGE) {
          Image *ima = (Image *)node->id;
          ImageUser *iuser = static_cast<ImageUser *>(node->storage);
          callback(ima, id, iuser, customdata);
        }
      }
      break;
    case NTREE_COMPOSIT:
      LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
        if (node->id && node->type == CMP_NODE_IMAGE) {
          Image *ima = (Image *)node->id;
          ImageUser *iuser = static_cast<ImageUser *>(node->storage);
          callback(ima, id, iuser, customdata);
        }
      }
      break;
  }
}

static void image_walk_gpu_materials(
    ID *id,
    ListBase *gpu_materials,
    void *customdata,
    void callback(Image *ima, ID *iuser_id, ImageUser *iuser, void *customdata))
{
  LISTBASE_FOREACH (LinkData *, link, gpu_materials) {
    GPUMaterial *gpu_material = (GPUMaterial *)link->data;
    ListBase textures = GPU_material_textures(gpu_material);
    LISTBASE_FOREACH (GPUMaterialTexture *, gpu_material_texture, &textures) {
      if (gpu_material_texture->iuser_available) {
        callback(gpu_material_texture->ima, id, &gpu_material_texture->iuser, customdata);
      }
    }
  }
}

static void image_walk_id_all_users(
    ID *id,
    bool skip_nested_nodes,
    void *customdata,
    void callback(Image *ima, ID *iuser_id, ImageUser *iuser, void *customdata))
{
  switch (GS(id->name)) {
    case ID_OB: {
      Object *ob = (Object *)id;
      if (ob->empty_drawtype == OB_EMPTY_IMAGE && ob->data) {
        callback(static_cast<Image *>(ob->data), &ob->id, ob->iuser, customdata);
      }
      break;
    }
    case ID_MA: {
      Material *ma = (Material *)id;
      if (ma->nodetree && ma->use_nodes && !skip_nested_nodes) {
        image_walk_ntree_all_users(ma->nodetree, &ma->id, customdata, callback);
      }
      image_walk_gpu_materials(id, &ma->gpumaterial, customdata, callback);
      break;
    }
    case ID_LA: {
      Light *light = (Light *)id;
      if (light->nodetree && light->use_nodes && !skip_nested_nodes) {
        image_walk_ntree_all_users(light->nodetree, &light->id, customdata, callback);
      }
      break;
    }
    case ID_WO: {
      World *world = (World *)id;
      if (world->nodetree && world->use_nodes && !skip_nested_nodes) {
        image_walk_ntree_all_users(world->nodetree, &world->id, customdata, callback);
      }
      image_walk_gpu_materials(id, &world->gpumaterial, customdata, callback);
      break;
    }
    case ID_TE: {
      Tex *tex = (Tex *)id;
      if (tex->type == TEX_IMAGE && tex->ima) {
        callback(tex->ima, &tex->id, &tex->iuser, customdata);
      }
      if (tex->nodetree && tex->use_nodes && !skip_nested_nodes) {
        image_walk_ntree_all_users(tex->nodetree, &tex->id, customdata, callback);
      }
      break;
    }
    case ID_NT: {
      bNodeTree *ntree = (bNodeTree *)id;
      image_walk_ntree_all_users(ntree, &ntree->id, customdata, callback);
      break;
    }
    case ID_CA: {
      Camera *cam = (Camera *)id;
      LISTBASE_FOREACH (CameraBGImage *, bgpic, &cam->bg_images) {
        callback(bgpic->ima, nullptr, &bgpic->iuser, customdata);
      }
      break;
    }
    case ID_WM: {
      wmWindowManager *wm = (wmWindowManager *)id;
      LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
        const bScreen *screen = BKE_workspace_active_screen_get(win->workspace_hook);

        LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
          if (area->spacetype == SPACE_IMAGE) {
            SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);
            callback(sima->image, nullptr, &sima->iuser, customdata);
          }
        }
      }
      break;
    }
    case ID_SCE: {
      Scene *scene = (Scene *)id;
      if (scene->nodetree && scene->use_nodes && !skip_nested_nodes) {
        image_walk_ntree_all_users(scene->nodetree, &scene->id, customdata, callback);
      }
      break;
    }
    case ID_SIM: {
      Simulation *simulation = (Simulation *)id;
      image_walk_ntree_all_users(simulation->nodetree, &simulation->id, customdata, callback);
      break;
    }
    default:
      break;
  }
}

void BKE_image_walk_all_users(
    const Main *mainp,
    void *customdata,
    void callback(Image *ima, ID *iuser_id, ImageUser *iuser, void *customdata))
{
  for (Scene *scene = static_cast<Scene *>(mainp->scenes.first); scene;
       scene = static_cast<Scene *>(scene->id.next)) {
    image_walk_id_all_users(&scene->id, false, customdata, callback);
  }

  for (Object *ob = static_cast<Object *>(mainp->objects.first); ob;
       ob = static_cast<Object *>(ob->id.next)) {
    image_walk_id_all_users(&ob->id, false, customdata, callback);
  }

  for (bNodeTree *ntree = static_cast<bNodeTree *>(mainp->nodetrees.first); ntree;
       ntree = static_cast<bNodeTree *>(ntree->id.next)) {
    image_walk_id_all_users(&ntree->id, false, customdata, callback);
  }

  for (Material *ma = static_cast<Material *>(mainp->materials.first); ma;
       ma = static_cast<Material *>(ma->id.next)) {
    image_walk_id_all_users(&ma->id, false, customdata, callback);
  }

  for (Light *light = static_cast<Light *>(mainp->materials.first); light;
       light = static_cast<Light *>(light->id.next)) {
    image_walk_id_all_users(&light->id, false, customdata, callback);
  }

  for (World *world = static_cast<World *>(mainp->materials.first); world;
       world = static_cast<World *>(world->id.next)) {
    image_walk_id_all_users(&world->id, false, customdata, callback);
  }

  for (Tex *tex = static_cast<Tex *>(mainp->textures.first); tex;
       tex = static_cast<Tex *>(tex->id.next)) {
    image_walk_id_all_users(&tex->id, false, customdata, callback);
  }

  for (Camera *cam = static_cast<Camera *>(mainp->cameras.first); cam;
       cam = static_cast<Camera *>(cam->id.next)) {
    image_walk_id_all_users(&cam->id, false, customdata, callback);
  }

  for (wmWindowManager *wm = static_cast<wmWindowManager *>(mainp->wm.first); wm;
       wm = static_cast<wmWindowManager *>(wm->id.next)) { /* only 1 wm */
    image_walk_id_all_users(&wm->id, false, customdata, callback);
  }
}

static void image_tag_frame_recalc(Image *ima, ID *iuser_id, ImageUser *iuser, void *customdata)
{
  Image *changed_image = static_cast<Image *>(customdata);

  if (ima == changed_image && BKE_image_is_animated(ima)) {
    iuser->flag |= IMA_NEED_FRAME_RECALC;

    if (iuser_id) {
      /* Must copy image user changes to CoW data-block. */
      DEG_id_tag_update(iuser_id, ID_RECALC_COPY_ON_WRITE);
    }
  }
}
