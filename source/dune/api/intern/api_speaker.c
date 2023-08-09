#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_sound.h"
#include "types_speaker.h"

#include "lang_translation.h"

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "dune_main.h"

#  include "wm_api.h"
#  include "wm_types.h"

#else

static void api_def_speaker(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Speaker", "ID");
  api_def_struct_ui_text(sapi, "Speaker", "Speaker data-block for 3D audio speaker objects");
  api_def_struct_ui_icon(sapi, ICON_SPEAKER);

  prop = api_def_prop(sapi, "muted", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "flag", SPK_MUTED);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Mute", "Mute the speaker");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SOUND);
#  if 0
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "sound", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Sound");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Sound", "Sound data-block used by this speaker");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_sound_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "volume_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Maximum Volume", "Maximum volume, no matter how near the object is");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_volume_max_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "volume_min", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Minimum Volume", "Minimum volume, no matter how far away the object is");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_volume_min_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "distance_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(
      prop,
      "Maximum Distance",
      "Maximum distance for volume calculation, no matter how far away the object is");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_distance_max_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "distance_reference", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(
      prop, "Reference Distance", "Reference distance at which volume is 100%");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_distance_reference_set", NULL
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "attenuation", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(
      prop, "Attenuation", "How strong the distance affects volume, depending on distance model");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_attenuation_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "cone_angle_outer", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 360.0f);
  api_def_prop_ui_text(
      prop,
      "Outer Cone Angle",
      "Angle of the outer cone, in degrees, outside this cone the volume is "
      "the outer cone volume, between inner and outer cone the volume is interpolated");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_cone_angle_outer_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "cone_angle_inner", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 360.0f);
  api_def_prop_ui_text(
      prop,
      "Inner Cone Angle",
      "Angle of the inner cone, in degrees, inside the cone the volume is 100%");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_cone_angle_inner_set", NULL
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "cone_volume_outer", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Outer Cone Volume", "Volume outside the outer cone");
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_cone_volume_outer_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "volume", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Volume", "How loud the sound is");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SOUND);
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_volume_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  prop = api_def_prop(sapi, "pitch", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.1f, 10.0f);
  api_def_prop_ui_text(prop, "Pitch", "Playback pitch of the sound");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_SOUND);
#  if 0
  api_def_prop_float_fns(prop, NULL, "api_Speaker_pitch_set", NULL);
  api_def_prop_update(prop, 0, "api_Speaker_update");
#  endif

  /* common */
  api_def_animdata_common(sapi);
}

void api_def_speaker(DuneApi *dapi)
{
  api_def_speaker(dapi);
}

#endif
