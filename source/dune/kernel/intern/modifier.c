/**
 * Modifier stack implementation.
 * KERNEL_modifier.h contains the function prototypes for this file.
 */

/* Allow using deprecated functionality for .blend file I/O. */
#define STRUCTS_DEPRECATED_ALLOW

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "STRUCTS_armature_types.h"
#include "STRUCTS_cloth_types.h"
#include "STRUCTS_dynamicpaint_types.h"
#include "STRUCTS_fluid_types.h"
#include "STRUCTS_gpencil_modifier_types.h"
#include "STRUCTS_mesh_types.h"
#include "STRUCTS_object_fluidsim_types.h"
#include "STRUCTS_object_force_types.h"
#include "STRUCTS_object_types.h"
#include "STRUCTS_scene_types.h"
#include "STRUCTS_screen_types.h"

#include "LI_linklist.h"
#include "LI_listbase.h"
#include "LI_path_util.h"
#include "LI_session_uuid.h"
#include "LI_string.h"
#include "LI_string_utils.h"
#include "LI_utildefines.h"

#include "TRANSLATION_translation.h"

#include "KE_DerivedMesh.h"
#include "KE_appdir.h"
#include "KE_editmesh.h"
#include "KE_editmesh_cache.h"
#include "KE_effect.h"
#include "KE_fluid.h"
#include "KE_global.h"
#include "KE_gpencil_modifier.h"
#include "KE_idtype.h"
#include "KE_key.h"
#include "KE_lib_id.h"
#include "KE_lib_query.h"
#include "KE_mesh.h"
#include "KE_mesh_wrapper.h"
#include "KE_multires.h"
#include "KE_object.h"
#include "KE_pointcache.h"

/* may move these, only for KE_modifier_path_relbase */
#include "KERNEL_main.h"
/* end */

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "MOD_modifiertypes.h"

#include "LOADER_read_write.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"bke.modifier"};
static ModifierTypeInfo *modifier_types[NUM_MODIFIER_TYPES] = {NULL};
static VirtualModifierData virtualModifierCommonData;

void KERNEL_modifier_init(void)
{
  ModifierData *md;

  /* Initialize modifier types */
  modifier_type_init(modifier_types); /* MOD_utils.c */

  /* Initialize global common storage used for virtual modifier list. */
  md = KERNEL_modifier_new(eModifierType_Armature);
  virtualModifierCommonData.amd = *((ArmatureModifierData *)md);
  KERNEL_modifier_free(md);

  md = KERNEL_modifier_new(eModifierType_Curve);
  virtualModifierCommonData.cmd = *((CurveModifierData *)md);
  KERNEL_modifier_free(md);

  md = KERNEL_modifier_new(eModifierType_Lattice);
  virtualModifierCommonData.lmd = *((LatticeModifierData *)md);
  KERNEL_modifier_free(md);

  md = KERNEL_modifier_new(eModifierType_ShapeKey);
  virtualModifierCommonData.smd = *((ShapeKeyModifierData *)md);
  KERNEL_modifier_free(md);

  virtualModifierCommonData.amd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.cmd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.lmd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.smd.modifier.mode |= eModifierMode_Virtual;
}

const ModifierTypeInfo *KERNEL_modifier_get_info(ModifierType type)
{
  /* type unsigned, no need to check < 0 */
  if (type < NUM_MODIFIER_TYPES && modifier_types[type] && modifier_types[type]->name[0] != '\0') {
    return modifier_types[type];
  }

  return NULL;
}

void KERNEL_modifier_type_panel_id(ModifierType type, char *r_idname)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(type);

  strcpy(r_idname, MODIFIER_TYPE_PANEL_PREFIX);
  strcat(r_idname, mti->name);
}

void KERNEL_modifier_panel_expand(ModifierData *md)
{
  md->ui_expand_flag |= UI_PANEL_DATA_EXPAND_ROOT;
}

/***/

static ModifierData *modifier_allocate_and_init(int type)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(type);
  ModifierData *md = MEM_callocN(mti->structSize, mti->structName);

  /* NOTE: this name must be made unique later. */
  LIB_strncpy(md->name, DATA_(mti->name), sizeof(md->name));

  md->type = type;
  md->mode = eModifierMode_Realtime | eModifierMode_Render;
  md->flag = eModifierFlag_OverrideLibrary_Local;
  md->ui_expand_flag = 1; /* Only open the main panel at the beginning, not the sub-panels. */

  if (mti->flags & eModifierTypeFlag_EnableInEditmode) {
    md->mode |= eModifierMode_Editmode;
  }

  if (mti->initData) {
    mti->initData(md);
  }

  return md;
}

ModifierData *KERNEL_modifier_new(int type)
{
  ModifierData *md = modifier_allocate_and_init(type);

  KERNEL_modifier_session_uuid_generate(md);

  return md;
}

static void modifier_free_data_id_us_cb(void *UNUSED(userData),
                                        Object *UNUSED(ob),
                                        ID **idpoin,
                                        int cb_flag)
{
  ID *id = *idpoin;
  if (id != NULL && (cb_flag & IDWALK_CB_USER) != 0) {
    id_us_min(id);
  }
}

void KERNEL_modifier_free_ex(ModifierData *md, const int flag)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(md->type);

  if ((flag & LIB_ID_CREATE_NO_USER_REFCOUNT) == 0) {
    if (mti->foreachIDLink) {
      mti->foreachIDLink(md, NULL, modifier_free_data_id_us_cb, NULL);
    }
  }

  if (mti->freeData) {
    mti->freeData(md);
  }
  if (md->error) {
    MEM_freeN(md->error);
  }

  MEM_freeN(md);
}

void KERNEL_modifier_free(ModifierData *md)
{
  KERNEL_modifier_free_ex(md, 0);
}

void KERNEL_modifier_remove_from_list(Object *ob, ModifierData *md)
{
  LIB_assert(LIB_findindex(&ob->modifiers, md) != -1);

  if (md->flag & eModifierFlag_Active) {
    /* Prefer the previous modifier but use the next if this modifier is the first in the list. */
    if (md->next != NULL) {
      KERNEL_object_modifier_set_active(ob, md->next);
    }
    else if (md->prev != NULL) {
      KERNEL_object_modifier_set_active(ob, md->prev);
    }
  }

  LIB_remlink(&ob->modifiers, md);
}

void KERNEL_modifier_session_uuid_generate(ModifierData *md)
{
  md->session_uuid = LIB_session_uuid_generate();
}

bool KERNEL_modifier_unique_name(ListBase *modifiers, ModifierData *md)
{
  if (modifiers && md) {
    const ModifierTypeInfo *mti = KERNEL_modifier_get_info(md->type);

    return LIB_uniquename(
        modifiers, md, DATA_(mti->name), '.', offsetof(ModifierData, name), sizeof(md->name));
  }
  return false;
}

bool KERNEL_modifier_depends_ontime(Scene *scene, ModifierData *md, const int dag_eval_mode)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(md->type);

  return mti->dependsOnTime && mti->dependsOnTime(scene, md, dag_eval_mode);
}

bool KERNEL_modifier_supports_mapping(ModifierData *md)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(md->type);

  return (mti->type == eModifierTypeType_OnlyDeform ||
          (mti->flags & eModifierTypeFlag_SupportsMapping));
}

bool KERNEL_modifier_is_preview(ModifierData *md)
{
  const ModifierTypeInfo *mti = KERNEL_modifier_get_info(md->type);

  /* Constructive modifiers are highly likely to also modify data like vgroups or vcol! */
  if (!((mti->flags & eModifierTypeFlag_UsesPreview) ||
        (mti->type == eModifierTypeType_Constructive))) {
    return false;
  }

  if (md->mode & eModifierMode_Realtime) {
    return true;
  }

  return false;
}

ModifierData *KERNEL_modifiers_findby_type(const Object *ob, ModifierType type)
{
  LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
    if (md->type == type) {
      return md;
    }
  }
  return NULL;
}

ModifierData *KERNEL_modifiers_findby_name(const Object *ob, const char *name)
{
  return LIB_findstring(&(ob->modifiers), name, offsetof(ModifierData, name));
}
