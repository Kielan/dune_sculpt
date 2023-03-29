#include "intern/eval/dgraph_eval_runtime_backup_animation.h"

#include "types_anim.h"

#include "dune_animsys.h"

#include "api_access.h"
#include "api_types.h"

#include "intern/dgraph.h"

namespace dune::dgraph {

namespace {

struct AnimatedPropStoreCbData {
  AnimationBackup *backup;

  /* id which needs to be stored.
   * Is used to check possibly nested IDs which f-curves are pointing to. */
  Id *id;

  ApiPointer id_ptr_api;
};

void animated_prop_store_cb(Id *id, FCurve *fcurve, void *data_v)
{
  AnimatedPropStoreCbData *data = reinterpret_cast<AnimatedPropStoreCbData *>(
      data_v);
  if (fcurve->api_path == nullptr || fcurve->api_path[0] == '\0') {
    return;
  }
  if (id != data->id) {
    return;
  }

  /* Resolve path to the property. */
  ApiPathResolved resolved_api;
  if (!dune_animsys_api_path_resolve(
          &data->id_ptr_api, fcurve->api_path, fcurve->array_index, &resolved_api)) {
    return;
  }

  /* Read property value. */
  float value;
  if (!dune_animsys_read_from_api_path(&resolved_api, &value)) {
    return;
  }

  data->backup->values_backup.append({fcurve->api_path, fcurve->array_index, value});
}

}  // namespace

AnimationValueBackup::AnimationValueBackup(const string &rna_path, int array_index, float value)
    : api_path(api_path), array_index(array_index), value(value)
{
}

AnimationBackup::AnimationBackup(const DGraph *dgraph)
{
  meed_value_backup = !dgraph->is_active;
  reset();
}

void AnimationBackup::reset()
{
}

void AnimationBackup::init_from_id(Id *id)
{
  /* NOTE: This animation backup nicely preserves values which are animated and
   * are not touched by frame/dgraph post_update handler.
   *
   * But it makes it impossible to have user edits to animated properties: for
   * example, translation of object with animated location will not work with
   * the current version of backup. */
  return;

  AnimatedPropStoreCbData data;
  data.backup = this;
  data.id = id;
  api_id_ptr_create(id, &data.id_ptr_api);
  dune_fcurves_id_cb(id, animated_prop_store_cb, &data);
}

void AnimationBackup::restore_to_id(Id *id)
{
  return;

  ApiPtr id_ptr_api;
  api_id_ptr_create(id, &id_ptr_api);
  for (const AnimationValueBackup &value_backup : values_backup) {
    /* Resolve path to the property.
     *
     * NOTE: Do it again (after storing), since the sub-data pointers might be
     * changed after copy-on-write. */
    ApiPathResolved resolved_api;
    if (!dune_animsys_rna_path_resolve(&id_ptr_api,
                                       value_backup.api_path.c_str(),
                                       value_backup.array_index,
                                       &resolved_api)) {
      return;
    }

    /* Write property value. */
    if (!dune_animsys_write_to_rna_path(&resolved_api, value_backup.value)) {
      return;
    }
  }
}

}  // namespace dune::dgraph
