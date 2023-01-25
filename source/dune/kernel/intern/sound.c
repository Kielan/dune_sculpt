#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_dunelib.h"
#include "LIB_iterator.h"
#include "LIB_math.h"
#include "LIB_threads.h"

#include "LANG_translation.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "structs_anim_types.h"
#include "structs_object_types.h"
#include "structs_packedFile_types.h"
#include "structs_scene_types.h"
#include "structs_screen_types.h"
#include "structs_sequence_types.h"
#include "structs_sound_types.h"
#include "structs_speaker_types.h"
#include "structs_windowmanager_types.h"

#ifdef WITH_AUDASPACE
#  include "../../../intern/audaspace/intern/AUD_Set.h"
#  include <AUD_Handle.h>
#  include <AUD_Sequence.h>
#  include <AUD_Sound.h>
#  include <AUD_Special.h>
#endif

#include "KE_bpath.h"
#include "KE_global.h"
#include "KE_idtype.h"
#include "KE_lib_id.h"
#include "KE_main.h"
#include "KE_packedFile.h"
#include "KE_scene.h"
#include "KE_sound.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "LOADER_read_write.h"

#include "SEQ_sequencer.h"
#include "SEQ_sound.h"

static void sound_free_audio(bSound *sound);

static void sound_copy_data(Main *UNUSED(dunemain),
                            ID *id_dst,
                            const ID *id_src,
                            const int UNUSED(flag))
{
  duneSound *sound_dst = (duneSound *)id_dst;
  const bSound *sound_src = (const duneSound *)id_src;

  sound_dst->handle = NULL;
  sound_dst->cache = NULL;
  sound_dst->waveform = NULL;
  sound_dst->playback_handle = NULL;
  sound_dst->spinlock = MEM_mallocN(sizeof(SpinLock), "sound_spinlock");
  LIB_spin_init(sound_dst->spinlock);

  /* Just to be sure, should not have any value actually after reading time. */
  sound_dst->ipo = NULL;
  sound_dst->newpackedfile = NULL;

  if (sound_src->packedfile != NULL) {
    sound_dst->packedfile = KERNEL_packedfile_duplicate(sound_src->packedfile);
  }

  KERNEL_sound_reset_runtime(sound_dst);
}

static void sound_free_data(ID *id)
{
  bSound *sound = (duneSound *)id;

  /* No animation-data here. */

  if (sound->packedfile) {
    KERNEL_packedfile_free(sound->packedfile);
    sound->packedfile = NULL;
  }

  sound_free_audio(sound);
  KERNEL_sound_free_waveform(sound);

  if (sound->spinlock) {
    LIB_spin_end(sound->spinlock);
    MEM_freeN(sound->spinlock);
    sound->spinlock = NULL;
  }
}

static void sound_foreach_cache(ID *id,
                                IDTypeForeachCacheFunctionCallback function_callback,
                                void *user_data)
{
  duneSound *sound = (duneSound *)id;
  IDCacheKey key = {
      .id_session_uuid = id->session_uuid,
      .offset_in_ID = offsetof(duneSound, waveform),
      .cache_v = sound->waveform,
  };

  function_callback(id, &key, &sound->waveform, 0, user_data);
}

static void sound_foreach_path(ID *id, DunePathForeachPathData *dunepath_data)
{
  duneSound *sound = (duneSound *)id;
  if (sound->packedfile != NULL && (dunepath_data->flag & KERNEL_DUNEPATH_FOREACH_PATH_SKIP_PACKED) != 0) {
    return;
  }

  /* FIXME: This does not check for empty path... */
  KERNEL_dunepath_foreach_path_fixed_process(dunepath_data, sound->filepath);
}

static void sound_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  duneSound *sound = (duneSound *)id;
  const bool is_undo = LOADER_write_is_undo(writer);

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  sound->tags = 0;
  sound->handle = NULL;
  sound->playback_handle = NULL;
  sound->spinlock = NULL;

  /* Do not store packed files in case this is a library override ID. */
  if (ID_IS_OVERRIDE_LIBRARY(sound) && !is_undo) {
    sound->packedfile = NULL;
  }

  /* write LibData */
  LOADER_write_id_struct(writer, duneSound, id_address, &sound->id);
  KERNEL_id_dune_write(writer, &sound->id);

  KERNEL_packedfile_dune_write(writer, sound->packedfile);
}
