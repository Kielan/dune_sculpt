
#include "mem_guardedalloc.h"

#include "lib_dynstr.h"
#include "lib_string.h"
#include "lib_string_utils.h"
#include "lib_vector.hh"

#include "gpu_shader_dependency_private.h"
#include "gpu_shader_private.hh"

#include "gpu_platform.h"

#include "clg_log.h"

static ClgLogRef LOG = {"gpu.shader"};

namespace dune::gpu {

/* -------------------------------------------------------------------- */
/** Debug functions */

/* Number of lines before and after the error line to print for compilation errors. */
#define DEBUG_CONTEXT_LINES 0
/**
 * Print dependencies sources list before the shader report.
 * Useful to debug include order or missing dependencies.
 */
#define DEBUG_DEPENDENCIES 0

void Shader::print_log(Span<const char *> sources,
                       char *log,
                       const char *stage,
                       const bool error,
                       GPULogParser *parser)
{
  const char line_prefix[] = "      | ";
  char err_col[] = "\033[31;1m";
  char warn_col[] = "\033[33;1m";
  char info_col[] = "\033[0;2m";
  char reset_col[] = "\033[0;0m";
  char *sources_combined = lib_string_join_arrayN((const char **)sources.data(), sources.size());
  DynStr *dynstr = lib_dynstr_new();

  if (!CLG_color_support_get(&LOG)) {
    err_col[0] = warn_col[0] = info_col[0] = reset_col[0] = '\0';
  }

  lib_dynstr_appendf(dynstr, "\n");

#if DEBUG_DEPENDENCIES
  lib_dynstr_appendf(
      dynstr, "%s%sIncluded files (in order):%s\n", info_col, line_prefix, reset_col);
#endif

  Vector<int64_t> sources_end_line;
  for (StringRefNull src : sources) {
    int64_t cursor = 0, line_count = 0;
    while ((cursor = src.find('\n', cursor) + 1)) {
      line_count++;
    }
    if (sources_end_line.is_empty() == false) {
      line_count += sources_end_line.last();
    }
    sources_end_line.append(line_count);
#if DEBUG_DEPENDENCIES
    StringRefNull filename = shader::gpu_shader_dependency_get_filename_from_source_string(src);
    if (!filename.is_empty()) {
      lib_dynstr_appendf(
          dynstr, "%s%s  %s%s\n", info_col, line_prefix, filename.c_str(), reset_col);
    }
#endif
  }
  if (sources_end_line.size() == 0) {
    sources_end_line.append(0);
  }

  char *log_line = log, *line_end;

  LogCursor previous_location;

  bool found_line_id = false;
  while ((line_end = strchr(log_line, '\n'))) {
    /* Skip empty lines. */
    if (line_end == log_line) {
      log_line++;
      continue;
    }

    /* Silence not useful lines. */
    StringRef logref = StringRefNull(log_line).substr(0, (size_t)line_end - (size_t)log_line);
    if (logref.endswith(" shader failed to compile with the following errors:") ||
        logref.endswith(" No code generated")) {
      log_line += (size_t)line_end - (size_t)log_line;
      continue;
    }

    GPULogItem log_item;
    log_line = parser->parse_line(log_line, log_item);

    /* Sanitize output. Really bad values can happen when the error line is buggy. */
    if (log_item.cursor.source >= sources.size()) {
      log_item.cursor.source = -1;
    }
    if (log_item.cursor.row >= sources_end_line.last()) {
      log_item.cursor.source = -1;
      log_item.cursor.row = -1;
    }

    if (log_item.cursor.row == -1) {
      found_line_id = false;
    }
    else if (log_item.source_base_row && log_item.cursor.source > 0) {
      log_item.cursor.row += sources_end_line[log_item.cursor.source - 1];
    }

    const char *src_line = sources_combined;

    /* Separate from previous block. */
    if (previous_location.source != log_item.cursor.source ||
        previous_location.row != log_item.cursor.row) {
      lib_dynstr_appendf(dynstr, "%s%s%s\n", info_col, line_prefix, reset_col);
    }
    else if (log_item.cursor.column != previous_location.column) {
      lib_dynstr_appendf(dynstr, "%s\n", line_prefix);
    }
    /* Print line from the source file that is producing the error. */
    if ((log_item.cursor.row != -1) && (log_item.cursor.row != previous_location.row ||
                                        log_item.cursor.column != previous_location.column)) {
      const char *src_line_end;
      found_line_id = false;
      /* error_line is 1 based in this case. */
      int src_line_index = 1;
      while ((src_line_end = strchr(src_line, '\n'))) {
        if (src_line_index >= log_item.cursor.row) {
          found_line_id = true;
          break;
        }
        if (src_line_index >= log_item.cursor.row - DEBUG_CONTEXT_LINES) {
          lib_dynstr_appendf(dynstr, "%5d | ", src_line_index);
          lib_dynstr_nappend(dynstr, src_line, (src_line_end + 1) - src_line);
        }
        /* Continue to next line. */
        src_line = src_line_end + 1;
        src_line_index++;
      }
      /* Print error source. */
      if (found_line_id) {
        if (log_item.cursor.row != previous_location.row) {
          lib_dynstr_appendf(dynstr, "%5d | ", src_line_index);
        }
        else {
          lib_dynstr_appendf(dynstr, line_prefix);
        }
        lib_dynstr_nappend(dynstr, src_line, (src_line_end + 1) - src_line);
        /* Print char offset. */
        lib_dynstr_appendf(dynstr, line_prefix);
        if (log_item.cursor.column != -1) {
          for (int i = 0; i < log_item.cursor.column; i++) {
            lib_dynstr_appendf(dynstr, " ");
          }
          lib_dynstr_appendf(dynstr, "^");
        }
        lib_dynstr_appendf(dynstr, "\n");

        /* Skip the error line. */
        src_line = src_line_end + 1;
        src_line_index++;
        while ((src_line_end = strchr(src_line, '\n'))) {
          if (src_line_index > log_item.cursor.row + DEBUG_CONTEXT_LINES) {
            break;
          }
          lib_dynstr_appendf(dynstr, "%5d | ", src_line_index);
          lib_dynstr_nappend(dynstr, src_line, (src_line_end + 1) - src_line);
          /* Continue to next line. */
          src_line = src_line_end + 1;
          src_line_index++;
        }
      }
    }
    lib_dynstr_appendf(dynstr, line_prefix);

    /* Search the correct source index. */
    int row_in_file = log_item.cursor.row;
    int source_index = log_item.cursor.source;
    if (source_index <= 0) {
      for (auto i : sources_end_line.index_range()) {
        if (log_item.cursor.row <= sources_end_line[i]) {
          source_index = i;
          break;
        }
      }
    }
    if (source_index > 0) {
      row_in_file -= sources_end_line[source_index - 1];
    }
    /* Print the filename the error line is coming from. */
    if (source_index > 0) {
      StringRefNull filename = shader::gpu_shader_dependency_get_filename_from_source_string(
          sources[source_index]);
      if (!filename.is_empty()) {
        lib_dynstr_appendf(dynstr,
                           "%s%s:%d:%d: %s",
                           info_col,
                           filename.c_str(),
                           row_in_file,
                           log_item.cursor.column + 1,
                           reset_col);
      }
    }

    if (log_item.severity == Severity::Error) {
      lib_dynstr_appendf(dynstr, "%s%s%s: ", err_col, "Error", info_col);
    }
    else if (log_item.severity == Severity::Error) {
      lib_dynstr_appendf(dynstr, "%s%s%s: ", warn_col, "Warning", info_col);
    }
    /* Print the error itself. */
    lib_dynstr_append(dynstr, info_col);
    lib_dynstr_nappend(dynstr, log_line, (line_end + 1) - log_line);
    lib_dynstr_append(dynstr, reset_col);
    /* Continue to next line. */
    log_line = line_end + 1;
    previous_location = log_item.cursor;
  }
  mem_freen(sources_combined);

  CLG_Severity severity = error ? CLG_SEVERITY_ERROR : CLG_SEVERITY_WARN;

  if (((LOG.type->flag & CLG_FLAG_USE) && (LOG.type->level >= 0)) ||
      (severity >= CLG_SEVERITY_WARN)) {
    const char *_str = lib_dynstr_get_cstring(dynstr);
    CLG_log_str(LOG.type, severity, this->name, stage, _str);
    mem_freen((void *)_str);
  }

  lib_dynstr_free(dynstr);
}

char *GPULogParser::skip_severity(char *log_line,
                                  GPULogItem &log_item,
                                  const char *error_msg,
                                  const char *warning_msg) const
{
  if (STREQLEN(log_line, error_msg, strlen(error_msg))) {
    log_line += strlen(error_msg);
    log_item.severity = Severity::Error;
  }
  else if (STREQLEN(log_line, warning_msg, strlen(warning_msg))) {
    log_line += strlen(warning_msg);
    log_item.severity = Severity::Warning;
  }
  return log_line;
}

char *GPULogParser::skip_separators(char *log_line, const StringRef separators) const
{
  while (at_any(log_line, separators)) {
    log_line++;
  }
  return log_line;
}

char *GPULogParser::skip_until(char *log_line, char stop_char) const
{
  char *cursor = log_line;
  while (!ELEM(cursor[0], '\n', '\0')) {
    if (cursor[0] == stop_char) {
      return cursor;
    }
    cursor++;
  }
  return log_line;
}

bool GPULogParser::at_number(const char *log_line) const
{
  return log_line[0] >= '0' && log_line[0] <= '9';
}

bool GPULogParser::at_any(const char *log_line, const StringRef chars) const
{
  return chars.find(log_line[0]) != StringRef::not_found;
}

int GPULogParser::parse_number(const char *log_line, char **r_new_position) const
{
  return (int)strtol(log_line, r_new_position, 10);
}

}  // namespace dune::gpu
