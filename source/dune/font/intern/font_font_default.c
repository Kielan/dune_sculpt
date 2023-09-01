/* API for loading default font files. */

#include <stdio.h>

#include "font_api.h"

#include "lib_path_util.h"

#include "dune_appdir.h"

static int font_load_font_default(const char *filename, const bool unique)
{
  const char *dir = font_appdir_folder_id(DUNE_DATAFILES, "fonts");
  if (dir == NULL) {
    fprintf(stderr,
            "%s: 'fonts' data path not found for '%s', will not be able to display text\n",
            __func__,
            filename);
    return -1;
  }

  char filepath[FILE_MAX];
  lib_join_dirfile(filepath, sizeof(filepath), dir, filename);

  return (unique) ? font_load_unique(filepath) : font_load(filepath);
}

int font_load_default(const bool unique)
{
  return font_load_font_default("droidsans.ttf", unique);
}

int font_load_mono_default(const bool unique)
{
  return font_load_font_default("bmonofont-i18n.ttf", unique);
}
