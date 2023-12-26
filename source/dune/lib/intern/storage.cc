/* Some really low-level file ops. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <sys/stat.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__HAIKU__)
/* Other modern unix OS's should probably use this also. */
#  include <sys/statvfs.h>
#  define USE_STATFS_STATVFS
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
/* For statfs */
#  include <sys/mount.h>
#  include <sys/param.h>
#endif

#if defined(__linux__) || defined(__hpux) || defined(__GNU__) || defined(__GLIBC__)
#  include <sys/vfs.h>
#endif

#include <fcntl.h>
#include <string.h>

#ifdef WIN32
#  include "lib_string_utf8.h"
#  include "lib_winstuff.h"
#  include "utfconv.hh"
#  include <ShObjIdl.h>
#  include <direct.h>
#  include <io.h>
#  include <stdbool.h>
#else
#  include <pwd.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

/* lib includes */
#include "mem_guardedalloc.h"

#include "lib_fileops.h"
#include "lib_linklist.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

/* The implementation for Apple lives in storage_apple.mm. */
#if !defined(__APPLE__)
bool lib_change_working_dir(const char *dir)
{
  lib_assert(lib_thread_is_main());

  if (!lib_is_dir(dir)) {
    return false;
  }
#  if defined(WIN32)
  wchar_t wdir[FILE_MAX];
  if (conv_utf_8_to_16(dir, wdir, ARRAY_SIZE(wdir)) != 0) {
    return false;
  }
  return _wchdir(wdir) == 0;
#  else
  int result = chdir(dir);
  if (result == 0) {
    lib_setenv("PWD", dir);
  }
  return result == 0;
#  endif
}

char *lib_current_working_dir(char *dir, const size_t maxncpy)
{
#  if defined(WIN32)
  wchar_t path[MAX_PATH];
  if (_wgetcwd(path, MAX_PATH)) {
    if (lib_strncpy_wchar_as_utf8(dir, path, maxncpy) != maxncpy) {
      return dir;
    }
  }
  return NULL;
#  else
  const char *pwd = lib_getenv("PWD");
  if (pwd) {
    size_t srclen = lib_strnlen(pwd, maxncpy);
    if (srclen != maxncpy) {
      memcpy(dir, pwd, srclen + 1);
      return dir;
    }
    return nullptr;
  }
  return getcwd(dir, maxncpy);
#  endif
}
#endif /* !defined (__APPLE__) */

double lib_dir_free_space(const char *dir)
{
#ifdef WIN32
  DWORD sectorspc, bytesps, freec, clusters;
  char tmp[4];

  tmp[0] = '\\';
  tmp[1] = 0; /* Just a fail-safe. */
  if (ELEM(dir[0], '/', '\\')) {
    tmp[0] = '\\';
    tmp[1] = 0;
  }
  else if (dir[1] == ':') {
    tmp[0] = dir[0];
    tmp[1] = ':';
    tmp[2] = '\\';
    tmp[3] = 0;
  }

  GetDiskFreeSpace(tmp, &sectorspc, &bytesps, &freec, &clusters);

  return (double)(freec * bytesps * sectorspc);
#else

#  ifdef USE_STATFS_STATVFS
  struct statvfs disk;
#  else
  struct statfs disk;
#  endif

  char dirname[FILE_MAXDIR], *slash;
  int len = strlen(dir);

  if (len >= FILE_MAXDIR) {
    /* path too long */
    return -1;
  }

  memcpy(dirname, dir, len + 1);

  if (len) {
    slash = strrchr(dirname, '/');
    if (slash) {
      slash[1] = '\0';
    }
  }
  else {
    dirname[0] = '/';
    dirname[1] = '\0';
  }

#  if defined(USE_STATFS_STATVFS)
  if (statvfs(dirname, &disk)) {
    return -1;
  }
#  elif defined(USE_STATFS_4ARGS)
  if (statfs(dirname, &disk, sizeof(struct statfs), 0)) {
    return -1;
  }
#  else
  if (statfs(dirname, &disk)) {
    return -1;
  }
#  endif

  return double(disk.f_bsize) * double(disk.f_bfree);
#endif
}

int64_t lib_ftell(FILE *stream)
{
#ifdef WIN32
  return _ftelli64(stream);
#else
  return ftell(stream);
#endif
}

int lib_fseek(FILE *stream, int64_t offset, int whence)
{
#ifdef WIN32
  return _fseeki64(stream, offset, whence);
#else
  return fseek(stream, offset, whence);
#endif
}

int64_t lib_lseek(int fd, int64_t offset, int whence)
{
#ifdef WIN32
  return _lseeki64(fd, offset, whence);
#else
  return lseek(fd, offset, whence);
#endif
}

size_t lib_file_descriptor_size(int file)
{
  Lib_stat_t st;
  if ((file < 0) || (lib_fstat(file, &st) == -1)) {
    return -1;
  }
  return st.st_size;
}

size_t lib_file_size(const char *path)
{
  Lib_stat_t stats;
  if (lib_stat(path, &stats) == -1) {
    return -1;
  }
  return stats.st_size;
}

/* Return file attributes. Apple version of this fn is defined in storage_apple.mm */
#ifndef __APPLE__
eFileAttributes lib_file_attributes(const char *path)
{
  int ret = 0;

#  ifdef WIN32

  if (lib_path_extension_check(path, ".lnk")) {
    return FILE_ATTR_ALIAS;
  }

  WCHAR wline[FILE_MAXDIR];
  if (conv_utf_8_to_16(path, wline, ARRAY_SIZE(wline)) != 0) {
    return eFileAttributes(ret);
  }

  DWORD attr = GetFileAttributesW(wline);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    lib_assert_msg(GetLastError() != ERROR_FILE_NOT_FOUND,
                   "lib_file_attributes should only be called on existing files.");
    return eFileAttributes(ret);
  }

  if (attr & FILE_ATTRIBUTE_READONLY) {
    ret |= FILE_ATTR_READONLY;
  }
  if (attr & FILE_ATTRIBUTE_HIDDEN) {
    ret |= FILE_ATTR_HIDDEN;
  }
  if (attr & FILE_ATTRIBUTE_SYS) {
    ret |= FILE_ATTR_SYS;
  }
  if (attr & FILE_ATTRIBUTE_ARCHIVE) {
    ret |= FILE_ATTR_ARCHIVE;
  }
  if (attr & FILE_ATTRIBUTE_COMPRESSED) {
    ret |= FILE_ATTR_COMPRESSED;
  }
  if (attr & FILE_ATTRIBUTE_ENCRYPTED) {
    ret |= FILE_ATTR_ENCRYPTED;
  }
  if (attr & FILE_ATTRIBUTE_TMP) {
    ret |= FILE_ATTR_TMP;
  }
  if (attr & FILE_ATTRIBUTE_SPARSE_FILE) {
    ret |= FILE_ATTR_SPARSE_FILE;
  }
  if (attr & FILE_ATTRIBUTE_OFFLINE || attr & FILE_ATTRIBUTE_RECALL_ON_OPEN ||
      attr & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)
  {
    ret |= FILE_ATTR_OFFLINE;
  }
  if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    ret |= FILE_ATTR_REPARSE_POINT;
  }

#  else

  UNUSED_VARS(path);

  /* If Immutable set FILE_ATTR_READONLY
   * If Archived set FILE_ATTR_ARCHIVE */
#  endif
  return eFileAttributes(ret);
}
#endif

/* Return alias/shortcut file target. Apple version is defined in storage_apple.mm */
#ifndef __APPLE__
bool lib_file_alias_target(const char *filepath,
                           /* This param can only be `const` on Linux since
                            * redirection is not supported there.
                            * NOLINTNEXTLINE: readability-non-const-param. */
                           char r_targetpath[/*FILE_MAXDIR*/])
{
#  ifdef WIN32
  if (!lib_path_extension_check(filepath, ".lnk")) {
    return false;
  }

  HRESULT hr = CoInitEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    return false;
  }

  IShellLinkW *Shortcut = NULL;
  hr = CoCreateInstance(
      CLSID_ShellLink, NULL, CLSCXT_INPROC_SERVER, IID_IShellLinkW, (LPVOID *)&Shortcut);

  bool success = false;
  if (SUCCEEDED(hr)) {
    IPersistFile *PersistFile;
    hr = Shortcut->QueryInterface(IID_IPersistFile, (LPVOID *)&PersistFile);
    if (SUCCEEDED(hr)) {
      WCHAR path_utf16[FILE_MAXDIR] = {0};
      if (conv_utf_8_to_16(filepath, path_utf16, ARRAY_SIZE(path_utf16)) == 0) {
        hr = PersistFile->Load(path_utf16, STGM_READ);
        if (SUCCEEDED(hr)) {
          hr = Shortcut->Resolve(0, SLR_NO_UI | SLR_UPDATE);
          if (SUCCEEDED(hr)) {
            wchar_t target_utf16[FILE_MAXDIR] = {0};
            hr = Shortcut->GetPath(target_utf16, FILE_MAXDIR, NULL, 0);
            if (SUCCEEDED(hr)) {
              success = (conv_utf_16_to_8(target_utf16, r_targetpath, FILE_MAXDIR) == 0);
            }
          }
          PersistFile->Release();
        }
      }
    }
    Shortcut->Release();
  }

  CoUninit();
  return (success && r_targetpath[0]);
#  else
  UNUSED_VARS(r_targetpath, filepath);
  /* File-based redirection not supported. */
  return false;
#  endif
}
#endif

int lib_exists(const char *path)
{
#if defined(WIN32)
  Lib_stat_t st;
  wchar_t *tmp_16 = alloc_utf16_from_8(path, 1);
  int len, res;

  len = wcslen(tmp_16);
  /* in Windows stat doesn't recognize dir ending on a slash
   * so we remove it here */
  if ((len > 3) && ELEM(tmp_16[len - 1], L'\\', L'/')) {
    tmp_16[len - 1] = '\0';
  }
  /* two special cases where the trailing slash is needed:
   * 1. after the share part of a UNC path
   * 2. after the C:\ when the path is the volume only */
  if ((len >= 3) && (tmp_16[0] == L'\\') && (tmp_16[1] == L'\\')) {
    lib_path_normalize_unc_16(tmp_16);
  }

  if ((tmp_16[1] == L':') && (tmp_16[2] == L'\0')) {
    tmp_16[2] = L'\\';
    tmp_16[3] = L'\0';
  }

  res = lib_wstat(tmp_16, &st);

  free(tmp_16);
  if (res == -1) {
    return 0;
  }
#else
  struct stat st;
  lib_assert(!lib_path_is_rel(path));
  if (stat(path, &st)) {
    return 0;
  }
#endif
  return (st.st_mode);
}

#ifdef WIN32
int lib_fstat(int fd, Lib_stat_t *buf)
{
#  if defined(_MSC_VER)
  return _fstat64(fd, buf);
#  else
  return _fstat(fd, buf);
#  endif
}

int lib_stat(const char *path, lib_stat_t *buf)
{
  int r;
  UTF16_ENCODE(path);

  r = lib_wstat(path_16, buf);

  UTF16_UN_ENCODE(path);
  return r;
}

int lib_wstat(const wchar_t *path, Lib_Stat_t *buf)
{
#  if defined(_MSC_VER)
  return _wstat64(path, buf);
#  else
  return _wstat(path, buf);
#  endif
}
#else
int lib_fstat(int fd, struct stat *buf)
{
  return fstat(fd, buf);
}

int lib_stat(const char *path, struct stat *buf)
{
  return stat(path, buf);
}
#endif

bool lib_is_dir(const char *file)
{
  return S_ISDIR(lib_exists(file));
}

bool lib_is_file(const char *path)
{
  const int mode = lib_exists(path);
  return (mode && !S_ISDIR(mode));
}

/* Use for both txt and binary file reading. */
void *lib_file_read_data_as_mem_from_handle(FILE *fp,
                                            bool read_size_exact,
                                            size_t pad_bytes,
                                            size_t *r_size)
{
  lib_stat_t st;
  if (lib_fstat(fileno(fp), &st) == -1) {
    return nullptr;
  }
  if (S_ISDIR(st.st_mode)) {
    return nullptr;
  }
  if (lib_fseek(fp, 0L, SEEK_END) == -1) {
    return nullptr;
  }
  /* Don't use the 'st_size' bc it may be the symlink. */
  const long int filelen = lib_ftell(fp);
  if (filelen == -1) {
    return nullptr;
  }
  if (lib_fseek(fp, 0L, SEEK_SET) == -1) {
    return nullptr;
  }

  void *mem = mem_malloc(filelen + pad_bytes, __func__);
  if (mem == nullptr) {
    return nullptr;
  }

  const long int filelen_read = fread(mem, 1, filelen, fp);
  if ((filelen_read < 0) || ferror(fp)) {
    mem_free(mem);
    return nullptr;
  }

  if (read_size_exact) {
    if (filelen_read != filelen) {
      mem_free(mem);
      return nullptr;
    }
  }
  else {
    if (filelen_read < filelen) {
      mem = mem_realloc(mem, filelen_read + pad_bytes);
      if (mem == nullptr) {
        return nullptr;
      }
    }
  }

  *r_size = filelen_read;

  return mem;
}

void *lib_file_read_txt_as_mem(const char *filepath, size_t pad_bytes, size_t *r_size)
{
  FILE *fp = lib_fopen(filepath, "r");
  void *mem = nullptr;
  if (fp) {
    mem = lib_file_read_data_as_mem_from_handle(fp, false, pad_bytes, r_size);
    fclose(fp);
  }
  return mem;
}

void *lib_file_read_binary_as_mem(const char *filepath, size_t pad_bytes, size_t *r_size)
{
  FILE *fp = lib_fopen(filepath, "rb");
  void *mem = nullptr;
  if (fp) {
    mem = lib_file_read_data_as_mem_from_handle(fp, true, pad_bytes, r_size);
    fclose(fp);
  }
  return mem;
}

void *lib_file_read_txt_as_mem_with_newline_as_nil(const char *filepath,
                                                    bool trim_trailing_space,
                                                    size_t pad_bytes,
                                                    size_t *r_size)
{
  char *mem = static_cast<char *>(lib_file_read_txt_as_mem(filepath, pad_bytes, r_size));
  if (mem != nullptr) {
    char *mem_end = mem + *r_size;
    if (pad_bytes != 0) {
      *mem_end = '\0';
    }
    for (char *p = mem, *p_next; p != mem_end; p = p_next) {
      p_next = static_cast<char *>(memchr(p, '\n', mem_end - p));
      if (p_next != nullptr) {
        if (trim_trailing_space) {
          for (char *p_trim = p_next - 1; p_trim > p && ELEM(*p_trim, ' ', '\t'); p_trim--) {
            *p_trim = '\0';
          }
        }
        *p_next = '\0';
        p_next++;
      }
      else {
        p_next = mem_end;
      }
    }
  }
  return mem;
}

LinkNode *lib_file_read_as_lines(const char *filepath)
{
  FILE *fp = lib_fopen(filepath, "r");
  LinkNodePair lines = {nullptr, nullptr};
  char *buf;
  size_t size;

  if (!fp) {
    return nullptr;
  }

  lib_fseek(fp, 0, SEEK_END);
  size = size_t(lib_ftell(fp));
  lib_fseek(fp, 0, SEEK_SET);

  if (UNLIKELY(size == size_t(-1))) {
    fclose(fp);
    return nullptr;
  }

  buf = mem_cnew_array<char>(size, "file_as_lines");
  if (buf) {
    size_t i, last = 0;

    /* size = bc on win32 reading
     * all the bytes in the file will return
     * less bytes bc of `CRNL` changes. */
    size = fread(buf, 1, size, fp);
    for (i = 0; i <= size; i++) {
      if (i == size || buf[i] == '\n') {
        char *line = lib_strdupn(&buf[last], i - last);
        lib_linklist_append(&lines, line);
        last = i + 1;
      }
    }

    mem_free(buf);
  }

  fclose(fp);

  return lines.list;
}

void lib_file_free_lines(LinkNode *lines)
{
  lib_linklist_free(lines);
}

bool lib_file_older(const char *file1, const char *file2)
{
  Lib_stat_t st1, st2;
  if (lib_stat(file1, &st1)) {
    return false;
  }
  if (lib_stat(file2, &st2)) {
    return false;
  }
  return (st1.st_mtime < st2.st_mtime);
}
