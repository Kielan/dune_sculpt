#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_iterator.h"
#include "BLI_math.h"
#include "BLI_threads.h"

#include "BLT_translation.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_anim_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_sequence_types.h"
#include "DNA_sound_types.h"
#include "DNA_speaker_types.h"
#include "DNA_windowmanager_types.h"

#ifdef WITH_AUDASPACE
#  include "../../../intern/audaspace/intern/AUD_Set.h"
#  include <AUD_Handle.h>
#  include <AUD_Sequence.h>
#  include <AUD_Sound.h>
#  include <AUD_Special.h>
#endif

#include "BKE_bpath.h"
#include "BKE_global.h"
#include "BKE_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_packedFile.h"
#include "BKE_scene.h"
#include "BKE_sound.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "BLO_read_write.h"

#include "SEQ_sequencer.h"
#include "SEQ_sound.h"

static void sound_free_audio(bSound *sound);

static void sound_copy_data(Main *UNUSED(bmain),
                            ID *id_dst,
                            const ID *id_src,
                            const int UNUSED(flag))
{
  bSound *sound_dst = (bSound *)id_dst;
  const bSound *sound_src = (const bSound *)id_src;

  sound_dst->handle = NULL;
  sound_dst->cache = NULL;
  sound_dst->waveform = NULL;
  sound_dst->playback_handle = NULL;
  sound_dst->spinlock = MEM_mallocN(sizeof(SpinLock), "sound_spinlock");
  BLI_spin_init(sound_dst->spinlock);

  /* Just to be sure, should not have any value actually after reading time. */
  sound_dst->ipo = NULL;
  sound_dst->newpackedfile = NULL;

  if (sound_src->packedfile != NULL) {
    sound_dst->packedfile = BKE_packedfile_duplicate(sound_src->packedfile);
  }

  BKE_sound_reset_runtime(sound_dst);
}

static void sound_free_data(ID *id)
{
  bSound *sound = (bSound *)id;

  /* No animation-data here. */

  if (sound->packedfile) {
    BKE_packedfile_free(sound->packedfile);
    sound->packedfile = NULL;
  }

  sound_free_audio(sound);
  BKE_sound_free_waveform(sound);

  if (sound->spinlock) {
    BLI_spin_end(sound->spinlock);
    MEM_freeN(sound->spinlock);
    sound->spinlock = NULL;
  }
}

static void sound_foreach_cache(ID *id,
                                IDTypeForeachCacheFunctionCallback function_callback,
                                void *user_data)
{
  bSound *sound = (bSound *)id;
  IDCacheKey key = {
      .id_session_uuid = id->session_uuid,
      .offset_in_ID = offsetof(bSound, waveform),
      .cache_v = sound->waveform,
  };

  function_callback(id, &key, &sound->waveform, 0, user_data);
}

static void sound_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  bSound *sound = (bSound *)id;
  if (sound->packedfile != NULL && (bpath_data->flag & BKE_BPATH_FOREACH_PATH_SKIP_PACKED) != 0) {
    return;
  }

  /* FIXME: This does not check for empty path... */
  BKE_bpath_foreach_path_fixed_process(bpath_data, sound->filepath);
}

static void sound_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  bSound *sound = (bSound *)id;
  const bool is_undo = BLO_write_is_undo(writer);

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
  BLO_write_id_struct(writer, bSound, id_address, &sound->id);
  BKE_id_blend_write(writer, &sound->id);

  BKE_packedfile_blend_write(writer, sound->packedfile);
}
