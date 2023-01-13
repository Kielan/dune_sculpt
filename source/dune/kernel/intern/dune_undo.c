/**
 * Dune file undo (known as 'Global Undo').
 * structs level diffing for undo.
 */

#ifndef _WIN32
#  include <unistd.h> /* for read close */
#else
#  include <io.h> /* for open close read */
#endif

#include <errno.h>
#include <fcntl.h> /* for open */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "structs_scene_types.h"

#include "LIB_path_util.h"
#include "LIB_string.h"
#include "LIB_utildefines.h"

#include "KERNEL_appdir.h"
#include "KERNEL_dune_undo.h" /* own include */
#include "KERNEL_dunefile.h"
#include "KERNEL_context.h"
#include "KERNEL_global.h"
#include "KERNEL_main.h"
#include "KERNEL_undo_system.h"

#include "LOADER_readfile.h"
#include "LOADER_undofile.h"
#include "LOADER_writefile.h"

#include "DEG_depsgraph.h"

/* -------------------------------------------------------------------- */
/** Global Undo **/

#define UNDO_DISK 0

bool KERNEL_memfile_undo_decode(MemFileUndoData *mfu,
                             const eUndoStepDir undo_direction,
                             const bool use_old_dunemain_data,
                             duneContext *C)
{
  Main *dunemain = CTX_data_main(C);
  char mainstr[sizeof(dunemain->filepath)];
  int success = 0, fileflags;

  LIB_strncpy(mainstr, KERNEL_main_dunefile_path(dunemain), sizeof(mainstr)); /* temporal store */

  fileflags = G.fileflags;
  G.fileflags |= G_FILE_NO_UI;

  if (UNDO_DISK) {
    const struct DuneFileReadParams params = {0};
    DuneFileReadReport bf_reports = {.reports = NULL};
    struct DuneFileData *dune_file_data = KERNEL_dunefile_read(mfu->filename, &params, &bf_reports);
    if (dune_file_data != NULL) {
      KERNEL_dunefile_read_setup(C, dune_file_data, &params, &bf_reports);
      success = true;
    }
  }
  else {
    struct DuneFileReadParams params = {0};
    params.undo_direction = undo_direction;
    if (!use_old_bmain_data) {
      params.skip_flags |= LOADER_READ_SKIP_UNDO_OLD_MAIN;
    }
    struct DuneFileData *bfd = KERNEL_dunefile_read_from_memfile(
        bmain, &mfu->memfile, &params, NULL);
    if (bfd != NULL) {
      KERNEL_dunefile_read_setup(C, bfd, &params, &(DuneFileReadReport){NULL});
      success = true;
    }
  }

  /* Restore, dunemain has been re-allocated. */
  dunemain = CTX_data_main(C);
  STRNCPY(bmain->filepath, mainstr);
  G.fileflags = fileflags;

  if (success) {
    /* important not to update time here, else non keyed transforms are lost */
    DEG_tag_on_visible_update(bmain, false);
  }

  return success;
}

MemFileUndoData *KERNEL_memfile_undo_encode(Main *dunemain, MemFileUndoData *mfu_prev)
{
  MemFileUndoData *mfu = MEM_callocN(sizeof(MemFileUndoData), __func__);

  /* Include recovery information since undo-data is written out as #DUNE_QUIT_FILE. */
  const int fileflags = G.fileflags | G_FILE_RECOVER_WRITE;

  /* disk save version */
  if (UNDO_DISK) {
    static int counter = 0;
    char filename[FILE_MAX];
    char numstr[32];

    /* Calculate current filename. */
    counter++;
    counter = counter % U.undosteps;

    LIB_snprintf(numstr, sizeof(numstr), "%d.blend", counter);
    LIB_join_dirfile(filename, sizeof(filename), KERNEL_tempdir_session(), numstr);

    /* success = */ /* UNUSED */ LOADER_write_file(
        dunemain, filename, fileflags, &(const struct DuneFileWriteParams){0}, NULL);

    LIB_strncpy(mfu->filename, filename, sizeof(mfu->filename));
  }
  else {
    MemFile *prevfile = (mfu_prev) ? &(mfu_prev->memfile) : NULL;
    if (prevfile) {
      LOADER_memfile_clear_future(prevfile);
    }
    /* success = */ /* UNUSED */ LOADER_write_file_mem(dunemain, prevfile, &mfu->memfile, fileflags);
    mfu->undo_size = mfu->memfile.size;
  }

  dunemain->is_memfile_undo_written = true;

  return mfu;
}

void KERNEL_memfile_undo_free(MemFileUndoData *mfu)
{
  LOADER_memfile_free(&mfu->memfile);
  MEM_freeN(mfu);
}
