/**
 * C Logging Library (log)
 * ========================
 *
 * Usage
 * -----
 *
 * - `LOG_REF_DECLARE_GLOBAL` macro to declare LogRef pointers.
 * - `log_` prefixed macros for logging.
 *
 * Identifiers
 * -----------
 *
 * LogRef holds an identifier which defines the category of the logger.
 *
 * You can define and use identifiers as needed, logging will lazily initialize them.
 *
 * By convention lower case dot separated identifiers are used, eg:
 * `module.sub_module`, this allows filtering by `module.*`,
 * see log_type_filter_include, log_type_filter_exclude
 *
 * There is currently no functionality to remove a category once it's created.
 *
 * Severity
 * --------
 *
 * - `INFO`: Simply log events, uses verbosity levels to control how much information to show.
 * - `WARN`: General warnings (which aren't necessary to show to users).
 * - `ERROR`: An error we can recover from, should not happen.
 * - `FATAL`: Similar to assert. This logs the message, then a stack trace and abort.
 * Verbosity Level
 * ---------------
 *
 * Usage:
 *
 * - 0: Always show (used for warnings, errors).
 *   Should never get in the way or become annoying.
 *
 * - 1: Top level module actions (eg: load a file, create a new window .. etc).
 *
 * - 2: Actions within a module (steps which compose an action, but don't flood output).
 *   Running a tool, full data recalculation.
 *
 * - 3: Detailed actions which may be of interest when debugging internal logic of a module
 *   These *may* flood the log with details.
 *
 * - 4+: May be used for more details than 3, should be avoided but not prevented.
 */

#ifndef __CLG_LOG_H__
#define __CLG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __GNUC__
#  define _LOG_ATTR_NONNULL(args...) __attribute__((nonnull(args)))
#else
#  define _LOG_ATTR_NONNULL(...)
#endif

#ifdef __GNUC__
#  define _LOG_ATTR_PRINTF_FORMAT(format_param, dots_param) \
    __attribute__((format(printf, format_param, dots_param)))
#else
#  define _LOG_ATTR_PRINTF_FORMAT(format_param, dots_param)
#endif

#define STRINGIFY_ARG(x) "" #x
#define STRINGIFY_APPEND(a, b) "" a #b
#define STRINGIFY(x) STRINGIFY_APPEND("", x)

struct LogCtx;

/* Don't typedef enums. */
enum _LogFlag {
  LOG_FLAG_USE = (1 << 0),
};

enum LogSeverity {
  LOG_SEVERITY_INFO = 0,
  LOG_SEVERITY_WARN,
  LOG_SEVERITY_ERROR,
  LOG_SEVERITY_FATAL,
};
#define LOG_SEVERITY_LEN (LOG_SEVERITY_FATAL + 1)

/* Each logger ID has one of these. */
typedef struct LogType {
  struct LogType *next;
  char identifier[64];
  /** FILE output. */
  struct LogCtx *ctx;
  /** Control behavior. */
  int level;
  enum LogFlag flag;
} LogType;

typedef struct LogRef {
  const char *id;
  LogType *type;
  struct LogRef *next;
} LogRef;

void log_str(LogType *lg,
             enum Severity severity,
             const char *file_line,
             const char *fn,
             const char *message) _LOG_ATTR_NONNULL(1, 3, 4, 5);
void logf(LogType *lg,
          enum Severity severity,
          const char *file_line,
          const char *fn,
          const char *format,
          ...) _LOG_ATTR_NONNULL(1, 3, 4, 5) _LOG_ATTR_PRINTF_FORMAT(5, 6);

/* Main initializer and destructor (per session, not logger). */
void log_init(void);
void log_exit(void);

void log_output_set(void *file_handle);
void log_output_use_basename_set(int value);
void log_output_use_timestamp_set(int value);
void log_error_fn_set(void (*error_fn)(void *file_handle));
void log_fatal_fn_set(void (*fatal_fn)(void *file_handle));
void log_backtrace_fn_set(void (*fatal_fn)(void *file_handle));

void log_type_filter_include(const char *type_filter, int type_filter_len);
void log_type_filter_exclude(const char *type_filter, int type_filter_len);

void log_level_set(int level);

void log_ref_init(LogRef *log_ref);

int CLG_color_support_get(CLG_LogRef *clg_ref);

/** Declare outside function, declare as extern in header. */
#define CLG_LOGREF_DECLARE_GLOBAL(var, id) \
  static CLG_LogRef _static_##var = {id}; \
  CLG_LogRef *var = &_static_##var

/** Initialize struct once. */
#define CLOG_ENSURE(clg_ref) \
  ((clg_ref)->type ? (clg_ref)->type : (CLG_logref_init(clg_ref), (clg_ref)->type))

#define LOG_CHECK(log_ref, verbose_level, ...) \
  ((void)LOG_ENSURE(log_ref), \
   ((Log_ref)->type->flag & LOG_FLAG_USE) && ((log_ref)->type->level >= verbose_level))

#define LOG_AT_SEVERITY(log_ref, severity, verbose_level, ...) \
  { \
    LogType *_log_type = LOG_ENSURE(log_ref); \
    if (((_lg_ty->flag & LOG_FLAG_USE) && (_lg_ty->level >= verbose_level)) || \
        (severity >= CLG_SEVERITY_WARN)) { \
      logf(_lg_ty, severity, __FILE__ ":" STRINGIFY(__LINE__), __func__, __VA_ARGS__); \
    } \
  } \
  ((void)0)

#define LOG_STR_AT_SEVERITY(log_ref, severity, verbose_level, str) \
  { \
    LogType *_lg_ty = LOG_ENSURE(clg_ref); \
    if (((_lg_ty->flag & LOG_FLAG_USE) && (_lg_ty->level >= verbose_level)) || \
        (severity >= LOG_SEVERITY_WARN)) { \
      log_str(_lg_ty, severity, __FILE__ ":" STRINGIFY(__LINE__), __func__, str); \
    } \
  } \
  ((void)0)

#define LOG_STR_AT_SEVERITY_N(log_ref, severity, verbose_level, str) \
  { \
    LogType *_lg_ty = LOG_ENSURE(clg_ref); \
    if (((_lg_ty->flag & LOG_FLAG_USE) && (_lg_ty->level >= verbose_level)) || \
        (severity >= LOG_SEVERITY_WARN)) { \
      const char *_str = str; \
      log_str(_lg_ty, severity, __FILE__ ":" STRINGIFY(__LINE__), __func__, _str); \
      mem_freen((void *)_str); \
    } \
  } \
  ((void)0)

#define LOG_INFO(clg_ref, level, ...) \
  LOG_AT_SEVERITY(clg_ref, LOG_SEVERITY_INFO, level, __VA_ARGS__)
#define LOG_WARN(clg_ref, ...) LOG_AT_SEVERITY(log_ref, LOG_SEVERITY_WARN, 0, __VA_ARGS__)
#define LOG_ERROR(clg_ref, ...) LOG_AT_SEVERITY(log_ref, LOG_SEVERITY_ERROR, 0, __VA_ARGS__)
#define LOG_FATAL(clg_ref, ...) LOG_AT_SEVERITY(log_ref, LOG_SEVERITY_FATAL, 0, __VA_ARGS__)

#define LOG_STR_INFO(clg_ref, level, str) \
  LOG_STR_AT_SEVERITY(clg_ref, CLG_SEVERITY_INFO, level, str)
#define LOG_STR_WARN(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, LOG_SEVERITY_WARN, 0, str)
#define LOG_STR_ERROR(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, LOG_SEVERITY_ERROR, 0, str)
#define LOG_STR_FATAL(clg_ref, str) CLOG_STR_AT_SEVERITY(clg_ref, LOG_SEVERITY_FATAL, 0, str)

/* Allocated string which is immediately freed. */
#define LOG_STR_INFO_N(log_ref, level, str) \
  LOG_STR_AT_SEVERITY_N(log_ref, LOG_SEVERITY_INFO, level, str)
#define LOG_STR_WARN_N(log_ref, str) LOG_STR_AT_SEVERITY_N(log_ref, LOG_SEVERITY_WARN, 0, str)
#define LOG_STR_ERROR_N(log_ref, str) LOG_STR_AT_SEVERITY_N(log_ref, LOG_SEVERITY_ERROR, 0, str)
#define LOG_STR_FATAL_N(log_ref, str) LOG_STR_AT_SEVERITY_N(log_ref, LOG_SEVERITY_FATAL, 0, str)

#ifdef __cplusplus
}
#endif

#endif /* __CLG_LOG_H__ */
