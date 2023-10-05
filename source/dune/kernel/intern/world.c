#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

/* Allow using deprecated functionality for .dune file I/O. */
#define TYPES_DEPRECATED_ALLOW

#include "types_defaults.h"
#include "types_scene.h"
#include "types_texture.h"
#include "types_world.h"

#include "lib_list.h"
#include "lib_utildefines.h"

#include "dune_anim_data.h"
#include "dune_icons.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_main.h"
#include "dune_node.h"
#include "dune_world.h"

#include "lang.h"

#include "draw_engine.h"

#include "graph.h"

#include "gpu_material.h"

#include "loader_read_write.h"

/* Free (or release) any data used by this world (does not free the world itself). */
static void world_free_data(Id *id)
{
  World *wrld = (World *)id;

  drawdata_free(id);

  /* is no lib link block, but world extension */
  if (wrld->nodetree) {
    ntreeFreeEmbeddedTree(wrld->nodetree);
    mem_freen(wrld->nodetree);
    wrld->nodetree = NULL;
  }

  gpy_material_free(&wrld->gpumaterial);

  dune_icon_id_delete((struct ID *)wrld);
  dune_previewimg_free(&wrld->preview);
}

static void world_init_data(Id *id)
{
  World *wrld = (World *)id;
  LIB_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(wrld, id));

  MEMCPY_STRUCT_AFTER(wrld, DNA_struct_default_get(World), id);
}

/**
 * Only copy internal data of World ID from source
 * to already allocated/initialized destination.
 * You probably never want to use that directly,
 * use KERNEL_id_copy or KERNEL_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * param flag: Copying options (see KERNEL_lib_id.h's LIB_ID_COPY_... flags for more).
 */
static void world_copy_data(Main *dunemain, ID *id_dst, const ID *id_src, const int flag)
{
  World *wrld_dst = (World *)id_dst;
  const World *wrld_src = (const World *)id_src;

  const bool is_localized = (flag & LIB_ID_CREATE_LOCAL) != 0;
  /* We always need allocation of our private ID data. */
  const int flag_private_id_data = flag & ~LIB_ID_CREATE_NO_ALLOCATE;

  if (wrld_src->nodetree) {
    if (is_localized) {
      wrld_dst->nodetree = ntreeLocalize(wrld_src->nodetree);
    }
    else {
      KERNEL_id_copy_ex(
          dunemain, (ID *)wrld_src->nodetree, (ID **)&wrld_dst->nodetree, flag_private_id_data);
    }
  }

  LIB_listbase_clear(&wrld_dst->gpumaterial);
  LIB_listbase_clear((ListBase *)&wrld_dst->drawdata);

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    KERNEL_previewimg_id_copy(&wrld_dst->id, &wrld_src->id);
  }
  else {
    wrld_dst->preview = NULL;
  }
}

static void world_foreach_id(ID *id, LibraryForeachIDData *data)
{
  World *world = (World *)id;

  if (world->nodetree) {
    /* nodetree **are owned by IDs**, treat them as mere sub-data and not real ID! */
    KERNEL_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, KERNEL_library_foreach_ID_embedded(data, (ID **)&world->nodetree));
  }
}

static void world_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  World *wrld = (World *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  LIB_listbase_clear(&wrld->gpumaterial);

  /* write LibData */
  LOADER_write_id_struct(writer, World, id_address, &wrld->id);
  KERNEL_id_dune_write(writer, &wrld->id);

  if (wrld->adt) {
    KERNEL_animdata_dune_write(writer, wrld->adt);
  }

  /* nodetree is integral part of world, no libdata */
  if (wrld->nodetree) {
    LOADER_write_struct(writer, bNodeTree, wrld->nodetree);
    ntreeDuneWrite(writer, wrld->nodetree);
  }

  KERNEL_previewimg_blend_write(writer, wrld->preview);
}

static void world_dune_read_data(DuneDataReader *reader, ID *id)
{
  World *wrld = (World *)id;
  LOADER_read_data_address(reader, &wrld->adt);
  KERNEL_animdata_dune_read_data(reader, wrld->adt);

  LOADER_read_data_address(reader, &wrld->preview);
  KERNEL_previewimg_dune_read(reader, wrld->preview);
  LIB_listbase_clear(&wrld->gpumaterial);
}

static void world_dune_read_lib(DuneLibReader *reader, ID *id)
{
  World *wrld = (World *)id;
  LOADER_read_id_address(reader, wrld->id.lib, &wrld->ipo); /* XXX deprecated, old animation system */
}

static void world_dune_read_expand(DuneExpander *expander, ID *id)
{
  World *wrld = (World *)id;
  LOADER_expand(expander, wrld->ipo); /* XXX deprecated, old animation system */
}

IDTypeInfo IDType_ID_WO = {
    .id_code = ID_WO,
    .id_filter = FILTER_ID_WO,
    .main_listbase_index = INDEX_ID_WO,
    .struct_size = sizeof(World),
    .name = "World",
    .name_plural = "worlds",
    .translation_context = BLT_I18NCONTEXT_ID_WORLD,
    .flags = IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = world_init_data,
    .copy_data = world_copy_data,
    .free_data = world_free_data,
    .make_local = NULL,
    .foreach_id = world_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .dune_write = world_blend_write,
    .dune_read_data = world_blend_read_data,
    .dune_read_lib = world_blend_read_lib,
    .dune_read_expand = world_blend_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

World *KERNEL_world_add(Main *dunemain, const char *name)
{
  World *wrld;

  wrld = KERNEL_id_new(dunemain, ID_WO, name);

  return wrld;
}

void KERNEL_world_eval(struct Depsgraph *depsgraph, World *world)
{
  DEG_debug_print_eval(depsgraph, __func__, world->id.name, world);
  GPU_material_free(&world->gpumaterial);
}
