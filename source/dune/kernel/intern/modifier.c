/** \file
 * \ingroup bke
 * Modifier stack implementation.
 * BKE_modifier.h contains the function prototypes for this file.
 */

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_armature_types.h"
#include "DNA_cloth_types.h"
#include "DNA_dynamicpaint_types.h"
#include "DNA_fluid_types.h"
#include "DNA_gpencil_modifier_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_fluidsim_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_session_uuid.h"
#include "BLI_string.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_DerivedMesh.h"
#include "BKE_appdir.h"
#include "BKE_editmesh.h"
#include "BKE_editmesh_cache.h"
#include "BKE_effect.h"
#include "BKE_fluid.h"
#include "BKE_global.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_idtype.h"
#include "BKE_key.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_mesh.h"
#include "BKE_mesh_wrapper.h"
#include "BKE_multires.h"
#include "BKE_object.h"
#include "BKE_pointcache.h"

/* may move these, only for BKE_modifier_path_relbase */
#include "BKE_main.h"
/* end */

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "MOD_modifiertypes.h"

#include "BLO_read_write.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"bke.modifier"};
static ModifierTypeInfo *modifier_types[NUM_MODIFIER_TYPES] = {NULL};
static VirtualModifierData virtualModifierCommonData;

void BKE_modifier_init(void)
{
  ModifierData *md;

  /* Initialize modifier types */
  modifier_type_init(modifier_types); /* MOD_utils.c */

  /* Initialize global common storage used for virtual modifier list. */
  md = BKE_modifier_new(eModifierType_Armature);
  virtualModifierCommonData.amd = *((ArmatureModifierData *)md);
  BKE_modifier_free(md);

  md = BKE_modifier_new(eModifierType_Curve);
  virtualModifierCommonData.cmd = *((CurveModifierData *)md);
  BKE_modifier_free(md);

  md = BKE_modifier_new(eModifierType_Lattice);
  virtualModifierCommonData.lmd = *((LatticeModifierData *)md);
  BKE_modifier_free(md);

  md = BKE_modifier_new(eModifierType_ShapeKey);
  virtualModifierCommonData.smd = *((ShapeKeyModifierData *)md);
  BKE_modifier_free(md);

  virtualModifierCommonData.amd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.cmd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.lmd.modifier.mode |= eModifierMode_Virtual;
  virtualModifierCommonData.smd.modifier.mode |= eModifierMode_Virtual;
}

const ModifierTypeInfo *BKE_modifier_get_info(ModifierType type)
{
  /* type unsigned, no need to check < 0 */
  if (type < NUM_MODIFIER_TYPES && modifier_types[type] && modifier_types[type]->name[0] != '\0') {
    return modifier_types[type];
  }

  return NULL;
}

void BKE_modifier_type_panel_id(ModifierType type, char *r_idname)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(type);

  strcpy(r_idname, MODIFIER_TYPE_PANEL_PREFIX);
  strcat(r_idname, mti->name);
}

void BKE_modifier_panel_expand(ModifierData *md)
{
  md->ui_expand_flag |= UI_PANEL_DATA_EXPAND_ROOT;
}

/***/

static ModifierData *modifier_allocate_and_init(int type)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(type);
  ModifierData *md = MEM_callocN(mti->structSize, mti->structName);

  /* NOTE: this name must be made unique later. */
  BLI_strncpy(md->name, DATA_(mti->name), sizeof(md->name));

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

ModifierData *BKE_modifier_new(int type)
{
  ModifierData *md = modifier_allocate_and_init(type);

  BKE_modifier_session_uuid_generate(md);

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

void BKE_modifier_free_ex(ModifierData *md, const int flag)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(md->type);

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

void BKE_modifier_free(ModifierData *md)
{
  BKE_modifier_free_ex(md, 0);
}

void BKE_modifier_remove_from_list(Object *ob, ModifierData *md)
{
  BLI_assert(BLI_findindex(&ob->modifiers, md) != -1);

  if (md->flag & eModifierFlag_Active) {
    /* Prefer the previous modifier but use the next if this modifier is the first in the list. */
    if (md->next != NULL) {
      BKE_object_modifier_set_active(ob, md->next);
    }
    else if (md->prev != NULL) {
      BKE_object_modifier_set_active(ob, md->prev);
    }
  }

  BLI_remlink(&ob->modifiers, md);
}

void BKE_modifier_session_uuid_generate(ModifierData *md)
{
  md->session_uuid = BLI_session_uuid_generate();
}

bool BKE_modifier_unique_name(ListBase *modifiers, ModifierData *md)
{
  if (modifiers && md) {
    const ModifierTypeInfo *mti = BKE_modifier_get_info(md->type);

    return BLI_uniquename(
        modifiers, md, DATA_(mti->name), '.', offsetof(ModifierData, name), sizeof(md->name));
  }
  return false;
}

bool BKE_modifier_depends_ontime(Scene *scene, ModifierData *md, const int dag_eval_mode)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(md->type);

  return mti->dependsOnTime && mti->dependsOnTime(scene, md, dag_eval_mode);
}

bool BKE_modifier_supports_mapping(ModifierData *md)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(md->type);

  return (mti->type == eModifierTypeType_OnlyDeform ||
          (mti->flags & eModifierTypeFlag_SupportsMapping));
}

bool BKE_modifier_is_preview(ModifierData *md)
{
  const ModifierTypeInfo *mti = BKE_modifier_get_info(md->type);

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

ModifierData *BKE_modifiers_findby_type(const Object *ob, ModifierType type)
{
  LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
    if (md->type == type) {
      return md;
    }
  }
  return NULL;
}

ModifierData *BKE_modifiers_findby_name(const Object *ob, const char *name)
{
  return BLI_findstring(&(ob->modifiers), name, offsetof(ModifierData, name));
}
