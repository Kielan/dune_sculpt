#ifndef WITH_PYTHON_MODULE

#  include <errno.h>
#  include <stdlib.h>
#  include <string.h>

#  include "MEM_guardedalloc.h"

#  include "CLG_log.h"

#  ifdef WIN32
#    include "BLI_winstuff.h"
#  endif

#  include "BLI_args.h"
#  include "BLI_fileops.h"
#  include "BLI_listbase.h"
#  include "BLI_mempool.h"
#  include "BLI_path_util.h"
#  include "BLI_string.h"
#  include "BLI_string_utf8.h"
#  include "BLI_system.h"
#  include "BLI_threads.h"
#  include "BLI_utildefines.h"

#  include "BLO_readfile.h" /* only for BLO_has_bfile_extension */

#  include "BKE_blender_version.h"
#  include "BKE_context.h"

#  include "BKE_global.h"
#  include "BKE_image_format.h"
#  include "BKE_lib_id.h"
#  include "BKE_main.h"
#  include "BKE_report.h"
#  include "BKE_scene.h"
#  include "BKE_sound.h"

#  ifdef WITH_FFMPEG
#    include "IMB_imbuf.h"
#  endif

#  ifdef WITH_PYTHON
#    include "BPY_extern_python.h"
#    include "BPY_extern_run.h"
#  endif

#  include "RE_engine.h"
#  include "RE_pipeline.h"

#  include "ED_datafiles.h"

#  include "WM_api.h"

#  ifdef WITH_LIBMV
#    include "libmv-capi.h"
#  endif

#  ifdef WITH_CYCLES_LOGGING
#    include "CCL_api.h"
#  endif

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"
#  include "DEG_depsgraph_debug.h"

#  include "WM_types.h"

#  include "creator_intern.h" /* own include */

/* -------------------------------------------------------------------- */
/** \name Utility String Parsing
 * \{ */

static bool parse_int_relative(const char *str,
                               const char *str_end_test,
                               int pos,
                               int neg,
                               int *r_value,
                               const char **r_err_msg)
{
  char *str_end = NULL;
  long value;

  errno = 0;

  switch (*str) {
    case '+':
      value = pos + strtol(str + 1, &str_end, 10);
      break;
    case '-':
      value = (neg - strtol(str + 1, &str_end, 10)) + 1;
      break;
    default:
      value = strtol(str, &str_end, 10);
      break;
  }

  if (*str_end != '\0' && (str_end != str_end_test)) {
    static const char *msg = "not a number";
    *r_err_msg = msg;
    return false;
  }
  if ((errno == ERANGE) || ((value < INT_MIN) || (value > INT_MAX))) {
    static const char *msg = "exceeds range";
    *r_err_msg = msg;
    return false;
  }
  *r_value = (int)value;
  return true;
}

static const char *parse_int_range_sep_search(const char *str, const char *str_end_test)
{
  const char *str_end_range = NULL;
  if (str_end_test) {
    str_end_range = memchr(str, '.', (str_end_test - str) - 1);
    if (str_end_range && (str_end_range[1] != '.')) {
      str_end_range = NULL;
    }
  }
  else {
    str_end_range = strstr(str, "..");
    if (str_end_range && (str_end_range[2] == '\0')) {
      str_end_range = NULL;
    }
  }
  return str_end_range;
}

/**
 * Parse a number as a range, eg: `1..4`.
 *
 * The \a str_end_range argument is a result of #parse_int_range_sep_search.
 */
static bool parse_int_range_relative(const char *str,
                                     const char *str_end_range,
                                     const char *str_end_test,
                                     int pos,
                                     int neg,
                                     int r_value_range[2],
                                     const char **r_err_msg)
{
  if (parse_int_relative(str, str_end_range, pos, neg, &r_value_range[0], r_err_msg) &&
      parse_int_relative(
          str_end_range + 2, str_end_test, pos, neg, &r_value_range[1], r_err_msg)) {
    return true;
  }
  return false;
}

static bool parse_int_relative_clamp(const char *str,
                                     const char *str_end_test,
                                     int pos,
                                     int neg,
                                     int min,
                                     int max,
                                     int *r_value,
                                     const char **r_err_msg)
{
  if (parse_int_relative(str, str_end_test, pos, neg, r_value, r_err_msg)) {
    CLAMP(*r_value, min, max);
    return true;
  }
  return false;
}

static bool parse_int_range_relative_clamp(const char *str,
                                           const char *str_end_range,
                                           const char *str_end_test,
                                           int pos,
                                           int neg,
                                           int min,
                                           int max,
                                           int r_value_range[2],
                                           const char **r_err_msg)
{
  if (parse_int_range_relative(
          str, str_end_range, str_end_test, pos, neg, r_value_range, r_err_msg)) {
    CLAMP(r_value_range[0], min, max);
    CLAMP(r_value_range[1], min, max);
    return true;
  }
  return false;
}

/**
 * No clamping, fails with any number outside the range.
 */
static bool parse_int_strict_range(const char *str,
                                   const char *str_end_test,
                                   const int min,
                                   const int max,
                                   int *r_value,
                                   const char **r_err_msg)
{
  char *str_end = NULL;
  long value;

  errno = 0;
  value = strtol(str, &str_end, 10);

  if (*str_end != '\0' && (str_end != str_end_test)) {
    static const char *msg = "not a number";
    *r_err_msg = msg;
    return false;
  }
  if ((errno == ERANGE) || ((value < min) || (value > max))) {
    static const char *msg = "exceeds range";
    *r_err_msg = msg;
    return false;
  }
  *r_value = (int)value;
  return true;
}

static bool parse_int(const char *str,
                      const char *str_end_test,
                      int *r_value,
                      const char **r_err_msg)
{
  return parse_int_strict_range(str, str_end_test, INT_MIN, INT_MAX, r_value, r_err_msg);
}

static bool parse_int_clamp(const char *str,
                            const char *str_end_test,
                            int min,
                            int max,
                            int *r_value,
                            const char **r_err_msg)
{
  if (parse_int(str, str_end_test, r_value, r_err_msg)) {
    CLAMP(*r_value, min, max);
    return true;
  }
  return false;
}

#  if 0
/**
 * Version of #parse_int_relative_clamp
 * that parses a comma separated list of numbers.
 */
static int *parse_int_relative_clamp_n(
    const char *str, int pos, int neg, int min, int max, int *r_value_len, const char **r_err_msg)
{
  const char sep = ',';
  int len = 1;
  for (int i = 0; str[i]; i++) {
    if (str[i] == sep) {
      len++;
    }
  }

  int *values = MEM_mallocN(sizeof(*values) * len, __func__);
  int i = 0;
  while (true) {
    const char *str_end = strchr(str, sep);
    if (ELEM(*str, sep, '\0')) {
      static const char *msg = "incorrect comma use";
      *r_err_msg = msg;
      goto fail;
    }
    else if (parse_int_relative_clamp(str, str_end, pos, neg, min, max, &values[i], r_err_msg)) {
      i++;
    }
    else {
      goto fail; /* error message already set */
    }

    if (str_end) { /* next */
      str = str_end + 1;
    }
    else { /* finished */
      break;
    }
  }

  *r_value_len = i;
  return values;

fail:
  MEM_freeN(values);
  return NULL;
}

#  endif

/**
 * Version of #parse_int_relative_clamp & #parse_int_range_relative_clamp
 * that parses a comma separated list of numbers.
 *
 * \note single values are evaluated as a range with matching start/end.
 */
static int (*parse_int_range_relative_clamp_n(const char *str,
                                              int pos,
                                              int neg,
                                              int min,
                                              int max,
                                              int *r_value_len,
                                              const char **r_err_msg))[2]
{
  const char sep = ',';
  int len = 1;
  for (int i = 0; str[i]; i++) {
    if (str[i] == sep) {
      len++;
    }
  }

  int(*values)[2] = MEM_mallocN(sizeof(*values) * len, __func__);
  int i = 0;
  while (true) {
    const char *str_end_range;
    const char *str_end = strchr(str, sep);
    if (ELEM(*str, sep, '\0')) {
      static const char *msg = "incorrect comma use";
      *r_err_msg = msg;
      goto fail;
    }
    else if ((str_end_range = parse_int_range_sep_search(str, str_end)) ?
                 parse_int_range_relative_clamp(
                     str, str_end_range, str_end, pos, neg, min, max, values[i], r_err_msg) :
                 parse_int_relative_clamp(
                     str, str_end, pos, neg, min, max, &values[i][0], r_err_msg)) {
      if (str_end_range == NULL) {
        values[i][1] = values[i][0];
      }
      i++;
    }
    else {
      goto fail; /* error message already set */
    }

    if (str_end) { /* next */
      str = str_end + 1;
    }
    else { /* finished */
      break;
    }
  }

  *r_value_len = i;
  return values;

fail:
  MEM_freeN(values);
  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Utilities Python Context Macro (#BPY_CTX_SETUP)
 * \{ */

#  ifdef WITH_PYTHON

struct BlendePyContextStore {
  wmWindowManager *wm;
  Scene *scene;
  wmWindow *win;
  bool has_win;
};
