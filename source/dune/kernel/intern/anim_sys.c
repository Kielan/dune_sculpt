#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_alloca.h"
#include "BLI_blenlib.h"
#include "BLI_dynstr.h"
#include "BLI_listbase.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_anim_types.h"
#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_texture_types.h"
#include "DNA_world_types.h"

#include "BKE_action.h"
#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_context.h"
#include "BKE_fcurve.h"
#include "BKE_global.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_nla.h"
#include "BKE_node.h"
#include "BKE_report.h"
#include "BKE_texture.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "RNA_access.h"
#include "RNA_prototypes.h"

#include "BLO_read_write.h"

#include "nla_private.h"

#include "atomic_ops.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"bke.anim_sys"};

/* *********************************** */
/* KeyingSet API */

/* Finding Tools --------------------------- */

KS_Path *BKE_keyingset_find_path(KeyingSet *ks,
                                 ID *id,
                                 const char group_name[],
                                 const char rna_path[],
                                 int array_index,
                                 int UNUSED(group_mode))
{
  KS_Path *ksp;

  /* sanity checks */
  if (ELEM(NULL, ks, rna_path, id)) {
    return NULL;
  }

  /* loop over paths in the current KeyingSet, finding the first one where all settings match
   * (i.e. the first one where none of the checks fail and equal 0)
   */
  for (ksp = ks->paths.first; ksp; ksp = ksp->next) {
    short eq_id = 1, eq_path = 1, eq_index = 1, eq_group = 1;

    /* id */
    if (id != ksp->id) {
      eq_id = 0;
    }

    /* path */
    if ((ksp->rna_path == NULL) || !STREQ(rna_path, ksp->rna_path)) {
      eq_path = 0;
    }

    /* index - need to compare whole-array setting too... */
    if (ksp->array_index != array_index) {
      eq_index = 0;
    }

    /* group */
    if (group_name) {
      /* FIXME: these checks need to be coded... for now, it's not too important though */
    }

    /* if all aspects are ok, return */
    if (eq_id && eq_path && eq_index && eq_group) {
      return ksp;
    }
  }

  /* none found */
  return NULL;
}

/* Defining Tools --------------------------- */

KeyingSet *BKE_keyingset_add(
    ListBase *list, const char idname[], const char name[], short flag, short keyingflag)
{
  KeyingSet *ks;

  /* allocate new KeyingSet */
  ks = MEM_callocN(sizeof(KeyingSet), "KeyingSet");

  BLI_strncpy(ks->idname,
              (idname) ? idname :
              (name)   ? name :
                         DATA_("KeyingSet"),
              sizeof(ks->idname));
  BLI_strncpy(ks->name, (name) ? name : (idname) ? idname : DATA_("Keying Set"), sizeof(ks->name));

  ks->flag = flag;
  ks->keyingflag = keyingflag;
  /* NOTE: assume that if one is set one way, the other should be too, so that it'll work */
  ks->keyingoverride = keyingflag;

  /* add KeyingSet to list */
  BLI_addtail(list, ks);

  /* Make sure KeyingSet has a unique idname */
  BLI_uniquename(
      list, ks, DATA_("KeyingSet"), '.', offsetof(KeyingSet, idname), sizeof(ks->idname));

  /* Make sure KeyingSet has a unique label (this helps with identification) */
  BLI_uniquename(list, ks, DATA_("Keying Set"), '.', offsetof(KeyingSet, name), sizeof(ks->name));

  /* return new KeyingSet for further editing */
  return ks;
}

KS_Path *BKE_keyingset_add_path(KeyingSet *ks,
                                ID *id,
                                const char group_name[],
                                const char rna_path[],
                                int array_index,
                                short flag,
                                short groupmode)
{
  KS_Path *ksp;

  /* sanity checks */
  if (ELEM(NULL, ks, rna_path)) {
    CLOG_ERROR(&LOG, "no Keying Set and/or RNA Path to add path with");
    return NULL;
  }

  /* ID is required for all types of KeyingSets */
  if (id == NULL) {
    CLOG_ERROR(&LOG, "No ID provided for Keying Set Path");
    return NULL;
  }

  /* don't add if there is already a matching KS_Path in the KeyingSet */
  if (BKE_keyingset_find_path(ks, id, group_name, rna_path, array_index, groupmode)) {
    if (G.debug & G_DEBUG) {
      CLOG_ERROR(&LOG, "destination already exists in Keying Set");
    }
    return NULL;
  }

  /* allocate a new KeyingSet Path */
  ksp = MEM_callocN(sizeof(KS_Path), "KeyingSet Path");

  /* just store absolute info */
  ksp->id = id;
  if (group_name) {
    BLI_strncpy(ksp->group, group_name, sizeof(ksp->group));
  }
  else {
    ksp->group[0] = '\0';
  }

  /* store additional info for relative paths (just in case user makes the set relative) */
  if (id) {
    ksp->idtype = GS(id->name);
  }

  /* just copy path info */
  /* TODO: should array index be checked too? */
  ksp->rna_path = BLI_strdup(rna_path);
  ksp->array_index = array_index;

  /* store flags */
  ksp->flag = flag;
  ksp->groupmode = groupmode;

  /* add KeyingSet path to KeyingSet */
  BLI_addtail(&ks->paths, ksp);

  /* return this path */
  return ksp;
}

void BKE_keyingset_free_path(KeyingSet *ks, KS_Path *ksp)
{
  /* sanity check */
  if (ELEM(NULL, ks, ksp)) {
    return;
  }

  /* free RNA-path info */
  if (ksp->rna_path) {
    MEM_freeN(ksp->rna_path);
  }

  /* free path itself */
  BLI_freelinkN(&ks->paths, ksp);
}
