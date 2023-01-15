#include <string.h>

#include "structs_anim_types.h"
#include "structs_cachefile_types.h"
#include "structs_constraint_types.h"
#include "structs_object_types.h"
#include "structs_scene_types.h"

#include "LIB_fileops.h"
#include "LIB_ghash.h"
#include "LIB_listbase.h"
#include "LIB_path_util.h"
#include "LIB_string.h"
#include "LIB_threads.h"
#include "LIB_utildefines.h"

#include "TRANSLATION_translation.h"

#include "KERNEL_anim_data.h"
#include "KERNEL_bpath.h"
#include "KERNEL_cachefile.h"
#include "KERNEL_idtype.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_main.h"
#include "KERNEL_modifier.h"
#include "KERNEL_scene.h"

#include "DEG_depsgraph_query.h"

#include "RE_engine.h"

#include "LOADER_read_write.h"

#include "MEM_guardedalloc.h"

#ifdef WITH_ALEMBIC
#  include "ABC_alembic.h"
#endif

#ifdef WITH_USD
#  include "usd.h"
#endif

static void cachefile_handle_free(CacheFile *cache_file);

static void cache_file_init_data(ID *id)
{
  CacheFile *cache_file = (CacheFile *)id;

  LIB_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(cache_file, id));

  cache_file->scale = 1.0f;
  cache_file->velocity_unit = CACHEFILE_VELOCITY_UNIT_SECOND;
  LIB_strncpy(cache_file->velocity_name, ".velocities", sizeof(cache_file->velocity_name));
}

static void cache_file_copy_data(Main *UNUSED(dunemain),
                                 ID *id_dst,
                                 const ID *id_src,
                                 const int UNUSED(flag))
{
  CacheFile *cache_file_dst = (CacheFile *)id_dst;
  const CacheFile *cache_file_src = (const CacheFile *)id_src;

  cache_file_dst->handle = NULL;
  cache_file_dst->handle_readers = NULL;
  LIB_duplicatelist(&cache_file_dst->object_paths, &cache_file_src->object_paths);
  LIB_duplicatelist(&cache_file_dst->layers, &cache_file_src->layers);
}

static void cache_file_free_data(ID *id)
{
  CacheFile *cache_file = (CacheFile *)id;
  cachefile_handle_free(cache_file);
  LIB_freelistN(&cache_file->object_paths);
  LIB_freelistN(&cache_file->layers);
}

static void cache_file_foreach_path(ID *id, BPathForeachPathData *dunepath_data)
{
  CacheFile *cache_file = (CacheFile *)id;
  KERNEL_dunepath_foreach_path_fixed_process(dunepath_data, cache_file->filepath);
}

static void cache_file_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  CacheFile *cache_file = (CacheFile *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  LIB_listbase_clear(&cache_file->object_paths);
  cache_file->handle = NULL;
  memset(cache_file->handle_filepath, 0, sizeof(cache_file->handle_filepath));
  cache_file->handle_readers = NULL;

  LOADER_write_id_struct(writer, CacheFile, id_address, &cache_file->id);
  KERNEL_id_dune_write(writer, &cache_file->id);

  if (cache_file->adt) {
    KERNEL_animdata_dune_write(writer, cache_file->adt);
  }

  /* write layers */
  LISTBASE_FOREACH (CacheFileLayer *, layer, &cache_file->layers) {
    LOADER_write_struct(writer, CacheFileLayer, layer);
  }
}

static void cache_file_dune_read_data(BlendDataReader *reader, ID *id)
{
  CacheFile *cache_file = (CacheFile *)id;
  LIB_listbase_clear(&cache_file->object_paths);
  cache_file->handle = NULL;
  cache_file->handle_filepath[0] = '\0';
  cache_file->handle_readers = NULL;

  /* relink animdata */
  LOADER_read_data_address(reader, &cache_file->adt);
  KERNEL_animdata_dune_read_data(reader, cache_file->adt);

  /* relink layers */
  LOADER_read_list(reader, &cache_file->layers);
}

IDTypeInfo IDType_ID_CF = {
    .id_code = ID_CF,
    .id_filter = FILTER_ID_CF,
    .main_listbase_index = INDEX_ID_CF,
    .struct_size = sizeof(CacheFile),
    .name = "CacheFile",
    .name_plural = "cache_files",
    .translation_context = BLT_I18NCONTEXT_ID_CACHEFILE,
    .flags = IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = cache_file_init_data,
    .copy_data = cache_file_copy_data,
    .free_data = cache_file_free_data,
    .make_local = NULL,
    .foreach_id = NULL,
    .foreach_cache = NULL,
    .foreach_path = cache_file_foreach_path,
    .owner_get = NULL,

    .dune_write = cache_file_dune_write,
    .dune_read_data = cache_file_dune_read_data,
    .dune_read_lib = NULL,
    .dune_read_expand = NULL,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* TODO: make this per cache file to avoid global locks. */
static SpinLock spin;

void KERNEL_cachefiles_init(void)
{
  LIB_spin_init(&spin);
}

void KERNEL_cachefiles_exit(void)
{
  LIB_spin_end(&spin);
}

void KERNEL_cachefile_reader_open(CacheFile *cache_file,
                               struct CacheReader **reader,
                               Object *object,
                               const char *object_path)
{
#if defined(WITH_ALEMBIC) || defined(WITH_USD)

  LIB_assert(cache_file->id.tag & LIB_TAG_COPIED_ON_WRITE);

  if (cache_file->handle == NULL) {
    return;
  }

  switch (cache_file->type) {
    case CACHEFILE_TYPE_ALEMBIC:
#  ifdef WITH_ALEMBIC
      /* Open Alembic cache reader. */
      *reader = CacheReader_open_alembic_object(cache_file->handle, *reader, object, object_path);
#  endif
      break;
    case CACHEFILE_TYPE_USD:
#  ifdef WITH_USD
      /* Open USD cache reader. */
      *reader = CacheReader_open_usd_object(cache_file->handle, *reader, object, object_path);
#  endif
      break;
    case CACHE_FILE_TYPE_INVALID:
      break;
  }

  /* Multiple modifiers and constraints can call this function concurrently. */
  LIB_spin_lock(&spin);
  if (*reader) {
    /* Register in set so we can free it when the cache file changes. */
    if (cache_file->handle_readers == NULL) {
      cache_file->handle_readers = LIB_gset_ptr_new("CacheFile.handle_readers");
    }
    LIB_gset_reinsert(cache_file->handle_readers, reader, NULL);
  }
  else if (cache_file->handle_readers) {
    /* Remove in case CacheReader_open_alembic_object free the existing reader. */
    LIB_gset_remove(cache_file->handle_readers, reader, NULL);
  }
  LIB_spin_unlock(&spin);
#else
  UNUSED_VARS(cache_file, reader, object, object_path);
#endif
}

void KERNEL_cachefile_reader_free(CacheFile *cache_file, struct CacheReader **reader)
{
#if defined(WITH_ALEMBIC) || defined(WITH_USD)
  /* Multiple modifiers and constraints can call this function concurrently, and
   * cachefile_handle_free() can also be called at the same time. */
  LIB_spin_lock(&spin);
  if (*reader != NULL) {
    if (cache_file) {
      LIB_assert(cache_file->id.tag & LIB_TAG_COPIED_ON_WRITE);

      switch (cache_file->type) {
        case CACHEFILE_TYPE_ALEMBIC:
#  ifdef WITH_ALEMBIC
          ABC_CacheReader_free(*reader);
#  endif
          break;
        case CACHEFILE_TYPE_USD:
#  ifdef WITH_USD
          USD_CacheReader_free(*reader);
#  endif
          break;
        case CACHE_FILE_TYPE_INVALID:
          break;
      }
    }

    *reader = NULL;

    if (cache_file && cache_file->handle_readers) {
      LIB_gset_remove(cache_file->handle_readers, reader, NULL);
    }
  }
  LIB_spin_unlock(&spin);
#else
  UNUSED_VARS(cache_file, reader);
#endif
}

static void cachefile_handle_free(CacheFile *cache_file)
{
#if defined(WITH_ALEMBIC) || defined(WITH_USD)

  /* Free readers in all modifiers and constraints that use the handle, before
   * we free the handle itself. */
  LIB_spin_lock(&spin);
  if (cache_file->handle_readers) {
    GSetIterator gs_iter;
    GSET_ITER (gs_iter, cache_file->handle_readers) {
      struct CacheReader **reader = LIB_gsetIterator_getKey(&gs_iter);
      if (*reader != NULL) {
        switch (cache_file->type) {
          case CACHEFILE_TYPE_ALEMBIC:
#  ifdef WITH_ALEMBIC
            ABC_CacheReader_free(*reader);
#  endif
            break;
          case CACHEFILE_TYPE_USD:
#  ifdef WITH_USD
            USD_CacheReader_free(*reader);
#  endif
            break;
          case CACHE_FILE_TYPE_INVALID:
            break;
        }

        *reader = NULL;
      }
    }

    LIB_gset_free(cache_file->handle_readers, NULL);
    cache_file->handle_readers = NULL;
  }
  LIB_spin_unlock(&spin);

  /* Free handle. */
  if (cache_file->handle) {

    switch (cache_file->type) {
      case CACHEFILE_TYPE_ALEMBIC:
#  ifdef WITH_ALEMBIC
        ABC_free_handle(cache_file->handle);
#  endif
        break;
      case CACHEFILE_TYPE_USD:
#  ifdef WITH_USD
        USD_free_handle(cache_file->handle);
#  endif
        break;
      case CACHE_FILE_TYPE_INVALID:
        break;
    }

    cache_file->handle = NULL;
  }

  cache_file->handle_filepath[0] = '\0';
#else
  UNUSED_VARS(cache_file);
#endif
}

void *KERNEL_cachefile_add(Main *dunemain, const char *name)
{
  CacheFile *cache_file = KERNEL_id_new(dunemain, ID_CF, name);

  return cache_file;
}

void KERNEL_cachefile_reload(Depsgraph *depsgraph, CacheFile *cache_file)
{
  /* To force reload, free the handle and tag depsgraph to load it again. */
  CacheFile *cache_file_eval = (CacheFile *)DEG_get_evaluated_id(depsgraph, &cache_file->id);
  if (cache_file_eval) {
    cachefile_handle_free(cache_file_eval);
  }

  DEG_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
}

void KERNEL_cachefile_eval(Main *dunemain, Depsgraph *depsgraph, CacheFile *cache_file)
{
  LIB_assert(cache_file->id.tag & LIB_TAG_COPIED_ON_WRITE);

  /* Compute filepath. */
  char filepath[FILE_MAX];
  if (!KERNEL_cachefile_filepath_get(dunemain, depsgraph, cache_file, filepath)) {
    return;
  }

  /* Test if filepath change or if we can keep the existing handle. */
  if (STREQ(cache_file->handle_filepath, filepath)) {
    return;
  }

  cachefile_handle_free(cache_file);
  LIB_freelistN(&cache_file->object_paths);

#ifdef WITH_ALEMBIC
  if (LIB_path_extension_check_glob(filepath, "*abc")) {
    cache_file->type = CACHEFILE_TYPE_ALEMBIC;
    cache_file->handle = ABC_create_handle(
        dunemain, filepath, cache_file->layers.first, &cache_file->object_paths);
    LIB_strncpy(cache_file->handle_filepath, filepath, FILE_MAX);
  }
#endif
#ifdef WITH_USD
  if (LIB_path_extension_check_glob(filepath, "*.usd;*.usda;*.usdc")) {
    cache_file->type = CACHEFILE_TYPE_USD;
    cache_file->handle = USD_create_handle(dunemain, filepath, &cache_file->object_paths);
    LIB_strncpy(cache_file->handle_filepath, filepath, FILE_MAX);
  }
#endif

  if (DEG_is_active(depsgraph)) {
    /* Flush object paths back to original data-block for UI. */
    CacheFile *cache_file_orig = (CacheFile *)DEG_get_original_id(&cache_file->id);
    LIB_freelistN(&cache_file_orig->object_paths);
    LIB_duplicatelist(&cache_file_orig->object_paths, &cache_file->object_paths);
  }
}

bool KERNEL_cachefile_filepath_get(const Main *dunemain,
                                const Depsgraph *depsgraph,
                                const CacheFile *cache_file,
                                char r_filepath[FILE_MAX])
{
  LIB_strncpy(r_filepath, cache_file->filepath, FILE_MAX);
  LIB_path_abs(r_filepath, ID_DUNE_PATH(dunemain, &cache_file->id));

  int fframe;
  int frame_len;

  if (cache_file->is_sequence && LIB_path_frame_get(r_filepath, &fframe, &frame_len)) {
    Scene *scene = DEG_get_evaluated_scene(depsgraph);
    const float ctime = KERNEL_scene_ctime_get(scene);
    const float fps = (((double)scene->r.frs_sec) / (double)scene->r.frs_sec_base);
    const float frame = KERNEL_cachefile_time_offset(cache_file, ctime, fps);

    char ext[32];
    LIB_path_frame_strip(r_filepath, ext);
    LIB_path_frame(r_filepath, frame, frame_len);
    LIB_path_extension_ensure(r_filepath, FILE_MAX, ext);

    /* TODO(kevin): store sequence range? */
    return LIB_exists(r_filepath);
  }

  return true;
}

float KERNEL_cachefile_time_offset(const CacheFile *cache_file, const float time, const float fps)
{
  const float time_offset = cache_file->frame_offset / fps;
  const float frame = (cache_file->override_frame ? cache_file->frame : time);
  return cache_file->is_sequence ? frame : frame / fps - time_offset;
}

bool KERNEL_cache_file_uses_render_procedural(const CacheFile *cache_file,
                                           Scene *scene,
                                           const int dag_eval_mode)
{
  RenderEngineType *render_engine_type = RE_engines_find(scene->r.engine);

  if (cache_file->type != CACHEFILE_TYPE_ALEMBIC ||
      !RE_engine_supports_alembic_procedural(render_engine_type, scene)) {
    return false;
  }

  /* The render time procedural is only enabled during viewport rendering. */
  const bool is_final_render = (eEvaluationMode)dag_eval_mode == DAG_EVAL_RENDER;
  return cache_file->use_render_procedural && !is_final_render;
}

CacheFileLayer *KERNEL_cachefile_add_layer(CacheFile *cache_file, const char filename[1024])
{
  for (CacheFileLayer *layer = cache_file->layers.first; layer; layer = layer->next) {
    if (STREQ(layer->filepath, filename)) {
      return NULL;
    }
  }

  const int num_layers = LIB_listbase_count(&cache_file->layers);

  CacheFileLayer *layer = MEM_callocN(sizeof(CacheFileLayer), "CacheFileLayer");
  LIB_strncpy(layer->filepath, filename, sizeof(layer->filepath));

  LIB_addtail(&cache_file->layers, layer);

  cache_file->active_layer = (char)(num_layers + 1);

  return layer;
}

CacheFileLayer *KERNEL_cachefile_get_active_layer(CacheFile *cache_file)
{
  return LIB_findlink(&cache_file->layers, cache_file->active_layer - 1);
}

void KERNEL_cachefile_remove_layer(CacheFile *cache_file, CacheFileLayer *layer)
{
  cache_file->active_layer = 0;
  LIB_remlink(&cache_file->layers, layer);
  MEM_freeN(layer);
}
