#include <stdlib.h> /* malloc */
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>

#include <zlib.h>
#include <zstd.h>

#ifdef WIN32
#  include "lib_fileops_types.h"
#  include "lib_winstuff.h"
#  include "utf_winfunc.hh"
#  include "utfconv.hh"
#  include <io.h>
#  include <shellapi.h>
#  include <shobjidl.h>
#  include <windows.h>
#else
#  if defined(__APPLE__)
#    include <CoreFoundation/CoreFoundation.h>
#    include <objc/message.h>
#    include <objc/runtime.h>
#  endif
#  include <dirent.h>
#  include <sys/param.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include "mem_guardedalloc.h"

#include "lib_fileops.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_string_utils.hh"
#include "lib_sys_types.h" /* for intptr_t support */
#include "lib_utildefines.h"

/* Sizes above this must be alloc. */
#define FILE_MAX_STATIC_BUF 256

#ifdef WIN32
/* Text string used as the "verb" for Windows shell ops. */
static const char *windows_op_string(FileExternalOp op)
{
  switch (op) {
    case FILE_EXTERNAL_OP_OPEN:
      return "open";
    case FILE_EXTERNAL_OP_FOLDER_OPEN:
      return "open";
    case FILE_EXTERNAL_OP_EDIT:
      return "edit";
    case FILE_EXTERNAL_OP_NEW:
      return "new";
    case FILE_EXTERNAL_OP_FIND:
      return "find";
    case FILE_EXTERNAL_OP_SHOW:
      return "show";
    case FILE_EXTERNAL_OP_PLAY:
      return "play";
    case FILE_EXTERNAL_OP_BROWSE:
      return "browse";
    case FILE_EXTERNAL_OP_PREVIEW:
      return "preview";
    case FILE_EXTERNAL_OP_PRINT:
      return "print";
    case FILE_EXTERNAL_OP_INSTALL:
      return "install";
    case FILE_EXTERNAL_OP_RUNAS:
      return "runas";
    case FILE_EXTERNAL_OP_PROPS:
      return "properties";
    case FILE_EXTERNAL_OP_FOLDER_FIND:
      return "find";
    case FILE_EXTERNAL_OP_FOLDER_CMD:
      return "cmd";
  }
  lib_assert_unreachable();
  return "";
}
#endif

int64_t lib_read(int fd, void *buf, size_t nbytes)
{
  /* Define our own read as `read` is not guaranteed to read the num of bytes requested.
   * This happens rarely but was observed with larger than 2GB files on Linux, see: #113473.
   *
   * Though this is a loop, the most common code-path will exit with "Success" case.
   * In the case where read more data than the file contains, it will loop twice,
   * exiting on EOF on the second iter. */
  int64_t nbytes_read_total = 0;
  while (true) {
    int64_t nbytes_read = read(fd,
                               buf,
#ifdef WIN32
                               /* Read must not exceed INT_MAX on WIN32, clamp. */
                               MIN2(nbytes, INT_MAX)
#else
                               nbytes
#endif
    );
    if (nbytes_read == nbytes) {
      /* Success (common case). */
      return nbytes_read_total + nbytes_read;
    }
    if (nbytes_read == 0) {
      /* EOF (common case for the second iteration when reading more data than `fd` contains). */
      return nbytes_read_total;
    }
    if (nbytes_read < 0) {
      /* Error. */
      return nbytes_read;
    }

    if (UNLIKELY(nbytes_read > nbytes)) {
      /* Badly behaving LIBC, reading more bytes than requested should never happen.
       * Possibly an invalid internal state/corruption, only check to prevent an eternal loop. */
      lib_assert_unreachable();
      /* Set the IO-error so there is some indication an error occurred. */
      if (errno == 0) {
        errno = EIO;
      }
      return -1;
    }

    /* If this is reached, fewer bytes were read than were requested. */
    buf = (void *)(((char *)buf) + nbytes_read);
    nbytes_read_total += nbytes_read;
    nbytes -= nbytes_read;
  }
}

bool lib_file_external_op_supported(const char *filepath, FileExternalOp op)
{
#ifdef WIN32
  const char *opstring = windows_op_string(op);
  return lib_windows_external_op_supported(filepath, opstring);
#else
  UNUSED_VARS(filepath, op);
  return false;
#endif
}

bool lib_file_external_op_ex(const char *filepath, FileExternalOperation operation)
{
#ifdef WIN32
  const char *opstring = wins_op_string(op);
  if (lib_windows_external_op_supported(filepath, opstring) &&
      lib_windows_external_op_ex(filepath, opstring))
  {
    return true;
  }
  return false;
#else
  UNUSED_VARS(filepath, operation);
  return false;
#endif
}

size_t lib_file_zstd_from_mem_at_pos(
    void *buf, size_t len, FILE *file, size_t file_offset, int compression_level)
{
  fseek(file, file_offset, SEEK_SET);

  ZSTD_CCxt *cxt = ZSTD_createCCxt();
  ZSTD_CCxt_setParam(cxt, ZSTD_c_compressionLevel, compression_level);

  ZSTD_inBuf input = {buf, len, 0};

  size_t out_len = ZSTD_CStreamOutSize();
  void *out_buf = mem_malloc(out_len, __func__);
  size_t total_written = 0;

  /* Compress block and write it out until the input has been consumed. */
  while (input.pos < input.size) {
    ZSTD_outBuf output = {out_buf, out_len, 0};
    size_t ret = ZSTD_compressStream2(cxt, &output, &input, ZSTD_e_continue);
    if (ZSTD_isError(ret)) {
      break;
    }
    if (fwrite(out_buf, 1, output.pos, file) != output.pos) {
      break;
    }
    total_written += output.pos;
  }

  /* Finalize the `Zstd` frame. */
  size_t ret = 1;
  while (ret != 0) {
    ZSTD_outBuf output = {out_buf, out_len, 0};
    ret = ZSTD_compressStream2(cxt, &output, &input, ZSTD_e_end);
    if (ZSTD_isError(ret)) {
      break;
    }
    if (fwrite(out_buf, 1, output.pos, file) != output.pos) {
      break;
    }
    total_written += output.pos;
  }

  mem_free(out_buf);
  ZSTD_freeCCxt(cxt);

  return ZSTD_isError(ret) ? 0 : total_written;
}

size_t lib_file_unzstd_to_mem_at_pos(void *buf, size_t len, FILE *file, size_t file_offset)
{
  fseek(file, file_offset, SEEK_SET);

  ZSTD_DCxt *cxt = ZSTD_createDCxt();

  size_t in_len = ZSTD_DStreamInSize();
  void *in_buf = mem_malloc(in_len, __func__);
  ZSTD_inBuf input = {in_buf, in_len, 0};

  ZSTD_outBuf output = {buf, len, 0};

  size_t ret = 0;
  /* Read and decompress chunks of input data until we have enough output. */
  while (output.pos < output.size && !ZSTD_isError(ret)) {
    input.size = fread(in_buf, 1, in_len, file);
    if (input.size == 0) {
      break;
    }

    /* Consume input data until we run out or have enough output. */
    input.pos = 0;
    while (input.pos < input.size && output.pos < output.size) {
      ret = ZSTD_decompressStream(ctx, &output, &input);

      if (ZSTD_isError(ret)) {
        break;
      }
    }
  }

  mem_free(in_buf);
  ZSTD_freeDCxt(cxt);

  return ZSTD_isError(ret) ? 0 : output.pos;
}

bool lib_file_magic_is_gzip(const char header[4])
{
  /* GZIP itself starts with the magic bytes 0x1f 0x8b.
   * The third byte indicates the compression method, which is 0x08 for DEFLATE. */
  return header[0] == 0x1f && header[1] == 0x8b && header[2] == 0x08;
}

bool lib_file_magic_is_zstd(const char header[4])
{
  /* ZSTD files consist of concatenated frames, each either a ZSTD frame or a skippable frame.
   * Both types of frames start w a magic num: `0xFD2FB528` for ZSTD frames and `0x184D2A5`
   * for skippable frames, with the * being anything from 0 to F.
   *
   * To check whether a file is ZSTD-compressed, we just check whether the first frame matches
   * either. Seeking through the file until a ZSTD frame is found would make things more
   * complicated and the probability of a false positive is rather low anyways.
   *
   * Note that LZ4 uses a compatible format, so even though its compressed frames have a
   * different magic number, a valid LZ4 file might also start with a skippable frame matching
   * the second check here.
   *
   * For more details, see https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md */

  uint32_t magic = *((uint32_t *)header);
  if (magic == 0xFD2FB528) {
    return true;
  }
  if ((magic >> 4) == 0x184D2A5) {
    return true;
  }
  return false;
}

bool lib_file_is_writable(const char *filepath)
{
  bool writable;
  if (lib_access(filepath, W_OK) == 0) {
    /* file exists and I can write to it */
    writable = true;
  }
  else if (errno != ENOENT) {
    /* most likely file or containing directory cannot be accessed */
    writable = false;
  }
  else {
    /* file doesn't exist check I can create it in parent directory */
    char parent[FILE_MAX];
    lib_path_split_dir_part(filepath, parent, sizeof(parent));
#ifdef WIN32
    /* windows does not have X_OK */
    writable = lib_access(parent, W_OK) == 0;
#else
    writable = lib_access(parent, X_OK | W_OK) == 0;
#endif
  }
  return writable;
}

bool lib_file_touch(const char *filepath)
{
  FILE *f = lib_fopen(filepath, "r+b");

  if (f != nullptr) {
    int c = getc(f);

    if (c == EOF) {
      /* Empty file, reopen in truncate write mode... */
      fclose(f);
      f = lib_fopen(filepath, "w+b");
    }
    else {
      /* else rewrite 1st byte. */
      rewind(f);
      putc(c, f);
    }
  }
  else {
    f = lib_fopen(filepath, "wb");
  }
  if (f) {
    fclose(f);
    return true;
  }
  return false;
}

static bool dir_create_recursive(char *dirname, int len)
{
  lib_assert(strlen(dirname) == len);
  lib_assert(lib_exists(dirname) == 0);
  /* Caller must ensure the path doesn't have trailing slashes. */
  lib_assert_msg(len && !lib_path_slash_is_native_compat(dirname[len - 1]),
                 "Paths must not end with a slash!");
  lib_assert_msg(!((len >= 3) && lib_path_slash_is_native_compat(dirname[len - 3]) &&
                   STREQ(dirname + (len - 2), "..")),
                 "Paths containing \"..\" components must be normalized first!");

  bool ret = true;
  char *dirname_parent_end = (char *)lib_path_parent_dir_end(dirname, len);
  if (dirname_parent_end) {
    const char dirname_parent_end_val = *dirname_parent_end;
    *dirname_parent_end = '\0';
#ifdef WIN32
    /* Check special case `c:\foo`, don't try create `c:`, harmless but unnecessary. */
    if (dirname[0] && !lib_path_is_win32_drive_only(dirname))
#endif
    {
      const int mode = lib_exists(dirname);
      if (mode != 0) {
        if (!S_ISDIR(mode)) {
          ret = false;
        }
      }
      else if (!dir_create_recursive(dirname, dirname_parent_end - dirname)) {
        ret = false;
      }
    }
    *dirname_parent_end = dirname_parent_end_value;
  }
  if (ret) {
#ifdef WIN32
    if (umkdir(dirname) == -1) {
      ret = false;
    }
#else
    if (mkdir(dirname, 0777) != 0) {
      ret = false;
    }
#endif
  }
  return ret;
}

bool lib_dir_create_recursive(const char *dirname)
{
  const int mode = lib_exists(dirname);
  if (mode != 0) {
    /* The file exists, either it's a directory (ok), or not,
     * in which case this fn can't do anything useful
     * (the caller could remove it and re-run this fn). */
    return S_ISDIR(mode) ? true : false;
  }

  char dirname_static_buf[FILE_MAX];
  char *dirname_mut = dirname_static_buf;

  size_t len = strlen(dirname);
  if (len >= sizeof(dirname_static_buf)) {
    dirname_mut = mem_cnew_array<char>(len + 1, __func__);
  }
  memcpy(dirname_mut, dirname, len + 1);

  /* Strip trailing chars, important for first entering dir_create_recursive
   * when then ensures this is the case for recursive calls. */
  while ((len > 0) && lib_path_slash_is_native_compat(dirname_mut[len - 1])) {
    len--;
  }
  dirname_mut[len] = '\0';

  const bool ret = (len > 0) && dir_create_recursive(dirname_mut, len);

  /* Ensure the string was properly restored. */
  lib_assert(memcmp(dirname, dirname_mut, len) == 0);

  if (dirname_mut != dirname_static_buf) {
    mem_free(dirname_mut);
  }

  return ret;
}

bool lib_file_ensure_parent_dir_exists(const char *filepath)
{
  char di[FILE_MAX];
  lib_path_split_dir_part(filepath, di, sizeof(di));

  /* Make if the dir doesn't exist. */
  return lib_dir_create_recursive(di);
}

int lib_rename(const char *from, const char *to)
{
  if (!lib_exists(from)) {
    return 1;
  }

  /* NOTE: there are no checks that `from` & `to` *aren't* the same file.
   * It's up to the caller to ensure this. In practice these paths are often generated
   * and known to be diff rather than arbitrary user input.
   * In the case of arbitrary paths (renaming a file in the file-sel for example),
   * the caller must ensure file renaming doesn't cause user data loss.
   *
   * Support for checking the files aren't the same could be added, however path comparison
   * alone is *not* a guarantee the files are diff (given the possibility of accessing
   * the same file through different paths via symbolic-links), we could instead support a
   * version of Python's `os.path.samefile(..)` which compares the I-node & device.
   * In this particular case we would not want to follow symbolic-links as well.
   * Since this functionality isn't required at the moment, leave this as-is.
   * Noting it as a potential improvement. */

  /* To avoid the concurrency 'time of check/time of use' (TOC/TOU) issue, this code attempts
   * to use available solutions for an 'atomic' (file-sys wise) rename op, instead of
   * 1st checking for an existing `to` target path, and then doing the rename op if it
   * does not exists at the time of check.
   *
   * Windows (through `MoveFileExW`) by default does not allow replacing an existing path. It is
   * however not clear whether its API is exposed to the TOC/TOU issue or not.
   *
   * On Linux or OSX, to keep ops atomic, special non-standardized variants of `rename` must
   * be used, depending on the OS. There may also be failure due to file sys not
   * supporting this op, in practice this should not be a problem in modern
   * systems.
   *   - https://man7.org/linux/man-pages/man2/rename.2.html
   *   - https://www.unix.com/man-page/mojave/2/renameatx_np/
   *
   * BSD systems do not have any such thing currently, and are therefore exposed to the TOC/TOU
   * issue. */

#ifdef WIN32
  return urename(from, to, false);
#elif defined(__APPLE__)
  return renamex_np(from, to, RENAME_EXCL);
#elif defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 28)
  /* Most common Linux cases. */
  return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
#  endif
#else
  /* At least all BSD's currently. */
  if (lib_exists(to)) {
    return 1;
  }
  return rename(from, to);
#endif
}

int lib_rename_overwrite(const char *from, const char *to)
{
  if (!lib_exists(from)) {
    return 1;
  }

#ifdef WIN32
  /* `urename` from `utfconv` intern utils uses `MoveFileExW`, which allows to replace an existing
   * file, but not an existing directory, even if empty. This will only delete empty directories. */
  if (lib_is_dir(to)) {
    if (lib_delete(to, true, false)) {
      return 1;
    }
  }
  return urename(from, to, true);
#else
  return rename(from, to);
#endif
}

#ifdef WIN32

static void callLocalErrorCb(const char *err)
{
  printf("%s\n", err);
}

FILE *lib_fopen(const char *filepath, const char *mode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return ufopen(filepath, mode);
}

void lib_get_short_name(char short_name[256], const char *filepath)
{
  wchar_t short_name_16[256];
  int i = 0;

  UTF16_ENCODE(filepath);

  GetShortPathNameW(filepath_16, short_name_16, 256);

  for (i = 0; i < 256; i++) {
    short_name[i] = (char)short_name_16[i];
  }

  UTF16_UN_ENCODE(filepath);
}

void *lib_gzopen(const char *filepath, const char *mode)
{
  gzFile gzfile;

  lib_assert(!lib_path_is_rel(filepath));

  /* Creates file before transcribing the path. */
  if (mode[0] == 'w') {
    FILE *file = ufopen(filepath, "a");
    if (file == NULL) {
      /* File couldn't be opened, e.g. due to permission error. */
      return NULL;
    }
    fclose(file);
  }

  /* tmp #if until we update all libs to 1.2.7
   * for correct wide char path handling */
#  if ZLIB_VERNUM >= 0x1270
  UTF16_ENCODE(filepath);

  gzfile = gzopen_w(filepath_16, mode);

  UTF16_UN_ENCODE(filepath);
#  else
  {
    char short_name[256];
    lib_get_short_name(short_name, filepath);
    gzfile = gzopen(short_name, mode);
  }
#  endif

  return gzfile;
}

int lib_open(const char *filepath, int oflag, int pmode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return uopen(filepath, oflag, pmode);
}

int lib_access(const char *filepath, int mode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return uaccess(filepath, mode);
}

static bool del_soft(const wchar_t *path_16, const char **err_msg)
{
  /* Dels file or dir to recycling bin. The latter moves all contained files and
   * dirs recursively to the recycling bin as well. */
  IFileOp *pfo;
  IShellItem *psi;

  HRESULT hr = CoInitEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  if (SUCCEEDED(hr)) {
    /* This is also the case when COM was prev init and CoInitEx returns
     * S_FALSE, which is not an err. Both HRESULT vals S_OK and S_FALSE indicate success. */

    hr = CoCreateInstance(
        CLSID_FileOp, NULL, CLSCXT_ALL, IID_IFileOp, (void **)&pfo);

    if (SUCCEEDED(hr)) {
      /* Flags for deletion:
       * FOF_ALLOWUNDO: Enables moving file to recycling bin.
       * FOF_SILENT: Don't show progress dialog box.
       * FOF_WANTNUKEWARNING: Show dialog box if file can't be moved to recycling bin. */
      hr = pfo->SetOpFlags(FOF_ALLOWUNDO | FOF_SILENT | FOF_WANTNUKEWARNING);

      if (SUCCEEDED(hr)) {
        hr = SHCreateItemFromParsingName(path_16, NULL, IID_IShellItem, (void **)&psi);

        if (SUCCEEDED(hr)) {
          hr = pfo->DeleteItem(psi, NULL);

          if (SUCCEEDED(hr)) {
            hr = pfo->PerformOps();

            if (FAILED(hr)) {
              *err_msg = "Failed to prepare del op";
            }
          }
          else {
            *err_msg = "Failed to prep del op";
          }
          psi->Release();
        }
        else {
          *err_msg = "Failed to parse path";
        }
      }
      else {
        *err_msg = "Failed to set op flags";
      }
      pfo->Release();
    }
    else {
      *err_msg = "Failed to create FileOp instance";
    }
    CoUninit();
  }
  else {
    *err_msg = "Failed to init COM";
  }

  return FAILED(hr);
}

static bool del_unique(const char *path, const bool dir)
{
  bool err;

  UTF16_ENCODE(path);

  if (dir) {
    err = !RemoveDirectoryW(path_16);
    if (err) {
      printf("Unable to remove directory\n");
    }
  }
  else {
    err = !DeleteFileW(path_16);
    if (err) {
      callLocalErrCb("Unable to del file");
    }
  }

  UTF16_UN_ENCODE(path);

  return err;
}

static bool del_recursive(const char *dir)
{
  struct direntry *filelist, *fl;
  bool err = false;
  uint filelist_num, i;

  i = filelist_num = lib_filelist_dir_contents(dir, &filelist);
  fl = filelist;
  while (i--) {
    if (FILENAME_IS_CURRPAR(fl->relname)) {
      /* Skip! */
    }
    else if (S_ISDIR(fl->type)) {
      char path[FILE_MAXDIR];

      /* dir listing produces dir path wo trailing slash... */
      STRNCPY(path, fl->path);
      lib_path_slash_ensure(path, sizeof(path));

      if (del_recursive(path)) {
        err = true;
      }
    }
    else {
      if (del_unique(fl->path, false)) {
        err = true;
      }
    }
    fl++;
  }

  if (!err && del_unique(dir, true)) {
    err = true;
  }

  lib_filelist_free(filelist, filelist_num);

  return err;
}

int lib_del(const char *path, bool dir, bool recursive)
{
  int err;

  lib_assert(!lib_path_is_rel(path));

  if (recursive) {
    err = del_recursive(path);
  }
  else {
    err = del_unique(path, dir);
  }

  return err;
}

/* Moves the files or dirs to the recycling bin. */
int lib_del_soft(const char *file, const char **err_msg)
{
  int err;

  lib_assert(!lib_path_is_rel(file));

  UTF16_ENCODE(file);

  err = del_soft(file_16, err_msg);

  UTF16_UN_ENCODE(file);

  return err;
}

/* MS-Windows doesn't support moving to a directory, it has to be
 * `mv filepath filepath` and not `mv filepath destination_directory` (same for copying).
 *
 * When `path_dst` ends w as slash:
 * ensure the filename component of `path_src` is added to a copy of `path_dst`. */
static const char *path_destination_ensure_filename(const char *path_src,
                                                    const char *path_dst,
                                                    char *buf,
                                                    size_t buf_size)
{
  const char *filename_src = lib_path_basename(path_src);
  /* Unlikely but possible this has no slashes. */
  if (filename_src != path_src) {
    const size_t path_dst_len = strlen(path_dst);
    /* Check if `path_dst` points to a directory. */
    if (path_dst_len && lib_path_slash_is_native_compat(path_dst[path_dst_len - 1])) {
      size_t buf_size_needed = path_dst_len + strlen(filename_src) + 1;
      char *path_dst_with_filename = (buf_size_needed <= buf_size) ?
                                         buf :
                                         mem_cnew_array<char>(buf_size_needed, __func__);
      lib_string_join(path_dst_with_filename, buf_size_needed, path_dst, filename_src);
      return path_dst_with_filename;
    }
  }
  return path_dst;
}

int lib_path_move(const char *path_src, const char *path_dst)
{
  char path_dst_buf[FILE_MAX_STATIC_BUF];
  const char *path_dst_with_filename = path_destination_ensure_filename(
      path_src, path_dst, path_dst_buf, sizeof(path_dst_buf));

  int err;

  UTF16_ENCODE(path_src);
  UTF16_ENCODE(path_dst_with_filename);
  err = !MoveFileW(path_src_16, path_dst_with_filename_16);
  UTF16_UN_ENCODE(path_dst_with_filename);
  UTF16_UN_ENCODE(path_src);

  if (err) {
    callLocalErrCb("Unable to move file");
    printf(" Move from '%s' to '%s' failed\n", path_src, path_dst_with_filename);
  }

  if (!ELEM(path_dst_with_filename, path_dst_buf, path_dst)) {
    mem_free((void *)path_dst_with_filename);
  }

  return err;
}

int lib_copy(const char *path_src, const char *path_dst)
{
  char path_dst_buf[FILE_MAX_STATIC_BUF];
  const char *path_dst_with_filename = path_destination_ensure_filename(
      path_src, path_dst, path_dst_buf, sizeof(path_dst_buf));
  int err;

  UTF16_ENCODE(path_src);
  UTF16_ENCODE(path_dst_with_filename);
  err = !CopyFileW(path_src_16, path_dst_with_filename_16, false);
  UTF16_UN_ENCODE(path_dst_with_filename);
  UTF16_UN_ENCODE(path_src);

  if (err) {
    callLocalErrorCb("Unable to copy file!");
    printf(" Copy from '%s' to '%s' failed\n", path_src, path_dst_with_filename);
  }

  if (!ELEM(path_dst_with_filename, path_dst_buf, path_dst)) {
    mem_free((void *)path_dst_with_filename);
  }

  return err;
}

#  if 0
int lib_create_symlink(const char *path_src, const char *path_dst)
{
  /* See patch from #30870, should this ever become needed. */
  callLocalErrCb("Linking files is unsupported on Windows");
  (void)path_src;
  (void)path_dst;
  return 1;
}
#  endif

#else /* The UNIX world */

/* results from recursive_operation and its callbacks */
enum {
  /* op succeeded */
  RecursiveOpCb_OK = 0,

  /* op requested not to perform recursive digging for current path */
  RecursiveOpCb_StopRecurs = 1,

  /* error occurred in cb and recursive walking should stop immediately */
  RecursiveOpCb_Err = 2,
};

typedef int (*RecursiveOpCb)(const char *from, const char *to);

/* Append `file` to `dir` (ensures for buf size before appending).
 * param dst: The destination mem (alloc by `malloc`). */
static void join_dirfile_alloc(char **dst, size_t *alloc_len, const char *dir, const char *file)
{
  size_t len = strlen(dir) + strlen(file) + 1;

  if (*dst == nullptr) {
    *dst = static_cast<char *>(malloc(len + 1));
  }
  else if (*alloc_len < len) {
    *dst = static_cast<char *>(realloc(*dst, len + 1));
  }

  *alloc_len = len;

  lib_path_join(*dst, len + 1, dir, file);
}

/* Scans startfrom, generating a corresponding destination name for each item found by
 * prefixing it with startto, recursively scanning subdirectories, and invoking the specified
 * cbs for files and subdirectories found as appropriate.
 *
 * param startfrom: Top-level src path.
 * param startto: Top-level destination path.
 * param cb_dir_pre: Optional, to be invoked before entering a subdir, can return
 *                   RecursiveOpCb_StopRecurs to skip the subdir.
 * param cb_file: Optional, to be invoked on each file found.
 * param cb_dir_post: optional, to be invoked after leaving a subdir.
 * return Zero on success. */
static int recursive_op(const char *startfrom,
                        const char *startto,
                        RecursiveOpCb cb_dir_pre,
                        RecursiveOpCb cb_file,
                        RecursiveOpCb cb_dir_post)
{
  /* This fn must *not* use any `mem_*` fns
   * as it's used to purge tmp files on when the processed is aborted,
   * in this case the `mem_*` state may have alrdy been freed (mem usage tracking for e.g.)
   * causing freed mem access, potentially crashing. This constraint doesn't apply to the
   * cbs themselves - unless they might also be called when aborting. */
  struct stat st;
  char *from = nullptr, *to = nullptr;
  char *from_path = nullptr, *to_path = nullptr;
  size_t from_alloc_len = -1, to_alloc_len = -1;
  int ret = 0;

  dirent **dirlist = nullptr;
  int dirlist_num = 0;

  do { /* once */
    /* ensure there's no trailing slash in file path */
    from = strdup(startfrom);
    lib_path_slash_rstrip(from);
    if (startto) {
      to = strdup(startto);
      lib_path_slash_rstrip(to);
    }

    ret = lstat(from, &st);
    if (ret < 0) {
      /* src wasn't found, nothing to op with */
      break;
    }

    if (!S_ISDIR(st.st_mode)) {
      /* src isn't a dir, can't do recursive walking for it,
       * so just call file cb and leave */
      if (cb_file != nullptr) {
        ret = cb_file(from, to);
        if (ret != RecursiveOp_
          Cb_OK) {
          ret = -1;
        }
      }
      break;
    }

    dirlist_num = scandir(startfrom, &dirlist, nullptr, alphasort);
    if (dirlist_num < 0) {
      /* err opening directory for listing */
      perror("scandir");
      ret = -1;
      break;
    }

    if (cb_dir_pre != nullptr) {
      ret = cb_dir_pre(from, to);
      if (ret != RecursiveOpCb_OK) {
        if (ret == RecursiveOpCb_StopRecurs) {
          /* cb requested not to perform recursive walking, not an error */
          ret = 0;
        }
        else {
          ret = -1;
        }
        break;
      }
    }

    for (int i = 0; i < dirlist_num; i++) {
      const dirent *const dirent = dirlist[i];

      if (FILENAME_IS_CURRPAR(dirent->d_name)) {
        continue;
      }

      join_dirfile_alloc(&from_path, &from_alloc_len, from, dirent->d_name);
      if (to) {
        join_dirfile_alloc(&to_path, &to_alloc_len, to, dirent->d_name);
      }

      bool is_dir;

#  ifdef __HAIKU__
      {
        struct stat st_dir;
        lstat(from_path, &st_dir);
        is_dir = S_ISDIR(st_dir.st_mode);
      }
#  else
      is_dir = (dirent->d_type == DT_DIR);
#  endif

      if (is_dir) {
        /* Recurse into sub-dirs. */
        ret = recursive_op(
            from_path, to_path, cb_dir_pre, cb_file, cb_dir_post);
      }
      else if (cb_file != nullptr) {
        ret = cb_file(from_path, to_path);
        if (ret != RecursiveOpCb_OK) {
          ret = -1;
        }
      }

      if (ret != 0) {
        break;
      }
    }
    if (ret != 0) {
      break;
    }

    if (cb_dir_post != nullptr) {
      ret = cb_dir_post(from, to);
      if (ret != RecursiveOpCb_OK) {
        ret = -1;
      }
    }
  } while (false);

  if (dirlist != nullptr) {
    for (int i = 0; i < dirlist_num; i++) {
      free(dirlist[i]);
    }
    free(dirlist);
  }
  if (from_path != nullptr) {
    free(from_path);
  }
  if (to_path != nullptr) {
    free(to_path);
  }
  if (from != nullptr) {
    free(from);
  }
  if (to != nullptr) {
    free(to);
  }

  return ret;
}

static int del_cb_post(const char *from, const char * /*to*/)
{
  if (rmdir(from)) {
    perror("rmdir");

    return RecursiveOpCb_Err;
  }

  return RecursiveOpCb_OK;
}

static int del_single_file(const char *from, const char * /*to*/)
{
  if (unlink(from)) {
    perror("unlink");

    return RecursiveOpCb_Err;
  }

  return RecursiveOpCb_OK;
}

#  ifdef __APPLE__
static int del_soft(const char *file, const char **err_msg)
{
  int ret = -1;

  Class NSAutoreleasePoolClass = objc_getClass("NSAutoreleasePool");
  SEL allocSel = sel_registerName("alloc");
  SEL initSel = sel_registerName("init");
  id poolAlloc = ((id(*)(Class, SEL))objc_msgSend)(NSAutoreleasePoolClass, allocSel);
  id pool = ((id(*)(id, SEL))objc_msgSend)(poolAlloc, initSel);

  Class NSStringClass = objc_getClass("NSString");
  SEL stringWithUTF8StringSel = sel_registerName("stringWithUTF8String:");
  id pathString = ((
      id(*)(Class, SEL, const char *))objc_msgSend)(NSStringClass, stringWithUTF8StringSel, file);

  Class NSFileManagerClass = objc_getClass("NSFileManager");
  SEL defaultManagerSel = sel_registerName("defaultManager");
  id fileManager = ((id(*)(Class, SEL))objc_msgSend)(NSFileManagerClass, defaultManagerSel);

  Class NSURLClass = objc_getClass("NSURL");
  SEL fileURLWithPathSel = sel_registerName("fileURLWithPath:");
  id nsurl = ((id(*)(Class, SEL, id))objc_msgSend)(NSURLClass, fileURLWithPathSel, pathString);

  SEL trashItemAtURLSel = sel_registerName("trashItemAtURL:resultingItemURL:error:");
  BOOL deleteSuccessful = ((
      BOOL(*)(id, SEL, id, id, id))objc_msgSend)(fileManager, trashItemAtURLSel, nsurl, nil, nil);

  if (deleteSuccessful) {
    ret = 0;
  }
  else {
    *err_msg = "The Cocoa API call to delete file or directory failed";
  }

  SEL drainSel = sel_registerName("drain");
  ((void (*)(id, SEL))objc_msgSend)(pool, drainSel);

  return ret;
}
#  else
static int del_soft(const char *file, const char **err_msg)
{
  const char *args[5];
  const char *process_failed;

  char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
  char *xdg_session_desktop = getenv("XDG_SESSION_DESKTOP");

  if ((xdg_current_desktop != nullptr && STREQ(xdg_current_desktop, "KDE")) ||
      (xdg_session_desktop != nullptr && STREQ(xdg_session_desktop, "KDE")))
  {
    args[0] = "kioclient5";
    args[1] = "move";
    args[2] = file;
    args[3] = "trash:/";
    args[4] = nullptr;
    process_failed = "kioclient5 reported failure";
  }
  else {
    args[0] = "gio";
    args[1] = "trash";
    args[2] = file;
    args[3] = nullptr;
    process_failed = "gio reported failure";
  }

  int pid = fork();

  if (pid != 0) {
    /* Parent process */
    int wstatus = 0;

    waitpid(pid, &wstatus, 0);

    if (!WIFEXITED(wstatus)) {
      *err_msg =
          "Dune may not support moving files or directories to trash on your system.";
      return -1;
    }
    if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus)) {
      *err_msg = process_failed;
      return -1;
    }

    return 0;
  }

  execvp(args[0], (char **)args);

  *err_msg = "Forking process failed.";
  return -1; /* This should only be reached if execvp fails and stack isn't replaced. */
}
#  endif

FILE *lib_fopen(const char *filepath, const char *mode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return fopen(filepath, mode);
}

void *lib_gzopen(const char *filepath, const char *mode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return gzopen(filepath, mode);
}

int lib_open(const char *filepath, int oflag, int pmode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return open(filepath, oflag, pmode);
}

int lib_access(const char *filepath, int mode)
{
  lib_assert(!lib_path_is_rel(filepath));

  return access(filepath, mode);
}

int lib_del(const char *path, bool dir, bool recursive)
{
  lib_assert(!lib_path_is_rel(path));

  if (recursive) {
    return recursive_op(path, nullptr, nullptr, del_single_file, del_cb_post);
  }
  if (dir) {
    return rmdir(path);
  }
  return remove(path);
}

int lib_del_soft(const char *file, const char **err_msg)
{
  lib_assert(!lib_path_is_rel(file));

  return del_soft(file, err_msg);
}

/* Do the 2 paths denote the same file-sys ob? */
static bool check_the_same(const char *path_a, const char *path_b)
{
  struct stat st_a, st_b;

  if (lstat(path_a, &st_a)) {
    return false;
  }

  if (lstat(path_b, &st_b)) {
    return false;
  }

  return st_a.st_dev == st_b.st_dev && st_a.st_ino == st_b.st_ino;
}

/* Sets the mode and ownership of file to the values from st. */
static int set_permissions(const char *file, const struct stat *st)
{
  if (chown(file, st->st_uid, st->st_gid)) {
    perror("chown");
    return -1;
  }

  if (chmod(file, st->st_mode)) {
    perror("chmod");
    return -1;
  }

  return 0;
}

/* pre-recursive cb for copying op
 * creates a destination dir where all src content fill be copied to */
static int copy_cb_pre(const char *from, const char *to)
{
  struct stat st;

  if (check_the_same(from, to)) {
    fprintf(stderr, "%s: '%s' is the same as '%s'\n", __func__, from, to);
    return RecursiveOpCb_Err;
  }

  if (lstat(from, &st)) {
    perror("stat");
    return RecursiveOpCb_Err;
  }

  /* create a dir */
  if (mkdir(to, st.st_mode)) {
    perror("mkdir");
    return RecursiveOpCb_Err;
  }

  /* set proper owner and group on new directory */
  if (chown(to, st.st_uid, st.st_gid)) {
    perror("chown");
    return RecursiveOpCb_Err;
  }

  return RecursiveOpCb_OK;
}

static int copy_single_file(const char *from, const char *to)
{
  FILE *from_stream, *to_stream;
  struct stat st;
  char buf[4096];
  size_t len;

  if (check_the_same(from, to)) {
    fprintf(stderr, "%s: '%s' is the same as '%s'\n", __func__, from, to);
    return RecursiveOpCb_Err;
  }

  if (lstat(from, &st)) {
    perror("lstat");
    return RecursiveOpCb_Err;
  }

  if (S_ISLNK(st.st_mode)) {
    /* symbolic links should be copied in special way */
    char *link_buf;
    int need_free;
    int64_t link_len;

    /* get large enough buf to read link content */
    if ((st.st_size + 1) < sizeof(buf)) {
      link_buf = buf;
      need_free = 0;
    }
    else {
      link_buf = mem_cnew_array<char>(st.st_size + 2, "copy_single_file link_buf");
      need_free = 1;
    }

    link_len = readlink(from, link_buf, st.st_size + 1);
    if (link_len < 0) {
      perror("readlink");

      if (need_free) {
        mem_free(link_buf);
      }

      return RecursiveOpCb_Err;
    }

    link_buf[link_len] = '\0';

    if (symlink(link_buf, to)) {
      perror("symlink");
      if (need_free) {
        mem_free(link_buf);
      }
      return RecursiveOpCb_Err;
    }

    if (need_free) {
      mem_free(link_buf);
    }

    return RecursiveOpCb_OK;
  }

  if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) {
    /* copy special type of file */
    if (mknod(to, st.st_mode, st.st_rdev)) {
      perror("mknod");
      return RecursiveOpCb_Err;
    }

    if (set_permissions(to, &st)) {
      return RecursiveOpCb_Err;
    }

    return RecursiveOpCb_OK;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "Copying of this kind of files isn't supported yet\n");
    return RecursiveOpCb_Err;
  }

  from_stream = fopen(from, "rb");
  if (!from_stream) {
    perror("fopen");
    return RecursiveOpCb_Err;
  }

  to_stream = fopen(to, "wb");
  if (!to_stream) {
    perror("fopen");
    fclose(from_stream);
    return RecursiveOpCb_Err;
  }

  while ((len = fread(buf, 1, sizeof(buf), from_stream)) > 0) {
    fwrite(buf, 1, len, to_stream);
  }

  fclose(to_stream);
  fclose(from_stream);

  if (set_permissions(to, &st)) {
    return RecursiveOpCb_Err;
  }

  return RecursiveOpCb_OK;
}

static int move_cb_pre(const char *from, const char *to)
{
  int ret = rename(from, to);

  if (ret) {
    return copy_cb_pre(from, to);
  }

  return RecursiveOp_Cb_StopRecurs;
}

static int move_single_file(const char *from, const char *to)
{
  int ret = rename(from, to);

  if (ret) {
    return copy_single_file(from, to);
  }

  return RecursiveOp_Cb_OK;
}

int lib_path_move(const char *path_src, const char *path_dst)
{
  int ret = recursive_op(path_src, path_dst, move_cb_pre, move_single_file, nullptr);

  if (ret && ret != -1) {
    return recursive_op(
        path_src, nullptr, nullptr, del_single_file, del_cb_post);
  }

  return ret;
}

static const char *path_destination_ensure_filename(const char *path_src,
                                                    const char *path_dst,
                                                    char *buf,
                                                    size_t buf_size)
{
  if (lib_is_dir(path_dst)) {
    char *path_src_no_slash = lib_strdup(path_src);
    lib_path_slash_rstrip(path_src_no_slash);
    const char *filename_src = lib_path_basename(path_src_no_slash);
    if (filename_src != path_src_no_slash) {
      const size_t buf_size_needed = strlen(path_dst) + 1 + strlen(filename_src) + 1;
      char *path_dst_with_filename = (buf_size_needed <= buf_size) ?
                                         buf :
                                         mem_cnew_array<char>(buf_size_needed, __func__);
      lib_path_join(path_dst_with_filename, buf_size_needed, path_dst, filename_src);
      path_dst = path_dst_with_filename;
    }
    mem_free(path_src_no_slash);
  }
  return path_dst;
}

int lib_copy(const char *path_src, const char *path_dst)
{
  char path_dst_buf[FILE_MAX_STATIC_BUF];
  const char *path_dst_with_filename = path_destination_ensure_filename(
      path_src, path_dst, path_dst_buf, sizeof(path_dst_buf));
  int ret;

  ret = recursive_op(
      path_src, path_dst_with_filename, copy_cb_pre, copy_single_file, nullptr);

  if (!ELEM(path_dst_with_filename, path_dst_buf, path_dst)) {
    mem_free((void *)path_dst_with_filename);
  }

  return ret;
}

#  if 0
int lib_create_symlink(const char *path_src, const char *path_dst)
{
  return symlink(path_dst, path_src);
}
#  endif

#endif
