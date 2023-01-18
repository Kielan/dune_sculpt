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
