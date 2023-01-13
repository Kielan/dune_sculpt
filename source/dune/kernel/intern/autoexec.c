/**
 * Currently just checks if a blend file can be trusted to autoexec,
 * may add signing here later.
 */

#include <stdlib.h>
#include <string.h>

#include "structs_userdef_types.h"

#include "LIB_fnmatch.h"
#include "LIB_path_util.h"
#include "LIB_utildefines.h"

#ifdef WIN32
#  include "LIB_string.h"
#endif

#include "KERNEL_autoexec.h" /* own include */

bool KERNEL_autoexec_match(const char *path)
{
  dunePathCompare *path_cmp;

#ifdef WIN32
  const int fnmatch_flags = FNM_CASEFOLD;
#else
  const int fnmatch_flags = 0;
#endif

  LIB_assert((U.flag & USER_SCRIPT_AUTOEXEC_DISABLE) == 0);

  for (path_cmp = U.autoexec_paths.first; path_cmp; path_cmp = path_cmp->next) {
    if (path_cmp->path[0] == '\0') {
      /* pass */
    }
    else if (path_cmp->flag & USER_PATHCMP_GLOB) {
      if (fnmatch(path_cmp->path, path, fnmatch_flags) == 0) {
        return true;
      }
    }
    else if (LIB_path_ncmp(path_cmp->path, path, strlen(path_cmp->path)) == 0) {
      return true;
    }
  }

  return false;
}
