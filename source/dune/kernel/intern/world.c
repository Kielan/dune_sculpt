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

  icon_id_delete((struct Id *)wrld);
  previewimg_free(&wrld->preview);
}

static void world_init_data(Id *id)
{
  World *wrld = (World *)id;
  lib_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(wrld, id));

  MEMCPY_STRUCT_AFTER(wrld, types_struct_default_get(World), id);
}

/* Only copy internal data of World Id from source
 * to already allocated/initialized destination.
 * You probably never want to use that directly,
 * use dune_id_copy or id_copy_ex for typical needs.
 *
 * WARNING! This fn will not handle Id user count!
 *
 * param flag: Copying options (see lib_id.h's LIB_ID_COPY_... flags for more). */
static void world_copy_data(Main *main, Id *id_dst, const Id *id_src, const int flag)
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
      id_copy_ex(
          dune, (Id *)wrld_src->nodetree, (Id **)&wrld_dst->nodetree, flag_private_id_data);
    }
  }

  lib_list_clear(&wrld_dst->gpumaterial);
  lib_list_clear((List *)&wrld_dst->drawdata);

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    dune_previewimg_id_copy(&wrld_dst->id, &wrld_src->id);
  }
  else {
    wrld_dst->preview = NULL;
  }
}

static void world_foreach_id(Id *id, LibForeachIdData *data)
{
  World *world = (World *)id;

  if (world->nodetree) {
    /* nodetree **are owned by Ids**, treat them as mere sub-data and not real ID! */
    FOREACHID_PROCESS_FN_CALL(
        data, foreach_id_embedded(data, (Id **)&world->nodetree));
  }
}

static void world_write(Writer *writer, Id *id, const void *id_address)
{
  World *wrld = (World *)id;

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  lib_list_clear(&wrld->gpumaterial);

  /* write LibData */
  loader_write_id_struct(writer, World, id_address, &wrld->id);
  id_write(writer, &wrld->id);

  if (wrld->adt) {
    animdata_write(writer, wrld->adt);
  }

  /* nodetree is integral part of world, no libdata */
  if (wrld->nodetree) {
    write_struct(writer, NodeTree, wrld->nodetree);
    ntreeWrite(writer, wrld->nodetree);
  }

  previewimg_write(writer, wrld->preview);
}

static void world_read_data(DataReader *reader, Id *id)
{
  World *wrld = (World *)id;
  loader_read_data_address(reader, &wrld->adt);
  animdata_read_data(reader, wrld->adt);

  loader_read_data_address(reader, &wrld->preview);
  previewimg_read(reader, wrld->preview);
  lib_list_clear(&wrld->gpumaterial);
}

static void world_read_lib(LibReader *reader, Id *id)
{
  World *wrld = (World *)id;
  loader_read_id_address(reader, wrld->id.lib, &wrld->ipo); /* deprecated, old animation system */
}

static void world_read_expand(Expander *expander, Id *id)
{
  World *wrld = (World *)id;
  loader_expand(expander, wrld->ipo); /* deprecated, old animation system */
}

IdTypeInfo IdTypeWrld = {
    .id_code = IdWrld,
    .id_filter = FILTER_ID_WO,
    .main_list_index = INDEX_ID_WO,
    .struct_size = sizeof(World),
    .name = "World",
    .name_plural = "worlds",
    .lang_cxt = LANG_CXT_ID_WORLD,
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

    .dune_write = world_write,
    .dune_read_data = world_read_data,
    .dune_read_lib = world_read_lib,
    .dune_read_expand = world_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

World *world_add(Main *main, const char *name)
{
  World *wrld;

  wrld = id_new(main, IdWrld, name);

  return wrld;
}

void world_eval(struct Graph *graph, World *world)
{
  graph_debug_print_eval(graph, __func__, world->id.name, world);
  gpu_material_free(&world->gpumaterial);
}
