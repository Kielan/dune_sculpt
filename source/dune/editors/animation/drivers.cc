#include <cctype>
#include <cstdio>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "types_anim.h"
#include "types_ob.h"
#include "types_texture.h"

#include "dune_anim_data.h"
#include "dune_animsys.h"
#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_fcurve_driver.h"
#include "dune_report.h"

#include "graph.hh"
#include "graph_build.hh"

#include "ed_keyframing.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_path.hh"
#include "api_prototypes.h"

#include "anim_fcurve.hh"

#include "anim_intern.h"

/* Anim Data Validation */
FCurve *verify_driver_fcurve(Id *id,
                             const char api_path[],
                             const int array_index,
                             eDriverFCurveCreationMode creation_mode)
{
  AnimData *adt;
  FCurve *fcu;

  /* sanity checks */
  if (ELEM(nullptr, id, api_path)) {
    return nullptr;
  }

  /* init animdata if none available yet */
  adt = dune_animdata_from_id(id);
  if (adt == nullptr && creation_mode != DRIVER_FCURVE_LOOKUP_ONLY) {
    adt = dune_animdata_ensure_id(id);
  }
  if (adt == nullptr) {
    /* if still none (as not allowed to add, or ID doesn't have animdata for some reason) */
    return nullptr;
  }

  /* try to find f-curve matching for this setting
   * - add if not found and allowed to add one
   * TODO: add auto-grouping support? how this works will need to be resolved */
  fcu = dune_fcurve_find(&adt->drivers, rna_path, array_index);

  if (fcu == nullptr && creation_mode != DRIVER_FCURVE_LOOKUP_ONLY) {
    /* use default settings to make a F-Curve */
    fcu = alloc_driver_fcurve(api_path, array_index, creation_mode);

    /* just add F-Curve to end of driver list */
    lib_addtail(&adt->drivers, fcu);
  }

  /* return the F-Curve */
  return fcu;
}

FCurve *alloc_driver_fcurve(const char api_path[],
                            const int array_index,
                            eDriverFCurveCreationMode creation_mode)
{
  FCurve *fcu = dune_fcurve_create();

  fcu->flag = (FCURVE_VISIBLE | FCURVE_SEL);
  fcu->auto_smoothing = U.auto_smoothing_new;

  /* store path - make copy, and store that */
  if (rna_path) {
    fcu->rna_path = lib_strdup(api_path);
  }
  fcu->array_index = array_index;

  if (!ELEM(creation_mode, DRIVER_FCURVE_LOOKUP_ONLY, DRIVER_FCURVE_EMPTY)) {
    /* add some new driver data */
    fcu->driver = static_cast<ChannelDriver *>(
        mem_calloc(sizeof(ChannelDriver), "ChannelDriver"));

    /* F-Mod or Keyframes? */
    if (creation_mode == DRIVER_FCURVE_GENERATOR) {
      /* Python API Backwards compatibility hack:
       * Create FMod so that old scripts won't break
       * for now before 2.7 series -- (September 4, 2013) */
      add_fmod(&fcu->mods, FMOD_TYPE_GENERATOR, fcu);
    }
    else {
      /* add 2 keyframes so that user has something to work with
       * - These are configured to 0,0 and 1,1 to give a 1-1 mapping
       *   which can be easily tweaked from there. */
      dune::animrig::insert_vert_fcurve(
          fcu, 0.0f, 0.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_FAST | INSERTKEY_NO_USERPREF);
      dune::animrig::insert_vert_fcurve(
          fcu, 1.0f, 1.0f, BEZT_KEYTYPE_KEYFRAME, INSERTKEY_FAST | INSERTKEY_NO_USERPREF);
      fcu->extend = FCURVE_EXTRAPOLATE_LINEAR;
      dune_fcurve_handles_recalc(fcu);
    }
  }

  return fcu;
}

/* Driver Management API */
/* Helper for anim_add_driver_w_target - Adds the actual driver */
static int add_driver_w_target(ReportList * /*reports*/,
                               Id *dst_id,
                               const char dst_path[],
                               int dst_index,
                               Id *src_id,
                               const char src_path[],
                               int src_index,
                               ApiPtr *dst_ptr,
                               ApiProp *dst_prop,
                               ApiPtr *src_ptr,
                               ApiProp *src_prop,
                               short flag,
                               int driver_type)
{
  FCurve *fcu;
  short add_mode = (flag & CREATEDRIVER_W_FMOD) ? DRIVER_FCURVE_GENERATOR :
                                                  DRIVER_FCURVE_KEYFRAMES;
  const char *prop_name = api_prop_id(src_prop);

  /* Create F-Curve with Driver */
  fcu = verify_driver_fcurve(dst_id, dst_path, dst_index, eDriverFCurveCreationMode(add_mode));

  if (fcu && fcu->driver) {
    ChannelDriver *driver = fcu->driver;
    DriverVar *dvar;

    /* Set the type of the driver */
    driver->type = driver_type;

    /* Set driver expression, so that the driver works out of the box
     * The following checks define a bit of "auto-detection magic" we use
     * to ensure that the drivers will behave as expected out of the box
     * when faced with properties with different units. */
    /* If we have N-1 mapping, should we include all those in the expression? */
    if ((api_prop_unit(dst_prop) == PROP_UNIT_ROTATION) &&
        (api_prop_unit(src_prop) != PROP_UNIT_ROTATION))
    {
      /* Rotation Destination: normal -> radians, so convert src to radians
       * (However, if both input and output is a rotation, don't apply such corrections) */
      STRNCPY(driver->expression, "radians(var)");
    }
    else if ((api_prop_unit(src_prop) == PROP_UNIT_ROTATION) &&
             (api_prop_unit(dst_prop) != PROP_UNIT_ROTATION))
    {
      /* Rotation Src: radians -> normal, so convert src to degrees
       * (However, if both input and output is a rotation, don't apply such corrections) */
      STRNCPY(driver->expression, "degrees(var)");
    }
    else {
      /* Just a normal prop wo any unit problems */
      STRNCPY(driver->expression, "var");
    }

    /* Create a driver var for the target
     *   - For transform props, we want to automatically use "transform channel" instead
     *     (The only issue is w quaternion rotations vs euler channels...)
     *   - To avoid problems w transform props depending on the final transform that they
     *     ctrl (thus creating pseudo-cycles - see #48734), we don't use transform channels
     *     when both the src and destinations are in same places */
    dvar = driver_add_new_var(driver);

    if (ELEM(src_ptr->type, &ApiOb, &ApiPoseBone) &&
        (STREQ(prop_name, "location") || STREQ(prop_name, "scale") ||
         STRPREFIX(prop_name, "rotation_")) &&
        (src_ptr->data != dst_ptr->data))
    {
      /* Transform Channel */
      DriverTarget *dtar;

      driver_change_var_type(dvar, DVAR_TYPE_TRANSFORM_CHAN);
      dtar = &dvar->targets[0];

      /* Bone or Ob target? */
      dtar->id = src_id;
      dtar->idtype = GS(src_id->name);

      if (src_ptr->type == &ApiPoseBone) {
        api_string_get(src_ptr, "name", dtar->pchan_name);
      }

      /* Transform channel depends on type */
      if (STREQ(prop_name, "location")) {
        if (src_index == 2) {
          dtar->transChan = DTAR_TRANSCHAN_LOCZ;
        }
        else if (src_index == 1) {
          dtar->transChan = DTAR_TRANSCHAN_LOCY;
        }
        else {
          dtar->transChan = DTAR_TRANSCHAN_LOCX;
        }
      }
      else if (STREQ(prop_name, "scale")) {
        if (src_index == 2) {
          dtar->transChan = DTAR_TRANSCHAN_SCALEZ;
        }
        else if (src_index == 1) {
          dtar->transChan = DTAR_TRANSCHAN_SCALEY;
        }
        else {
          dtar->transChan = DTAR_TRANSCHAN_SCALEX;
        }
      }
      else {
        /* W quaternions and axis-angle, this mapping may not be correct...
         * But bc those have 4 elements instead, there's not much we can do.  */
        if (src_index == 2) {
          dtar->transChan = DTAR_TRANSCHAN_ROTZ;
        }
        else if (src_index == 1) {
          dtar->transChan = DTAR_TRANSCHAN_ROTY;
        }
        else {
          dtar->transChan = DTAR_TRANSCHAN_ROTX;
        }
      }
    }
    else {
      /* Single api Prop */
      DriverTarget *dtar = &dvar->targets[0];

      /* Id is as-is */
      dtar->id = src_id;
      dtar->idtype = GS(src_id->name);

      /* Need to make a copy of the path (or build 1 w ar index built in) */
      if (api_prop_array_check(src_prop)) {
        dtar->api_path = lib_sprintf("%s[%d]", src_path, src_index);
      }
      else {
        dtar->api_path = lib_strdup(src_path);
      }
    }
  }

  /* set the done status */
  return (fcu != nullptr);
}

int anim_add_driver_w_target(ReportList *reports,
                             Id *dst_id,
                             const char dst_path[],
                             int dst_index,
                             Id *src_id,
                             const char src_path[],
                             int src_index,
                             short flag,
                             int driver_type,
                             short mapping_type)
{
  ApiPtr ptr;
  ApiProp *prop;

  ApiPtr ptr2;
  ApiProp *prop2;
  int done_tot = 0;

  /* validate ptrs first - exit if failure */
  ApiPtr id_ptr = api_id_ptr_create(dst_id);
  if (api_path_resolve_prop(&id_ptr, dst_path, &ptr, &prop) == false) {
    dune_reportf(
        reports,
        RPT_ERROR,
        "Could not add driver, as RNA path is invalid for the given ID (ID = %s, path = %s)",
        dst_id->name,
        dst_path);
    return 0;
  }

  ApiPtr id_ptr2 = api_id_ptr_create(src_id);
  if ((api_path_resolve_prop(&id_ptr2, src_path, &ptr2, &prop2) == false) ||
      (mapping_type == CREATEDRIVER_MAPPING_NONE))
  {
    /* No target: fall back to default method for adding a "simple" driver normally */
    return anim_add_driver(
        reports, dst_id, dst_path, dst_index, flag | CREATEDRIVER_WITH_DEFAULT_DVAR, driver_type);
  }

  /* handle curve-prop mappings based on mapping_type */
  switch (mapping_type) {
    /* N-N - Try to match as much as possible, then use the first one. */
    case CREATEDRIVER_MAPPING_N_N: {
      /* Use the shorter of the two (to avoid out of bounds access) */
      int dst_len = api_prop_array_check(prop) ? api_prop_array_length(&ptr, prop) : 1;
      int src_len = api_prop_array_check(prop) ? api_prop_array_length(&ptr2, prop2) : 1;

      int len = std::min(dst_len, src_len);

      for (int i = 0; i < len; i++) {
        done_tot += add_driver_w_target(reports,
                                           dst_id,
                                           dst_path,
                                           i,
                                           src_id,
                                           src_path,
                                           i,
                                           &ptr,
                                           prop,
                                           &ptr2,
                                           prop2,
                                           flag,
                                           driver_type);
      }
      break;
    }
      /* 1-N - Specified target index for all. */
    case CREATEDRIVER_MAPPING_1_N:
    default: {
      int len = api_prop_array_check(prop) ? api_prop_array_length(&ptr, prop) : 1;

      for (int i = 0; i < len; i++) {
        done_tot += add_driver_w_target(reports,
                                           dst_id,
                                           dst_path,
                                           i,
                                           src_id,
                                           src_path,
                                           src_index,
                                           &ptr,
                                           prop,
                                           &ptr2,
                                           prop2,
                                           flag,
                                           driver_type);
      }
      break;
    }

      /* 1-1 - Use the specified index (unless -1). */
    case CREATEDRIVER_MAPPING_1_1: {
      done_tot = add_driver_w_target(reports,
                                     dst_id,
                                     dst_path,
                                     dst_index,
                                     src_id,
                                     src_path,
                                     src_index,
                                     &ptr,
                                     prop,
                                     &ptr2,
                                     prop2,
                                     flag,
                                     driver_type);
      break;
    }
  }

  /* done */
  return done_tot;
}

int anim_add_driver(
    ReportList *reports, Id *id, const char api_path[], int array_index, short flag, int type)
{
  ApiPtr ptr;
  ApiProp *prop;
  FCurve *fcu;
  int array_index_max;
  int done_tot = 0;

  /* validate pointer first - exit if failure */
  ApiPtr id_ptr = api_id_ptr_create(id);
  if (api_path_resolve_prop(&id_ptr, api_path, &ptr, &prop) == false) {
    dune_reportf(
        reports,
        RPT_ERROR,
        "Could not add driver, as api path is invalid for the given Id (Id = %s, path = %s)",
        id->name,
        api_path);
    return 0;
  }

  /* key entire array convenience method */
  if (array_index == -1) {
    array_index_max = api_prop_array_length(&ptr, prop);
    array_index = 0;
  }
  else {
    array_index_max = array_index;
  }

  /* maximum index should be greater than the start index */
  if (array_index == array_index_max) {
    array_index_max += 1;
  }

  /* will only loop once unless the array index was -1 */
  for (; array_index < array_index_max; array_index++) {
    short add_mode = (flag & CREATEDRIVER_WITH_FMOD) ? 2 : 1;

    /* create F-Curve with Driver */
    fcu = verify_driver_fcurve(id, rna_path, array_index, eDriverFCurveCreationMode(add_mode));

    if (fcu && fcu->driver) {
      ChannelDriver *driver = fcu->driver;

      /* set the type of the driver */
      driver->type = type;

      /* Creating drivers for buttons will create the driver(s) with type
       * "scripted expression" so that their values won't be lost immediately,
       * so here we copy those values over to the driver's expression
       *
       * If the "default dvar" option (for easier UI setup of drivers) is provided,
       * include "var" in the expressions too, so that the user doesn't have to edit
       * it to get something to happen. It should be fine to just add it to the default
       * val, so that we get both in the expression, even if it's a bit more confusing
       * that way...  */
      if (type == DRIVER_TYPE_PYTHON) {
        PropType proptype = api_prop_type(prop);
        int array = api_prop_array_length(&ptr, prop);
        const char *dvar_prefix = (flag & CREATEDRIVER_WITH_DEFAULT_DVAR) ? "var + " : "";
        char *expression = driver->expression;
        const size_t expression_maxncpy = sizeof(driver->expression);
        int val;
        float fval;

        if (proptype == PROP_BOOL) {
          if (!array) {
            val = api_prop_bool_get(&ptr, prop);
          }
          else {
            val = api_prop_bool_get_index(&ptr, prop, array_index);
          }

          lib_snprintf(
              expression, expression_maxncpy, "%s%s", dvar_prefix, (val) ? "True" : "False");
        }
        else if (proptype == PROP_INT) {
          if (!array) {
            val = api_prop_int_get(&ptr, prop);
          }
          else {
            val = api_prop_int_get_index(&ptr, prop, array_index);
          }

          lib_snprintf(expression, expression_maxncpy, "%s%d", dvar_prefix, val);
        }
        else if (proptype == PROP_FLOAT) {
          if (!array) {
            fval = api_prop_float_get(&ptr, prop);
          }
          else {
            fval = api_prop_float_get_index(&ptr, prop, array_index);
          }

          lib_snprintf(expression, expression_maxncpy, "%s%.3f", dvar_prefix, fval);
          lib_str_rstrip_float_zero(expression, '\0');
        }
        else if (flag & CREATEDRIVER_WITH_DEFAULT_DVAR) {
          lib_strncpy(expression, "var", expression_maxncpy);
        }
      }

      /* for easier setup of drivers from UI, a driver variable should be
       * added if flag is set (UI calls only) */
      if (flag & CREATEDRIVER_WITH_DEFAULT_DVAR) {
        /* assume that users will mostly want this to be of type "Transform Channel" too,
         * since this allows the easiest setting up of common rig components */
        DriverVar *dvar = driver_add_new_variable(driver);
        driver_change_variable_type(dvar, DVAR_TYPE_TRANSFORM_CHAN);
      }
    }

    /* set the done status */
    done_tot += (fcu != nullptr);
  }

  /* done */
  return done_tot;
}

bool anim_remove_driver(
    ReportList * /*reports*/, ID *id, const char rna_path[], int array_index, short /*flag*/)
{
  AnimData *adt;
  FCurve *fcu;
  bool success = false;

  /* we don't check the validity of the path here yet, but it should be ok... */
  adt = dune_animdata_from_id(id);

  if (adt) {
    if (array_index == -1) {
      /* step through all drivers, removing all of those with the same base path */
      FCurve *fcu_iter = static_cast<FCurve *>(adt->drivers.first);

      while ((fcu = dune_fcurve_iter_step(fcu_iter, rna_path)) != nullptr) {
        /* Store the next fcurve for looping. */
        fcu_iter = fcu->next;

        /* remove F-Curve from driver stack, then free it */
        lib_remlink(&adt->drivers, fcu);
        dune_fcurve_free(fcu);

        /* done successfully */
        success = true;
      }
    }
    else {
      /* Find the matching driver and remove it only
       * Here is one of the places where we don't want new F-Curve + Driver added!
       * so 'add' var must be 0  */
      fcu = verify_driver_fcurve(id, api_path, array_index, DRIVER_FCURVE_LOOKUP_ONLY);
      if (fcu) {
        lib_remlink(&adt->drivers, fcu);
        dune_fcurve_free(fcu);

        success = true;
      }
    }
  }

  return success;
}

/* Driver Management API - Copy/Paste Drivers */
/* Copy/Paste Buffer for Driver Data... */
static FCurve *channeldriver_copypaste_buf = nullptr;

void anim_drivers_copybuf_free()
{
  /* free the buffer F-Curve if it exists, as if it were just another F-Curve */
  if (channeldriver_copypaste_buf) {
    dune_fcurve_free(channeldriver_copypaste_buf);
  }
  channeldriver_copypaste_buf = nullptr;
}

bool anim_driver_can_paste()
{
  return (channeldriver_copypaste_buf != nullptr);
}

bool anim_copy_driver(
    ReportList *reports, Id *id, const char api_path[], int array_index, short /*flag*/)
{
  ApiPtr ptr;
  ApiProp *prop;
  FCurve *fcu;

  /* validate ptr first - exit if failure */
  ApiPtr id_ptr = api_id_ptr_create(id);
  if (api_path_resolve_prop(&id_ptr, api_path, &ptr, &prop) == false) {
    dune_reportf(reports,
                RPT_ERROR,
                "Could not find driver to copy, as api path is invalid for the given ID (ID = %s, "
                "path = %s)",
                id->name,
                api_path);
    return false;
  }

  /* try to get F-Curve with Driver */
  fcu = verify_driver_fcurve(id, api_path, array_index, DRIVER_FCURVE_LOOKUP_ONLY);

  /* clear copy/paste buffer first (for consistency with other copy/paste buffers) */
  anim_drivers_copybuf_free();

  /* copy this to the copy/paste buf if it exists */
  if (fcu && fcu->driver) {
    /* Make copies of some info such as the api_path, then clear this info from the
     * F-Curve tmp so that we don't end up wasting mem storing the path
     * which won't get used ever. */
    char *tmp_path = fcu->rna_path;
    fcu->rna_path = nullptr;

    /* make a copy of the F-Curve with */
    channeldriver_copypaste_buf = dune_fcurve_copy(fcu);

    /* restore the path */
    fcu->api_path = tmp_path;

    /* copied... */
    return true;
  }

  /* done */
  return false;
}

bool anim_paste_driver(
    ReportList *reports, Id *id, const char api_path[], int array_index, short /*flag*/)
{
  ApiPtr ptr;
  ApiProp *prop;
  FCurve *fcu;

  /* validate ptr first - exit if failure */
  ApiPtr id_ptr = api_id_ptr_create(id);
  if (api_path_resolve_prop(&id_ptr, api_path, &ptr, &prop) == false) {
    dune_reportf(
        reports,
        RPT_ERROR,
        "Could not paste driver, as api path is invalid for the given Id (Id = %s, path = %s)",
        id->name,
        api_path);
    return false;
  }

  /* if the buffer is empty, cannot paste... */
  if (channeldriver_copypaste_buf == nullptr) {
    dune_report(reports, RPT_ERROR, "Paste driver: no driver to paste");
    return false;
  }

  /* create Driver F-Curve, but without data which will be copied across... */
  fcu = verify_driver_fcurve(id, rna_path, array_index, DRIVER_FCURVE_EMPTY);

  if (fcu) {
    /* copy across the curve data from the buffer curve
     * This step needs care to not miss new settings */
    /* keyframes/samples */
    fcu->bezt = static_cast<BezTriple *>(mem_dupalloc(channeldriver_copypaste_buf->bezt));
    fcu->fpt = static_cast<FPoint *>(mem_dupalloc(channeldriver_copypaste_buf->fpt));
    fcu->totvert = channeldriver_copypaste_buf->totvert;

    /* mods */
    copy_fmods(&fcu->mods, &channeldriver_copypaste_buf->mods);

    /* extrapolation mode */
    fcu->extend = channeldriver_copypaste_buf->extend;

    /* the 'juicy' stuff - the driver */
    fcu->driver = fcurve_copy_driver(channeldriver_copypaste_buf->driver);
  }

  /* done */
  return (fcu != nullptr);
}

/* Driver Management API - Copy/Paste Driver Vars */
/* Copy/Paste Buffer for Driver Vars... */
static List driver_vars_copybuf = {nullptr, nullptr};

void anim_driver_vars_copybuf_free()
{
  /* Free the driver variables kept in the buf */
  if (driver_vars_copybuf.first) {
    DriverVar *dvar, *dvarn;

    /* Free vars (and any data they use) */
    for (dvar = static_cast<DriverVar *>(driver_vars_copybuf.first); dvar; dvar = dvarn) {
      dvarn = dvar->next;
      driver_free_variable(&driver_vars_copybuf, dvar);
    }
  }

  lib_list_clear(&driver_vars_copybuf);
}

bool anim_driver_vars_can_paste()
{
  return (lib_list_is_empty(&driver_vars_copybuf) == false);
}

bool anim_driver_vars_copy(ReportList *reports, FCurve *fcu)
{
  /* sanity checks */
  if (ELEM(nullptr, fcu, fcu->driver)) {
    dune_report(reports, RPT_ERROR, "No driver to copy variables from");
    return false;
  }

  if (lib_list_is_empty(&fcu->driver->vars)) {
    dune_report(reports, RPT_ERROR, "Driver has no vars to copy");
    return false;
  }

  /* clear buf */
  anim_driver_vars_copybuf_free();

  /* copy over the vara */
  driver_vars_copy(&driver_vars_copybuf, &fcu->driver->vars);

  return (lib_list_is_empty(&driver_vars_copybuf) == false);
}

bool anim_driver_vars_paste(ReportList *reports, FCurve *fcu, bool replace)
{
  ChannelDriver *driver = (fcu) ? fcu->driver : nullptr;
  List tmp_list = {nullptr, nullptr};

  /* sanity checks */
  if (lib_list_is_empty(&driver_vars_copybuf)) {
    dune_report(reports, RPT_ERROR, "No driver vars in the internal clipboard to paste");
    return false;
  }

  if (ELEM(nullptr, fcu, fcu->driver)) {
    dune_report(reports, RPT_ERROR, "Cannot paste driver variables without a driver");
    return false;
  }

  /* 1) Make a new copy of the variables in the buffer - these will get pasted later... */
  driver_vars_copy(&tmp_list, &driver_vars_copybuf);

  /* 2) Prepare destination array */
  if (replace) {
    DriverVar *dvar, *dvarn;

    /* Free all existing vars first - We aren't retaining anything */
    for (dvar = static_cast<DriverVar *>(driver->vars.first); dvar; dvar = dvarn) {
      dvarn = dvar->next;
      driver_free_var_ex(driver, dvar);
    }

    lib_list_clear(&driver->vars);
  }

  /* 3) Add new vars */
  if (driver->vars.last) {
    DriverVar *last = static_cast<DriverVar *>(driver->vars.last);
    DriverVar *first = static_cast<DriverVar *>(tmp_list.first);

    last->next = first;
    first->prev = last;

    driver->vars.last = tmp_list.last;
  }
  else {
    driver->vars.first = tmp_list.first;
    driver->vars.last = tmp_list.last;
  }

  /* since driver variables are cached, the expression needs re-compiling too */
  dune_driver_invalidate_expression(driver, false, true);

  return true;
}

void anim_copy_as_driver(Id *target_id, const char *target_path, const char *var_name)
{
  /* Clear copy/paste buffer first (for consistency with other copy/paste bufs). */
  anim_drivers_copybuf_free();
  anim_driver_vars_copybuf_free();

  /* Create a dummy driver F-Curve. */
  FCurve *fcu = alloc_driver_fcurve(nullptr, 0, DRIVER_FCURVE_KEYFRAMES);
  ChannelDriver *driver = fcu->driver;

  /* Create a var. */
  DriverVar *var = driver_add_new_var(driver);
  DriverTarget *target = &var->targets[0];

  target->idtype = GS(target_id->name);
  target->id = target_id;
  target->rna_path = static_cast<char *>(MEM_dupallocN(target_path));

  /* Set the var name. */
  if (var_name) {
    STRNCPY(var->name, var_name);

    /* Sanitize the name. */
    for (int i = 0; var->name[i]; i++) {
      if (!(i > 0 ? isalnum(var->name[i]) : isalpha(var->name[i]))) {
        var->name[i] = '_';
      }
    }
  }

  STRNCPY(driver->expression, var->name);

  /* Store the driver into the copy/paste bufs. */
  channeldriver_copypaste_buf = fcu;

  driver_variables_copy(&driver_vars_copybuf, &driver->vars);
}

/* UI-Btn UI */
/* Add Driver - Enum Defines */
EnumPropItem prop_driver_create_mapping_types[] = {
    /* These names need reviewing. */
    {CREATEDRIVER_MAPPING_1_N,
     "SINGLE_MANY",
     0,
     "All from Target",
     "Drive all components of this property using the target picked"},
    {CREATEDRIVER_MAPPING_1_1,
     "DIRECT",
     0,
     "Single from Target",
     "Drive this component of this property using the target picked"},

    {CREATEDRIVER_MAPPING_N_N,
     "MATCH",
     ICON_COLOR,
     "Match Indices",
     "Create drivers for each pair of corresponding elements"},

    {CREATEDRIVER_MAPPING_NONE_ALL,
     "NONE_ALL",
     ICON_HAND,
     "Manually Create Later",
     "Create drivers for all properties without assigning any targets yet"},
    {CREATEDRIVER_MAPPING_NONE,
     "NONE_SINGLE",
     0,
     "Manually Create Later (Single)",
     "Create driver for this property only and without assigning any targets yet"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* Filtering cb for driver mapping types enum */
static const EnumPropItem *driver_mapping_type_itemf(Cxt *C,
                                                     ApiPtr * /*owner_ptr*/,
                                                     ApiProp * /*owner_prop*/,
                                                     bool *r_free)
{
  EnumPropItem *input = prop_driver_create_mapping_types;
  EnumPropItem *item = nullptr;

  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  int index;

  int totitem = 0;

  if (!C) { /* needed for docs */
    return prop_driver_create_mapping_types;
  }

  ui_cxt_active_but_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop && api_prop_animateable(&ptr, prop)) {
    const bool is_array = api_prop_array_check(prop);

    while (input->id) {
      if (ELEM(input->value, CREATEDRIVER_MAPPING_1_1, CREATEDRIVER_MAPPING_NONE) || (is_array)) {
        api_enum_item_add(&item, &totitem, input);
      }
      input++;
    }
  }
  else {
    /* We need at least this one! */
    api_enum_items_add_val(&item, &totitem, input, CREATEDRIVER_MAPPING_NONE);
  }

  api_enum_item_end(&item, &totitem);

  *r_free = true;
  return item;
}

/* Add Driver (With Menu) Btn Op */
static bool add_driver_btn_poll(Cxt *C)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  int index;
  bool driven, special;

  /* this op can only run if there's a prop btn active, and it can be animated */
  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (!(ptr.owner_id && ptr.data && prop)) {
    return false;
  }
  if (!api_prop_animateable(&ptr, prop)) {
    return false;
  }

  /* Don't do anything if there is an fcurve for animation wo a driver. */
  FCurve *fcu = dune_fcurve_find_by_api_cxt_ui(
      C, &ptr, prop, index, nullptr, nullptr, &driven, &special);
  return (fcu == nullptr || fcu->driver);
}

/* Wrapper for creating a driver wo knowing what the targets will be yet
 * (i.e. "manual/add later"). */
static int add_driver_btn_none(Cxt *C, WinOp *op, short mapping_type)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  int index;
  int success = 0;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (mapping_type == CREATEDRIVER_MAPPING_NONE_ALL) {
    index = -1;
  }

  if (ptr.owner_id && ptr.data && prop && api_prop_animateable(&ptr, prop)) {
    char *path = api_path_from_id_to_prop(&ptr, prop);
    short flags = CREATEDRIVER_WITH_DEFAULT_DVAR;

    if (path) {
      success += anim_add_driver(
          op->reports, ptr.owner_id, path, index, flags, DRIVER_TYPE_PYTHON);
      mem_free(path);
    }
  }

  if (success) {
    /* send updates */
    ui_cxt_update_anim_flag(C);
    graph_relations_tag_update(cxt_data_main(C));
    win_ev_add_notifier(C, NC_ANIM | ND_FCURVES_ORDER, nullptr); /* XXX */

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

static int add_driver_btn_menu_ex(Cxt *C, WinOp *op)
{
  short mapping_type = api_enum_get(op->ptr, "mapping_type");
  if (ELEM(mapping_type, CREATEDRIVER_MAPPING_NONE, CREATEDRIVER_MAPPING_NONE_ALL)) {
    /* Just create driver with no targets */
    return add_driver_btn_none(C, op, mapping_type);
  }

  /* Create Driver using Eyedropper */
  WinOpType *ot = win_optype_find("UI_OT_eyedropper_driver", true);

  /* We assume that it's fine to use the same set of props,
   * since they're actually the same. */
  win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, op->ptr, nullptr);

  return OP_FINISHED;
}

/* Show menu or create drivers */
static int add_driver_btn_menu_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  ApiProp *prop;

  if ((prop = api_struct_find_prop(op->ptr, "mapping_type")) &&
      api_prop_is_set(op->ptr, prop))
  {
    /* Mapping Type is Set - Directly go into creating drivers */
    return add_driver_btn_menu_ex(C, op);
  }

  /* Show menu */
  /* TODO: This should get filtered by the enum filter. */
  /* important to ex in the rgn we're currently in. */
  return win_menu_invoke_ex(C, op, WIN_OP_INVOKE_DEFAULT);
}

static void UNUSED_FN(ANIM_OT_driver_btn_add_menu)(wmOperatorType *ot)
{
  /* ids */
  ot->name = "Add Driver Menu";
  ot->idname = "ANIM_OT_driver_btn_add_menu";
  ot->description = "Add driver(s) for the property(s) represented by the highlighted button";

  /* cbs */
  ot->invoke = add_driver_btn_menu_invoke;
  ot->ex = add_driver_btn_menu_ex;
  ot->poll = add_driver_btn_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* props */
  ot->prop = api_def_enum(ot->sapi,
                          "mapping_type",
                          prop_driver_create_mapping_types,
                          0,
                          "Mapping Type",
                          "Method used to match target and driven props");
  api_def_enum_fns(ot->prop, driver_mapping_type_itemf);
}

/* Add Driver Btn Op */
static int add_driver_btn_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  int index;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop && api_prop_animateable(&ptr, prop)) {
    /* 1) Create a new "empty" driver for this property */
    char *path = api_path_from_id_to_prop(&ptr, prop);
    short flags = CREATEDRIVER_WITH_DEFAULT_DVAR;
    bool changed = false;

    if (path) {
      changed |= (anim_add_driver(
                      op->reports, ptr.owner_id, path, index, flags, DRIVER_TYPE_PYTHON) != 0);
      mem_free(path);
    }

    if (changed) {
      /* send updates */
      ui_cxt_update_anim_flag(C);
      graph_id_tag_update(ptr.owner_id, ID_RECALC_COPY_ON_WRITE);
      graph_relations_tag_update(cxt_data_main(C));
      win_ev_add_notifier(C, NC_ANIM | ND_FCURVES_ORDER, nullptr);
    }

    /* 2) Show editing panel for setting up this driver */
    /* TODO: Use a different one from the editing popover, so we can have the single/all toggle? */
    ui_popover_pnl_invoke(C, "GRAPH_PT_drivers_popover", true, op->reports);
  }

  return OP_INTERFACE;
}

void ANIM_OT_driver_btn_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Driver";
  ot->idname = "ANIM_OT_driver_btn_add";
  ot->description = "Add driver for the prop under the cursor";

  /* cbs */
  /* NOTE: No ex, as we need all these to use the current context info */
  ot->invoke = add_driver_btn_invoke;
  ot->poll = add_driver_btn_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* Remove Driver Btn Op ------------------------ */
static int remove_driver_btn_ex(Cxt *C, WinOp *op)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  bool changed = false;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (all) {
    index = -1;
  }

  if (ptr.owner_id && ptr.data && prop) {
    char *path = api_path_from_id_to_prop(&ptr, prop);

    if (path) {
      changed = anim_remove_driver(op->reports, ptr.owner_id, path, index, 0);

      mem_free(path);
    }
  }

  if (changed) {
    /* send updates */
    ui_cxt_update_anim_flag(C);
    graph_tag_update(cxt_data_main(C));
    win_eve_add_notifier(C, NC_ANIM | ND_FCURVES_ORDER, nullptr); /* XXX */
  }

  return (changed) ? OP_FINISHED : OP_CANCELLED;
}

void ANIM_OT_driver_btn_remove(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Driver";
  ot->idname = "ANIM_OT_driver_btn_remove";
  ot->description =
      "Remove the driver(s) for the connected prop(s) represented by the highlighted btn";

  /* cbs */
  ot->ex = remove_driver_btn_ex;
  /* TODO: `op->poll` need to have some driver to be able to do this. */

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* props */
  api_def_bool(ot->sapi, "all", true, "All", "Delete drivers for all elements of the array");
}

/* Edit Driver Btn Op ------------------------ */
static int edit_driver_btn_ex(Cxt *C, WinOp *op)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  int index;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop) {
    ui_popover_pnl_invoke(C, "GRAPH_PT_drivers_popover", true, op->reports);
  }

  return OP_INTERFACE;
}

void ANIM_OT_driver_btn_edit(WinOpType *ot)
{
  /* ids */
  ot->name = "Edit Driver";
  ot->idname = "ANIM_OT_driver_button_edit";
  ot->description =
      "Edit the drivers for the connected property represented by the highlighted button";

  /* cbs */
  ot->ex = edit_driver_btn_ex;
  /* TODO: `op->poll` need to have some driver to be able to do this. */

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* Copy Driver Btn Op */

static int copy_driver_btn_ex(Cxt *C, WinOp *op)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  bool changed = false;
  int index;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop && api_prop_animateable(&ptr, prop)) {
    char *path = api_path_from_id_to_prop(&ptr, prop);

    if (path) {
      /* only copy the driver for the button that this was involved for */
      changed = anim_copy_driver(op->reports, ptr.owner_id, path, index, 0);

      ui_cxt_update_anim_flag(C);

      mem_free(path);
    }
  }

  /* Since we're just copying, we don't rly need to do anything else. */
  return (changed) ? OP_FINISHED : OP_CANCELLED;
}

void ANIM_OT_copy_driver_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Driver";
  ot->idname = "ANIM_OT_copy_driver_btn";
  ot->description = "Copy the driver for the highlighted button";

  /* cbs */
  ot->ex = copy_driver_btn_ex;
  /* TODO: `op->poll` need to have some driver to be able to do this. */

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* Paste Driver Btn Op */
static int paste_driver_btn_ex(Cxt *C, WinOp *op)
{
  ApiPtr ptr = {nullptr};
  ApiProp *prop = nullptr;
  bool changed = false;
  int index;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop && api_prop_animateable(&ptr, prop)) {
    char *path = api_path_from_id_to_prop(&ptr, prop);

    if (path) {
      /* only copy the driver for the button that this was involved for */
      changed = anim_paste_driver(op->reports, ptr.owner_id, path, index, 0);

      ui_cxt_update_anim_flag(C);

      graph_tag_update(cxt_data_main(C));

      graph_id_tag_update(ptr.owner_id, ID_RECALC_ANIM);

      win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr); /* XXX */

      mem_free(path);
    }
  }

  /* Since we're just copying, we don't really need to do anything else. */
  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_paste_driver_btn(WinOpType *ot)
{
  /* identifiers */
  ot->name = "Paste Driver";
  ot->idname = "ANIM_OT_paste_driver_button";
  ot->description = "Paste the driver in the internal clipboard to the highlighted button";

  /* callbacks */
  ot->exec = paste_driver_button_exec;
  /* TODO: `op->poll` need to have some driver to be able to do this. */

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* ************************************************** */
