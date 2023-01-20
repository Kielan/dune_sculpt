#include <iostream>

#include "MEM_guardedalloc.h"

#include "structs_ID.h"
#include "structs_defaults.h"
#include "structs_scene_types.h"
#include "structs_simulation_types.h"

#include "LI_compiler_compat.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_math_vec_types.hh"
#include "LI_rand.h"
#include "LI_span.hh"
#include "LI_string.h"
#include "LI_utildefines.h"

#include "KE_anim_data.h"
#include "KE_animsys.h"
#include "KE_customdata.h"
#include "KE_idtype.h"
#include "KE_lib_id.h"
#include "KE_lib_query.h"
#include "KE_lib_remap.h"
#include "KE_main.h"
#include "KE_node.h"
#include "KE_pointcache.h"
#include "KE_simulation.h"

#include "NOD_geometry.h"

#include "LIB_map.hh"
#include "TRANSLATION_translation.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "LOADER_read_write.h"

static void simulation_init_data(ID *id)
{
  Simulation *simulation = (Simulation *)id;
  LIB_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(simulation, id));

  MEMCPY_STRUCT_AFTER(simulation, structs_struct_default_get(Simulation), id);

  bNodeTree *ntree = ntreeAddTree(nullptr, "Geometry Nodetree", ntreeType_Geometry->idname);
  simulation->nodetree = ntree;
}

static void simulation_copy_data(Main *dunemain, ID *id_dst, const ID *id_src, const int flag)
{
  Simulation *simulation_dst = (Simulation *)id_dst;
  const Simulation *simulation_src = (const Simulation *)id_src;

  /* We always need allocation of our private ID data. */
  const int flag_private_id_data = flag & ~LIB_ID_CREATE_NO_ALLOCATE;

  if (simulation_src->nodetree) {
    KERNEL_id_copy_ex(dunemain,
                   (ID *)simulation_src->nodetree,
                   (ID **)&simulation_dst->nodetree,
                   flag_private_id_data);
  }
}

static void simulation_free_data(ID *id)
{
  Simulation *simulation = (Simulation *)id;

  KERNEL_animdata_free(&simulation->id, false);

  if (simulation->nodetree) {
    ntreeFreeEmbeddedTree(simulation->nodetree);
    MEM_freeN(simulation->nodetree);
    simulation->nodetree = nullptr;
  }
}

static void simulation_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Simulation *simulation = (Simulation *)id;
  if (simulation->nodetree) {
    /* nodetree **are owned by IDs**, treat them as mere sub-data and not real ID! */
    KERNEL_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, KERNEL_library_foreach_ID_embedded(data, (ID **)&simulation->nodetree));
  }
}

static void simulation_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  Simulation *simulation = (Simulation *)id;

  LOADER_write_id_struct(writer, Simulation, id_address, &simulation->id);
  KERNEL_id_dune_write(writer, &simulation->id);

  if (simulation->adt) {
    KERNEL_animdata_dune_write(writer, simulation->adt);
  }

  /* nodetree is integral part of simulation, no libdata */
  if (simulation->nodetree) {
    LOADER_write_struct(writer, bNodeTree, simulation->nodetree);
    ntreeDuneWrite(writer, simulation->nodetree);
  }
}

static void simulation_dune_read_data(DuneDataReader *reader, ID *id)
{
  Simulation *simulation = (Simulation *)id;
  LOADER_read_data_address(reader, &simulation->adt);
  KERNEL_animdata_dune_read_data(reader, simulation->adt);
}

static void simulation_dune_read_lib(DuneLibReader *reader, ID *id)
{
  Simulation *simulation = (Simulation *)id;
  UNUSED_VARS(simulation, reader);
}

static void simulation_dune_read_expand(DuneExpander *expander, ID *id)
{
  Simulation *simulation = (Simulation *)id;
  UNUSED_VARS(simulation, expander);
}

IDTypeInfo IDType_ID_SIM = {
    /* id_code */ ID_SIM,
    /* id_filter */ FILTER_ID_SIM,
    /* main_listbase_index */ INDEX_ID_SIM,
    /* struct_size */ sizeof(Simulation),
    /* name */ "Simulation",
    /* name_plural */ "simulations",
    /* translation_context */ BLT_I18NCONTEXT_ID_SIMULATION,
    /* flags */ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /* asset_type_info */ nullptr,

    /* init_data */ simulation_init_data,
    /* copy_data */ simulation_copy_data,
    /* free_data */ simulation_free_data,
    /* make_local */ nullptr,
    /* foreach_id */ simulation_foreach_id,
    /* foreach_cache */ nullptr,
    /* foreach_path */ nullptr,
    /* owner_get */ nullptr,

    /* dune_write */ simulation_dune_write,
    /* dune_read_data */ simulation_dune_read_data,
    /* dune_read_lib */ simulation_dune_read_lib,
    /* dune_read_expand */ simulation_dune_read_expand,

    /* dune_read_undo_preserve */ nullptr,

    /* lib_override_apply_post */ nullptr,
};

void *KERNEL_simulation_add(Main *dunemain, const char *name)
{
  Simulation *simulation = (Simulation *)KERNEL_id_new(dunemain, ID_SIM, name);
  return simulation;
}

void KERNEL_simulation_data_update(Depsgraph *UNUSED(depsgraph),
                                Scene *UNUSED(scene),
                                Simulation *UNUSED(simulation))
{
}
