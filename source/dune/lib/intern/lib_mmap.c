#include "lib_mmap.h"
#include "lib_fileops.h"
#include "lib_list.h"
#include "mem_guardedalloc.h"

#include <string.h>

#ifndef WIN32
#  include <signal.h>
#  include <stdlib.h>
#  include <sys/mman.h> /* For mmap. */
#  include <unistd.h>   /* For read close. */
#else
#  include "lib_winstuff.h"
#  include <io.h> /* For open close read. */
#endif

struct lib_mmap_file {
  /* The address to which the file was mapped. */
  char *mem;

  /* The length of the file (and therefore the mapped rgn). */
  size_t length;

  /* Platform-specific handle for the mapping. */
  void *handle;

  /* Flag to indicate IO errors. Needs to be volatile since it's being set from
   * within the signal handler, which is not part of the normal ex flow. */
  volatile bool io_error;
};

#ifndef WIN32
/* When using mem-mapped files, any IO errors will result in a SIGBUS signal.
 * Therefore, we need to catch that signal and stop reading the file in question.
 * To do so, we keep a list of all current FileDatas that use mem-mapped files,
 * and if a SIGBUS is caught, we check if the failed address is inside one of the
 * mapped rgns.
 * If it is, we set a flag to indicate a failed read and remap the mem in
 * question to a zero-backed region in order to avoid additional signals.
 * The code that actually reads the mem area has to check whether the flag was
 * set after it's done reading.
 * If the error occurred outside of a mem-mapped region, we call the prev
 * handler if one was configured and abort the process otherwise. */

static struct error_handler_data {
  List open_mmaps;
  char configured;
  void (*next_handler)(int, siginfo_t *, void *);
} error_handler = {0};

static void sigbus_handler(int sig, siginfo_t *siginfo, void *ptr)
{
  /* We only handle SIGBUS here for now. */
  lib_assert(sig == SIGBUS);

  char *error_addr = (char *)siginfo->si_addr;
  /* Find the file that this error belongs to. */
  LIST_FOREACH (LinkData *, link, &error_handler.open_mmaps) {
    lib_mmap_file *file = link->data;

    /* Is the address where the error occurred in this file's mapped range? */
    if (error_addr >= file->mem && error_addr < file->mem + file->length) {
      file->io_error = true;

      /* Replace the mapped mem with zeroes. */
      const void *mapped_mem = mmap(
          file->mem, file->length, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
      if (mapped_mem == MAP_FAILED) {
        fprintf(stderr, "SIGBUS handler: Error replacing mapped file with zeros\n");
      }

      return;
    }
  }

  /* Fall back to other handler if there was one. */
  if (error_handler.next_handler) {
    error_handler.next_handler(sig, siginfo, ptr);
  }
  else {
    fprintf(stderr, "Unhandled SIGBUS caught\n");
    abort();
  }
}

/* Ensures that the error handler is set up and ready. */
static bool sigbus_handler_setup(void)
{
  if (!error_handler.configured) {
    struct sigaction newact = {0}, oldact = {0};

    newact.sa_sigaction = sigbus_handler;
    newact.sa_flags = SA_SIGINFO;

    if (sigaction(SIGBUS, &newact, &oldact)) {
      return false;
    }

    /* Remember the prev configured handler to fall back to it if the error
     * does not belong to any of the mapped files. */
    error_handler.next_handler = oldact.sa_sigaction;
    error_handler.configured = 1;
  }

  return true;
}

/* Adds a file to the list that the error handler checks. */
static void sigbus_handler_add(lib_mmap_file *file)
{
  lib_addtail(&error_handler.open_mmaps, lib_genericNode(file));
}

/* Removes a file from the list that the error handler checks. */
static void sigbus_handler_remove(lib_mmap_file *file)
{
  LinkData *link = lib_findptr(&error_handler.open_mmaps, file, offsetof(LinkData, data));
  lib_freelinkN(&error_handler.open_mmaps, link);
}
#endif

lib_mmap_file *lib_mmap_open(int fd)
{
  void *mem, *handle = NULL;
  const size_t length = lib_lseek(fd, 0, SEEK_END);
  if (UNLIKELY(length == (size_t)-1)) {
    return NULL;
  }

#ifndef WIN32
  /* Ensure that the SIGBUS handler is configured. */
  if (!sigbus_handler_setup()) {
    return NULL;
  }

  /* Map the given file to mem. */
  memory = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
  if (memory == MAP_FAILED) {
    return NULL;
  }
#else
  /* Convert the POSIX-style file descriptor to a Windows handle. */
  void *file_handle = (void *)_get_osfhandle(fd);
  /* Mem mapping on Windows is a two-step process - first we create a mapping,
   * then we create a view into that mapping.
   * In our case, one view that spans the entire file is enough. */
  handle = CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
  if (handle == NULL) {
    return NULL;
  }
  mem = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
  if (mem == NULL) {
    CloseHandle(handle);
    return NULL;
  }
#endif

  /* Now that the mapping was successful, allocate memory and set up the BLI_mmap_file. */
  lib_mmap_file *file = mem_calloc(sizeof(BLI_mmap_file), __func__);
  file->mem = mem;
  file->handle = handle;
  file->length = length;

#ifndef WIN32
  /* Register the file with the error handler. */
  sigbus_handler_add(file);
#endif

  return file;
}

bool lib_mmap_read(lib_mmap_file *file, void *dest, size_t offset, size_t length)
{
  /* If a prev read has already failed or we try to read past the end,
   * don't even attempt to read any further. */
  if (file->io_error || (offset + length > file->length)) {
    return false;
  }

#ifndef WIN32
  /* If an error occurs in this call, sigbus_handler will be called and will set
   * file->io_error to true. */
  memcpy(dest, file->mem + offset, length);
#else
  /* On Windows, we use exception handling to be notified of errors. */
  __try
  {
    memcpy(dest, file->mem + offset, length);
  }
  __except (GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER :
                                                            EXCEPTION_CONTINUE_SEARCH)
  {
    file->io_error = true;
    return false;
  }
#endif

  return !file->io_error;
}

void *lib_mmap_get_ptr(lib_mmap_file *file)
{
  return file->memory;
}

size_t lib_mmap_get_length(const lib_mmap_file *file)
{
  return file->length;
}

void lib_mmap_free(lib_mmap_file *file)
{
#ifndef WIN32
  munmap((void *)file->mem, file->length);
  sigbus_handler_remove(file);
#else
  UnmapViewOfFile(file->mem);
  CloseHandle(file->handle);
#endif

  mem_free(file);
}
